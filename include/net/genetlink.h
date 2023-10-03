/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NET_GENERIC_NETLINK_H
#define __NET_GENERIC_NETLINK_H

#include <linux/genetlink.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

#define GENLMSG_DEFAULT_SIZE (NLMSG_DEFAULT_SIZE - GENL_HDRLEN)

/**
 * struct genl_multicast_group - generic netlink multicast group
 * @name: name of the multicast group, names are per-family
 * @flags: GENL_* flags (%GENL_ADMIN_PERM or %GENL_UNS_ADMIN_PERM)
 */
struct genl_multicast_group {
	char			name[GENL_NAMSIZ];
	u8			flags;
};

struct genl_split_ops;
struct genl_info;

/**
 * struct genl_family - generic netlink family
 * @hdrsize: length of user specific header in bytes
 * @name: name of family
 * @version: protocol version
 * @maxattr: maximum number of attributes supported
 * @policy: netlink policy
 * @netnsok: set to true if the family can handle network
 *	namespaces and should be presented in all of them
 * @parallel_ops: operations can be called in parallel and aren't
 *	synchronized by the core genetlink code
 * @pre_doit: called before an operation's doit callback, it may
 *	do additional, common, filtering and return an error
 * @post_doit: called after an operation's doit callback, it may
 *	undo operations done by pre_doit, for example release locks
 * @module: pointer to the owning module (set to THIS_MODULE)
 * @mcgrps: multicast groups used by this family
 * @n_mcgrps: number of multicast groups
 * @resv_start_op: first operation for which reserved fields of the header
 *	can be validated and policies are required (see below);
 *	new families should leave this field at zero
 * @ops: the operations supported by this family
 * @n_ops: number of operations supported by this family
 * @small_ops: the small-struct operations supported by this family
 * @n_small_ops: number of small-struct operations supported by this family
 * @split_ops: the split do/dump form of operation definition
 * @n_split_ops: number of entries in @split_ops, not that with split do/dump
 *	ops the number of entries is not the same as number of commands
 *
 * Attribute policies (the combination of @policy and @maxattr fields)
 * can be attached at the family level or at the operation level.
 * If both are present the per-operation policy takes precedence.
 * For operations before @resv_start_op lack of policy means that the core
 * will perform no attribute parsing or validation. For newer operations
 * if policy is not provided core will reject all TLV attributes.
 */
struct genl_family {
	unsigned int		hdrsize;
	char			name[GENL_NAMSIZ];
	unsigned int		version;
	unsigned int		maxattr;
	u8			netnsok:1;
	u8			parallel_ops:1;
	u8			n_ops;
	u8			n_small_ops;
	u8			n_split_ops;
	u8			n_mcgrps;
	u8			resv_start_op;
	const struct nla_policy *policy;
	int			(*pre_doit)(const struct genl_split_ops *ops,
					    struct sk_buff *skb,
					    struct genl_info *info);
	void			(*post_doit)(const struct genl_split_ops *ops,
					     struct sk_buff *skb,
					     struct genl_info *info);
	const struct genl_ops *	ops;
	const struct genl_small_ops *small_ops;
	const struct genl_split_ops *split_ops;
	const struct genl_multicast_group *mcgrps;
	struct module		*module;

/* private: internal use only */
	/* protocol family identifier */
	int			id;
	/* starting number of multicast group IDs in this family */
	unsigned int		mcgrp_offset;
};

/**
 * struct genl_info - receiving information
 * @snd_seq: sending sequence number
 * @snd_portid: netlink portid of sender
 * @family: generic netlink family
 * @nlhdr: netlink message header
 * @genlhdr: generic netlink message header
 * @attrs: netlink attributes
 * @_net: network namespace
 * @user_ptr: user pointers
 * @extack: extended ACK report struct
 */
struct genl_info {
	u32			snd_seq;
	u32			snd_portid;
	const struct genl_family *family;
	const struct nlmsghdr *	nlhdr;
	struct genlmsghdr *	genlhdr;
	struct nlattr **	attrs;
	possible_net_t		_net;
	void *			user_ptr[2];
	struct netlink_ext_ack *extack;
};

