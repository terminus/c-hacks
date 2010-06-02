#ifndef __PROCESSOR_H
#define __PROCESSOR_H

/* This is mostly stolen from Linux kernel */

typedef struct { volatile int counter; } atomic_t;
#define atomic_read(v)          ((v)->counter)
#define atomic_set(v,i)         (((v)->counter) = (i))

#define LOCK_PREFIX "lock ; "

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__(
		LOCK_PREFIX "incl %0"
		:"=m" (v->counter)
		:"m" (v->counter));
}

#define rdtscll(val) do { \
	     unsigned int __a,__d; \
	     asm volatile("rdtsc" : "=a" (__a), "=d" (__d)); \
	     (val) = ((unsigned long)__a) | (((unsigned long)__d)<<32); \
} while(0)

#endif
