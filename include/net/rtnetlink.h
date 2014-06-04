#ifndef __NET_RTNETLINK_H
#define __NET_RTNETLINK_H

#include <linux/rtnetlink.h>
#include <net/netlink.h>

typedef int (*rtnl_doit_func)(struct sk_buff *, struct nlmsghdr *);
typedef int (*rtnl_dumpit_func)(struct sk_buff *, struct netlink_callback *);
typedef u16 (*rtnl_calcit_func)(struct sk_buff *, struct nlmsghdr *);

int __rtnl_register(int protocol, int msgtype,
		    rtnl_doit_func, rtnl_dumpit_func, rtnl_calcit_func);
void rtnl_register(int protocol, int msgtype,
		   rtnl_doit_func, rtnl_dumpit_func, rtnl_calcit_func);
int rtnl_unregister(int protocol, int msgtype);
void rtnl_unregister_all(int protocol);

static inline int rtnl_msg_family(const struct nlmsghdr *nlh)
{
	if (nlmsg_len(nlh) >= sizeof(struct rtgenmsg))
		return ((struct rtgenmsg *) nlmsg_data(nlh))->rtgen_family;
	else
		return AF_UNSPEC;
}

/**
 *	struct rtnl_link_ops - rtnetlink link operations
 *
 *	@list: Used internally
 *	@kind: Identifier
 *	@maxtype: Highest device specific netlink attribute number
 *	@policy: Netlink policy for device specific attribute validation
 *	@validate: Optional validation function for netlink/changelink parameters
 *	@priv_size: sizeof net_device private space
 *	@setup: net_device setup function
 *	@newlink: Function for configuring and registering a new device
 *	@changelink: Function for changing parameters of an existing device
 *	@dellink: Function to remove a device
 *	@get_size: Function to calculate required room for dumping device
 *		   specific netlink attributes
 *	@fill_info: Function to dump device specific netlink attributes
 *	@get_xstats_size: Function to calculate required room for dumping device
 *			  specific statistics
 *	@fill_xstats: Function to dump device specific statistics
 *	@get_num_tx_queues: Function to determine number of transmit queues
 *			    to create when creating a new device.
 *	@get_num_rx_queues: Function to determine number of receive queues
 *			    to create when creating a new device.
 */
struct rtnl_link_ops {
	struct list_head	list;

	const char		*kind;

	size_t			priv_size;
	void			(*setup)(struct net_device *dev);

	int			maxtype;
	const struct nla_policy	*policy;
	int			(*validate)(struct nlattr *tb[],
					    struct nlattr *data[]);

	int			(*newlink)(struct net *src_net,
					   struct net_device *dev,
					   struct nlattr *tb[],
					   struct nlattr *data[]);
	int			(*changelink)(struct net_device *dev,
					      struct nlattr *tb[],
					      struct nlattr *data[]);
	void			(*dellink)(struct net_device *dev,
					   struct list_head *head);

	size_t			(*get_size)(const struct net_device *dev);
	int			(*fill_info)(struct sk_buff *skb,
					     const struct net_device *dev);

	size_t			(*get_xstats_size)(const struct net_device *dev);
	int			(*fill_xstats)(struct sk_buff *skb,
					       const struct net_device *dev);
	unsigned int		(*get_num_tx_queues)(void);
	unsigned int		(*get_num_rx_queues)(void);

	int			slave_maxtype;
	const struct nla_policy	*slave_policy;
	int			(*slave_validate)(struct nlattr *tb[],
						  struct nlattr *data[]);
	int			(*slave_changelink)(struct net_device *dev,
						    struct net_device *slave_dev,
						    struct nlattr *tb[],
						    struct nlattr *data[]);
	size_t			(*get_slave_size)(const struct net_device *dev,
						  const struct net_device *slave_dev);
	int			(*fill_slave_info)(struct sk_buff *skb,
						   const struct net_device *dev,
						   const struct net_device *slave_dev);
};

int __rtnl_link_register(struct rtnl_link_ops *ops);
void __rtnl_link_unregister(struct rtnl_link_ops *ops);

int rtnl_link_register(struct rtnl_link_ops *ops);
void rtnl_link_unregister(struct rtnl_link_ops *ops);

/**
 * 	struct rtnl_af_ops - rtnetlink address family operations
 *
 *	@list: Used internally
 * 	@family: Address family
 * 	@fill_link_af: Function to fill IFLA_AF_SPEC with address family
 * 		       specific netlink attributes.
 * 	@get_link_af_size: Function to calculate size of address family specific
 * 			   netlink attributes.
 *	@validate_link_af: Validate a IFLA_AF_SPEC attribute, must check attr
 *			   for invalid configuration settings.
 * 	@set_link_af: Function to parse a IFLA_AF_SPEC attribute and modify
 *		      net_device accordingly.
 */
struct rtnl_af_ops {
	struct list_head	list;
	int			family;

	int			(*fill_link_af)(struct sk_buff *skb,
						const struct net_device *dev);
	size_t			(*get_link_af_size)(const struct net_device *dev);

	int			(*validate_link_af)(const struct net_device *dev,
						    const struct nlattr *attr);
	int			(*set_link_af)(struct net_device *dev,
					       const struct nlattr *attr);
};

void __rtnl_af_unregister(struct rtnl_af_ops *ops);

void rtnl_af_register(struct rtnl_af_ops *ops);
void rtnl_af_unregister(struct rtnl_af_ops *ops);

struct net *rtnl_link_get_net(struct net *src_net, struct nlattr *tb[]);
struct net_device *rtnl_create_link(struct net *net, char *ifname,
				    const struct rtnl_link_ops *ops,
				    struct nlattr *tb[]);
int rtnl_configure_link(struct net_device *dev, const struct ifinfomsg *ifm);

int rtnl_nla_parse_ifla(struct nlattr **tb, const struct nlattr *head, int len);

#define MODULE_ALIAS_RTNL_LINK(kind) MODULE_ALIAS("rtnl-link-" kind)

#endif
