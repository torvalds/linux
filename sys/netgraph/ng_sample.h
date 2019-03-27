/*
 * ng_sample.h
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
 * Author: Julian Elischer <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_sample.h,v 1.3 1999/01/20 00:22:14 archie Exp $
 */

#ifndef _NETGRAPH_NG_SAMPLE_H_
#define _NETGRAPH_NG_SAMPLE_H_

/* Node type name. This should be unique among all netgraph node types */
#define NG_XXX_NODE_TYPE	"sample"

/* Node type cookie. Should also be unique. This value MUST change whenever
   an incompatible change is made to this header file, to insure consistency.
   The de facto method for generating cookies is to take the output of the
   date command: date -u +'%s' */
#define NGM_XXX_COOKIE		915491374

/* Number of active DLCI's we can handle */
#define	XXX_NUM_DLCIS		16

/* Hook names */
#define NG_XXX_HOOK_DLCI_LEADIN	"dlci"
#define NG_XXX_HOOK_DOWNSTREAM	"downstream"
#define NG_XXX_HOOK_DEBUG	"debug"

/* Netgraph commands understood by this node type */
enum {
	NGM_XXX_SET_FLAG = 1,
	NGM_XXX_GET_STATUS,
};

/* This structure is returned by the NGM_XXX_GET_STATUS command */
struct ngxxxstat {
	u_int32_t   packets_in;		/* packets in from downstream */
	u_int32_t   packets_out;	/* packets out towards downstream */
};

/*
 * This is used to define the 'parse type' for a struct ngxxxstat, which
 * is bascially a description of how to convert a binary struct ngxxxstat
 * to an ASCII string and back.  See ng_parse.h for more info.
 *
 * This needs to be kept in sync with the above structure definition
 */
#define NG_XXX_STATS_TYPE_INFO	{				\
	  { "packets_in",	&ng_parse_uint32_type	},	\
	  { "packets_out",	&ng_parse_uint32_type	},	\
	  { NULL }						\
}

#endif /* _NETGRAPH_NG_SAMPLE_H_ */
