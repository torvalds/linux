#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/capability.h>
#include <linux/seq_file.h>

/* We are an ethernet device */
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/sock.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>
#include <net/checksum.h>   /* for ip_fast_csum() */
#include <net/arp.h>
#include <net/dst.h>
#include <linux/proc_fs.h>

/* And atm device */
#include <linux/atmdev.h>
#include <linux/atmlec.h>
#include <linux/atmmpc.h>
/* Modular too */
#include <linux/module.h>

#include "lec.h"
#include "mpc.h"
#include "resources.h"

/*
 * mpc.c: Implementation of MPOA client kernel part 
 */

#if 0
#define dprintk printk   /* debug */
#else
#define dprintk(format,args...)
#endif

#if 0
#define ddprintk printk  /* more debug */
#else
#define ddprintk(format,args...)
#endif



#define MPOA_TAG_LEN 4

/* mpc_daemon -> kernel */
static void MPOA_trigger_rcvd (struct k_message *msg, struct mpoa_client *mpc);
static void MPOA_res_reply_rcvd(struct k_message *msg, struct mpoa_client *mpc);
static void ingress_purge_rcvd(struct k_message *msg, struct mpoa_client *mpc);
static void egress_purge_rcvd(struct k_message *msg, struct mpoa_client *mpc);
static void mps_death(struct k_message *msg, struct mpoa_client *mpc);
static void clean_up(struct k_message *msg, struct mpoa_client *mpc, int action);
static void MPOA_cache_impos_rcvd(struct k_message *msg, struct mpoa_client *mpc);
static void set_mpc_ctrl_addr_rcvd(struct k_message *mesg, struct mpoa_client *mpc);
static void set_mps_mac_addr_rcvd(struct k_message *mesg, struct mpoa_client *mpc);

static uint8_t *copy_macs(struct mpoa_client *mpc, uint8_t *router_mac,
			  uint8_t *tlvs, uint8_t mps_macs, uint8_t device_type);
static void purge_egress_shortcut(struct atm_vcc *vcc, eg_cache_entry *entry);

static void send_set_mps_ctrl_addr(char *addr, struct mpoa_client *mpc);
static void mpoad_close(struct atm_vcc *vcc);
static int msg_from_mpoad(struct atm_vcc *vcc, struct sk_buff *skb);

static void mpc_push(struct atm_vcc *vcc, struct sk_buff *skb);
static int mpc_send_packet(struct sk_buff *skb, struct net_device *dev);
static int mpoa_event_listener(struct notifier_block *mpoa_notifier, unsigned long event, void *dev);
static void mpc_timer_refresh(void);
static void mpc_cache_check( unsigned long checking_time  );

static struct llc_snap_hdr llc_snap_mpoa_ctrl = {
	0xaa, 0xaa, 0x03,
	{0x00, 0x00, 0x5e},
	{0x00, 0x03}         /* For MPOA control PDUs */
};        
static struct llc_snap_hdr llc_snap_mpoa_data = {
	0xaa, 0xaa, 0x03,
	{0x00, 0x00, 0x00},
	{0x08, 0x00}         /* This is for IP PDUs only */
};        
static struct llc_snap_hdr llc_snap_mpoa_data_tagged = {
	0xaa, 0xaa, 0x03,
	{0x00, 0x00, 0x00},
	{0x88, 0x4c}         /* This is for tagged data PDUs */
};        

static struct notifier_block mpoa_notifier = {
	mpoa_event_listener,
	NULL,
	0
};

struct mpoa_client *mpcs = NULL; /* FIXME */
static struct atm_mpoa_qos *qos_head = NULL;
static DEFINE_TIMER(mpc_timer, NULL, 0, 0);


static struct mpoa_client *find_mpc_by_itfnum(int itf)
{
	struct mpoa_client *mpc;
	
	mpc = mpcs;  /* our global linked list */
	while (mpc != NULL) {
		if (mpc->dev_num == itf)
			return mpc;
		mpc = mpc->next;    
	}

	return NULL;   /* not found */
}

static struct mpoa_client *find_mpc_by_vcc(struct atm_vcc *vcc)
{
	struct mpoa_client *mpc;
	
	mpc = mpcs;  /* our global linked list */
	while (mpc != NULL) {
		if (mpc->mpoad_vcc == vcc)
			return mpc;
		mpc = mpc->next;
	}

	return NULL;   /* not found */
}

static struct mpoa_client *find_mpc_by_lec(struct net_device *dev)
{
	struct mpoa_client *mpc;
	
	mpc = mpcs;  /* our global linked list */
	while (mpc != NULL) {
		if (mpc->dev == dev)
			return mpc;
		mpc = mpc->next;
	}

	return NULL;   /* not found */
}

/*
 * Functions for managing QoS list
 */

/*
 * Overwrites the old entry or makes a new one.
 */
struct atm_mpoa_qos *atm_mpoa_add_qos(__be32 dst_ip, struct atm_qos *qos)
{
	struct atm_mpoa_qos *entry;

	entry = atm_mpoa_search_qos(dst_ip);
	if (entry != NULL) {
		entry->qos = *qos;
		return entry;
	}

	entry = kmalloc(sizeof(struct atm_mpoa_qos), GFP_KERNEL);
	if (entry == NULL) {
		printk("mpoa: atm_mpoa_add_qos: out of memory\n");
		return entry;
	}

	entry->ipaddr = dst_ip;
	entry->qos = *qos;

	entry->next = qos_head;
	qos_head = entry;

	return entry;
}

struct atm_mpoa_qos *atm_mpoa_search_qos(__be32 dst_ip)
{
	struct atm_mpoa_qos *qos;

	qos = qos_head;
	while( qos != NULL ){
		if(qos->ipaddr == dst_ip) {
			break;
		}
		qos = qos->next;
	}

	return qos;
}        

/*
 * Returns 0 for failure
 */
int atm_mpoa_delete_qos(struct atm_mpoa_qos *entry)
{

	struct atm_mpoa_qos *curr;

	if (entry == NULL) return 0;
	if (entry == qos_head) {
		qos_head = qos_head->next;
		kfree(entry);
		return 1;
	}

	curr = qos_head;
	while (curr != NULL) {
		if (curr->next == entry) {
			curr->next = entry->next;
			kfree(entry);
			return 1;
		}
		curr = curr->next;
	}

	return 0;
}

