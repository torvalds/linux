#ifndef _NET_STP_H
#define _NET_STP_H

struct stp_proto {
	unsigned char	group_address[ETH_ALEN];
	void		(*rcv)(const struct stp_proto *, struct sk_buff *,
			       struct net_device *);
	void		*data;
};

int stp_proto_register(const struct stp_proto *proto);
void stp_proto_unregister(const struct stp_proto *proto);

#endif /* _NET_STP_H */
