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

struct kvm_vfio_file {
	struct list_head node;
	struct file *file;
#ifdef CONFIG_SPAPR_TCE_IOMMU
	struct iommu_group *iommu_group;
#endif
};

struct kvm_vfio {
	struct list_head file_list;
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

static bool kvm_vfio_file_is_valid(struct file *file)
{
	bool (*fn)(struct file *file);
	bool ret;

	fn = symbol_get(vfio_file_is_valid);
	if (!fn)
		return false;

	ret = fn(file);

	symbol_put(vfio_file_is_valid);

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
					     struct kvm_vfio_file *kvf)
{
	if (WARN_ON_ONCE(!kvf->iommu_group))
		return;

	kvm_spapr_tce_release_iommu_group(kvm, kvf->iommu_group);
	iommu_group_put(kvf->iommu_group);
	kvf->iommu_group = NULL;
}
#endif

/*
 * Groups/devices can use the same or different IOMMU domains. If the same
 * then adding a new group/device may change the coherency of groups/devices
 * we've previously been told about. We don't want to care about any of
 * that so we retest each group/device and bail as soon as we find one that's
 * noncoherent.  This means we only ever [un]register_noncoherent_dma once
 * for the whole device.
 */
static void kvm_vfio_update_coherency(struct kvm_device *dev)
{
	struct kvm_vfio *kv = dev->private;
	bool noncoherent = false;
	struct kvm_vfio_file *kvf;

	list_for_each_entry(kvf, &kv->file_list, node) {
		if (!kvm_vfio_file_enforced_coherent(kvf->file)) {
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
}

static int kvm_vfio_file_add(struct kvm_device *dev, unsigned int fd)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_file *kvf;
	struct file *filp;
	int ret = 0;

	filp = fget(fd);
	if (!filp)
		return -EBADF;

	/* Ensure the FD is a vfio FD. */
	if (!kvm_vfio_file_is_valid(filp)) {
		ret = -EINVAL;
		goto out_fput;
	}

	mutex_lock(&kv->lock);

	list_for_each_entry(kvf, &kv->file_list, node) {
		if (kvf->file == filp) {
			ret = -EEXIST;
			goto out_unlock;
		}
	}

	kvf = kzalloc(sizeof(*kvf), GFP_KERNEL_ACCOUNT);
	if (!kvf) {
		ret = -ENOMEM;
		goto out_unlock;
	}

	kvf->file = get_file(filp);
	list_add_tail(&kvf->node, &kv->file_list);

	kvm_vfio_file_set_kvm(kvf->file, dev->kvm);
	kvm_vfio_update_coherency(dev);

out_unlock:
	mutex_unlock(&kv->lock);
out_fput:
	fput(filp);
	return ret;
}

static int kvm_vfio_file_del(struct kvm_device *dev, unsigned int fd)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_file *kvf;
	CLASS(fd, f)(fd);
	int ret;

	if (fd_empty(f))
		return -EBADF;

	ret = -ENOENT;

	mutex_lock(&kv->lock);

	list_for_each_entry(kvf, &kv->file_list, node) {
		if (kvf->file != fd_file(f))
			continue;

		list_del(&kvf->node);
#ifdef CONFIG_SPAPR_TCE_IOMMU
		kvm_spapr_tce_release_vfio_group(dev->kvm, kvf);
#endif
		kvm_vfio_file_set_kvm(kvf->file, NULL);
		fput(kvf->file);
		kfree(kvf);
		ret = 0;
		break;
	}

	kvm_vfio_update_coherency(dev);

	mutex_unlock(&kv->lock);
	return ret;
}

#ifdef CONFIG_SPAPR_TCE_IOMMU
static int kvm_vfio_file_set_spapr_tce(struct kvm_device *dev,
				       void __user *arg)
{
	struct kvm_vfio_spapr_tce param;
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_file *kvf;
	int ret;

	if (copy_from_user(&param, arg, sizeof(struct kvm_vfio_spapr_tce)))
		return -EFAULT;

	CLASS(fd, f)(param.groupfd);
	if (fd_empty(f))
		return -EBADF;

	ret = -ENOENT;

	mutex_lock(&kv->lock);

	list_for_each_entry(kvf, &kv->file_list, node) {
		if (kvf->file != fd_file(f))
			continue;

		if (!kvf->iommu_group) {
			kvf->iommu_group = kvm_vfio_file_iommu_group(kvf->file);
			if (WARN_ON_ONCE(!kvf->iommu_group)) {
				ret = -EIO;
				goto err_fdput;
			}
		}

		ret = kvm_spapr_tce_attach_iommu_group(dev->kvm, param.tablefd,
						       kvf->iommu_group);
		break;
	}

err_fdput:
	mutex_unlock(&kv->lock);
	return ret;
}
#endif

static int kvm_vfio_set_file(struct kvm_device *dev, long attr,
			     void __user *arg)
{
	int32_t __user *argp = arg;
	int32_t fd;

	switch (attr) {
	case KVM_DEV_VFIO_FILE_ADD:
		if (get_user(fd, argp))
			return -EFAULT;
		return kvm_vfio_file_add(dev, fd);

	case KVM_DEV_VFIO_FILE_DEL:
		if (get_user(fd, argp))
			return -EFAULT;
		return kvm_vfio_file_del(dev, fd);

#ifdef CONFIG_SPAPR_TCE_IOMMU
	case KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE:
		return kvm_vfio_file_set_spapr_tce(dev, arg);
#endif
	}

	return -ENXIO;
}

static int kvm_vfio_set_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_VFIO_FILE:
		return kvm_vfio_set_file(dev, attr->attr,
					 u64_to_user_ptr(attr->addr));
	}

	return -ENXIO;
}

