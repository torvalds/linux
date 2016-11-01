/*
 * SELinux NetLabel Support
 *
 * This file provides the necessary glue to tie NetLabel into the SELinux
 * subsystem.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007, 2008
 *
 * This program is free software;  you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY;  without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program;  if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/gfp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/sock.h>
#include <net/netlabel.h>
#include <net/ip.h>
#include <net/ipv6.h>

#include "objsec.h"
#include "security.h"
#include "netlabel.h"

/**
 * selinux_netlbl_sidlookup_cached - Cache a SID lookup
 * @skb: the packet
 * @secattr: the NetLabel security attributes
 * @sid: the SID
 *
 * Description:
 * Query the SELinux security server to lookup the correct SID for the given
 * security attributes.  If the query is successful, cache the result to speed
 * up future lookups.  Returns zero on success, negative values on failure.
 *
 */
static int selinux_netlbl_sidlookup_cached(struct sk_buff *skb,
					   u16 family,
					   struct netlbl_lsm_secattr *secattr,
					   u32 *sid)
{
	int rc;

	rc = security_netlbl_secattr_to_sid(secattr, sid);
	if (rc == 0 &&
	    (secattr->flags & NETLBL_SECATTR_CACHEABLE) &&
	    (secattr->flags & NETLBL_SECATTR_CACHE))
		netlbl_cache_add(skb, family, secattr);

	return rc;
}

/**
 * selinux_netlbl_sock_genattr - Generate the NetLabel socket secattr
 * @sk: the socket
 *
 * Description:
 * Generate the NetLabel security attributes for a socket, making full use of
 * the socket's attribute cache.  Returns a pointer to the security attributes
 * on success, NULL on failure.
 *
 */
static struct netlbl_lsm_secattr *selinux_netlbl_sock_genattr(struct sock *sk)
{
	int rc;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr *secattr;

	if (sksec->nlbl_secattr != NULL)
		return sksec->nlbl_secattr;

	secattr = netlbl_secattr_alloc(GFP_ATOMIC);
	if (secattr == NULL)
		return NULL;
	rc = security_netlbl_sid_to_secattr(sksec->sid, secattr);
	if (rc != 0) {
		netlbl_secattr_free(secattr);
		return NULL;
	}
	sksec->nlbl_secattr = secattr;

	return secattr;
}

/**
 * selinux_netlbl_sock_getattr - Get the cached NetLabel secattr
 * @sk: the socket
 * @sid: the SID
 *
 * Query the socket's cached secattr and if the SID matches the cached value
 * return the cache, otherwise return NULL.
 *
 */
static struct netlbl_lsm_secattr *selinux_netlbl_sock_getattr(
							const struct sock *sk,
							u32 sid)
{
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr *secattr = sksec->nlbl_secattr;

	if (secattr == NULL)
		return NULL;

	if ((secattr->flags & NETLBL_SECATTR_SECID) &&
	    (secattr->attr.secid == sid))
		return secattr;

	return NULL;
}

/**
 * selinux_netlbl_cache_invalidate - Invalidate the NetLabel cache
 *
 * Description:
 * Invalidate the NetLabel security attribute mapping cache.
 *
 */
void selinux_netlbl_cache_invalidate(void)
{
	netlbl_cache_invalidate();
}

/**
 * selinux_netlbl_err - Handle a NetLabel packet error
 * @skb: the packet
 * @error: the error code
 * @gateway: true if host is acting as a gateway, false otherwise
 *
 * Description:
 * When a packet is dropped due to a call to avc_has_perm() pass the error
 * code to the NetLabel subsystem so any protocol specific processing can be
 * done.  This is safe to call even if you are unsure if NetLabel labeling is
 * present on the packet, NetLabel is smart enough to only act when it should.
 *
 */
void selinux_netlbl_err(struct sk_buff *skb, u16 family, int error, int gateway)
{
	netlbl_skbuff_err(skb, family, error, gateway);
}

/**
 * selinux_netlbl_sk_security_free - Free the NetLabel fields
 * @sksec: the sk_security_struct
 *
 * Description:
 * Free all of the memory in the NetLabel fields of a sk_security_struct.
 *
 */
void selinux_netlbl_sk_security_free(struct sk_security_struct *sksec)
{
	if (sksec->nlbl_secattr != NULL)
		netlbl_secattr_free(sksec->nlbl_secattr);
}

/**
 * selinux_netlbl_sk_security_reset - Reset the NetLabel fields
 * @sksec: the sk_security_struct
 * @family: the socket family
 *
 * Description:
 * Called when the NetLabel state of a sk_security_struct needs to be reset.
 * The caller is responsible for all the NetLabel sk_security_struct locking.
 *
 */
void selinux_netlbl_sk_security_reset(struct sk_security_struct *sksec)
{
	sksec->nlbl_state = NLBL_UNSET;
}

