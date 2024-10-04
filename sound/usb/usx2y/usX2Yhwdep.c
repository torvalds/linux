// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Tascam US-X2Y USB soundcards
 *
 * FPGA Loader + ALSA Startup
 *
 * Copyright (c) 2003 by Karsten Wiese <annabellesgarden@yahoo.de>
 */

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/memalloc.h>
#include <sound/pcm.h>
#include <sound/hwdep.h>
#include "usx2y.h"
#include "usbusx2y.h"
#include "usX2Yhwdep.h"

static vm_fault_t snd_us428ctls_vm_fault(struct vm_fault *vmf)
{
	unsigned long offset;
	struct page *page;
	void *vaddr;

	offset = vmf->pgoff << PAGE_SHIFT;
	vaddr = (char *)((struct usx2ydev *)vmf->vma->vm_private_data)->us428ctls_sharedmem + offset;
	page = virt_to_page(vaddr);
	get_page(page);
	vmf->page = page;

	return 0;
}

static const struct vm_operations_struct us428ctls_vm_ops = {
	.fault = snd_us428ctls_vm_fault,
};

static int snd_us428ctls_mmap(struct snd_hwdep *hw, struct file *filp, struct vm_area_struct *area)
{
	unsigned long	size = (unsigned long)(area->vm_end - area->vm_start);
	struct usx2ydev	*us428 = hw->private_data;

	// FIXME this hwdep interface is used twice: fpga download and mmap for controlling Lights etc. Maybe better using 2 hwdep devs?
	// so as long as the device isn't fully initialised yet we return -EBUSY here.
	if (!(us428->chip_status & USX2Y_STAT_CHIP_INIT))
		return -EBUSY;

	/* if userspace tries to mmap beyond end of our buffer, fail */
	if (size > US428_SHAREDMEM_PAGES) {
		dev_dbg(hw->card->dev, "%s: mmap size %lu > %lu\n", __func__,
			size, (unsigned long)US428_SHAREDMEM_PAGES);
		return -EINVAL;
	}

	area->vm_ops = &us428ctls_vm_ops;
	vm_flags_set(area, VM_DONTEXPAND | VM_DONTDUMP);
	area->vm_private_data = hw->private_data;
	return 0;
}

static __poll_t snd_us428ctls_poll(struct snd_hwdep *hw, struct file *file, poll_table *wait)
{
	__poll_t	mask = 0;
	struct usx2ydev	*us428 = hw->private_data;
	struct us428ctls_sharedmem *shm = us428->us428ctls_sharedmem;

	if (us428->chip_status & USX2Y_STAT_CHIP_HUP)
		return EPOLLHUP;

	poll_wait(file, &us428->us428ctls_wait_queue_head, wait);

	if (shm && shm->ctl_snapshot_last != shm->ctl_snapshot_red)
		mask |= EPOLLIN;

	return mask;
}


static int snd_usx2y_hwdep_dsp_status(struct snd_hwdep *hw,
				      struct snd_hwdep_dsp_status *info)
{
	static const char * const type_ids[USX2Y_TYPE_NUMS] = {
		[USX2Y_TYPE_122] = "us122",
		[USX2Y_TYPE_224] = "us224",
		[USX2Y_TYPE_428] = "us428",
	};
	struct usx2ydev	*us428 = hw->private_data;
	int id = -1;

	switch (le16_to_cpu(us428->dev->descriptor.idProduct)) {
	case USB_ID_US122:
		id = USX2Y_TYPE_122;
		break;
	case USB_ID_US224:
		id = USX2Y_TYPE_224;
		break;
	case USB_ID_US428:
		id = USX2Y_TYPE_428;
		break;
	}
	if (id < 0)
		return -ENODEV;
	strcpy(info->id, type_ids[id]);
	info->num_dsps = 2;		// 0: Prepad Data, 1: FPGA Code
	if (us428->chip_status & USX2Y_STAT_CHIP_INIT)
		info->chip_ready = 1;
	info->version = USX2Y_DRIVER_VERSION;
	return 0;
}

