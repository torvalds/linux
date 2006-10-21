/*
 * sound/oss/midibuf.c
 *
 * Device file manager for /dev/midi#
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 */
#include <linux/stddef.h>
#include <linux/kmod.h>
#include <linux/spinlock.h>
#define MIDIBUF_C

#include "sound_config.h"


/*
 * Don't make MAX_QUEUE_SIZE larger than 4000
 */

#define MAX_QUEUE_SIZE	4000

static wait_queue_head_t midi_sleeper[MAX_MIDI_DEV];
static wait_queue_head_t input_sleeper[MAX_MIDI_DEV];

struct midi_buf
{
	int len, head, tail;
	unsigned char queue[MAX_QUEUE_SIZE];
};

struct midi_parms
{
	long prech_timeout;	/*
				 * Timeout before the first ch
				 */
};

static struct midi_buf *midi_out_buf[MAX_MIDI_DEV] = {NULL};
static struct midi_buf *midi_in_buf[MAX_MIDI_DEV] = {NULL};
static struct midi_parms parms[MAX_MIDI_DEV];

static void midi_poll(unsigned long dummy);


static DEFINE_TIMER(poll_timer, midi_poll, 0, 0);

static volatile int open_devs;
static DEFINE_SPINLOCK(lock);

#define DATA_AVAIL(q) (q->len)
#define SPACE_AVAIL(q) (MAX_QUEUE_SIZE - q->len)

#define QUEUE_BYTE(q, data) \
	if (SPACE_AVAIL(q)) \
	{ \
	  unsigned long flags; \
	  spin_lock_irqsave(&lock, flags); \
	  q->queue[q->tail] = (data); \
	  q->len++; q->tail = (q->tail+1) % MAX_QUEUE_SIZE; \
	  spin_unlock_irqrestore(&lock, flags); \
	}

#define REMOVE_BYTE(q, data) \
	if (DATA_AVAIL(q)) \
	{ \
	  unsigned long flags; \
	  spin_lock_irqsave(&lock, flags); \
	  data = q->queue[q->head]; \
	  q->len--; q->head = (q->head+1) % MAX_QUEUE_SIZE; \
	  spin_unlock_irqrestore(&lock, flags); \
	}

static void drain_midi_queue(int dev)
{

	/*
	 * Give the Midi driver time to drain its output queues
	 */

	if (midi_devs[dev]->buffer_status != NULL)
		while (!signal_pending(current) && midi_devs[dev]->buffer_status(dev)) 
			interruptible_sleep_on_timeout(&midi_sleeper[dev],
						       HZ/10);
}

static void midi_input_intr(int dev, unsigned char data)
{
	if (midi_in_buf[dev] == NULL)
		return;

	if (data == 0xfe)	/*
				 * Active sensing
				 */
		return;		/*
				 * Ignore
				 */

	if (SPACE_AVAIL(midi_in_buf[dev])) {
		QUEUE_BYTE(midi_in_buf[dev], data);
		wake_up(&input_sleeper[dev]);
	}
}

static void midi_output_intr(int dev)
{
	/*
	 * Currently NOP
	 */
}

static void midi_poll(unsigned long dummy)
{
	unsigned long   flags;
	int             dev;

	spin_lock_irqsave(&lock, flags);
	if (open_devs)
	{
		for (dev = 0; dev < num_midis; dev++)
			if (midi_devs[dev] != NULL && midi_out_buf[dev] != NULL)
			{
				int ok = 1;

				while (DATA_AVAIL(midi_out_buf[dev]) && ok)
				{
					int c = midi_out_buf[dev]->queue[midi_out_buf[dev]->head];

					spin_unlock_irqrestore(&lock,flags);/* Give some time to others */
					ok = midi_devs[dev]->outputc(dev, c);
					spin_lock_irqsave(&lock, flags);
					midi_out_buf[dev]->head = (midi_out_buf[dev]->head + 1) % MAX_QUEUE_SIZE;
					midi_out_buf[dev]->len--;
				}

				if (DATA_AVAIL(midi_out_buf[dev]) < 100)
					wake_up(&midi_sleeper[dev]);
			}
		poll_timer.expires = (1) + jiffies;
		add_timer(&poll_timer);
		/*
		 * Come back later
		 */
	}
	spin_unlock_irqrestore(&lock, flags);
}

