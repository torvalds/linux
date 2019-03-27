/*-
 * Copyright (c) 2013-2015, 2017 Mark R V Murray
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

#ifndef SYS_DEV_RANDOM_RANDOM_HARVESTQ_H_INCLUDED
#define	SYS_DEV_RANDOM_RANDOM_HARVESTQ_H_INCLUDED

#define	HARVESTSIZE	2	/* Max length in words of each harvested entropy unit */

/* These are used to queue harvested packets of entropy. The entropy
 * buffer size is pretty arbitrary.
 */
struct harvest_event {
	uint32_t	he_somecounter;		/* fast counter for clock jitter */
	uint32_t	he_entropy[HARVESTSIZE];/* some harvested entropy */
	uint8_t		he_size;		/* harvested entropy byte count */
	uint8_t		he_destination;		/* destination pool of this entropy */
	uint8_t		he_source;		/* origin of the entropy */
};

void read_rate_increment(u_int);

#define	RANDOM_HARVEST_INIT_LOCK(x)	mtx_init(&harvest_context.hc_mtx, "entropy harvest mutex", NULL, MTX_SPIN)
#define	RANDOM_HARVEST_LOCK(x)		mtx_lock_spin(&harvest_context.hc_mtx)
#define	RANDOM_HARVEST_UNLOCK(x)	mtx_unlock_spin(&harvest_context.hc_mtx)

#endif /* SYS_DEV_RANDOM_RANDOM_HARVESTQ_H_INCLUDED */
