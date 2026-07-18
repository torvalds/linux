// SPDX-License-Identifier: LGPL-2.1
#define _GNU_SOURCE
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "rseq.h"

int main(int argc, char **argv)
{
	if (__rseq_register_current_thread(true, false))
		return -1;
	return 0;
}
