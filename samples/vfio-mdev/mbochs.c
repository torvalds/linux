// SPDX-License-Identifier: GPL-2.0
/*
 * Mediated virtual PCI display host device driver
 *
 * Emulate enough of qemu stdvga to make bochs-drm.ko happy.  That is
 * basically the vram memory bar and the bochs dispi interface vbe
 * registers in the mmio register bar.	Specifically it does *not*
 * include any legacy vga stuff.  Device looks a lot like "qemu -device
 * secondary-vga".
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
#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_rect.h>
#include <drm/drm_modeset_lock.h>
#include <drm/drm_property.h>
#include <drm/drm_plane.h>


#define VBE_DISPI_INDEX_ID		0x0
#define VBE_DISPI_INDEX_XRES		0x1
#define VBE_DISPI_INDEX_YRES		0x2
#define VBE_DISPI_INDEX_BPP		0x3
#define VBE_DISPI_INDEX_ENABLE		0x4
#define VBE_DISPI_INDEX_BANK		0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH	0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT	0x7
#define VBE_DISPI_INDEX_X_OFFSET	0x8
#define VBE_DISPI_INDEX_Y_OFFSET	0x9
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0xa
#define VBE_DISPI_INDEX_COUNT		0xb

#define VBE_DISPI_ID0			0xB0C0
#define VBE_DISPI_ID1			0xB0C1
#define VBE_DISPI_ID2			0xB0C2
#define VBE_DISPI_ID3			0xB0C3
#define VBE_DISPI_ID4			0xB0C4
#define VBE_DISPI_ID5			0xB0C5

#define VBE_DISPI_DISABLED		0x00
#define VBE_DISPI_ENABLED		0x01
#define VBE_DISPI_GETCAPS		0x02
#define VBE_DISPI_8BIT_DAC		0x20
#define VBE_DISPI_LFB_ENABLED		0x40
#define VBE_DISPI_NOCLEARMEM		0x80


#define MBOCHS_NAME		  "mbochs"
#define MBOCHS_CLASS_NAME	  "mbochs"

#define MBOCHS_EDID_REGION_INDEX  VFIO_PCI_NUM_REGIONS
#define MBOCHS_NUM_REGIONS        (MBOCHS_EDID_REGION_INDEX+1)

#define MBOCHS_CONFIG_SPACE_SIZE  0xff
#define MBOCHS_MMIO_BAR_OFFSET	  PAGE_SIZE
#define MBOCHS_MMIO_BAR_SIZE	  PAGE_SIZE
#define MBOCHS_EDID_OFFSET	  (MBOCHS_MMIO_BAR_OFFSET +	\
				   MBOCHS_MMIO_BAR_SIZE)
#define MBOCHS_EDID_SIZE	  PAGE_SIZE
#define MBOCHS_MEMORY_BAR_OFFSET  (MBOCHS_EDID_OFFSET + \
				   MBOCHS_EDID_SIZE)

#define MBOCHS_EDID_BLOB_OFFSET   (MBOCHS_EDID_SIZE/2)

#define STORE_LE16(addr, val)	(*(u16 *)addr = val)
#define STORE_LE32(addr, val)	(*(u32 *)addr = val)


MODULE_LICENSE("GPL v2");

static int max_mbytes = 256;
module_param_named(count, max_mbytes, int, 0444);
MODULE_PARM_DESC(mem, "megabytes available to " MBOCHS_NAME " devices");


#define MBOCHS_TYPE_1 "small"
#define MBOCHS_TYPE_2 "medium"
#define MBOCHS_TYPE_3 "large"

static const struct mbochs_type {
	const char *name;
	u32 mbytes;
	u32 max_x;
	u32 max_y;
} mbochs_types[] = {
	{
		.name	= MBOCHS_CLASS_NAME "-" MBOCHS_TYPE_1,
		.mbytes = 4,
		.max_x  = 800,
		.max_y  = 600,
	}, {
		.name	= MBOCHS_CLASS_NAME "-" MBOCHS_TYPE_2,
		.mbytes = 16,
		.max_x  = 1920,
		.max_y  = 1440,
	}, {
		.name	= MBOCHS_CLASS_NAME "-" MBOCHS_TYPE_3,
		.mbytes = 64,
		.max_x  = 0,
		.max_y  = 0,
	},
};


static dev_t		mbochs_devt;
static struct class	*mbochs_class;
static struct cdev	mbochs_cdev;
static struct device	mbochs_dev;
static int		mbochs_used_mbytes;

struct vfio_region_info_ext {
	struct vfio_region_info          base;
	struct vfio_region_info_cap_type type;
};

struct mbochs_mode {
	u32 drm_format;
	u32 bytepp;
	u32 width;
	u32 height;
	u32 stride;
	u32 __pad;
	u64 offset;
	u64 size;
};

struct mbochs_dmabuf {
	struct mbochs_mode mode;
	u32 id;
	struct page **pages;
	pgoff_t pagecount;
	struct dma_buf *buf;
	struct mdev_state *mdev_state;
	struct list_head next;
	bool unlinked;
};

/* State of each mdev device */
struct mdev_state {
	u8 *vconfig;
	u64 bar_mask[3];
	u32 memory_bar_mask;
	struct mutex ops_lock;
	struct mdev_device *mdev;

	const struct mbochs_type *type;
	u16 vbe[VBE_DISPI_INDEX_COUNT];
	u64 memsize;
	struct page **pages;
	pgoff_t pagecount;
	struct vfio_region_gfx_edid edid_regs;
	u8 edid_blob[0x400];

	struct list_head dmabufs;
	u32 active_id;
	u32 next_id;
};

