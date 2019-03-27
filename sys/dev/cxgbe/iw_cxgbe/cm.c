/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013, 2016 Chelsio, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#ifdef TCP_OFFLOAD
#include <sys/types.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sockio.h>
#include <sys/taskqueue.h>
#include <netinet/in.h>
#include <net/route.h>

#include <netinet/in_systm.h>
#include <netinet/in_pcb.h>
#include <netinet6/in6_pcb.h>
#include <netinet/ip.h>
#include <netinet/in_fib.h>
#include <netinet6/in6_fib.h>
#include <netinet6/scope6_var.h>
#include <netinet/ip_var.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcpip.h>

#include <netinet/toecore.h>

struct sge_iq;
struct rss_header;
struct cpl_set_tcb_rpl;
#include <linux/types.h>
#include "offload.h"
#include "tom/t4_tom.h"

#define TOEPCB(so)  ((struct toepcb *)(so_sototcpcb((so))->t_toe))

#include "iw_cxgbe.h"
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/if_vlan.h>
#include <net/netevent.h>
#include <rdma/rdma_cm.h>

static spinlock_t req_lock;
static TAILQ_HEAD(c4iw_ep_list, c4iw_ep_common) req_list;
static struct work_struct c4iw_task;
static struct workqueue_struct *c4iw_taskq;
static LIST_HEAD(err_cqe_list);
static spinlock_t err_cqe_lock;
static LIST_HEAD(listen_port_list);
static DEFINE_MUTEX(listen_port_mutex);

static void process_req(struct work_struct *ctx);
static void start_ep_timer(struct c4iw_ep *ep);
static int stop_ep_timer(struct c4iw_ep *ep);
static int set_tcpinfo(struct c4iw_ep *ep);
static void process_timeout(struct c4iw_ep *ep);
static void process_err_cqes(void);
static void *alloc_ep(int size, gfp_t flags);
static void close_socket(struct socket *so);
static int send_mpa_req(struct c4iw_ep *ep);
static int send_mpa_reject(struct c4iw_ep *ep, const void *pdata, u8 plen);
static int send_mpa_reply(struct c4iw_ep *ep, const void *pdata, u8 plen);
static void close_complete_upcall(struct c4iw_ep *ep, int status);
static int send_abort(struct c4iw_ep *ep);
static void peer_close_upcall(struct c4iw_ep *ep);
static void peer_abort_upcall(struct c4iw_ep *ep);
static void connect_reply_upcall(struct c4iw_ep *ep, int status);
static int connect_request_upcall(struct c4iw_ep *ep);
static void established_upcall(struct c4iw_ep *ep);
static int process_mpa_reply(struct c4iw_ep *ep);
static int process_mpa_request(struct c4iw_ep *ep);
static void process_peer_close(struct c4iw_ep *ep);
static void process_conn_error(struct c4iw_ep *ep);
static void process_close_complete(struct c4iw_ep *ep);
static void ep_timeout(unsigned long arg);
static void setiwsockopt(struct socket *so);
static void init_iwarp_socket(struct socket *so, void *arg);
static void uninit_iwarp_socket(struct socket *so);
static void process_data(struct c4iw_ep *ep);
static void process_connected(struct c4iw_ep *ep);
static int c4iw_so_upcall(struct socket *so, void *arg, int waitflag);
static void process_socket_event(struct c4iw_ep *ep);
static void release_ep_resources(struct c4iw_ep *ep);
static int process_terminate(struct c4iw_ep *ep);
static int terminate(struct sge_iq *iq, const struct rss_header *rss,
    struct mbuf *m);
static int add_ep_to_req_list(struct c4iw_ep *ep, int ep_events);
static struct listen_port_info *
add_ep_to_listenlist(struct c4iw_listen_ep *lep);
static int rem_ep_from_listenlist(struct c4iw_listen_ep *lep);
static struct c4iw_listen_ep *
find_real_listen_ep(struct c4iw_listen_ep *master_lep, struct socket *so);
static int get_ifnet_from_raddr(struct sockaddr_storage *raddr,
		struct ifnet **ifp);
static void process_newconn(struct c4iw_listen_ep *master_lep,
		struct socket *new_so);
#define START_EP_TIMER(ep) \
    do { \
	    CTR3(KTR_IW_CXGBE, "start_ep_timer (%s:%d) ep %p", \
		__func__, __LINE__, (ep)); \
	    start_ep_timer(ep); \
    } while (0)

#define STOP_EP_TIMER(ep) \
    ({ \
	    CTR3(KTR_IW_CXGBE, "stop_ep_timer (%s:%d) ep %p", \
		__func__, __LINE__, (ep)); \
	    stop_ep_timer(ep); \
    })

#define GET_LOCAL_ADDR(pladdr, so) \
	do { \
		struct sockaddr_storage *__a = NULL; \
		struct  inpcb *__inp = sotoinpcb(so); \
		KASSERT(__inp != NULL, \
		   ("GET_LOCAL_ADDR(%s):so:%p, inp = NULL", __func__, so)); \
		if (__inp->inp_vflag & INP_IPV4) \
			in_getsockaddr(so, (struct sockaddr **)&__a); \
		else \
			in6_getsockaddr(so, (struct sockaddr **)&__a); \
		*(pladdr) = *__a; \
		free(__a, M_SONAME); \
	} while (0)

#define GET_REMOTE_ADDR(praddr, so) \
	do { \
		struct sockaddr_storage *__a = NULL; \
		struct  inpcb *__inp = sotoinpcb(so); \
		KASSERT(__inp != NULL, \
		   ("GET_REMOTE_ADDR(%s):so:%p, inp = NULL", __func__, so)); \
		if (__inp->inp_vflag & INP_IPV4) \
			in_getpeeraddr(so, (struct sockaddr **)&__a); \
		else \
			in6_getpeeraddr(so, (struct sockaddr **)&__a); \
		*(praddr) = *__a; \
		free(__a, M_SONAME); \
	} while (0)

static char *states[] = {
	"idle",
	"listen",
	"connecting",
	"mpa_wait_req",
	"mpa_req_sent",
	"mpa_req_rcvd",
	"mpa_rep_sent",
	"fpdu_mode",
	"aborting",
	"closing",
	"moribund",
	"dead",
	NULL,
};

static void deref_cm_id(struct c4iw_ep_common *epc)
{
      epc->cm_id->rem_ref(epc->cm_id);
      epc->cm_id = NULL;
      set_bit(CM_ID_DEREFED, &epc->history);
}

static void ref_cm_id(struct c4iw_ep_common *epc)
{
      set_bit(CM_ID_REFED, &epc->history);
      epc->cm_id->add_ref(epc->cm_id);
}

static void deref_qp(struct c4iw_ep *ep)
{
	c4iw_qp_rem_ref(&ep->com.qp->ibqp);
	clear_bit(QP_REFERENCED, &ep->com.flags);
	set_bit(QP_DEREFED, &ep->com.history);
}

static void ref_qp(struct c4iw_ep *ep)
{
	set_bit(QP_REFERENCED, &ep->com.flags);
	set_bit(QP_REFED, &ep->com.history);
	c4iw_qp_add_ref(&ep->com.qp->ibqp);
}
/* allocated per TCP port while listening */
struct listen_port_info {
	uint16_t port_num; /* TCP port address */
	struct list_head list; /* belongs to listen_port_list */
	struct list_head lep_list; /* per port lep list */
	uint32_t refcnt; /* number of lep's listening */
};

/*
 * Following two lists are used to manage INADDR_ANY listeners:
 * 1)listen_port_list
 * 2)lep_list
 *
 * Below is the INADDR_ANY listener lists overview on a system with a two port
 * adapter:
 *   |------------------|
 *   |listen_port_list  |
 *   |------------------|
 *            |
 *            |              |-----------|       |-----------|  
 *            |              | port_num:X|       | port_num:X|  
 *            |--------------|-list------|-------|-list------|-------....
 *                           | lep_list----|     | lep_list----|
 *                           | refcnt    | |     | refcnt    | |
 *                           |           | |     |           | |
 *                           |           | |     |           | |
 *                           |-----------| |     |-----------| |
 *                                         |                   |
 *                                         |                   |
 *                                         |                   |
 *                                         |                   |         lep1                  lep2         
 *                                         |                   |    |----------------|    |----------------|
 *                                         |                   |----| listen_ep_list |----| listen_ep_list |
 *                                         |                        |----------------|    |----------------|
 *                                         |
 *                                         |
 *                                         |        lep1                  lep2         
 *                                         |   |----------------|    |----------------|
 *                                         |---| listen_ep_list |----| listen_ep_list |
 *                                             |----------------|    |----------------|
 *
 * Because of two port adapter, the number of lep's are two(lep1 & lep2) for
 * each TCP port number.
 *
 * Here 'lep1' is always marked as Master lep, because solisten() is always
 * called through first lep. 
 *
 */
static struct listen_port_info *
add_ep_to_listenlist(struct c4iw_listen_ep *lep)
{
	uint16_t port;
	struct listen_port_info *port_info = NULL;
	struct sockaddr_storage *laddr = &lep->com.local_addr;

	port = (laddr->ss_family == AF_INET) ?
		((struct sockaddr_in *)laddr)->sin_port :
		((struct sockaddr_in6 *)laddr)->sin6_port;

	mutex_lock(&listen_port_mutex);

	list_for_each_entry(port_info, &listen_port_list, list)
		if (port_info->port_num == port)
			goto found_port;

	port_info = malloc(sizeof(*port_info), M_CXGBE, M_WAITOK);
	port_info->port_num = port;
	port_info->refcnt    = 0;

	list_add_tail(&port_info->list, &listen_port_list);
	INIT_LIST_HEAD(&port_info->lep_list);

found_port:
	port_info->refcnt++;
	list_add_tail(&lep->listen_ep_list, &port_info->lep_list);
	mutex_unlock(&listen_port_mutex);
	return port_info;
}

static int
rem_ep_from_listenlist(struct c4iw_listen_ep *lep)
{
	uint16_t port;
	struct listen_port_info *port_info = NULL;
	struct sockaddr_storage *laddr = &lep->com.local_addr;
	int refcnt = 0;

	port = (laddr->ss_family == AF_INET) ?
		((struct sockaddr_in *)laddr)->sin_port :
		((struct sockaddr_in6 *)laddr)->sin6_port;

	mutex_lock(&listen_port_mutex);

	/* get the port_info structure based on the lep's port address */
	list_for_each_entry(port_info, &listen_port_list, list) {
		if (port_info->port_num == port) {
			port_info->refcnt--;
			refcnt = port_info->refcnt;
			/* remove the current lep from the listen list */
			list_del(&lep->listen_ep_list);
			if (port_info->refcnt == 0) {
				/* Remove this entry from the list as there
				 * are no more listeners for this port_num.
				 */
				list_del(&port_info->list);
				kfree(port_info);
			}
			break;
		}
	}
	mutex_unlock(&listen_port_mutex);
	return refcnt;
}

/*
 * Find the lep that belongs to the ifnet on which the SYN frame was received.
 */
struct c4iw_listen_ep *
find_real_listen_ep(struct c4iw_listen_ep *master_lep, struct socket *so)
{
	struct adapter *adap = NULL;
	struct c4iw_listen_ep *lep = NULL;
	struct ifnet *ifp = NULL, *hw_ifp = NULL;
	struct listen_port_info *port_info = NULL;
	int i = 0, found_portinfo = 0, found_lep = 0;
	uint16_t port;

	/*
	 * STEP 1: Figure out 'ifp' of the physical interface, not pseudo
	 * interfaces like vlan, lagg, etc..
	 * TBD: lagg support, lagg + vlan support.
	 */
	ifp = TOEPCB(so)->l2te->ifp;
	if (ifp->if_type == IFT_L2VLAN) {
		hw_ifp = VLAN_TRUNKDEV(ifp);
		if (hw_ifp == NULL) {
			CTR4(KTR_IW_CXGBE, "%s: Failed to get parent ifnet of "
				"vlan ifnet %p, sock %p, master_lep %p",
				__func__, ifp, so, master_lep);
			return (NULL);
		}
	} else
		hw_ifp = ifp;

	/* STEP 2: Find 'port_info' with listener local port address. */
	port = (master_lep->com.local_addr.ss_family == AF_INET) ?
		((struct sockaddr_in *)&master_lep->com.local_addr)->sin_port :
		((struct sockaddr_in6 *)&master_lep->com.local_addr)->sin6_port;


	mutex_lock(&listen_port_mutex);
	list_for_each_entry(port_info, &listen_port_list, list)
		if (port_info->port_num == port) {
			found_portinfo =1;
			break;
		}
	if (!found_portinfo)
		goto out;

	/* STEP 3: Traverse through list of lep's that are bound to the current
	 * TCP port address and find the lep that belongs to the ifnet on which
	 * the SYN frame was received.
	 */
	list_for_each_entry(lep, &port_info->lep_list, listen_ep_list) {
		adap = lep->com.dev->rdev.adap;
		for_each_port(adap, i) {
			if (hw_ifp == adap->port[i]->vi[0].ifp) {
				found_lep =1;
				goto out;
			}
		}
	}
out:
	mutex_unlock(&listen_port_mutex);
	return found_lep ? lep : (NULL);
}

