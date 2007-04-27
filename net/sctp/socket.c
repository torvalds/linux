/* SCTP kernel reference Implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 Intel Corp.
 * Copyright (c) 2001-2002 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * These functions interface with the sockets layer to implement the
 * SCTP Extensions for the Sockets API.
 *
 * Note that the descriptions from the specification are USER level
 * functions--this file is the functions which populate the struct proto
 * for SCTP which is the BOTTOM of the sockets interface.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Narasimha Budihal     <narsi@refcode.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Daisy Chang           <daisyc@us.ibm.com>
 *    Sridhar Samudrala     <samudrala@us.ibm.com>
 *    Inaky Perez-Gonzalez  <inaky.gonzalez@intel.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *    Ryan Layer	    <rmlayer@us.ibm.com>
 *    Anup Pemmaiah         <pemmaiah@cc.usu.edu>
 *    Kevin Gao             <kevin.gao@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/ip.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/crypto.h>

#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/ipv6.h>
#include <net/inet_common.h>

#include <linux/socket.h> /* for sa_family_t */
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* WARNING:  Please do not remove the SCTP_STATIC attribute to
 * any of the functions below as they are used to export functions
 * used by a project regression testsuite.
 */

/* Forward declarations for internal helper functions. */
static int sctp_writeable(struct sock *sk);
static void sctp_wfree(struct sk_buff *skb);
static int sctp_wait_for_sndbuf(struct sctp_association *, long *timeo_p,
				size_t msg_len);
static int sctp_wait_for_packet(struct sock * sk, int *err, long *timeo_p);
static int sctp_wait_for_connect(struct sctp_association *, long *timeo_p);
static int sctp_wait_for_accept(struct sock *sk, long timeo);
static void sctp_wait_for_close(struct sock *sk, long timeo);
static struct sctp_af *sctp_sockaddr_af(struct sctp_sock *opt,
					union sctp_addr *addr, int len);
static int sctp_bindx_add(struct sock *, struct sockaddr *, int);
static int sctp_bindx_rem(struct sock *, struct sockaddr *, int);
static int sctp_send_asconf_add_ip(struct sock *, struct sockaddr *, int);
static int sctp_send_asconf_del_ip(struct sock *, struct sockaddr *, int);
static int sctp_send_asconf(struct sctp_association *asoc,
			    struct sctp_chunk *chunk);
static int sctp_do_bind(struct sock *, union sctp_addr *, int);
static int sctp_autobind(struct sock *sk);
static void sctp_sock_migrate(struct sock *, struct sock *,
			      struct sctp_association *, sctp_socket_type_t);
static char *sctp_hmac_alg = SCTP_COOKIE_HMAC_ALG;

extern struct kmem_cache *sctp_bucket_cachep;

/* Get the sndbuf space available at the time on the association.  */
static inline int sctp_wspace(struct sctp_association *asoc)
{
	struct sock *sk = asoc->base.sk;
	int amt = 0;

	if (asoc->ep->sndbuf_policy) {
		/* make sure that no association uses more than sk_sndbuf */
		amt = sk->sk_sndbuf - asoc->sndbuf_used;
	} else {
		/* do socket level accounting */
		amt = sk->sk_sndbuf - atomic_read(&sk->sk_wmem_alloc);
	}

	if (amt < 0)
		amt = 0;

	return amt;
}

/* Increment the used sndbuf space count of the corresponding association by
 * the size of the outgoing data chunk.
 * Also, set the skb destructor for sndbuf accounting later.
 *
 * Since it is always 1-1 between chunk and skb, and also a new skb is always
 * allocated for chunk bundling in sctp_packet_transmit(), we can use the
 * destructor in the data chunk skb for the purpose of the sndbuf space
 * tracking.
 */
static inline void sctp_set_owner_w(struct sctp_chunk *chunk)
{
	struct sctp_association *asoc = chunk->asoc;
	struct sock *sk = asoc->base.sk;

	/* The sndbuf space is tracked per association.  */
	sctp_association_hold(asoc);

	skb_set_owner_w(chunk->skb, sk);

	chunk->skb->destructor = sctp_wfree;
	/* Save the chunk pointer in skb for sctp_wfree to use later.  */
	*((struct sctp_chunk **)(chunk->skb->cb)) = chunk;

	asoc->sndbuf_used += SCTP_DATA_SNDSIZE(chunk) +
				sizeof(struct sk_buff) +
				sizeof(struct sctp_chunk);

	atomic_add(sizeof(struct sctp_chunk), &sk->sk_wmem_alloc);
}

/* Verify that this is a valid address. */
static inline int sctp_verify_addr(struct sock *sk, union sctp_addr *addr,
				   int len)
{
	struct sctp_af *af;

	/* Verify basic sockaddr. */
	af = sctp_sockaddr_af(sctp_sk(sk), addr, len);
	if (!af)
		return -EINVAL;

	/* Is this a valid SCTP address?  */
	if (!af->addr_valid(addr, sctp_sk(sk), NULL))
		return -EINVAL;

	if (!sctp_sk(sk)->pf->send_verify(sctp_sk(sk), (addr)))
		return -EINVAL;

	return 0;
}

/* Look up the association by its id.  If this is not a UDP-style
 * socket, the ID field is always ignored.
 */
struct sctp_association *sctp_id2assoc(struct sock *sk, sctp_assoc_t id)
{
	struct sctp_association *asoc = NULL;

	/* If this is not a UDP-style socket, assoc id should be ignored. */
	if (!sctp_style(sk, UDP)) {
		/* Return NULL if the socket state is not ESTABLISHED. It
		 * could be a TCP-style listening socket or a socket which
		 * hasn't yet called connect() to establish an association.
		 */
		if (!sctp_sstate(sk, ESTABLISHED))
			return NULL;

		/* Get the first and the only association from the list. */
		if (!list_empty(&sctp_sk(sk)->ep->asocs))
			asoc = list_entry(sctp_sk(sk)->ep->asocs.next,
					  struct sctp_association, asocs);
		return asoc;
	}

	/* Otherwise this is a UDP-style socket. */
	if (!id || (id == (sctp_assoc_t)-1))
		return NULL;

	spin_lock_bh(&sctp_assocs_id_lock);
	asoc = (struct sctp_association *)idr_find(&sctp_assocs_id, (int)id);
	spin_unlock_bh(&sctp_assocs_id_lock);

	if (!asoc || (asoc->base.sk != sk) || asoc->base.dead)
		return NULL;

	return asoc;
}

/* Look up the transport from an address and an assoc id. If both address and
 * id are specified, the associations matching the address and the id should be
 * the same.
 */
static struct sctp_transport *sctp_addr_id2transport(struct sock *sk,
					      struct sockaddr_storage *addr,
					      sctp_assoc_t id)
{
	struct sctp_association *addr_asoc = NULL, *id_asoc = NULL;
	struct sctp_transport *transport;
	union sctp_addr *laddr = (union sctp_addr *)addr;

	addr_asoc = sctp_endpoint_lookup_assoc(sctp_sk(sk)->ep,
					       laddr,
					       &transport);

	if (!addr_asoc)
		return NULL;

	id_asoc = sctp_id2assoc(sk, id);
	if (id_asoc && (id_asoc != addr_asoc))
		return NULL;

	sctp_get_pf_specific(sk->sk_family)->addr_v4map(sctp_sk(sk),
						(union sctp_addr *)addr);

	return transport;
}

/* API 3.1.2 bind() - UDP Style Syntax
 * The syntax of bind() is,
 *
 *   ret = bind(int sd, struct sockaddr *addr, int addrlen);
 *
 *   sd      - the socket descriptor returned by socket().
 *   addr    - the address structure (struct sockaddr_in or struct
 *             sockaddr_in6 [RFC 2553]),
 *   addr_len - the size of the address structure.
 */
SCTP_STATIC int sctp_bind(struct sock *sk, struct sockaddr *addr, int addr_len)
{
	int retval = 0;

	sctp_lock_sock(sk);

	SCTP_DEBUG_PRINTK("sctp_bind(sk: %p, addr: %p, addr_len: %d)\n",
			  sk, addr, addr_len);

	/* Disallow binding twice. */
	if (!sctp_sk(sk)->ep->base.bind_addr.port)
		retval = sctp_do_bind(sk, (union sctp_addr *)addr,
				      addr_len);
	else
		retval = -EINVAL;

	sctp_release_sock(sk);

	return retval;
}

static long sctp_get_port_local(struct sock *, union sctp_addr *);

/* Verify this is a valid sockaddr. */
static struct sctp_af *sctp_sockaddr_af(struct sctp_sock *opt,
					union sctp_addr *addr, int len)
{
	struct sctp_af *af;

	/* Check minimum size.  */
	if (len < sizeof (struct sockaddr))
		return NULL;

	/* Does this PF support this AF? */
	if (!opt->pf->af_supported(addr->sa.sa_family, opt))
		return NULL;

	/* If we get this far, af is valid. */
	af = sctp_get_af_specific(addr->sa.sa_family);

	if (len < af->sockaddr_len)
		return NULL;

	return af;
}

/* Bind a local address either to an endpoint or to an association.  */
SCTP_STATIC int sctp_do_bind(struct sock *sk, union sctp_addr *addr, int len)
{
	struct sctp_sock *sp = sctp_sk(sk);
	struct sctp_endpoint *ep = sp->ep;
	struct sctp_bind_addr *bp = &ep->base.bind_addr;
	struct sctp_af *af;
	unsigned short snum;
	int ret = 0;

	/* Common sockaddr verification. */
	af = sctp_sockaddr_af(sp, addr, len);
	if (!af) {
		SCTP_DEBUG_PRINTK("sctp_do_bind(sk: %p, newaddr: %p, len: %d) EINVAL\n",
				  sk, addr, len);
		return -EINVAL;
	}

	snum = ntohs(addr->v4.sin_port);

	SCTP_DEBUG_PRINTK_IPADDR("sctp_do_bind(sk: %p, new addr: ",
				 ", port: %d, new port: %d, len: %d)\n",
				 sk,
				 addr,
				 bp->port, snum,
				 len);

	/* PF specific bind() address verification. */
	if (!sp->pf->bind_verify(sp, addr))
		return -EADDRNOTAVAIL;

	/* We must either be unbound, or bind to the same port.  */
	if (bp->port && (snum != bp->port)) {
		SCTP_DEBUG_PRINTK("sctp_do_bind:"
				  " New port %d does not match existing port "
				  "%d.\n", snum, bp->port);
		return -EINVAL;
	}

	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	/* Make sure we are allowed to bind here.
	 * The function sctp_get_port_local() does duplicate address
	 * detection.
	 */
	if ((ret = sctp_get_port_local(sk, addr))) {
		if (ret == (long) sk) {
			/* This endpoint has a conflicting address. */
			return -EINVAL;
		} else {
			return -EADDRINUSE;
		}
	}

	/* Refresh ephemeral port.  */
	if (!bp->port)
		bp->port = inet_sk(sk)->num;

	/* Add the address to the bind address list.  */
	sctp_local_bh_disable();
	sctp_write_lock(&ep->base.addr_lock);

	/* Use GFP_ATOMIC since BHs are disabled.  */
	ret = sctp_add_bind_addr(bp, addr, 1, GFP_ATOMIC);
	sctp_write_unlock(&ep->base.addr_lock);
	sctp_local_bh_enable();

	/* Copy back into socket for getsockname() use. */
	if (!ret) {
		inet_sk(sk)->sport = htons(inet_sk(sk)->num);
		af->to_sk_saddr(addr, sk);
	}

	return ret;
}

 /* ADDIP Section 4.1.1 Congestion Control of ASCONF Chunks
 *
 * R1) One and only one ASCONF Chunk MAY be in transit and unacknowledged
 * at any one time.  If a sender, after sending an ASCONF chunk, decides
 * it needs to transfer another ASCONF Chunk, it MUST wait until the
 * ASCONF-ACK Chunk returns from the previous ASCONF Chunk before sending a
 * subsequent ASCONF. Note this restriction binds each side, so at any
 * time two ASCONF may be in-transit on any given association (one sent
 * from each endpoint).
 */
static int sctp_send_asconf(struct sctp_association *asoc,
			    struct sctp_chunk *chunk)
{
	int		retval = 0;

	/* If there is an outstanding ASCONF chunk, queue it for later
	 * transmission.
	 */
	if (asoc->addip_last_asconf) {
		list_add_tail(&chunk->list, &asoc->addip_chunk_list);
		goto out;
	}

	/* Hold the chunk until an ASCONF_ACK is received. */
	sctp_chunk_hold(chunk);
	retval = sctp_primitive_ASCONF(asoc, chunk);
	if (retval)
		sctp_chunk_free(chunk);
	else
		asoc->addip_last_asconf = chunk;

out:
	return retval;
}

/* Add a list of addresses as bind addresses to local endpoint or
 * association.
 *
 * Basically run through each address specified in the addrs/addrcnt
 * array/length pair, determine if it is IPv6 or IPv4 and call
 * sctp_do_bind() on it.
 *
 * If any of them fails, then the operation will be reversed and the
 * ones that were added will be removed.
 *
 * Only sctp_setsockopt_bindx() is supposed to call this function.
 */
int sctp_bindx_add(struct sock *sk, struct sockaddr *addrs, int addrcnt)
{
	int cnt;
	int retval = 0;
	void *addr_buf;
	struct sockaddr *sa_addr;
	struct sctp_af *af;

	SCTP_DEBUG_PRINTK("sctp_bindx_add (sk: %p, addrs: %p, addrcnt: %d)\n",
			  sk, addrs, addrcnt);

	addr_buf = addrs;
	for (cnt = 0; cnt < addrcnt; cnt++) {
		/* The list may contain either IPv4 or IPv6 address;
		 * determine the address length for walking thru the list.
		 */
		sa_addr = (struct sockaddr *)addr_buf;
		af = sctp_get_af_specific(sa_addr->sa_family);
		if (!af) {
			retval = -EINVAL;
			goto err_bindx_add;
		}

		retval = sctp_do_bind(sk, (union sctp_addr *)sa_addr,
				      af->sockaddr_len);

		addr_buf += af->sockaddr_len;

err_bindx_add:
		if (retval < 0) {
			/* Failed. Cleanup the ones that have been added */
			if (cnt > 0)
				sctp_bindx_rem(sk, addrs, cnt);
			return retval;
		}
	}

	return retval;
}

/* Send an ASCONF chunk with Add IP address parameters to all the peers of the
 * associations that are part of the endpoint indicating that a list of local
 * addresses are added to the endpoint.
 *
 * If any of the addresses is already in the bind address list of the
 * association, we do not send the chunk for that association.  But it will not
 * affect other associations.
 *
 * Only sctp_setsockopt_bindx() is supposed to call this function.
 */
static int sctp_send_asconf_add_ip(struct sock		*sk,
				   struct sockaddr	*addrs,
				   int 			addrcnt)
{
	struct sctp_sock		*sp;
	struct sctp_endpoint		*ep;
	struct sctp_association		*asoc;
	struct sctp_bind_addr		*bp;
	struct sctp_chunk		*chunk;
	struct sctp_sockaddr_entry	*laddr;
	union sctp_addr			*addr;
	union sctp_addr			saveaddr;
	void				*addr_buf;
	struct sctp_af			*af;
	struct list_head		*pos;
	struct list_head		*p;
	int 				i;
	int 				retval = 0;

	if (!sctp_addip_enable)
		return retval;

	sp = sctp_sk(sk);
	ep = sp->ep;

	SCTP_DEBUG_PRINTK("%s: (sk: %p, addrs: %p, addrcnt: %d)\n",
			  __FUNCTION__, sk, addrs, addrcnt);

	list_for_each(pos, &ep->asocs) {
		asoc = list_entry(pos, struct sctp_association, asocs);

		if (!asoc->peer.asconf_capable)
			continue;

		if (asoc->peer.addip_disabled_mask & SCTP_PARAM_ADD_IP)
			continue;

		if (!sctp_state(asoc, ESTABLISHED))
			continue;

		/* Check if any address in the packed array of addresses is
		 * in the bind address list of the association. If so,
		 * do not send the asconf chunk to its peer, but continue with
		 * other associations.
		 */
		addr_buf = addrs;
		for (i = 0; i < addrcnt; i++) {
			addr = (union sctp_addr *)addr_buf;
			af = sctp_get_af_specific(addr->v4.sin_family);
			if (!af) {
				retval = -EINVAL;
				goto out;
			}

			if (sctp_assoc_lookup_laddr(asoc, addr))
				break;

			addr_buf += af->sockaddr_len;
		}
		if (i < addrcnt)
			continue;

		/* Use the first address in bind addr list of association as
		 * Address Parameter of ASCONF CHUNK.
		 */
		sctp_read_lock(&asoc->base.addr_lock);
		bp = &asoc->base.bind_addr;
		p = bp->address_list.next;
		laddr = list_entry(p, struct sctp_sockaddr_entry, list);
		sctp_read_unlock(&asoc->base.addr_lock);

		chunk = sctp_make_asconf_update_ip(asoc, &laddr->a, addrs,
						   addrcnt, SCTP_PARAM_ADD_IP);
		if (!chunk) {
			retval = -ENOMEM;
			goto out;
		}

		retval = sctp_send_asconf(asoc, chunk);
		if (retval)
			goto out;

		/* Add the new addresses to the bind address list with
		 * use_as_src set to 0.
		 */
		sctp_local_bh_disable();
		sctp_write_lock(&asoc->base.addr_lock);
		addr_buf = addrs;
		for (i = 0; i < addrcnt; i++) {
			addr = (union sctp_addr *)addr_buf;
			af = sctp_get_af_specific(addr->v4.sin_family);
			memcpy(&saveaddr, addr, af->sockaddr_len);
			retval = sctp_add_bind_addr(bp, &saveaddr, 0,
						    GFP_ATOMIC);
			addr_buf += af->sockaddr_len;
		}
		sctp_write_unlock(&asoc->base.addr_lock);
		sctp_local_bh_enable();
	}

out:
	return retval;
}

/* Remove a list of addresses from bind addresses list.  Do not remove the
 * last address.
 *
 * Basically run through each address specified in the addrs/addrcnt
 * array/length pair, determine if it is IPv6 or IPv4 and call
 * sctp_del_bind() on it.
 *
 * If any of them fails, then the operation will be reversed and the
 * ones that were removed will be added back.
 *
 * At least one address has to be left; if only one address is
 * available, the operation will return -EBUSY.
 *
 * Only sctp_setsockopt_bindx() is supposed to call this function.
 */
int sctp_bindx_rem(struct sock *sk, struct sockaddr *addrs, int addrcnt)
{
	struct sctp_sock *sp = sctp_sk(sk);
	struct sctp_endpoint *ep = sp->ep;
	int cnt;
	struct sctp_bind_addr *bp = &ep->base.bind_addr;
	int retval = 0;
	void *addr_buf;
	union sctp_addr *sa_addr;
	struct sctp_af *af;

	SCTP_DEBUG_PRINTK("sctp_bindx_rem (sk: %p, addrs: %p, addrcnt: %d)\n",
			  sk, addrs, addrcnt);

	addr_buf = addrs;
	for (cnt = 0; cnt < addrcnt; cnt++) {
		/* If the bind address list is empty or if there is only one
		 * bind address, there is nothing more to be removed (we need
		 * at least one address here).
		 */
		if (list_empty(&bp->address_list) ||
		    (sctp_list_single_entry(&bp->address_list))) {
			retval = -EBUSY;
			goto err_bindx_rem;
		}

		sa_addr = (union sctp_addr *)addr_buf;
		af = sctp_get_af_specific(sa_addr->sa.sa_family);
		if (!af) {
			retval = -EINVAL;
			goto err_bindx_rem;
		}

		if (!af->addr_valid(sa_addr, sp, NULL)) {
			retval = -EADDRNOTAVAIL;
			goto err_bindx_rem;
		}

		if (sa_addr->v4.sin_port != htons(bp->port)) {
			retval = -EINVAL;
			goto err_bindx_rem;
		}

		/* FIXME - There is probably a need to check if sk->sk_saddr and
		 * sk->sk_rcv_addr are currently set to one of the addresses to
		 * be removed. This is something which needs to be looked into
		 * when we are fixing the outstanding issues with multi-homing
		 * socket routing and failover schemes. Refer to comments in
		 * sctp_do_bind(). -daisy
		 */
		sctp_local_bh_disable();
		sctp_write_lock(&ep->base.addr_lock);

		retval = sctp_del_bind_addr(bp, sa_addr);

		sctp_write_unlock(&ep->base.addr_lock);
		sctp_local_bh_enable();

		addr_buf += af->sockaddr_len;
err_bindx_rem:
		if (retval < 0) {
			/* Failed. Add the ones that has been removed back */
			if (cnt > 0)
				sctp_bindx_add(sk, addrs, cnt);
			return retval;
		}
	}

	return retval;
}

/* Send an ASCONF chunk with Delete IP address parameters to all the peers of
 * the associations that are part of the endpoint indicating that a list of
 * local addresses are removed from the endpoint.
 *
 * If any of the addresses is already in the bind address list of the
 * association, we do not send the chunk for that association.  But it will not
 * affect other associations.
 *
 * Only sctp_setsockopt_bindx() is supposed to call this function.
 */
static int sctp_send_asconf_del_ip(struct sock		*sk,
				   struct sockaddr	*addrs,
				   int			addrcnt)
{
	struct sctp_sock	*sp;
	struct sctp_endpoint	*ep;
	struct sctp_association	*asoc;
	struct sctp_transport	*transport;
	struct sctp_bind_addr	*bp;
	struct sctp_chunk	*chunk;
	union sctp_addr		*laddr;
	void			*addr_buf;
	struct sctp_af		*af;
	struct list_head	*pos, *pos1;
	struct sctp_sockaddr_entry *saddr;
	int 			i;
	int 			retval = 0;

	if (!sctp_addip_enable)
		return retval;

	sp = sctp_sk(sk);
	ep = sp->ep;

	SCTP_DEBUG_PRINTK("%s: (sk: %p, addrs: %p, addrcnt: %d)\n",
			  __FUNCTION__, sk, addrs, addrcnt);

	list_for_each(pos, &ep->asocs) {
		asoc = list_entry(pos, struct sctp_association, asocs);

		if (!asoc->peer.asconf_capable)
			continue;

		if (asoc->peer.addip_disabled_mask & SCTP_PARAM_DEL_IP)
			continue;

		if (!sctp_state(asoc, ESTABLISHED))
			continue;

		/* Check if any address in the packed array of addresses is
		 * not present in the bind address list of the association.
		 * If so, do not send the asconf chunk to its peer, but
		 * continue with other associations.
		 */
		addr_buf = addrs;
		for (i = 0; i < addrcnt; i++) {
			laddr = (union sctp_addr *)addr_buf;
			af = sctp_get_af_specific(laddr->v4.sin_family);
			if (!af) {
				retval = -EINVAL;
				goto out;
			}

			if (!sctp_assoc_lookup_laddr(asoc, laddr))
				break;

			addr_buf += af->sockaddr_len;
		}
		if (i < addrcnt)
			continue;

		/* Find one address in the association's bind address list
		 * that is not in the packed array of addresses. This is to
		 * make sure that we do not delete all the addresses in the
		 * association.
		 */
		sctp_read_lock(&asoc->base.addr_lock);
		bp = &asoc->base.bind_addr;
		laddr = sctp_find_unmatch_addr(bp, (union sctp_addr *)addrs,
					       addrcnt, sp);
		sctp_read_unlock(&asoc->base.addr_lock);
		if (!laddr)
			continue;

		chunk = sctp_make_asconf_update_ip(asoc, laddr, addrs, addrcnt,
						   SCTP_PARAM_DEL_IP);
		if (!chunk) {
			retval = -ENOMEM;
			goto out;
		}

		/* Reset use_as_src flag for the addresses in the bind address
		 * list that are to be deleted.
		 */
		sctp_local_bh_disable();
		sctp_write_lock(&asoc->base.addr_lock);
		addr_buf = addrs;
		for (i = 0; i < addrcnt; i++) {
			laddr = (union sctp_addr *)addr_buf;
			af = sctp_get_af_specific(laddr->v4.sin_family);
			list_for_each(pos1, &bp->address_list) {
				saddr = list_entry(pos1,
						   struct sctp_sockaddr_entry,
						   list);
				if (sctp_cmp_addr_exact(&saddr->a, laddr))
					saddr->use_as_src = 0;
			}
			addr_buf += af->sockaddr_len;
		}
		sctp_write_unlock(&asoc->base.addr_lock);
		sctp_local_bh_enable();

		/* Update the route and saddr entries for all the transports
		 * as some of the addresses in the bind address list are
		 * about to be deleted and cannot be used as source addresses.
		 */
		list_for_each(pos1, &asoc->peer.transport_addr_list) {
			transport = list_entry(pos1, struct sctp_transport,
					       transports);
			dst_release(transport->dst);
			sctp_transport_route(transport, NULL,
					     sctp_sk(asoc->base.sk));
		}

		retval = sctp_send_asconf(asoc, chunk);
	}
out:
	return retval;
}

