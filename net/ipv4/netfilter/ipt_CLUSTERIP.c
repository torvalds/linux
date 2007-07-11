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
#include <linux/proc_fs.h>
#include <linux/jhash.h>
#include <linux/bitops.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/netfilter_arp.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ipt_CLUSTERIP.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/checksum.h>

#define CLUSTERIP_VERSION "0.8"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Harald Welte <laforge@netfilter.org>");
MODULE_DESCRIPTION("iptables target for CLUSTERIP");

struct clusterip_config {
	struct list_head list;			/* list of all configs */
	atomic_t refcount;			/* reference count */
	atomic_t entries;			/* number of entries/rules
						 * referencing us */

	__be32 clusterip;			/* the IP address */
	u_int8_t clustermac[ETH_ALEN];		/* the MAC address */
	struct net_device *dev;			/* device */
	u_int16_t num_total_nodes;		/* total number of nodes */
	unsigned long local_nodes;		/* node number array */

#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *pde;		/* proc dir entry */
#endif
	enum clusterip_hashmode hash_mode;	/* which hashing mode */
	u_int32_t hash_initval;			/* hash initialization */
};

static LIST_HEAD(clusterip_configs);

/* clusterip_lock protects the clusterip_configs list */
static DEFINE_RWLOCK(clusterip_lock);

#ifdef CONFIG_PROC_FS
static const struct file_operations clusterip_proc_fops;
static struct proc_dir_entry *clusterip_procdir;
#endif

static inline void
clusterip_config_get(struct clusterip_config *c)
{
	atomic_inc(&c->refcount);
}

static inline void
clusterip_config_put(struct clusterip_config *c)
{
	if (atomic_dec_and_test(&c->refcount))
		kfree(c);
}

/* increase the count of entries(rules) using/referencing this config */
static inline void
clusterip_config_entry_get(struct clusterip_config *c)
{
	atomic_inc(&c->entries);
}

/* decrease the count of entries using/referencing this config.  If last
 * entry(rule) is removed, remove the config from lists, but don't free it
 * yet, since proc-files could still be holding references */
static inline void
clusterip_config_entry_put(struct clusterip_config *c)
{
	if (atomic_dec_and_test(&c->entries)) {
		write_lock_bh(&clusterip_lock);
		list_del(&c->list);
		write_unlock_bh(&clusterip_lock);

		dev_mc_delete(c->dev, c->clustermac, ETH_ALEN, 0);
		dev_put(c->dev);

		/* In case anyone still accesses the file, the open/close
		 * functions are also incrementing the refcount on their own,
		 * so it's safe to remove the entry even if it's in use. */
#ifdef CONFIG_PROC_FS
		remove_proc_entry(c->pde->name, c->pde->parent);
#endif
	}
}

static struct clusterip_config *
__clusterip_config_find(__be32 clusterip)
{
	struct list_head *pos;

	list_for_each(pos, &clusterip_configs) {
		struct clusterip_config *c = list_entry(pos,
					struct clusterip_config, list);
		if (c->clusterip == clusterip)
			return c;
	}

	return NULL;
}

static inline struct clusterip_config *
clusterip_config_find_get(__be32 clusterip, int entry)
{
	struct clusterip_config *c;

	read_lock_bh(&clusterip_lock);
	c = __clusterip_config_find(clusterip);
	if (!c) {
		read_unlock_bh(&clusterip_lock);
		return NULL;
	}
	atomic_inc(&c->refcount);
	if (entry)
		atomic_inc(&c->entries);
	read_unlock_bh(&clusterip_lock);

	return c;
}

static void
clusterip_config_init_nodelist(struct clusterip_config *c,
			       const struct ipt_clusterip_tgt_info *i)
{
	int n;

	for (n = 0; n < i->num_local_nodes; n++)
		set_bit(i->local_nodes[n] - 1, &c->local_nodes);
}

static struct clusterip_config *
clusterip_config_init(struct ipt_clusterip_tgt_info *i, __be32 ip,
			struct net_device *dev)
{
	struct clusterip_config *c;

	c = kzalloc(sizeof(*c), GFP_ATOMIC);
	if (!c)
		return NULL;