static void process_timeout(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs = {0};
	int abort = 1;

	CTR4(KTR_IW_CXGBE, "%s ep :%p, tid:%u, state %d", __func__,
			ep, ep->hwtid, ep->com.state);
	set_bit(TIMEDOUT, &ep->com.history);
	switch (ep->com.state) {
	case MPA_REQ_SENT:
		connect_reply_upcall(ep, -ETIMEDOUT);
		break;
	case MPA_REQ_WAIT:
	case MPA_REQ_RCVD:
	case MPA_REP_SENT:
	case FPDU_MODE:
		break;
	case CLOSING:
	case MORIBUND:
		if (ep->com.cm_id && ep->com.qp) {
			attrs.next_state = C4IW_QP_STATE_ERROR;
			c4iw_modify_qp(ep->com.dev, ep->com.qp,
					C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		}
		close_complete_upcall(ep, -ETIMEDOUT);
		break;
	case ABORTING:
	case DEAD:
		/*
		 * These states are expected if the ep timed out at the same
		 * time as another thread was calling stop_ep_timer().
		 * So we silently do nothing for these states.
		 */
		abort = 0;
		break;
	default:
		CTR4(KTR_IW_CXGBE, "%s unexpected state ep %p tid %u state %u"
				, __func__, ep, ep->hwtid, ep->com.state);
		abort = 0;
	}
	if (abort)
		c4iw_ep_disconnect(ep, 1, GFP_KERNEL);
	c4iw_put_ep(&ep->com);
	return;
}

struct cqe_list_entry {
	struct list_head entry;
	struct c4iw_dev *rhp;
	struct t4_cqe err_cqe;
};

static void
process_err_cqes(void)
{
	unsigned long flag;
	struct cqe_list_entry *cle;

	spin_lock_irqsave(&err_cqe_lock, flag);
	while (!list_empty(&err_cqe_list)) {
		struct list_head *tmp;
		tmp = err_cqe_list.next;
		list_del(tmp);
		tmp->next = tmp->prev = NULL;
		spin_unlock_irqrestore(&err_cqe_lock, flag);
		cle = list_entry(tmp, struct cqe_list_entry, entry);
		c4iw_ev_dispatch(cle->rhp, &cle->err_cqe);
		free(cle, M_CXGBE);
		spin_lock_irqsave(&err_cqe_lock, flag);
	}
	spin_unlock_irqrestore(&err_cqe_lock, flag);

	return;
}

static void
process_req(struct work_struct *ctx)
{
	struct c4iw_ep_common *epc;
	unsigned long flag;
	int ep_events;

	process_err_cqes();
	spin_lock_irqsave(&req_lock, flag);
	while (!TAILQ_EMPTY(&req_list)) {
		epc = TAILQ_FIRST(&req_list);
		TAILQ_REMOVE(&req_list, epc, entry);
		epc->entry.tqe_prev = NULL;
		ep_events = epc->ep_events;
		epc->ep_events = 0;
		spin_unlock_irqrestore(&req_lock, flag);
		mutex_lock(&epc->mutex);
		CTR5(KTR_IW_CXGBE, "%s: so %p, ep %p, ep_state %s events 0x%x",
		    __func__, epc->so, epc, states[epc->state], ep_events);
		if (ep_events & C4IW_EVENT_TERM)
			process_terminate((struct c4iw_ep *)epc);
		if (ep_events & C4IW_EVENT_TIMEOUT)
			process_timeout((struct c4iw_ep *)epc);
		if (ep_events & C4IW_EVENT_SOCKET)
			process_socket_event((struct c4iw_ep *)epc);
		mutex_unlock(&epc->mutex);
		c4iw_put_ep(epc);
		process_err_cqes();
		spin_lock_irqsave(&req_lock, flag);
	}
	spin_unlock_irqrestore(&req_lock, flag);
}

/*
 * XXX: doesn't belong here in the iWARP driver.
 * XXX: assumes that the connection was offloaded by cxgbe/t4_tom if TF_TOE is
 *      set.  Is this a valid assumption for active open?
 */
static int
set_tcpinfo(struct c4iw_ep *ep)
{
	struct socket *so = ep->com.so;
	struct inpcb *inp = sotoinpcb(so);
	struct tcpcb *tp;
	struct toepcb *toep;
	int rc = 0;

	INP_WLOCK(inp);
	tp = intotcpcb(inp);
	if ((tp->t_flags & TF_TOE) == 0) {
		rc = EINVAL;
		log(LOG_ERR, "%s: connection not offloaded (so %p, ep %p)\n",
		    __func__, so, ep);
		goto done;
	}
	toep = TOEPCB(so);

	ep->hwtid = toep->tid;
	ep->snd_seq = tp->snd_nxt;
	ep->rcv_seq = tp->rcv_nxt;
	ep->emss = max(tp->t_maxseg, 128);
done:
	INP_WUNLOCK(inp);
	return (rc);

}
static int
get_ifnet_from_raddr(struct sockaddr_storage *raddr, struct ifnet **ifp)
{
	int err = 0;

	if (raddr->ss_family == AF_INET) {
		struct sockaddr_in *raddr4 = (struct sockaddr_in *)raddr;
		struct nhop4_extended nh4 = {0};

		err = fib4_lookup_nh_ext(RT_DEFAULT_FIB, raddr4->sin_addr,
				NHR_REF, 0, &nh4);
		*ifp = nh4.nh_ifp;
		if (err)
			fib4_free_nh_ext(RT_DEFAULT_FIB, &nh4);
	} else {
		struct sockaddr_in6 *raddr6 = (struct sockaddr_in6 *)raddr;
		struct nhop6_extended nh6 = {0};
		struct in6_addr addr6;
		uint32_t scopeid;

		memset(&addr6, 0, sizeof(addr6));
		in6_splitscope((struct in6_addr *)&raddr6->sin6_addr,
					&addr6, &scopeid);
		err = fib6_lookup_nh_ext(RT_DEFAULT_FIB, &addr6, scopeid,
				NHR_REF, 0, &nh6);
		*ifp = nh6.nh_ifp;
		if (err)
			fib6_free_nh_ext(RT_DEFAULT_FIB, &nh6);
	}

	CTR2(KTR_IW_CXGBE, "%s: return: %d", __func__, err);
	return err;
}

static void
close_socket(struct socket *so)
{
	uninit_iwarp_socket(so);
	soclose(so);
}

static void
process_peer_close(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs = {0};
	int disconnect = 1;
	int release = 0;

	CTR4(KTR_IW_CXGBE, "%s:ppcB ep %p so %p state %s", __func__, ep,
	    ep->com.so, states[ep->com.state]);

	switch (ep->com.state) {

		case MPA_REQ_WAIT:
			CTR2(KTR_IW_CXGBE, "%s:ppc1 %p MPA_REQ_WAIT DEAD",
			    __func__, ep);
			/* Fallthrough */
		case MPA_REQ_SENT:
			CTR2(KTR_IW_CXGBE, "%s:ppc2 %p MPA_REQ_SENT DEAD",
			    __func__, ep);
			ep->com.state = DEAD;
			connect_reply_upcall(ep, -ECONNABORTED);

			disconnect = 0;
			STOP_EP_TIMER(ep);
			close_socket(ep->com.so);
			deref_cm_id(&ep->com);
			release = 1;
			break;

		case MPA_REQ_RCVD:

			/*
			 * We're gonna mark this puppy DEAD, but keep
			 * the reference on it until the ULP accepts or
			 * rejects the CR.
			 */
			CTR2(KTR_IW_CXGBE, "%s:ppc3 %p MPA_REQ_RCVD CLOSING",
			    __func__, ep);
			ep->com.state = CLOSING;
			break;

		case MPA_REP_SENT:
			CTR2(KTR_IW_CXGBE, "%s:ppc4 %p MPA_REP_SENT CLOSING",
			    __func__, ep);
			ep->com.state = CLOSING;
			break;

		case FPDU_MODE:
			CTR2(KTR_IW_CXGBE, "%s:ppc5 %p FPDU_MODE CLOSING",
			    __func__, ep);
			START_EP_TIMER(ep);
			ep->com.state = CLOSING;
			attrs.next_state = C4IW_QP_STATE_CLOSING;
			c4iw_modify_qp(ep->com.dev, ep->com.qp,
					C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
			peer_close_upcall(ep);
			break;

		case ABORTING:
			CTR2(KTR_IW_CXGBE, "%s:ppc6 %p ABORTING (disconn)",
			    __func__, ep);
			disconnect = 0;
			break;

		case CLOSING:
			CTR2(KTR_IW_CXGBE, "%s:ppc7 %p CLOSING MORIBUND",
			    __func__, ep);
			ep->com.state = MORIBUND;
			disconnect = 0;
			break;

		case MORIBUND:
			CTR2(KTR_IW_CXGBE, "%s:ppc8 %p MORIBUND DEAD", __func__,
			    ep);
			STOP_EP_TIMER(ep);
			if (ep->com.cm_id && ep->com.qp) {
				attrs.next_state = C4IW_QP_STATE_IDLE;
				c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
						C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
			}
			close_socket(ep->com.so);
			close_complete_upcall(ep, 0);
			ep->com.state = DEAD;
			release = 1;
			disconnect = 0;
			break;

		case DEAD:
			CTR2(KTR_IW_CXGBE, "%s:ppc9 %p DEAD (disconn)",
			    __func__, ep);
			disconnect = 0;
			break;

		default:
			panic("%s: ep %p state %d", __func__, ep,
			    ep->com.state);
			break;
	}


	if (disconnect) {

		CTR2(KTR_IW_CXGBE, "%s:ppca %p", __func__, ep);
		c4iw_ep_disconnect(ep, 0, M_NOWAIT);
	}
	if (release) {

		CTR2(KTR_IW_CXGBE, "%s:ppcb %p", __func__, ep);
		c4iw_put_ep(&ep->com);
	}
	CTR2(KTR_IW_CXGBE, "%s:ppcE %p", __func__, ep);
	return;
}

static void
process_conn_error(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs = {0};
	int ret;
	int state;

	state = ep->com.state;
	CTR5(KTR_IW_CXGBE, "%s:pceB ep %p so %p so->so_error %u state %s",
	    __func__, ep, ep->com.so, ep->com.so->so_error,
	    states[ep->com.state]);

	switch (state) {

		case MPA_REQ_WAIT:
			STOP_EP_TIMER(ep);
			c4iw_put_ep(&ep->parent_ep->com);
			break;

		case MPA_REQ_SENT:
			STOP_EP_TIMER(ep);
			connect_reply_upcall(ep, -ECONNRESET);
			break;

		case MPA_REP_SENT:
			ep->com.rpl_err = ECONNRESET;
			CTR1(KTR_IW_CXGBE, "waking up ep %p", ep);
			break;

		case MPA_REQ_RCVD:
			break;

		case MORIBUND:
		case CLOSING:
			STOP_EP_TIMER(ep);
			/*FALLTHROUGH*/
		case FPDU_MODE:

			if (ep->com.cm_id && ep->com.qp) {

				attrs.next_state = C4IW_QP_STATE_ERROR;
				ret = c4iw_modify_qp(ep->com.qp->rhp,
					ep->com.qp, C4IW_QP_ATTR_NEXT_STATE,
					&attrs, 1);
				if (ret)
					log(LOG_ERR,
							"%s - qp <- error failed!\n",
							__func__);
			}
			peer_abort_upcall(ep);
			break;

		case ABORTING:
			break;

		case DEAD:
			CTR2(KTR_IW_CXGBE, "%s so_error %d IN DEAD STATE!!!!",
			    __func__, ep->com.so->so_error);
			return;

		default:
			panic("%s: ep %p state %d", __func__, ep, state);
			break;
	}

	if (state != ABORTING) {
		close_socket(ep->com.so);
		ep->com.state = DEAD;
		c4iw_put_ep(&ep->com);
	}
	CTR2(KTR_IW_CXGBE, "%s:pceE %p", __func__, ep);
	return;
}

static void
process_close_complete(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs = {0};
	int release = 0;

	CTR4(KTR_IW_CXGBE, "%s:pccB ep %p so %p state %s", __func__, ep,
	    ep->com.so, states[ep->com.state]);

	/* The cm_id may be null if we failed to connect */
	set_bit(CLOSE_CON_RPL, &ep->com.history);

	switch (ep->com.state) {

		case CLOSING:
			CTR2(KTR_IW_CXGBE, "%s:pcc1 %p CLOSING MORIBUND",
			    __func__, ep);
			ep->com.state = MORIBUND;
			break;

		case MORIBUND:
			CTR2(KTR_IW_CXGBE, "%s:pcc1 %p MORIBUND DEAD", __func__,
			    ep);
			STOP_EP_TIMER(ep);

			if ((ep->com.cm_id) && (ep->com.qp)) {

				CTR2(KTR_IW_CXGBE, "%s:pcc2 %p QP_STATE_IDLE",
				    __func__, ep);
				attrs.next_state = C4IW_QP_STATE_IDLE;
				c4iw_modify_qp(ep->com.dev,
						ep->com.qp,
						C4IW_QP_ATTR_NEXT_STATE,
						&attrs, 1);
			}

			close_socket(ep->com.so);
			close_complete_upcall(ep, 0);
			ep->com.state = DEAD;
			release = 1;
			break;

		case ABORTING:
			CTR2(KTR_IW_CXGBE, "%s:pcc5 %p ABORTING", __func__, ep);
			break;

		case DEAD:
			CTR2(KTR_IW_CXGBE, "%s:pcc6 %p DEAD", __func__, ep);
			break;
		default:
			CTR2(KTR_IW_CXGBE, "%s:pcc7 %p unknown ep state",
					__func__, ep);
			panic("%s:pcc6 %p unknown ep state", __func__, ep);
			break;
	}

	if (release) {

		CTR2(KTR_IW_CXGBE, "%s:pcc8 %p", __func__, ep);
		release_ep_resources(ep);
	}
	CTR2(KTR_IW_CXGBE, "%s:pccE %p", __func__, ep);
	return;
}

static void
setiwsockopt(struct socket *so)
{
	int rc;
	struct sockopt sopt;
	int on = 1;

	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = IPPROTO_TCP;
	sopt.sopt_name = TCP_NODELAY;
	sopt.sopt_val = (caddr_t)&on;
	sopt.sopt_valsize = sizeof on;
	sopt.sopt_td = NULL;
	rc = -sosetopt(so, &sopt);
	if (rc) {
		log(LOG_ERR, "%s: can't set TCP_NODELAY on so %p (%d)\n",
		    __func__, so, rc);
	}
}

static void
init_iwarp_socket(struct socket *so, void *arg)
{
	if (SOLISTENING(so)) {
		SOLISTEN_LOCK(so);
		solisten_upcall_set(so, c4iw_so_upcall, arg);
		so->so_state |= SS_NBIO;
		SOLISTEN_UNLOCK(so);
	} else {
		SOCKBUF_LOCK(&so->so_rcv);
		soupcall_set(so, SO_RCV, c4iw_so_upcall, arg);
		so->so_state |= SS_NBIO;
		SOCKBUF_UNLOCK(&so->so_rcv);
	}
}

static void
uninit_iwarp_socket(struct socket *so)
{
	if (SOLISTENING(so)) {
		SOLISTEN_LOCK(so);
		solisten_upcall_set(so, NULL, NULL);
		SOLISTEN_UNLOCK(so);
	} else {
		SOCKBUF_LOCK(&so->so_rcv);
		soupcall_clear(so, SO_RCV);
		SOCKBUF_UNLOCK(&so->so_rcv);
	}
}

static void
process_data(struct c4iw_ep *ep)
{
	int ret = 0;
	int disconnect = 0;
	struct c4iw_qp_attributes attrs = {0};

	CTR5(KTR_IW_CXGBE, "%s: so %p, ep %p, state %s, sbused %d", __func__,
	    ep->com.so, ep, states[ep->com.state], sbused(&ep->com.so->so_rcv));

	switch (ep->com.state) {
	case MPA_REQ_SENT:
		disconnect = process_mpa_reply(ep);
		break;
	case MPA_REQ_WAIT:
		disconnect = process_mpa_request(ep);
		if (disconnect)
			/* Refered in process_newconn() */
			c4iw_put_ep(&ep->parent_ep->com);
		break;
	case FPDU_MODE:
		MPASS(ep->com.qp != NULL);
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		ret = c4iw_modify_qp(ep->com.dev, ep->com.qp,
					C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		if (ret != -EINPROGRESS)
			disconnect = 1;
		break;
	default:
		log(LOG_ERR, "%s: Unexpected streaming data. ep %p, "
			    "state %d, so %p, so_state 0x%x, sbused %u\n",
			    __func__, ep, ep->com.state, ep->com.so,
			    ep->com.so->so_state, sbused(&ep->com.so->so_rcv));
		break;
	}
	if (disconnect)
		c4iw_ep_disconnect(ep, disconnect == 2, GFP_KERNEL);

}

static void
process_connected(struct c4iw_ep *ep)
{
	struct socket *so = ep->com.so;

	if ((so->so_state & SS_ISCONNECTED) && !so->so_error) {
		if (send_mpa_req(ep))
			goto err;
	} else {
		connect_reply_upcall(ep, -so->so_error);
		goto err;
	}
	return;
err:
	close_socket(so);
	ep->com.state = DEAD;
	c4iw_put_ep(&ep->com);
	return;
}

static inline int c4iw_zero_addr(struct sockaddr *addr)
{
	struct in6_addr *ip6;

	if (addr->sa_family == AF_INET)
		return IN_ZERONET(
			ntohl(((struct sockaddr_in *)addr)->sin_addr.s_addr));
	else {
		ip6 = &((struct sockaddr_in6 *) addr)->sin6_addr;
		return (ip6->s6_addr32[0] | ip6->s6_addr32[1] |
				ip6->s6_addr32[2] | ip6->s6_addr32[3]) == 0;
	}
}

static inline int c4iw_loopback_addr(struct sockaddr *addr)
{
	if (addr->sa_family == AF_INET)
		return IN_LOOPBACK(
			ntohl(((struct sockaddr_in *) addr)->sin_addr.s_addr));
	else
		return IN6_IS_ADDR_LOOPBACK(
				&((struct sockaddr_in6 *) addr)->sin6_addr);
}

static inline int c4iw_any_addr(struct sockaddr *addr)
{
	return c4iw_zero_addr(addr) || c4iw_loopback_addr(addr);
}

static void
process_newconn(struct c4iw_listen_ep *master_lep, struct socket *new_so)
{
	struct c4iw_listen_ep *real_lep = NULL;
	struct c4iw_ep *new_ep = NULL;
	struct sockaddr_in *remote = NULL;
	int ret = 0;

	MPASS(new_so != NULL);

	if (c4iw_any_addr((struct sockaddr *)&master_lep->com.local_addr)) {
		/* Here we need to find the 'real_lep' that belongs to the
		 * incomming socket's network interface, such that the newly
		 * created 'ep' can be attached to the real 'lep'.
		 */
		real_lep = find_real_listen_ep(master_lep, new_so);
		if (real_lep == NULL) {
			CTR2(KTR_IW_CXGBE, "%s: Could not find the real listen "
					"ep for sock: %p", __func__, new_so);
			log(LOG_ERR,"%s: Could not find the real listen ep for "
					"sock: %p\n", __func__, new_so);
			/* FIXME: properly free the 'new_so' in failure case.
			 * Use of soabort() and  soclose() are not legal
			 * here(before soaccept()).
			 */
			return;
		}
	} else /* for Non-Wildcard address, master_lep is always the real_lep */
		real_lep = master_lep;

	new_ep = alloc_ep(sizeof(*new_ep), GFP_KERNEL);

	CTR6(KTR_IW_CXGBE, "%s: master_lep %p, real_lep: %p, new ep %p, "
	    "listening so %p, new so %p", __func__, master_lep, real_lep,
	    new_ep, master_lep->com.so, new_so);

	new_ep->com.dev = real_lep->com.dev;
	new_ep->com.so = new_so;
	new_ep->com.cm_id = NULL;
	new_ep->com.thread = real_lep->com.thread;
	new_ep->parent_ep = real_lep;

	GET_LOCAL_ADDR(&new_ep->com.local_addr, new_so);
	GET_REMOTE_ADDR(&new_ep->com.remote_addr, new_so);
	c4iw_get_ep(&real_lep->com);
	init_timer(&new_ep->timer);
	new_ep->com.state = MPA_REQ_WAIT;
	START_EP_TIMER(new_ep);

	setiwsockopt(new_so);
	ret = soaccept(new_so, (struct sockaddr **)&remote);
	if (ret != 0) {
		CTR4(KTR_IW_CXGBE,
				"%s:listen sock:%p, new sock:%p, ret:%d",
				__func__, master_lep->com.so, new_so, ret);
		if (remote != NULL)
			free(remote, M_SONAME);
		uninit_iwarp_socket(new_so);
		soclose(new_so);
		c4iw_put_ep(&new_ep->com);
		c4iw_put_ep(&real_lep->com);
		return;
	}
	free(remote, M_SONAME);

	/* MPA request might have been queued up on the socket already, so we
	 * initialize the socket/upcall_handler under lock to prevent processing
	 * MPA request on another thread(via process_req()) simultaniously.
	 */
	c4iw_get_ep(&new_ep->com); /* Dereferenced at the end below, this is to
				      avoid freeing of ep before ep unlock. */
	mutex_lock(&new_ep->com.mutex);
	init_iwarp_socket(new_so, &new_ep->com);

	ret = process_mpa_request(new_ep);
	if (ret) {
		/* ABORT */
		c4iw_ep_disconnect(new_ep, 1, GFP_KERNEL);
		c4iw_put_ep(&real_lep->com);
	}
	mutex_unlock(&new_ep->com.mutex);
	c4iw_put_ep(&new_ep->com);
	return;
}

static int
add_ep_to_req_list(struct c4iw_ep *ep, int new_ep_event)
{
	unsigned long flag;

	spin_lock_irqsave(&req_lock, flag);
	if (ep && ep->com.so) {
		ep->com.ep_events |= new_ep_event;
		if (!ep->com.entry.tqe_prev) {
			c4iw_get_ep(&ep->com);
			TAILQ_INSERT_TAIL(&req_list, &ep->com, entry);
			queue_work(c4iw_taskq, &c4iw_task);
		}
	}
	spin_unlock_irqrestore(&req_lock, flag);

	return (0);
}

static int
c4iw_so_upcall(struct socket *so, void *arg, int waitflag)
{
	struct c4iw_ep *ep = arg;

	CTR6(KTR_IW_CXGBE,
	    "%s: so %p, so_state 0x%x, ep %p, ep_state %s, tqe_prev %p",
	    __func__, so, so->so_state, ep, states[ep->com.state],
	    ep->com.entry.tqe_prev);

	MPASS(ep->com.so == so);
	/*
	 * Wake up any threads waiting in rdma_init()/rdma_fini(),
	 * with locks held.
	 */
	if (so->so_error)
		c4iw_wake_up(&ep->com.wr_wait, -ECONNRESET);
	add_ep_to_req_list(ep, C4IW_EVENT_SOCKET);

	return (SU_OK);
}


static int
terminate(struct sge_iq *iq, const struct rss_header *rss, struct mbuf *m)
{
	struct adapter *sc = iq->adapter;
	const struct cpl_rdma_terminate *cpl = mtod(m, const void *);
	unsigned int tid = GET_TID(cpl);
	struct toepcb *toep = lookup_tid(sc, tid);
	struct socket *so;
	struct c4iw_ep *ep;

	INP_WLOCK(toep->inp);
	so = inp_inpcbtosocket(toep->inp);
	ep = so->so_rcv.sb_upcallarg;
	INP_WUNLOCK(toep->inp);

	CTR3(KTR_IW_CXGBE, "%s: so %p, ep %p", __func__, so, ep);
	add_ep_to_req_list(ep, C4IW_EVENT_TERM);

	return 0;
}

static void
process_socket_event(struct c4iw_ep *ep)
{
	int state = ep->com.state;
	struct socket *so = ep->com.so;

	if (ep->com.state == DEAD) {
		CTR3(KTR_IW_CXGBE, "%s: Pending socket event discarded "
			"ep %p ep_state %s", __func__, ep, states[state]); 
		return;
	}

	CTR6(KTR_IW_CXGBE, "process_socket_event: so %p, so_state 0x%x, "
	    "so_err %d, sb_state 0x%x, ep %p, ep_state %s", so, so->so_state,
	    so->so_error, so->so_rcv.sb_state, ep, states[state]);

	if (state == CONNECTING) {
		process_connected(ep);
		return;
	}

	if (state == LISTEN) {
		struct c4iw_listen_ep *lep = (struct c4iw_listen_ep *)ep;
		struct socket *listen_so = so, *new_so = NULL;
		int error = 0;

		SOLISTEN_LOCK(listen_so);
		do {
			error = solisten_dequeue(listen_so, &new_so,
						SOCK_NONBLOCK);
			if (error) {
				CTR4(KTR_IW_CXGBE, "%s: lep %p listen_so %p "
					"error %d", __func__, lep, listen_so,
					error);
				return;
			}
			process_newconn(lep, new_so);

			/* solisten_dequeue() unlocks while return, so aquire
			 * lock again for sol_qlen and also for next iteration.
			 */
			SOLISTEN_LOCK(listen_so);
		} while (listen_so->sol_qlen);
		SOLISTEN_UNLOCK(listen_so);

		return;
	}

	/* connection error */
	if (so->so_error) {
		process_conn_error(ep);
		return;
	}

	/* peer close */
	if ((so->so_rcv.sb_state & SBS_CANTRCVMORE) && state <= CLOSING) {
		process_peer_close(ep);
		/*
		 * check whether socket disconnect event is pending before
		 * returning. Fallthrough if yes.
		 */
		if (!(so->so_state & SS_ISDISCONNECTED))
			return;
	}

	/* close complete */
	if (so->so_state & SS_ISDISCONNECTED) {
		process_close_complete(ep);
		return;
	}

	/* rx data */
	if (sbused(&ep->com.so->so_rcv)) {
		process_data(ep);
		return;
	}

	/* Socket events for 'MPA Request Received' and 'Close Complete'
	 * were already processed earlier in their previous events handlers.
	 * Hence, these socket events are skipped.
	 * And any other socket events must have handled above.
	 */
	MPASS((ep->com.state == MPA_REQ_RCVD) || (ep->com.state == MORIBUND));

	if ((ep->com.state != MPA_REQ_RCVD) && (ep->com.state != MORIBUND))
		log(LOG_ERR, "%s: Unprocessed socket event so %p, "
		"so_state 0x%x, so_err %d, sb_state 0x%x, ep %p, ep_state %s\n",
		__func__, so, so->so_state, so->so_error, so->so_rcv.sb_state,
			ep, states[state]);

}

SYSCTL_NODE(_hw, OID_AUTO, iw_cxgbe, CTLFLAG_RD, 0, "iw_cxgbe driver parameters");

static int dack_mode = 0;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, dack_mode, CTLFLAG_RWTUN, &dack_mode, 0,
		"Delayed ack mode (default = 0)");

int c4iw_max_read_depth = 8;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, c4iw_max_read_depth, CTLFLAG_RWTUN, &c4iw_max_read_depth, 0,
		"Per-connection max ORD/IRD (default = 8)");

static int enable_tcp_timestamps;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, enable_tcp_timestamps, CTLFLAG_RWTUN, &enable_tcp_timestamps, 0,
		"Enable tcp timestamps (default = 0)");