/* Helper for tunneling sctp_bindx() requests through sctp_setsockopt()
 *
 * API 8.1
 * int sctp_bindx(int sd, struct sockaddr *addrs, int addrcnt,
 *                int flags);
 *
 * If sd is an IPv4 socket, the addresses passed must be IPv4 addresses.
 * If the sd is an IPv6 socket, the addresses passed can either be IPv4
 * or IPv6 addresses.
 *
 * A single address may be specified as INADDR_ANY or IN6ADDR_ANY, see
 * Section 3.1.2 for this usage.
 *
 * addrs is a pointer to an array of one or more socket addresses. Each
 * address is contained in its appropriate structure (i.e. struct
 * sockaddr_in or struct sockaddr_in6) the family of the address type
 * must be used to distinguish the address length (note that this
 * representation is termed a "packed array" of addresses). The caller
 * specifies the number of addresses in the array with addrcnt.
 *
 * On success, sctp_bindx() returns 0. On failure, sctp_bindx() returns
 * -1, and sets errno to the appropriate error code.
 *
 * For SCTP, the port given in each socket address must be the same, or
 * sctp_bindx() will fail, setting errno to EINVAL.
 *
 * The flags parameter is formed from the bitwise OR of zero or more of
 * the following currently defined flags:
 *
 * SCTP_BINDX_ADD_ADDR
 *
 * SCTP_BINDX_REM_ADDR
 *
 * SCTP_BINDX_ADD_ADDR directs SCTP to add the given addresses to the
 * association, and SCTP_BINDX_REM_ADDR directs SCTP to remove the given
 * addresses from the association. The two flags are mutually exclusive;
 * if both are given, sctp_bindx() will fail with EINVAL. A caller may
 * not remove all addresses from an association; sctp_bindx() will
 * reject such an attempt with EINVAL.
 *
 * An application can use sctp_bindx(SCTP_BINDX_ADD_ADDR) to associate
 * additional addresses with an endpoint after calling bind().  Or use
 * sctp_bindx(SCTP_BINDX_REM_ADDR) to remove some addresses a listening
 * socket is associated with so that no new association accepted will be
 * associated with those addresses. If the endpoint supports dynamic
 * address a SCTP_BINDX_REM_ADDR or SCTP_BINDX_ADD_ADDR may cause a
 * endpoint to send the appropriate message to the peer to change the
 * peers address lists.
 *
 * Adding and removing addresses from a connected association is
 * optional functionality. Implementations that do not support this
 * functionality should return EOPNOTSUPP.
 *
 * Basically do nothing but copying the addresses from user to kernel
 * land and invoking either sctp_bindx_add() or sctp_bindx_rem() on the sk.
 * This is used for tunneling the sctp_bindx() request through sctp_setsockopt()
 * from userspace.
 *
 * We don't use copy_from_user() for optimization: we first do the
 * sanity checks (buffer size -fast- and access check-healthy
 * pointer); if all of those succeed, then we can alloc the memory
 * (expensive operation) needed to copy the data to kernel. Then we do
 * the copying without checking the user space area
 * (__copy_from_user()).
 *
 * On exit there is no need to do sockfd_put(), sys_setsockopt() does
 * it.
 *
 * sk        The sk of the socket
 * addrs     The pointer to the addresses in user land
 * addrssize Size of the addrs buffer
 * op        Operation to perform (add or remove, see the flags of
 *           sctp_bindx)
 *
 * Returns 0 if ok, <0 errno code on error.
 */
SCTP_STATIC int sctp_setsockopt_bindx(struct sock* sk,
				      struct sockaddr __user *addrs,
				      int addrs_size, int op)
{
	struct sockaddr *kaddrs;
	int err;
	int addrcnt = 0;
	int walk_size = 0;
	struct sockaddr *sa_addr;
	void *addr_buf;
	struct sctp_af *af;

	SCTP_DEBUG_PRINTK("sctp_setsocktopt_bindx: sk %p addrs %p"
			  " addrs_size %d opt %d\n", sk, addrs, addrs_size, op);

	if (unlikely(addrs_size <= 0))
		return -EINVAL;

	/* Check the user passed a healthy pointer.  */
	if (unlikely(!access_ok(VERIFY_READ, addrs, addrs_size)))
		return -EFAULT;

	/* Alloc space for the address array in kernel memory.  */
	kaddrs = kmalloc(addrs_size, GFP_KERNEL);
	if (unlikely(!kaddrs))
		return -ENOMEM;

	if (__copy_from_user(kaddrs, addrs, addrs_size)) {
		kfree(kaddrs);
		return -EFAULT;
	}

	/* Walk through the addrs buffer and count the number of addresses. */
	addr_buf = kaddrs;
	while (walk_size < addrs_size) {
		sa_addr = (struct sockaddr *)addr_buf;
		af = sctp_get_af_specific(sa_addr->sa_family);

		/* If the address family is not supported or if this address
		 * causes the address buffer to overflow return EINVAL.
		 */
		if (!af || (walk_size + af->sockaddr_len) > addrs_size) {
			kfree(kaddrs);
			return -EINVAL;
		}
		addrcnt++;
		addr_buf += af->sockaddr_len;
		walk_size += af->sockaddr_len;
	}

	/* Do the work. */
	switch (op) {
	case SCTP_BINDX_ADD_ADDR:
		err = sctp_bindx_add(sk, kaddrs, addrcnt);
		if (err)
			goto out;
		err = sctp_send_asconf_add_ip(sk, kaddrs, addrcnt);
		break;

	case SCTP_BINDX_REM_ADDR:
		err = sctp_bindx_rem(sk, kaddrs, addrcnt);
		if (err)
			goto out;
		err = sctp_send_asconf_del_ip(sk, kaddrs, addrcnt);
		break;

	default:
		err = -EINVAL;
		break;
	}

out:
	kfree(kaddrs);

	return err;
}

/* __sctp_connect(struct sock* sk, struct sockaddr *kaddrs, int addrs_size)
 *
 * Common routine for handling connect() and sctp_connectx().
 * Connect will come in with just a single address.
 */
static int __sctp_connect(struct sock* sk,
			  struct sockaddr *kaddrs,
			  int addrs_size)
{
	struct sctp_sock *sp;
	struct sctp_endpoint *ep;
	struct sctp_association *asoc = NULL;
	struct sctp_association *asoc2;
	struct sctp_transport *transport;
	union sctp_addr to;
	struct sctp_af *af;
	sctp_scope_t scope;
	long timeo;
	int err = 0;
	int addrcnt = 0;
	int walk_size = 0;
	union sctp_addr *sa_addr;
	void *addr_buf;

	sp = sctp_sk(sk);
	ep = sp->ep;

	/* connect() cannot be done on a socket that is already in ESTABLISHED
	 * state - UDP-style peeled off socket or a TCP-style socket that
	 * is already connected.
	 * It cannot be done even on a TCP-style listening socket.
	 */
	if (sctp_sstate(sk, ESTABLISHED) ||
	    (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING))) {
		err = -EISCONN;
		goto out_free;
	}

	/* Walk through the addrs buffer and count the number of addresses. */
	addr_buf = kaddrs;
	while (walk_size < addrs_size) {
		sa_addr = (union sctp_addr *)addr_buf;
		af = sctp_get_af_specific(sa_addr->sa.sa_family);

		/* If the address family is not supported or if this address
		 * causes the address buffer to overflow return EINVAL.
		 */
		if (!af || (walk_size + af->sockaddr_len) > addrs_size) {
			err = -EINVAL;
			goto out_free;
		}

		err = sctp_verify_addr(sk, sa_addr, af->sockaddr_len);
		if (err)
			goto out_free;

		memcpy(&to, sa_addr, af->sockaddr_len);

		/* Check if there already is a matching association on the
		 * endpoint (other than the one created here).
		 */
		asoc2 = sctp_endpoint_lookup_assoc(ep, sa_addr, &transport);
		if (asoc2 && asoc2 != asoc) {
			if (asoc2->state >= SCTP_STATE_ESTABLISHED)
				err = -EISCONN;
			else
				err = -EALREADY;
			goto out_free;
		}

		/* If we could not find a matching association on the endpoint,
		 * make sure that there is no peeled-off association matching
		 * the peer address even on another socket.
		 */
		if (sctp_endpoint_is_peeled_off(ep, sa_addr)) {
			err = -EADDRNOTAVAIL;
			goto out_free;
		}

		if (!asoc) {
			/* If a bind() or sctp_bindx() is not called prior to
			 * an sctp_connectx() call, the system picks an
			 * ephemeral port and will choose an address set
			 * equivalent to binding with a wildcard address.
			 */
			if (!ep->base.bind_addr.port) {
				if (sctp_autobind(sk)) {
					err = -EAGAIN;
					goto out_free;
				}
			} else {
				/*
				 * If an unprivileged user inherits a 1-many
				 * style socket with open associations on a
				 * privileged port, it MAY be permitted to
				 * accept new associations, but it SHOULD NOT
				 * be permitted to open new associations.
				 */
				if (ep->base.bind_addr.port < PROT_SOCK &&
				    !capable(CAP_NET_BIND_SERVICE)) {
					err = -EACCES;
					goto out_free;
				}
			}

			scope = sctp_scope(sa_addr);
			asoc = sctp_association_new(ep, sk, scope, GFP_KERNEL);
			if (!asoc) {
				err = -ENOMEM;
				goto out_free;
			}
		}

		/* Prime the peer's transport structures.  */
		transport = sctp_assoc_add_peer(asoc, sa_addr, GFP_KERNEL,
						SCTP_UNKNOWN);
		if (!transport) {
			err = -ENOMEM;
			goto out_free;
		}

		addrcnt++;
		addr_buf += af->sockaddr_len;
		walk_size += af->sockaddr_len;
	}

	err = sctp_assoc_set_bind_addr_from_ep(asoc, GFP_KERNEL);
	if (err < 0) {
		goto out_free;
	}

	err = sctp_primitive_ASSOCIATE(asoc, NULL);
	if (err < 0) {
		goto out_free;
	}

	/* Initialize sk's dport and daddr for getpeername() */
	inet_sk(sk)->dport = htons(asoc->peer.port);
	af = sctp_get_af_specific(to.sa.sa_family);
	af->to_sk_daddr(&to, sk);
	sk->sk_err = 0;

	timeo = sock_sndtimeo(sk, sk->sk_socket->file->f_flags & O_NONBLOCK);
	err = sctp_wait_for_connect(asoc, &timeo);

	/* Don't free association on exit. */
	asoc = NULL;

out_free:

	SCTP_DEBUG_PRINTK("About to exit __sctp_connect() free asoc: %p"
			  " kaddrs: %p err: %d\n",
			  asoc, kaddrs, err);
	if (asoc)
		sctp_association_free(asoc);
	return err;
}

/* Helper for tunneling sctp_connectx() requests through sctp_setsockopt()
 *
 * API 8.9
 * int sctp_connectx(int sd, struct sockaddr *addrs, int addrcnt);
 *
 * If sd is an IPv4 socket, the addresses passed must be IPv4 addresses.
 * If the sd is an IPv6 socket, the addresses passed can either be IPv4
 * or IPv6 addresses.
 *
 * A single address may be specified as INADDR_ANY or IN6ADDR_ANY, see
 * Section 3.1.2 for this usage.
 *
 * addrs is a pointer to an array of one or more socket addresses. Each
 * address is contained in its appropriate structure (i.e. struct
 * sockaddr_in or struct sockaddr_in6) the family of the address type
 * must be used to distengish the address length (note that this
 * representation is termed a "packed array" of addresses). The caller
 * specifies the number of addresses in the array with addrcnt.
 *
 * On success, sctp_connectx() returns 0. On failure, sctp_connectx() returns
 * -1, and sets errno to the appropriate error code.
 *
 * For SCTP, the port given in each socket address must be the same, or
 * sctp_connectx() will fail, setting errno to EINVAL.
 *
 * An application can use sctp_connectx to initiate an association with
 * an endpoint that is multi-homed.  Much like sctp_bindx() this call
 * allows a caller to specify multiple addresses at which a peer can be
 * reached.  The way the SCTP stack uses the list of addresses to set up
 * the association is implementation dependant.  This function only
 * specifies that the stack will try to make use of all the addresses in
 * the list when needed.
 *
 * Note that the list of addresses passed in is only used for setting up
 * the association.  It does not necessarily equal the set of addresses
 * the peer uses for the resulting association.  If the caller wants to
 * find out the set of peer addresses, it must use sctp_getpaddrs() to
 * retrieve them after the association has been set up.
 *
 * Basically do nothing but copying the addresses from user to kernel
 * land and invoking either sctp_connectx(). This is used for tunneling
 * the sctp_connectx() request through sctp_setsockopt() from userspace.
 *
 * We don't use copy_from_user() for optimization: we first do the
 * sanity checks (buffer size -fast- and access check-healthy
 * pointer); if all of those succeed, then we can alloc the memory
 * (expensive operation) needed to copy the data to kernel. Then we do
 * the copying without checking the user space area
 * (__copy_from_user()).
 *
 * On exit there is no need to do sockfd_put(), sys_setsockopt() does
 * it.
 *
 * sk        The sk of the socket
 * addrs     The pointer to the addresses in user land
 * addrssize Size of the addrs buffer
 *
 * Returns 0 if ok, <0 errno code on error.
 */
SCTP_STATIC int sctp_setsockopt_connectx(struct sock* sk,
				      struct sockaddr __user *addrs,
				      int addrs_size)
{
	int err = 0;
	struct sockaddr *kaddrs;

	SCTP_DEBUG_PRINTK("%s - sk %p addrs %p addrs_size %d\n",
			  __FUNCTION__, sk, addrs, addrs_size);

	if (unlikely(addrs_size <= 0))
		return -EINVAL;

	/* Check the user passed a healthy pointer.  */
	if (unlikely(!access_ok(VERIFY_READ, addrs, addrs_size)))
		return -EFAULT;

	/* Alloc space for the address array in kernel memory.  */
	kaddrs = kmalloc(addrs_size, GFP_KERNEL);
	if (unlikely(!kaddrs))
		return -ENOMEM;

	if (__copy_from_user(kaddrs, addrs, addrs_size)) {
		err = -EFAULT;
	} else {
		err = __sctp_connect(sk, kaddrs, addrs_size);
	}

	kfree(kaddrs);
	return err;
}

/* API 3.1.4 close() - UDP Style Syntax
 * Applications use close() to perform graceful shutdown (as described in
 * Section 10.1 of [SCTP]) on ALL the associations currently represented
 * by a UDP-style socket.
 *
 * The syntax is
 *
 *   ret = close(int sd);
 *
 *   sd      - the socket descriptor of the associations to be closed.
 *
 * To gracefully shutdown a specific association represented by the
 * UDP-style socket, an application should use the sendmsg() call,
 * passing no user data, but including the appropriate flag in the
 * ancillary data (see Section xxxx).
 *
 * If sd in the close() call is a branched-off socket representing only
 * one association, the shutdown is performed on that association only.
 *
 * 4.1.6 close() - TCP Style Syntax
 *
 * Applications use close() to gracefully close down an association.
 *
 * The syntax is:
 *
 *    int close(int sd);
 *
 *      sd      - the socket descriptor of the association to be closed.
 *
 * After an application calls close() on a socket descriptor, no further
 * socket operations will succeed on that descriptor.
 *
 * API 7.1.4 SO_LINGER
 *
 * An application using the TCP-style socket can use this option to
 * perform the SCTP ABORT primitive.  The linger option structure is:
 *
 *  struct  linger {
 *     int     l_onoff;                // option on/off
 *     int     l_linger;               // linger time
 * };
 *
 * To enable the option, set l_onoff to 1.  If the l_linger value is set
 * to 0, calling close() is the same as the ABORT primitive.  If the
 * value is set to a negative value, the setsockopt() call will return
 * an error.  If the value is set to a positive value linger_time, the
 * close() can be blocked for at most linger_time ms.  If the graceful
 * shutdown phase does not finish during this period, close() will
 * return but the graceful shutdown phase continues in the system.
 */
SCTP_STATIC void sctp_close(struct sock *sk, long timeout)
{
	struct sctp_endpoint *ep;
	struct sctp_association *asoc;
	struct list_head *pos, *temp;

	SCTP_DEBUG_PRINTK("sctp_close(sk: 0x%p, timeout:%ld)\n", sk, timeout);

	sctp_lock_sock(sk);
	sk->sk_shutdown = SHUTDOWN_MASK;

	ep = sctp_sk(sk)->ep;

	/* Walk all associations on an endpoint.  */
	list_for_each_safe(pos, temp, &ep->asocs) {
		asoc = list_entry(pos, struct sctp_association, asocs);

		if (sctp_style(sk, TCP)) {
			/* A closed association can still be in the list if
			 * it belongs to a TCP-style listening socket that is
			 * not yet accepted. If so, free it. If not, send an
			 * ABORT or SHUTDOWN based on the linger options.
			 */
			if (sctp_state(asoc, CLOSED)) {
				sctp_unhash_established(asoc);
				sctp_association_free(asoc);
				continue;
			}
		}

		if (sock_flag(sk, SOCK_LINGER) && !sk->sk_lingertime) {
			struct sctp_chunk *chunk;

			chunk = sctp_make_abort_user(asoc, NULL, 0);
			if (chunk)
				sctp_primitive_ABORT(asoc, chunk);
		} else
			sctp_primitive_SHUTDOWN(asoc, NULL);
	}

	/* Clean up any skbs sitting on the receive queue.  */
	sctp_queue_purge_ulpevents(&sk->sk_receive_queue);
	sctp_queue_purge_ulpevents(&sctp_sk(sk)->pd_lobby);

	/* On a TCP-style socket, block for at most linger_time if set. */
	if (sctp_style(sk, TCP) && timeout)
		sctp_wait_for_close(sk, timeout);

	/* This will run the backlog queue.  */
	sctp_release_sock(sk);

	/* Supposedly, no process has access to the socket, but
	 * the net layers still may.
	 */
	sctp_local_bh_disable();
	sctp_bh_lock_sock(sk);

	/* Hold the sock, since sk_common_release() will put sock_put()
	 * and we have just a little more cleanup.
	 */
	sock_hold(sk);
	sk_common_release(sk);

	sctp_bh_unlock_sock(sk);
	sctp_local_bh_enable();

	sock_put(sk);

	SCTP_DBG_OBJCNT_DEC(sock);
}

/* Handle EPIPE error. */
static int sctp_error(struct sock *sk, int flags, int err)
{
	if (err == -EPIPE)
		err = sock_error(sk) ? : -EPIPE;
	if (err == -EPIPE && !(flags & MSG_NOSIGNAL))
		send_sig(SIGPIPE, current, 0);
	return err;
}

/* API 3.1.3 sendmsg() - UDP Style Syntax
 *
 * An application uses sendmsg() and recvmsg() calls to transmit data to
 * and receive data from its peer.
 *
 *  ssize_t sendmsg(int socket, const struct msghdr *message,
 *                  int flags);
 *
 *  socket  - the socket descriptor of the endpoint.
 *  message - pointer to the msghdr structure which contains a single
 *            user message and possibly some ancillary data.
 *
 *            See Section 5 for complete description of the data
 *            structures.
 *
 *  flags   - flags sent or received with the user message, see Section
 *            5 for complete description of the flags.
 *
 * Note:  This function could use a rewrite especially when explicit
 * connect support comes in.
 */
/* BUG:  We do not implement the equivalent of sk_stream_wait_memory(). */

SCTP_STATIC int sctp_msghdr_parse(const struct msghdr *, sctp_cmsgs_t *);

SCTP_STATIC int sctp_sendmsg(struct kiocb *iocb, struct sock *sk,
			     struct msghdr *msg, size_t msg_len)
{
	struct sctp_sock *sp;
	struct sctp_endpoint *ep;
	struct sctp_association *new_asoc=NULL, *asoc=NULL;
	struct sctp_transport *transport, *chunk_tp;
	struct sctp_chunk *chunk;
	union sctp_addr to;
	struct sockaddr *msg_name = NULL;
	struct sctp_sndrcvinfo default_sinfo = { 0 };
	struct sctp_sndrcvinfo *sinfo;
	struct sctp_initmsg *sinit;
	sctp_assoc_t associd = 0;
	sctp_cmsgs_t cmsgs = { NULL };
	int err;
	sctp_scope_t scope;
	long timeo;
	__u16 sinfo_flags = 0;
	struct sctp_datamsg *datamsg;
	struct list_head *pos;
	int msg_flags = msg->msg_flags;

	SCTP_DEBUG_PRINTK("sctp_sendmsg(sk: %p, msg: %p, msg_len: %zu)\n",
			  sk, msg, msg_len);

	err = 0;
	sp = sctp_sk(sk);
	ep = sp->ep;

	SCTP_DEBUG_PRINTK("Using endpoint: %p.\n", ep);

