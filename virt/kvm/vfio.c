/*
 * VFIO-KVM bridge pseudo device
 *
 * Copyright (C) 2013 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/file.h>
#include <linux/kvm_host.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include "vfio.h"

#ifdef CONFIG_SPAPR_TCE_IOMMU
#include <asm/kvm_ppc.h>
#endif

struct kvm_vfio_group {
	struct list_head node;
	struct vfio_group *vfio_group;
};

struct kvm_vfio {
	struct list_head group_list;
	struct mutex lock;
	bool noncoherent;
};

static struct vfio_group *kvm_vfio_group_get_external_user(struct file *filep)
{
	struct vfio_group *vfio_group;
	struct vfio_group *(*fn)(struct file *);

	fn = symbol_get(vfio_group_get_external_user);
	if (!fn)
		return ERR_PTR(-EINVAL);

	vfio_group = fn(filep);

	symbol_put(vfio_group_get_external_user);

	return vfio_group;
}

static bool kvm_vfio_external_group_match_file(struct vfio_group *group,
					       struct file *filep)
{
	bool ret, (*fn)(struct vfio_group *, struct file *);

	fn = symbol_get(vfio_external_group_match_file);
	if (!fn)
		return false;

	ret = fn(group, filep);

	symbol_put(vfio_external_group_match_file);

	return ret;
}

static void kvm_vfio_group_put_external_user(struct vfio_group *vfio_group)
{
	void (*fn)(struct vfio_group *);

	fn = symbol_get(vfio_group_put_external_user);
	if (!fn)
		return;

	fn(vfio_group);

	symbol_put(vfio_group_put_external_user);
}

static void kvm_vfio_group_set_kvm(struct vfio_group *group, struct kvm *kvm)
{
	void (*fn)(struct vfio_group *, struct kvm *);

	fn = symbol_get(vfio_group_set_kvm);
	if (!fn)
		return;

	fn(group, kvm);

	symbol_put(vfio_group_set_kvm);
}

static bool kvm_vfio_group_is_coherent(struct vfio_group *vfio_group)
{
	long (*fn)(struct vfio_group *, unsigned long);
	long ret;

	fn = symbol_get(vfio_external_check_extension);
	if (!fn)
		return false;

	ret = fn(vfio_group, VFIO_DMA_CC_IOMMU);

	symbol_put(vfio_external_check_extension);

	return ret > 0;
}

#ifdef CONFIG_SPAPR_TCE_IOMMU
static int kvm_vfio_external_user_iommu_id(struct vfio_group *vfio_group)
{
	int (*fn)(struct vfio_group *);
	int ret = -EINVAL;

	fn = symbol_get(vfio_external_user_iommu_id);
	if (!fn)
		return ret;

	ret = fn(vfio_group);

	symbol_put(vfio_external_user_iommu_id);

	return ret;
}

static struct iommu_group *kvm_vfio_group_get_iommu_group(
		struct vfio_group *group)
{
	int group_id = kvm_vfio_external_user_iommu_id(group);

	if (group_id < 0)
		return NULL;

	return iommu_group_get_by_id(group_id);
}

static void kvm_spapr_tce_release_vfio_group(struct kvm *kvm,
		struct vfio_group *vfio_group)
{
	struct iommu_group *grp = kvm_vfio_group_get_iommu_group(vfio_group);

	if (WARN_ON_ONCE(!grp))
		return;

	kvm_spapr_tce_release_iommu_group(kvm, grp);
	iommu_group_put(grp);
}
#endif

/*
 * Groups can use the same or different IOMMU domains.  If the same then
 * adding a new group may change the coherency of groups we've previously
 * been told about.  We don't want to care about any of that so we retest
 * each group and bail as soon as we find one that's noncoherent.  This
 * means we only ever [un]register_noncoherent_dma once for the whole device.
 */
static void kvm_vfio_update_coherency(struct kvm_device *dev)
{
	struct kvm_vfio *kv = dev->private;
	bool noncoherent = false;
	struct kvm_vfio_group *kvg;

	mutex_lock(&kv->lock);

	list_for_each_entry(kvg, &kv->group_list, node) {
		if (!kvm_vfio_group_is_coherent(kvg->vfio_group)) {
			noncoherent = true;
			break;
		}
	}

	if (noncoherent != kv->noncoherent) {
		kv->noncoherent = noncoherent;

		if (kv->noncoherent)
			kvm_arch_register_noncoherent_dma(dev->kvm);
		else
			kvm_arch_unregister_noncoherent_dma(dev->kvm);
	}

	mutex_unlock(&kv->lock);
}

