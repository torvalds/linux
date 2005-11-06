#ifndef _FIB_LOOKUP_H
#define _FIB_LOOKUP_H

#include <linux/types.h>
#include <linux/list.h>
#include <net/ip_fib.h>

struct fib_alias {
	struct list_head	fa_list;
	struct rcu_head rcu;
	struct fib_info		*fa_info;
	u8			fa_tos;
	u8			fa_type;
	u8			fa_scope;
	u8			fa_state;
};

#define FA_S_ACCESSED	0x01

/* Exported by fib_semantics.c */
extern int fib_semantic_match(struct list_head *head,
			      const struct flowi *flp,
			      struct fib_result *res, __u32 zone, __u32 mask,
				int prefixlen);
extern void fib_release_info(struct fib_info *);
extern struct fib_info *fib_create_info(const struct rtmsg *r,
					struct kern_rta *rta,
					const struct nlmsghdr *,
					int *err);
extern int fib_nh_match(struct rtmsg *r, struct nlmsghdr *,
			struct kern_rta *rta, struct fib_info *fi);
extern int fib_dump_info(struct sk_buff *skb, u32 pid, u32 seq, int event,
			 u8 tb_id, u8 type, u8 scope, void *dst,
			 int dst_len, u8 tos, struct fib_info *fi,
			 unsigned int);
extern void rtmsg_fib(int event, u32 key, struct fib_alias *fa,
		      int z, int tb_id,
		      struct nlmsghdr *n, struct netlink_skb_parms *req);
extern struct fib_alias *fib_find_alias(struct list_head *fah,
					u8 tos, u32 prio);
extern int fib_detect_death(struct fib_info *fi, int order,
			    struct fib_info **last_resort,
			    int *last_idx, int *dflt);

#endif /* _FIB_LOOKUP_H */
