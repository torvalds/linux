// SPDX-License-Identifier: GPL-2.0
/*
 * Mediated virtual PCI display host device driver
 *
 * See mdpy-defs.h for device specs
 *
 *   (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 * based on mtty driver which is:
 *   Copyright (c) 2016, NVIDIA CORPORATION. All rights reserved.
 *	 Author: Neo Jia <cjia@nvidia.com>
 *		 Kirti Wankhede <kwankhede@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/vfio.h>
#include <linux/iommu.h>
#include <linux/sysfs.h>
#include <linux/mdev.h>
#include <linux/pci.h>
#include <drm/drm_fourcc.h>
#include "mdpy-defs.h"

#define MDPY_NAME		"mdpy"
#define MDPY_CLASS_NAME		"mdpy"

#define MDPY_CONFIG_SPACE_SIZE	0xff
#define MDPY_MEMORY_BAR_OFFSET	PAGE_SIZE
#define MDPY_DISPLAY_REGION	16

#define STORE_LE16(addr, val)	(*(u16 *)addr = val)
#define STORE_LE32(addr, val)	(*(u32 *)addr = val)


MODULE_LICENSE("GPL v2");

static int max_devices = 4;
module_param_named(count, max_devices, int, 0444);
MODULE_PARM_DESC(count, "number of " MDPY_NAME " devices");


#define MDPY_TYPE_1 "vga"
#define MDPY_TYPE_2 "xga"
#define MDPY_TYPE_3 "hd"

static const struct mdpy_type {
	const char *name;
	u32 format;
	u32 bytepp;
	u32 width;
	u32 height;
} mdpy_types[] = {
	{
		.name	= MDPY_CLASS_NAME "-" MDPY_TYPE_1,
		.format = DRM_FORMAT_XRGB8888,
		.bytepp = 4,
		.width	= 640,
		.height = 480,
	}, {
		.name	= MDPY_CLASS_NAME "-" MDPY_TYPE_2,
		.format = DRM_FORMAT_XRGB8888,
		.bytepp = 4,
		.width	= 1024,
		.height = 768,
	}, {
		.name	= MDPY_CLASS_NAME "-" MDPY_TYPE_3,
		.format = DRM_FORMAT_XRGB8888,
		.bytepp = 4,
		.width	= 1920,
		.height = 1080,
	},
};

static dev_t		mdpy_devt;
static struct class	*mdpy_class;
static struct cdev	mdpy_cdev;
static struct device	mdpy_dev;
static u32		mdpy_count;
static const struct vfio_device_ops mdpy_dev_ops;

/* State of each mdev device */
struct mdev_state {
	struct vfio_device vdev;
	u8 *vconfig;
	u32 bar_mask;
	struct mutex ops_lock;
	struct mdev_device *mdev;
	struct vfio_device_info dev_info;

	const struct mdpy_type *type;
	u32 memsize;
	void *memblk;
};

static void mdpy_create_config_space(struct mdev_state *mdev_state)
{
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_VENDOR_ID],
		   MDPY_PCI_VENDOR_ID);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_DEVICE_ID],
		   MDPY_PCI_DEVICE_ID);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_SUBSYSTEM_VENDOR_ID],
		   MDPY_PCI_SUBVENDOR_ID);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_SUBSYSTEM_ID],
		   MDPY_PCI_SUBDEVICE_ID);

	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_COMMAND],
		   PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_STATUS],
		   PCI_STATUS_CAP_LIST);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_CLASS_DEVICE],
		   PCI_CLASS_DISPLAY_OTHER);
	mdev_state->vconfig[PCI_CLASS_REVISION] =  0x01;

	STORE_LE32((u32 *) &mdev_state->vconfig[PCI_BASE_ADDRESS_0],
		   PCI_BASE_ADDRESS_SPACE_MEMORY |
		   PCI_BASE_ADDRESS_MEM_TYPE_32	 |
		   PCI_BASE_ADDRESS_MEM_PREFETCH);
	mdev_state->bar_mask = ~(mdev_state->memsize) + 1;

	/* vendor specific capability for the config registers */
	mdev_state->vconfig[PCI_CAPABILITY_LIST]       = MDPY_VENDORCAP_OFFSET;
	mdev_state->vconfig[MDPY_VENDORCAP_OFFSET + 0] = 0x09; /* vendor cap */
	mdev_state->vconfig[MDPY_VENDORCAP_OFFSET + 1] = 0x00; /* next ptr */
	mdev_state->vconfig[MDPY_VENDORCAP_OFFSET + 2] = MDPY_VENDORCAP_SIZE;
	STORE_LE32((u32 *) &mdev_state->vconfig[MDPY_FORMAT_OFFSET],
		   mdev_state->type->format);
	STORE_LE32((u32 *) &mdev_state->vconfig[MDPY_WIDTH_OFFSET],
		   mdev_state->type->width);
	STORE_LE32((u32 *) &mdev_state->vconfig[MDPY_HEIGHT_OFFSET],
		   mdev_state->type->height);
}

