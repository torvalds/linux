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
#include <net/sock.h>
#include <net/genetlink.h>

static DEFINE_MUTEX(genl_mutex); /* serialization of message processing */
static DECLARE_RWSEM(cb_lock);

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
int lockdep_genl_is_held(void)
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

#define GENL_FAM_TAB_SIZE	16
#define GENL_FAM_TAB_MASK	(GENL_FAM_TAB_SIZE - 1)

static struct list_head family_ht[GENL_FAM_TAB_SIZE];
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

static int genl_ctrl_event(int event, struct genl_family *family,
			   const struct genl_multicast_group *grp,
			   int grp_id);

static inline unsigned int genl_family_hash(unsigned int id)
{
	return id & GENL_FAM_TAB_MASK;
}

static inline struct list_head *genl_family_chain(unsigned int id)
{
	return &family_ht[genl_family_hash(id)];
}

static struct genl_family *genl_family_find_byid(unsigned int id)
{
	struct genl_family *f;

	list_for_each_entry(f, genl_family_chain(id), family_list)
		if (f->id == id)
			return f;

	return NULL;
}

static struct genl_family *genl_family_find_byname(char *name)
{
	struct genl_family *f;
	int i;

	for (i = 0; i < GENL_FAM_TAB_SIZE; i++)
		list_for_each_entry(f, genl_family_chain(i), family_list)
			if (strcmp(f->name, name) == 0)
				return f;

	return NULL;
}

static const struct genl_ops *genl_get_cmd(u8 cmd, struct genl_family *family)
{
	int i;

	for (i = 0; i < family->n_ops; i++)
		if (family->ops[i].cmd == cmd)
			return &family->ops[i];

	return NULL;
}

