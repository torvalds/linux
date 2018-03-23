/*
 * Copyright (C) 2016 Linaro
 * Author: Christoffer Dall <christoffer.dall@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/cpu.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>
#include <kvm/arm_vgic.h>
#include <asm/kvm_mmu.h>
#include "vgic.h"

/*
 * Structure to control looping through the entire vgic state.  We start at
 * zero for each field and move upwards.  So, if dist_id is 0 we print the
 * distributor info.  When dist_id is 1, we have already printed it and move
 * on.
 *
 * When vcpu_id < nr_cpus we print the vcpu info until vcpu_id == nr_cpus and
 * so on.
 */
struct vgic_state_iter {
	int nr_cpus;
	int nr_spis;
	int nr_lpis;
	int dist_id;
	int vcpu_id;
	int intid;
	int lpi_idx;
	u32 *lpi_array;
};

static void iter_next(struct vgic_state_iter *iter)
{
	if (iter->dist_id == 0) {
		iter->dist_id++;
		return;
	}

	iter->intid++;
	if (iter->intid == VGIC_NR_PRIVATE_IRQS &&
	    ++iter->vcpu_id < iter->nr_cpus)
		iter->intid = 0;

	if (iter->intid >= (iter->nr_spis + VGIC_NR_PRIVATE_IRQS)) {
		if (iter->lpi_idx < iter->nr_lpis)
			iter->intid = iter->lpi_array[iter->lpi_idx];
		iter->lpi_idx++;
	}
}

static void iter_init(struct kvm *kvm, struct vgic_state_iter *iter,
		      loff_t pos)
{
	int nr_cpus = atomic_read(&kvm->online_vcpus);

	memset(iter, 0, sizeof(*iter));

	iter->nr_cpus = nr_cpus;
	iter->nr_spis = kvm->arch.vgic.nr_spis;
	if (kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3) {
		iter->nr_lpis = vgic_copy_lpi_list(kvm, NULL, &iter->lpi_array);
		if (iter->nr_lpis < 0)
			iter->nr_lpis = 0;
	}

	/* Fast forward to the right position if needed */
	while (pos--)
		iter_next(iter);
}

static bool end_of_vgic(struct vgic_state_iter *iter)
{
	return iter->dist_id > 0 &&
		iter->vcpu_id == iter->nr_cpus &&
		iter->intid >= (iter->nr_spis + VGIC_NR_PRIVATE_IRQS) &&
		iter->lpi_idx > iter->nr_lpis;
}

static void *vgic_debug_start(struct seq_file *s, loff_t *pos)
{
	struct kvm *kvm = (struct kvm *)s->private;
	struct vgic_state_iter *iter;

	mutex_lock(&kvm->lock);
	iter = kvm->arch.vgic.iter;
	if (iter) {
		iter = ERR_PTR(-EBUSY);
		goto out;
	}

	iter = kmalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter) {
		iter = ERR_PTR(-ENOMEM);
		goto out;
	}

	iter_init(kvm, iter, *pos);
	kvm->arch.vgic.iter = iter;

	if (end_of_vgic(iter))
		iter = NULL;
out:
	mutex_unlock(&kvm->lock);
	return iter;
}

static void *vgic_debug_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kvm *kvm = (struct kvm *)s->private;
	struct vgic_state_iter *iter = kvm->arch.vgic.iter;

	++*pos;
	iter_next(iter);
	if (end_of_vgic(iter))
		iter = NULL;
	return iter;
}

static void vgic_debug_stop(struct seq_file *s, void *v)
{
	struct kvm *kvm = (struct kvm *)s->private;
	struct vgic_state_iter *iter;

	/*
	 * If the seq file wasn't properly opened, there's nothing to clearn
	 * up.
	 */
	if (IS_ERR(v))
		return;

	mutex_lock(&kvm->lock);
	iter = kvm->arch.vgic.iter;
	kfree(iter->lpi_array);
	kfree(iter);
	kvm->arch.vgic.iter = NULL;
	mutex_unlock(&kvm->lock);
}

