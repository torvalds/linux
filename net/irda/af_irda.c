/*********************************************************************
 *
 * Filename:      af_irda.c
 * Version:       0.9
 * Description:   IrDA sockets implementation
 * Status:        Stable
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun May 31 10:12:43 1998
 * Modified at:   Sat Dec 25 21:10:23 1999
 * Modified by:   Dag Brattli <dag@brattli.net>
 * Sources:       af_netroom.c, af_ax25.c, af_rose.c, af_x25.c etc.
 *
 *     Copyright (c) 1999 Dag Brattli <dagb@cs.uit.no>
 *     Copyright (c) 1999-2003 Jean Tourrilhes <jt@hpl.hp.com>
 *     All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 *     Linux-IrDA now supports four different types of IrDA sockets:
 *
 *     o SOCK_STREAM:    TinyTP connections with SAR disabled. The
 *                       max SDU size is 0 for conn. of this type
 *     o SOCK_SEQPACKET: TinyTP connections with SAR enabled. TTP may
 *                       fragment the messages, but will preserve
 *                       the message boundaries
 *     o SOCK_DGRAM:     IRDAPROTO_UNITDATA: TinyTP connections with Unitdata
 *                       (unreliable) transfers
 *                       IRDAPROTO_ULTRA: Connectionless and unreliable data
 *
 ********************************************************************/

#include <linux/capability.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/irda.h>
#include <linux/poll.h>

#include <asm/ioctls.h>		/* TIOCOUTQ, TIOCINQ */
#include <asm/uaccess.h>

#include <net/sock.h>
#include <net/tcp_states.h>

#include <net/irda/af_irda.h>

static int irda_create(struct net *net, struct socket *sock, int protocol, int kern);

static const struct proto_ops irda_stream_ops;
static const struct proto_ops irda_seqpacket_ops;
static const struct proto_ops irda_dgram_ops;

#ifdef CONFIG_IRDA_ULTRA
static const struct proto_ops irda_ultra_ops;
#define ULTRA_MAX_DATA 382
#endif /* CONFIG_IRDA_ULTRA */

#define IRDA_MAX_HEADER (TTP_MAX_HEADER)

/*
 * Function irda_data_indication (instance, sap, skb)
 *
 *    Received some data from TinyTP. Just queue it on the receive queue
 *
 */
static int irda_data_indication(void *instance, void *sap, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;
	int err;

	IRDA_DEBUG(3, "%s()\n", __func__);

	self = instance;
	sk = instance;

	err = sock_queue_rcv_skb(sk, skb);
	if (err) {
		IRDA_DEBUG(1, "%s(), error: no more mem!\n", __func__);
		self->rx_flow = FLOW_STOP;

		/* When we return error, TTP will need to requeue the skb */
		return err;
	}

	return 0;
}

/*
 * Function irda_disconnect_indication (instance, sap, reason, skb)
 *
 *    Connection has been closed. Check reason to find out why
 *
 */
static void irda_disconnect_indication(void *instance, void *sap,
				       LM_REASON reason, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	self = instance;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	/* Don't care about it, but let's not leak it */
	if(skb)
		dev_kfree_skb(skb);

	sk = instance;
	if (sk == NULL) {
		IRDA_DEBUG(0, "%s(%p) : BUG : sk is NULL\n",
			   __func__, self);
		return;
	}

	/* Prevent race conditions with irda_release() and irda_shutdown() */
	bh_lock_sock(sk);
	if (!sock_flag(sk, SOCK_DEAD) && sk->sk_state != TCP_CLOSE) {
		sk->sk_state     = TCP_CLOSE;
		sk->sk_shutdown |= SEND_SHUTDOWN;

		sk->sk_state_change(sk);

		/* Close our TSAP.
		 * If we leave it open, IrLMP put it back into the list of
		 * unconnected LSAPs. The problem is that any incoming request
		 * can then be matched to this socket (and it will be, because
		 * it is at the head of the list). This would prevent any
		 * listening socket waiting on the same TSAP to get those
		 * requests. Some apps forget to close sockets, or hang to it
		 * a bit too long, so we may stay in this dead state long
		 * enough to be noticed...
		 * Note : all socket function do check sk->sk_state, so we are
		 * safe...
		 * Jean II
		 */
		if (self->tsap) {
			irttp_close_tsap(self->tsap);
			self->tsap = NULL;
		}
	}
	bh_unlock_sock(sk);

	/* Note : once we are there, there is not much you want to do
	 * with the socket anymore, apart from closing it.
	 * For example, bind() and connect() won't reset sk->sk_err,
	 * sk->sk_shutdown and sk->sk_flags to valid values...
	 * Jean II
	 */
}

/*
 * Function irda_connect_confirm (instance, sap, qos, max_sdu_size, skb)
 *
 *    Connections has been confirmed by the remote device
 *
 */
static void irda_connect_confirm(void *instance, void *sap,
				 struct qos_info *qos,
				 __u32 max_sdu_size, __u8 max_header_size,
				 struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	self = instance;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	sk = instance;
	if (sk == NULL) {
		dev_kfree_skb(skb);
		return;
	}

	dev_kfree_skb(skb);
	// Should be ??? skb_queue_tail(&sk->sk_receive_queue, skb);

	/* How much header space do we need to reserve */
	self->max_header_size = max_header_size;

	/* IrTTP max SDU size in transmit direction */
	self->max_sdu_size_tx = max_sdu_size;

	/* Find out what the largest chunk of data that we can transmit is */
	switch (sk->sk_type) {
	case SOCK_STREAM:
		if (max_sdu_size != 0) {
			IRDA_ERROR("%s: max_sdu_size must be 0\n",
				   __func__);
			return;
		}
		self->max_data_size = irttp_get_max_seg_size(self->tsap);
		break;
	case SOCK_SEQPACKET:
		if (max_sdu_size == 0) {
			IRDA_ERROR("%s: max_sdu_size cannot be 0\n",
				   __func__);
			return;
		}
		self->max_data_size = max_sdu_size;
		break;
	default:
		self->max_data_size = irttp_get_max_seg_size(self->tsap);
	}

	IRDA_DEBUG(2, "%s(), max_data_size=%d\n", __func__,
		   self->max_data_size);

	memcpy(&self->qos_tx, qos, sizeof(struct qos_info));

	/* We are now connected! */
	sk->sk_state = TCP_ESTABLISHED;
	sk->sk_state_change(sk);
}

/*
 * Function irda_connect_indication(instance, sap, qos, max_sdu_size, userdata)
 *
 *    Incoming connection
 *
 */
static void irda_connect_indication(void *instance, void *sap,
				    struct qos_info *qos, __u32 max_sdu_size,
				    __u8 max_header_size, struct sk_buff *skb)
{
	struct irda_sock *self;
	struct sock *sk;

	self = instance;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	sk = instance;
	if (sk == NULL) {
		dev_kfree_skb(skb);
		return;
	}

	/* How much header space do we need to reserve */
	self->max_header_size = max_header_size;

	/* IrTTP max SDU size in transmit direction */
	self->max_sdu_size_tx = max_sdu_size;

	/* Find out what the largest chunk of data that we can transmit is */
	switch (sk->sk_type) {
	case SOCK_STREAM:
		if (max_sdu_size != 0) {
			IRDA_ERROR("%s: max_sdu_size must be 0\n",
				   __func__);
			kfree_skb(skb);
			return;
		}
		self->max_data_size = irttp_get_max_seg_size(self->tsap);
		break;
	case SOCK_SEQPACKET:
		if (max_sdu_size == 0) {
			IRDA_ERROR("%s: max_sdu_size cannot be 0\n",
				   __func__);
			kfree_skb(skb);
			return;
		}
		self->max_data_size = max_sdu_size;
		break;
	default:
		self->max_data_size = irttp_get_max_seg_size(self->tsap);
	}

	IRDA_DEBUG(2, "%s(), max_data_size=%d\n", __func__,
		   self->max_data_size);

	memcpy(&self->qos_tx, qos, sizeof(struct qos_info));

	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_state_change(sk);
}

/*
 * Function irda_connect_response (handle)
 *
 *    Accept incoming connection
 *
 */
static void irda_connect_response(struct irda_sock *self)
{
	struct sk_buff *skb;

	IRDA_DEBUG(2, "%s()\n", __func__);

	skb = alloc_skb(TTP_MAX_HEADER + TTP_SAR_HEADER,
			GFP_ATOMIC);
	if (skb == NULL) {
		IRDA_DEBUG(0, "%s() Unable to allocate sk_buff!\n",
			   __func__);
		return;
	}

	/* Reserve space for MUX_CONTROL and LAP header */
	skb_reserve(skb, IRDA_MAX_HEADER);

	irttp_connect_response(self->tsap, self->max_sdu_size_rx, skb);
}

/*
 * Function irda_flow_indication (instance, sap, flow)
 *
 *    Used by TinyTP to tell us if it can accept more data or not
 *
 */
static void irda_flow_indication(void *instance, void *sap, LOCAL_FLOW flow)
{
	struct irda_sock *self;
	struct sock *sk;

	IRDA_DEBUG(2, "%s()\n", __func__);

	self = instance;
	sk = instance;
	BUG_ON(sk == NULL);

	switch (flow) {
	case FLOW_STOP:
		IRDA_DEBUG(1, "%s(), IrTTP wants us to slow down\n",
			   __func__);
		self->tx_flow = flow;
		break;
	case FLOW_START:
		self->tx_flow = flow;
		IRDA_DEBUG(1, "%s(), IrTTP wants us to start again\n",
			   __func__);
		wake_up_interruptible(sk_sleep(sk));
		break;
	default:
		IRDA_DEBUG(0, "%s(), Unknown flow command!\n", __func__);
		/* Unknown flow command, better stop */
		self->tx_flow = flow;
		break;
	}
}

