/*
 * Measure tsc drift between processors/cores.
 *
 * Commandline: rdtsc number-of-processors
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <asm/msr.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include "processor.h"
#include "error.h"

/*
 * Since kernel 2.6.26: old headers might not have this define.
 */
#ifndef RUSAGE_THREAD
#define RUSAGE_THREAD 1
#endif

extern char *program_invocation_short_name;

int		spawn;
atomic_t	assembled;
atomic_t	command;
atomic_t	failed;

/* This is per-processor: Cache line aligned; we don't
 * want it to ping pong between processors.
 */
struct tsc {
	unsigned long ts;
} __attribute__((aligned(64)));

/* Every specify their tsc value */
struct tsc counter[32];

/* Every specify their tsc value: message passing version
 * (done in order of processor id) */
struct tsc counter_mp[32];

struct {
	struct rusage u, v;
	unsigned long int assemble;
	unsigned long stamp_counter;
	unsigned long stamp_counter_mp;
} rundata[32];

enum {
	SETUP = 0,
	BEFORE_STAMP,
	STAMP_COUNTER,
	STAMP_COUNTER_MP,
};

void thread_bind(int tid) {
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(tid, &set);

	__e_m(
	   sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0,
	   "(CPU=%d, errno=%d)", tid, errno);
}

/*
 * The idea behind this state machine is taken from Linux's
 * lockless stop-machine code.
 */
void *state_machine(void *args) {
	struct rusage u, v;
	int tid = (int)((unsigned long)args);
	unsigned long assemble, stamp_counter, stamp_counter_mp;
	int master = 0, slave;

	__e_m(tid > (spawn - 1), "How'd this happen?");

	if (tid == 0)
		master = 1;

	slave = !master;

	/* We want to see how badly we ended up spinning,
	 * waiting for threads on other CPUs to catch up.
	 *
	 * We'll use these variables as very unscientific
	 * counters.
	 */
	assemble = stamp_counter = stamp_counter_mp = 0;

	if (slave) {
		while (atomic_read(&command) != SETUP);
	}

	if (master) {
		atomic_set(&assembled, 0);
		atomic_set(&command, SETUP);
	}

	/*
	 * We are screwed if this fails: there will be gaps
	 * in ids. And the _mp version will hang forever.
	 */
	thread_bind(tid);

	/* Remember the page-faults and such. */
	__w(
	getrusage(RUSAGE_THREAD, &u) != 0,
	   	"(errno=%d)", errno);

	/* Done with setup. */
	atomic_inc(&assembled);

	/* wait for master to tell us to assemble */
	if (slave) {
		while (atomic_read(&command) != BEFORE_STAMP)
			assemble++;
	}

	/* give assemble command */
	if (master) {
		/* We want to kick-off all the slaves at
		 * the same time. Wait for them to assemble. */
		while (atomic_read(&assembled) != spawn);

		atomic_set(&assembled, 0);
		atomic_set(&command, BEFORE_STAMP);
	}

	/* assemble work */
	atomic_inc(&assembled);

	/* Everybody is at BEFORE_STAMP, raring to STAMP */

	/* give stamp command */
	if (master) {
		/* wait for slaves */
		while (atomic_read(&assembled) != spawn)
			assemble++;
		atomic_set(&assembled, 0);
		atomic_set(&command, STAMP_COUNTER);
	}

	/* wait for stamp command */
	if (slave) {
		while (atomic_read(&command) != STAMP_COUNTER)
			stamp_counter++;
	}

	/* stamp work */
	rdtscll(counter[tid].ts);
	atomic_inc(&assembled);

	/* give stamp_mp command */
	if (master) {
		/* wait for slaves */
		while (atomic_read(&assembled) != spawn)
			stamp_counter++;

		atomic_set(&assembled, 0);
		atomic_set(&command, STAMP_COUNTER_MP);
	}

	/* wait for stamp command */
	if (slave) {
		while (atomic_read(&command) != STAMP_COUNTER_MP)
			stamp_counter_mp++;
	}

	/*
	 * Do the work.
	 */
	while (atomic_read(&assembled) != tid);
	rdtscll(counter_mp[tid].ts);
	atomic_inc(&assembled);

	if (master) {
		while (atomic_read(&assembled) != spawn);
	}

	__w(
		getrusage(RUSAGE_THREAD, &v) != 0,
	   	"(errno=%d)", errno);
	
	rundata[tid].u = u;
	rundata[tid].v = v;
	rundata[tid].assemble = assemble;

	rundata[tid].stamp_counter = stamp_counter;
	rundata[tid].stamp_counter_mp = stamp_counter_mp;

	return NULL;
}

void dump_stats(int threads) {
	int i;
	unsigned long min_stamp;

	printf("\t\t%4s %11s %20s %20s %20s\n\t\t",
		"CPU", "Faults", "Assemble", "Stamp", "Stamp mp");
	for (i=0; i<4; i++) printf("_"); printf(" ");
	for (i=0; i<11; i++) printf("_"); printf(" ");
	for (i=0; i<20; i++) printf("_"); printf(" ");
	for (i=0; i<20; i++) printf("_"); printf(" ");
	for (i=0; i<20; i++) printf("_"); printf(" ");

	printf("\n");

	for (i=0; i<threads; i++)
		printf("\t\t%4d %5ld,%5ld %20ld %20ld %20ld\n", i, rundata[i].v.ru_minflt - rundata[i].u.ru_minflt,
							  rundata[i].v.ru_majflt - rundata[i].u.ru_majflt,
							  rundata[i].assemble, rundata[i].stamp_counter,
							  rundata[i].stamp_counter_mp);
	printf("\n\t\t%4s %32s %32s\n\t\t",
		"CPU", "Stamp", "Stamp-mp");
	for (i=0; i<4; i++) printf("_"); printf(" ");
	for (i=0; i<32; i++) printf("_"); printf(" ");
	for (i=0; i<32; i++) printf("_"); printf(" ");

	printf("\n");
	min_stamp = 0xffffffffffffffffUL;
	for (i=0; i<threads; i++) {
		if (counter[i].ts < min_stamp) {
			min_stamp = counter[i].ts;
		}
	}

	for (i=0; i<threads; i++)
		printf("\t\t%4d %32lu %32lu\n", i, counter[i].ts-min_stamp, counter_mp[i].ts-min_stamp);

	printf("\n");
}

int main(int argc, char **argv) {
	int i;
	
	__e(argc != 2);
	spawn = atoi(argv[1]);

	__w(spawn == 1, "Why do you want to run this?");

	/* Spawn a thread for each procesor */
	pthread_t thread[32];
	for (i=0; i < spawn; i++) {
		pthread_create(&thread[i], NULL, &state_machine, (void*)(long)i);
	}

	/*
	 * Assembly point
	 */
	for (i=0; i < spawn; i++) {
		pthread_join(thread[i], NULL);
	}

	dump_stats(spawn);

	return 0;
}
