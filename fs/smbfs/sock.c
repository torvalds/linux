/*
 *  sock.c
 *
 *  Copyright (C) 1995, 1996 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 *  Please add a note about your changes to smbfs in the ChangeLog file.
 */

#include <linux/fs.h>
#include <linux/time.h>
#include <linux/errno.h>
#include <linux/socket.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/in.h>
#include <linux/net.h>
#include <linux/tcp.h>
#include <linux/mm.h>
#include <linux/netdevice.h>
#include <linux/smp_lock.h>
#include <linux/workqueue.h>
#include <net/scm.h>
#include <net/ip.h>

#include <linux/smb_fs.h>
#include <linux/smb.h>
#include <linux/smbno.h>

#include <asm/uaccess.h>
#include <asm/ioctls.h>

#include "smb_debug.h"
#include "proto.h"
#include "request.h"


static int
_recvfrom(struct socket *socket, unsigned char *ubuf, int size, unsigned flags)
{
	struct kvec iov = {ubuf, size};
	struct msghdr msg = {.msg_flags = flags};
	msg.msg_flags |= MSG_DONTWAIT | MSG_NOSIGNAL;
	return kernel_recvmsg(socket, &msg, &iov, 1, size, msg.msg_flags);
}

/*
 * Return the server this socket belongs to
 */
static struct smb_sb_info *
server_from_socket(struct socket *socket)
{
	return socket->sk->sk_user_data;
}

/*
 * Called when there is data on the socket.
 */
void
smb_data_ready(struct sock *sk, int len)
{
	struct smb_sb_info *server = server_from_socket(sk->sk_socket);
	void (*data_ready)(struct sock *, int) = server->data_ready;

	data_ready(sk, len);
	VERBOSE("(%p, %d)\n", sk, len);
	smbiod_wake_up();
}

int
smb_valid_socket(struct inode * inode)
{
	return (inode && S_ISSOCK(inode->i_mode) && 
		SOCKET_I(inode)->type == SOCK_STREAM);
}

static struct socket *
server_sock(struct smb_sb_info *server)
{
	struct file *file;

	if (server && (file = server->sock_file))
	{
#ifdef SMBFS_PARANOIA
		if (!smb_valid_socket(file->f_dentry->d_inode))
			PARANOIA("bad socket!\n");
#endif
		return SOCKET_I(file->f_dentry->d_inode);
	}
	return NULL;
}

void
smb_close_socket(struct smb_sb_info *server)
{
	struct file * file = server->sock_file;

	if (file) {
		struct socket *sock = server_sock(server);

		VERBOSE("closing socket %p\n", sock);
		sock->sk->sk_data_ready = server->data_ready;
		server->sock_file = NULL;
		fput(file);
	}
}

static int
smb_get_length(struct socket *socket, unsigned char *header)
{
	int result;

	result = _recvfrom(socket, header, 4, MSG_PEEK);
	if (result == -EAGAIN)
		return -ENODATA;
	if (result < 0) {
		PARANOIA("recv error = %d\n", -result);
		return result;
	}
	if (result < 4)
		return -ENODATA;

	switch (header[0]) {
	case 0x00:
	case 0x82:
		break;

	case 0x85:
		DEBUG1("Got SESSION KEEP ALIVE\n");
		_recvfrom(socket, header, 4, 0);	/* read away */
		return -ENODATA;

	default:
		PARANOIA("Invalid NBT packet, code=%x\n", header[0]);
		return -EIO;
	}

	/* The length in the RFC NB header is the raw data length */
	return smb_len(header);
}

int
smb_recv_available(struct smb_sb_info *server)
{
	mm_segment_t oldfs;
	int avail, err;
	struct socket *sock = server_sock(server);

	oldfs = get_fs();
	set_fs(get_ds());
	err = sock->ops->ioctl(sock, SIOCINQ, (unsigned long) &avail);
	set_fs(oldfs);
	return (err >= 0) ? avail : err;
}

/*
 * Adjust the kvec to move on 'n' bytes (from nfs/sunrpc)
 */
static int
smb_move_iov(struct kvec **data, size_t *num, struct kvec *vec, unsigned amount)
{
	struct kvec *iv = *data;
	int i;
	int len;

	/*
	 *	Eat any sent kvecs
	 */
	while (iv->iov_len <= amount) {
		amount -= iv->iov_len;
		iv++;
		(*num)--;
	}

	/*
	 *	And chew down the partial one
	 */
	vec[0].iov_len = iv->iov_len-amount;
	vec[0].iov_base =((unsigned char *)iv->iov_base)+amount;
	iv++;

	len = vec[0].iov_len;

	/*
	 *	And copy any others
	 */
	for (i = 1; i < *num; i++) {
		vec[i] = *iv++;
		len += vec[i].iov_len;
	}

	*data = vec;
	return len;
}

/*
 * smb_receive_header
 * Only called by the smbiod thread.
 */
