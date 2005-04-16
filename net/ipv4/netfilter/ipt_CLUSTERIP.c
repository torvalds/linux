/* Cluster IP hashmark target 
 * (C) 2003-2004 by Harald Welte <laforge@netfilter.org>
 * based on ideas of Fabio Olive Leite <olive@unixforge.org>
 *
 * Development of this code funded by SuSE Linux AG, http://www.suse.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/config.h>
#include <linux/proc_fs.h>
#include <linux/jhash.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <net/checksum.h>

#include <linux/netfilter_arp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_CLUSTERIP.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/lockhelp.h>

#define CLUSTERIP_VERSION "0.6"

#define DEBUG_CLUSTERIP

#ifdef DEBUG_CLUSTERIP
#define DEBUGP	printk
#else
#define DEBUGP
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables target for CLUSTERIP");

struct clusterip_config {
	struct list_head list;			/* list of all configs */
	atomic_t refcount;			/* reference count */

	u_int32_t clusterip;			/* the IP address */
	u_int8_t clustermac[ETH_ALEN];		/* the MAC address */
	struct net_device *dev;			/* device */
	u_int16_t num_total_nodes;		/* total number of nodes */
	u_int16_t num_local_nodes;		/* number of local nodes */
	u_int16_t local_nodes[CLUSTERIP_MAX_NODES];	/* node number array */

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *pde;		/* proc dir entry */
#endif
	enum clusterip_hashmode hash_mode;	/* which hashing mode */
	u_int32_t hash_initval;			/* hash initialization */
};

static LIST_HEAD(clusterip_configs);

/* clusterip_lock protects the clusterip_configs list _AND_ the configurable
 * data within all structurses (num_local_nodes, local_nodes[]) */
static DECLARE_RWLOCK(clusterip_lock);

#ifdef CONFIG_PROC_FS
static struct file_operations clusterip_proc_fops;
static struct proc_dir_entry *clusterip_procdir;
#endif

static inline void
clusterip_config_get(struct clusterip_config *c) {
	atomic_inc(&c->refcount);
}

static inline void
clusterip_config_put(struct clusterip_config *c) {
	if (atomic_dec_and_test(&c->refcount)) {
		WRITE_LOCK(&clusterip_lock);
		list_del(&c->list);
		WRITE_UNLOCK(&clusterip_lock);
		dev_mc_delete(c->dev, c->clustermac, ETH_ALEN, 0);
		dev_put(c->dev);
		kfree(c);
	}
}


static struct clusterip_config *
__clusterip_config_find(u_int32_t clusterip)
{
	struct list_head *pos;

	MUST_BE_READ_LOCKED(&clusterip_lock);
	list_for_each(pos, &clusterip_configs) {
		struct clusterip_config *c = list_entry(pos, 
					struct clusterip_config, list);
		if (c->clusterip == clusterip) {
			return c;
		}
	}

	return NULL;
}

static inline struct clusterip_config *
clusterip_config_find_get(u_int32_t clusterip)
{
	struct clusterip_config *c;

	READ_LOCK(&clusterip_lock);
	c = __clusterip_config_find(clusterip);
	if (!c) {
		READ_UNLOCK(&clusterip_lock);
		return NULL;
	}
	atomic_inc(&c->refcount);
	READ_UNLOCK(&clusterip_lock);

	return c;
}

static struct clusterip_config *
clusterip_config_init(struct ipt_clusterip_tgt_info *i, u_int32_t ip,
			struct net_device *dev)
{
	struct clusterip_config *c;
	char buffer[16];

	c = kmalloc(sizeof(*c), GFP_ATOMIC);
	if (!c)
		return NULL;

	memset(c, 0, sizeof(*c));
	c->dev = dev;
	c->clusterip = ip;
	memcpy(&c->clustermac, &i->clustermac, ETH_ALEN);
	c->num_total_nodes = i->num_total_nodes;
	c->num_local_nodes = i->num_local_nodes;
	memcpy(&c->local_nodes, &i->local_nodes, sizeof(&c->local_nodes));
	c->hash_mode = i->hash_mode;
	c->hash_initval = i->hash_initval;
	atomic_set(&c->refcount, 1);

#ifdef CONFIG_PROC_FS
	/* create proc dir entry */
	sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(ip));
	c->pde = create_proc_entry(buffer, S_IWUSR|S_IRUSR, clusterip_procdir);
	if (!c->pde) {
		kfree(c);
		return NULL;
	}
	c->pde->proc_fops = &clusterip_proc_fops;
	c->pde->data = c;