/* this is buggered - we need locking for qos_head */
void atm_mpoa_disp_qos(struct seq_file *m)
{
	struct atm_mpoa_qos *qos;

	qos = qos_head;
	seq_printf(m, "QoS entries for shortcuts:\n");
	seq_printf(m, "IP address\n  TX:max_pcr pcr     min_pcr max_cdv max_sdu\n  RX:max_pcr pcr     min_pcr max_cdv max_sdu\n");

	while (qos != NULL) {
		seq_printf(m, "%u.%u.%u.%u\n     %-7d %-7d %-7d %-7d %-7d\n     %-7d %-7d %-7d %-7d %-7d\n",
				NIPQUAD(qos->ipaddr),
				qos->qos.txtp.max_pcr, qos->qos.txtp.pcr, qos->qos.txtp.min_pcr, qos->qos.txtp.max_cdv, qos->qos.txtp.max_sdu,
				qos->qos.rxtp.max_pcr, qos->qos.rxtp.pcr, qos->qos.rxtp.min_pcr, qos->qos.rxtp.max_cdv, qos->qos.rxtp.max_sdu);
		qos = qos->next;
	}
}

static struct net_device *find_lec_by_itfnum(int itf)
{
	struct net_device *dev;
	char name[IFNAMSIZ];

	sprintf(name, "lec%d", itf);
	dev = dev_get_by_name(name);
	
	return dev;
}

static struct mpoa_client *alloc_mpc(void)
{
	struct mpoa_client *mpc;

	mpc = kzalloc(sizeof (struct mpoa_client), GFP_KERNEL);
	if (mpc == NULL)
		return NULL;
	rwlock_init(&mpc->ingress_lock);
	rwlock_init(&mpc->egress_lock);
	mpc->next = mpcs;
	atm_mpoa_init_cache(mpc);

	mpc->parameters.mpc_p1 = MPC_P1;
	mpc->parameters.mpc_p2 = MPC_P2;
	memset(mpc->parameters.mpc_p3,0,sizeof(mpc->parameters.mpc_p3));
	mpc->parameters.mpc_p4 = MPC_P4;
	mpc->parameters.mpc_p5 = MPC_P5; 
	mpc->parameters.mpc_p6 = MPC_P6;
	
	mpcs = mpc;
	
	return mpc;
}

/*
 *
 * start_mpc() puts the MPC on line. All the packets destined
 * to the lec underneath us are now being monitored and 
 * shortcuts will be established.
 *
 */
static void start_mpc(struct mpoa_client *mpc, struct net_device *dev)
{
	
	dprintk("mpoa: (%s) start_mpc:\n", mpc->dev->name); 
	if (dev->hard_start_xmit == NULL) {
		printk("mpoa: (%s) start_mpc: dev->hard_start_xmit == NULL, not starting\n",
		       dev->name);
		return;
	}
	mpc->old_hard_start_xmit = dev->hard_start_xmit;
	dev->hard_start_xmit = mpc_send_packet;

	return;
}

static void stop_mpc(struct mpoa_client *mpc)
{
	
	dprintk("mpoa: (%s) stop_mpc:", mpc->dev->name); 

	/* Lets not nullify lec device's dev->hard_start_xmit */
	if (mpc->dev->hard_start_xmit != mpc_send_packet) {
		dprintk(" mpc already stopped, not fatal\n");
		return;
	}
	dprintk("\n");
	mpc->dev->hard_start_xmit = mpc->old_hard_start_xmit;
	mpc->old_hard_start_xmit = NULL;
	/* close_shortcuts(mpc);    ??? FIXME */
	
	return;
}

static const char *mpoa_device_type_string(char type) __attribute__ ((unused));

static const char *mpoa_device_type_string(char type)
{
	switch(type) {
	case NON_MPOA:
		return "non-MPOA device";
		break;
	case MPS:
		return "MPS";
		break;
	case MPC:
		return "MPC";
		break;
	case MPS_AND_MPC:
		return "both MPS and MPC";
		break;
	default:
		return "unspecified (non-MPOA) device";
		break;
	}

	return ""; /* not reached */
}

/*
 * lec device calls this via its dev->priv->lane2_ops->associate_indicator()
 * when it sees a TLV in LE_ARP packet.
 * We fill in the pointer above when we see a LANE2 lec initializing
 * See LANE2 spec 3.1.5
 *
 * Quite a big and ugly function but when you look at it
 * all it does is to try to locate and parse MPOA Device
 * Type TLV.
 * We give our lec a pointer to this function and when the
 * lec sees a TLV it uses the pointer to call this function.
 *
 */
static void lane2_assoc_ind(struct net_device *dev, uint8_t *mac_addr,
			    uint8_t *tlvs, uint32_t sizeoftlvs)
{
	uint32_t type;
	uint8_t length, mpoa_device_type, number_of_mps_macs;
	uint8_t *end_of_tlvs;
	struct mpoa_client *mpc;
	
	mpoa_device_type = number_of_mps_macs = 0; /* silence gcc */
	dprintk("mpoa: (%s) lane2_assoc_ind: received TLV(s), ", dev->name);
	dprintk("total length of all TLVs %d\n", sizeoftlvs);
	mpc = find_mpc_by_lec(dev); /* Sampo-Fix: moved here from below */
	if (mpc == NULL) {
		printk("mpoa: (%s) lane2_assoc_ind: no mpc\n", dev->name);
		return;
	}
	end_of_tlvs = tlvs + sizeoftlvs;
	while (end_of_tlvs - tlvs >= 5) {
		type = (tlvs[0] << 24) | (tlvs[1] << 16) | (tlvs[2] << 8) | tlvs[3];
		length = tlvs[4];
		tlvs += 5;
		dprintk("    type 0x%x length %02x\n", type, length);
		if (tlvs + length > end_of_tlvs) {
			printk("TLV value extends past its buffer, aborting parse\n");
			return;
		}
		
		if (type == 0) {
			printk("mpoa: (%s) lane2_assoc_ind: TLV type was 0, returning\n", dev->name);
			return;
		}

		if (type != TLV_MPOA_DEVICE_TYPE) {
			tlvs += length;
			continue;  /* skip other TLVs */
		}
		mpoa_device_type = *tlvs++;
		number_of_mps_macs = *tlvs++;
		dprintk("mpoa: (%s) MPOA device type '%s', ", dev->name, mpoa_device_type_string(mpoa_device_type));
		if (mpoa_device_type == MPS_AND_MPC &&
		    length < (42 + number_of_mps_macs*ETH_ALEN)) { /* :) */
			printk("\nmpoa: (%s) lane2_assoc_ind: short MPOA Device Type TLV\n",
			       dev->name);
			continue;
		}
		if ((mpoa_device_type == MPS || mpoa_device_type == MPC)
		    && length < 22 + number_of_mps_macs*ETH_ALEN) {
			printk("\nmpoa: (%s) lane2_assoc_ind: short MPOA Device Type TLV\n",
				dev->name);
			continue;
		}
		if (mpoa_device_type != MPS && mpoa_device_type != MPS_AND_MPC) {
			dprintk("ignoring non-MPS device\n");
			if (mpoa_device_type == MPC) tlvs += 20;
			continue;  /* we are only interested in MPSs */
		}
		if (number_of_mps_macs == 0 && mpoa_device_type == MPS_AND_MPC) {
			printk("\nmpoa: (%s) lane2_assoc_ind: MPS_AND_MPC has zero MACs\n", dev->name);
			continue;  /* someone should read the spec */
		}
		dprintk("this MPS has %d MAC addresses\n", number_of_mps_macs);
		
		/* ok, now we can go and tell our daemon the control address of MPS */
		send_set_mps_ctrl_addr(tlvs, mpc);
		
		tlvs = copy_macs(mpc, mac_addr, tlvs, number_of_mps_macs, mpoa_device_type);
		if (tlvs == NULL) return;
	}
	if (end_of_tlvs - tlvs != 0)
		printk("mpoa: (%s) lane2_assoc_ind: ignoring %Zd bytes of trailing TLV carbage\n",
		       dev->name, end_of_tlvs - tlvs);
	return;
}