/*
 * Function irda_getvalue_confirm (obj_id, value, priv)
 *
 *    Got answer from remote LM-IAS, just pass object to requester...
 *
 * Note : duplicate from above, but we need our own version that
 * doesn't touch the dtsap_sel and save the full value structure...
 */
static void irda_getvalue_confirm(int result, __u16 obj_id,
				  struct ias_value *value, void *priv)
{
	struct irda_sock *self;

	self = (struct irda_sock *) priv;
	if (!self) {
		IRDA_WARNING("%s: lost myself!\n", __func__);
		return;
	}

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	/* We probably don't need to make any more queries */
	iriap_close(self->iriap);
	self->iriap = NULL;

	/* Check if request succeeded */
	if (result != IAS_SUCCESS) {
		IRDA_DEBUG(1, "%s(), IAS query failed! (%d)\n", __func__,
			   result);

		self->errno = result;	/* We really need it later */

		/* Wake up any processes waiting for result */
		wake_up_interruptible(&self->query_wait);

		return;
	}

	/* Pass the object to the caller (so the caller must delete it) */
	self->ias_result = value;
	self->errno = 0;

	/* Wake up any processes waiting for result */
	wake_up_interruptible(&self->query_wait);
}

/*
 * Function irda_selective_discovery_indication (discovery)
 *
 *    Got a selective discovery indication from IrLMP.
 *
 * IrLMP is telling us that this node is new and matching our hint bit
 * filter. Wake up any process waiting for answer...
 */
static void irda_selective_discovery_indication(discinfo_t *discovery,
						DISCOVERY_MODE mode,
						void *priv)
{
	struct irda_sock *self;

	IRDA_DEBUG(2, "%s()\n", __func__);

	self = (struct irda_sock *) priv;
	if (!self) {
		IRDA_WARNING("%s: lost myself!\n", __func__);
		return;
	}

	/* Pass parameter to the caller */
	self->cachedaddr = discovery->daddr;

	/* Wake up process if its waiting for device to be discovered */
	wake_up_interruptible(&self->query_wait);
}

/*
 * Function irda_discovery_timeout (priv)
 *
 *    Timeout in the selective discovery process
 *
 * We were waiting for a node to be discovered, but nothing has come up
 * so far. Wake up the user and tell him that we failed...
 */
static void irda_discovery_timeout(u_long priv)
{
	struct irda_sock *self;

	IRDA_DEBUG(2, "%s()\n", __func__);

	self = (struct irda_sock *) priv;
	BUG_ON(self == NULL);

	/* Nothing for the caller */
	self->cachelog = NULL;
	self->cachedaddr = 0;
	self->errno = -ETIME;

	/* Wake up process if its still waiting... */
	wake_up_interruptible(&self->query_wait);
}

/*
 * Function irda_open_tsap (self)
 *
 *    Open local Transport Service Access Point (TSAP)
 *
 */
static int irda_open_tsap(struct irda_sock *self, __u8 tsap_sel, char *name)
{
	notify_t notify;

	if (self->tsap) {
		IRDA_WARNING("%s: busy!\n", __func__);
		return -EBUSY;
	}

	/* Initialize callbacks to be used by the IrDA stack */
	irda_notify_init(&notify);
	notify.connect_confirm       = irda_connect_confirm;
	notify.connect_indication    = irda_connect_indication;
	notify.disconnect_indication = irda_disconnect_indication;
	notify.data_indication       = irda_data_indication;
	notify.udata_indication	     = irda_data_indication;
	notify.flow_indication       = irda_flow_indication;
	notify.instance = self;
	strncpy(notify.name, name, NOTIFY_MAX_NAME);

	self->tsap = irttp_open_tsap(tsap_sel, DEFAULT_INITIAL_CREDIT,
				     &notify);
	if (self->tsap == NULL) {
		IRDA_DEBUG(0, "%s(), Unable to allocate TSAP!\n",
			   __func__);
		return -ENOMEM;
	}
	/* Remember which TSAP selector we actually got */
	self->stsap_sel = self->tsap->stsap_sel;

	return 0;
}

/*
 * Function irda_open_lsap (self)
 *
 *    Open local Link Service Access Point (LSAP). Used for opening Ultra
 *    sockets
 */
#ifdef CONFIG_IRDA_ULTRA
static int irda_open_lsap(struct irda_sock *self, int pid)
{
	notify_t notify;

	if (self->lsap) {
		IRDA_WARNING("%s(), busy!\n", __func__);
		return -EBUSY;
	}

	/* Initialize callbacks to be used by the IrDA stack */
	irda_notify_init(&notify);
	notify.udata_indication	= irda_data_indication;
	notify.instance = self;
	strncpy(notify.name, "Ultra", NOTIFY_MAX_NAME);

	self->lsap = irlmp_open_lsap(LSAP_CONNLESS, &notify, pid);
	if (self->lsap == NULL) {
		IRDA_DEBUG( 0, "%s(), Unable to allocate LSAP!\n", __func__);
		return -ENOMEM;
	}

	return 0;
}
#endif /* CONFIG_IRDA_ULTRA */

/*
 * Function irda_find_lsap_sel (self, name)
 *
 *    Try to lookup LSAP selector in remote LM-IAS
 *
 * Basically, we start a IAP query, and then go to sleep. When the query
 * return, irda_getvalue_confirm will wake us up, and we can examine the
 * result of the query...
 * Note that in some case, the query fail even before we go to sleep,
 * creating some races...
 */
static int irda_find_lsap_sel(struct irda_sock *self, char *name)
{
	IRDA_DEBUG(2, "%s(%p, %s)\n", __func__, self, name);

	if (self->iriap) {
		IRDA_WARNING("%s(): busy with a previous query\n",
			     __func__);
		return -EBUSY;
	}

	self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
				 irda_getvalue_confirm);
	if(self->iriap == NULL)
		return -ENOMEM;

	/* Treat unexpected wakeup as disconnect */
	self->errno = -EHOSTUNREACH;

	/* Query remote LM-IAS */
	iriap_getvaluebyclass_request(self->iriap, self->saddr, self->daddr,
				      name, "IrDA:TinyTP:LsapSel");

	/* Wait for answer, if not yet finished (or failed) */
	if (wait_event_interruptible(self->query_wait, (self->iriap==NULL)))
		/* Treat signals as disconnect */
		return -EHOSTUNREACH;

	/* Check what happened */
	if (self->errno)
	{
		/* Requested object/attribute doesn't exist */
		if((self->errno == IAS_CLASS_UNKNOWN) ||
		   (self->errno == IAS_ATTRIB_UNKNOWN))
			return -EADDRNOTAVAIL;
		else
			return -EHOSTUNREACH;
	}

	/* Get the remote TSAP selector */
	switch (self->ias_result->type) {
	case IAS_INTEGER:
		IRDA_DEBUG(4, "%s() int=%d\n",
			   __func__, self->ias_result->t.integer);

		if (self->ias_result->t.integer != -1)
			self->dtsap_sel = self->ias_result->t.integer;
		else
			self->dtsap_sel = 0;
		break;
	default:
		self->dtsap_sel = 0;
		IRDA_DEBUG(0, "%s(), bad type!\n", __func__);
		break;
	}
	if (self->ias_result)
		irias_delete_value(self->ias_result);

	if (self->dtsap_sel)
		return 0;

	return -EADDRNOTAVAIL;
}

/*
 * Function irda_discover_daddr_and_lsap_sel (self, name)
 *
 *    This try to find a device with the requested service.
 *
 * It basically look into the discovery log. For each address in the list,
 * it queries the LM-IAS of the device to find if this device offer
 * the requested service.
 * If there is more than one node supporting the service, we complain
 * to the user (it should move devices around).
 * The, we set both the destination address and the lsap selector to point
 * on the service on the unique device we have found.
 *
 * Note : this function fails if there is more than one device in range,
 * because IrLMP doesn't disconnect the LAP when the last LSAP is closed.
 * Moreover, we would need to wait the LAP disconnection...
 */
static int irda_discover_daddr_and_lsap_sel(struct irda_sock *self, char *name)
{
	discinfo_t *discoveries;	/* Copy of the discovery log */
	int	number;			/* Number of nodes in the log */
	int	i;
	int	err = -ENETUNREACH;
	__u32	daddr = DEV_ADDR_ANY;	/* Address we found the service on */
	__u8	dtsap_sel = 0x0;	/* TSAP associated with it */

	IRDA_DEBUG(2, "%s(), name=%s\n", __func__, name);

	/* Ask lmp for the current discovery log
	 * Note : we have to use irlmp_get_discoveries(), as opposed
	 * to play with the cachelog directly, because while we are
	 * making our ias query, le log might change... */
	discoveries = irlmp_get_discoveries(&number, self->mask.word,
					    self->nslots);
	/* Check if the we got some results */
	if (discoveries == NULL)
		return -ENETUNREACH;	/* No nodes discovered */

	/*
	 * Now, check all discovered devices (if any), and connect
	 * client only about the services that the client is
	 * interested in...
	 */
	for(i = 0; i < number; i++) {
		/* Try the address in the log */
		self->daddr = discoveries[i].daddr;
		self->saddr = 0x0;
		IRDA_DEBUG(1, "%s(), trying daddr = %08x\n",
			   __func__, self->daddr);

		/* Query remote LM-IAS for this service */
		err = irda_find_lsap_sel(self, name);
		switch (err) {
		case 0:
			/* We found the requested service */
			if(daddr != DEV_ADDR_ANY) {
				IRDA_DEBUG(1, "%s(), discovered service ''%s'' in two different devices !!!\n",
					   __func__, name);
				self->daddr = DEV_ADDR_ANY;
				kfree(discoveries);
				return -ENOTUNIQ;
			}
			/* First time we found that one, save it ! */
			daddr = self->daddr;
			dtsap_sel = self->dtsap_sel;
			break;
		case -EADDRNOTAVAIL:
			/* Requested service simply doesn't exist on this node */
			break;
		default:
			/* Something bad did happen :-( */
			IRDA_DEBUG(0, "%s(), unexpected IAS query failure\n", __func__);
			self->daddr = DEV_ADDR_ANY;
			kfree(discoveries);
			return -EHOSTUNREACH;
			break;
		}
	}
	/* Cleanup our copy of the discovery log */
	kfree(discoveries);

	/* Check out what we found */
	if(daddr == DEV_ADDR_ANY) {
		IRDA_DEBUG(1, "%s(), cannot discover service ''%s'' in any device !!!\n",
			   __func__, name);
		self->daddr = DEV_ADDR_ANY;
		return -EADDRNOTAVAIL;
	}

	/* Revert back to discovered device & service */
	self->daddr = daddr;
	self->saddr = 0x0;
	self->dtsap_sel = dtsap_sel;

	IRDA_DEBUG(1, "%s(), discovered requested service ''%s'' at address %08x\n",
		   __func__, name, self->daddr);

	return 0;
}

