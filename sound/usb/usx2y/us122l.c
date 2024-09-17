// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2007, 2008 Karsten Wiese <fzu@wemgehoertderstaat.de>
 */

#include <linux/slab.h>
#include <linux/usb.h>
#include <linux/usb/audio.h>
#include <linux/module.h>
#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#define MODNAME "US122L"
#include "usb_stream.c"
#include "../usbaudio.h"
#include "../midi.h"
#include "us122l.h"

MODULE_AUTHOR("Karsten Wiese <fzu@wemgehoertderstaat.de>");
MODULE_DESCRIPTION("TASCAM "NAME_ALLCAPS" Version 0.5");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-max */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* Id for this card */
							/* Enable this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for "NAME_ALLCAPS".");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for "NAME_ALLCAPS".");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable "NAME_ALLCAPS".");

/* driver_info flags */
#define US122L_FLAG_US144	BIT(0)

static int snd_us122l_card_used[SNDRV_CARDS];

static int us122l_create_usbmidi(struct snd_card *card)
{
	static const struct snd_usb_midi_endpoint_info quirk_data = {
		.out_ep = 4,
		.in_ep = 3,
		.out_cables =	0x001,
		.in_cables =	0x001
	};
	static const struct snd_usb_audio_quirk quirk = {
		.vendor_name =	"US122L",
		.product_name =	NAME_ALLCAPS,
		.ifnum =	1,
		.type = QUIRK_MIDI_US122L,
		.data = &quirk_data
	};
	struct usb_device *dev = US122L(card)->dev;
	struct usb_interface *iface = usb_ifnum_to_if(dev, 1);

	return snd_usbmidi_create(card, iface,
				  &US122L(card)->midi_list, &quirk);
}

static int us144_create_usbmidi(struct snd_card *card)
{
	static const struct snd_usb_midi_endpoint_info quirk_data = {
		.out_ep = 4,
		.in_ep = 3,
		.out_cables =	0x001,
		.in_cables =	0x001
	};
	static const struct snd_usb_audio_quirk quirk = {
		.vendor_name =	"US144",
		.product_name =	NAME_ALLCAPS,
		.ifnum =	0,
		.type = QUIRK_MIDI_US122L,
		.data = &quirk_data
	};
	struct usb_device *dev = US122L(card)->dev;
	struct usb_interface *iface = usb_ifnum_to_if(dev, 0);

	return snd_usbmidi_create(card, iface,
				  &US122L(card)->midi_list, &quirk);
}

static void pt_info_set(struct usb_device *dev, u8 v)
{
	int ret;

	ret = usb_control_msg_send(dev, 0, 'I',
				   USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
				   v, 0, NULL, 0, 1000, GFP_NOIO);
	snd_printdd(KERN_DEBUG "%i\n", ret);
}

static void usb_stream_hwdep_vm_open(struct vm_area_struct *area)
{
	struct us122l *us122l = area->vm_private_data;

	atomic_inc(&us122l->mmap_count);
	snd_printdd(KERN_DEBUG "%i\n", atomic_read(&us122l->mmap_count));
}

static vm_fault_t usb_stream_hwdep_vm_fault(struct vm_fault *vmf)
{
	unsigned long offset;
	struct page *page;
	void *vaddr;
	struct us122l *us122l = vmf->vma->vm_private_data;
	struct usb_stream *s;

	mutex_lock(&us122l->mutex);
	s = us122l->sk.s;
	if (!s)
		goto unlock;

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset < PAGE_ALIGN(s->read_size)) {
		vaddr = (char *)s + offset;
	} else {
		offset -= PAGE_ALIGN(s->read_size);
		if (offset >= PAGE_ALIGN(s->write_size))
			goto unlock;

		vaddr = us122l->sk.write_page + offset;
	}
	page = virt_to_page(vaddr);

	get_page(page);
	mutex_unlock(&us122l->mutex);

	vmf->page = page;

	return 0;
unlock:
	mutex_unlock(&us122l->mutex);
	return VM_FAULT_SIGBUS;
}

static void usb_stream_hwdep_vm_close(struct vm_area_struct *area)
{
	struct us122l *us122l = area->vm_private_data;

	atomic_dec(&us122l->mmap_count);
	snd_printdd(KERN_DEBUG "%i\n", atomic_read(&us122l->mmap_count));
}

