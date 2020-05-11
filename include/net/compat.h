/* SPDX-License-Identifier: GPL-2.0 */
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

#else /* defined(CONFIG_COMPAT) */
/*
 * To avoid compiler warnings:
 */
#define compat_msghdr	msghdr
#define compat_mmsghdr	mmsghdr
#endif /* defined(CONFIG_COMPAT) */

int __get_compat_msghdr(struct msghdr *kmsg, struct compat_msghdr __user *umsg,
			struct sockaddr __user **save_addr, compat_uptr_t *ptr,
			compat_size_t *len);
int get_compat_msghdr(struct msghdr *, struct compat_msghdr __user *,
		      struct sockaddr __user **, struct iovec **);
struct sock_fprog __user *get_compat_bpf_fprog(char __user *optval);
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
