/*
 * kernel userspace event delivery
 *
 * Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2004 Novell, Inc.  All rights reserved.
 * Copyright (C) 2004 IBM, Inc. All rights reserved.
 *
 * Licensed under the GNU GPL v2.
 *
 * Authors:
 *	Robert Love		<rml@novell.com>
 *	Kay Sievers		<kay.sievers@vrfy.org>
 *	Arjan van de Ven	<arjanv@redhat.com>
 *	Greg Kroah-Hartman	<greg@kroah.com>
 */

#include <linux/spinlock.h>
#include <linux/socket.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/string.h>
#include <linux/kobject.h>
#include <net/sock.h>

#define BUFFER_SIZE	2048	/* buffer for the variables */
#define NUM_ENVP	32	/* number of env pointers */

/* the strings here must match the enum in include/linux/kobject.h */
const char *kobject_actions[] = {
	"add",
	"remove",
	"change",
	"move",
	"online",
	"offline",
};

#if defined(CONFIG_HOTPLUG)
u64 uevent_seqnum;
char uevent_helper[UEVENT_HELPER_PATH_LEN] = "/sbin/hotplug";
static DEFINE_SPINLOCK(sequence_lock);
#if defined(CONFIG_NET)
static struct sock *uevent_sock;
#endif

/**
 * kobject_uevent_env - send an uevent with environmental data
 *
 * @action: action that is happening (usually KOBJ_MOVE)
 * @kobj: struct kobject that the action is happening to
 * @envp_ext: pointer to environmental data
 *
 * Returns 0 if kobject_uevent() is completed with success or the
 * corresponding error when it fails.
 */
int kobject_uevent_env(struct kobject *kobj, enum kobject_action action,
			char *envp_ext[])
{
	char **envp;
	char *buffer;
	char *scratch;
	const char *action_string;
	const char *devpath = NULL;
	const char *subsystem;
	struct kobject *top_kobj;
	struct kset *kset;
	struct kset_uevent_ops *uevent_ops;
	u64 seq;
	char *seq_buff;
	int i = 0;
	int retval = 0;
	int j;

	pr_debug("%s\n", __FUNCTION__);

	action_string = kobject_actions[action];
	if (!action_string) {
		pr_debug("kobject attempted to send uevent without action_string!\n");
		return -EINVAL;
	}

	/* search the kset we belong to */
	top_kobj = kobj;
	while (!top_kobj->kset && top_kobj->parent) {
		top_kobj = top_kobj->parent;
	}
	if (!top_kobj->kset) {
		pr_debug("kobject attempted to send uevent without kset!\n");
		return -EINVAL;
	}

	kset = top_kobj->kset;
	uevent_ops = kset->uevent_ops;

	/*  skip the event, if the filter returns zero. */
	if (uevent_ops && uevent_ops->filter)
		if (!uevent_ops->filter(kset, kobj)) {
			pr_debug("kobject filter function caused the event to drop!\n");
			return 0;
		}

	/* originating subsystem */
	if (uevent_ops && uevent_ops->name)
		subsystem = uevent_ops->name(kset, kobj);
	else
		subsystem = kobject_name(&kset->kobj);
	if (!subsystem) {
		pr_debug("unset subsytem caused the event to drop!\n");
		return 0;
	}

	/* environment index */
	envp = kzalloc(NUM_ENVP * sizeof (char *), GFP_KERNEL);
	if (!envp)
		return -ENOMEM;

	/* environment values */
	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!buffer) {
		retval = -ENOMEM;
		goto exit;
	}

	/* complete object path */
	devpath = kobject_get_path(kobj, GFP_KERNEL);
	if (!devpath) {
		retval = -ENOENT;
		goto exit;
	}

	/* event environemnt for helper process only */
	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	/* default keys */
	scratch = buffer;
	envp [i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", action_string) + 1;
	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVPATH=%s", devpath) + 1;
	envp [i++] = scratch;
	scratch += sprintf(scratch, "SUBSYSTEM=%s", subsystem) + 1;
	for (j = 0; envp_ext && envp_ext[j]; j++)
		envp[i++] = envp_ext[j];
	/* just reserve the space, overwrite it after kset call has returned */
	envp[i++] = seq_buff = scratch;
	scratch += strlen("SEQNUM=18446744073709551616") + 1;

	/* let the kset specific function add its stuff */
	if (uevent_ops && uevent_ops->uevent) {
		retval = uevent_ops->uevent(kset, kobj,
				  &envp[i], NUM_ENVP - i, scratch,
				  BUFFER_SIZE - (scratch - buffer));
		if (retval) {
			pr_debug ("%s - uevent() returned %d\n",
				  __FUNCTION__, retval);
			goto exit;
		}
	}

	/* we will send an event, request a new sequence number */
	spin_lock(&sequence_lock);
	seq = ++uevent_seqnum;
	spin_unlock(&sequence_lock);
	sprintf(seq_buff, "SEQNUM=%llu", (unsigned long long)seq);

#if defined(CONFIG_NET)
	/* send netlink message */
	if (uevent_sock) {
		struct sk_buff *skb;
		size_t len;

		/* allocate message with the maximum possible size */
		len = strlen(action_string) + strlen(devpath) + 2;
		skb = alloc_skb(len + BUFFER_SIZE, GFP_KERNEL);
		if (skb) {
			/* add header */
			scratch = skb_put(skb, len);
			sprintf(scratch, "%s@%s", action_string, devpath);

			/* copy keys to our continuous event payload buffer */
			for (i = 2; envp[i]; i++) {
				len = strlen(envp[i]) + 1;
				scratch = skb_put(skb, len);
				strcpy(scratch, envp[i]);
			}

			NETLINK_CB(skb).dst_group = 1;
			netlink_broadcast(uevent_sock, skb, 0, 1, GFP_KERNEL);
		}
	}
#endif

	/* call uevent_helper, usually only enabled during early boot */
	if (uevent_helper[0]) {
		char *argv [3];

		argv [0] = uevent_helper;
		argv [1] = (char *)subsystem;
		argv [2] = NULL;
		call_usermodehelper (argv[0], argv, envp, UMH_WAIT_EXEC);
	}

exit:
	kfree(devpath);
	kfree(buffer);
	kfree(envp);
	return retval;
}

