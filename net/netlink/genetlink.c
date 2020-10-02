// SPDX-License-Identifier: GPL-2.0
/*
 * NETLINK      Generic Netlink Family
 *
 * 		Authors:	Jamal Hadi Salim
 * 				Thomas Graf <tgraf@suug.ch>
 *				Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <linux/rwsem.h>
#include <linux/idr.h>
#include <net/sock.h>
#include <net/genetlink.h>

static DEFINE_MUTEX(genl_mutex); /* serialization of message processing */
static DECLARE_RWSEM(cb_lock);

atomic_t genl_sk_destructing_cnt = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(genl_sk_destructing_waitq);

void genl_lock(void)
{
	mutex_lock(&genl_mutex);
}
EXPORT_SYMBOL(genl_lock);

void genl_unlock(void)
{
	mutex_unlock(&genl_mutex);
}
EXPORT_SYMBOL(genl_unlock);

#ifdef CONFIG_LOCKDEP
bool lockdep_genl_is_held(void)
{
	return lockdep_is_held(&genl_mutex);
}
EXPORT_SYMBOL(lockdep_genl_is_held);
#endif

static void genl_lock_all(void)
{
	down_write(&cb_lock);
	genl_lock();
}

static void genl_unlock_all(void)
{
	genl_unlock();
	up_write(&cb_lock);
}

static DEFINE_IDR(genl_fam_idr);

/*
 * Bitmap of multicast groups that are currently in use.
 *
 * To avoid an allocation at boot of just one unsigned long,
 * declare it global instead.
 * Bit 0 is marked as already used since group 0 is invalid.
 * Bit 1 is marked as already used since the drop-monitor code
 * abuses the API and thinks it can statically use group 1.
 * That group will typically conflict with other groups that
 * any proper users use.
 * Bit 16 is marked as used since it's used for generic netlink
 * and the code no longer marks pre-reserved IDs as used.
 * Bit 17 is marked as already used since the VFS quota code
 * also abused this API and relied on family == group ID, we
 * cater to that by giving it a static family and group ID.
 * Bit 18 is marked as already used since the PMCRAID driver
 * did the same thing as the VFS quota code (maybe copied?)
 */
static unsigned long mc_group_start = 0x3 | BIT(GENL_ID_CTRL) |
				      BIT(GENL_ID_VFS_DQUOT) |
				      BIT(GENL_ID_PMCRAID);
static unsigned long *mc_groups = &mc_group_start;
static unsigned long mc_groups_longs = 1;

static int genl_ctrl_event(int event, const struct genl_family *family,
			   const struct genl_multicast_group *grp,
			   int grp_id);

static const struct genl_family *genl_family_find_byid(unsigned int id)
{
	return idr_find(&genl_fam_idr, id);
}

static const struct genl_family *genl_family_find_byname(char *name)
{
	const struct genl_family *family;
	unsigned int id;

	idr_for_each_entry(&genl_fam_idr, family, id)
		if (strcmp(family->name, name) == 0)
			return family;

	return NULL;
}

static int genl_get_cmd_cnt(const struct genl_family *family)
{
	return family->n_ops + family->n_small_ops;
}

static void genl_op_from_full(const struct genl_family *family,
			      unsigned int i, struct genl_ops *op)
{
	*op = family->ops[i];
}

static int genl_get_cmd_full(u8 cmd, const struct genl_family *family,
			     struct genl_ops *op)
{
	int i;

	for (i = 0; i < family->n_ops; i++)
		if (family->ops[i].cmd == cmd) {
			genl_op_from_full(family, i, op);
			return 0;
		}

	return -ENOENT;
}

static void genl_op_from_small(const struct genl_family *family,
			       unsigned int i, struct genl_ops *op)
{
	memset(op, 0, sizeof(*op));
	op->doit	= family->small_ops[i].doit;
	op->dumpit	= family->small_ops[i].dumpit;
	op->cmd		= family->small_ops[i].cmd;
	op->internal_flags = family->small_ops[i].internal_flags;
	op->flags	= family->small_ops[i].flags;
	op->validate	= family->small_ops[i].validate;
}

static int genl_get_cmd_small(u8 cmd, const struct genl_family *family,
			      struct genl_ops *op)
{
	int i;

	for (i = 0; i < family->n_small_ops; i++)
		if (family->small_ops[i].cmd == cmd) {
			genl_op_from_small(family, i, op);
			return 0;
		}

	return -ENOENT;
}

static int genl_get_cmd(u8 cmd, const struct genl_family *family,
			struct genl_ops *op)
{
	if (!genl_get_cmd_full(cmd, family, op))
		return 0;
	return genl_get_cmd_small(cmd, family, op);
}

static void genl_get_cmd_by_index(unsigned int i,
				  const struct genl_family *family,
				  struct genl_ops *op)
{
	if (i < family->n_ops)
		genl_op_from_full(family, i, op);
	else if (i < family->n_ops + family->n_small_ops)
		genl_op_from_small(family, i - family->n_ops, op);
	else
		WARN_ON_ONCE(1);
}

