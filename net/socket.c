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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
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
#include <linux/wanrouter.h>
#include <linux/if_bridge.h>
#include <linux/if_frad.h>
#include <linux/if_vlan.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/mount.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/compat.h>
#include <linux/kmod.h>
#include <linux/audit.h>
#include <linux/wireless.h>
#include <linux/nsproxy.h>
#include <linux/magic.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>

#include <net/compat.h>
#include <net/wext.h>
#include <net/cls_cgroup.h>

#include <net/sock.h>
#include <linux/netfilter.h>

#include <linux/if_tun.h>
#include <linux/ipv6_route.h>
#include <linux/route.h>
#include <linux/sockios.h>
#include <linux/atalk.h>

static int sock_no_open(struct inode *irrelevant, struct file *dontcare);
static ssize_t sock_aio_read(struct kiocb *iocb, const struct iovec *iov,
			 unsigned long nr_segs, loff_t pos);
static ssize_t sock_aio_write(struct kiocb *iocb, const struct iovec *iov,
			  unsigned long nr_segs, loff_t pos);
static int sock_mmap(struct file *file, struct vm_area_struct *vma);

static int sock_close(struct inode *inode, struct file *file);
static unsigned int sock_poll(struct file *file,
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

/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static const struct file_operations socket_file_ops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.aio_read =	sock_aio_read,
	.aio_write =	sock_aio_write,
	.poll =		sock_poll,
	.unlocked_ioctl = sock_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_sock_ioctl,
#endif
	.mmap =		sock_mmap,
	.open =		sock_no_open,	/* special open code to disallow open via /proc */
	.release =	sock_close,
	.fasync =	sock_fasync,
	.sendpage =	sock_sendpage,
	.splice_write = generic_splice_sendpage,
	.splice_read =	sock_splice_read,
};

/*
 *	The protocol list. Each protocol is registered in here.
 */

static DEFINE_SPINLOCK(net_family_lock);
static const struct net_proto_family __rcu *net_families[NPROTO] __read_mostly;

/*
 *	Statistics counters of the socket lists
 */

static DEFINE_PER_CPU(int, sockets_in_use);

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

int move_addr_to_kernel(void __user *uaddr, int ulen, struct sockaddr *kaddr)
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

static int move_addr_to_user(struct sockaddr *kaddr, int klen,
			     void __user *uaddr, int __user *ulen)
{
	int err;
	int len;

	err = get_user(len, ulen);
	if (err)
		return err;
	if (len > klen)
		len = klen;
	if (len < 0 || len > sizeof(struct sockaddr_storage))
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

static struct kmem_cache *sock_inode_cachep __read_mostly;

static struct inode *sock_alloc_inode(struct super_block *sb)
{
	struct socket_alloc *ei;
	struct socket_wq *wq;

	ei = kmem_cache_alloc(sock_inode_cachep, GFP_KERNEL);
	if (!ei)
		return NULL;
	wq = kmalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq) {
		kmem_cache_free(sock_inode_cachep, ei);
		return NULL;
	}
	init_waitqueue_head(&wq->wait);
	wq->fasync_list = NULL;
	RCU_INIT_POINTER(ei->socket.wq, wq);

	ei->socket.state = SS_UNCONNECTED;
	ei->socket.flags = 0;
	ei->socket.ops = NULL;
	ei->socket.sk = NULL;
	ei->socket.file = NULL;

	return &ei->vfs_inode;
}

static void sock_destroy_inode(struct inode *inode)
{
	struct socket_alloc *ei;
	struct socket_wq *wq;

	ei = container_of(inode, struct socket_alloc, vfs_inode);
	wq = rcu_dereference_protected(ei->socket.wq, 1);
	kfree_rcu(wq, rcu);
	kmem_cache_free(sock_inode_cachep, ei);
}

static void init_once(void *foo)
{
	struct socket_alloc *ei = (struct socket_alloc *)foo;

	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	sock_inode_cachep = kmem_cache_create("sock_inode_cache",
					      sizeof(struct socket_alloc),
					      0,
					      (SLAB_HWCACHE_ALIGN |
					       SLAB_RECLAIM_ACCOUNT |
					       SLAB_MEM_SPREAD),
					      init_once);
	if (sock_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static const struct super_operations sockfs_ops = {
	.alloc_inode	= sock_alloc_inode,
	.destroy_inode	= sock_destroy_inode,
	.statfs		= simple_statfs,
};

/*
 * sockfs_dname() is called from d_path().
 */
static char *sockfs_dname(struct dentry *dentry, char *buffer, int buflen)
{
	return dynamic_dname(dentry, buffer, buflen, "socket:[%lu]",
				dentry->d_inode->i_ino);
}

static const struct dentry_operations sockfs_dentry_operations = {
	.d_dname  = sockfs_dname,
};

static struct dentry *sockfs_mount(struct file_system_type *fs_type,
			 int flags, const char *dev_name, void *data)
{
	return mount_pseudo(fs_type, "socket:", &sockfs_ops,
		&sockfs_dentry_operations, SOCKFS_MAGIC);
}

static struct vfsmount *sock_mnt __read_mostly;

static struct file_system_type sock_fs_type = {
	.name =		"sockfs",
	.mount =	sockfs_mount,
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

static int sock_alloc_file(struct socket *sock, struct file **f, int flags)
{
	struct qstr name = { .name = "" };
	struct path path;
	struct file *file;
	int fd;

	fd = get_unused_fd_flags(flags);
	if (unlikely(fd < 0))
		return fd;

	path.dentry = d_alloc_pseudo(sock_mnt->mnt_sb, &name);
	if (unlikely(!path.dentry)) {
		put_unused_fd(fd);
		return -ENOMEM;
	}
	path.mnt = mntget(sock_mnt);

	d_instantiate(path.dentry, SOCK_INODE(sock));
	SOCK_INODE(sock)->i_fop = &socket_file_ops;

	file = alloc_file(&path, FMODE_READ | FMODE_WRITE,
		  &socket_file_ops);
	if (unlikely(!file)) {
		/* drop dentry, keep inode */
		ihold(path.dentry->d_inode);
		path_put(&path);
		put_unused_fd(fd);
		return -ENFILE;
	}

	sock->file = file;
	file->f_flags = O_RDWR | (flags & O_NONBLOCK);
	file->f_pos = 0;
	file->private_data = sock;

	*f = file;
	return fd;
}

int sock_map_fd(struct socket *sock, int flags)
{
	struct file *newfile;
	int fd = sock_alloc_file(sock, &newfile, flags);

	if (likely(fd >= 0))
		fd_install(fd, newfile);

	return fd;
}
EXPORT_SYMBOL(sock_map_fd);

static struct socket *sock_from_file(struct file *file, int *err)
{
	if (file->f_op == &socket_file_ops)
		return file->private_data;	/* set in sock_map_fd */

	*err = -ENOTSOCK;
	return NULL;
}

/**
 *	sockfd_lookup - Go from a file number to its socket slot
 *	@fd: file handle
 *	@err: pointer to an error code return
 *
 *	The file handle passed in is locked and the socket it is bound
 *	too is returned. If an error occurs the err pointer is overwritten
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
	struct file *file;
	struct socket *sock;

	*err = -EBADF;
	file = fget_light(fd, fput_needed);
	if (file) {
		sock = sock_from_file(file, err);
		if (sock)
			return sock;
		fput_light(file, *fput_needed);
	}
	return NULL;
}

/**
 *	sock_alloc	-	allocate a socket
 *
 *	Allocate a new inode and socket object. The two are bound together
 *	and initialised. The socket is then returned. If we are out of inodes
 *	NULL is returned.
 */

static struct socket *sock_alloc(void)
{
	struct inode *inode;
	struct socket *sock;

	inode = new_inode_pseudo(sock_mnt->mnt_sb);
	if (!inode)
		return NULL;

	sock = SOCKET_I(inode);

	kmemcheck_annotate_bitfield(sock, type);
	inode->i_ino = get_next_ino();
	inode->i_mode = S_IFSOCK | S_IRWXUGO;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();

	percpu_add(sockets_in_use, 1);
	return sock;
}

/*
 *	In theory you can't get an open on this inode, but /proc provides
 *	a back door. Remember to keep it shut otherwise you'll let the
 *	creepy crawlies in.
 */

static int sock_no_open(struct inode *irrelevant, struct file *dontcare)
{
	return -ENXIO;
}

const struct file_operations bad_sock_fops = {
	.owner = THIS_MODULE,
	.open = sock_no_open,
	.llseek = noop_llseek,
};

/**
 *	sock_release	-	close a socket
 *	@sock: socket to close
 *
 *	The socket is released from the protocol stack if it has a release
 *	callback, and the inode is then released if the socket is bound to
 *	an inode not a file.
 */

void sock_release(struct socket *sock)
{
	if (sock->ops) {
		struct module *owner = sock->ops->owner;

		sock->ops->release(sock);
		sock->ops = NULL;
		module_put(owner);
	}

	if (rcu_dereference_protected(sock->wq, 1)->fasync_list)
		printk(KERN_ERR "sock_release: fasync list not empty!\n");

	percpu_sub(sockets_in_use, 1);
	if (!sock->file) {
		iput(SOCK_INODE(sock));
		return;
	}
	sock->file = NULL;
}
EXPORT_SYMBOL(sock_release);

int sock_tx_timestamp(struct sock *sk, __u8 *tx_flags)
{
	*tx_flags = 0;
	if (sock_flag(sk, SOCK_TIMESTAMPING_TX_HARDWARE))
		*tx_flags |= SKBTX_HW_TSTAMP;
	if (sock_flag(sk, SOCK_TIMESTAMPING_TX_SOFTWARE))
		*tx_flags |= SKBTX_SW_TSTAMP;
	return 0;
}
EXPORT_SYMBOL(sock_tx_timestamp);

static inline int __sock_sendmsg_nosec(struct kiocb *iocb, struct socket *sock,
				       struct msghdr *msg, size_t size)
{
	struct sock_iocb *si = kiocb_to_siocb(iocb);

	sock_update_classid(sock->sk);

	si->sock = sock;
	si->scm = NULL;
	si->msg = msg;
	si->size = size;

	return sock->ops->sendmsg(iocb, sock, msg, size);
}

static inline int __sock_sendmsg(struct kiocb *iocb, struct socket *sock,
				 struct msghdr *msg, size_t size)
{
	int err = security_socket_sendmsg(sock, msg, size);

	return err ?: __sock_sendmsg_nosec(iocb, sock, msg, size);
}

int sock_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_sendmsg(&iocb, sock, msg, size);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}
EXPORT_SYMBOL(sock_sendmsg);

static int sock_sendmsg_nosec(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_sendmsg_nosec(&iocb, sock, msg, size);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}

int kernel_sendmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);
	/*
	 * the following is safe, since for compiler definitions of kvec and
	 * iovec are identical, yielding the same in-core layout and alignment
	 */
	msg->msg_iov = (struct iovec *)vec;
	msg->msg_iovlen = num;
	result = sock_sendmsg(sock, msg, size);
	set_fs(oldfs);
	return result;
}
EXPORT_SYMBOL(kernel_sendmsg);

