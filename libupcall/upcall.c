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

/**
 *
 * This is a consolidation of the code needed to interact with the upcall
 * event mechanism. We have the event loop where we park execution
 * contexts when they are not in use, the API for setiing up the system, and
 * an API for adding and removing event subscriptions.
 */

#define _GNU_SOURCE

#include <sys/syscall.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "upcall.h"

#ifndef SYS_upcall_create
#define SYS_upcall_create 468
#endif

#ifndef SYS_upcall_submit
#define SYS_upcall_submit 469
#endif

#define EVTS 4

extern int upcall_create(int flags)
{
	return syscall(SYS_upcall_create, flags);
}

extern int upcall_submit(int upfd, int in_cnt, struct up_event *in,
		int out_cnt, struct up_event *out)
{
	return syscall(SYS_upcall_submit, upfd, in_cnt, in, out_cnt, out);
}

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
	work[work_cnt].buf = (void *)bufs;
	work[work_cnt].len = cnt;
	work_cnt++;
}

void return_buffer(void *buf, size_t len)
{
	buffers[buf_cnt].iov_base = buf;
	buffers[buf_cnt].iov_len = len;
	buf_cnt++;
}

void upcall_worker_setup(int upfd, size_t bufs, size_t buf_sz)
{
	work_max = 4 * EVTS;
	recv_cnt = EVTS;
	work_cnt = 0;

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
		buffers[buf_cnt].iov_len = buf_sz;
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
	work[work_cnt].fd = fd;
	work[work_cnt].type = UP_READ;
	work[work_cnt].work_fn = work_fn;
	work_cnt++;
}

void add_accept(int fd, void (*work_fn)(struct up_event *evt))
{
	if (work_cnt == work_max)
		expand_queue();
	
	work[work_cnt].fd = fd;
	work[work_cnt].type = UP_ACCEPT;
	work[work_cnt].work_fn = work_fn;
	work_cnt++;
}

void run_event_loop(int upfd, int continuous)
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

		buf_cnt = 0;
		work_cnt = 0;
		for (int i = 0; i < ret; i++) {
			receive[i].work_fn(&receive[i]);
		}
		memset(receive, 0, recv_cnt * sizeof(struct up_event));
	} while(continuous);
}