static int enable_tcp_sack;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, enable_tcp_sack, CTLFLAG_RWTUN, &enable_tcp_sack, 0,
		"Enable tcp SACK (default = 0)");

static int enable_tcp_window_scaling = 1;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, enable_tcp_window_scaling, CTLFLAG_RWTUN, &enable_tcp_window_scaling, 0,
		"Enable tcp window scaling (default = 1)");

int c4iw_debug = 0;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, c4iw_debug, CTLFLAG_RWTUN, &c4iw_debug, 0,
		"Enable debug logging (default = 0)");

static int peer2peer = 1;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, peer2peer, CTLFLAG_RWTUN, &peer2peer, 0,
		"Support peer2peer ULPs (default = 1)");

static int p2p_type = FW_RI_INIT_P2PTYPE_READ_REQ;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, p2p_type, CTLFLAG_RWTUN, &p2p_type, 0,
		"RDMAP opcode to use for the RTR message: 1 = RDMA_READ 0 = RDMA_WRITE (default 1)");

static int ep_timeout_secs = 60;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, ep_timeout_secs, CTLFLAG_RWTUN, &ep_timeout_secs, 0,
		"CM Endpoint operation timeout in seconds (default = 60)");

static int mpa_rev = 1;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, mpa_rev, CTLFLAG_RWTUN, &mpa_rev, 0,
		"MPA Revision, 0 supports amso1100, 1 is RFC5044 spec compliant, 2 is IETF MPA Peer Connect Draft compliant (default = 1)");