#endif

	WRITE_LOCK(&clusterip_lock);
	list_add(&c->list, &clusterip_configs);
	WRITE_UNLOCK(&clusterip_lock);

	return c;
}

static int
clusterip_add_node(struct clusterip_config *c, u_int16_t nodenum)
{
	int i;

	WRITE_LOCK(&clusterip_lock);

	if (c->num_local_nodes >= CLUSTERIP_MAX_NODES
	    || nodenum > CLUSTERIP_MAX_NODES) {
		WRITE_UNLOCK(&clusterip_lock);
		return 1;
	}

	/* check if we alrady have this number in our array */
	for (i = 0; i < c->num_local_nodes; i++) {
		if (c->local_nodes[i] == nodenum) {
			WRITE_UNLOCK(&clusterip_lock);
			return 1;
		}
	}

	c->local_nodes[c->num_local_nodes++] = nodenum;

	WRITE_UNLOCK(&clusterip_lock);
	return 0;
}

static int
clusterip_del_node(struct clusterip_config *c, u_int16_t nodenum)
{
	int i;

	WRITE_LOCK(&clusterip_lock);

	if (c->num_local_nodes <= 1 || nodenum > CLUSTERIP_MAX_NODES) {
		WRITE_UNLOCK(&clusterip_lock);
		return 1;
	}
		
	for (i = 0; i < c->num_local_nodes; i++) {
		if (c->local_nodes[i] == nodenum) {
			int size = sizeof(u_int16_t)*(c->num_local_nodes-(i+1));
			memmove(&c->local_nodes[i], &c->local_nodes[i+1], size);
			c->num_local_nodes--;
			WRITE_UNLOCK(&clusterip_lock);
			return 0;
		}
	}

	WRITE_UNLOCK(&clusterip_lock);
	return 1;
}

static inline u_int32_t
clusterip_hashfn(struct sk_buff *skb, struct clusterip_config *config)
{
	struct iphdr *iph = skb->nh.iph;
	unsigned long hashval;
	u_int16_t sport, dport;
	struct tcphdr *th;
	struct udphdr *uh;
	struct icmphdr *ih;

	switch (iph->protocol) {
	case IPPROTO_TCP:
		th = (void *)iph+iph->ihl*4;
		sport = ntohs(th->source);
		dport = ntohs(th->dest);
		break;
	case IPPROTO_UDP:
		uh = (void *)iph+iph->ihl*4;
		sport = ntohs(uh->source);
		dport = ntohs(uh->dest);
		break;
	case IPPROTO_ICMP:
		ih = (void *)iph+iph->ihl*4;
		sport = ntohs(ih->un.echo.id);
		dport = (ih->type<<8)|ih->code;
		break;
	default:
		if (net_ratelimit()) {
			printk(KERN_NOTICE "CLUSTERIP: unknown protocol `%u'\n",
				iph->protocol);
		}
		sport = dport = 0;
	}

	switch (config->hash_mode) {
	case CLUSTERIP_HASHMODE_SIP:
		hashval = jhash_1word(ntohl(iph->saddr),
				      config->hash_initval);
		break;
	case CLUSTERIP_HASHMODE_SIP_SPT:
		hashval = jhash_2words(ntohl(iph->saddr), sport, 
				       config->hash_initval);
		break;
	case CLUSTERIP_HASHMODE_SIP_SPT_DPT:
		hashval = jhash_3words(ntohl(iph->saddr), sport, dport,
				       config->hash_initval);
		break;
	default:
		/* to make gcc happy */
		hashval = 0;
		/* This cannot happen, unless the check function wasn't called
		 * at rule load time */
		printk("CLUSTERIP: unknown mode `%u'\n", config->hash_mode);
		BUG();
		break;
	}

	/* node numbers are 1..n, not 0..n */
	return ((hashval % config->num_total_nodes)+1);
}

static inline int
clusterip_responsible(struct clusterip_config *config, u_int32_t hash)
{
	int i;

	READ_LOCK(&clusterip_lock);

	if (config->num_local_nodes == 0) {
		READ_UNLOCK(&clusterip_lock);
		return 0;
	}

	for (i = 0; i < config->num_local_nodes; i++) {
		if (config->local_nodes[i] == hash) {
			READ_UNLOCK(&clusterip_lock);
			return 1;
		}
	}

	READ_UNLOCK(&clusterip_lock);

	return 0;
}