static const char *vbe_name_list[VBE_DISPI_INDEX_COUNT] = {
	[VBE_DISPI_INDEX_ID]               = "id",
	[VBE_DISPI_INDEX_XRES]             = "xres",
	[VBE_DISPI_INDEX_YRES]             = "yres",
	[VBE_DISPI_INDEX_BPP]              = "bpp",
	[VBE_DISPI_INDEX_ENABLE]           = "enable",
	[VBE_DISPI_INDEX_BANK]             = "bank",
	[VBE_DISPI_INDEX_VIRT_WIDTH]       = "virt-width",
	[VBE_DISPI_INDEX_VIRT_HEIGHT]      = "virt-height",
	[VBE_DISPI_INDEX_X_OFFSET]         = "x-offset",
	[VBE_DISPI_INDEX_Y_OFFSET]         = "y-offset",
	[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = "video-mem",
};

static const char *vbe_name(u32 index)
{
	if (index < ARRAY_SIZE(vbe_name_list))
		return vbe_name_list[index];
	return "(invalid)";
}

static struct page *__mbochs_get_page(struct mdev_state *mdev_state,
				      pgoff_t pgoff);
static struct page *mbochs_get_page(struct mdev_state *mdev_state,
				    pgoff_t pgoff);

static const struct mbochs_type *mbochs_find_type(struct kobject *kobj)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mbochs_types); i++)
		if (strcmp(mbochs_types[i].name, kobj->name) == 0)
			return mbochs_types + i;
	return NULL;
}

static void mbochs_create_config_space(struct mdev_state *mdev_state)
{
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_VENDOR_ID],
		   0x1234);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_DEVICE_ID],
		   0x1111);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_SUBSYSTEM_VENDOR_ID],
		   PCI_SUBVENDOR_ID_REDHAT_QUMRANET);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_SUBSYSTEM_ID],
		   PCI_SUBDEVICE_ID_QEMU);

	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_COMMAND],
		   PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	STORE_LE16((u16 *) &mdev_state->vconfig[PCI_CLASS_DEVICE],
		   PCI_CLASS_DISPLAY_OTHER);
	mdev_state->vconfig[PCI_CLASS_REVISION] =  0x01;

	STORE_LE32((u32 *) &mdev_state->vconfig[PCI_BASE_ADDRESS_0],
		   PCI_BASE_ADDRESS_SPACE_MEMORY |
		   PCI_BASE_ADDRESS_MEM_TYPE_32	 |
		   PCI_BASE_ADDRESS_MEM_PREFETCH);
	mdev_state->bar_mask[0] = ~(mdev_state->memsize) + 1;

	STORE_LE32((u32 *) &mdev_state->vconfig[PCI_BASE_ADDRESS_2],
		   PCI_BASE_ADDRESS_SPACE_MEMORY |
		   PCI_BASE_ADDRESS_MEM_TYPE_32);
	mdev_state->bar_mask[2] = ~(MBOCHS_MMIO_BAR_SIZE) + 1;
}

static int mbochs_check_framebuffer(struct mdev_state *mdev_state,
				    struct mbochs_mode *mode)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	u16 *vbe = mdev_state->vbe;
	u32 virt_width;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	if (!(vbe[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED))
		goto nofb;

	memset(mode, 0, sizeof(*mode));
	switch (vbe[VBE_DISPI_INDEX_BPP]) {
	case 32:
		mode->drm_format = DRM_FORMAT_XRGB8888;
		mode->bytepp = 4;
		break;
	default:
		dev_info_ratelimited(dev, "%s: bpp %d not supported\n",
				     __func__, vbe[VBE_DISPI_INDEX_BPP]);
		goto nofb;
	}

	mode->width  = vbe[VBE_DISPI_INDEX_XRES];
	mode->height = vbe[VBE_DISPI_INDEX_YRES];
	virt_width  = vbe[VBE_DISPI_INDEX_VIRT_WIDTH];
	if (virt_width < mode->width)
		virt_width = mode->width;
	mode->stride = virt_width * mode->bytepp;
	mode->size   = (u64)mode->stride * mode->height;
	mode->offset = ((u64)vbe[VBE_DISPI_INDEX_X_OFFSET] * mode->bytepp +
		       (u64)vbe[VBE_DISPI_INDEX_Y_OFFSET] * mode->stride);

	if (mode->width < 64 || mode->height < 64) {
		dev_info_ratelimited(dev, "%s: invalid resolution %dx%d\n",
				     __func__, mode->width, mode->height);
		goto nofb;
	}
	if (mode->offset + mode->size > mdev_state->memsize) {
		dev_info_ratelimited(dev, "%s: framebuffer memory overflow\n",
				     __func__);
		goto nofb;
	}

	return 0;

nofb:
	memset(mode, 0, sizeof(*mode));
	return -EINVAL;
}

static bool mbochs_modes_equal(struct mbochs_mode *mode1,
			       struct mbochs_mode *mode2)
{
	return memcmp(mode1, mode2, sizeof(struct mbochs_mode)) == 0;
}

static void handle_pci_cfg_write(struct mdev_state *mdev_state, u16 offset,
				 char *buf, u32 count)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	int index = (offset - PCI_BASE_ADDRESS_0) / 0x04;
	u32 cfg_addr;

	switch (offset) {
	case PCI_BASE_ADDRESS_0:
	case PCI_BASE_ADDRESS_2:
		cfg_addr = *(u32 *)buf;

		if (cfg_addr == 0xffffffff) {
			cfg_addr = (cfg_addr & mdev_state->bar_mask[index]);
		} else {
			cfg_addr &= PCI_BASE_ADDRESS_MEM_MASK;
			if (cfg_addr)
				dev_info(dev, "BAR #%d @ 0x%x\n",
					 index, cfg_addr);
		}

		cfg_addr |= (mdev_state->vconfig[offset] &
			     ~PCI_BASE_ADDRESS_MEM_MASK);
		STORE_LE32(&mdev_state->vconfig[offset], cfg_addr);
		break;
	}
}