/*
 * Function irda_getname (sock, uaddr, uaddr_len, peer)
 *
 *    Return the our own, or peers socket address (sockaddr_irda)
 *
 */
static int irda_getname(struct socket *sock, struct sockaddr *uaddr,
			int *uaddr_len, int peer)
{
	struct sockaddr_irda saddr;
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);

	memset(&saddr, 0, sizeof(saddr));
	if (peer) {
		if (sk->sk_state != TCP_ESTABLISHED)
			return -ENOTCONN;

		saddr.sir_family = AF_IRDA;
		saddr.sir_lsap_sel = self->dtsap_sel;
		saddr.sir_addr = self->daddr;
	} else {
		saddr.sir_family = AF_IRDA;
		saddr.sir_lsap_sel = self->stsap_sel;
		saddr.sir_addr = self->saddr;
	}

	IRDA_DEBUG(1, "%s(), tsap_sel = %#x\n", __func__, saddr.sir_lsap_sel);
	IRDA_DEBUG(1, "%s(), addr = %08x\n", __func__, saddr.sir_addr);

	/* uaddr_len come to us uninitialised */
	*uaddr_len = sizeof (struct sockaddr_irda);
	memcpy(uaddr, &saddr, *uaddr_len);

	return 0;
}

/*
 * Function irda_listen (sock, backlog)
 *
 *    Just move to the listen state
 *
 */
static int irda_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	int err = -EOPNOTSUPP;

	IRDA_DEBUG(2, "%s()\n", __func__);

	lock_sock(sk);

	if ((sk->sk_type != SOCK_STREAM) && (sk->sk_type != SOCK_SEQPACKET) &&
	    (sk->sk_type != SOCK_DGRAM))
		goto out;

	if (sk->sk_state != TCP_LISTEN) {
		sk->sk_max_ack_backlog = backlog;
		sk->sk_state           = TCP_LISTEN;

		err = 0;
	}
out:
	release_sock(sk);

	return err;
}

/*
 * Function irda_bind (sock, uaddr, addr_len)
 *
 *    Used by servers to register their well known TSAP
 *
 */
static int irda_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct sockaddr_irda *addr = (struct sockaddr_irda *) uaddr;
	struct irda_sock *self = irda_sk(sk);
	int err;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	if (addr_len != sizeof(struct sockaddr_irda))
		return -EINVAL;

	lock_sock(sk);
#ifdef CONFIG_IRDA_ULTRA
	/* Special care for Ultra sockets */
	if ((sk->sk_type == SOCK_DGRAM) &&
	    (sk->sk_protocol == IRDAPROTO_ULTRA)) {
		self->pid = addr->sir_lsap_sel;
		err = -EOPNOTSUPP;
		if (self->pid & 0x80) {
			IRDA_DEBUG(0, "%s(), extension in PID not supp!\n", __func__);
			goto out;
		}
		err = irda_open_lsap(self, self->pid);
		if (err < 0)
			goto out;

		/* Pretend we are connected */
		sock->state = SS_CONNECTED;
		sk->sk_state   = TCP_ESTABLISHED;
		err = 0;

		goto out;
	}
#endif /* CONFIG_IRDA_ULTRA */

	self->ias_obj = irias_new_object(addr->sir_name, jiffies);
	err = -ENOMEM;
	if (self->ias_obj == NULL)
		goto out;

	err = irda_open_tsap(self, addr->sir_lsap_sel, addr->sir_name);
	if (err < 0) {
		irias_delete_object(self->ias_obj);
		self->ias_obj = NULL;
		goto out;
	}

	/*  Register with LM-IAS */
	irias_add_integer_attrib(self->ias_obj, "IrDA:TinyTP:LsapSel",
				 self->stsap_sel, IAS_KERNEL_ATTR);
	irias_insert_object(self->ias_obj);

	err = 0;
out:
	release_sock(sk);
	return err;
}

/*
 * Function irda_accept (sock, newsock, flags)
 *
 *    Wait for incoming connection
 *
 */
static int irda_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	struct irda_sock *new, *self = irda_sk(sk);
	struct sock *newsk;
	struct sk_buff *skb;
	int err;

	IRDA_DEBUG(2, "%s()\n", __func__);

	err = irda_create(sock_net(sk), newsock, sk->sk_protocol, 0);
	if (err)
		return err;

	err = -EINVAL;

	lock_sock(sk);
	if (sock->state != SS_UNCONNECTED)
		goto out;

	if ((sk = sock->sk) == NULL)
		goto out;

	err = -EOPNOTSUPP;
	if ((sk->sk_type != SOCK_STREAM) && (sk->sk_type != SOCK_SEQPACKET) &&
	    (sk->sk_type != SOCK_DGRAM))
		goto out;

	err = -EINVAL;
	if (sk->sk_state != TCP_LISTEN)
		goto out;

	/*
	 *	The read queue this time is holding sockets ready to use
	 *	hooked into the SABM we saved
	 */

	/*
	 * We can perform the accept only if there is incoming data
	 * on the listening socket.
	 * So, we will block the caller until we receive any data.
	 * If the caller was waiting on select() or poll() before
	 * calling us, the data is waiting for us ;-)
	 * Jean II
	 */
	while (1) {
		skb = skb_dequeue(&sk->sk_receive_queue);
		if (skb)
			break;

		/* Non blocking operation */
		err = -EWOULDBLOCK;
		if (flags & O_NONBLOCK)
			goto out;

		err = wait_event_interruptible(*(sk_sleep(sk)),
					skb_peek(&sk->sk_receive_queue));
		if (err)
			goto out;
	}

	newsk = newsock->sk;
	err = -EIO;
	if (newsk == NULL)
		goto out;

	newsk->sk_state = TCP_ESTABLISHED;

	new = irda_sk(newsk);

	/* Now attach up the new socket */
	new->tsap = irttp_dup(self->tsap, new);
	err = -EPERM; /* value does not seem to make sense. -arnd */
	if (!new->tsap) {
		IRDA_DEBUG(0, "%s(), dup failed!\n", __func__);
		kfree_skb(skb);
		goto out;
	}

	new->stsap_sel = new->tsap->stsap_sel;
	new->dtsap_sel = new->tsap->dtsap_sel;
	new->saddr = irttp_get_saddr(new->tsap);
	new->daddr = irttp_get_daddr(new->tsap);

	new->max_sdu_size_tx = self->max_sdu_size_tx;
	new->max_sdu_size_rx = self->max_sdu_size_rx;
	new->max_data_size   = self->max_data_size;
	new->max_header_size = self->max_header_size;

	memcpy(&new->qos_tx, &self->qos_tx, sizeof(struct qos_info));

	/* Clean up the original one to keep it in listen state */
	irttp_listen(self->tsap);

	kfree_skb(skb);
	sk->sk_ack_backlog--;

	newsock->state = SS_CONNECTED;

	irda_connect_response(new);
	err = 0;
out:
	release_sock(sk);
	return err;
}

/*
 * Function irda_connect (sock, uaddr, addr_len, flags)
 *
 *    Connect to a IrDA device
 *
 * The main difference with a "standard" connect is that with IrDA we need
 * to resolve the service name into a TSAP selector (in TCP, port number
 * doesn't have to be resolved).
 * Because of this service name resoltion, we can offer "auto-connect",
 * where we connect to a service without specifying a destination address.
 *
 * Note : by consulting "errno", the user space caller may learn the cause
 * of the failure. Most of them are visible in the function, others may come
 * from subroutines called and are listed here :
 *	o EBUSY : already processing a connect
 *	o EHOSTUNREACH : bad addr->sir_addr argument
 *	o EADDRNOTAVAIL : bad addr->sir_name argument
 *	o ENOTUNIQ : more than one node has addr->sir_name (auto-connect)
 *	o ENETUNREACH : no node found on the network (auto-connect)
 */