static const struct vm_operations_struct usb_stream_hwdep_vm_ops = {
	.open = usb_stream_hwdep_vm_open,
	.fault = usb_stream_hwdep_vm_fault,
	.close = usb_stream_hwdep_vm_close,
};

static int usb_stream_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	struct us122l	*us122l = hw->private_data;
	struct usb_interface *iface;

	snd_printdd(KERN_DEBUG "%p %p\n", hw, file);
	if (hw->used >= 2)
		return -EBUSY;

	if (!us122l->first)
		us122l->first = file;

	if (us122l->is_us144) {
		iface = usb_ifnum_to_if(us122l->dev, 0);
		usb_autopm_get_interface(iface);
	}
	iface = usb_ifnum_to_if(us122l->dev, 1);
	usb_autopm_get_interface(iface);
	return 0;
}

static int usb_stream_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct us122l	*us122l = hw->private_data;
	struct usb_interface *iface;

	snd_printdd(KERN_DEBUG "%p %p\n", hw, file);

	if (us122l->is_us144) {
		iface = usb_ifnum_to_if(us122l->dev, 0);
		usb_autopm_put_interface(iface);
	}
	iface = usb_ifnum_to_if(us122l->dev, 1);
	usb_autopm_put_interface(iface);
	if (us122l->first == file)
		us122l->first = NULL;
	mutex_lock(&us122l->mutex);
	if (us122l->master == file)
		us122l->master = us122l->slave;

	us122l->slave = NULL;
	mutex_unlock(&us122l->mutex);
	return 0;
}

static int usb_stream_hwdep_mmap(struct snd_hwdep *hw,
				 struct file *filp, struct vm_area_struct *area)
{
	unsigned long	size = area->vm_end - area->vm_start;
	struct us122l	*us122l = hw->private_data;
	unsigned long offset;
	struct usb_stream *s;
	int err = 0;
	bool read;

	offset = area->vm_pgoff << PAGE_SHIFT;
	mutex_lock(&us122l->mutex);
	s = us122l->sk.s;
	read = offset < s->read_size;
	if (read && area->vm_flags & VM_WRITE) {
		err = -EPERM;
		goto out;
	}
	snd_printdd(KERN_DEBUG "%lu %u\n", size,
		    read ? s->read_size : s->write_size);
	/* if userspace tries to mmap beyond end of our buffer, fail */
	if (size > PAGE_ALIGN(read ? s->read_size : s->write_size)) {
		snd_printk(KERN_WARNING "%lu > %u\n", size,
			   read ? s->read_size : s->write_size);
		err = -EINVAL;
		goto out;
	}

	area->vm_ops = &usb_stream_hwdep_vm_ops;
	vm_flags_set(area, VM_DONTDUMP);
	if (!read)
		vm_flags_set(area, VM_DONTEXPAND);
	area->vm_private_data = us122l;
	atomic_inc(&us122l->mmap_count);
out:
	mutex_unlock(&us122l->mutex);
	return err;
}

static __poll_t usb_stream_hwdep_poll(struct snd_hwdep *hw,
					  struct file *file, poll_table *wait)
{
	struct us122l	*us122l = hw->private_data;
	unsigned int	*polled;
	__poll_t	mask;

	poll_wait(file, &us122l->sk.sleep, wait);

	mask = EPOLLIN | EPOLLOUT | EPOLLWRNORM | EPOLLERR;
	if (mutex_trylock(&us122l->mutex)) {
		struct usb_stream *s = us122l->sk.s;

		if (s && s->state == usb_stream_ready) {
			if (us122l->first == file)
				polled = &s->periods_polled;
			else
				polled = &us122l->second_periods_polled;
			if (*polled != s->periods_done) {
				*polled = s->periods_done;
				mask = EPOLLIN | EPOLLOUT | EPOLLWRNORM;
			} else {
				mask = 0;
			}
		}
		mutex_unlock(&us122l->mutex);
	}
	return mask;
}

static void us122l_stop(struct us122l *us122l)
{
	struct list_head *p;

	list_for_each(p, &us122l->midi_list)
		snd_usbmidi_input_stop(p);

	usb_stream_stop(&us122l->sk);
	usb_stream_free(&us122l->sk);
}