static int markers_enabled;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, markers_enabled, CTLFLAG_RWTUN, &markers_enabled, 0,
		"Enable MPA MARKERS (default(0) = disabled)");

static int crc_enabled = 1;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, crc_enabled, CTLFLAG_RWTUN, &crc_enabled, 0,
		"Enable MPA CRC (default(1) = enabled)");

static int rcv_win = 256 * 1024;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, rcv_win, CTLFLAG_RWTUN, &rcv_win, 0,
		"TCP receive window in bytes (default = 256KB)");

static int snd_win = 128 * 1024;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, snd_win, CTLFLAG_RWTUN, &snd_win, 0,
		"TCP send window in bytes (default = 128KB)");

int use_dsgl = 1;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, use_dsgl, CTLFLAG_RWTUN, &use_dsgl, 0,
		"Use DSGL for PBL/FastReg (default=1)");

int inline_threshold = 128;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, inline_threshold, CTLFLAG_RWTUN, &inline_threshold, 0,
		"inline vs dsgl threshold (default=128)");

static int reuseaddr = 0;
SYSCTL_INT(_hw_iw_cxgbe, OID_AUTO, reuseaddr, CTLFLAG_RWTUN, &reuseaddr, 0,
		"Enable SO_REUSEADDR & SO_REUSEPORT socket options on all iWARP client connections(default = 0)");

static void
start_ep_timer(struct c4iw_ep *ep)
{

	if (timer_pending(&ep->timer)) {
		CTR2(KTR_IW_CXGBE, "%s: ep %p, already started", __func__, ep);
		printk(KERN_ERR "%s timer already started! ep %p\n", __func__,
		    ep);
		return;
	}
	clear_bit(TIMEOUT, &ep->com.flags);
	c4iw_get_ep(&ep->com);
	ep->timer.expires = jiffies + ep_timeout_secs * HZ;
	ep->timer.data = (unsigned long)ep;
	ep->timer.function = ep_timeout;
	add_timer(&ep->timer);
}

static int
stop_ep_timer(struct c4iw_ep *ep)
{

	del_timer_sync(&ep->timer);
	if (!test_and_set_bit(TIMEOUT, &ep->com.flags)) {
		c4iw_put_ep(&ep->com);
		return 0;
	}
	return 1;
}

static void *
alloc_ep(int size, gfp_t gfp)
{
	struct c4iw_ep_common *epc;

	epc = kzalloc(size, gfp);
	if (epc == NULL)
		return (NULL);

	kref_init(&epc->kref);
	mutex_init(&epc->mutex);
	c4iw_init_wr_wait(&epc->wr_wait);

	return (epc);
}

void _c4iw_free_ep(struct kref *kref)
{
	struct c4iw_ep *ep;
	struct c4iw_ep_common *epc;

	ep = container_of(kref, struct c4iw_ep, com.kref);
	epc = &ep->com;
	KASSERT(!epc->entry.tqe_prev, ("%s epc %p still on req list",
	    __func__, epc));
	if (test_bit(QP_REFERENCED, &ep->com.flags))
		deref_qp(ep);
	CTR4(KTR_IW_CXGBE, "%s: ep %p, history 0x%lx, flags 0x%lx",
	    __func__, ep, epc->history, epc->flags);
	kfree(ep);
}

static void release_ep_resources(struct c4iw_ep *ep)
{
	CTR2(KTR_IW_CXGBE, "%s:rerB %p", __func__, ep);
	set_bit(RELEASE_RESOURCES, &ep->com.flags);
	c4iw_put_ep(&ep->com);
	CTR2(KTR_IW_CXGBE, "%s:rerE %p", __func__, ep);
}

static int
send_mpa_req(struct c4iw_ep *ep)
{
	int mpalen;
	struct mpa_message *mpa;
	struct mpa_v2_conn_params mpa_v2_params;
	struct mbuf *m;
	char mpa_rev_to_use = mpa_rev;
	int err = 0;

	if (ep->retry_with_mpa_v1)
		mpa_rev_to_use = 1;
	mpalen = sizeof(*mpa) + ep->plen;
	if (mpa_rev_to_use == 2)
		mpalen += sizeof(struct mpa_v2_conn_params);

	mpa = malloc(mpalen, M_CXGBE, M_NOWAIT);
	if (mpa == NULL) {
		err = -ENOMEM;
		CTR3(KTR_IW_CXGBE, "%s:smr1 ep: %p , error: %d",
				__func__, ep, err);
		goto err;
	}

	memset(mpa, 0, mpalen);
	memcpy(mpa->key, MPA_KEY_REQ, sizeof(mpa->key));
	mpa->flags = (crc_enabled ? MPA_CRC : 0) |
		(markers_enabled ? MPA_MARKERS : 0) |
		(mpa_rev_to_use == 2 ? MPA_ENHANCED_RDMA_CONN : 0);
	mpa->private_data_size = htons(ep->plen);
	mpa->revision = mpa_rev_to_use;

	if (mpa_rev_to_use == 1) {
		ep->tried_with_mpa_v1 = 1;
		ep->retry_with_mpa_v1 = 0;
	}

	if (mpa_rev_to_use == 2) {
		mpa->private_data_size = htons(ntohs(mpa->private_data_size) +
					    sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons((u16)ep->ird);
		mpa_v2_params.ord = htons((u16)ep->ord);

		if (peer2peer) {
			mpa_v2_params.ird |= htons(MPA_V2_PEER2PEER_MODEL);

			if (p2p_type == FW_RI_INIT_P2PTYPE_RDMA_WRITE) {
				mpa_v2_params.ord |=
				    htons(MPA_V2_RDMA_WRITE_RTR);
			} else if (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ) {
				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_READ_RTR);
			}
		}
		memcpy(mpa->private_data, &mpa_v2_params,
			sizeof(struct mpa_v2_conn_params));

		if (ep->plen) {

			memcpy(mpa->private_data +
				sizeof(struct mpa_v2_conn_params),
				ep->mpa_pkt + sizeof(*mpa), ep->plen);
		}
	} else {

		if (ep->plen)
			memcpy(mpa->private_data,
					ep->mpa_pkt + sizeof(*mpa), ep->plen);
		CTR2(KTR_IW_CXGBE, "%s:smr7 %p", __func__, ep);
	}

	m = m_getm(NULL, mpalen, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		err = -ENOMEM;
		CTR3(KTR_IW_CXGBE, "%s:smr2 ep: %p , error: %d",
				__func__, ep, err);
		free(mpa, M_CXGBE);
		goto err;
	}
	m_copyback(m, 0, mpalen, (void *)mpa);
	free(mpa, M_CXGBE);

	err = -sosend(ep->com.so, NULL, NULL, m, NULL, MSG_DONTWAIT,
			ep->com.thread);
	if (err) {
		CTR3(KTR_IW_CXGBE, "%s:smr3 ep: %p , error: %d",
				__func__, ep, err);
		goto err;
	}

	START_EP_TIMER(ep);
	ep->com.state = MPA_REQ_SENT;
	ep->mpa_attr.initiator = 1;
	CTR3(KTR_IW_CXGBE, "%s:smrE %p, error: %d", __func__, ep, err);
	return 0;
err:
	connect_reply_upcall(ep, err);
	CTR3(KTR_IW_CXGBE, "%s:smrE %p, error: %d", __func__, ep, err);
	return err;
}

