// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO-KVM bridge pseudo device
 *
 * Copyright (C) 2013 Red Hat, Inc.  All rights reserved.
 *     Author: Alex Williamson <alex.williamson@redhat.com>
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
	struct file *file;
#ifdef CONFIG_SPAPR_TCE_IOMMU
	struct iommu_group *iommu_group;
#endif
};

struct kvm_vfio {
	struct list_head group_list;
	struct mutex lock;
	bool noncoherent;
};

static void kvm_vfio_file_set_kvm(struct file *file, struct kvm *kvm)
{
	void (*fn)(struct file *file, struct kvm *kvm);

	fn = symbol_get(vfio_file_set_kvm);
	if (!fn)
		return;

	fn(file, kvm);

	symbol_put(vfio_file_set_kvm);
}

static bool kvm_vfio_file_enforced_coherent(struct file *file)
{
	bool (*fn)(struct file *file);
	bool ret;

	fn = symbol_get(vfio_file_enforced_coherent);
	if (!fn)
		return false;

	ret = fn(file);

	symbol_put(vfio_file_enforced_coherent);

	return ret;
}

static bool kvm_vfio_file_is_group(struct file *file)
{
	bool (*fn)(struct file *file);
	bool ret;

	fn = symbol_get(vfio_file_is_group);
	if (!fn)
		return false;

	ret = fn(file);

	symbol_put(vfio_file_is_group);

	return ret;
}

#ifdef CONFIG_SPAPR_TCE_IOMMU
static struct iommu_group *kvm_vfio_file_iommu_group(struct file *file)
{
	struct iommu_group *(*fn)(struct file *file);
	struct iommu_group *ret;

	fn = symbol_get(vfio_file_iommu_group);
	if (!fn)
		return NULL;

	ret = fn(file);

	symbol_put(vfio_file_iommu_group);

	return ret;
}

static void kvm_spapr_tce_release_vfio_group(struct kvm *kvm,
					     struct kvm_vfio_group *kvg)
{
	if (WARN_ON_ONCE(!kvg->iommu_group))
		return;

	kvm_spapr_tce_release_iommu_group(kvm, kvg->iommu_group);
	iommu_group_put(kvg->iommu_group);
	kvg->iommu_group = NULL;
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
		if (!kvm_vfio_file_enforced_coherent(kvg->file)) {
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

static int kvm_vfio_group_add(struct kvm_device *dev, unsigned int fd)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_group *kvg;
	struct file *filp;
	int ret;

	filp = fget(fd);
	if (!filp)
		return -EBADF;

	/* Ensure the FD is a vfio group FD.*/
	if (!kvm_vfio_file_is_group(filp)) {
		ret = -EINVAL;
		goto err_fput;
	}

	mutex_lock(&kv->lock);

	list_for_each_entry(kvg, &kv->group_list, node) {
		if (kvg->file == filp) {
			ret = -EEXIST;
			goto err_unlock;
		}
	}

	kvg = kzalloc(sizeof(*kvg), GFP_KERNEL_ACCOUNT);
	if (!kvg) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	kvg->file = filp;
	list_add_tail(&kvg->node, &kv->group_list);

	kvm_arch_start_assignment(dev->kvm);

	mutex_unlock(&kv->lock);

	kvm_vfio_file_set_kvm(kvg->file, dev->kvm);
	kvm_vfio_update_coherency(dev);

	return 0;
err_unlock:
	mutex_unlock(&kv->lock);
err_fput:
	fput(filp);
	return ret;
}

static int kvm_vfio_group_del(struct kvm_device *dev, unsigned int fd)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_group *kvg;
	struct fd f;
	int ret;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	ret = -ENOENT;

	mutex_lock(&kv->lock);

	list_for_each_entry(kvg, &kv->group_list, node) {
		if (kvg->file != f.file)
			continue;

		list_del(&kvg->node);
		kvm_arch_end_assignment(dev->kvm);
#ifdef CONFIG_SPAPR_TCE_IOMMU
		kvm_spapr_tce_release_vfio_group(dev->kvm, kvg);
#endif
		kvm_vfio_file_set_kvm(kvg->file, NULL);
		fput(kvg->file);
		kfree(kvg);
		ret = 0;
		break;
	}

	mutex_unlock(&kv->lock);

	fdput(f);

	kvm_vfio_update_coherency(dev);

	return ret;
}

#ifdef CONFIG_SPAPR_TCE_IOMMU
static int kvm_vfio_group_set_spapr_tce(struct kvm_device *dev,
					void __user *arg)
{
	struct kvm_vfio_spapr_tce param;
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_group *kvg;
	struct fd f;
	int ret;

	if (copy_from_user(&param, arg, sizeof(struct kvm_vfio_spapr_tce)))
		return -EFAULT;

	f = fdget(param.groupfd);
	if (!f.file)
		return -EBADF;

	ret = -ENOENT;

	mutex_lock(&kv->lock);

	list_for_each_entry(kvg, &kv->group_list, node) {
		if (kvg->file != f.file)
			continue;

		if (!kvg->iommu_group) {
			kvg->iommu_group = kvm_vfio_file_iommu_group(kvg->file);
			if (WARN_ON_ONCE(!kvg->iommu_group)) {
				ret = -EIO;
				goto err_fdput;
			}
		}

		ret = kvm_spapr_tce_attach_iommu_group(dev->kvm, param.tablefd,
						       kvg->iommu_group);
		break;
	}

err_fdput:
	mutex_unlock(&kv->lock);
	fdput(f);
	return ret;
}
#endif

static int kvm_vfio_set_group(struct kvm_device *dev, long attr,
			      void __user *arg)
{
	int32_t __user *argp = arg;
	int32_t fd;

	switch (attr) {
	case KVM_DEV_VFIO_GROUP_ADD:
		if (get_user(fd, argp))
			return -EFAULT;
		return kvm_vfio_group_add(dev, fd);

	case KVM_DEV_VFIO_GROUP_DEL:
		if (get_user(fd, argp))
			return -EFAULT;
		return kvm_vfio_group_del(dev, fd);

#ifdef CONFIG_SPAPR_TCE_IOMMU
	case KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE:
		return kvm_vfio_group_set_spapr_tce(dev, arg);
#endif
	}

	return -ENXIO;
}

static int kvm_vfio_set_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_VFIO_GROUP:
		return kvm_vfio_set_group(dev, attr->attr,
					  u64_to_user_ptr(attr->addr));
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
		kvm_spapr_tce_release_vfio_group(dev->kvm, kvg);
#endif
		kvm_vfio_file_set_kvm(kvg->file, NULL);
		fput(kvg->file);
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

	kv = kzalloc(sizeof(*kv), GFP_KERNEL_ACCOUNT);
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