static int irda_connect(struct socket *sock, struct sockaddr *uaddr,
			int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_irda *addr = (struct sockaddr_irda *) uaddr;
	struct irda_sock *self = irda_sk(sk);
	int err;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	lock_sock(sk);
	/* Don't allow connect for Ultra sockets */
	err = -ESOCKTNOSUPPORT;
	if ((sk->sk_type == SOCK_DGRAM) && (sk->sk_protocol == IRDAPROTO_ULTRA))
		goto out;

	if (sk->sk_state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		err = 0;
		goto out;   /* Connect completed during a ERESTARTSYS event */
	}

	if (sk->sk_state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		err = -ECONNREFUSED;
		goto out;
	}

	err = -EISCONN;      /* No reconnect on a seqpacket socket */
	if (sk->sk_state == TCP_ESTABLISHED)
		goto out;

	sk->sk_state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	err = -EINVAL;
	if (addr_len != sizeof(struct sockaddr_irda))
		goto out;

	/* Check if user supplied any destination device address */
	if ((!addr->sir_addr) || (addr->sir_addr == DEV_ADDR_ANY)) {
		/* Try to find one suitable */
		err = irda_discover_daddr_and_lsap_sel(self, addr->sir_name);
		if (err) {
			IRDA_DEBUG(0, "%s(), auto-connect failed!\n", __func__);
			goto out;
		}
	} else {
		/* Use the one provided by the user */
		self->daddr = addr->sir_addr;
		IRDA_DEBUG(1, "%s(), daddr = %08x\n", __func__, self->daddr);

		/* If we don't have a valid service name, we assume the
		 * user want to connect on a specific LSAP. Prevent
		 * the use of invalid LSAPs (IrLMP 1.1 p10). Jean II */
		if((addr->sir_name[0] != '\0') ||
		   (addr->sir_lsap_sel >= 0x70)) {
			/* Query remote LM-IAS using service name */
			err = irda_find_lsap_sel(self, addr->sir_name);
			if (err) {
				IRDA_DEBUG(0, "%s(), connect failed!\n", __func__);
				goto out;
			}
		} else {
			/* Directly connect to the remote LSAP
			 * specified by the sir_lsap field.
			 * Please use with caution, in IrDA LSAPs are
			 * dynamic and there is no "well-known" LSAP. */
			self->dtsap_sel = addr->sir_lsap_sel;
		}
	}

	/* Check if we have opened a local TSAP */
	if (!self->tsap)
		irda_open_tsap(self, LSAP_ANY, addr->sir_name);

	/* Move to connecting socket, start sending Connect Requests */
	sock->state = SS_CONNECTING;
	sk->sk_state   = TCP_SYN_SENT;

	/* Connect to remote device */
	err = irttp_connect_request(self->tsap, self->dtsap_sel,
				    self->saddr, self->daddr, NULL,
				    self->max_sdu_size_rx, NULL);
	if (err) {
		IRDA_DEBUG(0, "%s(), connect failed!\n", __func__);
		goto out;
	}

	/* Now the loop */
	err = -EINPROGRESS;
	if (sk->sk_state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		goto out;

	err = -ERESTARTSYS;
	if (wait_event_interruptible(*(sk_sleep(sk)),
				     (sk->sk_state != TCP_SYN_SENT)))
		goto out;

	if (sk->sk_state != TCP_ESTABLISHED) {
		sock->state = SS_UNCONNECTED;
		if (sk->sk_prot->disconnect(sk, flags))
			sock->state = SS_DISCONNECTING;
		err = sock_error(sk);
		if (!err)
			err = -ECONNRESET;
		goto out;
	}

	sock->state = SS_CONNECTED;

	/* At this point, IrLMP has assigned our source address */
	self->saddr = irttp_get_saddr(self->tsap);
	err = 0;
out:
	release_sock(sk);
	return err;
}

static struct proto irda_proto = {
	.name	  = "IRDA",
	.owner	  = THIS_MODULE,
	.obj_size = sizeof(struct irda_sock),
};

/*
 * Function irda_create (sock, protocol)
 *
 *    Create IrDA socket
 *
 */
static int irda_create(struct net *net, struct socket *sock, int protocol,
		       int kern)
{
	struct sock *sk;
	struct irda_sock *self;

	IRDA_DEBUG(2, "%s()\n", __func__);

	if (net != &init_net)
		return -EAFNOSUPPORT;

	/* Check for valid socket type */
	switch (sock->type) {
	case SOCK_STREAM:     /* For TTP connections with SAR disabled */
	case SOCK_SEQPACKET:  /* For TTP connections with SAR enabled */
	case SOCK_DGRAM:      /* For TTP Unitdata or LMP Ultra transfers */
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	/* Allocate networking socket */
	sk = sk_alloc(net, PF_IRDA, GFP_ATOMIC, &irda_proto);
	if (sk == NULL)
		return -ENOMEM;

	self = irda_sk(sk);
	IRDA_DEBUG(2, "%s() : self is %p\n", __func__, self);

	init_waitqueue_head(&self->query_wait);

	switch (sock->type) {
	case SOCK_STREAM:
		sock->ops = &irda_stream_ops;
		self->max_sdu_size_rx = TTP_SAR_DISABLE;
		break;
	case SOCK_SEQPACKET:
		sock->ops = &irda_seqpacket_ops;
		self->max_sdu_size_rx = TTP_SAR_UNBOUND;
		break;
	case SOCK_DGRAM:
		switch (protocol) {
#ifdef CONFIG_IRDA_ULTRA
		case IRDAPROTO_ULTRA:
			sock->ops = &irda_ultra_ops;
			/* Initialise now, because we may send on unbound
			 * sockets. Jean II */
			self->max_data_size = ULTRA_MAX_DATA - LMP_PID_HEADER;
			self->max_header_size = IRDA_MAX_HEADER + LMP_PID_HEADER;
			break;
#endif /* CONFIG_IRDA_ULTRA */
		case IRDAPROTO_UNITDATA:
			sock->ops = &irda_dgram_ops;
			/* We let Unitdata conn. be like seqpack conn. */
			self->max_sdu_size_rx = TTP_SAR_UNBOUND;
			break;
		default:
			sk_free(sk);
			return -ESOCKTNOSUPPORT;
		}
		break;
	default:
		sk_free(sk);
		return -ESOCKTNOSUPPORT;
	}

	/* Initialise networking socket struct */
	sock_init_data(sock, sk);	/* Note : set sk->sk_refcnt to 1 */
	sk->sk_family = PF_IRDA;
	sk->sk_protocol = protocol;

	/* Register as a client with IrLMP */
	self->ckey = irlmp_register_client(0, NULL, NULL, NULL);
	self->mask.word = 0xffff;
	self->rx_flow = self->tx_flow = FLOW_START;
	self->nslots = DISCOVERY_DEFAULT_SLOTS;
	self->daddr = DEV_ADDR_ANY;	/* Until we get connected */
	self->saddr = 0x0;		/* so IrLMP assign us any link */
	return 0;
}

/*
 * Function irda_destroy_socket (self)
 *
 *    Destroy socket
 *
 */
static void irda_destroy_socket(struct irda_sock *self)
{
	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	/* Unregister with IrLMP */
	irlmp_unregister_client(self->ckey);
	irlmp_unregister_service(self->skey);

	/* Unregister with LM-IAS */
	if (self->ias_obj) {
		irias_delete_object(self->ias_obj);
		self->ias_obj = NULL;
	}

	if (self->iriap) {
		iriap_close(self->iriap);
		self->iriap = NULL;
	}

	if (self->tsap) {
		irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
		irttp_close_tsap(self->tsap);
		self->tsap = NULL;
	}
#ifdef CONFIG_IRDA_ULTRA
	if (self->lsap) {
		irlmp_close_lsap(self->lsap);
		self->lsap = NULL;
	}
#endif /* CONFIG_IRDA_ULTRA */
}

/*
 * Function irda_release (sock)
 */
static int irda_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	IRDA_DEBUG(2, "%s()\n", __func__);

	if (sk == NULL)
		return 0;

	lock_sock(sk);
	sk->sk_state       = TCP_CLOSE;
	sk->sk_shutdown   |= SEND_SHUTDOWN;
	sk->sk_state_change(sk);

	/* Destroy IrDA socket */
	irda_destroy_socket(irda_sk(sk));

	sock_orphan(sk);
	sock->sk   = NULL;
	release_sock(sk);

	/* Purge queues (see sock_init_data()) */
	skb_queue_purge(&sk->sk_receive_queue);

	/* Destroy networking socket if we are the last reference on it,
	 * i.e. if(sk->sk_refcnt == 0) -> sk_free(sk) */
	sock_put(sk);

	/* Notes on socket locking and deallocation... - Jean II
	 * In theory we should put pairs of sock_hold() / sock_put() to
	 * prevent the socket to be destroyed whenever there is an
	 * outstanding request or outstanding incoming packet or event.
	 *
	 * 1) This may include IAS request, both in connect and getsockopt.
	 * Unfortunately, the situation is a bit more messy than it looks,
	 * because we close iriap and kfree(self) above.
	 *
	 * 2) This may include selective discovery in getsockopt.
	 * Same stuff as above, irlmp registration and self are gone.
	 *
	 * Probably 1 and 2 may not matter, because it's all triggered
	 * by a process and the socket layer already prevent the
	 * socket to go away while a process is holding it, through
	 * sockfd_put() and fput()...
	 *
	 * 3) This may include deferred TSAP closure. In particular,
	 * we may receive a late irda_disconnect_indication()
	 * Fortunately, (tsap_cb *)->close_pend should protect us
	 * from that.
	 *
	 * I did some testing on SMP, and it looks solid. And the socket
	 * memory leak is now gone... - Jean II
	 */

	return 0;
}

/*
 * Function irda_sendmsg (iocb, sock, msg, len)
 *
 *    Send message down to TinyTP. This function is used for both STREAM and
 *    SEQPACK services. This is possible since it forces the client to
 *    fragment the message if necessary
 */
static int irda_sendmsg(struct kiocb *iocb, struct socket *sock,
			struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self;
	struct sk_buff *skb;
	int err = -EPIPE;

	IRDA_DEBUG(4, "%s(), len=%zd\n", __func__, len);

	/* Note : socket.c set MSG_EOR on SEQPACKET sockets */
	if (msg->msg_flags & ~(MSG_DONTWAIT | MSG_EOR | MSG_CMSG_COMPAT |
			       MSG_NOSIGNAL)) {
		err = -EINVAL;
		goto out;
	}

	lock_sock(sk);

	if (sk->sk_shutdown & SEND_SHUTDOWN)
		goto out_err;

	if (sk->sk_state != TCP_ESTABLISHED) {
		err = -ENOTCONN;
		goto out;
	}

	self = irda_sk(sk);

	/* Check if IrTTP is wants us to slow down */

	if (wait_event_interruptible(*(sk_sleep(sk)),
	    (self->tx_flow != FLOW_STOP  ||  sk->sk_state != TCP_ESTABLISHED))) {
		err = -ERESTARTSYS;
		goto out;
	}

	/* Check if we are still connected */
	if (sk->sk_state != TCP_ESTABLISHED) {
		err = -ENOTCONN;
		goto out;
	}

	/* Check that we don't send out too big frames */
	if (len > self->max_data_size) {
		IRDA_DEBUG(2, "%s(), Chopping frame from %zd to %d bytes!\n",
			   __func__, len, self->max_data_size);
		len = self->max_data_size;
	}

	skb = sock_alloc_send_skb(sk, len + self->max_header_size + 16,
				  msg->msg_flags & MSG_DONTWAIT, &err);
	if (!skb)
		goto out_err;

	skb_reserve(skb, self->max_header_size + 16);
	skb_reset_transport_header(skb);
	skb_put(skb, len);
	err = memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len);
	if (err) {
		kfree_skb(skb);
		goto out_err;
	}

	/*
	 * Just send the message to TinyTP, and let it deal with possible
	 * errors. No need to duplicate all that here
	 */
	err = irttp_data_request(self->tsap, skb);
	if (err) {
		IRDA_DEBUG(0, "%s(), err=%d\n", __func__, err);
		goto out_err;
	}

	release_sock(sk);
	/* Tell client how much data we actually sent */
	return len;

out_err:
	err = sk_stream_error(sk, msg->msg_flags, err);
out:
	release_sock(sk);
	return err;

}

