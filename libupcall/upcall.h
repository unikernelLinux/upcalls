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

struct up_event {
	int32_t		fd;
	int32_t		result;
	void		*buf;
	uint64_t	len;
	void		(*work_fn)(void *arg);
} __attribute__((packed));

#define UPIOGQCNT       0x00000001

#define UPCALL_PCPU             0x00010000
#define UPCALL_PCACHE           0x00020000
#define UPCALL_SINGLE           0x00040000
#define UPCALL_MODEL_MASK       0x00070000
#define UPCALL_MASK             (O_CLOEXEC | UPCALL_MODEL_MASK)

typedef unsigned __poll_t;

int upcall_create(int flags);

int upcall_submit(int upfd, int in_cnt, struct up_event *in,
		int out_cnt, struct up_event *out);

#endif
