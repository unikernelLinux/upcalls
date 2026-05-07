/*
 * Measure the cost of context switching between 2 threads using eventfd.
 * We will measure both on the same CPU, on different cores of same socket,
 * and cores on different sockets.
 */

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <pthread.h>

#define UKL_USER
#include "tsc_logger.h"
#undef UKL_USER

#define READ 0
#define WRITE 1

static __thread struct TscLog *log;

static int efd1;
static int efd2;

static void init_logger(size_t count)
{
	// We will store 1 wait and 1 wake up event per count
	size_t size = 2 * count * TscLogEntrySize(1);
	log = calloc(1, sizeof(struct TscLog) + size + L1_CACHE_BYTES);
	if (!log) {
		printf("OOM\n");
		exit(1);
	}

	if ((uint64_t)log & (L1_CACHE_BYTES - 1)) {
		log = (struct TscLog*)(((uint64_t)log + L1_CACHE_BYTES) &
				~((uint64_t)L1_CACHE_BYTES - 1));
	}

	log->hdr.info.cur = &(log->entries[0]);
	log->hdr.info.end = (void *)((uint64_t)log->hdr.info.cur + size);
	log->hdr.info.overflow = 0;
	log->hdr.info.valperentry = 1;
}

static inline void add_read(void)
{
	tsclog_1(log, READ);
}

static inline void add_write(void)
{
	tsclog_1(log, WRITE);
}

static void *sleeper(void *arg)
{
	uint64_t count = (uint64_t)arg;
	uint64_t val;

	init_logger(count);

	for (uint64_t i = 0; i < count; i++) {
		read(efd1, &val, sizeof(uint64_t));
		add_read();

		val = 1;
		add_write();
		write(efd2, &val, sizeof(uint64_t));
	}

	return log;
}

static void *waker(void *arg)
{
	uint64_t count = (uint64_t)arg;
	uint64_t val;

	init_logger(count);

	for (uint64_t i = 0; i < count; i++) {
		val = 1;
		add_write();
		write(efd1, &val, sizeof(uint64_t));
		
		read(efd2, &val, sizeof(uint64_t));
		add_read();
	}

	return log;
}

static void run_one(long slp_cpu, long wke_cpu, void *slp_log, void *wke_log, uint64_t count)
{
	pthread_attr_t slp_attr, wke_attr;
	pthread_t wkr, slpr;
	size_t nr_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	cpu_set_t *sleep, *wake;

	efd1 = eventfd(0, 0);
	efd2 = eventfd(0, 0);

	pthread_attr_init(&slp_attr);
	sleep = CPU_ALLOC(nr_cpus);
	CPU_SET_S(slp_cpu, CPU_ALLOC_SIZE(nr_cpus), sleep);
	if (pthread_attr_setaffinity_np(&slp_attr, CPU_ALLOC_SIZE(nr_cpus), sleep)) {
		perror("set affinity");
		exit(1);
	}

	pthread_create(&slpr, &slp_attr, sleeper, (void *)count);

	pthread_attr_init(&wke_attr);
	wake = CPU_ALLOC(nr_cpus);
	CPU_SET_S(wke_cpu, CPU_ALLOC_SIZE(nr_cpus), wake);
	if (pthread_attr_setaffinity_np(&wke_attr, CPU_ALLOC_SIZE(nr_cpus), wake)) {
		perror("set affinity");
		exit(1);
	}

	pthread_create(&wkr, &wke_attr, waker, (void *)count);

	pthread_join(slpr, slp_log);
	pthread_join(wkr, wke_log);

	CPU_FREE(wake);
	CPU_FREE(sleep);
	close(efd1);
	close(efd2);
}

static void write_times(int fd, struct TscLog *slp, struct TscLog *wkr, float tsc_khz)
{
	size_t latency;
	double nsec;
	struct TscLogEntry *sc, *se, *wc, *we;

	sc = (struct TscLogEntry *)&(slp->entries[0]);
	wc = (struct TscLogEntry *)&(wkr->entries[0]);
	se = slp->hdr.info.cur;
	we = wkr->hdr.info.cur;

	dprintf(fd, "LATENCY_USEC\n");

	while (sc != se && wc != we) {
		if (sc->values[0] == READ) {
			latency = sc->tsc - wc->tsc;
		} else {
			latency = wc->tsc - sc->tsc;
		}

		nsec = ((double)(latency * 1000)) / (double)tsc_khz;

		dprintf(fd, "%f\n", nsec);

		sc = (struct TscLogEntry *)((uint8_t*)sc + TscLogEntrySize(1));
		wc = (struct TscLogEntry *)((uint8_t*)wc + TscLogEntrySize(1));
	}
}

int main(int argc, char **argv)
{
	struct TscLog *logs[6];
	long base, core, socket;
	uint64_t count;
	float tsc_khz;
	int fd;

	if (argc < 6) {
		printf("Usage: %s <base cpu> <same socket> <different socket> <count> <tsc_mhz>\n", argv[0]);
		return -1;
	}

	my_pid.pid = getpid();

	base = strtol(argv[1], NULL, 10);
	core = strtol(argv[2], NULL, 10);
	socket = strtol(argv[3], NULL, 10);
	count = strtoul(argv[4], NULL, 10);
	tsc_khz = strtof(argv[5], NULL);

	tsc_khz *= 1000;

	run_one(base, base, (void *)&logs[0], (void *)&logs[1], count);
	run_one(base, core, (void *)&logs[2], (void *)&logs[3], count);
	run_one(base, socket, (void *)&logs[4], (void *)&logs[5], count);

	fd = creat("same_core.csv", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	write_times(fd, logs[0], logs[1], tsc_khz);
	close(fd);
	fd = creat("same_socket.csv", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	write_times(fd, logs[2], logs[3], tsc_khz);
	close(fd);
	fd = creat("cross_socket.csv", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	write_times(fd, logs[4], logs[5], tsc_khz);
	close(fd);
}
