#ifndef _NET_DN_DEV_H
#define _NET_DN_DEV_H


struct dn_dev;

struct dn_ifaddr {
	struct dn_ifaddr __rcu *ifa_next;
	struct dn_dev    *ifa_dev;
	__le16            ifa_local;
	__le16            ifa_address;
	__u8              ifa_flags;
	__u8              ifa_scope;
	char              ifa_label[IFNAMSIZ];
	struct rcu_head   rcu;
};

#define DN_DEV_S_RU  0 /* Run - working normally   */
#define DN_DEV_S_CR  1 /* Circuit Rejected         */
#define DN_DEV_S_DS  2 /* Data Link Start          */
#define DN_DEV_S_RI  3 /* Routing Layer Initialize */
#define DN_DEV_S_RV  4 /* Routing Layer Verify     */
#define DN_DEV_S_RC  5 /* Routing Layer Complete   */
#define DN_DEV_S_OF  6 /* Off                      */
#define DN_DEV_S_HA  7 /* Halt                     */


/*
 * The dn_dev_parms structure contains the set of parameters
 * for each device (hence inclusion in the dn_dev structure)
 * and an array is used to store the default types of supported
 * device (in dn_dev.c).
 *
 * The type field matches the ARPHRD_ constants and is used in
 * searching the list for supported devices when new devices
 * come up.
 *
 * The mode field is used to find out if a device is broadcast,
 * multipoint, or pointopoint. Please note that DECnet thinks
 * different ways about devices to the rest of the kernel
 * so the normal IFF_xxx flags are invalid here. For devices
 * which can be any combination of the previously mentioned
 * attributes, you can set this on a per device basis by
 * installing an up() routine.
 *
 * The device state field, defines the initial state in which the
 * device will come up. In the dn_dev structure, it is the actual
 * state.
 *
 * Things have changed here. I've killed timer1 since it's a user space
 * issue for a user space routing deamon to sort out. The kernel does
 * not need to be bothered with it.
 *
 * Timers:
 * t2 - Rate limit timer, min time between routing and hello messages
 * t3 - Hello timer, send hello messages when it expires
 *
 * Callbacks:
 * up() - Called to initialize device, return value can veto use of
 *        device with DECnet.
 * down() - Called to turn device off when it goes down
 * timer3() - Called once for each ifaddr when timer 3 goes off
 * 
 * sysctl - Hook for sysctl things
 *
 */
struct dn_dev_parms {
	int type;	          /* ARPHRD_xxx                         */
	int mode;	          /* Broadcast, Unicast, Mulitpoint     */
#define DN_DEV_BCAST  1
#define DN_DEV_UCAST  2
#define DN_DEV_MPOINT 4
	int state;                /* Initial state                      */
	int forwarding;	          /* 0=EndNode, 1=L1Router, 2=L2Router  */
	unsigned long t2;         /* Default value of t2                */
	unsigned long t3;         /* Default value of t3                */
	int priority;             /* Priority to be a router            */
	char *name;               /* Name for sysctl                    */
	int  (*up)(struct net_device *);
	void (*down)(struct net_device *);
	void (*timer3)(struct net_device *, struct dn_ifaddr *ifa);
	void *sysctl;
};


struct dn_dev {
	struct dn_ifaddr __rcu *ifa_list;
	struct net_device *dev;
	struct dn_dev_parms parms;
	char use_long;
	struct timer_list timer;
	unsigned long t3;
	struct neigh_parms *neigh_parms;
	__u8 addr[ETH_ALEN];
	struct neighbour *router; /* Default router on circuit */
	struct neighbour *peer;   /* Peer on pointopoint links */
	unsigned long uptime;     /* Time device went up in jiffies */
};

struct dn_short_packet {
	__u8    msgflg;
	__le16 dstnode;
	__le16 srcnode;
	__u8   forward;
} __packed;

struct dn_long_packet {
	__u8   msgflg;
	__u8   d_area;
	__u8   d_subarea;
	__u8   d_id[6];
	__u8   s_area;
	__u8   s_subarea;
	__u8   s_id[6];
	__u8   nl2;
	__u8   visit_ct;
	__u8   s_class;
	__u8   pt;
} __packed;

/*------------------------- DRP - Routing messages ---------------------*/

struct endnode_hello_message {
	__u8   msgflg;
	__u8   tiver[3];
	__u8   id[6];
	__u8   iinfo;
	__le16 blksize;
	__u8   area;
	__u8   seed[8];
	__u8   neighbor[6];
	__le16 timer;
	__u8   mpd;
	__u8   datalen;
	__u8   data[2];
} __packed;

struct rtnode_hello_message {
	__u8   msgflg;
	__u8   tiver[3];
	__u8   id[6];
	__u8   iinfo;
	__le16  blksize;
	__u8   priority;
	__u8   area;
	__le16  timer;
	__u8   mpd;
} __packed;


void dn_dev_init(void);
void dn_dev_cleanup(void);

int dn_dev_ioctl(unsigned int cmd, void __user *arg);

void dn_dev_devices_off(void);
void dn_dev_devices_on(void);

void dn_dev_init_pkt(struct sk_buff *skb);
void dn_dev_veri_pkt(struct sk_buff *skb);
void dn_dev_hello(struct sk_buff *skb);

void dn_dev_up(struct net_device *);
void dn_dev_down(struct net_device *);

int dn_dev_set_default(struct net_device *dev, int force);
struct net_device *dn_dev_get_default(void);
int dn_dev_bind_default(__le16 *addr);

int register_dnaddr_notifier(struct notifier_block *nb);
int unregister_dnaddr_notifier(struct notifier_block *nb);

static inline int dn_dev_islocal(struct net_device *dev, __le16 addr)
{
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa;
	int res = 0;

	rcu_read_lock();
	dn_db = rcu_dereference(dev->dn_ptr);
	if (dn_db == NULL) {
		printk(KERN_DEBUG "dn_dev_islocal: Called for non DECnet device\n");
		goto out;
	}

	for (ifa = rcu_dereference(dn_db->ifa_list);
	     ifa != NULL;
	     ifa = rcu_dereference(ifa->ifa_next))
		if ((addr ^ ifa->ifa_local) == 0) {
			res = 1;
			break;
		}
out:
	rcu_read_unlock();
	return res;
}

#endif /* _NET_DN_DEV_H */
