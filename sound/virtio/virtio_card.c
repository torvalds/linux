// SPDX-License-Identifier: GPL-2.0+
/*
 * virtio-snd: Virtio sound device
 * Copyright (C) 2021 OpenSynergy GmbH
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/virtio_config.h>
#include <sound/initval.h>
#include <uapi/linux/virtio_ids.h>

#include "virtio_card.h"

u32 virtsnd_msg_timeout_ms = MSEC_PER_SEC;
module_param_named(msg_timeout_ms, virtsnd_msg_timeout_ms, uint, 0644);
MODULE_PARM_DESC(msg_timeout_ms, "Message completion timeout in milliseconds");

static void virtsnd_remove(struct virtio_device *vdev);

/**
 * virtsnd_event_send() - Add an event to the event queue.
 * @vqueue: Underlying event virtqueue.
 * @event: Event.
 * @notify: Indicates whether or not to send a notification to the device.
 * @gfp: Kernel flags for memory allocation.
 *
 * Context: Any context.
 */
static void virtsnd_event_send(struct virtqueue *vqueue,
			       struct virtio_snd_event *event, bool notify,
			       gfp_t gfp)
{
	struct scatterlist sg;
	struct scatterlist *psgs[1] = { &sg };

	/* reset event content */
	memset(event, 0, sizeof(*event));

	sg_init_one(&sg, event, sizeof(*event));

	if (virtqueue_add_sgs(vqueue, psgs, 0, 1, event, gfp) || !notify)
		return;

	if (virtqueue_kick_prepare(vqueue))
		virtqueue_notify(vqueue);
}

/**
 * virtsnd_event_dispatch() - Dispatch an event from the device side.
 * @snd: VirtIO sound device.
 * @event: VirtIO sound event.
 *
 * Context: Any context.
 */
static void virtsnd_event_dispatch(struct virtio_snd *snd,
				   struct virtio_snd_event *event)
{
	switch (le32_to_cpu(event->hdr.code)) {
	case VIRTIO_SND_EVT_JACK_CONNECTED:
	case VIRTIO_SND_EVT_JACK_DISCONNECTED:
		virtsnd_jack_event(snd, event);
		break;
	case VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED:
	case VIRTIO_SND_EVT_PCM_XRUN:
		virtsnd_pcm_event(snd, event);
		break;
	case VIRTIO_SND_EVT_CTL_NOTIFY:
		virtsnd_kctl_event(snd, event);
		break;
	}
}

/**
 * virtsnd_event_notify_cb() - Dispatch all reported events from the event queue.
 * @vqueue: Underlying event virtqueue.
 *
 * This callback function is called upon a vring interrupt request from the
 * device.
 *
 * Context: Interrupt context.
 */
static void virtsnd_event_notify_cb(struct virtqueue *vqueue)
{
	struct virtio_snd *snd = vqueue->vdev->priv;
	struct virtio_snd_queue *queue = virtsnd_event_queue(snd);
	struct virtio_snd_event *event;
	u32 length;
	unsigned long flags;

	spin_lock_irqsave(&queue->lock, flags);
	do {
		virtqueue_disable_cb(vqueue);
		while ((event = virtqueue_get_buf(vqueue, &length))) {
			virtsnd_event_dispatch(snd, event);
			virtsnd_event_send(vqueue, event, true, GFP_ATOMIC);
		}
	} while (!virtqueue_enable_cb(vqueue));
	spin_unlock_irqrestore(&queue->lock, flags);
}

