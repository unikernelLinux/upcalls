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

#define EVTS 10

#ifndef FLAGS
#define FLAGS UPCALL_PCPU
#endif

extern __thread struct worker_thread *me;
extern __thread struct buffer_cache *msg_cache;
extern __thread struct buffer_cache *conn_cache;
static __thread struct up_event *work;
static __thread int work_cnt;
static __thread int work_max;
static __thread struct up_event *receive;
static __thread int recv_cnt;
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

static void add_read(int fd, void *buf, uint64_t len);
static void add_accept(int fd);

void on_accept(void *v)
{
	struct up_event *arg = (struct up_event *)v;
	int listen_sock = arg->fd;
	int incoming;
	struct connection *new;

	while (1) {
		if ((incoming = accept4(listen_sock, NULL, NULL, SOCK_NONBLOCK)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// We exhausted the incoming queue
				add_accept(listen_sock);
				return;
			}
			perror("accept4:");
			// It is likely that we exhausted the number of file descriptors here, dump some per thread stats
			for (size_t i = 0; i < nr_cpus; i++) {
				printf("Thread %ld accept count %ld close count %ld\n", i, threads[i]->accept_count, threads[i]->conn_count);
			}

			exit(1);
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
				perror("Malloc on read");
				exit(1);
			}
		}

		// Register our first read
		add_read(new->fd, new->buffer, msg_size);
	}
}

void on_read(void *v)
{
	struct up_event *arg = (struct up_event *)v;
	struct connection *conn = conns[arg->fd];
	ssize_t ret;
	size_t cursor;
	 
	if (conn->fd < 0)
		return; 
	
	conn->event_count++;
		
	cursor = conn->cursor;

	if (arg->result == 0) {
		on_close(conn);
		return;
	}

	cursor += arg->result;
	
	if (cursor < msg_size) {
		// Need the rest of the message
		add_read(conn->fd, &conn[cursor], msg_size - cursor);
		return;
	}

	conn->cursor = cursor = 0;
	conn->state = WRITING;

	do {
		if ((ret = write(conn->fd, &(conn->buffer[cursor]), msg_size - cursor)) <= 0) {
			if (ret == 0) {
				on_close(conn);
				return;
			}

			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			perror("write to client");
			continue;
		}
		cursor += ret;
	} while (cursor < msg_size);

	conn->state = READING;

	add_read(conn->fd, conn->buffer, msg_size);
}

static void expand_queues(void)
{
	work_max += EVTS;
	work = realloc(work, work_max * sizeof(struct up_event));
	if (!work) {
		perror("OOM");
		exit(1);
	}

	recv_cnt += 2 * EVTS;
	receive = realloc(receive, recv_cnt * sizeof(struct up_event));
	if (!receive) {
		perror("OOM");
		exit(1);
	}
}

static void add_accept(int fd)
{
	if (work_cnt == work_max)
		expand_queues();

	memset(&work[work_cnt], 0, sizeof(struct up_event));

	work[work_cnt].fd = fd;
	work[work_cnt].buf = (void*)0xDEADBEEF;
	work[work_cnt].work_fn = on_accept;
	work_cnt++;
}

static void add_read(int fd, void *buf, uint64_t len)
{
	if (work_cnt == work_max)
		expand_queues();

	work[work_cnt].fd = fd;
	work[work_cnt].result = 0;
	work[work_cnt].buf = buf;
	work[work_cnt].len = len;
	work[work_cnt].work_fn = on_read;
	work_cnt++;
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

	work_max = EVTS;
	recv_cnt = 2 * EVTS;

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

	work = calloc(work_max, sizeof(struct up_event));
	if (!work) {
		perror("OOM");
		exit(1);
	}

	receive = calloc(recv_cnt, sizeof(struct up_event));
	if (!receive) {
		perror("OOM");
		exit(1);
	}

	add_accept(me->listen_sock);

	setup_perf(me->perf_fds, me->perf_ids, me->index);

	ioctl(me->perf_fds[0], PERF_EVENT_IOC_RESET, 0);
	ioctl(me->perf_fds[0], PERF_EVENT_IOC_ENABLE, 0);

	pthread_mutex_lock(&setup_lock);
	setup_count++;
	while(setup_count)
		pthread_cond_wait(&setup_cond, &setup_lock);
	pthread_mutex_unlock(&setup_lock);

	// Work loop
	while (1) {
		int ret;
		ret = upcall_submit(upfd, work_cnt, work, recv_cnt, receive);
		if (ret < 0) {
			perror("upcall_submit failed");
			exit(ret);
		}


		work_cnt = 0;

		for (int i = 0; i < ret; i++) {
			if (receive[i].work_fn)
				receive[i].work_fn(&receive[i]);
		}
		memset(receive, 0, recv_cnt * sizeof(struct up_event));
	}

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