static int send_mpa_reject(struct c4iw_ep *ep, const void *pdata, u8 plen)
{
	int mpalen ;
	struct mpa_message *mpa;
	struct mpa_v2_conn_params mpa_v2_params;
	struct mbuf *m;
	int err;

	CTR4(KTR_IW_CXGBE, "%s:smrejB %p %u %d", __func__, ep, ep->hwtid,
	    ep->plen);

	mpalen = sizeof(*mpa) + plen;

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {

		mpalen += sizeof(struct mpa_v2_conn_params);
		CTR4(KTR_IW_CXGBE, "%s:smrej1 %p %u %d", __func__, ep,
		    ep->mpa_attr.version, mpalen);
	}

	mpa = malloc(mpalen, M_CXGBE, M_NOWAIT);
	if (mpa == NULL)
		return (-ENOMEM);

	memset(mpa, 0, mpalen);
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = MPA_REJECT;
	mpa->revision = mpa_rev;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {

		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size = htons(ntohs(mpa->private_data_size) +
					    sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons(((u16)ep->ird) |
				(peer2peer ? MPA_V2_PEER2PEER_MODEL :
				 0));
		mpa_v2_params.ord = htons(((u16)ep->ord) | (peer2peer ?
					(p2p_type ==
					 FW_RI_INIT_P2PTYPE_RDMA_WRITE ?
					 MPA_V2_RDMA_WRITE_RTR : p2p_type ==
					 FW_RI_INIT_P2PTYPE_READ_REQ ?
					 MPA_V2_RDMA_READ_RTR : 0) : 0));
		memcpy(mpa->private_data, &mpa_v2_params,
				sizeof(struct mpa_v2_conn_params));

		if (ep->plen)
			memcpy(mpa->private_data +
				sizeof(struct mpa_v2_conn_params), pdata, plen);
		CTR5(KTR_IW_CXGBE, "%s:smrej3 %p %d %d %d", __func__, ep,
		    mpa_v2_params.ird, mpa_v2_params.ord, ep->plen);
	} else
		if (plen)
			memcpy(mpa->private_data, pdata, plen);

	m = m_getm(NULL, mpalen, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		free(mpa, M_CXGBE);
		return (-ENOMEM);
	}
	m_copyback(m, 0, mpalen, (void *)mpa);
	free(mpa, M_CXGBE);

	err = -sosend(ep->com.so, NULL, NULL, m, NULL, MSG_DONTWAIT, ep->com.thread);
	if (!err)
		ep->snd_seq += mpalen;
	CTR4(KTR_IW_CXGBE, "%s:smrejE %p %u %d", __func__, ep, ep->hwtid, err);
	return err;
}

static int send_mpa_reply(struct c4iw_ep *ep, const void *pdata, u8 plen)
{
	int mpalen;
	struct mpa_message *mpa;
	struct mbuf *m;
	struct mpa_v2_conn_params mpa_v2_params;
	int err;

	CTR2(KTR_IW_CXGBE, "%s:smrepB %p", __func__, ep);

	mpalen = sizeof(*mpa) + plen;

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {

		CTR3(KTR_IW_CXGBE, "%s:smrep1 %p %d", __func__, ep,
		    ep->mpa_attr.version);
		mpalen += sizeof(struct mpa_v2_conn_params);
	}

	mpa = malloc(mpalen, M_CXGBE, M_NOWAIT);
	if (mpa == NULL)
		return (-ENOMEM);

	memset(mpa, 0, sizeof(*mpa));
	memcpy(mpa->key, MPA_KEY_REP, sizeof(mpa->key));
	mpa->flags = (ep->mpa_attr.crc_enabled ? MPA_CRC : 0) |
		(markers_enabled ? MPA_MARKERS : 0);
	mpa->revision = ep->mpa_attr.version;
	mpa->private_data_size = htons(plen);

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {

		mpa->flags |= MPA_ENHANCED_RDMA_CONN;
		mpa->private_data_size +=
			htons(sizeof(struct mpa_v2_conn_params));
		mpa_v2_params.ird = htons((u16)ep->ird);
		mpa_v2_params.ord = htons((u16)ep->ord);
		CTR5(KTR_IW_CXGBE, "%s:smrep3 %p %d %d %d", __func__, ep,
		    ep->mpa_attr.version, mpa_v2_params.ird, mpa_v2_params.ord);

		if (peer2peer && (ep->mpa_attr.p2p_type !=
			FW_RI_INIT_P2PTYPE_DISABLED)) {

			mpa_v2_params.ird |= htons(MPA_V2_PEER2PEER_MODEL);

			if (p2p_type == FW_RI_INIT_P2PTYPE_RDMA_WRITE) {

				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_WRITE_RTR);
				CTR5(KTR_IW_CXGBE, "%s:smrep4 %p %d %d %d",
				    __func__, ep, p2p_type, mpa_v2_params.ird,
				    mpa_v2_params.ord);
			}
			else if (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ) {

				mpa_v2_params.ord |=
					htons(MPA_V2_RDMA_READ_RTR);
				CTR5(KTR_IW_CXGBE, "%s:smrep5 %p %d %d %d",
				    __func__, ep, p2p_type, mpa_v2_params.ird,
				    mpa_v2_params.ord);
			}
		}

		memcpy(mpa->private_data, &mpa_v2_params,
			sizeof(struct mpa_v2_conn_params));

		if (ep->plen)
			memcpy(mpa->private_data +
				sizeof(struct mpa_v2_conn_params), pdata, plen);
	} else
		if (plen)
			memcpy(mpa->private_data, pdata, plen);

	m = m_getm(NULL, mpalen, M_NOWAIT, MT_DATA);
	if (m == NULL) {
		free(mpa, M_CXGBE);
		return (-ENOMEM);
	}
	m_copyback(m, 0, mpalen, (void *)mpa);
	free(mpa, M_CXGBE);


	ep->com.state = MPA_REP_SENT;
	ep->snd_seq += mpalen;
	err = -sosend(ep->com.so, NULL, NULL, m, NULL, MSG_DONTWAIT,
			ep->com.thread);
	CTR3(KTR_IW_CXGBE, "%s:smrepE %p %d", __func__, ep, err);
	return err;
}



static void close_complete_upcall(struct c4iw_ep *ep, int status)
{
	struct iw_cm_event event;

	CTR2(KTR_IW_CXGBE, "%s:ccuB %p", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	event.status = status;

	if (ep->com.cm_id) {

		CTR2(KTR_IW_CXGBE, "%s:ccu1 %1", __func__, ep);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		deref_cm_id(&ep->com);
		set_bit(CLOSE_UPCALL, &ep->com.history);
	}
	CTR2(KTR_IW_CXGBE, "%s:ccuE %p", __func__, ep);
}

static int
send_abort(struct c4iw_ep *ep)
{
	struct socket *so = ep->com.so;
	struct sockopt sopt;
	int rc;
	struct linger l;

	CTR5(KTR_IW_CXGBE, "%s ep %p so %p state %s tid %d", __func__, ep, so,
	    states[ep->com.state], ep->hwtid);

	l.l_onoff = 1;
	l.l_linger = 0;

	/* linger_time of 0 forces RST to be sent */
	sopt.sopt_dir = SOPT_SET;
	sopt.sopt_level = SOL_SOCKET;
	sopt.sopt_name = SO_LINGER;
	sopt.sopt_val = (caddr_t)&l;
	sopt.sopt_valsize = sizeof l;
	sopt.sopt_td = NULL;
	rc = -sosetopt(so, &sopt);
	if (rc != 0) {
		log(LOG_ERR, "%s: sosetopt(%p, linger = 0) failed with %d.\n",
		    __func__, so, rc);
	}

	uninit_iwarp_socket(so);
	soclose(so);
	set_bit(ABORT_CONN, &ep->com.history);

	/*
	 * TBD: iw_cxgbe driver should receive ABORT reply for every ABORT
	 * request it has sent. But the current TOE driver is not propagating
	 * this ABORT reply event (via do_abort_rpl) to iw_cxgbe. So as a work-
	 * around de-refererece 'ep' here instead of doing it in abort_rpl()
	 * handler(not yet implemented) of iw_cxgbe driver.
	 */
	release_ep_resources(ep);
	ep->com.state = DEAD;

	return (0);
}

static void peer_close_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	CTR2(KTR_IW_CXGBE, "%s:pcuB %p", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_DISCONNECT;

	if (ep->com.cm_id) {

		CTR2(KTR_IW_CXGBE, "%s:pcu1 %p", __func__, ep);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		set_bit(DISCONN_UPCALL, &ep->com.history);
	}
	CTR2(KTR_IW_CXGBE, "%s:pcuE %p", __func__, ep);
}

static void peer_abort_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	CTR2(KTR_IW_CXGBE, "%s:pauB %p", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CLOSE;
	event.status = -ECONNRESET;

	if (ep->com.cm_id) {

		CTR2(KTR_IW_CXGBE, "%s:pau1 %p", __func__, ep);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		deref_cm_id(&ep->com);
		set_bit(ABORT_UPCALL, &ep->com.history);
	}
	CTR2(KTR_IW_CXGBE, "%s:pauE %p", __func__, ep);
}

static void connect_reply_upcall(struct c4iw_ep *ep, int status)
{
	struct iw_cm_event event;

	CTR3(KTR_IW_CXGBE, "%s:cruB %p, status: %d", __func__, ep, status);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REPLY;
	event.status = ((status == -ECONNABORTED) || (status == -EPIPE)) ?
					-ECONNRESET : status;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;

	if ((status == 0) || (status == -ECONNREFUSED)) {

		if (!ep->tried_with_mpa_v1) {

			CTR2(KTR_IW_CXGBE, "%s:cru1 %p", __func__, ep);
			/* this means MPA_v2 is used */
			event.ord = ep->ird;
			event.ird = ep->ord;
			event.private_data_len = ep->plen -
				sizeof(struct mpa_v2_conn_params);
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message) +
				sizeof(struct mpa_v2_conn_params);
		} else {

			CTR2(KTR_IW_CXGBE, "%s:cru2 %p", __func__, ep);
			/* this means MPA_v1 is used */
			event.ord = c4iw_max_read_depth;
			event.ird = c4iw_max_read_depth;
			event.private_data_len = ep->plen;
			event.private_data = ep->mpa_pkt +
				sizeof(struct mpa_message);
		}
	}

	if (ep->com.cm_id) {

		CTR2(KTR_IW_CXGBE, "%s:cru3 %p", __func__, ep);
		set_bit(CONN_RPL_UPCALL, &ep->com.history);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
	}

	if(status == -ECONNABORTED) {

		CTR3(KTR_IW_CXGBE, "%s:cruE %p %d", __func__, ep, status);
		return;
	}

	if (status < 0) {

		CTR3(KTR_IW_CXGBE, "%s:cru4 %p %d", __func__, ep, status);
		deref_cm_id(&ep->com);
	}

	CTR2(KTR_IW_CXGBE, "%s:cruE %p", __func__, ep);
}

static int connect_request_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;
	int ret;

	CTR3(KTR_IW_CXGBE, "%s: ep %p, mpa_v1 %d", __func__, ep,
	    ep->tried_with_mpa_v1);

	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_CONNECT_REQUEST;
	event.local_addr = ep->com.local_addr;
	event.remote_addr = ep->com.remote_addr;
	event.provider_data = ep;

	if (!ep->tried_with_mpa_v1) {
		/* this means MPA_v2 is used */
		event.ord = ep->ord;
		event.ird = ep->ird;
		event.private_data_len = ep->plen -
			sizeof(struct mpa_v2_conn_params);
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message) +
			sizeof(struct mpa_v2_conn_params);
	} else {

		/* this means MPA_v1 is used. Send max supported */
		event.ord = c4iw_max_read_depth;
		event.ird = c4iw_max_read_depth;
		event.private_data_len = ep->plen;
		event.private_data = ep->mpa_pkt + sizeof(struct mpa_message);
	}

	c4iw_get_ep(&ep->com);
	ret = ep->parent_ep->com.cm_id->event_handler(ep->parent_ep->com.cm_id,
	    &event);
	if(ret) {
		CTR3(KTR_IW_CXGBE, "%s: ep %p, Failure while notifying event to"
			" IWCM, err:%d", __func__, ep, ret);
		c4iw_put_ep(&ep->com);
	} else
		/* Dereference parent_ep only in success case.
		 * In case of failure, parent_ep is dereferenced by the caller
		 * of process_mpa_request().
		 */
		c4iw_put_ep(&ep->parent_ep->com);

	set_bit(CONNREQ_UPCALL, &ep->com.history);
	return ret;
}

static void established_upcall(struct c4iw_ep *ep)
{
	struct iw_cm_event event;

	CTR2(KTR_IW_CXGBE, "%s:euB %p", __func__, ep);
	memset(&event, 0, sizeof(event));
	event.event = IW_CM_EVENT_ESTABLISHED;
	event.ird = ep->ord;
	event.ord = ep->ird;

	if (ep->com.cm_id) {

		CTR2(KTR_IW_CXGBE, "%s:eu1 %p", __func__, ep);
		ep->com.cm_id->event_handler(ep->com.cm_id, &event);
		set_bit(ESTAB_UPCALL, &ep->com.history);
	}
	CTR2(KTR_IW_CXGBE, "%s:euE %p", __func__, ep);
}


#define RELAXED_IRD_NEGOTIATION 1

/*
 * process_mpa_reply - process streaming mode MPA reply
 *
 * Returns:
 *
 * 0 upon success indicating a connect request was delivered to the ULP
 * or the mpa request is incomplete but valid so far.
 *
 * 1 if a failure requires the caller to close the connection.
 *
 * 2 if a failure requires the caller to abort the connection.
 */