int MIDIbuf_open(int dev, struct file *file)
{
	int mode, err;

	dev = dev >> 4;
	mode = translate_mode(file);

	if (num_midis > MAX_MIDI_DEV)
	{
		printk(KERN_ERR "midi: Too many midi interfaces\n");
		num_midis = MAX_MIDI_DEV;
	}
	if (dev < 0 || dev >= num_midis || midi_devs[dev] == NULL)
		  return -ENXIO;
	/*
	 *    Interrupts disabled. Be careful
	 */

	module_put(midi_devs[dev]->owner);

	if ((err = midi_devs[dev]->open(dev, mode,
				 midi_input_intr, midi_output_intr)) < 0)
		return err;

	parms[dev].prech_timeout = MAX_SCHEDULE_TIMEOUT;
	midi_in_buf[dev] = (struct midi_buf *) vmalloc(sizeof(struct midi_buf));

	if (midi_in_buf[dev] == NULL)
	{
		printk(KERN_WARNING "midi: Can't allocate buffer\n");
		midi_devs[dev]->close(dev);
		return -EIO;
	}
	midi_in_buf[dev]->len = midi_in_buf[dev]->head = midi_in_buf[dev]->tail = 0;

	midi_out_buf[dev] = (struct midi_buf *) vmalloc(sizeof(struct midi_buf));

	if (midi_out_buf[dev] == NULL)
	{
		printk(KERN_WARNING "midi: Can't allocate buffer\n");
		midi_devs[dev]->close(dev);
		vfree(midi_in_buf[dev]);
		midi_in_buf[dev] = NULL;
		return -EIO;
	}
	midi_out_buf[dev]->len = midi_out_buf[dev]->head = midi_out_buf[dev]->tail = 0;
	open_devs++;

	init_waitqueue_head(&midi_sleeper[dev]);
	init_waitqueue_head(&input_sleeper[dev]);

	if (open_devs < 2)	/* This was first open */
	{
		poll_timer.expires = 1 + jiffies;
		add_timer(&poll_timer);	/* Start polling */
	}
	return err;
}

void MIDIbuf_release(int dev, struct file *file)
{
	int mode;

	dev = dev >> 4;
	mode = translate_mode(file);

	if (dev < 0 || dev >= num_midis || midi_devs[dev] == NULL)
		return;

	/*
	 * Wait until the queue is empty
	 */

	if (mode != OPEN_READ)
	{
		midi_devs[dev]->outputc(dev, 0xfe);	/*
							   * Active sensing to shut the
							   * devices
							 */

		while (!signal_pending(current) && DATA_AVAIL(midi_out_buf[dev]))
			  interruptible_sleep_on(&midi_sleeper[dev]);
		/*
		 *	Sync
		 */

		drain_midi_queue(dev);	/*
					 * Ensure the output queues are empty
					 */
	}

	midi_devs[dev]->close(dev);

	open_devs--;
	if (open_devs == 0)
		del_timer_sync(&poll_timer);
	vfree(midi_in_buf[dev]);
	vfree(midi_out_buf[dev]);
	midi_in_buf[dev] = NULL;
	midi_out_buf[dev] = NULL;

	module_put(midi_devs[dev]->owner);
}