/**
 * virtsnd_find_vqs() - Enumerate and initialize all virtqueues.
 * @snd: VirtIO sound device.
 *
 * After calling this function, the event queue is disabled.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_find_vqs(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	static vq_callback_t *callbacks[VIRTIO_SND_VQ_MAX] = {
		[VIRTIO_SND_VQ_CONTROL] = virtsnd_ctl_notify_cb,
		[VIRTIO_SND_VQ_EVENT] = virtsnd_event_notify_cb,
		[VIRTIO_SND_VQ_TX] = virtsnd_pcm_tx_notify_cb,
		[VIRTIO_SND_VQ_RX] = virtsnd_pcm_rx_notify_cb
	};
	static const char *names[VIRTIO_SND_VQ_MAX] = {
		[VIRTIO_SND_VQ_CONTROL] = "virtsnd-ctl",
		[VIRTIO_SND_VQ_EVENT] = "virtsnd-event",
		[VIRTIO_SND_VQ_TX] = "virtsnd-tx",
		[VIRTIO_SND_VQ_RX] = "virtsnd-rx"
	};
	struct virtqueue *vqs[VIRTIO_SND_VQ_MAX] = { 0 };
	unsigned int i;
	unsigned int n;
	int rc;

	rc = virtio_find_vqs(vdev, VIRTIO_SND_VQ_MAX, vqs, callbacks, names,
			     NULL);
	if (rc) {
		dev_err(&vdev->dev, "failed to initialize virtqueues\n");
		return rc;
	}

	for (i = 0; i < VIRTIO_SND_VQ_MAX; ++i)
		snd->queues[i].vqueue = vqs[i];

	/* Allocate events and populate the event queue */
	virtqueue_disable_cb(vqs[VIRTIO_SND_VQ_EVENT]);

	n = virtqueue_get_vring_size(vqs[VIRTIO_SND_VQ_EVENT]);

	snd->event_msgs = kmalloc_array(n, sizeof(*snd->event_msgs),
					GFP_KERNEL);
	if (!snd->event_msgs)
		return -ENOMEM;

	for (i = 0; i < n; ++i)
		virtsnd_event_send(vqs[VIRTIO_SND_VQ_EVENT],
				   &snd->event_msgs[i], false, GFP_KERNEL);

	return 0;
}

/**
 * virtsnd_enable_event_vq() - Enable the event virtqueue.
 * @snd: VirtIO sound device.
 *
 * Context: Any context.
 */
static void virtsnd_enable_event_vq(struct virtio_snd *snd)
{
	struct virtio_snd_queue *queue = virtsnd_event_queue(snd);

	if (!virtqueue_enable_cb(queue->vqueue))
		virtsnd_event_notify_cb(queue->vqueue);
}

/**
 * virtsnd_disable_event_vq() - Disable the event virtqueue.
 * @snd: VirtIO sound device.
 *
 * Context: Any context.
 */
static void virtsnd_disable_event_vq(struct virtio_snd *snd)
{
	struct virtio_snd_queue *queue = virtsnd_event_queue(snd);
	struct virtio_snd_event *event;
	u32 length;
	unsigned long flags;

	if (queue->vqueue) {
		spin_lock_irqsave(&queue->lock, flags);
		virtqueue_disable_cb(queue->vqueue);
		while ((event = virtqueue_get_buf(queue->vqueue, &length)))
			virtsnd_event_dispatch(snd, event);
		spin_unlock_irqrestore(&queue->lock, flags);
	}
}

/**
 * virtsnd_build_devs() - Read configuration and build ALSA devices.
 * @snd: VirtIO sound device.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_build_devs(struct virtio_snd *snd)
{
	struct virtio_device *vdev = snd->vdev;
	struct device *dev = &vdev->dev;
	int rc;

	rc = snd_card_new(dev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1,
			  THIS_MODULE, 0, &snd->card);
	if (rc < 0)
		return rc;

	snd->card->private_data = snd;

	strscpy(snd->card->driver, VIRTIO_SND_CARD_DRIVER,
		sizeof(snd->card->driver));
	strscpy(snd->card->shortname, VIRTIO_SND_CARD_NAME,
		sizeof(snd->card->shortname));
	if (dev->parent->bus)
		snprintf(snd->card->longname, sizeof(snd->card->longname),
			 VIRTIO_SND_CARD_NAME " at %s/%s/%s",
			 dev->parent->bus->name, dev_name(dev->parent),
			 dev_name(dev));
	else
		snprintf(snd->card->longname, sizeof(snd->card->longname),
			 VIRTIO_SND_CARD_NAME " at %s/%s",
			 dev_name(dev->parent), dev_name(dev));

	rc = virtsnd_jack_parse_cfg(snd);
	if (rc)
		return rc;

	rc = virtsnd_pcm_parse_cfg(snd);
	if (rc)
		return rc;

	rc = virtsnd_chmap_parse_cfg(snd);
	if (rc)
		return rc;

	if (virtio_has_feature(vdev, VIRTIO_SND_F_CTLS)) {
		rc = virtsnd_kctl_parse_cfg(snd);
		if (rc)
			return rc;
	}

	if (snd->njacks) {
		rc = virtsnd_jack_build_devs(snd);
		if (rc)
			return rc;
	}

	if (snd->nsubstreams) {
		rc = virtsnd_pcm_build_devs(snd);
		if (rc)
			return rc;
	}

	if (snd->nchmaps) {
		rc = virtsnd_chmap_build_devs(snd);
		if (rc)
			return rc;
	}

	if (snd->nkctls) {
		rc = virtsnd_kctl_build_devs(snd);
		if (rc)
			return rc;
	}

	return snd_card_register(snd->card);
}

/**
 * virtsnd_validate() - Validate if the device can be started.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context.
 * Return: 0 on success, -EINVAL on failure.
 */