static void handle_mmio_write(struct mdev_state *mdev_state, u16 offset,
			      char *buf, u32 count)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	int index;
	u16 reg16;

	switch (offset) {
	case 0x400 ... 0x41f: /* vga ioports remapped */
		goto unhandled;
	case 0x500 ... 0x515: /* bochs dispi interface */
		if (count != 2)
			goto unhandled;
		index = (offset - 0x500) / 2;
		reg16 = *(u16 *)buf;
		if (index < ARRAY_SIZE(mdev_state->vbe))
			mdev_state->vbe[index] = reg16;
		dev_dbg(dev, "%s: vbe write %d = %d (%s)\n",
			__func__, index, reg16, vbe_name(index));
		break;
	case 0x600 ... 0x607: /* qemu extended regs */
		goto unhandled;
	default:
unhandled:
		dev_dbg(dev, "%s: @0x%03x, count %d (unhandled)\n",
			__func__, offset, count);
		break;
	}
}

static void handle_mmio_read(struct mdev_state *mdev_state, u16 offset,
			     char *buf, u32 count)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	struct vfio_region_gfx_edid *edid;
	u16 reg16 = 0;
	int index;

	switch (offset) {
	case 0x000 ... 0x3ff: /* edid block */
		edid = &mdev_state->edid_regs;
		if (edid->link_state != VFIO_DEVICE_GFX_LINK_STATE_UP ||
		    offset >= edid->edid_size) {
			memset(buf, 0, count);
			break;
		}
		memcpy(buf, mdev_state->edid_blob + offset, count);
		break;
	case 0x500 ... 0x515: /* bochs dispi interface */
		if (count != 2)
			goto unhandled;
		index = (offset - 0x500) / 2;
		if (index < ARRAY_SIZE(mdev_state->vbe))
			reg16 = mdev_state->vbe[index];
		dev_dbg(dev, "%s: vbe read %d = %d (%s)\n",
			__func__, index, reg16, vbe_name(index));
		*(u16 *)buf = reg16;
		break;
	default:
unhandled:
		dev_dbg(dev, "%s: @0x%03x, count %d (unhandled)\n",
			__func__, offset, count);
		memset(buf, 0, count);
		break;
	}
}

static void handle_edid_regs(struct mdev_state *mdev_state, u16 offset,
			     char *buf, u32 count, bool is_write)
{
	char *regs = (void *)&mdev_state->edid_regs;

	if (offset + count > sizeof(mdev_state->edid_regs))
		return;
	if (count != 4)
		return;
	if (offset % 4)
		return;

	if (is_write) {
		switch (offset) {
		case offsetof(struct vfio_region_gfx_edid, link_state):
		case offsetof(struct vfio_region_gfx_edid, edid_size):
			memcpy(regs + offset, buf, count);
			break;
		default:
			/* read-only regs */
			break;
		}
	} else {
		memcpy(buf, regs + offset, count);
	}
}

static void handle_edid_blob(struct mdev_state *mdev_state, u16 offset,
			     char *buf, u32 count, bool is_write)
{
	if (offset + count > mdev_state->edid_regs.edid_max_size)
		return;
	if (is_write)
		memcpy(mdev_state->edid_blob + offset, buf, count);
	else
		memcpy(buf, mdev_state->edid_blob + offset, count);
}

static ssize_t mdev_access(struct mdev_device *mdev, char *buf, size_t count,
			   loff_t pos, bool is_write)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);
	struct device *dev = mdev_dev(mdev);
	struct page *pg;
	loff_t poff;
	char *map;
	int ret = 0;

	mutex_lock(&mdev_state->ops_lock);

	if (pos < MBOCHS_CONFIG_SPACE_SIZE) {
		if (is_write)
			handle_pci_cfg_write(mdev_state, pos, buf, count);
		else
			memcpy(buf, (mdev_state->vconfig + pos), count);

	} else if (pos >= MBOCHS_MMIO_BAR_OFFSET &&
		   pos + count <= (MBOCHS_MMIO_BAR_OFFSET +
				   MBOCHS_MMIO_BAR_SIZE)) {
		pos -= MBOCHS_MMIO_BAR_OFFSET;
		if (is_write)
			handle_mmio_write(mdev_state, pos, buf, count);
		else
			handle_mmio_read(mdev_state, pos, buf, count);

	} else if (pos >= MBOCHS_EDID_OFFSET &&
		   pos + count <= (MBOCHS_EDID_OFFSET +
				   MBOCHS_EDID_SIZE)) {
		pos -= MBOCHS_EDID_OFFSET;
		if (pos < MBOCHS_EDID_BLOB_OFFSET) {
			handle_edid_regs(mdev_state, pos, buf, count, is_write);
		} else {
			pos -= MBOCHS_EDID_BLOB_OFFSET;
			handle_edid_blob(mdev_state, pos, buf, count, is_write);
		}

	} else if (pos >= MBOCHS_MEMORY_BAR_OFFSET &&
		   pos + count <=
		   MBOCHS_MEMORY_BAR_OFFSET + mdev_state->memsize) {
		pos -= MBOCHS_MMIO_BAR_OFFSET;
		poff = pos & ~PAGE_MASK;
		pg = __mbochs_get_page(mdev_state, pos >> PAGE_SHIFT);
		map = kmap(pg);
		if (is_write)
			memcpy(map + poff, buf, count);
		else
			memcpy(buf, map + poff, count);
		kunmap(pg);
		put_page(pg);

	} else {
		dev_dbg(dev, "%s: %s @0x%llx (unhandled)\n",
			__func__, is_write ? "WR" : "RD", pos);
		ret = -1;
		goto accessfailed;
	}

	ret = count;


accessfailed:
	mutex_unlock(&mdev_state->ops_lock);

	return ret;
}

