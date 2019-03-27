/*
 * ng_ether_echo.c
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
 * Author: Julian Elisher <julian@freebsd.org>
 *
 * $FreeBSD$
 * $Whistle: ng_echo.c,v 1.13 1999/11/01 09:24:51 julian Exp $
 */

/*
 * Netgraph "ether_echo" node
 *
 * This node simply bounces data and messages back to whence they came.
 * However it swaps the source and destination ether fields.
 * No testing is done!
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <netgraph/ng_message.h>
#include <netgraph/netgraph.h>
#include <netgraph/ng_ether_echo.h>

#include <net/ethernet.h>

/* Netgraph methods */
static ng_constructor_t	ngee_cons;
static ng_rcvmsg_t	ngee_rcvmsg;
static ng_rcvdata_t	ngee_rcvdata;
static ng_disconnect_t	ngee_disconnect;

/* Netgraph type */
static struct ng_type typestruct = {
	.version =	NG_ABI_VERSION,
	.name =		NG_ETHER_ECHO_NODE_TYPE,
	.constructor =	ngee_cons,
	.rcvmsg =	ngee_rcvmsg,
	.rcvdata =	ngee_rcvdata,
	.disconnect =	ngee_disconnect,
};
NETGRAPH_INIT(ether_echo, &typestruct);

static int
ngee_cons(node_p node)
{
	return (0);
}

/*
 * Receive control message. We just bounce it back as a reply.
 */
static int
ngee_rcvmsg(node_p node, item_p item, hook_p lasthook)
{
	struct ng_mesg *msg;
	int error = 0;

	NGI_GET_MSG(item, msg);
	msg->header.flags |= NGF_RESP;
	NG_RESPOND_MSG(error, node, item, msg);
	return (error);
}

/*
 * Receive data
 */
static int
ngee_rcvdata(hook_p hook, item_p item)
{
	int error;
	struct mbuf *m;

	struct ether_header *eh;
	struct ether_addr tmpaddr;

	/* Make sure we have an entire header */
	NGI_GET_M(item, m);
	if (m->m_len < sizeof(*eh) ) {
		m = m_pullup(m, sizeof(*eh));
		if (m == NULL) {
			NG_FREE_ITEM(item);
			return(EINVAL);
		}
	}
	eh = mtod(m, struct ether_header *);

	/* swap the source and destination fields */
	bcopy(eh->ether_dhost, &tmpaddr, ETHER_ADDR_LEN);
	bcopy(eh->ether_shost, eh->ether_dhost, ETHER_ADDR_LEN);
	bcopy(&tmpaddr, eh->ether_shost, ETHER_ADDR_LEN);

	NG_FWD_NEW_DATA(error, item, hook, m);
	return (error);
}

/*
 * Removal of the last link destroys the nodeo
 */
static int
ngee_disconnect(hook_p hook)
{
	if ((NG_NODE_NUMHOOKS(NG_HOOK_NODE(hook)) == 0)
	&& (NG_NODE_IS_VALID(NG_HOOK_NODE(hook)))) {
		ng_rmnode_self(NG_HOOK_NODE(hook));
	}
	return (0);
}