/**
 * selinux_netlbl_skbuff_getsid - Get the sid of a packet using NetLabel
 * @skb: the packet
 * @family: protocol family
 * @type: NetLabel labeling protocol type
 * @sid: the SID
 *
 * Description:
 * Call the NetLabel mechanism to get the security attributes of the given
 * packet and use those attributes to determine the correct context/SID to
 * assign to the packet.  Returns zero on success, negative values on failure.
 *
 */
int selinux_netlbl_skbuff_getsid(struct sk_buff *skb,
				 u16 family,
				 u32 *type,
				 u32 *sid)
{
	int rc;
	struct netlbl_lsm_secattr secattr;

	if (!netlbl_enabled()) {
		*sid = SECSID_NULL;
		return 0;
	}

	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, family, &secattr);
	if (rc == 0 && secattr.flags != NETLBL_SECATTR_NONE)
		rc = selinux_netlbl_sidlookup_cached(skb, family,
						     &secattr, sid);
	else
		*sid = SECSID_NULL;
	*type = secattr.type;
	netlbl_secattr_destroy(&secattr);

	return rc;
}

/**
 * selinux_netlbl_skbuff_setsid - Set the NetLabel on a packet given a sid
 * @skb: the packet
 * @family: protocol family
 * @sid: the SID
 *
 * Description
 * Call the NetLabel mechanism to set the label of a packet using @sid.
 * Returns zero on success, negative values on failure.
 *
 */
int selinux_netlbl_skbuff_setsid(struct sk_buff *skb,
				 u16 family,
				 u32 sid)
{
	int rc;
	struct netlbl_lsm_secattr secattr_storage;
	struct netlbl_lsm_secattr *secattr = NULL;
	struct sock *sk;

	/* if this is a locally generated packet check to see if it is already
	 * being labeled by it's parent socket, if it is just exit */
	sk = skb_to_full_sk(skb);
	if (sk != NULL) {
		struct sk_security_struct *sksec = sk->sk_security;
		if (sksec->nlbl_state != NLBL_REQSKB)
			return 0;
		secattr = selinux_netlbl_sock_getattr(sk, sid);
	}
	if (secattr == NULL) {
		secattr = &secattr_storage;
		netlbl_secattr_init(secattr);
		rc = security_netlbl_sid_to_secattr(sid, secattr);
		if (rc != 0)
			goto skbuff_setsid_return;
	}

	rc = netlbl_skbuff_setattr(skb, family, secattr);

skbuff_setsid_return:
	if (secattr == &secattr_storage)
		netlbl_secattr_destroy(secattr);
	return rc;
}

/**
 * selinux_netlbl_inet_conn_request - Label an incoming stream connection
 * @req: incoming connection request socket
 *
 * Description:
 * A new incoming connection request is represented by @req, we need to label
 * the new request_sock here and the stack will ensure the on-the-wire label
 * will get preserved when a full sock is created once the connection handshake
 * is complete.  Returns zero on success, negative values on failure.
 *
 */
int selinux_netlbl_inet_conn_request(struct request_sock *req, u16 family)
{
	int rc;
	struct netlbl_lsm_secattr secattr;

	if (family != PF_INET && family != PF_INET6)
		return 0;

	netlbl_secattr_init(&secattr);
	rc = security_netlbl_sid_to_secattr(req->secid, &secattr);
	if (rc != 0)
		goto inet_conn_request_return;
	rc = netlbl_req_setattr(req, &secattr);
inet_conn_request_return:
	netlbl_secattr_destroy(&secattr);
	return rc;
}

/**
 * selinux_netlbl_inet_csk_clone - Initialize the newly created sock
 * @sk: the new sock
 *
 * Description:
 * A new connection has been established using @sk, we've already labeled the
 * socket via the request_sock struct in selinux_netlbl_inet_conn_request() but
 * we need to set the NetLabel state here since we now have a sock structure.
 *
 */
void selinux_netlbl_inet_csk_clone(struct sock *sk, u16 family)
{
	struct sk_security_struct *sksec = sk->sk_security;

	if (family == PF_INET)
		sksec->nlbl_state = NLBL_LABELED;
	else
		sksec->nlbl_state = NLBL_UNSET;
}

/**
 * selinux_netlbl_socket_post_create - Label a socket using NetLabel
 * @sock: the socket to label
 * @family: protocol family
 *
 * Description:
 * Attempt to label a socket using the NetLabel mechanism using the given
 * SID.  Returns zero values on success, negative values on failure.
 *
 */
int selinux_netlbl_socket_post_create(struct sock *sk, u16 family)
{
	int rc;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr *secattr;

	if (family != PF_INET && family != PF_INET6)
		return 0;

	secattr = selinux_netlbl_sock_genattr(sk);
	if (secattr == NULL)
		return -ENOMEM;
	rc = netlbl_sock_setattr(sk, family, secattr);
	switch (rc) {
	case 0:
		sksec->nlbl_state = NLBL_LABELED;
		break;
	case -EDESTADDRREQ:
		sksec->nlbl_state = NLBL_REQSKB;
		rc = 0;
		break;
	}

	return rc;
}

