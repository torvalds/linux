/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	Bridge per vlan tunnels
 *
 *	Authors:
 *	Roopa Prabhu		<roopa@cumulusnetworks.com>
 */

#ifndef _BR_PRIVATE_TUNNEL_H
#define _BR_PRIVATE_TUNNEL_H

struct vtunnel_info {
	u32	tunid;
	u16	vid;
	u16	flags;
};

/* br_netlink_tunnel.c */
int br_parse_vlan_tunnel_info(struct nlattr *attr,
			      struct vtunnel_info *tinfo);
int br_process_vlan_tunnel_info(const struct net_bridge *br,
				const struct net_bridge_port *p,
				int cmd,
				struct vtunnel_info *tinfo_curr,
				struct vtunnel_info *tinfo_last,
				bool *changed);
int br_get_vlan_tunnel_info_size(struct net_bridge_vlan_group *vg);
int br_fill_vlan_tunnel_info(struct sk_buff *skb,
			     struct net_bridge_vlan_group *vg);

#ifdef CONFIG_BRIDGE_VLAN_FILTERING
/* br_vlan_tunnel.c */
int vlan_tunnel_init(struct net_bridge_vlan_group *vg);
void vlan_tunnel_deinit(struct net_bridge_vlan_group *vg);
int nbp_vlan_tunnel_info_delete(const struct net_bridge_port *port, u16 vid);
int nbp_vlan_tunnel_info_add(const struct net_bridge_port *port, u16 vid,
			     u32 tun_id);
void nbp_vlan_tunnel_info_flush(struct net_bridge_port *port);
void vlan_tunnel_info_del(struct net_bridge_vlan_group *vg,
			  struct net_bridge_vlan *vlan);
void br_handle_ingress_vlan_tunnel(struct sk_buff *skb,
				   struct net_bridge_port *p,
				   struct net_bridge_vlan_group *vg);
int br_handle_egress_vlan_tunnel(struct sk_buff *skb,
				 struct net_bridge_vlan *vlan);
bool vlan_tunid_inrange(const struct net_bridge_vlan *v_curr,
			const struct net_bridge_vlan *v_last);
int br_vlan_tunnel_info(const struct net_bridge_port *p, int cmd,
			u16 vid, u32 tun_id, bool *changed);
#else
static inline int vlan_tunnel_init(struct net_bridge_vlan_group *vg)
{
	return 0;
}

static inline int nbp_vlan_tunnel_info_delete(const struct net_bridge_port *port,
					      u16 vid)
{
	return 0;
}

static inline int nbp_vlan_tunnel_info_add(const struct net_bridge_port *port,
					   u16 vid, u32 tun_id)
{
	return 0;
}

static inline void nbp_vlan_tunnel_info_flush(struct net_bridge_port *port)
{
}

static inline void vlan_tunnel_info_del(struct net_bridge_vlan_group *vg,
					struct net_bridge_vlan *vlan)
{
}

static inline int br_handle_ingress_vlan_tunnel(struct sk_buff *skb,
						struct net_bridge_port *p,
						struct net_bridge_vlan_group *vg)
{
	return 0;
}
#endif

#endif
