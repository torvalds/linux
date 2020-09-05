// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NET		An implementation of the SOCKET network access protocol.
 *
 * Version:	@(#)socket.c	1.1.93	18/02/95
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Anonymous	:	NOTSOCK/BADF cleanup. Error fix in
 *					shutdown()
 *		Alan Cox	:	verify_area() fixes
 *		Alan Cox	:	Removed DDI
 *		Jonathan Kamens	:	SOCK_DGRAM reconnect bug
 *		Alan Cox	:	Moved a load of checks to the very
 *					top level.
 *		Alan Cox	:	Move address structures to/from user
 *					mode above the protocol layers.
 *		Rob Janssen	:	Allow 0 length sends.
 *		Alan Cox	:	Asynchronous I/O support (cribbed from the
 *					tty drivers).
 *		Niibe Yutaka	:	Asynchronous I/O for writes (4.4BSD style)
 *		Jeff Uphoff	:	Made max number of sockets command-line
 *					configurable.
 *		Matti Aarnio	:	Made the number of sockets dynamic,
 *					to be allocated when needed, and mr.
 *					Uphoff's max is used as max to be
 *					allowed to allocate.
 *		Linus		:	Argh. removed all the socket allocation
 *					altogether: it's in the inode now.
 *		Alan Cox	:	Made sock_alloc()/sock_release() public
 *					for NetROM and future kernel nfsd type
 *					stuff.
 *		Alan Cox	:	sendmsg/recvmsg basics.
 *		Tom Dyas	:	Export net symbols.
 *		Marcin Dalecki	:	Fixed problems with CONFIG_NET="n".
 *		Alan Cox	:	Added thread locking to sys_* calls
 *					for sockets. May have errors at the
 *					moment.
 *		Kevin Buhr	:	Fixed the dumb errors in the above.
 *		Andi Kleen	:	Some small cleanups, optimizations,
 *					and fixed a copy_from_user() bug.
 *		Tigran Aivazian	:	sys_send(args) calls sys_sendto(args, NULL, 0)
 *		Tigran Aivazian	:	Made listen(2) backlog sanity checks
 *					protocol-independent
 *
 *	This module is effectively the top level interface to the BSD socket
 *	paradigm.
 *
 *	Based upon Swansea University Computer Society NET3.039
 */

#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/thread_info.h>
#include <linux/rcupdate.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mutex.h>
#include <linux/if_bridge.h>
#include <linux/if_frad.h>
#include <linux/if_vlan.h>
#include <linux/ptp_classify.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/pseudo_fs.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kmod.h>
#include <linux/audit.h>
#include <linux/wireless.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/xattr.h>
#include <linux/nospec.h>
#include <linux/indirect_call_wrapper.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>

#include <net/compat.h>
#include <net/wext.h>
#include <net/cls_cgroup.h>

#include <net/sock.h>
#include <linux/netfilter.h>

#include <linux/if_tun.h>
#include <linux/ipv6_route.h>
#include <linux/route.h>
#include <linux/termios.h>
#include <linux/sockios.h>
#include <net/busy_poll.h>
#include <linux/errqueue.h>

#ifdef CONFIG_NET_RX_BUSY_POLL
unsigned int sysctl_net_busy_read __read_mostly;
unsigned int sysctl_net_busy_poll __read_mostly;
#endif

static ssize_t sock_read_iter(struct kiocb *iocb, struct iov_iter *to);
static ssize_t sock_write_iter(struct kiocb *iocb, struct iov_iter *from);
static int sock_mmap(struct file *file, struct vm_area_struct *vma);

static int sock_close(struct inode *inode, struct file *file);
static __poll_t sock_poll(struct file *file,
			      struct poll_table_struct *wait);
static long sock_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
static long compat_sock_ioctl(struct file *file,
			      unsigned int cmd, unsigned long arg);
#endif
static int sock_fasync(int fd, struct file *filp, int on);
static ssize_t sock_sendpage(struct file *file, struct page *page,
			     int offset, size_t size, loff_t *ppos, int more);
static ssize_t sock_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags);

#ifdef CONFIG_PROC_FS
static void sock_show_fdinfo(struct seq_file *m, struct file *f)
{
	struct socket *sock = f->private_data;

	if (sock->ops->show_fdinfo)
		sock->ops->show_fdinfo(m, sock);
}
#else
#define sock_show_fdinfo NULL
#endif

/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static const struct file_operations socket_file_ops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read_iter =	sock_read_iter,
	.write_iter =	sock_write_iter,
	.poll =		sock_poll,
	.unlocked_ioctl = sock_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_sock_ioctl,
#endif
	.mmap =		sock_mmap,
	.release =	sock_close,
	.fasync =	sock_fasync,
	.sendpage =	sock_sendpage,
	.splice_write = generic_splice_sendpage,
	.splice_read =	sock_splice_read,
	.show_fdinfo =	sock_show_fdinfo,
};

/*
 *	The protocol list. Each protocol is registered in here.
 */

static DEFINE_SPINLOCK(net_family_lock);
static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;

/*
 * Support routines.
 * Move socket addresses back and forth across the kernel/user
 * divide and look after the messy bits.
 */

/**
 *	move_addr_to_kernel	-	copy a socket address into kernel space
 *	@uaddr: Address in user space
 *	@kaddr: Address in kernel space
 *	@ulen: Length in user space
 *
 *	The address is copied into kernel space. If the provided address is
 *	too long an error code of -EINVAL is returned. If the copy gives
 *	invalid addresses -EFAULT is returned. On a success 0 is returned.
 */

int move_addr_to_kernel(void __user *uaddr, int ulen, struct sockaddr_storage *kaddr)
{
	if (ulen < 0 || ulen > sizeof(struct sockaddr_storage))
		return -EINVAL;
	if (ulen == 0)
		return 0;
	if (copy_from_user(kaddr, uaddr, ulen))
		return -EFAULT;
	return audit_sockaddr(ulen, kaddr);
}

/**
 *	move_addr_to_user	-	copy an address to user space
 *	@kaddr: kernel space address
 *	@klen: length of address in kernel
 *	@uaddr: user space address
 *	@ulen: pointer to user length field
 *
 *	The value pointed to by ulen on entry is the buffer length available.
 *	This is overwritten with the buffer space used. -EINVAL is returned
 *	if an overlong buffer is specified or a negative buffer size. -EFAULT
 *	is returned if either the buffer or the length field are not
 *	accessible.
 *	After copying the data up to the limit the user specifies, the true
 *	length of the data is written over the length limit the user
 *	specified. Zero is returned for a success.
 */

static int move_addr_to_user(struct sockaddr_storage *kaddr, int klen,
			     void __user *uaddr, int __user *ulen)
{
	int err;
	int len;

	BUG_ON(klen > sizeof(struct sockaddr_storage));
	err = get_user(len, ulen);
	if (err)
		return err;
	if (len > klen)
		len = klen;
	if (len < 0)
		return -EINVAL;
	if (len) {
		if (audit_sockaddr(klen, kaddr))
			return -ENOMEM;
		if (copy_to_user(uaddr, kaddr, len))
			return -EFAULT;
	}
	/*
	 *      "fromlen shall refer to the value before truncation.."
	 *                      1003.1g
	 */
	return __put_user(klen, ulen);
}

static struct kmem_cache *sock_inode_cachep __ro_after_init;

static struct inode *sock_alloc_inode(struct super_block *sb)
{
	struct socket_alloc *ei;

	ei = kmem_cache_alloc(sock_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	init_waitqueue_head(&ei->socket.wq.wait);
	ei->socket.wq.fasync_list = NULL;
	ei->socket.wq.flags = 0;

	ei->socket.state = SS_UNCONNECTED;
	ei->socket.flags = 0;
	ei->socket.ops = NULL;
	ei->socket.sk = NULL;
	ei->socket.file = NULL;

	return &ei->vfs_inode;
}

static void sock_free_inode(struct inode *inode)
{
	struct socket_alloc *ei;

	ei = container_of(inode, struct socket_alloc, vfs_inode);
	kmem_cache_free(sock_inode_cachep, ei);
}

static void init_once(void *foo)
{
	struct socket_alloc *ei = (struct socket_alloc *)foo;

	inode_init_once(&ei->vfs_inode);
}

static void init_inodecache(void)
{
	sock_inode_cachep = kmem_cache_create("sock_inode_cache",
					      sizeof(struct socket_alloc),
					      0,
					      (SLAB_HWCACHE_ALIGN |
					       SLAB_RECLAIM_ACCOUNT |
					       SLAB_MEM_SPREAD | SLAB_ACCOUNT),
					      init_once);
	BUG_ON(sock_inode_cachep == NULL);
}

static const struct super_operations sockfs_ops = {
	.alloc_inode	= sock_alloc_inode,
	.free_inode	= sock_free_inode,
	.statfs		= simple_statfs,
};

/*
 * sockfs_dname() is called from d_path().
 */
static char *sockfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "socket:[%lu]",
				d_inode(dentry)->i_ino);
}

static const struct dentry_operations sockfs_dentry_operations = {
	.d_dname  = sockfs_dname,
};

static int sockfs_xattr_get(const struct xattr_handler *handler,
			    struct dentry *dentry, struct inode *inode,
			    const char *suffix, void *value, size_t size)
{
	if (value) {
		if (dentry->d_name.len + 1 > size)
			return -ERANGE;
		memcpy(value, dentry->d_name.name, dentry->d_name.len + 1);
	}
	return dentry->d_name.len + 1;
}

#define XATTR_SOCKPROTONAME_SUFFIX "sockprotoname"
#define XATTR_NAME_SOCKPROTONAME (XATTR_SYSTEM_PREFIX XATTR_SOCKPROTONAME_SUFFIX)
#define XATTR_NAME_SOCKPROTONAME_LEN (sizeof(XATTR_NAME_SOCKPROTONAME)-1)

static const struct xattr_handler sockfs_xattr_handler = {
	.name = XATTR_NAME_SOCKPROTONAME,
	.get = sockfs_xattr_get,
};

static int sockfs_security_xattr_set(const struct xattr_handler *handler,
				     struct dentry *dentry, struct inode *inode,
				     const char *suffix, const void *value,
				     size_t size, int flags)
{
	/* Handled by LSM. */
	return -EAGAIN;
}

static const struct xattr_handler sockfs_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.set = sockfs_security_xattr_set,
};

static const struct xattr_handler *sockfs_xattr_handlers[] = {
	&sockfs_xattr_handler,
	&sockfs_security_xattr_handler,
	NULL
};

static int sockfs_init_fs_context(struct fs_context *fc)
{
	struct pseudo_fs_context *ctx = init_pseudo(fc, SOCKFS_MAGIC);
	if (!ctx)
		return -ENOMEM;
	ctx->ops = &sockfs_ops;
	ctx->dops = &sockfs_dentry_operations;
	ctx->xattr = sockfs_xattr_handlers;
	return 0;
}

static struct vfsmount *sock_mnt __read_mostly;

static struct file_system_type sock_fs_type = {
	.name =		"sockfs",
	.init_fs_context = sockfs_init_fs_context,
	.kill_sb =	kill_anon_super,
};

/*
 *	Obtains the first available file descriptor and sets it up for use.
 *
 *	These functions create file structures and maps them to fd space
 *	of the current process. On success it returns file descriptor
 *	and file struct implicitly stored in sock->file.
 *	Note that another thread may close file descriptor before we return
 *	from this function. We use the fact that now we do not refer
 *	to socket after mapping. If one day we will need it, this
 *	function will increment ref. count on file by 1.
 *
 *	In any case returned fd MAY BE not valid!
 *	This race condition is unavoidable
 *	with shared fd spaces, we cannot solve it inside kernel,
 *	but we take care of internal coherence yet.
 */

/**
 *	sock_alloc_file - Bind a &socket to a &file
 *	@sock: socket
 *	@flags: file status flags
 *	@dname: protocol name
 *
 *	Returns the &file bound with @sock, implicitly storing it
 *	in sock->file. If dname is %NULL, sets to "".
 *	On failure the return is a ERR pointer (see linux/err.h).
 *	This function uses GFP_KERNEL internally.
 */

struct file *sock_alloc_file(struct socket *sock, int flags, const char *dname)
{
	struct file *file;

	if (!dname)
		dname = sock->sk ? sock->sk->sk_prot_creator->name : "";

	file = alloc_file_pseudo(SOCK_INODE(sock), sock_mnt, dname,
				O_RDWR | (flags & O_NONBLOCK),
				&socket_file_ops);
	if (IS_ERR(file)) {
		sock_release(sock);
		return file;
	}

	sock->file = file;
	file->private_data = sock;
	stream_open(SOCK_INODE(sock), file);
	return file;
}
EXPORT_SYMBOL(sock_alloc_file);

static int sock_map_fd(struct socket *sock, int flags)
{
	struct file *newfile;
	int fd = get_unused_fd_flags(flags);
	if (unlikely(fd < 0)) {
		sock_release(sock);
		return fd;
	}

	newfile = sock_alloc_file(sock, flags, NULL);
	if (!IS_ERR(newfile)) {
		fd_install(fd, newfile);
		return fd;
	}

	put_unused_fd(fd);
	return PTR_ERR(newfile);
}

