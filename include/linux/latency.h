/*
 * latency.h: Explicit system-wide latency-expectation infrastructure
 *
 * (C) Copyright 2006 Intel Corporation
 * Author: Arjan van de Ven <arjan@linux.intel.com>
 *
 */

#ifndef _INCLUDE_GUARD_LATENCY_H_
#define _INCLUDE_GUARD_LATENCY_H_

#include <linux/notifier.h>

void set_acceptable_latency(char *identifier, int usecs);
void modify_acceptable_latency(char *identifier, int usecs);
void remove_acceptable_latency(char *identifier);
void synchronize_acceptable_latency(void);
int system_latency_constraint(void);

int register_latency_notifier(struct notifier_block * nb);
int unregister_latency_notifier(struct notifier_block * nb);

#define INFINITE_LATENCY 1000000

#endif
