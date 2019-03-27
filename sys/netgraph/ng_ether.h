
/*
 * ng_ether.h
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
 * $Whistle: ng_ether.h,v 1.1 1999/02/02 03:17:22 julian Exp $
 */

#ifndef _NETGRAPH_NG_ETHER_H_
#define _NETGRAPH_NG_ETHER_H_

/* Node type name and magic cookie */
#define NG_ETHER_NODE_TYPE	"ether"
#define NGM_ETHER_COOKIE	917786906

/* Hook names */
#define NG_ETHER_HOOK_LOWER	"lower"		/* connection to raw device */
#define NG_ETHER_HOOK_UPPER	"upper"		/* connection to upper layers */
#define NG_ETHER_HOOK_DIVERT	"divert"	/* alias for lower */
#define NG_ETHER_HOOK_ORPHAN	"orphans"	/* like lower, unknowns only */

/* Netgraph control messages */
enum {
	NGM_ETHER_GET_IFNAME = 1,	/* get the interface name */
	NGM_ETHER_GET_IFINDEX,		/* get the interface global index # */
	NGM_ETHER_GET_ENADDR,		/* get Ethernet address */
	NGM_ETHER_SET_ENADDR,		/* set Ethernet address */
	NGM_ETHER_GET_PROMISC,		/* get node's promiscuous mode bit */
	NGM_ETHER_SET_PROMISC,		/* enable/disable promiscuous mode */
	NGM_ETHER_GET_AUTOSRC,		/* get source address override */
	NGM_ETHER_SET_AUTOSRC,		/* enable/disable src addr override */
	NGM_ETHER_ADD_MULTI,		/* add multicast membership */
	NGM_ETHER_DEL_MULTI,		/* delete multicast membership */
	NGM_ETHER_DETACH,		/* our way to be shut down */
};

#endif /* _NETGRAPH_NG_ETHER_H_ */