static int process_mpa_reply(struct c4iw_ep *ep)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;
	u16 resp_ird, resp_ord;
	u8 rtr_mismatch = 0, insuff_ird = 0;
	struct c4iw_qp_attributes attrs = {0};
	enum c4iw_qp_attr_mask mask;
	int err;
	struct mbuf *top, *m;
	int flags = MSG_DONTWAIT;
	struct uio uio;
	int disconnect = 0;

	CTR2(KTR_IW_CXGBE, "%s:pmrB %p", __func__, ep);

	/*
	 * Stop mpa timer.  If it expired, then
	 * we ignore the MPA reply.  process_timeout()
	 * will abort the connection.
	 */
	if (STOP_EP_TIMER(ep))
		return 0;

	uio.uio_resid = 1000000;
	uio.uio_td = ep->com.thread;
	err = soreceive(ep->com.so, NULL, &uio, &top, NULL, &flags);

	if (err) {

		if (err == EWOULDBLOCK) {

			CTR2(KTR_IW_CXGBE, "%s:pmr1 %p", __func__, ep);
			START_EP_TIMER(ep);
			return 0;
		}
		err = -err;
		CTR2(KTR_IW_CXGBE, "%s:pmr2 %p", __func__, ep);
		goto err;
	}

	if (ep->com.so->so_rcv.sb_mb) {

		CTR2(KTR_IW_CXGBE, "%s:pmr3 %p", __func__, ep);
		printf("%s data after soreceive called! so %p sb_mb %p top %p\n",
		       __func__, ep->com.so, ep->com.so->so_rcv.sb_mb, top);
	}

	m = top;

	do {

		CTR2(KTR_IW_CXGBE, "%s:pmr4 %p", __func__, ep);
		/*
		 * If we get more than the supported amount of private data
		 * then we must fail this connection.
		 */
		if (ep->mpa_pkt_len + m->m_len > sizeof(ep->mpa_pkt)) {

			CTR3(KTR_IW_CXGBE, "%s:pmr5 %p %d", __func__, ep,
			    ep->mpa_pkt_len + m->m_len);
			err = (-EINVAL);
			goto err_stop_timer;
		}

		/*
		 * copy the new data into our accumulation buffer.
		 */
		m_copydata(m, 0, m->m_len, &(ep->mpa_pkt[ep->mpa_pkt_len]));
		ep->mpa_pkt_len += m->m_len;
		if (!m->m_next)
			m = m->m_nextpkt;
		else
			m = m->m_next;
	} while (m);

	m_freem(top);
	/*
	 * if we don't even have the mpa message, then bail.
	 */
	if (ep->mpa_pkt_len < sizeof(*mpa)) {
		return 0;
	}
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/* Validate MPA header. */
	if (mpa->revision > mpa_rev) {

		CTR4(KTR_IW_CXGBE, "%s:pmr6 %p %d %d", __func__, ep,
		    mpa->revision, mpa_rev);
		printk(KERN_ERR MOD "%s MPA version mismatch. Local = %d, "
				" Received = %d\n", __func__, mpa_rev, mpa->revision);
		err = -EPROTO;
		goto err_stop_timer;
	}

	if (memcmp(mpa->key, MPA_KEY_REP, sizeof(mpa->key))) {

		CTR2(KTR_IW_CXGBE, "%s:pmr7 %p", __func__, ep);
		err = -EPROTO;
		goto err_stop_timer;
	}

	plen = ntohs(mpa->private_data_size);

	/*
	 * Fail if there's too much private data.
	 */
	if (plen > MPA_MAX_PRIVATE_DATA) {

		CTR2(KTR_IW_CXGBE, "%s:pmr8 %p", __func__, ep);
		err = -EPROTO;
		goto err_stop_timer;
	}

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen)) {

		CTR2(KTR_IW_CXGBE, "%s:pmr9 %p", __func__, ep);
		STOP_EP_TIMER(ep);
		err = -EPROTO;
		goto err_stop_timer;
	}

	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 * We'll continue process when more data arrives.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen)) {

		CTR2(KTR_IW_CXGBE, "%s:pmra %p", __func__, ep);
		return 0;
	}

	if (mpa->flags & MPA_REJECT) {

		CTR2(KTR_IW_CXGBE, "%s:pmrb %p", __func__, ep);
		err = -ECONNREFUSED;
		goto err_stop_timer;
	}

	/*
	 * If we get here we have accumulated the entire mpa
	 * start reply message including private data. And
	 * the MPA header is valid.
	 */
	ep->com.state = FPDU_MODE;
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
	ep->mpa_attr.recv_marker_enabled = markers_enabled;
	ep->mpa_attr.xmit_marker_enabled = mpa->flags & MPA_MARKERS ? 1 : 0;
	ep->mpa_attr.version = mpa->revision;
	ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;

	if (mpa->revision == 2) {

		CTR2(KTR_IW_CXGBE, "%s:pmrc %p", __func__, ep);
		ep->mpa_attr.enhanced_rdma_conn =
			mpa->flags & MPA_ENHANCED_RDMA_CONN ? 1 : 0;

		if (ep->mpa_attr.enhanced_rdma_conn) {

			CTR2(KTR_IW_CXGBE, "%s:pmrd %p", __func__, ep);
			mpa_v2_params = (struct mpa_v2_conn_params *)
				(ep->mpa_pkt + sizeof(*mpa));
			resp_ird = ntohs(mpa_v2_params->ird) &
				MPA_V2_IRD_ORD_MASK;
			resp_ord = ntohs(mpa_v2_params->ord) &
				MPA_V2_IRD_ORD_MASK;

			/*
			 * This is a double-check. Ideally, below checks are
			 * not required since ird/ord stuff has been taken
			 * care of in c4iw_accept_cr
			 */
			if (ep->ird < resp_ord) {
				if (RELAXED_IRD_NEGOTIATION && resp_ord <=
				   ep->com.dev->rdev.adap->params.max_ordird_qp)
					ep->ird = resp_ord;
				else
					insuff_ird = 1;
			} else if (ep->ird > resp_ord) {
				ep->ird = resp_ord;
			}
			if (ep->ord > resp_ird) {
				if (RELAXED_IRD_NEGOTIATION)
					ep->ord = resp_ird;
				else
					insuff_ird = 1;
			}
			if (insuff_ird) {
				err = -ENOMEM;
				ep->ird = resp_ord;
				ep->ord = resp_ird;
			}

			if (ntohs(mpa_v2_params->ird) &
				MPA_V2_PEER2PEER_MODEL) {

				CTR2(KTR_IW_CXGBE, "%s:pmrf %p", __func__, ep);
				if (ntohs(mpa_v2_params->ord) &
					MPA_V2_RDMA_WRITE_RTR) {

					CTR2(KTR_IW_CXGBE, "%s:pmrg %p", __func__, ep);
					ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_RDMA_WRITE;
				}
				else if (ntohs(mpa_v2_params->ord) &
					MPA_V2_RDMA_READ_RTR) {

					CTR2(KTR_IW_CXGBE, "%s:pmrh %p", __func__, ep);
					ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_READ_REQ;
				}
			}
		}
	} else {

		CTR2(KTR_IW_CXGBE, "%s:pmri %p", __func__, ep);

		if (mpa->revision == 1) {

			CTR2(KTR_IW_CXGBE, "%s:pmrj %p", __func__, ep);

			if (peer2peer) {

				CTR2(KTR_IW_CXGBE, "%s:pmrk %p", __func__, ep);
				ep->mpa_attr.p2p_type = p2p_type;
			}
		}
	}

	if (set_tcpinfo(ep)) {

		CTR2(KTR_IW_CXGBE, "%s:pmrl %p", __func__, ep);
		printf("%s set_tcpinfo error\n", __func__);
		err = -ECONNRESET;
		goto err;
	}

	CTR6(KTR_IW_CXGBE, "%s - crc_enabled = %d, recv_marker_enabled = %d, "
	    "xmit_marker_enabled = %d, version = %d p2p_type = %d", __func__,
	    ep->mpa_attr.crc_enabled, ep->mpa_attr.recv_marker_enabled,
	    ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version,
	    ep->mpa_attr.p2p_type);

	/*
	 * If responder's RTR does not match with that of initiator, assign
	 * FW_RI_INIT_P2PTYPE_DISABLED in mpa attributes so that RTR is not
	 * generated when moving QP to RTS state.
	 * A TERM message will be sent after QP has moved to RTS state
	 */
	if ((ep->mpa_attr.version == 2) && peer2peer &&
		(ep->mpa_attr.p2p_type != p2p_type)) {

		CTR2(KTR_IW_CXGBE, "%s:pmrm %p", __func__, ep);
		ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;
		rtr_mismatch = 1;
	}


	//ep->ofld_txq = TOEPCB(ep->com.so)->ofld_txq;
	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = C4IW_QP_STATE_RTS;

	mask = C4IW_QP_ATTR_NEXT_STATE |
		C4IW_QP_ATTR_LLP_STREAM_HANDLE | C4IW_QP_ATTR_MPA_ATTR |
		C4IW_QP_ATTR_MAX_IRD | C4IW_QP_ATTR_MAX_ORD;

	/* bind QP and TID with INIT_WR */
	err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp, mask, &attrs, 1);

	if (err) {

		CTR2(KTR_IW_CXGBE, "%s:pmrn %p", __func__, ep);
		goto err;
	}

	/*
	 * If responder's RTR requirement did not match with what initiator
	 * supports, generate TERM message
	 */
	if (rtr_mismatch) {

		CTR2(KTR_IW_CXGBE, "%s:pmro %p", __func__, ep);
		printk(KERN_ERR "%s: RTR mismatch, sending TERM\n", __func__);
		attrs.layer_etype = LAYER_MPA | DDP_LLP;
		attrs.ecode = MPA_NOMATCH_RTR;
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		attrs.send_term = 1;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
			C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		err = -ENOMEM;
		disconnect = 1;
		goto out;
	}

	/*
	 * Generate TERM if initiator IRD is not sufficient for responder
	 * provided ORD. Currently, we do the same behaviour even when
	 * responder provided IRD is also not sufficient as regards to
	 * initiator ORD.
	 */
	if (insuff_ird) {

		CTR2(KTR_IW_CXGBE, "%s:pmrp %p", __func__, ep);
		printk(KERN_ERR "%s: Insufficient IRD, sending TERM\n",
				__func__);
		attrs.layer_etype = LAYER_MPA | DDP_LLP;
		attrs.ecode = MPA_INSUFF_IRD;
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		attrs.send_term = 1;
		err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp,
			C4IW_QP_ATTR_NEXT_STATE, &attrs, 1);
		err = -ENOMEM;
		disconnect = 1;
		goto out;
	}
	goto out;
err_stop_timer:
	STOP_EP_TIMER(ep);
err:
	disconnect = 2;
out:
	connect_reply_upcall(ep, err);
	CTR2(KTR_IW_CXGBE, "%s:pmrE %p", __func__, ep);
	return disconnect;
}

/*
 * process_mpa_request - process streaming mode MPA request
 *
 * Returns:
 *
 * 0 upon success indicating a connect request was delivered to the ULP
 * or the mpa request is incomplete but valid so far.
 *
 * 1 if a failure requires the caller to close the connection.
 *
 * 2 if a failure requires the caller to abort the connection.
 */
static int
process_mpa_request(struct c4iw_ep *ep)
{
	struct mpa_message *mpa;
	struct mpa_v2_conn_params *mpa_v2_params;
	u16 plen;
	int flags = MSG_DONTWAIT;
	int rc;
	struct iovec iov;
	struct uio uio;
	enum c4iw_ep_state state = ep->com.state;

	CTR3(KTR_IW_CXGBE, "%s: ep %p, state %s", __func__, ep, states[state]);

	if (state != MPA_REQ_WAIT)
		return 0;

	iov.iov_base = &ep->mpa_pkt[ep->mpa_pkt_len];
	iov.iov_len = sizeof(ep->mpa_pkt) - ep->mpa_pkt_len;
	uio.uio_iov = &iov;
	uio.uio_iovcnt = 1;
	uio.uio_offset = 0;
	uio.uio_resid = sizeof(ep->mpa_pkt) - ep->mpa_pkt_len;
	uio.uio_segflg = UIO_SYSSPACE;
	uio.uio_rw = UIO_READ;
	uio.uio_td = NULL; /* uio.uio_td = ep->com.thread; */

	rc = soreceive(ep->com.so, NULL, &uio, NULL, NULL, &flags);
	if (rc == EAGAIN)
		return 0;
	else if (rc)
		goto err_stop_timer;

	KASSERT(uio.uio_offset > 0, ("%s: sorecieve on so %p read no data",
	    __func__, ep->com.so));
	ep->mpa_pkt_len += uio.uio_offset;

	/*
	 * If we get more than the supported amount of private data then we must
	 * fail this connection.  XXX: check so_rcv->sb_cc, or peek with another
	 * soreceive, or increase the size of mpa_pkt by 1 and abort if the last
	 * byte is filled by the soreceive above.
	 */

	/* Don't even have the MPA message.  Wait for more data to arrive. */
	if (ep->mpa_pkt_len < sizeof(*mpa))
		return 0;
	mpa = (struct mpa_message *) ep->mpa_pkt;

	/*
	 * Validate MPA Header.
	 */
	if (mpa->revision > mpa_rev) {
		log(LOG_ERR, "%s: MPA version mismatch. Local = %d,"
		    " Received = %d\n", __func__, mpa_rev, mpa->revision);
		goto err_stop_timer;
	}

	if (memcmp(mpa->key, MPA_KEY_REQ, sizeof(mpa->key)))
		goto err_stop_timer;

	/*
	 * Fail if there's too much private data.
	 */
	plen = ntohs(mpa->private_data_size);
	if (plen > MPA_MAX_PRIVATE_DATA)
		goto err_stop_timer;

	/*
	 * If plen does not account for pkt size
	 */
	if (ep->mpa_pkt_len > (sizeof(*mpa) + plen))
		goto err_stop_timer;

	ep->plen = (u8) plen;

	/*
	 * If we don't have all the pdata yet, then bail.
	 */
	if (ep->mpa_pkt_len < (sizeof(*mpa) + plen))
		return 0;

	/*
	 * If we get here we have accumulated the entire mpa
	 * start reply message including private data.
	 */
	ep->mpa_attr.initiator = 0;
	ep->mpa_attr.crc_enabled = (mpa->flags & MPA_CRC) | crc_enabled ? 1 : 0;
	ep->mpa_attr.recv_marker_enabled = markers_enabled;
	ep->mpa_attr.xmit_marker_enabled = mpa->flags & MPA_MARKERS ? 1 : 0;
	ep->mpa_attr.version = mpa->revision;
	if (mpa->revision == 1)
		ep->tried_with_mpa_v1 = 1;
	ep->mpa_attr.p2p_type = FW_RI_INIT_P2PTYPE_DISABLED;

	if (mpa->revision == 2) {
		ep->mpa_attr.enhanced_rdma_conn =
		    mpa->flags & MPA_ENHANCED_RDMA_CONN ? 1 : 0;
		if (ep->mpa_attr.enhanced_rdma_conn) {
			mpa_v2_params = (struct mpa_v2_conn_params *)
				(ep->mpa_pkt + sizeof(*mpa));
			ep->ird = ntohs(mpa_v2_params->ird) &
				MPA_V2_IRD_ORD_MASK;
			ep->ird = min_t(u32, ep->ird,
					cur_max_read_depth(ep->com.dev));
			ep->ord = ntohs(mpa_v2_params->ord) &
				MPA_V2_IRD_ORD_MASK;
			ep->ord = min_t(u32, ep->ord,
					cur_max_read_depth(ep->com.dev));
			CTR3(KTR_IW_CXGBE, "%s initiator ird %u ord %u",
				 __func__, ep->ird, ep->ord);
			if (ntohs(mpa_v2_params->ird) & MPA_V2_PEER2PEER_MODEL)
				if (peer2peer) {
					if (ntohs(mpa_v2_params->ord) &
							MPA_V2_RDMA_WRITE_RTR)
						ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_RDMA_WRITE;
					else if (ntohs(mpa_v2_params->ord) &
							MPA_V2_RDMA_READ_RTR)
						ep->mpa_attr.p2p_type =
						FW_RI_INIT_P2PTYPE_READ_REQ;
				}
		}
	} else if (mpa->revision == 1 && peer2peer)
		ep->mpa_attr.p2p_type = p2p_type;

	if (set_tcpinfo(ep))
		goto err_stop_timer;

	CTR5(KTR_IW_CXGBE, "%s: crc_enabled = %d, recv_marker_enabled = %d, "
	    "xmit_marker_enabled = %d, version = %d", __func__,
	    ep->mpa_attr.crc_enabled, ep->mpa_attr.recv_marker_enabled,
	    ep->mpa_attr.xmit_marker_enabled, ep->mpa_attr.version);

	ep->com.state = MPA_REQ_RCVD;
	STOP_EP_TIMER(ep);

	/* drive upcall */
	if (ep->parent_ep->com.state != DEAD)
		if (connect_request_upcall(ep))
			goto err_out;
	return 0;

err_stop_timer:
	STOP_EP_TIMER(ep);
err_out:
	return 2;
}