static int genl_allocate_reserve_groups(int n_groups, int *first_id)
{
	unsigned long *new_groups;
	int start = 0;
	int i;
	int id;
	bool fits;

	do {
		if (start == 0)
			id = find_first_zero_bit(mc_groups,
						 mc_groups_longs *
						 BITS_PER_LONG);
		else
			id = find_next_zero_bit(mc_groups,
						mc_groups_longs * BITS_PER_LONG,
						start);

		fits = true;
		for (i = id;
		     i < min_t(int, id + n_groups,
			       mc_groups_longs * BITS_PER_LONG);
		     i++) {
			if (test_bit(i, mc_groups)) {
				start = i;
				fits = false;
				break;
			}
		}

		if (id + n_groups > mc_groups_longs * BITS_PER_LONG) {
			unsigned long new_longs = mc_groups_longs +
						  BITS_TO_LONGS(n_groups);
			size_t nlen = new_longs * sizeof(unsigned long);

			if (mc_groups == &mc_group_start) {
				new_groups = kzalloc(nlen, GFP_KERNEL);
				if (!new_groups)
					return -ENOMEM;
				mc_groups = new_groups;
				*mc_groups = mc_group_start;
			} else {
				new_groups = krealloc(mc_groups, nlen,
						      GFP_KERNEL);
				if (!new_groups)
					return -ENOMEM;
				mc_groups = new_groups;
				for (i = 0; i < BITS_TO_LONGS(n_groups); i++)
					mc_groups[mc_groups_longs + i] = 0;
			}
			mc_groups_longs = new_longs;
		}
	} while (!fits);

	for (i = id; i < id + n_groups; i++)
		set_bit(i, mc_groups);
	*first_id = id;
	return 0;
}

static struct genl_family genl_ctrl;

static int genl_validate_assign_mc_groups(struct genl_family *family)
{
	int first_id;
	int n_groups = family->n_mcgrps;
	int err = 0, i;
	bool groups_allocated = false;

	if (!n_groups)
		return 0;

	for (i = 0; i < n_groups; i++) {
		const struct genl_multicast_group *grp = &family->mcgrps[i];

		if (WARN_ON(grp->name[0] == '\0'))
			return -EINVAL;
		if (WARN_ON(memchr(grp->name, '\0', GENL_NAMSIZ) == NULL))
			return -EINVAL;
	}

	/* special-case our own group and hacks */
	if (family == &genl_ctrl) {
		first_id = GENL_ID_CTRL;
		BUG_ON(n_groups != 1);
	} else if (strcmp(family->name, "NET_DM") == 0) {
		first_id = 1;
		BUG_ON(n_groups != 1);
	} else if (family->id == GENL_ID_VFS_DQUOT) {
		first_id = GENL_ID_VFS_DQUOT;
		BUG_ON(n_groups != 1);
	} else if (family->id == GENL_ID_PMCRAID) {
		first_id = GENL_ID_PMCRAID;
		BUG_ON(n_groups != 1);
	} else {
		groups_allocated = true;
		err = genl_allocate_reserve_groups(n_groups, &first_id);
		if (err)
			return err;
	}

	family->mcgrp_offset = first_id;

	/* if still initializing, can't and don't need to realloc bitmaps */
	if (!init_net.genl_sock)
		return 0;

	if (family->netnsok) {
		struct net *net;

		netlink_table_grab();
		rcu_read_lock();
		for_each_net_rcu(net) {
			err = __netlink_change_ngroups(net->genl_sock,
					mc_groups_longs * BITS_PER_LONG);
			if (err) {
				/*
				 * No need to roll back, can only fail if
				 * memory allocation fails and then the
				 * number of _possible_ groups has been
				 * increased on some sockets which is ok.
				 */
				break;
			}
		}
		rcu_read_unlock();
		netlink_table_ungrab();
	} else {
		err = netlink_change_ngroups(init_net.genl_sock,
					     mc_groups_longs * BITS_PER_LONG);
	}

	if (groups_allocated && err) {
		for (i = 0; i < family->n_mcgrps; i++)
			clear_bit(family->mcgrp_offset + i, mc_groups);
	}

	return err;
}

static void genl_unregister_mc_groups(const struct genl_family *family)
{
	struct net *net;
	int i;

	netlink_table_grab();
	rcu_read_lock();
	for_each_net_rcu(net) {
		for (i = 0; i < family->n_mcgrps; i++)
			__netlink_clear_multicast_users(
				net->genl_sock, family->mcgrp_offset + i);
	}
	rcu_read_unlock();
	netlink_table_ungrab();

	for (i = 0; i < family->n_mcgrps; i++) {
		int grp_id = family->mcgrp_offset + i;

		if (grp_id != 1)
			clear_bit(grp_id, mc_groups);
		genl_ctrl_event(CTRL_CMD_DELMCAST_GRP, family,
				&family->mcgrps[i], grp_id);
	}
}

