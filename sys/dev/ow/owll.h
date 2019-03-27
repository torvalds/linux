/*-
 * Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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

#ifndef DEV_OW_OWLL_H
#define DEV_OW_OWLL_H 1

/*
 * Generalized parameters for the mode of operation in the bus. All units
 * are in nanoseconds, and assume that all timings are < 4s.
 * See owll_if.m for timings, and refer to AN937 for details.
 */
struct ow_timing 
{
	uint32_t	t_slot;		/* Slot time */
	uint32_t	t_low0;		/* Time low for a 0 bit. */
	uint32_t	t_low1;		/* Time low for a 1 bit. */
	uint32_t	t_lowr;		/* Time slave holds line down per bit */
	uint32_t	t_release;	/* Time after t_rdv to float high */
	uint32_t	t_rec;		/* After sample before M low */
	uint32_t	t_rdv;		/* Time to poll the bit after M low */
	uint32_t	t_rstl;		/* Time M low on reset */
	uint32_t	t_rsth;		/* Time M high on reset */
	uint32_t	t_pdl;		/* Time S low on reset */
	uint32_t	t_pdh;		/* Time R high after M low on reset */
};

#include "owll_if.h"

#endif /* DEV_OW_OWLL_H */