static int mbochs_reset(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);
	u32 size64k = mdev_state->memsize / (64 * 1024);
	int i;

	for (i = 0; i < ARRAY_SIZE(mdev_state->vbe); i++)
		mdev_state->vbe[i] = 0;
	mdev_state->vbe[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;
	mdev_state->vbe[VBE_DISPI_INDEX_VIDEO_MEMORY_64K] = size64k;
	return 0;
}

static int mbochs_create(struct kobject *kobj, struct mdev_device *mdev)
{
	const struct mbochs_type *type = mbochs_find_type(kobj);
	struct device *dev = mdev_dev(mdev);
	struct mdev_state *mdev_state;

	if (!type)
		type = &mbochs_types[0];
	if (type->mbytes + mbochs_used_mbytes > max_mbytes)
		return -ENOMEM;

	mdev_state = kzalloc(sizeof(struct mdev_state), GFP_KERNEL);
	if (mdev_state == NULL)
		return -ENOMEM;

	mdev_state->vconfig = kzalloc(MBOCHS_CONFIG_SPACE_SIZE, GFP_KERNEL);
	if (mdev_state->vconfig == NULL)
		goto err_mem;

	mdev_state->memsize = type->mbytes * 1024 * 1024;
	mdev_state->pagecount = mdev_state->memsize >> PAGE_SHIFT;
	mdev_state->pages = kcalloc(mdev_state->pagecount,
				    sizeof(struct page *),
				    GFP_KERNEL);
	if (!mdev_state->pages)
		goto err_mem;

	dev_info(dev, "%s: %s, %d MB, %ld pages\n", __func__,
		 kobj->name, type->mbytes, mdev_state->pagecount);

	mutex_init(&mdev_state->ops_lock);
	mdev_state->mdev = mdev;
	mdev_set_drvdata(mdev, mdev_state);
	INIT_LIST_HEAD(&mdev_state->dmabufs);
	mdev_state->next_id = 1;

	mdev_state->type = type;
	mdev_state->edid_regs.max_xres = type->max_x;
	mdev_state->edid_regs.max_yres = type->max_y;
	mdev_state->edid_regs.edid_offset = MBOCHS_EDID_BLOB_OFFSET;
	mdev_state->edid_regs.edid_max_size = sizeof(mdev_state->edid_blob);
	mbochs_create_config_space(mdev_state);
	mbochs_reset(mdev);

	mbochs_used_mbytes += type->mbytes;
	return 0;

err_mem:
	kfree(mdev_state->vconfig);
	kfree(mdev_state);
	return -ENOMEM;
}

static int mbochs_remove(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);

	mbochs_used_mbytes -= mdev_state->type->mbytes;
	mdev_set_drvdata(mdev, NULL);
	kfree(mdev_state->pages);
	kfree(mdev_state->vconfig);
	kfree(mdev_state);
	return 0;
}