/*
 * Function irda_recvmsg_dgram (iocb, sock, msg, size, flags)
 *
 *    Try to receive message and copy it to user. The frame is discarded
 *    after being read, regardless of how much the user actually read
 */
static int irda_recvmsg_dgram(struct kiocb *iocb, struct socket *sock,
			      struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);
	struct sk_buff *skb;
	size_t copied;
	int err;

	IRDA_DEBUG(4, "%s()\n", __func__);

	skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT,
				flags & MSG_DONTWAIT, &err);
	if (!skb)
		return err;

	skb_reset_transport_header(skb);
	copied = skb->len;

	if (copied > size) {
		IRDA_DEBUG(2, "%s(), Received truncated frame (%zd < %zd)!\n",
			   __func__, copied, size);
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}
	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	skb_free_datagram(sk, skb);

	/*
	 *  Check if we have previously stopped IrTTP and we know
	 *  have more free space in our rx_queue. If so tell IrTTP
	 *  to start delivering frames again before our rx_queue gets
	 *  empty
	 */
	if (self->rx_flow == FLOW_STOP) {
		if ((atomic_read(&sk->sk_rmem_alloc) << 2) <= sk->sk_rcvbuf) {
			IRDA_DEBUG(2, "%s(), Starting IrTTP\n", __func__);
			self->rx_flow = FLOW_START;
			irttp_flow_request(self->tsap, FLOW_START);
		}
	}

	return copied;
}

/*
 * Function irda_recvmsg_stream (iocb, sock, msg, size, flags)
 */
static int irda_recvmsg_stream(struct kiocb *iocb, struct socket *sock,
			       struct msghdr *msg, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);
	int noblock = flags & MSG_DONTWAIT;
	size_t copied = 0;
	int target, err;
	long timeo;

	IRDA_DEBUG(3, "%s()\n", __func__);

	if ((err = sock_error(sk)) < 0)
		return err;

	if (sock->flags & __SO_ACCEPTCON)
		return -EINVAL;

	err =-EOPNOTSUPP;
	if (flags & MSG_OOB)
		return -EOPNOTSUPP;

	err = 0;
	target = sock_rcvlowat(sk, flags & MSG_WAITALL, size);
	timeo = sock_rcvtimeo(sk, noblock);

	msg->msg_namelen = 0;

	do {
		int chunk;
		struct sk_buff *skb = skb_dequeue(&sk->sk_receive_queue);

		if (skb == NULL) {
			DEFINE_WAIT(wait);
			err = 0;

			if (copied >= target)
				break;

			prepare_to_wait_exclusive(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);

			/*
			 *	POSIX 1003.1g mandates this order.
			 */
			err = sock_error(sk);
			if (err)
				;
			else if (sk->sk_shutdown & RCV_SHUTDOWN)
				;
			else if (noblock)
				err = -EAGAIN;
			else if (signal_pending(current))
				err = sock_intr_errno(timeo);
			else if (sk->sk_state != TCP_ESTABLISHED)
				err = -ENOTCONN;
			else if (skb_peek(&sk->sk_receive_queue) == NULL)
				/* Wait process until data arrives */
				schedule();

			finish_wait(sk_sleep(sk), &wait);

			if (err)
				return err;
			if (sk->sk_shutdown & RCV_SHUTDOWN)
				break;

			continue;
		}

		chunk = min_t(unsigned int, skb->len, size);
		if (memcpy_toiovec(msg->msg_iov, skb->data, chunk)) {
			skb_queue_head(&sk->sk_receive_queue, skb);
			if (copied == 0)
				copied = -EFAULT;
			break;
		}
		copied += chunk;
		size -= chunk;

		/* Mark read part of skb as used */
		if (!(flags & MSG_PEEK)) {
			skb_pull(skb, chunk);

			/* put the skb back if we didn't use it up.. */
			if (skb->len) {
				IRDA_DEBUG(1, "%s(), back on q!\n",
					   __func__);
				skb_queue_head(&sk->sk_receive_queue, skb);
				break;
			}

			kfree_skb(skb);
		} else {
			IRDA_DEBUG(0, "%s() questionable!?\n", __func__);

			/* put message back and return */
			skb_queue_head(&sk->sk_receive_queue, skb);
			break;
		}
	} while (size);

	/*
	 *  Check if we have previously stopped IrTTP and we know
	 *  have more free space in our rx_queue. If so tell IrTTP
	 *  to start delivering frames again before our rx_queue gets
	 *  empty
	 */
	if (self->rx_flow == FLOW_STOP) {
		if ((atomic_read(&sk->sk_rmem_alloc) << 2) <= sk->sk_rcvbuf) {
			IRDA_DEBUG(2, "%s(), Starting IrTTP\n", __func__);
			self->rx_flow = FLOW_START;
			irttp_flow_request(self->tsap, FLOW_START);
		}
	}

	return copied;
}

/*
 * Function irda_sendmsg_dgram (iocb, sock, msg, len)
 *
 *    Send message down to TinyTP for the unreliable sequenced
 *    packet service...
 *
 */
static int irda_sendmsg_dgram(struct kiocb *iocb, struct socket *sock,
			      struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self;
	struct sk_buff *skb;
	int err;

	IRDA_DEBUG(4, "%s(), len=%zd\n", __func__, len);

	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_CMSG_COMPAT))
		return -EINVAL;

	lock_sock(sk);

	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		err = -EPIPE;
		goto out;
	}

	err = -ENOTCONN;
	if (sk->sk_state != TCP_ESTABLISHED)
		goto out;

	self = irda_sk(sk);

	/*
	 * Check that we don't send out too big frames. This is an unreliable
	 * service, so we have no fragmentation and no coalescence
	 */
	if (len > self->max_data_size) {
		IRDA_DEBUG(0, "%s(), Warning to much data! "
			   "Chopping frame from %zd to %d bytes!\n",
			   __func__, len, self->max_data_size);
		len = self->max_data_size;
	}

	skb = sock_alloc_send_skb(sk, len + self->max_header_size,
				  msg->msg_flags & MSG_DONTWAIT, &err);
	err = -ENOBUFS;
	if (!skb)
		goto out;

	skb_reserve(skb, self->max_header_size);
	skb_reset_transport_header(skb);

	IRDA_DEBUG(4, "%s(), appending user data\n", __func__);
	skb_put(skb, len);
	err = memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len);
	if (err) {
		kfree_skb(skb);
		goto out;
	}

	/*
	 * Just send the message to TinyTP, and let it deal with possible
	 * errors. No need to duplicate all that here
	 */
	err = irttp_udata_request(self->tsap, skb);
	if (err) {
		IRDA_DEBUG(0, "%s(), err=%d\n", __func__, err);
		goto out;
	}

	release_sock(sk);
	return len;

out:
	release_sock(sk);
	return err;
}

/*
 * Function irda_sendmsg_ultra (iocb, sock, msg, len)
 *
 *    Send message down to IrLMP for the unreliable Ultra
 *    packet service...
 */
#ifdef CONFIG_IRDA_ULTRA
static int irda_sendmsg_ultra(struct kiocb *iocb, struct socket *sock,
			      struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self;
	__u8 pid = 0;
	int bound = 0;
	struct sk_buff *skb;
	int err;

	IRDA_DEBUG(4, "%s(), len=%zd\n", __func__, len);

	err = -EINVAL;
	if (msg->msg_flags & ~(MSG_DONTWAIT|MSG_CMSG_COMPAT))
		return -EINVAL;

	lock_sock(sk);

	err = -EPIPE;
	if (sk->sk_shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		goto out;
	}

	self = irda_sk(sk);

	/* Check if an address was specified with sendto. Jean II */
	if (msg->msg_name) {
		struct sockaddr_irda *addr = (struct sockaddr_irda *) msg->msg_name;
		err = -EINVAL;
		/* Check address, extract pid. Jean II */
		if (msg->msg_namelen < sizeof(*addr))
			goto out;
		if (addr->sir_family != AF_IRDA)
			goto out;

		pid = addr->sir_lsap_sel;
		if (pid & 0x80) {
			IRDA_DEBUG(0, "%s(), extension in PID not supp!\n", __func__);
			err = -EOPNOTSUPP;
			goto out;
		}
	} else {
		/* Check that the socket is properly bound to an Ultra
		 * port. Jean II */
		if ((self->lsap == NULL) ||
		    (sk->sk_state != TCP_ESTABLISHED)) {
			IRDA_DEBUG(0, "%s(), socket not bound to Ultra PID.\n",
				   __func__);
			err = -ENOTCONN;
			goto out;
		}
		/* Use PID from socket */
		bound = 1;
	}

	/*
	 * Check that we don't send out too big frames. This is an unreliable
	 * service, so we have no fragmentation and no coalescence
	 */
	if (len > self->max_data_size) {
		IRDA_DEBUG(0, "%s(), Warning to much data! "
			   "Chopping frame from %zd to %d bytes!\n",
			   __func__, len, self->max_data_size);
		len = self->max_data_size;
	}

	skb = sock_alloc_send_skb(sk, len + self->max_header_size,
				  msg->msg_flags & MSG_DONTWAIT, &err);
	err = -ENOBUFS;
	if (!skb)
		goto out;

	skb_reserve(skb, self->max_header_size);
	skb_reset_transport_header(skb);

	IRDA_DEBUG(4, "%s(), appending user data\n", __func__);
	skb_put(skb, len);
	err = memcpy_fromiovec(skb_transport_header(skb), msg->msg_iov, len);
	if (err) {
		kfree_skb(skb);
		goto out;
	}

	err = irlmp_connless_data_request((bound ? self->lsap : NULL),
					  skb, pid);
	if (err)
		IRDA_DEBUG(0, "%s(), err=%d\n", __func__, err);
out:
	release_sock(sk);
	return err ? : len;
}
#endif /* CONFIG_IRDA_ULTRA */