static int usx2y_create_usbmidi(struct snd_card *card)
{
	static const struct snd_usb_midi_endpoint_info quirk_data_1 = {
		.out_ep = 0x06,
		.in_ep = 0x06,
		.out_cables =	0x001,
		.in_cables =	0x001
	};
	static const struct snd_usb_audio_quirk quirk_1 = {
		.vendor_name =	"TASCAM",
		.product_name =	NAME_ALLCAPS,
		.ifnum =	0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = &quirk_data_1
	};
	static const struct snd_usb_midi_endpoint_info quirk_data_2 = {
		.out_ep = 0x06,
		.in_ep = 0x06,
		.out_cables =	0x003,
		.in_cables =	0x003
	};
	static const struct snd_usb_audio_quirk quirk_2 = {
		.vendor_name =	"TASCAM",
		.product_name =	"US428",
		.ifnum =	0,
		.type = QUIRK_MIDI_FIXED_ENDPOINT,
		.data = &quirk_data_2
	};
	struct usb_device *dev = usx2y(card)->dev;
	struct usb_interface *iface = usb_ifnum_to_if(dev, 0);
	const struct snd_usb_audio_quirk *quirk =
		le16_to_cpu(dev->descriptor.idProduct) == USB_ID_US428 ?
		&quirk_2 : &quirk_1;

	return snd_usbmidi_create(card, iface, &usx2y(card)->midi_list, quirk);
}

static int usx2y_create_alsa_devices(struct snd_card *card)
{
	int err;

	err = usx2y_create_usbmidi(card);
	if (err < 0) {
		dev_err(card->dev, "%s: usx2y_create_usbmidi error %i\n",
			__func__, err);
		return err;
	}
	err = usx2y_audio_create(card);
	if (err < 0)
		return err;
	err = usx2y_hwdep_pcm_new(card);
	if (err < 0)
		return err;
	err = snd_card_register(card);
	if (err < 0)
		return err;
	return 0;
}

static int snd_usx2y_hwdep_dsp_load(struct snd_hwdep *hw,
				    struct snd_hwdep_dsp_image *dsp)
{
	struct usx2ydev *priv = hw->private_data;
	struct usb_device *dev = priv->dev;
	int lret, err;
	char *buf;

	buf = memdup_user(dsp->image, dsp->length);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	err = usb_set_interface(dev, 0, 1);
	if (err)
		dev_err(&dev->dev, "usb_set_interface error\n");
	else
		err = usb_bulk_msg(dev, usb_sndbulkpipe(dev, 2), buf, dsp->length, &lret, 6000);
	kfree(buf);
	if (err)
		return err;
	if (dsp->index == 1) {
		msleep(250);				// give the device some time
		err = usx2y_async_seq04_init(priv);
		if (err) {
			dev_err(&dev->dev, "usx2y_async_seq04_init error\n");
			return err;
		}
		err = usx2y_in04_init(priv);
		if (err) {
			dev_err(&dev->dev, "usx2y_in04_init error\n");
			return err;
		}
		err = usx2y_create_alsa_devices(hw->card);
		if (err) {
			dev_err(&dev->dev, "usx2y_create_alsa_devices error %i\n", err);
			return err;
		}
		priv->chip_status |= USX2Y_STAT_CHIP_INIT;
	}
	return err;
}

int usx2y_hwdep_new(struct snd_card *card, struct usb_device *device)
{
	int err;
	struct snd_hwdep *hw;
	struct usx2ydev	*us428 = usx2y(card);

	err = snd_hwdep_new(card, SND_USX2Y_LOADER_ID, 0, &hw);
	if (err < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_USX2Y;
	hw->private_data = us428;
	hw->ops.dsp_status = snd_usx2y_hwdep_dsp_status;
	hw->ops.dsp_load = snd_usx2y_hwdep_dsp_load;
	hw->ops.mmap = snd_us428ctls_mmap;
	hw->ops.poll = snd_us428ctls_poll;
	hw->exclusive = 1;
	sprintf(hw->name, "/dev/bus/usb/%03d/%03d", device->bus->busnum, device->devnum);

	us428->us428ctls_sharedmem = alloc_pages_exact(US428_SHAREDMEM_PAGES, GFP_KERNEL);
	if (!us428->us428ctls_sharedmem)
		return -ENOMEM;
	memset(us428->us428ctls_sharedmem, -1, US428_SHAREDMEM_PAGES);
	us428->us428ctls_sharedmem->ctl_snapshot_last = -2;

	return 0;
}