/**
 *	sock_from_file - Return the &socket bounded to @file.
 *	@file: file
 *	@err: pointer to an error code return
 *
 *	On failure returns %NULL and assigns -ENOTSOCK to @err.
 */

struct socket *sock_from_file(struct file *file, int *err)
{
	if (file->f_op == &socket_file_ops)
		return file->private_data;	/* set in sock_map_fd */

	*err = -ENOTSOCK;
	return NULL;
}
EXPORT_SYMBOL(sock_from_file);

/**
 *	sockfd_lookup - Go from a file number to its socket slot
 *	@fd: file handle
 *	@err: pointer to an error code return
 *
 *	The file handle passed in is locked and the socket it is bound
 *	to is returned. If an error occurs the err pointer is overwritten
 *	with a negative errno code and NULL is returned. The function checks
 *	for both invalid handles and passing a handle which is not a socket.
 *
 *	On a success the socket object pointer is returned.
 */

struct socket *sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct socket *sock;

	file = fget(fd);
	if (!file) {
		*err = -EBADF;
		return NULL;
	}

	sock = sock_from_file(file, err);
	if (!sock)
		fput(file);
	return sock;
}
EXPORT_SYMBOL(sockfd_lookup);

static struct socket *sockfd_lookup_light(int fd, int *err, int *fput_needed)
{
	struct fd f = fdget(fd);
	struct socket *sock;

	*err = -EBADF;
	if (f.file) {
		sock = sock_from_file(f.file, err);
		if (likely(sock)) {
			*fput_needed = f.flags & FDPUT_FPUT;
			return sock;
		}
		fdput(f);
	}
	return NULL;
}

static ssize_t sockfs_listxattr(struct dentry *dentry, char *buffer,
				size_t size)
{
	ssize_t len;
	ssize_t used = 0;

	len = security_inode_listsecurity(d_inode(dentry), buffer, size);
	if (len < 0)
		return len;
	used += len;
	if (buffer) {
		if (size < used)
			return -ERANGE;
		buffer += len;
	}

	len = (XATTR_NAME_SOCKPROTONAME_LEN + 1);
	used += len;
	if (buffer) {
		if (size < used)
			return -ERANGE;
		memcpy(buffer, XATTR_NAME_SOCKPROTONAME, len);
		buffer += len;
	}

	return used;
}

static int sockfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int err = simple_setattr(dentry, iattr);

	if (!err && (iattr->ia_valid & ATTR_UID)) {
		struct socket *sock = SOCKET_I(d_inode(dentry));

		if (sock->sk)
			sock->sk->sk_uid = iattr->ia_uid;
		else
			err = -ENOENT;
	}

	return err;
}

static const struct inode_operations sockfs_inode_ops = {
	.listxattr = sockfs_listxattr,
	.setattr = sockfs_setattr,
};

/**
 *	sock_alloc - allocate a socket
 *
 *	Allocate a new inode and socket object. The two are bound together
 *	and initialised. The socket is then returned. If we are out of inodes
 *	NULL is returned. This functions uses GFP_KERNEL internally.
 */

struct socket *sock_alloc(void)
{
	struct inode *inode;
	struct socket *sock;

	inode = new_inode_pseudo(sock_mnt->mnt_sb);
	if (!inode)
		return NULL;

	sock = SOCKET_I(inode);

	inode->i_ino = get_next_ino();
	inode->i_mode = S_IFSOCK | S_IRWXUGO;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_op = &sockfs_inode_ops;

	return sock;
}
EXPORT_SYMBOL(sock_alloc);

static void __sock_release(struct socket *sock, struct inode *inode)
{
	if (sock->ops) {
		struct module *owner = sock->ops->owner;

		if (inode)
			inode_lock(inode);
		sock->ops->release(sock);
		sock->sk = NULL;
		if (inode)
			inode_unlock(inode);
		sock->ops = NULL;
		module_put(owner);
	}

	if (sock->wq.fasync_list)
		pr_err("%s: fasync list not empty!\n", __func__);

	if (!sock->file) {
		iput(SOCK_INODE(sock));
		return;
	}
	sock->file = NULL;
}

/**
 *	sock_release - close a socket
 *	@sock: socket to close
 *
 *	The socket is released from the protocol stack if it has a release
 *	callback, and the inode is then released if the socket is bound to
 *	an inode not a file.
 */
void sock_release(struct socket *sock)
{
	__sock_release(sock, NULL);
}
EXPORT_SYMBOL(sock_release);

void __sock_tx_timestamp(__u16 tsflags, __u8 *tx_flags)
{
	u8 flags = *tx_flags;

	if (tsflags & SOF_TIMESTAMPING_TX_HARDWARE)
		flags |= SKBTX_HW_TSTAMP;

	if (tsflags & SOF_TIMESTAMPING_TX_SOFTWARE)
		flags |= SKBTX_SW_TSTAMP;

	if (tsflags & SOF_TIMESTAMPING_TX_SCHED)
		flags |= SKBTX_SCHED_TSTAMP;

	*tx_flags = flags;
}
EXPORT_SYMBOL(__sock_tx_timestamp);

INDIRECT_CALLABLE_DECLARE(int inet_sendmsg(struct socket *, struct msghdr *,
					   size_t));
INDIRECT_CALLABLE_DECLARE(int inet6_sendmsg(struct socket *, struct msghdr *,
					    size_t));
static inline int sock_sendmsg_nosec(struct socket *sock, struct msghdr *msg)
{
	int ret = INDIRECT_CALL_INET(sock->ops->sendmsg, inet6_sendmsg,
				     inet_sendmsg, sock, msg,
				     msg_data_left(msg));
	BUG_ON(ret == -EIOCBQUEUED);
	return ret;
}

/**
 *	sock_sendmsg - send a message through @sock
 *	@sock: socket
 *	@msg: message to send
 *
 *	Sends @msg through @sock, passing through LSM.
 *	Returns the number of bytes sent, or an error code.
 */
int sock_sendmsg(struct socket *sock, struct msghdr *msg)
{
	int err = security_socket_sendmsg(sock, msg,
					  msg_data_left(msg));

	return err ?: sock_sendmsg_nosec(sock, msg);
}
EXPORT_SYMBOL(sock_sendmsg);

/**
 *	kernel_sendmsg - send a message through @sock (kernel-space)
 *	@sock: socket
 *	@msg: message header
 *	@vec: kernel vec
 *	@num: vec array length
 *	@size: total message data size
 *
 *	Builds the message data with @vec and sends it through @sock.
 *	Returns the number of bytes sent, or an error code.
 */

int kernel_sendmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size)
{
	iov_iter_kvec(&msg->msg_iter, WRITE, vec, num, size);
	return sock_sendmsg(sock, msg);
}
EXPORT_SYMBOL(kernel_sendmsg);

/**
 *	kernel_sendmsg_locked - send a message through @sock (kernel-space)
 *	@sk: sock
 *	@msg: message header
 *	@vec: output s/g array
 *	@num: output s/g array length
 *	@size: total message data size
 *
 *	Builds the message data with @vec and sends it through @sock.
 *	Returns the number of bytes sent, or an error code.
 *	Caller must hold @sk.
 */

int kernel_sendmsg_locked(struct sock *sk, struct msghdr *msg,
			  struct kvec *vec, size_t num, size_t size)
{
	struct socket *sock = sk->sk_socket;

	if (!sock->ops->sendmsg_locked)
		return sock_no_sendmsg_locked(sk, msg, size);

	iov_iter_kvec(&msg->msg_iter, WRITE, vec, num, size);

	return sock->ops->sendmsg_locked(sk, msg, msg_data_left(msg));
}
EXPORT_SYMBOL(kernel_sendmsg_locked);

static bool skb_is_err_queue(const struct sk_buff *skb)
{
	/* pkt_type of skbs enqueued on the error queue are set to
	 * PACKET_OUTGOING in skb_set_err_queue(). This is only safe to do
	 * in recvmsg, since skbs received on a local socket will never
	 * have a pkt_type of PACKET_OUTGOING.
	 */
	return skb->pkt_type == PACKET_OUTGOING;
}

/* On transmit, software and hardware timestamps are returned independently.
 * As the two skb clones share the hardware timestamp, which may be updated
 * before the software timestamp is received, a hardware TX timestamp may be
 * returned only if there is no software TX timestamp. Ignore false software
 * timestamps, which may be made in the __sock_recv_timestamp() call when the
 * option SO_TIMESTAMP_OLD(NS) is enabled on the socket, even when the skb has a
 * hardware timestamp.
 */
static bool skb_is_swtx_tstamp(const struct sk_buff *skb, int false_tstamp)
{
	return skb->tstamp && !false_tstamp && skb_is_err_queue(skb);
}

static void put_ts_pktinfo(struct msghdr *msg, struct sk_buff *skb)
{
	struct scm_ts_pktinfo ts_pktinfo;
	struct net_device *orig_dev;

	if (!skb_mac_header_was_set(skb))
		return;

	memset(&ts_pktinfo, 0, sizeof(ts_pktinfo));

	rcu_read_lock();
	orig_dev = dev_get_by_napi_id(skb_napi_id(skb));
	if (orig_dev)
		ts_pktinfo.if_index = orig_dev->ifindex;
	rcu_read_unlock();

	ts_pktinfo.pkt_length = skb->len - skb_mac_offset(skb);
	put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMPING_PKTINFO,
		 sizeof(ts_pktinfo), &ts_pktinfo);
}

/*
 * called from sock_recv_timestamp() if sock_flag(sk, SOCK_RCVTSTAMP)
 */
void __sock_recv_timestamp(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	int need_software_tstamp = sock_flag(sk, SOCK_RCVTSTAMP);
	int new_tstamp = sock_flag(sk, SOCK_TSTAMP_NEW);
	struct scm_timestamping_internal tss;

	int empty = 1, false_tstamp = 0;
	struct skb_shared_hwtstamps *shhwtstamps =
		skb_hwtstamps(skb);

	/* Race occurred between timestamp enabling and packet
	   receiving.  Fill in the current time for now. */
	if (need_software_tstamp && skb->tstamp == 0) {
		__net_timestamp(skb);
		false_tstamp = 1;
	}

	if (need_software_tstamp) {
		if (!sock_flag(sk, SOCK_RCVTSTAMPNS)) {
			if (new_tstamp) {
				struct __kernel_sock_timeval tv;

				skb_get_new_timestamp(skb, &tv);
				put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMP_NEW,
					 sizeof(tv), &tv);
			} else {
				struct __kernel_old_timeval tv;

				skb_get_timestamp(skb, &tv);
				put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMP_OLD,
					 sizeof(tv), &tv);
			}
		} else {
			if (new_tstamp) {
				struct __kernel_timespec ts;

				skb_get_new_timestampns(skb, &ts);
				put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMPNS_NEW,
					 sizeof(ts), &ts);
			} else {
				struct __kernel_old_timespec ts;

				skb_get_timestampns(skb, &ts);
				put_cmsg(msg, SOL_SOCKET, SO_TIMESTAMPNS_OLD,
					 sizeof(ts), &ts);
			}
		}
	}

	memset(&tss, 0, sizeof(tss));
	if ((sk->sk_tsflags & SOF_TIMESTAMPING_SOFTWARE) &&
	    ktime_to_timespec64_cond(skb->tstamp, tss.ts + 0))
		empty = 0;
	if (shhwtstamps &&
	    (sk->sk_tsflags & SOF_TIMESTAMPING_RAW_HARDWARE) &&
	    !skb_is_swtx_tstamp(skb, false_tstamp) &&
	    ktime_to_timespec64_cond(shhwtstamps->hwtstamp, tss.ts + 2)) {
		empty = 0;
		if ((sk->sk_tsflags & SOF_TIMESTAMPING_OPT_PKTINFO) &&
		    !skb_is_err_queue(skb))
			put_ts_pktinfo(msg, skb);
	}
	if (!empty) {
		if (sock_flag(sk, SOCK_TSTAMP_NEW))
			put_cmsg_scm_timestamping64(msg, &tss);
		else
			put_cmsg_scm_timestamping(msg, &tss);

		if (skb_is_err_queue(skb) && skb->len &&
		    SKB_EXT_ERR(skb)->opt_stats)
			put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMPING_OPT_STATS,
				 skb->len, skb->data);
	}
}
EXPORT_SYMBOL_GPL(__sock_recv_timestamp);

void __sock_recv_wifi_status(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	int ack;

	if (!sock_flag(sk, SOCK_WIFI_STATUS))
		return;
	if (!skb->wifi_acked_valid)
		return;

	ack = skb->wifi_acked;

	put_cmsg(msg, SOL_SOCKET, SCM_WIFI_STATUS, sizeof(ack), &ack);
}
EXPORT_SYMBOL_GPL(__sock_recv_wifi_status);

static inline void sock_recv_drops(struct msghdr *msg, struct sock *sk,
				   struct sk_buff *skb)
{
	if (sock_flag(sk, SOCK_RXQ_OVFL) && skb && SOCK_SKB_CB(skb)->dropcount)
		put_cmsg(msg, SOL_SOCKET, SO_RXQ_OVFL,
			sizeof(__u32), &SOCK_SKB_CB(skb)->dropcount);
}