/*
 * Function irda_shutdown (sk, how)
 */
static int irda_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);

	IRDA_DEBUG(1, "%s(%p)\n", __func__, self);

	lock_sock(sk);

	sk->sk_state       = TCP_CLOSE;
	sk->sk_shutdown   |= SEND_SHUTDOWN;
	sk->sk_state_change(sk);

	if (self->iriap) {
		iriap_close(self->iriap);
		self->iriap = NULL;
	}

	if (self->tsap) {
		irttp_disconnect_request(self->tsap, NULL, P_NORMAL);
		irttp_close_tsap(self->tsap);
		self->tsap = NULL;
	}

	/* A few cleanup so the socket look as good as new... */
	self->rx_flow = self->tx_flow = FLOW_START;	/* needed ??? */
	self->daddr = DEV_ADDR_ANY;	/* Until we get re-connected */
	self->saddr = 0x0;		/* so IrLMP assign us any link */

	release_sock(sk);

	return 0;
}

/*
 * Function irda_poll (file, sock, wait)
 */
static unsigned int irda_poll(struct file * file, struct socket *sock,
			      poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);
	unsigned int mask;

	IRDA_DEBUG(4, "%s()\n", __func__);

	poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	/* Exceptional events? */
	if (sk->sk_err)
		mask |= POLLERR;
	if (sk->sk_shutdown & RCV_SHUTDOWN) {
		IRDA_DEBUG(0, "%s(), POLLHUP\n", __func__);
		mask |= POLLHUP;
	}

	/* Readable? */
	if (!skb_queue_empty(&sk->sk_receive_queue)) {
		IRDA_DEBUG(4, "Socket is readable\n");
		mask |= POLLIN | POLLRDNORM;
	}

	/* Connection-based need to check for termination and startup */
	switch (sk->sk_type) {
	case SOCK_STREAM:
		if (sk->sk_state == TCP_CLOSE) {
			IRDA_DEBUG(0, "%s(), POLLHUP\n", __func__);
			mask |= POLLHUP;
		}

		if (sk->sk_state == TCP_ESTABLISHED) {
			if ((self->tx_flow == FLOW_START) &&
			    sock_writeable(sk))
			{
				mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
			}
		}
		break;
	case SOCK_SEQPACKET:
		if ((self->tx_flow == FLOW_START) &&
		    sock_writeable(sk))
		{
			mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
		}
		break;
	case SOCK_DGRAM:
		if (sock_writeable(sk))
			mask |= POLLOUT | POLLWRNORM | POLLWRBAND;
		break;
	default:
		break;
	}

	return mask;
}

/*
 * Function irda_ioctl (sock, cmd, arg)
 */
static int irda_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err;

	IRDA_DEBUG(4, "%s(), cmd=%#x\n", __func__, cmd);

	err = -EINVAL;
	switch (cmd) {
	case TIOCOUTQ: {
		long amount;

		amount = sk->sk_sndbuf - sk_wmem_alloc_get(sk);
		if (amount < 0)
			amount = 0;
		err = put_user(amount, (unsigned int __user *)arg);
		break;
	}

	case TIOCINQ: {
		struct sk_buff *skb;
		long amount = 0L;
		/* These two are safe on a single CPU system as only user tasks fiddle here */
		if ((skb = skb_peek(&sk->sk_receive_queue)) != NULL)
			amount = skb->len;
		err = put_user(amount, (unsigned int __user *)arg);
		break;
	}

	case SIOCGSTAMP:
		if (sk != NULL)
			err = sock_get_timestamp(sk, (struct timeval __user *)arg);
		break;

	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
		break;
	default:
		IRDA_DEBUG(1, "%s(), doing device ioctl!\n", __func__);
		err = -ENOIOCTLCMD;
	}

	return err;
}

#ifdef CONFIG_COMPAT
/*
 * Function irda_ioctl (sock, cmd, arg)
 */
static int irda_compat_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	/*
	 * All IRDA's ioctl are standard ones.
	 */
	return -ENOIOCTLCMD;
}
#endif

/*
 * Function irda_setsockopt (sock, level, optname, optval, optlen)
 *
 *    Set some options for the socket
 *
 */
static int irda_setsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);
	struct irda_ias_set    *ias_opt;
	struct ias_object      *ias_obj;
	struct ias_attrib *	ias_attr;	/* Attribute in IAS object */
	int opt, free_ias = 0, err = 0;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	if (level != SOL_IRLMP)
		return -ENOPROTOOPT;

	lock_sock(sk);

	switch (optname) {
	case IRLMP_IAS_SET:
		/* The user want to add an attribute to an existing IAS object
		 * (in the IAS database) or to create a new object with this
		 * attribute.
		 * We first query IAS to know if the object exist, and then
		 * create the right attribute...
		 */

		if (optlen != sizeof(struct irda_ias_set)) {
			err = -EINVAL;
			goto out;
		}

		ias_opt = kmalloc(sizeof(struct irda_ias_set), GFP_ATOMIC);
		if (ias_opt == NULL) {
			err = -ENOMEM;
			goto out;
		}

		/* Copy query to the driver. */
		if (copy_from_user(ias_opt, optval, optlen)) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}

		/* Find the object we target.
		 * If the user gives us an empty string, we use the object
		 * associated with this socket. This will workaround
		 * duplicated class name - Jean II */
		if(ias_opt->irda_class_name[0] == '\0') {
			if(self->ias_obj == NULL) {
				kfree(ias_opt);
				err = -EINVAL;
				goto out;
			}
			ias_obj = self->ias_obj;
		} else
			ias_obj = irias_find_object(ias_opt->irda_class_name);

		/* Only ROOT can mess with the global IAS database.
		 * Users can only add attributes to the object associated
		 * with the socket they own - Jean II */
		if((!capable(CAP_NET_ADMIN)) &&
		   ((ias_obj == NULL) || (ias_obj != self->ias_obj))) {
			kfree(ias_opt);
			err = -EPERM;
			goto out;
		}

		/* If the object doesn't exist, create it */
		if(ias_obj == (struct ias_object *) NULL) {
			/* Create a new object */
			ias_obj = irias_new_object(ias_opt->irda_class_name,
						   jiffies);
			if (ias_obj == NULL) {
				kfree(ias_opt);
				err = -ENOMEM;
				goto out;
			}
			free_ias = 1;
		}

		/* Do we have the attribute already ? */
		if(irias_find_attrib(ias_obj, ias_opt->irda_attrib_name)) {
			kfree(ias_opt);
			if (free_ias) {
				kfree(ias_obj->name);
				kfree(ias_obj);
			}
			err = -EINVAL;
			goto out;
		}

		/* Look at the type */
		switch(ias_opt->irda_attrib_type) {
		case IAS_INTEGER:
			/* Add an integer attribute */
			irias_add_integer_attrib(
				ias_obj,
				ias_opt->irda_attrib_name,
				ias_opt->attribute.irda_attrib_int,
				IAS_USER_ATTR);
			break;
		case IAS_OCT_SEQ:
			/* Check length */
			if(ias_opt->attribute.irda_attrib_octet_seq.len >
			   IAS_MAX_OCTET_STRING) {
				kfree(ias_opt);
				if (free_ias) {
					kfree(ias_obj->name);
					kfree(ias_obj);
				}

				err = -EINVAL;
				goto out;
			}
			/* Add an octet sequence attribute */
			irias_add_octseq_attrib(
			      ias_obj,
			      ias_opt->irda_attrib_name,
			      ias_opt->attribute.irda_attrib_octet_seq.octet_seq,
			      ias_opt->attribute.irda_attrib_octet_seq.len,
			      IAS_USER_ATTR);
			break;
		case IAS_STRING:
			/* Should check charset & co */
			/* Check length */
			/* The length is encoded in a __u8, and
			 * IAS_MAX_STRING == 256, so there is no way
			 * userspace can pass us a string too large.
			 * Jean II */
			/* NULL terminate the string (avoid troubles) */
			ias_opt->attribute.irda_attrib_string.string[ias_opt->attribute.irda_attrib_string.len] = '\0';
			/* Add a string attribute */
			irias_add_string_attrib(
				ias_obj,
				ias_opt->irda_attrib_name,
				ias_opt->attribute.irda_attrib_string.string,
				IAS_USER_ATTR);
			break;
		default :
			kfree(ias_opt);
			if (free_ias) {
				kfree(ias_obj->name);
				kfree(ias_obj);
			}
			err = -EINVAL;
			goto out;
		}
		irias_insert_object(ias_obj);
		kfree(ias_opt);
		break;
	case IRLMP_IAS_DEL:
		/* The user want to delete an object from our local IAS
		 * database. We just need to query the IAS, check is the
		 * object is not owned by the kernel and delete it.
		 */

		if (optlen != sizeof(struct irda_ias_set)) {
			err = -EINVAL;
			goto out;
		}

		ias_opt = kmalloc(sizeof(struct irda_ias_set), GFP_ATOMIC);
		if (ias_opt == NULL) {
			err = -ENOMEM;
			goto out;
		}

		/* Copy query to the driver. */
		if (copy_from_user(ias_opt, optval, optlen)) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}

		/* Find the object we target.
		 * If the user gives us an empty string, we use the object
		 * associated with this socket. This will workaround
		 * duplicated class name - Jean II */
		if(ias_opt->irda_class_name[0] == '\0')
			ias_obj = self->ias_obj;
		else
			ias_obj = irias_find_object(ias_opt->irda_class_name);
		if(ias_obj == (struct ias_object *) NULL) {
			kfree(ias_opt);
			err = -EINVAL;
			goto out;
		}

		/* Only ROOT can mess with the global IAS database.
		 * Users can only del attributes from the object associated
		 * with the socket they own - Jean II */
		if((!capable(CAP_NET_ADMIN)) &&
		   ((ias_obj == NULL) || (ias_obj != self->ias_obj))) {
			kfree(ias_opt);
			err = -EPERM;
			goto out;
		}

		/* Find the attribute (in the object) we target */
		ias_attr = irias_find_attrib(ias_obj,
					     ias_opt->irda_attrib_name);
		if(ias_attr == (struct ias_attrib *) NULL) {
			kfree(ias_opt);
			err = -EINVAL;
			goto out;
		}

		/* Check is the user space own the object */
		if(ias_attr->value->owner != IAS_USER_ATTR) {
			IRDA_DEBUG(1, "%s(), attempting to delete a kernel attribute\n", __func__);
			kfree(ias_opt);
			err = -EPERM;
			goto out;
		}

		/* Remove the attribute (and maybe the object) */
		irias_delete_attrib(ias_obj, ias_attr, 1);
		kfree(ias_opt);
		break;
	case IRLMP_MAX_SDU_SIZE:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto out;
		}

		if (get_user(opt, (int __user *)optval)) {
			err = -EFAULT;
			goto out;
		}

		/* Only possible for a seqpacket service (TTP with SAR) */
		if (sk->sk_type != SOCK_SEQPACKET) {
			IRDA_DEBUG(2, "%s(), setting max_sdu_size = %d\n",
				   __func__, opt);
			self->max_sdu_size_rx = opt;
		} else {
			IRDA_WARNING("%s: not allowed to set MAXSDUSIZE for this socket type!\n",
				     __func__);
			err = -ENOPROTOOPT;
			goto out;
		}
		break;
	case IRLMP_HINTS_SET:
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto out;
		}

		/* The input is really a (__u8 hints[2]), easier as an int */
		if (get_user(opt, (int __user *)optval)) {
			err = -EFAULT;
			goto out;
		}

		/* Unregister any old registration */
		if (self->skey)
			irlmp_unregister_service(self->skey);

		self->skey = irlmp_register_service((__u16) opt);
		break;
	case IRLMP_HINT_MASK_SET:
		/* As opposed to the previous case which set the hint bits
		 * that we advertise, this one set the filter we use when
		 * making a discovery (nodes which don't match any hint
		 * bit in the mask are not reported).
		 */
		if (optlen < sizeof(int)) {
			err = -EINVAL;
			goto out;
		}

		/* The input is really a (__u8 hints[2]), easier as an int */
		if (get_user(opt, (int __user *)optval)) {
			err = -EFAULT;
			goto out;
		}

		/* Set the new hint mask */
		self->mask.word = (__u16) opt;
		/* Mask out extension bits */
		self->mask.word &= 0x7f7f;
		/* Check if no bits */
		if(!self->mask.word)
			self->mask.word = 0xFFFF;

		break;
	default:
		err = -ENOPROTOOPT;
		break;
	}

