/*
 * Copyright (C) 2007, 2008 Karsten Wiese <fzu@wemgehoertderstaat.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <sound/core.h>
#include <sound/hwdep.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#define MODNAME "US122L"
#include "usb_stream.c"
#include "../usbaudio.h"
#include "us122l.h"

MODULE_AUTHOR("Karsten Wiese <fzu@wemgehoertderstaat.de>");
MODULE_DESCRIPTION("TASCAM "NAME_ALLCAPS" Version 0.5");
MODULE_LICENSE("GPL");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-max */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* Id for this card */
							/* Enable this card */
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for "NAME_ALLCAPS".");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for "NAME_ALLCAPS".");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable "NAME_ALLCAPS".");

static int snd_us122l_card_used[SNDRV_CARDS];


static int us122l_create_usbmidi(struct snd_card *card)
{
	static struct snd_usb_midi_endpoint_info quirk_data = {
		.out_ep = 4,
		.in_ep = 3,
		.out_cables =	0x001,
		.in_cables =	0x001
	};
	static struct snd_usb_audio_quirk quirk = {
		.vendor_name =	"US122L",
		.product_name =	NAME_ALLCAPS,
		.ifnum = 	1,
		.type = QUIRK_MIDI_US122L,
		.data = &quirk_data
	};
	struct usb_device *dev = US122L(card)->chip.dev;
	struct usb_interface *iface = usb_ifnum_to_if(dev, 1);

	return snd_usb_create_midi_interface(&US122L(card)->chip,
					     iface, &quirk);
}

/*
 * Wrapper for usb_control_msg().
 * Allocates a temp buffer to prevent dmaing from/to the stack.
 */
static int us122l_ctl_msg(struct usb_device *dev, unsigned int pipe,
			  __u8 request, __u8 requesttype,
			  __u16 value, __u16 index, void *data,
			  __u16 size, int timeout)
{
	int err;
	void *buf = NULL;

	if (size > 0) {
		buf = kmemdup(data, size, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;
	}
	err = usb_control_msg(dev, pipe, request, requesttype,
			      value, index, buf, size, timeout);
	if (size > 0) {
		memcpy(data, buf, size);
		kfree(buf);
	}
	return err;
}

static void pt_info_set(struct usb_device *dev, u8 v)
{
	int ret;

	ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0),
			      'I',
			      USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			      v, 0, NULL, 0, 1000);
	snd_printdd(KERN_DEBUG "%i\n", ret);
}

static void usb_stream_hwdep_vm_open(struct vm_area_struct *area)
{
	struct us122l *us122l = area->vm_private_data;
	atomic_inc(&us122l->mmap_count);
	snd_printdd(KERN_DEBUG "%i\n", atomic_read(&us122l->mmap_count));
}

static int usb_stream_hwdep_vm_fault(struct vm_area_struct *area,
				     struct vm_fault *vmf)
{
	unsigned long offset;
	struct page *page;
	void *vaddr;
	struct us122l *us122l = area->vm_private_data;
	struct usb_stream *s;

	mutex_lock(&us122l->mutex);
	s = us122l->sk.s;
	if (!s)
		goto unlock;

	offset = vmf->pgoff << PAGE_SHIFT;
	if (offset < PAGE_ALIGN(s->read_size))
		vaddr = (char *)s + offset;
	else {
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

static struct vm_operations_struct usb_stream_hwdep_vm_ops = {
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
	iface = usb_ifnum_to_if(us122l->chip.dev, 1);
	usb_autopm_get_interface(iface);
	return 0;
}

static int usb_stream_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	struct us122l	*us122l = hw->private_data;
	struct usb_interface *iface = usb_ifnum_to_if(us122l->chip.dev, 1);
	snd_printdd(KERN_DEBUG "%p %p\n", hw, file);
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
	area->vm_flags |= VM_RESERVED;
	area->vm_private_data = us122l;
	atomic_inc(&us122l->mmap_count);
out:
	mutex_unlock(&us122l->mutex);
	return err;
}

static unsigned int usb_stream_hwdep_poll(struct snd_hwdep *hw,
					  struct file *file, poll_table *wait)
{
	struct us122l	*us122l = hw->private_data;
	struct usb_stream *s = us122l->sk.s;
	unsigned	*polled;
	unsigned int	mask;

	poll_wait(file, &us122l->sk.sleep, wait);

