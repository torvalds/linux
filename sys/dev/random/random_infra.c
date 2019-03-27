/*-
 * Copyright (c) 2015 Mark R V Murray
 * All rights reserved.
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/random.h>
#include <sys/sysctl.h>

#if defined(RANDOM_LOADABLE)
#include <sys/lock.h>
#include <sys/sx.h>
#endif

#include <dev/random/randomdev.h>

/* Set up the sysctl root node for the entropy device */
SYSCTL_NODE(_kern, OID_AUTO, random, CTLFLAG_RW, 0, "Cryptographically Secure Random Number Generator");

MALLOC_DEFINE(M_ENTROPY, "entropy", "Entropy harvesting buffers and data structures");

struct sources_head source_list = LIST_HEAD_INITIALIZER(source_list);

#if defined(RANDOM_LOADABLE)
struct random_algorithm *p_random_alg_context = NULL;
#else /* !defined(RANDOM_LOADABLE) */
struct random_algorithm *p_random_alg_context = &random_alg_context;
#endif /* defined(RANDOM_LOADABLE) */

#if defined(RANDOM_LOADABLE)

struct random_readers {
	int	(*read_random_uio)(struct uio *, bool);
	u_int	(*read_random)(void *, u_int);
} random_reader_context = {
	(int (*)(struct uio *, bool))nullop,
	(u_int (*)(void *, u_int))nullop,
};

struct sx randomdev_config_lock;

static void
random_infra_sysinit(void *dummy __unused)
{

	RANDOM_CONFIG_INIT_LOCK();
}
SYSINIT(random_device_h_init, SI_SUB_RANDOM, SI_ORDER_FIRST, random_infra_sysinit, NULL);

void
random_infra_init(int (*p_random_read_uio)(struct uio *, bool), u_int (*p_random_read)(void *, u_int))
{

	RANDOM_CONFIG_X_LOCK();
	random_reader_context.read_random_uio = p_random_read_uio;
	random_reader_context.read_random = p_random_read;
	RANDOM_CONFIG_X_UNLOCK();
}

void
random_infra_uninit(void)
{

	RANDOM_CONFIG_X_LOCK();
	random_reader_context.read_random_uio = (int (*)(struct uio *, bool))nullop;
	random_reader_context.read_random = (u_int (*)(void *, u_int))nullop;
	RANDOM_CONFIG_X_UNLOCK();
}

static void
random_infra_sysuninit(void *dummy __unused)
{

	RANDOM_CONFIG_DEINIT_LOCK();
}
SYSUNINIT(random_device_h_init, SI_SUB_RANDOM, SI_ORDER_FIRST, random_infra_sysuninit, NULL);

int
read_random_uio(struct uio *uio, bool nonblock)
{
	int retval;

	RANDOM_CONFIG_S_LOCK();
	retval = random_reader_context.read_random_uio(uio, nonblock);
	RANDOM_CONFIG_S_UNLOCK();
	return (retval);
}

u_int
read_random(void *buf, u_int len)
{
	u_int retval;

	RANDOM_CONFIG_S_LOCK();
	retval = random_reader_context.read_random(buf, len);
	RANDOM_CONFIG_S_UNLOCK();
	return (retval);
}

#endif /* defined(RANDOM_LOADABLE) */
