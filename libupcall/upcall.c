/**
 * Upcall support library
 * Copyright (C) 2024 Eric B Munson
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#define _GNU_SOURCE

#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "upcall.h"

#ifndef SYS_upcall_create
#define SYS_upcall_create 468
#endif

#ifndef SYS_upcall_submit
#define SYS_upcall_submit 469
#endif

#define EVTS 4

/* ------------------------------------------------------------------ */
/* Low-level syscall wrappers — private to this file                   */
/* ------------------------------------------------------------------ */

static int upcall_create(int flags)
{
	return syscall(SYS_upcall_create, flags);
}

static int upcall_submit(int upfd, int in_cnt, struct up_event *in,
		int out_cnt, struct up_event *out)
{
	return syscall(SYS_upcall_submit, upfd, in_cnt, in, out_cnt, out);
}

/* ------------------------------------------------------------------ */
/* Per-worker thread-local submission state                            */
/* ------------------------------------------------------------------ */

static __thread struct up_event *work;
static __thread int work_cnt;
static __thread int work_max;
static __thread struct up_event *receive;
static __thread int recv_cnt;
static __thread struct iovec *buffers;
static __thread int buf_cnt;
static __thread int buf_max;

static void expand_queue(void)
{
	work_max += EVTS;
	work = realloc(work, work_max * sizeof(struct up_event));
	if (!work) {
		perror("OOM");
		exit(1);
	}
}

static void add_buffers(struct iovec *bufs, size_t cnt)
{
	if (work_cnt == work_max)
		expand_queue();

	memset(&work[work_cnt], 0, sizeof(struct up_event));
	work[work_cnt].type = UP_VEC;
	work[work_cnt].buf  = (void *)bufs;
	work[work_cnt].len  = cnt;
	work_cnt++;
}

void return_buffer(void *buf, size_t len)
{
	buffers[buf_cnt].iov_base = buf;
	buffers[buf_cnt].iov_len  = len;
	buf_cnt++;
}

static void upcall_worker_setup(int upfd, size_t bufs, size_t buf_sz)
{
	work_max  = 4 * EVTS;
	recv_cnt  = bufs;	/* match completions to pool size */
	work_cnt  = 0;

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

	buffers = calloc(bufs, sizeof(struct iovec));
	if (!buffers) {
		perror("OOM");
		exit(1);
	}
	buf_max = bufs;
	for (buf_cnt = 0; buf_cnt < buf_max; buf_cnt++) {
		buffers[buf_cnt].iov_len  = buf_sz;
		buffers[buf_cnt].iov_base = calloc(1, buf_sz);
		if (!buffers[buf_cnt].iov_base) {
			perror("OOM");
			exit(1);
		}
	}

	add_buffers(buffers, buf_cnt);
}

void add_read(int fd, void (*work_fn)(struct up_event *evt))
{
	if (work_cnt == work_max)
		expand_queue();

	memset(&work[work_cnt], 0, sizeof(struct up_event));
	work[work_cnt].fd      = fd;
	work[work_cnt].type    = UP_READ;
	work[work_cnt].work_fn = work_fn;
	work_cnt++;
}

void add_write(int fd, void *buf, size_t len,
	       void (*work_fn)(struct up_event *evt))
{
	if (work_cnt == work_max)
		expand_queue();

	memset(&work[work_cnt], 0, sizeof(struct up_event));
	work[work_cnt].fd      = fd;
	work[work_cnt].buf     = buf;
	work[work_cnt].len     = len;
	work[work_cnt].type    = UP_WRITE;
	work[work_cnt].work_fn = work_fn;
	work_cnt++;
}

void add_accept(int fd, void (*work_fn)(struct up_event *evt))
{
	if (work_cnt == work_max)
		expand_queue();

	work[work_cnt].fd      = fd;
	work[work_cnt].type    = UP_ACCEPT;
	work[work_cnt].work_fn = work_fn;
	work_cnt++;
}