/*
 * Store at least advertizing router's MAC address
 * plus the possible MAC address(es) to mpc->mps_macs.
 * For a freshly allocated MPOA client mpc->mps_macs == 0.
 */
static uint8_t *copy_macs(struct mpoa_client *mpc, uint8_t *router_mac,
			  uint8_t *tlvs, uint8_t mps_macs, uint8_t device_type)
{
	int num_macs;
	num_macs = (mps_macs > 1) ? mps_macs : 1;

	if (mpc->number_of_mps_macs != num_macs) { /* need to reallocate? */
		if (mpc->number_of_mps_macs != 0) kfree(mpc->mps_macs);
		mpc->number_of_mps_macs = 0;
		mpc->mps_macs = kmalloc(num_macs*ETH_ALEN, GFP_KERNEL);
		if (mpc->mps_macs == NULL) {
			printk("mpoa: (%s) copy_macs: out of mem\n", mpc->dev->name);
			return NULL;
		}
	}
	memcpy(mpc->mps_macs, router_mac, ETH_ALEN);
	tlvs += 20; if (device_type == MPS_AND_MPC) tlvs += 20;
	if (mps_macs > 0)
		memcpy(mpc->mps_macs, tlvs, mps_macs*ETH_ALEN);
	tlvs += mps_macs*ETH_ALEN;
	mpc->number_of_mps_macs = num_macs;

	return tlvs;
}

static int send_via_shortcut(struct sk_buff *skb, struct mpoa_client *mpc)
{
	in_cache_entry *entry;
	struct iphdr *iph;
	char *buff;
	__be32 ipaddr = 0;

	static struct {
		struct llc_snap_hdr hdr;
		__be32 tag;
	} tagged_llc_snap_hdr = {
		{0xaa, 0xaa, 0x03, {0x00, 0x00, 0x00}, {0x88, 0x4c}},
		0
	};

	buff = skb->data + mpc->dev->hard_header_len;
	iph = (struct iphdr *)buff;
	ipaddr = iph->daddr;

	ddprintk("mpoa: (%s) send_via_shortcut: ipaddr 0x%x\n", mpc->dev->name, ipaddr);        

	entry = mpc->in_ops->get(ipaddr, mpc);
	if (entry == NULL) {
		entry = mpc->in_ops->add_entry(ipaddr, mpc);
		if (entry != NULL) mpc->in_ops->put(entry);
		return 1;
	}
	if (mpc->in_ops->cache_hit(entry, mpc) != OPEN){   /* threshold not exceeded or VCC not ready */
		ddprintk("mpoa: (%s) send_via_shortcut: cache_hit: returns != OPEN\n", mpc->dev->name);        
		mpc->in_ops->put(entry);
		return 1;
	}

	ddprintk("mpoa: (%s) send_via_shortcut: using shortcut\n", mpc->dev->name);        
	/* MPOA spec A.1.4, MPOA client must decrement IP ttl at least by one */
	if (iph->ttl <= 1) {
		ddprintk("mpoa: (%s) send_via_shortcut: IP ttl = %u, using LANE\n", mpc->dev->name, iph->ttl);        
		mpc->in_ops->put(entry);
		return 1;
	}
	iph->ttl--;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	if (entry->ctrl_info.tag != 0) {
		ddprintk("mpoa: (%s) send_via_shortcut: adding tag 0x%x\n", mpc->dev->name, entry->ctrl_info.tag);
		tagged_llc_snap_hdr.tag = entry->ctrl_info.tag;
		skb_pull(skb, ETH_HLEN);                       /* get rid of Eth header */
		skb_push(skb, sizeof(tagged_llc_snap_hdr));    /* add LLC/SNAP header   */
		memcpy(skb->data, &tagged_llc_snap_hdr, sizeof(tagged_llc_snap_hdr));
	} else {
		skb_pull(skb, ETH_HLEN);                        /* get rid of Eth header */
		skb_push(skb, sizeof(struct llc_snap_hdr));     /* add LLC/SNAP header + tag  */
		memcpy(skb->data, &llc_snap_mpoa_data, sizeof(struct llc_snap_hdr));
	}

	atomic_add(skb->truesize, &sk_atm(entry->shortcut)->sk_wmem_alloc);
	ATM_SKB(skb)->atm_options = entry->shortcut->atm_options;
	entry->shortcut->send(entry->shortcut, skb);
	entry->packets_fwded++;
	mpc->in_ops->put(entry);

	return 0;
}

/*
 * Probably needs some error checks and locking, not sure...
 */
static int mpc_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	int retval;
	struct mpoa_client *mpc;
	struct ethhdr *eth;
	int i = 0;
	
	mpc = find_mpc_by_lec(dev); /* this should NEVER fail */
	if(mpc == NULL) {
		printk("mpoa: (%s) mpc_send_packet: no MPC found\n", dev->name);
		goto non_ip;
	}

	eth = (struct ethhdr *)skb->data;
	if (eth->h_proto != htons(ETH_P_IP))
		goto non_ip; /* Multi-Protocol Over ATM :-) */

	while (i < mpc->number_of_mps_macs) {
		if (!compare_ether_addr(eth->h_dest, (mpc->mps_macs + i*ETH_ALEN)))
			if ( send_via_shortcut(skb, mpc) == 0 )           /* try shortcut */
				return 0;                                 /* success!     */
		i++;
	}

 non_ip:
	retval = mpc->old_hard_start_xmit(skb,dev);
	
	return retval;
}