/**
 * selinux_netlbl_sock_rcv_skb - Do an inbound access check using NetLabel
 * @sksec: the sock's sk_security_struct
 * @skb: the packet
 * @family: protocol family
 * @ad: the audit data
 *
 * Description:
 * Fetch the NetLabel security attributes from @skb and perform an access check
 * against the receiving socket.  Returns zero on success, negative values on
 * error.
 *
 */
int selinux_netlbl_sock_rcv_skb(struct sk_security_struct *sksec,
				struct sk_buff *skb,
				u16 family,
				struct common_audit_data *ad)
{
	int rc;
	u32 nlbl_sid;
	u32 perm;
	struct netlbl_lsm_secattr secattr;

	if (!netlbl_enabled())
		return 0;

	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, family, &secattr);
	if (rc == 0 && secattr.flags != NETLBL_SECATTR_NONE)
		rc = selinux_netlbl_sidlookup_cached(skb, family,
						     &secattr, &nlbl_sid);
	else
		nlbl_sid = SECINITSID_UNLABELED;
	netlbl_secattr_destroy(&secattr);
	if (rc != 0)
		return rc;

	switch (sksec->sclass) {
	case SECCLASS_UDP_SOCKET:
		perm = UDP_SOCKET__RECVFROM;
		break;
	case SECCLASS_TCP_SOCKET:
		perm = TCP_SOCKET__RECVFROM;
		break;
	default:
		perm = RAWIP_SOCKET__RECVFROM;
	}

	rc = avc_has_perm(sksec->sid, nlbl_sid, sksec->sclass, perm, ad);
	if (rc == 0)
		return 0;

	if (nlbl_sid != SECINITSID_UNLABELED)
		netlbl_skbuff_err(skb, family, rc, 0);
	return rc;
}

/**
 * selinux_netlbl_option - Is this a NetLabel option
 * @level: the socket level or protocol
 * @optname: the socket option name
 *
 * Description:
 * Returns true if @level and @optname refer to a NetLabel option.
 * Helper for selinux_netlbl_socket_setsockopt().
 */
static inline int selinux_netlbl_option(int level, int optname)
{
	return (level == IPPROTO_IP && optname == IP_OPTIONS) ||
		(level == IPPROTO_IPV6 && optname == IPV6_HOPOPTS);
}

/**
 * selinux_netlbl_socket_setsockopt - Do not allow users to remove a NetLabel
 * @sock: the socket
 * @level: the socket level or protocol
 * @optname: the socket option name
 *
 * Description:
 * Check the setsockopt() call and if the user is trying to replace the IP
 * options on a socket and a NetLabel is in place for the socket deny the
 * access; otherwise allow the access.  Returns zero when the access is
 * allowed, -EACCES when denied, and other negative values on error.
 *
 */
int selinux_netlbl_socket_setsockopt(struct socket *sock,
				     int level,
				     int optname)
{
	int rc = 0;
	struct sock *sk = sock->sk;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr secattr;

	if (selinux_netlbl_option(level, optname) &&
	    (sksec->nlbl_state == NLBL_LABELED ||
	     sksec->nlbl_state == NLBL_CONNLABELED)) {
		netlbl_secattr_init(&secattr);
		lock_sock(sk);
		/* call the netlabel function directly as we want to see the
		 * on-the-wire label that is assigned via the socket's options
		 * and not the cached netlabel/lsm attributes */
		rc = netlbl_sock_getattr(sk, &secattr);
		release_sock(sk);
		if (rc == 0)
			rc = -EACCES;
		else if (rc == -ENOMSG)
			rc = 0;
		netlbl_secattr_destroy(&secattr);
	}

	return rc;
}

/**
 * selinux_netlbl_socket_connect - Label a client-side socket on connect
 * @sk: the socket to label
 * @addr: the destination address
 *
 * Description:
 * Attempt to label a connected socket with NetLabel using the given address.
 * Returns zero values on success, negative values on failure.
 *
 */
int selinux_netlbl_socket_connect(struct sock *sk, struct sockaddr *addr)
{
	int rc;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr *secattr;

	if (sksec->nlbl_state != NLBL_REQSKB &&
	    sksec->nlbl_state != NLBL_CONNLABELED)
		return 0;

	lock_sock(sk);

	/* connected sockets are allowed to disconnect when the address family
	 * is set to AF_UNSPEC, if that is what is happening we want to reset
	 * the socket */
	if (addr->sa_family == AF_UNSPEC) {
		netlbl_sock_delattr(sk);
		sksec->nlbl_state = NLBL_REQSKB;
		rc = 0;
		goto socket_connect_return;
	}
	secattr = selinux_netlbl_sock_genattr(sk);
	if (secattr == NULL) {
		rc = -ENOMEM;
		goto socket_connect_return;
	}
	rc = netlbl_conn_setattr(sk, addr, secattr);
	if (rc == 0)
		sksec->nlbl_state = NLBL_CONNLABELED;

socket_connect_return:
	release_sock(sk);
	return rc;
}