static ssize_t mbochs_read(struct mdev_device *mdev, char __user *buf,
			   size_t count, loff_t *ppos)
{
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			ret =  mdev_access(mdev, (char *)&val, sizeof(val),
					   *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			ret = mdev_access(mdev, (char *)&val, sizeof(val),
					  *ppos, false);
			if (ret <= 0)
				goto read_err;

			if (copy_to_user(buf, &val, sizeof(val)))
				goto read_err;

			filled = 2;
		} else {
			u8 val;

			ret = mdev_access(mdev, (char *)&val, sizeof(val),
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

static ssize_t mbochs_write(struct mdev_device *mdev, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	unsigned int done = 0;
	int ret;

	while (count) {
		size_t filled;

		if (count >= 4 && !(*ppos % 4)) {
			u32 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 4;
		} else if (count >= 2 && !(*ppos % 2)) {
			u16 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev, (char *)&val, sizeof(val),
					  *ppos, true);
			if (ret <= 0)
				goto write_err;

			filled = 2;
		} else {
			u8 val;

			if (copy_from_user(&val, buf, sizeof(val)))
				goto write_err;

			ret = mdev_access(mdev, (char *)&val, sizeof(val),
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

static struct page *__mbochs_get_page(struct mdev_state *mdev_state,
				      pgoff_t pgoff)
{
	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	if (!mdev_state->pages[pgoff]) {
		mdev_state->pages[pgoff] =
			alloc_pages(GFP_HIGHUSER | __GFP_ZERO, 0);
		if (!mdev_state->pages[pgoff])
			return NULL;
	}

	get_page(mdev_state->pages[pgoff]);
	return mdev_state->pages[pgoff];
}

static struct page *mbochs_get_page(struct mdev_state *mdev_state,
				    pgoff_t pgoff)
{
	struct page *page;

	if (WARN_ON(pgoff >= mdev_state->pagecount))
		return NULL;

	mutex_lock(&mdev_state->ops_lock);
	page = __mbochs_get_page(mdev_state, pgoff);
	mutex_unlock(&mdev_state->ops_lock);

	return page;
}

static void mbochs_put_pages(struct mdev_state *mdev_state)
{
	struct device *dev = mdev_dev(mdev_state->mdev);
	int i, count = 0;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	for (i = 0; i < mdev_state->pagecount; i++) {
		if (!mdev_state->pages[i])
			continue;
		put_page(mdev_state->pages[i]);
		mdev_state->pages[i] = NULL;
		count++;
	}
	dev_dbg(dev, "%s: %d pages released\n", __func__, count);
}

static vm_fault_t mbochs_region_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mdev_state *mdev_state = vma->vm_private_data;
	pgoff_t page_offset = (vmf->address - vma->vm_start) >> PAGE_SHIFT;

	if (page_offset >= mdev_state->pagecount)
		return VM_FAULT_SIGBUS;

	vmf->page = mbochs_get_page(mdev_state, page_offset);
	if (!vmf->page)
		return VM_FAULT_SIGBUS;

	return 0;
}

static const struct vm_operations_struct mbochs_region_vm_ops = {
	.fault = mbochs_region_vm_fault,
};

static int mbochs_mmap(struct mdev_device *mdev, struct vm_area_struct *vma)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);

	if (vma->vm_pgoff != MBOCHS_MEMORY_BAR_OFFSET >> PAGE_SHIFT)
		return -EINVAL;
	if (vma->vm_end < vma->vm_start)
		return -EINVAL;
	if (vma->vm_end - vma->vm_start > mdev_state->memsize)
		return -EINVAL;
	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	vma->vm_ops = &mbochs_region_vm_ops;
	vma->vm_private_data = mdev_state;
	return 0;
}

static vm_fault_t mbochs_dmabuf_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct mbochs_dmabuf *dmabuf = vma->vm_private_data;

	if (WARN_ON(vmf->pgoff >= dmabuf->pagecount))
		return VM_FAULT_SIGBUS;

	vmf->page = dmabuf->pages[vmf->pgoff];
	get_page(vmf->page);
	return 0;
}

static const struct vm_operations_struct mbochs_dmabuf_vm_ops = {
	.fault = mbochs_dmabuf_vm_fault,
};

static int mbochs_mmap_dmabuf(struct dma_buf *buf, struct vm_area_struct *vma)
{
	struct mbochs_dmabuf *dmabuf = buf->priv;
	struct device *dev = mdev_dev(dmabuf->mdev_state->mdev);

	dev_dbg(dev, "%s: %d\n", __func__, dmabuf->id);

	if ((vma->vm_flags & VM_SHARED) == 0)
		return -EINVAL;

	vma->vm_ops = &mbochs_dmabuf_vm_ops;
	vma->vm_private_data = dmabuf;
	return 0;
}

static void mbochs_print_dmabuf(struct mbochs_dmabuf *dmabuf,
				const char *prefix)
{
	struct device *dev = mdev_dev(dmabuf->mdev_state->mdev);
	u32 fourcc = dmabuf->mode.drm_format;

	dev_dbg(dev, "%s/%d: %c%c%c%c, %dx%d, stride %d, off 0x%llx, size 0x%llx, pages %ld\n",
		prefix, dmabuf->id,
		fourcc ? ((fourcc >>  0) & 0xff) : '-',
		fourcc ? ((fourcc >>  8) & 0xff) : '-',
		fourcc ? ((fourcc >> 16) & 0xff) : '-',
		fourcc ? ((fourcc >> 24) & 0xff) : '-',
		dmabuf->mode.width, dmabuf->mode.height, dmabuf->mode.stride,
		dmabuf->mode.offset, dmabuf->mode.size, dmabuf->pagecount);
}

static struct sg_table *mbochs_map_dmabuf(struct dma_buf_attachment *at,
					  enum dma_data_direction direction)
{
	struct mbochs_dmabuf *dmabuf = at->dmabuf->priv;
	struct device *dev = mdev_dev(dmabuf->mdev_state->mdev);
	struct sg_table *sg;

	dev_dbg(dev, "%s: %d\n", __func__, dmabuf->id);

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		goto err1;
	if (sg_alloc_table_from_pages(sg, dmabuf->pages, dmabuf->pagecount,
				      0, dmabuf->mode.size, GFP_KERNEL) < 0)
		goto err2;
	if (!dma_map_sg(at->dev, sg->sgl, sg->nents, direction))
		goto err3;

	return sg;

err3:
	sg_free_table(sg);
err2:
	kfree(sg);
err1:
	return ERR_PTR(-ENOMEM);
}

static void mbochs_unmap_dmabuf(struct dma_buf_attachment *at,
				struct sg_table *sg,
				enum dma_data_direction direction)
{
	struct mbochs_dmabuf *dmabuf = at->dmabuf->priv;
	struct device *dev = mdev_dev(dmabuf->mdev_state->mdev);

	dev_dbg(dev, "%s: %d\n", __func__, dmabuf->id);

	sg_free_table(sg);
	kfree(sg);
}

static void mbochs_release_dmabuf(struct dma_buf *buf)
{
	struct mbochs_dmabuf *dmabuf = buf->priv;
	struct mdev_state *mdev_state = dmabuf->mdev_state;
	struct device *dev = mdev_dev(mdev_state->mdev);
	pgoff_t pg;

	dev_dbg(dev, "%s: %d\n", __func__, dmabuf->id);

	for (pg = 0; pg < dmabuf->pagecount; pg++)
		put_page(dmabuf->pages[pg]);

	mutex_lock(&mdev_state->ops_lock);
	dmabuf->buf = NULL;
	if (dmabuf->unlinked)
		kfree(dmabuf);
	mutex_unlock(&mdev_state->ops_lock);
}

static void *mbochs_kmap_dmabuf(struct dma_buf *buf, unsigned long page_num)
{
	struct mbochs_dmabuf *dmabuf = buf->priv;
	struct page *page = dmabuf->pages[page_num];

	return kmap(page);
}

static void mbochs_kunmap_dmabuf(struct dma_buf *buf, unsigned long page_num,
				 void *vaddr)
{
	kunmap(vaddr);
}

static struct dma_buf_ops mbochs_dmabuf_ops = {
	.map_dma_buf	  = mbochs_map_dmabuf,
	.unmap_dma_buf	  = mbochs_unmap_dmabuf,
	.release	  = mbochs_release_dmabuf,
	.map		  = mbochs_kmap_dmabuf,
	.unmap		  = mbochs_kunmap_dmabuf,
	.mmap		  = mbochs_mmap_dmabuf,
};

static struct mbochs_dmabuf *mbochs_dmabuf_alloc(struct mdev_state *mdev_state,
						 struct mbochs_mode *mode)
{
	struct mbochs_dmabuf *dmabuf;
	pgoff_t page_offset, pg;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	dmabuf = kzalloc(sizeof(struct mbochs_dmabuf), GFP_KERNEL);
	if (!dmabuf)
		return NULL;

	dmabuf->mode = *mode;
	dmabuf->id = mdev_state->next_id++;
	dmabuf->pagecount = DIV_ROUND_UP(mode->size, PAGE_SIZE);
	dmabuf->pages = kcalloc(dmabuf->pagecount, sizeof(struct page *),
				GFP_KERNEL);
	if (!dmabuf->pages)
		goto err_free_dmabuf;

	page_offset = dmabuf->mode.offset >> PAGE_SHIFT;
	for (pg = 0; pg < dmabuf->pagecount; pg++) {
		dmabuf->pages[pg] = __mbochs_get_page(mdev_state,
						      page_offset + pg);
		if (!dmabuf->pages[pg])
			goto err_free_pages;
	}

	dmabuf->mdev_state = mdev_state;
	list_add(&dmabuf->next, &mdev_state->dmabufs);

	mbochs_print_dmabuf(dmabuf, __func__);
	return dmabuf;

err_free_pages:
	while (pg > 0)
		put_page(dmabuf->pages[--pg]);
	kfree(dmabuf->pages);
err_free_dmabuf:
	kfree(dmabuf);
	return NULL;
}

static struct mbochs_dmabuf *
mbochs_dmabuf_find_by_mode(struct mdev_state *mdev_state,
			   struct mbochs_mode *mode)
{
	struct mbochs_dmabuf *dmabuf;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	list_for_each_entry(dmabuf, &mdev_state->dmabufs, next)
		if (mbochs_modes_equal(&dmabuf->mode, mode))
			return dmabuf;

	return NULL;
}

static struct mbochs_dmabuf *
mbochs_dmabuf_find_by_id(struct mdev_state *mdev_state, u32 id)
{
	struct mbochs_dmabuf *dmabuf;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	list_for_each_entry(dmabuf, &mdev_state->dmabufs, next)
		if (dmabuf->id == id)
			return dmabuf;

	return NULL;
}

static int mbochs_dmabuf_export(struct mbochs_dmabuf *dmabuf)
{
	struct mdev_state *mdev_state = dmabuf->mdev_state;
	struct device *dev = mdev_dev(mdev_state->mdev);
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *buf;

	WARN_ON(!mutex_is_locked(&mdev_state->ops_lock));

	if (!IS_ALIGNED(dmabuf->mode.offset, PAGE_SIZE)) {
		dev_info_ratelimited(dev, "%s: framebuffer not page-aligned\n",
				     __func__);
		return -EINVAL;
	}

	exp_info.ops = &mbochs_dmabuf_ops;
	exp_info.size = dmabuf->mode.size;
	exp_info.priv = dmabuf;

	buf = dma_buf_export(&exp_info);
	if (IS_ERR(buf)) {
		dev_info_ratelimited(dev, "%s: dma_buf_export failed: %ld\n",
				     __func__, PTR_ERR(buf));
		return PTR_ERR(buf);
	}

	dmabuf->buf = buf;
	dev_dbg(dev, "%s: %d\n", __func__, dmabuf->id);
	return 0;
}

static int mbochs_get_region_info(struct mdev_device *mdev,
				  struct vfio_region_info_ext *ext)
{
	struct vfio_region_info *region_info = &ext->base;
	struct mdev_state *mdev_state;

	mdev_state = mdev_get_drvdata(mdev);
	if (!mdev_state)
		return -EINVAL;

	if (region_info->index >= MBOCHS_NUM_REGIONS)
		return -EINVAL;

	switch (region_info->index) {
	case VFIO_PCI_CONFIG_REGION_INDEX:
		region_info->offset = 0;
		region_info->size   = MBOCHS_CONFIG_SPACE_SIZE;
		region_info->flags  = (VFIO_REGION_INFO_FLAG_READ |
				       VFIO_REGION_INFO_FLAG_WRITE);
		break;
	case VFIO_PCI_BAR0_REGION_INDEX:
		region_info->offset = MBOCHS_MEMORY_BAR_OFFSET;
		region_info->size   = mdev_state->memsize;
		region_info->flags  = (VFIO_REGION_INFO_FLAG_READ  |
				       VFIO_REGION_INFO_FLAG_WRITE |
				       VFIO_REGION_INFO_FLAG_MMAP);
		break;
	case VFIO_PCI_BAR2_REGION_INDEX:
		region_info->offset = MBOCHS_MMIO_BAR_OFFSET;
		region_info->size   = MBOCHS_MMIO_BAR_SIZE;
		region_info->flags  = (VFIO_REGION_INFO_FLAG_READ  |
				       VFIO_REGION_INFO_FLAG_WRITE);
		break;
	case MBOCHS_EDID_REGION_INDEX:
		ext->base.argsz = sizeof(*ext);
		ext->base.offset = MBOCHS_EDID_OFFSET;
		ext->base.size = MBOCHS_EDID_SIZE;
		ext->base.flags = (VFIO_REGION_INFO_FLAG_READ  |
				   VFIO_REGION_INFO_FLAG_WRITE |
				   VFIO_REGION_INFO_FLAG_CAPS);
		ext->base.cap_offset = offsetof(typeof(*ext), type);
		ext->type.header.id = VFIO_REGION_INFO_CAP_TYPE;
		ext->type.header.version = 1;
		ext->type.header.next = 0;
		ext->type.type = VFIO_REGION_TYPE_GFX;
		ext->type.subtype = VFIO_REGION_SUBTYPE_GFX_EDID;
		break;
	default:
		region_info->size   = 0;
		region_info->offset = 0;
		region_info->flags  = 0;
	}

	return 0;
}

static int mbochs_get_irq_info(struct mdev_device *mdev,
			       struct vfio_irq_info *irq_info)
{
	irq_info->count = 0;
	return 0;
}

static int mbochs_get_device_info(struct mdev_device *mdev,
				  struct vfio_device_info *dev_info)
{
	dev_info->flags = VFIO_DEVICE_FLAGS_PCI;
	dev_info->num_regions = MBOCHS_NUM_REGIONS;
	dev_info->num_irqs = VFIO_PCI_NUM_IRQS;
	return 0;
}

static int mbochs_query_gfx_plane(struct mdev_device *mdev,
				  struct vfio_device_gfx_plane_info *plane)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);
	struct device *dev = mdev_dev(mdev);
	struct mbochs_dmabuf *dmabuf;
	struct mbochs_mode mode;
	int ret;

	if (plane->flags & VFIO_GFX_PLANE_TYPE_PROBE) {
		if (plane->flags == (VFIO_GFX_PLANE_TYPE_PROBE |
				     VFIO_GFX_PLANE_TYPE_DMABUF))
			return 0;
		return -EINVAL;
	}

	if (plane->flags != VFIO_GFX_PLANE_TYPE_DMABUF)
		return -EINVAL;

	plane->drm_format_mod = 0;
	plane->x_pos	      = 0;
	plane->y_pos	      = 0;
	plane->x_hot	      = 0;
	plane->y_hot	      = 0;

	mutex_lock(&mdev_state->ops_lock);

	ret = -EINVAL;
	if (plane->drm_plane_type == DRM_PLANE_TYPE_PRIMARY)
		ret = mbochs_check_framebuffer(mdev_state, &mode);
	if (ret < 0) {
		plane->drm_format     = 0;
		plane->width	      = 0;
		plane->height	      = 0;
		plane->stride	      = 0;
		plane->size	      = 0;
		plane->dmabuf_id      = 0;
		goto done;
	}

	dmabuf = mbochs_dmabuf_find_by_mode(mdev_state, &mode);
	if (!dmabuf)
		mbochs_dmabuf_alloc(mdev_state, &mode);
	if (!dmabuf) {
		mutex_unlock(&mdev_state->ops_lock);
		return -ENOMEM;
	}

	plane->drm_format     = dmabuf->mode.drm_format;
	plane->width	      = dmabuf->mode.width;
	plane->height	      = dmabuf->mode.height;
	plane->stride	      = dmabuf->mode.stride;
	plane->size	      = dmabuf->mode.size;
	plane->dmabuf_id      = dmabuf->id;

done:
	if (plane->drm_plane_type == DRM_PLANE_TYPE_PRIMARY &&
	    mdev_state->active_id != plane->dmabuf_id) {
		dev_dbg(dev, "%s: primary: %d => %d\n", __func__,
			mdev_state->active_id, plane->dmabuf_id);
		mdev_state->active_id = plane->dmabuf_id;
	}
	mutex_unlock(&mdev_state->ops_lock);
	return 0;
}