static void handle_pci_cfg_write(struct mdev_state *mdev_state, u16 offset,
				 char *buf, u32 count)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	u32 cfg_addr;

	switch (offset) {
	case PCI_BASE_ADDRESS_0:
		cfg_addr = *(u32 *)buf;

		if (cfg_addr == 0xffffffff) {
			cfg_addr = (cfg_addr & mdev_state->bar_mask);
		} else {
			cfg_addr &= PCI_BASE_ADDRESS_MEM_MASK;
			if (cfg_addr)
				dev_info(dev, "BAR0 @ 0x%x\n", cfg_addr);
		}

		cfg_addr |= (mdev_state->vconfig[offset] &
			     ~PCI_BASE_ADDRESS_MEM_MASK);
		STORE_LE32(&mdev_state->vconfig[offset], cfg_addr);
		break;
	}
}

static ssize_t mdev_access(struct mdev_state *mdev_state, char *buf,
			   size_t count, loff_t pos, bool is_write)
{
	int ret = 0;

	mutex_lock(&mdev_state->ops_lock);

	if (pos < MDPY_CONFIG_SPACE_SIZE) {
		if (is_write)
			handle_pci_cfg_write(mdev_state, pos, buf, count);
		else
			memcpy(buf, (mdev_state->vconfig + pos), count);

	} else if ((pos >= MDPY_MEMORY_BAR_OFFSET) &&
		   (pos + count <=
		    MDPY_MEMORY_BAR_OFFSET + mdev_state->memsize)) {
		pos -= MDPY_MEMORY_BAR_OFFSET;
		if (is_write)
			memcpy(mdev_state->memblk, buf, count);
		else
			memcpy(buf, mdev_state->memblk, count);

	} else {
		dev_info(mdev_state->vdev.dev,
			 "%s: %s @0x%llx (unhandled)\n", __func__,
			 is_write ? "WR" : "RD", pos);
		ret = -1;
		goto accessfailed;
	}

	ret = count;


accessfailed:
	mutex_unlock(&mdev_state->ops_lock);

	return ret;
}

static int mdpy_reset(struct mdev_state *mdev_state)
{
	u32 stride, i;

	/* initialize with gray gradient */
	stride = mdev_state->type->width * mdev_state->type->bytepp;
	for (i = 0; i < mdev_state->type->height; i++)
		memset(mdev_state->memblk + i * stride,
		       i * 255 / mdev_state->type->height,
		       stride);
	return 0;
}

