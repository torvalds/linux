/*
 * Copyright (C) 2016 Red Hat, Inc.
 * Author: Michael S. Tsirkin <mst@redhat.com>
 * This work is licensed under the terms of the GNU GPL, version 2.
 *
 * Common macros and functions for ring benchmarking.
 */
#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>

extern bool do_exit;

#if defined(__x86_64__) || defined(__i386__)
#include "x86intrin.h"

static inline void wait_cycles(unsigned long long cycles)
{
	unsigned long long t;

	t = __rdtsc();
	while (__rdtsc() - t < cycles) {}
}

#define VMEXIT_CYCLES 500
#define VMENTRY_CYCLES 500

#elif defined(__s390x__)
static inline void wait_cycles(unsigned long long cycles)
{
	asm volatile("0: brctg %0,0b" : : "d" (cycles));
}

/* tweak me */
#define VMEXIT_CYCLES 200
#define VMENTRY_CYCLES 200

#else
static inline void wait_cycles(unsigned long long cycles)
{
	_Exit(5);
}
#define VMEXIT_CYCLES 0
#define VMENTRY_CYCLES 0
#endif

static inline void vmexit(void)
{
	if (!do_exit)
		return;
	
	wait_cycles(VMEXIT_CYCLES);
}
static inline void vmentry(void)
{
	if (!do_exit)
		return;
	
	wait_cycles(VMENTRY_CYCLES);
}

/* implemented by ring */
void alloc_ring(void);
/* guest side */
int add_inbuf(unsigned, void *, void *);
void *get_buf(unsigned *, void **);
void disable_call();
bool used_empty();
bool enable_call();
void kick_available();
/* host side */
void disable_kick();
bool avail_empty();
bool enable_kick();
bool use_buf(unsigned *, void **);
void call_used();

/* implemented by main */
extern bool do_sleep;
void kick(void);
void wait_for_kick(void);
void call(void);
void wait_for_call(void);

extern unsigned ring_size;

/* Compiler barrier - similar to what Linux uses */
#define barrier() asm volatile("" ::: "memory")

/* Is there a portable way to do this? */
#if defined(__x86_64__) || defined(__i386__)
#define cpu_relax() asm ("rep; nop" ::: "memory")
#elif defined(__s390x__)
#define cpu_relax() barrier()
#else
#define cpu_relax() assert(0)
#endif

extern bool do_relax;

static inline void busy_wait(void)
{
	if (do_relax)
		cpu_relax();
	else
		/* prevent compiler from removing busy loops */
		barrier();
} 

/*
 * Not using __ATOMIC_SEQ_CST since gcc docs say they are only synchronized
 * with other __ATOMIC_SEQ_CST calls.
 */
#define smp_mb() __sync_synchronize()

/*
 * This abuses the atomic builtins for thread fences, and
 * adds a compiler barrier.
 */
#define smp_release() do { \
    barrier(); \
    __atomic_thread_fence(__ATOMIC_RELEASE); \
} while (0)

#define smp_acquire() do { \
    __atomic_thread_fence(__ATOMIC_ACQUIRE); \
    barrier(); \
} while (0)

#endif