/*********************************************************************** 
 * IPTABLES TARGET 
 ***********************************************************************/

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const void *targinfo,
       void *userinfo)
{
	const struct ipt_clusterip_tgt_info *cipinfo = targinfo;
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack *ct = ip_conntrack_get((*pskb), &ctinfo);
	u_int32_t hash;

	/* don't need to clusterip_config_get() here, since refcount
	 * is only decremented by destroy() - and ip_tables guarantees
	 * that the ->target() function isn't called after ->destroy() */

	if (!ct) {
		printk(KERN_ERR "CLUSTERIP: no conntrack!\n");
			/* FIXME: need to drop invalid ones, since replies
			 * to outgoing connections of other nodes will be 
			 * marked as INVALID */
		return NF_DROP;
	}

	/* special case: ICMP error handling. conntrack distinguishes between
	 * error messages (RELATED) and information requests (see below) */
	if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP
	    && (ctinfo == IP_CT_RELATED 
		|| ctinfo == IP_CT_IS_REPLY+IP_CT_IS_REPLY))
		return IPT_CONTINUE;

	/* ip_conntrack_icmp guarantees us that we only have ICMP_ECHO, 
	 * TIMESTAMP, INFO_REQUEST or ADDRESS type icmp packets from here
	 * on, which all have an ID field [relevant for hashing]. */

	hash = clusterip_hashfn(*pskb, cipinfo->config);

	switch (ctinfo) {
		case IP_CT_NEW:
			ct->mark = hash;
			break;
		case IP_CT_RELATED:
		case IP_CT_RELATED+IP_CT_IS_REPLY:
			/* FIXME: we don't handle expectations at the
			 * moment.  they can arrive on a different node than
			 * the master connection (e.g. FTP passive mode) */
		case IP_CT_ESTABLISHED:
		case IP_CT_ESTABLISHED+IP_CT_IS_REPLY:
			break;
		default:
			break;
	}

#ifdef DEBUG_CLUSTERP
	DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
#endif
	DEBUGP("hash=%u ct_hash=%lu ", hash, ct->mark);
	if (!clusterip_responsible(cipinfo->config, hash)) {
		DEBUGP("not responsible\n");
		return NF_DROP;
	}
	DEBUGP("responsible\n");

	/* despite being received via linklayer multicast, this is
	 * actually a unicast IP packet. TCP doesn't like PACKET_MULTICAST */
	(*pskb)->pkt_type = PACKET_HOST;

	return IPT_CONTINUE;
}

static int
checkentry(const char *tablename,
	   const struct ipt_entry *e,
           void *targinfo,
           unsigned int targinfosize,
           unsigned int hook_mask)
{
	struct ipt_clusterip_tgt_info *cipinfo = targinfo;

	struct clusterip_config *config;

	if (targinfosize != IPT_ALIGN(sizeof(struct ipt_clusterip_tgt_info))) {
		printk(KERN_WARNING "CLUSTERIP: targinfosize %u != %Zu\n",
		       targinfosize,
		       IPT_ALIGN(sizeof(struct ipt_clusterip_tgt_info)));
		return 0;
	}

	if (cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP &&
	    cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP_SPT &&
	    cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP_SPT_DPT) {
		printk(KERN_WARNING "CLUSTERIP: unknown mode `%u'\n",
			cipinfo->hash_mode);
		return 0;

	}
	if (e->ip.dmsk.s_addr != 0xffffffff
	    || e->ip.dst.s_addr == 0) {
		printk(KERN_ERR "CLUSTERIP: Please specify destination IP\n");
		return 0;
	}

	/* FIXME: further sanity checks */

	config = clusterip_config_find_get(e->ip.dst.s_addr);
	if (!config) {
		if (!(cipinfo->flags & CLUSTERIP_FLAG_NEW)) {
			printk(KERN_WARNING "CLUSTERIP: no config found for %u.%u.%u.%u, need 'new'\n", NIPQUAD(e->ip.dst.s_addr));
			return 0;
		} else {
			struct net_device *dev;

			if (e->ip.iniface[0] == '\0') {
				printk(KERN_WARNING "CLUSTERIP: Please specify an interface name\n");
				return 0;
			}

			dev = dev_get_by_name(e->ip.iniface);
			if (!dev) {
				printk(KERN_WARNING "CLUSTERIP: no such interface %s\n", e->ip.iniface);
				return 0;
			}

			config = clusterip_config_init(cipinfo, 
							e->ip.dst.s_addr, dev);
			if (!config) {
				printk(KERN_WARNING "CLUSTERIP: cannot allocate config\n");
				dev_put(dev);
				return 0;
			}
			dev_mc_add(config->dev,config->clustermac, ETH_ALEN, 0);
		}
	}

	cipinfo->config = config;

	return 1;
}