/* Of course we are going to have problems once we hit
 * 2^16 alive types, but that can only happen by year 2K
*/
static u16 genl_generate_id(void)
{
	static u16 id_gen_idx = GENL_MIN_ID;
	int i;

	for (i = 0; i <= GENL_MAX_ID - GENL_MIN_ID; i++) {
		if (id_gen_idx != GENL_ID_VFS_DQUOT &&
		    id_gen_idx != GENL_ID_PMCRAID &&
		    !genl_family_find_byid(id_gen_idx))
			return id_gen_idx;
		if (++id_gen_idx > GENL_MAX_ID)
			id_gen_idx = GENL_MIN_ID;
	}

	return 0;
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

		if (id >= mc_groups_longs * BITS_PER_LONG) {
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

	/* if still initializing, can't and don't need to to realloc bitmaps */
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

static void genl_unregister_mc_groups(struct genl_family *family)
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
	const struct genl_ops *ops = family->ops;
	unsigned int n_ops = family->n_ops;
	int i, j;

	if (WARN_ON(n_ops && !ops))
		return -EINVAL;

	if (!n_ops)
		return 0;

	for (i = 0; i < n_ops; i++) {
		if (ops[i].dumpit == NULL && ops[i].doit == NULL)
			return -EINVAL;
		for (j = i + 1; j < n_ops; j++)
			if (ops[i].cmd == ops[j].cmd)
				return -EINVAL;
	}

	return 0;
}

/**
 * __genl_register_family - register a generic netlink family
 * @family: generic netlink family
 *
 * Registers the specified family after validating it first. Only one
 * family may be registered with the same family name or identifier.
 * The family id may equal GENL_ID_GENERATE causing an unique id to
 * be automatically generated and assigned.
 *
 * The family's ops array must already be assigned, you can use the
 * genl_register_family_with_ops() helper function.
 *
 * Return 0 on success or a negative error code.
 */
int __genl_register_family(struct genl_family *family)
{
	int err = -EINVAL, i;

	if (family->id && family->id < GENL_MIN_ID)
		goto errout;

	if (family->id > GENL_MAX_ID)
		goto errout;

	err = genl_validate_ops(family);
	if (err)
		return err;

	genl_lock_all();

	if (genl_family_find_byname(family->name)) {
		err = -EEXIST;
		goto errout_locked;
	}

	if (family->id == GENL_ID_GENERATE) {
		u16 newid = genl_generate_id();

		if (!newid) {
			err = -ENOMEM;
			goto errout_locked;
		}

		family->id = newid;
	} else if (genl_family_find_byid(family->id)) {
		err = -EEXIST;
		goto errout_locked;
	}

	if (family->maxattr && !family->parallel_ops) {
		family->attrbuf = kmalloc((family->maxattr+1) *
					sizeof(struct nlattr *), GFP_KERNEL);
		if (family->attrbuf == NULL) {
			err = -ENOMEM;
			goto errout_locked;
		}
	} else
		family->attrbuf = NULL;

	err = genl_validate_assign_mc_groups(family);
	if (err)
		goto errout_locked;

	list_add_tail(&family->family_list, genl_family_chain(family->id));
	genl_unlock_all();

	/* send all events */
	genl_ctrl_event(CTRL_CMD_NEWFAMILY, family, NULL, 0);
	for (i = 0; i < family->n_mcgrps; i++)
		genl_ctrl_event(CTRL_CMD_NEWMCAST_GRP, family,
				&family->mcgrps[i], family->mcgrp_offset + i);

	return 0;

errout_locked:
	genl_unlock_all();
errout:
	return err;
}
EXPORT_SYMBOL(__genl_register_family);

/**
 * genl_unregister_family - unregister generic netlink family
 * @family: generic netlink family
 *
 * Unregisters the specified family.
 *
 * Returns 0 on success or a negative error code.
 */
int genl_unregister_family(struct genl_family *family)
{
	struct genl_family *rc;

	genl_lock_all();

	genl_unregister_mc_groups(family);

	list_for_each_entry(rc, genl_family_chain(family->id), family_list) {
		if (family->id != rc->id || strcmp(rc->name, family->name))
			continue;

		list_del(&rc->family_list);
		family->n_ops = 0;
		genl_unlock_all();

		kfree(family->attrbuf);
		genl_ctrl_event(CTRL_CMD_DELFAMILY, family, NULL, 0);
		return 0;
	}

	genl_unlock_all();

	return -ENOENT;
}
EXPORT_SYMBOL(genl_unregister_family);

/**
 * genlmsg_new_unicast - Allocate generic netlink message for unicast
 * @payload: size of the message payload
 * @info: information on destination
 * @flags: the type of memory to allocate
 *
 * Allocates a new sk_buff large enough to cover the specified payload
 * plus required Netlink headers. Will check receiving socket for
 * memory mapped i/o capability and use it if enabled. Will fall back
 * to non-mapped skb if message size exceeds the frame size of the ring.
 */
struct sk_buff *genlmsg_new_unicast(size_t payload, struct genl_info *info,
				    gfp_t flags)
{
	size_t len = nlmsg_total_size(genlmsg_total_size(payload));

	return netlink_alloc_skb(info->dst_sk, len, info->snd_portid, flags);
}
EXPORT_SYMBOL_GPL(genlmsg_new_unicast);

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
				struct genl_family *family, int flags, u8 cmd)
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

static int genl_lock_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	/* our ops are always const - netlink API doesn't propagate that */
	const struct genl_ops *ops = cb->data;
	int rc;

	genl_lock();
	rc = ops->dumpit(skb, cb);
	genl_unlock();
	return rc;
}

static int genl_lock_done(struct netlink_callback *cb)
{
	/* our ops are always const - netlink API doesn't propagate that */
	const struct genl_ops *ops = cb->data;
	int rc = 0;

	if (ops->done) {
		genl_lock();
		rc = ops->done(cb);
		genl_unlock();
	}
	return rc;
}

static int genl_family_rcv_msg(struct genl_family *family,
			       struct sk_buff *skb,
			       struct nlmsghdr *nlh)
{
	const struct genl_ops *ops;
	struct net *net = sock_net(skb->sk);
	struct genl_info info;
	struct genlmsghdr *hdr = nlmsg_data(nlh);
	struct nlattr **attrbuf;
	int hdrlen, err;

	/* this family doesn't exist in this netns */
	if (!family->netnsok && !net_eq(net, &init_net))
		return -ENOENT;

	hdrlen = GENL_HDRLEN + family->hdrsize;
	if (nlh->nlmsg_len < nlmsg_msg_size(hdrlen))
		return -EINVAL;