static int us122l_set_sample_rate(struct usb_device *dev, int rate)
{
	unsigned int ep = 0x81;
	unsigned char data[3];
	int err;

	data[0] = rate;
	data[1] = rate >> 8;
	data[2] = rate >> 16;
	err = usb_control_msg_send(dev, 0, UAC_SET_CUR,
				   USB_TYPE_CLASS | USB_RECIP_ENDPOINT | USB_DIR_OUT,
				   UAC_EP_CS_ATTR_SAMPLE_RATE << 8, ep, data, 3,
				   1000, GFP_NOIO);
	if (err)
		snd_printk(KERN_ERR "%d: cannot set freq %d to ep 0x%x\n",
			   dev->devnum, rate, ep);
	return err;
}

static bool us122l_start(struct us122l *us122l,
			 unsigned int rate, unsigned int period_frames)
{
	struct list_head *p;
	int err;
	unsigned int use_packsize = 0;
	bool success = false;

	if (us122l->dev->speed == USB_SPEED_HIGH) {
		/* The us-122l's descriptor defaults to iso max_packsize 78,
		   which isn't needed for samplerates <= 48000.
		   Lets save some memory:
		*/
		switch (rate) {
		case 44100:
			use_packsize = 36;
			break;
		case 48000:
			use_packsize = 42;
			break;
		case 88200:
			use_packsize = 72;
			break;
		}
	}
	if (!usb_stream_new(&us122l->sk, us122l->dev, 1, 2,
			    rate, use_packsize, period_frames, 6))
		goto out;

	err = us122l_set_sample_rate(us122l->dev, rate);
	if (err < 0) {
		us122l_stop(us122l);
		snd_printk(KERN_ERR "us122l_set_sample_rate error\n");
		goto out;
	}
	err = usb_stream_start(&us122l->sk);
	if (err < 0) {
		us122l_stop(us122l);
		snd_printk(KERN_ERR "%s error %i\n", __func__, err);
		goto out;
	}
	list_for_each(p, &us122l->midi_list)
		snd_usbmidi_input_start(p);
	success = true;
out:
	return success;
}

static int usb_stream_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
				  unsigned int cmd, unsigned long arg)
{
	struct usb_stream_config cfg;
	struct us122l *us122l = hw->private_data;
	struct usb_stream *s;
	unsigned int min_period_frames;
	int err = 0;
	bool high_speed;

	if (cmd != SNDRV_USB_STREAM_IOCTL_SET_PARAMS)
		return -ENOTTY;

	if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg)))
		return -EFAULT;

	if (cfg.version != USB_STREAM_INTERFACE_VERSION)
		return -ENXIO;

	high_speed = us122l->dev->speed == USB_SPEED_HIGH;
	if ((cfg.sample_rate != 44100 && cfg.sample_rate != 48000  &&
	     (!high_speed ||
	      (cfg.sample_rate != 88200 && cfg.sample_rate != 96000))) ||
	    cfg.frame_size != 6 ||
	    cfg.period_frames > 0x3000)
		return -EINVAL;

	switch (cfg.sample_rate) {
	case 44100:
		min_period_frames = 48;
		break;
	case 48000:
		min_period_frames = 52;
		break;
	default:
		min_period_frames = 104;
		break;
	}
	if (!high_speed)
		min_period_frames <<= 1;
	if (cfg.period_frames < min_period_frames)
		return -EINVAL;

	snd_power_wait(hw->card);

	mutex_lock(&us122l->mutex);
	s = us122l->sk.s;
	if (!us122l->master) {
		us122l->master = file;
	} else if (us122l->master != file) {
		if (!s || memcmp(&cfg, &s->cfg, sizeof(cfg))) {
			err = -EIO;
			goto unlock;
		}
		us122l->slave = file;
	}
	if (!s || memcmp(&cfg, &s->cfg, sizeof(cfg)) ||
	    s->state == usb_stream_xrun) {
		us122l_stop(us122l);
		if (!us122l_start(us122l, cfg.sample_rate, cfg.period_frames))
			err = -EIO;
		else
			err = 1;
	}
unlock:
	mutex_unlock(&us122l->mutex);
	wake_up_all(&us122l->sk.sleep);
	return err;
}

