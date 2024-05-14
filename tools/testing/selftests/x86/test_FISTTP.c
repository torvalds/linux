// SPDX-License-Identifier: GPL-2.0
#undef _GNU_SOURCE
#define _GNU_SOURCE 1
#undef __USE_GNU
#define __USE_GNU 1
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <fenv.h>

unsigned long long res64 = -1;
unsigned int res32 = -1;
unsigned short res16 = -1;

int test(void)
{
	int ex;

	feclearexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	asm volatile ("\n"
	"	fld1""\n"
	"	fisttp	res16""\n"
	"	fld1""\n"
	"	fisttpl	res32""\n"
	"	fld1""\n"
	"	fisttpll res64""\n"
	: : : "memory"
	);
	if (res16 != 1 || res32 != 1 || res64 != 1) {
		printf("[BAD]\tfisttp 1\n");
		return 1;
	}
	ex = fetestexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	if (ex != 0) {
		printf("[BAD]\tfisttp 1: wrong exception state\n");
		return 1;
	}

	feclearexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	asm volatile ("\n"
	"	fldpi""\n"
	"	fisttp	res16""\n"
	"	fldpi""\n"
	"	fisttpl	res32""\n"
	"	fldpi""\n"
	"	fisttpll res64""\n"
	: : : "memory"
	);
	if (res16 != 3 || res32 != 3 || res64 != 3) {
		printf("[BAD]\tfisttp pi\n");
		return 1;
	}
	ex = fetestexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	if (ex != FE_INEXACT) {
		printf("[BAD]\tfisttp pi: wrong exception state\n");
		return 1;
	}

	feclearexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	asm volatile ("\n"
	"	fldpi""\n"
	"	fchs""\n"
	"	fisttp	res16""\n"
	"	fldpi""\n"
	"	fchs""\n"
	"	fisttpl	res32""\n"
	"	fldpi""\n"
	"	fchs""\n"
	"	fisttpll res64""\n"
	: : : "memory"
	);
	if (res16 != 0xfffd || res32 != 0xfffffffd || res64 != 0xfffffffffffffffdULL) {
		printf("[BAD]\tfisttp -pi\n");
		return 1;
	}
	ex = fetestexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	if (ex != FE_INEXACT) {
		printf("[BAD]\tfisttp -pi: wrong exception state\n");
		return 1;
	}

	feclearexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	asm volatile ("\n"
	"	fldln2""\n"
	"	fisttp	res16""\n"
	"	fldln2""\n"
	"	fisttpl	res32""\n"
	"	fldln2""\n"
	"	fisttpll res64""\n"
	: : : "memory"
	);
	/* Test truncation to zero (round-to-nearest would give 1 here) */
	if (res16 != 0 || res32 != 0 || res64 != 0) {
		printf("[BAD]\tfisttp ln2\n");
		return 1;
	}
	ex = fetestexcept(FE_DIVBYZERO|FE_INEXACT|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
	if (ex != FE_INEXACT) {
		printf("[BAD]\tfisttp ln2: wrong exception state\n");
		return 1;
	}

	return 0;
}

void sighandler(int sig)
{
	printf("[FAIL]\tGot signal %d, exiting\n", sig);
	exit(1);
}

int main(int argc, char **argv, char **envp)
{
	int err = 0;

	/* SIGILL triggers on 32-bit kernels w/o fisttp emulation
	 * when run with "no387 nofxsr". Other signals are caught
	 * just in case.
	 */
	signal(SIGILL, sighandler);
	signal(SIGFPE, sighandler);
	signal(SIGSEGV, sighandler);

	printf("[RUN]\tTesting fisttp instructions\n");
	err |= test();
	if (!err)
		printf("[OK]\tfisttp\n");
	else
		printf("[FAIL]\tfisttp errors: %d\n", err);

	return err;
}