out:
	release_sock(sk);

	return err;
}

/*
 * Function irda_extract_ias_value(ias_opt, ias_value)
 *
 *    Translate internal IAS value structure to the user space representation
 *
 * The external representation of IAS values, as we exchange them with
 * user space program is quite different from the internal representation,
 * as stored in the IAS database (because we need a flat structure for
 * crossing kernel boundary).
 * This function transform the former in the latter. We also check
 * that the value type is valid.
 */
static int irda_extract_ias_value(struct irda_ias_set *ias_opt,
				  struct ias_value *ias_value)
{
	/* Look at the type */
	switch (ias_value->type) {
	case IAS_INTEGER:
		/* Copy the integer */
		ias_opt->attribute.irda_attrib_int = ias_value->t.integer;
		break;
	case IAS_OCT_SEQ:
		/* Set length */
		ias_opt->attribute.irda_attrib_octet_seq.len = ias_value->len;
		/* Copy over */
		memcpy(ias_opt->attribute.irda_attrib_octet_seq.octet_seq,
		       ias_value->t.oct_seq, ias_value->len);
		break;
	case IAS_STRING:
		/* Set length */
		ias_opt->attribute.irda_attrib_string.len = ias_value->len;
		ias_opt->attribute.irda_attrib_string.charset = ias_value->charset;
		/* Copy over */
		memcpy(ias_opt->attribute.irda_attrib_string.string,
		       ias_value->t.string, ias_value->len);
		/* NULL terminate the string (avoid troubles) */
		ias_opt->attribute.irda_attrib_string.string[ias_value->len] = '\0';
		break;
	case IAS_MISSING:
	default :
		return -EINVAL;
	}

	/* Copy type over */
	ias_opt->irda_attrib_type = ias_value->type;

	return 0;
}

/*
 * Function irda_getsockopt (sock, level, optname, optval, optlen)
 */
