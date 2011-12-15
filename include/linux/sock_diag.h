#ifndef __SOCK_DIAG_H__
#define __SOCK_DIAG_H__

#define SOCK_DIAG_BY_FAMILY 20

struct sk_buff;
struct nlmsghdr;

struct sock_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
};

struct sock_diag_handler {
	__u8 family;
	int (*dump)(struct sk_buff *skb, struct nlmsghdr *nlh);
};

int sock_diag_register(struct sock_diag_handler *h);
void sock_diag_unregister(struct sock_diag_handler *h);

void sock_diag_register_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));
void sock_diag_unregister_inet_compat(int (*fn)(struct sk_buff *skb, struct nlmsghdr *nlh));

extern struct sock *sock_diag_nlsk;
#endif