static int mbochs_get_gfx_dmabuf(struct mdev_device *mdev,
				 u32 id)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);
	struct mbochs_dmabuf *dmabuf;

	mutex_lock(&mdev_state->ops_lock);

	dmabuf = mbochs_dmabuf_find_by_id(mdev_state, id);
	if (!dmabuf) {
		mutex_unlock(&mdev_state->ops_lock);
		return -ENOENT;
	}

	if (!dmabuf->buf)
		mbochs_dmabuf_export(dmabuf);

	mutex_unlock(&mdev_state->ops_lock);

	if (!dmabuf->buf)
		return -EINVAL;

	return dma_buf_fd(dmabuf->buf, 0);
}

static long mbochs_ioctl(struct mdev_device *mdev, unsigned int cmd,
			unsigned long arg)
{
	int ret = 0;
	unsigned long minsz, outsz;
	struct mdev_state *mdev_state;

	mdev_state = mdev_get_drvdata(mdev);

	switch (cmd) {
	case VFIO_DEVICE_GET_INFO:
	{
		struct vfio_device_info info;

		minsz = offsetofend(struct vfio_device_info, num_irqs);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		if (info.argsz < minsz)
			return -EINVAL;

		ret = mbochs_get_device_info(mdev, &info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, minsz))
			return -EFAULT;

		return 0;
	}
	case VFIO_DEVICE_GET_REGION_INFO:
	{
		struct vfio_region_info_ext info;

		minsz = offsetofend(typeof(info), base.offset);

		if (copy_from_user(&info, (void __user *)arg, minsz))
			return -EFAULT;

		outsz = info.base.argsz;
		if (outsz < minsz)
			return -EINVAL;
		if (outsz > sizeof(info))
			return -EINVAL;

		ret = mbochs_get_region_info(mdev, &info);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &info, outsz))
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
		    (info.index >= VFIO_PCI_NUM_IRQS))
			return -EINVAL;

		ret = mbochs_get_irq_info(mdev, &info);
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

		ret = mbochs_query_gfx_plane(mdev, &plane);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)arg, &plane, minsz))
			return -EFAULT;

		return 0;
	}

	case VFIO_DEVICE_GET_GFX_DMABUF:
	{
		u32 dmabuf_id;

		if (get_user(dmabuf_id, (__u32 __user *)arg))
			return -EFAULT;

		return mbochs_get_gfx_dmabuf(mdev, dmabuf_id);
	}

	case VFIO_DEVICE_SET_IRQS:
		return -EINVAL;

	case VFIO_DEVICE_RESET:
		return mbochs_reset(mdev);
	}
	return -ENOTTY;
}

