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
#include <time.h>

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

static int upcall_create(size_t batch_sz, int flags)
{
	return syscall(SYS_upcall_create, batch_sz, flags);
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

/* ------------------------------------------------------------------ */
/* Completion-batch-size histogram (opt-in via UPCALL_BATCH_STATS env) */
/*                                                                     */
/* Each worker owns a cacheline-padded slot: slot[0] = # of submits,   */
/* slot[1 + n] = # of submits that returned n completions (n in        */
/* 0..batch_size).  Recording is a couple of writes to the worker's    */
/* own slot — no locks, no false sharing.  A monitor thread snapshots   */
/* all slots every UPCALL_BATCH_INTERVAL_MS and writes per-interval     */
/* deltas to the CSV named by $UPCALL_BATCH_STATS.  Disabled (NULL) when */
/* the env var is unset, in which case the hot path skips it entirely.  */
/* ------------------------------------------------------------------ */
#define UPCALL_CACHELINE 64
#define UPCALL_BATCH_INTERVAL_MS 100	/* default; override via env */

static uint64_t   *g_batch_stats;	/* nr_workers padded slots; NULL = off */
static size_t      g_batch_stride;	/* per-worker stride, in uint64 units */
static size_t      g_batch_hist_len;	/* histogram buckets = batch_size + 1 */
static const char *g_batch_path;	/* CSV output path (from env) */

static __thread uint64_t *my_batch_slot;	/* this worker's slot, or NULL */

static __thread int g_worker_id_tls = -1;

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
	if (buf_cnt == buf_max) {
		buf_max *= 2;
		buffers = realloc(buffers, buf_max * sizeof(struct iovec));
		if (!buffers) {
			perror("OOM growing buffer pool");
			exit(1);
		}
	}
	buffers[buf_cnt].iov_base = buf;
	buffers[buf_cnt].iov_len  = len;
	buf_cnt++;
}