static int genl_validate_ops(const struct genl_family *family)
{
	int i, j;

	if (WARN_ON(family->n_ops && !family->ops) ||
	    WARN_ON(family->n_small_ops && !family->small_ops))
		return -EINVAL;

	for (i = 0; i < genl_get_cmd_cnt(family); i++) {
		struct genl_ops op;

		genl_get_cmd_by_index(i, family, &op);
		if (op.dumpit == NULL && op.doit == NULL)
			return -EINVAL;
		for (j = i + 1; j < genl_get_cmd_cnt(family); j++) {
			struct genl_ops op2;

			genl_get_cmd_by_index(j, family, &op2);
			if (op.cmd == op2.cmd)
				return -EINVAL;
		}
	}

	return 0;
}

/**
 * genl_register_family - register a generic netlink family
 * @family: generic netlink family
 *
 * Registers the specified family after validating it first. Only one
 * family may be registered with the same family name or identifier.
 *
 * The family's ops, multicast groups and module pointer must already
 * be assigned.
 *
 * Return 0 on success or a negative error code.
 */
int genl_register_family(struct genl_family *family)
{
	int err, i;
	int start = GENL_START_ALLOC, end = GENL_MAX_ID;

	err = genl_validate_ops(family);
	if (err)
		return err;

	genl_lock_all();

	if (genl_family_find_byname(family->name)) {
		err = -EEXIST;
		goto errout_locked;
	}

	/*
	 * Sadly, a few cases need to be special-cased
	 * due to them having previously abused the API
	 * and having used their family ID also as their
	 * multicast group ID, so we use reserved IDs
	 * for both to be sure we can do that mapping.
	 */
	if (family == &genl_ctrl) {
		/* and this needs to be special for initial family lookups */
		start = end = GENL_ID_CTRL;
	} else if (strcmp(family->name, "pmcraid") == 0) {
		start = end = GENL_ID_PMCRAID;
	} else if (strcmp(family->name, "VFS_DQUOT") == 0) {
		start = end = GENL_ID_VFS_DQUOT;
	}

	family->id = idr_alloc_cyclic(&genl_fam_idr, family,
				      start, end + 1, GFP_KERNEL);
	if (family->id < 0) {
		err = family->id;
		goto errout_locked;
	}

	err = genl_validate_assign_mc_groups(family);
	if (err)
		goto errout_remove;

	genl_unlock_all();

	/* send all events */
	genl_ctrl_event(CTRL_CMD_NEWFAMILY, family, NULL, 0);
	for (i = 0; i < family->n_mcgrps; i++)
		genl_ctrl_event(CTRL_CMD_NEWMCAST_GRP, family,
				&family->mcgrps[i], family->mcgrp_offset + i);

	return 0;

errout_remove:
	idr_remove(&genl_fam_idr, family->id);
errout_locked:
	genl_unlock_all();
	return err;
}
EXPORT_SYMBOL(genl_register_family);

/**
 * genl_unregister_family - unregister generic netlink family
 * @family: generic netlink family
 *
 * Unregisters the specified family.
 *
 * Returns 0 on success or a negative error code.
 */
int genl_unregister_family(const struct genl_family *family)
{
	genl_lock_all();

	if (!genl_family_find_byid(family->id)) {
		genl_unlock_all();
		return -ENOENT;
	}

	genl_unregister_mc_groups(family);

	idr_remove(&genl_fam_idr, family->id);

	up_write(&cb_lock);
	wait_event(genl_sk_destructing_waitq,
		   atomic_read(&genl_sk_destructing_cnt) == 0);
	genl_unlock();

	genl_ctrl_event(CTRL_CMD_DELFAMILY, family, NULL, 0);

	return 0;
}
EXPORT_SYMBOL(genl_unregister_family);

/**
 * genlmsg_put - Add generic netlink header to netlink message
 * @skb: socket buffer holding the message
 * @portid: netlink portid the message is addressed to
 * @seq: sequence number (usually the one of the sender)
 * @family: generic netlink family
 * @flags: netlink message flags
 * @cmd: generic netlink command
 *
 * Returns pointer to user specific header
 */
void *genlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
		  const struct genl_family *family, int flags, u8 cmd)
{
	struct nlmsghdr *nlh;
	struct genlmsghdr *hdr;

	nlh = nlmsg_put(skb, portid, seq, family->id, GENL_HDRLEN +
			family->hdrsize, flags);
	if (nlh == NULL)
		return NULL;

	hdr = nlmsg_data(nlh);
	hdr->cmd = cmd;
	hdr->version = family->version;
	hdr->reserved = 0;

	return (char *) hdr + GENL_HDRLEN;
}
EXPORT_SYMBOL(genlmsg_put);