	c->dev = dev;
	c->clusterip = ip;
	memcpy(&c->clustermac, &i->clustermac, ETH_ALEN);
	c->num_total_nodes = i->num_total_nodes;
	clusterip_config_init_nodelist(c, i);
	c->hash_mode = i->hash_mode;
	c->hash_initval = i->hash_initval;
	atomic_set(&c->refcount, 1);
	atomic_set(&c->entries, 1);

#ifdef CONFIG_PROC_FS
	{
		char buffer[16];

		/* create proc dir entry */
		sprintf(buffer, "%u.%u.%u.%u", NIPQUAD(ip));
		c->pde = create_proc_entry(buffer, S_IWUSR|S_IRUSR,
					   clusterip_procdir);
		if (!c->pde) {
			kfree(c);
			return NULL;
		}
	}
	c->pde->proc_fops = &clusterip_proc_fops;
	c->pde->data = c;
#endif

	write_lock_bh(&clusterip_lock);
	list_add(&c->list, &clusterip_configs);
	write_unlock_bh(&clusterip_lock);

	return c;
}

#ifdef CONFIG_PROC_FS
static int
clusterip_add_node(struct clusterip_config *c, u_int16_t nodenum)
{

	if (nodenum == 0 ||
	    nodenum > c->num_total_nodes)
		return 1;

	/* check if we already have this number in our bitfield */
	if (test_and_set_bit(nodenum - 1, &c->local_nodes))
		return 1;

	return 0;
}

static bool
clusterip_del_node(struct clusterip_config *c, u_int16_t nodenum)
{
	if (nodenum == 0 ||
	    nodenum > c->num_total_nodes)
		return true;

	if (test_and_clear_bit(nodenum - 1, &c->local_nodes))
		return false;

	return true;
}
#endif

