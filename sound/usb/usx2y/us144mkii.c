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
 * @param dev_idx: Internal counter for the number of TASCAM devices probed.
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

void tascam_free_urbs(struct tascam_card *tascam)
{
	int i;

	usb_kill_anchored_urbs(&tascam->playback_anchor);
	for (i = 0; i < NUM_PLAYBACK_URBS; i++) {
		if (tascam->playback_urbs[i]) {
			usb_free_coherent(
				tascam->dev, tascam->playback_urb_alloc_size,
				tascam->playback_urbs[i]->transfer_buffer,
				tascam->playback_urbs[i]->transfer_dma);
			usb_free_urb(tascam->playback_urbs[i]);
			tascam->playback_urbs[i] = NULL;
		}
	}

	usb_kill_anchored_urbs(&tascam->feedback_anchor);
	for (i = 0; i < NUM_FEEDBACK_URBS; i++) {
		if (tascam->feedback_urbs[i]) {
			usb_free_coherent(
				tascam->dev, tascam->feedback_urb_alloc_size,
				tascam->feedback_urbs[i]->transfer_buffer,
				tascam->feedback_urbs[i]->transfer_dma);
			usb_free_urb(tascam->feedback_urbs[i]);
			tascam->feedback_urbs[i] = NULL;
		}
	}

	usb_kill_anchored_urbs(&tascam->capture_anchor);
	for (i = 0; i < NUM_CAPTURE_URBS; i++) {
		if (tascam->capture_urbs[i]) {
			usb_free_coherent(
				tascam->dev, tascam->capture_urb_alloc_size,
				tascam->capture_urbs[i]->transfer_buffer,
				tascam->capture_urbs[i]->transfer_dma);
			usb_free_urb(tascam->capture_urbs[i]);
			tascam->capture_urbs[i] = NULL;
		}
	}

	usb_kill_anchored_urbs(&tascam->midi_in_anchor);
	for (i = 0; i < NUM_MIDI_IN_URBS; i++) {
		if (tascam->midi_in_urbs[i]) {
			usb_free_coherent(
				tascam->dev, MIDI_IN_BUF_SIZE,
				tascam->midi_in_urbs[i]->transfer_buffer,
				tascam->midi_in_urbs[i]->transfer_dma);
			usb_free_urb(tascam->midi_in_urbs[i]);
			tascam->midi_in_urbs[i] = NULL;
		}
	}

	usb_kill_anchored_urbs(&tascam->midi_out_anchor);
	for (i = 0; i < NUM_MIDI_OUT_URBS; i++) {
		if (tascam->midi_out_urbs[i]) {
			usb_free_coherent(
				tascam->dev, MIDI_OUT_BUF_SIZE,
				tascam->midi_out_urbs[i]->transfer_buffer,
				tascam->midi_out_urbs[i]->transfer_dma);
			usb_free_urb(tascam->midi_out_urbs[i]);
			tascam->midi_out_urbs[i] = NULL;
		}
	}

	kfree(tascam->capture_routing_buffer);
	tascam->capture_routing_buffer = NULL;
	kfree(tascam->capture_decode_dst_block);
	tascam->capture_decode_dst_block = NULL;
	kfree(tascam->capture_decode_raw_block);
	tascam->capture_decode_raw_block = NULL;
	kfree(tascam->capture_ring_buffer);
	tascam->capture_ring_buffer = NULL;
}

