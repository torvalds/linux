/*
 * ng_mppc.h
 */

/*-
 * Copyright (c) 1996-2000 Whistle Communications, Inc.
 * All rights reserved.
 * 
 * Subject to the following obligations and disclaimer of warranty, use and
 * redistribution of this software, in source or object code forms, with or
 * without modifications are expressly permitted by Whistle Communications;
 * provided, however, that:
 * 1. Any and all reproductions of the source or object code must include the
 *    copyright notice above and the following disclaimer of warranties; and
 * 2. No rights are granted, in any manner or form, to use Whistle
 *    Communications, Inc. trademarks, including the mark "WHISTLE
 *    COMMUNICATIONS" on advertising, endorsements, or otherwise except as
 *    such appears in the above copyright notice or in the software.
 * 
 * THIS SOFTWARE IS BEING PROVIDED BY WHISTLE COMMUNICATIONS "AS IS", AND
 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, WHISTLE COMMUNICATIONS MAKES NO
 * REPRESENTATIONS OR WARRANTIES, EXPRESS OR IMPLIED, REGARDING THIS SOFTWARE,
 * INCLUDING WITHOUT LIMITATION, ANY AND ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
 * WHISTLE COMMUNICATIONS DOES NOT WARRANT, GUARANTEE, OR MAKE ANY
 * REPRESENTATIONS REGARDING THE USE OF, OR THE RESULTS OF THE USE OF THIS
 * SOFTWARE IN TERMS OF ITS CORRECTNESS, ACCURACY, RELIABILITY OR OTHERWISE.
 * IN NO EVENT SHALL WHISTLE COMMUNICATIONS BE LIABLE FOR ANY DAMAGES
 * RESULTING FROM OR ARISING OUT OF ANY USE OF THIS SOFTWARE, INCLUDING
 * WITHOUT LIMITATION, ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * PUNITIVE, OR CONSEQUENTIAL DAMAGES, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES, LOSS OF USE, DATA OR PROFITS, HOWEVER CAUSED AND UNDER ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF WHISTLE COMMUNICATIONS IS ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 * Author: Archie Cobbs <archie@freebsd.org>
 *
 * $Whistle: ng_mppc.h,v 1.3 2000/02/12 01:17:22 archie Exp $
 * $FreeBSD$
 */

#ifndef _NETGRAPH_NG_MPPC_H_
#define _NETGRAPH_NG_MPPC_H_

/* Node type name and magic cookie */
#define NG_MPPC_NODE_TYPE	"mppc"
#define NGM_MPPC_COOKIE		942886745

/* Hook names */
#define NG_MPPC_HOOK_COMP	"comp"		/* compression hook */
#define NG_MPPC_HOOK_DECOMP	"decomp"	/* decompression hook */

/* Length of MPPE key */
#define MPPE_KEY_LEN		16

/* Max expansion due to MPPC header and compression algorithm */
#define MPPC_MAX_BLOWUP(n)	((n) * 9 / 8 + 26)

/* MPPC/MPPE PPP negotiation bits */
#define MPPC_BIT		0x00000001	/* mppc compression bits */
#define MPPE_40			0x00000020	/* use 40 bit key */
#define MPPE_56			0x00000080	/* use 56 bit key */
#define MPPE_128		0x00000040	/* use 128 bit key */
#define MPPE_BITS		0x000000e0	/* mppe encryption bits */
#define MPPE_STATELESS		0x01000000	/* use stateless mode */
#define MPPC_VALID_BITS		0x010000e1	/* possibly valid bits */

/* Config struct (per-direction) */
struct ng_mppc_config {
	u_char		enable;			/* enable */
	u_int32_t	bits;			/* config bits */
	u_char		startkey[MPPE_KEY_LEN];	/* start key */
};

/* Netgraph commands */
enum {
	NGM_MPPC_CONFIG_COMP = 1,
	NGM_MPPC_CONFIG_DECOMP,
	NGM_MPPC_RESETREQ,			/* sent either way! */
};

#endif /* _NETGRAPH_NG_MPPC_H_ */