#define SND_USB_STREAM_ID "USB STREAM"
static int usb_stream_hwdep_new(struct snd_card *card)
{
	int err;
	struct snd_hwdep *hw;
	struct usb_device *dev = US122L(card)->dev;

	err = snd_hwdep_new(card, SND_USB_STREAM_ID, 0, &hw);
	if (err < 0)
		return err;

	hw->iface = SNDRV_HWDEP_IFACE_USB_STREAM;
	hw->private_data = US122L(card);
	hw->ops.open = usb_stream_hwdep_open;
	hw->ops.release = usb_stream_hwdep_release;
	hw->ops.ioctl = usb_stream_hwdep_ioctl;
	hw->ops.ioctl_compat = usb_stream_hwdep_ioctl;
	hw->ops.mmap = usb_stream_hwdep_mmap;
	hw->ops.poll = usb_stream_hwdep_poll;

	sprintf(hw->name, "/dev/bus/usb/%03d/%03d/hwdeppcm",
		dev->bus->busnum, dev->devnum);
	return 0;
}

static bool us122l_create_card(struct snd_card *card)
{
	int err;
	struct us122l *us122l = US122L(card);

	if (us122l->is_us144) {
		err = usb_set_interface(us122l->dev, 0, 1);
		if (err) {
			snd_printk(KERN_ERR "usb_set_interface error\n");
			return false;
		}
	}
	err = usb_set_interface(us122l->dev, 1, 1);
	if (err) {
		snd_printk(KERN_ERR "usb_set_interface error\n");
		return false;
	}

	pt_info_set(us122l->dev, 0x11);
	pt_info_set(us122l->dev, 0x10);

	if (!us122l_start(us122l, 44100, 256))
		return false;

	if (us122l->is_us144)
		err = us144_create_usbmidi(card);
	else
		err = us122l_create_usbmidi(card);
	if (err < 0) {
		snd_printk(KERN_ERR "us122l_create_usbmidi error %i\n", err);
		goto stop;
	}
	err = usb_stream_hwdep_new(card);
	if (err < 0) {
		/* release the midi resources */
		struct list_head *p;

		list_for_each(p, &us122l->midi_list)
			snd_usbmidi_disconnect(p);

		goto stop;
	}
	return true;

stop:
	us122l_stop(us122l);
	return false;
}

static void snd_us122l_free(struct snd_card *card)
{
	struct us122l	*us122l = US122L(card);
	int		index = us122l->card_index;

	if (index >= 0 && index < SNDRV_CARDS)
		snd_us122l_card_used[index] = 0;
}

static int usx2y_create_card(struct usb_device *device,
			     struct usb_interface *intf,
			     struct snd_card **cardp,
			     unsigned long flags)
{
	int		dev;
	struct snd_card *card;
	int err;

	for (dev = 0; dev < SNDRV_CARDS; ++dev)
		if (enable[dev] && !snd_us122l_card_used[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	err = snd_card_new(&intf->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct us122l), &card);
	if (err < 0)
		return err;
	snd_us122l_card_used[US122L(card)->card_index = dev] = 1;
	card->private_free = snd_us122l_free;
	US122L(card)->dev = device;
	mutex_init(&US122L(card)->mutex);
	init_waitqueue_head(&US122L(card)->sk.sleep);
	US122L(card)->is_us144 = flags & US122L_FLAG_US144;
	INIT_LIST_HEAD(&US122L(card)->midi_list);
	strcpy(card->driver, "USB "NAME_ALLCAPS"");
	sprintf(card->shortname, "TASCAM "NAME_ALLCAPS"");
	sprintf(card->longname, "%s (%x:%x if %d at %03d/%03d)",
		card->shortname,
		le16_to_cpu(device->descriptor.idVendor),
		le16_to_cpu(device->descriptor.idProduct),
		0,
		US122L(card)->dev->bus->busnum,
		US122L(card)->dev->devnum
		);
	*cardp = card;
	return 0;
}

static int us122l_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *device_id,
			    struct snd_card **cardp)
{
	struct usb_device *device = interface_to_usbdev(intf);
	struct snd_card *card;
	int err;

	err = usx2y_create_card(device, intf, &card, device_id->driver_info);
	if (err < 0)
		return err;

	if (!us122l_create_card(card)) {
		snd_card_free(card);
		return -EINVAL;
	}

	err = snd_card_register(card);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	usb_get_intf(usb_ifnum_to_if(device, 0));
	usb_get_dev(device);
	*cardp = card;
	return 0;
}