static inline struct net *genl_info_net(const struct genl_info *info)
{
	return read_pnet(&info->_net);
}

static inline void genl_info_net_set(struct genl_info *info, struct net *net)
{
	write_pnet(&info->_net, net);
}

static inline void *genl_info_userhdr(const struct genl_info *info)
{
	return (u8 *)info->genlhdr + GENL_HDRLEN;
}

#define GENL_SET_ERR_MSG(info, msg) NL_SET_ERR_MSG((info)->extack, msg)

#define GENL_SET_ERR_MSG_FMT(info, msg, args...) \
	NL_SET_ERR_MSG_FMT((info)->extack, msg, ##args)

/* Report that a root attribute is missing */
#define GENL_REQ_ATTR_CHECK(info, attr) ({				\
	struct genl_info *__info = (info);				\
									\
	NL_REQ_ATTR_CHECK(__info->extack, NULL, __info->attrs, (attr)); \
})

enum genl_validate_flags {
	GENL_DONT_VALIDATE_STRICT		= BIT(0),
	GENL_DONT_VALIDATE_DUMP			= BIT(1),
	GENL_DONT_VALIDATE_DUMP_STRICT		= BIT(2),
};

/**
 * struct genl_small_ops - generic netlink operations (small version)
 * @cmd: command identifier
 * @internal_flags: flags used by the family
 * @flags: GENL_* flags (%GENL_ADMIN_PERM or %GENL_UNS_ADMIN_PERM)
 * @validate: validation flags from enum genl_validate_flags
 * @doit: standard command callback
 * @dumpit: callback for dumpers
 *
 * This is a cut-down version of struct genl_ops for users who don't need
 * most of the ancillary infra and want to save space.
 */
struct genl_small_ops {
	int	(*doit)(struct sk_buff *skb, struct genl_info *info);
	int	(*dumpit)(struct sk_buff *skb, struct netlink_callback *cb);
	u8	cmd;
	u8	internal_flags;
	u8	flags;
	u8	validate;
};

/**
 * struct genl_ops - generic netlink operations
 * @cmd: command identifier
 * @internal_flags: flags used by the family
 * @flags: GENL_* flags (%GENL_ADMIN_PERM or %GENL_UNS_ADMIN_PERM)
 * @maxattr: maximum number of attributes supported
 * @policy: netlink policy (takes precedence over family policy)
 * @validate: validation flags from enum genl_validate_flags
 * @doit: standard command callback
 * @start: start callback for dumps
 * @dumpit: callback for dumpers
 * @done: completion callback for dumps
 */
struct genl_ops {
	int		       (*doit)(struct sk_buff *skb,
				       struct genl_info *info);
	int		       (*start)(struct netlink_callback *cb);
	int		       (*dumpit)(struct sk_buff *skb,
					 struct netlink_callback *cb);
	int		       (*done)(struct netlink_callback *cb);
	const struct nla_policy *policy;
	unsigned int		maxattr;
	u8			cmd;
	u8			internal_flags;
	u8			flags;
	u8			validate;
};

/**
 * struct genl_split_ops - generic netlink operations (do/dump split version)
 * @cmd: command identifier
 * @internal_flags: flags used by the family
 * @flags: GENL_* flags (%GENL_ADMIN_PERM or %GENL_UNS_ADMIN_PERM)
 * @validate: validation flags from enum genl_validate_flags
 * @policy: netlink policy (takes precedence over family policy)
 * @maxattr: maximum number of attributes supported
 *
 * Do callbacks:
 * @pre_doit: called before an operation's @doit callback, it may
 *	do additional, common, filtering and return an error
 * @doit: standard command callback
 * @post_doit: called after an operation's @doit callback, it may
 *	undo operations done by pre_doit, for example release locks
 *
 * Dump callbacks:
 * @start: start callback for dumps
 * @dumpit: callback for dumpers
 * @done: completion callback for dumps
 *
 * Do callbacks can be used if %GENL_CMD_CAP_DO is set in @flags.
 * Dump callbacks can be used if %GENL_CMD_CAP_DUMP is set in @flags.
 * Exactly one of those flags must be set.
 */
