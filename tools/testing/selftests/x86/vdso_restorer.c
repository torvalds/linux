/*
 * vdso_restorer.c - tests vDSO-based signal restore
 * Copyright (c) 2015 Andrew Lutomirski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * This makes sure that sa_restorer == NULL keeps working on 32-bit
 * configurations.  Modern glibc doesn't use it under any circumstances,
 * so it's easy to overlook breakage.
 *
 * 64-bit userspace has never supported sa_restorer == NULL, so this is
 * 32-bit only.
 */

#define _GNU_SOURCE

#include <err.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <syscall.h>
#include <sys/syscall.h>

/* Open-code this -- the headers are too messy to easily use them. */
struct real_sigaction {
	void *handler;
	unsigned long flags;
	void *restorer;
	unsigned int mask[2];
};

static volatile sig_atomic_t handler_called;

static void handler_with_siginfo(int sig, siginfo_t *info, void *ctx_void)
{
	handler_called = 1;
}

static void handler_without_siginfo(int sig)
{
	handler_called = 1;
}

int main()
{
	int nerrs = 0;
	struct real_sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.handler = handler_with_siginfo;
	sa.flags = SA_SIGINFO;
	sa.restorer = NULL;	/* request kernel-provided restorer */

	if (syscall(SYS_rt_sigaction, SIGUSR1, &sa, NULL, 8) != 0)
		err(1, "raw rt_sigaction syscall");

	raise(SIGUSR1);

	if (handler_called) {
		printf("[OK]\tSA_SIGINFO handler returned successfully\n");
	} else {
		printf("[FAIL]\tSA_SIGINFO handler was not called\n");
		nerrs++;
	}

	sa.flags = 0;
	sa.handler = handler_without_siginfo;
	if (syscall(SYS_sigaction, SIGUSR1, &sa, 0) != 0)
		err(1, "raw sigaction syscall");
	handler_called = 0;

	raise(SIGUSR1);

	if (handler_called) {
		printf("[OK]\t!SA_SIGINFO handler returned successfully\n");
	} else {
		printf("[FAIL]\t!SA_SIGINFO handler was not called\n");
		nerrs++;
	}
}
