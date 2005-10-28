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
#include <linux/kobject_uevent.h>
#include <linux/kobject.h>
#include <net/sock.h>

#define BUFFER_SIZE	1024	/* buffer for the hotplug env */
#define NUM_ENVP	32	/* number of env pointers */

#if defined(CONFIG_KOBJECT_UEVENT) || defined(CONFIG_HOTPLUG)
static char *action_to_string(enum kobject_action action)
{
	switch (action) {
	case KOBJ_ADD:
		return "add";
	case KOBJ_REMOVE:
		return "remove";
	case KOBJ_CHANGE:
		return "change";
	case KOBJ_MOUNT:
		return "mount";
	case KOBJ_UMOUNT:
		return "umount";
	case KOBJ_OFFLINE:
		return "offline";
	case KOBJ_ONLINE:
		return "online";
	default:
		return NULL;
	}
}
#endif

#ifdef CONFIG_KOBJECT_UEVENT
static struct sock *uevent_sock;

/**
 * send_uevent - notify userspace by sending event through netlink socket
 *
 * @signal: signal name
 * @obj: object path (kobject)
 * @envp: possible hotplug environment to pass with the message
 * @gfp_mask:
 */
static int send_uevent(const char *signal, const char *obj,
		       char **envp, gfp_t gfp_mask)
{
	struct sk_buff *skb;
	char *pos;
	int len;

	if (!uevent_sock)
		return -EIO;

	len = strlen(signal) + 1;
	len += strlen(obj) + 1;

	/* allocate buffer with the maximum possible message size */
	skb = alloc_skb(len + BUFFER_SIZE, gfp_mask);
	if (!skb)
		return -ENOMEM;

	pos = skb_put(skb, len);
	sprintf(pos, "%s@%s", signal, obj);

	/* copy the environment key by key to our continuous buffer */
	if (envp) {
		int i;

		for (i = 2; envp[i]; i++) {
			len = strlen(envp[i]) + 1;
			pos = skb_put(skb, len);
			strcpy(pos, envp[i]);
		}
	}

	NETLINK_CB(skb).dst_group = 1;
	return netlink_broadcast(uevent_sock, skb, 0, 1, gfp_mask);
}

static int do_kobject_uevent(struct kobject *kobj, enum kobject_action action, 
			     struct attribute *attr, gfp_t gfp_mask)
{
	char *path;
	char *attrpath;
	char *signal;
	int len;
	int rc = -ENOMEM;

	path = kobject_get_path(kobj, gfp_mask);
	if (!path)
		return -ENOMEM;

	signal = action_to_string(action);
	if (!signal)
		return -EINVAL;

	if (attr) {
		len = strlen(path);
		len += strlen(attr->name) + 2;
		attrpath = kmalloc(len, gfp_mask);
		if (!attrpath)
			goto exit;
		sprintf(attrpath, "%s/%s", path, attr->name);
		rc = send_uevent(signal, attrpath, NULL, gfp_mask);
		kfree(attrpath);
	} else
		rc = send_uevent(signal, path, NULL, gfp_mask);

exit:
	kfree(path);
	return rc;
}

/**
 * kobject_uevent - notify userspace by sending event through netlink socket
 * 
 * @signal: signal name
 * @kobj: struct kobject that the event is happening to
 * @attr: optional struct attribute the event belongs to
 */
int kobject_uevent(struct kobject *kobj, enum kobject_action action,
		   struct attribute *attr)
{
	return do_kobject_uevent(kobj, action, attr, GFP_KERNEL);
}
EXPORT_SYMBOL_GPL(kobject_uevent);

int kobject_uevent_atomic(struct kobject *kobj, enum kobject_action action,
			  struct attribute *attr)
{
	return do_kobject_uevent(kobj, action, attr, GFP_ATOMIC);
}
EXPORT_SYMBOL_GPL(kobject_uevent_atomic);

static int __init kobject_uevent_init(void)
{
	uevent_sock = netlink_kernel_create(NETLINK_KOBJECT_UEVENT, 1, NULL,
					    THIS_MODULE);

	if (!uevent_sock) {
		printk(KERN_ERR
		       "kobject_uevent: unable to create netlink socket!\n");
		return -ENODEV;
	}

	return 0;
}

postcore_initcall(kobject_uevent_init);

#else
static inline int send_uevent(const char *signal, const char *obj,
			      char **envp, int gfp_mask)
{
	return 0;
}

#endif /* CONFIG_KOBJECT_UEVENT */


#ifdef CONFIG_HOTPLUG
char hotplug_path[HOTPLUG_PATH_LEN] = "/sbin/hotplug";
u64 hotplug_seqnum;
static DEFINE_SPINLOCK(sequence_lock);

/**
 * kobject_hotplug - notify userspace by executing /sbin/hotplug
 *
 * @action: action that is happening (usually "ADD" or "REMOVE")
 * @kobj: struct kobject that the action is happening to
 */