	ops = genl_get_cmd(hdr->cmd, family);
	if (ops == NULL)
		return -EOPNOTSUPP;

	if ((ops->flags & GENL_ADMIN_PERM) &&
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	if ((nlh->nlmsg_flags & NLM_F_DUMP) == NLM_F_DUMP) {
		int rc;

		if (ops->dumpit == NULL)
			return -EOPNOTSUPP;

		if (!family->parallel_ops) {
			struct netlink_dump_control c = {
				.module = family->module,
				/* we have const, but the netlink API doesn't */
				.data = (void *)ops,
				.dump = genl_lock_dumpit,
				.done = genl_lock_done,
			};

			genl_unlock();
			rc = __netlink_dump_start(net->genl_sock, skb, nlh, &c);
			genl_lock();

		} else {
			struct netlink_dump_control c = {
				.module = family->module,
				.dump = ops->dumpit,
				.done = ops->done,
			};

			rc = __netlink_dump_start(net->genl_sock, skb, nlh, &c);
		}

		return rc;
	}

	if (ops->doit == NULL)
		return -EOPNOTSUPP;

	if (family->maxattr && family->parallel_ops) {
		attrbuf = kmalloc((family->maxattr+1) *
					sizeof(struct nlattr *), GFP_KERNEL);
		if (attrbuf == NULL)
			return -ENOMEM;
	} else
		attrbuf = family->attrbuf;

	if (attrbuf) {
		err = nlmsg_parse(nlh, hdrlen, attrbuf, family->maxattr,
				  ops->policy);
		if (err < 0)
			goto out;
	}

	info.snd_seq = nlh->nlmsg_seq;
	info.snd_portid = NETLINK_CB(skb).portid;
	info.nlhdr = nlh;
	info.genlhdr = nlmsg_data(nlh);
	info.userhdr = nlmsg_data(nlh) + GENL_HDRLEN;
	info.attrs = attrbuf;
	info.dst_sk = skb->sk;
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
	if (family->parallel_ops)
		kfree(attrbuf);

	return err;
}

static int genl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct genl_family *family;
	int err;

	family = genl_family_find_byid(nlh->nlmsg_type);
	if (family == NULL)
		return -ENOENT;

	if (!family->parallel_ops)
		genl_lock();