static inline u_int32_t
clusterip_hashfn(const struct sk_buff *skb,
		 const struct clusterip_config *config)
{
	const struct iphdr *iph = ip_hdr(skb);
	unsigned long hashval;
	u_int16_t sport, dport;
	const u_int16_t *ports;

	switch (iph->protocol) {
	case IPPROTO_TCP:
	case IPPROTO_UDP:
	case IPPROTO_UDPLITE:
	case IPPROTO_SCTP:
	case IPPROTO_DCCP:
	case IPPROTO_ICMP:
		ports = (const void *)iph+iph->ihl*4;
		sport = ports[0];
		dport = ports[1];
		break;
	default:
		if (net_ratelimit())
			printk(KERN_NOTICE "CLUSTERIP: unknown protocol `%u'\n",
				iph->protocol);
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
	return (hashval % config->num_total_nodes) + 1;
}

static inline int
clusterip_responsible(const struct clusterip_config *config, u_int32_t hash)
{
	return test_bit(hash - 1, &config->local_nodes);
}

/***********************************************************************
 * IPTABLES TARGET
 ***********************************************************************/

static unsigned int
target(struct sk_buff **pskb,
       const struct net_device *in,
       const struct net_device *out,
       unsigned int hooknum,
       const struct xt_target *target,
       const void *targinfo)
{
	const struct ipt_clusterip_tgt_info *cipinfo = targinfo;
	struct nf_conn *ct;
	enum ip_conntrack_info ctinfo;
	u_int32_t hash;

	/* don't need to clusterip_config_get() here, since refcount
	 * is only decremented by destroy() - and ip_tables guarantees
	 * that the ->target() function isn't called after ->destroy() */

	ct = nf_ct_get(*pskb, &ctinfo);
	if (ct == NULL) {
		printk(KERN_ERR "CLUSTERIP: no conntrack!\n");
			/* FIXME: need to drop invalid ones, since replies
			 * to outgoing connections of other nodes will be
			 * marked as INVALID */
		return NF_DROP;
	}

	/* special case: ICMP error handling. conntrack distinguishes between
	 * error messages (RELATED) and information requests (see below) */
	if (ip_hdr(*pskb)->protocol == IPPROTO_ICMP
	    && (ctinfo == IP_CT_RELATED
		|| ctinfo == IP_CT_RELATED+IP_CT_IS_REPLY))
		return XT_CONTINUE;

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

#ifdef DEBUG
	DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
#endif
	pr_debug("hash=%u ct_hash=%u ", hash, ct->mark);
	if (!clusterip_responsible(cipinfo->config, hash)) {
		pr_debug("not responsible\n");
		return NF_DROP;
	}
	pr_debug("responsible\n");

	/* despite being received via linklayer multicast, this is
	 * actually a unicast IP packet. TCP doesn't like PACKET_MULTICAST */
	(*pskb)->pkt_type = PACKET_HOST;

	return XT_CONTINUE;
}

static bool
checkentry(const char *tablename,
	   const void *e_void,
	   const struct xt_target *target,
	   void *targinfo,
	   unsigned int hook_mask)
{
	struct ipt_clusterip_tgt_info *cipinfo = targinfo;
	const struct ipt_entry *e = e_void;

	struct clusterip_config *config;

	if (cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP &&
	    cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP_SPT &&
	    cipinfo->hash_mode != CLUSTERIP_HASHMODE_SIP_SPT_DPT) {
		printk(KERN_WARNING "CLUSTERIP: unknown mode `%u'\n",
			cipinfo->hash_mode);
		return false;

	}
	if (e->ip.dmsk.s_addr != htonl(0xffffffff)
	    || e->ip.dst.s_addr == 0) {
		printk(KERN_ERR "CLUSTERIP: Please specify destination IP\n");
		return false;
	}

	/* FIXME: further sanity checks */

	config = clusterip_config_find_get(e->ip.dst.s_addr, 1);
	if (!config) {
		if (!(cipinfo->flags & CLUSTERIP_FLAG_NEW)) {
			printk(KERN_WARNING "CLUSTERIP: no config found for %u.%u.%u.%u, need 'new'\n", NIPQUAD(e->ip.dst.s_addr));
			return false;
		} else {
			struct net_device *dev;

			if (e->ip.iniface[0] == '\0') {
				printk(KERN_WARNING "CLUSTERIP: Please specify an interface name\n");
				return false;
			}

			dev = dev_get_by_name(e->ip.iniface);
			if (!dev) {
				printk(KERN_WARNING "CLUSTERIP: no such interface %s\n", e->ip.iniface);
				return false;
			}

			config = clusterip_config_init(cipinfo,
							e->ip.dst.s_addr, dev);
			if (!config) {
				printk(KERN_WARNING "CLUSTERIP: cannot allocate config\n");
				dev_put(dev);
				return false;
			}
			dev_mc_add(config->dev,config->clustermac, ETH_ALEN, 0);
		}
	}
	cipinfo->config = config;

	if (nf_ct_l3proto_try_module_get(target->family) < 0) {
		printk(KERN_WARNING "can't load conntrack support for "
				    "proto=%d\n", target->family);
		return false;
	}

	return true;
}

/* drop reference count of cluster config when rule is deleted */
static void destroy(const struct xt_target *target, void *targinfo)
{
	struct ipt_clusterip_tgt_info *cipinfo = targinfo;

	/* if no more entries are referencing the config, remove it
	 * from the list and destroy the proc entry */
	clusterip_config_entry_put(cipinfo->config);

	clusterip_config_put(cipinfo->config);

	nf_ct_l3proto_module_put(target->family);
}

#ifdef CONFIG_COMPAT
struct compat_ipt_clusterip_tgt_info
{
	u_int32_t	flags;
	u_int8_t	clustermac[6];
	u_int16_t	num_total_nodes;
	u_int16_t	num_local_nodes;
	u_int16_t	local_nodes[CLUSTERIP_MAX_NODES];
	u_int32_t	hash_mode;
	u_int32_t	hash_initval;
	compat_uptr_t	config;
};
#endif /* CONFIG_COMPAT */

static struct xt_target clusterip_tgt __read_mostly = {
	.name		= "CLUSTERIP",
	.family		= AF_INET,
	.target		= target,
	.checkentry	= checkentry,
	.destroy	= destroy,
	.targetsize	= sizeof(struct ipt_clusterip_tgt_info),
#ifdef CONFIG_COMPAT
	.compatsize	= sizeof(struct compat_ipt_clusterip_tgt_info),
#endif /* CONFIG_COMPAT */
	.me		= THIS_MODULE
};


/***********************************************************************
 * ARP MANGLING CODE
 ***********************************************************************/

/* hardcoded for 48bit ethernet and 32bit ipv4 addresses */
struct arp_payload {
	u_int8_t src_hw[ETH_ALEN];
	__be32 src_ip;
	u_int8_t dst_hw[ETH_ALEN];
	__be32 dst_ip;
} __attribute__ ((packed));

#ifdef DEBUG
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
	struct arphdr *arp = arp_hdr(*pskb);
	struct arp_payload *payload;
	struct clusterip_config *c;

	/* we don't care about non-ethernet and non-ipv4 ARP */
	if (arp->ar_hrd != htons(ARPHRD_ETHER)
	    || arp->ar_pro != htons(ETH_P_IP)
	    || arp->ar_pln != 4 || arp->ar_hln != ETH_ALEN)
		return NF_ACCEPT;

	/* we only want to mangle arp requests and replies */
	if (arp->ar_op != htons(ARPOP_REPLY)
	    && arp->ar_op != htons(ARPOP_REQUEST))
		return NF_ACCEPT;

	payload = (void *)(arp+1);

	/* if there is no clusterip configuration for the arp reply's
	 * source ip, we don't want to mangle it */
	c = clusterip_config_find_get(payload->src_ip, 0);
	if (!c)
		return NF_ACCEPT;

	/* normally the linux kernel always replies to arp queries of
	 * addresses on different interfacs.  However, in the CLUSTERIP case
	 * this wouldn't work, since we didn't subscribe the mcast group on
	 * other interfaces */
	if (c->dev != out) {
		pr_debug("CLUSTERIP: not mangling arp reply on different "
			 "interface: cip'%s'-skb'%s'\n",
			 c->dev->name, out->name);
		clusterip_config_put(c);
		return NF_ACCEPT;
	}

	/* mangle reply hardware address */
	memcpy(payload->src_hw, c->clustermac, arp->ar_hln);

#ifdef DEBUG
	pr_debug(KERN_DEBUG "CLUSTERIP mangled arp reply: ");
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

struct clusterip_seq_position {
	unsigned int pos;	/* position */
	unsigned int weight;	/* number of bits set == size */
	unsigned int bit;	/* current bit */
	unsigned long val;	/* current value */
};

static void *clusterip_seq_start(struct seq_file *s, loff_t *pos)
{
	struct proc_dir_entry *pde = s->private;
	struct clusterip_config *c = pde->data;
	unsigned int weight;
	u_int32_t local_nodes;
	struct clusterip_seq_position *idx;

	/* FIXME: possible race */
	local_nodes = c->local_nodes;
	weight = hweight32(local_nodes);
	if (*pos >= weight)
		return NULL;

	idx = kmalloc(sizeof(struct clusterip_seq_position), GFP_KERNEL);
	if (!idx)
		return ERR_PTR(-ENOMEM);

	idx->pos = *pos;
	idx->weight = weight;
	idx->bit = ffs(local_nodes);
	idx->val = local_nodes;
	clear_bit(idx->bit - 1, &idx->val);

	return idx;
}

static void *clusterip_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct clusterip_seq_position *idx = (struct clusterip_seq_position *)v;

	*pos = ++idx->pos;
	if (*pos >= idx->weight) {
		kfree(v);
		return NULL;
	}
	idx->bit = ffs(idx->val);
	clear_bit(idx->bit - 1, &idx->val);
	return idx;
}

