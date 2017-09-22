#ifndef _RDMA_NETLINK_H
#define _RDMA_NETLINK_H


#include <linux/netlink.h>
#include <uapi/rdma/rdma_netlink.h>

struct rdma_nl_cbs {
	int (*doit)(struct sk_buff *skb, struct nlmsghdr *nlh,
		    struct netlink_ext_ack *extack);
	int (*dump)(struct sk_buff *skb, struct netlink_callback *nlcb);
	u8 flags;
};

enum rdma_nl_flags {
	/* Require CAP_NET_ADMIN */
	RDMA_NL_ADMIN_PERM	= 1 << 0,
};

/* Define this module as providing netlink services for NETLINK_RDMA, with
 * index _index.  Since the client indexes were setup in a uapi header as an
 * enum and we do no want to change that, the user must supply the expanded
 * constant as well and the compiler checks they are the same.
 */
#define MODULE_ALIAS_RDMA_NETLINK(_index, _val)                                \
	static inline void __chk_##_index(void)                                \
	{                                                                      \
		BUILD_BUG_ON(_index != _val);                                  \
	}                                                                      \
	MODULE_ALIAS("rdma-netlink-subsys-" __stringify(_val))

/**
 * Register client in RDMA netlink.
 * @index: Index of the added client
 * @cb_table: A table for op->callback
 */
void rdma_nl_register(unsigned int index,
		      const struct rdma_nl_cbs cb_table[]);

/**
 * Remove a client from IB netlink.
 * @index: Index of the removed IB client.
 */
void rdma_nl_unregister(unsigned int index);

/**
 * Put a new message in a supplied skb.
 * @skb: The netlink skb.
 * @nlh: Pointer to put the header of the new netlink message.
 * @seq: The message sequence number.
 * @len: The requested message length to allocate.
 * @client: Calling IB netlink client.
 * @op: message content op.
 * Returns the allocated buffer on success and NULL on failure.
 */
void *ibnl_put_msg(struct sk_buff *skb, struct nlmsghdr **nlh, int seq,
		   int len, int client, int op, int flags);
/**
 * Put a new attribute in a supplied skb.
 * @skb: The netlink skb.
 * @nlh: Header of the netlink message to append the attribute to.
 * @len: The length of the attribute data.
 * @data: The attribute data to put.
 * @type: The attribute type.
 * Returns the 0 and a negative error code on failure.
 */
int ibnl_put_attr(struct sk_buff *skb, struct nlmsghdr *nlh,
		  int len, void *data, int type);

/**
 * Send the supplied skb to a specific userspace PID.
 * @skb: The netlink skb
 * @pid: Userspace netlink process ID
 * Returns 0 on success or a negative error code.
 */
int rdma_nl_unicast(struct sk_buff *skb, u32 pid);

/**
 * Send, with wait/1 retry, the supplied skb to a specific userspace PID.
 * @skb: The netlink skb
 * @pid: Userspace netlink process ID
 * Returns 0 on success or a negative error code.
 */
int rdma_nl_unicast_wait(struct sk_buff *skb, __u32 pid);

/**
 * Send the supplied skb to a netlink group.
 * @skb: The netlink skb
 * @group: Netlink group ID
 * @flags: allocation flags
 * Returns 0 on success or a negative error code.
 */
int rdma_nl_multicast(struct sk_buff *skb, unsigned int group, gfp_t flags);

/**
 * Check if there are any listeners to the netlink group
 * @group: the netlink group ID
 * Returns 0 on success or a negative for no listeners.
 */
int rdma_nl_chk_listeners(unsigned int group);
#endif /* _RDMA_NETLINK_H */
