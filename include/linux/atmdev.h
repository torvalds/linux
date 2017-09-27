/* atmdev.h - ATM device driver declarations and various related items */
#ifndef LINUX_ATMDEV_H
#define LINUX_ATMDEV_H


#include <linux/wait.h> /* wait_queue_head_t */
#include <linux/time.h> /* struct timeval */
#include <linux/net.h>
#include <linux/bug.h>
#include <linux/skbuff.h> /* struct sk_buff */
#include <linux/uio.h>
#include <net/sock.h>
#include <linux/atomic.h>
#include <linux/refcount.h>
#include <uapi/linux/atmdev.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>

extern struct proc_dir_entry *atm_proc_root;
#endif

#ifdef CONFIG_COMPAT
#include <linux/compat.h>
struct compat_atm_iobuf {
	int length;
	compat_uptr_t buffer;
};
#endif

struct k_atm_aal_stats {
#define __HANDLE_ITEM(i) atomic_t i
	__AAL_STAT_ITEMS
#undef __HANDLE_ITEM
};


struct k_atm_dev_stats {
	struct k_atm_aal_stats aal0;
	struct k_atm_aal_stats aal34;
	struct k_atm_aal_stats aal5;
};

struct device;

enum {
	ATM_VF_ADDR,		/* Address is in use. Set by anybody, cleared
				   by device driver. */
	ATM_VF_READY,		/* VC is ready to transfer data. Set by device
				   driver, cleared by anybody. */
	ATM_VF_PARTIAL,		/* resources are bound to PVC (partial PVC
				   setup), controlled by socket layer */
	ATM_VF_REGIS,		/* registered with demon, controlled by SVC
				   socket layer */
	ATM_VF_BOUND,		/* local SAP is set, controlled by SVC socket
				   layer */
	ATM_VF_RELEASED,	/* demon has indicated/requested release,
				   controlled by SVC socket layer */
	ATM_VF_HASQOS,		/* QOS parameters have been set */
	ATM_VF_LISTEN,		/* socket is used for listening */
	ATM_VF_META,		/* SVC socket isn't used for normal data
				   traffic and doesn't depend on signaling
				   to be available */
	ATM_VF_SESSION,		/* VCC is p2mp session control descriptor */
	ATM_VF_HASSAP,		/* SAP has been set */
	ATM_VF_CLOSE,		/* asynchronous close - treat like VF_RELEASED*/
	ATM_VF_WAITING,		/* waiting for reply from sigd */
	ATM_VF_IS_CLIP,		/* in use by CLIP protocol */
};


#define ATM_VF2VS(flags) \
    (test_bit(ATM_VF_READY,&(flags)) ? ATM_VS_CONNECTED : \
     test_bit(ATM_VF_RELEASED,&(flags)) ? ATM_VS_CLOSING : \
     test_bit(ATM_VF_LISTEN,&(flags)) ? ATM_VS_LISTEN : \
     test_bit(ATM_VF_REGIS,&(flags)) ? ATM_VS_INUSE : \
     test_bit(ATM_VF_BOUND,&(flags)) ? ATM_VS_BOUND : ATM_VS_IDLE)


enum {
	ATM_DF_REMOVED,		/* device was removed from atm_devs list */
};


#define ATM_PHY_SIG_LOST    0	/* no carrier/light */
#define ATM_PHY_SIG_UNKNOWN 1	/* carrier/light status is unknown */
#define ATM_PHY_SIG_FOUND   2	/* carrier/light okay */

#define ATM_ATMOPT_CLP	1	/* set CLP bit */