static void clusterip_seq_stop(struct seq_file *s, void *v)
{
	kfree(v);
}

static int clusterip_seq_show(struct seq_file *s, void *v)
{
	struct clusterip_seq_position *idx = (struct clusterip_seq_position *)v;

	if (idx->pos != 0)
		seq_putc(s, ',');

	seq_printf(s, "%u", idx->bit);

	if (idx->pos == idx->weight - 1)
		seq_putc(s, '\n');

	return 0;
}

static const struct seq_operations clusterip_seq_ops = {
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
	struct proc_dir_entry *pde = PDE(file->f_path.dentry->d_inode);
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

static const struct file_operations clusterip_proc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = clusterip_proc_open,
	.read	 = seq_read,
	.write	 = clusterip_proc_write,
	.llseek	 = seq_lseek,
	.release = clusterip_proc_release,
};

#endif /* CONFIG_PROC_FS */

static int __init ipt_clusterip_init(void)
{
	int ret;

	ret = xt_register_target(&clusterip_tgt);
	if (ret < 0)
		return ret;

	ret = nf_register_hook(&cip_arp_ops);
	if (ret < 0)
		goto cleanup_target;

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

#ifdef CONFIG_PROC_FS
cleanup_hook:
	nf_unregister_hook(&cip_arp_ops);
#endif /* CONFIG_PROC_FS */
cleanup_target:
	xt_unregister_target(&clusterip_tgt);
	return ret;
}

static void __exit ipt_clusterip_fini(void)
{
	printk(KERN_NOTICE "ClusterIP Version %s unloading\n",
		CLUSTERIP_VERSION);
#ifdef CONFIG_PROC_FS
	remove_proc_entry(clusterip_procdir->name, clusterip_procdir->parent);
#endif
	nf_unregister_hook(&cip_arp_ops);
	xt_unregister_target(&clusterip_tgt);
}

module_init(ipt_clusterip_init);
module_exit(ipt_clusterip_fini);
