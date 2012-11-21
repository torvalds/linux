#ifndef _RDMA_NETLINK_H
#define _RDMA_NETLINK_H


#include <linux/netlink.h>
#include <uapi/rdma/rdma_netlink.h>

struct ibnl_client_cbs {
	int (*dump)(struct sk_buff *skb, struct netlink_callback *nlcb);
	struct module *module;
};

int ibnl_init(void);
void ibnl_cleanup(void);

/**
 * Add a a client to the list of IB netlink exporters.
 * @index: Index of the added client
 * @nops: Number of supported ops by the added client.
 * @cb_table: A table for op->callback
 *
 * Returns 0 on success or a negative error code.
 */
int ibnl_add_client(int index, int nops,
		    const struct ibnl_client_cbs cb_table[]);

/**
 * Remove a client from IB netlink.
 * @index: Index of the removed IB client.
 *
 * Returns 0 on success or a negative error code.
 */
int ibnl_remove_client(int index);

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
		   int len, int client, int op);
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

#endif /* _RDMA_NETLINK_H */