/* drop reference count of cluster config when rule is deleted */
static void destroy(void *matchinfo, unsigned int matchinfosize)
{
	struct ipt_clusterip_tgt_info *cipinfo = matchinfo;

	/* we first remove the proc entry and then drop the reference
	 * count.  In case anyone still accesses the file, the open/close
	 * functions are also incrementing the refcount on their own */
#ifdef CONFIG_PROC_FS
	remove_proc_entry(cipinfo->config->pde->name,
			  cipinfo->config->pde->parent);
#endif
	clusterip_config_put(cipinfo->config);
}

static struct ipt_target clusterip_tgt = { 
	.name = "CLUSTERIP",
	.target = &target, 
	.checkentry = &checkentry, 
	.destroy = &destroy,
	.me = THIS_MODULE
};


/*********************************************************************** 
 * ARP MANGLING CODE 
 ***********************************************************************/

/* hardcoded for 48bit ethernet and 32bit ipv4 addresses */
struct arp_payload {
	u_int8_t src_hw[ETH_ALEN];
	u_int32_t src_ip;
	u_int8_t dst_hw[ETH_ALEN];
	u_int32_t dst_ip;
} __attribute__ ((packed));

#ifdef CLUSTERIP_DEBUG
static void arp_print(struct arp_payload *payload) 
{
#define HBUFFERLEN 30
	char hbuffer[HBUFFERLEN];
	int j,k;
	const char hexbuf[]= "0123456789abcdef";

	for (k=0, j=0; k < HBUFFERLEN-3 && j < ETH_ALEN; j++) {
		hbuffer[k++]=hexbuf[(payload->src_hw[j]>>4)&15];
		hbuffer[k++]=hexbuf[payload->src_hw[j]&15];
		hbuffer[k++]=':';
	}
	hbuffer[--k]='\0';

	printk("src %u.%u.%u.%u@%s, dst %u.%u.%u.%u\n", 
		NIPQUAD(payload->src_ip), hbuffer,
		NIPQUAD(payload->dst_ip));
}
#endif

static unsigned int
arp_mangle(unsigned int hook,
	   struct sk_buff **pskb,
	   const struct net_device *in,
	   const struct net_device *out,
	   int (*okfn)(struct sk_buff *))
{
	struct arphdr *arp = (*pskb)->nh.arph;
	struct arp_payload *payload;
	struct clusterip_config *c;

	/* we don't care about non-ethernet and non-ipv4 ARP */
	if (arp->ar_hrd != htons(ARPHRD_ETHER)
	    || arp->ar_pro != htons(ETH_P_IP)
	    || arp->ar_pln != 4 || arp->ar_hln != ETH_ALEN)
		return NF_ACCEPT;

	/* we only want to mangle arp replies */
	if (arp->ar_op != htons(ARPOP_REPLY))
		return NF_ACCEPT;

	payload = (void *)(arp+1);

	/* if there is no clusterip configuration for the arp reply's 
	 * source ip, we don't want to mangle it */
	c = clusterip_config_find_get(payload->src_ip);
	if (!c)
		return NF_ACCEPT;

	/* normally the linux kernel always replies to arp queries of 
	 * addresses on different interfacs.  However, in the CLUSTERIP case
	 * this wouldn't work, since we didn't subscribe the mcast group on
	 * other interfaces */
	if (c->dev != out) {
		DEBUGP("CLUSTERIP: not mangling arp reply on different "
		       "interface: cip'%s'-skb'%s'\n", c->dev->name, out->name);
		clusterip_config_put(c);
		return NF_ACCEPT;
	}

	/* mangle reply hardware address */
	memcpy(payload->src_hw, c->clustermac, arp->ar_hln);

#ifdef CLUSTERIP_DEBUG
	DEBUGP(KERN_DEBUG "CLUSTERIP mangled arp reply: ");
	arp_print(payload);
#endif

	clusterip_config_put(c);

	return NF_ACCEPT;
}

static struct nf_hook_ops cip_arp_ops = {
	.hook = arp_mangle,
	.pf = NF_ARP,
	.hooknum = NF_ARP_OUT,
	.priority = -1
};

/*********************************************************************** 
 * PROC DIR HANDLING 
 ***********************************************************************/

#ifdef CONFIG_PROC_FS

static void *clusterip_seq_start(struct seq_file *s, loff_t *pos)
{
	struct proc_dir_entry *pde = s->private;
	struct clusterip_config *c = pde->data;
	unsigned int *nodeidx;

	READ_LOCK(&clusterip_lock);
	if (*pos >= c->num_local_nodes)
		return NULL;

	nodeidx = kmalloc(sizeof(unsigned int), GFP_KERNEL);
	if (!nodeidx)
		return ERR_PTR(-ENOMEM);

	*nodeidx = *pos;
	return nodeidx;
}