	/* We cannot send a message over a TCP-style listening socket. */
	if (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING)) {
		err = -EPIPE;
		goto out_nounlock;
	}

	/* Parse out the SCTP CMSGs.  */
	err = sctp_msghdr_parse(msg, &cmsgs);

	if (err) {
		SCTP_DEBUG_PRINTK("msghdr parse err = %x\n", err);
		goto out_nounlock;
	}

	/* Fetch the destination address for this packet.  This
	 * address only selects the association--it is not necessarily
	 * the address we will send to.
	 * For a peeled-off socket, msg_name is ignored.
	 */
	if (!sctp_style(sk, UDP_HIGH_BANDWIDTH) && msg->msg_name) {
		int msg_namelen = msg->msg_namelen;

		err = sctp_verify_addr(sk, (union sctp_addr *)msg->msg_name,
				       msg_namelen);
		if (err)
			return err;

		if (msg_namelen > sizeof(to))
			msg_namelen = sizeof(to);
		memcpy(&to, msg->msg_name, msg_namelen);
		msg_name = msg->msg_name;
	}

	sinfo = cmsgs.info;
	sinit = cmsgs.init;

	/* Did the user specify SNDRCVINFO?  */
	if (sinfo) {
		sinfo_flags = sinfo->sinfo_flags;
		associd = sinfo->sinfo_assoc_id;
	}

	SCTP_DEBUG_PRINTK("msg_len: %zu, sinfo_flags: 0x%x\n",
			  msg_len, sinfo_flags);

	/* SCTP_EOF or SCTP_ABORT cannot be set on a TCP-style socket. */
	if (sctp_style(sk, TCP) && (sinfo_flags & (SCTP_EOF | SCTP_ABORT))) {
		err = -EINVAL;
		goto out_nounlock;
	}

	/* If SCTP_EOF is set, no data can be sent. Disallow sending zero
	 * length messages when SCTP_EOF|SCTP_ABORT is not set.
	 * If SCTP_ABORT is set, the message length could be non zero with
	 * the msg_iov set to the user abort reason.
	 */
	if (((sinfo_flags & SCTP_EOF) && (msg_len > 0)) ||
	    (!(sinfo_flags & (SCTP_EOF|SCTP_ABORT)) && (msg_len == 0))) {
		err = -EINVAL;
		goto out_nounlock;
	}

	/* If SCTP_ADDR_OVER is set, there must be an address
	 * specified in msg_name.
	 */
	if ((sinfo_flags & SCTP_ADDR_OVER) && (!msg->msg_name)) {
		err = -EINVAL;
		goto out_nounlock;
	}

	transport = NULL;

	SCTP_DEBUG_PRINTK("About to look up association.\n");

	sctp_lock_sock(sk);

	/* If a msg_name has been specified, assume this is to be used.  */
	if (msg_name) {
		/* Look for a matching association on the endpoint. */
		asoc = sctp_endpoint_lookup_assoc(ep, &to, &transport);
		if (!asoc) {
			/* If we could not find a matching association on the
			 * endpoint, make sure that it is not a TCP-style
			 * socket that already has an association or there is
			 * no peeled-off association on another socket.
			 */
			if ((sctp_style(sk, TCP) &&
			     sctp_sstate(sk, ESTABLISHED)) ||
			    sctp_endpoint_is_peeled_off(ep, &to)) {
				err = -EADDRNOTAVAIL;
				goto out_unlock;
			}
		}
	} else {
		asoc = sctp_id2assoc(sk, associd);
		if (!asoc) {
			err = -EPIPE;
			goto out_unlock;
		}
	}

	if (asoc) {
		SCTP_DEBUG_PRINTK("Just looked up association: %p.\n", asoc);

		/* We cannot send a message on a TCP-style SCTP_SS_ESTABLISHED
		 * socket that has an association in CLOSED state. This can
		 * happen when an accepted socket has an association that is
		 * already CLOSED.
		 */
		if (sctp_state(asoc, CLOSED) && sctp_style(sk, TCP)) {
			err = -EPIPE;
			goto out_unlock;
		}

		if (sinfo_flags & SCTP_EOF) {
			SCTP_DEBUG_PRINTK("Shutting down association: %p\n",
					  asoc);
			sctp_primitive_SHUTDOWN(asoc, NULL);
			err = 0;
			goto out_unlock;
		}
		if (sinfo_flags & SCTP_ABORT) {
			struct sctp_chunk *chunk;

			chunk = sctp_make_abort_user(asoc, msg, msg_len);
			if (!chunk) {
				err = -ENOMEM;
				goto out_unlock;
			}

			SCTP_DEBUG_PRINTK("Aborting association: %p\n", asoc);
			sctp_primitive_ABORT(asoc, chunk);
			err = 0;
			goto out_unlock;
		}
	}

	/* Do we need to create the association?  */
	if (!asoc) {
		SCTP_DEBUG_PRINTK("There is no association yet.\n");

		if (sinfo_flags & (SCTP_EOF | SCTP_ABORT)) {
			err = -EINVAL;
			goto out_unlock;
		}

		/* Check for invalid stream against the stream counts,
		 * either the default or the user specified stream counts.
		 */
		if (sinfo) {
			if (!sinit || (sinit && !sinit->sinit_num_ostreams)) {
				/* Check against the defaults. */
				if (sinfo->sinfo_stream >=
				    sp->initmsg.sinit_num_ostreams) {
					err = -EINVAL;
					goto out_unlock;
				}
			} else {
				/* Check against the requested.  */
				if (sinfo->sinfo_stream >=
				    sinit->sinit_num_ostreams) {
					err = -EINVAL;
					goto out_unlock;
				}
			}
		}

		/*
		 * API 3.1.2 bind() - UDP Style Syntax
		 * If a bind() or sctp_bindx() is not called prior to a
		 * sendmsg() call that initiates a new association, the
		 * system picks an ephemeral port and will choose an address
		 * set equivalent to binding with a wildcard address.
		 */
		if (!ep->base.bind_addr.port) {
			if (sctp_autobind(sk)) {
				err = -EAGAIN;
				goto out_unlock;
			}
		} else {
			/*
			 * If an unprivileged user inherits a one-to-many
			 * style socket with open associations on a privileged
			 * port, it MAY be permitted to accept new associations,
			 * but it SHOULD NOT be permitted to open new
			 * associations.
			 */
			if (ep->base.bind_addr.port < PROT_SOCK &&
			    !capable(CAP_NET_BIND_SERVICE)) {
				err = -EACCES;
				goto out_unlock;
			}
		}

		scope = sctp_scope(&to);
		new_asoc = sctp_association_new(ep, sk, scope, GFP_KERNEL);
		if (!new_asoc) {
			err = -ENOMEM;
			goto out_unlock;
		}
		asoc = new_asoc;

		/* If the SCTP_INIT ancillary data is specified, set all
		 * the association init values accordingly.
		 */
		if (sinit) {
			if (sinit->sinit_num_ostreams) {
				asoc->c.sinit_num_ostreams =
					sinit->sinit_num_ostreams;
			}
			if (sinit->sinit_max_instreams) {
				asoc->c.sinit_max_instreams =
					sinit->sinit_max_instreams;
			}
			if (sinit->sinit_max_attempts) {
				asoc->max_init_attempts
					= sinit->sinit_max_attempts;
			}
			if (sinit->sinit_max_init_timeo) {
				asoc->max_init_timeo =
				 msecs_to_jiffies(sinit->sinit_max_init_timeo);
			}
		}

		/* Prime the peer's transport structures.  */
		transport = sctp_assoc_add_peer(asoc, &to, GFP_KERNEL, SCTP_UNKNOWN);
		if (!transport) {
			err = -ENOMEM;
			goto out_free;
		}
		err = sctp_assoc_set_bind_addr_from_ep(asoc, GFP_KERNEL);
		if (err < 0) {
			err = -ENOMEM;
			goto out_free;
		}
	}

	/* ASSERT: we have a valid association at this point.  */
	SCTP_DEBUG_PRINTK("We have a valid association.\n");

	if (!sinfo) {
		/* If the user didn't specify SNDRCVINFO, make up one with
		 * some defaults.
		 */
		default_sinfo.sinfo_stream = asoc->default_stream;
		default_sinfo.sinfo_flags = asoc->default_flags;
		default_sinfo.sinfo_ppid = asoc->default_ppid;
		default_sinfo.sinfo_context = asoc->default_context;
		default_sinfo.sinfo_timetolive = asoc->default_timetolive;
		default_sinfo.sinfo_assoc_id = sctp_assoc2id(asoc);
		sinfo = &default_sinfo;
	}

	/* API 7.1.7, the sndbuf size per association bounds the
	 * maximum size of data that can be sent in a single send call.
	 */
	if (msg_len > sk->sk_sndbuf) {
		err = -EMSGSIZE;
		goto out_free;
	}

	/* If fragmentation is disabled and the message length exceeds the
	 * association fragmentation point, return EMSGSIZE.  The I-D
	 * does not specify what this error is, but this looks like
	 * a great fit.
	 */
	if (sctp_sk(sk)->disable_fragments && (msg_len > asoc->frag_point)) {
		err = -EMSGSIZE;
		goto out_free;
	}

	if (sinfo) {
		/* Check for invalid stream. */
		if (sinfo->sinfo_stream >= asoc->c.sinit_num_ostreams) {
			err = -EINVAL;
			goto out_free;
		}
	}

	timeo = sock_sndtimeo(sk, msg->msg_flags & MSG_DONTWAIT);
	if (!sctp_wspace(asoc)) {
		err = sctp_wait_for_sndbuf(asoc, &timeo, msg_len);
		if (err)
			goto out_free;
	}

	/* If an address is passed with the sendto/sendmsg call, it is used
	 * to override the primary destination address in the TCP model, or
	 * when SCTP_ADDR_OVER flag is set in the UDP model.
	 */
	if ((sctp_style(sk, TCP) && msg_name) ||
	    (sinfo_flags & SCTP_ADDR_OVER)) {
		chunk_tp = sctp_assoc_lookup_paddr(asoc, &to);
		if (!chunk_tp) {
			err = -EINVAL;
			goto out_free;
		}
	} else
		chunk_tp = NULL;

	/* Auto-connect, if we aren't connected already. */
	if (sctp_state(asoc, CLOSED)) {
		err = sctp_primitive_ASSOCIATE(asoc, NULL);
		if (err < 0)
			goto out_free;
		SCTP_DEBUG_PRINTK("We associated primitively.\n");
	}

	/* Break the message into multiple chunks of maximum size. */
	datamsg = sctp_datamsg_from_user(asoc, sinfo, msg, msg_len);
	if (!datamsg) {
		err = -ENOMEM;
		goto out_free;
	}

	/* Now send the (possibly) fragmented message. */
	list_for_each(pos, &datamsg->chunks) {
		chunk = list_entry(pos, struct sctp_chunk, frag_list);
		sctp_datamsg_track(chunk);

		/* Do accounting for the write space.  */
		sctp_set_owner_w(chunk);

		chunk->transport = chunk_tp;

		/* Send it to the lower layers.  Note:  all chunks
		 * must either fail or succeed.   The lower layer
		 * works that way today.  Keep it that way or this
		 * breaks.
		 */
		err = sctp_primitive_SEND(asoc, chunk);
		/* Did the lower layer accept the chunk? */
		if (err)
			sctp_chunk_free(chunk);
		SCTP_DEBUG_PRINTK("We sent primitively.\n");
	}

	sctp_datamsg_free(datamsg);
	if (err)
		goto out_free;
	else
		err = msg_len;

	/* If we are already past ASSOCIATE, the lower
	 * layers are responsible for association cleanup.
	 */
	goto out_unlock;

out_free:
	if (new_asoc)
		sctp_association_free(asoc);
out_unlock:
	sctp_release_sock(sk);

out_nounlock:
	return sctp_error(sk, msg_flags, err);

#if 0
do_sock_err:
	if (msg_len)
		err = msg_len;
	else
		err = sock_error(sk);
	goto out;

do_interrupted:
	if (msg_len)
		err = msg_len;
	goto out;
#endif /* 0 */
}

/* This is an extended version of skb_pull() that removes the data from the
 * start of a skb even when data is spread across the list of skb's in the
 * frag_list. len specifies the total amount of data that needs to be removed.
 * when 'len' bytes could be removed from the skb, it returns 0.
 * If 'len' exceeds the total skb length,  it returns the no. of bytes that
 * could not be removed.
 */
static int sctp_skb_pull(struct sk_buff *skb, int len)
{
	struct sk_buff *list;
	int skb_len = skb_headlen(skb);
	int rlen;

	if (len <= skb_len) {
		__skb_pull(skb, len);
		return 0;
	}
	len -= skb_len;
	__skb_pull(skb, skb_len);

	for (list = skb_shinfo(skb)->frag_list; list; list = list->next) {
		rlen = sctp_skb_pull(list, len);
		skb->len -= (len-rlen);
		skb->data_len -= (len-rlen);

		if (!rlen)
			return 0;

		len = rlen;
	}

	return len;
}

/* API 3.1.3  recvmsg() - UDP Style Syntax
 *
 *  ssize_t recvmsg(int socket, struct msghdr *message,
 *                    int flags);
 *
 *  socket  - the socket descriptor of the endpoint.
 *  message - pointer to the msghdr structure which contains a single
 *            user message and possibly some ancillary data.
 *
 *            See Section 5 for complete description of the data
 *            structures.
 *
 *  flags   - flags sent or received with the user message, see Section
 *            5 for complete description of the flags.
 */
static struct sk_buff *sctp_skb_recv_datagram(struct sock *, int, int, int *);

SCTP_STATIC int sctp_recvmsg(struct kiocb *iocb, struct sock *sk,
			     struct msghdr *msg, size_t len, int noblock,
			     int flags, int *addr_len)
{
	struct sctp_ulpevent *event = NULL;
	struct sctp_sock *sp = sctp_sk(sk);
	struct sk_buff *skb;
	int copied;
	int err = 0;
	int skb_len;

	SCTP_DEBUG_PRINTK("sctp_recvmsg(%s: %p, %s: %p, %s: %zd, %s: %d, %s: "
			  "0x%x, %s: %p)\n", "sk", sk, "msghdr", msg,
			  "len", len, "knoblauch", noblock,
			  "flags", flags, "addr_len", addr_len);

	sctp_lock_sock(sk);

	if (sctp_style(sk, TCP) && !sctp_sstate(sk, ESTABLISHED)) {
		err = -ENOTCONN;
		goto out;
	}

	skb = sctp_skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	/* Get the total length of the skb including any skb's in the
	 * frag_list.
	 */
	skb_len = skb->len;

	copied = skb_len;
	if (copied > len)
		copied = len;

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);

	event = sctp_skb2event(skb);

	if (err)
		goto out_free;

	sock_recv_timestamp(msg, sk, skb);
	if (sctp_ulpevent_is_notification(event)) {
		msg->msg_flags |= MSG_NOTIFICATION;
		sp->pf->event_msgname(event, msg->msg_name, addr_len);
	} else {
		sp->pf->skb_msgname(skb, msg->msg_name, addr_len);
	}

	/* Check if we allow SCTP_SNDRCVINFO. */
	if (sp->subscribe.sctp_data_io_event)
		sctp_ulpevent_read_sndrcvinfo(event, msg);
#if 0
	/* FIXME: we should be calling IP/IPv6 layers.  */
	if (sk->sk_protinfo.af_inet.cmsg_flags)
		ip_cmsg_recv(msg, skb);
#endif

	err = copied;

	/* If skb's length exceeds the user's buffer, update the skb and
	 * push it back to the receive_queue so that the next call to
	 * recvmsg() will return the remaining data. Don't set MSG_EOR.
	 */
	if (skb_len > copied) {
		msg->msg_flags &= ~MSG_EOR;
		if (flags & MSG_PEEK)
			goto out_free;
		sctp_skb_pull(skb, copied);
		skb_queue_head(&sk->sk_receive_queue, skb);

		/* When only partial message is copied to the user, increase
		 * rwnd by that amount. If all the data in the skb is read,
		 * rwnd is updated when the event is freed.
		 */
		sctp_assoc_rwnd_increase(event->asoc, copied);
		goto out;
	} else if ((event->msg_flags & MSG_NOTIFICATION) ||
		   (event->msg_flags & MSG_EOR))
		msg->msg_flags |= MSG_EOR;
	else
		msg->msg_flags &= ~MSG_EOR;

out_free:
	if (flags & MSG_PEEK) {
		/* Release the skb reference acquired after peeking the skb in
		 * sctp_skb_recv_datagram().
		 */
		kfree_skb(skb);
	} else {
		/* Free the event which includes releasing the reference to
		 * the owner of the skb, freeing the skb and updating the
		 * rwnd.
		 */
		sctp_ulpevent_free(event);
	}
out:
	sctp_release_sock(sk);
	return err;
}

/* 7.1.12 Enable/Disable message fragmentation (SCTP_DISABLE_FRAGMENTS)
 *
 * This option is a on/off flag.  If enabled no SCTP message
 * fragmentation will be performed.  Instead if a message being sent
 * exceeds the current PMTU size, the message will NOT be sent and
 * instead a error will be indicated to the user.
 */
static int sctp_setsockopt_disable_fragments(struct sock *sk,
					    char __user *optval, int optlen)
{
	int val;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	sctp_sk(sk)->disable_fragments = (val == 0) ? 0 : 1;

	return 0;
}

static int sctp_setsockopt_events(struct sock *sk, char __user *optval,
					int optlen)
{
	if (optlen != sizeof(struct sctp_event_subscribe))
		return -EINVAL;
	if (copy_from_user(&sctp_sk(sk)->subscribe, optval, optlen))
		return -EFAULT;
	return 0;
}

/* 7.1.8 Automatic Close of associations (SCTP_AUTOCLOSE)
 *
 * This socket option is applicable to the UDP-style socket only.  When
 * set it will cause associations that are idle for more than the
 * specified number of seconds to automatically close.  An association
 * being idle is defined an association that has NOT sent or received
 * user data.  The special value of '0' indicates that no automatic
 * close of any associations should be performed.  The option expects an
 * integer defining the number of seconds of idle time before an
 * association is closed.
 */
static int sctp_setsockopt_autoclose(struct sock *sk, char __user *optval,
					    int optlen)
{
	struct sctp_sock *sp = sctp_sk(sk);

	/* Applicable to UDP-style socket only */
	if (sctp_style(sk, TCP))
		return -EOPNOTSUPP;
	if (optlen != sizeof(int))
		return -EINVAL;
	if (copy_from_user(&sp->autoclose, optval, optlen))
		return -EFAULT;

	return 0;
}

/* 7.1.13 Peer Address Parameters (SCTP_PEER_ADDR_PARAMS)
 *
 * Applications can enable or disable heartbeats for any peer address of
 * an association, modify an address's heartbeat interval, force a
 * heartbeat to be sent immediately, and adjust the address's maximum
 * number of retransmissions sent before an address is considered
 * unreachable.  The following structure is used to access and modify an
 * address's parameters:
 *
 *  struct sctp_paddrparams {
 *     sctp_assoc_t            spp_assoc_id;
 *     struct sockaddr_storage spp_address;
 *     uint32_t                spp_hbinterval;
 *     uint16_t                spp_pathmaxrxt;
 *     uint32_t                spp_pathmtu;
 *     uint32_t                spp_sackdelay;
 *     uint32_t                spp_flags;
 * };
 *
 *   spp_assoc_id    - (one-to-many style socket) This is filled in the
 *                     application, and identifies the association for
 *                     this query.
 *   spp_address     - This specifies which address is of interest.
 *   spp_hbinterval  - This contains the value of the heartbeat interval,
 *                     in milliseconds.  If a  value of zero
 *                     is present in this field then no changes are to
 *                     be made to this parameter.
 *   spp_pathmaxrxt  - This contains the maximum number of
 *                     retransmissions before this address shall be
 *                     considered unreachable. If a  value of zero
 *                     is present in this field then no changes are to
 *                     be made to this parameter.
 *   spp_pathmtu     - When Path MTU discovery is disabled the value
 *                     specified here will be the "fixed" path mtu.
 *                     Note that if the spp_address field is empty
 *                     then all associations on this address will
 *                     have this fixed path mtu set upon them.
 *
 *   spp_sackdelay   - When delayed sack is enabled, this value specifies
 *                     the number of milliseconds that sacks will be delayed
 *                     for. This value will apply to all addresses of an
 *                     association if the spp_address field is empty. Note
 *                     also, that if delayed sack is enabled and this
 *                     value is set to 0, no change is made to the last
 *                     recorded delayed sack timer value.
 *
 *   spp_flags       - These flags are used to control various features
 *                     on an association. The flag field may contain
 *                     zero or more of the following options.
 *
 *                     SPP_HB_ENABLE  - Enable heartbeats on the
 *                     specified address. Note that if the address
 *                     field is empty all addresses for the association
 *                     have heartbeats enabled upon them.
 *
 *                     SPP_HB_DISABLE - Disable heartbeats on the
 *                     speicifed address. Note that if the address
 *                     field is empty all addresses for the association
 *                     will have their heartbeats disabled. Note also
 *                     that SPP_HB_ENABLE and SPP_HB_DISABLE are
 *                     mutually exclusive, only one of these two should
 *                     be specified. Enabling both fields will have
 *                     undetermined results.
 *
 *                     SPP_HB_DEMAND - Request a user initiated heartbeat
 *                     to be made immediately.
 *
 *                     SPP_HB_TIME_IS_ZERO - Specify's that the time for
 *                     heartbeat delayis to be set to the value of 0
 *                     milliseconds.
 *
 *                     SPP_PMTUD_ENABLE - This field will enable PMTU
 *                     discovery upon the specified address. Note that
 *                     if the address feild is empty then all addresses
 *                     on the association are effected.
 *
 *                     SPP_PMTUD_DISABLE - This field will disable PMTU
 *                     discovery upon the specified address. Note that
 *                     if the address feild is empty then all addresses
 *                     on the association are effected. Not also that
 *                     SPP_PMTUD_ENABLE and SPP_PMTUD_DISABLE are mutually
 *                     exclusive. Enabling both will have undetermined
 *                     results.
 *
 *                     SPP_SACKDELAY_ENABLE - Setting this flag turns
 *                     on delayed sack. The time specified in spp_sackdelay
 *                     is used to specify the sack delay for this address. Note
 *                     that if spp_address is empty then all addresses will
 *                     enable delayed sack and take on the sack delay
 *                     value specified in spp_sackdelay.
 *                     SPP_SACKDELAY_DISABLE - Setting this flag turns
 *                     off delayed sack. If the spp_address field is blank then
 *                     delayed sack is disabled for the entire association. Note
 *                     also that this field is mutually exclusive to
 *                     SPP_SACKDELAY_ENABLE, setting both will have undefined
 *                     results.
 */
static int sctp_apply_peer_addr_params(struct sctp_paddrparams *params,
				       struct sctp_transport   *trans,
				       struct sctp_association *asoc,
				       struct sctp_sock        *sp,
				       int                      hb_change,
				       int                      pmtud_change,
				       int                      sackdelay_change)
{
	int error;

	if (params->spp_flags & SPP_HB_DEMAND && trans) {
		error = sctp_primitive_REQUESTHEARTBEAT (trans->asoc, trans);
		if (error)
			return error;
	}

	/* Note that unless the spp_flag is set to SPP_HB_ENABLE the value of
	 * this field is ignored.  Note also that a value of zero indicates
	 * the current setting should be left unchanged.
	 */
	if (params->spp_flags & SPP_HB_ENABLE) {

		/* Re-zero the interval if the SPP_HB_TIME_IS_ZERO is
		 * set.  This lets us use 0 value when this flag
		 * is set.
		 */
		if (params->spp_flags & SPP_HB_TIME_IS_ZERO)
			params->spp_hbinterval = 0;

		if (params->spp_hbinterval ||
		    (params->spp_flags & SPP_HB_TIME_IS_ZERO)) {
			if (trans) {
				trans->hbinterval =
				    msecs_to_jiffies(params->spp_hbinterval);
			} else if (asoc) {
				asoc->hbinterval =
				    msecs_to_jiffies(params->spp_hbinterval);
			} else {
				sp->hbinterval = params->spp_hbinterval;
			}
		}
	}

	if (hb_change) {
		if (trans) {
			trans->param_flags =
				(trans->param_flags & ~SPP_HB) | hb_change;
		} else if (asoc) {
			asoc->param_flags =
				(asoc->param_flags & ~SPP_HB) | hb_change;
		} else {
			sp->param_flags =
				(sp->param_flags & ~SPP_HB) | hb_change;
		}
	}

	/* When Path MTU discovery is disabled the value specified here will
	 * be the "fixed" path mtu (i.e. the value of the spp_flags field must
	 * include the flag SPP_PMTUD_DISABLE for this field to have any
	 * effect).
	 */
	if ((params->spp_flags & SPP_PMTUD_DISABLE) && params->spp_pathmtu) {
		if (trans) {
			trans->pathmtu = params->spp_pathmtu;
			sctp_assoc_sync_pmtu(asoc);
		} else if (asoc) {
			asoc->pathmtu = params->spp_pathmtu;
			sctp_frag_point(sp, params->spp_pathmtu);
		} else {
			sp->pathmtu = params->spp_pathmtu;
		}
	}

	if (pmtud_change) {
		if (trans) {
			int update = (trans->param_flags & SPP_PMTUD_DISABLE) &&
				(params->spp_flags & SPP_PMTUD_ENABLE);
			trans->param_flags =
				(trans->param_flags & ~SPP_PMTUD) | pmtud_change;
			if (update) {
				sctp_transport_pmtu(trans);
				sctp_assoc_sync_pmtu(asoc);
			}
		} else if (asoc) {
			asoc->param_flags =
				(asoc->param_flags & ~SPP_PMTUD) | pmtud_change;
		} else {
			sp->param_flags =
				(sp->param_flags & ~SPP_PMTUD) | pmtud_change;
		}
	}

	/* Note that unless the spp_flag is set to SPP_SACKDELAY_ENABLE the
	 * value of this field is ignored.  Note also that a value of zero
	 * indicates the current setting should be left unchanged.
	 */
	if ((params->spp_flags & SPP_SACKDELAY_ENABLE) && params->spp_sackdelay) {
		if (trans) {
			trans->sackdelay =
				msecs_to_jiffies(params->spp_sackdelay);
		} else if (asoc) {
			asoc->sackdelay =
				msecs_to_jiffies(params->spp_sackdelay);
		} else {
			sp->sackdelay = params->spp_sackdelay;
		}
	}

	if (sackdelay_change) {
		if (trans) {
			trans->param_flags =
				(trans->param_flags & ~SPP_SACKDELAY) |
				sackdelay_change;
		} else if (asoc) {
			asoc->param_flags =
				(asoc->param_flags & ~SPP_SACKDELAY) |
				sackdelay_change;
		} else {
			sp->param_flags =
				(sp->param_flags & ~SPP_SACKDELAY) |
				sackdelay_change;
		}
	}

	/* Note that unless the spp_flag is set to SPP_PMTUD_ENABLE the value
	 * of this field is ignored.  Note also that a value of zero
	 * indicates the current setting should be left unchanged.
	 */
	if ((params->spp_flags & SPP_PMTUD_ENABLE) && params->spp_pathmaxrxt) {
		if (trans) {
			trans->pathmaxrxt = params->spp_pathmaxrxt;
		} else if (asoc) {
			asoc->pathmaxrxt = params->spp_pathmaxrxt;
		} else {
			sp->pathmaxrxt = params->spp_pathmaxrxt;
		}
	}

	return 0;
}

