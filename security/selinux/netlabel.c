/*
 * SELinux NetLabel Support
 *
 * This file provides the necessary glue to tie NetLabel into the SELinux
 * subsystem.
 *
 * Author: Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007
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
#include <net/sock.h>
#include <net/netlabel.h>

#include "objsec.h"
#include "security.h"

/**
 * selinux_netlbl_sock_setsid - Label a socket using the NetLabel mechanism
 * @sk: the socket to label
 * @sid: the SID to use
 *
 * Description:
 * Attempt to label a socket using the NetLabel mechanism using the given
 * SID.  Returns zero values on success, negative values on failure.  The
 * caller is responsibile for calling rcu_read_lock() before calling this
 * this function and rcu_read_unlock() after this function returns.
 *
 */
static int selinux_netlbl_sock_setsid(struct sock *sk, u32 sid)
{
	int rc;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr secattr;

	rc = security_netlbl_sid_to_secattr(sid, &secattr);
	if (rc != 0)
		return rc;

	rc = netlbl_sock_setattr(sk, &secattr);
	if (rc == 0) {
		spin_lock_bh(&sksec->nlbl_lock);
		sksec->nlbl_state = NLBL_LABELED;
		spin_unlock_bh(&sksec->nlbl_lock);
	}

	return rc;
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
 * selinux_netlbl_sk_security_reset - Reset the NetLabel fields
 * @ssec: the sk_security_struct
 * @family: the socket family
 *
 * Description:
 * Called when the NetLabel state of a sk_security_struct needs to be reset.
 * The caller is responsibile for all the NetLabel sk_security_struct locking.
 *
 */
void selinux_netlbl_sk_security_reset(struct sk_security_struct *ssec,
				      int family)
{
        if (family == PF_INET)
		ssec->nlbl_state = NLBL_REQUIRE;
	else
		ssec->nlbl_state = NLBL_UNSET;
}

/**
 * selinux_netlbl_sk_security_init - Setup the NetLabel fields
 * @ssec: the sk_security_struct
 * @family: the socket family
 *
 * Description:
 * Called when a new sk_security_struct is allocated to initialize the NetLabel
 * fields.
 *
 */
void selinux_netlbl_sk_security_init(struct sk_security_struct *ssec,
				     int family)
{
	/* No locking needed, we are the only one who has access to ssec */
	selinux_netlbl_sk_security_reset(ssec, family);
	spin_lock_init(&ssec->nlbl_lock);
}

/**
 * selinux_netlbl_sk_security_clone - Copy the NetLabel fields
 * @ssec: the original sk_security_struct
 * @newssec: the cloned sk_security_struct
 *
 * Description:
 * Clone the NetLabel specific sk_security_struct fields from @ssec to
 * @newssec.
 *
 */
void selinux_netlbl_sk_security_clone(struct sk_security_struct *ssec,
				      struct sk_security_struct *newssec)
{
	/* We don't need to take newssec->nlbl_lock because we are the only
	 * thread with access to newssec, but we do need to take the RCU read
	 * lock as other threads could have access to ssec */
	rcu_read_lock();
	selinux_netlbl_sk_security_reset(newssec, ssec->sk->sk_family);
	newssec->sclass = ssec->sclass;
	rcu_read_unlock();
}

/**
 * selinux_netlbl_skbuff_getsid - Get the sid of a packet using NetLabel
 * @skb: the packet
 * @base_sid: the SELinux SID to use as a context for MLS only attributes
 * @sid: the SID
 *
 * Description:
 * Call the NetLabel mechanism to get the security attributes of the given
 * packet and use those attributes to determine the correct context/SID to
 * assign to the packet.  Returns zero on success, negative values on failure.
 *
 */
int selinux_netlbl_skbuff_getsid(struct sk_buff *skb, u32 base_sid, u32 *sid)
{
	int rc;
	struct netlbl_lsm_secattr secattr;

	netlbl_secattr_init(&secattr);
	rc = netlbl_skbuff_getattr(skb, &secattr);
	if (rc == 0 && secattr.flags != NETLBL_SECATTR_NONE)
		rc = security_netlbl_secattr_to_sid(&secattr, base_sid, sid);
	else
		*sid = SECSID_NULL;
	netlbl_secattr_destroy(&secattr);

	return rc;
}

/**
 * selinux_netlbl_sock_graft - Netlabel the new socket
 * @sk: the new connection
 * @sock: the new socket
 *
 * Description:
 * The connection represented by @sk is being grafted onto @sock so set the
 * socket's NetLabel to match the SID of @sk.
 *
 */
void selinux_netlbl_sock_graft(struct sock *sk, struct socket *sock)
{
	struct inode_security_struct *isec = SOCK_INODE(sock)->i_security;
	struct sk_security_struct *sksec = sk->sk_security;
	struct netlbl_lsm_secattr secattr;
	u32 nlbl_peer_sid;

	sksec->sclass = isec->sclass;

	rcu_read_lock();

	if (sksec->nlbl_state != NLBL_REQUIRE) {
		rcu_read_unlock();
		return;
	}

	netlbl_secattr_init(&secattr);
	if (netlbl_sock_getattr(sk, &secattr) == 0 &&
	    secattr.flags != NETLBL_SECATTR_NONE &&
	    security_netlbl_secattr_to_sid(&secattr,
					   SECINITSID_NETMSG,
					   &nlbl_peer_sid) == 0)
		sksec->peer_sid = nlbl_peer_sid;
	netlbl_secattr_destroy(&secattr);

	/* Try to set the NetLabel on the socket to save time later, if we fail
	 * here we will pick up the pieces in later calls to
	 * selinux_netlbl_inode_permission(). */
	selinux_netlbl_sock_setsid(sk, sksec->sid);

	rcu_read_unlock();
}

/**
 * selinux_netlbl_socket_post_create - Label a socket using NetLabel
 * @sock: the socket to label
 *
 * Description:
 * Attempt to label a socket using the NetLabel mechanism using the given
 * SID.  Returns zero values on success, negative values on failure.
 *
 */
int selinux_netlbl_socket_post_create(struct socket *sock)
{
	int rc = 0;
	struct sock *sk = sock->sk;
	struct inode_security_struct *isec = SOCK_INODE(sock)->i_security;
	struct sk_security_struct *sksec = sk->sk_security;

	sksec->sclass = isec->sclass;

	rcu_read_lock();
	if (sksec->nlbl_state == NLBL_REQUIRE)
		rc = selinux_netlbl_sock_setsid(sk, sksec->sid);
	rcu_read_unlock();

	return rc;
}

/**
 * selinux_netlbl_inode_permission - Verify the socket is NetLabel labeled
 * @inode: the file descriptor's inode
 * @mask: the permission mask
 *
 * Description:
 * Looks at a file's inode and if it is marked as a socket protected by
 * NetLabel then verify that the socket has been labeled, if not try to label
 * the socket now with the inode's SID.  Returns zero on success, negative
 * values on failure.
 *
 */
int selinux_netlbl_inode_permission(struct inode *inode, int mask)
{
	int rc;
	struct sock *sk;
	struct socket *sock;
	struct sk_security_struct *sksec;

	if (!S_ISSOCK(inode->i_mode) ||
	    ((mask & (MAY_WRITE | MAY_APPEND)) == 0))
		return 0;
	sock = SOCKET_I(inode);
	sk = sock->sk;
	sksec = sk->sk_security;

	rcu_read_lock();
	if (sksec->nlbl_state != NLBL_REQUIRE) {
		rcu_read_unlock();
		return 0;
	}
	local_bh_disable();
	bh_lock_sock_nested(sk);
	rc = selinux_netlbl_sock_setsid(sk, sksec->sid);
	bh_unlock_sock(sk);
	local_bh_enable();
	rcu_read_unlock();

	return rc;
}

/**
 * selinux_netlbl_sock_rcv_skb - Do an inbound access check using NetLabel
 * @sksec: the sock's sk_security_struct
 * @skb: the packet
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
				struct avc_audit_data *ad)
{
	int rc;
	u32 nlbl_sid;
	u32 perm;

	rc = selinux_netlbl_skbuff_getsid(skb, SECINITSID_NETMSG, &nlbl_sid);
	if (rc != 0)
		return rc;
	if (nlbl_sid == SECSID_NULL)
		nlbl_sid = SECINITSID_UNLABELED;

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
		netlbl_skbuff_err(skb, rc);
	return rc;
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

	rcu_read_lock();
	if (level == IPPROTO_IP && optname == IP_OPTIONS &&
	    sksec->nlbl_state == NLBL_LABELED) {
		netlbl_secattr_init(&secattr);
		lock_sock(sk);
		rc = netlbl_sock_getattr(sk, &secattr);
		release_sock(sk);
		if (rc == 0 && secattr.flags != NETLBL_SECATTR_NONE)
			rc = -EACCES;
		netlbl_secattr_destroy(&secattr);
	}
	rcu_read_unlock();

	return rc;
}
