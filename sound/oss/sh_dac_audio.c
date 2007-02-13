/*
 * sound/oss/sh_dac_audio.c
 *
 * SH DAC based sound :(
 *
 *  Copyright (C) 2004,2005  Andriy Skulysh
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/linkage.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/delay.h>
#include <asm/clock.h>
#include <asm/cpu/dac.h>
#include <asm/cpu/timer.h>
#include <asm/machvec.h>
#include <asm/hp6xx.h>
#include <asm/hd64461.h>

#define MODNAME "sh_dac_audio"

#define TMU_TOCR_INIT	0x00

#define TMU1_TCR_INIT	0x0020	/* Clock/4, rising edge; interrupt on */
#define TMU1_TSTR_INIT  0x02	/* Bit to turn on TMU1 */

#define BUFFER_SIZE 48000

static int rate;
static int empty;
static char *data_buffer, *buffer_begin, *buffer_end;
static int in_use, device_major;

static void dac_audio_start_timer(void)
{
	u8 tstr;

	tstr = ctrl_inb(TMU_TSTR);
	tstr |= TMU1_TSTR_INIT;
	ctrl_outb(tstr, TMU_TSTR);
}

static void dac_audio_stop_timer(void)
{
	u8 tstr;

	tstr = ctrl_inb(TMU_TSTR);
	tstr &= ~TMU1_TSTR_INIT;
	ctrl_outb(tstr, TMU_TSTR);
}

static void dac_audio_reset(void)
{
	dac_audio_stop_timer();
	buffer_begin = buffer_end = data_buffer;
	empty = 1;
}

static void dac_audio_sync(void)
{
	while (!empty)
		schedule();
}

static void dac_audio_start(void)
{
	if (mach_is_hp6xx()) {
		u16 v = inw(HD64461_GPADR);
		v &= ~HD64461_GPADR_SPEAKER;
		outw(v, HD64461_GPADR);
	}

	sh_dac_enable(CONFIG_SOUND_SH_DAC_AUDIO_CHANNEL);
	ctrl_outw(TMU1_TCR_INIT, TMU1_TCR);
}
static void dac_audio_stop(void)
{
	dac_audio_stop_timer();

	if (mach_is_hp6xx()) {
		u16 v = inw(HD64461_GPADR);
		v |= HD64461_GPADR_SPEAKER;
		outw(v, HD64461_GPADR);
	}

 	sh_dac_output(0, CONFIG_SOUND_SH_DAC_AUDIO_CHANNEL);
	sh_dac_disable(CONFIG_SOUND_SH_DAC_AUDIO_CHANNEL);
}

static void dac_audio_set_rate(void)
{
	unsigned long interval;
 	struct clk *clk;

 	clk = clk_get("module_clk");
 	interval = (clk_get_rate(clk) / 4) / rate;
 	clk_put(clk);
	ctrl_outl(interval, TMU1_TCOR);
	ctrl_outl(interval, TMU1_TCNT);
}

static int dac_audio_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	int val;

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		dac_audio_sync();
		return 0;

	case SNDCTL_DSP_RESET:
		dac_audio_reset();
		return 0;

	case SNDCTL_DSP_GETFMTS:
		return put_user(AFMT_U8, (int *)arg);

	case SNDCTL_DSP_SETFMT:
		return put_user(AFMT_U8, (int *)arg);

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return 0;

	case SOUND_PCM_WRITE_RATE:
		val = *(int *)arg;
		if (val > 0) {
			rate = val;
			dac_audio_set_rate();
		}
		return put_user(rate, (int *)arg);

	case SNDCTL_DSP_STEREO:
		return put_user(0, (int *)arg);

	case SOUND_PCM_WRITE_CHANNELS:
		return put_user(1, (int *)arg);

	case SNDCTL_DSP_SETDUPLEX:
		return -EINVAL;

	case SNDCTL_DSP_PROFILE:
		return -EINVAL;

	case SNDCTL_DSP_GETBLKSIZE:
		return put_user(BUFFER_SIZE, (int *)arg);

	case SNDCTL_DSP_SETFRAGMENT:
		return 0;

	default:
		printk(KERN_ERR "sh_dac_audio: unimplemented ioctl=0x%x\n",
		       cmd);
		return -EINVAL;
	}
	return -EINVAL;
}