void kobject_hotplug(struct kobject *kobj, enum kobject_action action)
{
	char *argv [3];
	char **envp = NULL;
	char *buffer = NULL;
	char *seq_buff;
	char *scratch;
	int i = 0;
	int retval;
	char *kobj_path = NULL;
	const char *name = NULL;
	char *action_string;
	u64 seq;
	struct kobject *top_kobj = kobj;
	struct kset *kset;
	static struct kset_hotplug_ops null_hotplug_ops;
	struct kset_hotplug_ops *hotplug_ops = &null_hotplug_ops;

	/* If this kobj does not belong to a kset,
	   try to find a parent that does. */
	if (!top_kobj->kset && top_kobj->parent) {
		do {
			top_kobj = top_kobj->parent;
		} while (!top_kobj->kset && top_kobj->parent);
	}

	if (top_kobj->kset)
		kset = top_kobj->kset;
	else
		return;

	if (kset->hotplug_ops)
		hotplug_ops = kset->hotplug_ops;

	/* If the kset has a filter operation, call it.
	   Skip the event, if the filter returns zero. */
	if (hotplug_ops->filter) {
		if (!hotplug_ops->filter(kset, kobj))
			return;
	}

	pr_debug ("%s\n", __FUNCTION__);

	action_string = action_to_string(action);
	if (!action_string)
		return;

	envp = kmalloc(NUM_ENVP * sizeof (char *), GFP_KERNEL);
	if (!envp)
		return;
	memset (envp, 0x00, NUM_ENVP * sizeof (char *));

	buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (!buffer)
		goto exit;

	if (hotplug_ops->name)
		name = hotplug_ops->name(kset, kobj);
	if (name == NULL)
		name = kobject_name(&kset->kobj);

	argv [0] = hotplug_path;
	argv [1] = (char *)name; /* won't be changed but 'const' has to go */
	argv [2] = NULL;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buffer;

	envp [i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", action_string) + 1;

	kobj_path = kobject_get_path(kobj, GFP_KERNEL);
	if (!kobj_path)
		goto exit;

	envp [i++] = scratch;
	scratch += sprintf (scratch, "DEVPATH=%s", kobj_path) + 1;

	envp [i++] = scratch;
	scratch += sprintf(scratch, "SUBSYSTEM=%s", name) + 1;

	/* reserve space for the sequence,
	 * put the real one in after the hotplug call */
	envp[i++] = seq_buff = scratch;
	scratch += strlen("SEQNUM=18446744073709551616") + 1;

	if (hotplug_ops->hotplug) {
		/* have the kset specific function add its stuff */
		retval = hotplug_ops->hotplug (kset, kobj,
				  &envp[i], NUM_ENVP - i, scratch,
				  BUFFER_SIZE - (scratch - buffer));
		if (retval) {
			pr_debug ("%s - hotplug() returned %d\n",
				  __FUNCTION__, retval);
			goto exit;
		}
	}

	spin_lock(&sequence_lock);
	seq = ++hotplug_seqnum;
	spin_unlock(&sequence_lock);
	sprintf(seq_buff, "SEQNUM=%llu", (unsigned long long)seq);

	pr_debug ("%s: %s %s seq=%llu %s %s %s %s %s\n",
		  __FUNCTION__, argv[0], argv[1], (unsigned long long)seq,
		  envp[0], envp[1], envp[2], envp[3], envp[4]);

	send_uevent(action_string, kobj_path, envp, GFP_KERNEL);

	if (!hotplug_path[0])
		goto exit;

	retval = call_usermodehelper (argv[0], argv, envp, 0);
	if (retval)
		pr_debug ("%s - call_usermodehelper returned %d\n",
			  __FUNCTION__, retval);

exit:
	kfree(kobj_path);
	kfree(buffer);
	kfree(envp);
	return;
}
EXPORT_SYMBOL(kobject_hotplug);

/**
 * add_hotplug_env_var - helper for creating hotplug environment variables
 * @envp: Pointer to table of environment variables, as passed into
 * hotplug() method.
 * @num_envp: Number of environment variable slots available, as
 * passed into hotplug() method.
 * @cur_index: Pointer to current index into @envp.  It should be
 * initialized to 0 before the first call to add_hotplug_env_var(),
 * and will be incremented on success.
 * @buffer: Pointer to buffer for environment variables, as passed
 * into hotplug() method.
 * @buffer_size: Length of @buffer, as passed into hotplug() method.
 * @cur_len: Pointer to current length of space used in @buffer.
 * Should be initialized to 0 before the first call to
 * add_hotplug_env_var(), and will be incremented on success.
 * @format: Format for creating environment variable (of the form
 * "XXX=%x") for snprintf().
 *
 * Returns 0 if environment variable was added successfully or -ENOMEM
 * if no space was available.
 */
int add_hotplug_env_var(char **envp, int num_envp, int *cur_index,
			char *buffer, int buffer_size, int *cur_len,
			const char *format, ...)
{
	va_list args;

	/*
	 * We check against num_envp - 1 to make sure there is at
	 * least one slot left after we return, since the hotplug
	 * method needs to set the last slot to NULL.
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
EXPORT_SYMBOL(add_hotplug_env_var);

#endif /* CONFIG_HOTPLUG */
