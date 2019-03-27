/*-
 * Copyright (c) 2015 Netflix, Inc.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *__FBSDID("$FreeBSD$");
 *
 */
#ifndef _SYS_KERN_TESTFRWKT_H_
#define _SYS_KERN_TESTFRWKT_H_

#define TEST_NAME_LEN 32
#define TEST_OPTION_SPACE 256

struct kern_test {
	char name[TEST_NAME_LEN];
	int num_threads;	       	/* Fill in how many threads you want */
	int tot_threads_running;	/* For framework */
	uint8_t test_options[TEST_OPTION_SPACE];
};


typedef void (*kerntfunc)(struct kern_test *);

#ifdef _KERNEL
int kern_testframework_register(const char *name, kerntfunc);

int kern_testframework_deregister(const char *name);
#endif
#endif
