/* Public domain. */

#ifndef _LINUX_RCULIST_H
#define _LINUX_RCULIST_H

#include <linux/list.h>
#include <linux/rcupdate.h>

#define list_add_rcu(a, b)		list_add(a, b)
#define list_add_tail_rcu(a, b)		list_add_tail(a, b)
#define list_del_rcu(a)			list_del(a)
#define list_for_each_entry_rcu		list_for_each_entry
#define list_for_each_entry_lockless(a, b, c)	list_for_each_entry(a, b, c)

#endif
