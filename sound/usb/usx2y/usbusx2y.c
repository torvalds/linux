// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * usbusy2y.c - ALSA USB US-428 Driver
 *
2005-04-14 Karsten Wiese
	Version 0.8.7.2:
	Call snd_card_free() instead of snd_card_free_in_thread() to prevent oops with dead keyboard symptom.
	Tested ok with kernel 2.6.12-rc2.

2004-12-14 Karsten Wiese
	Version 0.8.7.1:
	snd_pcm_open for rawusb pcm-devices now returns -EBUSY if called without rawusb's hwdep device being open.

2004-12-02 Karsten Wiese
	Version 0.8.7:
	Use macro usb_maxpacket() for portability.

2004-10-26 Karsten Wiese
	Version 0.8.6:
	wake_up() process waiting in usx2y_urbs_start() on error.

2004-10-21 Karsten Wiese
	Version 0.8.5:
	nrpacks is runtime or compiletime configurable now with tested values from 1 to 4.

2004-10-03 Karsten Wiese
	Version 0.8.2:
	Avoid any possible racing while in prepare callback.

2004-09-30 Karsten Wiese
	Version 0.8.0:
	Simplified things and made ohci work again.

2004-09-20 Karsten Wiese
	Version 0.7.3:
	Use usb_kill_urb() instead of deprecated (kernel 2.6.9) usb_unlink_urb().

2004-07-13 Karsten Wiese
	Version 0.7.1:
	Don't sleep in START/STOP callbacks anymore.
	us428 channels C/D not handled just for this version, sorry.

2004-06-21 Karsten Wiese
	Version 0.6.4:
	Temporarely suspend midi input
	to sanely call usb_set_interface() when setting format.

2004-06-12 Karsten Wiese
	Version 0.6.3:
	Made it thus the following rule is enforced:
	"All pcm substreams of one usx2y have to operate at the same rate & format."

2004-04-06 Karsten Wiese
	Version 0.6.0:
	Runs on 2.6.5 kernel without any "--with-debug=" things.
	us224 reported running.

2004-01-14 Karsten Wiese
	Version 0.5.1:
	Runs with 2.6.1 kernel.

2003-12-30 Karsten Wiese
	Version 0.4.1:
	Fix 24Bit 4Channel capturing for the us428.

2003-11-27 Karsten Wiese, Martin Langer
	Version 0.4:
	us122 support.
	us224 could be tested by uncommenting the sections containing USB_ID_US224

2003-11-03 Karsten Wiese
	Version 0.3:
	24Bit support. 
	"arecord -D hw:1 -c 2 -r 48000 -M -f S24_3LE|aplay -D hw:1 -c 2 -r 48000 -M -f S24_3LE" works.

2003-08-22 Karsten Wiese
	Version 0.0.8:
	Removed EZUSB Firmware. First Stage Firmwaredownload is now done by tascam-firmware downloader.
	See:
	http://usb-midi-fw.sourceforge.net/tascam-firmware.tar.gz

2003-06-18 Karsten Wiese
	Version 0.0.5:
	changed to compile with kernel 2.4.21 and alsa 0.9.4

2002-10-16 Karsten Wiese
	Version 0.0.4:
	compiles again with alsa-current.
	USB_ISO_ASAP not used anymore (most of the time), instead
	urb->start_frame is calculated here now, some calls inside usb-driver don't need to happen anymore.

	To get the best out of this:
	Disable APM-support in the kernel as APM-BIOS calls (once each second) hard disable interrupt for many precious milliseconds.
	This helped me much on my slowish PII 400 & PIII 500.
	ACPI yet untested but might cause the same bad behaviour.
	Use a kernel with lowlatency and preemptiv patches applied.
	To autoload snd-usb-midi append a line 
		post-install snd-usb-us428 modprobe snd-usb-midi
	to /etc/modules.conf.

	known problems:
	sliders, knobs, lights not yet handled except MASTER Volume slider.
       	"pcm -c 2" doesn't work. "pcm -c 2 -m direct_interleaved" does.
	KDE3: "Enable full duplex operation" deadlocks.

	