static int ktime2ts(ktime_t kt, struct timespec *ts)
{
	if (kt.tv64) {
		*ts = ktime_to_timespec(kt);
		return 1;
	} else {
		return 0;
	}
}

/*
 * called from sock_recv_timestamp() if sock_flag(sk, SOCK_RCVTSTAMP)
 */
void __sock_recv_timestamp(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	int need_software_tstamp = sock_flag(sk, SOCK_RCVTSTAMP);
	struct timespec ts[3];
	int empty = 1;
	struct skb_shared_hwtstamps *shhwtstamps =
		skb_hwtstamps(skb);

	/* Race occurred between timestamp enabling and packet
	   receiving.  Fill in the current time for now. */
	if (need_software_tstamp && skb->tstamp.tv64 == 0)
		__net_timestamp(skb);

	if (need_software_tstamp) {
		if (!sock_flag(sk, SOCK_RCVTSTAMPNS)) {
			struct timeval tv;
			skb_get_timestamp(skb, &tv);
			put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMP,
				 sizeof(tv), &tv);
		} else {
			skb_get_timestampns(skb, &ts[0]);
			put_cmsg(msg, SOL_SOCKET, SCM_TIMESTAMPNS,
				 sizeof(ts[0]), &ts[0]);
		}
	}


	memset(ts, 0, sizeof(ts));
	if (skb->tstamp.tv64 &&
	    sock_flag(sk, SOCK_TIMESTAMPING_SOFTWARE)) {
		skb_get_timestampns(skb, ts + 0);
		empty = 0;
	}
	if (shhwtstamps) {
		if (sock_flag(sk, SOCK_TIMESTAMPING_SYS_HARDWARE) &&
		    ktime2ts(shhwtstamps->syststamp, ts + 1))
			empty = 0;
		if (sock_flag(sk, SOCK_TIMESTAMPING_RAW_HARDWARE) &&
		    ktime2ts(shhwtstamps->hwtstamp, ts + 2))
			empty = 0;
	}
	if (!empty)
		put_cmsg(msg, SOL_SOCKET,
			 SCM_TIMESTAMPING, sizeof(ts), &ts);
}
EXPORT_SYMBOL_GPL(__sock_recv_timestamp);

static inline void sock_recv_drops(struct msghdr *msg, struct sock *sk,
				   struct sk_buff *skb)
{
	if (sock_flag(sk, SOCK_RXQ_OVFL) && skb && skb->dropcount)
		put_cmsg(msg, SOL_SOCKET, SO_RXQ_OVFL,
			sizeof(__u32), &skb->dropcount);
}

void __sock_recv_ts_and_drops(struct msghdr *msg, struct sock *sk,
	struct sk_buff *skb)
{
	sock_recv_timestamp(msg, sk, skb);
	sock_recv_drops(msg, sk, skb);
}
EXPORT_SYMBOL_GPL(__sock_recv_ts_and_drops);

static inline int __sock_recvmsg_nosec(struct kiocb *iocb, struct socket *sock,
				       struct msghdr *msg, size_t size, int flags)
{
	struct sock_iocb *si = kiocb_to_siocb(iocb);

	sock_update_classid(sock->sk);

	si->sock = sock;
	si->scm = NULL;
	si->msg = msg;
	si->size = size;
	si->flags = flags;

	return sock->ops->recvmsg(iocb, sock, msg, size, flags);
}

static inline int __sock_recvmsg(struct kiocb *iocb, struct socket *sock,
				 struct msghdr *msg, size_t size, int flags)
{
	int err = security_socket_recvmsg(sock, msg, size, flags);

	return err ?: __sock_recvmsg_nosec(iocb, sock, msg, size, flags);
}

int sock_recvmsg(struct socket *sock, struct msghdr *msg,
		 size_t size, int flags)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_recvmsg(&iocb, sock, msg, size, flags);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}
EXPORT_SYMBOL(sock_recvmsg);

static int sock_recvmsg_nosec(struct socket *sock, struct msghdr *msg,
			      size_t size, int flags)
{
	struct kiocb iocb;
	struct sock_iocb siocb;
	int ret;

	init_sync_kiocb(&iocb, NULL);
	iocb.private = &siocb;
	ret = __sock_recvmsg_nosec(&iocb, sock, msg, size, flags);
	if (-EIOCBQUEUED == ret)
		ret = wait_on_sync_kiocb(&iocb);
	return ret;
}

/**
 * kernel_recvmsg - Receive a message from a socket (kernel space)
 * @sock:       The socket to receive the message from
 * @msg:        Received message
 * @vec:        Input s/g array for message data
 * @num:        Size of input s/g array
 * @size:       Number of bytes to read
 * @flags:      Message flags (MSG_DONTWAIT, etc...)
 *
 * On return the msg structure contains the scatter/gather array passed in the
 * vec argument. The array is modified so that it consists of the unfilled
 * portion of the original array.
 *
 * The returned value is the total number of bytes received, or an error.
 */
int kernel_recvmsg(struct socket *sock, struct msghdr *msg,
		   struct kvec *vec, size_t num, size_t size, int flags)
{
	mm_segment_t oldfs = get_fs();
	int result;

	set_fs(KERNEL_DS);
	/*
	 * the following is safe, since for compiler definitions of kvec and
	 * iovec are identical, yielding the same in-core layout and alignment
	 */
	msg->msg_iov = (struct iovec *)vec, msg->msg_iovlen = num;
	result = sock_recvmsg(sock, msg, size, flags);
	set_fs(oldfs);
	return result;
}
EXPORT_SYMBOL(kernel_recvmsg);

static void sock_aio_dtor(struct kiocb *iocb)
{
	kfree(iocb->private);
}

static ssize_t sock_sendpage(struct file *file, struct page *page,
			     int offset, size_t size, loff_t *ppos, int more)
{
	struct socket *sock;
	int flags;

	sock = file->private_data;

	flags = !(file->f_flags & O_NONBLOCK) ? 0 : MSG_DONTWAIT;
	if (more)
		flags |= MSG_MORE;

	return kernel_sendpage(sock, page, offset, size, flags);
}

static ssize_t sock_splice_read(struct file *file, loff_t *ppos,
				struct pipe_inode_info *pipe, size_t len,
				unsigned int flags)
{
	struct socket *sock = file->private_data;

	if (unlikely(!sock->ops->splice_read))
		return -EINVAL;

	sock_update_classid(sock->sk);

	return sock->ops->splice_read(sock, ppos, pipe, len, flags);
}

static struct sock_iocb *alloc_sock_iocb(struct kiocb *iocb,
					 struct sock_iocb *siocb)
{
	if (!is_sync_kiocb(iocb)) {
		siocb = kmalloc(sizeof(*siocb), GFP_KERNEL);
		if (!siocb)
			return NULL;
		iocb->ki_dtor = sock_aio_dtor;
	}

	siocb->kiocb = iocb;
	iocb->private = siocb;
	return siocb;
}

static ssize_t do_sock_read(struct msghdr *msg, struct kiocb *iocb,
		struct file *file, const struct iovec *iov,
		unsigned long nr_segs)
{
	struct socket *sock = file->private_data;
	size_t size = 0;
	int i;

	for (i = 0; i < nr_segs; i++)
		size += iov[i].iov_len;

	msg->msg_name = NULL;
	msg->msg_namelen = 0;
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_iov = (struct iovec *)iov;
	msg->msg_iovlen = nr_segs;
	msg->msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;

	return __sock_recvmsg(iocb, sock, msg, size, msg->msg_flags);
}