/*
 * Upcall from the adapter indicating data has been transmitted.
 * For us its just the single MPA request or reply.  We can now free
 * the skb holding the mpa message.
 */
int c4iw_reject_cr(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	int err;
	struct c4iw_ep *ep = to_ep(cm_id);
	int abort = 0;

	mutex_lock(&ep->com.mutex);
	CTR2(KTR_IW_CXGBE, "%s:crcB %p", __func__, ep);

	if ((ep->com.state == DEAD) ||
			(ep->com.state != MPA_REQ_RCVD)) {

		CTR2(KTR_IW_CXGBE, "%s:crc1 %p", __func__, ep);
		mutex_unlock(&ep->com.mutex);
		c4iw_put_ep(&ep->com);
		return -ECONNRESET;
	}
	set_bit(ULP_REJECT, &ep->com.history);

	if (mpa_rev == 0) {

		CTR2(KTR_IW_CXGBE, "%s:crc2 %p", __func__, ep);
		abort = 1;
	}
	else {

		CTR2(KTR_IW_CXGBE, "%s:crc3 %p", __func__, ep);
		abort = send_mpa_reject(ep, pdata, pdata_len);
	}
	STOP_EP_TIMER(ep);
	err = c4iw_ep_disconnect(ep, abort != 0, GFP_KERNEL);
	mutex_unlock(&ep->com.mutex);
	c4iw_put_ep(&ep->com);
	CTR3(KTR_IW_CXGBE, "%s:crc4 %p, err: %d", __func__, ep, err);
	return 0;
}

int c4iw_accept_cr(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	int err;
	struct c4iw_qp_attributes attrs = {0};
	enum c4iw_qp_attr_mask mask;
	struct c4iw_ep *ep = to_ep(cm_id);
	struct c4iw_dev *h = to_c4iw_dev(cm_id->device);
	struct c4iw_qp *qp = get_qhp(h, conn_param->qpn);
	int abort = 0;

	mutex_lock(&ep->com.mutex);
	CTR2(KTR_IW_CXGBE, "%s:cacB %p", __func__, ep);

	if ((ep->com.state == DEAD) ||
			(ep->com.state != MPA_REQ_RCVD)) {

		CTR2(KTR_IW_CXGBE, "%s:cac1 %p", __func__, ep);
		err = -ECONNRESET;
		goto err_out;
	}

	BUG_ON(!qp);

	set_bit(ULP_ACCEPT, &ep->com.history);

	if ((conn_param->ord > c4iw_max_read_depth) ||
		(conn_param->ird > c4iw_max_read_depth)) {

		CTR2(KTR_IW_CXGBE, "%s:cac2 %p", __func__, ep);
		err = -EINVAL;
		goto err_abort;
	}

	if (ep->mpa_attr.version == 2 && ep->mpa_attr.enhanced_rdma_conn) {

		CTR2(KTR_IW_CXGBE, "%s:cac3 %p", __func__, ep);

		if (conn_param->ord > ep->ird) {
			if (RELAXED_IRD_NEGOTIATION) {
				conn_param->ord = ep->ird;
			} else {
				ep->ird = conn_param->ird;
				ep->ord = conn_param->ord;
				send_mpa_reject(ep, conn_param->private_data,
						conn_param->private_data_len);
				err = -ENOMEM;
				goto err_abort;
			}
		}
		if (conn_param->ird < ep->ord) {
			if (RELAXED_IRD_NEGOTIATION &&
			    ep->ord <= h->rdev.adap->params.max_ordird_qp) {
				conn_param->ird = ep->ord;
			} else {
				err = -ENOMEM;
				goto err_abort;
			}
		}
	}
	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (ep->mpa_attr.version == 1) {
		if (peer2peer && ep->ird == 0)
			ep->ird = 1;
	} else {
		if (peer2peer &&
		    (ep->mpa_attr.p2p_type != FW_RI_INIT_P2PTYPE_DISABLED) &&
		    (p2p_type == FW_RI_INIT_P2PTYPE_READ_REQ) && ep->ird == 0)
			ep->ird = 1;
	}

	CTR4(KTR_IW_CXGBE, "%s %d ird %d ord %d", __func__, __LINE__,
			ep->ird, ep->ord);

	ep->com.cm_id = cm_id;
	ref_cm_id(&ep->com);
	ep->com.qp = qp;
	ref_qp(ep);
	//ep->ofld_txq = TOEPCB(ep->com.so)->ofld_txq;

	/* bind QP to EP and move to RTS */
	attrs.mpa_attr = ep->mpa_attr;
	attrs.max_ird = ep->ird;
	attrs.max_ord = ep->ord;
	attrs.llp_stream_handle = ep;
	attrs.next_state = C4IW_QP_STATE_RTS;

	/* bind QP and TID with INIT_WR */
	mask = C4IW_QP_ATTR_NEXT_STATE |
		C4IW_QP_ATTR_LLP_STREAM_HANDLE |
		C4IW_QP_ATTR_MPA_ATTR |
		C4IW_QP_ATTR_MAX_IRD |
		C4IW_QP_ATTR_MAX_ORD;

	err = c4iw_modify_qp(ep->com.qp->rhp, ep->com.qp, mask, &attrs, 1);
	if (err) {
		CTR3(KTR_IW_CXGBE, "%s:caca %p, err: %d", __func__, ep, err);
		goto err_defef_cm_id;
	}

	err = send_mpa_reply(ep, conn_param->private_data,
			conn_param->private_data_len);
	if (err) {
		CTR3(KTR_IW_CXGBE, "%s:cacb %p, err: %d", __func__, ep, err);
		goto err_defef_cm_id;
	}

	ep->com.state = FPDU_MODE;
	established_upcall(ep);
	mutex_unlock(&ep->com.mutex);
	c4iw_put_ep(&ep->com);
	CTR2(KTR_IW_CXGBE, "%s:cacE %p", __func__, ep);
	return 0;
err_defef_cm_id:
	deref_cm_id(&ep->com);
err_abort:
	abort = 1;
err_out:
	if (abort)
		c4iw_ep_disconnect(ep, 1, GFP_KERNEL);
	mutex_unlock(&ep->com.mutex);
	c4iw_put_ep(&ep->com);
	CTR2(KTR_IW_CXGBE, "%s:cacE err %p", __func__, ep);
	return err;
}

static int
c4iw_sock_create(struct sockaddr_storage *laddr, struct socket **so)
{
	int ret;
	int size, on;
	struct socket *sock = NULL;
	struct sockopt sopt;

	ret = sock_create_kern(laddr->ss_family,
			SOCK_STREAM, IPPROTO_TCP, &sock);
	if (ret) {
		CTR2(KTR_IW_CXGBE, "%s:Failed to create TCP socket. err %d",
				__func__, ret);
		return ret;
	}

	if (reuseaddr) {
		bzero(&sopt, sizeof(struct sockopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = SOL_SOCKET;
		sopt.sopt_name = SO_REUSEADDR;
		on = 1;
		sopt.sopt_val = &on;
		sopt.sopt_valsize = sizeof(on);
		ret = -sosetopt(sock, &sopt);
		if (ret != 0) {
			log(LOG_ERR, "%s: sosetopt(%p, SO_REUSEADDR) "
				"failed with %d.\n", __func__, sock, ret);
		}
		bzero(&sopt, sizeof(struct sockopt));
		sopt.sopt_dir = SOPT_SET;
		sopt.sopt_level = SOL_SOCKET;
		sopt.sopt_name = SO_REUSEPORT;
		on = 1;
		sopt.sopt_val = &on;
		sopt.sopt_valsize = sizeof(on);
		ret = -sosetopt(sock, &sopt);
		if (ret != 0) {
			log(LOG_ERR, "%s: sosetopt(%p, SO_REUSEPORT) "
				"failed with %d.\n", __func__, sock, ret);
		}
	}

	ret = -sobind(sock, (struct sockaddr *)laddr, curthread);
	if (ret) {
		CTR2(KTR_IW_CXGBE, "%s:Failed to bind socket. err %p",
				__func__, ret);
		sock_release(sock);
		return ret;
	}

	size = laddr->ss_family == AF_INET6 ?
		sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	ret = sock_getname(sock, (struct sockaddr *)laddr, &size, 0);
	if (ret) {
		CTR2(KTR_IW_CXGBE, "%s:sock_getname failed. err %p",
				__func__, ret);
		sock_release(sock);
		return ret;
	}

	*so = sock;
	return 0;
}

int c4iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	int err = 0;
	struct c4iw_dev *dev = to_c4iw_dev(cm_id->device);
	struct c4iw_ep *ep = NULL;
	struct ifnet    *nh_ifp;        /* Logical egress interface */
#ifdef VIMAGE
	struct rdma_cm_id *rdma_id = (struct rdma_cm_id*)cm_id->context;
	struct vnet *vnet = rdma_id->route.addr.dev_addr.net;
#endif

	CTR2(KTR_IW_CXGBE, "%s:ccB %p", __func__, cm_id);


	if ((conn_param->ord > c4iw_max_read_depth) ||
		(conn_param->ird > c4iw_max_read_depth)) {

		CTR2(KTR_IW_CXGBE, "%s:cc1 %p", __func__, cm_id);
		err = -EINVAL;
		goto out;
	}
	ep = alloc_ep(sizeof(*ep), GFP_KERNEL);
	cm_id->provider_data = ep;

	init_timer(&ep->timer);
	ep->plen = conn_param->private_data_len;

	if (ep->plen) {

		CTR2(KTR_IW_CXGBE, "%s:cc3 %p", __func__, ep);
		memcpy(ep->mpa_pkt + sizeof(struct mpa_message),
				conn_param->private_data, ep->plen);
	}
	ep->ird = conn_param->ird;
	ep->ord = conn_param->ord;

	if (peer2peer && ep->ord == 0) {

		CTR2(KTR_IW_CXGBE, "%s:cc4 %p", __func__, ep);
		ep->ord = 1;
	}

	ep->com.dev = dev;
	ep->com.cm_id = cm_id;
	ref_cm_id(&ep->com);
	ep->com.qp = get_qhp(dev, conn_param->qpn);

	if (!ep->com.qp) {

		CTR2(KTR_IW_CXGBE, "%s:cc5 %p", __func__, ep);
		err = -EINVAL;
		goto fail;
	}
	ref_qp(ep);
	ep->com.thread = curthread;

	CURVNET_SET(vnet);
	err = get_ifnet_from_raddr(&cm_id->remote_addr, &nh_ifp);
	CURVNET_RESTORE();

	if (err) {

		CTR2(KTR_IW_CXGBE, "%s:cc7 %p", __func__, ep);
		printk(KERN_ERR MOD "%s - cannot find route.\n", __func__);
		err = EHOSTUNREACH;
		return err;
	}

	if (!(nh_ifp->if_capenable & IFCAP_TOE) ||
	    TOEDEV(nh_ifp) == NULL) {
		err = -ENOPROTOOPT;
		goto fail;
	}
	ep->com.state = CONNECTING;
	ep->tos = 0;
	ep->com.local_addr = cm_id->local_addr;
	ep->com.remote_addr = cm_id->remote_addr;

	err = c4iw_sock_create(&cm_id->local_addr, &ep->com.so);
	if (err)
		goto fail;

	setiwsockopt(ep->com.so);
	init_iwarp_socket(ep->com.so, &ep->com);
	err = -soconnect(ep->com.so, (struct sockaddr *)&ep->com.remote_addr,
		ep->com.thread);
	if (err)
		goto fail_free_so;
	CTR2(KTR_IW_CXGBE, "%s:ccE, ep %p", __func__, ep);
	return 0;

fail_free_so:
	uninit_iwarp_socket(ep->com.so);
	ep->com.state = DEAD;
	sock_release(ep->com.so);
fail:
	deref_cm_id(&ep->com);
	c4iw_put_ep(&ep->com);
	ep = NULL;
out:
	CTR2(KTR_IW_CXGBE, "%s:ccE Error %d", __func__, err);
	return err;
}

/*
 * iwcm->create_listen.  Returns -errno on failure.
 */
int
c4iw_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	struct c4iw_dev *dev = to_c4iw_dev(cm_id->device);
	struct c4iw_listen_ep *lep = NULL;
	struct listen_port_info *port_info = NULL;
	int rc = 0;

	CTR3(KTR_IW_CXGBE, "%s: cm_id %p, backlog %s", __func__, cm_id,
			backlog);
	lep = alloc_ep(sizeof(*lep), GFP_KERNEL);
	lep->com.cm_id = cm_id;
	ref_cm_id(&lep->com);
	lep->com.dev = dev;
	lep->backlog = backlog;
	lep->com.local_addr = cm_id->local_addr;
	lep->com.thread = curthread;
	cm_id->provider_data = lep;
	lep->com.state = LISTEN;

	/* In case of INDADDR_ANY, ibcore creates cmid for each device and
	 * invokes iw_cxgbe listener callbacks assuming that iw_cxgbe creates
	 * HW listeners for each device seperately. But toecore expects single
	 * solisten() call with INADDR_ANY address to create HW listeners on
	 * all devices for a given port number. So iw_cxgbe driver calls
	 * solisten() only once for INADDR_ANY(usually done at first time
	 * listener callback from ibcore). And all the subsequent INADDR_ANY
	 * listener callbacks from ibcore(for the same port address) do not
	 * invoke solisten() as first listener callback has already created
	 * listeners for all other devices(via solisten).
	 */
	if (c4iw_any_addr((struct sockaddr *)&lep->com.local_addr)) {
		port_info = add_ep_to_listenlist(lep);
		/* skip solisten() if refcnt > 1, as the listeners were
		 * alredy created by 'Master lep'
		 */
		if (port_info->refcnt > 1) {
			/* As there will be only one listener socket for a TCP
			 * port, copy Master lep's socket pointer to other lep's
			 * that are belonging to same TCP port.
			 */
			struct c4iw_listen_ep *head_lep =
					container_of(port_info->lep_list.next,
					struct c4iw_listen_ep, listen_ep_list);
			lep->com.so =  head_lep->com.so;
			goto out;
		}
	}
	rc = c4iw_sock_create(&cm_id->local_addr, &lep->com.so);
	if (rc) {
		CTR2(KTR_IW_CXGBE, "%s:Failed to create socket. err %d",
				__func__, rc);
		goto fail;
	}

	rc = -solisten(lep->com.so, backlog, curthread);
	if (rc) {
		CTR3(KTR_IW_CXGBE, "%s:Failed to listen on sock:%p. err %d",
				__func__, lep->com.so, rc);
		goto fail_free_so;
	}
	init_iwarp_socket(lep->com.so, &lep->com);
out:
	return 0;

fail_free_so:
	sock_release(lep->com.so);
fail:
	if (port_info)
		rem_ep_from_listenlist(lep);
	deref_cm_id(&lep->com);
	c4iw_put_ep(&lep->com);
	return rc;
}

