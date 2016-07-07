#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <net/netlink.h>
#include <net/genetlink.h>

static const struct genl_multicast_group quota_mcgrps[] = {
	{ .name = "events", },
};

/* Netlink family structure for quota */
static struct genl_family quota_genl_family = {
	/*
	 * Needed due to multicast group ID abuse - old code assumed
	 * the family ID was also a valid multicast group ID (which
	 * isn't true) and userspace might thus rely on it. Assign a
	 * static ID for this group to make dealing with that easier.
	 */
	.id = GENL_ID_VFS_DQUOT,
	.hdrsize = 0,
	.name = "VFS_DQUOT",
	.version = 1,
	.maxattr = QUOTA_NL_A_MAX,
	.mcgrps = quota_mcgrps,
	.n_mcgrps = ARRAY_SIZE(quota_mcgrps),
};

/**
 * quota_send_warning - Send warning to userspace about exceeded quota
 * @qid: The kernel internal quota identifier.
 * @dev: The device on which the fs is mounted (sb->s_dev)
 * @warntype: The type of the warning: QUOTA_NL_...
 *
 * This can be used by filesystems (including those which don't use
 * dquot) to send a message to userspace relating to quota limits.
 *
 */

void quota_send_warning(struct kqid qid, dev_t dev,
			const char warntype)
{
	static atomic_t seq;
	struct sk_buff *skb;
	void *msg_head;
	int ret;
	int msg_size = 4 * nla_total_size(sizeof(u32)) +
		       2 * nla_total_size_64bit(sizeof(u64));

	/* We have to allocate using GFP_NOFS as we are called from a
	 * filesystem performing write and thus further recursion into
	 * the fs to free some data could cause deadlocks. */
	skb = genlmsg_new(msg_size, GFP_NOFS);
	if (!skb) {
		printk(KERN_ERR
		  "VFS: Not enough memory to send quota warning.\n");
		return;
	}
	msg_head = genlmsg_put(skb, 0, atomic_add_return(1, &seq),
			&quota_genl_family, 0, QUOTA_NL_C_WARNING);
	if (!msg_head) {
		printk(KERN_ERR
		  "VFS: Cannot store netlink header in quota warning.\n");
		goto err_out;
	}
	ret = nla_put_u32(skb, QUOTA_NL_A_QTYPE, qid.type);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64_64bit(skb, QUOTA_NL_A_EXCESS_ID,
				from_kqid_munged(&init_user_ns, qid),
				QUOTA_NL_A_PAD);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_WARNING, warntype);
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_DEV_MAJOR, MAJOR(dev));
	if (ret)
		goto attr_err_out;
	ret = nla_put_u32(skb, QUOTA_NL_A_DEV_MINOR, MINOR(dev));
	if (ret)
		goto attr_err_out;
	ret = nla_put_u64_64bit(skb, QUOTA_NL_A_CAUSED_ID,
				from_kuid_munged(&init_user_ns, current_uid()),
				QUOTA_NL_A_PAD);
	if (ret)
		goto attr_err_out;
	genlmsg_end(skb, msg_head);

	genlmsg_multicast(&quota_genl_family, skb, 0, 0, GFP_NOFS);
	return;
attr_err_out:
	printk(KERN_ERR "VFS: Not enough space to compose quota message!\n");
err_out:
	kfree_skb(skb);
}
EXPORT_SYMBOL(quota_send_warning);

static int __init quota_init(void)
{
	if (genl_register_family(&quota_genl_family) != 0)
		printk(KERN_ERR
		       "VFS: Failed to create quota netlink interface.\n");
	return 0;
};
fs_initcall(quota_init);