static ssize_t sock_aio_read(struct kiocb *iocb, const struct iovec *iov,
				unsigned long nr_segs, loff_t pos)
{
	struct sock_iocb siocb, *x;

	if (pos != 0)
		return -ESPIPE;

	if (iocb->ki_left == 0)	/* Match SYS5 behaviour */
		return 0;


	x = alloc_sock_iocb(iocb, &siocb);
	if (!x)
		return -ENOMEM;
	return do_sock_read(&x->async_msg, iocb, iocb->ki_filp, iov, nr_segs);
}

static ssize_t do_sock_write(struct msghdr *msg, struct kiocb *iocb,
			struct file *file, const struct iovec *iov,
			unsigned long nr_segs)
{
	struct socket *sock = file->private_data;
	size_t size = 0;
	int i;

	for (i = 0; i < nr_segs; i++)
		size += iov[i].iov_len;

	msg->msg_name = NULL;
	msg->msg_namelen = 0;
	msg->msg_control = NULL;
	msg->msg_controllen = 0;
	msg->msg_iov = (struct iovec *)iov;
	msg->msg_iovlen = nr_segs;
	msg->msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;
	if (sock->type == SOCK_SEQPACKET)
		msg->msg_flags |= MSG_EOR;

	return __sock_sendmsg(iocb, sock, msg, size);
}

static ssize_t sock_aio_write(struct kiocb *iocb, const struct iovec *iov,
			  unsigned long nr_segs, loff_t pos)
{
	struct sock_iocb siocb, *x;

	if (pos != 0)
		return -ESPIPE;

	x = alloc_sock_iocb(iocb, &siocb);
	if (!x)
		return -ENOMEM;

	return do_sock_write(&x->async_msg, iocb, iocb->ki_filp, iov, nr_segs);
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
	if (err == -ENOIOCTLCMD)
		err = dev_ioctl(net, cmd, argp);

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
	if (cmd >= SIOCDEVPRIVATE && cmd <= (SIOCDEVPRIVATE + 15)) {
		err = dev_ioctl(net, cmd, argp);
	} else
#ifdef CONFIG_WEXT_CORE
	if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
		err = dev_ioctl(net, cmd, argp);
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
		default:
			err = sock_do_ioctl(net, sock, cmd, arg);
			break;
		}
	return err;
}

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
static unsigned int sock_poll(struct file *file, poll_table *wait)
{
	struct socket *sock;

	/*
	 *      We can't return errors to poll, so it's either yes or no.
	 */
	sock = file->private_data;
	return sock->ops->poll(file, sock, wait);
}

static int sock_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct socket *sock = file->private_data;

	return sock->ops->mmap(file, sock, vma);
}

static int sock_close(struct inode *inode, struct file *filp)
{
	/*
	 *      It was possible the inode is NULL we were
	 *      closing an unfinished socket.
	 */

	if (!inode) {
		printk(KERN_DEBUG "sock_close: NULL inode\n");
		return 0;
	}
	sock_release(SOCKET_I(inode));
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
	struct socket_wq *wq;

	if (sk == NULL)
		return -EINVAL;

	lock_sock(sk);
	wq = rcu_dereference_protected(sock->wq, sock_owned_by_user(sk));
	fasync_helper(fd, filp, on, &wq->fasync_list);

	if (!wq->fasync_list)
		sock_reset_flag(sk, SOCK_FASYNC);
	else
		sock_set_flag(sk, SOCK_FASYNC);

	release_sock(sk);
	return 0;
}

/* This function may be called only under socket lock or callback_lock or rcu_lock */

int sock_wake_async(struct socket *sock, int how, int band)
{
	struct socket_wq *wq;

	if (!sock)
		return -1;
	rcu_read_lock();
	wq = rcu_dereference(sock->wq);
	if (!wq || !wq->fasync_list) {
		rcu_read_unlock();
		return -1;
	}
	switch (how) {
	case SOCK_WAKE_WAITD:
		if (test_bit(SOCK_ASYNC_WAITDATA, &sock->flags))
			break;
		goto call_kill;
	case SOCK_WAKE_SPACE:
		if (!test_and_clear_bit(SOCK_ASYNC_NOSPACE, &sock->flags))
			break;
		/* fall through */
	case SOCK_WAKE_IO:
call_kill:
		kill_fasync(&wq->fasync_list, SIGIO, band);
		break;
	case SOCK_WAKE_URG:
		kill_fasync(&wq->fasync_list, SIGURG, band);
	}
	rcu_read_unlock();
	return 0;
}
EXPORT_SYMBOL(sock_wake_async);

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
		static int warned;
		if (!warned) {
			warned = 1;
			printk(KERN_INFO "%s uses obsolete (PF_INET,SOCK_PACKET)\n",
			       current->comm);
		}
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
		if (net_ratelimit())
			printk(KERN_WARNING "socket: no more sockets\n");
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

int sock_create(int family, int type, int protocol, struct socket **res)
{
	return __sock_create(current->nsproxy->net_ns, family, type, protocol, res, 0);
}
EXPORT_SYMBOL(sock_create);

int sock_create_kern(int family, int type, int protocol, struct socket **res)
{
	return __sock_create(&init_net, family, type, protocol, res, 1);
}
EXPORT_SYMBOL(sock_create_kern);

SYSCALL_DEFINE3(socket, int, family, int, type, int, protocol)
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
		goto out;

	retval = sock_map_fd(sock, flags & (O_CLOEXEC | O_NONBLOCK));
	if (retval < 0)
		goto out_release;

out:
	/* It may be already another descriptor 8) Not kernel problem. */
	return retval;

out_release:
	sock_release(sock);
	return retval;
}

/*
 *	Create a pair of connected sockets.
 */

SYSCALL_DEFINE4(socketpair, int, family, int, type, int, protocol,
		int __user *, usockvec)
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
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	err = sock_create(family, type, protocol, &sock1);
	if (err < 0)
		goto out;

	err = sock_create(family, type, protocol, &sock2);
	if (err < 0)
		goto out_release_1;

	err = sock1->ops->socketpair(sock1, sock2);
	if (err < 0)
		goto out_release_both;

	fd1 = sock_alloc_file(sock1, &newfile1, flags);
	if (unlikely(fd1 < 0)) {
		err = fd1;
		goto out_release_both;
	}

	fd2 = sock_alloc_file(sock2, &newfile2, flags);
	if (unlikely(fd2 < 0)) {
		err = fd2;
		fput(newfile1);
		put_unused_fd(fd1);
		sock_release(sock2);
		goto out;
	}

	audit_fd_pair(fd1, fd2);
	fd_install(fd1, newfile1);
	fd_install(fd2, newfile2);
	/* fd1 and fd2 may be already another descriptors.
	 * Not kernel problem.
	 */

	err = put_user(fd1, &usockvec[0]);
	if (!err)
		err = put_user(fd2, &usockvec[1]);
	if (!err)
		return 0;

	sys_close(fd2);
	sys_close(fd1);
	return err;

out_release_both:
	sock_release(sock2);
out_release_1:
	sock_release(sock1);
out:
	return err;
}

/*
 *	Bind a name to a socket. Nothing much to do here since it's
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */

SYSCALL_DEFINE3(bind, int, fd, struct sockaddr __user *, umyaddr, int, addrlen)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		err = move_addr_to_kernel(umyaddr, addrlen, (struct sockaddr *)&address);
		if (err >= 0) {
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

/*
 *	Perform a listen. Basically, we allow the protocol to do anything
 *	necessary for a listen, and if that works, we mark the socket as
 *	ready for listening.
 */

SYSCALL_DEFINE2(listen, int, fd, int, backlog)
{
	struct socket *sock;
	int err, fput_needed;
	int somaxconn;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock) {
		somaxconn = sock_net(sock->sk)->core.sysctl_somaxconn;
		if ((unsigned)backlog > somaxconn)
			backlog = somaxconn;

		err = security_socket_listen(sock, backlog);
		if (!err)
			err = sock->ops->listen(sock, backlog);

		fput_light(sock->file, fput_needed);
	}
	return err;
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
 *	clean when we restucture accept also.
 */

SYSCALL_DEFINE4(accept4, int, fd, struct sockaddr __user *, upeer_sockaddr,
		int __user *, upeer_addrlen, int, flags)
{
	struct socket *sock, *newsock;
	struct file *newfile;
	int err, len, newfd, fput_needed;
	struct sockaddr_storage address;

	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;

	if (SOCK_NONBLOCK != O_NONBLOCK && (flags & SOCK_NONBLOCK))
		flags = (flags & ~SOCK_NONBLOCK) | O_NONBLOCK;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	err = -ENFILE;
	newsock = sock_alloc();
	if (!newsock)
		goto out_put;

	newsock->type = sock->type;
	newsock->ops = sock->ops;

	/*
	 * We don't need try_module_get here, as the listening socket (sock)
	 * has the protocol module (sock->ops->owner) held.
	 */
	__module_get(newsock->ops->owner);

	newfd = sock_alloc_file(newsock, &newfile, flags);
	if (unlikely(newfd < 0)) {
		err = newfd;
		sock_release(newsock);
		goto out_put;
	}

	err = security_socket_accept(sock, newsock);
	if (err)
		goto out_fd;

	err = sock->ops->accept(sock, newsock, sock->file->f_flags);
	if (err < 0)
		goto out_fd;

	if (upeer_sockaddr) {
		if (newsock->ops->getname(newsock, (struct sockaddr *)&address,
					  &len, 2) < 0) {
			err = -ECONNABORTED;
			goto out_fd;
		}
		err = move_addr_to_user((struct sockaddr *)&address,
					len, upeer_sockaddr, upeer_addrlen);
		if (err < 0)
			goto out_fd;
	}

	/* File flags are not inherited via accept() unlike another OSes. */

	fd_install(newfd, newfile);
	err = newfd;

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
out_fd:
	fput(newfile);
	put_unused_fd(newfd);
	goto out_put;
}

SYSCALL_DEFINE3(accept, int, fd, struct sockaddr __user *, upeer_sockaddr,
		int __user *, upeer_addrlen)
{
	return sys_accept4(fd, upeer_sockaddr, upeer_addrlen, 0);
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

SYSCALL_DEFINE3(connect, int, fd, struct sockaddr __user *, uservaddr,
		int, addrlen)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;
	err = move_addr_to_kernel(uservaddr, addrlen, (struct sockaddr *)&address);
	if (err < 0)
		goto out_put;

	err =
	    security_socket_connect(sock, (struct sockaddr *)&address, addrlen);
	if (err)
		goto out_put;

	err = sock->ops->connect(sock, (struct sockaddr *)&address, addrlen,
				 sock->file->f_flags);
out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *	Get the local address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

SYSCALL_DEFINE3(getsockname, int, fd, struct sockaddr __user *, usockaddr,
		int __user *, usockaddr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int len, err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	err = security_socket_getsockname(sock);
	if (err)
		goto out_put;

	err = sock->ops->getname(sock, (struct sockaddr *)&address, &len, 0);
	if (err)
		goto out_put;
	err = move_addr_to_user((struct sockaddr *)&address, len, usockaddr, usockaddr_len);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *	Get the remote address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

SYSCALL_DEFINE3(getpeername, int, fd, struct sockaddr __user *, usockaddr,
		int __user *, usockaddr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int len, err, fput_needed;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = security_socket_getpeername(sock);
		if (err) {
			fput_light(sock->file, fput_needed);
			return err;
		}

		err =
		    sock->ops->getname(sock, (struct sockaddr *)&address, &len,
				       1);
		if (!err)
			err = move_addr_to_user((struct sockaddr *)&address, len, usockaddr,
						usockaddr_len);
		fput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */

SYSCALL_DEFINE6(sendto, int, fd, void __user *, buff, size_t, len,
		unsigned, flags, struct sockaddr __user *, addr,
		int, addr_len)
{
	struct socket *sock;
	struct sockaddr_storage address;
	int err;
	struct msghdr msg;
	struct iovec iov;
	int fput_needed;

	if (len > INT_MAX)
		len = INT_MAX;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	iov.iov_base = buff;
	iov.iov_len = len;
	msg.msg_name = NULL;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_namelen = 0;
	if (addr) {
		err = move_addr_to_kernel(addr, addr_len, (struct sockaddr *)&address);
		if (err < 0)
			goto out_put;
		msg.msg_name = (struct sockaddr *)&address;
		msg.msg_namelen = addr_len;
	}
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags = flags;
	err = sock_sendmsg(sock, &msg, len);

out_put:
	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *	Send a datagram down a socket.
 */

SYSCALL_DEFINE4(send, int, fd, void __user *, buff, size_t, len,
		unsigned, flags)
{
	return sys_sendto(fd, buff, len, flags, NULL, 0);
}

/*
 *	Receive a frame from the socket and optionally record the address of the
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */

SYSCALL_DEFINE6(recvfrom, int, fd, void __user *, ubuf, size_t, size,
		unsigned, flags, struct sockaddr __user *, addr,
		int __user *, addr_len)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	struct sockaddr_storage address;
	int err, err2;
	int fput_needed;

	if (size > INT_MAX)
		size = INT_MAX;
	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		goto out;

	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iovlen = 1;
	msg.msg_iov = &iov;
	iov.iov_len = size;
	iov.iov_base = ubuf;
	msg.msg_name = (struct sockaddr *)&address;
	msg.msg_namelen = sizeof(address);
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg(sock, &msg, size, flags);

	if (err >= 0 && addr != NULL) {
		err2 = move_addr_to_user((struct sockaddr *)&address,
					 msg.msg_namelen, addr, addr_len);
		if (err2 < 0)
			err = err2;
	}

	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *	Receive a datagram from a socket.
 */

asmlinkage long sys_recv(int fd, void __user *ubuf, size_t size,
			 unsigned flags)
{
	return sys_recvfrom(fd, ubuf, size, flags, NULL, NULL);
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */

SYSCALL_DEFINE5(setsockopt, int, fd, int, level, int, optname,
		char __user *, optval, int, optlen)
{
	int err, fput_needed;
	struct socket *sock;

	if (optlen < 0)
		return -EINVAL;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = security_socket_setsockopt(sock, level, optname);
		if (err)
			goto out_put;

		if (level == SOL_SOCKET)
			err =
			    sock_setsockopt(sock, level, optname, optval,
					    optlen);
		else
			err =
			    sock->ops->setsockopt(sock, level, optname, optval,
						  optlen);
out_put:
		fput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	Get a socket option. Because we don't know the option lengths we have
 *	to pass a user mode parameter for the protocols to sort out.
 */

SYSCALL_DEFINE5(getsockopt, int, fd, int, level, int, optname,
		char __user *, optval, int __user *, optlen)
{
	int err, fput_needed;
	struct socket *sock;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = security_socket_getsockopt(sock, level, optname);
		if (err)
			goto out_put;

		if (level == SOL_SOCKET)
			err =
			    sock_getsockopt(sock, level, optname, optval,
					    optlen);
		else
			err =
			    sock->ops->getsockopt(sock, level, optname, optval,
						  optlen);
out_put:
		fput_light(sock->file, fput_needed);
	}
	return err;
}

/*
 *	Shutdown a socket.
 */

SYSCALL_DEFINE2(shutdown, int, fd, int, how)
{
	int err, fput_needed;
	struct socket *sock;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (sock != NULL) {
		err = security_socket_shutdown(sock, how);
		if (!err)
			err = sock->ops->shutdown(sock, how);
		fput_light(sock->file, fput_needed);
	}
	return err;
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

static int __sys_sendmsg(struct socket *sock, struct msghdr __user *msg,
			 struct msghdr *msg_sys, unsigned flags,
			 struct used_address *used_address)
{
	struct compat_msghdr __user *msg_compat =
	    (struct compat_msghdr __user *)msg;
	struct sockaddr_storage address;
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	unsigned char ctl[sizeof(struct cmsghdr) + 20]
	    __attribute__ ((aligned(sizeof(__kernel_size_t))));
	/* 20 is size of ipv6_pktinfo */
	unsigned char *ctl_buf = ctl;
	int err, ctl_len, iov_size, total_len;

	err = -EFAULT;
	if (MSG_CMSG_COMPAT & flags) {
		if (get_compat_msghdr(msg_sys, msg_compat))
			return -EFAULT;
	} else if (copy_from_user(msg_sys, msg, sizeof(struct msghdr)))
		return -EFAULT;

	/* do not move before msg_sys is valid */
	err = -EMSGSIZE;
	if (msg_sys->msg_iovlen > UIO_MAXIOV)
		goto out;

	/* Check whether to allocate the iovec area */
	err = -ENOMEM;
	iov_size = msg_sys->msg_iovlen * sizeof(struct iovec);
	if (msg_sys->msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out;
	}

	/* This will also move the address data into kernel space */
	if (MSG_CMSG_COMPAT & flags) {
		err = verify_compat_iovec(msg_sys, iov,
					  (struct sockaddr *)&address,
					  VERIFY_READ);
	} else
		err = verify_iovec(msg_sys, iov,
				   (struct sockaddr *)&address,
				   VERIFY_READ);
	if (err < 0)
		goto out_freeiov;
	total_len = err;

	err = -ENOBUFS;

	if (msg_sys->msg_controllen > INT_MAX)
		goto out_freeiov;
	ctl_len = msg_sys->msg_controllen;
	if ((MSG_CMSG_COMPAT & flags) && ctl_len) {
		err =
		    cmsghdr_from_user_compat_to_kern(msg_sys, sock->sk, ctl,
						     sizeof(ctl));
		if (err)
			goto out_freeiov;
		ctl_buf = msg_sys->msg_control;
		ctl_len = msg_sys->msg_controllen;
	} else if (ctl_len) {
		if (ctl_len > sizeof(ctl)) {
			ctl_buf = sock_kmalloc(sock->sk, ctl_len, GFP_KERNEL);
			if (ctl_buf == NULL)
				goto out_freeiov;
		}
		err = -EFAULT;
		/*
		 * Careful! Before this, msg_sys->msg_control contains a user pointer.
		 * Afterwards, it will be a kernel pointer. Thus the compiler-assisted
		 * checking falls down on this.
		 */
		if (copy_from_user(ctl_buf,
				   (void __user __force *)msg_sys->msg_control,
				   ctl_len))
			goto out_freectl;
		msg_sys->msg_control = ctl_buf;
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
		err = sock_sendmsg_nosec(sock, msg_sys, total_len);
		goto out_freectl;
	}
	err = sock_sendmsg(sock, msg_sys, total_len);
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
out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out:
	return err;
}

/*
 *	BSD sendmsg interface
 */

SYSCALL_DEFINE3(sendmsg, int, fd, struct msghdr __user *, msg, unsigned, flags)
{
	int fput_needed, err;
	struct msghdr msg_sys;
	struct socket *sock = sockfd_lookup_light(fd, &err, &fput_needed);

	if (!sock)
		goto out;

	err = __sys_sendmsg(sock, msg, &msg_sys, flags, NULL);

	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *	Linux sendmmsg interface
 */

int __sys_sendmmsg(int fd, struct mmsghdr __user *mmsg, unsigned int vlen,
		   unsigned int flags)
{
	int fput_needed, err, datagrams;
	struct socket *sock;
	struct mmsghdr __user *entry;
	struct compat_mmsghdr __user *compat_entry;
	struct msghdr msg_sys;
	struct used_address used_address;

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

	while (datagrams < vlen) {
		if (MSG_CMSG_COMPAT & flags) {
			err = __sys_sendmsg(sock, (struct msghdr __user *)compat_entry,
					    &msg_sys, flags, &used_address);
			if (err < 0)
				break;
			err = __put_user(err, &compat_entry->msg_len);
			++compat_entry;
		} else {
			err = __sys_sendmsg(sock, (struct msghdr __user *)entry,
					    &msg_sys, flags, &used_address);
			if (err < 0)
				break;
			err = put_user(err, &entry->msg_len);
			++entry;
		}

		if (err)
			break;
		++datagrams;
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
	return __sys_sendmmsg(fd, mmsg, vlen, flags);
}

static int __sys_recvmsg(struct socket *sock, struct msghdr __user *msg,
			 struct msghdr *msg_sys, unsigned flags, int nosec)
{
	struct compat_msghdr __user *msg_compat =
	    (struct compat_msghdr __user *)msg;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	unsigned long cmsg_ptr;
	int err, iov_size, total_len, len;

	/* kernel mode address */
	struct sockaddr_storage addr;

	/* user mode address pointers */
	struct sockaddr __user *uaddr;
	int __user *uaddr_len;

	if (MSG_CMSG_COMPAT & flags) {
		if (get_compat_msghdr(msg_sys, msg_compat))
			return -EFAULT;
	} else if (copy_from_user(msg_sys, msg, sizeof(struct msghdr)))
		return -EFAULT;

	err = -EMSGSIZE;
	if (msg_sys->msg_iovlen > UIO_MAXIOV)
		goto out;

	/* Check whether to allocate the iovec area */
	err = -ENOMEM;
	iov_size = msg_sys->msg_iovlen * sizeof(struct iovec);
	if (msg_sys->msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out;
	}

	/*
	 *      Save the user-mode address (verify_iovec will change the
	 *      kernel msghdr to use the kernel address space)
	 */

	uaddr = (__force void __user *)msg_sys->msg_name;
	uaddr_len = COMPAT_NAMELEN(msg);
	if (MSG_CMSG_COMPAT & flags) {
		err = verify_compat_iovec(msg_sys, iov,
					  (struct sockaddr *)&addr,
					  VERIFY_WRITE);
	} else
		err = verify_iovec(msg_sys, iov,
				   (struct sockaddr *)&addr,
				   VERIFY_WRITE);
	if (err < 0)
		goto out_freeiov;
	total_len = err;

	cmsg_ptr = (unsigned long)msg_sys->msg_control;
	msg_sys->msg_flags = flags & (MSG_CMSG_CLOEXEC|MSG_CMSG_COMPAT);

	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = (nosec ? sock_recvmsg_nosec : sock_recvmsg)(sock, msg_sys,
							  total_len, flags);
	if (err < 0)
		goto out_freeiov;
	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user((struct sockaddr *)&addr,
					msg_sys->msg_namelen, uaddr,
					uaddr_len);
		if (err < 0)
			goto out_freeiov;
	}
	err = __put_user((msg_sys->msg_flags & ~MSG_CMSG_COMPAT),
			 COMPAT_FLAGS(msg));
	if (err)
		goto out_freeiov;
	if (MSG_CMSG_COMPAT & flags)
		err = __put_user((unsigned long)msg_sys->msg_control - cmsg_ptr,
				 &msg_compat->msg_controllen);
	else
		err = __put_user((unsigned long)msg_sys->msg_control - cmsg_ptr,
				 &msg->msg_controllen);
	if (err)
		goto out_freeiov;
	err = len;

out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out:
	return err;
}

/*
 *	BSD recvmsg interface
 */

SYSCALL_DEFINE3(recvmsg, int, fd, struct msghdr __user *, msg,
		unsigned int, flags)
{
	int fput_needed, err;
	struct msghdr msg_sys;
	struct socket *sock = sockfd_lookup_light(fd, &err, &fput_needed);

	if (!sock)
		goto out;

	err = __sys_recvmsg(sock, msg, &msg_sys, flags, 0);

	fput_light(sock->file, fput_needed);
out:
	return err;
}

/*
 *     Linux recvmmsg interface
 */

int __sys_recvmmsg(int fd, struct mmsghdr __user *mmsg, unsigned int vlen,
		   unsigned int flags, struct timespec *timeout)
{
	int fput_needed, err, datagrams;
	struct socket *sock;
	struct mmsghdr __user *entry;
	struct compat_mmsghdr __user *compat_entry;
	struct msghdr msg_sys;
	struct timespec end_time;

	if (timeout &&
	    poll_select_set_timeout(&end_time, timeout->tv_sec,
				    timeout->tv_nsec))
		return -EINVAL;

	datagrams = 0;

	sock = sockfd_lookup_light(fd, &err, &fput_needed);
	if (!sock)
		return err;

	err = sock_error(sock->sk);
	if (err)
		goto out_put;

	entry = mmsg;
	compat_entry = (struct compat_mmsghdr __user *)mmsg;

	while (datagrams < vlen) {
		/*
		 * No need to ask LSM for more than the first datagram.
		 */
		if (MSG_CMSG_COMPAT & flags) {
			err = __sys_recvmsg(sock, (struct msghdr __user *)compat_entry,
					    &msg_sys, flags & ~MSG_WAITFORONE,
					    datagrams);
			if (err < 0)
				break;
			err = __put_user(err, &compat_entry->msg_len);
			++compat_entry;
		} else {
			err = __sys_recvmsg(sock, (struct msghdr __user *)entry,
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
			ktime_get_ts(timeout);
			*timeout = timespec_sub(end_time, *timeout);
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
	}

out_put:
	fput_light(sock->file, fput_needed);

	if (err == 0)
		return datagrams;

	if (datagrams != 0) {
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

		return datagrams;
	}

	return err;
}

SYSCALL_DEFINE5(recvmmsg, int, fd, struct mmsghdr __user *, mmsg,
		unsigned int, vlen, unsigned int, flags,
		struct timespec __user *, timeout)
{
	int datagrams;
	struct timespec timeout_sys;

	if (!timeout)
		return __sys_recvmmsg(fd, mmsg, vlen, flags, NULL);

	if (copy_from_user(&timeout_sys, timeout, sizeof(timeout_sys)))
		return -EFAULT;

	datagrams = __sys_recvmmsg(fd, mmsg, vlen, flags, &timeout_sys);

	if (datagrams > 0 &&
	    copy_to_user(timeout, &timeout_sys, sizeof(timeout_sys)))
		datagrams = -EFAULT;

	return datagrams;
}

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
	unsigned long a[6];
	unsigned long a0, a1;
	int err;
	unsigned int len;

	if (call < 1 || call > SYS_SENDMMSG)
		return -EINVAL;

	len = nargs[call];
	if (len > sizeof(a))
		return -EINVAL;

	/* copy_from_user should be SMP safe. */
	if (copy_from_user(a, args, len))
		return -EFAULT;

	audit_socketcall(nargs[call] / sizeof(unsigned long), a);

	a0 = a[0];
	a1 = a[1];

	switch (call) {
	case SYS_SOCKET:
		err = sys_socket(a0, a1, a[2]);
		break;
	case SYS_BIND:
		err = sys_bind(a0, (struct sockaddr __user *)a1, a[2]);
		break;
	case SYS_CONNECT:
		err = sys_connect(a0, (struct sockaddr __user *)a1, a[2]);
		break;
	case SYS_LISTEN:
		err = sys_listen(a0, a1);
		break;
	case SYS_ACCEPT:
		err = sys_accept4(a0, (struct sockaddr __user *)a1,
				  (int __user *)a[2], 0);
		break;
	case SYS_GETSOCKNAME:
		err =
		    sys_getsockname(a0, (struct sockaddr __user *)a1,
				    (int __user *)a[2]);
		break;
	case SYS_GETPEERNAME:
		err =
		    sys_getpeername(a0, (struct sockaddr __user *)a1,
				    (int __user *)a[2]);
		break;
	case SYS_SOCKETPAIR:
		err = sys_socketpair(a0, a1, a[2], (int __user *)a[3]);
		break;
	case SYS_SEND:
		err = sys_send(a0, (void __user *)a1, a[2], a[3]);
		break;
	case SYS_SENDTO:
		err = sys_sendto(a0, (void __user *)a1, a[2], a[3],
				 (struct sockaddr __user *)a[4], a[5]);
		break;
	case SYS_RECV:
		err = sys_recv(a0, (void __user *)a1, a[2], a[3]);
		break;
	case SYS_RECVFROM:
		err = sys_recvfrom(a0, (void __user *)a1, a[2], a[3],
				   (struct sockaddr __user *)a[4],
				   (int __user *)a[5]);
		break;
	case SYS_SHUTDOWN:
		err = sys_shutdown(a0, a1);
		break;
	case SYS_SETSOCKOPT:
		err = sys_setsockopt(a0, a1, a[2], (char __user *)a[3], a[4]);
		break;
	case SYS_GETSOCKOPT:
		err =
		    sys_getsockopt(a0, a1, a[2], (char __user *)a[3],
				   (int __user *)a[4]);
		break;
	case SYS_SENDMSG:
		err = sys_sendmsg(a0, (struct msghdr __user *)a1, a[2]);
		break;
	case SYS_SENDMMSG:
		err = sys_sendmmsg(a0, (struct mmsghdr __user *)a1, a[2], a[3]);
		break;
	case SYS_RECVMSG:
		err = sys_recvmsg(a0, (struct msghdr __user *)a1, a[2]);
		break;
	case SYS_RECVMMSG:
		err = sys_recvmmsg(a0, (struct mmsghdr __user *)a1, a[2], a[3],
				   (struct timespec __user *)a[4]);
		break;
	case SYS_ACCEPT4:
		err = sys_accept4(a0, (struct sockaddr __user *)a1,
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
 *	socket interface. The value ops->family coresponds to the
 *	socket system call protocol family.
 */
int sock_register(const struct net_proto_family *ops)
{
	int err;

	if (ops->family >= NPROTO) {
		printk(KERN_CRIT "protocol %d >= NPROTO(%d)\n", ops->family,
		       NPROTO);
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

	printk(KERN_INFO "NET: Registered protocol family %d\n", ops->family);
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
	rcu_assign_pointer(net_families[family], NULL);
	spin_unlock(&net_family_lock);

	synchronize_rcu();

	printk(KERN_INFO "NET: Unregistered protocol family %d\n", family);
}
EXPORT_SYMBOL(sock_unregister);

static int __init sock_init(void)
{
	int err;

	/*
	 *      Initialize sock SLAB cache.
	 */

	sk_init();

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
		goto out_fs;
	sock_mnt = kern_mount(&sock_fs_type);
	if (IS_ERR(sock_mnt)) {
		err = PTR_ERR(sock_mnt);
		goto out_mount;
	}

	/* The real protocol initialization is performed in later initcalls.
	 */

#ifdef CONFIG_NETFILTER
	netfilter_init();
#endif

#ifdef CONFIG_NETWORK_PHY_TIMESTAMPING
	skb_timestamping_init();
#endif

out:
	return err;

out_mount:
	unregister_filesystem(&sock_fs_type);
out_fs:
	goto out;
}

core_initcall(sock_init);	/* early initcall */

#ifdef CONFIG_PROC_FS
void socket_seq_show(struct seq_file *seq)
{
	int cpu;
	int counter = 0;

	for_each_possible_cpu(cpu)
	    counter += per_cpu(sockets_in_use, cpu);

	/* It can be negative, by the way. 8) */
	if (counter < 0)
		counter = 0;

	seq_printf(seq, "sockets: used %d\n", counter);
}
#endif				/* CONFIG_PROC_FS */

#ifdef CONFIG_COMPAT
static int do_siocgstamp(struct net *net, struct socket *sock,
			 unsigned int cmd, struct compat_timeval __user *up)
{
	mm_segment_t old_fs = get_fs();
	struct timeval ktv;
	int err;

	set_fs(KERNEL_DS);
	err = sock_do_ioctl(net, sock, cmd, (unsigned long)&ktv);
	set_fs(old_fs);
	if (!err) {
		err = put_user(ktv.tv_sec, &up->tv_sec);
		err |= __put_user(ktv.tv_usec, &up->tv_usec);
	}
	return err;
}

static int do_siocgstampns(struct net *net, struct socket *sock,
			 unsigned int cmd, struct compat_timespec __user *up)
{
	mm_segment_t old_fs = get_fs();
	struct timespec kts;
	int err;

	set_fs(KERNEL_DS);
	err = sock_do_ioctl(net, sock, cmd, (unsigned long)&kts);
	set_fs(old_fs);
	if (!err) {
		err = put_user(kts.tv_sec, &up->tv_sec);
		err |= __put_user(kts.tv_nsec, &up->tv_nsec);
	}
	return err;
}

static int dev_ifname32(struct net *net, struct compat_ifreq __user *uifr32)
{
	struct ifreq __user *uifr;
	int err;

	uifr = compat_alloc_user_space(sizeof(struct ifreq));
	if (copy_in_user(uifr, uifr32, sizeof(struct compat_ifreq)))
		return -EFAULT;

	err = dev_ioctl(net, SIOCGIFNAME, uifr);
	if (err)
		return err;

	if (copy_in_user(uifr32, uifr, sizeof(struct compat_ifreq)))
		return -EFAULT;

	return 0;
}

static int dev_ifconf(struct net *net, struct compat_ifconf __user *uifc32)
{
	struct compat_ifconf ifc32;
	struct ifconf ifc;
	struct ifconf __user *uifc;
	struct compat_ifreq __user *ifr32;
	struct ifreq __user *ifr;
	unsigned int i, j;
	int err;

	if (copy_from_user(&ifc32, uifc32, sizeof(struct compat_ifconf)))
		return -EFAULT;

	if (ifc32.ifcbuf == 0) {
		ifc32.ifc_len = 0;
		ifc.ifc_len = 0;
		ifc.ifc_req = NULL;
		uifc = compat_alloc_user_space(sizeof(struct ifconf));
	} else {
		size_t len = ((ifc32.ifc_len / sizeof(struct compat_ifreq)) + 1) *
			sizeof(struct ifreq);
		uifc = compat_alloc_user_space(sizeof(struct ifconf) + len);
		ifc.ifc_len = len;
		ifr = ifc.ifc_req = (void __user *)(uifc + 1);
		ifr32 = compat_ptr(ifc32.ifcbuf);
		for (i = 0; i < ifc32.ifc_len; i += sizeof(struct compat_ifreq)) {
			if (copy_in_user(ifr, ifr32, sizeof(struct compat_ifreq)))
				return -EFAULT;
			ifr++;
			ifr32++;
		}
	}
	if (copy_to_user(uifc, &ifc, sizeof(struct ifconf)))
		return -EFAULT;

	err = dev_ioctl(net, SIOCGIFCONF, uifc);
	if (err)
		return err;

	if (copy_from_user(&ifc, uifc, sizeof(struct ifconf)))
		return -EFAULT;

	ifr = ifc.ifc_req;
	ifr32 = compat_ptr(ifc32.ifcbuf);
	for (i = 0, j = 0;
	     i + sizeof(struct compat_ifreq) <= ifc32.ifc_len && j < ifc.ifc_len;
	     i += sizeof(struct compat_ifreq), j += sizeof(struct ifreq)) {
		if (copy_in_user(ifr32, ifr, sizeof(struct compat_ifreq)))
			return -EFAULT;
		ifr32++;
		ifr++;
	}

	if (ifc32.ifcbuf == 0) {
		/* Translate from 64-bit structure multiple to
		 * a 32-bit one.
		 */
		i = ifc.ifc_len;
		i = ((i / sizeof(struct ifreq)) * sizeof(struct compat_ifreq));
		ifc32.ifc_len = i;
	} else {
		ifc32.ifc_len = i;
	}
	if (copy_to_user(uifc32, &ifc32, sizeof(struct compat_ifconf)))
		return -EFAULT;

	return 0;
}

static int ethtool_ioctl(struct net *net, struct compat_ifreq __user *ifr32)
{
	struct compat_ethtool_rxnfc __user *compat_rxnfc;
	bool convert_in = false, convert_out = false;
	size_t buf_size = ALIGN(sizeof(struct ifreq), 8);
	struct ethtool_rxnfc __user *rxnfc;
	struct ifreq __user *ifr;
	u32 rule_cnt = 0, actual_rule_cnt;
	u32 ethcmd;
	u32 data;
	int ret;

	if (get_user(data, &ifr32->ifr_ifru.ifru_data))
		return -EFAULT;

	compat_rxnfc = compat_ptr(data);

	if (get_user(ethcmd, &compat_rxnfc->cmd))
		return -EFAULT;

	/* Most ethtool structures are defined without padding.
	 * Unfortunately struct ethtool_rxnfc is an exception.
	 */
	switch (ethcmd) {
	default:
		break;
	case ETHTOOL_GRXCLSRLALL:
		/* Buffer size is variable */
		if (get_user(rule_cnt, &compat_rxnfc->rule_cnt))
			return -EFAULT;
		if (rule_cnt > KMALLOC_MAX_SIZE / sizeof(u32))
			return -ENOMEM;
		buf_size += rule_cnt * sizeof(u32);
		/* fall through */
	case ETHTOOL_GRXRINGS:
	case ETHTOOL_GRXCLSRLCNT:
	case ETHTOOL_GRXCLSRULE:
		convert_out = true;
		/* fall through */
	case ETHTOOL_SRXCLSRLDEL:
	case ETHTOOL_SRXCLSRLINS:
		buf_size += sizeof(struct ethtool_rxnfc);
		convert_in = true;
		break;
	}

	ifr = compat_alloc_user_space(buf_size);
	rxnfc = (void *)ifr + ALIGN(sizeof(struct ifreq), 8);

	if (copy_in_user(&ifr->ifr_name, &ifr32->ifr_name, IFNAMSIZ))
		return -EFAULT;

	if (put_user(convert_in ? rxnfc : compat_ptr(data),
		     &ifr->ifr_ifru.ifru_data))
		return -EFAULT;

	if (convert_in) {
		/* We expect there to be holes between fs.m_ext and
		 * fs.ring_cookie and at the end of fs, but nowhere else.
		 */
		BUILD_BUG_ON(offsetof(struct compat_ethtool_rxnfc, fs.m_ext) +
			     sizeof(compat_rxnfc->fs.m_ext) !=
			     offsetof(struct ethtool_rxnfc, fs.m_ext) +
			     sizeof(rxnfc->fs.m_ext));
		BUILD_BUG_ON(
			offsetof(struct compat_ethtool_rxnfc, fs.location) -
			offsetof(struct compat_ethtool_rxnfc, fs.ring_cookie) !=
			offsetof(struct ethtool_rxnfc, fs.location) -
			offsetof(struct ethtool_rxnfc, fs.ring_cookie));

		if (copy_in_user(rxnfc, compat_rxnfc,
				 (void *)(&rxnfc->fs.m_ext + 1) -
				 (void *)rxnfc) ||
		    copy_in_user(&rxnfc->fs.ring_cookie,
				 &compat_rxnfc->fs.ring_cookie,
				 (void *)(&rxnfc->fs.location + 1) -
				 (void *)&rxnfc->fs.ring_cookie) ||
		    copy_in_user(&rxnfc->rule_cnt, &compat_rxnfc->rule_cnt,
				 sizeof(rxnfc->rule_cnt)))
			return -EFAULT;
	}

	ret = dev_ioctl(net, SIOCETHTOOL, ifr);
	if (ret)
		return ret;

	if (convert_out) {
		if (copy_in_user(compat_rxnfc, rxnfc,
				 (const void *)(&rxnfc->fs.m_ext + 1) -
				 (const void *)rxnfc) ||
		    copy_in_user(&compat_rxnfc->fs.ring_cookie,
				 &rxnfc->fs.ring_cookie,
				 (const void *)(&rxnfc->fs.location + 1) -
				 (const void *)&rxnfc->fs.ring_cookie) ||
		    copy_in_user(&compat_rxnfc->rule_cnt, &rxnfc->rule_cnt,
				 sizeof(rxnfc->rule_cnt)))
			return -EFAULT;

		if (ethcmd == ETHTOOL_GRXCLSRLALL) {
			/* As an optimisation, we only copy the actual
			 * number of rules that the underlying
			 * function returned.  Since Mallory might
			 * change the rule count in user memory, we
			 * check that it is less than the rule count
			 * originally given (as the user buffer size),
			 * which has been range-checked.
			 */
			if (get_user(actual_rule_cnt, &rxnfc->rule_cnt))
				return -EFAULT;
			if (actual_rule_cnt < rule_cnt)
				rule_cnt = actual_rule_cnt;
			if (copy_in_user(&compat_rxnfc->rule_locs[0],
					 &rxnfc->rule_locs[0],
					 rule_cnt * sizeof(u32)))
				return -EFAULT;
		}
	}

	return 0;
}

static int compat_siocwandev(struct net *net, struct compat_ifreq __user *uifr32)
{
	void __user *uptr;
	compat_uptr_t uptr32;
	struct ifreq __user *uifr;

	uifr = compat_alloc_user_space(sizeof(*uifr));
	if (copy_in_user(uifr, uifr32, sizeof(struct compat_ifreq)))
		return -EFAULT;

	if (get_user(uptr32, &uifr32->ifr_settings.ifs_ifsu))
		return -EFAULT;

	uptr = compat_ptr(uptr32);

	if (put_user(uptr, &uifr->ifr_settings.ifs_ifsu.raw_hdlc))
		return -EFAULT;

	return dev_ioctl(net, SIOCWANDEV, uifr);
}

static int bond_ioctl(struct net *net, unsigned int cmd,
			 struct compat_ifreq __user *ifr32)
{
	struct ifreq kifr;
	struct ifreq __user *uifr;
	mm_segment_t old_fs;
	int err;
	u32 data;
	void __user *datap;

	switch (cmd) {
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDCHANGEACTIVE:
		if (copy_from_user(&kifr, ifr32, sizeof(struct compat_ifreq)))
			return -EFAULT;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		err = dev_ioctl(net, cmd,
				(struct ifreq __user __force *) &kifr);
		set_fs(old_fs);

		return err;
	case SIOCBONDSLAVEINFOQUERY:
	case SIOCBONDINFOQUERY:
		uifr = compat_alloc_user_space(sizeof(*uifr));
		if (copy_in_user(&uifr->ifr_name, &ifr32->ifr_name, IFNAMSIZ))
			return -EFAULT;

		if (get_user(data, &ifr32->ifr_ifru.ifru_data))
			return -EFAULT;

		datap = compat_ptr(data);
		if (put_user(datap, &uifr->ifr_ifru.ifru_data))
			return -EFAULT;

		return dev_ioctl(net, cmd, uifr);
	default:
		return -EINVAL;
	}
}

static int siocdevprivate_ioctl(struct net *net, unsigned int cmd,
				 struct compat_ifreq __user *u_ifreq32)
{
	struct ifreq __user *u_ifreq64;
	char tmp_buf[IFNAMSIZ];
	void __user *data64;
	u32 data32;

	if (copy_from_user(&tmp_buf[0], &(u_ifreq32->ifr_ifrn.ifrn_name[0]),
			   IFNAMSIZ))
		return -EFAULT;
	if (__get_user(data32, &u_ifreq32->ifr_ifru.ifru_data))
		return -EFAULT;
	data64 = compat_ptr(data32);

	u_ifreq64 = compat_alloc_user_space(sizeof(*u_ifreq64));

	/* Don't check these user accesses, just let that get trapped
	 * in the ioctl handler instead.
	 */
	if (copy_to_user(&u_ifreq64->ifr_ifrn.ifrn_name[0], &tmp_buf[0],
			 IFNAMSIZ))
		return -EFAULT;
	if (__put_user(data64, &u_ifreq64->ifr_ifru.ifru_data))
		return -EFAULT;

	return dev_ioctl(net, cmd, u_ifreq64);
}

static int dev_ifsioc(struct net *net, struct socket *sock,
			 unsigned int cmd, struct compat_ifreq __user *uifr32)
{
	struct ifreq __user *uifr;
	int err;

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
	mm_segment_t old_fs;
	int err;

	uifmap32 = &uifr32->ifr_ifru.ifru_map;
	err = copy_from_user(&ifr, uifr32, sizeof(ifr.ifr_name));
	err |= __get_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
	err |= __get_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
	err |= __get_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
	err |= __get_user(ifr.ifr_map.irq, &uifmap32->irq);
	err |= __get_user(ifr.ifr_map.dma, &uifmap32->dma);
	err |= __get_user(ifr.ifr_map.port, &uifmap32->port);
	if (err)
		return -EFAULT;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = dev_ioctl(net, cmd, (void  __user __force *)&ifr);
	set_fs(old_fs);

	if (cmd == SIOCGIFMAP && !err) {
		err = copy_to_user(uifr32, &ifr, sizeof(ifr.ifr_name));
		err |= __put_user(ifr.ifr_map.mem_start, &uifmap32->mem_start);
		err |= __put_user(ifr.ifr_map.mem_end, &uifmap32->mem_end);
		err |= __put_user(ifr.ifr_map.base_addr, &uifmap32->base_addr);
		err |= __put_user(ifr.ifr_map.irq, &uifmap32->irq);
		err |= __put_user(ifr.ifr_map.dma, &uifmap32->dma);
		err |= __put_user(ifr.ifr_map.port, &uifmap32->port);
		if (err)
			err = -EFAULT;
	}
	return err;
}

static int compat_siocshwtstamp(struct net *net, struct compat_ifreq __user *uifr32)
{
	void __user *uptr;
	compat_uptr_t uptr32;
	struct ifreq __user *uifr;

	uifr = compat_alloc_user_space(sizeof(*uifr));
	if (copy_in_user(uifr, uifr32, sizeof(struct compat_ifreq)))
		return -EFAULT;

	if (get_user(uptr32, &uifr32->ifr_data))
		return -EFAULT;

	uptr = compat_ptr(uptr32);

	if (put_user(uptr, &uifr->ifr_data))
		return -EFAULT;

	return dev_ioctl(net, SIOCSHWTSTAMP, uifr);
}

struct rtentry32 {
	u32		rt_pad1;
	struct sockaddr rt_dst;         /* target address               */
	struct sockaddr rt_gateway;     /* gateway addr (RTF_GATEWAY)   */
	struct sockaddr rt_genmask;     /* target network mask (IP)     */
	unsigned short	rt_flags;
	short		rt_pad2;
	u32		rt_pad3;
	unsigned char	rt_tos;
	unsigned char	rt_class;
	short		rt_pad4;
	short		rt_metric;      /* +1 for binary compatibility! */
	/* char * */ u32 rt_dev;        /* forcing the device at add    */
	u32		rt_mtu;         /* per route MTU/Window         */
	u32		rt_window;      /* Window clamping              */
	unsigned short  rt_irtt;        /* Initial RTT                  */
};

struct in6_rtmsg32 {
	struct in6_addr		rtmsg_dst;
	struct in6_addr		rtmsg_src;
	struct in6_addr		rtmsg_gateway;
	u32			rtmsg_type;
	u16			rtmsg_dst_len;
	u16			rtmsg_src_len;
	u32			rtmsg_metric;
	u32			rtmsg_info;
	u32			rtmsg_flags;
	s32			rtmsg_ifindex;
};

static int routing_ioctl(struct net *net, struct socket *sock,
			 unsigned int cmd, void __user *argp)
{
	int ret;
	void *r = NULL;
	struct in6_rtmsg r6;
	struct rtentry r4;
	char devname[16];
	u32 rtdev;
	mm_segment_t old_fs = get_fs();

	if (sock && sock->sk && sock->sk->sk_family == AF_INET6) { /* ipv6 */
		struct in6_rtmsg32 __user *ur6 = argp;
		ret = copy_from_user(&r6.rtmsg_dst, &(ur6->rtmsg_dst),
			3 * sizeof(struct in6_addr));
		ret |= __get_user(r6.rtmsg_type, &(ur6->rtmsg_type));
		ret |= __get_user(r6.rtmsg_dst_len, &(ur6->rtmsg_dst_len));
		ret |= __get_user(r6.rtmsg_src_len, &(ur6->rtmsg_src_len));
		ret |= __get_user(r6.rtmsg_metric, &(ur6->rtmsg_metric));
		ret |= __get_user(r6.rtmsg_info, &(ur6->rtmsg_info));
		ret |= __get_user(r6.rtmsg_flags, &(ur6->rtmsg_flags));
		ret |= __get_user(r6.rtmsg_ifindex, &(ur6->rtmsg_ifindex));

		r = (void *) &r6;
	} else { /* ipv4 */
		struct rtentry32 __user *ur4 = argp;
		ret = copy_from_user(&r4.rt_dst, &(ur4->rt_dst),
					3 * sizeof(struct sockaddr));
		ret |= __get_user(r4.rt_flags, &(ur4->rt_flags));
		ret |= __get_user(r4.rt_metric, &(ur4->rt_metric));
		ret |= __get_user(r4.rt_mtu, &(ur4->rt_mtu));
		ret |= __get_user(r4.rt_window, &(ur4->rt_window));
		ret |= __get_user(r4.rt_irtt, &(ur4->rt_irtt));
		ret |= __get_user(rtdev, &(ur4->rt_dev));
		if (rtdev) {
			ret |= copy_from_user(devname, compat_ptr(rtdev), 15);
			r4.rt_dev = (char __user __force *)devname;
			devname[15] = 0;
		} else
			r4.rt_dev = NULL;

		r = (void *) &r4;
	}

	if (ret) {
		ret = -EFAULT;
		goto out;
	}

	set_fs(KERNEL_DS);
	ret = sock_do_ioctl(net, sock, cmd, (unsigned long) r);
	set_fs(old_fs);

out:
	return ret;
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
		return siocdevprivate_ioctl(net, cmd, argp);

	switch (cmd) {
	case SIOCSIFBR:
	case SIOCGIFBR:
		return old_bridge_ioctl(argp);
	case SIOCGIFNAME:
		return dev_ifname32(net, argp);
	case SIOCGIFCONF:
		return dev_ifconf(net, argp);
	case SIOCETHTOOL:
		return ethtool_ioctl(net, argp);
	case SIOCWANDEV:
		return compat_siocwandev(net, argp);
	case SIOCGIFMAP:
	case SIOCSIFMAP:
		return compat_sioc_ifmap(net, cmd, argp);
	case SIOCBONDENSLAVE:
	case SIOCBONDRELEASE:
	case SIOCBONDSETHWADDR:
	case SIOCBONDSLAVEINFOQUERY:
	case SIOCBONDINFOQUERY:
	case SIOCBONDCHANGEACTIVE:
		return bond_ioctl(net, cmd, argp);
	case SIOCADDRT:
	case SIOCDELRT:
		return routing_ioctl(net, sock, cmd, argp);
	case SIOCGSTAMP:
		return do_siocgstamp(net, sock, cmd, argp);
	case SIOCGSTAMPNS:
		return do_siocgstampns(net, sock, cmd, argp);
	case SIOCSHWTSTAMP:
		return compat_siocshwtstamp(net, argp);

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
	case SIOCSIFNAME:
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
		return dev_ifsioc(net, sock, cmd, argp);

	case SIOCSARP:
	case SIOCGARP:
	case SIOCDARP:
	case SIOCATMARK:
		return sock_do_ioctl(net, sock, cmd, arg);
	}

	/* Prevent warning from compat_sys_ioctl, these always
	 * result in -EINVAL in the native case anyway. */
	switch (cmd) {
	case SIOCRTMSG:
	case SIOCGIFCOUNT:
	case SIOCSRARP:
	case SIOCGRARP:
	case SIOCDRARP:
	case SIOCSIFLINK:
	case SIOCGIFSLAVE:
	case SIOCSIFSLAVE:
		return -EINVAL;
	}

	return -ENOIOCTLCMD;
}

static long compat_sock_ioctl(struct file *file, unsigned cmd,
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

int kernel_bind(struct socket *sock, struct sockaddr *addr, int addrlen)
{
	return sock->ops->bind(sock, addr, addrlen);
}
EXPORT_SYMBOL(kernel_bind);

int kernel_listen(struct socket *sock, int backlog)
{
	return sock->ops->listen(sock, backlog);
}
EXPORT_SYMBOL(kernel_listen);

int kernel_accept(struct socket *sock, struct socket **newsock, int flags)
{
	struct sock *sk = sock->sk;
	int err;

	err = sock_create_lite(sk->sk_family, sk->sk_type, sk->sk_protocol,
			       newsock);
	if (err < 0)
		goto done;

	err = sock->ops->accept(sock, *newsock, flags);
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

int kernel_connect(struct socket *sock, struct sockaddr *addr, int addrlen,
		   int flags)
{
	return sock->ops->connect(sock, addr, addrlen, flags);
}
EXPORT_SYMBOL(kernel_connect);

int kernel_getsockname(struct socket *sock, struct sockaddr *addr,
			 int *addrlen)
{
	return sock->ops->getname(sock, addr, addrlen, 0);
}
EXPORT_SYMBOL(kernel_getsockname);

int kernel_getpeername(struct socket *sock, struct sockaddr *addr,
			 int *addrlen)
{
	return sock->ops->getname(sock, addr, addrlen, 1);
}
EXPORT_SYMBOL(kernel_getpeername);

int kernel_getsockopt(struct socket *sock, int level, int optname,
			char *optval, int *optlen)
{
	mm_segment_t oldfs = get_fs();
	char __user *uoptval;
	int __user *uoptlen;
	int err;

	uoptval = (char __user __force *) optval;
	uoptlen = (int __user __force *) optlen;

	set_fs(KERNEL_DS);
	if (level == SOL_SOCKET)
		err = sock_getsockopt(sock, level, optname, uoptval, uoptlen);
	else
		err = sock->ops->getsockopt(sock, level, optname, uoptval,
					    uoptlen);
	set_fs(oldfs);
	return err;
}
EXPORT_SYMBOL(kernel_getsockopt);

int kernel_setsockopt(struct socket *sock, int level, int optname,
			char *optval, unsigned int optlen)
{
	mm_segment_t oldfs = get_fs();
	char __user *uoptval;
	int err;

	uoptval = (char __user __force *) optval;

	set_fs(KERNEL_DS);
	if (level == SOL_SOCKET)
		err = sock_setsockopt(sock, level, optname, uoptval, optlen);
	else
		err = sock->ops->setsockopt(sock, level, optname, uoptval,
					    optlen);
	set_fs(oldfs);
	return err;
}
EXPORT_SYMBOL(kernel_setsockopt);

int kernel_sendpage(struct socket *sock, struct page *page, int offset,
		    size_t size, int flags)
{
	sock_update_classid(sock->sk);

	if (sock->ops->sendpage)
		return sock->ops->sendpage(sock, page, offset, size, flags);

	return sock_no_sendpage(sock, page, offset, size, flags);
}
EXPORT_SYMBOL(kernel_sendpage);

int kernel_sock_ioctl(struct socket *sock, int cmd, unsigned long arg)
{
	mm_segment_t oldfs = get_fs();
	int err;

	set_fs(KERNEL_DS);
	err = sock->ops->ioctl(sock, cmd, arg);
	set_fs(oldfs);

	return err;
}
EXPORT_SYMBOL(kernel_sock_ioctl);

int kernel_sock_shutdown(struct socket *sock, enum sock_shutdown_cmd how)
{
	return sock->ops->shutdown(sock, how);
}
EXPORT_SYMBOL(kernel_sock_shutdown);