static int kvm_vfio_has_attr(struct kvm_device *dev,
			     struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_VFIO_FILE:
		switch (attr->attr) {
		case KVM_DEV_VFIO_FILE_ADD:
		case KVM_DEV_VFIO_FILE_DEL:
#ifdef CONFIG_SPAPR_TCE_IOMMU
		case KVM_DEV_VFIO_GROUP_SET_SPAPR_TCE:
#endif
			return 0;
		}

		break;
	}

	return -ENXIO;
}

static void kvm_vfio_release(struct kvm_device *dev)
{
	struct kvm_vfio *kv = dev->private;
	struct kvm_vfio_file *kvf, *tmp;

	list_for_each_entry_safe(kvf, tmp, &kv->file_list, node) {
#ifdef CONFIG_SPAPR_TCE_IOMMU
		kvm_spapr_tce_release_vfio_group(dev->kvm, kvf);
#endif
		kvm_vfio_file_set_kvm(kvf->file, NULL);
		fput(kvf->file);
		list_del(&kvf->node);
		kfree(kvf);
	}

	kvm_vfio_update_coherency(dev);

	kfree(kv);
	kfree(dev); /* alloc by kvm_ioctl_create_device, free by .release */
}

static int kvm_vfio_create(struct kvm_device *dev, u32 type);

static const struct kvm_device_ops kvm_vfio_ops = {
	.name = "kvm-vfio",
	.create = kvm_vfio_create,
	.release = kvm_vfio_release,
	.set_attr = kvm_vfio_set_attr,
	.has_attr = kvm_vfio_has_attr,
};

static int kvm_vfio_create(struct kvm_device *dev, u32 type)
{
	struct kvm_device *tmp;
	struct kvm_vfio *kv;

	lockdep_assert_held(&dev->kvm->lock);

	/* Only one VFIO "device" per VM */
	list_for_each_entry(tmp, &dev->kvm->devices, vm_node)
		if (tmp->ops == &kvm_vfio_ops)
			return -EBUSY;

	kv = kzalloc(sizeof(*kv), GFP_KERNEL_ACCOUNT);
	if (!kv)
		return -ENOMEM;

	INIT_LIST_HEAD(&kv->file_list);
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
