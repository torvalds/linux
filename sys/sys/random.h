/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000-2015, 2017 Mark R. V. Murray
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
 * $FreeBSD$
 */

#ifndef	_SYS_RANDOM_H_
#define	_SYS_RANDOM_H_

#include <sys/types.h>

#ifdef _KERNEL

struct uio;

#if defined(DEV_RANDOM)
u_int read_random(void *, u_int);
int read_random_uio(struct uio *, bool);
#else
static __inline int
read_random_uio(void *a __unused, u_int b __unused)
{
	return (0);
}
static __inline u_int
read_random(void *a __unused, u_int b __unused)
{
	return (0);
}
#endif

/*
 * Note: if you add or remove members of random_entropy_source, remember to
 * also update the strings in the static array random_source_descr[] in
 * random_harvestq.c.
 */
enum random_entropy_source {
	RANDOM_START = 0,
	RANDOM_CACHED = 0,
	/* Environmental sources */
	RANDOM_ATTACH,
	RANDOM_KEYBOARD,
	RANDOM_MOUSE,
	RANDOM_NET_TUN,
	RANDOM_NET_ETHER,
	RANDOM_NET_NG,
	RANDOM_INTERRUPT,
	RANDOM_SWI,
	RANDOM_FS_ATIME,
	RANDOM_UMA,	/* Special!! UMA/SLAB Allocator */
	RANDOM_ENVIRONMENTAL_END = RANDOM_UMA,
	/* Fast hardware random-number sources from here on. */
	RANDOM_PURE_START,
	RANDOM_PURE_OCTEON = RANDOM_PURE_START,
	RANDOM_PURE_SAFE,
	RANDOM_PURE_GLXSB,
	RANDOM_PURE_UBSEC,
	RANDOM_PURE_HIFN,
	RANDOM_PURE_RDRAND,
	RANDOM_PURE_NEHEMIAH,
	RANDOM_PURE_RNDTEST,
	RANDOM_PURE_VIRTIO,
	RANDOM_PURE_BROADCOM,
	RANDOM_PURE_CCP,
	RANDOM_PURE_DARN,
	RANDOM_PURE_TPM,
	ENTROPYSOURCE
};
_Static_assert(ENTROPYSOURCE <= 32,
    "hardcoded assumption that values fit in a typical word-sized bitset");

#define RANDOM_LEGACY_BOOT_ENTROPY_MODULE	"/boot/entropy"
#define RANDOM_CACHED_BOOT_ENTROPY_MODULE	"boot_entropy_cache"
#define	RANDOM_CACHED_SKIP_START	256

#if defined(DEV_RANDOM)
extern u_int hc_source_mask;
void random_harvest_queue_(const void *, u_int, enum random_entropy_source);
void random_harvest_fast_(const void *, u_int);
void random_harvest_direct_(const void *, u_int, enum random_entropy_source);

static __inline void
random_harvest_queue(const void *entropy, u_int size, enum random_entropy_source origin)
{

	if (hc_source_mask & (1 << origin))
		random_harvest_queue_(entropy, size, origin);
}

static __inline void
random_harvest_fast(const void *entropy, u_int size, enum random_entropy_source origin)
{

	if (hc_source_mask & (1 << origin))
		random_harvest_fast_(entropy, size);
}

static __inline void
random_harvest_direct(const void *entropy, u_int size, enum random_entropy_source origin)
{

	if (hc_source_mask & (1 << origin))
		random_harvest_direct_(entropy, size, origin);
}

void random_harvest_register_source(enum random_entropy_source);
void random_harvest_deregister_source(enum random_entropy_source);
#else
#define random_harvest_queue(a, b, c) do {} while (0)
#define random_harvest_fast(a, b, c) do {} while (0)
#define random_harvest_direct(a, b, c) do {} while (0)
#define random_harvest_register_source(a) do {} while (0)
#define random_harvest_deregister_source(a) do {} while (0)
#endif

#if defined(RANDOM_ENABLE_UMA)
#define random_harvest_fast_uma(a, b, c)	random_harvest_fast(a, b, c)
#else /* !defined(RANDOM_ENABLE_UMA) */
#define random_harvest_fast_uma(a, b, c)	do {} while (0)
#endif /* defined(RANDOM_ENABLE_UMA) */

#if defined(RANDOM_ENABLE_ETHER)
#define random_harvest_queue_ether(a, b)	random_harvest_queue(a, b, RANDOM_NET_ETHER)
#else /* !defined(RANDOM_ENABLE_ETHER) */
#define random_harvest_queue_ether(a, b)	do {} while (0)
#endif /* defined(RANDOM_ENABLE_ETHER) */


#endif /* _KERNEL */

#define GRND_NONBLOCK	0x1
#define GRND_RANDOM	0x2

__BEGIN_DECLS
ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);
__END_DECLS

#endif /* _SYS_RANDOM_H_ */
