// SPDX-License-Identifier: GPL-2.0-only

#include <linux/debugfs.h>
#include <linux/fault-inject.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>

static struct {
	struct fault_attr attr;
	char devname[IFNAMSIZ];
	bool filtered;
} skb_realloc = {
	.attr = FAULT_ATTR_INITIALIZER,
	.filtered = false,
};

static bool should_fail_net_realloc_skb(struct sk_buff *skb)
{
	struct net_device *net = skb->dev;

	if (skb_realloc.filtered &&
	    strncmp(net->name, skb_realloc.devname, IFNAMSIZ))
		/* device name filter set, but names do not match */
		return false;

	if (!should_fail(&skb_realloc.attr, 1))
		return false;

	return true;
}
ALLOW_ERROR_INJECTION(should_fail_net_realloc_skb, TRUE);

void skb_might_realloc(struct sk_buff *skb)
{
	if (!should_fail_net_realloc_skb(skb))
		return;

	pskb_expand_head(skb, 0, 0, GFP_ATOMIC);
}
EXPORT_SYMBOL(skb_might_realloc);

static int __init fail_skb_realloc_setup(char *str)
{
	return setup_fault_attr(&skb_realloc.attr, str);
}
__setup("fail_skb_realloc=", fail_skb_realloc_setup);

static void reset_settings(void)
{
	skb_realloc.filtered = false;
	memset(&skb_realloc.devname, 0, IFNAMSIZ);
}

static ssize_t devname_write(struct file *file, const char __user *buffer,
			     size_t count, loff_t *ppos)
{
	ssize_t ret;

	reset_settings();
	ret = simple_write_to_buffer(&skb_realloc.devname, IFNAMSIZ,
				     ppos, buffer, count);
	if (ret < 0)
		return ret;

	skb_realloc.devname[IFNAMSIZ - 1] = '\0';
	/* Remove a possible \n at the end of devname */
	strim(skb_realloc.devname);

	if (strnlen(skb_realloc.devname, IFNAMSIZ))
		skb_realloc.filtered = true;

	return count;
}

static ssize_t devname_read(struct file *file,
			    char __user *buffer,
			    size_t size, loff_t *ppos)
{
	if (!skb_realloc.filtered)
		return 0;

	return simple_read_from_buffer(buffer, size, ppos, &skb_realloc.devname,
				       strlen(skb_realloc.devname));
}

static const struct file_operations devname_ops = {
	.write = devname_write,
	.read = devname_read,
};

static int __init fail_skb_realloc_debugfs(void)
{
	umode_t mode = S_IFREG | 0600;
	struct dentry *dir;

	dir = fault_create_debugfs_attr("fail_skb_realloc", NULL,
					&skb_realloc.attr);
	if (IS_ERR(dir))
		return PTR_ERR(dir);

	debugfs_create_file("devname", mode, dir, NULL, &devname_ops);

	return 0;
}

late_initcall(fail_skb_realloc_debugfs);
