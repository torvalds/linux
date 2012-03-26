#ifndef __NET_GENERIC_NETLINK_H
#define __NET_GENERIC_NETLINK_H

#include <linux/genetlink.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

/**
 * struct genl_multicast_group - generic netlink multicast group
 * @name: name of the multicast group, names are per-family
 * @id: multicast group ID, assigned by the core, to use with
 *      genlmsg_multicast().
 * @list: list entry for linking
 * @family: pointer to family, need not be set before registering
 */
struct genl_multicast_group {
	struct genl_family	*family;	/* private */
	struct list_head	list;		/* private */
	char			name[GENL_NAMSIZ];
	u32			id;
};

struct genl_ops;
struct genl_info;

/**
 * struct genl_family - generic netlink family
 * @id: protocol family idenfitier
 * @hdrsize: length of user specific header in bytes
 * @name: name of family
 * @version: protocol version
 * @maxattr: maximum number of attributes supported
 * @netnsok: set to true if the family can handle network
 *	namespaces and should be presented in all of them
 * @pre_doit: called before an operation's doit callback, it may
 *	do additional, common, filtering and return an error
 * @post_doit: called after an operation's doit callback, it may
 *	undo operations done by pre_doit, for example release locks
 * @attrbuf: buffer to store parsed attributes
 * @ops_list: list of all assigned operations
 * @family_list: family list
 * @mcast_groups: multicast groups list
 */
struct genl_family {
	unsigned int		id;
	unsigned int		hdrsize;
	char			name[GENL_NAMSIZ];
	unsigned int		version;
	unsigned int		maxattr;
	bool			netnsok;
	int			(*pre_doit)(struct genl_ops *ops,
					    struct sk_buff *skb,
					    struct genl_info *info);
	void			(*post_doit)(struct genl_ops *ops,
					     struct sk_buff *skb,
					     struct genl_info *info);
	struct nlattr **	attrbuf;	/* private */
	struct list_head	ops_list;	/* private */
	struct list_head	family_list;	/* private */
	struct list_head	mcast_groups;	/* private */
};

/**
 * struct genl_info - receiving information
 * @snd_seq: sending sequence number
 * @snd_pid: netlink pid of sender
 * @nlhdr: netlink message header
 * @genlhdr: generic netlink message header
 * @userhdr: user specific header
 * @attrs: netlink attributes
 * @_net: network namespace
 * @user_ptr: user pointers
 */
struct genl_info {
	u32			snd_seq;
	u32			snd_pid;
	struct nlmsghdr *	nlhdr;
	struct genlmsghdr *	genlhdr;
	void *			userhdr;
	struct nlattr **	attrs;
#ifdef CONFIG_NET_NS
	struct net *		_net;
#endif
	void *			user_ptr[2];
};

static inline struct net *genl_info_net(struct genl_info *info)
{
	return read_pnet(&info->_net);
}

static inline void genl_info_net_set(struct genl_info *info, struct net *net)
{
	write_pnet(&info->_net, net);
}

/**
 * struct genl_ops - generic netlink operations
 * @cmd: command identifier
 * @internal_flags: flags used by the family
 * @flags: flags
 * @policy: attribute validation policy
 * @doit: standard command callback
 * @dumpit: callback for dumpers
 * @done: completion callback for dumps
 * @ops_list: operations list
 */
struct genl_ops {
	u8			cmd;
	u8			internal_flags;
	unsigned int		flags;
	const struct nla_policy	*policy;
	int		       (*doit)(struct sk_buff *skb,
				       struct genl_info *info);
	int		       (*dumpit)(struct sk_buff *skb,
					 struct netlink_callback *cb);
	int		       (*done)(struct netlink_callback *cb);
	struct list_head	ops_list;
};

extern int genl_register_family(struct genl_family *family);
extern int genl_register_family_with_ops(struct genl_family *family,
	struct genl_ops *ops, size_t n_ops);