2002-08-31 Karsten Wiese
	Version 0.0.3: audio also simplex;
	simplifying: iso urbs only 1 packet, melted structs.
	ASYNC_UNLINK not used anymore: no more crashes so far.....
	for alsa 0.9 rc3.

2002-08-09 Karsten Wiese
	Version 0.0.2: midi works with snd-usb-midi, audio (only fullduplex now) with i.e. bristol.
	The firmware has been sniffed from win2k us-428 driver 3.09.

 *   Copyright (c) 2002 - 2004 Karsten Wiese
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include <sound/rawmidi.h>
#include "usx2y.h"
#include "usbusx2y.h"
#include "usX2Yhwdep.h"



MODULE_AUTHOR("Karsten Wiese <annabellesgarden@yahoo.de>");
MODULE_DESCRIPTION("TASCAM "NAME_ALLCAPS" Version 0.8.7.2");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{TASCAM(0x1604),"NAME_ALLCAPS"(0x8001)(0x8005)(0x8007)}}");

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX; /* Index 0-max */
static char* id[SNDRV_CARDS] = SNDRV_DEFAULT_STR; /* Id for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP; /* Enable this card */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for "NAME_ALLCAPS".");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for "NAME_ALLCAPS".");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable "NAME_ALLCAPS".");


static int snd_usx2y_card_used[SNDRV_CARDS];

static void usx2y_usb_disconnect(struct usb_device* usb_device, void* ptr);
static void snd_usx2y_card_private_free(struct snd_card *card);

/* 
 * pipe 4 is used for switching the lamps, setting samplerate, volumes ....   
 */
static void i_usx2y_out04_int(struct urb *urb)
{
#ifdef CONFIG_SND_DEBUG
	if (urb->status) {
		int 		i;
		struct usx2ydev *usx2y = urb->context;
		for (i = 0; i < 10 && usx2y->as04.urb[i] != urb; i++);
		snd_printdd("i_usx2y_out04_int() urb %i status=%i\n", i, urb->status);
	}
#endif
}

static void i_usx2y_in04_int(struct urb *urb)
{
	int			err = 0;
	struct usx2ydev		*usx2y = urb->context;
	struct us428ctls_sharedmem	*us428ctls = usx2y->us428ctls_sharedmem;

	usx2y->in04_int_calls++;

	if (urb->status) {
		snd_printdd("Interrupt Pipe 4 came back with status=%i\n", urb->status);
		return;
	}

	//	printk("%i:0x%02X ", 8, (int)((unsigned char*)usx2y->in04_buf)[8]); Master volume shows 0 here if fader is at max during boot ?!?
	if (us428ctls) {
		int diff = -1;
		if (-2 == us428ctls->ctl_snapshot_last) {
			diff = 0;
			memcpy(usx2y->in04_last, usx2y->in04_buf, sizeof(usx2y->in04_last));
			us428ctls->ctl_snapshot_last = -1;
		} else {
			int i;
			for (i = 0; i < 21; i++) {
				if (usx2y->in04_last[i] != ((char*)usx2y->in04_buf)[i]) {
					if (diff < 0)
						diff = i;
					usx2y->in04_last[i] = ((char*)usx2y->in04_buf)[i];
				}
			}
		}
		if (0 <= diff) {
			int n = us428ctls->ctl_snapshot_last + 1;
			if (n >= N_US428_CTL_BUFS  ||  n < 0)
				n = 0;
			memcpy(us428ctls->ctl_snapshot + n, usx2y->in04_buf, sizeof(us428ctls->ctl_snapshot[0]));
			us428ctls->ctl_snapshot_differs_at[n] = diff;
			us428ctls->ctl_snapshot_last = n;
			wake_up(&usx2y->us428ctls_wait_queue_head);
		}
	}
	
	
	if (usx2y->us04) {
		if (0 == usx2y->us04->submitted)
			do {
				err = usb_submit_urb(usx2y->us04->urb[usx2y->us04->submitted++], GFP_ATOMIC);
			} while (!err && usx2y->us04->submitted < usx2y->us04->len);
	} else
		if (us428ctls && us428ctls->p4out_last >= 0 && us428ctls->p4out_last < N_US428_P4OUT_BUFS) {
			if (us428ctls->p4out_last != us428ctls->p4out_sent) {
				int j, send = us428ctls->p4out_sent + 1;
				if (send >= N_US428_P4OUT_BUFS)
					send = 0;
				for (j = 0; j < URBS_ASYNC_SEQ  &&  !err; ++j)
					if (0 == usx2y->as04.urb[j]->status) {
						struct us428_p4out *p4out = us428ctls->p4out + send;	// FIXME if more than 1 p4out is new, 1 gets lost.
						usb_fill_bulk_urb(usx2y->as04.urb[j], usx2y->dev,
								  usb_sndbulkpipe(usx2y->dev, 0x04), &p4out->val.vol,
								  p4out->type == ELT_LIGHT ? sizeof(struct us428_lights) : 5,
								  i_usx2y_out04_int, usx2y);
						err = usb_submit_urb(usx2y->as04.urb[j], GFP_ATOMIC);
						us428ctls->p4out_sent = send;
						break;
					}
			}
		}

	if (err)
		snd_printk(KERN_ERR "in04_int() usb_submit_urb err=%i\n", err);

	urb->dev = usx2y->dev;
	usb_submit_urb(urb, GFP_ATOMIC);
}

