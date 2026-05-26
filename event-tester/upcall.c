/*
 * This is the upcall implementation of the tcp_echo server.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>

#include <sys/ioctl.h>

#include <linux/perf_event.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "tcp_echo.h"
#include "../libupcall/upcall.h"

extern __thread struct worker_thread *me;
extern __thread struct buffer_cache *msg_cache;
extern __thread struct buffer_cache *conn_cache;
extern struct connection **conns;
extern struct worker_thread **threads;
extern size_t nr_cpus;
extern size_t msg_size;
extern struct addrinfo *res;

struct connection *new_conn(int fd);

void my_read(struct up_event *arg);

void my_accept(struct up_event *arg)
{
	int incoming = arg->result;
	struct connection *new;

	if (incoming < 0) {
		printf("Error on accept %d\n", incoming);
		goto out;
	}

	me->accept_count++;

	new = new_conn(incoming);
	if (!new)
		exit(1);

	conns[incoming] = new;
	new->state = WAITING;

	if (!new->buffer) {
		new->buffer = cache_alloc(msg_cache, me->index);
		if (!new->buffer) {
			perror("Malloc on accept");
			exit(1);
		}
	}

	add_read(new->fd, my_read);
out:
	add_accept(arg->fd, my_accept);
}

static void my_write(struct up_event *arg)
{
	struct connection *conn = conns[arg->fd];

	if (arg->result == 0) {
		on_close(conn);
		return;
	}

	if (arg->result < 0) {
		printf("Error on write %d\n", arg->result);
		return;
	}

	if (arg->result + conn->cursor < msg_size) {
		conn->cursor += arg->result;
		add_write(conn->fd, (conn->buffer + conn->cursor),
			  msg_size - conn->cursor, my_write);
		return;
	}

	conn->cursor = 0;

	add_read(conn->fd, my_read);
}

void my_read(struct up_event *arg)
{
	struct connection *conn = conns[arg->fd];
	uint8_t *buf = (uint8_t *)arg->buf;

	if (conn->fd < 0)
		return;

	if (arg->result == 0) {
		on_close(conn);
		return;
	}

	conn->event_count++;

	if (arg->result < 0) {
		printf("Error on read %d\n", arg->result);
		return;
	}

	memcpy(&(conn->buffer[conn->cursor]), buf, arg->result);
	conn->cursor += arg->result;
	return_buffer(buf, arg->len);

	if (conn->cursor < msg_size) {
		add_read(conn->fd, my_read);
		return;
	}

	conn->cursor = 0;

	add_write(conn->fd, conn->buffer, msg_size, my_write);
}

/*
 * Per-worker setup callback: called by libupcall from each worker thread
 * after pool initialisation.  Registers the worker's listen socket so
 * it is ready before upcall_workers_go() opens the event loop.
 */
static void upcall_echo_setup(int worker_id, int nr_workers)
{
	int i = 1;

	me = calloc(1, sizeof(struct worker_thread));
	if (!me) {
		perror("calloc:");
		exit(1);
	}

	me->index = worker_id;

	msg_cache = init_cache(msg_size, 1024, me->index);
	if (!msg_cache) {
		perror("OOM");
		exit(1);
	}

	conn_cache = init_cache(sizeof(struct connection), 1024, me->index);
	if (!conn_cache) {
		perror("OOM");
		exit(1);
	}

	threads[worker_id] = me;

	me->listen_sock = socket(res->ai_family,
				 res->ai_socktype | SOCK_NONBLOCK,
				 res->ai_protocol);
	if (me->listen_sock < 0) {
		perror("socket():");
		exit(1);
	}

	if (setsockopt(me->listen_sock, SOL_SOCKET, SO_REUSEPORT, &i, sizeof(i))) {
		perror("setsockopt SO_REUSEPORT:");
		exit(1);
	}

	if (setsockopt(me->listen_sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i))) {
		perror("setsockopt SO_REUSEADDR:");
		exit(1);
	}

	if (bind(me->listen_sock, res->ai_addr, res->ai_addrlen)) {
		perror("bind():");
		exit(1);
	}

	if (listen(me->listen_sock, BACKLOG)) {
		perror("listen():");
		exit(1);
	}

	add_accept(me->listen_sock, my_accept);

	setup_perf(me->perf_fds, me->perf_ids, me->index);

	ioctl(me->perf_fds[0], PERF_EVENT_IOC_RESET, 0);
	ioctl(me->perf_fds[0], PERF_EVENT_IOC_ENABLE, 0);
}

void on_close(void *arg)
{
	int closed_fd;
	struct connection *conn = (struct connection *)arg;

	closed_fd = conn->fd;
	conn->fd = -1;

	if (closed_fd >= 0) {
		conns[closed_fd] = NULL;
		conn->state = CLOSING;
		close(closed_fd);

		pthread_mutex_destroy(&conn->lock);

		cache_free(msg_cache, conn->buffer, me->index);
		cache_free(conn_cache, conn, me->index);
		me->conn_count++;
	}
}

void init_threads(uint64_t ignored)
{
	if (upcall_init(BUF_COUNT, msg_size, upcall_echo_setup, NULL)) {
		fprintf(stderr, "upcall_init failed\n");
		exit(1);
	}
}

void workers_go(void)
{
	upcall_workers_go();
}