static int mdpy_probe(struct mdev_device *mdev)
{
	const struct mdpy_type *type =
		&mdpy_types[mdev_get_type_group_id(mdev)];
	struct device *dev = mdev_dev(mdev);
	struct mdev_state *mdev_state;
	u32 fbsize;
	int ret;

	if (mdpy_count >= max_devices)
		return -ENOMEM;

	mdev_state = kzalloc(sizeof(struct mdev_state), GFP_KERNEL);
	if (mdev_state == NULL)
		return -ENOMEM;
	vfio_init_group_dev(&mdev_state->vdev, &mdev->dev, &mdpy_dev_ops);

	mdev_state->vconfig = kzalloc(MDPY_CONFIG_SPACE_SIZE, GFP_KERNEL);
	if (mdev_state->vconfig == NULL) {
		kfree(mdev_state);
		return -ENOMEM;
	}

	fbsize = roundup_pow_of_two(type->width * type->height * type->bytepp);

	mdev_state->memblk = vmalloc_user(fbsize);
	if (!mdev_state->memblk) {
		kfree(mdev_state->vconfig);
		kfree(mdev_state);
		return -ENOMEM;
	}
	dev_info(dev, "%s: %s (%dx%d)\n", __func__, type->name, type->width,
		 type->height);

	mutex_init(&mdev_state->ops_lock);
	mdev_state->mdev = mdev;
	mdev_state->type    = type;
	mdev_state->memsize = fbsize;
	mdpy_create_config_space(mdev_state);
	mdpy_reset(mdev_state);

	mdpy_count++;

	ret = vfio_register_group_dev(&mdev_state->vdev);
	if (ret) {
		kfree(mdev_state->vconfig);
		kfree(mdev_state);
		return ret;
	}
	dev_set_drvdata(&mdev->dev, mdev_state);
	return 0;
}

static void mdpy_remove(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state = dev_get_drvdata(&mdev->dev);

	dev_info(&mdev->dev, "%s\n", __func__);

	vfio_unregister_group_dev(&mdev_state->vdev);
	vfree(mdev_state->memblk);
	kfree(mdev_state->vconfig);
	kfree(mdev_state);

	mdpy_count--;
}

static ssize_t mdpy_read(struct vfio_device *vdev, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 2;
		} else {
			u8 val;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 1;
		}

		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;

read_err:
	return -EFAULT;
}

static ssize_t mdpy_write(struct vfio_device *vdev, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev_state, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 1;
		}
		count -= filled;
		done += filled;
		*ppos += filled;
		buf += filled;
	}

	return done;
write_err:
	return -EFAULT;
}

static int mdpy_mmap(struct vfio_device *vdev, struct vm_area_struct *vma)
{
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);

	if (vma->vm_pgoff != MDPY_MEMORY_BAR_OFFSET >> PAGE_SHIFT)
		return -EINVAL;
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if (vma->vm_end - vma->vm_start > mdev_state->memsize)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	return remap_vmalloc_range(vma, mdev_state->memblk, 0);
}

static int mdpy_get_region_info(struct mdev_state *mdev_state,
				struct vfio_region_info *region_info,
				u16 *cap_type_id, void **cap_type)
{
	if (region_info->index >= VFIO_PCI_NUM_REGIONS &&
	    region_info->index != MDPY_DISPLAY_REGION)
		return -EINVAL;

	switch (region_info->index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		region_info->offset = 0;
		region_info->size   = MDPY_CONFIG_SPACE_SIZE;
		region_info->flags  = (VFIO_REGION_INFO_FLAG_READ |
				       VFIO_REGION_INFO_FLAG_WRITE);
		break;
	case VFIO_PCI_BAR0_REGION_INDEX:
	case MDPY_DISPLAY_REGION:
		region_info->offset = MDPY_MEMORY_BAR_OFFSET;
		region_info->size   = mdev_state->memsize;
		region_info->flags  = (VFIO_REGION_INFO_FLAG_READ  |
				       VFIO_REGION_INFO_FLAG_WRITE |
				       VFIO_REGION_INFO_FLAG_MMAP);
		break;
	default:
		region_info->size   = 0;
		region_info->offset = 0;
		region_info->flags  = 0;
	}

	return 0;
}

static int mdpy_get_irq_info(struct vfio_irq_info *irq_info)
{
	irq_info->count = 0;
	return 0;
}

static int mdpy_get_device_info(struct vfio_device_info *dev_info)
{
	dev_info->flags = VFIO_DEVICE_FLAGS_PCI;
	dev_info->num_regions = VFIO_PCI_NUM_REGIONS;
	dev_info->num_irqs = VFIO_PCI_NUM_IRQS;
	return 0;
}

