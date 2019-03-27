/*-
 * Copyright (c) 2018, Matthew Macy (mmacy@freebsd.org)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Neither the name of Matthew Macy nor the names of its
 *     contributors may be used to endorse or promote products derived from
 *     this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef __NET_IFLIB_PRIVATE_H_
#define __NET_IFLIB_PRIVATE_H_


#define	IFC_LEGACY		0x001
#define	IFC_QFLUSH		0x002
#define	IFC_MULTISEG		0x004
#define	IFC_SPARE1		0x008
#define	IFC_SC_ALLOCATED	0x010
#define	IFC_INIT_DONE		0x020
#define	IFC_PREFETCH		0x040
#define	IFC_DO_RESET		0x080
#define	IFC_DO_WATCHDOG		0x100
#define	IFC_CHECK_HUNG		0x200
#define	IFC_PSEUDO		0x400
#define	IFC_IN_DETACH		0x800

#define IFC_NETMAP_TX_IRQ	0x80000000

MALLOC_DECLARE(M_IFLIB);

#define IFLIB_MAX_TX_BYTES		(2*1024*1024)
#define IFLIB_MIN_TX_BYTES		(8*1024)
#define IFLIB_DEFAULT_TX_QDEPTH	2048


struct iflib_cloneattach_ctx {
	struct if_clone *cc_ifc;
	caddr_t cc_params;
	const char *cc_name;
	int cc_len;
};

extern driver_t iflib_pseudodriver;
int noop_attach(device_t dev);
int iflib_pseudo_detach(device_t dev);

int iflib_pseudo_register(device_t dev, if_shared_ctx_t sctx, if_ctx_t *ctxp,
	    struct iflib_cloneattach_ctx *clctx);

int iflib_pseudo_deregister(if_ctx_t ctx);

uint32_t iflib_get_flags(if_ctx_t ctx);
void iflib_set_detach(if_ctx_t ctx);
void iflib_stop(if_ctx_t ctx);

#endif