static int sctp_setsockopt_peer_addr_params(struct sock *sk,
					    char __user *optval, int optlen)
{
	struct sctp_paddrparams  params;
	struct sctp_transport   *trans = NULL;
	struct sctp_association *asoc = NULL;
	struct sctp_sock        *sp = sctp_sk(sk);
	int error;
	int hb_change, pmtud_change, sackdelay_change;

	if (optlen != sizeof(struct sctp_paddrparams))
		return - EINVAL;

	if (copy_from_user(&params, optval, optlen))
		return -EFAULT;

	/* Validate flags and value parameters. */
	hb_change        = params.spp_flags & SPP_HB;
	pmtud_change     = params.spp_flags & SPP_PMTUD;
	sackdelay_change = params.spp_flags & SPP_SACKDELAY;

	if (hb_change        == SPP_HB ||
	    pmtud_change     == SPP_PMTUD ||
	    sackdelay_change == SPP_SACKDELAY ||
	    params.spp_sackdelay > 500 ||
	    (params.spp_pathmtu
	    && params.spp_pathmtu < SCTP_DEFAULT_MINSEGMENT))
		return -EINVAL;

	/* If an address other than INADDR_ANY is specified, and
	 * no transport is found, then the request is invalid.
	 */
	if (!sctp_is_any(( union sctp_addr *)&params.spp_address)) {
		trans = sctp_addr_id2transport(sk, &params.spp_address,
					       params.spp_assoc_id);
		if (!trans)
			return -EINVAL;
	}

	/* Get association, if assoc_id != 0 and the socket is a one
	 * to many style socket, and an association was not found, then
	 * the id was invalid.
	 */
	asoc = sctp_id2assoc(sk, params.spp_assoc_id);
	if (!asoc && params.spp_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	/* Heartbeat demand can only be sent on a transport or
	 * association, but not a socket.
	 */
	if (params.spp_flags & SPP_HB_DEMAND && !trans && !asoc)
		return -EINVAL;

	/* Process parameters. */
	error = sctp_apply_peer_addr_params(&params, trans, asoc, sp,
					    hb_change, pmtud_change,
					    sackdelay_change);

	if (error)
		return error;

	/* If changes are for association, also apply parameters to each
	 * transport.
	 */
	if (!trans && asoc) {
		struct list_head *pos;

		list_for_each(pos, &asoc->peer.transport_addr_list) {
			trans = list_entry(pos, struct sctp_transport,
					   transports);
			sctp_apply_peer_addr_params(&params, trans, asoc, sp,
						    hb_change, pmtud_change,
						    sackdelay_change);
		}
	}

	return 0;
}

/* 7.1.23. Delayed Ack Timer (SCTP_DELAYED_ACK_TIME)
 *
 *   This options will get or set the delayed ack timer.  The time is set
 *   in milliseconds.  If the assoc_id is 0, then this sets or gets the
 *   endpoints default delayed ack timer value.  If the assoc_id field is
 *   non-zero, then the set or get effects the specified association.
 *
 *   struct sctp_assoc_value {
 *       sctp_assoc_t            assoc_id;
 *       uint32_t                assoc_value;
 *   };
 *
 *     assoc_id    - This parameter, indicates which association the
 *                   user is preforming an action upon. Note that if
 *                   this field's value is zero then the endpoints
 *                   default value is changed (effecting future
 *                   associations only).
 *
 *     assoc_value - This parameter contains the number of milliseconds
 *                   that the user is requesting the delayed ACK timer
 *                   be set to. Note that this value is defined in
 *                   the standard to be between 200 and 500 milliseconds.
 *
 *                   Note: a value of zero will leave the value alone,
 *                   but disable SACK delay. A non-zero value will also
 *                   enable SACK delay.
 */

static int sctp_setsockopt_delayed_ack_time(struct sock *sk,
					    char __user *optval, int optlen)
{
	struct sctp_assoc_value  params;
	struct sctp_transport   *trans = NULL;
	struct sctp_association *asoc = NULL;
	struct sctp_sock        *sp = sctp_sk(sk);

	if (optlen != sizeof(struct sctp_assoc_value))
		return - EINVAL;

	if (copy_from_user(&params, optval, optlen))
		return -EFAULT;

	/* Validate value parameter. */
	if (params.assoc_value > 500)
		return -EINVAL;

	/* Get association, if assoc_id != 0 and the socket is a one
	 * to many style socket, and an association was not found, then
	 * the id was invalid.
	 */
	asoc = sctp_id2assoc(sk, params.assoc_id);
	if (!asoc && params.assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	if (params.assoc_value) {
		if (asoc) {
			asoc->sackdelay =
				msecs_to_jiffies(params.assoc_value);
			asoc->param_flags =
				(asoc->param_flags & ~SPP_SACKDELAY) |
				SPP_SACKDELAY_ENABLE;
		} else {
			sp->sackdelay = params.assoc_value;
			sp->param_flags =
				(sp->param_flags & ~SPP_SACKDELAY) |
				SPP_SACKDELAY_ENABLE;
		}
	} else {
		if (asoc) {
			asoc->param_flags =
				(asoc->param_flags & ~SPP_SACKDELAY) |
				SPP_SACKDELAY_DISABLE;
		} else {
			sp->param_flags =
				(sp->param_flags & ~SPP_SACKDELAY) |
				SPP_SACKDELAY_DISABLE;
		}
	}

	/* If change is for association, also apply to each transport. */
	if (asoc) {
		struct list_head *pos;

		list_for_each(pos, &asoc->peer.transport_addr_list) {
			trans = list_entry(pos, struct sctp_transport,
					   transports);
			if (params.assoc_value) {
				trans->sackdelay =
					msecs_to_jiffies(params.assoc_value);
				trans->param_flags =
					(trans->param_flags & ~SPP_SACKDELAY) |
					SPP_SACKDELAY_ENABLE;
			} else {
				trans->param_flags =
					(trans->param_flags & ~SPP_SACKDELAY) |
					SPP_SACKDELAY_DISABLE;
			}
		}
	}

	return 0;
}

/* 7.1.3 Initialization Parameters (SCTP_INITMSG)
 *
 * Applications can specify protocol parameters for the default association
 * initialization.  The option name argument to setsockopt() and getsockopt()
 * is SCTP_INITMSG.
 *
 * Setting initialization parameters is effective only on an unconnected
 * socket (for UDP-style sockets only future associations are effected
 * by the change).  With TCP-style sockets, this option is inherited by
 * sockets derived from a listener socket.
 */
static int sctp_setsockopt_initmsg(struct sock *sk, char __user *optval, int optlen)
{
	struct sctp_initmsg sinit;
	struct sctp_sock *sp = sctp_sk(sk);

	if (optlen != sizeof(struct sctp_initmsg))
		return -EINVAL;
	if (copy_from_user(&sinit, optval, optlen))
		return -EFAULT;

	if (sinit.sinit_num_ostreams)
		sp->initmsg.sinit_num_ostreams = sinit.sinit_num_ostreams;
	if (sinit.sinit_max_instreams)
		sp->initmsg.sinit_max_instreams = sinit.sinit_max_instreams;
	if (sinit.sinit_max_attempts)
		sp->initmsg.sinit_max_attempts = sinit.sinit_max_attempts;
	if (sinit.sinit_max_init_timeo)
		sp->initmsg.sinit_max_init_timeo = sinit.sinit_max_init_timeo;

	return 0;
}

/*
 * 7.1.14 Set default send parameters (SCTP_DEFAULT_SEND_PARAM)
 *
 *   Applications that wish to use the sendto() system call may wish to
 *   specify a default set of parameters that would normally be supplied
 *   through the inclusion of ancillary data.  This socket option allows
 *   such an application to set the default sctp_sndrcvinfo structure.
 *   The application that wishes to use this socket option simply passes
 *   in to this call the sctp_sndrcvinfo structure defined in Section
 *   5.2.2) The input parameters accepted by this call include
 *   sinfo_stream, sinfo_flags, sinfo_ppid, sinfo_context,
 *   sinfo_timetolive.  The user must provide the sinfo_assoc_id field in
 *   to this call if the caller is using the UDP model.
 */
static int sctp_setsockopt_default_send_param(struct sock *sk,
						char __user *optval, int optlen)
{
	struct sctp_sndrcvinfo info;
	struct sctp_association *asoc;
	struct sctp_sock *sp = sctp_sk(sk);

	if (optlen != sizeof(struct sctp_sndrcvinfo))
		return -EINVAL;
	if (copy_from_user(&info, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, info.sinfo_assoc_id);
	if (!asoc && info.sinfo_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	if (asoc) {
		asoc->default_stream = info.sinfo_stream;
		asoc->default_flags = info.sinfo_flags;
		asoc->default_ppid = info.sinfo_ppid;
		asoc->default_context = info.sinfo_context;
		asoc->default_timetolive = info.sinfo_timetolive;
	} else {
		sp->default_stream = info.sinfo_stream;
		sp->default_flags = info.sinfo_flags;
		sp->default_ppid = info.sinfo_ppid;
		sp->default_context = info.sinfo_context;
		sp->default_timetolive = info.sinfo_timetolive;
	}

	return 0;
}

/* 7.1.10 Set Primary Address (SCTP_PRIMARY_ADDR)
 *
 * Requests that the local SCTP stack use the enclosed peer address as
 * the association primary.  The enclosed address must be one of the
 * association peer's addresses.
 */
static int sctp_setsockopt_primary_addr(struct sock *sk, char __user *optval,
					int optlen)
{
	struct sctp_prim prim;
	struct sctp_transport *trans;

	if (optlen != sizeof(struct sctp_prim))
		return -EINVAL;

	if (copy_from_user(&prim, optval, sizeof(struct sctp_prim)))
		return -EFAULT;

	trans = sctp_addr_id2transport(sk, &prim.ssp_addr, prim.ssp_assoc_id);
	if (!trans)
		return -EINVAL;

	sctp_assoc_set_primary(trans->asoc, trans);

	return 0;
}

/*
 * 7.1.5 SCTP_NODELAY
 *
 * Turn on/off any Nagle-like algorithm.  This means that packets are
 * generally sent as soon as possible and no unnecessary delays are
 * introduced, at the cost of more packets in the network.  Expects an
 *  integer boolean flag.
 */
static int sctp_setsockopt_nodelay(struct sock *sk, char __user *optval,
					int optlen)
{
	int val;

	if (optlen < sizeof(int))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	sctp_sk(sk)->nodelay = (val == 0) ? 0 : 1;
	return 0;
}

/*
 *
 * 7.1.1 SCTP_RTOINFO
 *
 * The protocol parameters used to initialize and bound retransmission
 * timeout (RTO) are tunable. sctp_rtoinfo structure is used to access
 * and modify these parameters.
 * All parameters are time values, in milliseconds.  A value of 0, when
 * modifying the parameters, indicates that the current value should not
 * be changed.
 *
 */
static int sctp_setsockopt_rtoinfo(struct sock *sk, char __user *optval, int optlen) {
	struct sctp_rtoinfo rtoinfo;
	struct sctp_association *asoc;

	if (optlen != sizeof (struct sctp_rtoinfo))
		return -EINVAL;

	if (copy_from_user(&rtoinfo, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, rtoinfo.srto_assoc_id);

	/* Set the values to the specific association */
	if (!asoc && rtoinfo.srto_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	if (asoc) {
		if (rtoinfo.srto_initial != 0)
			asoc->rto_initial =
				msecs_to_jiffies(rtoinfo.srto_initial);
		if (rtoinfo.srto_max != 0)
			asoc->rto_max = msecs_to_jiffies(rtoinfo.srto_max);
		if (rtoinfo.srto_min != 0)
			asoc->rto_min = msecs_to_jiffies(rtoinfo.srto_min);
	} else {
		/* If there is no association or the association-id = 0
		 * set the values to the endpoint.
		 */
		struct sctp_sock *sp = sctp_sk(sk);

		if (rtoinfo.srto_initial != 0)
			sp->rtoinfo.srto_initial = rtoinfo.srto_initial;
		if (rtoinfo.srto_max != 0)
			sp->rtoinfo.srto_max = rtoinfo.srto_max;
		if (rtoinfo.srto_min != 0)
			sp->rtoinfo.srto_min = rtoinfo.srto_min;
	}

	return 0;
}

/*
 *
 * 7.1.2 SCTP_ASSOCINFO
 *
 * This option is used to tune the the maximum retransmission attempts
 * of the association.
 * Returns an error if the new association retransmission value is
 * greater than the sum of the retransmission value  of the peer.
 * See [SCTP] for more information.
 *
 */
static int sctp_setsockopt_associnfo(struct sock *sk, char __user *optval, int optlen)
{

	struct sctp_assocparams assocparams;
	struct sctp_association *asoc;

	if (optlen != sizeof(struct sctp_assocparams))
		return -EINVAL;
	if (copy_from_user(&assocparams, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, assocparams.sasoc_assoc_id);

	if (!asoc && assocparams.sasoc_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	/* Set the values to the specific association */
	if (asoc) {
		if (assocparams.sasoc_asocmaxrxt != 0) {
			__u32 path_sum = 0;
			int   paths = 0;
			struct list_head *pos;
			struct sctp_transport *peer_addr;

			list_for_each(pos, &asoc->peer.transport_addr_list) {
				peer_addr = list_entry(pos,
						struct sctp_transport,
						transports);
				path_sum += peer_addr->pathmaxrxt;
				paths++;
			}

			/* Only validate asocmaxrxt if we have more then
			 * one path/transport.  We do this because path
			 * retransmissions are only counted when we have more
			 * then one path.
			 */
			if (paths > 1 &&
			    assocparams.sasoc_asocmaxrxt > path_sum)
				return -EINVAL;

			asoc->max_retrans = assocparams.sasoc_asocmaxrxt;
		}

		if (assocparams.sasoc_cookie_life != 0) {
			asoc->cookie_life.tv_sec =
					assocparams.sasoc_cookie_life / 1000;
			asoc->cookie_life.tv_usec =
					(assocparams.sasoc_cookie_life % 1000)
					* 1000;
		}
	} else {
		/* Set the values to the endpoint */
		struct sctp_sock *sp = sctp_sk(sk);

		if (assocparams.sasoc_asocmaxrxt != 0)
			sp->assocparams.sasoc_asocmaxrxt =
						assocparams.sasoc_asocmaxrxt;
		if (assocparams.sasoc_cookie_life != 0)
			sp->assocparams.sasoc_cookie_life =
						assocparams.sasoc_cookie_life;
	}
	return 0;
}

/*
 * 7.1.16 Set/clear IPv4 mapped addresses (SCTP_I_WANT_MAPPED_V4_ADDR)
 *
 * This socket option is a boolean flag which turns on or off mapped V4
 * addresses.  If this option is turned on and the socket is type
 * PF_INET6, then IPv4 addresses will be mapped to V6 representation.
 * If this option is turned off, then no mapping will be done of V4
 * addresses and a user will receive both PF_INET6 and PF_INET type
 * addresses on the socket.
 */
static int sctp_setsockopt_mappedv4(struct sock *sk, char __user *optval, int optlen)
{
	int val;
	struct sctp_sock *sp = sctp_sk(sk);

	if (optlen < sizeof(int))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;
	if (val)
		sp->v4mapped = 1;
	else
		sp->v4mapped = 0;

	return 0;
}

/*
 * 7.1.17 Set the maximum fragrmentation size (SCTP_MAXSEG)
 *
 * This socket option specifies the maximum size to put in any outgoing
 * SCTP chunk.  If a message is larger than this size it will be
 * fragmented by SCTP into the specified size.  Note that the underlying
 * SCTP implementation may fragment into smaller sized chunks when the
 * PMTU of the underlying association is smaller than the value set by
 * the user.
 */
static int sctp_setsockopt_maxseg(struct sock *sk, char __user *optval, int optlen)
{
	struct sctp_association *asoc;
	struct list_head *pos;
	struct sctp_sock *sp = sctp_sk(sk);
	int val;

	if (optlen < sizeof(int))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;
	if ((val != 0) && ((val < 8) || (val > SCTP_MAX_CHUNK_LEN)))
		return -EINVAL;
	sp->user_frag = val;

	/* Update the frag_point of the existing associations. */
	list_for_each(pos, &(sp->ep->asocs)) {
		asoc = list_entry(pos, struct sctp_association, asocs);
		asoc->frag_point = sctp_frag_point(sp, asoc->pathmtu);
	}

	return 0;
}


/*
 *  7.1.9 Set Peer Primary Address (SCTP_SET_PEER_PRIMARY_ADDR)
 *
 *   Requests that the peer mark the enclosed address as the association
 *   primary. The enclosed address must be one of the association's
 *   locally bound addresses. The following structure is used to make a
 *   set primary request:
 */
static int sctp_setsockopt_peer_primary_addr(struct sock *sk, char __user *optval,
					     int optlen)
{
	struct sctp_sock	*sp;
	struct sctp_endpoint	*ep;
	struct sctp_association	*asoc = NULL;
	struct sctp_setpeerprim	prim;
	struct sctp_chunk	*chunk;
	int 			err;

	sp = sctp_sk(sk);
	ep = sp->ep;

	if (!sctp_addip_enable)
		return -EPERM;

	if (optlen != sizeof(struct sctp_setpeerprim))
		return -EINVAL;

	if (copy_from_user(&prim, optval, optlen))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, prim.sspp_assoc_id);
	if (!asoc)
		return -EINVAL;

	if (!asoc->peer.asconf_capable)
		return -EPERM;

	if (asoc->peer.addip_disabled_mask & SCTP_PARAM_SET_PRIMARY)
		return -EPERM;

	if (!sctp_state(asoc, ESTABLISHED))
		return -ENOTCONN;

	if (!sctp_assoc_lookup_laddr(asoc, (union sctp_addr *)&prim.sspp_addr))
		return -EADDRNOTAVAIL;

	/* Create an ASCONF chunk with SET_PRIMARY parameter	*/
	chunk = sctp_make_asconf_set_prim(asoc,
					  (union sctp_addr *)&prim.sspp_addr);
	if (!chunk)
		return -ENOMEM;

	err = sctp_send_asconf(asoc, chunk);

	SCTP_DEBUG_PRINTK("We set peer primary addr primitively.\n");

	return err;
}

static int sctp_setsockopt_adaptation_layer(struct sock *sk, char __user *optval,
					  int optlen)
{
	struct sctp_setadaptation adaptation;

	if (optlen != sizeof(struct sctp_setadaptation))
		return -EINVAL;
	if (copy_from_user(&adaptation, optval, optlen))
		return -EFAULT;

	sctp_sk(sk)->adaptation_ind = adaptation.ssb_adaptation_ind;

	return 0;
}

/*
 * 7.1.29.  Set or Get the default context (SCTP_CONTEXT)
 *
 * The context field in the sctp_sndrcvinfo structure is normally only
 * used when a failed message is retrieved holding the value that was
 * sent down on the actual send call.  This option allows the setting of
 * a default context on an association basis that will be received on
 * reading messages from the peer.  This is especially helpful in the
 * one-2-many model for an application to keep some reference to an
 * internal state machine that is processing messages on the
 * association.  Note that the setting of this value only effects
 * received messages from the peer and does not effect the value that is
 * saved with outbound messages.
 */
static int sctp_setsockopt_context(struct sock *sk, char __user *optval,
				   int optlen)
{
	struct sctp_assoc_value params;
	struct sctp_sock *sp;
	struct sctp_association *asoc;

	if (optlen != sizeof(struct sctp_assoc_value))
		return -EINVAL;
	if (copy_from_user(&params, optval, optlen))
		return -EFAULT;

	sp = sctp_sk(sk);

	if (params.assoc_id != 0) {
		asoc = sctp_id2assoc(sk, params.assoc_id);
		if (!asoc)
			return -EINVAL;
		asoc->default_rcv_context = params.assoc_value;
	} else {
		sp->default_rcv_context = params.assoc_value;
	}

	return 0;
}

/*
 * 7.1.24.  Get or set fragmented interleave (SCTP_FRAGMENT_INTERLEAVE)
 *
 * This options will at a minimum specify if the implementation is doing
 * fragmented interleave.  Fragmented interleave, for a one to many
 * socket, is when subsequent calls to receive a message may return
 * parts of messages from different associations.  Some implementations
 * may allow you to turn this value on or off.  If so, when turned off,
 * no fragment interleave will occur (which will cause a head of line
 * blocking amongst multiple associations sharing the same one to many
 * socket).  When this option is turned on, then each receive call may
 * come from a different association (thus the user must receive data
 * with the extended calls (e.g. sctp_recvmsg) to keep track of which
 * association each receive belongs to.
 *
 * This option takes a boolean value.  A non-zero value indicates that
 * fragmented interleave is on.  A value of zero indicates that
 * fragmented interleave is off.
 *
 * Note that it is important that an implementation that allows this
 * option to be turned on, have it off by default.  Otherwise an unaware
 * application using the one to many model may become confused and act
 * incorrectly.
 */
static int sctp_setsockopt_fragment_interleave(struct sock *sk,
					       char __user *optval,
					       int optlen)
{
	int val;

	if (optlen != sizeof(int))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	sctp_sk(sk)->frag_interleave = (val == 0) ? 0 : 1;

	return 0;
}

/*
 * 7.1.25.  Set or Get the sctp partial delivery point
 *       (SCTP_PARTIAL_DELIVERY_POINT)
 * This option will set or get the SCTP partial delivery point.  This
 * point is the size of a message where the partial delivery API will be
 * invoked to help free up rwnd space for the peer.  Setting this to a
 * lower value will cause partial delivery's to happen more often.  The
 * calls argument is an integer that sets or gets the partial delivery
 * point.
 */
static int sctp_setsockopt_partial_delivery_point(struct sock *sk,
						  char __user *optval,
						  int optlen)
{
	u32 val;

	if (optlen != sizeof(u32))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	sctp_sk(sk)->pd_point = val;

	return 0; /* is this the right error code? */
}

/*
 * 7.1.28.  Set or Get the maximum burst (SCTP_MAX_BURST)
 *
 * This option will allow a user to change the maximum burst of packets
 * that can be emitted by this association.  Note that the default value
 * is 4, and some implementations may restrict this setting so that it
 * can only be lowered.
 *
 * NOTE: This text doesn't seem right.  Do this on a socket basis with
 * future associations inheriting the socket value.
 */
static int sctp_setsockopt_maxburst(struct sock *sk,
				    char __user *optval,
				    int optlen)
{
	int val;

	if (optlen != sizeof(int))
		return -EINVAL;
	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	if (val < 0)
		return -EINVAL;

	sctp_sk(sk)->max_burst = val;

	return 0;
}

/* API 6.2 setsockopt(), getsockopt()
 *
 * Applications use setsockopt() and getsockopt() to set or retrieve
 * socket options.  Socket options are used to change the default
 * behavior of sockets calls.  They are described in Section 7.
 *
 * The syntax is:
 *
 *   ret = getsockopt(int sd, int level, int optname, void __user *optval,
 *                    int __user *optlen);
 *   ret = setsockopt(int sd, int level, int optname, const void __user *optval,
 *                    int optlen);
 *
 *   sd      - the socket descript.
 *   level   - set to IPPROTO_SCTP for all SCTP options.
 *   optname - the option name.
 *   optval  - the buffer to store the value of the option.
 *   optlen  - the size of the buffer.
 */
SCTP_STATIC int sctp_setsockopt(struct sock *sk, int level, int optname,
				char __user *optval, int optlen)
{
	int retval = 0;

	SCTP_DEBUG_PRINTK("sctp_setsockopt(sk: %p... optname: %d)\n",
			  sk, optname);

	/* I can hardly begin to describe how wrong this is.  This is
	 * so broken as to be worse than useless.  The API draft
	 * REALLY is NOT helpful here...  I am not convinced that the
	 * semantics of setsockopt() with a level OTHER THAN SOL_SCTP
	 * are at all well-founded.
	 */
	if (level != SOL_SCTP) {
		struct sctp_af *af = sctp_sk(sk)->pf->af;
		retval = af->setsockopt(sk, level, optname, optval, optlen);
		goto out_nounlock;
	}

	sctp_lock_sock(sk);

	switch (optname) {
	case SCTP_SOCKOPT_BINDX_ADD:
		/* 'optlen' is the size of the addresses buffer. */
		retval = sctp_setsockopt_bindx(sk, (struct sockaddr __user *)optval,
					       optlen, SCTP_BINDX_ADD_ADDR);
		break;

	case SCTP_SOCKOPT_BINDX_REM:
		/* 'optlen' is the size of the addresses buffer. */
		retval = sctp_setsockopt_bindx(sk, (struct sockaddr __user *)optval,
					       optlen, SCTP_BINDX_REM_ADDR);
		break;

	case SCTP_SOCKOPT_CONNECTX:
		/* 'optlen' is the size of the addresses buffer. */
		retval = sctp_setsockopt_connectx(sk, (struct sockaddr __user *)optval,
					       optlen);
		break;

	case SCTP_DISABLE_FRAGMENTS:
		retval = sctp_setsockopt_disable_fragments(sk, optval, optlen);
		break;

	case SCTP_EVENTS:
		retval = sctp_setsockopt_events(sk, optval, optlen);
		break;

	case SCTP_AUTOCLOSE:
		retval = sctp_setsockopt_autoclose(sk, optval, optlen);
		break;

	case SCTP_PEER_ADDR_PARAMS:
		retval = sctp_setsockopt_peer_addr_params(sk, optval, optlen);
		break;

	case SCTP_DELAYED_ACK_TIME:
		retval = sctp_setsockopt_delayed_ack_time(sk, optval, optlen);
		break;
	case SCTP_PARTIAL_DELIVERY_POINT:
		retval = sctp_setsockopt_partial_delivery_point(sk, optval, optlen);
		break;

	case SCTP_INITMSG:
		retval = sctp_setsockopt_initmsg(sk, optval, optlen);
		break;
	case SCTP_DEFAULT_SEND_PARAM:
		retval = sctp_setsockopt_default_send_param(sk, optval,
							    optlen);
		break;
	case SCTP_PRIMARY_ADDR:
		retval = sctp_setsockopt_primary_addr(sk, optval, optlen);
		break;
	case SCTP_SET_PEER_PRIMARY_ADDR:
		retval = sctp_setsockopt_peer_primary_addr(sk, optval, optlen);
		break;
	case SCTP_NODELAY:
		retval = sctp_setsockopt_nodelay(sk, optval, optlen);
		break;
	case SCTP_RTOINFO:
		retval = sctp_setsockopt_rtoinfo(sk, optval, optlen);
		break;
	case SCTP_ASSOCINFO:
		retval = sctp_setsockopt_associnfo(sk, optval, optlen);
		break;
	case SCTP_I_WANT_MAPPED_V4_ADDR:
		retval = sctp_setsockopt_mappedv4(sk, optval, optlen);
		break;
	case SCTP_MAXSEG:
		retval = sctp_setsockopt_maxseg(sk, optval, optlen);
		break;
	case SCTP_ADAPTATION_LAYER:
		retval = sctp_setsockopt_adaptation_layer(sk, optval, optlen);
		break;
	case SCTP_CONTEXT:
		retval = sctp_setsockopt_context(sk, optval, optlen);
		break;
	case SCTP_FRAGMENT_INTERLEAVE:
		retval = sctp_setsockopt_fragment_interleave(sk, optval, optlen);
		break;
	case SCTP_MAX_BURST:
		retval = sctp_setsockopt_maxburst(sk, optval, optlen);
		break;
	default:
		retval = -ENOPROTOOPT;
		break;
	}

	sctp_release_sock(sk);

out_nounlock:
	return retval;
}

/* API 3.1.6 connect() - UDP Style Syntax
 *
 * An application may use the connect() call in the UDP model to initiate an
 * association without sending data.
 *
 * The syntax is:
 *
 * ret = connect(int sd, const struct sockaddr *nam, socklen_t len);
 *
 * sd: the socket descriptor to have a new association added to.
 *
 * nam: the address structure (either struct sockaddr_in or struct
 *    sockaddr_in6 defined in RFC2553 [7]).
 *
 * len: the size of the address.
 */
SCTP_STATIC int sctp_connect(struct sock *sk, struct sockaddr *addr,
			     int addr_len)
{
	int err = 0;
	struct sctp_af *af;

	sctp_lock_sock(sk);

	SCTP_DEBUG_PRINTK("%s - sk: %p, sockaddr: %p, addr_len: %d\n",
			  __FUNCTION__, sk, addr, addr_len);

	/* Validate addr_len before calling common connect/connectx routine. */
	af = sctp_get_af_specific(addr->sa_family);
	if (!af || addr_len < af->sockaddr_len) {
		err = -EINVAL;
	} else {
		/* Pass correct addr len to common routine (so it knows there
		 * is only one address being passed.
		 */
		err = __sctp_connect(sk, addr, af->sockaddr_len);
	}

	sctp_release_sock(sk);
	return err;
}

/* FIXME: Write comments. */
SCTP_STATIC int sctp_disconnect(struct sock *sk, int flags)
{
	return -EOPNOTSUPP; /* STUB */
}

/* 4.1.4 accept() - TCP Style Syntax
 *
 * Applications use accept() call to remove an established SCTP
 * association from the accept queue of the endpoint.  A new socket
 * descriptor will be returned from accept() to represent the newly
 * formed association.
 */
SCTP_STATIC struct sock *sctp_accept(struct sock *sk, int flags, int *err)
{
	struct sctp_sock *sp;
	struct sctp_endpoint *ep;
	struct sock *newsk = NULL;
	struct sctp_association *asoc;
	long timeo;
	int error = 0;

	sctp_lock_sock(sk);

	sp = sctp_sk(sk);
	ep = sp->ep;

	if (!sctp_style(sk, TCP)) {
		error = -EOPNOTSUPP;
		goto out;
	}

	if (!sctp_sstate(sk, LISTENING)) {
		error = -EINVAL;
		goto out;
	}

	timeo = sock_rcvtimeo(sk, flags & O_NONBLOCK);

	error = sctp_wait_for_accept(sk, timeo);
	if (error)
		goto out;

	/* We treat the list of associations on the endpoint as the accept
	 * queue and pick the first association on the list.
	 */
	asoc = list_entry(ep->asocs.next, struct sctp_association, asocs);

	newsk = sp->pf->create_accept_sk(sk, asoc);
	if (!newsk) {
		error = -ENOMEM;
		goto out;
	}

	/* Populate the fields of the newsk from the oldsk and migrate the
	 * asoc to the newsk.
	 */
	sctp_sock_migrate(sk, newsk, asoc, SCTP_SOCKET_TCP);

out:
	sctp_release_sock(sk);
	*err = error;
	return newsk;
}

/* The SCTP ioctl handler. */
SCTP_STATIC int sctp_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

/* This is the function which gets called during socket creation to
 * initialized the SCTP-specific portion of the sock.
 * The sock structure should already be zero-filled memory.
 */
SCTP_STATIC int sctp_init_sock(struct sock *sk)
{
	struct sctp_endpoint *ep;
	struct sctp_sock *sp;

	SCTP_DEBUG_PRINTK("sctp_init_sock(sk: %p)\n", sk);

	sp = sctp_sk(sk);

	/* Initialize the SCTP per socket area.  */
	switch (sk->sk_type) {
	case SOCK_SEQPACKET:
		sp->type = SCTP_SOCKET_UDP;
		break;
	case SOCK_STREAM:
		sp->type = SCTP_SOCKET_TCP;
		break;
	default:
		return -ESOCKTNOSUPPORT;
	}

	/* Initialize default send parameters. These parameters can be
	 * modified with the SCTP_DEFAULT_SEND_PARAM socket option.
	 */
	sp->default_stream = 0;
	sp->default_ppid = 0;
	sp->default_flags = 0;
	sp->default_context = 0;
	sp->default_timetolive = 0;

	sp->default_rcv_context = 0;
	sp->max_burst = sctp_max_burst;

	/* Initialize default setup parameters. These parameters
	 * can be modified with the SCTP_INITMSG socket option or
	 * overridden by the SCTP_INIT CMSG.
	 */
	sp->initmsg.sinit_num_ostreams   = sctp_max_outstreams;
	sp->initmsg.sinit_max_instreams  = sctp_max_instreams;
	sp->initmsg.sinit_max_attempts   = sctp_max_retrans_init;
	sp->initmsg.sinit_max_init_timeo = sctp_rto_max;

	/* Initialize default RTO related parameters.  These parameters can
	 * be modified for with the SCTP_RTOINFO socket option.
	 */
	sp->rtoinfo.srto_initial = sctp_rto_initial;
	sp->rtoinfo.srto_max     = sctp_rto_max;
	sp->rtoinfo.srto_min     = sctp_rto_min;

	/* Initialize default association related parameters. These parameters
	 * can be modified with the SCTP_ASSOCINFO socket option.
	 */
	sp->assocparams.sasoc_asocmaxrxt = sctp_max_retrans_association;
	sp->assocparams.sasoc_number_peer_destinations = 0;
	sp->assocparams.sasoc_peer_rwnd = 0;
	sp->assocparams.sasoc_local_rwnd = 0;
	sp->assocparams.sasoc_cookie_life = sctp_valid_cookie_life;

	/* Initialize default event subscriptions. By default, all the
	 * options are off.
	 */
	memset(&sp->subscribe, 0, sizeof(struct sctp_event_subscribe));

	/* Default Peer Address Parameters.  These defaults can
	 * be modified via SCTP_PEER_ADDR_PARAMS
	 */
	sp->hbinterval  = sctp_hb_interval;
	sp->pathmaxrxt  = sctp_max_retrans_path;
	sp->pathmtu     = 0; // allow default discovery
	sp->sackdelay   = sctp_sack_timeout;
	sp->param_flags = SPP_HB_ENABLE |
			  SPP_PMTUD_ENABLE |
			  SPP_SACKDELAY_ENABLE;

	/* If enabled no SCTP message fragmentation will be performed.
	 * Configure through SCTP_DISABLE_FRAGMENTS socket option.
	 */
	sp->disable_fragments = 0;

	/* Enable Nagle algorithm by default.  */
	sp->nodelay           = 0;

	/* Enable by default. */
	sp->v4mapped          = 1;

	/* Auto-close idle associations after the configured
	 * number of seconds.  A value of 0 disables this
	 * feature.  Configure through the SCTP_AUTOCLOSE socket option,
	 * for UDP-style sockets only.
	 */
	sp->autoclose         = 0;

	/* User specified fragmentation limit. */
	sp->user_frag         = 0;

	sp->adaptation_ind = 0;

	sp->pf = sctp_get_pf_specific(sk->sk_family);

	/* Control variables for partial data delivery. */
	atomic_set(&sp->pd_mode, 0);
	skb_queue_head_init(&sp->pd_lobby);
	sp->frag_interleave = 0;

	/* Create a per socket endpoint structure.  Even if we
	 * change the data structure relationships, this may still
	 * be useful for storing pre-connect address information.
	 */
	ep = sctp_endpoint_new(sk, GFP_KERNEL);
	if (!ep)
		return -ENOMEM;

	sp->ep = ep;
	sp->hmac = NULL;

	SCTP_DBG_OBJCNT_INC(sock);
	return 0;
}

/* Cleanup any SCTP per socket resources.  */
SCTP_STATIC int sctp_destroy_sock(struct sock *sk)
{
	struct sctp_endpoint *ep;

	SCTP_DEBUG_PRINTK("sctp_destroy_sock(sk: %p)\n", sk);

	/* Release our hold on the endpoint. */
	ep = sctp_sk(sk)->ep;
	sctp_endpoint_free(ep);

	return 0;
}

/* API 4.1.7 shutdown() - TCP Style Syntax
 *     int shutdown(int socket, int how);
 *
 *     sd      - the socket descriptor of the association to be closed.
 *     how     - Specifies the type of shutdown.  The  values  are
 *               as follows:
 *               SHUT_RD
 *                     Disables further receive operations. No SCTP
 *                     protocol action is taken.
 *               SHUT_WR
 *                     Disables further send operations, and initiates
 *                     the SCTP shutdown sequence.
 *               SHUT_RDWR
 *                     Disables further send  and  receive  operations
 *                     and initiates the SCTP shutdown sequence.
 */
SCTP_STATIC void sctp_shutdown(struct sock *sk, int how)
{
	struct sctp_endpoint *ep;
	struct sctp_association *asoc;

	if (!sctp_style(sk, TCP))
		return;

	if (how & SEND_SHUTDOWN) {
		ep = sctp_sk(sk)->ep;
		if (!list_empty(&ep->asocs)) {
			asoc = list_entry(ep->asocs.next,
					  struct sctp_association, asocs);
			sctp_primitive_SHUTDOWN(asoc, NULL);
		}
	}
}

/* 7.2.1 Association Status (SCTP_STATUS)

 * Applications can retrieve current status information about an
 * association, including association state, peer receiver window size,
 * number of unacked data chunks, and number of data chunks pending
 * receipt.  This information is read-only.
 */
static int sctp_getsockopt_sctp_status(struct sock *sk, int len,
				       char __user *optval,
				       int __user *optlen)
{
	struct sctp_status status;
	struct sctp_association *asoc = NULL;
	struct sctp_transport *transport;
	sctp_assoc_t associd;
	int retval = 0;

	if (len != sizeof(status)) {
		retval = -EINVAL;
		goto out;
	}

	if (copy_from_user(&status, optval, sizeof(status))) {
		retval = -EFAULT;
		goto out;
	}

	associd = status.sstat_assoc_id;
	asoc = sctp_id2assoc(sk, associd);
	if (!asoc) {
		retval = -EINVAL;
		goto out;
	}

	transport = asoc->peer.primary_path;

	status.sstat_assoc_id = sctp_assoc2id(asoc);
	status.sstat_state = asoc->state;
	status.sstat_rwnd =  asoc->peer.rwnd;
	status.sstat_unackdata = asoc->unack_data;

	status.sstat_penddata = sctp_tsnmap_pending(&asoc->peer.tsn_map);
	status.sstat_instrms = asoc->c.sinit_max_instreams;
	status.sstat_outstrms = asoc->c.sinit_num_ostreams;
	status.sstat_fragmentation_point = asoc->frag_point;
	status.sstat_primary.spinfo_assoc_id = sctp_assoc2id(transport->asoc);
	memcpy(&status.sstat_primary.spinfo_address, &transport->ipaddr,
			transport->af_specific->sockaddr_len);
	/* Map ipv4 address into v4-mapped-on-v6 address.  */
	sctp_get_pf_specific(sk->sk_family)->addr_v4map(sctp_sk(sk),
		(union sctp_addr *)&status.sstat_primary.spinfo_address);
	status.sstat_primary.spinfo_state = transport->state;
	status.sstat_primary.spinfo_cwnd = transport->cwnd;
	status.sstat_primary.spinfo_srtt = transport->srtt;
	status.sstat_primary.spinfo_rto = jiffies_to_msecs(transport->rto);
	status.sstat_primary.spinfo_mtu = transport->pathmtu;

	if (status.sstat_primary.spinfo_state == SCTP_UNKNOWN)
		status.sstat_primary.spinfo_state = SCTP_ACTIVE;

	if (put_user(len, optlen)) {
		retval = -EFAULT;
		goto out;
	}

	SCTP_DEBUG_PRINTK("sctp_getsockopt_sctp_status(%d): %d %d %d\n",
			  len, status.sstat_state, status.sstat_rwnd,
			  status.sstat_assoc_id);

	if (copy_to_user(optval, &status, len)) {
		retval = -EFAULT;
		goto out;
	}

out:
	return (retval);
}


/* 7.2.2 Peer Address Information (SCTP_GET_PEER_ADDR_INFO)
 *
 * Applications can retrieve information about a specific peer address
 * of an association, including its reachability state, congestion
 * window, and retransmission timer values.  This information is
 * read-only.
 */
static int sctp_getsockopt_peer_addr_info(struct sock *sk, int len,
					  char __user *optval,
					  int __user *optlen)
{
	struct sctp_paddrinfo pinfo;
	struct sctp_transport *transport;
	int retval = 0;

	if (len != sizeof(pinfo)) {
		retval = -EINVAL;
		goto out;
	}

	if (copy_from_user(&pinfo, optval, sizeof(pinfo))) {
		retval = -EFAULT;
		goto out;
	}

	transport = sctp_addr_id2transport(sk, &pinfo.spinfo_address,
					   pinfo.spinfo_assoc_id);
	if (!transport)
		return -EINVAL;

	pinfo.spinfo_assoc_id = sctp_assoc2id(transport->asoc);
	pinfo.spinfo_state = transport->state;
	pinfo.spinfo_cwnd = transport->cwnd;
	pinfo.spinfo_srtt = transport->srtt;
	pinfo.spinfo_rto = jiffies_to_msecs(transport->rto);
	pinfo.spinfo_mtu = transport->pathmtu;

	if (pinfo.spinfo_state == SCTP_UNKNOWN)
		pinfo.spinfo_state = SCTP_ACTIVE;

	if (put_user(len, optlen)) {
		retval = -EFAULT;
		goto out;
	}

	if (copy_to_user(optval, &pinfo, len)) {
		retval = -EFAULT;
		goto out;
	}

out:
	return (retval);
}

/* 7.1.12 Enable/Disable message fragmentation (SCTP_DISABLE_FRAGMENTS)
 *
 * This option is a on/off flag.  If enabled no SCTP message
 * fragmentation will be performed.  Instead if a message being sent
 * exceeds the current PMTU size, the message will NOT be sent and
 * instead a error will be indicated to the user.
 */
static int sctp_getsockopt_disable_fragments(struct sock *sk, int len,
					char __user *optval, int __user *optlen)
{
	int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);
	val = (sctp_sk(sk)->disable_fragments == 1);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

/* 7.1.15 Set notification and ancillary events (SCTP_EVENTS)
 *
 * This socket option is used to specify various notifications and
 * ancillary data the user wishes to receive.
 */
static int sctp_getsockopt_events(struct sock *sk, int len, char __user *optval,
				  int __user *optlen)
{
	if (len != sizeof(struct sctp_event_subscribe))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->subscribe, len))
		return -EFAULT;
	return 0;
}

/* 7.1.8 Automatic Close of associations (SCTP_AUTOCLOSE)
 *
 * This socket option is applicable to the UDP-style socket only.  When
 * set it will cause associations that are idle for more than the
 * specified number of seconds to automatically close.  An association
 * being idle is defined an association that has NOT sent or received
 * user data.  The special value of '0' indicates that no automatic
 * close of any associations should be performed.  The option expects an
 * integer defining the number of seconds of idle time before an
 * association is closed.
 */
static int sctp_getsockopt_autoclose(struct sock *sk, int len, char __user *optval, int __user *optlen)
{
	/* Applicable to UDP-style socket only */
	if (sctp_style(sk, TCP))
		return -EOPNOTSUPP;
	if (len != sizeof(int))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->autoclose, len))
		return -EFAULT;
	return 0;
}

/* Helper routine to branch off an association to a new socket.  */
SCTP_STATIC int sctp_do_peeloff(struct sctp_association *asoc,
				struct socket **sockp)
{
	struct sock *sk = asoc->base.sk;
	struct socket *sock;
	struct inet_sock *inetsk;
	int err = 0;

	/* An association cannot be branched off from an already peeled-off
	 * socket, nor is this supported for tcp style sockets.
	 */
	if (!sctp_style(sk, UDP))
		return -EINVAL;

	/* Create a new socket.  */
	err = sock_create(sk->sk_family, SOCK_SEQPACKET, IPPROTO_SCTP, &sock);
	if (err < 0)
		return err;

	/* Populate the fields of the newsk from the oldsk and migrate the
	 * asoc to the newsk.
	 */
	sctp_sock_migrate(sk, sock->sk, asoc, SCTP_SOCKET_UDP_HIGH_BANDWIDTH);

	/* Make peeled-off sockets more like 1-1 accepted sockets.
	 * Set the daddr and initialize id to something more random
	 */
	inetsk = inet_sk(sock->sk);
	inetsk->daddr = asoc->peer.primary_addr.v4.sin_addr.s_addr;
	inetsk->id = asoc->next_tsn ^ jiffies;

	*sockp = sock;

	return err;
}

static int sctp_getsockopt_peeloff(struct sock *sk, int len, char __user *optval, int __user *optlen)
{
	sctp_peeloff_arg_t peeloff;
	struct socket *newsock;
	int retval = 0;
	struct sctp_association *asoc;

	if (len != sizeof(sctp_peeloff_arg_t))
		return -EINVAL;
	if (copy_from_user(&peeloff, optval, len))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, peeloff.associd);
	if (!asoc) {
		retval = -EINVAL;
		goto out;
	}

	SCTP_DEBUG_PRINTK("%s: sk: %p asoc: %p\n", __FUNCTION__, sk, asoc);

	retval = sctp_do_peeloff(asoc, &newsock);
	if (retval < 0)
		goto out;

	/* Map the socket to an unused fd that can be returned to the user.  */
	retval = sock_map_fd(newsock);
	if (retval < 0) {
		sock_release(newsock);
		goto out;
	}

	SCTP_DEBUG_PRINTK("%s: sk: %p asoc: %p newsk: %p sd: %d\n",
			  __FUNCTION__, sk, asoc, newsock->sk, retval);

	/* Return the fd mapped to the new socket.  */
	peeloff.sd = retval;
	if (copy_to_user(optval, &peeloff, len))
		retval = -EFAULT;

out:
	return retval;
}

/* 7.1.13 Peer Address Parameters (SCTP_PEER_ADDR_PARAMS)
 *
 * Applications can enable or disable heartbeats for any peer address of
 * an association, modify an address's heartbeat interval, force a
 * heartbeat to be sent immediately, and adjust the address's maximum
 * number of retransmissions sent before an address is considered
 * unreachable.  The following structure is used to access and modify an
 * address's parameters:
 *
 *  struct sctp_paddrparams {
 *     sctp_assoc_t            spp_assoc_id;
 *     struct sockaddr_storage spp_address;
 *     uint32_t                spp_hbinterval;
 *     uint16_t                spp_pathmaxrxt;
 *     uint32_t                spp_pathmtu;
 *     uint32_t                spp_sackdelay;
 *     uint32_t                spp_flags;
 * };
 *
 *   spp_assoc_id    - (one-to-many style socket) This is filled in the
 *                     application, and identifies the association for
 *                     this query.
 *   spp_address     - This specifies which address is of interest.
 *   spp_hbinterval  - This contains the value of the heartbeat interval,
 *                     in milliseconds.  If a  value of zero
 *                     is present in this field then no changes are to
 *                     be made to this parameter.
 *   spp_pathmaxrxt  - This contains the maximum number of
 *                     retransmissions before this address shall be
 *                     considered unreachable. If a  value of zero
 *                     is present in this field then no changes are to
 *                     be made to this parameter.
 *   spp_pathmtu     - When Path MTU discovery is disabled the value
 *                     specified here will be the "fixed" path mtu.
 *                     Note that if the spp_address field is empty
 *                     then all associations on this address will
 *                     have this fixed path mtu set upon them.
 *
 *   spp_sackdelay   - When delayed sack is enabled, this value specifies
 *                     the number of milliseconds that sacks will be delayed
 *                     for. This value will apply to all addresses of an
 *                     association if the spp_address field is empty. Note
 *                     also, that if delayed sack is enabled and this
 *                     value is set to 0, no change is made to the last
 *                     recorded delayed sack timer value.
 *
 *   spp_flags       - These flags are used to control various features
 *                     on an association. The flag field may contain
 *                     zero or more of the following options.
 *
 *                     SPP_HB_ENABLE  - Enable heartbeats on the
 *                     specified address. Note that if the address
 *                     field is empty all addresses for the association
 *                     have heartbeats enabled upon them.
 *
 *                     SPP_HB_DISABLE - Disable heartbeats on the
 *                     speicifed address. Note that if the address
 *                     field is empty all addresses for the association
 *                     will have their heartbeats disabled. Note also
 *                     that SPP_HB_ENABLE and SPP_HB_DISABLE are
 *                     mutually exclusive, only one of these two should
 *                     be specified. Enabling both fields will have
 *                     undetermined results.
 *
 *                     SPP_HB_DEMAND - Request a user initiated heartbeat
 *                     to be made immediately.
 *
 *                     SPP_PMTUD_ENABLE - This field will enable PMTU
 *                     discovery upon the specified address. Note that
 *                     if the address feild is empty then all addresses
 *                     on the association are effected.
 *
 *                     SPP_PMTUD_DISABLE - This field will disable PMTU
 *                     discovery upon the specified address. Note that
 *                     if the address feild is empty then all addresses
 *                     on the association are effected. Not also that
 *                     SPP_PMTUD_ENABLE and SPP_PMTUD_DISABLE are mutually
 *                     exclusive. Enabling both will have undetermined
 *                     results.
 *
 *                     SPP_SACKDELAY_ENABLE - Setting this flag turns
 *                     on delayed sack. The time specified in spp_sackdelay
 *                     is used to specify the sack delay for this address. Note
 *                     that if spp_address is empty then all addresses will
 *                     enable delayed sack and take on the sack delay
 *                     value specified in spp_sackdelay.
 *                     SPP_SACKDELAY_DISABLE - Setting this flag turns
 *                     off delayed sack. If the spp_address field is blank then
 *                     delayed sack is disabled for the entire association. Note
 *                     also that this field is mutually exclusive to
 *                     SPP_SACKDELAY_ENABLE, setting both will have undefined
 *                     results.
 */
static int sctp_getsockopt_peer_addr_params(struct sock *sk, int len,
					    char __user *optval, int __user *optlen)
{
	struct sctp_paddrparams  params;
	struct sctp_transport   *trans = NULL;
	struct sctp_association *asoc = NULL;
	struct sctp_sock        *sp = sctp_sk(sk);

	if (len != sizeof(struct sctp_paddrparams))
		return -EINVAL;

	if (copy_from_user(&params, optval, len))
		return -EFAULT;

	/* If an address other than INADDR_ANY is specified, and
	 * no transport is found, then the request is invalid.
	 */
	if (!sctp_is_any(( union sctp_addr *)&params.spp_address)) {
		trans = sctp_addr_id2transport(sk, &params.spp_address,
					       params.spp_assoc_id);
		if (!trans) {
			SCTP_DEBUG_PRINTK("Failed no transport\n");
			return -EINVAL;
		}
	}

	/* Get association, if assoc_id != 0 and the socket is a one
	 * to many style socket, and an association was not found, then
	 * the id was invalid.
	 */
	asoc = sctp_id2assoc(sk, params.spp_assoc_id);
	if (!asoc && params.spp_assoc_id && sctp_style(sk, UDP)) {
		SCTP_DEBUG_PRINTK("Failed no association\n");
		return -EINVAL;
	}

	if (trans) {
		/* Fetch transport values. */
		params.spp_hbinterval = jiffies_to_msecs(trans->hbinterval);
		params.spp_pathmtu    = trans->pathmtu;
		params.spp_pathmaxrxt = trans->pathmaxrxt;
		params.spp_sackdelay  = jiffies_to_msecs(trans->sackdelay);

		/*draft-11 doesn't say what to return in spp_flags*/
		params.spp_flags      = trans->param_flags;
	} else if (asoc) {
		/* Fetch association values. */
		params.spp_hbinterval = jiffies_to_msecs(asoc->hbinterval);
		params.spp_pathmtu    = asoc->pathmtu;
		params.spp_pathmaxrxt = asoc->pathmaxrxt;
		params.spp_sackdelay  = jiffies_to_msecs(asoc->sackdelay);

		/*draft-11 doesn't say what to return in spp_flags*/
		params.spp_flags      = asoc->param_flags;
	} else {
		/* Fetch socket values. */
		params.spp_hbinterval = sp->hbinterval;
		params.spp_pathmtu    = sp->pathmtu;
		params.spp_sackdelay  = sp->sackdelay;
		params.spp_pathmaxrxt = sp->pathmaxrxt;

		/*draft-11 doesn't say what to return in spp_flags*/
		params.spp_flags      = sp->param_flags;
	}

	if (copy_to_user(optval, &params, len))
		return -EFAULT;

	if (put_user(len, optlen))
		return -EFAULT;

	return 0;
}

/* 7.1.23. Delayed Ack Timer (SCTP_DELAYED_ACK_TIME)
 *
 *   This options will get or set the delayed ack timer.  The time is set
 *   in milliseconds.  If the assoc_id is 0, then this sets or gets the
 *   endpoints default delayed ack timer value.  If the assoc_id field is
 *   non-zero, then the set or get effects the specified association.
 *
 *   struct sctp_assoc_value {
 *       sctp_assoc_t            assoc_id;
 *       uint32_t                assoc_value;
 *   };
 *
 *     assoc_id    - This parameter, indicates which association the
 *                   user is preforming an action upon. Note that if
 *                   this field's value is zero then the endpoints
 *                   default value is changed (effecting future
 *                   associations only).
 *
 *     assoc_value - This parameter contains the number of milliseconds
 *                   that the user is requesting the delayed ACK timer
 *                   be set to. Note that this value is defined in
 *                   the standard to be between 200 and 500 milliseconds.
 *
 *                   Note: a value of zero will leave the value alone,
 *                   but disable SACK delay. A non-zero value will also
 *                   enable SACK delay.
 */
static int sctp_getsockopt_delayed_ack_time(struct sock *sk, int len,
					    char __user *optval,
					    int __user *optlen)
{
	struct sctp_assoc_value  params;
	struct sctp_association *asoc = NULL;
	struct sctp_sock        *sp = sctp_sk(sk);

	if (len != sizeof(struct sctp_assoc_value))
		return - EINVAL;

	if (copy_from_user(&params, optval, len))
		return -EFAULT;

	/* Get association, if assoc_id != 0 and the socket is a one
	 * to many style socket, and an association was not found, then
	 * the id was invalid.
	 */
	asoc = sctp_id2assoc(sk, params.assoc_id);
	if (!asoc && params.assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	if (asoc) {
		/* Fetch association values. */
		if (asoc->param_flags & SPP_SACKDELAY_ENABLE)
			params.assoc_value = jiffies_to_msecs(
				asoc->sackdelay);
		else
			params.assoc_value = 0;
	} else {
		/* Fetch socket values. */
		if (sp->param_flags & SPP_SACKDELAY_ENABLE)
			params.assoc_value  = sp->sackdelay;
		else
			params.assoc_value  = 0;
	}

	if (copy_to_user(optval, &params, len))
		return -EFAULT;

	if (put_user(len, optlen))
		return -EFAULT;

	return 0;
}

/* 7.1.3 Initialization Parameters (SCTP_INITMSG)
 *
 * Applications can specify protocol parameters for the default association
 * initialization.  The option name argument to setsockopt() and getsockopt()
 * is SCTP_INITMSG.
 *
 * Setting initialization parameters is effective only on an unconnected
 * socket (for UDP-style sockets only future associations are effected
 * by the change).  With TCP-style sockets, this option is inherited by
 * sockets derived from a listener socket.
 */
static int sctp_getsockopt_initmsg(struct sock *sk, int len, char __user *optval, int __user *optlen)
{
	if (len != sizeof(struct sctp_initmsg))
		return -EINVAL;
	if (copy_to_user(optval, &sctp_sk(sk)->initmsg, len))
		return -EFAULT;
	return 0;
}

static int sctp_getsockopt_peer_addrs_num_old(struct sock *sk, int len,
					      char __user *optval,
					      int __user *optlen)
{
	sctp_assoc_t id;
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;

	if (len != sizeof(sctp_assoc_t))
		return -EINVAL;

	if (copy_from_user(&id, optval, sizeof(sctp_assoc_t)))
		return -EFAULT;

	/* For UDP-style sockets, id specifies the association to query.  */
	asoc = sctp_id2assoc(sk, id);
	if (!asoc)
		return -EINVAL;

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		cnt ++;
	}

	return cnt;
}

/*
 * Old API for getting list of peer addresses. Does not work for 32-bit
 * programs running on a 64-bit kernel
 */
static int sctp_getsockopt_peer_addrs_old(struct sock *sk, int len,
					  char __user *optval,
					  int __user *optlen)
{
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs_old getaddrs;
	struct sctp_transport *from;
	void __user *to;
	union sctp_addr temp;
	struct sctp_sock *sp = sctp_sk(sk);
	int addrlen;

	if (len != sizeof(struct sctp_getaddrs_old))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs_old)))
		return -EFAULT;

	if (getaddrs.addr_num <= 0) return -EINVAL;

	/* For UDP-style sockets, id specifies the association to query.  */
	asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
	if (!asoc)
		return -EINVAL;

	to = (void __user *)getaddrs.addrs;
	list_for_each(pos, &asoc->peer.transport_addr_list) {
		from = list_entry(pos, struct sctp_transport, transports);
		memcpy(&temp, &from->ipaddr, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sp, &temp);
		addrlen = sctp_get_af_specific(sk->sk_family)->sockaddr_len;
		if (copy_to_user(to, &temp, addrlen))
			return -EFAULT;
		to += addrlen ;
		cnt ++;
		if (cnt >= getaddrs.addr_num) break;
	}
	getaddrs.addr_num = cnt;
	if (copy_to_user(optval, &getaddrs, sizeof(struct sctp_getaddrs_old)))
		return -EFAULT;

	return 0;
}

