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

#ifndef UPCALL_H_
#define UPCALL_H_

#include <stdint.h>
#include <sys/uio.h>

typedef enum {
	UP_READ,	/* Requesting a read of the fd */
	UP_WRITE,	/* Requesting a write of the fd */
	UP_ACCEPT,	/* Requesting an accept4 on the fd (will imply SOCK_NONBLOCK) */
	UP_VEC,		/* Give the struct iovec array at buf with len items to the kernel */
	NR_ACTIONS
} up_action_t;

struct up_event {
	int32_t		fd;
	int32_t		result;
	void		*buf;
	uint64_t	len;
	void		(*work_fn)(struct up_event *arg);
	union {
		up_action_t	type;
		uint64_t	pad;
	};
} __attribute__((packed));

#define UPCALL_MASK             (O_CLOEXEC)

typedef unsigned __poll_t;

/*
 * Returns the number of online CPUs — equal to the number of worker threads
 * that upcall_init() will spawn.
 */
int upcall_nr_workers(void);

/*
 * Spawn one worker thread per online CPU, each pinned to its CPU.
 * Internally calls upcall_worker_setup(bufs, buf_sz) per worker, then
 * invokes setup_fn (if non-NULL) so each worker can register its initial
 * events (add_accept, add_read, etc.).  Blocks until every worker has
 * completed setup_fn.  Workers then wait for upcall_workers_go().
 *
 * loop_fn (may be NULL) is called from each worker between event batches.
 *
 * Returns 0 on success, -errno on failure.
 */
int upcall_init(size_t bufs, size_t buf_sz,
		void (*setup_fn)(int worker_id, int nr_workers),
		void (*loop_fn)(void));

/*
 * Release all workers into the event loop.  Must be called after
 * upcall_init() returns.  The window between upcall_init() and
 * upcall_workers_go() is the correct place for the main thread to
 * perform any final setup (e.g. printing a "server ready" message).
 */
void upcall_workers_go(void);

/*
 * Returns the 0-indexed worker ID of the calling thread.
 * Returns -1 if called from outside a libupcall worker thread.
 */
int upcall_worker_id(void);

/* --- Callback-facing API ---
 * Safe to call from setup_fn, loop_fn, and event callbacks.
 * Calls queue work into the calling worker's submission batch; the batch
 * is submitted to the kernel on the next run_event_loop round-trip.
 */
void return_buffer(void *buf, size_t len);
void add_read(int fd, void (*work_fn)(struct up_event *evt));
void add_accept(int fd, void (*work_fn)(struct up_event *evt));
void add_write(int fd, void *buf, size_t len,
	       void (*work_fn)(struct up_event *evt));

#endif