static struct genl_dumpit_info *genl_dumpit_info_alloc(void)
{
	return kmalloc(sizeof(struct genl_dumpit_info), GFP_KERNEL);
}

static void genl_dumpit_info_free(const struct genl_dumpit_info *info)
{
	kfree(info);
}

static struct nlattr **
genl_family_rcv_msg_attrs_parse(const struct genl_family *family,
				struct nlmsghdr *nlh,
				struct netlink_ext_ack *extack,
				const struct genl_ops *ops,
				int hdrlen,
				enum genl_validate_flags no_strict_flag)
{
	enum netlink_validation validate = ops->validate & no_strict_flag ?
					   NL_VALIDATE_LIBERAL :
					   NL_VALIDATE_STRICT;
	struct nlattr **attrbuf;
	int err;

	if (!family->maxattr)
		return NULL;

	attrbuf = kmalloc_array(family->maxattr + 1,
				sizeof(struct nlattr *), GFP_KERNEL);
	if (!attrbuf)
		return ERR_PTR(-ENOMEM);

	err = __nlmsg_parse(nlh, hdrlen, attrbuf, family->maxattr,
			    family->policy, validate, extack);
	if (err) {
		kfree(attrbuf);
		return ERR_PTR(err);
	}
	return attrbuf;
}

static void genl_family_rcv_msg_attrs_free(struct nlattr **attrbuf)
{
	kfree(attrbuf);
}

struct genl_start_context {
	const struct genl_family *family;
	struct nlmsghdr *nlh;
	struct netlink_ext_ack *extack;
	const struct genl_ops *ops;
	int hdrlen;
};

static int genl_start(struct netlink_callback *cb)
{
	struct genl_start_context *ctx = cb->data;
	const struct genl_ops *ops = ctx->ops;
	struct genl_dumpit_info *info;
	struct nlattr **attrs = NULL;
	int rc = 0;

	if (ops->validate & GENL_DONT_VALIDATE_DUMP)
		goto no_attrs;

	if (ctx->nlh->nlmsg_len < nlmsg_msg_size(ctx->hdrlen))
		return -EINVAL;

	attrs = genl_family_rcv_msg_attrs_parse(ctx->family, ctx->nlh, ctx->extack,
						ops, ctx->hdrlen,
						GENL_DONT_VALIDATE_DUMP_STRICT);
	if (IS_ERR(attrs))
		return PTR_ERR(attrs);

no_attrs:
	info = genl_dumpit_info_alloc();
	if (!info) {
		genl_family_rcv_msg_attrs_free(attrs);
		return -ENOMEM;
	}
	info->family = ctx->family;
	info->op = *ops;
	info->attrs = attrs;

	cb->data = info;
	if (ops->start) {
		if (!ctx->family->parallel_ops)
			genl_lock();
		rc = ops->start(cb);
		if (!ctx->family->parallel_ops)
			genl_unlock();
	}

	if (rc) {
		genl_family_rcv_msg_attrs_free(info->attrs);
		genl_dumpit_info_free(info);
		cb->data = NULL;
	}
	return rc;
}

static int genl_lock_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct genl_ops *ops = &genl_dumpit_info(cb)->op;
	int rc;

	genl_lock();
	rc = ops->dumpit(skb, cb);
	genl_unlock();
	return rc;
}

static int genl_lock_done(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	const struct genl_ops *ops = &info->op;
	int rc = 0;

	if (ops->done) {
		genl_lock();
		rc = ops->done(cb);
		genl_unlock();
	}
	genl_family_rcv_msg_attrs_free(info->attrs);
	genl_dumpit_info_free(info);
	return rc;
}

static int genl_parallel_done(struct netlink_callback *cb)
{
	const struct genl_dumpit_info *info = genl_dumpit_info(cb);
	const struct genl_ops *ops = &info->op;
	int rc = 0;

	if (ops->done)
		rc = ops->done(cb);
	genl_family_rcv_msg_attrs_free(info->attrs);
	genl_dumpit_info_free(info);
	return rc;
}

static int genl_family_rcv_msg_dumpit(const struct genl_family *family,
				      struct sk_buff *skb,
				      struct nlmsghdr *nlh,
				      struct netlink_ext_ack *extack,
				      const struct genl_ops *ops,
				      int hdrlen, struct net *net)
{
	struct genl_start_context ctx;
	int err;

	if (!ops->dumpit)
		return -EOPNOTSUPP;

	ctx.family = family;
	ctx.nlh = nlh;
	ctx.extack = extack;
	ctx.ops = ops;
	ctx.hdrlen = hdrlen;

	if (!family->parallel_ops) {
		struct netlink_dump_control c = {
			.module = family->module,
			.data = &ctx,
			.start = genl_start,
			.dump = genl_lock_dumpit,
			.done = genl_lock_done,
		};

		genl_unlock();
		err = __netlink_dump_start(net->genl_sock, skb, nlh, &c);
		genl_lock();
	} else {
		struct netlink_dump_control c = {
			.module = family->module,
			.data = &ctx,
			.start = genl_start,
			.dump = ops->dumpit,
			.done = genl_parallel_done,
		};

		err = __netlink_dump_start(net->genl_sock, skb, nlh, &c);
	}

	return err;
}