static int sctp_getsockopt_peer_addrs(struct sock *sk, int len,
				      char __user *optval, int __user *optlen)
{
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs getaddrs;
	struct sctp_transport *from;
	void __user *to;
	union sctp_addr temp;
	struct sctp_sock *sp = sctp_sk(sk);
	int addrlen;
	size_t space_left;
	int bytes_copied;

	if (len < sizeof(struct sctp_getaddrs))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	/* For UDP-style sockets, id specifies the association to query.  */
	asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
	if (!asoc)
		return -EINVAL;

	to = optval + offsetof(struct sctp_getaddrs,addrs);
	space_left = len - sizeof(struct sctp_getaddrs) -
			offsetof(struct sctp_getaddrs,addrs);

	list_for_each(pos, &asoc->peer.transport_addr_list) {
		from = list_entry(pos, struct sctp_transport, transports);
		memcpy(&temp, &from->ipaddr, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sp, &temp);
		addrlen = sctp_get_af_specific(sk->sk_family)->sockaddr_len;
		if(space_left < addrlen)
			return -ENOMEM;
		if (copy_to_user(to, &temp, addrlen))
			return -EFAULT;
		to += addrlen;
		cnt++;
		space_left -= addrlen;
	}

	if (put_user(cnt, &((struct sctp_getaddrs __user *)optval)->addr_num))
		return -EFAULT;
	bytes_copied = ((char __user *)to) - optval;
	if (put_user(bytes_copied, optlen))
		return -EFAULT;

	return 0;
}