int
smb_receive_header(struct smb_sb_info *server)
{
	struct socket *sock;
	int result = 0;
	unsigned char peek_buf[4];

	result = -EIO; 
	sock = server_sock(server);
	if (!sock)
		goto out;
	if (sock->sk->sk_state != TCP_ESTABLISHED)
		goto out;

	if (!server->smb_read) {
		result = smb_get_length(sock, peek_buf);
		if (result < 0) {
			if (result == -ENODATA)
				result = 0;
			goto out;
		}
		server->smb_len = result + 4;

		if (server->smb_len < SMB_HEADER_LEN) {
			PARANOIA("short packet: %d\n", result);
			server->rstate = SMB_RECV_DROP;
			result = -EIO;
			goto out;
		}
		if (server->smb_len > SMB_MAX_PACKET_SIZE) {
			PARANOIA("long packet: %d\n", result);
			server->rstate = SMB_RECV_DROP;
			result = -EIO;
			goto out;
		}
	}

	result = _recvfrom(sock, server->header + server->smb_read,
			   SMB_HEADER_LEN - server->smb_read, 0);
	VERBOSE("_recvfrom: %d\n", result);
	if (result < 0) {
		VERBOSE("receive error: %d\n", result);
		goto out;
	}
	server->smb_read += result;

	if (server->smb_read == SMB_HEADER_LEN)
		server->rstate = SMB_RECV_HCOMPLETE;
out:
	return result;
}

static char drop_buffer[PAGE_SIZE];

/*
 * smb_receive_drop - read and throw away the data
 * Only called by the smbiod thread.
 *
 * FIXME: we are in the kernel, could we just tell the socket that we want
 * to drop stuff from the buffer?
 */
int
smb_receive_drop(struct smb_sb_info *server)
{
	struct socket *sock;
	unsigned int flags;
	struct kvec iov;
	struct msghdr msg;
	int rlen = smb_len(server->header) - server->smb_read + 4;
	int result = -EIO;

	if (rlen > PAGE_SIZE)
		rlen = PAGE_SIZE;

	sock = server_sock(server);
	if (!sock)
		goto out;
	if (sock->sk->sk_state != TCP_ESTABLISHED)
		goto out;

	flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	iov.iov_base = drop_buffer;
	iov.iov_len = PAGE_SIZE;
	msg.msg_flags = flags;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;

	result = kernel_recvmsg(sock, &msg, &iov, 1, rlen, flags);

	VERBOSE("read: %d\n", result);
	if (result < 0) {
		VERBOSE("receive error: %d\n", result);
		goto out;
	}
	server->smb_read += result;

	if (server->smb_read >= server->smb_len)
		server->rstate = SMB_RECV_END;

out:
	return result;
}

/*
 * smb_receive
 * Only called by the smbiod thread.
 */
int
smb_receive(struct smb_sb_info *server, struct smb_request *req)
{
	struct socket *sock;
	unsigned int flags;
	struct kvec iov[4];
	struct kvec *p = req->rq_iov;
	size_t num = req->rq_iovlen;
	struct msghdr msg;
	int rlen;
	int result = -EIO;

	sock = server_sock(server);
	if (!sock)
		goto out;
	if (sock->sk->sk_state != TCP_ESTABLISHED)
		goto out;

	flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	msg.msg_flags = flags;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;

	/* Dont repeat bytes and count available bufferspace */
	rlen = smb_move_iov(&p, &num, iov, req->rq_bytes_recvd);
	if (req->rq_rlen < rlen)
		rlen = req->rq_rlen;

	result = kernel_recvmsg(sock, &msg, p, num, rlen, flags);

	VERBOSE("read: %d\n", result);
	if (result < 0) {
		VERBOSE("receive error: %d\n", result);
		goto out;
	}
	req->rq_bytes_recvd += result;
	server->smb_read += result;

out:
	return result;
}

/*
 * Try to send a SMB request. This may return after sending only parts of the
 * request. SMB_REQ_TRANSMITTED will be set if a request was fully sent.
 *
 * Parts of this was taken from xprt_sendmsg from net/sunrpc/xprt.c
 */
int
smb_send_request(struct smb_request *req)
{
	struct smb_sb_info *server = req->rq_server;
	struct socket *sock;
	struct msghdr msg = {.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT};
        int slen = req->rq_slen - req->rq_bytes_sent;
	int result = -EIO;
	struct kvec iov[4];
	struct kvec *p = req->rq_iov;
	size_t num = req->rq_iovlen;

	sock = server_sock(server);
	if (!sock)
		goto out;
	if (sock->sk->sk_state != TCP_ESTABLISHED)
		goto out;

	/* Dont repeat bytes */
	if (req->rq_bytes_sent)
		smb_move_iov(&p, &num, iov, req->rq_bytes_sent);

	result = kernel_sendmsg(sock, &msg, p, num, slen);

	if (result >= 0) {
		req->rq_bytes_sent += result;
		if (req->rq_bytes_sent >= req->rq_slen)
			req->rq_flags |= SMB_REQ_TRANSMITTED;
	}
out:
	return result;
}
