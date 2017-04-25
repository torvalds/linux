#ifndef __NET_GENERIC_NETLINK_H
#define __NET_GENERIC_NETLINK_H

#include <linux/genetlink.h>
#include <net/netlink.h>
#include <net/net_namespace.h>

#define GENLMSG_DEFAULT_SIZE (NLMSG_DEFAULT_SIZE - GENL_HDRLEN)

/**
 * struct genl_multicast_group - generic netlink multicast group
 * @name: name of the multicast group, names are per-family
 */
struct genl_multicast_group {
	char			name[GENL_NAMSIZ];
};

struct genl_ops;
struct genl_info;

/**
 * struct genl_family - generic netlink family
 * @id: protocol family identifier (private)
 * @hdrsize: length of user specific header in bytes
 * @name: name of family
 * @version: protocol version
 * @maxattr: maximum number of attributes supported
 * @netnsok: set to true if the family can handle network
 *	namespaces and should be presented in all of them
 * @parallel_ops: operations can be called in parallel and aren't
 *	synchronized by the core genetlink code
 * @pre_doit: called before an operation's doit callback, it may
 *	do additional, common, filtering and return an error
 * @post_doit: called after an operation's doit callback, it may
 *	undo operations done by pre_doit, for example release locks
 * @mcast_bind: a socket bound to the given multicast group (which
 *	is given as the offset into the groups array)
 * @mcast_unbind: a socket was unbound from the given multicast group.
 *	Note that unbind() will not be called symmetrically if the
 *	generic netlink family is removed while there are still open
 *	sockets.
 * @attrbuf: buffer to store parsed attributes (private)
 * @mcgrps: multicast groups used by this family
 * @n_mcgrps: number of multicast groups
 * @mcgrp_offset: starting number of multicast group IDs in this family
 *	(private)
 * @ops: the operations supported by this family
 * @n_ops: number of operations supported by this family
 */
struct genl_family {
	int			id;		/* private */
	unsigned int		hdrsize;
	char			name[GENL_NAMSIZ];
	unsigned int		version;
	unsigned int		maxattr;
	bool			netnsok;
	bool			parallel_ops;
	int			(*pre_doit)(const struct genl_ops *ops,
					    struct sk_buff *skb,
					    struct genl_info *info);
	void			(*post_doit)(const struct genl_ops *ops,
					     struct sk_buff *skb,
					     struct genl_info *info);
	int			(*mcast_bind)(struct net *net, int group);
	void			(*mcast_unbind)(struct net *net, int group);
	struct nlattr **	attrbuf;	/* private */
	const struct genl_ops *	ops;
	const struct genl_multicast_group *mcgrps;
	unsigned int		n_ops;
	unsigned int		n_mcgrps;
	unsigned int		mcgrp_offset;	/* private */
	struct module		*module;
};

struct nlattr **genl_family_attrbuf(const struct genl_family *family);

/**
 * struct genl_info - receiving information
 * @snd_seq: sending sequence number
 * @snd_portid: netlink portid of sender
 * @nlhdr: netlink message header
 * @genlhdr: generic netlink message header
 * @userhdr: user specific header
 * @attrs: netlink attributes
 * @_net: network namespace
 * @user_ptr: user pointers
 */
struct genl_info {
	u32			snd_seq;
	u32			snd_portid;
	struct nlmsghdr *	nlhdr;
	struct genlmsghdr *	genlhdr;
	void *			userhdr;
	struct nlattr **	attrs;
	possible_net_t		_net;
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
 * @start: start callback for dumps
 * @dumpit: callback for dumpers
 * @done: completion callback for dumps
 * @ops_list: operations list
 */
struct genl_ops {
	const struct nla_policy	*policy;
	int		       (*doit)(struct sk_buff *skb,
				       struct genl_info *info);
	int		       (*start)(struct netlink_callback *cb);
	int		       (*dumpit)(struct sk_buff *skb,
					 struct netlink_callback *cb);
	int		       (*done)(struct netlink_callback *cb);
	u8			cmd;
	u8			internal_flags;
	u8			flags;
};

int genl_register_family(struct genl_family *family);
int genl_unregister_family(const struct genl_family *family);
void genl_notify(const struct genl_family *family, struct sk_buff *skb,
		 struct genl_info *info, u32 group, gfp_t flags);

void *genlmsg_put(struct sk_buff *skb, u32 portid, u32 seq,
		  const struct genl_family *family, int flags, u8 cmd);

/**
 * genlmsg_nlhdr - Obtain netlink header from user specified header
 * @user_hdr: user header as returned from genlmsg_put()
 * @family: generic netlink family
 *
 * Returns pointer to netlink header.
 */
static inline struct nlmsghdr *
genlmsg_nlhdr(void *user_hdr, const struct genl_family *family)
{
	return (struct nlmsghdr *)((char *)user_hdr -
				   family->hdrsize -
				   GENL_HDRLEN -
				   NLMSG_HDRLEN);
}

/**
 * genlmsg_parse - parse attributes of a genetlink message
 * @nlh: netlink message header
 * @family: genetlink message family
 * @tb: destination array with maxtype+1 elements
 * @maxtype: maximum attribute type to be expected
 * @policy: validation policy
 * */
static inline int genlmsg_parse(const struct nlmsghdr *nlh,
				const struct genl_family *family,
				struct nlattr *tb[], int maxtype,
				const struct nla_policy *policy)
{
	return nlmsg_parse(nlh, family->hdrsize + GENL_HDRLEN, tb, maxtype,
			   policy);
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
					      const struct genl_family *family)
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