static int atm_mpoa_vcc_attach(struct atm_vcc *vcc, void __user *arg)
{
	int bytes_left;
	struct mpoa_client *mpc;
	struct atmmpc_ioc ioc_data;
	in_cache_entry *in_entry;
	__be32  ipaddr;

	bytes_left = copy_from_user(&ioc_data, arg, sizeof(struct atmmpc_ioc));
	if (bytes_left != 0) {
		printk("mpoa: mpc_vcc_attach: Short read (missed %d bytes) from userland\n", bytes_left);
		return -EFAULT;
	}
	ipaddr = ioc_data.ipaddr;
	if (ioc_data.dev_num < 0 || ioc_data.dev_num >= MAX_LEC_ITF)
		return -EINVAL;
	
	mpc = find_mpc_by_itfnum(ioc_data.dev_num);
	if (mpc == NULL)
		return -EINVAL;
	
	if (ioc_data.type == MPC_SOCKET_INGRESS) {
		in_entry = mpc->in_ops->get(ipaddr, mpc);
		if (in_entry == NULL || in_entry->entry_state < INGRESS_RESOLVED) {
			printk("mpoa: (%s) mpc_vcc_attach: did not find RESOLVED entry from ingress cache\n",
				mpc->dev->name);
			if (in_entry != NULL) mpc->in_ops->put(in_entry);
			return -EINVAL;
		}
		printk("mpoa: (%s) mpc_vcc_attach: attaching ingress SVC, entry = %u.%u.%u.%u\n",
		       mpc->dev->name, NIPQUAD(in_entry->ctrl_info.in_dst_ip));
		in_entry->shortcut = vcc;
		mpc->in_ops->put(in_entry);
	} else {
		printk("mpoa: (%s) mpc_vcc_attach: attaching egress SVC\n", mpc->dev->name);
	}

	vcc->proto_data = mpc->dev;
	vcc->push = mpc_push;

	return 0;
}

/*
 *
 */
static void mpc_vcc_close(struct atm_vcc *vcc, struct net_device *dev)
{
	struct mpoa_client *mpc;
	in_cache_entry *in_entry;
	eg_cache_entry *eg_entry;
	
	mpc = find_mpc_by_lec(dev);
	if (mpc == NULL) {
		printk("mpoa: (%s) mpc_vcc_close: close for unknown MPC\n", dev->name);
		return;
	}

	dprintk("mpoa: (%s) mpc_vcc_close:\n", dev->name);
	in_entry = mpc->in_ops->get_by_vcc(vcc, mpc);
	if (in_entry) {
		dprintk("mpoa: (%s) mpc_vcc_close: ingress SVC closed ip = %u.%u.%u.%u\n",
		       mpc->dev->name, NIPQUAD(in_entry->ctrl_info.in_dst_ip));
		in_entry->shortcut = NULL;
		mpc->in_ops->put(in_entry);
	}
	eg_entry = mpc->eg_ops->get_by_vcc(vcc, mpc);
	if (eg_entry) {
		dprintk("mpoa: (%s) mpc_vcc_close: egress SVC closed\n", mpc->dev->name);
		eg_entry->shortcut = NULL;
		mpc->eg_ops->put(eg_entry);
	}

	if (in_entry == NULL && eg_entry == NULL)
		dprintk("mpoa: (%s) mpc_vcc_close:  unused vcc closed\n", dev->name);

	return;
}

static void mpc_push(struct atm_vcc *vcc, struct sk_buff *skb)
{
	struct net_device *dev = (struct net_device *)vcc->proto_data;
	struct sk_buff *new_skb;
	eg_cache_entry *eg;
	struct mpoa_client *mpc;
	__be32 tag;
	char *tmp;
	
	ddprintk("mpoa: (%s) mpc_push:\n", dev->name);
	if (skb == NULL) {
		dprintk("mpoa: (%s) mpc_push: null skb, closing VCC\n", dev->name);
		mpc_vcc_close(vcc, dev);
		return;
	}
	
	skb->dev = dev;
	if (memcmp(skb->data, &llc_snap_mpoa_ctrl, sizeof(struct llc_snap_hdr)) == 0) {
		struct sock *sk = sk_atm(vcc);

		dprintk("mpoa: (%s) mpc_push: control packet arrived\n", dev->name);
		/* Pass control packets to daemon */
		skb_queue_tail(&sk->sk_receive_queue, skb);
		sk->sk_data_ready(sk, skb->len);
		return;
	}

	/* data coming over the shortcut */
	atm_return(vcc, skb->truesize);

	mpc = find_mpc_by_lec(dev);
	if (mpc == NULL) {
		printk("mpoa: (%s) mpc_push: unknown MPC\n", dev->name);
		return;
	}

	if (memcmp(skb->data, &llc_snap_mpoa_data_tagged, sizeof(struct llc_snap_hdr)) == 0) { /* MPOA tagged data */
		ddprintk("mpoa: (%s) mpc_push: tagged data packet arrived\n", dev->name);

	} else if (memcmp(skb->data, &llc_snap_mpoa_data, sizeof(struct llc_snap_hdr)) == 0) { /* MPOA data */
		printk("mpoa: (%s) mpc_push: non-tagged data packet arrived\n", dev->name);
		printk("           mpc_push: non-tagged data unsupported, purging\n");
		dev_kfree_skb_any(skb);
		return;
	} else {
		printk("mpoa: (%s) mpc_push: garbage arrived, purging\n", dev->name);
		dev_kfree_skb_any(skb);
		return;
	}

	tmp = skb->data + sizeof(struct llc_snap_hdr);
	tag = *(__be32 *)tmp;

	eg = mpc->eg_ops->get_by_tag(tag, mpc);
	if (eg == NULL) {
		printk("mpoa: (%s) mpc_push: Didn't find egress cache entry, tag = %u\n",
		       dev->name,tag);
		purge_egress_shortcut(vcc, NULL);
		dev_kfree_skb_any(skb);
		return;
	}
	
	/*
	 * See if ingress MPC is using shortcut we opened as a return channel.
	 * This means we have a bi-directional vcc opened by us.
	 */ 
	if (eg->shortcut == NULL) {
		eg->shortcut = vcc;
		printk("mpoa: (%s) mpc_push: egress SVC in use\n", dev->name);
	}

	skb_pull(skb, sizeof(struct llc_snap_hdr) + sizeof(tag)); /* get rid of LLC/SNAP header */
	new_skb = skb_realloc_headroom(skb, eg->ctrl_info.DH_length); /* LLC/SNAP is shorter than MAC header :( */
	dev_kfree_skb_any(skb);
	if (new_skb == NULL){
		mpc->eg_ops->put(eg);
		return;
	}
	skb_push(new_skb, eg->ctrl_info.DH_length);     /* add MAC header */
	memcpy(new_skb->data, eg->ctrl_info.DLL_header, eg->ctrl_info.DH_length);
	new_skb->protocol = eth_type_trans(new_skb, dev);
	new_skb->nh.raw = new_skb->data;

	eg->latest_ip_addr = new_skb->nh.iph->saddr;
	eg->packets_rcvd++;
	mpc->eg_ops->put(eg);

	memset(ATM_SKB(skb), 0, sizeof(struct atm_skb_data));
	netif_rx(new_skb);

	return;
}

static struct atmdev_ops mpc_ops = { /* only send is required */
	.close	= mpoad_close,
	.send	= msg_from_mpoad
};