static void print_dist_state(struct seq_file *s, struct vgic_dist *dist)
{
	bool v3 = dist->vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3;

	seq_printf(s, "Distributor\n");
	seq_printf(s, "===========\n");
	seq_printf(s, "vgic_model:\t%s\n", v3 ? "GICv3" : "GICv2");
	seq_printf(s, "nr_spis:\t%d\n", dist->nr_spis);
	if (v3)
		seq_printf(s, "nr_lpis:\t%d\n", dist->lpi_list_count);
	seq_printf(s, "enabled:\t%d\n", dist->enabled);
	seq_printf(s, "\n");

	seq_printf(s, "P=pending_latch, L=line_level, A=active\n");
	seq_printf(s, "E=enabled, H=hw, C=config (level=1, edge=0)\n");
}

static void print_header(struct seq_file *s, struct vgic_irq *irq,
			 struct kvm_vcpu *vcpu)
{
	int id = 0;
	char *hdr = "SPI ";

	if (vcpu) {
		hdr = "VCPU";
		id = vcpu->vcpu_id;
	}

	seq_printf(s, "\n");
	seq_printf(s, "%s%2d TYP   ID TGT_ID PLAEHC     HWID   TARGET SRC PRI VCPU_ID\n", hdr, id);
	seq_printf(s, "---------------------------------------------------------------\n");
}

static void print_irq_state(struct seq_file *s, struct vgic_irq *irq,
			    struct kvm_vcpu *vcpu)
{
	char *type;
	if (irq->intid < VGIC_NR_SGIS)
		type = "SGI";
	else if (irq->intid < VGIC_NR_PRIVATE_IRQS)
		type = "PPI";
	else if (irq->intid < VGIC_MAX_SPI)
		type = "SPI";
	else
		type = "LPI";

	if (irq->intid ==0 || irq->intid == VGIC_NR_PRIVATE_IRQS)
		print_header(s, irq, vcpu);

	seq_printf(s, "       %s %4d "
		      "    %2d "
		      "%d%d%d%d%d%d "
		      "%8d "
		      "%8x "
		      " %2x "
		      "%3d "
		      "     %2d "
		      "\n",
			type, irq->intid,
			(irq->target_vcpu) ? irq->target_vcpu->vcpu_id : -1,
			irq->pending_latch,
			irq->line_level,
			irq->active,
			irq->enabled,
			irq->hw,
			irq->config == VGIC_CONFIG_LEVEL,
			irq->hwintid,
			irq->mpidr,
			irq->source,
			irq->priority,
			(irq->vcpu) ? irq->vcpu->vcpu_id : -1);
}

static int vgic_debug_show(struct seq_file *s, void *v)
{
	struct kvm *kvm = (struct kvm *)s->private;
	struct vgic_state_iter *iter = (struct vgic_state_iter *)v;
	struct vgic_irq *irq;
	struct kvm_vcpu *vcpu = NULL;
	unsigned long flags;

	if (iter->dist_id == 0) {
		print_dist_state(s, &kvm->arch.vgic);
		return 0;
	}

	if (!kvm->arch.vgic.initialized)
		return 0;

	if (iter->vcpu_id < iter->nr_cpus)
		vcpu = kvm_get_vcpu(kvm, iter->vcpu_id);

	irq = vgic_get_irq(kvm, vcpu, iter->intid);
	if (!irq) {
		seq_printf(s, "       LPI %4d freed\n", iter->intid);
		return 0;
	}

	spin_lock_irqsave(&irq->irq_lock, flags);
	print_irq_state(s, irq, vcpu);
	spin_unlock_irqrestore(&irq->irq_lock, flags);

	vgic_put_irq(kvm, irq);
	return 0;
}

static const struct seq_operations vgic_debug_seq_ops = {
	.start = vgic_debug_start,
	.next  = vgic_debug_next,
	.stop  = vgic_debug_stop,
	.show  = vgic_debug_show
};

static int debug_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = seq_open(file, &vgic_debug_seq_ops);
	if (!ret) {
		struct seq_file *seq;
		/* seq_open will have modified file->private_data */
		seq = file->private_data;
		seq->private = inode->i_private;
	}

	return ret;
};

static const struct file_operations vgic_debug_fops = {
	.owner   = THIS_MODULE,
	.open    = debug_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

void vgic_debug_init(struct kvm *kvm)
{
	debugfs_create_file("vgic-state", 0444, kvm->debugfs_dentry, kvm,
			    &vgic_debug_fops);
}

void vgic_debug_destroy(struct kvm *kvm)
{
}