static int mdpy_query_gfx_plane(struct mdev_state *mdev_state,
				struct vfio_device_gfx_plane_info *plane)
{
	if (plane->flags & VFIO_GFX_PLANE_TYPE_PROBE) {
		if (plane->flags == (VFIO_GFX_PLANE_TYPE_PROBE |
				     VFIO_GFX_PLANE_TYPE_REGION))
			return 0;
		return -EINVAL;
	}

	if (plane->flags != VFIO_GFX_PLANE_TYPE_REGION)
		return -EINVAL;

	plane->drm_format     = mdev_state->type->format;
	plane->width	      = mdev_state->type->width;
	plane->height	      = mdev_state->type->height;
	plane->stride	      = (mdev_state->type->width *
				 mdev_state->type->bytepp);
	plane->size	      = mdev_state->memsize;
	plane->region_index   = MDPY_DISPLAY_REGION;

	/* unused */
	plane->drm_format_mod = 0;
	plane->x_pos	      = 0;
	plane->y_pos	      = 0;
	plane->x_hot	      = 0;
	plane->y_hot	      = 0;

	return 0;
}

static long mdpy_ioctl(struct vfio_device *vdev, unsigned int cmd,
		       unsigned long arg)
{
	int ret = 0;
	unsigned long minsz;
	struct mdev_state *mdev_state =
		container_of(vdev, struct mdev_state, vdev);

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = mdpy_get_device_info(&info);
		if (ret)
			return ret;

		memcpy(&mdev_state->dev_info, &info, sizeof(info));

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info info;
		u16 cap_type_id = 0;
		void *cap_type = NULL;

		minsz = offsetofend(struct vfio_region_info, offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = mdpy_get_region_info(mdev_state, &info, &cap_type_id,
					   &cap_type);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_GET_IRQ_INFO:
	{
		struct vfio_irq_info info;

		minsz = offsetofend(struct vfio_irq_info, count);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if ((info.argsz < minsz) ||
		    (info.index >= mdev_state->dev_info.num_irqs))
			return -EINVAL;

		ret = mdpy_get_irq_info(&info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_QUERY_GFX_PLANE:
	{
		struct vfio_device_gfx_plane_info plane;

		minsz = offsetofend(struct vfio_device_gfx_plane_info,
				    region_index);

		if (copy_from_user(&plane, (void __user *)arg, minsz))
			return -EFAULT;

		if (plane.argsz < minsz)
			return -EINVAL;

		ret = mdpy_query_gfx_plane(mdev_state, &plane);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &plane, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_SET_IRQS:
		return -EINVAL;

	case VFIO_DEVICE_RESET:
		return mdpy_reset(mdev_state);
	}
	return -ENOTTY;
}

static int mdpy_open(struct vfio_device *vdev)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static void mdpy_close(struct vfio_device *vdev)
{
	module_put(THIS_MODULE);
}

static ssize_t
resolution_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mdev_state *mdev_state = dev_get_drvdata(dev);

	return sprintf(buf, "%dx%d\n",
		       mdev_state->type->width,
		       mdev_state->type->height);
}
static DEVICE_ATTR_RO(resolution);

static struct attribute *mdev_dev_attrs[] = {
	&dev_attr_resolution.attr,
	NULL,
};

static const struct attribute_group mdev_dev_group = {
	.name  = "vendor",
	.attrs = mdev_dev_attrs,
};

static const struct attribute_group *mdev_dev_groups[] = {
	&mdev_dev_group,
	NULL,
};

static ssize_t name_show(struct mdev_type *mtype,
			 struct mdev_type_attribute *attr, char *buf)
{
	const struct mdpy_type *type =
		&mdpy_types[mtype_get_type_group_id(mtype)];

	return sprintf(buf, "%s\n", type->name);
}
static MDEV_TYPE_ATTR_RO(name);

static ssize_t description_show(struct mdev_type *mtype,
				struct mdev_type_attribute *attr, char *buf)
{
	const struct mdpy_type *type =
		&mdpy_types[mtype_get_type_group_id(mtype)];

	return sprintf(buf, "virtual display, %dx%d framebuffer\n",
		       type->width, type->height);
}
static MDEV_TYPE_ATTR_RO(description);

static ssize_t available_instances_show(struct mdev_type *mtype,
					struct mdev_type_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", max_devices - mdpy_count);
}
static MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct mdev_type *mtype,
			       struct mdev_type_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_PCI_STRING);
}
static MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *mdev_types_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_description.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group mdev_type_group1 = {
	.name  = MDPY_TYPE_1,
	.attrs = mdev_types_attrs,
};