static int genl_family_rcv_msg_doit(const struct genl_family *family,
				    struct sk_buff *skb,
				    struct nlmsghdr *nlh,
				    struct netlink_ext_ack *extack,
				    const struct genl_ops *ops,
				    int hdrlen, struct net *net)
{
	struct nlattr **attrbuf;
	struct genl_info info;
	int err;

	if (!ops->doit)
		return -EOPNOTSUPP;

	attrbuf = genl_family_rcv_msg_attrs_parse(family, nlh, extack,
						  ops, hdrlen,
						  GENL_DONT_VALIDATE_STRICT);
	if (IS_ERR(attrbuf))
		return PTR_ERR(attrbuf);

	info.snd_seq = nlh->nlmsg_seq;
	info.snd_portid = NETLINK_CB(skb).portid;
	info.nlhdr = nlh;
	info.genlhdr = nlmsg_data(nlh);
	info.userhdr = nlmsg_data(nlh) + GENL_HDRLEN;
	info.attrs = attrbuf;
	info.extack = extack;
	genl_info_net_set(&info, net);
	memset(&info.user_ptr, 0, sizeof(info.user_ptr));

	if (family->pre_doit) {
		err = family->pre_doit(ops, skb, &info);
		if (err)
			goto out;
	}

	err = ops->doit(skb, &info);

	if (family->post_doit)
		family->post_doit(ops, skb, &info);

out:
	genl_family_rcv_msg_attrs_free(attrbuf);

	return err;
}