static int mbochs_open(struct mdev_device *mdev)
{
	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	return 0;
}

static void mbochs_close(struct mdev_device *mdev)
{
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);
	struct mbochs_dmabuf *dmabuf, *tmp;

	mutex_lock(&mdev_state->ops_lock);

	list_for_each_entry_safe(dmabuf, tmp, &mdev_state->dmabufs, next) {
		list_del(&dmabuf->next);
		if (dmabuf->buf) {
			/* free in mbochs_release_dmabuf() */
			dmabuf->unlinked = true;
		} else {
			kfree(dmabuf);
		}
	}
	mbochs_put_pages(mdev_state);

	mutex_unlock(&mdev_state->ops_lock);
	module_put(THIS_MODULE);
}

static ssize_t
memory_show(struct device *dev, struct device_attribute *attr,
	    char *buf)
{
	struct mdev_device *mdev = mdev_from_dev(dev);
	struct mdev_state *mdev_state = mdev_get_drvdata(mdev);

	return sprintf(buf, "%d MB\n", mdev_state->type->mbytes);
}
static DEVICE_ATTR_RO(memory);

static struct attribute *mdev_dev_attrs[] = {
	&dev_attr_memory.attr,
	NULL,
};

static const struct attribute_group mdev_dev_group = {
	.name  = "vendor",
	.attrs = mdev_dev_attrs,
};