static int virtsnd_validate(struct virtio_device *vdev)
{
	if (!vdev->config->get) {
		dev_err(&vdev->dev, "configuration access disabled\n");
		return -EINVAL;
	}

	if (!virtio_has_feature(vdev, VIRTIO_F_VERSION_1)) {
		dev_err(&vdev->dev,
			"device does not comply with spec version 1.x\n");
		return -EINVAL;
	}

	if (!virtsnd_msg_timeout_ms) {
		dev_err(&vdev->dev, "msg_timeout_ms value cannot be zero\n");
		return -EINVAL;
	}

	if (virtsnd_pcm_validate(vdev))
		return -EINVAL;

	return 0;
}

/**
 * virtsnd_probe() - Create and initialize the device.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context that permits to sleep.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_probe(struct virtio_device *vdev)
{
	struct virtio_snd *snd;
	unsigned int i;
	int rc;

	snd = devm_kzalloc(&vdev->dev, sizeof(*snd), GFP_KERNEL);
	if (!snd)
		return -ENOMEM;

	snd->vdev = vdev;
	INIT_LIST_HEAD(&snd->ctl_msgs);
	INIT_LIST_HEAD(&snd->pcm_list);

	vdev->priv = snd;

	for (i = 0; i < VIRTIO_SND_VQ_MAX; ++i)
		spin_lock_init(&snd->queues[i].lock);

	rc = virtsnd_find_vqs(snd);
	if (rc)
		goto on_exit;

	virtio_device_ready(vdev);

	rc = virtsnd_build_devs(snd);
	if (rc)
		goto on_exit;

	virtsnd_enable_event_vq(snd);

on_exit:
	if (rc)
		virtsnd_remove(vdev);

	return rc;
}

/**
 * virtsnd_remove() - Remove VirtIO and ALSA devices.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context that permits to sleep.
 */
static void virtsnd_remove(struct virtio_device *vdev)
{
	struct virtio_snd *snd = vdev->priv;
	unsigned int i;

	virtsnd_disable_event_vq(snd);
	virtsnd_ctl_msg_cancel_all(snd);

	if (snd->card)
		snd_card_free(snd->card);

	vdev->config->del_vqs(vdev);
	virtio_reset_device(vdev);

	for (i = 0; snd->substreams && i < snd->nsubstreams; ++i) {
		struct virtio_pcm_substream *vss = &snd->substreams[i];

		cancel_work_sync(&vss->elapsed_period);
		virtsnd_pcm_msg_free(vss);
	}

	kfree(snd->event_msgs);
}

#ifdef CONFIG_PM_SLEEP
/**
 * virtsnd_freeze() - Suspend device.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_freeze(struct virtio_device *vdev)
{
	struct virtio_snd *snd = vdev->priv;
	unsigned int i;

	virtsnd_disable_event_vq(snd);
	virtsnd_ctl_msg_cancel_all(snd);

	vdev->config->del_vqs(vdev);
	virtio_reset_device(vdev);

	for (i = 0; i < snd->nsubstreams; ++i)
		cancel_work_sync(&snd->substreams[i].elapsed_period);

	kfree(snd->event_msgs);
	snd->event_msgs = NULL;

	return 0;
}

/**
 * virtsnd_restore() - Resume device.
 * @vdev: VirtIO parent device.
 *
 * Context: Any context.
 * Return: 0 on success, -errno on failure.
 */
static int virtsnd_restore(struct virtio_device *vdev)
{
	struct virtio_snd *snd = vdev->priv;
	int rc;

	rc = virtsnd_find_vqs(snd);
	if (rc)
		return rc;

	virtio_device_ready(vdev);

	virtsnd_enable_event_vq(snd);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_SOUND, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
	VIRTIO_SND_F_CTLS
};

static struct virtio_driver virtsnd_driver = {
	.driver.name = KBUILD_MODNAME,
	.id_table = id_table,
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.validate = virtsnd_validate,
	.probe = virtsnd_probe,
	.remove = virtsnd_remove,
#ifdef CONFIG_PM_SLEEP
	.freeze = virtsnd_freeze,
	.restore = virtsnd_restore,
#endif
};

module_virtio_driver(virtsnd_driver);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio sound card driver");
MODULE_LICENSE("GPL");