struct atm_vcc {
	/* struct sock has to be the first member of atm_vcc */
	struct sock	sk;
	unsigned long	flags;		/* VCC flags (ATM_VF_*) */
	short		vpi;		/* VPI and VCI (types must be equal */
					/* with sockaddr) */
	int 		vci;
	unsigned long	aal_options;	/* AAL layer options */
	unsigned long	atm_options;	/* ATM layer options */
	struct atm_dev	*dev;		/* device back pointer */
	struct atm_qos	qos;		/* QOS */
	struct atm_sap	sap;		/* SAP */
	void (*release_cb)(struct atm_vcc *vcc); /* release_sock callback */
	void (*push)(struct atm_vcc *vcc,struct sk_buff *skb);
	void (*pop)(struct atm_vcc *vcc,struct sk_buff *skb); /* optional */
	int (*push_oam)(struct atm_vcc *vcc,void *cell);
	int (*send)(struct atm_vcc *vcc,struct sk_buff *skb);
	void		*dev_data;	/* per-device data */
	void		*proto_data;	/* per-protocol data */
	struct k_atm_aal_stats *stats;	/* pointer to AAL stats group */
	struct module *owner;		/* owner of ->push function */
	/* SVC part --- may move later ------------------------------------- */
	short		itf;		/* interface number */
	struct sockaddr_atmsvc local;
	struct sockaddr_atmsvc remote;
	/* Multipoint part ------------------------------------------------- */
	struct atm_vcc	*session;	/* session VCC descriptor */
	/* Other stuff ----------------------------------------------------- */
	void		*user_back;	/* user backlink - not touched by */
					/* native ATM stack. Currently used */
					/* by CLIP and sch_atm. */
};

static inline struct atm_vcc *atm_sk(struct sock *sk)
{
	return (struct atm_vcc *)sk;
}

static inline struct atm_vcc *ATM_SD(struct socket *sock)
{
	return atm_sk(sock->sk);
}

static inline struct sock *sk_atm(struct atm_vcc *vcc)
{
	return (struct sock *)vcc;
}

struct atm_dev_addr {
	struct sockaddr_atmsvc addr;	/* ATM address */
	struct list_head entry;		/* next address */
};

enum atm_addr_type_t { ATM_ADDR_LOCAL, ATM_ADDR_LECS };

struct atm_dev {
	const struct atmdev_ops *ops;	/* device operations; NULL if unused */
	const struct atmphy_ops *phy;	/* PHY operations, may be undefined */
					/* (NULL) */
	const char	*type;		/* device type name */
	int		number;		/* device index */
	void		*dev_data;	/* per-device data */
	void		*phy_data;	/* private PHY date */
	unsigned long	flags;		/* device flags (ATM_DF_*) */
	struct list_head local;		/* local ATM addresses */
	struct list_head lecs;		/* LECS ATM addresses learned via ILMI */
	unsigned char	esi[ESI_LEN];	/* ESI ("MAC" addr) */
	struct atm_cirange ci_range;	/* VPI/VCI range */
	struct k_atm_dev_stats stats;	/* statistics */
	char		signal;		/* signal status (ATM_PHY_SIG_*) */
	int		link_rate;	/* link rate (default: OC3) */
	refcount_t	refcnt;		/* reference count */
	spinlock_t	lock;		/* protect internal members */
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc_entry; /* proc entry */
	char *proc_name;		/* proc entry name */
#endif
	struct device class_dev;	/* sysfs device */
	struct list_head dev_list;	/* linkage */
};

 
/* OF: send_Oam Flags */

#define ATM_OF_IMMED  1		/* Attempt immediate delivery */
#define ATM_OF_INRATE 2		/* Attempt in-rate delivery */


/*
 * ioctl, getsockopt, and setsockopt are optional and can be set to NULL.
 */

struct atmdev_ops { /* only send is required */
	void (*dev_close)(struct atm_dev *dev);
	int (*open)(struct atm_vcc *vcc);
	void (*close)(struct atm_vcc *vcc);
	int (*ioctl)(struct atm_dev *dev,unsigned int cmd,void __user *arg);
#ifdef CONFIG_COMPAT
	int (*compat_ioctl)(struct atm_dev *dev,unsigned int cmd,
			    void __user *arg);
#endif
	int (*getsockopt)(struct atm_vcc *vcc,int level,int optname,
	    void __user *optval,int optlen);
	int (*setsockopt)(struct atm_vcc *vcc,int level,int optname,
	    void __user *optval,unsigned int optlen);
	int (*send)(struct atm_vcc *vcc,struct sk_buff *skb);
	int (*send_oam)(struct atm_vcc *vcc,void *cell,int flags);
	void (*phy_put)(struct atm_dev *dev,unsigned char value,
	    unsigned long addr);
	unsigned char (*phy_get)(struct atm_dev *dev,unsigned long addr);
	int (*change_qos)(struct atm_vcc *vcc,struct atm_qos *qos,int flags);
	int (*proc_read)(struct atm_dev *dev,loff_t *pos,char *page);
	struct module *owner;
};

