#ifndef __NET_FIB_NOTIFIER_H
#define __NET_FIB_NOTIFIER_H

#include <linux/types.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <net/net_namespace.h>

struct fib_notifier_info {
	struct net *net;
	int family;
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

struct fib_notifier_ops {
	int family;
	struct list_head list;
	unsigned int (*fib_seq_read)(struct net *net);
	int (*fib_dump)(struct net *net, struct notifier_block *nb);
	struct module *owner;
	struct rcu_head rcu;
};

int call_fib_notifier(struct notifier_block *nb, struct net *net,
		      enum fib_event_type event_type,
		      struct fib_notifier_info *info);
int call_fib_notifiers(struct net *net, enum fib_event_type event_type,
		       struct fib_notifier_info *info);
int register_fib_notifier(struct notifier_block *nb,
			  void (*cb)(struct notifier_block *nb));
int unregister_fib_notifier(struct notifier_block *nb);
struct fib_notifier_ops *
fib_notifier_ops_register(const struct fib_notifier_ops *tmpl, struct net *net);
void fib_notifier_ops_unregister(struct fib_notifier_ops *ops);

#endif
