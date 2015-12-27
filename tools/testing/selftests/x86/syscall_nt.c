/*
 * syscall_nt.c - checks syscalls with NT set
 * Copyright (c) 2014-2015 Andrew Lutomirski
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
 * Some obscure user-space code requires the ability to make system calls
 * with FLAGS.NT set.  Make sure it works.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <asm/processor-flags.h>

#ifdef __x86_64__
# define WIDTH "q"
#else
# define WIDTH "l"
#endif

static unsigned long get_eflags(void)
{
	unsigned long eflags;
	asm volatile ("pushf" WIDTH "\n\tpop" WIDTH " %0" : "=rm" (eflags));
	return eflags;
}

static void set_eflags(unsigned long eflags)
{
	asm volatile ("push" WIDTH " %0\n\tpopf" WIDTH
		      : : "rm" (eflags) : "flags");
}

int main()
{
	printf("[RUN]\tSet NT and issue a syscall\n");
	set_eflags(get_eflags() | X86_EFLAGS_NT);
	syscall(SYS_getpid);
	if (get_eflags() & X86_EFLAGS_NT) {
		printf("[OK]\tThe syscall worked and NT is still set\n");
		return 0;
	} else {
		printf("[FAIL]\tThe syscall worked but NT was cleared\n");
		return 1;
	}
}