/*
 * Prepare some urbs
 */
int usx2y_async_seq04_init(struct usx2ydev *usx2y)
{
	int	err = 0,
		i;

	usx2y->as04.buffer = kmalloc_array(URBS_ASYNC_SEQ,
					   URB_DATA_LEN_ASYNC_SEQ, GFP_KERNEL);
	if (NULL == usx2y->as04.buffer) {
		err = -ENOMEM;
	} else
		for (i = 0; i < URBS_ASYNC_SEQ; ++i) {
			if (NULL == (usx2y->as04.urb[i] = usb_alloc_urb(0, GFP_KERNEL))) {
				err = -ENOMEM;
				break;
			}
			usb_fill_bulk_urb(	usx2y->as04.urb[i], usx2y->dev,
						usb_sndbulkpipe(usx2y->dev, 0x04),
						usx2y->as04.buffer + URB_DATA_LEN_ASYNC_SEQ*i, 0,
						i_usx2y_out04_int, usx2y
				);
			err = usb_urb_ep_type_check(usx2y->as04.urb[i]);
			if (err < 0)
				break;
		}
	return err;
}

int usx2y_in04_init(struct usx2ydev *usx2y)
{
	if (! (usx2y->in04_urb = usb_alloc_urb(0, GFP_KERNEL)))
		return -ENOMEM;

	if (! (usx2y->in04_buf = kmalloc(21, GFP_KERNEL)))
		return -ENOMEM;
	 
	init_waitqueue_head(&usx2y->in04_wait_queue);
	usb_fill_int_urb(usx2y->in04_urb, usx2y->dev, usb_rcvintpipe(usx2y->dev, 0x4),
			 usx2y->in04_buf, 21,
			 i_usx2y_in04_int, usx2y,
			 10);
	if (usb_urb_ep_type_check(usx2y->in04_urb))
		return -EINVAL;
	return usb_submit_urb(usx2y->in04_urb, GFP_KERNEL);
}

static void usx2y_unlinkseq(struct snd_usx2y_async_seq *s)
{
	int	i;
	for (i = 0; i < URBS_ASYNC_SEQ; ++i) {
		usb_kill_urb(s->urb[i]);
		usb_free_urb(s->urb[i]);
		s->urb[i] = NULL;
	}
	kfree(s->buffer);
}


static const struct usb_device_id snd_usx2y_usb_id_table[] = {
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US428 
	},
	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US122 
	},
 	{
		.match_flags =	USB_DEVICE_ID_MATCH_DEVICE,
		.idVendor =	0x1604,
		.idProduct =	USB_ID_US224
	},
	{ /* terminator */ }
};

static int usx2y_create_card(struct usb_device *device,
			     struct usb_interface *intf,
			     struct snd_card **cardp)
{
	int		dev;
	struct snd_card *	card;
	int err;