extern int genl_unregister_family(struct genl_family *family);
extern int genl_register_ops(struct genl_family *, struct genl_ops *ops);
extern int genl_unregister_ops(struct genl_family *, struct genl_ops *ops);
extern int genl_register_mc_group(struct genl_family *family,
				  struct genl_multicast_group *grp);
extern void genl_unregister_mc_group(struct genl_family *family,
				     struct genl_multicast_group *grp);
extern void genl_notify(struct sk_buff *skb, struct net *net, u32 pid,
			u32 group, struct nlmsghdr *nlh, gfp_t flags);

void *genlmsg_put(struct sk_buff *skb, u32 pid, u32 seq,
				struct genl_family *family, int flags, u8 cmd);

/**
 * genlmsg_nlhdr - Obtain netlink header from user specified header
 * @user_hdr: user header as returned from genlmsg_put()
 * @family: generic netlink family
 *
 * Returns pointer to netlink header.
 */
static inline struct nlmsghdr *genlmsg_nlhdr(void *user_hdr,
					     struct genl_family *family)
{
	return (struct nlmsghdr *)((char *)user_hdr -
				   family->hdrsize -
				   GENL_HDRLEN -
				   NLMSG_HDRLEN);
}

/**
 * genl_dump_check_consistent - check if sequence is consistent and advertise if not
 * @cb: netlink callback structure that stores the sequence number
 * @user_hdr: user header as returned from genlmsg_put()
 * @family: generic netlink family
 *
 * Cf. nl_dump_check_consistent(), this just provides a wrapper to make it
 * simpler to use with generic netlink.
 */
static inline void genl_dump_check_consistent(struct netlink_callback *cb,
					      void *user_hdr,
					      struct genl_family *family)
{
	nl_dump_check_consistent(cb, genlmsg_nlhdr(user_hdr, family));
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
				      struct genl_family *family,
				      int flags, u8 cmd)
{
	return genlmsg_put(skb, info->snd_pid, info->snd_seq, family,
			   flags, cmd);
}

/**
 * genlmsg_end - Finalize a generic netlink message
 * @skb: socket buffer the message is stored in
 * @hdr: user specific header
 */
static inline int genlmsg_end(struct sk_buff *skb, void *hdr)
{
	return nlmsg_end(skb, hdr - GENL_HDRLEN - NLMSG_HDRLEN);
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
 * @net: the net namespace
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 */
static inline int genlmsg_multicast_netns(struct net *net, struct sk_buff *skb,
					  u32 pid, unsigned int group, gfp_t flags)
{
	return nlmsg_multicast(net->genl_sock, skb, pid, group, flags);
}

/**
 * genlmsg_multicast - multicast a netlink message to the default netns
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 */
static inline int genlmsg_multicast(struct sk_buff *skb, u32 pid,
				    unsigned int group, gfp_t flags)
{
	return genlmsg_multicast_netns(&init_net, skb, pid, group, flags);
}

/**
 * genlmsg_multicast_allns - multicast a netlink message to all net namespaces
 * @skb: netlink message as socket buffer
 * @pid: own netlink pid to avoid sending to yourself
 * @group: multicast group id
 * @flags: allocation flags
 *
 * This function must hold the RTNL or rcu_read_lock().
 */
int genlmsg_multicast_allns(struct sk_buff *skb, u32 pid,
			    unsigned int group, gfp_t flags);

/**
 * genlmsg_unicast - unicast a netlink message
 * @skb: netlink message as socket buffer
 * @pid: netlink pid of the destination socket
 */
static inline int genlmsg_unicast(struct net *net, struct sk_buff *skb, u32 pid)
{
	return nlmsg_unicast(net->genl_sock, skb, pid);
}

/**
 * genlmsg_reply - reply to a request
 * @skb: netlink message to be sent back
 * @info: receiver information
 */
static inline int genlmsg_reply(struct sk_buff *skb, struct genl_info *info)
{
	return genlmsg_unicast(genl_info_net(info), skb, info->snd_pid);
}

/**
 * gennlmsg_data - head of message payload
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


#endif	/* __NET_GENERIC_NETLINK_H */