	err = genl_family_rcv_msg(family, skb, nlh);

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

static struct genl_family genl_ctrl = {
	.id = GENL_ID_CTRL,
	.name = "nlctrl",
	.version = 0x2,
	.maxattr = CTRL_ATTR_MAX,
	.netnsok = true,
};

static int ctrl_fill_info(struct genl_family *family, u32 portid, u32 seq,
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

	if (family->n_ops) {
		struct nlattr *nla_ops;
		int i;

		nla_ops = nla_nest_start(skb, CTRL_ATTR_OPS);
		if (nla_ops == NULL)
			goto nla_put_failure;

		for (i = 0; i < family->n_ops; i++) {
			struct nlattr *nest;
			const struct genl_ops *ops = &family->ops[i];
			u32 op_flags = ops->flags;

			if (ops->dumpit)
				op_flags |= GENL_CMD_CAP_DUMP;
			if (ops->doit)
				op_flags |= GENL_CMD_CAP_DO;
			if (ops->policy)
				op_flags |= GENL_CMD_CAP_HASPOL;

			nest = nla_nest_start(skb, i + 1);
			if (nest == NULL)
				goto nla_put_failure;

			if (nla_put_u32(skb, CTRL_ATTR_OP_ID, ops->cmd) ||
			    nla_put_u32(skb, CTRL_ATTR_OP_FLAGS, op_flags))
				goto nla_put_failure;

			nla_nest_end(skb, nest);
		}

		nla_nest_end(skb, nla_ops);
	}

	if (family->n_mcgrps) {
		struct nlattr *nla_grps;
		int i;

		nla_grps = nla_nest_start(skb, CTRL_ATTR_MCAST_GROUPS);
		if (nla_grps == NULL)
			goto nla_put_failure;

		for (i = 0; i < family->n_mcgrps; i++) {
			struct nlattr *nest;
			const struct genl_multicast_group *grp;

			grp = &family->mcgrps[i];

			nest = nla_nest_start(skb, i + 1);
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

	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ctrl_fill_mcgrp_info(struct genl_family *family,
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

	nla_grps = nla_nest_start(skb, CTRL_ATTR_MCAST_GROUPS);
	if (nla_grps == NULL)
		goto nla_put_failure;

	nest = nla_nest_start(skb, 1);
	if (nest == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, CTRL_ATTR_MCAST_GRP_ID, grp_id) ||
	    nla_put_string(skb, CTRL_ATTR_MCAST_GRP_NAME,
			   grp->name))
		goto nla_put_failure;

	nla_nest_end(skb, nest);
	nla_nest_end(skb, nla_grps);

	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ctrl_dumpfamily(struct sk_buff *skb, struct netlink_callback *cb)
{

	int i, n = 0;
	struct genl_family *rt;
	struct net *net = sock_net(skb->sk);
	int chains_to_skip = cb->args[0];
	int fams_to_skip = cb->args[1];

	for (i = chains_to_skip; i < GENL_FAM_TAB_SIZE; i++) {
		n = 0;
		list_for_each_entry(rt, genl_family_chain(i), family_list) {
			if (!rt->netnsok && !net_eq(net, &init_net))
				continue;
			if (++n < fams_to_skip)
				continue;
			if (ctrl_fill_info(rt, NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq, NLM_F_MULTI,
					   skb, CTRL_CMD_NEWFAMILY) < 0)
				goto errout;
		}

		fams_to_skip = 0;
	}

errout:
	cb->args[0] = i;
	cb->args[1] = n;

	return skb->len;
}

static struct sk_buff *ctrl_build_family_msg(struct genl_family *family,
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
ctrl_build_mcgrp_msg(struct genl_family *family,
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
	struct genl_family *res = NULL;
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

static int genl_ctrl_event(int event, struct genl_family *family,
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

static struct genl_ops genl_ctrl_ops[] = {
	{
		.cmd		= CTRL_CMD_GETFAMILY,
		.doit		= ctrl_getfamily,
		.dumpit		= ctrl_dumpfamily,
		.policy		= ctrl_policy,
	},
};

static struct genl_multicast_group genl_ctrl_groups[] = {
	{ .name = "notify", },
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
	int i, err;

	for (i = 0; i < GENL_FAM_TAB_SIZE; i++)
		INIT_LIST_HEAD(&family_ht[i]);

	err = genl_register_family_with_ops_groups(&genl_ctrl, genl_ctrl_ops,
						   genl_ctrl_groups);
	if (err < 0)
		goto problem;

	err = register_pernet_subsys(&genl_pernet_ops);
	if (err)
		goto problem;

	return 0;

problem:
	panic("GENL: Cannot register controller: %d\n", err);
}

subsys_initcall(genl_init);

static int genlmsg_mcast(struct sk_buff *skb, u32 portid, unsigned long group,
			 gfp_t flags)
{
	struct sk_buff *tmp;
	struct net *net, *prev = NULL;
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
			if (err)
				goto error;
		}

		prev = net;
	}

	return nlmsg_multicast(prev->genl_sock, skb, portid, group, flags);
 error:
	kfree_skb(skb);
	return err;
}

int genlmsg_multicast_allns(struct genl_family *family, struct sk_buff *skb,
			    u32 portid, unsigned int group, gfp_t flags)
{
	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return -EINVAL;
	group = family->mcgrp_offset + group;
	return genlmsg_mcast(skb, portid, group, flags);
}
EXPORT_SYMBOL(genlmsg_multicast_allns);

void genl_notify(struct genl_family *family,
		 struct sk_buff *skb, struct net *net, u32 portid, u32 group,
		 struct nlmsghdr *nlh, gfp_t flags)
{
	struct sock *sk = net->genl_sock;
	int report = 0;

	if (nlh)
		report = nlmsg_report(nlh);

	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return;
	group = family->mcgrp_offset + group;
	nlmsg_notify(sk, skb, portid, group, report, flags);
}
EXPORT_SYMBOL(genl_notify);