static struct attribute_group mdev_type_group2 = {
	.name  = MDPY_TYPE_2,
	.attrs = mdev_types_attrs,
};

static struct attribute_group mdev_type_group3 = {
	.name  = MDPY_TYPE_3,
	.attrs = mdev_types_attrs,
};

static struct attribute_group *mdev_type_groups[] = {
	&mdev_type_group1,
	&mdev_type_group2,
	&mdev_type_group3,
	NULL,
};

static const struct vfio_device_ops mdpy_dev_ops = {
	.open = mdpy_open,
	.release = mdpy_close,
	.read = mdpy_read,
	.write = mdpy_write,
	.ioctl = mdpy_ioctl,
	.mmap = mdpy_mmap,
};

static struct mdev_driver mdpy_driver = {
	.driver = {
		.name = "mdpy",
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
		.dev_groups = mdev_dev_groups,
	},
	.probe = mdpy_probe,
	.remove	= mdpy_remove,
};

static const struct mdev_parent_ops mdev_fops = {
	.owner			= THIS_MODULE,
	.device_driver          = &mdpy_driver,
	.supported_type_groups	= mdev_type_groups,
};

static const struct file_operations vd_fops = {
	.owner		= THIS_MODULE,
};

static void mdpy_device_release(struct device *dev)
{
	/* nothing */
}

static int __init mdpy_dev_init(void)
{
	int ret = 0;

	ret = alloc_chrdev_region(&mdpy_devt, 0, MINORMASK + 1, MDPY_NAME);
	if (ret < 0) {
		pr_err("Error: failed to register mdpy_dev, err: %d\n", ret);
		return ret;
	}
	cdev_init(&mdpy_cdev, &vd_fops);
	cdev_add(&mdpy_cdev, mdpy_devt, MINORMASK + 1);
	pr_info("%s: major %d\n", __func__, MAJOR(mdpy_devt));

	ret = mdev_register_driver(&mdpy_driver);
	if (ret)
		goto err_cdev;

	mdpy_class = class_create(THIS_MODULE, MDPY_CLASS_NAME);
	if (IS_ERR(mdpy_class)) {
		pr_err("Error: failed to register mdpy_dev class\n");
		ret = PTR_ERR(mdpy_class);
		goto err_driver;
	}
	mdpy_dev.class = mdpy_class;
	mdpy_dev.release = mdpy_device_release;
	dev_set_name(&mdpy_dev, "%s", MDPY_NAME);

	ret = device_register(&mdpy_dev);
	if (ret)
		goto err_class;

	ret = mdev_register_device(&mdpy_dev, &mdev_fops);
	if (ret)
		goto err_device;

	return 0;

err_device:
	device_unregister(&mdpy_dev);
err_class:
	class_destroy(mdpy_class);
err_driver:
	mdev_unregister_driver(&mdpy_driver);
err_cdev:
	cdev_del(&mdpy_cdev);
	unregister_chrdev_region(mdpy_devt, MINORMASK + 1);
	return ret;
}

static void __exit mdpy_dev_exit(void)
{
	mdpy_dev.bus = NULL;
	mdev_unregister_device(&mdpy_dev);

	device_unregister(&mdpy_dev);
	mdev_unregister_driver(&mdpy_driver);
	cdev_del(&mdpy_cdev);
	unregister_chrdev_region(mdpy_devt, MINORMASK + 1);
	class_destroy(mdpy_class);
	mdpy_class = NULL;
}

module_init(mdpy_dev_init)
module_exit(mdpy_dev_exit)