static void run_event_loop(int upfd, int continuous)
{
	int ret;

	do {
		if (buf_cnt > 0)
			add_buffers(buffers, buf_cnt);
		ret = upcall_submit(upfd, work_cnt, work, recv_cnt, receive);
		if (ret < 0) {
			perror("upcall_submit failed");
			exit(1);
		}

		buf_cnt  = 0;
		work_cnt = 0;
		for (int i = 0; i < ret; i++)
			receive[i].work_fn(&receive[i]);
		memset(receive, 0, recv_cnt * sizeof(struct up_event));
	} while (continuous);
}

/* ------------------------------------------------------------------ */
/* Managed worker pool                                                 */
/* ------------------------------------------------------------------ */

static int    g_upfd       = -1;
static int    g_nr_workers = 0;
static size_t g_bufs;
static size_t g_buf_sz;
static void (*g_setup_fn)(int worker_id, int nr_workers);
static void (*g_loop_fn)(void);

static __thread int g_worker_id_tls = -1;

/* init barrier: main waits for all workers to complete setup_fn */
static pthread_mutex_t g_init_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_init_cond  = PTHREAD_COND_INITIALIZER;
static int             g_init_count = 0;

/* go barrier: workers wait for upcall_workers_go() */
static bool           g_go      = false;
static pthread_cond_t g_go_cond = PTHREAD_COND_INITIALIZER;

int upcall_nr_workers(void)
{
	return get_nprocs();
}

int upcall_worker_id(void)
{
	return g_worker_id_tls;
}

static void *upcall_worker_fn(void *arg)
{
	int id = (intptr_t)arg;

	g_worker_id_tls = id;

	upcall_worker_setup(g_upfd, g_bufs, g_buf_sz);

	if (g_setup_fn)
		g_setup_fn(id, g_nr_workers);

	/* Signal setup done, then wait for workers_go */
	pthread_mutex_lock(&g_init_lock);
	g_init_count++;
	pthread_cond_signal(&g_init_cond);
	while (!g_go)
		pthread_cond_wait(&g_go_cond, &g_init_lock);
	pthread_mutex_unlock(&g_init_lock);

	for (;;) {
		run_event_loop(g_upfd, false);
		if (g_loop_fn)
			g_loop_fn();
	}
	return NULL;
}

int upcall_init(size_t bufs, size_t buf_sz,
		void (*setup_fn)(int worker_id, int nr_workers),
		void (*loop_fn)(void))
{
	int nr = get_nprocs();
	pthread_attr_t attr;
	cpu_set_t cpuset;
	pthread_t tid;
	int ret;

	g_upfd = upcall_create(0);
	if (g_upfd < 0)
		return -errno;

	g_nr_workers = nr;
	g_bufs       = bufs;
	g_buf_sz     = buf_sz;
	g_setup_fn   = setup_fn;
	g_loop_fn    = loop_fn;

	ret = pthread_attr_init(&attr);
	if (ret)
		return -ret;
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	for (int i = 0; i < nr; i++) {
		CPU_ZERO(&cpuset);
		CPU_SET(i, &cpuset);
		ret = pthread_attr_setaffinity_np(&attr, sizeof(cpuset), &cpuset);
		if (ret) {
			pthread_attr_destroy(&attr);
			return -ret;
		}
		ret = pthread_create(&tid, &attr, upcall_worker_fn,
				     (void *)(intptr_t)i);
		if (ret) {
			pthread_attr_destroy(&attr);
			return -ret;
		}
	}
	pthread_attr_destroy(&attr);

	/* Wait until every worker has completed setup_fn */
	pthread_mutex_lock(&g_init_lock);
	while (g_init_count < nr)
		pthread_cond_wait(&g_init_cond, &g_init_lock);
	pthread_mutex_unlock(&g_init_lock);

	return 0;
}

void upcall_workers_go(void)
{
	pthread_mutex_lock(&g_init_lock);
	g_go = true;
	pthread_cond_broadcast(&g_go_cond);
	pthread_mutex_unlock(&g_init_lock);
}
