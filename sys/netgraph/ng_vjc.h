
/*
 * ng_vjc.h
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
 * $Whistle: ng_vjc.h,v 1.6 1999/01/25 02:40:22 archie Exp $
 */

#ifndef _NETGRAPH_NG_VJC_H_
#define _NETGRAPH_NG_VJC_H_

 /* Node type name and magic cookie */
#define NG_VJC_NODE_TYPE	"vjc"
#define NGM_VJC_COOKIE		868219210

 /* Hook names */
#define NG_VJC_HOOK_IP		"ip"		/* normal IP traffic */
#define NG_VJC_HOOK_VJCOMP	"vjcomp"	/* compressed TCP */
#define NG_VJC_HOOK_VJUNCOMP	"vjuncomp"	/* uncompressed TCP */
#define NG_VJC_HOOK_VJIP	"vjip"		/* uncompressed IP */

 /* Minimum and maximum number of compression channels */
#define NG_VJC_MIN_CHANNELS	4
#define NG_VJC_MAX_CHANNELS	16

 /* Configure struct */
struct ngm_vjc_config {
	u_char	enableComp;	/* Enable compression */
	u_char	enableDecomp;	/* Enable decompression */
	u_char	maxChannel;	/* Number of compression channels - 1 */
	u_char	compressCID;	/* OK to compress outgoing CID's */
};

/* Keep this in sync with the above structure definition */
#define NG_VJC_CONFIG_TYPE_INFO	{				\
	  { "enableComp",	&ng_parse_uint8_type	},	\
	  { "enableDecomp",	&ng_parse_uint8_type	},	\
	  { "maxChannel",	&ng_parse_uint8_type	},	\
	  { "compressCID",	&ng_parse_uint8_type	},	\
	  { NULL }						\
}

 /* Netgraph commands */
enum {
	NGM_VJC_SET_CONFIG,	/* Supply a struct ngm_vjc_config */
	NGM_VJC_GET_CONFIG,	/* Returns a struct ngm_vjc_config */
	NGM_VJC_GET_STATE,	/* Returns current struct slcompress */
	NGM_VJC_CLR_STATS,	/* Clears statistics counters */
	NGM_VJC_RECV_ERROR,	/* Indicate loss of incoming frame */
};

#endif /* _NETGRAPH_NG_VJC_H_ */