int
c4iw_destroy_listen(struct iw_cm_id *cm_id)
{
	struct c4iw_listen_ep *lep = to_listen_ep(cm_id);

	mutex_lock(&lep->com.mutex);
	CTR3(KTR_IW_CXGBE, "%s: cm_id %p, state %s", __func__, cm_id,
	    states[lep->com.state]);

	lep->com.state = DEAD;
	if (c4iw_any_addr((struct sockaddr *)&lep->com.local_addr)) {
		/* if no refcount then close listen socket */
		if (!rem_ep_from_listenlist(lep))
			close_socket(lep->com.so);
	} else
		close_socket(lep->com.so);
	deref_cm_id(&lep->com);
	mutex_unlock(&lep->com.mutex);
	c4iw_put_ep(&lep->com);
	return 0;
}

int __c4iw_ep_disconnect(struct c4iw_ep *ep, int abrupt, gfp_t gfp)
{
	int ret;
	mutex_lock(&ep->com.mutex);
	ret = c4iw_ep_disconnect(ep, abrupt, gfp);
	mutex_unlock(&ep->com.mutex);
	return ret;
}

int c4iw_ep_disconnect(struct c4iw_ep *ep, int abrupt, gfp_t gfp)
{
	int ret = 0;
	int close = 0;
	int fatal = 0;
	struct c4iw_rdev *rdev;


	CTR2(KTR_IW_CXGBE, "%s:cedB %p", __func__, ep);

	rdev = &ep->com.dev->rdev;

	if (c4iw_fatal_error(rdev)) {

		CTR2(KTR_IW_CXGBE, "%s:ced1 %p", __func__, ep);
		fatal = 1;
		close_complete_upcall(ep, -ECONNRESET);
		send_abort(ep);
		ep->com.state = DEAD;
	}
	CTR3(KTR_IW_CXGBE, "%s:ced2 %p %s", __func__, ep,
	    states[ep->com.state]);

	/*
	 * Ref the ep here in case we have fatal errors causing the
	 * ep to be released and freed.
	 */
	c4iw_get_ep(&ep->com);
	switch (ep->com.state) {

		case MPA_REQ_WAIT:
		case MPA_REQ_SENT:
		case MPA_REQ_RCVD:
		case MPA_REP_SENT:
		case FPDU_MODE:
			close = 1;
			if (abrupt)
				ep->com.state = ABORTING;
			else {
				ep->com.state = CLOSING;
				START_EP_TIMER(ep);
			}
			set_bit(CLOSE_SENT, &ep->com.flags);
			break;

		case CLOSING:

			if (!test_and_set_bit(CLOSE_SENT, &ep->com.flags)) {

				close = 1;
				if (abrupt) {
					STOP_EP_TIMER(ep);
					ep->com.state = ABORTING;
				} else
					ep->com.state = MORIBUND;
			}
			break;

		case MORIBUND:
		case ABORTING:
		case DEAD:
			CTR3(KTR_IW_CXGBE,
			    "%s ignoring disconnect ep %p state %u", __func__,
			    ep, ep->com.state);
			break;

		default:
			BUG();
			break;
	}


	if (close) {

		CTR2(KTR_IW_CXGBE, "%s:ced3 %p", __func__, ep);

		if (abrupt) {

			CTR2(KTR_IW_CXGBE, "%s:ced4 %p", __func__, ep);
			set_bit(EP_DISC_ABORT, &ep->com.history);
			close_complete_upcall(ep, -ECONNRESET);
			ret = send_abort(ep);
			if (ret)
				fatal = 1;
		} else {

			CTR2(KTR_IW_CXGBE, "%s:ced5 %p", __func__, ep);
			set_bit(EP_DISC_CLOSE, &ep->com.history);

			if (!ep->parent_ep)
				ep->com.state = MORIBUND;

			CURVNET_SET(ep->com.so->so_vnet);
			sodisconnect(ep->com.so);
			CURVNET_RESTORE();
		}

	}

	if (fatal) {
		set_bit(EP_DISC_FAIL, &ep->com.history);
		if (!abrupt) {
			STOP_EP_TIMER(ep);
			close_complete_upcall(ep, -EIO);
		}
		if (ep->com.qp) {
			struct c4iw_qp_attributes attrs = {0};

			attrs.next_state = C4IW_QP_STATE_ERROR;
			ret = c4iw_modify_qp(ep->com.dev, ep->com.qp,
						C4IW_QP_ATTR_NEXT_STATE,
						&attrs, 1);
			if (ret) {
				CTR2(KTR_IW_CXGBE, "%s:ced7 %p", __func__, ep);
				printf("%s - qp <- error failed!\n", __func__);
			}
		}
		release_ep_resources(ep);
		ep->com.state = DEAD;
		CTR2(KTR_IW_CXGBE, "%s:ced6 %p", __func__, ep);
	}
	c4iw_put_ep(&ep->com);
	CTR2(KTR_IW_CXGBE, "%s:cedE %p", __func__, ep);
	return ret;
}

#ifdef C4IW_EP_REDIRECT
int c4iw_ep_redirect(void *ctx, struct dst_entry *old, struct dst_entry *new,
		struct l2t_entry *l2t)
{
	struct c4iw_ep *ep = ctx;

	if (ep->dst != old)
		return 0;

	PDBG("%s ep %p redirect to dst %p l2t %p\n", __func__, ep, new,
			l2t);
	dst_hold(new);
	cxgb4_l2t_release(ep->l2t);
	ep->l2t = l2t;
	dst_release(old);
	ep->dst = new;
	return 1;
}
#endif



static void ep_timeout(unsigned long arg)
{
	struct c4iw_ep *ep = (struct c4iw_ep *)arg;

	if (!test_and_set_bit(TIMEOUT, &ep->com.flags)) {

		/*
		 * Only insert if it is not already on the list.
		 */
		if (!(ep->com.ep_events & C4IW_EVENT_TIMEOUT)) {
			CTR2(KTR_IW_CXGBE, "%s:et1 %p", __func__, ep);
			add_ep_to_req_list(ep, C4IW_EVENT_TIMEOUT);
		}
	}
}

static int fw6_wr_rpl(struct adapter *sc, const __be64 *rpl)
{
	uint64_t val = be64toh(*rpl);
	int ret;
	struct c4iw_wr_wait *wr_waitp;

	ret = (int)((val >> 8) & 0xff);
	wr_waitp = (struct c4iw_wr_wait *)rpl[1];
	CTR3(KTR_IW_CXGBE, "%s wr_waitp %p ret %u", __func__, wr_waitp, ret);
	if (wr_waitp)
		c4iw_wake_up(wr_waitp, ret ? -ret : 0);

	return (0);
}

static int fw6_cqe_handler(struct adapter *sc, const __be64 *rpl)
{
	struct cqe_list_entry *cle;
	unsigned long flag;

	cle = malloc(sizeof(*cle), M_CXGBE, M_NOWAIT);
	cle->rhp = sc->iwarp_softc;
	cle->err_cqe = *(const struct t4_cqe *)(&rpl[0]);

	spin_lock_irqsave(&err_cqe_lock, flag);
	list_add_tail(&cle->entry, &err_cqe_list);
	queue_work(c4iw_taskq, &c4iw_task);
	spin_unlock_irqrestore(&err_cqe_lock, flag);

	return (0);
}

static int
process_terminate(struct c4iw_ep *ep)
{
	struct c4iw_qp_attributes attrs = {0};

	CTR2(KTR_IW_CXGBE, "%s:tB %p %d", __func__, ep);

	if (ep && ep->com.qp) {

		printk(KERN_WARNING MOD "TERM received tid %u qpid %u\n",
				ep->hwtid, ep->com.qp->wq.sq.qid);
		attrs.next_state = C4IW_QP_STATE_TERMINATE;
		c4iw_modify_qp(ep->com.dev, ep->com.qp, C4IW_QP_ATTR_NEXT_STATE, &attrs,
				1);
	} else
		printk(KERN_WARNING MOD "TERM received tid %u no ep/qp\n",
								ep->hwtid);
	CTR2(KTR_IW_CXGBE, "%s:tE %p %d", __func__, ep);

	return 0;
}

int __init c4iw_cm_init(void)
{

	t4_register_cpl_handler(CPL_RDMA_TERMINATE, terminate);
	t4_register_fw_msg_handler(FW6_TYPE_WR_RPL, fw6_wr_rpl);
	t4_register_fw_msg_handler(FW6_TYPE_CQE, fw6_cqe_handler);
	t4_register_an_handler(c4iw_ev_handler);

	TAILQ_INIT(&req_list);
	spin_lock_init(&req_lock);
	INIT_LIST_HEAD(&err_cqe_list);
	spin_lock_init(&err_cqe_lock);

	INIT_WORK(&c4iw_task, process_req);

	c4iw_taskq = create_singlethread_workqueue("iw_cxgbe");
	if (!c4iw_taskq)
		return -ENOMEM;

	return 0;
}

void __exit c4iw_cm_term(void)
{
	WARN_ON(!TAILQ_EMPTY(&req_list));
	WARN_ON(!list_empty(&err_cqe_list));
	flush_workqueue(c4iw_taskq);
	destroy_workqueue(c4iw_taskq);

	t4_register_cpl_handler(CPL_RDMA_TERMINATE, NULL);
	t4_register_fw_msg_handler(FW6_TYPE_WR_RPL, NULL);
	t4_register_fw_msg_handler(FW6_TYPE_CQE, NULL);
	t4_register_an_handler(NULL);
}
#endif
