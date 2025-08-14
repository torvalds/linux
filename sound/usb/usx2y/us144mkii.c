// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2025 Šerif Rami <ramiserifpersia@gmail.com>
/*
 * ALSA Driver for TASCAM US-144MKII Audio Interface
 */

#include "us144mkii.h"

MODULE_AUTHOR("Šerif Rami <ramiserifpersia@gmail.com>");
MODULE_DESCRIPTION("ALSA Driver for TASCAM US-144MKII");
MODULE_LICENSE("GPL");

/**
 * @brief Module parameters for ALSA card instantiation.
 *
 * These parameters allow users to configure how the ALSA sound card
 * for the TASCAM US-144MKII is instantiated.
 *
 * @param index: Array of integers specifying the ALSA card index for each
 * device. Defaults to -1 (automatic).
 * @param id: Array of strings specifying the ALSA card ID for each device.
 *            Defaults to "US144MKII".
 * @param enable: Array of booleans to enable or disable each device.
 *                Defaults to {1, 0, ..., 0} (first device enabled).
 * @param dev_idx: Internal counter for probed TASCAM devices.
 */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = { 1, [1 ...(SNDRV_CARDS - 1)] = 0 };
static int dev_idx;

static int tascam_probe(struct usb_interface *intf,
			const struct usb_device_id *usb_id);
static void tascam_disconnect(struct usb_interface *intf);
static int tascam_suspend(struct usb_interface *intf, pm_message_t message);
static int tascam_resume(struct usb_interface *intf);

/**
 * tascam_card_private_free() - Frees private data for the sound card.
 * card.
 * @card: Pointer to the ALSA sound card instance.
 *
 * This function is called when the sound card is being freed. It releases
 * the reference to the USB device.
 */
static void tascam_card_private_free(struct snd_card *card)
{
	struct tascam_card *tascam = card->private_data;

	if (tascam && tascam->dev) {
		usb_put_dev(tascam->dev);
		tascam->dev = NULL;
	}
}

