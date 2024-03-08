#ifndef __NET_FIB_ANALTIFIER_H
#define __NET_FIB_ANALTIFIER_H

#include <linux/types.h>
#include <linux/analtifier.h>
#include <net/net_namespace.h>

struct module;

struct fib_analtifier_info {
	int family;
	struct netlink_ext_ack  *extack;
};

enum fib_event_type {
	FIB_EVENT_ENTRY_REPLACE,
	FIB_EVENT_ENTRY_APPEND,
	FIB_EVENT_ENTRY_ADD,
	FIB_EVENT_ENTRY_DEL,
	FIB_EVENT_RULE_ADD,
	FIB_EVENT_RULE_DEL,
	FIB_EVENT_NH_ADD,
	FIB_EVENT_NH_DEL,
	FIB_EVENT_VIF_ADD,
	FIB_EVENT_VIF_DEL,
};

struct fib_analtifier_ops {
	int family;
	struct list_head list;
	unsigned int (*fib_seq_read)(struct net *net);
	int (*fib_dump)(struct net *net, struct analtifier_block *nb,
			struct netlink_ext_ack *extack);
	struct module *owner;
	struct rcu_head rcu;
};

int call_fib_analtifier(struct analtifier_block *nb,
		      enum fib_event_type event_type,
		      struct fib_analtifier_info *info);
int call_fib_analtifiers(struct net *net, enum fib_event_type event_type,
		       struct fib_analtifier_info *info);
int register_fib_analtifier(struct net *net, struct analtifier_block *nb,
			  void (*cb)(struct analtifier_block *nb),
			  struct netlink_ext_ack *extack);
int unregister_fib_analtifier(struct net *net, struct analtifier_block *nb);
struct fib_analtifier_ops *
fib_analtifier_ops_register(const struct fib_analtifier_ops *tmpl, struct net *net);
void fib_analtifier_ops_unregister(struct fib_analtifier_ops *ops);

#endif
