/*
 * NETLINK      Generic Netlink Family
 *
 * 		Authors:	Jamal Hadi Salim
 * 				Thomas Graf <tgraf@suug.ch>
 *				Johannes Berg <johannes@sipsolutions.net>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/mutex.h>
#include <linux/bitmap.h>
#include <net/sock.h>
#include <net/genetlink.h>

static DEFINE_MUTEX(genl_mutex); /* serialization of message processing */

static inline void genl_lock(void)
{
	mutex_lock(&genl_mutex);
}

static inline void genl_unlock(void)
{
	mutex_unlock(&genl_mutex);
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
 */
static unsigned long mc_group_start = 0x1;
static unsigned long *mc_groups = &mc_group_start;
static unsigned long mc_groups_longs = 1;

static int genl_ctrl_event(int event, void *data);

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

static struct genl_ops *genl_get_cmd(u8 cmd, struct genl_family *family)
{
	struct genl_ops *ops;

	list_for_each_entry(ops, &family->ops_list, ops_list)
		if (ops->cmd == cmd)
			return ops;

	return NULL;
}

/* Of course we are going to have problems once we hit
 * 2^16 alive types, but that can only happen by year 2K
*/
static inline u16 genl_generate_id(void)
{
	static u16 id_gen_idx = GENL_MIN_ID;
	int i;

	for (i = 0; i <= GENL_MAX_ID - GENL_MIN_ID; i++) {
		if (!genl_family_find_byid(id_gen_idx))
			return id_gen_idx;
		if (++id_gen_idx > GENL_MAX_ID)
			id_gen_idx = GENL_MIN_ID;
	}

	return 0;
}

static struct genl_multicast_group notify_grp;

/**
 * genl_register_mc_group - register a multicast group
 *
 * Registers the specified multicast group and notifies userspace
 * about the new group.
 *
 * Returns 0 on success or a negative error code.
 *
 * @family: The generic netlink family the group shall be registered for.
 * @grp: The group to register, must have a name.
 */
int genl_register_mc_group(struct genl_family *family,
			   struct genl_multicast_group *grp)
{
	int id;
	unsigned long *new_groups;
	int err = 0;

	BUG_ON(grp->name[0] == '\0');

	genl_lock();

	/* special-case our own group */
	if (grp == &notify_grp)
		id = GENL_ID_CTRL;
	else
		id = find_first_zero_bit(mc_groups,
					 mc_groups_longs * BITS_PER_LONG);


	if (id >= mc_groups_longs * BITS_PER_LONG) {
		size_t nlen = (mc_groups_longs + 1) * sizeof(unsigned long);

		if (mc_groups == &mc_group_start) {
			new_groups = kzalloc(nlen, GFP_KERNEL);
			if (!new_groups) {
				err = -ENOMEM;
				goto out;
			}
			mc_groups = new_groups;
			*mc_groups = mc_group_start;
		} else {
			new_groups = krealloc(mc_groups, nlen, GFP_KERNEL);
			if (!new_groups) {
				err = -ENOMEM;
				goto out;
			}
			mc_groups = new_groups;
			mc_groups[mc_groups_longs] = 0;
		}
		mc_groups_longs++;
	}

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
				rcu_read_unlock();
				netlink_table_ungrab();
				goto out;
			}
		}
		rcu_read_unlock();
		netlink_table_ungrab();
	} else {
		err = netlink_change_ngroups(init_net.genl_sock,
					     mc_groups_longs * BITS_PER_LONG);
		if (err)
			goto out;
	}

	grp->id = id;
	set_bit(id, mc_groups);
	list_add_tail(&grp->list, &family->mcast_groups);
	grp->family = family;

	genl_ctrl_event(CTRL_CMD_NEWMCAST_GRP, grp);
 out:
	genl_unlock();
	return err;
}
EXPORT_SYMBOL(genl_register_mc_group);

static void __genl_unregister_mc_group(struct genl_family *family,
				       struct genl_multicast_group *grp)
{
	struct net *net;
	BUG_ON(grp->family != family);

	netlink_table_grab();
	rcu_read_lock();
	for_each_net_rcu(net)
		__netlink_clear_multicast_users(net->genl_sock, grp->id);
	rcu_read_unlock();
	netlink_table_ungrab();