struct atmphy_ops {
	int (*start)(struct atm_dev *dev);
	int (*ioctl)(struct atm_dev *dev,unsigned int cmd,void __user *arg);
	void (*interrupt)(struct atm_dev *dev);
	int (*stop)(struct atm_dev *dev);
};

struct atm_skb_data {
	struct atm_vcc	*vcc;		/* ATM VCC */
	unsigned long	atm_options;	/* ATM layer options */
};

#define VCC_HTABLE_SIZE 32

extern struct hlist_head vcc_hash[VCC_HTABLE_SIZE];
extern rwlock_t vcc_sklist_lock;

#define ATM_SKB(skb) (((struct atm_skb_data *) (skb)->cb))

struct atm_dev *atm_dev_register(const char *type, struct device *parent,
				 const struct atmdev_ops *ops,
				 int number, /* -1 == pick first available */
				 unsigned long *flags);
struct atm_dev *atm_dev_lookup(int number);
void atm_dev_deregister(struct atm_dev *dev);

/* atm_dev_signal_change
 *
 * Propagate lower layer signal change in atm_dev->signal to netdevice.
 * The event will be sent via a notifier call chain.
 */
void atm_dev_signal_change(struct atm_dev *dev, char signal);

void vcc_insert_socket(struct sock *sk);

void atm_dev_release_vccs(struct atm_dev *dev);


static inline void atm_force_charge(struct atm_vcc *vcc,int truesize)
{
	atomic_add(truesize, &sk_atm(vcc)->sk_rmem_alloc);
}


static inline void atm_return(struct atm_vcc *vcc,int truesize)
{
	atomic_sub(truesize, &sk_atm(vcc)->sk_rmem_alloc);
}


static inline int atm_may_send(struct atm_vcc *vcc,unsigned int size)
{
	return (size + refcount_read(&sk_atm(vcc)->sk_wmem_alloc)) <
	       sk_atm(vcc)->sk_sndbuf;
}


static inline void atm_dev_hold(struct atm_dev *dev)
{
	refcount_inc(&dev->refcnt);
}


static inline void atm_dev_put(struct atm_dev *dev)
{
	if (refcount_dec_and_test(&dev->refcnt)) {
		BUG_ON(!test_bit(ATM_DF_REMOVED, &dev->flags));
		if (dev->ops->dev_close)
			dev->ops->dev_close(dev);
		put_device(&dev->class_dev);
	}
}


int atm_charge(struct atm_vcc *vcc,int truesize);
struct sk_buff *atm_alloc_charge(struct atm_vcc *vcc,int pdu_size,
    gfp_t gfp_flags);
int atm_pcr_goal(const struct atm_trafprm *tp);

void vcc_release_async(struct atm_vcc *vcc, int reply);

struct atm_ioctl {
	struct module *owner;
	/* A module reference is kept if appropriate over this call.
	 * Return -ENOIOCTLCMD if you don't handle it. */
	int (*ioctl)(struct socket *, unsigned int cmd, unsigned long arg);
	struct list_head list;
};

/**
 * register_atm_ioctl - register handler for ioctl operations
 *
 * Special (non-device) handlers of ioctl's should
 * register here. If you're a normal device, you should
 * set .ioctl in your atmdev_ops instead.
 */
void register_atm_ioctl(struct atm_ioctl *);

/**
 * deregister_atm_ioctl - remove the ioctl handler
 */
void deregister_atm_ioctl(struct atm_ioctl *);


/* register_atmdevice_notifier - register atm_dev notify events
 *
 * Clients like br2684 will register notify events
 * Currently we notify of signal found/lost
 */
int register_atmdevice_notifier(struct notifier_block *nb);
void unregister_atmdevice_notifier(struct notifier_block *nb);

#endif