static struct atm_dev mpc_dev = {
	.ops	= &mpc_ops,
	.type	= "mpc",
	.number	= 42,
	.lock	= SPIN_LOCK_UNLOCKED
	/* members not explicitly initialised will be 0 */
};

static int atm_mpoa_mpoad_attach (struct atm_vcc *vcc, int arg)
{
	struct mpoa_client *mpc;
	struct lec_priv *priv;
	int err;
	
	if (mpcs == NULL) {
		init_timer(&mpc_timer);
		mpc_timer_refresh();

		/* This lets us now how our LECs are doing */
		err = register_netdevice_notifier(&mpoa_notifier);
		if (err < 0) {
			del_timer(&mpc_timer);
			return err;
		}
	}
	
	mpc = find_mpc_by_itfnum(arg);
	if (mpc == NULL) {
		dprintk("mpoa: mpoad_attach: allocating new mpc for itf %d\n", arg);
		mpc = alloc_mpc();
		if (mpc == NULL)
			return -ENOMEM;
		mpc->dev_num = arg;
		mpc->dev = find_lec_by_itfnum(arg); /* NULL if there was no lec */
	}
	if (mpc->mpoad_vcc) {
		printk("mpoa: mpoad_attach: mpoad is already present for itf %d\n", arg);
		return -EADDRINUSE;
	}

	if (mpc->dev) { /* check if the lec is LANE2 capable */
		priv = (struct lec_priv *)mpc->dev->priv;
		if (priv->lane_version < 2) {
			dev_put(mpc->dev);
			mpc->dev = NULL;
		} else
			priv->lane2_ops->associate_indicator = lane2_assoc_ind;  
	}

	mpc->mpoad_vcc = vcc;
	vcc->dev = &mpc_dev;
	vcc_insert_socket(sk_atm(vcc));
	set_bit(ATM_VF_META,&vcc->flags);
	set_bit(ATM_VF_READY,&vcc->flags);

	if (mpc->dev) {
		char empty[ATM_ESA_LEN];
		memset(empty, 0, ATM_ESA_LEN);
		
		start_mpc(mpc, mpc->dev);
		/* set address if mpcd e.g. gets killed and restarted.
		 * If we do not do it now we have to wait for the next LE_ARP
		 */
		if ( memcmp(mpc->mps_ctrl_addr, empty, ATM_ESA_LEN) != 0 )
			send_set_mps_ctrl_addr(mpc->mps_ctrl_addr, mpc);
	}

	__module_get(THIS_MODULE);
	return arg;
}

static void send_set_mps_ctrl_addr(char *addr, struct mpoa_client *mpc)
{
	struct k_message mesg;

	memcpy (mpc->mps_ctrl_addr, addr, ATM_ESA_LEN);
	
	mesg.type = SET_MPS_CTRL_ADDR;
	memcpy(mesg.MPS_ctrl, addr, ATM_ESA_LEN);
	msg_to_mpoad(&mesg, mpc);

	return;
}

static void mpoad_close(struct atm_vcc *vcc)
{
	struct mpoa_client *mpc;
	struct sk_buff *skb;

	mpc = find_mpc_by_vcc(vcc);
	if (mpc == NULL) {
		printk("mpoa: mpoad_close: did not find MPC\n");
		return;
	}
	if (!mpc->mpoad_vcc) {
		printk("mpoa: mpoad_close: close for non-present mpoad\n");
		return;
	}
	
	mpc->mpoad_vcc = NULL;
	if (mpc->dev) {
		struct lec_priv *priv = (struct lec_priv *)mpc->dev->priv;
		priv->lane2_ops->associate_indicator = NULL;
		stop_mpc(mpc);
		dev_put(mpc->dev);
	}

	mpc->in_ops->destroy_cache(mpc);
	mpc->eg_ops->destroy_cache(mpc);

	while ((skb = skb_dequeue(&sk_atm(vcc)->sk_receive_queue))) {
		atm_return(vcc, skb->truesize);
		kfree_skb(skb);
	}
	
	printk("mpoa: (%s) going down\n",
		(mpc->dev) ? mpc->dev->name : "<unknown>");
	module_put(THIS_MODULE);

	return;
}

/*
 *
 */
static int msg_from_mpoad(struct atm_vcc *vcc, struct sk_buff *skb)
{
	
	struct mpoa_client *mpc = find_mpc_by_vcc(vcc);
	struct k_message *mesg = (struct k_message*)skb->data;
	atomic_sub(skb->truesize, &sk_atm(vcc)->sk_wmem_alloc);
	
	if (mpc == NULL) {
		printk("mpoa: msg_from_mpoad: no mpc found\n");
		return 0;
	}
	dprintk("mpoa: (%s) msg_from_mpoad:", (mpc->dev) ? mpc->dev->name : "<unknown>");
	switch(mesg->type) {
	case MPOA_RES_REPLY_RCVD:
		dprintk(" mpoa_res_reply_rcvd\n");
		MPOA_res_reply_rcvd(mesg, mpc);
		break;
	case MPOA_TRIGGER_RCVD:
		dprintk(" mpoa_trigger_rcvd\n");
		MPOA_trigger_rcvd(mesg, mpc);
		break;
	case INGRESS_PURGE_RCVD:
		dprintk(" nhrp_purge_rcvd\n");
		ingress_purge_rcvd(mesg, mpc);
		break;
	case EGRESS_PURGE_RCVD:
		dprintk(" egress_purge_reply_rcvd\n");
		egress_purge_rcvd(mesg, mpc);
		break;
	case MPS_DEATH:
		dprintk(" mps_death\n");
		mps_death(mesg, mpc);
		break;
	case CACHE_IMPOS_RCVD:
		dprintk(" cache_impos_rcvd\n");
		MPOA_cache_impos_rcvd(mesg, mpc);
		break;
	case SET_MPC_CTRL_ADDR:
		dprintk(" set_mpc_ctrl_addr\n");
		set_mpc_ctrl_addr_rcvd(mesg, mpc);
		break;
	case SET_MPS_MAC_ADDR:
		dprintk(" set_mps_mac_addr\n");
		set_mps_mac_addr_rcvd(mesg, mpc);
		break;
	case CLEAN_UP_AND_EXIT:
		dprintk(" clean_up_and_exit\n");
		clean_up(mesg, mpc, DIE);
		break;
	case RELOAD:
		dprintk(" reload\n");
		clean_up(mesg, mpc, RELOAD);
		break;
	case SET_MPC_PARAMS:
		dprintk(" set_mpc_params\n");
		mpc->parameters = mesg->content.params;
		break;
	default:
		dprintk(" unknown message %d\n", mesg->type);
		break;
	}
	kfree_skb(skb);

	return 0;
}

