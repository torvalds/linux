#ifndef __BEN_VLAN_802_1Q_INC__
#define __BEN_VLAN_802_1Q_INC__

#include <linux/if_vlan.h>
#include <linux/u64_stats_sync.h>


/**
 *	struct vlan_priority_tci_mapping - vlan egress priority mappings
 *	@priority: skb priority
 *	@vlan_qos: vlan priority: (skb->priority << 13) & 0xE000
 *	@next: pointer to next struct
 */
struct vlan_priority_tci_mapping {
	u32					priority;
	u16					vlan_qos;
	struct vlan_priority_tci_mapping	*next;
};


/**
 *	struct vlan_pcpu_stats - VLAN percpu rx/tx stats
 *	@rx_packets: number of received packets
 *	@rx_bytes: number of received bytes
 *	@rx_multicast: number of received multicast packets
 *	@tx_packets: number of transmitted packets
 *	@tx_bytes: number of transmitted bytes
 *	@syncp: synchronization point for 64bit counters
 *	@rx_errors: number of rx errors
 *	@tx_dropped: number of tx drops
 */
struct vlan_pcpu_stats {
	u64			rx_packets;
	u64			rx_bytes;
	u64			rx_multicast;
	u64			tx_packets;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
	u32			rx_errors;
	u32			tx_dropped;
};

/**
 *	struct vlan_dev_info - VLAN private device data
 *	@nr_ingress_mappings: number of ingress priority mappings
 *	@ingress_priority_map: ingress priority mappings
 *	@nr_egress_mappings: number of egress priority mappings
 *	@egress_priority_map: hash of egress priority mappings
 *	@vlan_id: VLAN identifier
 *	@flags: device flags
 *	@real_dev: underlying netdevice
 *	@real_dev_addr: address of underlying netdevice
 *	@dent: proc dir entry
 *	@vlan_pcpu_stats: ptr to percpu rx stats
 */
struct vlan_dev_info {
	unsigned int				nr_ingress_mappings;
	u32					ingress_priority_map[8];
	unsigned int				nr_egress_mappings;
	struct vlan_priority_tci_mapping	*egress_priority_map[16];

	u16					vlan_id;
	u16					flags;

	struct net_device			*real_dev;
	unsigned char				real_dev_addr[ETH_ALEN];

	struct proc_dir_entry			*dent;
	struct vlan_pcpu_stats __percpu		*vlan_pcpu_stats;
};

static inline struct vlan_dev_info *vlan_dev_info(const struct net_device *dev)
{
	return netdev_priv(dev);
}

/* found in vlan_dev.c */
void vlan_dev_set_ingress_priority(const struct net_device *dev,
				   u32 skb_prio, u16 vlan_prio);
int vlan_dev_set_egress_priority(const struct net_device *dev,
				 u32 skb_prio, u16 vlan_prio);
int vlan_dev_change_flags(const struct net_device *dev, u32 flag, u32 mask);
void vlan_dev_get_realdev_name(const struct net_device *dev, char *result);

int vlan_check_real_dev(struct net_device *real_dev, u16 vlan_id);
void vlan_setup(struct net_device *dev);
int register_vlan_dev(struct net_device *dev);
void unregister_vlan_dev(struct net_device *dev, struct list_head *head);

static inline u32 vlan_get_ingress_priority(struct net_device *dev,
					    u16 vlan_tci)
{
	struct vlan_dev_info *vip = vlan_dev_info(dev);

	return vip->ingress_priority_map[(vlan_tci >> VLAN_PRIO_SHIFT) & 0x7];
}

#ifdef CONFIG_VLAN_8021Q_GVRP
extern int vlan_gvrp_request_join(const struct net_device *dev);
extern void vlan_gvrp_request_leave(const struct net_device *dev);
extern int vlan_gvrp_init_applicant(struct net_device *dev);
extern void vlan_gvrp_uninit_applicant(struct net_device *dev);
extern int vlan_gvrp_init(void);
extern void vlan_gvrp_uninit(void);
#else
static inline int vlan_gvrp_request_join(const struct net_device *dev) { return 0; }
static inline void vlan_gvrp_request_leave(const struct net_device *dev) {}
static inline int vlan_gvrp_init_applicant(struct net_device *dev) { return 0; }
static inline void vlan_gvrp_uninit_applicant(struct net_device *dev) {}
static inline int vlan_gvrp_init(void) { return 0; }
static inline void vlan_gvrp_uninit(void) {}
#endif

extern const char vlan_fullname[];
extern const char vlan_version[];
extern int vlan_netlink_init(void);
extern void vlan_netlink_fini(void);

extern struct rtnl_link_ops vlan_link_ops;

extern int vlan_net_id;

struct proc_dir_entry;

struct vlan_net {
	/* /proc/net/vlan */
	struct proc_dir_entry *proc_vlan_dir;
	/* /proc/net/vlan/config */
	struct proc_dir_entry *proc_vlan_conf;
	/* Determines interface naming scheme. */
	unsigned short name_type;
};

#endif /* !(__BEN_VLAN_802_1Q_INC__) */
