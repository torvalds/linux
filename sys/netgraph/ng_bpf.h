/*
 * ng_bpf.h
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
 * $Whistle: ng_bpf.h,v 1.3 1999/12/03 20:30:23 archie Exp $
 */

#ifndef _NETGRAPH_NG_BPF_H_
#define _NETGRAPH_NG_BPF_H_

/* Node type name and magic cookie */
#define NG_BPF_NODE_TYPE	"bpf"
#define NGM_BPF_COOKIE		944100792

/* Program structure for one hook */
struct ng_bpf_hookprog {
	char		thisHook[NG_HOOKSIZ];		/* name of hook */
	char		ifMatch[NG_HOOKSIZ];		/* match dest hook */
	char		ifNotMatch[NG_HOOKSIZ];		/* !match dest hook */
	int32_t		bpf_prog_len;			/* #insns in program */
	struct bpf_insn	bpf_prog[];			/* bpf program */
};

#define NG_BPF_HOOKPROG_SIZE(numInsn)	\
	(sizeof(struct ng_bpf_hookprog) + (numInsn) * sizeof(struct bpf_insn))

/* Keep this in sync with the above structure definition */
#define NG_BPF_HOOKPROG_TYPE_INFO(bptype)	{		\
	  { "thisHook",		&ng_parse_hookbuf_type	},	\
	  { "ifMatch",		&ng_parse_hookbuf_type	},	\
	  { "ifNotMatch",	&ng_parse_hookbuf_type	},	\
	  { "bpf_prog_len",	&ng_parse_int32_type	},	\
	  { "bpf_prog",		(bptype)		},	\
	  { NULL }						\
}

/* Statistics structure for one hook */
struct ng_bpf_hookstat {
	u_int64_t	recvFrames;
	u_int64_t	recvOctets;
	u_int64_t	recvMatchFrames;
	u_int64_t	recvMatchOctets;
	u_int64_t	xmitFrames;
	u_int64_t	xmitOctets;
};

/* Keep this in sync with the above structure definition */
#define NG_BPF_HOOKSTAT_TYPE_INFO	{			\
	  { "recvFrames",	&ng_parse_uint64_type	},	\
	  { "recvOctets",	&ng_parse_uint64_type	},	\
	  { "recvMatchFrames",	&ng_parse_uint64_type	},	\
	  { "recvMatchOctets",	&ng_parse_uint64_type	},	\
	  { "xmitFrames",	&ng_parse_uint64_type	},	\
	  { "xmitOctets",	&ng_parse_uint64_type	},	\
	  { NULL }						\
}

/* Netgraph commands */
enum {
	NGM_BPF_SET_PROGRAM = 1,	/* supply a struct ng_bpf_hookprog */
	NGM_BPF_GET_PROGRAM,		/* returns a struct ng_bpf_hookprog */
	NGM_BPF_GET_STATS,		/* supply name as char[NG_HOOKSIZ] */
	NGM_BPF_CLR_STATS,		/* supply name as char[NG_HOOKSIZ] */
	NGM_BPF_GETCLR_STATS,		/* supply name as char[NG_HOOKSIZ] */
};

#endif /* _NETGRAPH_NG_BPF_H_ */