static int irda_getsockopt(struct socket *sock, int level, int optname,
			   char __user *optval, int __user *optlen)
{
	struct sock *sk = sock->sk;
	struct irda_sock *self = irda_sk(sk);
	struct irda_device_list list;
	struct irda_device_info *discoveries;
	struct irda_ias_set *	ias_opt;	/* IAS get/query params */
	struct ias_object *	ias_obj;	/* Object in IAS */
	struct ias_attrib *	ias_attr;	/* Attribute in IAS object */
	int daddr = DEV_ADDR_ANY;	/* Dest address for IAS queries */
	int val = 0;
	int len = 0;
	int err = 0;
	int offset, total;

	IRDA_DEBUG(2, "%s(%p)\n", __func__, self);

	if (level != SOL_IRLMP)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	if(len < 0)
		return -EINVAL;

	lock_sock(sk);

	switch (optname) {
	case IRLMP_ENUMDEVICES:
		/* Ask lmp for the current discovery log */
		discoveries = irlmp_get_discoveries(&list.len, self->mask.word,
						    self->nslots);
		/* Check if the we got some results */
		if (discoveries == NULL) {
			err = -EAGAIN;
			goto out;		/* Didn't find any devices */
		}

		/* Write total list length back to client */
		if (copy_to_user(optval, &list,
				 sizeof(struct irda_device_list) -
				 sizeof(struct irda_device_info)))
			err = -EFAULT;

		/* Offset to first device entry */
		offset = sizeof(struct irda_device_list) -
			sizeof(struct irda_device_info);

		/* Copy the list itself - watch for overflow */
		if (list.len > 2048) {
			err = -EINVAL;
			goto bed;
		}
		total = offset + (list.len * sizeof(struct irda_device_info));
		if (total > len)
			total = len;
		if (copy_to_user(optval+offset, discoveries, total - offset))
			err = -EFAULT;

		/* Write total number of bytes used back to client */
		if (put_user(total, optlen))
			err = -EFAULT;
bed:
		/* Free up our buffer */
		kfree(discoveries);
		break;
	case IRLMP_MAX_SDU_SIZE:
		val = self->max_data_size;
		len = sizeof(int);
		if (put_user(len, optlen)) {
			err = -EFAULT;
			goto out;
		}

		if (copy_to_user(optval, &val, len)) {
			err = -EFAULT;
			goto out;
		}

		break;
	case IRLMP_IAS_GET:
		/* The user want an object from our local IAS database.
		 * We just need to query the IAS and return the value
		 * that we found */

		/* Check that the user has allocated the right space for us */
		if (len != sizeof(struct irda_ias_set)) {
			err = -EINVAL;
			goto out;
		}

		ias_opt = kmalloc(sizeof(struct irda_ias_set), GFP_ATOMIC);
		if (ias_opt == NULL) {
			err = -ENOMEM;
			goto out;
		}

		/* Copy query to the driver. */
		if (copy_from_user(ias_opt, optval, len)) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}

		/* Find the object we target.
		 * If the user gives us an empty string, we use the object
		 * associated with this socket. This will workaround
		 * duplicated class name - Jean II */
		if(ias_opt->irda_class_name[0] == '\0')
			ias_obj = self->ias_obj;
		else
			ias_obj = irias_find_object(ias_opt->irda_class_name);
		if(ias_obj == (struct ias_object *) NULL) {
			kfree(ias_opt);
			err = -EINVAL;
			goto out;
		}

		/* Find the attribute (in the object) we target */
		ias_attr = irias_find_attrib(ias_obj,
					     ias_opt->irda_attrib_name);
		if(ias_attr == (struct ias_attrib *) NULL) {
			kfree(ias_opt);
			err = -EINVAL;
			goto out;
		}

		/* Translate from internal to user structure */
		err = irda_extract_ias_value(ias_opt, ias_attr->value);
		if(err) {
			kfree(ias_opt);
			goto out;
		}

		/* Copy reply to the user */
		if (copy_to_user(optval, ias_opt,
				 sizeof(struct irda_ias_set))) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}
		/* Note : don't need to put optlen, we checked it */
		kfree(ias_opt);
		break;
	case IRLMP_IAS_QUERY:
		/* The user want an object from a remote IAS database.
		 * We need to use IAP to query the remote database and
		 * then wait for the answer to come back. */

		/* Check that the user has allocated the right space for us */
		if (len != sizeof(struct irda_ias_set)) {
			err = -EINVAL;
			goto out;
		}

		ias_opt = kmalloc(sizeof(struct irda_ias_set), GFP_ATOMIC);
		if (ias_opt == NULL) {
			err = -ENOMEM;
			goto out;
		}

		/* Copy query to the driver. */
		if (copy_from_user(ias_opt, optval, len)) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}

		/* At this point, there are two cases...
		 * 1) the socket is connected - that's the easy case, we
		 *	just query the device we are connected to...
		 * 2) the socket is not connected - the user doesn't want
		 *	to connect and/or may not have a valid service name
		 *	(so can't create a fake connection). In this case,
		 *	we assume that the user pass us a valid destination
		 *	address in the requesting structure...
		 */
		if(self->daddr != DEV_ADDR_ANY) {
			/* We are connected - reuse known daddr */
			daddr = self->daddr;
		} else {
			/* We are not connected, we must specify a valid
			 * destination address */
			daddr = ias_opt->daddr;
			if((!daddr) || (daddr == DEV_ADDR_ANY)) {
				kfree(ias_opt);
				err = -EINVAL;
				goto out;
			}
		}

		/* Check that we can proceed with IAP */
		if (self->iriap) {
			IRDA_WARNING("%s: busy with a previous query\n",
				     __func__);
			kfree(ias_opt);
			err = -EBUSY;
			goto out;
		}

		self->iriap = iriap_open(LSAP_ANY, IAS_CLIENT, self,
					 irda_getvalue_confirm);

		if (self->iriap == NULL) {
			kfree(ias_opt);
			err = -ENOMEM;
			goto out;
		}

		/* Treat unexpected wakeup as disconnect */
		self->errno = -EHOSTUNREACH;

		/* Query remote LM-IAS */
		iriap_getvaluebyclass_request(self->iriap,
					      self->saddr, daddr,
					      ias_opt->irda_class_name,
					      ias_opt->irda_attrib_name);

		/* Wait for answer, if not yet finished (or failed) */
		if (wait_event_interruptible(self->query_wait,
					     (self->iriap == NULL))) {
			/* pending request uses copy of ias_opt-content
			 * we can free it regardless! */
			kfree(ias_opt);
			/* Treat signals as disconnect */
			err = -EHOSTUNREACH;
			goto out;
		}

		/* Check what happened */
		if (self->errno)
		{
			kfree(ias_opt);
			/* Requested object/attribute doesn't exist */
			if((self->errno == IAS_CLASS_UNKNOWN) ||
			   (self->errno == IAS_ATTRIB_UNKNOWN))
				err = -EADDRNOTAVAIL;
			else
				err = -EHOSTUNREACH;

			goto out;
		}

		/* Translate from internal to user structure */
		err = irda_extract_ias_value(ias_opt, self->ias_result);
		if (self->ias_result)
			irias_delete_value(self->ias_result);
		if (err) {
			kfree(ias_opt);
			goto out;
		}

		/* Copy reply to the user */
		if (copy_to_user(optval, ias_opt,
				 sizeof(struct irda_ias_set))) {
			kfree(ias_opt);
			err = -EFAULT;
			goto out;
		}
		/* Note : don't need to put optlen, we checked it */
		kfree(ias_opt);
		break;
	case IRLMP_WAITDEVICE:
		/* This function is just another way of seeing life ;-)
		 * IRLMP_ENUMDEVICES assumes that you have a static network,
		 * and that you just want to pick one of the devices present.
		 * On the other hand, in here we assume that no device is
		 * present and that at some point in the future a device will
		 * come into range. When this device arrive, we just wake
		 * up the caller, so that he has time to connect to it before
		 * the device goes away...
		 * Note : once the node has been discovered for more than a
		 * few second, it won't trigger this function, unless it
		 * goes away and come back changes its hint bits (so we
		 * might call it IRLMP_WAITNEWDEVICE).
		 */

		/* Check that the user is passing us an int */
		if (len != sizeof(int)) {
			err = -EINVAL;
			goto out;
		}
		/* Get timeout in ms (max time we block the caller) */
		if (get_user(val, (int __user *)optval)) {
			err = -EFAULT;
			goto out;
		}

		/* Tell IrLMP we want to be notified */
		irlmp_update_client(self->ckey, self->mask.word,
				    irda_selective_discovery_indication,
				    NULL, (void *) self);

		/* Do some discovery (and also return cached results) */
		irlmp_discovery_request(self->nslots);

		/* Wait until a node is discovered */
		if (!self->cachedaddr) {
			IRDA_DEBUG(1, "%s(), nothing discovered yet, going to sleep...\n", __func__);

			/* Set watchdog timer to expire in <val> ms. */
			self->errno = 0;
			setup_timer(&self->watchdog, irda_discovery_timeout,
					(unsigned long)self);
			self->watchdog.expires = jiffies + (val * HZ/1000);
			add_timer(&(self->watchdog));

			/* Wait for IR-LMP to call us back */
			__wait_event_interruptible(self->query_wait,
			      (self->cachedaddr != 0 || self->errno == -ETIME),
						   err);

			/* If watchdog is still activated, kill it! */
			if(timer_pending(&(self->watchdog)))
				del_timer(&(self->watchdog));

			IRDA_DEBUG(1, "%s(), ...waking up !\n", __func__);

			if (err != 0)
				goto out;
		}
		else
			IRDA_DEBUG(1, "%s(), found immediately !\n",
				   __func__);

		/* Tell IrLMP that we have been notified */
		irlmp_update_client(self->ckey, self->mask.word,
				    NULL, NULL, NULL);

		/* Check if the we got some results */
		if (!self->cachedaddr)
			return -EAGAIN;		/* Didn't find any devices */
		daddr = self->cachedaddr;
		/* Cleanup */
		self->cachedaddr = 0;

		/* We return the daddr of the device that trigger the
		 * wakeup. As irlmp pass us only the new devices, we
		 * are sure that it's not an old device.
		 * If the user want more details, he should query
		 * the whole discovery log and pick one device...
		 */
		if (put_user(daddr, (int __user *)optval)) {
			err = -EFAULT;
			goto out;
		}

		break;
	default:
		err = -ENOPROTOOPT;
	}

out:

	release_sock(sk);

	return err;
}

static const struct net_proto_family irda_family_ops = {
	.family = PF_IRDA,
	.create = irda_create,
	.owner	= THIS_MODULE,
};

static const struct proto_ops irda_stream_ops = {
	.family =	PF_IRDA,
	.owner =	THIS_MODULE,
	.release =	irda_release,
	.bind =		irda_bind,
	.connect =	irda_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	irda_accept,
	.getname =	irda_getname,
	.poll =		irda_poll,
	.ioctl =	irda_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	irda_compat_ioctl,
#endif
	.listen =	irda_listen,
	.shutdown =	irda_shutdown,
	.setsockopt =	irda_setsockopt,
	.getsockopt =	irda_getsockopt,
	.sendmsg =	irda_sendmsg,
	.recvmsg =	irda_recvmsg_stream,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static const struct proto_ops irda_seqpacket_ops = {
	.family =	PF_IRDA,
	.owner =	THIS_MODULE,
	.release =	irda_release,
	.bind =		irda_bind,
	.connect =	irda_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	irda_accept,
	.getname =	irda_getname,
	.poll =		datagram_poll,
	.ioctl =	irda_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	irda_compat_ioctl,
#endif
	.listen =	irda_listen,
	.shutdown =	irda_shutdown,
	.setsockopt =	irda_setsockopt,
	.getsockopt =	irda_getsockopt,
	.sendmsg =	irda_sendmsg,
	.recvmsg =	irda_recvmsg_dgram,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

static const struct proto_ops irda_dgram_ops = {
	.family =	PF_IRDA,
	.owner =	THIS_MODULE,
	.release =	irda_release,
	.bind =		irda_bind,
	.connect =	irda_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	irda_accept,
	.getname =	irda_getname,
	.poll =		datagram_poll,
	.ioctl =	irda_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	irda_compat_ioctl,
#endif
	.listen =	irda_listen,
	.shutdown =	irda_shutdown,
	.setsockopt =	irda_setsockopt,
	.getsockopt =	irda_getsockopt,
	.sendmsg =	irda_sendmsg_dgram,
	.recvmsg =	irda_recvmsg_dgram,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};

#ifdef CONFIG_IRDA_ULTRA
static const struct proto_ops irda_ultra_ops = {
	.family =	PF_IRDA,
	.owner =	THIS_MODULE,
	.release =	irda_release,
	.bind =		irda_bind,
	.connect =	sock_no_connect,
	.socketpair =	sock_no_socketpair,
	.accept =	sock_no_accept,
	.getname =	irda_getname,
	.poll =		datagram_poll,
	.ioctl =	irda_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl =	irda_compat_ioctl,
#endif
	.listen =	sock_no_listen,
	.shutdown =	irda_shutdown,
	.setsockopt =	irda_setsockopt,
	.getsockopt =	irda_getsockopt,
	.sendmsg =	irda_sendmsg_ultra,
	.recvmsg =	irda_recvmsg_dgram,
	.mmap =		sock_no_mmap,
	.sendpage =	sock_no_sendpage,
};
#endif /* CONFIG_IRDA_ULTRA */

/*
 * Function irsock_init (pro)
 *
 *    Initialize IrDA protocol
 *
 */
int __init irsock_init(void)
{
	int rc = proto_register(&irda_proto, 0);

	if (rc == 0)
		rc = sock_register(&irda_family_ops);

	return rc;
}

/*
 * Function irsock_cleanup (void)
 *
 *    Remove IrDA protocol
 *
 */
void irsock_cleanup(void)
{
	sock_unregister(PF_IRDA);
	proto_unregister(&irda_proto);
}
