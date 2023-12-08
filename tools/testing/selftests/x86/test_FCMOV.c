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

#define TEST(insn) \
long double __attribute__((noinline)) insn(long flags) \
{						\
	long double out;			\
	asm ("\n"				\
	"	push	%1""\n"			\
	"	popf""\n"			\
	"	fldpi""\n"			\
	"	fld1""\n"			\
	"	" #insn " %%st(1), %%st" "\n"	\
	"	ffree	%%st(1)" "\n"		\
	: "=t" (out)				\
	: "r" (flags)				\
	);					\
	return out;				\
}

TEST(fcmovb)
TEST(fcmove)
TEST(fcmovbe)
TEST(fcmovu)
TEST(fcmovnb)
TEST(fcmovne)
TEST(fcmovnbe)
TEST(fcmovnu)

enum {
	CF = 1 << 0,
	PF = 1 << 2,
	ZF = 1 << 6,
};

void sighandler(int sig)
{
	printf("[FAIL]\tGot signal %d, exiting\n", sig);
	exit(1);
}

int main(int argc, char **argv, char **envp)
{
	int err = 0;

	/* SIGILL triggers on 32-bit kernels w/o fcomi emulation
	 * when run with "no387 nofxsr". Other signals are caught
	 * just in case.
	 */
	signal(SIGILL, sighandler);
	signal(SIGFPE, sighandler);
	signal(SIGSEGV, sighandler);

	printf("[RUN]\tTesting fcmovCC instructions\n");
	/* If fcmovCC() returns 1.0, the move wasn't done */
	err |= !(fcmovb(0)   == 1.0); err |= !(fcmovnb(0)  != 1.0);
	err |= !(fcmove(0)   == 1.0); err |= !(fcmovne(0)  != 1.0);
	err |= !(fcmovbe(0)  == 1.0); err |= !(fcmovnbe(0) != 1.0);
	err |= !(fcmovu(0)   == 1.0); err |= !(fcmovnu(0)  != 1.0);

	err |= !(fcmovb(CF)  != 1.0); err |= !(fcmovnb(CF)  == 1.0);
	err |= !(fcmove(CF)  == 1.0); err |= !(fcmovne(CF)  != 1.0);
	err |= !(fcmovbe(CF) != 1.0); err |= !(fcmovnbe(CF) == 1.0);
	err |= !(fcmovu(CF)  == 1.0); err |= !(fcmovnu(CF)  != 1.0);

	err |= !(fcmovb(ZF)  == 1.0); err |= !(fcmovnb(ZF)  != 1.0);
	err |= !(fcmove(ZF)  != 1.0); err |= !(fcmovne(ZF)  == 1.0);
	err |= !(fcmovbe(ZF) != 1.0); err |= !(fcmovnbe(ZF) == 1.0);
	err |= !(fcmovu(ZF)  == 1.0); err |= !(fcmovnu(ZF)  != 1.0);

	err |= !(fcmovb(PF)  == 1.0); err |= !(fcmovnb(PF)  != 1.0);
	err |= !(fcmove(PF)  == 1.0); err |= !(fcmovne(PF)  != 1.0);
	err |= !(fcmovbe(PF) == 1.0); err |= !(fcmovnbe(PF) != 1.0);
	err |= !(fcmovu(PF)  != 1.0); err |= !(fcmovnu(PF)  == 1.0);

        if (!err)
                printf("[OK]\tfcmovCC\n");
	else
		printf("[FAIL]\tfcmovCC errors: %d\n", err);

	return err;
}