void __sock_recv_ts_and_drops(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	sock_recv_timestamp(msg, sk, skb);
	sock_recv_drops(msg, sk, skb);
}
EXPORT_SYMBOL_GPL(__sock_recv_ts_and_drops);

INDIRECT_CALLABLE_DECLARE(int inet_recvmsg(struct socket *, struct msghdr *,
					   size_t, int));
INDIRECT_CALLABLE_DECLARE(int inet6_recvmsg(struct socket *, struct msghdr *,
					    size_t, int));
static inline int sock_recvmsg_nosec(struct socket *sock, struct msghdr *msg,
				     int flags)
{
	return INDIRECT_CALL_INET(sock->ops->recvmsg, inet6_recvmsg,
				  inet_recvmsg, sock, msg, msg_data_left(msg),
				  flags);
}

/**
 *	sock_recvmsg - receive a message from @sock
 *	@sock: socket
 *	@msg: message to receive
 *	@flags: message flags
 *
 *	Receives @msg from @sock, passing through LSM. Returns the total number
 *	of bytes received, or an error.
 */
int sock_recvmsg(struct socket *sock, struct msghdr *msg, int flags)
{
	int err = security_socket_recvmsg(sock, msg, msg_data_left(msg), flags);

	return err ?: sock_recvmsg_nosec(sock, msg, flags);
}
EXPORT_SYMBOL(sock_recvmsg);

/**
 *	kernel_recvmsg - Receive a message from a socket (kernel space)
 *	@sock: The socket to receive the message from
 *	@msg: Received message
 *	@vec: Input s/g array for message data
 *	@num: Size of input s/g array
 *	@size: Number of bytes to read
 *	@flags: Message flags (MSG_DONTWAIT, etc...)
 *
 *	On return the msg structure contains the scatter/gather array passed in the
 *	vec argument. The array is modified so that it consists of the unfilled
 *	portion of the original array.
 *
 *	The returned value is the total number of bytes received, or an error.
 */

int kernel_recvmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size, int flags)
{
	msg->msg_control_is_user = false;
	iov_iter_kvec(&msg->msg_iter, READ, vec, num, size);
	return sock_recvmsg(sock, msg, flags);
}
EXPORT_SYMBOL(kernel_recvmsg);

static ssize_t sock_sendpage(struct file *file, struct page *page,
			     int offset, size_t size, loff_t *ppos, int more)
{
	struct socket *sock;
	int flags;

	sock = file->private_data;

	flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;
	/* more is a combination of MSG_MORE and MSG_SENDPAGE_NOTLAST */
	flags |= more;

	return kernel_sendpage(sock, page, offset, size, flags);
}

static ssize_t sock_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	struct socket *sock = file->private_data;

	if (unlikely(!sock->ops->splice_read))
		return generic_file_splice_read(file, ppos, pipe, len, flags);

	return sock->ops->splice_read(sock, ppos, pipe, len, flags);
}

static ssize_t sock_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct socket *sock = file->private_data;
	struct msghdr msg = {.msg_iter = *to,
			     .msg_iocb = iocb};
	ssize_t res;

	if (file->f_flags & O_NONBLOCK || (iocb->ki_flags & IOCB_NOWAIT))
		msg.msg_flags = MSG_DONTWAIT;

	if (iocb->ki_pos != 0)
		return -ESPIPE;

	if (!iov_iter_count(to))	/* Match SYS5 behaviour */
		return 0;

	res = sock_recvmsg(sock, &msg, msg.msg_flags);
	*to = msg.msg_iter;
	return res;
}

static ssize_t sock_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct socket *sock = file->private_data;
	struct msghdr msg = {.msg_iter = *from,
			     .msg_iocb = iocb};
	ssize_t res;

	if (iocb->ki_pos != 0)
		return -ESPIPE;

	if (file->f_flags & O_NONBLOCK || (iocb->ki_flags & IOCB_NOWAIT))
		msg.msg_flags = MSG_DONTWAIT;

	if (sock->type == SOCK_SEQPACKET)
		msg.msg_flags |= MSG_EOR;

	res = sock_sendmsg(sock, &msg);
	*from = msg.msg_iter;
	return res;
}

/*
 * Atomic setting of ioctl hooks to avoid race
 * with module unload.
 */

static DEFINE_MUTEX(br_ioctl_mutex);
static int (*br_ioctl_hook) (struct net *, unsigned int cmd, void __user *arg);

void brioctl_set(int (*hook) (struct net *, unsigned int, void __user *))
{
	mutex_lock(&br_ioctl_mutex);
	br_ioctl_hook = hook;
	mutex_unlock(&br_ioctl_mutex);
}
EXPORT_SYMBOL(brioctl_set);

static DEFINE_MUTEX(vlan_ioctl_mutex);
static int (*vlan_ioctl_hook) (struct net *, void __user *arg);

void vlan_ioctl_set(int (*hook) (struct net *, void __user *))
{
	mutex_lock(&vlan_ioctl_mutex);
	vlan_ioctl_hook = hook;
	mutex_unlock(&vlan_ioctl_mutex);
}
EXPORT_SYMBOL(vlan_ioctl_set);

static DEFINE_MUTEX(dlci_ioctl_mutex);
static int (*dlci_ioctl_hook) (unsigned int, void __user *);

void dlci_ioctl_set(int (*hook) (unsigned int, void __user *))
{
	mutex_lock(&dlci_ioctl_mutex);
	dlci_ioctl_hook = hook;
	mutex_unlock(&dlci_ioctl_mutex);
}
EXPORT_SYMBOL(dlci_ioctl_set);

static long sock_do_ioctl(struct net *net, struct socket *sock,
			  unsigned int cmd, unsigned long arg)
{
	int err;
	void __user *argp = (void __user *)arg;

	err = sock->ops->ioctl(sock, cmd, arg);

	/*
	 * If this ioctl is unknown try to hand it down
	 * to the NIC driver.
	 */
	if (err != -ENOIOCTLCMD)
		return err;

	if (cmd == SIOCGIFCONF) {
		struct ifconf ifc;
		if (copy_from_user(&ifc, argp, sizeof(struct ifconf)))
			return -EFAULT;
		rtnl_lock();
		err = dev_ifconf(net, &ifc, sizeof(struct ifreq));
		rtnl_unlock();
		if (!err && copy_to_user(argp, &ifc, sizeof(struct ifconf)))
			err = -EFAULT;
	} else if (is_socket_ioctl_cmd(cmd)) {
		struct ifreq ifr;
		bool need_copyout;
		if (copy_from_user(&ifr, argp, sizeof(struct ifreq)))
			return -EFAULT;
		err = dev_ioctl(net, cmd, &ifr, &need_copyout);
		if (!err && need_copyout)
			if (copy_to_user(argp, &ifr, sizeof(struct ifreq)))
				return -EFAULT;
	} else {
		err = -ENOTTY;
	}
	return err;
}

/*
 *	With an ioctl, arg may well be a user mode pointer, but we don't know
 *	what to do with it - that's up to the protocol still.
 */

static long sock_ioctl(struct file *file, unsigned cmd, unsigned long arg)
{
	struct socket *sock;
	struct sock *sk;
	void __user *argp = (void __user *)arg;
	int pid, err;
	struct net *net;

	sock = file->private_data;
	sk = sock->sk;
	net = sock_net(sk);
	if (unlikely(cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15))) {
		struct ifreq ifr;
		bool need_copyout;
		if (copy_from_user(&ifr, argp, sizeof(struct ifreq)))
			return -EFAULT;
		err = dev_ioctl(net, cmd, &ifr, &need_copyout);
		if (!err && need_copyout)
			if (copy_to_user(argp, &ifr, sizeof(struct ifreq)))
				return -EFAULT;
	} else
#ifdef CONFIG_WEXT_CORE
	if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
		err = wext_handle_ioctl(net, cmd, argp);
	} else
#endif
		switch (cmd) {
		case FIOSETOWN:
		case SIOCSPGRP:
			err = -EFAULT;
			if (get_user(pid, (int __user *)argp))
				break;
			err = f_setown(sock->file, pid, 1);
			break;
		case FIOGETOWN:
		case SIOCGPGRP:
			err = put_user(f_getown(sock->file),
				       (int __user *)argp);
			break;
		case SIOCGIFBR:
		case SIOCSIFBR:
		case SIOCBRADDBR:
		case SIOCBRDELBR:
			err = -ENOPKG;
			if (!br_ioctl_hook)
				request_module("bridge");

			mutex_lock(&br_ioctl_mutex);
			if (br_ioctl_hook)
				err = br_ioctl_hook(net, cmd, argp);
			mutex_unlock(&br_ioctl_mutex);
			break;
		case SIOCGIFVLAN:
		case SIOCSIFVLAN:
			err = -ENOPKG;
			if (!vlan_ioctl_hook)
				request_module("8021q");

			mutex_lock(&vlan_ioctl_mutex);
			if (vlan_ioctl_hook)
				err = vlan_ioctl_hook(net, argp);
			mutex_unlock(&vlan_ioctl_mutex);
			break;
		case SIOCADDDLCI:
		case SIOCDELDLCI:
			err = -ENOPKG;
			if (!dlci_ioctl_hook)
				request_module("dlci");

			mutex_lock(&dlci_ioctl_mutex);
			if (dlci_ioctl_hook)
				err = dlci_ioctl_hook(cmd, argp);
			mutex_unlock(&dlci_ioctl_mutex);
			break;
		case SIOCGSKNS:
			err = -EPERM;
			if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
				break;

			err = open_related_ns(&net->ns, get_net_ns);
			break;
		case SIOCGSTAMP_OLD:
		case SIOCGSTAMPNS_OLD:
			if (!sock->ops->gettstamp) {
				err = -ENOIOCTLCMD;
				break;
			}
			err = sock->ops->gettstamp(sock, argp,
						   cmd == SIOCGSTAMP_OLD,
						   !IS_ENABLED(CONFIG_64BIT));
			break;
		case SIOCGSTAMP_NEW:
		case SIOCGSTAMPNS_NEW:
			if (!sock->ops->gettstamp) {
				err = -ENOIOCTLCMD;
				break;
			}
			err = sock->ops->gettstamp(sock, argp,
						   cmd == SIOCGSTAMP_NEW,
						   false);
			break;
		default:
			err = sock_do_ioctl(net, sock, cmd, arg);
			break;
		}
	return err;
}

/**
 *	sock_create_lite - creates a socket
 *	@family: protocol family (AF_INET, ...)
 *	@type: communication type (SOCK_STREAM, ...)
 *	@protocol: protocol (0, ...)
 *	@res: new socket
 *
 *	Creates a new socket and assigns it to @res, passing through LSM.
 *	The new socket initialization is not complete, see kernel_accept().
 *	Returns 0 or an error. On failure @res is set to %NULL.
 *	This function internally uses GFP_KERNEL.
 */

int sock_create_lite(int family, int type, int protocol, struct socket **res)
{
	int err;
	struct socket *sock = NULL;

	err = security_socket_create(family, type, protocol, 1);
	if (err)
		goto out;

	sock = sock_alloc();
	if (!sock) {
		err = -ENOMEM;
		goto out;
	}

	sock->type = type;
	err = security_socket_post_create(sock, family, type, protocol, 1);
	if (err)
		goto out_release;

out:
	*res = sock;
	return err;
out_release:
	sock_release(sock);
	sock = NULL;
	goto out;
}
EXPORT_SYMBOL(sock_create_lite);

/* No kernel lock held - perfect */
static __poll_t sock_poll(struct file *file, poll_table *wait)
{
	struct socket *sock = file->private_data;
	__poll_t events = poll_requested_events(wait), flag = 0;

	if (!sock->ops->poll)
		return 0;

	if (sk_can_busy_loop(sock->sk)) {
		/* poll once if requested by the syscall */
		if (events & POLL_BUSY_LOOP)
			sk_busy_loop(sock->sk, 1);

		/* if this socket can poll_ll, tell the system call */
		flag = POLL_BUSY_LOOP;
	}

	return sock->ops->poll(file, sock, wait) | flag;
}

static int sock_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct socket *sock = file->private_data;

	return sock->ops->mmap(file, sock, vma);
}

static int sock_close(struct inode *inode, struct file *filp)
{
	__sock_release(SOCKET_I(inode), inode);
	return 0;
}

/*
 *	Update the socket async list
 *
 *	Fasync_list locking strategy.
 *
 *	1. fasync_list is modified only under process context socket lock
 *	   i.e. under semaphore.
 *	2. fasync_list is used under read_lock(&sk->sk_callback_lock)
 *	   or under socket lock
 */

static int sock_fasync(int fd, struct file *filp, int on)
{
	struct socket *sock = filp->private_data;
	struct sock *sk = sock->sk;
	struct socket_wq *wq = &sock->wq;

	if (sk == NULL)
		return -EINVAL;

	lock_sock(sk);
	fasync_helper(fd, filp, on, &wq->fasync_list);

	if (!wq->fasync_list)
		sock_reset_flag(sk, SOCK_FASYNC);
	else
		sock_set_flag(sk, SOCK_FASYNC);

	release_sock(sk);
	return 0;
}

