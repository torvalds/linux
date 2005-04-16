#ifndef NET_COMPAT_H
#define NET_COMPAT_H

#include <linux/config.h>

#if defined(CONFIG_COMPAT)

#include <linux/compat.h>

struct compat_msghdr {
	compat_uptr_t	msg_name;	/* void * */
	compat_int_t	msg_namelen;
	compat_uptr_t	msg_iov;	/* struct compat_iovec * */
	compat_size_t	msg_iovlen;
	compat_uptr_t	msg_control;	/* void * */
	compat_size_t	msg_controllen;
	compat_uint_t	msg_flags;
};

struct compat_cmsghdr {
	compat_size_t	cmsg_len;
	compat_int_t	cmsg_level;
	compat_int_t	cmsg_type;
};

#else /* defined(CONFIG_COMPAT) */
#define compat_msghdr	msghdr		/* to avoid compiler warnings */
#endif /* defined(CONFIG_COMPAT) */

extern int get_compat_msghdr(struct msghdr *, struct compat_msghdr __user *);
extern int verify_compat_iovec(struct msghdr *, struct iovec *, char *, int);
extern asmlinkage long compat_sys_sendmsg(int,struct compat_msghdr __user *,unsigned);
extern asmlinkage long compat_sys_recvmsg(int,struct compat_msghdr __user *,unsigned);
extern asmlinkage long compat_sys_getsockopt(int, int, int, char __user *, int __user *);
extern int put_cmsg_compat(struct msghdr*, int, int, int, void *);
extern int cmsghdr_from_user_compat_to_kern(struct msghdr *, unsigned char *,
		int);

#endif /* NET_COMPAT_H */