static int sctp_getsockopt_local_addrs_num_old(struct sock *sk, int len,
					       char __user *optval,
					       int __user *optlen)
{
	sctp_assoc_t id;
	struct sctp_bind_addr *bp;
	struct sctp_association *asoc;
	struct list_head *pos, *temp;
	struct sctp_sockaddr_entry *addr;
	rwlock_t *addr_lock;
	int cnt = 0;

	if (len != sizeof(sctp_assoc_t))
		return -EINVAL;

	if (copy_from_user(&id, optval, sizeof(sctp_assoc_t)))
		return -EFAULT;

	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 *  If the id field is set to the value '0' then the locally bound
	 *  addresses are returned without regard to any particular
	 *  association.
	 */
	if (0 == id) {
		bp = &sctp_sk(sk)->ep->base.bind_addr;
		addr_lock = &sctp_sk(sk)->ep->base.addr_lock;
	} else {
		asoc = sctp_id2assoc(sk, id);
		if (!asoc)
			return -EINVAL;
		bp = &asoc->base.bind_addr;
		addr_lock = &asoc->base.addr_lock;
	}

	sctp_read_lock(addr_lock);

	/* If the endpoint is bound to 0.0.0.0 or ::0, count the valid
	 * addresses from the global local address list.
	 */
	if (sctp_list_single_entry(&bp->address_list)) {
		addr = list_entry(bp->address_list.next,
				  struct sctp_sockaddr_entry, list);
		if (sctp_is_any(&addr->a)) {
			list_for_each_safe(pos, temp, &sctp_local_addr_list) {
				addr = list_entry(pos,
						  struct sctp_sockaddr_entry,
						  list);
				if ((PF_INET == sk->sk_family) &&
				    (AF_INET6 == addr->a.sa.sa_family))
					continue;
				cnt++;
			}
		} else {
			cnt = 1;
		}
		goto done;
	}

	list_for_each(pos, &bp->address_list) {
		cnt ++;
	}

done:
	sctp_read_unlock(addr_lock);
	return cnt;
}

/* Helper function that copies local addresses to user and returns the number
 * of addresses copied.
 */
static int sctp_copy_laddrs_to_user_old(struct sock *sk, __u16 port, int max_addrs,
					void __user *to)
{
	struct list_head *pos, *next;
	struct sctp_sockaddr_entry *addr;
	union sctp_addr temp;
	int cnt = 0;
	int addrlen;

	list_for_each_safe(pos, next, &sctp_local_addr_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		if ((PF_INET == sk->sk_family) &&
		    (AF_INET6 == addr->a.sa.sa_family))
			continue;
		memcpy(&temp, &addr->a, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sctp_sk(sk),
								&temp);
		addrlen = sctp_get_af_specific(temp.sa.sa_family)->sockaddr_len;
		if (copy_to_user(to, &temp, addrlen))
			return -EFAULT;

		to += addrlen;
		cnt ++;
		if (cnt >= max_addrs) break;
	}

	return cnt;
}

static int sctp_copy_laddrs_to_user(struct sock *sk, __u16 port,
				    void __user **to, size_t space_left)
{
	struct list_head *pos, *next;
	struct sctp_sockaddr_entry *addr;
	union sctp_addr temp;
	int cnt = 0;
	int addrlen;

	list_for_each_safe(pos, next, &sctp_local_addr_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		if ((PF_INET == sk->sk_family) &&
		    (AF_INET6 == addr->a.sa.sa_family))
			continue;
		memcpy(&temp, &addr->a, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sctp_sk(sk),
								&temp);
		addrlen = sctp_get_af_specific(temp.sa.sa_family)->sockaddr_len;
		if(space_left<addrlen)
			return -ENOMEM;
		if (copy_to_user(*to, &temp, addrlen))
			return -EFAULT;

		*to += addrlen;
		cnt ++;
		space_left -= addrlen;
	}

	return cnt;
}

/* Old API for getting list of local addresses. Does not work for 32-bit
 * programs running on a 64-bit kernel
 */
static int sctp_getsockopt_local_addrs_old(struct sock *sk, int len,
					   char __user *optval, int __user *optlen)
{
	struct sctp_bind_addr *bp;
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs_old getaddrs;
	struct sctp_sockaddr_entry *addr;
	void __user *to;
	union sctp_addr temp;
	struct sctp_sock *sp = sctp_sk(sk);
	int addrlen;
	rwlock_t *addr_lock;
	int err = 0;

	if (len != sizeof(struct sctp_getaddrs_old))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs_old)))
		return -EFAULT;

	if (getaddrs.addr_num <= 0) return -EINVAL;
	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 *  If the id field is set to the value '0' then the locally bound
	 *  addresses are returned without regard to any particular
	 *  association.
	 */
	if (0 == getaddrs.assoc_id) {
		bp = &sctp_sk(sk)->ep->base.bind_addr;
		addr_lock = &sctp_sk(sk)->ep->base.addr_lock;
	} else {
		asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
		if (!asoc)
			return -EINVAL;
		bp = &asoc->base.bind_addr;
		addr_lock = &asoc->base.addr_lock;
	}

	to = getaddrs.addrs;

	sctp_read_lock(addr_lock);

	/* If the endpoint is bound to 0.0.0.0 or ::0, get the valid
	 * addresses from the global local address list.
	 */
	if (sctp_list_single_entry(&bp->address_list)) {
		addr = list_entry(bp->address_list.next,
				  struct sctp_sockaddr_entry, list);
		if (sctp_is_any(&addr->a)) {
			cnt = sctp_copy_laddrs_to_user_old(sk, bp->port,
							   getaddrs.addr_num,
							   to);
			if (cnt < 0) {
				err = cnt;
				goto unlock;
			}
			goto copy_getaddrs;
		}
	}

	list_for_each(pos, &bp->address_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		memcpy(&temp, &addr->a, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sp, &temp);
		addrlen = sctp_get_af_specific(temp.sa.sa_family)->sockaddr_len;
		if (copy_to_user(to, &temp, addrlen)) {
			err = -EFAULT;
			goto unlock;
		}
		to += addrlen;
		cnt ++;
		if (cnt >= getaddrs.addr_num) break;
	}

copy_getaddrs:
	getaddrs.addr_num = cnt;
	if (copy_to_user(optval, &getaddrs, sizeof(struct sctp_getaddrs_old)))
		err = -EFAULT;

unlock:
	sctp_read_unlock(addr_lock);
	return err;
}

static int sctp_getsockopt_local_addrs(struct sock *sk, int len,
				       char __user *optval, int __user *optlen)
{
	struct sctp_bind_addr *bp;
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;
	struct sctp_getaddrs getaddrs;
	struct sctp_sockaddr_entry *addr;
	void __user *to;
	union sctp_addr temp;
	struct sctp_sock *sp = sctp_sk(sk);
	int addrlen;
	rwlock_t *addr_lock;
	int err = 0;
	size_t space_left;
	int bytes_copied;

	if (len <= sizeof(struct sctp_getaddrs))
		return -EINVAL;

	if (copy_from_user(&getaddrs, optval, sizeof(struct sctp_getaddrs)))
		return -EFAULT;

	/*
	 *  For UDP-style sockets, id specifies the association to query.
	 *  If the id field is set to the value '0' then the locally bound
	 *  addresses are returned without regard to any particular
	 *  association.
	 */
	if (0 == getaddrs.assoc_id) {
		bp = &sctp_sk(sk)->ep->base.bind_addr;
		addr_lock = &sctp_sk(sk)->ep->base.addr_lock;
	} else {
		asoc = sctp_id2assoc(sk, getaddrs.assoc_id);
		if (!asoc)
			return -EINVAL;
		bp = &asoc->base.bind_addr;
		addr_lock = &asoc->base.addr_lock;
	}

	to = optval + offsetof(struct sctp_getaddrs,addrs);
	space_left = len - sizeof(struct sctp_getaddrs) -
			 offsetof(struct sctp_getaddrs,addrs);

	sctp_read_lock(addr_lock);

	/* If the endpoint is bound to 0.0.0.0 or ::0, get the valid
	 * addresses from the global local address list.
	 */
	if (sctp_list_single_entry(&bp->address_list)) {
		addr = list_entry(bp->address_list.next,
				  struct sctp_sockaddr_entry, list);
		if (sctp_is_any(&addr->a)) {
			cnt = sctp_copy_laddrs_to_user(sk, bp->port,
						       &to, space_left);
			if (cnt < 0) {
				err = cnt;
				goto unlock;
			}
			goto copy_getaddrs;
		}
	}

	list_for_each(pos, &bp->address_list) {
		addr = list_entry(pos, struct sctp_sockaddr_entry, list);
		memcpy(&temp, &addr->a, sizeof(temp));
		sctp_get_pf_specific(sk->sk_family)->addr_v4map(sp, &temp);
		addrlen = sctp_get_af_specific(temp.sa.sa_family)->sockaddr_len;
		if(space_left < addrlen)
			return -ENOMEM; /*fixme: right error?*/
		if (copy_to_user(to, &temp, addrlen)) {
			err = -EFAULT;
			goto unlock;
		}
		to += addrlen;
		cnt ++;
		space_left -= addrlen;
	}

copy_getaddrs:
	if (put_user(cnt, &((struct sctp_getaddrs __user *)optval)->addr_num))
		return -EFAULT;
	bytes_copied = ((char __user *)to) - optval;
	if (put_user(bytes_copied, optlen))
		return -EFAULT;

unlock:
	sctp_read_unlock(addr_lock);
	return err;
}

/* 7.1.10 Set Primary Address (SCTP_PRIMARY_ADDR)
 *
 * Requests that the local SCTP stack use the enclosed peer address as
 * the association primary.  The enclosed address must be one of the
 * association peer's addresses.
 */
