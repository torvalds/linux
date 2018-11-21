// SPDX-License-Identifier: GPL-2.0
/* net/atm/proc.c - ATM /proc interface
 *
 * Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA
 *
 * seq_file api usage by romieu@fr.zoreil.com
 *
 * Evaluating the efficiency of the whole thing if left as an exercise to
 * the reader.
 */

#include <linux/module.h> /* for EXPORT_SYMBOL */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/netdevice.h>
#include <linux/atmclip.h>
#include <linux/init.h> /* for __init */
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/atmclip.h>
#include <linux/uaccess.h>
#include <linux/param.h> /* for HZ */
#include <linux/atomic.h>
#include "resources.h"
#include "common.h" /* atm_proc_init prototype */
#include "signaling.h" /* to get sigd - ugly too */

static ssize_t proc_dev_atm_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos);

static const struct file_operations proc_atm_dev_ops = {
	.read =		proc_dev_atm_read,
	.llseek =	noop_llseek,
};

static void add_stats(struct seq_file *seq, const char *aal,
  const struct k_atm_aal_stats *stats)
{
	seq_printf(seq, "%s ( %d %d %d %d %d )", aal,
		   atomic_read(&stats->tx), atomic_read(&stats->tx_err),
		   atomic_read(&stats->rx), atomic_read(&stats->rx_err),
		   atomic_read(&stats->rx_drop));
}

static void atm_dev_info(struct seq_file *seq, const struct atm_dev *dev)
{
	int i;

	seq_printf(seq, "%3d %-8s", dev->number, dev->type);
	for (i = 0; i < ESI_LEN; i++)
		seq_printf(seq, "%02x", dev->esi[i]);
	seq_puts(seq, "  ");
	add_stats(seq, "0", &dev->stats.aal0);
	seq_puts(seq, "  ");
	add_stats(seq, "5", &dev->stats.aal5);
	seq_printf(seq, "\t[%d]", refcount_read(&dev->refcnt));
	seq_putc(seq, '\n');
}

struct vcc_state {
	int bucket;
	struct sock *sk;
};

static inline int compare_family(struct sock *sk, int family)
{
	return !family || (sk->sk_family == family);
}

static int __vcc_walk(struct sock **sock, int family, int *bucket, loff_t l)
{
	struct sock *sk = *sock;

	if (sk == SEQ_START_TOKEN) {
		for (*bucket = 0; *bucket < VCC_HTABLE_SIZE; ++*bucket) {
			struct hlist_head *head = &vcc_hash[*bucket];

			sk = hlist_empty(head) ? NULL : __sk_head(head);
			if (sk)
				break;
		}
		l--;
	}
try_again:
	for (; sk; sk = sk_next(sk)) {
		l -= compare_family(sk, family);
		if (l < 0)
			goto out;
	}
	if (!sk && ++*bucket < VCC_HTABLE_SIZE) {
		sk = sk_head(&vcc_hash[*bucket]);
		goto try_again;
	}
	sk = SEQ_START_TOKEN;
out:
	*sock = sk;
	return (l < 0);
}

static inline void *vcc_walk(struct seq_file *seq, loff_t l)
{
	struct vcc_state *state = seq->private;
	int family = (uintptr_t)(PDE_DATA(file_inode(seq->file)));

	return __vcc_walk(&state->sk, family, &state->bucket, l) ?
	       state : NULL;
}

static void *vcc_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(vcc_sklist_lock)
{
	struct vcc_state *state = seq->private;
	loff_t left = *pos;

	read_lock(&vcc_sklist_lock);
	state->sk = SEQ_START_TOKEN;
	return left ? vcc_walk(seq, left) : SEQ_START_TOKEN;
}

static void vcc_seq_stop(struct seq_file *seq, void *v)
	__releases(vcc_sklist_lock)
{
	read_unlock(&vcc_sklist_lock);
}

static void *vcc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	v = vcc_walk(seq, 1);
	*pos += !!PTR_ERR(v);
	return v;
}