static int snd_us122l_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *device = interface_to_usbdev(intf);
	struct snd_card *card;
	int err;

	if (id->driver_info & US122L_FLAG_US144 &&
			device->speed == USB_SPEED_HIGH) {
		snd_printk(KERN_ERR "disable ehci-hcd to run US-144\n");
		return -ENODEV;
	}

	snd_printdd(KERN_DEBUG"%p:%i\n",
		    intf, intf->cur_altsetting->desc.bInterfaceNumber);
	if (intf->cur_altsetting->desc.bInterfaceNumber != 1)
		return 0;

	err = us122l_usb_probe(usb_get_intf(intf), id, &card);
	if (err < 0) {
		usb_put_intf(intf);
		return err;
	}

	usb_set_intfdata(intf, card);
	return 0;
}

static void snd_us122l_disconnect(struct usb_interface *intf)
{
	struct snd_card *card;
	struct us122l *us122l;
	struct list_head *p;

	card = usb_get_intfdata(intf);
	if (!card)
		return;

	snd_card_disconnect(card);

	us122l = US122L(card);
	mutex_lock(&us122l->mutex);
	us122l_stop(us122l);
	mutex_unlock(&us122l->mutex);

	/* release the midi resources */
	list_for_each(p, &us122l->midi_list) {
		snd_usbmidi_disconnect(p);
	}

	usb_put_intf(usb_ifnum_to_if(us122l->dev, 0));
	usb_put_intf(usb_ifnum_to_if(us122l->dev, 1));
	usb_put_dev(us122l->dev);

	while (atomic_read(&us122l->mmap_count))
		msleep(500);

	snd_card_free(card);
}

static int snd_us122l_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct snd_card *card;
	struct us122l *us122l;
	struct list_head *p;

	card = usb_get_intfdata(intf);
	if (!card)
		return 0;
	snd_power_change_state(card, SNDRV_CTL_POWER_D3hot);

	us122l = US122L(card);
	if (!us122l)
		return 0;

	list_for_each(p, &us122l->midi_list)
		snd_usbmidi_input_stop(p);

	mutex_lock(&us122l->mutex);
	usb_stream_stop(&us122l->sk);
	mutex_unlock(&us122l->mutex);

	return 0;
}

static int snd_us122l_resume(struct usb_interface *intf)
{
	struct snd_card *card;
	struct us122l *us122l;
	struct list_head *p;
	int err;

	card = usb_get_intfdata(intf);
	if (!card)
		return 0;

	us122l = US122L(card);
	if (!us122l)
		return 0;

	mutex_lock(&us122l->mutex);
	/* needed, doesn't restart without: */
	if (us122l->is_us144) {
		err = usb_set_interface(us122l->dev, 0, 1);
		if (err) {
			snd_printk(KERN_ERR "usb_set_interface error\n");
			goto unlock;
		}
	}
	err = usb_set_interface(us122l->dev, 1, 1);
	if (err) {
		snd_printk(KERN_ERR "usb_set_interface error\n");
		goto unlock;
	}

	pt_info_set(us122l->dev, 0x11);
	pt_info_set(us122l->dev, 0x10);

	err = us122l_set_sample_rate(us122l->dev,
				     us122l->sk.s->cfg.sample_rate);
	if (err < 0) {
		snd_printk(KERN_ERR "us122l_set_sample_rate error\n");
		goto unlock;
	}
	err = usb_stream_start(&us122l->sk);
	if (err)
		goto unlock;

	list_for_each(p, &us122l->midi_list)
		snd_usbmidi_input_start(p);
unlock:
	mutex_unlock(&us122l->mutex);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return err;
}

static const struct usb_device_id snd_us122l_usb_id_table[] = {
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x0644,
		.idProduct =	USB_ID_US122L
	},
	{	/* US-144 only works at USB1.1! Disable module ehci-hcd. */
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x0644,
		.idProduct =	USB_ID_US144,
		.driver_info =	US122L_FLAG_US144
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x0644,
		.idProduct =	USB_ID_US122MKII
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x0644,
		.idProduct =	USB_ID_US144MKII,
		.driver_info =	US122L_FLAG_US144
	},
	{ /* terminator */ }
};
MODULE_DEVICE_TABLE(usb, snd_us122l_usb_id_table);

static struct usb_driver snd_us122l_usb_driver = {
	.name =		"snd-usb-us122l",
	.probe =	snd_us122l_probe,
	.disconnect =	snd_us122l_disconnect,
	.suspend =	snd_us122l_suspend,
	.resume =	snd_us122l_resume,
	.reset_resume =	snd_us122l_resume,
	.id_table =	snd_us122l_usb_id_table,
	.supports_autosuspend = 1
};

module_usb_driver(snd_us122l_usb_driver);
