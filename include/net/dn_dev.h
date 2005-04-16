#ifndef _NET_DN_DEV_H
#define _NET_DN_DEV_H


struct dn_dev;

struct dn_ifaddr {
	struct dn_ifaddr *ifa_next;
	struct dn_dev    *ifa_dev;
	dn_address       ifa_local;
	dn_address       ifa_address;
	unsigned char    ifa_flags;
	unsigned char    ifa_scope;
	char             ifa_label[IFNAMSIZ];
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
	int ctl_name;             /* Index for sysctl                   */
	int  (*up)(struct net_device *);
	void (*down)(struct net_device *);
	void (*timer3)(struct net_device *, struct dn_ifaddr *ifa);
	void *sysctl;
};


struct dn_dev {
	struct dn_ifaddr *ifa_list;
	struct net_device *dev;
	struct dn_dev_parms parms;
	char use_long;
        struct timer_list timer;
        unsigned long t3;
	struct neigh_parms *neigh_parms;
	unsigned char addr[ETH_ALEN];
	struct neighbour *router; /* Default router on circuit */
	struct neighbour *peer;   /* Peer on pointopoint links */
	unsigned long uptime;     /* Time device went up in jiffies */
};

struct dn_short_packet
{
	unsigned char   msgflg          __attribute__((packed));
        unsigned short  dstnode         __attribute__((packed));
        unsigned short  srcnode         __attribute__((packed));
        unsigned char   forward         __attribute__((packed));
};

struct dn_long_packet
{
	unsigned char   msgflg          __attribute__((packed));
        unsigned char   d_area          __attribute__((packed));
        unsigned char   d_subarea       __attribute__((packed));
        unsigned char   d_id[6]         __attribute__((packed));
        unsigned char   s_area          __attribute__((packed));
        unsigned char   s_subarea       __attribute__((packed));
        unsigned char   s_id[6]         __attribute__((packed));
        unsigned char   nl2             __attribute__((packed));
        unsigned char   visit_ct        __attribute__((packed));
        unsigned char   s_class         __attribute__((packed));
        unsigned char   pt              __attribute__((packed));
};

/*------------------------- DRP - Routing messages ---------------------*/

struct endnode_hello_message
{
	unsigned char   msgflg          __attribute__((packed));
        unsigned char   tiver[3]        __attribute__((packed));
        unsigned char   id[6]           __attribute__((packed));
        unsigned char   iinfo           __attribute__((packed));
        unsigned short  blksize         __attribute__((packed));
        unsigned char   area            __attribute__((packed));
        unsigned char   seed[8]         __attribute__((packed));
        unsigned char   neighbor[6]     __attribute__((packed));
        unsigned short  timer           __attribute__((packed));
        unsigned char   mpd             __attribute__((packed));
        unsigned char   datalen         __attribute__((packed));
        unsigned char   data[2]         __attribute__((packed));
};

struct rtnode_hello_message
{
	unsigned char   msgflg          __attribute__((packed));
        unsigned char   tiver[3]        __attribute__((packed));
        unsigned char   id[6]           __attribute__((packed));
        unsigned char   iinfo           __attribute__((packed));
        unsigned short  blksize         __attribute__((packed));
        unsigned char   priority        __attribute__((packed));
        unsigned char   area            __attribute__((packed));
        unsigned short  timer           __attribute__((packed));
        unsigned char   mpd             __attribute__((packed));
};


extern void dn_dev_init(void);
extern void dn_dev_cleanup(void);

extern int dn_dev_ioctl(unsigned int cmd, void __user *arg);

extern void dn_dev_devices_off(void);
extern void dn_dev_devices_on(void);

extern void dn_dev_init_pkt(struct sk_buff *skb);
extern void dn_dev_veri_pkt(struct sk_buff *skb);
extern void dn_dev_hello(struct sk_buff *skb);

extern void dn_dev_up(struct net_device *);
extern void dn_dev_down(struct net_device *);

extern int dn_dev_set_default(struct net_device *dev, int force);
extern struct net_device *dn_dev_get_default(void);
extern int dn_dev_bind_default(dn_address *addr);

extern int register_dnaddr_notifier(struct notifier_block *nb);
extern int unregister_dnaddr_notifier(struct notifier_block *nb);

static inline int dn_dev_islocal(struct net_device *dev, dn_address addr)
{
	struct dn_dev *dn_db = dev->dn_ptr;
	struct dn_ifaddr *ifa;

	if (dn_db == NULL) {
		printk(KERN_DEBUG "dn_dev_islocal: Called for non DECnet device\n");
		return 0;
	}

	for(ifa = dn_db->ifa_list; ifa; ifa = ifa->ifa_next)
		if ((addr ^ ifa->ifa_local) == 0)
			return 1;

	return 0;
}

#endif /* _NET_DN_DEV_H */