/**
 * tascam_probe() - Probes for the TASCAM US-144MKII device.
 * @intf: The USB interface being probed.
 * @usb_id: The USB device ID.
 *
 * This function is the entry point for the USB driver on device match.
 * is found. It performs initial device setup, including:
 * - Checking for the second interface (MIDI) and associating it.
 * - Performing a vendor-specific handshake with the device.
 * - Setting alternate settings for USB interfaces.
 * - Creating and registering the ALSA sound card.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int tascam_probe(struct usb_interface *intf,
			const struct usb_device_id *usb_id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct snd_card *card;
	struct tascam_card *tascam;
	int err;
	char *handshake_buf __free(kfree) = NULL;

	if (dev->speed != USB_SPEED_HIGH)
		dev_info(
			&dev->dev,
			"Device is connected to a USB 1.1 port, this is not supported.\n");

	/* The device has two interfaces; we drive both from this driver. */
	if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
		tascam = usb_get_intfdata(usb_ifnum_to_if(dev, 0));
		if (tascam) {
			usb_set_intfdata(intf, tascam);
			tascam->iface1 = intf;
		}
		return 0; /* Let the core handle this interface */
	}

	if (dev_idx >= SNDRV_CARDS) {
		dev_err(&dev->dev, "Too many TASCAM devices present");
		return -ENODEV;
	}

	if (!enable[dev_idx]) {
		dev_info(&dev->dev, "TASCAM US-144MKII device disabled");
		return -ENOENT;
	}

	handshake_buf = kmalloc(1, GFP_KERNEL);
	if (!handshake_buf)
		return -ENOMEM;

	/* Perform vendor-specific handshake */
	err = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
			      VENDOR_REQ_MODE_CONTROL, RT_D2H_VENDOR_DEV,
			      MODE_VAL_HANDSHAKE_READ, 0x0000, handshake_buf, 1,
			      USB_CTRL_TIMEOUT_MS);
	if (err < 0) {
		dev_err(&dev->dev, "Handshake read failed with %d\n", err);
		return err;
	}

	if (handshake_buf[0] != 0x12 && handshake_buf[0] != 0x16 &&
	    handshake_buf[0] != 0x30) {
		dev_err(&dev->dev, "Unexpected handshake value: 0x%x\n",
			handshake_buf[0]);
		return -ENODEV;
	}

	/* Set alternate settings to enable audio/MIDI endpoints */
	err = usb_set_interface(dev, 0, 1);
	if (err < 0) {
		dev_err(&dev->dev,
			"Failed to set alt setting 1 on interface 0: %d\n",
			err);
		return err;
	}

	err = usb_set_interface(dev, 1, 1);
	if (err < 0) {
		dev_err(&dev->dev,
			"Failed to set alt setting 1 on interface 1: %d\n",
			err);
		return err;
	}

	err = snd_card_new(&dev->dev, index[dev_idx], id[dev_idx], THIS_MODULE,
			   sizeof(struct tascam_card), &card);
	if (err < 0) {
		dev_err(&dev->dev, "Failed to create sound card instance\n");
		return err;
	}

	tascam = card->private_data;
	card->private_free = tascam_card_private_free;
	tascam->dev = usb_get_dev(dev);
	tascam->card = card;
	tascam->iface0 = intf;
	tascam->digital_out_source = 1;
	tascam->capture_34_source = 1;

	strscpy(card->driver, DRIVER_NAME, sizeof(card->driver));
	if (dev->descriptor.idProduct == USB_PID_TASCAM_US144) {
		strscpy(card->shortname, "TASCAM US-144",
			sizeof(card->shortname));
	} else if (dev->descriptor.idProduct == USB_PID_TASCAM_US144MKII) {
		strscpy(card->shortname, "TASCAM US-144MKII",
			sizeof(card->shortname));
	} else {
		strscpy(card->shortname, "TASCAM Unknown",
			sizeof(card->shortname));
	}
	snprintf(card->longname, sizeof(card->longname), "%s (%04x:%04x) at %s",
		 card->shortname, USB_VID_TASCAM, dev->descriptor.idProduct,
		 dev_name(&dev->dev));

	err = snd_card_register(card);
	if (err < 0)
		goto free_card;

	usb_set_intfdata(intf, tascam);

	dev_idx++;
	return 0;

free_card:
	snd_card_free(card);
	return err;
}

/**
 * tascam_disconnect() - Disconnects the TASCAM US-144MKII device.
 * @intf: The USB interface being disconnected.
 *
 * This function is called when the device is disconnected from the system.
 * It cleans up all allocated resources by freeing the sound card.
 */
static void tascam_disconnect(struct usb_interface *intf)
{
	struct tascam_card *tascam = usb_get_intfdata(intf);

	if (!tascam)
		return;

	if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
		snd_card_disconnect(tascam->card);
		snd_card_free(tascam->card);
		dev_idx--;
	}
}

/**
 * tascam_suspend() - Handles device suspension.
 * @intf: The USB interface being suspended.
 * @message: Power management message.
 *
 * This function is a stub for handling device suspension.
 *
 * Return: 0 on success.
 */
static int tascam_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

/**
 * tascam_resume() - Handles device resumption from suspend.
 * @intf: The USB interface being resumed.
 *
 * This function is a stub for handling device resumption.
 *
 * Return: 0 on success.
 */
static int tascam_resume(struct usb_interface *intf)
{
	return 0;
}

static const struct usb_device_id tascam_usb_ids[] = {
	{ USB_DEVICE(USB_VID_TASCAM, USB_PID_TASCAM_US144) },
	{ USB_DEVICE(USB_VID_TASCAM, USB_PID_TASCAM_US144MKII) },
	{ /* Terminating entry */ }
};
MODULE_DEVICE_TABLE(usb, tascam_usb_ids);

static struct usb_driver tascam_alsa_driver = {
	.name = DRIVER_NAME,
	.probe = tascam_probe,
	.disconnect = tascam_disconnect,
	.suspend = tascam_suspend,
	.resume = tascam_resume,
	.id_table = tascam_usb_ids,
};

module_usb_driver(tascam_alsa_driver);
