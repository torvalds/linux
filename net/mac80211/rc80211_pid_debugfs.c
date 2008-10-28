/*
 * Copyright 2007, Mattias Nissler <mattias.nissler@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/skbuff.h>

#include <net/mac80211.h>
#include "rate.h"

#include "rc80211_pid.h"

static void rate_control_pid_event(struct rc_pid_event_buffer *buf,
				   enum rc_pid_event_type type,
				   union rc_pid_event_data *data)
{
	struct rc_pid_event *ev;
	unsigned long status;

	spin_lock_irqsave(&buf->lock, status);
	ev = &(buf->ring[buf->next_entry]);
	buf->next_entry = (buf->next_entry + 1) % RC_PID_EVENT_RING_SIZE;

	ev->timestamp = jiffies;
	ev->id = buf->ev_count++;
	ev->type = type;
	ev->data = *data;

	spin_unlock_irqrestore(&buf->lock, status);

	wake_up_all(&buf->waitqueue);
}

void rate_control_pid_event_tx_status(struct rc_pid_event_buffer *buf,
				      struct ieee80211_tx_info *stat)
{
	union rc_pid_event_data evd;

	memcpy(&evd.tx_status, stat, sizeof(struct ieee80211_tx_info));
	rate_control_pid_event(buf, RC_PID_EVENT_TYPE_TX_STATUS, &evd);
}

void rate_control_pid_event_rate_change(struct rc_pid_event_buffer *buf,
					       int index, int rate)
{
	union rc_pid_event_data evd;

	evd.index = index;
	evd.rate = rate;
	rate_control_pid_event(buf, RC_PID_EVENT_TYPE_RATE_CHANGE, &evd);
}

void rate_control_pid_event_tx_rate(struct rc_pid_event_buffer *buf,
					   int index, int rate)
{
	union rc_pid_event_data evd;

	evd.index = index;
	evd.rate = rate;
	rate_control_pid_event(buf, RC_PID_EVENT_TYPE_TX_RATE, &evd);
}

void rate_control_pid_event_pf_sample(struct rc_pid_event_buffer *buf,
					     s32 pf_sample, s32 prop_err,
					     s32 int_err, s32 der_err)
{
	union rc_pid_event_data evd;

	evd.pf_sample = pf_sample;
	evd.prop_err = prop_err;
	evd.int_err = int_err;
	evd.der_err = der_err;
	rate_control_pid_event(buf, RC_PID_EVENT_TYPE_PF_SAMPLE, &evd);
}

static int rate_control_pid_events_open(struct inode *inode, struct file *file)
{
	struct rc_pid_sta_info *sinfo = inode->i_private;
	struct rc_pid_event_buffer *events = &sinfo->events;
	struct rc_pid_events_file_info *file_info;
	unsigned long status;

	/* Allocate a state struct */
	file_info = kmalloc(sizeof(*file_info), GFP_KERNEL);
	if (file_info == NULL)
		return -ENOMEM;

	spin_lock_irqsave(&events->lock, status);

	file_info->next_entry = events->next_entry;
	file_info->events = events;

	spin_unlock_irqrestore(&events->lock, status);

	file->private_data = file_info;

	return 0;
}

static int rate_control_pid_events_release(struct inode *inode,
					   struct file *file)
{
	struct rc_pid_events_file_info *file_info = file->private_data;

	kfree(file_info);

	return 0;
}

static unsigned int rate_control_pid_events_poll(struct file *file,
						 poll_table *wait)
{
	struct rc_pid_events_file_info *file_info = file->private_data;

	poll_wait(file, &file_info->events->waitqueue, wait);

	return POLLIN | POLLRDNORM;
}

#define RC_PID_PRINT_BUF_SIZE 64

static ssize_t rate_control_pid_events_read(struct file *file, char __user *buf,
					    size_t length, loff_t *offset)
{
	struct rc_pid_events_file_info *file_info = file->private_data;
	struct rc_pid_event_buffer *events = file_info->events;
	struct rc_pid_event *ev;
	char pb[RC_PID_PRINT_BUF_SIZE];
	int ret;
	int p;
	unsigned long status;

	/* Check if there is something to read. */
	if (events->next_entry == file_info->next_entry) {
		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		/* Wait */
		ret = wait_event_interruptible(events->waitqueue,
				events->next_entry != file_info->next_entry);

		if (ret)
			return ret;
	}

	/* Write out one event per call. I don't care whether it's a little
	 * inefficient, this is debugging code anyway. */
	spin_lock_irqsave(&events->lock, status);

	/* Get an event */
	ev = &(events->ring[file_info->next_entry]);
	file_info->next_entry = (file_info->next_entry + 1) %
				RC_PID_EVENT_RING_SIZE;

	/* Print information about the event. Note that userpace needs to
	 * provide large enough buffers. */
	length = length < RC_PID_PRINT_BUF_SIZE ?
		 length : RC_PID_PRINT_BUF_SIZE;
	p = snprintf(pb, length, "%u %lu ", ev->id, ev->timestamp);
	switch (ev->type) {
	case RC_PID_EVENT_TYPE_TX_STATUS:
		p += snprintf(pb + p, length - p, "tx_status %u %u",
			      ev->data.tx_status.status.excessive_retries,
			      ev->data.tx_status.status.retry_count);
		break;
	case RC_PID_EVENT_TYPE_RATE_CHANGE:
		p += snprintf(pb + p, length - p, "rate_change %d %d",
			      ev->data.index, ev->data.rate);
		break;
	case RC_PID_EVENT_TYPE_TX_RATE:
		p += snprintf(pb + p, length - p, "tx_rate %d %d",
			      ev->data.index, ev->data.rate);
		break;
	case RC_PID_EVENT_TYPE_PF_SAMPLE:
		p += snprintf(pb + p, length - p,
			      "pf_sample %d %d %d %d",
			      ev->data.pf_sample, ev->data.prop_err,
			      ev->data.int_err, ev->data.der_err);
		break;
	}
	p += snprintf(pb + p, length - p, "\n");

	spin_unlock_irqrestore(&events->lock, status);

	if (copy_to_user(buf, pb, p))
		return -EFAULT;

	return p;
}

#undef RC_PID_PRINT_BUF_SIZE

static struct file_operations rc_pid_fop_events = {
	.owner = THIS_MODULE,
	.read = rate_control_pid_events_read,
	.poll = rate_control_pid_events_poll,
	.open = rate_control_pid_events_open,
	.release = rate_control_pid_events_release,
};

void rate_control_pid_add_sta_debugfs(void *priv, void *priv_sta,
					     struct dentry *dir)
{
	struct rc_pid_sta_info *spinfo = priv_sta;

	spinfo->events_entry = debugfs_create_file("rc_pid_events", S_IRUGO,
						   dir, spinfo,
						   &rc_pid_fop_events);
}

void rate_control_pid_remove_sta_debugfs(void *priv, void *priv_sta)
{
	struct rc_pid_sta_info *spinfo = priv_sta;

	debugfs_remove(spinfo->events_entry);
}
