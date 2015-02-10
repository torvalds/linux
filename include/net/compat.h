#ifndef NET_COMPAT_H
#define NET_COMPAT_H


struct sock;

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

struct compat_mmsghdr {
	struct compat_msghdr msg_hdr;
	compat_uint_t        msg_len;
};

struct compat_cmsghdr {
	compat_size_t	cmsg_len;
	compat_int_t	cmsg_level;
	compat_int_t	cmsg_type;
};

int compat_sock_get_timestamp(struct sock *, struct timeval __user *);
int compat_sock_get_timestampns(struct sock *, struct timespec __user *);

#else /* defined(CONFIG_COMPAT) */
/*
 * To avoid compiler warnings:
 */
#define compat_msghdr	msghdr
#define compat_mmsghdr	mmsghdr
#endif /* defined(CONFIG_COMPAT) */

ssize_t get_compat_msghdr(struct msghdr *, struct compat_msghdr __user *,
		      struct sockaddr __user **, struct iovec **);
asmlinkage long compat_sys_sendmsg(int, struct compat_msghdr __user *,
				   unsigned int);
asmlinkage long compat_sys_sendmmsg(int, struct compat_mmsghdr __user *,
				    unsigned int, unsigned int);
asmlinkage long compat_sys_recvmsg(int, struct compat_msghdr __user *,
				   unsigned int);
asmlinkage long compat_sys_recvmmsg(int, struct compat_mmsghdr __user *,
				    unsigned int, unsigned int,
				    struct compat_timespec __user *);
asmlinkage long compat_sys_getsockopt(int, int, int, char __user *,
				      int __user *);
int put_cmsg_compat(struct msghdr*, int, int, int, void *);

int cmsghdr_from_user_compat_to_kern(struct msghdr *, struct sock *,
				     unsigned char *, int);

int compat_mc_setsockopt(struct sock *, int, int, char __user *, unsigned int,
			 int (*)(struct sock *, int, int, char __user *,
				 unsigned int));
int compat_mc_getsockopt(struct sock *, int, int, char __user *, int __user *,
			 int (*)(struct sock *, int, int, char __user *,
				 int __user *));

#endif /* NET_COMPAT_H */