	clear_bit(grp->id, mc_groups);
	list_del(&grp->list);
	genl_ctrl_event(CTRL_CMD_DELMCAST_GRP, grp);
	grp->id = 0;
	grp->family = NULL;
}

/**
 * genl_unregister_mc_group - unregister a multicast group
 *
 * Unregisters the specified multicast group and notifies userspace
 * about it. All current listeners on the group are removed.
 *
 * Note: It is not necessary to unregister all multicast groups before
 *       unregistering the family, unregistering the family will cause
 *       all assigned multicast groups to be unregistered automatically.
 *
 * @family: Generic netlink family the group belongs to.
 * @grp: The group to unregister, must have been registered successfully
 *	 previously.
 */
void genl_unregister_mc_group(struct genl_family *family,
			      struct genl_multicast_group *grp)
{
	genl_lock();
	__genl_unregister_mc_group(family, grp);
	genl_unlock();
}
EXPORT_SYMBOL(genl_unregister_mc_group);

static void genl_unregister_mc_groups(struct genl_family *family)
{
	struct genl_multicast_group *grp, *tmp;

	list_for_each_entry_safe(grp, tmp, &family->mcast_groups, list)
		__genl_unregister_mc_group(family, grp);
}

/**
 * genl_register_ops - register generic netlink operations
 * @family: generic netlink family
 * @ops: operations to be registered
 *
 * Registers the specified operations and assigns them to the specified
 * family. Either a doit or dumpit callback must be specified or the
 * operation will fail. Only one operation structure per command
 * identifier may be registered.
 *
 * See include/net/genetlink.h for more documenation on the operations
 * structure.
 *
 * Returns 0 on success or a negative error code.
 */
int genl_register_ops(struct genl_family *family, struct genl_ops *ops)
{
	int err = -EINVAL;

	if (ops->dumpit == NULL && ops->doit == NULL)
		goto errout;

	if (genl_get_cmd(ops->cmd, family)) {
		err = -EEXIST;
		goto errout;
	}

	if (ops->dumpit)
		ops->flags |= GENL_CMD_CAP_DUMP;
	if (ops->doit)
		ops->flags |= GENL_CMD_CAP_DO;
	if (ops->policy)
		ops->flags |= GENL_CMD_CAP_HASPOL;

	genl_lock();
	list_add_tail(&ops->ops_list, &family->ops_list);
	genl_unlock();

	genl_ctrl_event(CTRL_CMD_NEWOPS, ops);
	err = 0;
errout:
	return err;
}

/**
 * genl_unregister_ops - unregister generic netlink operations
 * @family: generic netlink family
 * @ops: operations to be unregistered
 *
 * Unregisters the specified operations and unassigns them from the
 * specified family. The operation blocks until the current message
 * processing has finished and doesn't start again until the
 * unregister process has finished.
 *
 * Note: It is not necessary to unregister all operations before
 *       unregistering the family, unregistering the family will cause
 *       all assigned operations to be unregistered automatically.
 *
 * Returns 0 on success or a negative error code.
 */
int genl_unregister_ops(struct genl_family *family, struct genl_ops *ops)
{
	struct genl_ops *rc;

	genl_lock();
	list_for_each_entry(rc, &family->ops_list, ops_list) {
		if (rc == ops) {
			list_del(&ops->ops_list);
			genl_unlock();
			genl_ctrl_event(CTRL_CMD_DELOPS, ops);
			return 0;
		}
	}
	genl_unlock();

	return -ENOENT;
}

/**
 * genl_register_family - register a generic netlink family
 * @family: generic netlink family
 *
 * Registers the specified family after validating it first. Only one
 * family may be registered with the same family name or identifier.
 * The family id may equal GENL_ID_GENERATE causing an unique id to
 * be automatically generated and assigned.
 *
 * Return 0 on success or a negative error code.
 */
