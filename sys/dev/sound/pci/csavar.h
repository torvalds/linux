/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 Seigo Tanimura
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _CSA_VAR_H
#define _CSA_VAR_H
struct csa_card {
	u_int16_t subvendor, subdevice;
	char *name;
	void *amp;
	void *amp_init;
	int (*active)(int);
	int inv_eapd;
};

/* Resources. */
struct csa_res {
	int io_rid; /* io rid */
	struct resource *io; /* io */
	int mem_rid; /* memory rid */
	struct resource *mem; /* memory */
	int irq_rid; /* irq rid */
	struct resource *irq; /* irq */
};
typedef struct csa_res csa_res;

/* State of the bridge. */
struct csa_bridgeinfo {
	u_int32_t hisr; /* The value of HISR on this interrupt. */
	struct csa_card *card;
};

void csa_clearserialfifos(csa_res *resp);

/* Common functions for csa. */
struct csa_card *csa_findsubcard(device_t dev);
int csa_readcodec(csa_res *resp, u_long offset, u_int32_t *data);
int csa_writecodec(csa_res *resp, u_long offset, u_int32_t data);

u_int32_t csa_readio(csa_res *resp, u_long offset);
void csa_writeio(csa_res *resp, u_long offset, u_int32_t data);
u_int32_t csa_readmem(csa_res *resp, u_long offset);
void csa_writemem(csa_res *resp, u_long offset, u_int32_t data);

void csa_resetdsp(csa_res *resp);
#endif /* _CSA_VAR_H */