static int genl_family_rcv_msg(const struct genl_family *family,
			       struct sk_buff *skb,
			       struct nlmsghdr *nlh,
			       struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct genlmsghdr *hdr = nlmsg_data(nlh);
	struct genl_ops op;
	int hdrlen;

	/* this family doesn't exist in this netns */
	if (!family->netnsok && !net_eq(net, &init_net))
		return -ENOENT;

	hdrlen = GENL_HDRLEN + family->hdrsize;
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return -EINVAL;

	if (genl_get_cmd(hdr->cmd, family, &op))
		return -EOPNOTSUPP;

	if ((op.flags & GENL_ADMIN_PERM) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if ((op.flags & GENL_UNS_ADMIN_PERM) &&
	    !netlink_ns_capable(skb, net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if ((nlh->nlmsg_flags & NLM_F_DUMP) == NLM_F_DUMP)
		return genl_family_rcv_msg_dumpit(family, skb, nlh, extack,
						  &op, hdrlen, net);
	else
		return genl_family_rcv_msg_doit(family, skb, nlh, extack,
						&op, hdrlen, net);
}

static int genl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct netlink_ext_ack *extack)
{
	const struct genl_family *family;
	int err;

	family = genl_family_find_byid(nlh->nlmsg_type);
	if (family == NULL)
		return -ENOENT;

	if (!family->parallel_ops)
		genl_lock();

	err = genl_family_rcv_msg(family, skb, nlh, extack);

	if (!family->parallel_ops)
		genl_unlock();

	return err;
}

static void genl_rcv(struct sk_buff *skb)
{
	down_read(&cb_lock);
	netlink_rcv_skb(skb, &genl_rcv_msg);
	up_read(&cb_lock);
}

/**************************************************************************
 * Controller
 **************************************************************************/

static struct genl_family genl_ctrl;

static int ctrl_fill_info(const struct genl_family *family, u32 portid, u32 seq,
			  u32 flags, struct sk_buff *skb, u8 cmd)
{
	void *hdr;

	hdr = genlmsg_put(skb, portid, seq, &genl_ctrl, flags, cmd);
	if (hdr == NULL)
		return -1;

	if (nla_put_string(skb, CTRL_ATTR_FAMILY_NAME, family->name) ||
	    nla_put_u16(skb, CTRL_ATTR_FAMILY_ID, family->id) ||
	    nla_put_u32(skb, CTRL_ATTR_VERSION, family->version) ||
	    nla_put_u32(skb, CTRL_ATTR_HDRSIZE, family->hdrsize) ||
	    nla_put_u32(skb, CTRL_ATTR_MAXATTR, family->maxattr))
		goto nla_put_failure;

	if (genl_get_cmd_cnt(family)) {
		struct nlattr *nla_ops;
		int i;

		nla_ops = nla_nest_start_noflag(skb, CTRL_ATTR_OPS);
		if (nla_ops == NULL)
			goto nla_put_failure;

		for (i = 0; i < genl_get_cmd_cnt(family); i++) {
			struct nlattr *nest;
			struct genl_ops op;
			u32 op_flags;

			genl_get_cmd_by_index(i, family, &op);
			op_flags = op.flags;
			if (op.dumpit)
				op_flags |= GENL_CMD_CAP_DUMP;
			if (op.doit)
				op_flags |= GENL_CMD_CAP_DO;
			if (family->policy)
				op_flags |= GENL_CMD_CAP_HASPOL;

			nest = nla_nest_start_noflag(skb, i + 1);
			if (nest == NULL)
				goto nla_put_failure;

			if (nla_put_u32(skb, CTRL_ATTR_OP_ID, op.cmd) ||
			    nla_put_u32(skb, CTRL_ATTR_OP_FLAGS, op_flags))
				goto nla_put_failure;

			nla_nest_end(skb, nest);
		}

		nla_nest_end(skb, nla_ops);
	}

	if (family->n_mcgrps) {
		struct nlattr *nla_grps;
		int i;

		nla_grps = nla_nest_start_noflag(skb, CTRL_ATTR_MCAST_GROUPS);
		if (nla_grps == NULL)
			goto nla_put_failure;

		for (i = 0; i < family->n_mcgrps; i++) {
			struct nlattr *nest;
			const struct genl_multicast_group *grp;

			grp = &family->mcgrps[i];

			nest = nla_nest_start_noflag(skb, i + 1);
			if (nest == NULL)
				goto nla_put_failure;

			if (nla_put_u32(skb, CTRL_ATTR_MCAST_GRP_ID,
					family->mcgrp_offset + i) ||
			    nla_put_string(skb, CTRL_ATTR_MCAST_GRP_NAME,
					   grp->name))
				goto nla_put_failure;

			nla_nest_end(skb, nest);
		}
		nla_nest_end(skb, nla_grps);
	}

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ctrl_fill_mcgrp_info(const struct genl_family *family,
				const struct genl_multicast_group *grp,
				int grp_id, u32 portid, u32 seq, u32 flags,
				struct sk_buff *skb, u8 cmd)
{
	void *hdr;
	struct nlattr *nla_grps;
	struct nlattr *nest;

	hdr = genlmsg_put(skb, portid, seq, &genl_ctrl, flags, cmd);
	if (hdr == NULL)
		return -1;

	if (nla_put_string(skb, CTRL_ATTR_FAMILY_NAME, family->name) ||
	    nla_put_u16(skb, CTRL_ATTR_FAMILY_ID, family->id))
		goto nla_put_failure;

	nla_grps = nla_nest_start_noflag(skb, CTRL_ATTR_MCAST_GROUPS);
	if (nla_grps == NULL)
		goto nla_put_failure;

	nest = nla_nest_start_noflag(skb, 1);
	if (nest == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, CTRL_ATTR_MCAST_GRP_ID, grp_id) ||
	    nla_put_string(skb, CTRL_ATTR_MCAST_GRP_NAME,
			   grp->name))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	nla_nest_end(skb, nla_grps);

	genlmsg_end(skb, hdr);
	return 0;

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ctrl_dumpfamily(struct sk_buff *skb, struct netlink_callback *cb)
{
	int n = 0;
	struct genl_family *rt;
	struct net *net = sock_net(skb->sk);
	int fams_to_skip = cb->args[0];
	unsigned int id;

	idr_for_each_entry(&genl_fam_idr, rt, id) {
		if (!rt->netnsok && !net_eq(net, &init_net))
			continue;

		if (n++ < fams_to_skip)
			continue;

		if (ctrl_fill_info(rt, NETLINK_CB(cb->skb).portid,
				   cb->nlh->nlmsg_seq, NLM_F_MULTI,
				   skb, CTRL_CMD_NEWFAMILY) < 0) {
			n--;
			break;
		}
	}

	cb->args[0] = n;
	return skb->len;
}

static struct sk_buff *ctrl_build_family_msg(const struct genl_family *family,
					     u32 portid, int seq, u8 cmd)
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return ERR_PTR(-ENOBUFS);

	err = ctrl_fill_info(family, portid, seq, 0, skb, cmd);
	if (err < 0) {
		nlmsg_free(skb);
		return ERR_PTR(err);
	}

	return skb;
}

static struct sk_buff *
ctrl_build_mcgrp_msg(const struct genl_family *family,
		     const struct genl_multicast_group *grp,
		     int grp_id, u32 portid, int seq, u8 cmd)
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return ERR_PTR(-ENOBUFS);

	err = ctrl_fill_mcgrp_info(family, grp, grp_id, portid,
				   seq, 0, skb, cmd);
	if (err < 0) {
		nlmsg_free(skb);
		return ERR_PTR(err);
	}

	return skb;
}

static const struct nla_policy ctrl_policy[CTRL_ATTR_MAX+1] = {
	[CTRL_ATTR_FAMILY_ID]	= { .type = NLA_U16 },
	[CTRL_ATTR_FAMILY_NAME]	= { .type = NLA_NUL_STRING,
				    .len = GENL_NAMSIZ - 1 },
};

static int ctrl_getfamily(struct sk_buff *skb, struct genl_info *info)
{
	struct sk_buff *msg;
	const struct genl_family *res = NULL;
	int err = -EINVAL;

	if (info->attrs[CTRL_ATTR_FAMILY_ID]) {
		u16 id = nla_get_u16(info->attrs[CTRL_ATTR_FAMILY_ID]);
		res = genl_family_find_byid(id);
		err = -ENOENT;
	}

	if (info->attrs[CTRL_ATTR_FAMILY_NAME]) {
		char *name;

		name = nla_data(info->attrs[CTRL_ATTR_FAMILY_NAME]);
		res = genl_family_find_byname(name);
#ifdef CONFIG_MODULES
		if (res == NULL) {
			genl_unlock();
			up_read(&cb_lock);
			request_module("net-pf-%d-proto-%d-family-%s",
				       PF_NETLINK, NETLINK_GENERIC, name);
			down_read(&cb_lock);
			genl_lock();
			res = genl_family_find_byname(name);
		}
#endif
		err = -ENOENT;
	}

	if (res == NULL)
		return err;

	if (!res->netnsok && !net_eq(genl_info_net(info), &init_net)) {
		/* family doesn't exist here */
		return -ENOENT;
	}

	msg = ctrl_build_family_msg(res, info->snd_portid, info->snd_seq,
				    CTRL_CMD_NEWFAMILY);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	return genlmsg_reply(msg, info);
}

static int genl_ctrl_event(int event, const struct genl_family *family,
			   const struct genl_multicast_group *grp,
			   int grp_id)
{
	struct sk_buff *msg;

	/* genl is still initialising */
	if (!init_net.genl_sock)
		return 0;

	switch (event) {
	case CTRL_CMD_NEWFAMILY:
	case CTRL_CMD_DELFAMILY:
		WARN_ON(grp);
		msg = ctrl_build_family_msg(family, 0, 0, event);
		break;
	case CTRL_CMD_NEWMCAST_GRP:
	case CTRL_CMD_DELMCAST_GRP:
		BUG_ON(!grp);
		msg = ctrl_build_mcgrp_msg(family, grp, grp_id, 0, 0, event);
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (!family->netnsok) {
		genlmsg_multicast_netns(&genl_ctrl, &init_net, msg, 0,
					0, GFP_KERNEL);
	} else {
		rcu_read_lock();
		genlmsg_multicast_allns(&genl_ctrl, msg, 0,
					0, GFP_ATOMIC);
		rcu_read_unlock();
	}

	return 0;
}

struct ctrl_dump_policy_ctx {
	struct netlink_policy_dump_state *state;
	u16 fam_id;
};

static int ctrl_dumppolicy_start(struct netlink_callback *cb)
{
	struct ctrl_dump_policy_ctx *ctx = (void *)cb->ctx;
	struct nlattr *tb[CTRL_ATTR_MAX + 1];
	const struct genl_family *rt;
	int err;

	BUILD_BUG_ON(sizeof(*ctx) > sizeof(cb->ctx));

	err = genlmsg_parse(cb->nlh, &genl_ctrl, tb, genl_ctrl.maxattr,
			    genl_ctrl.policy, cb->extack);
	if (err)
		return err;

	if (!tb[CTRL_ATTR_FAMILY_ID] && !tb[CTRL_ATTR_FAMILY_NAME])
		return -EINVAL;

	if (tb[CTRL_ATTR_FAMILY_ID]) {
		ctx->fam_id = nla_get_u16(tb[CTRL_ATTR_FAMILY_ID]);
	} else {
		rt = genl_family_find_byname(
			nla_data(tb[CTRL_ATTR_FAMILY_NAME]));
		if (!rt)
			return -ENOENT;
		ctx->fam_id = rt->id;
	}

	rt = genl_family_find_byid(ctx->fam_id);
	if (!rt)
		return -ENOENT;

	if (!rt->policy)
		return -ENODATA;

	return netlink_policy_dump_start(rt->policy, rt->maxattr, &ctx->state);
}

static int ctrl_dumppolicy(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct ctrl_dump_policy_ctx *ctx = (void *)cb->ctx;

	while (netlink_policy_dump_loop(ctx->state)) {
		void *hdr;
		struct nlattr *nest;

		hdr = genlmsg_put(skb, NETLINK_CB(cb->skb).portid,
				  cb->nlh->nlmsg_seq, &genl_ctrl,
				  NLM_F_MULTI, CTRL_CMD_GETPOLICY);
		if (!hdr)
			goto nla_put_failure;

		if (nla_put_u16(skb, CTRL_ATTR_FAMILY_ID, ctx->fam_id))
			goto nla_put_failure;

		nest = nla_nest_start(skb, CTRL_ATTR_POLICY);
		if (!nest)
			goto nla_put_failure;

		if (netlink_policy_dump_write(skb, ctx->state))
			goto nla_put_failure;

		nla_nest_end(skb, nest);

		genlmsg_end(skb, hdr);
		continue;

nla_put_failure:
		genlmsg_cancel(skb, hdr);
		break;
	}

	return skb->len;
}

static int ctrl_dumppolicy_done(struct netlink_callback *cb)
{
	struct ctrl_dump_policy_ctx *ctx = (void *)cb->ctx;

	netlink_policy_dump_free(ctx->state);
	return 0;
}

static const struct genl_ops genl_ctrl_ops[] = {
	{
		.cmd		= CTRL_CMD_GETFAMILY,
		.validate = GENL_DONT_VALIDATE_STRICT | GENL_DONT_VALIDATE_DUMP,
		.doit		= ctrl_getfamily,
		.dumpit		= ctrl_dumpfamily,
	},
	{
		.cmd		= CTRL_CMD_GETPOLICY,
		.start		= ctrl_dumppolicy_start,
		.dumpit		= ctrl_dumppolicy,
		.done		= ctrl_dumppolicy_done,
	},
};

static const struct genl_multicast_group genl_ctrl_groups[] = {
	{ .name = "notify", },
};

static struct genl_family genl_ctrl __ro_after_init = {
	.module = THIS_MODULE,
	.ops = genl_ctrl_ops,
	.n_ops = ARRAY_SIZE(genl_ctrl_ops),
	.mcgrps = genl_ctrl_groups,
	.n_mcgrps = ARRAY_SIZE(genl_ctrl_groups),
	.id = GENL_ID_CTRL,
	.name = "nlctrl",
	.version = 0x2,
	.maxattr = CTRL_ATTR_MAX,
	.policy = ctrl_policy,
	.netnsok = true,
};

static int __net_init genl_pernet_init(struct net *net)
{
	struct netlink_kernel_cfg cfg = {
		.input		= genl_rcv,
		.flags		= NL_CFG_F_NONROOT_RECV,
	};

	/* we'll bump the group number right afterwards */
	net->genl_sock = netlink_kernel_create(net, NETLINK_GENERIC, &cfg);

	if (!net->genl_sock && net_eq(net, &init_net))
		panic("GENL: Cannot initialize generic netlink\n");

	if (!net->genl_sock)
		return -ENOMEM;

	return 0;
}

static void __net_exit genl_pernet_exit(struct net *net)
{
	netlink_kernel_release(net->genl_sock);
	net->genl_sock = NULL;
}

static struct pernet_operations genl_pernet_ops = {
	.init = genl_pernet_init,
	.exit = genl_pernet_exit,
};

static int __init genl_init(void)
{
	int err;

	err = genl_register_family(&genl_ctrl);
	if (err < 0)
		goto problem;

	err = register_pernet_subsys(&genl_pernet_ops);
	if (err)
		goto problem;

	return 0;

problem:
	panic("GENL: Cannot register controller: %d\n", err);
}

core_initcall(genl_init);

static int genlmsg_mcast(struct sk_buff *skb, u32 portid, unsigned long group,
			 gfp_t flags)
{
	struct sk_buff *tmp;
	struct net *net, *prev = NULL;
	bool delivered = false;
	int err;

	for_each_net_rcu(net) {
		if (prev) {
			tmp = skb_clone(skb, flags);
			if (!tmp) {
				err = -ENOMEM;
				goto error;
			}
			err = nlmsg_multicast(prev->genl_sock, tmp,
					      portid, group, flags);
			if (!err)
				delivered = true;
			else if (err != -ESRCH)
				goto error;
		}

		prev = net;
	}

	err = nlmsg_multicast(prev->genl_sock, skb, portid, group, flags);
	if (!err)
		delivered = true;
	else if (err != -ESRCH)
		return err;
	return delivered ? 0 : -ESRCH;
 error:
	kfree_skb(skb);
	return err;
}

int genlmsg_multicast_allns(const struct genl_family *family,
			    struct sk_buff *skb, u32 portid,
			    unsigned int group, gfp_t flags)
{
	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return -EINVAL;
	group = family->mcgrp_offset + group;
	return genlmsg_mcast(skb, portid, group, flags);
}
EXPORT_SYMBOL(genlmsg_multicast_allns);

void genl_notify(const struct genl_family *family, struct sk_buff *skb,
		 struct genl_info *info, u32 group, gfp_t flags)
{
	struct net *net = genl_info_net(info);
	struct sock *sk = net->genl_sock;
	int report = 0;

	if (info->nlhdr)
		report = nlmsg_report(info->nlhdr);

	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return;
	group = family->mcgrp_offset + group;
	nlmsg_notify(sk, skb, info->snd_portid, group, report, flags);
}
EXPORT_SYMBOL(genl_notify);