	switch (s->state) {
	case usb_stream_ready:
		if (us122l->first == file)
			polled = &s->periods_polled;
		else
			polled = &us122l->second_periods_polled;
		if (*polled != s->periods_done) {
			*polled = s->periods_done;
			mask = POLLIN | POLLOUT | POLLWRNORM;
			break;
		}
		/* Fall through */
		mask = 0;
		break;
	default:
		mask = POLLIN | POLLOUT | POLLWRNORM | POLLERR;
		break;
	}
	return mask;
}

static void us122l_stop(struct us122l *us122l)
{
	struct list_head *p;
	list_for_each(p, &us122l->chip.midi_list)
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
	err = us122l_ctl_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR,
			     USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT,
			     SAMPLING_FREQ_CONTROL << 8, ep, data, 3, 1000);
	if (err < 0)
		snd_printk(KERN_ERR "%d: cannot set freq %d to ep 0x%x\n",
			   dev->devnum, rate, ep);
	return err;
}

static bool us122l_start(struct us122l *us122l,
			 unsigned rate, unsigned period_frames)
{
	struct list_head *p;
	int err;
	unsigned use_packsize = 0;
	bool success = false;

	if (us122l->chip.dev->speed == USB_SPEED_HIGH) {
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
	if (!usb_stream_new(&us122l->sk, us122l->chip.dev, 1, 2,
			    rate, use_packsize, period_frames, 6))
		goto out;

	err = us122l_set_sample_rate(us122l->chip.dev, rate);
	if (err < 0) {
		us122l_stop(us122l);
		snd_printk(KERN_ERR "us122l_set_sample_rate error \n");
		goto out;
	}
	err = usb_stream_start(&us122l->sk);
	if (err < 0) {
		us122l_stop(us122l);
		snd_printk(KERN_ERR "us122l_start error %i \n", err);
		goto out;
	}
	list_for_each(p, &us122l->chip.midi_list)
		snd_usbmidi_input_start(p);
	success = true;
out:
	return success;
}

static int usb_stream_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
				  unsigned cmd, unsigned long arg)
{
	struct usb_stream_config *cfg;
	struct us122l *us122l = hw->private_data;
	unsigned min_period_frames;
	int err = 0;
	bool high_speed;

	if (cmd != SNDRV_USB_STREAM_IOCTL_SET_PARAMS)
		return -ENOTTY;

	cfg = kmalloc(sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	if (copy_from_user(cfg, (void *)arg, sizeof(*cfg))) {
		err = -EFAULT;
		goto free;
	}
	if (cfg->version != USB_STREAM_INTERFACE_VERSION) {
		err = -ENXIO;
		goto free;
	}
	high_speed = us122l->chip.dev->speed == USB_SPEED_HIGH;
	if ((cfg->sample_rate != 44100 && cfg->sample_rate != 48000  &&
	     (!high_speed ||
	      (cfg->sample_rate != 88200 && cfg->sample_rate != 96000))) ||
	    cfg->frame_size != 6 ||
	    cfg->period_frames > 0x3000) {
		err = -EINVAL;
		goto free;
	}
	switch (cfg->sample_rate) {
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
	if (cfg->period_frames < min_period_frames) {
		err = -EINVAL;
		goto free;
	}

	snd_power_wait(hw->card, SNDRV_CTL_POWER_D0);

	mutex_lock(&us122l->mutex);
	if (!us122l->master)
		us122l->master = file;
	else if (us122l->master != file) {
		if (memcmp(cfg, &us122l->sk.s->cfg, sizeof(*cfg))) {
			err = -EIO;
			goto unlock;
		}
		us122l->slave = file;
	}
	if (!us122l->sk.s ||
	    memcmp(cfg, &us122l->sk.s->cfg, sizeof(*cfg)) ||
	    us122l->sk.s->state == usb_stream_xrun) {
		us122l_stop(us122l);
		if (!us122l_start(us122l, cfg->sample_rate, cfg->period_frames))
			err = -EIO;
		else
			err = 1;
	}
unlock:
	mutex_unlock(&us122l->mutex);
free:
	kfree(cfg);
	return err;
}

#define SND_USB_STREAM_ID "USB STREAM"
static int usb_stream_hwdep_new(struct snd_card *card)
{
	int err;
	struct snd_hwdep *hw;
	struct usb_device *dev = US122L(card)->chip.dev;

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

	sprintf(hw->name, "/proc/bus/usb/%03d/%03d/hwdeppcm",
		dev->bus->busnum, dev->devnum);
	return 0;
}


static bool us122l_create_card(struct snd_card *card)
{
	int err;
	struct us122l *us122l = US122L(card);

	err = usb_set_interface(us122l->chip.dev, 1, 1);
	if (err) {
		snd_printk(KERN_ERR "usb_set_interface error \n");
		return false;
	}

	pt_info_set(us122l->chip.dev, 0x11);
	pt_info_set(us122l->chip.dev, 0x10);

	if (!us122l_start(us122l, 44100, 256))
		return false;

	err = us122l_create_usbmidi(card);
	if (err < 0) {
		snd_printk(KERN_ERR "us122l_create_usbmidi error %i \n", err);
		us122l_stop(us122l);
		return false;
	}
	err = usb_stream_hwdep_new(card);
	if (err < 0) {
/* release the midi resources */
		struct list_head *p;
		list_for_each(p, &us122l->chip.midi_list)
			snd_usbmidi_disconnect(p);

		us122l_stop(us122l);
		return false;
	}
	return true;
}

static struct snd_card *usx2y_create_card(struct usb_device *device)
{
	int		dev;
	struct snd_card *card;
	for (dev = 0; dev < SNDRV_CARDS; ++dev)
		if (enable[dev] && !snd_us122l_card_used[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return NULL;
	card = snd_card_new(index[dev], id[dev], THIS_MODULE,
			    sizeof(struct us122l));
	if (!card)
		return NULL;
	snd_us122l_card_used[US122L(card)->chip.index = dev] = 1;

	US122L(card)->chip.dev = device;
	US122L(card)->chip.card = card;
	mutex_init(&US122L(card)->mutex);
	init_waitqueue_head(&US122L(card)->sk.sleep);
	INIT_LIST_HEAD(&US122L(card)->chip.midi_list);
	strcpy(card->driver, "USB "NAME_ALLCAPS"");
	sprintf(card->shortname, "TASCAM "NAME_ALLCAPS"");
	sprintf(card->longname, "%s (%x:%x if %d at %03d/%03d)",
		card->shortname,
		le16_to_cpu(device->descriptor.idVendor),
		le16_to_cpu(device->descriptor.idProduct),
		0,
		US122L(card)->chip.dev->bus->busnum,
		US122L(card)->chip.dev->devnum
		);
	snd_card_set_dev(card, &device->dev);
	return card;
}

static void *us122l_usb_probe(struct usb_interface *intf,
			      const struct usb_device_id *device_id)
{
	struct usb_device *device = interface_to_usbdev(intf);
	struct snd_card *card = usx2y_create_card(device);

	if (!card)
		return NULL;

	if (!us122l_create_card(card) ||
	    snd_card_register(card) < 0) {
		snd_card_free(card);
		return NULL;
	}

	usb_get_dev(device);
	return card;
}

static int snd_us122l_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct snd_card *card;
	snd_printdd(KERN_DEBUG"%p:%i\n",
		    intf, intf->cur_altsetting->desc.bInterfaceNumber);
	if (intf->cur_altsetting->desc.bInterfaceNumber != 1)
		return 0;

	card = us122l_usb_probe(usb_get_intf(intf), id);

	if (card) {
		usb_set_intfdata(intf, card);
		return 0;
	}

	usb_put_intf(intf);
	return -EIO;
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
	us122l->chip.shutdown = 1;

/* release the midi resources */
	list_for_each(p, &us122l->chip.midi_list) {
		snd_usbmidi_disconnect(p);
	}

	usb_put_intf(intf);
	usb_put_dev(US122L(card)->chip.dev);

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

	list_for_each(p, &us122l->chip.midi_list)
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
	err = usb_set_interface(us122l->chip.dev, 1, 1);
	if (err) {
		snd_printk(KERN_ERR "usb_set_interface error \n");
		goto unlock;
	}

	pt_info_set(us122l->chip.dev, 0x11);
	pt_info_set(us122l->chip.dev, 0x10);

	err = us122l_set_sample_rate(us122l->chip.dev,
				     us122l->sk.s->cfg.sample_rate);
	if (err < 0) {
		snd_printk(KERN_ERR "us122l_set_sample_rate error \n");
		goto unlock;
	}
	err = usb_stream_start(&us122l->sk);
	if (err)
		goto unlock;

	list_for_each(p, &us122l->chip.midi_list)
		snd_usbmidi_input_start(p);
unlock:
	mutex_unlock(&us122l->mutex);
	snd_power_change_state(card, SNDRV_CTL_POWER_D0);
	return err;
}

static struct usb_device_id snd_us122l_usb_id_table[] = {
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x0644,
		.idProduct =	USB_ID_US122L
	},
/*  	{ */		/* US-144 maybe works when @USB1.1. Untested. */
/* 		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE, */
/* 		.idVendor =	0x0644, */
/* 		.idProduct =	USB_ID_US144 */
/* 	}, */
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


static int __init snd_us122l_module_init(void)
{
	return usb_register(&snd_us122l_usb_driver);
}

static void __exit snd_us122l_module_exit(void)
{
	usb_deregister(&snd_us122l_usb_driver);
}

module_init(snd_us122l_module_init)
module_exit(snd_us122l_module_exit)
