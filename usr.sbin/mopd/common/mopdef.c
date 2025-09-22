/*	$OpenBSD: mopdef.c,v 1.8 2009/10/27 23:59:52 deraadt Exp $ */

/*
 * Copyright (c) 1995 Mats O Jansson.  All rights reserved.
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
 */

#include <sys/types.h>

#define MOPDEF_SURPESS_EXTERN
#include "common/mopdef.h"

u_char dl_mcst[6] = MOP_DL_MULTICAST;	/* Dump/Load Multicast         */
u_char rc_mcst[6] = MOP_RC_MULTICAST;	/* Remote Console Multicast    */
u_char dl_802_proto[5] = MOP_K_PROTO_802_DL; /* MOP Dump/Load 802.2      */
u_char rc_802_proto[5] = MOP_K_PROTO_802_RC; /* MOP Remote Console 802.2 */
u_char lp_802_proto[5] = MOP_K_PROTO_802_LP; /* Loopback 802.2           */

int
mopdef_dummy(void)
{
	/* Just to keep them as variables */
	return (dl_mcst[0] - rc_mcst[0] - lp_802_proto[1] - rc_802_proto[1] -
	    lp_802_proto[1]);
}
