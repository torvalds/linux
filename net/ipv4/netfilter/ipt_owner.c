/* Kernel module to match various things tied to sockets associated with
   locally generated outgoing packets. */

/* (C) 2000 Marc Boucher <marc@mbsi.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <net/sock.h>

#include <linux/netfilter_ipv4/ipt_owner.h>
#include <linux/netfilter_ipv4/ip_tables.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("iptables owner match");

static int
match_comm(const struct sk_buff *skb, const char *comm)
{
	struct task_struct *g, *p;
	struct files_struct *files;
	int i;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if(strncmp(p->comm, comm, sizeof(p->comm)))
			continue;

		task_lock(p);
		files = p->files;
		if(files) {
			spin_lock(&files->file_lock);
			for (i=0; i < files->max_fds; i++) {
				if (fcheck_files(files, i) ==
				    skb->sk->sk_socket->file) {
					spin_unlock(&files->file_lock);
					task_unlock(p);
					read_unlock(&tasklist_lock);
					return 1;
				}
			}
			spin_unlock(&files->file_lock);
		}
		task_unlock(p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
	return 0;
}

static int
match_pid(const struct sk_buff *skb, pid_t pid)
{
	struct task_struct *p;
	struct files_struct *files;
	int i;

	read_lock(&tasklist_lock);
	p = find_task_by_pid(pid);
	if (!p)
		goto out;
	task_lock(p);
	files = p->files;
	if(files) {
		spin_lock(&files->file_lock);
		for (i=0; i < files->max_fds; i++) {
			if (fcheck_files(files, i) ==
			    skb->sk->sk_socket->file) {
				spin_unlock(&files->file_lock);
				task_unlock(p);
				read_unlock(&tasklist_lock);
				return 1;
			}
		}
		spin_unlock(&files->file_lock);
	}
	task_unlock(p);
out:
	read_unlock(&tasklist_lock);
	return 0;
}

static int
match_sid(const struct sk_buff *skb, pid_t sid)
{
	struct task_struct *g, *p;
	struct file *file = skb->sk->sk_socket->file;
	int i, found=0;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		struct files_struct *files;
		if (p->signal->session != sid)
			continue;

		task_lock(p);
		files = p->files;
		if (files) {
			spin_lock(&files->file_lock);
			for (i=0; i < files->max_fds; i++) {
				if (fcheck_files(files, i) == file) {
					found = 1;
					break;
				}
			}
			spin_unlock(&files->file_lock);
		}
		task_unlock(p);
		if (found)
			goto out;
	} while_each_thread(g, p);
out:
	read_unlock(&tasklist_lock);

	return found;
}

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const void *matchinfo,
      int offset,
      int *hotdrop)
{
	const struct ipt_owner_info *info = matchinfo;

	if (!skb->sk || !skb->sk->sk_socket || !skb->sk->sk_socket->file)
		return 0;

	if(info->match & IPT_OWNER_UID) {
		if ((skb->sk->sk_socket->file->f_uid != info->uid) ^
		    !!(info->invert & IPT_OWNER_UID))
			return 0;
	}

	if(info->match & IPT_OWNER_GID) {
		if ((skb->sk->sk_socket->file->f_gid != info->gid) ^
		    !!(info->invert & IPT_OWNER_GID))
			return 0;
	}

	if(info->match & IPT_OWNER_PID) {
		if (!match_pid(skb, info->pid) ^
		    !!(info->invert & IPT_OWNER_PID))
			return 0;
	}

	if(info->match & IPT_OWNER_SID) {
		if (!match_sid(skb, info->sid) ^
		    !!(info->invert & IPT_OWNER_SID))
			return 0;
	}

	if(info->match & IPT_OWNER_COMM) {
		if (!match_comm(skb, info->comm) ^
		    !!(info->invert & IPT_OWNER_COMM))
			return 0;
	}

	return 1;
}

static int
checkentry(const char *tablename,
           const struct ipt_ip *ip,
           void *matchinfo,
           unsigned int matchsize,
           unsigned int hook_mask)
{
        if (hook_mask
            & ~((1 << NF_IP_LOCAL_OUT) | (1 << NF_IP_POST_ROUTING))) {
                printk("ipt_owner: only valid for LOCAL_OUT or POST_ROUTING.\n");
                return 0;
        }

	if (matchsize != IPT_ALIGN(sizeof(struct ipt_owner_info))) {
		printk("Matchsize %u != %Zu\n", matchsize,
		       IPT_ALIGN(sizeof(struct ipt_owner_info)));
		return 0;
	}
#ifdef CONFIG_SMP
	/* files->file_lock can not be used in a BH */
	if (((struct ipt_owner_info *)matchinfo)->match
	    & (IPT_OWNER_PID|IPT_OWNER_SID|IPT_OWNER_COMM)) {
		printk("ipt_owner: pid, sid and command matching is broken "
		       "on SMP.\n");
		return 0;
	}
#endif
	return 1;
}

static struct ipt_match owner_match = {
	.name		= "owner",
	.match		= &match,
	.checkentry	= &checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	return ipt_register_match(&owner_match);
}

static void __exit fini(void)
{
	ipt_unregister_match(&owner_match);
}

module_init(init);
module_exit(fini);
