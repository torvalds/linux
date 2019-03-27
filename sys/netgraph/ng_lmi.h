/*
 * ng_lmi.h
 */

/*-
 * Copyright (c) 1996-1999 Whistle Communications, Inc.
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
 * $FreeBSD$
 * $Whistle: ng_lmi.h,v 1.9 1999/01/20 00:22:13 archie Exp $
 */

#ifndef _NETGRAPH_NG_LMI_H_
#define _NETGRAPH_NG_LMI_H_

/* Node type name and magic cookie */
#define NG_LMI_NODE_TYPE		"lmi"
#define NGM_LMI_COOKIE			867184133

/* My hook names */
#define NG_LMI_HOOK_DEBUG		"debug"
#define NG_LMI_HOOK_ANNEXA		"annexA"
#define NG_LMI_HOOK_ANNEXD		"annexD"
#define NG_LMI_HOOK_GROUPOF4		"group4"
#define NG_LMI_HOOK_AUTO0		"auto0"
#define NG_LMI_HOOK_AUTO1023		"auto1023"

/* Netgraph commands */
enum {
	NGM_LMI_GET_STATUS = 1,
};

#define NGM_LMI_STAT_ARYSIZE		(1024/8)

struct nglmistat {
	u_char  proto[12];	/* Active proto (same as hook name) */
	u_char  hook[12];	/* Active hook */
	u_char  fixed;		/* Set to fixed LMI mode */
	u_char  autod;		/* Currently auto-detecting */
	u_char  seen[NGM_LMI_STAT_ARYSIZE];	/* DLCIs ever seen */
	u_char  up[NGM_LMI_STAT_ARYSIZE];	/* DLCIs currently up */
};

/* Some default values */
#define NG_LMI_KEEPALIVE_RATE		10	/* seconds per keepalive */
#define NG_LMI_POLL_RATE		3	/* faster when AUTO polling */
#define NG_LMI_SEQ_PER_FULL		5	/* keepalives per full status */
#define NG_LMI_LMI_PRIORITY		64	/* priority for LMI data */

#endif /* _NETGRAPH_NG_LMI_H_ */