static int kvm_vfio_set_group(struct kvm_device *dev, long attr, u64 arg)
{
	struct kvm_vfio *kv = dev->private;
	struct vfio_group *vfio_group;
	struct kvm_vfio_group *kvg;
	int32_t __user *argp = (int32_t __user *)(unsigned long)arg;
	struct fd f;
	int32_t fd;
	int ret;

	switch (attr) {
	case KVM_DEV_VFIO_GROUP_ADD:
		if (get_user(fd, argp))
			return -EFAULT;

		f = fdget(fd);
		if (!f.file)
			return -EBADF;

		vfio_group = kvm_vfio_group_get_external_user(f.file);
		fdput(f);

		if (IS_ERR(vfio_group))
			return PTR_ERR(vfio_group);

		mutex_lock(&kv->lock);

		list_for_each_entry(kvg, &kv->group_list, node) {
			if (kvg->vfio_group == vfio_group) {
				mutex_unlock(&kv->lock);
				kvm_vfio_group_put_external_user(vfio_group);
				return -EEXIST;
			}
		}

		kvg = kzalloc(sizeof(*kvg), GFP_KERNEL);
		if (!kvg) {
			mutex_unlock(&kv->lock);
			kvm_vfio_group_put_external_user(vfio_group);
			return -ENOMEM;
		}

		list_add_tail(&kvg->node, &kv->group_list);
		kvg->vfio_group = vfio_group;

		kvm_arch_start_assignment(dev->kvm);

		mutex_unlock(&kv->lock);

		kvm_vfio_group_set_kvm(vfio_group, dev->kvm);

		kvm_vfio_update_coherency(dev);

		return 0;

	case KVM_DEV_VFIO_GROUP_DEL:
		if (get_user(fd, argp))
			return -EFAULT;

		f = fdget(fd);
		if (!f.file)
			return -EBADF;

		ret = -ENOENT;

		mutex_lock(&kv->lock);

		list_for_each_entry(kvg, &kv->group_list, node) {
			if (!kvm_vfio_external_group_match_file(kvg->vfio_group,
								f.file))
				continue;

			list_del(&kvg->node);
			kvm_arch_end_assignment(dev->kvm);
#ifdef CONFIG_SPAPR_TCE_IOMMU
			kvm_spapr_tce_release_vfio_group(dev->kvm,
							 kvg->vfio_group);
#endif
			kvm_vfio_group_set_kvm(kvg->vfio_group, NULL);
			kvm_vfio_group_put_external_user(kvg->vfio_group);
			kfree(kvg);
			ret = 0;
			break;
		}

		mutex_unlock(&kv->lock);

		fdput(f);

		kvm_vfio_update_coherency(dev);

		return ret;

#ifdef CONFIG_SPAPR_TCE_IOMMU
	case KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE: {
		struct kvm_vfio_spapr_tce param;
		struct kvm_vfio *kv = dev->private;
		struct vfio_group *vfio_group;
		struct kvm_vfio_group *kvg;
		struct fd f;
		struct iommu_group *grp;

		if (copy_from_user(&param, (void __user *)arg,
				sizeof(struct kvm_vfio_spapr_tce)))
			return -EFAULT;

		f = fdget(param.groupfd);
		if (!f.file)
			return -EBADF;

		vfio_group = kvm_vfio_group_get_external_user(f.file);
		fdput(f);

		if (IS_ERR(vfio_group))
			return PTR_ERR(vfio_group);

		grp = kvm_vfio_group_get_iommu_group(vfio_group);
		if (WARN_ON_ONCE(!grp)) {
			kvm_vfio_group_put_external_user(vfio_group);
			return -EIO;
		}

		ret = -ENOENT;

		mutex_lock(&kv->lock);

		list_for_each_entry(kvg, &kv->group_list, node) {
			if (kvg->vfio_group != vfio_group)
				continue;

			ret = kvm_spapr_tce_attach_iommu_group(dev->kvm,
					param.tablefd, grp);
			break;
		}

		mutex_unlock(&kv->lock);

		iommu_group_put(grp);
		kvm_vfio_group_put_external_user(vfio_group);

		return ret;
	}
#endif /* CONFIG_SPAPR_TCE_IOMMU */
	}

	return -ENXIO;
}

static int kvm_vfio_set_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_VFIO_GROUP:
		return kvm_vfio_set_group(dev, attr->attr, attr->addr);
	}

	return -ENXIO;
}

static int kvm_vfio_has_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_VFIO_GROUP:
		switch (attr->attr) {
		case KVM_DEV_VFIO_GROUP_ADD:
		case KVM_DEV_VFIO_GROUP_DEL:
#ifdef CONFIG_SPAPR_TCE_IOMMU
		case KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE:
#endif
			return 0;
		}

		break;
	}

	return -ENXIO;
}

static void kvm_vfio_destroy(struct kvm_device *dev)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_group *kvg, *tmp;

	list_for_each_entry_safe(kvg, tmp, &kv->group_list, node) {
#ifdef CONFIG_SPAPR_TCE_IOMMU
		kvm_spapr_tce_release_vfio_group(dev->kvm, kvg->vfio_group);
#endif
		kvm_vfio_group_set_kvm(kvg->vfio_group, NULL);
		kvm_vfio_group_put_external_user(kvg->vfio_group);
		list_del(&kvg->node);
		kfree(kvg);
		kvm_arch_end_assignment(dev->kvm);
	}

	kvm_vfio_update_coherency(dev);

	kfree(kv);
	kfree(dev); /* alloc by kvm_ioctl_create_device, free by .destroy */
}

static int kvm_vfio_create(struct kvm_device *dev, u32 type);

static struct kvm_device_ops kvm_vfio_ops = {
	.name = "kvm-vfio",
	.create = kvm_vfio_create,
	.destroy = kvm_vfio_destroy,
	.set_attr = kvm_vfio_set_attr,
	.has_attr = kvm_vfio_has_attr,
};

static int kvm_vfio_create(struct kvm_device *dev, u32 type)
{
	struct kvm_device *tmp;
	struct kvm_vfio *kv;

	/* Only one VFIO "device" per VM */
	list_for_each_entry(tmp, &dev->kvm->devices, vm_node)
		if (tmp->ops == &kvm_vfio_ops)
			return -EBUSY;

	kv = kzalloc(sizeof(*kv), GFP_KERNEL);
	if (!kv)
		return -ENOMEM;

	INIT_LIST_HEAD(&kv->group_list);
	mutex_init(&kv->lock);

	dev->private = kv;

	return 0;
}

int kvm_vfio_ops_init(void)
{
	return kvm_register_device_ops(&kvm_vfio_ops, KVM_DEV_TYPE_VFIO);
}

void kvm_vfio_ops_exit(void)
{
	kvm_unregister_device_ops(KVM_DEV_TYPE_VFIO);
}