static void pvc_info(struct seq_file *seq, struct atm_vcc *vcc)
{
	static const char *const class_name[] = {
		"off", "UBR", "CBR", "VBR", "ABR"};
	static const char *const aal_name[] = {
		"---",	"1",	"2",	"3/4",	/*  0- 3 */
		"???",	"5",	"???",	"???",	/*  4- 7 */
		"???",	"???",	"???",	"???",	/*  8-11 */
		"???",	"0",	"???",	"???"};	/* 12-15 */

	seq_printf(seq, "%3d %3d %5d %-3s %7d %-5s %7d %-6s",
		   vcc->dev->number, vcc->vpi, vcc->vci,
		   vcc->qos.aal >= ARRAY_SIZE(aal_name) ? "err" :
		   aal_name[vcc->qos.aal], vcc->qos.rxtp.min_pcr,
		   class_name[vcc->qos.rxtp.traffic_class],
		   vcc->qos.txtp.min_pcr,
		   class_name[vcc->qos.txtp.traffic_class]);
	if (test_bit(ATM_VF_IS_CLIP, &vcc->flags)) {
		struct clip_vcc *clip_vcc = CLIP_VCC(vcc);
		struct net_device *dev;

		dev = clip_vcc->entry ? clip_vcc->entry->neigh->dev : NULL;
		seq_printf(seq, "CLIP, Itf:%s, Encap:",
		    dev ? dev->name : "none?");
		seq_printf(seq, "%s", clip_vcc->encap ? "LLC/SNAP" : "None");
	}
	seq_putc(seq, '\n');
}

static const char *vcc_state(struct atm_vcc *vcc)
{
	static const char *const map[] = { ATM_VS2TXT_MAP };

	return map[ATM_VF2VS(vcc->flags)];
}

static void vcc_info(struct seq_file *seq, struct atm_vcc *vcc)
{
	struct sock *sk = sk_atm(vcc);

	seq_printf(seq, "%pK ", vcc);
	if (!vcc->dev)
		seq_printf(seq, "Unassigned    ");
	else
		seq_printf(seq, "%3d %3d %5d ", vcc->dev->number, vcc->vpi,
			vcc->vci);
	switch (sk->sk_family) {
	case AF_ATMPVC:
		seq_printf(seq, "PVC");
		break;
	case AF_ATMSVC:
		seq_printf(seq, "SVC");
		break;
	default:
		seq_printf(seq, "%3d", sk->sk_family);
	}
	seq_printf(seq, " %04lx  %5d %7d/%7d %7d/%7d [%d]\n",
		   vcc->flags, sk->sk_err,
		   sk_wmem_alloc_get(sk), sk->sk_sndbuf,
		   sk_rmem_alloc_get(sk), sk->sk_rcvbuf,
		   refcount_read(&sk->sk_refcnt));
}

static void svc_info(struct seq_file *seq, struct atm_vcc *vcc)
{
	if (!vcc->dev)
		seq_printf(seq, sizeof(void *) == 4 ?
			   "N/A@%pK%10s" : "N/A@%pK%2s", vcc, "");
	else
		seq_printf(seq, "%3d %3d %5d         ",
			   vcc->dev->number, vcc->vpi, vcc->vci);
	seq_printf(seq, "%-10s ", vcc_state(vcc));
	seq_printf(seq, "%s%s", vcc->remote.sas_addr.pub,
	    *vcc->remote.sas_addr.pub && *vcc->remote.sas_addr.prv ? "+" : "");
	if (*vcc->remote.sas_addr.prv) {
		int i;

		for (i = 0; i < ATM_ESA_LEN; i++)
			seq_printf(seq, "%02x", vcc->remote.sas_addr.prv[i]);
	}
	seq_putc(seq, '\n');
}

static int atm_dev_seq_show(struct seq_file *seq, void *v)
{
	static char atm_dev_banner[] =
		"Itf Type    ESI/\"MAC\"addr "
		"AAL(TX,err,RX,err,drop) ...               [refcnt]\n";

	if (v == &atm_devs)
		seq_puts(seq, atm_dev_banner);
	else {
		struct atm_dev *dev = list_entry(v, struct atm_dev, dev_list);

		atm_dev_info(seq, dev);
	}
	return 0;
}

static const struct seq_operations atm_dev_seq_ops = {
	.start	= atm_dev_seq_start,
	.next	= atm_dev_seq_next,
	.stop	= atm_dev_seq_stop,
	.show	= atm_dev_seq_show,
};

static int pvc_seq_show(struct seq_file *seq, void *v)
{
	static char atm_pvc_banner[] =
		"Itf VPI VCI   AAL RX(PCR,Class) TX(PCR,Class)\n";

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, atm_pvc_banner);
	else {
		struct vcc_state *state = seq->private;
		struct atm_vcc *vcc = atm_sk(state->sk);

		pvc_info(seq, vcc);
	}
	return 0;
}

