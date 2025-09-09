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
 * mechanism found in UKL. We have the event loop where we park execution
 * contexts when they are not in use, the API for setiing up the system, and
 * an API for adding and removing event subscriptions.
 */

#ifndef UPCALL_H_
#define UPCALL_H_

#include <fcntl.h>

struct work_item {
	void *arg;
	void (*work_fn)(void *arg);
} __attribute__((packed));

#define UPIOGQCNT       0x00000001
#define UPIOSTSK        0x00000002

#define UPCALL_PCPU             0x00010000
#define UPCALL_PCACHE           0x00020000
#define UPCALL_SINGLE           0x00040000
#define UPCALL_MODEL_MASK       0x00070000
#define UPCALL_MASK             (O_CLOEXEC | UPCALL_MODEL_MASK)

#define UPCALL_ADD 1
#define UPCALL_DEL 2

typedef unsigned __poll_t;

/* Epoll event masks */
#define UPCALLIN         (__poll_t)0x00000001
#define UPCALLPRI        (__poll_t)0x00000002
#define UPCALLOUT        (__poll_t)0x00000004
#define UPCALLERR        (__poll_t)0x00000008
#define UPCALLHUP        (__poll_t)0x00000010
#define UPCALLNVAL       (__poll_t)0x00000020
#define UPCALLRDNORM     (__poll_t)0x00000040
#define UPCALLRDBAND     (__poll_t)0x00000080
#define UPCALLWRNORM     (__poll_t)0x00000100
#define UPCALLWRBAND     (__poll_t)0x00000200
#define UPCALLMSG        (__poll_t)0x00000400
#define UPCALLRDHUP      (__poll_t)0x00002000

#define UPCALL_VALID	(UPCALLIN|UPCALLPRI|UPCALLOUT|UPCALLERR|UPCALLHUP|UPCALLNVAL|\
			UPCALLRDNORM|UPCALLRDBAND|UPCALLWRNORM|UPCALLWRBAND|\
			UPCALLMSG|UPCALLRDHUP)

/**
 * Setup the event handler for execution
 *
 * This funciton must be called prior to registering any events of interest. 
 * @int concurrency_model The concurrency model to be used, should be from
 *     @enum concurrency_models.
 * @unsigned int thrd_cnt The number of threads, per event queue, to be created
 *     and ready for event handling.
 * @int *upfd A return param, allows for use of upcall fd by setup functions, nullable
 * @void (*setup_fn)(void *) An optional setup function that will be run on each
 *     worker thread before they start waiting for events, pass NULL for no setup
 * @return fd for upcall object on success, -ERRNO otherwise
 */
int init_event_handler(int flags, unsigned int thrd_cnt, int *upfd, void (*setup_fn)(void*), void *setup_arg);

/**
 * Register a handler for an event.
 *
 * Register a function (@work_fn) to be called with the argument @arg when the
 * specified @event occurs on the requested file descriptor @fd.
 * @int upcall_fd The file descriptor for the upcall object
 * @int fd The file descriptor to watch for events
 * @int event The event of interest, must be either UPCALL_READ or UPCALL_WRITE
 * @void (*work_fn)(void *arg) The function to be run when the specified event
 * 	occurs
 * @void *arg The argument to be passed to @work_fn, can be NULL.
 *
 * @Returns 0 on success, ERRNO on failure.
 */
int register_event(int upcall_fd, int fd, __poll_t event, void (*work_fn)(void *arg), void *arg);

int unregister_event(int upcall_fd, int fd, __poll_t event);

#endif
