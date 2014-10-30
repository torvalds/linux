/*
 * thread-stack.h: Synthesize a thread's stack using call / return events
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __PERF_THREAD_STACK_H
#define __PERF_THREAD_STACK_H

#include <sys/types.h>

#include <linux/types.h>

struct thread;
struct ip_callchain;

int thread_stack__event(struct thread *thread, u32 flags, u64 from_ip,
			u64 to_ip, u16 insn_len, u64 trace_nr);
void thread_stack__sample(struct thread *thread, struct ip_callchain *chain,
			  size_t sz, u64 ip);
void thread_stack__free(struct thread *thread);

#endif