static ssize_t dac_audio_write(struct file *file, const char *buf, size_t count,
			       loff_t * ppos)
{
	int free;
	int nbytes;

	if (count < 0)
		return -EINVAL;

	if (!count) {
		dac_audio_sync();
		return 0;
	}

	free = buffer_begin - buffer_end;

	if (free < 0)
		free += BUFFER_SIZE;
	if ((free == 0) && (empty))
		free = BUFFER_SIZE;
	if (count > free)
		count = free;
	if (buffer_begin > buffer_end) {
		if (copy_from_user((void *)buffer_end, buf, count))
			return -EFAULT;

		buffer_end += count;
	} else {
		nbytes = data_buffer + BUFFER_SIZE - buffer_end;
		if (nbytes > count) {
			if (copy_from_user((void *)buffer_end, buf, count))
				return -EFAULT;
			buffer_end += count;
		} else {
			if (copy_from_user((void *)buffer_end, buf, nbytes))
				return -EFAULT;
			if (copy_from_user
			    ((void *)data_buffer, buf + nbytes, count - nbytes))
				return -EFAULT;
			buffer_end = data_buffer + count - nbytes;
		}
	}

	if (empty) {
		empty = 0;
		dac_audio_start_timer();
	}

	return count;
}

static ssize_t dac_audio_read(struct file *file, char *buf, size_t count,
			      loff_t * ppos)
{
	return -EINVAL;
}

static int dac_audio_open(struct inode *inode, struct file *file)
{
	if (file->f_mode & FMODE_READ)
		return -ENODEV;
	if (in_use)
		return -EBUSY;

	in_use = 1;

	dac_audio_start();

	return 0;
}

static int dac_audio_release(struct inode *inode, struct file *file)
{
	dac_audio_sync();
	dac_audio_stop();
	in_use = 0;

	return 0;
}

const struct file_operations dac_audio_fops = {
      .read =		dac_audio_read,
      .write =	dac_audio_write,
      .ioctl =	dac_audio_ioctl,
      .open =		dac_audio_open,
      .release =	dac_audio_release,
};

static irqreturn_t timer1_interrupt(int irq, void *dev)
{
	unsigned long timer_status;

	timer_status = ctrl_inw(TMU1_TCR);
	timer_status &= ~0x100;
	ctrl_outw(timer_status, TMU1_TCR);

	if (!empty) {
		sh_dac_output(*buffer_begin, CONFIG_SOUND_SH_DAC_AUDIO_CHANNEL);
		buffer_begin++;

		if (buffer_begin == data_buffer + BUFFER_SIZE)
			buffer_begin = data_buffer;
		if (buffer_begin == buffer_end) {
			empty = 1;
			dac_audio_stop_timer();
		}
	}
	return IRQ_HANDLED;
}

static int __init dac_audio_init(void)
{
	int retval;

	if ((device_major = register_sound_dsp(&dac_audio_fops, -1)) < 0) {
		printk(KERN_ERR "Cannot register dsp device");
		return device_major;
	}

	in_use = 0;

	data_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
	if (data_buffer == NULL)
		return -ENOMEM;

	dac_audio_reset();
	rate = 8000;
	dac_audio_set_rate();

	retval =
	    request_irq(TIMER1_IRQ, timer1_interrupt, IRQF_DISABLED, MODNAME, 0);
	if (retval < 0) {
		printk(KERN_ERR "sh_dac_audio: IRQ %d request failed\n",
		       TIMER1_IRQ);
		return retval;
	}

	return 0;
}

static void __exit dac_audio_exit(void)
{
	free_irq(TIMER1_IRQ, 0);

	unregister_sound_dsp(device_major);
	kfree((void *)data_buffer);
}

module_init(dac_audio_init);
module_exit(dac_audio_exit);

MODULE_AUTHOR("Andriy Skulysh, askulysh@image.kiev.ua");
MODULE_DESCRIPTION("SH DAC sound driver");
MODULE_LICENSE("GPL");