static int sctp_getsockopt_primary_addr(struct sock *sk, int len,
					char __user *optval, int __user *optlen)
{
	struct sctp_prim prim;
	struct sctp_association *asoc;
	struct sctp_sock *sp = sctp_sk(sk);

	if (len != sizeof(struct sctp_prim))
		return -EINVAL;

	if (copy_from_user(&prim, optval, sizeof(struct sctp_prim)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, prim.ssp_assoc_id);
	if (!asoc)
		return -EINVAL;

	if (!asoc->peer.primary_path)
		return -ENOTCONN;

	memcpy(&prim.ssp_addr, &asoc->peer.primary_path->ipaddr,
		asoc->peer.primary_path->af_specific->sockaddr_len);

	sctp_get_pf_specific(sk->sk_family)->addr_v4map(sp,
			(union sctp_addr *)&prim.ssp_addr);

	if (copy_to_user(optval, &prim, sizeof(struct sctp_prim)))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.11  Set Adaptation Layer Indicator (SCTP_ADAPTATION_LAYER)
 *
 * Requests that the local endpoint set the specified Adaptation Layer
 * Indication parameter for all future INIT and INIT-ACK exchanges.
 */
static int sctp_getsockopt_adaptation_layer(struct sock *sk, int len,
				  char __user *optval, int __user *optlen)
{
	struct sctp_setadaptation adaptation;

	if (len != sizeof(struct sctp_setadaptation))
		return -EINVAL;

	adaptation.ssb_adaptation_ind = sctp_sk(sk)->adaptation_ind;
	if (copy_to_user(optval, &adaptation, len))
		return -EFAULT;

	return 0;
}

/*
 *
 * 7.1.14 Set default send parameters (SCTP_DEFAULT_SEND_PARAM)
 *
 *   Applications that wish to use the sendto() system call may wish to
 *   specify a default set of parameters that would normally be supplied
 *   through the inclusion of ancillary data.  This socket option allows
 *   such an application to set the default sctp_sndrcvinfo structure.


 *   The application that wishes to use this socket option simply passes
 *   in to this call the sctp_sndrcvinfo structure defined in Section
 *   5.2.2) The input parameters accepted by this call include
 *   sinfo_stream, sinfo_flags, sinfo_ppid, sinfo_context,
 *   sinfo_timetolive.  The user must provide the sinfo_assoc_id field in
 *   to this call if the caller is using the UDP model.
 *
 *   For getsockopt, it get the default sctp_sndrcvinfo structure.
 */
static int sctp_getsockopt_default_send_param(struct sock *sk,
					int len, char __user *optval,
					int __user *optlen)
{
	struct sctp_sndrcvinfo info;
	struct sctp_association *asoc;
	struct sctp_sock *sp = sctp_sk(sk);

	if (len != sizeof(struct sctp_sndrcvinfo))
		return -EINVAL;
	if (copy_from_user(&info, optval, sizeof(struct sctp_sndrcvinfo)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, info.sinfo_assoc_id);
	if (!asoc && info.sinfo_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	if (asoc) {
		info.sinfo_stream = asoc->default_stream;
		info.sinfo_flags = asoc->default_flags;
		info.sinfo_ppid = asoc->default_ppid;
		info.sinfo_context = asoc->default_context;
		info.sinfo_timetolive = asoc->default_timetolive;
	} else {
		info.sinfo_stream = sp->default_stream;
		info.sinfo_flags = sp->default_flags;
		info.sinfo_ppid = sp->default_ppid;
		info.sinfo_context = sp->default_context;
		info.sinfo_timetolive = sp->default_timetolive;
	}

	if (copy_to_user(optval, &info, sizeof(struct sctp_sndrcvinfo)))
		return -EFAULT;

	return 0;
}

/*
 *
 * 7.1.5 SCTP_NODELAY
 *
 * Turn on/off any Nagle-like algorithm.  This means that packets are
 * generally sent as soon as possible and no unnecessary delays are
 * introduced, at the cost of more packets in the network.  Expects an
 * integer boolean flag.
 */

static int sctp_getsockopt_nodelay(struct sock *sk, int len,
				   char __user *optval, int __user *optlen)
{
	int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);
	val = (sctp_sk(sk)->nodelay == 1);
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;
	return 0;
}

/*
 *
 * 7.1.1 SCTP_RTOINFO
 *
 * The protocol parameters used to initialize and bound retransmission
 * timeout (RTO) are tunable. sctp_rtoinfo structure is used to access
 * and modify these parameters.
 * All parameters are time values, in milliseconds.  A value of 0, when
 * modifying the parameters, indicates that the current value should not
 * be changed.
 *
 */
static int sctp_getsockopt_rtoinfo(struct sock *sk, int len,
				char __user *optval,
				int __user *optlen) {
	struct sctp_rtoinfo rtoinfo;
	struct sctp_association *asoc;

	if (len != sizeof (struct sctp_rtoinfo))
		return -EINVAL;

	if (copy_from_user(&rtoinfo, optval, sizeof (struct sctp_rtoinfo)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, rtoinfo.srto_assoc_id);

	if (!asoc && rtoinfo.srto_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	/* Values corresponding to the specific association. */
	if (asoc) {
		rtoinfo.srto_initial = jiffies_to_msecs(asoc->rto_initial);
		rtoinfo.srto_max = jiffies_to_msecs(asoc->rto_max);
		rtoinfo.srto_min = jiffies_to_msecs(asoc->rto_min);
	} else {
		/* Values corresponding to the endpoint. */
		struct sctp_sock *sp = sctp_sk(sk);

		rtoinfo.srto_initial = sp->rtoinfo.srto_initial;
		rtoinfo.srto_max = sp->rtoinfo.srto_max;
		rtoinfo.srto_min = sp->rtoinfo.srto_min;
	}

	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &rtoinfo, len))
		return -EFAULT;

	return 0;
}

/*
 *
 * 7.1.2 SCTP_ASSOCINFO
 *
 * This option is used to tune the the maximum retransmission attempts
 * of the association.
 * Returns an error if the new association retransmission value is
 * greater than the sum of the retransmission value  of the peer.
 * See [SCTP] for more information.
 *
 */
static int sctp_getsockopt_associnfo(struct sock *sk, int len,
				     char __user *optval,
				     int __user *optlen)
{

	struct sctp_assocparams assocparams;
	struct sctp_association *asoc;
	struct list_head *pos;
	int cnt = 0;

	if (len != sizeof (struct sctp_assocparams))
		return -EINVAL;

	if (copy_from_user(&assocparams, optval,
			sizeof (struct sctp_assocparams)))
		return -EFAULT;

	asoc = sctp_id2assoc(sk, assocparams.sasoc_assoc_id);

	if (!asoc && assocparams.sasoc_assoc_id && sctp_style(sk, UDP))
		return -EINVAL;

	/* Values correspoinding to the specific association */
	if (asoc) {
		assocparams.sasoc_asocmaxrxt = asoc->max_retrans;
		assocparams.sasoc_peer_rwnd = asoc->peer.rwnd;
		assocparams.sasoc_local_rwnd = asoc->a_rwnd;
		assocparams.sasoc_cookie_life = (asoc->cookie_life.tv_sec
						* 1000) +
						(asoc->cookie_life.tv_usec
						/ 1000);

		list_for_each(pos, &asoc->peer.transport_addr_list) {
			cnt ++;
		}

		assocparams.sasoc_number_peer_destinations = cnt;
	} else {
		/* Values corresponding to the endpoint */
		struct sctp_sock *sp = sctp_sk(sk);

		assocparams.sasoc_asocmaxrxt = sp->assocparams.sasoc_asocmaxrxt;
		assocparams.sasoc_peer_rwnd = sp->assocparams.sasoc_peer_rwnd;
		assocparams.sasoc_local_rwnd = sp->assocparams.sasoc_local_rwnd;
		assocparams.sasoc_cookie_life =
					sp->assocparams.sasoc_cookie_life;
		assocparams.sasoc_number_peer_destinations =
					sp->assocparams.
					sasoc_number_peer_destinations;
	}

	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &assocparams, len))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.16 Set/clear IPv4 mapped addresses (SCTP_I_WANT_MAPPED_V4_ADDR)
 *
 * This socket option is a boolean flag which turns on or off mapped V4
 * addresses.  If this option is turned on and the socket is type
 * PF_INET6, then IPv4 addresses will be mapped to V6 representation.
 * If this option is turned off, then no mapping will be done of V4
 * addresses and a user will receive both PF_INET6 and PF_INET type
 * addresses on the socket.
 */
static int sctp_getsockopt_mappedv4(struct sock *sk, int len,
				    char __user *optval, int __user *optlen)
{
	int val;
	struct sctp_sock *sp = sctp_sk(sk);

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);
	val = sp->v4mapped;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.29.  Set or Get the default context (SCTP_CONTEXT)
 * (chapter and verse is quoted at sctp_setsockopt_context())
 */
static int sctp_getsockopt_context(struct sock *sk, int len,
				   char __user *optval, int __user *optlen)
{
	struct sctp_assoc_value params;
	struct sctp_sock *sp;
	struct sctp_association *asoc;

	if (len != sizeof(struct sctp_assoc_value))
		return -EINVAL;

	if (copy_from_user(&params, optval, len))
		return -EFAULT;

	sp = sctp_sk(sk);

	if (params.assoc_id != 0) {
		asoc = sctp_id2assoc(sk, params.assoc_id);
		if (!asoc)
			return -EINVAL;
		params.assoc_value = asoc->default_rcv_context;
	} else {
		params.assoc_value = sp->default_rcv_context;
	}

	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &params, len))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.17 Set the maximum fragrmentation size (SCTP_MAXSEG)
 *
 * This socket option specifies the maximum size to put in any outgoing
 * SCTP chunk.  If a message is larger than this size it will be
 * fragmented by SCTP into the specified size.  Note that the underlying
 * SCTP implementation may fragment into smaller sized chunks when the
 * PMTU of the underlying association is smaller than the value set by
 * the user.
 */
static int sctp_getsockopt_maxseg(struct sock *sk, int len,
				  char __user *optval, int __user *optlen)
{
	int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);

	val = sctp_sk(sk)->user_frag;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.24.  Get or set fragmented interleave (SCTP_FRAGMENT_INTERLEAVE)
 * (chapter and verse is quoted at sctp_setsockopt_fragment_interleave())
 */
static int sctp_getsockopt_fragment_interleave(struct sock *sk, int len,
					       char __user *optval, int __user *optlen)
{
	int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);

	val = sctp_sk(sk)->frag_interleave;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

/*
 * 7.1.25.  Set or Get the sctp partial delivery point
 * (chapter and verse is quoted at sctp_setsockopt_partial_delivery_point())
 */
static int sctp_getsockopt_partial_delivery_point(struct sock *sk, int len,
						  char __user *optval,
						  int __user *optlen)
{
        u32 val;

	if (len < sizeof(u32))
		return -EINVAL;

	len = sizeof(u32);

	val = sctp_sk(sk)->pd_point;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return -ENOTSUPP;
}

/*
 * 7.1.28.  Set or Get the maximum burst (SCTP_MAX_BURST)
 * (chapter and verse is quoted at sctp_setsockopt_maxburst())
 */
static int sctp_getsockopt_maxburst(struct sock *sk, int len,
				    char __user *optval,
				    int __user *optlen)
{
        int val;

	if (len < sizeof(int))
		return -EINVAL;

	len = sizeof(int);

	val = sctp_sk(sk)->max_burst;
	if (put_user(len, optlen))
		return -EFAULT;
	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return -ENOTSUPP;
}

SCTP_STATIC int sctp_getsockopt(struct sock *sk, int level, int optname,
				char __user *optval, int __user *optlen)
{
	int retval = 0;
	int len;

	SCTP_DEBUG_PRINTK("sctp_getsockopt(sk: %p... optname: %d)\n",
			  sk, optname);

	/* I can hardly begin to describe how wrong this is.  This is
	 * so broken as to be worse than useless.  The API draft
	 * REALLY is NOT helpful here...  I am not convinced that the
	 * semantics of getsockopt() with a level OTHER THAN SOL_SCTP
	 * are at all well-founded.
	 */
	if (level != SOL_SCTP) {
		struct sctp_af *af = sctp_sk(sk)->pf->af;

		retval = af->getsockopt(sk, level, optname, optval, optlen);
		return retval;
	}

	if (get_user(len, optlen))
		return -EFAULT;

	sctp_lock_sock(sk);

	switch (optname) {
	case SCTP_STATUS:
		retval = sctp_getsockopt_sctp_status(sk, len, optval, optlen);
		break;
	case SCTP_DISABLE_FRAGMENTS:
		retval = sctp_getsockopt_disable_fragments(sk, len, optval,
							   optlen);
		break;
	case SCTP_EVENTS:
		retval = sctp_getsockopt_events(sk, len, optval, optlen);
		break;
	case SCTP_AUTOCLOSE:
		retval = sctp_getsockopt_autoclose(sk, len, optval, optlen);
		break;
	case SCTP_SOCKOPT_PEELOFF:
		retval = sctp_getsockopt_peeloff(sk, len, optval, optlen);
		break;
	case SCTP_PEER_ADDR_PARAMS:
		retval = sctp_getsockopt_peer_addr_params(sk, len, optval,
							  optlen);
		break;
	case SCTP_DELAYED_ACK_TIME:
		retval = sctp_getsockopt_delayed_ack_time(sk, len, optval,
							  optlen);
		break;
	case SCTP_INITMSG:
		retval = sctp_getsockopt_initmsg(sk, len, optval, optlen);
		break;
	case SCTP_GET_PEER_ADDRS_NUM_OLD:
		retval = sctp_getsockopt_peer_addrs_num_old(sk, len, optval,
							    optlen);
		break;
	case SCTP_GET_LOCAL_ADDRS_NUM_OLD:
		retval = sctp_getsockopt_local_addrs_num_old(sk, len, optval,
							     optlen);
		break;
	case SCTP_GET_PEER_ADDRS_OLD:
		retval = sctp_getsockopt_peer_addrs_old(sk, len, optval,
							optlen);
		break;
	case SCTP_GET_LOCAL_ADDRS_OLD:
		retval = sctp_getsockopt_local_addrs_old(sk, len, optval,
							 optlen);
		break;
	case SCTP_GET_PEER_ADDRS:
		retval = sctp_getsockopt_peer_addrs(sk, len, optval,
						    optlen);
		break;
	case SCTP_GET_LOCAL_ADDRS:
		retval = sctp_getsockopt_local_addrs(sk, len, optval,
						     optlen);
		break;
	case SCTP_DEFAULT_SEND_PARAM:
		retval = sctp_getsockopt_default_send_param(sk, len,
							    optval, optlen);
		break;
	case SCTP_PRIMARY_ADDR:
		retval = sctp_getsockopt_primary_addr(sk, len, optval, optlen);
		break;
	case SCTP_NODELAY:
		retval = sctp_getsockopt_nodelay(sk, len, optval, optlen);
		break;
	case SCTP_RTOINFO:
		retval = sctp_getsockopt_rtoinfo(sk, len, optval, optlen);
		break;
	case SCTP_ASSOCINFO:
		retval = sctp_getsockopt_associnfo(sk, len, optval, optlen);
		break;
	case SCTP_I_WANT_MAPPED_V4_ADDR:
		retval = sctp_getsockopt_mappedv4(sk, len, optval, optlen);
		break;
	case SCTP_MAXSEG:
		retval = sctp_getsockopt_maxseg(sk, len, optval, optlen);
		break;
	case SCTP_GET_PEER_ADDR_INFO:
		retval = sctp_getsockopt_peer_addr_info(sk, len, optval,
							optlen);
		break;
	case SCTP_ADAPTATION_LAYER:
		retval = sctp_getsockopt_adaptation_layer(sk, len, optval,
							optlen);
		break;
	case SCTP_CONTEXT:
		retval = sctp_getsockopt_context(sk, len, optval, optlen);
		break;
	case SCTP_FRAGMENT_INTERLEAVE:
		retval = sctp_getsockopt_fragment_interleave(sk, len, optval,
							     optlen);
		break;
	case SCTP_PARTIAL_DELIVERY_POINT:
		retval = sctp_getsockopt_partial_delivery_point(sk, len, optval,
								optlen);
		break;
	case SCTP_MAX_BURST:
		retval = sctp_getsockopt_maxburst(sk, len, optval, optlen);
		break;
	default:
		retval = -ENOPROTOOPT;
		break;
	}

	sctp_release_sock(sk);
	return retval;
}

static void sctp_hash(struct sock *sk)
{
	/* STUB */
}

static void sctp_unhash(struct sock *sk)
{
	/* STUB */
}

/* Check if port is acceptable.  Possibly find first available port.
 *
 * The port hash table (contained in the 'global' SCTP protocol storage
 * returned by struct sctp_protocol *sctp_get_protocol()). The hash
 * table is an array of 4096 lists (sctp_bind_hashbucket). Each
 * list (the list number is the port number hashed out, so as you
 * would expect from a hash function, all the ports in a given list have
 * such a number that hashes out to the same list number; you were
 * expecting that, right?); so each list has a set of ports, with a
 * link to the socket (struct sock) that uses it, the port number and
 * a fastreuse flag (FIXME: NPI ipg).
 */
static struct sctp_bind_bucket *sctp_bucket_create(
	struct sctp_bind_hashbucket *head, unsigned short snum);

static long sctp_get_port_local(struct sock *sk, union sctp_addr *addr)
{
	struct sctp_bind_hashbucket *head; /* hash list */
	struct sctp_bind_bucket *pp; /* hash list port iterator */
	unsigned short snum;
	int ret;

	snum = ntohs(addr->v4.sin_port);

	SCTP_DEBUG_PRINTK("sctp_get_port() begins, snum=%d\n", snum);
	sctp_local_bh_disable();

	if (snum == 0) {
		/* Search for an available port.
		 *
		 * 'sctp_port_rover' was the last port assigned, so
		 * we start to search from 'sctp_port_rover +
		 * 1'. What we do is first check if port 'rover' is
		 * already in the hash table; if not, we use that; if
		 * it is, we try next.
		 */
		int low = sysctl_local_port_range[0];
		int high = sysctl_local_port_range[1];
		int remaining = (high - low) + 1;
		int rover;
		int index;

		sctp_spin_lock(&sctp_port_alloc_lock);
		rover = sctp_port_rover;
		do {
			rover++;
			if ((rover < low) || (rover > high))
				rover = low;
			index = sctp_phashfn(rover);
			head = &sctp_port_hashtable[index];
			sctp_spin_lock(&head->lock);
			for (pp = head->chain; pp; pp = pp->next)
				if (pp->port == rover)
					goto next;
			break;
		next:
			sctp_spin_unlock(&head->lock);
		} while (--remaining > 0);
		sctp_port_rover = rover;
		sctp_spin_unlock(&sctp_port_alloc_lock);

		/* Exhausted local port range during search? */
		ret = 1;
		if (remaining <= 0)
			goto fail;

		/* OK, here is the one we will use.  HEAD (the port
		 * hash table list entry) is non-NULL and we hold it's
		 * mutex.
		 */
		snum = rover;
	} else {
		/* We are given an specific port number; we verify
		 * that it is not being used. If it is used, we will
		 * exahust the search in the hash list corresponding
		 * to the port number (snum) - we detect that with the
		 * port iterator, pp being NULL.
		 */
		head = &sctp_port_hashtable[sctp_phashfn(snum)];
		sctp_spin_lock(&head->lock);
		for (pp = head->chain; pp; pp = pp->next) {
			if (pp->port == snum)
				goto pp_found;
		}
	}
	pp = NULL;
	goto pp_not_found;
pp_found:
	if (!hlist_empty(&pp->owner)) {
		/* We had a port hash table hit - there is an
		 * available port (pp != NULL) and it is being
		 * used by other socket (pp->owner not empty); that other
		 * socket is going to be sk2.
		 */
		int reuse = sk->sk_reuse;
		struct sock *sk2;
		struct hlist_node *node;

		SCTP_DEBUG_PRINTK("sctp_get_port() found a possible match\n");
		if (pp->fastreuse && sk->sk_reuse)
			goto success;

		/* Run through the list of sockets bound to the port
		 * (pp->port) [via the pointers bind_next and
		 * bind_pprev in the struct sock *sk2 (pp->sk)]. On each one,
		 * we get the endpoint they describe and run through
		 * the endpoint's list of IP (v4 or v6) addresses,
		 * comparing each of the addresses with the address of
		 * the socket sk. If we find a match, then that means
		 * that this port/socket (sk) combination are already
		 * in an endpoint.
		 */
		sk_for_each_bound(sk2, node, &pp->owner) {
			struct sctp_endpoint *ep2;
			ep2 = sctp_sk(sk2)->ep;

			if (reuse && sk2->sk_reuse)
				continue;

			if (sctp_bind_addr_match(&ep2->base.bind_addr, addr,
						 sctp_sk(sk))) {
				ret = (long)sk2;
				goto fail_unlock;
			}
		}
		SCTP_DEBUG_PRINTK("sctp_get_port(): Found a match\n");
	}
pp_not_found:
	/* If there was a hash table miss, create a new port.  */
	ret = 1;
	if (!pp && !(pp = sctp_bucket_create(head, snum)))
		goto fail_unlock;

	/* In either case (hit or miss), make sure fastreuse is 1 only
	 * if sk->sk_reuse is too (that is, if the caller requested
	 * SO_REUSEADDR on this socket -sk-).
	 */
	if (hlist_empty(&pp->owner))
		pp->fastreuse = sk->sk_reuse ? 1 : 0;
	else if (pp->fastreuse && !sk->sk_reuse)
		pp->fastreuse = 0;

	/* We are set, so fill up all the data in the hash table
	 * entry, tie the socket list information with the rest of the
	 * sockets FIXME: Blurry, NPI (ipg).
	 */
success:
	inet_sk(sk)->num = snum;
	if (!sctp_sk(sk)->bind_hash) {
		sk_add_bind_node(sk, &pp->owner);
		sctp_sk(sk)->bind_hash = pp;
	}
	ret = 0;

fail_unlock:
	sctp_spin_unlock(&head->lock);

fail:
	sctp_local_bh_enable();
	return ret;
}

/* Assign a 'snum' port to the socket.  If snum == 0, an ephemeral
 * port is requested.
 */
static int sctp_get_port(struct sock *sk, unsigned short snum)
{
	long ret;
	union sctp_addr addr;
	struct sctp_af *af = sctp_sk(sk)->pf->af;

	/* Set up a dummy address struct from the sk. */
	af->from_sk(&addr, sk);
	addr.v4.sin_port = htons(snum);

	/* Note: sk->sk_num gets filled in if ephemeral port request. */
	ret = sctp_get_port_local(sk, &addr);

	return (ret ? 1 : 0);
}

/*
 * 3.1.3 listen() - UDP Style Syntax
 *
 *   By default, new associations are not accepted for UDP style sockets.
 *   An application uses listen() to mark a socket as being able to
 *   accept new associations.
 */
SCTP_STATIC int sctp_seqpacket_listen(struct sock *sk, int backlog)
{
	struct sctp_sock *sp = sctp_sk(sk);
	struct sctp_endpoint *ep = sp->ep;

	/* Only UDP style sockets that are not peeled off are allowed to
	 * listen().
	 */
	if (!sctp_style(sk, UDP))
		return -EINVAL;

	/* If backlog is zero, disable listening. */
	if (!backlog) {
		if (sctp_sstate(sk, CLOSED))
			return 0;

		sctp_unhash_endpoint(ep);
		sk->sk_state = SCTP_SS_CLOSED;
	}

	/* Return if we are already listening. */
	if (sctp_sstate(sk, LISTENING))
		return 0;

	/*
	 * If a bind() or sctp_bindx() is not called prior to a listen()
	 * call that allows new associations to be accepted, the system
	 * picks an ephemeral port and will choose an address set equivalent
	 * to binding with a wildcard address.
	 *
	 * This is not currently spelled out in the SCTP sockets
	 * extensions draft, but follows the practice as seen in TCP
	 * sockets.
	 */
	if (!ep->base.bind_addr.port) {
		if (sctp_autobind(sk))
			return -EAGAIN;
	}
	sk->sk_state = SCTP_SS_LISTENING;
	sctp_hash_endpoint(ep);
	return 0;
}

/*
 * 4.1.3 listen() - TCP Style Syntax
 *
 *   Applications uses listen() to ready the SCTP endpoint for accepting
 *   inbound associations.
 */
SCTP_STATIC int sctp_stream_listen(struct sock *sk, int backlog)
{
	struct sctp_sock *sp = sctp_sk(sk);
	struct sctp_endpoint *ep = sp->ep;

	/* If backlog is zero, disable listening. */
	if (!backlog) {
		if (sctp_sstate(sk, CLOSED))
			return 0;

		sctp_unhash_endpoint(ep);
		sk->sk_state = SCTP_SS_CLOSED;
	}

	if (sctp_sstate(sk, LISTENING))
		return 0;

	/*
	 * If a bind() or sctp_bindx() is not called prior to a listen()
	 * call that allows new associations to be accepted, the system
	 * picks an ephemeral port and will choose an address set equivalent
	 * to binding with a wildcard address.
	 *
	 * This is not currently spelled out in the SCTP sockets
	 * extensions draft, but follows the practice as seen in TCP
	 * sockets.
	 */
	if (!ep->base.bind_addr.port) {
		if (sctp_autobind(sk))
			return -EAGAIN;
	}
	sk->sk_state = SCTP_SS_LISTENING;
	sk->sk_max_ack_backlog = backlog;
	sctp_hash_endpoint(ep);
	return 0;
}

/*
 *  Move a socket to LISTENING state.
 */
int sctp_inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	struct crypto_hash *tfm = NULL;
	int err = -EINVAL;

	if (unlikely(backlog < 0))
		goto out;

	sctp_lock_sock(sk);

	if (sock->state != SS_UNCONNECTED)
		goto out;

	/* Allocate HMAC for generating cookie. */
	if (sctp_hmac_alg) {
		tfm = crypto_alloc_hash(sctp_hmac_alg, 0, CRYPTO_ALG_ASYNC);
		if (!tfm) {
			err = -ENOSYS;
			goto out;
		}
	}

	switch (sock->type) {
	case SOCK_SEQPACKET:
		err = sctp_seqpacket_listen(sk, backlog);
		break;
	case SOCK_STREAM:
		err = sctp_stream_listen(sk, backlog);
		break;
	default:
		break;
	}

	if (err)
		goto cleanup;

	/* Store away the transform reference. */
	sctp_sk(sk)->hmac = tfm;
out:
	sctp_release_sock(sk);
	return err;
cleanup:
	crypto_free_hash(tfm);
	goto out;
}

/*
 * This function is done by modeling the current datagram_poll() and the
 * tcp_poll().  Note that, based on these implementations, we don't
 * lock the socket in this function, even though it seems that,
 * ideally, locking or some other mechanisms can be used to ensure
 * the integrity of the counters (sndbuf and wmem_alloc) used
 * in this place.  We assume that we don't need locks either until proven
 * otherwise.
 *
 * Another thing to note is that we include the Async I/O support
 * here, again, by modeling the current TCP/UDP code.  We don't have
 * a good way to test with it yet.
 */
unsigned int sctp_poll(struct file *file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct sctp_sock *sp = sctp_sk(sk);
	unsigned int mask;

	poll_wait(file, sk->sk_sleep, wait);

	/* A TCP-style listening socket becomes readable when the accept queue
	 * is not empty.
	 */
	if (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING))
		return (!list_empty(&sp->ep->asocs)) ?
			(POLLIN | POLLRDNORM) : 0;

	mask = 0;

	/* Is there any exceptional events?  */
	if (sk->sk_err || !skb_queue_empty(&sk->sk_error_queue))
		mask |= POLLERR;
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		mask |= POLLRDHUP;
	if (sk->sk_shutdown == SHUTDOWN_MASK)
		mask |= POLLHUP;

	/* Is it readable?  Reconsider this code with TCP-style support.  */
	if (!skb_queue_empty(&sk->sk_receive_queue) ||
	    (sk->sk_shutdown & RCV_SHUTDOWN))
		mask |= POLLIN | POLLRDNORM;

	/* The association is either gone or not ready.  */
	if (!sctp_style(sk, UDP) && sctp_sstate(sk, CLOSED))
		return mask;

	/* Is it writable?  */
	if (sctp_writeable(sk)) {
		mask |= POLLOUT | POLLWRNORM;
	} else {
		set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);
		/*
		 * Since the socket is not locked, the buffer
		 * might be made available after the writeable check and
		 * before the bit is set.  This could cause a lost I/O
		 * signal.  tcp_poll() has a race breaker for this race
		 * condition.  Based on their implementation, we put
		 * in the following code to cover it as well.
		 */
		if (sctp_writeable(sk))
			mask |= POLLOUT | POLLWRNORM;
	}
	return mask;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

static struct sctp_bind_bucket *sctp_bucket_create(
	struct sctp_bind_hashbucket *head, unsigned short snum)
{
	struct sctp_bind_bucket *pp;

	pp = kmem_cache_alloc(sctp_bucket_cachep, GFP_ATOMIC);
	SCTP_DBG_OBJCNT_INC(bind_bucket);
	if (pp) {
		pp->port = snum;
		pp->fastreuse = 0;
		INIT_HLIST_HEAD(&pp->owner);
		if ((pp->next = head->chain) != NULL)
			pp->next->pprev = &pp->next;
		head->chain = pp;
		pp->pprev = &head->chain;
	}
	return pp;
}

/* Caller must hold hashbucket lock for this tb with local BH disabled */
static void sctp_bucket_destroy(struct sctp_bind_bucket *pp)
{
	if (pp && hlist_empty(&pp->owner)) {
		if (pp->next)
			pp->next->pprev = pp->pprev;
		*(pp->pprev) = pp->next;
		kmem_cache_free(sctp_bucket_cachep, pp);
		SCTP_DBG_OBJCNT_DEC(bind_bucket);
	}
}

/* Release this socket's reference to a local port.  */
static inline void __sctp_put_port(struct sock *sk)
{
	struct sctp_bind_hashbucket *head =
		&sctp_port_hashtable[sctp_phashfn(inet_sk(sk)->num)];
	struct sctp_bind_bucket *pp;

	sctp_spin_lock(&head->lock);
	pp = sctp_sk(sk)->bind_hash;
	__sk_del_bind_node(sk);
	sctp_sk(sk)->bind_hash = NULL;
	inet_sk(sk)->num = 0;
	sctp_bucket_destroy(pp);
	sctp_spin_unlock(&head->lock);
}