/* Remember that this function may not do things that sleep */
int msg_to_mpoad(struct k_message *mesg, struct mpoa_client *mpc)
{
	struct sk_buff *skb;
	struct sock *sk;

	if (mpc == NULL || !mpc->mpoad_vcc) {
		printk("mpoa: msg_to_mpoad: mesg %d to a non-existent mpoad\n", mesg->type);
		return -ENXIO;
	}

	skb = alloc_skb(sizeof(struct k_message), GFP_ATOMIC);
	if (skb == NULL)
		return -ENOMEM;
	skb_put(skb, sizeof(struct k_message));
	memcpy(skb->data, mesg, sizeof(struct k_message));
	atm_force_charge(mpc->mpoad_vcc, skb->truesize);
	
	sk = sk_atm(mpc->mpoad_vcc);
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);

	return 0;
}

static int mpoa_event_listener(struct notifier_block *mpoa_notifier, unsigned long event, void *dev_ptr)
{
	struct net_device *dev;
	struct mpoa_client *mpc;
	struct lec_priv *priv;

	dev = (struct net_device *)dev_ptr;
	if (dev->name == NULL || strncmp(dev->name, "lec", 3))
		return NOTIFY_DONE; /* we are only interested in lec:s */
	
	switch (event) {
	case NETDEV_REGISTER:       /* a new lec device was allocated */
		priv = (struct lec_priv *)dev->priv;
		if (priv->lane_version < 2)
			break;
		priv->lane2_ops->associate_indicator = lane2_assoc_ind;
		mpc = find_mpc_by_itfnum(priv->itfnum);
		if (mpc == NULL) {
			dprintk("mpoa: mpoa_event_listener: allocating new mpc for %s\n",
			       dev->name);
			mpc = alloc_mpc();
			if (mpc == NULL) {
				printk("mpoa: mpoa_event_listener: no new mpc");
				break;
			}
		}
		mpc->dev_num = priv->itfnum;
		mpc->dev = dev;
		dev_hold(dev);
		dprintk("mpoa: (%s) was initialized\n", dev->name);
		break;
	case NETDEV_UNREGISTER:
		/* the lec device was deallocated */
		mpc = find_mpc_by_lec(dev);
		if (mpc == NULL)
			break;
		dprintk("mpoa: device (%s) was deallocated\n", dev->name);
		stop_mpc(mpc);
		dev_put(mpc->dev);
		mpc->dev = NULL;
		break;
	case NETDEV_UP:
		/* the dev was ifconfig'ed up */
		mpc = find_mpc_by_lec(dev);
		if (mpc == NULL)
			break;
		if (mpc->mpoad_vcc != NULL) {
			start_mpc(mpc, dev);
		}
		break;
	case NETDEV_DOWN:
		/* the dev was ifconfig'ed down */
		/* this means that the flow of packets from the
		 * upper layer stops
		 */
		mpc = find_mpc_by_lec(dev);
		if (mpc == NULL)
			break;
		if (mpc->mpoad_vcc != NULL) {
			stop_mpc(mpc);
		}
		break;
	case NETDEV_REBOOT:
	case NETDEV_CHANGE:
	case NETDEV_CHANGEMTU:
	case NETDEV_CHANGEADDR:
	case NETDEV_GOING_DOWN:
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}

/*
 * Functions which are called after a message is received from mpcd.
 * Msg is reused on purpose.
 */


static void MPOA_trigger_rcvd(struct k_message *msg, struct mpoa_client *mpc)
{
	__be32 dst_ip = msg->content.in_info.in_dst_ip;
	in_cache_entry *entry;

	entry = mpc->in_ops->get(dst_ip, mpc);
	if(entry == NULL){
		entry = mpc->in_ops->add_entry(dst_ip, mpc);
		entry->entry_state = INGRESS_RESOLVING;
		msg->type = SND_MPOA_RES_RQST;
		msg->content.in_info = entry->ctrl_info;
		msg_to_mpoad(msg, mpc);
		do_gettimeofday(&(entry->reply_wait));
		mpc->in_ops->put(entry);
		return;
	}
	
	if(entry->entry_state == INGRESS_INVALID){
		entry->entry_state = INGRESS_RESOLVING;
		msg->type = SND_MPOA_RES_RQST;
		msg->content.in_info = entry->ctrl_info;
		msg_to_mpoad(msg, mpc);
		do_gettimeofday(&(entry->reply_wait));
		mpc->in_ops->put(entry);
		return;
	}
	
	printk("mpoa: (%s) MPOA_trigger_rcvd: entry already in resolving state\n",
		(mpc->dev) ? mpc->dev->name : "<unknown>");
	mpc->in_ops->put(entry);
	return;
}

/*
 * Things get complicated because we have to check if there's an egress
 * shortcut with suitable traffic parameters we could use. 
 */
static void check_qos_and_open_shortcut(struct k_message *msg, struct mpoa_client *client, in_cache_entry *entry)
{
	__be32 dst_ip = msg->content.in_info.in_dst_ip;
	struct atm_mpoa_qos *qos = atm_mpoa_search_qos(dst_ip);
	eg_cache_entry *eg_entry = client->eg_ops->get_by_src_ip(dst_ip, client);

	if(eg_entry && eg_entry->shortcut){
		if(eg_entry->shortcut->qos.txtp.traffic_class &
		   msg->qos.txtp.traffic_class &
		   (qos ? qos->qos.txtp.traffic_class : ATM_UBR | ATM_CBR)){
			    if(eg_entry->shortcut->qos.txtp.traffic_class == ATM_UBR)
				    entry->shortcut = eg_entry->shortcut;
			    else if(eg_entry->shortcut->qos.txtp.max_pcr > 0)
				    entry->shortcut = eg_entry->shortcut;
		}
	 	if(entry->shortcut){
			dprintk("mpoa: (%s) using egress SVC to reach %u.%u.%u.%u\n",client->dev->name, NIPQUAD(dst_ip));
			client->eg_ops->put(eg_entry);
			return;
		}
	}
	if (eg_entry != NULL)
		client->eg_ops->put(eg_entry);

	/* No luck in the egress cache we must open an ingress SVC */
	msg->type = OPEN_INGRESS_SVC;
	if (qos && (qos->qos.txtp.traffic_class == msg->qos.txtp.traffic_class))
	{
		msg->qos = qos->qos;
		printk("mpoa: (%s) trying to get a CBR shortcut\n",client->dev->name);
    	}
	else memset(&msg->qos,0,sizeof(struct atm_qos));
	msg_to_mpoad(msg, client);
	return;
}

static void MPOA_res_reply_rcvd(struct k_message *msg, struct mpoa_client *mpc)
{
	__be32 dst_ip = msg->content.in_info.in_dst_ip;
	in_cache_entry *entry = mpc->in_ops->get(dst_ip, mpc);

	dprintk("mpoa: (%s) MPOA_res_reply_rcvd: ip %u.%u.%u.%u\n", mpc->dev->name, NIPQUAD(dst_ip));
	ddprintk("mpoa: (%s) MPOA_res_reply_rcvd() entry = %p", mpc->dev->name, entry);
	if(entry == NULL){
		printk("\nmpoa: (%s) ARGH, received res. reply for an entry that doesn't exist.\n", mpc->dev->name);
		return;
	}
	ddprintk(" entry_state = %d ", entry->entry_state);	

	if (entry->entry_state == INGRESS_RESOLVED) {
		printk("\nmpoa: (%s) MPOA_res_reply_rcvd for RESOLVED entry!\n", mpc->dev->name);
		mpc->in_ops->put(entry);
		return;
	}

	entry->ctrl_info = msg->content.in_info;
	do_gettimeofday(&(entry->tv));
	do_gettimeofday(&(entry->reply_wait)); /* Used in refreshing func from now on */
	entry->refresh_time = 0;
	ddprintk("entry->shortcut = %p\n", entry->shortcut);

	if(entry->entry_state == INGRESS_RESOLVING && entry->shortcut != NULL){
		entry->entry_state = INGRESS_RESOLVED; 
		mpc->in_ops->put(entry);
		return; /* Shortcut already open... */
	}

	if (entry->shortcut != NULL) {
		printk("mpoa: (%s) MPOA_res_reply_rcvd: entry->shortcut != NULL, impossible!\n",
		       mpc->dev->name);
		mpc->in_ops->put(entry);
		return;
	}
	
	check_qos_and_open_shortcut(msg, mpc, entry);
	entry->entry_state = INGRESS_RESOLVED;
	mpc->in_ops->put(entry);

	return;

}

static void ingress_purge_rcvd(struct k_message *msg, struct mpoa_client *mpc)
{
	__be32 dst_ip = msg->content.in_info.in_dst_ip;
	__be32 mask = msg->ip_mask;
	in_cache_entry *entry = mpc->in_ops->get_with_mask(dst_ip, mpc, mask);

	if(entry == NULL){
		printk("mpoa: (%s) ingress_purge_rcvd: purge for a non-existing entry, ", mpc->dev->name);
		printk("ip = %u.%u.%u.%u\n", NIPQUAD(dst_ip));
		return;
	}

	do {
		dprintk("mpoa: (%s) ingress_purge_rcvd: removing an ingress entry, ip = %u.%u.%u.%u\n" ,
			mpc->dev->name, NIPQUAD(dst_ip));
		write_lock_bh(&mpc->ingress_lock);
		mpc->in_ops->remove_entry(entry, mpc);
		write_unlock_bh(&mpc->ingress_lock);
		mpc->in_ops->put(entry);
		entry = mpc->in_ops->get_with_mask(dst_ip, mpc, mask);
	} while (entry != NULL);

	return;
} 

static void egress_purge_rcvd(struct k_message *msg, struct mpoa_client *mpc)
{
	__be32 cache_id = msg->content.eg_info.cache_id;
	eg_cache_entry *entry = mpc->eg_ops->get_by_cache_id(cache_id, mpc);
	
	if (entry == NULL) {
		dprintk("mpoa: (%s) egress_purge_rcvd: purge for a non-existing entry\n", mpc->dev->name);
		return;
	}

	write_lock_irq(&mpc->egress_lock);
	mpc->eg_ops->remove_entry(entry, mpc);
	write_unlock_irq(&mpc->egress_lock);

	mpc->eg_ops->put(entry);

	return;
} 

static void purge_egress_shortcut(struct atm_vcc *vcc, eg_cache_entry *entry)
{
	struct sock *sk;
	struct k_message *purge_msg;
	struct sk_buff *skb;

	dprintk("mpoa: purge_egress_shortcut: entering\n");
	if (vcc == NULL) {
		printk("mpoa: purge_egress_shortcut: vcc == NULL\n");
		return;
	}

	skb = alloc_skb(sizeof(struct k_message), GFP_ATOMIC);
	if (skb == NULL) {
		 printk("mpoa: purge_egress_shortcut: out of memory\n");
		return;
	}

	skb_put(skb, sizeof(struct k_message));
	memset(skb->data, 0, sizeof(struct k_message));
	purge_msg = (struct k_message *)skb->data;
	purge_msg->type = DATA_PLANE_PURGE;
	if (entry != NULL)
		purge_msg->content.eg_info = entry->ctrl_info;

	atm_force_charge(vcc, skb->truesize);

	sk = sk_atm(vcc);
	skb_queue_tail(&sk->sk_receive_queue, skb);
	sk->sk_data_ready(sk, skb->len);
	dprintk("mpoa: purge_egress_shortcut: exiting:\n");

	return;
}

/*
 * Our MPS died. Tell our daemon to send NHRP data plane purge to each
 * of the egress shortcuts we have.
 */
static void mps_death( struct k_message * msg, struct mpoa_client * mpc )
{
	eg_cache_entry *entry;

	dprintk("mpoa: (%s) mps_death:\n", mpc->dev->name);

	if(memcmp(msg->MPS_ctrl, mpc->mps_ctrl_addr, ATM_ESA_LEN)){
		printk("mpoa: (%s) mps_death: wrong MPS\n", mpc->dev->name);
		return;
	}

	/* FIXME: This knows too much of the cache structure */
	read_lock_irq(&mpc->egress_lock);
	entry = mpc->eg_cache;
	while (entry != NULL) {
		purge_egress_shortcut(entry->shortcut, entry);
		entry = entry->next;
	}
	read_unlock_irq(&mpc->egress_lock);

	mpc->in_ops->destroy_cache(mpc);
	mpc->eg_ops->destroy_cache(mpc);

	return;
}

static void MPOA_cache_impos_rcvd( struct k_message * msg, struct mpoa_client * mpc)
{
	uint16_t holding_time;
	eg_cache_entry *entry = mpc->eg_ops->get_by_cache_id(msg->content.eg_info.cache_id, mpc);
	
	holding_time = msg->content.eg_info.holding_time;
	dprintk("mpoa: (%s) MPOA_cache_impos_rcvd: entry = %p, holding_time = %u\n",
	       mpc->dev->name, entry, holding_time);
	if(entry == NULL && holding_time) {
		entry = mpc->eg_ops->add_entry(msg, mpc);
		mpc->eg_ops->put(entry);
		return;
	}
	if(holding_time){
		mpc->eg_ops->update(entry, holding_time);
		return;
	}
	
	write_lock_irq(&mpc->egress_lock);
	mpc->eg_ops->remove_entry(entry, mpc);
	write_unlock_irq(&mpc->egress_lock);

	mpc->eg_ops->put(entry);
	
	return;
}

static void set_mpc_ctrl_addr_rcvd(struct k_message *mesg, struct mpoa_client *mpc)
{
	struct lec_priv *priv;
	int i, retval ;

	uint8_t tlv[4 + 1 + 1 + 1 + ATM_ESA_LEN];

	tlv[0] = 00; tlv[1] = 0xa0; tlv[2] = 0x3e; tlv[3] = 0x2a; /* type  */
	tlv[4] = 1 + 1 + ATM_ESA_LEN;  /* length                           */
	tlv[5] = 0x02;                 /* MPOA client                      */
	tlv[6] = 0x00;                 /* number of MPS MAC addresses      */

	memcpy(&tlv[7], mesg->MPS_ctrl, ATM_ESA_LEN); /* MPC ctrl ATM addr */
	memcpy(mpc->our_ctrl_addr, mesg->MPS_ctrl, ATM_ESA_LEN);

	dprintk("mpoa: (%s) setting MPC ctrl ATM address to ",
	       (mpc->dev) ? mpc->dev->name : "<unknown>");
	for (i = 7; i < sizeof(tlv); i++)
		dprintk("%02x ", tlv[i]);
	dprintk("\n");

	if (mpc->dev) {
		priv = (struct lec_priv *)mpc->dev->priv;
		retval = priv->lane2_ops->associate_req(mpc->dev, mpc->dev->dev_addr, tlv, sizeof(tlv));
		if (retval == 0)
			printk("mpoa: (%s) MPOA device type TLV association failed\n", mpc->dev->name);
		retval = priv->lane2_ops->resolve(mpc->dev, NULL, 1, NULL, NULL);
		if (retval < 0)
			printk("mpoa: (%s) targetless LE_ARP request failed\n", mpc->dev->name);
	}

	return;
}

static void set_mps_mac_addr_rcvd(struct k_message *msg, struct mpoa_client *client)
{

	if(client->number_of_mps_macs)
		kfree(client->mps_macs);
	client->number_of_mps_macs = 0;
	client->mps_macs = kmalloc(ETH_ALEN,GFP_KERNEL);
	if (client->mps_macs == NULL) {
		printk("mpoa: set_mps_mac_addr_rcvd: out of memory\n");
		return;
	}
	client->number_of_mps_macs = 1;
	memcpy(client->mps_macs, msg->MPS_ctrl, ETH_ALEN);
	
	return;
}

/*
 * purge egress cache and tell daemon to 'action' (DIE, RELOAD)
 */
static void clean_up(struct k_message *msg, struct mpoa_client *mpc, int action)
{

	eg_cache_entry *entry;
	msg->type = SND_EGRESS_PURGE;


	/* FIXME: This knows too much of the cache structure */
	read_lock_irq(&mpc->egress_lock);
	entry = mpc->eg_cache;
	while (entry != NULL){
		    msg->content.eg_info = entry->ctrl_info;
		    dprintk("mpoa: cache_id %u\n", entry->ctrl_info.cache_id);
		    msg_to_mpoad(msg, mpc);
		    entry = entry->next;
	}
	read_unlock_irq(&mpc->egress_lock);

	msg->type = action;
	msg_to_mpoad(msg, mpc);
	return;
}

static void mpc_timer_refresh(void)
{
	mpc_timer.expires = jiffies + (MPC_P2 * HZ);
	mpc_timer.data = mpc_timer.expires;
	mpc_timer.function = mpc_cache_check;
	add_timer(&mpc_timer);
	
	return;
}

static void mpc_cache_check( unsigned long checking_time  )
{
	struct mpoa_client *mpc = mpcs;
	static unsigned long previous_resolving_check_time;
	static unsigned long previous_refresh_time;
	
	while( mpc != NULL ){
		mpc->in_ops->clear_count(mpc);
		mpc->eg_ops->clear_expired(mpc);
		if(checking_time - previous_resolving_check_time > mpc->parameters.mpc_p4 * HZ ){
			mpc->in_ops->check_resolving(mpc);
			previous_resolving_check_time = checking_time;
		}
		if(checking_time - previous_refresh_time > mpc->parameters.mpc_p5 * HZ ){
			mpc->in_ops->refresh(mpc);
			previous_refresh_time = checking_time;
		}
		mpc = mpc->next;
	}
	mpc_timer_refresh();
	
	return;
}

static int atm_mpoa_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	struct atm_vcc *vcc = ATM_SD(sock);

	if (cmd != ATMMPC_CTRL && cmd != ATMMPC_DATA)
		return -ENOIOCTLCMD;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (cmd) {
		case ATMMPC_CTRL:
			err = atm_mpoa_mpoad_attach(vcc, (int)arg);
			if (err >= 0)
				sock->state = SS_CONNECTED;
			break;
		case ATMMPC_DATA:
			err = atm_mpoa_vcc_attach(vcc, (void __user *)arg);
			break;
		default:
			break;
	}
	return err;
}