static const struct seq_operations pvc_seq_ops = {
	.start	= vcc_seq_start,
	.next	= vcc_seq_next,
	.stop	= vcc_seq_stop,
	.show	= pvc_seq_show,
};

static int vcc_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN) {
		seq_printf(seq, sizeof(void *) == 4 ? "%-8s%s" : "%-16s%s",
			"Address ", "Itf VPI VCI   Fam Flags Reply "
			"Send buffer     Recv buffer      [refcnt]\n");
	} else {
		struct vcc_state *state = seq->private;
		struct atm_vcc *vcc = atm_sk(state->sk);

		vcc_info(seq, vcc);
	}
	return 0;
}

static const struct seq_operations vcc_seq_ops = {
	.start	= vcc_seq_start,
	.next	= vcc_seq_next,
	.stop	= vcc_seq_stop,
	.show	= vcc_seq_show,
};

static int svc_seq_show(struct seq_file *seq, void *v)
{
	static const char atm_svc_banner[] =
		"Itf VPI VCI           State      Remote\n";

	if (v == SEQ_START_TOKEN)
		seq_puts(seq, atm_svc_banner);
	else {
		struct vcc_state *state = seq->private;
		struct atm_vcc *vcc = atm_sk(state->sk);

		svc_info(seq, vcc);
	}
	return 0;
}

static const struct seq_operations svc_seq_ops = {
	.start	= vcc_seq_start,
	.next	= vcc_seq_next,
	.stop	= vcc_seq_stop,
	.show	= svc_seq_show,
};

static ssize_t proc_dev_atm_read(struct file *file, char __user *buf,
				 size_t count, loff_t *pos)
{
	struct atm_dev *dev;
	unsigned long page;
	int length;

	if (count == 0)
		return 0;
	page = get_zeroed_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	dev = PDE_DATA(file_inode(file));
	if (!dev->ops->proc_read)
		length = -EINVAL;
	else {
		length = dev->ops->proc_read(dev, pos, (char *)page);
		if (length > count)
			length = -EINVAL;
	}
	if (length >= 0) {
		if (copy_to_user(buf, (char *)page, length))
			length = -EFAULT;
		(*pos)++;
	}
	free_page(page);
	return length;
}

struct proc_dir_entry *atm_proc_root;
EXPORT_SYMBOL(atm_proc_root);


int atm_proc_dev_register(struct atm_dev *dev)
{
	int error;

	/* No proc info */
	if (!dev->ops->proc_read)
		return 0;

	error = -ENOMEM;
	dev->proc_name = kasprintf(GFP_KERNEL, "%s:%d", dev->type, dev->number);
	if (!dev->proc_name)
		goto err_out;

	dev->proc_entry = proc_create_data(dev->proc_name, 0, atm_proc_root,
					   &proc_atm_dev_ops, dev);
	if (!dev->proc_entry)
		goto err_free_name;
	return 0;

err_free_name:
	kfree(dev->proc_name);
err_out:
	return error;
}

void atm_proc_dev_deregister(struct atm_dev *dev)
{
	if (!dev->ops->proc_read)
		return;

	remove_proc_entry(dev->proc_name, atm_proc_root);
	kfree(dev->proc_name);
}

int __init atm_proc_init(void)
{
	atm_proc_root = proc_net_mkdir(&init_net, "atm", init_net.proc_net);
	if (!atm_proc_root)
		return -ENOMEM;
	proc_create_seq("devices", 0444, atm_proc_root, &atm_dev_seq_ops);
	proc_create_seq_private("pvc", 0444, atm_proc_root, &pvc_seq_ops,
			sizeof(struct vcc_state), (void *)(uintptr_t)PF_ATMPVC);
	proc_create_seq_private("svc", 0444, atm_proc_root, &svc_seq_ops,
			sizeof(struct vcc_state), (void *)(uintptr_t)PF_ATMSVC);
	proc_create_seq_private("vc", 0444, atm_proc_root, &vcc_seq_ops,
			sizeof(struct vcc_state), NULL);
	return 0;
}

void atm_proc_exit(void)
{
	remove_proc_subtree("atm", init_net.proc_net);
}