static void upcall_worker_setup(int upfd, size_t batch_sz, size_t buf_sz)
{
	work_max  = batch_sz;
	recv_cnt  = batch_sz;	/* match completions to pool size */
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

	buffers = calloc(batch_sz, sizeof(struct iovec));
	if (!buffers) {
		perror("OOM");
		exit(1);
	}
	buf_max = batch_sz;
	for (buf_cnt = 0; buf_cnt < buf_max; buf_cnt++) {
		buffers[buf_cnt].iov_len  = buf_sz;
		buffers[buf_cnt].iov_base = calloc(1, buf_sz);
		if (!buffers[buf_cnt].iov_base) {
			perror("OOM");
			exit(1);
		}
	}

	/* buf_cnt stays at bufs here; reset to 0 so run_event_loop's
	 * "if (buf_cnt > 0) add_buffers(...)" doesn't submit a second
	 * UP_VEC pointing at the same pool, which would duplicate every
	 * buffer pointer and cause silent data corruption. */
	buf_cnt = 0;
	add_buffers(buffers, buf_max);

	/* Point this worker at its own cacheline-padded stats slot (if the
	 * histogram is enabled).  g_worker_id_tls is already set by the caller. */
	if (g_batch_stats)
		my_batch_slot = g_batch_stats +
				(size_t)g_worker_id_tls * g_batch_stride;
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

void add_close(int fd)
{
	if (work_cnt == work_max)
		expand_queue();

	work[work_cnt].fd 	   = fd;
	work[work_cnt].type    = UP_CLOSE;
	work[work_cnt].work_fn = NULL;
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

		if (my_batch_slot) {
			size_t b = (size_t)ret;

			if (b >= g_batch_hist_len)
				b = g_batch_hist_len - 1;
			my_batch_slot[0]++;
			my_batch_slot[1 + b]++;
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

/* init barrier: main waits for all workers to complete setup_fn */
static pthread_mutex_t g_init_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_init_cond  = PTHREAD_COND_INITIALIZER;
static int             g_init_count = 0;

/* go barrier: workers wait for upcall_workers_go() */
static bool           g_go      = false;
static pthread_cond_t g_go_cond = PTHREAD_COND_INITIALIZER;

size_t upcall_buf_sz(void)
{
	return g_buf_sz;
}

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

/*
 * Snapshot every worker's batch histogram once per interval and append the
 * per-interval deltas to $UPCALL_BATCH_STATS as CSV.  Runs unpinned and mostly
 * asleep; reads are plain aligned 64-bit loads of monotonically-incremented
 * counters, so a torn read is impossible on x86-64 and deltas stay coherent.
 */
static void *batch_monitor_fn(void *arg)
{
	long interval_ms = UPCALL_BATCH_INTERVAL_MS;
	const char *iv = getenv("UPCALL_BATCH_INTERVAL_MS");
	size_t n = (size_t)g_nr_workers;
	struct timespec start;
	uint64_t *prev;
	FILE *f;

	(void)arg;
	if (iv) {
		long v = atol(iv);
		if (v > 0)
			interval_ms = v;
	}

	f = fopen(g_batch_path, "w");
	if (!f) {
		perror("UPCALL_BATCH_STATS: fopen");
		return NULL;
	}
	prev = calloc(n * g_batch_stride, sizeof(uint64_t));
	if (!prev) {
		fclose(f);
		return NULL;
	}

	fprintf(f, "time_ms,worker,submits");
	for (size_t b = 0; b < g_batch_hist_len; b++)
		fprintf(f, ",h%zu", b);
	fprintf(f, "\n");

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		struct timespec req = {
			.tv_sec  = interval_ms / 1000,
			.tv_nsec = (interval_ms % 1000) * 1000000L,
		};
		struct timespec now;
		long tms;

		nanosleep(&req, NULL);
		clock_gettime(CLOCK_MONOTONIC, &now);
		tms = (now.tv_sec - start.tv_sec) * 1000 +
		      (now.tv_nsec - start.tv_nsec) / 1000000;

		for (size_t w = 0; w < n; w++) {
			uint64_t *cur = g_batch_stats + w * g_batch_stride;
			uint64_t *pv  = prev + w * g_batch_stride;

			fprintf(f, "%ld,%zu,%llu", tms, w,
				(unsigned long long)(cur[0] - pv[0]));
			for (size_t b = 0; b < g_batch_hist_len; b++)
				fprintf(f, ",%llu",
					(unsigned long long)(cur[1 + b] - pv[1 + b]));
			fprintf(f, "\n");
			for (size_t k = 0; k < g_batch_stride; k++)
				pv[k] = cur[k];
		}
		fflush(f);
	}
	return NULL;
}

int upcall_init(size_t batch_sz, size_t buf_sz,
		void (*setup_fn)(int worker_id, int nr_workers),
		void (*loop_fn)(void))
{
	int nr = get_nprocs();
	pthread_attr_t attr;
	cpu_set_t cpuset;
	pthread_t tid;
	int ret;

	g_upfd = upcall_create(batch_sz, 0);
	if (g_upfd < 0)
		return -errno;

	g_nr_workers = nr;
	g_bufs       = batch_sz;
	g_buf_sz     = buf_sz;
	g_setup_fn   = setup_fn;
	g_loop_fn    = loop_fn;

	/*
	 * Opt-in completion-batch histogram.  Allocate the padded per-worker
	 * slots (and start the monitor) before spawning workers, so each worker
	 * finds a valid slot in upcall_worker_setup().  Off entirely when
	 * $UPCALL_BATCH_STATS is unset.
	 */
	g_batch_path = getenv("UPCALL_BATCH_STATS");
	if (g_batch_path) {
		const size_t per_line = UPCALL_CACHELINE / sizeof(uint64_t);

		g_batch_hist_len = batch_sz + 1;
		/* [submits] + hist[0..batch_sz], rounded up to a cacheline. */
		g_batch_stride = ((1 + g_batch_hist_len) + (per_line - 1)) &
				 ~(per_line - 1);
		g_batch_stats = aligned_alloc(UPCALL_CACHELINE,
				(size_t)nr * g_batch_stride * sizeof(uint64_t));
		if (g_batch_stats) {
			pthread_t mon;

			memset(g_batch_stats, 0,
			       (size_t)nr * g_batch_stride * sizeof(uint64_t));
			if (pthread_create(&mon, NULL, batch_monitor_fn, NULL) == 0)
				pthread_detach(mon);
		} else {
			perror("UPCALL_BATCH_STATS: aligned_alloc");
			g_batch_path = NULL;
		}
	}

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