/* This function may be called only under rcu_lock */

int sock_wake_async(struct socket_wq *wq, int how, int band)
{
	if (!wq || !wq->fasync_list)
		return -1;

	switch (how) {
	case SOCK_WAKE_WAITD:
		if (test_bit(SOCKWQ_ASYNC_WAITDATA, &wq->flags))
			break;
		goto call_kill;
	case SOCK_WAKE_SPACE:
		if (!test_and_clear_bit(SOCKWQ_ASYNC_NOSPACE, &wq->flags))
			break;
		fallthrough;
	case SOCK_WAKE_IO:
call_kill:
		kill_fasync(&wq->fasync_list, SIGIO, band);
		break;
	case SOCK_WAKE_URG:
		kill_fasync(&wq->fasync_list, SIGURG, band);
	}

	return 0;
}
EXPORT_SYMBOL(sock_wake_async);

/**
 *	__sock_create - creates a socket
 *	@net: net namespace
 *	@family: protocol family (AF_INET, ...)
 *	@type: communication type (SOCK_STREAM, ...)
 *	@protocol: protocol (0, ...)
 *	@res: new socket
 *	@kern: boolean for kernel space sockets
 *
 *	Creates a new socket and assigns it to @res, passing through LSM.
 *	Returns 0 or an error. On failure @res is set to %NULL. @kern must
 *	be set to true if the socket resides in kernel space.
 *	This function internally uses GFP_KERNEL.
 */

int __sock_create(struct net *net, int family, int type, int protocol,
			 struct socket **res, int kern)
{
	int err;
	struct socket *sock;
	const struct net_proto_family *pf;

	/*
	 *      Check protocol is in range
	 */
	if (family < 0 || family >= NPROTO)
		return -EAFNOSUPPORT;
	if (type < 0 || type >= SOCK_MAX)
		return -EINVAL;

	/* Compatibility.

	   This uglymoron is moved from INET layer to here to avoid
	   deadlock in module load.
	 */
	if (family == PF_INET && type == SOCK_PACKET) {
		pr_info_once("%s uses obsolete (PF_INET,SOCK_PACKET)\n",
			     current->comm);
		family = PF_PACKET;
	}

	err = security_socket_create(family, type, protocol, kern);
	if (err)
		return err;

	/*
	 *	Allocate the socket and allow the family to set things up. if
	 *	the protocol is 0, the family is instructed to select an appropriate
	 *	default.
	 */
	sock = sock_alloc();
	if (!sock) {
		net_warn_ratelimited("socket: no more sockets\n");
		return -ENFILE;	/* Not exactly a match, but its the
				   closest posix thing */
	}

	sock->type = type;

#ifdef CONFIG_MODULES
	/* Attempt to load a protocol module if the find failed.
	 *
	 * 12/09/1996 Marcin: But! this makes REALLY only sense, if the user
	 * requested real, full-featured networking support upon configuration.
	 * Otherwise module support will break!
	 */
	if (rcu_access_pointer(net_families[family]) == NULL)
		request_module("net-pf-%d", family);
#endif

	rcu_read_lock();
	pf = rcu_dereference(net_families[family]);
	err = -EAFNOSUPPORT;
	if (!pf)
		goto out_release;

	/*
	 * We will call the ->create function, that possibly is in a loadable
	 * module, so we have to bump that loadable module refcnt first.
	 */
	if (!try_module_get(pf->owner))
		goto out_release;

	/* Now protected by module ref count */
	rcu_read_unlock();

	err = pf->create(net, sock, protocol, kern);
	if (err < 0)
		goto out_module_put;

	/*
	 * Now to bump the refcnt of the [loadable] module that owns this
	 * socket at sock_release time we decrement its refcnt.
	 */
	if (!try_module_get(sock->ops->owner))
		goto out_module_busy;

	/*
	 * Now that we're done with the ->create function, the [loadable]
	 * module can have its refcnt decremented
	 */
	module_put(pf->owner);
	err = security_socket_post_create(sock, family, type, protocol, kern);
	if (err)
		goto out_sock_release;
	*res = sock;

	return 0;

out_module_busy:
	err = -EAFNOSUPPORT;
out_module_put:
	sock->ops = NULL;
	module_put(pf->owner);
out_sock_release:
	sock_release(sock);
	return err;

out_release:
	rcu_read_unlock();
	goto out_sock_release;
}
EXPORT_SYMBOL(__sock_create);

/**
 *	sock_create - creates a socket
 *	@family: protocol family (AF_INET, ...)
 *	@type: communication type (SOCK_STREAM, ...)
 *	@protocol: protocol (0, ...)
 *	@res: new socket
 *
 *	A wrapper around __sock_create().
 *	Returns 0 or an error. This function internally uses GFP_KERNEL.
 */

int sock_create(int family, int type, int protocol, struct socket **res)
{
	return __sock_create(current->nsproxy->net_ns, family, type, protocol, res, 0);
}
EXPORT_SYMBOL(sock_create);

/**
 *	sock_create_kern - creates a socket (kernel space)
 *	@net: net namespace
 *	@family: protocol family (AF_INET, ...)
 *	@type: communication type (SOCK_STREAM, ...)
 *	@protocol: protocol (0, ...)
 *	@res: new socket
 *
 *	A wrapper around __sock_create().
 *	Returns 0 or an error. This function internally uses GFP_KERNEL.
 */

int sock_create_kern(struct net *net, int family, int type, int protocol, struct socket **res)
{
	return __sock_create(net, family, type, protocol, res, 1);
}
EXPORT_SYMBOL(sock_create_kern);

int __sys_socket(int family, int type, int protocol)
{
	int retval;
	struct socket *sock;
	int flags;

	/* Check the SOCK_* constants for consistency.  */
	BUILD_BUG_ON(SOCK_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON((SOCK_MAX | SOCK_TYPE_MASK) != SOCK_TYPE_MASK);
	BUILD_BUG_ON(SOCK_CLOEXEC & SOCK_TYPE_MASK);
	BUILD_BUG_ON(SOCK_NONBLOCK & SOCK_TYPE_MASK);

	flags = type & ~SOCK_TYPE_MASK;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	type &= SOCK_TYPE_MASK;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	retval = sock_create(family, type, protocol, &sock);
	if (retval < 0)
		return retval;

	return sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
}

SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
{
	return __sys_socket(family, type, protocol);
}

/*
 *	Create a pair of connected sockets.
 */

int __sys_socketpair(int family, int type, int protocol, int __user *usockvec)
{
	struct socket *sock1, *sock2;
	int fd1, fd2, err;
	struct file *newfile1, *newfile2;
	int flags;

	flags = type & ~SOCK_TYPE_MASK;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	type &= SOCK_TYPE_MASK;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	/*
	 * reserve descriptors and make sure we won't fail
	 * to return them to userland.
	 */
	fd1 = get_unused_fd_flags(flags);
	if (unlikely(fd1 < 0))
		return fd1;

	fd2 = get_unused_fd_flags(flags);
	if (unlikely(fd2 < 0)) {
		put_unused_fd(fd1);
		return fd2;
	}

	err = put_user(fd1, &usockvec[0]);
	if (err)
		goto out;

	err = put_user(fd2, &usockvec[1]);
	if (err)
		goto out;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	err = sock_create(family, type, protocol, &sock1);
	if (unlikely(err < 0))
		goto out;

	err = sock_create(family, type, protocol, &sock2);
	if (unlikely(err < 0)) {
		sock_release(sock1);
		goto out;
	}

	err = security_socket_socketpair(sock1, sock2);
	if (unlikely(err)) {
		sock_release(sock2);
		sock_release(sock1);
		goto out;
	}

	err = sock1->ops->socketpair(sock1, sock2);
	if (unlikely(err < 0)) {
		sock_release(sock2);
		sock_release(sock1);
		goto out;
	}

	newfile1 = sock_alloc_file(sock1, flags, NULL);
	if (IS_ERR(newfile1)) {
		err = PTR_ERR(newfile1);
		sock_release(sock2);
		goto out;
	}

	newfile2 = sock_alloc_file(sock2, flags, NULL);
	if (IS_ERR(newfile2)) {
		err = PTR_ERR(newfile2);
		fput(newfile1);
		goto out;
	}

	audit_fd_pair(fd1, fd2);

	fd_install(fd1, newfile1);
	fd_install(fd2, newfile2);
	return 0;

out:
	put_unused_fd(fd2);
	put_unused_fd(fd1);
	return err;
}

SYSCALL_DEFINE4(socketpair, int, family, int, type, int, protocol,
		int __user *, usockvec)
{
	return __sys_socketpair(family, type, protocol, usockvec);
}

/*
 *	Bind a name to a socket. Nothing much to do here since it's
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */

int __sys_bind(int fd, struct sockaddr __user *umyaddr, int addrlen)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		err = move_addr_to_kernel(umyaddr, addrlen, &address);
		if (!err) {
			err = security_socket_bind(sock,
						   (struct sockaddr *)&address,
						   addrlen);
			if (!err)
				err = sock->ops->bind(sock,
						      (struct sockaddr *)
						      &address, addrlen);
		}
		fput_light(sock->file, fput_needed);
	}
	return err;
}

SYSCALL_DEFINE3(bind, int, fd, struct sockaddr __user *, umyaddr, int, addrlen)
{
	return __sys_bind(fd, umyaddr, addrlen);
}

/*
 *	Perform a listen. Basically, we allow the protocol to do anything
 *	necessary for a listen, and if that works, we mark the socket as
 *	ready for listening.
 */

int __sys_listen(int fd, int backlog)
{
	struct socket *sock;
	int err, fput_needed;
	int somaxconn;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		somaxconn = READ_ONCE(sock_net(sock->sk)->core.sysctl_somaxconn);
		if ((unsigned int)backlog > somaxconn)
			backlog = somaxconn;

		err = security_socket_listen(sock, backlog);
		if (!err)
			err = sock->ops->listen(sock, backlog);

		fput_light(sock->file, fput_needed);
	}
	return err;
}

SYSCALL_DEFINE2(listen, int, fd, int, backlog)
{
	return __sys_listen(fd, backlog);
}

int __sys_accept4_file(struct file *file, unsigned file_flags,
		       struct sockaddr __user *upeer_sockaddr,
		       int __user *upeer_addrlen, int flags,
		       unsigned long nofile)
{
	struct socket *sock, *newsock;
	struct file *newfile;
	int err, len, newfd;
	struct sockaddr_storage address;

	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	sock = sock_from_file(file, &err);
	if (!sock)
		goto out;

	err = -ENFILE;
	newsock = sock_alloc();
	if (!newsock)
		goto out;

	newsock->type = sock->type;
	newsock->ops = sock->ops;

	/*
	 * We don't need try_module_get here, as the listening socket (sock)
	 * has the protocol module (sock->ops->owner) held.
	 */
	__module_get(newsock->ops->owner);

	newfd = __get_unused_fd_flags(flags, nofile);
	if (unlikely(newfd < 0)) {
		err = newfd;
		sock_release(newsock);
		goto out;
	}
	newfile = sock_alloc_file(newsock, flags, sock->sk->sk_prot_creator->name);
	if (IS_ERR(newfile)) {
		err = PTR_ERR(newfile);
		put_unused_fd(newfd);
		goto out;
	}

	err = security_socket_accept(sock, newsock);
	if (err)
		goto out_fd;

	err = sock->ops->accept(sock, newsock, sock->file->f_flags | file_flags,
					false);
	if (err < 0)
		goto out_fd;

	if (upeer_sockaddr) {
		len = newsock->ops->getname(newsock,
					(struct sockaddr *)&address, 2);
		if (len < 0) {
			err = -ECONNABORTED;
			goto out_fd;
		}
		err = move_addr_to_user(&address,
					len, upeer_sockaddr, upeer_addrlen);
		if (err < 0)
			goto out_fd;
	}

	/* File flags are not inherited via accept() unlike another OSes. */

	fd_install(newfd, newfile);
	err = newfd;
out:
	return err;
out_fd:
	fput(newfile);
	put_unused_fd(newfd);
	goto out;

}

/*
 *	For accept, we attempt to create a new socket, set up the link
 *	with the client, wake up the client, then return the new
 *	connected fd. We collect the address of the connector in kernel
 *	space and move it to user at the very end. This is unclean because
 *	we open the socket then return an error.
 *
 *	1003.1g adds the ability to recvmsg() to query connection pending
 *	status to recvmsg. We need to add that support in a way thats
 *	clean when we restructure accept also.
 */

int __sys_accept4(int fd, struct sockaddr __user *upeer_sockaddr,
		  int __user *upeer_addrlen, int flags)
{
	int ret = -EBADF;
	struct fd f;

	f = fdget(fd);
	if (f.file) {
		ret = __sys_accept4_file(f.file, 0, upeer_sockaddr,
						upeer_addrlen, flags,
						rlimit(RLIMIT_NOFILE));
		fdput(f);
	}

	return ret;
}

SYSCALL_DEFINE4(accept4, int, fd, struct sockaddr __user *, upeer_sockaddr,
		int __user *, upeer_addrlen, int, flags)
{
	return __sys_accept4(fd, upeer_sockaddr, upeer_addrlen, flags);
}

