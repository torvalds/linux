/*
 * kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/kallsyms.h>
#include <linux/stacktrace.h>

void print_stack_trace(struct stack_trace *trace, int spaces)
{
	int i, j;

	for (i = 0; i < trace->nr_entries; i++) {
		unsigned long ip = trace->entries[i];

		for (j = 0; j < spaces + 1; j++)
			printk(" ");
		print_ip_sym(ip);
	}
}