int MIDIbuf_write(int dev, struct file *file, const char __user *buf, int count)
{
	int c, n, i;
	unsigned char tmp_data;

	dev = dev >> 4;

	if (!count)
		return 0;

	c = 0;

	while (c < count)
	{
		n = SPACE_AVAIL(midi_out_buf[dev]);

		if (n == 0) {	/*
				 * No space just now.
				 */

			if (file->f_flags & O_NONBLOCK) {
				c = -EAGAIN;
				goto out;
			}

			interruptible_sleep_on(&midi_sleeper[dev]);
			if (signal_pending(current)) 
			{
				c = -EINTR;
				goto out;
			}
			n = SPACE_AVAIL(midi_out_buf[dev]);
		}
		if (n > (count - c))
			n = count - c;

		for (i = 0; i < n; i++)
		{
			/* BROKE BROKE BROKE - CANT DO THIS WITH CLI !! */
			/* yes, think the same, so I removed the cli() brackets 
				QUEUE_BYTE is protected against interrupts */
			if (copy_from_user((char *) &tmp_data, &(buf)[c], 1)) {
				c = -EFAULT;
				goto out;
			}
			QUEUE_BYTE(midi_out_buf[dev], tmp_data);
			c++;
		}
	}
out:
	return c;
}


int MIDIbuf_read(int dev, struct file *file, char __user *buf, int count)
{
	int n, c = 0;
	unsigned char tmp_data;

	dev = dev >> 4;

	if (!DATA_AVAIL(midi_in_buf[dev])) {	/*
						 * No data yet, wait
						 */
 		if (file->f_flags & O_NONBLOCK) {
 			c = -EAGAIN;
			goto out;
 		}
		interruptible_sleep_on_timeout(&input_sleeper[dev],
					       parms[dev].prech_timeout);

		if (signal_pending(current))
			c = -EINTR;	/* The user is getting restless */
	}
	if (c == 0 && DATA_AVAIL(midi_in_buf[dev]))	/*
							 * Got some bytes
							 */
	{
		n = DATA_AVAIL(midi_in_buf[dev]);
		if (n > count)
			n = count;
		c = 0;

		while (c < n)
		{
			char *fixit;
			REMOVE_BYTE(midi_in_buf[dev], tmp_data);
			fixit = (char *) &tmp_data;
			/* BROKE BROKE BROKE */
			/* yes removed the cli() brackets again
			 should q->len,tail&head be atomic_t? */
			if (copy_to_user(&(buf)[c], fixit, 1)) {
				c = -EFAULT;
				goto out;
			}
			c++;
		}
	}
out:
	return c;
}

int MIDIbuf_ioctl(int dev, struct file *file,
		  unsigned int cmd, void __user *arg)
{
	int val;

	dev = dev >> 4;
	
	if (((cmd >> 8) & 0xff) == 'C') 
	{
		if (midi_devs[dev]->coproc)	/* Coprocessor ioctl */
			return midi_devs[dev]->coproc->ioctl(midi_devs[dev]->coproc->devc, cmd, arg, 0);
/*		printk("/dev/midi%d: No coprocessor for this device\n", dev);*/
		return -ENXIO;
	}
	else
	{
		switch (cmd) 
		{
			case SNDCTL_MIDI_PRETIME:
				if (get_user(val, (int __user *)arg))
					return -EFAULT;
				if (val < 0)
					val = 0;
				val = (HZ * val) / 10;
				parms[dev].prech_timeout = val;
				return put_user(val, (int __user *)arg);
			
			default:
				if (!midi_devs[dev]->ioctl)
					return -EINVAL;
				return midi_devs[dev]->ioctl(dev, cmd, arg);
		}
	}
}

/* No kernel lock - fine */
unsigned int MIDIbuf_poll(int dev, struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	dev = dev >> 4;

	/* input */
	poll_wait(file, &input_sleeper[dev], wait);
	if (DATA_AVAIL(midi_in_buf[dev]))
		mask |= POLLIN | POLLRDNORM;

	/* output */
	poll_wait(file, &midi_sleeper[dev], wait);
	if (!SPACE_AVAIL(midi_out_buf[dev]))
		mask |= POLLOUT | POLLWRNORM;
	
	return mask;
}


int MIDIbuf_avail(int dev)
{
	if (midi_in_buf[dev])
		return DATA_AVAIL (midi_in_buf[dev]);
	return 0;
}
EXPORT_SYMBOL(MIDIbuf_avail);