SYSCALL_DEFINE3(accept, int, fd, struct sockaddr __user *, upeer_sockaddr,
		int __user *, upeer_addrlen)
{
	return __sys_accept4(fd, upeer_sockaddr, upeer_addrlen, 0);
}

/*
 *	Attempt to connect to a socket with the server address.  The address
 *	is in user space so we verify it is OK and move it to kernel space.
 *
 *	For 1003.1g we need to add clean support for a bind to AF_UNSPEC to
 *	break bindings
 *
 *	NOTE: 1003.1g draft 6.3 is broken with respect to AX.25/NetROM and
 *	other SEQPACKET protocols that take time to connect() as it doesn't
 *	include the -EINPROGRESS status for such sockets.
 */

int __sys_connect_file(struct file *file, struct sockaddr_storage *address,
		       int addrlen, int file_flags)
{
	struct socket *sock;
	int err;

	sock = sock_from_file(file, &err);
	if (!sock)
		goto out;

	err =
	    security_socket_connect(sock, (struct sockaddr *)address, addrlen);
	if (err)
		goto out;

	err = sock->ops->connect(sock, (struct sockaddr *)address, addrlen,
				 sock->file->f_flags | file_flags);
out:
	return err;
}

int __sys_connect(int fd, struct sockaddr __user *uservaddr, int addrlen)
{
	int ret = -EBADF;
	struct fd f;

	f = fdget(fd);
	if (f.file) {
		struct sockaddr_storage address;

		ret = move_addr_to_kernel(uservaddr, addrlen, &address);
		if (!ret)
			ret = __sys_connect_file(f.file, &address, addrlen, 0);
		fdput(f);
	}

	return ret;
}

SYSCALL_DEFINE3(connect, int, fd, struct sockaddr __user *, uservaddr,
		int, addrlen)
{
	return __sys_connect(fd, uservaddr, addrlen);
}

/*
 *	Get the local address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

int __sys_getsockname(int fd, struct sockaddr __user *usockaddr,
		      int __user *usockaddr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	err = security_socket_getsockname(sock);
	if (err)
		goto out_put;

	err = sock->ops->getname(sock, (struct sockaddr *)&address, 0);
	if (err < 0)
		goto out_put;
        /* "err" is actually length in this case */
	err = move_addr_to_user(&address, err, usockaddr, usockaddr_len);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE3(getsockname, int, fd, struct sockaddr __user *, usockaddr,
		int __user *, usockaddr_len)
{
	return __sys_getsockname(fd, usockaddr, usockaddr_len);
}

/*
 *	Get the remote address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

int __sys_getpeername(int fd, struct sockaddr __user *usockaddr,
		      int __user *usockaddr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = security_socket_getpeername(sock);
		if (err) {
			fput_light(sock->file, fput_needed);
			return err;
		}

		err = sock->ops->getname(sock, (struct sockaddr *)&address, 1);
		if (err >= 0)
			/* "err" is actually length in this case */
			err = move_addr_to_user(&address, err, usockaddr,
						usockaddr_len);
		fput_light(sock->file, fput_needed);
	}
	return err;
}