int genl_register_family(struct genl_family *family)
{
	int err = -EINVAL;

	if (family->id && family->id < GENL_MIN_ID)
		goto errout;

	if (family->id > GENL_MAX_ID)
		goto errout;

	INIT_LIST_HEAD(&family->ops_list);
	INIT_LIST_HEAD(&family->mcast_groups);

	genl_lock();

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

	if (family->maxattr) {
		family->attrbuf = kmalloc((family->maxattr+1) *
					sizeof(struct nlattr *), GFP_KERNEL);
		if (family->attrbuf == NULL) {
			err = -ENOMEM;
			goto errout_locked;
		}
	} else
		family->attrbuf = NULL;

	list_add_tail(&family->family_list, genl_family_chain(family->id));
	genl_unlock();

	genl_ctrl_event(CTRL_CMD_NEWFAMILY, family);

	return 0;

errout_locked:
	genl_unlock();
errout:
	return err;
}

/**
 * genl_register_family_with_ops - register a generic netlink family
 * @family: generic netlink family
 * @ops: operations to be registered
 * @n_ops: number of elements to register
 *
 * Registers the specified family and operations from the specified table.
 * Only one family may be registered with the same family name or identifier.
 *
 * The family id may equal GENL_ID_GENERATE causing an unique id to
 * be automatically generated and assigned.
 *
 * Either a doit or dumpit callback must be specified for every registered
 * operation or the function will fail. Only one operation structure per
 * command identifier may be registered.
 *
 * See include/net/genetlink.h for more documenation on the operations
 * structure.
 *
 * This is equivalent to calling genl_register_family() followed by
 * genl_register_ops() for every operation entry in the table taking
 * care to unregister the family on error path.
 *
 * Return 0 on success or a negative error code.
 */
int genl_register_family_with_ops(struct genl_family *family,
	struct genl_ops *ops, size_t n_ops)
{
	int err, i;

	err = genl_register_family(family);
	if (err)
		return err;

	for (i = 0; i < n_ops; ++i, ++ops) {
		err = genl_register_ops(family, ops);
		if (err)
			goto err_out;
	}
	return 0;
err_out:
	genl_unregister_family(family);
	return err;
}
EXPORT_SYMBOL(genl_register_family_with_ops);

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

	genl_lock();

	genl_unregister_mc_groups(family);

	list_for_each_entry(rc, genl_family_chain(family->id), family_list) {
		if (family->id != rc->id || strcmp(rc->name, family->name))
			continue;

		list_del(&rc->family_list);
		INIT_LIST_HEAD(&family->ops_list);
		genl_unlock();

		kfree(family->attrbuf);
		genl_ctrl_event(CTRL_CMD_DELFAMILY, family);
		return 0;
	}

	genl_unlock();

	return -ENOENT;
}

static int genl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh)
{
	struct genl_ops *ops;
	struct genl_family *family;
	struct net *net = sock_net(skb->sk);
	struct genl_info info;
	struct genlmsghdr *hdr = nlmsg_data(nlh);
	int hdrlen, err;

	family = genl_family_find_byid(nlh->nlmsg_type);
	if (family == NULL)
		return -ENOENT;

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
	    security_netlink_recv(skb, CAP_NET_ADMIN))
		return -EPERM;

	if (nlh->nlmsg_flags & NLM_F_DUMP) {
		if (ops->dumpit == NULL)
			return -EOPNOTSUPP;

		genl_unlock();
		err = netlink_dump_start(net->genl_sock, skb, nlh,
					 ops->dumpit, ops->done);
		genl_lock();
		return err;
	}

	if (ops->doit == NULL)
		return -EOPNOTSUPP;

	if (family->attrbuf) {
		err = nlmsg_parse(nlh, hdrlen, family->attrbuf, family->maxattr,
				  ops->policy);
		if (err < 0)
			return err;
	}

	info.snd_seq = nlh->nlmsg_seq;
	info.snd_pid = NETLINK_CB(skb).pid;
	info.nlhdr = nlh;
	info.genlhdr = nlmsg_data(nlh);
	info.userhdr = nlmsg_data(nlh) + GENL_HDRLEN;
	info.attrs = family->attrbuf;
	genl_info_net_set(&info, net);

	return ops->doit(skb, &info);
}