const struct attribute_group *mdev_dev_groups[] = {
	&mdev_dev_group,
	NULL,
};

static ssize_t
name_show(struct kobject *kobj, struct device *dev, char *buf)
{
	return sprintf(buf, "%s\n", kobj->name);
}
MDEV_TYPE_ATTR_RO(name);

static ssize_t
description_show(struct kobject *kobj, struct device *dev, char *buf)
{
	const struct mbochs_type *type = mbochs_find_type(kobj);

	return sprintf(buf, "virtual display, %d MB video memory\n",
		       type ? type->mbytes  : 0);
}
MDEV_TYPE_ATTR_RO(description);

static ssize_t
available_instances_show(struct kobject *kobj, struct device *dev, char *buf)
{
	const struct mbochs_type *type = mbochs_find_type(kobj);
	int count = (max_mbytes - mbochs_used_mbytes) / type->mbytes;

	return sprintf(buf, "%d\n", count);
}
MDEV_TYPE_ATTR_RO(available_instances);

static ssize_t device_api_show(struct kobject *kobj, struct device *dev,
			       char *buf)
{
	return sprintf(buf, "%s\n", VFIO_DEVICE_API_PCI_STRING);
}
MDEV_TYPE_ATTR_RO(device_api);

static struct attribute *mdev_types_attrs[] = {
	&mdev_type_attr_name.attr,
	&mdev_type_attr_description.attr,
	&mdev_type_attr_device_api.attr,
	&mdev_type_attr_available_instances.attr,
	NULL,
};

static struct attribute_group mdev_type_group1 = {
	.name  = MBOCHS_TYPE_1,
	.attrs = mdev_types_attrs,
};

static struct attribute_group mdev_type_group2 = {
	.name  = MBOCHS_TYPE_2,
	.attrs = mdev_types_attrs,
};

static struct attribute_group mdev_type_group3 = {
	.name  = MBOCHS_TYPE_3,
	.attrs = mdev_types_attrs,
};

static struct attribute_group *mdev_type_groups[] = {
	&mdev_type_group1,
	&mdev_type_group2,
	&mdev_type_group3,
	NULL,
};

static const struct mdev_parent_ops mdev_fops = {
	.owner			= THIS_MODULE,
	.mdev_attr_groups	= mdev_dev_groups,
	.supported_type_groups	= mdev_type_groups,
	.create			= mbochs_create,
	.remove			= mbochs_remove,
	.open			= mbochs_open,
	.release		= mbochs_close,
	.read			= mbochs_read,
	.write			= mbochs_write,
	.ioctl			= mbochs_ioctl,
	.mmap			= mbochs_mmap,
};

static const struct file_operations vd_fops = {
	.owner		= THIS_MODULE,
};

static void mbochs_device_release(struct device *dev)
{
	/* nothing */
}

static int __init mbochs_dev_init(void)
{
	int ret = 0;

	ret = alloc_chrdev_region(&mbochs_devt, 0, MINORMASK, MBOCHS_NAME);
	if (ret < 0) {
		pr_err("Error: failed to register mbochs_dev, err: %d\n", ret);
		return ret;
	}
	cdev_init(&mbochs_cdev, &vd_fops);
	cdev_add(&mbochs_cdev, mbochs_devt, MINORMASK);
	pr_info("%s: major %d\n", __func__, MAJOR(mbochs_devt));

	mbochs_class = class_create(THIS_MODULE, MBOCHS_CLASS_NAME);
	if (IS_ERR(mbochs_class)) {
		pr_err("Error: failed to register mbochs_dev class\n");
		ret = PTR_ERR(mbochs_class);
		goto failed1;
	}
	mbochs_dev.class = mbochs_class;
	mbochs_dev.release = mbochs_device_release;
	dev_set_name(&mbochs_dev, "%s", MBOCHS_NAME);

	ret = device_register(&mbochs_dev);
	if (ret)
		goto failed2;

	ret = mdev_register_device(&mbochs_dev, &mdev_fops);
	if (ret)
		goto failed3;

	return 0;

failed3:
	device_unregister(&mbochs_dev);
failed2:
	class_destroy(mbochs_class);
failed1:
	cdev_del(&mbochs_cdev);
	unregister_chrdev_region(mbochs_devt, MINORMASK);
	return ret;
}

static void __exit mbochs_dev_exit(void)
{
	mbochs_dev.bus = NULL;
	mdev_unregister_device(&mbochs_dev);

	device_unregister(&mbochs_dev);
	cdev_del(&mbochs_cdev);
	unregister_chrdev_region(mbochs_devt, MINORMASK);
	class_destroy(mbochs_class);
	mbochs_class = NULL;
}

module_init(mbochs_dev_init)
module_exit(mbochs_dev_exit)