struct genl_split_ops {
	union {
		struct {
			int (*pre_doit)(const struct genl_split_ops *ops,
					struct sk_buff *skb,
					struct genl_info *info);
			int (*doit)(struct sk_buff *skb,
				    struct genl_info *info);
			void (*post_doit)(const struct genl_split_ops *ops,
					  struct sk_buff *skb,
					  struct genl_info *info);
		};
		struct {
			int (*start)(struct netlink_callback *cb);
			int (*dumpit)(struct sk_buff *skb,
				      struct netlink_callback *cb);
			int (*done)(struct netlink_callback *cb);
		};
	};
	const struct nla_policy *policy;
	unsigned int		maxattr;
	u8			cmd;
	u8			internal_flags;
	u8			flags;
	u8			validate;
};

/**
 * struct genl_dumpit_info - info that is available during dumpit op call
 * @op: generic netlink ops - for internal genl code usage
 * @attrs: netlink attributes
 * @info: struct genl_info describing the request
 */
struct genl_dumpit_info {
	struct genl_split_ops op;
	struct genl_info info;
};

static inline const struct genl_dumpit_info *
genl_dumpit_info(struct netlink_callback *cb)
{
	return cb->data;
}

static inline const struct genl_info *
genl_info_dump(struct netlink_callback *cb)
{
	return &genl_dumpit_info(cb)->info;
}

/**
 * genl_info_init_ntf() - initialize genl_info for notifications
 * @info:   genl_info struct to set up
 * @family: pointer to the genetlink family
 * @cmd:    command to be used in the notification
 *
 * Initialize a locally declared struct genl_info to pass to various APIs.
 * Intended to be used when creating notifications.
 */
static inline void
genl_info_init_ntf(struct genl_info *info, const struct genl_family *family,
		   u8 cmd)
{
	struct genlmsghdr *hdr = (void *) &info->user_ptr[0];

	memset(info, 0, sizeof(*info));
	info->family = family;
	info->genlhdr = hdr;
	hdr->cmd = cmd;
}

static inline bool genl_info_is_ntf(const struct genl_info *info)
{
	return !info->nlhdr;
}

int genl_register_family(struct genl_family *family);
int genl_unregister_family(const struct genl_family *family);
void genl_notify(const struct genl_family *family, struct sk_buff *skb,
		 struct genl_info *info, u32 group, gfp_t flags);

void *genlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
		  const struct genl_family *family, int flags, u8 cmd);

static inline void *
__genlmsg_iput(struct sk_buff *skb, const struct genl_info *info, int flags)
{
	return genlmsg_put(skb, info->snd_portid, info->snd_seq, info->family,
			   flags, info->genlhdr->cmd);
}

/**
 * genlmsg_iput - start genetlink message based on genl_info
 * @skb: skb in which message header will be placed
 * @info: genl_info as provided to do/dump handlers
 *
 * Convenience wrapper which starts a genetlink message based on
 * information in user request. @info should be either the struct passed
 * by genetlink core to do/dump handlers (when constructing replies to
 * such requests) or a struct initialized by genl_info_init_ntf()
 * when constructing notifications.
 *
 * Returns pointer to new genetlink header.
 */
static inline void *
genlmsg_iput(struct sk_buff *skb, const struct genl_info *info)
{
	return __genlmsg_iput(skb, info, 0);
}

/**
 * genlmsg_nlhdr - Obtain netlink header from user specified header
 * @user_hdr: user header as returned from genlmsg_put()
 *
 * Returns pointer to netlink header.
 */