static void genl_rcv(struct sk_buff *skb)
{
	genl_lock();
	netlink_rcv_skb(skb, &genl_rcv_msg);
	genl_unlock();
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

static int ctrl_fill_info(struct genl_family *family, u32 pid, u32 seq,
			  u32 flags, struct sk_buff *skb, u8 cmd)
{
	void *hdr;

	hdr = genlmsg_put(skb, pid, seq, &genl_ctrl, flags, cmd);
	if (hdr == NULL)
		return -1;

	NLA_PUT_STRING(skb, CTRL_ATTR_FAMILY_NAME, family->name);
	NLA_PUT_U16(skb, CTRL_ATTR_FAMILY_ID, family->id);
	NLA_PUT_U32(skb, CTRL_ATTR_VERSION, family->version);
	NLA_PUT_U32(skb, CTRL_ATTR_HDRSIZE, family->hdrsize);
	NLA_PUT_U32(skb, CTRL_ATTR_MAXATTR, family->maxattr);

	if (!list_empty(&family->ops_list)) {
		struct nlattr *nla_ops;
		struct genl_ops *ops;
		int idx = 1;

		nla_ops = nla_nest_start(skb, CTRL_ATTR_OPS);
		if (nla_ops == NULL)
			goto nla_put_failure;

		list_for_each_entry(ops, &family->ops_list, ops_list) {
			struct nlattr *nest;

			nest = nla_nest_start(skb, idx++);
			if (nest == NULL)
				goto nla_put_failure;

			NLA_PUT_U32(skb, CTRL_ATTR_OP_ID, ops->cmd);
			NLA_PUT_U32(skb, CTRL_ATTR_OP_FLAGS, ops->flags);

			nla_nest_end(skb, nest);
		}

		nla_nest_end(skb, nla_ops);
	}

	if (!list_empty(&family->mcast_groups)) {
		struct genl_multicast_group *grp;
		struct nlattr *nla_grps;
		int idx = 1;

		nla_grps = nla_nest_start(skb, CTRL_ATTR_MCAST_GROUPS);
		if (nla_grps == NULL)
			goto nla_put_failure;

		list_for_each_entry(grp, &family->mcast_groups, list) {
			struct nlattr *nest;

			nest = nla_nest_start(skb, idx++);
			if (nest == NULL)
				goto nla_put_failure;

			NLA_PUT_U32(skb, CTRL_ATTR_MCAST_GRP_ID, grp->id);
			NLA_PUT_STRING(skb, CTRL_ATTR_MCAST_GRP_NAME,
				       grp->name);

			nla_nest_end(skb, nest);
		}
		nla_nest_end(skb, nla_grps);
	}

	return genlmsg_end(skb, hdr);

nla_put_failure:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

static int ctrl_fill_mcgrp_info(struct genl_multicast_group *grp, u32 pid,
				u32 seq, u32 flags, struct sk_buff *skb,
				u8 cmd)
{
	void *hdr;
	struct nlattr *nla_grps;
	struct nlattr *nest;

	hdr = genlmsg_put(skb, pid, seq, &genl_ctrl, flags, cmd);
	if (hdr == NULL)
		return -1;

	NLA_PUT_STRING(skb, CTRL_ATTR_FAMILY_NAME, grp->family->name);
	NLA_PUT_U16(skb, CTRL_ATTR_FAMILY_ID, grp->family->id);

	nla_grps = nla_nest_start(skb, CTRL_ATTR_MCAST_GROUPS);
	if (nla_grps == NULL)
		goto nla_put_failure;

	nest = nla_nest_start(skb, 1);
	if (nest == NULL)
		goto nla_put_failure;

	NLA_PUT_U32(skb, CTRL_ATTR_MCAST_GRP_ID, grp->id);
	NLA_PUT_STRING(skb, CTRL_ATTR_MCAST_GRP_NAME,
		       grp->name);

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

	for (i = 0; i < GENL_FAM_TAB_SIZE; i++) {
		if (i < chains_to_skip)
			continue;
		n = 0;
		list_for_each_entry(rt, genl_family_chain(i), family_list) {
			if (!rt->netnsok && !net_eq(net, &init_net))
				continue;
			if (++n < fams_to_skip)
				continue;
			if (ctrl_fill_info(rt, NETLINK_CB(cb->skb).pid,
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
					     u32 pid, int seq, u8 cmd)
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return ERR_PTR(-ENOBUFS);

	err = ctrl_fill_info(family, pid, seq, 0, skb, cmd);
	if (err < 0) {
		nlmsg_free(skb);
		return ERR_PTR(err);
	}

	return skb;
}

static struct sk_buff *ctrl_build_mcgrp_msg(struct genl_multicast_group *grp,
					    u32 pid, int seq, u8 cmd)
{
	struct sk_buff *skb;
	int err;

	skb = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (skb == NULL)
		return ERR_PTR(-ENOBUFS);

	err = ctrl_fill_mcgrp_info(grp, pid, seq, 0, skb, cmd);
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
		err = -ENOENT;
	}

	if (res == NULL)
		return err;

	if (!res->netnsok && !net_eq(genl_info_net(info), &init_net)) {
		/* family doesn't exist here */
		return -ENOENT;
	}

	msg = ctrl_build_family_msg(res, info->snd_pid, info->snd_seq,
				    CTRL_CMD_NEWFAMILY);
	if (IS_ERR(msg))
		return PTR_ERR(msg);

	return genlmsg_reply(msg, info);
}

static int genl_ctrl_event(int event, void *data)
{
	struct sk_buff *msg;
	struct genl_family *family;
	struct genl_multicast_group *grp;

	/* genl is still initialising */
	if (!init_net.genl_sock)
		return 0;

	switch (event) {
	case CTRL_CMD_NEWFAMILY:
	case CTRL_CMD_DELFAMILY:
		family = data;
		msg = ctrl_build_family_msg(family, 0, 0, event);
		break;
	case CTRL_CMD_NEWMCAST_GRP:
	case CTRL_CMD_DELMCAST_GRP:
		grp = data;
		family = grp->family;
		msg = ctrl_build_mcgrp_msg(data, 0, 0, event);
		break;
	default:
		return -EINVAL;
	}

	if (IS_ERR(msg))
		return PTR_ERR(msg);

	if (!family->netnsok) {
		genlmsg_multicast_netns(&init_net, msg, 0,
					GENL_ID_CTRL, GFP_KERNEL);
	} else {
		rcu_read_lock();
		genlmsg_multicast_allns(msg, 0, GENL_ID_CTRL, GFP_ATOMIC);
		rcu_read_unlock();
	}

	return 0;
}

static struct genl_ops genl_ctrl_ops = {
	.cmd		= CTRL_CMD_GETFAMILY,
	.doit		= ctrl_getfamily,
	.dumpit		= ctrl_dumpfamily,
	.policy		= ctrl_policy,
};

static struct genl_multicast_group notify_grp = {
	.name		= "notify",
};

static int __net_init genl_pernet_init(struct net *net)
{
	/* we'll bump the group number right afterwards */
	net->genl_sock = netlink_kernel_create(net, NETLINK_GENERIC, 0,
					       genl_rcv, &genl_mutex,
					       THIS_MODULE);

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

	err = genl_register_family(&genl_ctrl);
	if (err < 0)
		goto problem;

	err = genl_register_ops(&genl_ctrl, &genl_ctrl_ops);
	if (err < 0)
		goto problem;

	netlink_set_nonroot(NETLINK_GENERIC, NL_NONROOT_RECV);

	err = register_pernet_subsys(&genl_pernet_ops);
	if (err)
		goto problem;

	err = genl_register_mc_group(&genl_ctrl, &notify_grp);
	if (err < 0)
		goto problem;

	return 0;

problem:
	panic("GENL: Cannot register controller: %d\n", err);
}

subsys_initcall(genl_init);

EXPORT_SYMBOL(genl_register_ops);
EXPORT_SYMBOL(genl_unregister_ops);
EXPORT_SYMBOL(genl_register_family);
EXPORT_SYMBOL(genl_unregister_family);

static int genlmsg_mcast(struct sk_buff *skb, u32 pid, unsigned long group,
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
					      pid, group, flags);
			if (err)
				goto error;
		}

		prev = net;
	}

	return nlmsg_multicast(prev->genl_sock, skb, pid, group, flags);
 error:
	kfree_skb(skb);
	return err;
}

int genlmsg_multicast_allns(struct sk_buff *skb, u32 pid, unsigned int group,
			    gfp_t flags)
{
	return genlmsg_mcast(skb, pid, group, flags);
}
EXPORT_SYMBOL(genlmsg_multicast_allns);
