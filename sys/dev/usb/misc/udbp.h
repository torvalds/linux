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
 * This file was derived from src/sys/netgraph/ng_sample.h, revision 1.1
 * written by Julian Elischer, Whistle Communications.
 *
 * $FreeBSD$
 */

#ifndef	_NETGRAPH_UDBP_H_
#define	_NETGRAPH_UDBP_H_

/* Node type name. This should be unique among all netgraph node types */
#define	NG_UDBP_NODE_TYPE	"udbp"

/* Node type cookie. Should also be unique. This value MUST change whenever
   an incompatible change is made to this header file, to insure consistency.
   The de facto method for generating cookies is to take the output of the
   date command: date -u +'%s' */
#define	NGM_UDBP_COOKIE		944609300


#define	NG_UDBP_HOOK_NAME	"data"

/* Netgraph commands understood by this node type */
enum {
	NGM_UDBP_SET_FLAG = 1,
	NGM_UDBP_GET_STATUS,
};

/* This structure is returned by the NGM_UDBP_GET_STATUS command */
struct ngudbpstat {
	uint32_t packets_in;		/* packets in from downstream */
	uint32_t packets_out;		/* packets out towards downstream */
};

/*
 * This is used to define the 'parse type' for a struct ngudbpstat, which
 * is bascially a description of how to convert a binary struct ngudbpstat
 * to an ASCII string and back.  See ng_parse.h for more info.
 *
 * This needs to be kept in sync with the above structure definition
 */
#define	NG_UDBP_STATS_TYPE_INFO	{					\
	  { "packets_in",	&ng_parse_int32_type	},		\
	  { "packets_out",	&ng_parse_int32_type	},		\
	  { NULL },							\
}

#endif					/* _NETGRAPH_UDBP_H_ */