int tascam_alloc_urbs(struct tascam_card *tascam)
{
	int i;
	size_t max_packet_size;

	max_packet_size = ((96000 / 8000) + 2) * BYTES_PER_FRAME;
	tascam->playback_urb_alloc_size =
		max_packet_size * PLAYBACK_URB_PACKETS;

	for (i = 0; i < NUM_PLAYBACK_URBS; i++) {
		struct urb *urb =
			usb_alloc_urb(PLAYBACK_URB_PACKETS, GFP_KERNEL);

		if (!urb)
			goto error;
		tascam->playback_urbs[i] = urb;

		urb->transfer_buffer = usb_alloc_coherent(
			tascam->dev, tascam->playback_urb_alloc_size,
			GFP_KERNEL, &urb->transfer_dma);
		if (!urb->transfer_buffer)
			goto error;

		urb->dev = tascam->dev;
		urb->pipe = usb_sndisocpipe(tascam->dev, EP_AUDIO_OUT);
		urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		urb->interval = 1;
		urb->context = tascam;
		urb->complete = playback_urb_complete;
	}

	tascam->feedback_urb_alloc_size =
		FEEDBACK_PACKET_SIZE * FEEDBACK_URB_PACKETS;

	for (i = 0; i < NUM_FEEDBACK_URBS; i++) {
		struct urb *f_urb =
			usb_alloc_urb(FEEDBACK_URB_PACKETS, GFP_KERNEL);

		if (!f_urb)
			goto error;
		tascam->feedback_urbs[i] = f_urb;

		f_urb->transfer_buffer = usb_alloc_coherent(
			tascam->dev, tascam->feedback_urb_alloc_size,
			GFP_KERNEL, &f_urb->transfer_dma);
		if (!f_urb->transfer_buffer)
			goto error;

		f_urb->dev = tascam->dev;
		f_urb->pipe =
			usb_rcvisocpipe(tascam->dev, EP_PLAYBACK_FEEDBACK);
		f_urb->transfer_flags = URB_ISO_ASAP | URB_NO_TRANSFER_DMA_MAP;
		f_urb->interval = 4;
		f_urb->context = tascam;
		f_urb->complete = feedback_urb_complete;
	}

	tascam->capture_urb_alloc_size = CAPTURE_URB_SIZE;
	for (i = 0; i < NUM_CAPTURE_URBS; i++) {
		struct urb *c_urb = usb_alloc_urb(0, GFP_KERNEL);

		if (!c_urb)
			goto error;
		tascam->capture_urbs[i] = c_urb;

		c_urb->transfer_buffer = usb_alloc_coherent(
			tascam->dev, tascam->capture_urb_alloc_size, GFP_KERNEL,
			&c_urb->transfer_dma);
		if (!c_urb->transfer_buffer)
			goto error;

		usb_fill_bulk_urb(c_urb, tascam->dev,
				  usb_rcvbulkpipe(tascam->dev, EP_AUDIO_IN),
				  c_urb->transfer_buffer,
				  tascam->capture_urb_alloc_size,
				  capture_urb_complete, tascam);
		c_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	/* MIDI URB and buffer allocation */
	for (i = 0; i < NUM_MIDI_IN_URBS; i++) {
		struct urb *m_urb = usb_alloc_urb(0, GFP_KERNEL);

		if (!m_urb)
			goto error;
		tascam->midi_in_urbs[i] = m_urb;
		m_urb->transfer_buffer =
			usb_alloc_coherent(tascam->dev, MIDI_IN_BUF_SIZE,
					   GFP_KERNEL, &m_urb->transfer_dma);
		if (!m_urb->transfer_buffer)
			goto error;
		usb_fill_bulk_urb(m_urb, tascam->dev,
				  usb_rcvbulkpipe(tascam->dev, EP_MIDI_IN),
				  m_urb->transfer_buffer, MIDI_IN_BUF_SIZE,
				  tascam_midi_in_urb_complete, tascam);
		m_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	for (i = 0; i < NUM_MIDI_OUT_URBS; i++) {
		struct urb *m_urb = usb_alloc_urb(0, GFP_KERNEL);

		if (!m_urb)
			goto error;
		tascam->midi_out_urbs[i] = m_urb;
		m_urb->transfer_buffer =
			usb_alloc_coherent(tascam->dev, MIDI_OUT_BUF_SIZE,
					   GFP_KERNEL, &m_urb->transfer_dma);
		if (!m_urb->transfer_buffer)
			goto error;
		usb_fill_bulk_urb(m_urb, tascam->dev,
				  usb_sndbulkpipe(tascam->dev, EP_MIDI_OUT),
				  m_urb->transfer_buffer,
				  0, /* length set later */
				  tascam_midi_out_urb_complete, tascam);
		m_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	tascam->capture_ring_buffer =
		kmalloc(CAPTURE_RING_BUFFER_SIZE, GFP_KERNEL);
	if (!tascam->capture_ring_buffer)
		goto error;

	tascam->capture_decode_raw_block =
		kmalloc(RAW_BYTES_PER_DECODE_BLOCK, GFP_KERNEL);
	if (!tascam->capture_decode_raw_block)
		goto error;

	tascam->capture_decode_dst_block =
		kmalloc(FRAMES_PER_DECODE_BLOCK * DECODED_CHANNELS_PER_FRAME *
				DECODED_SAMPLE_SIZE,
			GFP_KERNEL);
	if (!tascam->capture_decode_dst_block)
		goto error;

	tascam->capture_routing_buffer =
		kmalloc(FRAMES_PER_DECODE_BLOCK * DECODED_CHANNELS_PER_FRAME *
				DECODED_SAMPLE_SIZE,
			GFP_KERNEL);
	if (!tascam->capture_routing_buffer)
		goto error;

	return 0;

error:
	dev_err(tascam->card->dev, "Failed to allocate URBs\n");
	tascam_free_urbs(tascam);
	return -ENOMEM;
}

void tascam_stop_work_handler(struct work_struct *work)
{
	struct tascam_card *tascam =
		container_of(work, struct tascam_card, stop_work);

	usb_kill_anchored_urbs(&tascam->playback_anchor);
	usb_kill_anchored_urbs(&tascam->feedback_anchor);
	usb_kill_anchored_urbs(&tascam->capture_anchor);
	atomic_set(&tascam->active_urbs, 0);
}

/**
 * tascam_card_private_free() - Frees private data associated with the sound
 * card.
 * @card: Pointer to the ALSA sound card instance.
 *
 * This function is called when the sound card is being freed. It releases
 * resources allocated for the tascam_card structure, including the MIDI
 * input FIFO and decrements the USB device reference count.
 */
static void tascam_card_private_free(struct snd_card *card)
{
	struct tascam_card *tascam = card->private_data;

	if (tascam) {
		kfifo_free(&tascam->midi_in_fifo);
		if (tascam->dev) {
			usb_put_dev(tascam->dev);
			tascam->dev = NULL;
		}
	}
}

/**
 * tascam_suspend() - Handles device suspension.
 * @intf: The USB interface being suspended.
 * @message: Power management message.
 *
 * This function is called when the device is suspended. It stops all active
 * streams, kills all URBs, and sends a vendor-specific deep sleep command
 * to the device to ensure a stable low-power state.
 *
 * Return: 0 on success.
 */
static int tascam_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct tascam_card *tascam = usb_get_intfdata(intf);

	if (!tascam)
		return 0;

	snd_pcm_suspend_all(tascam->pcm);

	cancel_work_sync(&tascam->stop_work);
	cancel_work_sync(&tascam->capture_work);
	cancel_work_sync(&tascam->midi_in_work);
	cancel_work_sync(&tascam->midi_out_work);
	cancel_work_sync(&tascam->stop_pcm_work);
	usb_kill_anchored_urbs(&tascam->playback_anchor);
	usb_kill_anchored_urbs(&tascam->capture_anchor);
	usb_kill_anchored_urbs(&tascam->feedback_anchor);
	usb_kill_anchored_urbs(&tascam->midi_in_anchor);
	usb_kill_anchored_urbs(&tascam->midi_out_anchor);

	dev_info(&intf->dev, "sending deep sleep command\n");
	int err = usb_control_msg(tascam->dev, usb_sndctrlpipe(tascam->dev, 0),
				  VENDOR_REQ_DEEP_SLEEP, RT_H2D_VENDOR_DEV,
				  0x0000, 0x0000, NULL, 0, USB_CTRL_TIMEOUT_MS);
	if (err < 0)
		dev_err(&intf->dev, "deep sleep command failed: %d\n", err);

	return 0;
}

/**
 * tascam_resume() - Handles device resumption from suspend.
 * @intf: The USB interface being resumed.
 *
 * This function is called when the device resumes from suspend. It
 * re-establishes the active USB interface settings and re-configures the sample
 * rate if it was previously active.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int tascam_resume(struct usb_interface *intf)
{
	struct tascam_card *tascam = usb_get_intfdata(intf);
	int err;

	if (!tascam)
		return 0;

	dev_info(&intf->dev, "resuming TASCAM US-144MKII\n");

	/*
	 * The device requires a full re-initialization sequence upon resume.
	 * First, re-establish the active USB interface settings.
	 */
	err = usb_set_interface(tascam->dev, 0, 1);
	if (err < 0) {
		dev_err(&intf->dev,
			"resume: failed to set alt setting on intf 0: %d\n",
			err);
		return err;
	}
	err = usb_set_interface(tascam->dev, 1, 1);
	if (err < 0) {
		dev_err(&intf->dev,
			"resume: failed to set alt setting on intf 1: %d\n",
			err);
		return err;
	}

	/* Re-configure the sample rate if one was previously active */
	if (tascam->current_rate > 0)
		us144mkii_configure_device_for_rate(tascam,
						    tascam->current_rate);

	return 0;
}

static void tascam_error_timer(struct timer_list *t)
{
	struct tascam_card *tascam =
		container_of(t, struct tascam_card, error_timer);

	if (atomic_read(&tascam->midi_in_active))
		schedule_work(&tascam->midi_in_work);
	if (atomic_read(&tascam->midi_out_active))
		schedule_work(&tascam->midi_out_work);
}

/**
 * tascam_probe() - Probes for the TASCAM US-144MKII device.
 * @intf: The USB interface being probed.
 * @usb_id: The USB device ID.
 *
 * This function is the entry point for the USB driver when a matching device
 * is found. It performs initial device setup, including:
 * - Checking for the second interface (MIDI) and associating it.
 * - Performing a vendor-specific handshake with the device.
 * - Setting alternate settings for USB interfaces.
 * - Creating and registering the ALSA sound card, PCM device, and MIDI device.
 * - Allocating and initializing URBs for audio and MIDI transfers.
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
	    handshake_buf[0] != 0x30 && handshake_buf[0] != 0x32) {
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

	spin_lock_init(&tascam->lock);
	spin_lock_init(&tascam->midi_in_lock);
	spin_lock_init(&tascam->midi_out_lock);
	init_usb_anchor(&tascam->playback_anchor);
	init_usb_anchor(&tascam->capture_anchor);
	init_usb_anchor(&tascam->feedback_anchor);
	init_usb_anchor(&tascam->midi_in_anchor);
	init_usb_anchor(&tascam->midi_out_anchor);

	timer_setup(&tascam->error_timer, tascam_error_timer, 0);

	INIT_WORK(&tascam->stop_work, tascam_stop_work_handler);
	INIT_WORK(&tascam->stop_pcm_work, tascam_stop_pcm_work_handler);
	INIT_WORK(&tascam->capture_work, tascam_capture_work_handler);
	init_completion(&tascam->midi_out_drain_completion);

	if (kfifo_alloc(&tascam->midi_in_fifo, MIDI_IN_FIFO_SIZE, GFP_KERNEL)) {
		snd_card_free(card);
		return -ENOMEM;
	}

	strscpy(card->driver, DRIVER_NAME, sizeof(card->driver));
	if (le16_to_cpu(dev->descriptor.idProduct) == USB_PID_TASCAM_US144) {
		strscpy(card->shortname, "TASCAM US-144",
			sizeof(card->shortname));
	} else if (le16_to_cpu(dev->descriptor.idProduct) == USB_PID_TASCAM_US144MKII) {
		strscpy(card->shortname, "TASCAM US-144MKII",
			sizeof(card->shortname));
	} else {
		strscpy(card->shortname, "TASCAM Unknown",
			sizeof(card->shortname));
	}
	snprintf(card->longname, sizeof(card->longname), "%s (%04x:%04x) at %s",
		 card->shortname, USB_VID_TASCAM, dev->descriptor.idProduct,
		 dev_name(&dev->dev));

	err = snd_pcm_new(card, "US144MKII PCM", 0, 1, 1, &tascam->pcm);
	if (err < 0)
		goto free_card;
	tascam->pcm->private_data = tascam;
	strscpy(tascam->pcm->name, "US144MKII PCM", sizeof(tascam->pcm->name));

	err = tascam_init_pcm(tascam->pcm);
	if (err < 0)
		goto free_card;

	err = tascam_create_midi(tascam);
	if (err < 0)
		goto free_card;

	err = tascam_create_controls(tascam);
	if (err < 0)
		goto free_card;

	err = tascam_alloc_urbs(tascam);
	if (err < 0)
		goto free_card;

	err = snd_card_register(card);
	if (err < 0)
		goto free_card;

	usb_set_intfdata(intf, tascam);

	dev_idx++;
	return 0;

free_card:
	tascam_free_urbs(tascam);
	snd_card_free(card);
	return err;
}

/**
 * tascam_disconnect() - Disconnects the TASCAM US-144MKII device.
 * @intf: The USB interface being disconnected.
 *
 * This function is called when the device is disconnected from the system.
 * It cleans up all allocated resources, including killing URBs, freeing
 * the sound card, and releasing memory.
 */
static void tascam_disconnect(struct usb_interface *intf)
{
	struct tascam_card *tascam = usb_get_intfdata(intf);

	if (!tascam)
		return;

	if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
		/* Ensure all deferred work is complete before freeing resources */
		snd_card_disconnect(tascam->card);
		cancel_work_sync(&tascam->stop_work);
		cancel_work_sync(&tascam->capture_work);
		cancel_work_sync(&tascam->midi_in_work);
		cancel_work_sync(&tascam->midi_out_work);
		cancel_work_sync(&tascam->stop_pcm_work);

		usb_kill_anchored_urbs(&tascam->playback_anchor);
		usb_kill_anchored_urbs(&tascam->capture_anchor);
		usb_kill_anchored_urbs(&tascam->feedback_anchor);
		usb_kill_anchored_urbs(&tascam->midi_in_anchor);
		usb_kill_anchored_urbs(&tascam->midi_out_anchor);
		timer_delete_sync(&tascam->error_timer);
		tascam_free_urbs(tascam);
		snd_card_free(tascam->card);
		dev_idx--;
	}
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