SYSCALL_DEFINE3(getpeername, int, fd, struct sockaddr __user *, usockaddr,
		int __user *, usockaddr_len)
{
	return __sys_getpeername(fd, usockaddr, usockaddr_len);
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */
int __sys_sendto(int fd, void __user *buff, size_t len, unsigned int flags,
		 struct sockaddr __user *addr,  int addr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err;
	struct msghdr msg;
	struct iovec iov;
	int fput_needed;

	err = import_single_range(WRITE, buff, len, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_name = NULL;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	if (addr) {
		err = move_addr_to_kernel(addr, addr_len, &address);
		if (err < 0)
			goto out_put;
		msg.msg_name = (struct sockaddr *)&address;
		msg.msg_namelen = addr_len;
	}
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags = flags;
	err = sock_sendmsg(sock, &msg);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE6(sendto, int, fd, void __user *, buff, size_t, len,
		unsigned int, flags, struct sockaddr __user *, addr,
		int, addr_len)
{
	return __sys_sendto(fd, buff, len, flags, addr, addr_len);
}

/*
 *	Send a datagram down a socket.
 */

SYSCALL_DEFINE4(send, int, fd, void __user *, buff, size_t, len,
		unsigned int, flags)
{
	return __sys_sendto(fd, buff, len, flags, NULL, 0);
}

/*
 *	Receive a frame from the socket and optionally record the address of the
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */
int __sys_recvfrom(int fd, void __user *ubuf, size_t size, unsigned int flags,
		   struct sockaddr __user *addr, int __user *addr_len)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_storage address;
	int err, err2;
	int fput_needed;

	err = import_single_range(READ, ubuf, size, &iov, &msg.msg_iter);
	if (unlikely(err))
		return err;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	/* Save some cycles and don't copy the address if not needed */
	msg.msg_name = addr ? (struct sockaddr *)&address : NULL;
	/* We assume all kernel code knows the size of sockaddr_storage */
	msg.msg_namelen = 0;
	msg.msg_iocb = NULL;
	msg.msg_flags = 0;
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg(sock, &msg, flags);

	if (err >= 0 && addr != NULL) {
		err2 = move_addr_to_user(&address,
					 msg.msg_namelen, addr, addr_len);
		if (err2 < 0)
			err = err2;
	}

	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE6(recvfrom, int, fd, void __user *, ubuf, size_t, size,
		unsigned int, flags, struct sockaddr __user *, addr,
		int __user *, addr_len)
{
	return __sys_recvfrom(fd, ubuf, size, flags, addr, addr_len);
}

/*
 *	Receive a datagram from a socket.
 */

SYSCALL_DEFINE4(recv, int, fd, void __user *, ubuf, size_t, size,
		unsigned int, flags)
{
	return __sys_recvfrom(fd, ubuf, size, flags, NULL, NULL);
}

static bool sock_use_custom_sol_socket(const struct socket *sock)
{
	const struct sock *sk = sock->sk;

	/* Use sock->ops->setsockopt() for MPTCP */
	return IS_ENABLED(CONFIG_MPTCP) &&
	       sk->sk_protocol == IPPROTO_MPTCP &&
	       sk->sk_type == SOCK_STREAM &&
	       (sk->sk_family == AF_INET || sk->sk_family == AF_INET6);
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */
int __sys_setsockopt(int fd, int level, int optname, char __user *user_optval,
		int optlen)
{
	sockptr_t optval = USER_SOCKPTR(user_optval);
	char *kernel_optval = NULL;
	int err, fput_needed;
	struct socket *sock;

	if (optlen < 0)
		return -EINVAL;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		return err;

	err = security_socket_setsockopt(sock, level, optname);
	if (err)
		goto out_put;

	if (!in_compat_syscall())
		err = BPF_CGROUP_RUN_PROG_SETSOCKOPT(sock->sk, &level, &optname,
						     user_optval, &optlen,
						     &kernel_optval);
	if (err < 0)
		goto out_put;
	if (err > 0) {
		err = 0;
		goto out_put;
	}

	if (kernel_optval)
		optval = KERNEL_SOCKPTR(kernel_optval);
	if (level == SOL_SOCKET && !sock_use_custom_sol_socket(sock))
		err = sock_setsockopt(sock, level, optname, optval, optlen);
	else if (unlikely(!sock->ops->setsockopt))
		err = -EOPNOTSUPP;
	else
		err = sock->ops->setsockopt(sock, level, optname, optval,
					    optlen);
	kfree(kernel_optval);
out_put:
	fput_light(sock->file, fput_needed);
	return err;
}

SYSCALL_DEFINE5(setsockopt, int, fd, int, level, int, optname,
		char __user *, optval, int, optlen)
{
	return __sys_setsockopt(fd, level, optname, optval, optlen);
}

/*
 *	Get a socket option. Because we don't know the option lengths we have
 *	to pass a user mode parameter for the protocols to sort out.
 */
int __sys_getsockopt(int fd, int level, int optname, char __user *optval,
		int __user *optlen)
{
	int err, fput_needed;
	struct socket *sock;
	int max_optlen;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		return err;

	err = security_socket_getsockopt(sock, level, optname);
	if (err)
		goto out_put;

	if (!in_compat_syscall())
		max_optlen = BPF_CGROUP_GETSOCKOPT_MAX_OPTLEN(optlen);

	if (level == SOL_SOCKET)
		err = sock_getsockopt(sock, level, optname, optval, optlen);
	else if (unlikely(!sock->ops->getsockopt))
		err = -EOPNOTSUPP;
	else
		err = sock->ops->getsockopt(sock, level, optname, optval,
					    optlen);

	if (!in_compat_syscall())
		err = BPF_CGROUP_RUN_PROG_GETSOCKOPT(sock->sk, level, optname,
						     optval, optlen, max_optlen,
						     err);
out_put:
	fput_light(sock->file, fput_needed);
	return err;
}

SYSCALL_DEFINE5(getsockopt, int, fd, int, level, int, optname,
		char __user *, optval, int __user *, optlen)
{
	return __sys_getsockopt(fd, level, optname, optval, optlen);
}

/*
 *	Shutdown a socket.
 */

int __sys_shutdown_sock(struct socket *sock, int how)
{
	int err;

	err = security_socket_shutdown(sock, how);
	if (!err)
		err = sock->ops->shutdown(sock, how);

	return err;
}

int __sys_shutdown(int fd, int how)
{
	int err, fput_needed;
	struct socket *sock;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = __sys_shutdown_sock(sock, how);
		fput_light(sock->file, fput_needed);
	}
	return err;
}

SYSCALL_DEFINE2(shutdown, int, fd, int, how)
{
	return __sys_shutdown(fd, how);
}

/* A couple of helpful macros for getting the address of the 32/64 bit
 * fields which are the same type (int / unsigned) on our platforms.
 */
#define COMPAT_MSG(msg, member)	((MSG_CMSG_COMPAT & flags) ? &msg##_compat->member : &msg->member)
#define COMPAT_NAMELEN(msg)	COMPAT_MSG(msg, msg_namelen)
#define COMPAT_FLAGS(msg)	COMPAT_MSG(msg, msg_flags)

struct used_address {
	struct sockaddr_storage name;
	unsigned int name_len;
};

int __copy_msghdr_from_user(struct msghdr *kmsg,
			    struct user_msghdr __user *umsg,
			    struct sockaddr __user **save_addr,
			    struct iovec __user **uiov, size_t *nsegs)
{
	struct user_msghdr msg;
	ssize_t err;

	if (copy_from_user(&msg, umsg, sizeof(*umsg)))
		return -EFAULT;

	kmsg->msg_control_is_user = true;
	kmsg->msg_control_user = msg.msg_control;
	kmsg->msg_controllen = msg.msg_controllen;
	kmsg->msg_flags = msg.msg_flags;

	kmsg->msg_namelen = msg.msg_namelen;
	if (!msg.msg_name)
		kmsg->msg_namelen = 0;

	if (kmsg->msg_namelen < 0)
		return -EINVAL;

	if (kmsg->msg_namelen > sizeof(struct sockaddr_storage))
		kmsg->msg_namelen = sizeof(struct sockaddr_storage);

	if (save_addr)
		*save_addr = msg.msg_name;

	if (msg.msg_name && kmsg->msg_namelen) {
		if (!save_addr) {
			err = move_addr_to_kernel(msg.msg_name,
						  kmsg->msg_namelen,
						  kmsg->msg_name);
			if (err < 0)
				return err;
		}
	} else {
		kmsg->msg_name = NULL;
		kmsg->msg_namelen = 0;
	}

	if (msg.msg_iovlen > UIO_MAXIOV)
		return -EMSGSIZE;

	kmsg->msg_iocb = NULL;
	*uiov = msg.msg_iov;
	*nsegs = msg.msg_iovlen;
	return 0;
}

static int copy_msghdr_from_user(struct msghdr *kmsg,
				 struct user_msghdr __user *umsg,
				 struct sockaddr __user **save_addr,
				 struct iovec **iov)
{
	struct user_msghdr msg;
	ssize_t err;

	err = __copy_msghdr_from_user(kmsg, umsg, save_addr, &msg.msg_iov,
					&msg.msg_iovlen);
	if (err)
		return err;

	err = import_iovec(save_addr ? READ : WRITE,
			    msg.msg_iov, msg.msg_iovlen,
			    UIO_FASTIOV, iov, &kmsg->msg_iter);
	return err < 0 ? err : 0;
}

static int ____sys_sendmsg(struct socket *sock, struct msghdr *msg_sys,
			   unsigned int flags, struct used_address *used_address,
			   unsigned int allowed_msghdr_flags)
{
	unsigned char ctl[sizeof(struct cmsghdr) + 20]
				__aligned(sizeof(__kernel_size_t));
	/* 20 is size of ipv6_pktinfo */
	unsigned char *ctl_buf = ctl;
	int ctl_len;
	ssize_t err;

	err = -ENOBUFS;

	if (msg_sys->msg_controllen > INT_MAX)
		goto out;
	flags |= (msg_sys->msg_flags & allowed_msghdr_flags);
	ctl_len = msg_sys->msg_controllen;
	if ((MSG_CMSG_COMPAT & flags) && ctl_len) {
		err =
		    cmsghdr_from_user_compat_to_kern(msg_sys, sock->sk, ctl,
						     sizeof(ctl));
		if (err)
			goto out;
		ctl_buf = msg_sys->msg_control;
		ctl_len = msg_sys->msg_controllen;
	} else if (ctl_len) {
		BUILD_BUG_ON(sizeof(struct cmsghdr) !=
			     CMSG_ALIGN(sizeof(struct cmsghdr)));
		if (ctl_len > sizeof(ctl)) {
			ctl_buf = sock_kmalloc(sock->sk, ctl_len, GFP_KERNEL);
			if (ctl_buf == NULL)
				goto out;
		}
		err = -EFAULT;
		if (copy_from_user(ctl_buf, msg_sys->msg_control_user, ctl_len))
			goto out_freectl;
		msg_sys->msg_control = ctl_buf;
		msg_sys->msg_control_is_user = false;
	}
	msg_sys->msg_flags = flags;

	if (sock->file->f_flags & O_NONBLOCK)
		msg_sys->msg_flags |= MSG_DONTWAIT;
	/*
	 * If this is sendmmsg() and current destination address is same as
	 * previously succeeded address, omit asking LSM's decision.
	 * used_address->name_len is initialized to UINT_MAX so that the first
	 * destination address never matches.
	 */
	if (used_address && msg_sys->msg_name &&
	    used_address->name_len == msg_sys->msg_namelen &&
	    !memcmp(&used_address->name, msg_sys->msg_name,
		    used_address->name_len)) {
		err = sock_sendmsg_nosec(sock, msg_sys);
		goto out_freectl;
	}
	err = sock_sendmsg(sock, msg_sys);
	/*
	 * If this is sendmmsg() and sending to current destination address was
	 * successful, remember it.
	 */
	if (used_address && err >= 0) {
		used_address->name_len = msg_sys->msg_namelen;
		if (msg_sys->msg_name)
			memcpy(&used_address->name, msg_sys->msg_name,
			       used_address->name_len);
	}

out_freectl:
	if (ctl_buf != ctl)
		sock_kfree_s(sock->sk, ctl_buf, ctl_len);
out:
	return err;
}

int sendmsg_copy_msghdr(struct msghdr *msg,
			struct user_msghdr __user *umsg, unsigned flags,
			struct iovec **iov)
{
	int err;

	if (flags & MSG_CMSG_COMPAT) {
		struct compat_msghdr __user *msg_compat;

		msg_compat = (struct compat_msghdr __user *) umsg;
		err = get_compat_msghdr(msg, msg_compat, NULL, iov);
	} else {
		err = copy_msghdr_from_user(msg, umsg, NULL, iov);
	}
	if (err < 0)
		return err;

	return 0;
}

static int ___sys_sendmsg(struct socket *sock, struct user_msghdr __user *msg,
			 struct msghdr *msg_sys, unsigned int flags,
			 struct used_address *used_address,
			 unsigned int allowed_msghdr_flags)
{
	struct sockaddr_storage address;
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	ssize_t err;

	msg_sys->msg_name = &address;

	err = sendmsg_copy_msghdr(msg_sys, msg, flags, &iov);
	if (err < 0)
		return err;

	err = ____sys_sendmsg(sock, msg_sys, flags, used_address,
				allowed_msghdr_flags);
	kfree(iov);
	return err;
}

/*
 *	BSD sendmsg interface
 */
long __sys_sendmsg_sock(struct socket *sock, struct msghdr *msg,
			unsigned int flags)
{
	/* disallow ancillary data requests from this path */
	if (msg->msg_control || msg->msg_controllen)
		return -EINVAL;

	return ____sys_sendmsg(sock, msg, flags, NULL, 0);
}

long __sys_sendmsg(int fd, struct user_msghdr __user *msg, unsigned int flags,
		   bool forbid_cmsg_compat)
{
	int fput_needed, err;
	struct msghdr msg_sys;
	struct socket *sock;

	if (forbid_cmsg_compat && (flags & MSG_CMSG_COMPAT))
		return -EINVAL;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	err = ___sys_sendmsg(sock, msg, &msg_sys, flags, NULL, 0);

	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE3(sendmsg, int, fd, struct user_msghdr __user *, msg, unsigned int, flags)
{
	return __sys_sendmsg(fd, msg, flags, true);
}

/*
 *	Linux sendmmsg interface
 */

int __sys_sendmmsg(int fd, struct mmsghdr __user *mmsg, unsigned int vlen,
		   unsigned int flags, bool forbid_cmsg_compat)
{
	int fput_needed, err, datagrams;
	struct socket *sock;
	struct mmsghdr __user *entry;
	struct compat_mmsghdr __user *compat_entry;
	struct msghdr msg_sys;
	struct used_address used_address;
	unsigned int oflags = flags;

	if (forbid_cmsg_compat && (flags & MSG_CMSG_COMPAT))
		return -EINVAL;

	if (vlen > UIO_MAXIOV)
		vlen = UIO_MAXIOV;

	datagrams = 0;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		return err;

	used_address.name_len = UINT_MAX;
	entry = mmsg;
	compat_entry = (struct compat_mmsghdr __user *)mmsg;
	err = 0;
	flags |= MSG_BATCH;

	while (datagrams < vlen) {
		if (datagrams == vlen - 1)
			flags = oflags;

		if (MSG_CMSG_COMPAT & flags) {
			err = ___sys_sendmsg(sock, (struct user_msghdr __user *)compat_entry,
					     &msg_sys, flags, &used_address, MSG_EOR);
			if (err < 0)
				break;
			err = __put_user(err, &compat_entry->msg_len);
			++compat_entry;
		} else {
			err = ___sys_sendmsg(sock,
					     (struct user_msghdr __user *)entry,
					     &msg_sys, flags, &used_address, MSG_EOR);
			if (err < 0)
				break;
			err = put_user(err, &entry->msg_len);
			++entry;
		}

		if (err)
			break;
		++datagrams;
		if (msg_data_left(&msg_sys))
			break;
		cond_resched();
	}

	fput_light(sock->file, fput_needed);

	/* We only return an error if no datagrams were able to be sent */
	if (datagrams != 0)
		return datagrams;

	return err;
}

SYSCALL_DEFINE4(sendmmsg, int, fd, struct mmsghdr __user *, mmsg,
		unsigned int, vlen, unsigned int, flags)
{
	return __sys_sendmmsg(fd, mmsg, vlen, flags, true);
}

int recvmsg_copy_msghdr(struct msghdr *msg,
			struct user_msghdr __user *umsg, unsigned flags,
			struct sockaddr __user **uaddr,
			struct iovec **iov)
{
	ssize_t err;

	if (MSG_CMSG_COMPAT & flags) {
		struct compat_msghdr __user *msg_compat;

		msg_compat = (struct compat_msghdr __user *) umsg;
		err = get_compat_msghdr(msg, msg_compat, uaddr, iov);
	} else {
		err = copy_msghdr_from_user(msg, umsg, uaddr, iov);
	}
	if (err < 0)
		return err;

	return 0;
}

static int ____sys_recvmsg(struct socket *sock, struct msghdr *msg_sys,
			   struct user_msghdr __user *msg,
			   struct sockaddr __user *uaddr,
			   unsigned int flags, int nosec)
{
	struct compat_msghdr __user *msg_compat =
					(struct compat_msghdr __user *) msg;
	int __user *uaddr_len = COMPAT_NAMELEN(msg);
	struct sockaddr_storage addr;
	unsigned long cmsg_ptr;
	int len;
	ssize_t err;

	msg_sys->msg_name = &addr;
	cmsg_ptr = (unsigned long)msg_sys->msg_control;
	msg_sys->msg_flags = flags & (MSG_CMSG_CLOEXEC|MSG_CMSG_COMPAT);

	/* We assume all kernel code knows the size of sockaddr_storage */
	msg_sys->msg_namelen = 0;

	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;

	if (unlikely(nosec))
		err = sock_recvmsg_nosec(sock, msg_sys, flags);
	else
		err = sock_recvmsg(sock, msg_sys, flags);

	if (err < 0)
		goto out;
	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user(&addr,
					msg_sys->msg_namelen, uaddr,
					uaddr_len);
		if (err < 0)
			goto out;
	}
	err = __put_user((msg_sys->msg_flags & ~MSG_CMSG_COMPAT),
			 COMPAT_FLAGS(msg));
	if (err)
		goto out;
	if (MSG_CMSG_COMPAT & flags)
		err = __put_user((unsigned long)msg_sys->msg_control - cmsg_ptr,
				 &msg_compat->msg_controllen);
	else
		err = __put_user((unsigned long)msg_sys->msg_control - cmsg_ptr,
				 &msg->msg_controllen);
	if (err)
		goto out;
	err = len;
out:
	return err;
}

static int ___sys_recvmsg(struct socket *sock, struct user_msghdr __user *msg,
			 struct msghdr *msg_sys, unsigned int flags, int nosec)
{
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	/* user mode address pointers */
	struct sockaddr __user *uaddr;
	ssize_t err;

	err = recvmsg_copy_msghdr(msg_sys, msg, flags, &uaddr, &iov);
	if (err < 0)
		return err;

	err = ____sys_recvmsg(sock, msg_sys, msg, uaddr, flags, nosec);
	kfree(iov);
	return err;
}

/*
 *	BSD recvmsg interface
 */

long __sys_recvmsg_sock(struct socket *sock, struct msghdr *msg,
			struct user_msghdr __user *umsg,
			struct sockaddr __user *uaddr, unsigned int flags)
{
	if (msg->msg_control || msg->msg_controllen) {
		/* disallow ancillary data reqs unless cmsg is plain data */
		if (!(sock->ops->flags & PROTO_CMSG_DATA_ONLY))
			return -EINVAL;
	}

	return ____sys_recvmsg(sock, msg, umsg, uaddr, flags, 0);
}

long __sys_recvmsg(int fd, struct user_msghdr __user *msg, unsigned int flags,
		   bool forbid_cmsg_compat)
{
	int fput_needed, err;
	struct msghdr msg_sys;
	struct socket *sock;

	if (forbid_cmsg_compat && (flags & MSG_CMSG_COMPAT))
		return -EINVAL;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	err = ___sys_recvmsg(sock, msg, &msg_sys, flags, 0);

	fput_light(sock->file, fput_needed);
out:
	return err;
}

SYSCALL_DEFINE3(recvmsg, int, fd, struct user_msghdr __user *, msg,
		unsigned int, flags)
{
	return __sys_recvmsg(fd, msg, flags, true);
}

/*
 *     Linux recvmmsg interface
 */

static int do_recvmmsg(int fd, struct mmsghdr __user *mmsg,
			  unsigned int vlen, unsigned int flags,
			  struct timespec64 *timeout)
{
	int fput_needed, err, datagrams;
	struct socket *sock;
	struct mmsghdr __user *entry;
	struct compat_mmsghdr __user *compat_entry;
	struct msghdr msg_sys;
	struct timespec64 end_time;
	struct timespec64 timeout64;

	if (timeout &&
	    poll_select_set_timeout(&end_time, timeout->tv_sec,
				    timeout->tv_nsec))
		return -EINVAL;

	datagrams = 0;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		return err;

	if (likely(!(flags & MSG_ERRQUEUE))) {
		err = sock_error(sock->sk);
		if (err) {
			datagrams = err;
			goto out_put;
		}
	}

	entry = mmsg;
	compat_entry = (struct compat_mmsghdr __user *)mmsg;

	while (datagrams < vlen) {
		/*
		 * No need to ask LSM for more than the first datagram.
		 */
		if (MSG_CMSG_COMPAT & flags) {
			err = ___sys_recvmsg(sock, (struct user_msghdr __user *)compat_entry,
					     &msg_sys, flags & ~MSG_WAITFORONE,
					     datagrams);
			if (err < 0)
				break;
			err = __put_user(err, &compat_entry->msg_len);
			++compat_entry;
		} else {
			err = ___sys_recvmsg(sock,
					     (struct user_msghdr __user *)entry,
					     &msg_sys, flags & ~MSG_WAITFORONE,
					     datagrams);
			if (err < 0)
				break;
			err = put_user(err, &entry->msg_len);
			++entry;
		}

		if (err)
			break;
		++datagrams;

		/* MSG_WAITFORONE turns on MSG_DONTWAIT after one packet */
		if (flags & MSG_WAITFORONE)
			flags |= MSG_DONTWAIT;

		if (timeout) {
			ktime_get_ts64(&timeout64);
			*timeout = timespec64_sub(end_time, timeout64);
			if (timeout->tv_sec < 0) {
				timeout->tv_sec = timeout->tv_nsec = 0;
				break;
			}

			/* Timeout, return less than vlen datagrams */
			if (timeout->tv_nsec == 0 && timeout->tv_sec == 0)
				break;
		}

		/* Out of band data, return right away */
		if (msg_sys.msg_flags & MSG_OOB)
			break;
		cond_resched();
	}

	if (err == 0)
		goto out_put;

	if (datagrams == 0) {
		datagrams = err;
		goto out_put;
	}

	/*
	 * We may return less entries than requested (vlen) if the
	 * sock is non block and there aren't enough datagrams...
	 */
	if (err != -EAGAIN) {
		/*
		 * ... or  if recvmsg returns an error after we
		 * received some datagrams, where we record the
		 * error to return on the next call or if the
		 * app asks about it using getsockopt(SO_ERROR).
		 */
		sock->sk->sk_err = -err;
	}
out_put:
	fput_light(sock->file, fput_needed);

	return datagrams;
}

int __sys_recvmmsg(int fd, struct mmsghdr __user *mmsg,
		   unsigned int vlen, unsigned int flags,
		   struct __kernel_timespec __user *timeout,
		   struct old_timespec32 __user *timeout32)
{
	int datagrams;
	struct timespec64 timeout_sys;

	if (timeout && get_timespec64(&timeout_sys, timeout))
		return -EFAULT;

	if (timeout32 && get_old_timespec32(&timeout_sys, timeout32))
		return -EFAULT;

	if (!timeout && !timeout32)
		return do_recvmmsg(fd, mmsg, vlen, flags, NULL);

	datagrams = do_recvmmsg(fd, mmsg, vlen, flags, &timeout_sys);

	if (datagrams <= 0)
		return datagrams;

	if (timeout && put_timespec64(&timeout_sys, timeout))
		datagrams = -EFAULT;

	if (timeout32 && put_old_timespec32(&timeout_sys, timeout32))
		datagrams = -EFAULT;

	return datagrams;
}

SYSCALL_DEFINE5(recvmmsg, int, fd, struct mmsghdr __user *, mmsg,
		unsigned int, vlen, unsigned int, flags,
		struct __kernel_timespec __user *, timeout)
{
	if (flags & MSG_CMSG_COMPAT)
		return -EINVAL;

	return __sys_recvmmsg(fd, mmsg, vlen, flags, timeout, NULL);
}

#ifdef CONFIG_COMPAT_32BIT_TIME
SYSCALL_DEFINE5(recvmmsg_time32, int, fd, struct mmsghdr __user *, mmsg,
		unsigned int, vlen, unsigned int, flags,
		struct old_timespec32 __user *, timeout)
{
	if (flags & MSG_CMSG_COMPAT)
		return -EINVAL;

	return __sys_recvmmsg(fd, mmsg, vlen, flags, NULL, timeout);
}
#endif

#ifdef __ARCH_WANT_SYS_SOCKETCALL
/* Argument list sizes for sys_socketcall */
#define AL(x) ((x) * sizeof(unsigned long))
static const unsigned char nargs[21] = {
	AL(0), AL(3), AL(3), AL(3), AL(2), AL(3),
	AL(3), AL(3), AL(4), AL(4), AL(4), AL(6),
	AL(6), AL(2), AL(5), AL(5), AL(3), AL(3),
	AL(4), AL(5), AL(4)
};

#undef AL

/*
 *	System call vectors.
 *
 *	Argument checking cleaned up. Saved 20% in size.
 *  This function doesn't need to set the kernel lock because
 *  it is set by the callees.
 */

SYSCALL_DEFINE2(socketcall, int, call, unsigned long __user *, args)
{
	unsigned long a[AUDITSC_ARGS];
	unsigned long a0, a1;
	int err;
	unsigned int len;

	if (call < 1 || call > SYS_SENDMMSG)
		return -EINVAL;
	call = array_index_nospec(call, SYS_SENDMMSG + 1);

	len = nargs[call];
	if (len > sizeof(a))
		return -EINVAL;

	/* copy_from_user should be SMP safe. */
	if (copy_from_user(a, args, len))
		return -EFAULT;

	err = audit_socketcall(nargs[call] / sizeof(unsigned long), a);
	if (err)
		return err;

	a0 = a[0];
	a1 = a[1];

	switch (call) {
	case SYS_SOCKET:
		err = __sys_socket(a0, a1, a[2]);
		break;
	case SYS_BIND:
		err = __sys_bind(a0, (struct sockaddr __user *)a1, a[2]);
		break;
	case SYS_CONNECT:
		err = __sys_connect(a0, (struct sockaddr __user *)a1, a[2]);
		break;
	case SYS_LISTEN:
		err = __sys_listen(a0, a1);
		break;
	case SYS_ACCEPT:
		err = __sys_accept4(a0, (struct sockaddr __user *)a1,
				    (int __user *)a[2], 0);
		break;
	case SYS_GETSOCKNAME:
		err =
		    __sys_getsockname(a0, (struct sockaddr __user *)a1,
				      (int __user *)a[2]);
		break;
	case SYS_GETPEERNAME:
		err =
		    __sys_getpeername(a0, (struct sockaddr __user *)a1,
				      (int __user *)a[2]);
		break;
	case SYS_SOCKETPAIR:
		err = __sys_socketpair(a0, a1, a[2], (int __user *)a[3]);
		break;
	case SYS_SEND:
		err = __sys_sendto(a0, (void __user *)a1, a[2], a[3],
				   NULL, 0);
		break;
	case SYS_SENDTO:
		err = __sys_sendto(a0, (void __user *)a1, a[2], a[3],
				   (struct sockaddr __user *)a[4], a[5]);
		break;
	case SYS_RECV:
		err = __sys_recvfrom(a0, (void __user *)a1, a[2], a[3],
				     NULL, NULL);
		break;
	case SYS_RECVFROM:
		err = __sys_recvfrom(a0, (void __user *)a1, a[2], a[3],
				     (struct sockaddr __user *)a[4],
				     (int __user *)a[5]);
		break;
	case SYS_SHUTDOWN:
		err = __sys_shutdown(a0, a1);
		break;
	case SYS_SETSOCKOPT:
		err = __sys_setsockopt(a0, a1, a[2], (char __user *)a[3],
				       a[4]);
		break;
	case SYS_GETSOCKOPT:
		err =
		    __sys_getsockopt(a0, a1, a[2], (char __user *)a[3],
				     (int __user *)a[4]);
		break;
	case SYS_SENDMSG:
		err = __sys_sendmsg(a0, (struct user_msghdr __user *)a1,
				    a[2], true);
		break;
	case SYS_SENDMMSG:
		err = __sys_sendmmsg(a0, (struct mmsghdr __user *)a1, a[2],
				     a[3], true);
		break;
	case SYS_RECVMSG:
		err = __sys_recvmsg(a0, (struct user_msghdr __user *)a1,
				    a[2], true);
		break;
	case SYS_RECVMMSG:
		if (IS_ENABLED(CONFIG_64BIT))
			err = __sys_recvmmsg(a0, (struct mmsghdr __user *)a1,
					     a[2], a[3],
					     (struct __kernel_timespec __user *)a[4],
					     NULL);
		else
			err = __sys_recvmmsg(a0, (struct mmsghdr __user *)a1,
					     a[2], a[3], NULL,
					     (struct old_timespec32 __user *)a[4]);
		break;
	case SYS_ACCEPT4:
		err = __sys_accept4(a0, (struct sockaddr __user *)a1,
				    (int __user *)a[2], a[3]);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

#endif				/* __ARCH_WANT_SYS_SOCKETCALL */

/**
 *	sock_register - add a socket protocol handler
 *	@ops: description of protocol
 *
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	socket interface. The value ops->family corresponds to the
 *	socket system call protocol family.
 */
int sock_register(const struct net_proto_family *ops)
{
	int err;

	if (ops->family >= NPROTO) {
		pr_crit("protocol %d >= NPROTO(%d)\n", ops->family, NPROTO);
		return -ENOBUFS;
	}

	spin_lock(&net_family_lock);
	if (rcu_dereference_protected(net_families[ops->family],
				      lockdep_is_held(&net_family_lock)))
		err = -EEXIST;
	else {
		rcu_assign_pointer(net_families[ops->family], ops);
		err = 0;
	}
	spin_unlock(&net_family_lock);

	pr_info("NET: Registered protocol family %d\n", ops->family);
	return err;
}
EXPORT_SYMBOL(sock_register);

/**
 *	sock_unregister - remove a protocol handler
 *	@family: protocol family to remove
 *
 *	This function is called by a protocol handler that wants to
 *	remove its address family, and have it unlinked from the
 *	new socket creation.
 *
 *	If protocol handler is a module, then it can use module reference
 *	counts to protect against new references. If protocol handler is not
 *	a module then it needs to provide its own protection in
 *	the ops->create routine.
 */
void sock_unregister(int family)
{
	BUG_ON(family < 0 || family >= NPROTO);

	spin_lock(&net_family_lock);
	RCU_INIT_POINTER(net_families[family], NULL);
	spin_unlock(&net_family_lock);

	synchronize_rcu();

	pr_info("NET: Unregistered protocol family %d\n", family);
}
EXPORT_SYMBOL(sock_unregister);

bool sock_is_registered(int family)
{
	return family < NPROTO && rcu_access_pointer(net_families[family]);
}

static int __init sock_init(void)
{
	int err;
	/*
	 *      Initialize the network sysctl infrastructure.
	 */
	err = net_sysctl_init();
	if (err)
		goto out;

	/*
	 *      Initialize skbuff SLAB cache
	 */
	skb_init();

	/*
	 *      Initialize the protocols module.
	 */

	init_inodecache();

	err = register_filesystem(&sock_fs_type);
	if (err)
		goto out;
	sock_mnt = kern_mount(&sock_fs_type);
	if (IS_ERR(sock_mnt)) {
		err = PTR_ERR(sock_mnt);
		goto out_mount;
	}

	/* The real protocol initialization is performed in later initcalls.
	 */

#ifdef CONFIG_NETFILTER
	err = netfilter_init();
	if (err)
		goto out;
#endif

	ptp_classifier_init();

out:
	return err;

out_mount:
	unregister_filesystem(&sock_fs_type);
	goto out;
}

core_initcall(sock_init);	/* early initcall */

#ifdef CONFIG_PROC_FS
void socket_seq_show(struct seq_file *seq)
{
	seq_printf(seq, "sockets: used %d\n",
		   sock_inuse_get(seq->private));
}
#endif				/* CONFIG_PROC_FS */

#ifdef CONFIG_COMPAT
static int compat_dev_ifconf(struct net *net, struct compat_ifconf __user *uifc32)
{
	struct compat_ifconf ifc32;
	struct ifconf ifc;
	int err;

	if (copy_from_user(&ifc32, uifc32, sizeof(struct compat_ifconf)))
		return -EFAULT;

	ifc.ifc_len = ifc32.ifc_len;
	ifc.ifc_req = compat_ptr(ifc32.ifcbuf);

	rtnl_lock();
	err = dev_ifconf(net, &ifc, sizeof(struct compat_ifreq));
	rtnl_unlock();
	if (err)
		return err;

	ifc32.ifc_len = ifc.ifc_len;
	if (copy_to_user(uifc32, &ifc32, sizeof(struct compat_ifconf)))
		return -EFAULT;

	return 0;
}

static int compat_siocwandev(struct net *net, struct compat_ifreq __user *uifr32)
{
	compat_uptr_t uptr32;
	struct ifreq ifr;
	void __user *saved;
	int err;

	if (copy_from_user(&ifr, uifr32, sizeof(struct compat_ifreq)))
		return -EFAULT;

	if (get_user(uptr32, &uifr32->ifr_settings.ifs_ifsu))
		return -EFAULT;

	saved = ifr.ifr_settings.ifs_ifsu.raw_hdlc;
	ifr.ifr_settings.ifs_ifsu.raw_hdlc = compat_ptr(uptr32);

	err = dev_ioctl(net, SIOCWANDEV, &ifr, NULL);
	if (!err) {
		ifr.ifr_settings.ifs_ifsu.raw_hdlc = saved;
		if (copy_to_user(uifr32, &ifr, sizeof(struct compat_ifreq)))
			err = -EFAULT;
	}
	return err;
}

/* Handle ioctls that use ifreq::ifr_data and just need struct ifreq converted */
static int compat_ifr_data_ioctl(struct net *net, unsigned int cmd,
				 struct compat_ifreq __user *u_ifreq32)
{
	struct ifreq ifreq;
	u32 data32;

	if (!is_socket_ioctl_cmd(cmd))
		return -ENOTTY;
	if (copy_from_user(ifreq.ifr_name, u_ifreq32->ifr_name, IFNAMSIZ))
		return -EFAULT;
	if (get_user(data32, &u_ifreq32->ifr_data))
		return -EFAULT;
	ifreq.ifr_data = compat_ptr(data32);

	return dev_ioctl(net, cmd, &ifreq, NULL);
}

static int compat_ifreq_ioctl(struct net *net, struct socket *sock,
			      unsigned int cmd,
			      struct compat_ifreq __user *uifr32)
{
	struct ifreq __user *uifr;
	int err;

	/* Handle the fact that while struct ifreq has the same *layout* on
	 * 32/64 for everything but ifreq::ifru_ifmap and ifreq::ifru_data,
	 * which are handled elsewhere, it still has different *size* due to
	 * ifreq::ifru_ifmap (which is 16 bytes on 32 bit, 24 bytes on 64-bit,
	 * resulting in struct ifreq being 32 and 40 bytes respectively).
	 * As a result, if the struct happens to be at the end of a page and
	 * the next page isn't readable/writable, we get a fault. To prevent
	 * that, copy back and forth to the full size.
	 */

	uifr = compat_alloc_user_space(sizeof(*uifr));
	if (copy_in_user(uifr, uifr32, sizeof(*uifr32)))
		return -EFAULT;

	err = sock_do_ioctl(net, sock, cmd, (unsigned long)uifr);

	if (!err) {
		switch (cmd) {
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCGIFINDEX:
		case SIOCGIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFPFLAGS:
		case SIOCGIFTXQLEN:
		case SIOCGMIIPHY:
		case SIOCGMIIREG:
		case SIOCGIFNAME:
			if (copy_in_user(uifr32, uifr, sizeof(*uifr32)))
				err = -EFAULT;
			break;
		}
	}
	return err;
}

static int compat_sioc_ifmap(struct net *net, unsigned int cmd,
			struct compat_ifreq __user *uifr32)
{
	struct ifreq ifr;
	struct compat_ifmap __user *uifmap32;
	int err;

	uifmap32 = &uifr32->ifr_ifru.ifru_map;
	err = copy_from_user(&ifr, uifr32, sizeof(ifr.ifr_name));
	err |= get_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
	err |= get_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
	err |= get_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
	err |= get_user(ifr.ifr_map.irq, &uifmap32->irq);
	err |= get_user(ifr.ifr_map.dma, &uifmap32->dma);
	err |= get_user(ifr.ifr_map.port, &uifmap32->port);
	if (err)
		return -EFAULT;

	err = dev_ioctl(net, cmd, &ifr, NULL);

	if (cmd == SIOCGIFMAP && !err) {
		err = copy_to_user(uifr32, &ifr, sizeof(ifr.ifr_name));
		err |= put_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
		err |= put_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
		err |= put_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
		err |= put_user(ifr.ifr_map.irq, &uifmap32->irq);
		err |= put_user(ifr.ifr_map.dma, &uifmap32->dma);
		err |= put_user(ifr.ifr_map.port, &uifmap32->port);
		if (err)
			err = -EFAULT;
	}
	return err;
}

/* Since old style bridge ioctl's endup using SIOCDEVPRIVATE
 * for some operations; this forces use of the newer bridge-utils that
 * use compatible ioctls
 */
static int old_bridge_ioctl(compat_ulong_t __user *argp)
{
	compat_ulong_t tmp;

	if (get_user(tmp, argp))
		return -EFAULT;
	if (tmp == BRCTL_GET_VERSION)
		return BRCTL_VERSION + 1;
	return -EINVAL;
}

static int compat_sock_ioctl_trans(struct file *file, struct socket *sock,
			 unsigned int cmd, unsigned long arg)
{
	void __user *argp = compat_ptr(arg);
	struct sock *sk = sock->sk;
	struct net *net = sock_net(sk);

	if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15))
		return compat_ifr_data_ioctl(net, cmd, argp);

	switch (cmd) {
	case SIOCSIFBR:
	case SIOCGIFBR:
		return old_bridge_ioctl(argp);
	case SIOCGIFCONF:
		return compat_dev_ifconf(net, argp);
	case SIOCWANDEV:
		return compat_siocwandev(net, argp);
	case SIOCGIFMAP:
	case SIOCSIFMAP:
		return compat_sioc_ifmap(net, cmd, argp);
	case SIOCGSTAMP_OLD:
	case SIOCGSTAMPNS_OLD:
		if (!sock->ops->gettstamp)
			return -ENOIOCTLCMD;
		return sock->ops->gettstamp(sock, argp, cmd == SIOCGSTAMP_OLD,
					    !COMPAT_USE_64BIT_TIME);

	case SIOCETHTOOL:
	case SIOCBONDSLAVEINFOQUERY:
	case SIOCBONDINFOQUERY:
	case SIOCSHWTSTAMP:
	case SIOCGHWTSTAMP:
		return compat_ifr_data_ioctl(net, cmd, argp);

	case FIOSETOWN:
	case SIOCSPGRP:
	case FIOGETOWN:
	case SIOCGPGRP:
	case SIOCBRADDBR:
	case SIOCBRDELBR:
	case SIOCGIFVLAN:
	case SIOCSIFVLAN:
	case SIOCADDDLCI:
	case SIOCDELDLCI:
	case SIOCGSKNS:
	case SIOCGSTAMP_NEW:
	case SIOCGSTAMPNS_NEW:
		return sock_ioctl(file, cmd, arg);

	case SIOCGIFFLAGS:
	case SIOCSIFFLAGS:
	case SIOCGIFMETRIC:
	case SIOCSIFMETRIC:
	case SIOCGIFMTU:
	case SIOCSIFMTU:
	case SIOCGIFMEM:
	case SIOCSIFMEM:
	case SIOCGIFHWADDR:
	case SIOCSIFHWADDR:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
	case SIOCGIFINDEX:
	case SIOCGIFADDR:
	case SIOCSIFADDR:
	case SIOCSIFHWBROADCAST:
	case SIOCDIFADDR:
	case SIOCGIFBRDADDR:
	case SIOCSIFBRDADDR:
	case SIOCGIFDSTADDR:
	case SIOCSIFDSTADDR:
	case SIOCGIFNETMASK:
	case SIOCSIFNETMASK:
	case SIOCSIFPFLAGS:
	case SIOCGIFPFLAGS:
	case SIOCGIFTXQLEN:
	case SIOCSIFTXQLEN:
	case SIOCBRADDIF:
	case SIOCBRDELIF:
	case SIOCGIFNAME:
	case SIOCSIFNAME:
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
		return compat_ifreq_ioctl(net, sock, cmd, argp);

	case SIOCSARP:
	case SIOCGARP:
	case SIOCDARP:
	case SIOCOUTQ:
	case SIOCOUTQNSD:
	case SIOCATMARK:
		return sock_do_ioctl(net, sock, cmd, arg);
	}

	return -ENOIOCTLCMD;
}

static long compat_sock_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct socket *sock = file->private_data;
	int ret = -ENOIOCTLCMD;
	struct sock *sk;
	struct net *net;

	sk = sock->sk;
	net = sock_net(sk);

	if (sock->ops->compat_ioctl)
		ret = sock->ops->compat_ioctl(sock, cmd, arg);

	if (ret == -ENOIOCTLCMD &&
	    (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST))
		ret = compat_wext_handle_ioctl(net, cmd, arg);

	if (ret == -ENOIOCTLCMD)
		ret = compat_sock_ioctl_trans(file, sock, cmd, arg);

	return ret;
}
#endif

/**
 *	kernel_bind - bind an address to a socket (kernel space)
 *	@sock: socket
 *	@addr: address
 *	@addrlen: length of address
 *
 *	Returns 0 or an error.
 */

int kernel_bind(struct socket *sock, struct sockaddr *addr, int addrlen)
{
	return sock->ops->bind(sock, addr, addrlen);
}
EXPORT_SYMBOL(kernel_bind);

/**
 *	kernel_listen - move socket to listening state (kernel space)
 *	@sock: socket
 *	@backlog: pending connections queue size
 *
 *	Returns 0 or an error.
 */

int kernel_listen(struct socket *sock, int backlog)
{
	return sock->ops->listen(sock, backlog);
}
EXPORT_SYMBOL(kernel_listen);

/**
 *	kernel_accept - accept a connection (kernel space)
 *	@sock: listening socket
 *	@newsock: new connected socket
 *	@flags: flags
 *
 *	@flags must be SOCK_CLOEXEC, SOCK_NONBLOCK or 0.
 *	If it fails, @newsock is guaranteed to be %NULL.
 *	Returns 0 or an error.
 */

int kernel_accept(struct socket *sock, struct socket **newsock, int flags)
{
	struct sock *sk = sock->sk;
	int err;

	err = sock_create_lite(sk->sk_family, sk->sk_type, sk->sk_protocol,
			       newsock);
	if (err < 0)
		goto done;

	err = sock->ops->accept(sock, *newsock, flags, true);
	if (err < 0) {
		sock_release(*newsock);
		*newsock = NULL;
		goto done;
	}

	(*newsock)->ops = sock->ops;
	__module_get((*newsock)->ops->owner);

done:
	return err;
}
EXPORT_SYMBOL(kernel_accept);

/**
 *	kernel_connect - connect a socket (kernel space)
 *	@sock: socket
 *	@addr: address
 *	@addrlen: address length
 *	@flags: flags (O_NONBLOCK, ...)
 *
 *	For datagram sockets, @addr is the addres to which datagrams are sent
 *	by default, and the only address from which datagrams are received.
 *	For stream sockets, attempts to connect to @addr.
 *	Returns 0 or an error code.
 */

int kernel_connect(struct socket *sock, struct sockaddr *addr, int addrlen,
		   int flags)
{
	return sock->ops->connect(sock, addr, addrlen, flags);
}
EXPORT_SYMBOL(kernel_connect);

/**
 *	kernel_getsockname - get the address which the socket is bound (kernel space)
 *	@sock: socket
 *	@addr: address holder
 *
 * 	Fills the @addr pointer with the address which the socket is bound.
 *	Returns 0 or an error code.
 */

int kernel_getsockname(struct socket *sock, struct sockaddr *addr)
{
	return sock->ops->getname(sock, addr, 0);
}
EXPORT_SYMBOL(kernel_getsockname);

/**
 *	kernel_getpeername - get the address which the socket is connected (kernel space)
 *	@sock: socket
 *	@addr: address holder
 *
 * 	Fills the @addr pointer with the address which the socket is connected.
 *	Returns 0 or an error code.
 */

int kernel_getpeername(struct socket *sock, struct sockaddr *addr)
{
	return sock->ops->getname(sock, addr, 1);
}
EXPORT_SYMBOL(kernel_getpeername);

/**
 *	kernel_sendpage - send a &page through a socket (kernel space)
 *	@sock: socket
 *	@page: page
 *	@offset: page offset
 *	@size: total size in bytes
 *	@flags: flags (MSG_DONTWAIT, ...)
 *
 *	Returns the total amount sent in bytes or an error.
 */

int kernel_sendpage(struct socket *sock, struct page *page, int offset,
		    size_t size, int flags)
{
	if (sock->ops->sendpage) {
		/* Warn in case the improper page to zero-copy send */
		WARN_ONCE(!sendpage_ok(page), "improper page for zero-copy send");
		return sock->ops->sendpage(sock, page, offset, size, flags);
	}
	return sock_no_sendpage(sock, page, offset, size, flags);
}
EXPORT_SYMBOL(kernel_sendpage);

/**
 *	kernel_sendpage_locked - send a &page through the locked sock (kernel space)
 *	@sk: sock
 *	@page: page
 *	@offset: page offset
 *	@size: total size in bytes
 *	@flags: flags (MSG_DONTWAIT, ...)
 *
 *	Returns the total amount sent in bytes or an error.
 *	Caller must hold @sk.
 */

int kernel_sendpage_locked(struct sock *sk, struct page *page, int offset,
			   size_t size, int flags)
{
	struct socket *sock = sk->sk_socket;

	if (sock->ops->sendpage_locked)
		return sock->ops->sendpage_locked(sk, page, offset, size,
						  flags);

	return sock_no_sendpage_locked(sk, page, offset, size, flags);
}
EXPORT_SYMBOL(kernel_sendpage_locked);

/**
 *	kernel_sock_shutdown - shut down part of a full-duplex connection (kernel space)
 *	@sock: socket
 *	@how: connection part
 *
 *	Returns 0 or an error.
 */

int kernel_sock_shutdown(struct socket *sock, enum sock_shutdown_cmd how)
{
	return sock->ops->shutdown(sock, how);
}
EXPORT_SYMBOL(kernel_sock_shutdown);

/**
 *	kernel_sock_ip_overhead - returns the IP overhead imposed by a socket
 *	@sk: socket
 *
 *	This routine returns the IP overhead imposed by a socket i.e.
 *	the length of the underlying IP header, depending on whether
 *	this is an IPv4 or IPv6 socket and the length from IP options turned
 *	on at the socket. Assumes that the caller has a lock on the socket.
 */

u32 kernel_sock_ip_overhead(struct sock *sk)
{
	struct inet_sock *inet;
	struct ip_options_rcu *opt;
	u32 overhead = 0;
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6_pinfo *np;
	struct ipv6_txoptions *optv6 = NULL;
#endif /* IS_ENABLED(CONFIG_IPV6) */

	if (!sk)
		return overhead;

	switch (sk->sk_family) {
	case AF_INET:
		inet = inet_sk(sk);
		overhead += sizeof(struct iphdr);
		opt = rcu_dereference_protected(inet->inet_opt,
						sock_owned_by_user(sk));
		if (opt)
			overhead += opt->opt.optlen;
		return overhead;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		np = inet6_sk(sk);
		overhead += sizeof(struct ipv6hdr);
		if (np)
			optv6 = rcu_dereference_protected(np->opt,
							  sock_owned_by_user(sk));
		if (optv6)
			overhead += (optv6->opt_flen + optv6->opt_nflen);
		return overhead;
#endif /* IS_ENABLED(CONFIG_IPV6) */
	default: /* Returns 0 overhead if the socket is not ipv4 or ipv6 */
		return overhead;
	}
}
EXPORT_SYMBOL(kernel_sock_ip_overhead);
