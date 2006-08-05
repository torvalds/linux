/*
 * SELinux interface to the NetLabel subsystem
 *
 * Author : Paul Moore <paul.moore@hp.com>
 *
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2006
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

#ifndef _SELINUX_NETLABEL_H_
#define _SELINUX_NETLABEL_H_

#ifdef CONFIG_NETLABEL
void selinux_netlbl_cache_invalidate(void);
int selinux_netlbl_socket_post_create(struct socket *sock,
				      int sock_family,
				      u32 sid);
void selinux_netlbl_sock_graft(struct sock *sk, struct socket *sock);
u32 selinux_netlbl_inet_conn_request(struct sk_buff *skb, u32 sock_sid);
int selinux_netlbl_sock_rcv_skb(struct sk_security_struct *sksec,
				struct sk_buff *skb,
				struct avc_audit_data *ad);
u32 selinux_netlbl_socket_getpeersec_stream(struct socket *sock);
u32 selinux_netlbl_socket_getpeersec_dgram(struct sk_buff *skb);

int __selinux_netlbl_inode_permission(struct inode *inode, int mask);
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
static inline int selinux_netlbl_inode_permission(struct inode *inode,
						  int mask)
{
	int rc = 0;
	struct inode_security_struct *isec;
	struct sk_security_struct *sksec;

	if (!S_ISSOCK(inode->i_mode))
		return 0;

	isec = inode->i_security;
	sksec = SOCKET_I(inode)->sk->sk_security;
	down(&isec->sem);
	if (unlikely(sksec->nlbl_state == NLBL_REQUIRE &&
		     (mask & (MAY_WRITE | MAY_APPEND))))
		rc = __selinux_netlbl_inode_permission(inode, mask);
	up(&isec->sem);

	return rc;
}
#else
static inline void selinux_netlbl_cache_invalidate(void)
{
	return;
}

static inline int selinux_netlbl_socket_post_create(struct socket *sock,
						    int sock_family,
						    u32 sid)
{
	return 0;
}

static inline void selinux_netlbl_sock_graft(struct sock *sk,
					     struct socket *sock)
{
	return;
}

static inline u32 selinux_netlbl_inet_conn_request(struct sk_buff *skb,
						   u32 sock_sid)
{
	return SECSID_NULL;
}

static inline int selinux_netlbl_sock_rcv_skb(struct sk_security_struct *sksec,
					      struct sk_buff *skb,
					      struct avc_audit_data *ad)
{
	return 0;
}

static inline u32 selinux_netlbl_socket_getpeersec_stream(struct socket *sock)
{
	return SECSID_NULL;
}

static inline u32 selinux_netlbl_socket_getpeersec_dgram(struct sk_buff *skb)
{
	return SECSID_NULL;
}

static inline int selinux_netlbl_inode_permission(struct inode *inode,
						  int mask)
{
	return 0;
}
#endif /* CONFIG_NETLABEL */

#endif