static struct atm_ioctl atm_ioctl_ops = {
	.owner	= THIS_MODULE,
	.ioctl	= atm_mpoa_ioctl,
};

static __init int atm_mpoa_init(void)
{
	register_atm_ioctl(&atm_ioctl_ops);

	if (mpc_proc_init() != 0)
		printk(KERN_INFO "mpoa: failed to initialize /proc/mpoa\n");

	printk("mpc.c: " __DATE__ " " __TIME__ " initialized\n");

	return 0;
}

static void __exit atm_mpoa_cleanup(void)
{
	struct mpoa_client *mpc, *tmp;
	struct atm_mpoa_qos *qos, *nextqos;
	struct lec_priv *priv;

	mpc_proc_clean();

	del_timer(&mpc_timer);
	unregister_netdevice_notifier(&mpoa_notifier);
	deregister_atm_ioctl(&atm_ioctl_ops);

	mpc = mpcs;
	mpcs = NULL;
	while (mpc != NULL) {
		tmp = mpc->next;
		if (mpc->dev != NULL) {
			stop_mpc(mpc);
			priv = (struct lec_priv *)mpc->dev->priv;
			if (priv->lane2_ops != NULL)
				priv->lane2_ops->associate_indicator = NULL;
		}
		ddprintk("mpoa: cleanup_module: about to clear caches\n");
		mpc->in_ops->destroy_cache(mpc);
		mpc->eg_ops->destroy_cache(mpc);
		ddprintk("mpoa: cleanup_module: caches cleared\n");
		kfree(mpc->mps_macs);
		memset(mpc, 0, sizeof(struct mpoa_client));
		ddprintk("mpoa: cleanup_module: about to kfree %p\n", mpc);
		kfree(mpc);
		ddprintk("mpoa: cleanup_module: next mpc is at %p\n", tmp);
		mpc = tmp;
	}

	qos = qos_head;
	qos_head = NULL;
	while (qos != NULL) {
		nextqos = qos->next;
		dprintk("mpoa: cleanup_module: freeing qos entry %p\n", qos);
		kfree(qos);
		qos = nextqos;
	}

	return;
}

module_init(atm_mpoa_init);
module_exit(atm_mpoa_cleanup);

MODULE_LICENSE("GPL");
