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
#include <sched.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>

#include <sys/ioctl.h>

#include <linux/perf_event.h>
#include <arpa/inet.h>

#include <netinet/in.h>

#include "tcp_echo.h"

#include "../libupcall/upcall.h"

#define EVTS 2

#ifndef FLAGS
#define FLAGS UPCALL_PCACHE
#endif

#ifndef BUF_COUNT
#define BUF_COUNT 128
#endif

extern __thread struct worker_thread *me;
extern __thread struct buffer_cache *msg_cache;
extern __thread struct buffer_cache *conn_cache;
extern struct connection **conns;
extern struct worker_thread **threads;
extern size_t nr_cpus;
extern size_t msg_size;

extern struct addrinfo *res;

static pthread_cond_t setup_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t setup_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long setup_count;

static int upfd;

struct connection *new_conn(int fd);

void my_read(struct up_event *arg);

void my_accept(struct up_event *arg)
{
	int incoming = arg->result;
	struct connection *new;

	me->accept_count++;

	new = new_conn(incoming);
	if (!new)
		exit(1);

	conns[incoming] = new;
	new->state = WAITING;

	if (!new->buffer) {
		new->buffer = cache_alloc(msg_cache, me->index);
		if (!new->buffer) {
			perror("Malloc on read");
			exit(1);
		}
	}

	// Register our first read
	add_read(new->fd, my_read);
	add_accept(arg->fd, my_accept);
}

static void  my_write(int fd, uint8_t *buf, size_t len)
{
	size_t cursor = 0;
	ssize_t ret;

	do {
		if ((ret = write(fd, &(buf[cursor]), len - cursor)) <= 0) {
			if (ret == 0) {
				on_close(conns[fd]);
				return;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			perror("write to client");
			continue;
		}
		cursor += ret;
	} while (cursor < len);
}

void my_read(struct up_event *arg)
{
	struct connection *conn = conns[arg->fd];
	uint8_t *buf = (uint8_t*)arg->buf;
	 
	if (conn->fd < 0)
		return; 
	
	if (arg->result == 0) {
		on_close(conn);
		return;
	}

	conn->event_count++;

	if (arg->result == msg_size) {
		// The simplest case, we got everything in one go, echo it back
		// and issue another read

		conn->cursor = 0;
		conn->state = WRITING;
		my_write(conn->fd, buf, msg_size);
		conn->state = READING;
		goto out;
	}

	// We got a fragment, copy it into our buffer
	memcpy(&(conn->buffer[conn->cursor]), buf, arg->result);
	conn->cursor += arg->result;
	
	if (conn->cursor < msg_size) {
		// Need the rest of the message, issue another read but don't echo yet
		goto out;
	}

	// We have the whole message, echo it back
	my_write(conn->fd, conn->buffer, msg_size);

out:
	return_buffer(buf, arg->len);
	add_read(conn->fd, my_read);
}

static void *worker_setup(void *arg)
{
	int i = 1;

	me = calloc(1, sizeof(struct worker_thread));
	if (!me) {
		perror("calloc:");
		exit(1);
	}

	me->index = sched_getcpu();
	if (me->index < 0) {
		perror("sched_getcpu():");
		exit(1);
	}

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

	threads[me->index] = me;

	me->listen_sock = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK, res->ai_protocol);

	if (setsockopt(me->listen_sock, SOL_SOCKET, SO_REUSEPORT, &i, sizeof(i))) {
		perror("setsockopt():");
		exit(1);
	}

	if (setsockopt(me->listen_sock, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i))) {
		perror("setsockopt():");
		exit(1);
	}

	if (me->listen_sock < 0) {
		perror("socket():");
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

	upcall_worker_setup(upfd, BUF_COUNT, msg_size);

	add_accept(me->listen_sock, my_accept);

	setup_perf(me->perf_fds, me->perf_ids, me->index);

	ioctl(me->perf_fds[0], PERF_EVENT_IOC_RESET, 0);
	ioctl(me->perf_fds[0], PERF_EVENT_IOC_ENABLE, 0);


	pthread_mutex_lock(&setup_lock);
	setup_count++;
	while(setup_count)
		pthread_cond_wait(&setup_cond, &setup_lock);
	pthread_mutex_unlock(&setup_lock);


	run_event_loop(upfd, 1);

	return NULL;
}

void init_threads(uint64_t nr_cpus)
{
	pthread_attr_t attrs;
	cpu_set_t *event_cpu;
	int upcall_flags = FLAGS;
	int ret = 1;
	pthread_t dummy;

	pthread_attr_init(&attrs);
	pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_DETACHED);

	upfd = upcall_create(upcall_flags);
	if (upfd < 0) {
		printf("Event Handler setup failed\n");
		exit(-errno);
	}

	event_cpu = CPU_ALLOC(nr_cpus);
	for (unsigned long i = 0; i < nr_cpus; i++) {
		CPU_SET_S(i, CPU_ALLOC_SIZE(nr_cpus), event_cpu);
		if (pthread_attr_setaffinity_np(&attrs, CPU_ALLOC_SIZE(nr_cpus), event_cpu)) {
			ret = EINVAL;
			goto out_close;
		}

		if (pthread_create(&dummy, &attrs, worker_setup, NULL)) {
			ret = EINVAL;
			goto out_close;
		}

		CPU_CLR_S(i, CPU_ALLOC_SIZE(nr_cpus), event_cpu);
	}

	CPU_FREE(event_cpu);

	pthread_mutex_lock(&setup_lock);
	while (setup_count < nr_cpus) {
		pthread_mutex_unlock(&setup_lock);
		pthread_mutex_lock(&setup_lock);
	}
	setup_count = 0;
	pthread_mutex_unlock(&setup_lock);
	pthread_cond_broadcast(&setup_cond);

	return;

out_close:
	close(upfd);
	upfd = -1;
	exit(ret);
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
