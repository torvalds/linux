#ifndef __SOCK_DIAG_H__
#define __SOCK_DIAG_H__

#include <linux/types.h>

#define SOCK_DIAG_BY_FAMILY 20

struct sock_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
};

enum {
	SK_MEMINFO_RMEM_ALLOC,
	SK_MEMINFO_RCVBUF,
	SK_MEMINFO_WMEM_ALLOC,
	SK_MEMINFO_SNDBUF,
	SK_MEMINFO_FWD_ALLOC,
	SK_MEMINFO_WMEM_QUEUED,
	SK_MEMINFO_OPTMEM,
	SK_MEMINFO_BACKLOG,

	SK_MEMINFO_VARS,
};

#ifdef __KERNEL__
struct sk_buff;
struct nlmsghdr;
struct sock;

struct sock_diag_handler {
	__u8 family;
	int (*dump)(struct sk_buff *skb, struct nlmsghdr *nlh);
};

int sock_diag_register(const struct sock_diag_handler *h);
void sock_diag_unregister(const struct sock_diag_handler *h);

void sock_diag_register_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));
void sock_diag_unregister_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));

int sock_diag_check_cookie(void *sk, __u32 *cookie);
void sock_diag_save_cookie(void *sk, __u32 *cookie);

int sock_diag_put_meminfo(struct sock *sk, struct sk_buff *skb, int attr);

#endif /* KERNEL */
#endif