	for (dev = 0; dev < SNDRV_CARDS; ++dev)
		if (enable[dev] && !snd_usx2y_card_used[dev])
			break;
	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	err = snd_card_new(&intf->dev, index[dev], id[dev], THIS_MODULE,
			   sizeof(struct usx2ydev), &card);
	if (err < 0)
		return err;
	snd_usx2y_card_used[usx2y(card)->card_index = dev] = 1;
	card->private_free = snd_usx2y_card_private_free;
	usx2y(card)->dev = device;
	init_waitqueue_head(&usx2y(card)->prepare_wait_queue);
	mutex_init(&usx2y(card)->pcm_mutex);
	INIT_LIST_HEAD(&usx2y(card)->midi_list);
	strcpy(card->driver, "USB "NAME_ALLCAPS"");
	sprintf(card->shortname, "TASCAM "NAME_ALLCAPS"");
	sprintf(card->longname, "%s (%x:%x if %d at %03d/%03d)",
		card->shortname, 
		le16_to_cpu(device->descriptor.idVendor),
		le16_to_cpu(device->descriptor.idProduct),
		0,//us428(card)->usbmidi.ifnum,
		usx2y(card)->dev->bus->busnum, usx2y(card)->dev->devnum
		);
	*cardp = card;
	return 0;
}


static int usx2y_usb_probe(struct usb_device *device,
			   struct usb_interface *intf,
			   const struct usb_device_id *device_id,
			   struct snd_card **cardp)
{
	int		err;
	struct snd_card *	card;

	*cardp = NULL;
	if (le16_to_cpu(device->descriptor.idVendor) != 0x1604 ||
	    (le16_to_cpu(device->descriptor.idProduct) != USB_ID_US122 &&
	     le16_to_cpu(device->descriptor.idProduct) != USB_ID_US224 &&
	     le16_to_cpu(device->descriptor.idProduct) != USB_ID_US428))
		return -EINVAL;

	err = usx2y_create_card(device, intf, &card);
	if (err < 0)
		return err;
	if ((err = usx2y_hwdep_new(card, device)) < 0  ||
	    (err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	*cardp = card;
	return 0;
}

/*
 * new 2.5 USB kernel API
 */
static int snd_usx2y_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct snd_card *card;
	int err;

	err = usx2y_usb_probe(interface_to_usbdev(intf), intf, id, &card);
	if (err < 0)
		return err;
	dev_set_drvdata(&intf->dev, card);
	return 0;
}

static void snd_usx2y_disconnect(struct usb_interface *intf)
{
	usx2y_usb_disconnect(interface_to_usbdev(intf),
				 usb_get_intfdata(intf));
}

MODULE_DEVICE_TABLE(usb, snd_usx2y_usb_id_table);
static struct usb_driver snd_usx2y_usb_driver = {
	.name =		"snd-usb-usx2y",
	.probe =	snd_usx2y_probe,
	.disconnect =	snd_usx2y_disconnect,
	.id_table =	snd_usx2y_usb_id_table,
};

static void snd_usx2y_card_private_free(struct snd_card *card)
{
	kfree(usx2y(card)->in04_buf);
	usb_free_urb(usx2y(card)->in04_urb);
	if (usx2y(card)->us428ctls_sharedmem)
		free_pages_exact(usx2y(card)->us428ctls_sharedmem,
				 sizeof(*usx2y(card)->us428ctls_sharedmem));
	if (usx2y(card)->card_index >= 0  &&  usx2y(card)->card_index < SNDRV_CARDS)
		snd_usx2y_card_used[usx2y(card)->card_index] = 0;
}

/*
 * Frees the device.
 */
static void usx2y_usb_disconnect(struct usb_device *device, void* ptr)
{
	if (ptr) {
		struct snd_card *card = ptr;
		struct usx2ydev *usx2y = usx2y(card);
		struct list_head *p;
		usx2y->chip_status = USX2Y_STAT_CHIP_HUP;
		usx2y_unlinkseq(&usx2y->as04);
		usb_kill_urb(usx2y->in04_urb);
		snd_card_disconnect(card);
		/* release the midi resources */
		list_for_each(p, &usx2y->midi_list) {
			snd_usbmidi_disconnect(p);
		}
		if (usx2y->us428ctls_sharedmem) 
			wake_up(&usx2y->us428ctls_wait_queue_head);
		snd_card_free(card);
	}
}

module_usb_driver(snd_usx2y_usb_driver);