static void *clusterip_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct proc_dir_entry *pde = s->private;
	struct clusterip_config *c = pde->data;
	unsigned int *nodeidx = (unsigned int *)v;

	*pos = ++(*nodeidx);
	if (*pos >= c->num_local_nodes) {
		kfree(v);
		return NULL;
	}
	return nodeidx;
}

static void clusterip_seq_stop(struct seq_file *s, void *v)
{
	kfree(v);

	READ_UNLOCK(&clusterip_lock);
}

static int clusterip_seq_show(struct seq_file *s, void *v)
{
	struct proc_dir_entry *pde = s->private;
	struct clusterip_config *c = pde->data;
	unsigned int *nodeidx = (unsigned int *)v;

	if (*nodeidx != 0) 
		seq_putc(s, ',');
	seq_printf(s, "%u", c->local_nodes[*nodeidx]);

	if (*nodeidx == c->num_local_nodes-1)
		seq_putc(s, '\n');

	return 0;
}

static struct seq_operations clusterip_seq_ops = {
	.start	= clusterip_seq_start,
	.next	= clusterip_seq_next,
	.stop	= clusterip_seq_stop,
	.show	= clusterip_seq_show,
};

static int clusterip_proc_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &clusterip_seq_ops);

	if (!ret) {
		struct seq_file *sf = file->private_data;
		struct proc_dir_entry *pde = PDE(inode);
		struct clusterip_config *c = pde->data;

		sf->private = pde;

		clusterip_config_get(c);
	}

	return ret;
}

static int clusterip_proc_release(struct inode *inode, struct file *file)
{
	struct proc_dir_entry *pde = PDE(inode);
	struct clusterip_config *c = pde->data;
	int ret;

	ret = seq_release(inode, file);

	if (!ret)
		clusterip_config_put(c);

	return ret;
}

static ssize_t clusterip_proc_write(struct file *file, const char __user *input,
				size_t size, loff_t *ofs)
{
#define PROC_WRITELEN	10
	char buffer[PROC_WRITELEN+1];
	struct proc_dir_entry *pde = PDE(file->f_dentry->d_inode);
	struct clusterip_config *c = pde->data;
	unsigned long nodenum;

	if (copy_from_user(buffer, input, PROC_WRITELEN))
		return -EFAULT;

	if (*buffer == '+') {
		nodenum = simple_strtoul(buffer+1, NULL, 10);
		if (clusterip_add_node(c, nodenum))
			return -ENOMEM;
	} else if (*buffer == '-') {
		nodenum = simple_strtoul(buffer+1, NULL,10);
		if (clusterip_del_node(c, nodenum))
			return -ENOENT;
	} else
		return -EIO;

	return size;
}

static struct file_operations clusterip_proc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = clusterip_proc_open,
	.read	 = seq_read,
	.write	 = clusterip_proc_write,
	.llseek	 = seq_lseek,
	.release = clusterip_proc_release,
};

#endif /* CONFIG_PROC_FS */

static int init_or_cleanup(int fini)
{
	int ret;

	if (fini)
		goto cleanup;

	if (ipt_register_target(&clusterip_tgt)) {
		ret = -EINVAL;
		goto cleanup_none;
	}

	if (nf_register_hook(&cip_arp_ops) < 0) {
		ret = -EINVAL;
		goto cleanup_target;
	}

#ifdef CONFIG_PROC_FS
	clusterip_procdir = proc_mkdir("ipt_CLUSTERIP", proc_net);
	if (!clusterip_procdir) {
		printk(KERN_ERR "CLUSTERIP: Unable to proc dir entry\n");
		ret = -ENOMEM;
		goto cleanup_hook;
	}
#endif /* CONFIG_PROC_FS */

	printk(KERN_NOTICE "ClusterIP Version %s loaded successfully\n",
		CLUSTERIP_VERSION);

	return 0;

cleanup:
	printk(KERN_NOTICE "ClusterIP Version %s unloading\n",
		CLUSTERIP_VERSION);
#ifdef CONFIG_PROC_FS
	remove_proc_entry(clusterip_procdir->name, clusterip_procdir->parent);
#endif
cleanup_hook:
	nf_unregister_hook(&cip_arp_ops);
cleanup_target:
	ipt_unregister_target(&clusterip_tgt);
cleanup_none:
	return -EINVAL;
}

static int __init init(void)
{
	return init_or_cleanup(0);
}

static void __exit fini(void)
{
	init_or_cleanup(1);
}

module_init(init);
module_exit(fini);
