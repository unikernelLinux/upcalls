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

extern int upcall_create(int flags)
{
	return syscall(SYS_upcall_create, flags);
}

extern int upcall_submit(int upfd, int in_cnt, struct up_event *in,
		int out_cnt, struct up_event *out)
{
	return syscall(SYS_upcall_submit, upfd, in_cnt, in, out_cnt, out);
}

struct worker {
	int dying;
	int upfd;
	pthread_t me;
	void (*setup_fn)(void *);
	void * setup_arg;
};

static unsigned long setup_count;

static struct worker *workers;

static pthread_cond_t setup_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t setup_lock = PTHREAD_MUTEX_INITIALIZER;

static void wait_for_setup()
{
	pthread_mutex_lock(&setup_lock);
	setup_count++;
	while (setup_count)
		pthread_cond_wait(&setup_cond, &setup_lock);
	pthread_mutex_unlock(&setup_lock);
}

extern void parse_clusters(size_t num_cpus, int queues, cpu_set_t **clusters)
{
	int lead_cpu[queues];
	int next_cluster = 0;
	char cpu[256] = {0};
	char line[1024];
	int entry, fd;
	char *comma;

	memset(lead_cpu, -1, queues * sizeof(int));

	for (size_t i = 0; i < num_cpus; i++) {
		snprintf(cpu, 256, "/sys/devices/system/cpu/cpu%ld/topology/cluster_cpus_list", i);
		fd = open(cpu, O_RDONLY);
		if (fd < 0) {
			perror("Failed to open cluster_cpus_list for reading\n");
			fprintf(stderr, "Tried to read '%s'\n", cpu);
			return;
		}

		memset(line, 0, 1024);
		if (read(fd, line, 1023) < 0) {
			perror("Failed to read\n");
			close(fd);
			return;
		}
		close(fd);

		entry = strtol(line, NULL, 10);
		if (entry == i) {
			if (next_cluster >= queues) {
				// Yikes
				fprintf(stderr, "We think there should be more clusters than exist\n");
				return;
			}

			lead_cpu[next_cluster] = i;
			clusters[next_cluster] = CPU_ALLOC(num_cpus);
			CPU_ZERO_S(CPU_ALLOC_SIZE(num_cpus), clusters[next_cluster]);
			CPU_SET_S(i, CPU_ALLOC_SIZE(num_cpus), clusters[next_cluster]);
			next_cluster++;
		} else {
			int my_cluster;
			for (my_cluster = 0; my_cluster < next_cluster; my_cluster++) {
				if (lead_cpu[my_cluster] == entry)
					break;
			}

			if (my_cluster >= next_cluster) {
				fprintf(stderr, "Failed to find a cluster for %ld\n", i);
				exit(1);
			}

			CPU_SET_S(i, CPU_ALLOC_SIZE(num_cpus), clusters[my_cluster]);
		}
	}
}