static inline struct nlmsghdr *genlmsg_nlhdr(void *user_hdr)
{
	return (struct nlmsghdr *)((char *)user_hdr -
				   GENL_HDRLEN -
				   NLMSG_HDRLEN);
}

/**
 * genlmsg_parse_deprecated - parse attributes of a genetlink message
 * @nlh: netlink message header
 * @family: genetlink message family
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 * @extack: extended ACK report struct
 */
static inline int genlmsg_parse_deprecated(const struct nlmsghdr *nlh,
					   const struct genl_family *family,
					   struct nlattr *tb[], int maxtype,
					   const struct nla_policy *policy,
					   struct netlink_ext_ack *extack)
{
	return __nlmsg_parse(nlh, family->hdrsize + GENL_HDRLEN, tb, maxtype,
			     policy, NL_VALIDATE_LIBERAL, extack);
}

/**
 * genlmsg_parse - parse attributes of a genetlink message
 * @nlh: netlink message header
 * @family: genetlink message family
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 * @extack: extended ACK report struct
 */
static inline int genlmsg_parse(const struct nlmsghdr *nlh,
				const struct genl_family *family,
				struct nlattr *tb[], int maxtype,
				const struct nla_policy *policy,
				struct netlink_ext_ack *extack)
{
	return __nlmsg_parse(nlh, family->hdrsize + GENL_HDRLEN, tb, maxtype,
			     policy, NL_VALIDATE_STRICT, extack);
}

/**
 * genl_dump_check_consistent - check if sequence is consistent and advertise if not
 * @cb: netlink callback structure that stores the sequence number
 * @user_hdr: user header as returned from genlmsg_put()
 *
 * Cf. nl_dump_check_consistent(), this just provides a wrapper to make it
 * simpler to use with generic netlink.
 */
static inline void genl_dump_check_consistent(struct netlink_callback *cb,
					      void *user_hdr)
{
	nl_dump_check_consistent(cb, genlmsg_nlhdr(user_hdr));
}

/**
 * genlmsg_put_reply - Add generic netlink header to a reply message
 * @skb: socket buffer holding the message
 * @info: receiver info
 * @family: generic netlink family
 * @flags: netlink message flags
 * @cmd: generic netlink command
 *
 * Returns pointer to user specific header
 */
static inline void *genlmsg_put_reply(struct sk_buff *skb,
				      struct genl_info *info,
				      const struct genl_family *family,
				      int flags, u8 cmd)
{
	return genlmsg_put(skb, info->snd_portid, info->snd_seq, family,
			   flags, cmd);
}

/**
 * genlmsg_end - Finalize a generic netlink message
 * @skb: socket buffer the message is stored in
 * @hdr: user specific header
 */
