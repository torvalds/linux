/*-
 * Copyright (c) 2018 Justin Hibbits
 * Copyright (c) 2013 The FreeBSD Foundation
 * Copyright (c) 2013 David E. O'Brien <obrien@NUXI.org>
 * Copyright (c) 2012 Konstantin Belousov <kib@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/random.h>
#include <sys/systm.h>

#include <machine/cpu.h>
#include <machine/md_var.h>

#include <dev/random/randomdev.h>

/*
 * Power ISA 3.0 adds a "darn" instruction (Deliver A Random Number).  The RNG
 * backing this instruction conforms to NIST SP800-90B and SP800-90C at the
 * point of hardware design, and provides a minimum of 0.5 bits of entropy per
 * bit.
 */

#define	RETRY_COUNT	10

static u_int random_darn_read(void *, u_int);

static struct random_source random_darn = {
	.rs_ident = "PowerISA DARN random number generator",
	.rs_source = RANDOM_PURE_DARN,
	.rs_read = random_darn_read
};

static inline int
darn_rng_store(u_long *buf)
{
	u_long rndval;
	int retry;

	for (retry = RETRY_COUNT; retry > 0; --retry) {
		/* "DARN %rN, 1" instruction */
		/*
		 * Arguments for DARN: rN and "L", where "L" can be one of:
		 * 0 - 32-bit conditional random number
		 * 1 - Conditional random number (conditioned to remove bias)
		 * 2 - Raw random number	 (unprocessed, may include bias)
		 * 3 - Reserved
		 */
	    	__asm __volatile(".long 0x7c0105e6 | (%0 << 21)" :
	    	    "+r"(rndval));
		if (rndval != ~0)
			break;
	}

	*buf = rndval;
	return (retry);
}

/* It is required that buf length is a multiple of sizeof(u_long). */
static u_int
random_darn_read(void *buf, u_int c)
{
	u_long *b, rndval;
	u_int count;

	KASSERT(c % sizeof(*b) == 0, ("partial read %d", c));
	b = buf;
	for (count = c; count > 0; count -= sizeof(*b)) {
		if (darn_rng_store(&rndval) == 0)
			break;
		*b++ = rndval;
	}
	return (c - count);
}

static int
darn_modevent(module_t mod, int type, void *unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		if (cpu_features2 & PPC_FEATURE2_DARN) {
			random_source_register(&random_darn);
			printf("random: fast provider: \"%s\"\n", random_darn.rs_ident);
		}
		break;

	case MOD_UNLOAD:
		if (cpu_features2 & PPC_FEATURE2_DARN)
			random_source_deregister(&random_darn);
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}

	return (error);
}

DEV_MODULE(darn, darn_modevent, NULL);
MODULE_VERSION(darn, 1);
MODULE_DEPEND(darn, random_device, 1, 1, 1);