EXPORT_SYMBOL_GPL(kobject_uevent_env);

/**
 * kobject_uevent - notify userspace by ending an uevent
 *
 * @action: action that is happening (usually KOBJ_ADD and KOBJ_REMOVE)
 * @kobj: struct kobject that the action is happening to
 *
 * Returns 0 if kobject_uevent() is completed with success or the
 * corresponding error when it fails.
 */
int kobject_uevent(struct kobject *kobj, enum kobject_action action)
{
	return kobject_uevent_env(kobj, action, NULL);
}

EXPORT_SYMBOL_GPL(kobject_uevent);

/**
 * add_uevent_var - helper for creating event variables
 * @envp: Pointer to table of environment variables, as passed into
 * uevent() method.
 * @num_envp: Number of environment variable slots available, as
 * passed into uevent() method.
 * @cur_index: Pointer to current index into @envp.  It should be
 * initialized to 0 before the first call to add_uevent_var(),
 * and will be incremented on success.
 * @buffer: Pointer to buffer for environment variables, as passed
 * into uevent() method.
 * @buffer_size: Length of @buffer, as passed into uevent() method.
 * @cur_len: Pointer to current length of space used in @buffer.
 * Should be initialized to 0 before the first call to
 * add_uevent_var(), and will be incremented on success.
 * @format: Format for creating environment variable (of the form
 * "XXX=%x") for snprintf().
 *
 * Returns 0 if environment variable was added successfully or -ENOMEM
 * if no space was available.
 */
int add_uevent_var(char **envp, int num_envp, int *cur_index,
		   char *buffer, int buffer_size, int *cur_len,
		   const char *format, ...)
{
	va_list args;

	/*
	 * We check against num_envp - 1 to make sure there is at
	 * least one slot left after we return, since kobject_uevent()
	 * needs to set the last slot to NULL.
	 */
	if (*cur_index >= num_envp - 1)
		return -ENOMEM;

	envp[*cur_index] = buffer + *cur_len;

	va_start(args, format);
	*cur_len += vsnprintf(envp[*cur_index],
			      max(buffer_size - *cur_len, 0),
			      format, args) + 1;
	va_end(args);

	if (*cur_len > buffer_size)
		return -ENOMEM;

	(*cur_index)++;
	return 0;
}
EXPORT_SYMBOL_GPL(add_uevent_var);

#if defined(CONFIG_NET)
static int __init kobject_uevent_init(void)
{
	uevent_sock = netlink_kernel_create(NETLINK_KOBJECT_UEVENT, 1, NULL,
					    NULL, THIS_MODULE);

	if (!uevent_sock) {
		printk(KERN_ERR
		       "kobject_uevent: unable to create netlink socket!\n");
		return -ENODEV;
	}

	return 0;
}

postcore_initcall(kobject_uevent_init);
#endif

#endif /* CONFIG_HOTPLUG */
