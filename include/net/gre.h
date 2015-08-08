#ifndef __LINUX_GRE_H
#define __LINUX_GRE_H

#include <linux/skbuff.h>
#include <net/ip_tunnels.h>

struct gre_base_hdr {
	__be16 flags;
	__be16 protocol;
};
#define GRE_HEADER_SECTION 4

#define GREPROTO_CISCO		0
#define GREPROTO_PPTP		1
#define GREPROTO_MAX		2
#define GRE_IP_PROTO_MAX	2

struct gre_protocol {
	int  (*handler)(struct sk_buff *skb);
	void (*err_handler)(struct sk_buff *skb, u32 info);
};

int gre_add_protocol(const struct gre_protocol *proto, u8 version);
int gre_del_protocol(const struct gre_protocol *proto, u8 version);

struct net_device *gretap_fb_dev_create(struct net *net, const char *name,
				       u8 name_assign_type);
#endif