void sctp_put_port(struct sock *sk)
{
	sctp_local_bh_disable();
	__sctp_put_port(sk);
	sctp_local_bh_enable();
}

/*
 * The system picks an ephemeral port and choose an address set equivalent
 * to binding with a wildcard address.
 * One of those addresses will be the primary address for the association.
 * This automatically enables the multihoming capability of SCTP.
 */
static int sctp_autobind(struct sock *sk)
{
	union sctp_addr autoaddr;
	struct sctp_af *af;
	__be16 port;

	/* Initialize a local sockaddr structure to INADDR_ANY. */
	af = sctp_sk(sk)->pf->af;

	port = htons(inet_sk(sk)->num);
	af->inaddr_any(&autoaddr, port);

	return sctp_do_bind(sk, &autoaddr, af->sockaddr_len);
}

/* Parse out IPPROTO_SCTP CMSG headers.  Perform only minimal validation.
 *
 * From RFC 2292
 * 4.2 The cmsghdr Structure *
 *
 * When ancillary data is sent or received, any number of ancillary data
 * objects can be specified by the msg_control and msg_controllen members of
 * the msghdr structure, because each object is preceded by
 * a cmsghdr structure defining the object's length (the cmsg_len member).
 * Historically Berkeley-derived implementations have passed only one object
 * at a time, but this API allows multiple objects to be
 * passed in a single call to sendmsg() or recvmsg(). The following example
 * shows two ancillary data objects in a control buffer.
 *
 *   |<--------------------------- msg_controllen -------------------------->|
 *   |                                                                       |
 *
 *   |<----- ancillary data object ----->|<----- ancillary data object ----->|
 *
 *   |<---------- CMSG_SPACE() --------->|<---------- CMSG_SPACE() --------->|
 *   |                                   |                                   |
 *
 *   |<---------- cmsg_len ---------->|  |<--------- cmsg_len ----------->|  |
 *
 *   |<--------- CMSG_LEN() --------->|  |<-------- CMSG_LEN() ---------->|  |
 *   |                                |  |                                |  |
 *
 *   +-----+-----+-----+--+-----------+--+-----+-----+-----+--+-----------+--+
 *   |cmsg_|cmsg_|cmsg_|XX|           |XX|cmsg_|cmsg_|cmsg_|XX|           |XX|
 *
 *   |len  |level|type |XX|cmsg_data[]|XX|len  |level|type |XX|cmsg_data[]|XX|
 *
 *   +-----+-----+-----+--+-----------+--+-----+-----+-----+--+-----------+--+
 *    ^
 *    |
 *
 * msg_control
 * points here
 */
SCTP_STATIC int sctp_msghdr_parse(const struct msghdr *msg,
				  sctp_cmsgs_t *cmsgs)
{
	struct cmsghdr *cmsg;

	for (cmsg = CMSG_FIRSTHDR(msg);
	     cmsg != NULL;
	     cmsg = CMSG_NXTHDR((struct msghdr*)msg, cmsg)) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;

		/* Should we parse this header or ignore?  */
		if (cmsg->cmsg_level != IPPROTO_SCTP)
			continue;

		/* Strictly check lengths following example in SCM code.  */
		switch (cmsg->cmsg_type) {
		case SCTP_INIT:
			/* SCTP Socket API Extension
			 * 5.2.1 SCTP Initiation Structure (SCTP_INIT)
			 *
			 * This cmsghdr structure provides information for
			 * initializing new SCTP associations with sendmsg().
			 * The SCTP_INITMSG socket option uses this same data
			 * structure.  This structure is not used for
			 * recvmsg().
			 *
			 * cmsg_level    cmsg_type      cmsg_data[]
			 * ------------  ------------   ----------------------
			 * IPPROTO_SCTP  SCTP_INIT      struct sctp_initmsg
			 */
			if (cmsg->cmsg_len !=
			    CMSG_LEN(sizeof(struct sctp_initmsg)))
				return -EINVAL;
			cmsgs->init = (struct sctp_initmsg *)CMSG_DATA(cmsg);
			break;

		case SCTP_SNDRCV:
			/* SCTP Socket API Extension
			 * 5.2.2 SCTP Header Information Structure(SCTP_SNDRCV)
			 *
			 * This cmsghdr structure specifies SCTP options for
			 * sendmsg() and describes SCTP header information
			 * about a received message through recvmsg().
			 *
			 * cmsg_level    cmsg_type      cmsg_data[]
			 * ------------  ------------   ----------------------
			 * IPPROTO_SCTP  SCTP_SNDRCV    struct sctp_sndrcvinfo
			 */
			if (cmsg->cmsg_len !=
			    CMSG_LEN(sizeof(struct sctp_sndrcvinfo)))
				return -EINVAL;

			cmsgs->info =
				(struct sctp_sndrcvinfo *)CMSG_DATA(cmsg);

			/* Minimally, validate the sinfo_flags. */
			if (cmsgs->info->sinfo_flags &
			    ~(SCTP_UNORDERED | SCTP_ADDR_OVER |
			      SCTP_ABORT | SCTP_EOF))
				return -EINVAL;
			break;

		default:
			return -EINVAL;
		}
	}
	return 0;
}

/*
 * Wait for a packet..
 * Note: This function is the same function as in core/datagram.c
 * with a few modifications to make lksctp work.
 */
static int sctp_wait_for_packet(struct sock * sk, int *err, long *timeo_p)
{
	int error;
	DEFINE_WAIT(wait);

	prepare_to_wait_exclusive(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);

	/* Socket errors? */
	error = sock_error(sk);
	if (error)
		goto out;

	if (!skb_queue_empty(&sk->sk_receive_queue))
		goto ready;

	/* Socket shut down?  */
	if (sk->sk_shutdown & RCV_SHUTDOWN)
		goto out;

	/* Sequenced packets can come disconnected.  If so we report the
	 * problem.
	 */
	error = -ENOTCONN;

	/* Is there a good reason to think that we may receive some data?  */
	if (list_empty(&sctp_sk(sk)->ep->asocs) && !sctp_sstate(sk, LISTENING))
		goto out;

	/* Handle signals.  */
	if (signal_pending(current))
		goto interrupted;

	/* Let another process have a go.  Since we are going to sleep
	 * anyway.  Note: This may cause odd behaviors if the message
	 * does not fit in the user's buffer, but this seems to be the
	 * only way to honor MSG_DONTWAIT realistically.
	 */
	sctp_release_sock(sk);
	*timeo_p = schedule_timeout(*timeo_p);
	sctp_lock_sock(sk);

ready:
	finish_wait(sk->sk_sleep, &wait);
	return 0;

interrupted:
	error = sock_intr_errno(*timeo_p);

out:
	finish_wait(sk->sk_sleep, &wait);
	*err = error;
	return error;
}

/* Receive a datagram.
 * Note: This is pretty much the same routine as in core/datagram.c
 * with a few changes to make lksctp work.
 */
static struct sk_buff *sctp_skb_recv_datagram(struct sock *sk, int flags,
					      int noblock, int *err)
{
	int error;
	struct sk_buff *skb;
	long timeo;

	timeo = sock_rcvtimeo(sk, noblock);

	SCTP_DEBUG_PRINTK("Timeout: timeo: %ld, MAX: %ld.\n",
			  timeo, MAX_SCHEDULE_TIMEOUT);

	do {
		/* Again only user level code calls this function,
		 * so nothing interrupt level
		 * will suddenly eat the receive_queue.
		 *
		 *  Look at current nfs client by the way...
		 *  However, this function was corrent in any case. 8)
		 */
		if (flags & MSG_PEEK) {
			spin_lock_bh(&sk->sk_receive_queue.lock);
			skb = skb_peek(&sk->sk_receive_queue);
			if (skb)
				atomic_inc(&skb->users);
			spin_unlock_bh(&sk->sk_receive_queue.lock);
		} else {
			skb = skb_dequeue(&sk->sk_receive_queue);
		}

		if (skb)
			return skb;

		/* Caller is allowed not to check sk->sk_err before calling. */
		error = sock_error(sk);
		if (error)
			goto no_packet;

		if (sk->sk_shutdown & RCV_SHUTDOWN)
			break;

		/* User doesn't want to wait.  */
		error = -EAGAIN;
		if (!timeo)
			goto no_packet;
	} while (sctp_wait_for_packet(sk, err, &timeo) == 0);

	return NULL;

no_packet:
	*err = error;
	return NULL;
}

/* If sndbuf has changed, wake up per association sndbuf waiters.  */
static void __sctp_write_space(struct sctp_association *asoc)
{
	struct sock *sk = asoc->base.sk;
	struct socket *sock = sk->sk_socket;

	if ((sctp_wspace(asoc) > 0) && sock) {
		if (waitqueue_active(&asoc->wait))
			wake_up_interruptible(&asoc->wait);

		if (sctp_writeable(sk)) {
			if (sk->sk_sleep && waitqueue_active(sk->sk_sleep))
				wake_up_interruptible(sk->sk_sleep);

			/* Note that we try to include the Async I/O support
			 * here by modeling from the current TCP/UDP code.
			 * We have not tested with it yet.
			 */
			if (sock->fasync_list &&
			    !(sk->sk_shutdown & SEND_SHUTDOWN))
				sock_wake_async(sock, 2, POLL_OUT);
		}
	}
}

/* Do accounting for the sndbuf space.
 * Decrement the used sndbuf space of the corresponding association by the
 * data size which was just transmitted(freed).
 */
static void sctp_wfree(struct sk_buff *skb)
{
	struct sctp_association *asoc;
	struct sctp_chunk *chunk;
	struct sock *sk;

	/* Get the saved chunk pointer.  */
	chunk = *((struct sctp_chunk **)(skb->cb));
	asoc = chunk->asoc;
	sk = asoc->base.sk;
	asoc->sndbuf_used -= SCTP_DATA_SNDSIZE(chunk) +
				sizeof(struct sk_buff) +
				sizeof(struct sctp_chunk);

	atomic_sub(sizeof(struct sctp_chunk), &sk->sk_wmem_alloc);

	sock_wfree(skb);
	__sctp_write_space(asoc);

	sctp_association_put(asoc);
}

/* Do accounting for the receive space on the socket.
 * Accounting for the association is done in ulpevent.c
 * We set this as a destructor for the cloned data skbs so that
 * accounting is done at the correct time.
 */
void sctp_sock_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
	struct sctp_ulpevent *event = sctp_skb2event(skb);

	atomic_sub(event->rmem_len, &sk->sk_rmem_alloc);
}


/* Helper function to wait for space in the sndbuf.  */
static int sctp_wait_for_sndbuf(struct sctp_association *asoc, long *timeo_p,
				size_t msg_len)
{
	struct sock *sk = asoc->base.sk;
	int err = 0;
	long current_timeo = *timeo_p;
	DEFINE_WAIT(wait);

	SCTP_DEBUG_PRINTK("wait_for_sndbuf: asoc=%p, timeo=%ld, msg_len=%zu\n",
			  asoc, (long)(*timeo_p), msg_len);

	/* Increment the association's refcnt.  */
	sctp_association_hold(asoc);

	/* Wait on the association specific sndbuf space. */
	for (;;) {
		prepare_to_wait_exclusive(&asoc->wait, &wait,
					  TASK_INTERRUPTIBLE);
		if (!*timeo_p)
			goto do_nonblock;
		if (sk->sk_err || asoc->state >= SCTP_STATE_SHUTDOWN_PENDING ||
		    asoc->base.dead)
			goto do_error;
		if (signal_pending(current))
			goto do_interrupted;
		if (msg_len <= sctp_wspace(asoc))
			break;

		/* Let another process have a go.  Since we are going
		 * to sleep anyway.
		 */
		sctp_release_sock(sk);
		current_timeo = schedule_timeout(current_timeo);
		BUG_ON(sk != asoc->base.sk);
		sctp_lock_sock(sk);

		*timeo_p = current_timeo;
	}

out:
	finish_wait(&asoc->wait, &wait);

	/* Release the association's refcnt.  */
	sctp_association_put(asoc);

	return err;

do_error:
	err = -EPIPE;
	goto out;

do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto out;

do_nonblock:
	err = -EAGAIN;
	goto out;
}

/* If socket sndbuf has changed, wake up all per association waiters.  */
void sctp_write_space(struct sock *sk)
{
	struct sctp_association *asoc;
	struct list_head *pos;

	/* Wake up the tasks in each wait queue.  */
	list_for_each(pos, &((sctp_sk(sk))->ep->asocs)) {
		asoc = list_entry(pos, struct sctp_association, asocs);
		__sctp_write_space(asoc);
	}
}

/* Is there any sndbuf space available on the socket?
 *
 * Note that sk_wmem_alloc is the sum of the send buffers on all of the
 * associations on the same socket.  For a UDP-style socket with
 * multiple associations, it is possible for it to be "unwriteable"
 * prematurely.  I assume that this is acceptable because
 * a premature "unwriteable" is better than an accidental "writeable" which
 * would cause an unwanted block under certain circumstances.  For the 1-1
 * UDP-style sockets or TCP-style sockets, this code should work.
 *  - Daisy
 */
static int sctp_writeable(struct sock *sk)
{
	int amt = 0;

	amt = sk->sk_sndbuf - atomic_read(&sk->sk_wmem_alloc);
	if (amt < 0)
		amt = 0;
	return amt;
}

/* Wait for an association to go into ESTABLISHED state. If timeout is 0,
 * returns immediately with EINPROGRESS.
 */
static int sctp_wait_for_connect(struct sctp_association *asoc, long *timeo_p)
{
	struct sock *sk = asoc->base.sk;
	int err = 0;
	long current_timeo = *timeo_p;
	DEFINE_WAIT(wait);

	SCTP_DEBUG_PRINTK("%s: asoc=%p, timeo=%ld\n", __FUNCTION__, asoc,
			  (long)(*timeo_p));

	/* Increment the association's refcnt.  */
	sctp_association_hold(asoc);

	for (;;) {
		prepare_to_wait_exclusive(&asoc->wait, &wait,
					  TASK_INTERRUPTIBLE);
		if (!*timeo_p)
			goto do_nonblock;
		if (sk->sk_shutdown & RCV_SHUTDOWN)
			break;
		if (sk->sk_err || asoc->state >= SCTP_STATE_SHUTDOWN_PENDING ||
		    asoc->base.dead)
			goto do_error;
		if (signal_pending(current))
			goto do_interrupted;

		if (sctp_state(asoc, ESTABLISHED))
			break;

		/* Let another process have a go.  Since we are going
		 * to sleep anyway.
		 */
		sctp_release_sock(sk);
		current_timeo = schedule_timeout(current_timeo);
		sctp_lock_sock(sk);

		*timeo_p = current_timeo;
	}

out:
	finish_wait(&asoc->wait, &wait);

	/* Release the association's refcnt.  */
	sctp_association_put(asoc);

	return err;

do_error:
	if (asoc->init_err_counter + 1 > asoc->max_init_attempts)
		err = -ETIMEDOUT;
	else
		err = -ECONNREFUSED;
	goto out;

do_interrupted:
	err = sock_intr_errno(*timeo_p);
	goto out;

do_nonblock:
	err = -EINPROGRESS;
	goto out;
}

static int sctp_wait_for_accept(struct sock *sk, long timeo)
{
	struct sctp_endpoint *ep;
	int err = 0;
	DEFINE_WAIT(wait);

	ep = sctp_sk(sk)->ep;


	for (;;) {
		prepare_to_wait_exclusive(sk->sk_sleep, &wait,
					  TASK_INTERRUPTIBLE);

		if (list_empty(&ep->asocs)) {
			sctp_release_sock(sk);
			timeo = schedule_timeout(timeo);
			sctp_lock_sock(sk);
		}

		err = -EINVAL;
		if (!sctp_sstate(sk, LISTENING))
			break;

		err = 0;
		if (!list_empty(&ep->asocs))
			break;

		err = sock_intr_errno(timeo);
		if (signal_pending(current))
			break;

		err = -EAGAIN;
		if (!timeo)
			break;
	}

	finish_wait(sk->sk_sleep, &wait);

	return err;
}

void sctp_wait_for_close(struct sock *sk, long timeout)
{
	DEFINE_WAIT(wait);

	do {
		prepare_to_wait(sk->sk_sleep, &wait, TASK_INTERRUPTIBLE);
		if (list_empty(&sctp_sk(sk)->ep->asocs))
			break;
		sctp_release_sock(sk);
		timeout = schedule_timeout(timeout);
		sctp_lock_sock(sk);
	} while (!signal_pending(current) && timeout);

	finish_wait(sk->sk_sleep, &wait);
}

static void sctp_sock_rfree_frag(struct sk_buff *skb)
{
	struct sk_buff *frag;

	if (!skb->data_len)
		goto done;

	/* Don't forget the fragments. */
	for (frag = skb_shinfo(skb)->frag_list; frag; frag = frag->next)
		sctp_sock_rfree_frag(frag);

done:
	sctp_sock_rfree(skb);
}

static void sctp_skb_set_owner_r_frag(struct sk_buff *skb, struct sock *sk)
{
	struct sk_buff *frag;

	if (!skb->data_len)
		goto done;

	/* Don't forget the fragments. */
	for (frag = skb_shinfo(skb)->frag_list; frag; frag = frag->next)
		sctp_skb_set_owner_r_frag(frag, sk);

done:
	sctp_skb_set_owner_r(skb, sk);
}

/* Populate the fields of the newsk from the oldsk and migrate the assoc
 * and its messages to the newsk.
 */
static void sctp_sock_migrate(struct sock *oldsk, struct sock *newsk,
			      struct sctp_association *assoc,
			      sctp_socket_type_t type)
{
	struct sctp_sock *oldsp = sctp_sk(oldsk);
	struct sctp_sock *newsp = sctp_sk(newsk);
	struct sctp_bind_bucket *pp; /* hash list port iterator */
	struct sctp_endpoint *newep = newsp->ep;
	struct sk_buff *skb, *tmp;
	struct sctp_ulpevent *event;
	int flags = 0;

	/* Migrate socket buffer sizes and all the socket level options to the
	 * new socket.
	 */
	newsk->sk_sndbuf = oldsk->sk_sndbuf;
	newsk->sk_rcvbuf = oldsk->sk_rcvbuf;
	/* Brute force copy old sctp opt. */
	inet_sk_copy_descendant(newsk, oldsk);

	/* Restore the ep value that was overwritten with the above structure
	 * copy.
	 */
	newsp->ep = newep;
	newsp->hmac = NULL;

	/* Hook this new socket in to the bind_hash list. */
	pp = sctp_sk(oldsk)->bind_hash;
	sk_add_bind_node(newsk, &pp->owner);
	sctp_sk(newsk)->bind_hash = pp;
	inet_sk(newsk)->num = inet_sk(oldsk)->num;

	/* Copy the bind_addr list from the original endpoint to the new
	 * endpoint so that we can handle restarts properly
	 */
	if (PF_INET6 == assoc->base.sk->sk_family)
		flags = SCTP_ADDR6_ALLOWED;
	if (assoc->peer.ipv4_address)
		flags |= SCTP_ADDR4_PEERSUPP;
	if (assoc->peer.ipv6_address)
		flags |= SCTP_ADDR6_PEERSUPP;
	sctp_bind_addr_copy(&newsp->ep->base.bind_addr,
			     &oldsp->ep->base.bind_addr,
			     SCTP_SCOPE_GLOBAL, GFP_KERNEL, flags);

	/* Move any messages in the old socket's receive queue that are for the
	 * peeled off association to the new socket's receive queue.
	 */
	sctp_skb_for_each(skb, &oldsk->sk_receive_queue, tmp) {
		event = sctp_skb2event(skb);
		if (event->asoc == assoc) {
			sctp_sock_rfree_frag(skb);
			__skb_unlink(skb, &oldsk->sk_receive_queue);
			__skb_queue_tail(&newsk->sk_receive_queue, skb);
			sctp_skb_set_owner_r_frag(skb, newsk);
		}
	}

	/* Clean up any messages pending delivery due to partial
	 * delivery.   Three cases:
	 * 1) No partial deliver;  no work.
	 * 2) Peeling off partial delivery; keep pd_lobby in new pd_lobby.
	 * 3) Peeling off non-partial delivery; move pd_lobby to receive_queue.
	 */
	skb_queue_head_init(&newsp->pd_lobby);
	atomic_set(&sctp_sk(newsk)->pd_mode, assoc->ulpq.pd_mode);

	if (atomic_read(&sctp_sk(oldsk)->pd_mode)) {
		struct sk_buff_head *queue;

		/* Decide which queue to move pd_lobby skbs to. */
		if (assoc->ulpq.pd_mode) {
			queue = &newsp->pd_lobby;
		} else
			queue = &newsk->sk_receive_queue;

		/* Walk through the pd_lobby, looking for skbs that
		 * need moved to the new socket.
		 */
		sctp_skb_for_each(skb, &oldsp->pd_lobby, tmp) {
			event = sctp_skb2event(skb);
			if (event->asoc == assoc) {
				sctp_sock_rfree_frag(skb);
				__skb_unlink(skb, &oldsp->pd_lobby);
				__skb_queue_tail(queue, skb);
				sctp_skb_set_owner_r_frag(skb, newsk);
			}
		}

		/* Clear up any skbs waiting for the partial
		 * delivery to finish.
		 */
		if (assoc->ulpq.pd_mode)
			sctp_clear_pd(oldsk, NULL);

	}

	sctp_skb_for_each(skb, &assoc->ulpq.reasm, tmp) {
		sctp_sock_rfree_frag(skb);
		sctp_skb_set_owner_r_frag(skb, newsk);
	}

	sctp_skb_for_each(skb, &assoc->ulpq.lobby, tmp) {
		sctp_sock_rfree_frag(skb);
		sctp_skb_set_owner_r_frag(skb, newsk);
	}

	/* Set the type of socket to indicate that it is peeled off from the
	 * original UDP-style socket or created with the accept() call on a
	 * TCP-style socket..
	 */
	newsp->type = type;

	/* Mark the new socket "in-use" by the user so that any packets
	 * that may arrive on the association after we've moved it are
	 * queued to the backlog.  This prevents a potential race between
	 * backlog processing on the old socket and new-packet processing
	 * on the new socket.
	 */
	sctp_lock_sock(newsk);
	sctp_assoc_migrate(assoc, newsk);

	/* If the association on the newsk is already closed before accept()
	 * is called, set RCV_SHUTDOWN flag.
	 */
	if (sctp_state(assoc, CLOSED) && sctp_style(newsk, TCP))
		newsk->sk_shutdown |= RCV_SHUTDOWN;

	newsk->sk_state = SCTP_SS_ESTABLISHED;
	sctp_release_sock(newsk);
}

/* This proto struct describes the ULP interface for SCTP.  */
struct proto sctp_prot = {
	.name        =	"SCTP",
	.owner       =	THIS_MODULE,
	.close       =	sctp_close,
	.connect     =	sctp_connect,
	.disconnect  =	sctp_disconnect,
	.accept      =	sctp_accept,
	.ioctl       =	sctp_ioctl,
	.init        =	sctp_init_sock,
	.destroy     =	sctp_destroy_sock,
	.shutdown    =	sctp_shutdown,
	.setsockopt  =	sctp_setsockopt,
	.getsockopt  =	sctp_getsockopt,
	.sendmsg     =	sctp_sendmsg,
	.recvmsg     =	sctp_recvmsg,
	.bind        =	sctp_bind,
	.backlog_rcv =	sctp_backlog_rcv,
	.hash        =	sctp_hash,
	.unhash      =	sctp_unhash,
	.get_port    =	sctp_get_port,
	.obj_size    =  sizeof(struct sctp_sock),
};

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
struct proto sctpv6_prot = {
	.name		= "SCTPv6",
	.owner		= THIS_MODULE,
	.close		= sctp_close,
	.connect	= sctp_connect,
	.disconnect	= sctp_disconnect,
	.accept		= sctp_accept,
	.ioctl		= sctp_ioctl,
	.init		= sctp_init_sock,
	.destroy	= sctp_destroy_sock,
	.shutdown	= sctp_shutdown,
	.setsockopt	= sctp_setsockopt,
	.getsockopt	= sctp_getsockopt,
	.sendmsg	= sctp_sendmsg,
	.recvmsg	= sctp_recvmsg,
	.bind		= sctp_bind,
	.backlog_rcv	= sctp_backlog_rcv,
	.hash		= sctp_hash,
	.unhash		= sctp_unhash,
	.get_port	= sctp_get_port,
	.obj_size	= sizeof(struct sctp6_sock),
};
#endif /* defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE) */