static inline void genlmsg_end(struct sk_buff *skb, void *hdr)
{
	nlmsg_end(skb, hdr - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_cancel - Cancel construction of a generic netlink message
 * @skb: socket buffer the message is stored in
 * @hdr: generic netlink message header
 */
static inline void genlmsg_cancel(struct sk_buff *skb, void *hdr)
{
	if (hdr)
		nlmsg_cancel(skb, hdr - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_multicast_netns - multicast a netlink message to a specific netns
 * @family: the generic netlink family
 * @net: the net namespace
 * @skb: netlink message as socket buffer
 * @portid: own netlink portid to avoid sending to yourself
 * @group: offset of multicast group in groups array
 * @flags: allocation flags
 */
static inline int genlmsg_multicast_netns(const struct genl_family *family,
					  struct net *net, struct sk_buff *skb,
					  u32 portid, unsigned int group, gfp_t flags)
{
	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return -EINVAL;
	group = family->mcgrp_offset + group;
	return nlmsg_multicast(net->genl_sock, skb, portid, group, flags);
}

/**
 * genlmsg_multicast - multicast a netlink message to the default netns
 * @family: the generic netlink family
 * @skb: netlink message as socket buffer
 * @portid: own netlink portid to avoid sending to yourself
 * @group: offset of multicast group in groups array
 * @flags: allocation flags
 */
static inline int genlmsg_multicast(const struct genl_family *family,
				    struct sk_buff *skb, u32 portid,
				    unsigned int group, gfp_t flags)
{
	return genlmsg_multicast_netns(family, &init_net, skb,
				       portid, group, flags);
}

/**
 * genlmsg_multicast_allns - multicast a netlink message to all net namespaces
 * @family: the generic netlink family
 * @skb: netlink message as socket buffer
 * @portid: own netlink portid to avoid sending to yourself
 * @group: offset of multicast group in groups array
 * @flags: allocation flags
 *
 * This function must hold the RTNL or rcu_read_lock().
 */
int genlmsg_multicast_allns(const struct genl_family *family,
			    struct sk_buff *skb, u32 portid,
			    unsigned int group, gfp_t flags);

/**
 * genlmsg_unicast - unicast a netlink message
 * @net: network namespace to look up @portid in
 * @skb: netlink message as socket buffer
 * @portid: netlink portid of the destination socket
 */
static inline int genlmsg_unicast(struct net *net, struct sk_buff *skb, u32 portid)
{
	return nlmsg_unicast(net->genl_sock, skb, portid);
}

/**
 * genlmsg_reply - reply to a request
 * @skb: netlink message to be sent back
 * @info: receiver information
 */
static inline int genlmsg_reply(struct sk_buff *skb, struct genl_info *info)
{
	return genlmsg_unicast(genl_info_net(info), skb, info->snd_portid);
}

/**
 * genlmsg_data - head of message payload
 * @gnlh: genetlink message header
 */
static inline void *genlmsg_data(const struct genlmsghdr *gnlh)
{
	return ((unsigned char *) gnlh + GENL_HDRLEN);
}

/**
 * genlmsg_len - length of message payload
 * @gnlh: genetlink message header
 */
static inline int genlmsg_len(const struct genlmsghdr *gnlh)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)((unsigned char *)gnlh -
							NLMSG_HDRLEN);
	return (nlh->nlmsg_len - GENL_HDRLEN - NLMSG_HDRLEN);
}

/**
 * genlmsg_msg_size - length of genetlink message not including padding
 * @payload: length of message payload
 */
static inline int genlmsg_msg_size(int payload)
{
	return GENL_HDRLEN + payload;
}

/**
 * genlmsg_total_size - length of genetlink message including padding
 * @payload: length of message payload
 */
static inline int genlmsg_total_size(int payload)
{
	return NLMSG_ALIGN(genlmsg_msg_size(payload));
}

/**
 * genlmsg_new - Allocate a new generic netlink message
 * @payload: size of the message payload
 * @flags: the type of memory to allocate.
 */
static inline struct sk_buff *genlmsg_new(size_t payload, gfp_t flags)
{
	return nlmsg_new(genlmsg_total_size(payload), flags);
}

/**
 * genl_set_err - report error to genetlink broadcast listeners
 * @family: the generic netlink family
 * @net: the network namespace to report the error to
 * @portid: the PORTID of a process that we want to skip (if any)
 * @group: the broadcast group that will notice the error
 * 	(this is the offset of the multicast group in the groups array)
 * @code: error code, must be negative (as usual in kernelspace)
 *
 * This function returns the number of broadcast listeners that have set the
 * NETLINK_RECV_NO_ENOBUFS socket option.
 */
static inline int genl_set_err(const struct genl_family *family,
			       struct net *net, u32 portid,
			       u32 group, int code)
{
	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return -EINVAL;
	group = family->mcgrp_offset + group;
	return netlink_set_err(net->genl_sock, portid, group, code);
}

static inline int genl_has_listeners(const struct genl_family *family,
				     struct net *net, unsigned int group)
{
	if (WARN_ON_ONCE(group >= family->n_mcgrps))
		return -EINVAL;
	group = family->mcgrp_offset + group;
	return netlink_has_listeners(net->genl_sock, group);
}
#endif	/* __NET_GENERIC_NETLINK_H */
