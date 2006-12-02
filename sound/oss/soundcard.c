/*
 * linux/sound/oss/soundcard.c
 *
 * Sound card driver for Linux
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer     : ioctl code reworked (vmalloc/vfree removed)
 *                   integrated sound_switch.c
 * Stefan Reinauer   : integrated /proc/sound (equals to /dev/sndstat,
 *                   which should disappear in the near future)
 * Eric Dumas	     : devfs support (22-Jan-98) <dumas@linux.eu.org> with
 *                   fixups by C. Scott Ananian <cananian@alumni.princeton.edu>
 * Richard Gooch     : moved common (non OSS-specific) devices to sound_core.c
 * Rob Riggs	     : Added persistent DMA buffers support (1998/10/17)
 * Christoph Hellwig : Some cleanup work (2000/03/01)
 */


#include "sound_config.h"
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include <linux/kmod.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/major.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/module.h>

/*
 * This ought to be moved into include/asm/dma.h
 */
#ifndef valid_dma
#define valid_dma(n) ((n) >= 0 && (n) < MAX_DMA_CHANNELS && (n) != 4)
#endif

/*
 * Table for permanently allocated memory (used when unloading the module)
 */
void *          sound_mem_blocks[1024];
int             sound_nblocks = 0;

/* Persistent DMA buffers */
#ifdef CONFIG_SOUND_DMAP
int             sound_dmap_flag = 1;
#else
int             sound_dmap_flag = 0;
#endif

static char     dma_alloc_map[MAX_DMA_CHANNELS];

#define DMA_MAP_UNAVAIL		0
#define DMA_MAP_FREE		1
#define DMA_MAP_BUSY		2


unsigned long seq_time = 0;	/* Time for /dev/sequencer */
extern struct class *sound_class;

/*
 * Table for configurable mixer volume handling
 */
static mixer_vol_table mixer_vols[MAX_MIXER_DEV];
static int num_mixer_volumes;

int *load_mixer_volumes(char *name, int *levels, int present)
{
	int             i, n;

	for (i = 0; i < num_mixer_volumes; i++) {
		if (strcmp(name, mixer_vols[i].name) == 0) {
			if (present)
				mixer_vols[i].num = i;
			return mixer_vols[i].levels;
		}
	}
	if (num_mixer_volumes >= MAX_MIXER_DEV) {
		printk(KERN_ERR "Sound: Too many mixers (%s)\n", name);
		return levels;
	}
	n = num_mixer_volumes++;

	strcpy(mixer_vols[n].name, name);

	if (present)
		mixer_vols[n].num = n;
	else
		mixer_vols[n].num = -1;

	for (i = 0; i < 32; i++)
		mixer_vols[n].levels[i] = levels[i];
	return mixer_vols[n].levels;
}
EXPORT_SYMBOL(load_mixer_volumes);

static int set_mixer_levels(void __user * arg)
{
        /* mixer_vol_table is 174 bytes, so IMHO no reason to not allocate it on the stack */
	mixer_vol_table buf;   

	if (__copy_from_user(&buf, arg, sizeof(buf)))
		return -EFAULT;
	load_mixer_volumes(buf.name, buf.levels, 0);
	if (__copy_to_user(arg, &buf, sizeof(buf)))
		return -EFAULT;
	return 0;
}

static int get_mixer_levels(void __user * arg)
{
	int n;

	if (__get_user(n, (int __user *)(&(((mixer_vol_table __user *)arg)->num))))
		return -EFAULT;
	if (n < 0 || n >= num_mixer_volumes)
		return -EINVAL;
	if (__copy_to_user(arg, &mixer_vols[n], sizeof(mixer_vol_table)))
		return -EFAULT;
	return 0;
}

/* 4K page size but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE (3*1024)

static ssize_t sound_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	int dev = iminor(file->f_dentry->d_inode);
	int ret = -EINVAL;

	/*
	 *	The OSS drivers aren't remotely happy without this locking,
	 *	and unless someone fixes them when they are about to bite the
	 *	big one anyway, we might as well bandage here..
	 */
	 
	lock_kernel();
	
	DEB(printk("sound_read(dev=%d, count=%d)\n", dev, count));
	switch (dev & 0x0f) {
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		ret = audio_read(dev, file, buf, count);
		break;

	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		ret = sequencer_read(dev, file, buf, count);
		break;

	case SND_DEV_MIDIN:
		ret = MIDIbuf_read(dev, file, buf, count);
	}
	unlock_kernel();
	return ret;
}

static ssize_t sound_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	int dev = iminor(file->f_dentry->d_inode);
	int ret = -EINVAL;
	
	lock_kernel();
	DEB(printk("sound_write(dev=%d, count=%d)\n", dev, count));
	switch (dev & 0x0f) {
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		ret =  sequencer_write(dev, file, buf, count);
		break;

	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		ret = audio_write(dev, file, buf, count);
		break;

	case SND_DEV_MIDIN:
		ret =  MIDIbuf_write(dev, file, buf, count);
		break;
	}
	unlock_kernel();
	return ret;
}

static int sound_open(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);
	int retval;

	DEB(printk("sound_open(dev=%d)\n", dev));
	if ((dev >= SND_NDEVS) || (dev < 0)) {
		printk(KERN_ERR "Invalid minor device %d\n", dev);
		return -ENXIO;
	}
	switch (dev & 0x0f) {
	case SND_DEV_CTL:
		dev >>= 4;
		if (dev >= 0 && dev < MAX_MIXER_DEV && mixer_devs[dev] == NULL) {
			request_module("mixer%d", dev);
		}
		if (dev && (dev >= num_mixers || mixer_devs[dev] == NULL))
			return -ENXIO;
	
		if (!try_module_get(mixer_devs[dev]->owner))
			return -ENXIO;
		break;

	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		if ((retval = sequencer_open(dev, file)) < 0)
			return retval;
		break;

	case SND_DEV_MIDIN:
		if ((retval = MIDIbuf_open(dev, file)) < 0)
			return retval;
		break;

	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		if ((retval = audio_open(dev, file)) < 0)
			return retval;
		break;

	default:
		printk(KERN_ERR "Invalid minor device %d\n", dev);
		return -ENXIO;
	}

	return 0;
}

static int sound_release(struct inode *inode, struct file *file)
{
	int dev = iminor(inode);

	lock_kernel();
	DEB(printk("sound_release(dev=%d)\n", dev));
	switch (dev & 0x0f) {
	case SND_DEV_CTL:
		module_put(mixer_devs[dev >> 4]->owner);
		break;
		
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		sequencer_release(dev, file);
		break;

	case SND_DEV_MIDIN:
		MIDIbuf_release(dev, file);
		break;

	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		audio_release(dev, file);
		break;

	default:
		printk(KERN_ERR "Sound error: Releasing unknown device 0x%02x\n", dev);
	}
	unlock_kernel();

	return 0;
}

static int get_mixer_info(int dev, void __user *arg)
{
	mixer_info info;
	memset(&info, 0, sizeof(info));
	strlcpy(info.id, mixer_devs[dev]->id, sizeof(info.id));
	strlcpy(info.name, mixer_devs[dev]->name, sizeof(info.name));
	info.modify_counter = mixer_devs[dev]->modify_counter;
	if (__copy_to_user(arg, &info,  sizeof(info)))
		return -EFAULT;
	return 0;
}

static int get_old_mixer_info(int dev, void __user *arg)
{
	_old_mixer_info info;
	memset(&info, 0, sizeof(info));
 	strlcpy(info.id, mixer_devs[dev]->id, sizeof(info.id));
 	strlcpy(info.name, mixer_devs[dev]->name, sizeof(info.name));
 	if (copy_to_user(arg, &info,  sizeof(info)))
		return -EFAULT;
	return 0;
}

static int sound_mixer_ioctl(int mixdev, unsigned int cmd, void __user *arg)
{
 	if (mixdev < 0 || mixdev >= MAX_MIXER_DEV)
 		return -ENXIO;
 	/* Try to load the mixer... */
 	if (mixer_devs[mixdev] == NULL) {
 		request_module("mixer%d", mixdev);
 	}
 	if (mixdev >= num_mixers || !mixer_devs[mixdev])
 		return -ENXIO;
	if (cmd == SOUND_MIXER_INFO)
		return get_mixer_info(mixdev, arg);
	if (cmd == SOUND_OLD_MIXER_INFO)
		return get_old_mixer_info(mixdev, arg);
	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		mixer_devs[mixdev]->modify_counter++;
	if (!mixer_devs[mixdev]->ioctl)
		return -EINVAL;
	return mixer_devs[mixdev]->ioctl(mixdev, cmd, arg);
}

static int sound_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	int len = 0, dtype;
	int dev = iminor(inode);
	void __user *p = (void __user *)arg;

	if (_SIOC_DIR(cmd) != _SIOC_NONE && _SIOC_DIR(cmd) != 0) {
		/*
		 * Have to validate the address given by the process.
		 */
		len = _SIOC_SIZE(cmd);
		if (len < 1 || len > 65536 || !p)
			return -EFAULT;
		if (_SIOC_DIR(cmd) & _SIOC_WRITE)
			if (!access_ok(VERIFY_READ, p, len))
				return -EFAULT;
		if (_SIOC_DIR(cmd) & _SIOC_READ)
			if (!access_ok(VERIFY_WRITE, p, len))
				return -EFAULT;
	}
	DEB(printk("sound_ioctl(dev=%d, cmd=0x%x, arg=0x%x)\n", dev, cmd, arg));
	if (cmd == OSS_GETVERSION)
		return __put_user(SOUND_VERSION, (int __user *)p);
	
	if (_IOC_TYPE(cmd) == 'M' && num_mixers > 0 &&   /* Mixer ioctl */
	    (dev & 0x0f) != SND_DEV_CTL) {              
		dtype = dev & 0x0f;
		switch (dtype) {
		case SND_DEV_DSP:
		case SND_DEV_DSP16:
		case SND_DEV_AUDIO:
			return sound_mixer_ioctl(audio_devs[dev >> 4]->mixer_dev,
						 cmd, p);
			
		default:
			return sound_mixer_ioctl(dev >> 4, cmd, p);
		}
	}
	switch (dev & 0x0f) {
	case SND_DEV_CTL:
		if (cmd == SOUND_MIXER_GETLEVELS)
			return get_mixer_levels(p);
		if (cmd == SOUND_MIXER_SETLEVELS)
			return set_mixer_levels(p);
		return sound_mixer_ioctl(dev >> 4, cmd, p);

	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_ioctl(dev, file, cmd, p);

	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return audio_ioctl(dev, file, cmd, p);
		break;

	case SND_DEV_MIDIN:
		return MIDIbuf_ioctl(dev, file, cmd, p);
		break;

	}
	return -EINVAL;
}

static unsigned int sound_poll(struct file *file, poll_table * wait)
{
	struct inode *inode = file->f_dentry->d_inode;
	int dev = iminor(inode);

	DEB(printk("sound_poll(dev=%d)\n", dev));
	switch (dev & 0x0f) {
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_poll(dev, file, wait);

	case SND_DEV_MIDIN:
		return MIDIbuf_poll(dev, file, wait);

	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return DMAbuf_poll(file, dev >> 4, wait);
	}
	return 0;
}

static int sound_mmap(struct file *file, struct vm_area_struct *vma)
{
	int dev_class;
	unsigned long size;
	struct dma_buffparms *dmap = NULL;
	int dev = iminor(file->f_dentry->d_inode);

	dev_class = dev & 0x0f;
	dev >>= 4;

	if (dev_class != SND_DEV_DSP && dev_class != SND_DEV_DSP16 && dev_class != SND_DEV_AUDIO) {
		printk(KERN_ERR "Sound: mmap() not supported for other than audio devices\n");
		return -EINVAL;
	}
	lock_kernel();
	if (vma->vm_flags & VM_WRITE)	/* Map write and read/write to the output buf */
		dmap = audio_devs[dev]->dmap_out;
	else if (vma->vm_flags & VM_READ)
		dmap = audio_devs[dev]->dmap_in;
	else {
		printk(KERN_ERR "Sound: Undefined mmap() access\n");
		unlock_kernel();
		return -EINVAL;
	}

	if (dmap == NULL) {
		printk(KERN_ERR "Sound: mmap() error. dmap == NULL\n");
		unlock_kernel();
		return -EIO;
	}
	if (dmap->raw_buf == NULL) {
		printk(KERN_ERR "Sound: mmap() called when raw_buf == NULL\n");
		unlock_kernel();
		return -EIO;
	}
	if (dmap->mapping_flags) {
		printk(KERN_ERR "Sound: mmap() called twice for the same DMA buffer\n");
		unlock_kernel();
		return -EIO;
	}
	if (vma->vm_pgoff != 0) {
		printk(KERN_ERR "Sound: mmap() offset must be 0.\n");
		unlock_kernel();
		return -EINVAL;
	}
	size = vma->vm_end - vma->vm_start;

	if (size != dmap->bytes_in_use) {
		printk(KERN_WARNING "Sound: mmap() size = %ld. Should be %d\n", size, dmap->bytes_in_use);
	}
	if (remap_pfn_range(vma, vma->vm_start,
			virt_to_phys(dmap->raw_buf) >> PAGE_SHIFT,
			vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		unlock_kernel();
		return -EAGAIN;
	}

	dmap->mapping_flags |= DMA_MAP_MAPPED;

	if( audio_devs[dev]->d->mmap)
		audio_devs[dev]->d->mmap(dev);

	memset(dmap->raw_buf,
	       dmap->neutral_byte,
	       dmap->bytes_in_use);
	unlock_kernel();
	return 0;
}

struct file_operations oss_sound_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.read		= sound_read,
	.write		= sound_write,
	.poll		= sound_poll,
	.ioctl		= sound_ioctl,
	.mmap		= sound_mmap,
	.open		= sound_open,
	.release	= sound_release,
};

/*
 *	Create the required special subdevices
 */
 
static int create_special_devices(void)
{
	int seq1,seq2;
	seq1=register_sound_special(&oss_sound_fops, 1);
	if(seq1==-1)
		goto bad;
	seq2=register_sound_special(&oss_sound_fops, 8);
	if(seq2!=-1)
		return 0;
	unregister_sound_special(1);
bad:
	return -1;
}


/* These device names follow the official Linux device list,
 * Documentation/devices.txt.  Let us know if there are other
 * common names we should support for compatibility.
 * Only those devices not created by the generic code in sound_core.c are
 * registered here.
 */
static const struct {
	unsigned short minor;
	char *name;
	umode_t mode;
	int *num;
} dev_list[] = { /* list of minor devices */
/* seems to be some confusion here -- this device is not in the device list */
	{SND_DEV_DSP16,     "dspW",	 S_IWUGO | S_IRUSR | S_IRGRP,
	 &num_audiodevs},
	{SND_DEV_AUDIO,     "audio",	 S_IWUGO | S_IRUSR | S_IRGRP,
	 &num_audiodevs},
};

static int dmabuf;
static int dmabug;

module_param(dmabuf, int, 0444);
module_param(dmabug, int, 0444);

static int __init oss_init(void)
{
	int             err;
	int i, j;
	
#ifdef CONFIG_PCI
	if(dmabug)
		isa_dma_bridge_buggy = dmabug;
#endif

	err = create_special_devices();
	if (err) {
		printk(KERN_ERR "sound: driver already loaded/included in kernel\n");
		return err;
	}

	/* Protecting the innocent */
	sound_dmap_flag = (dmabuf > 0 ? 1 : 0);

	for (i = 0; i < sizeof (dev_list) / sizeof *dev_list; i++) {
		device_create(sound_class, NULL,
			      MKDEV(SOUND_MAJOR, dev_list[i].minor),
			      "%s", dev_list[i].name);

		if (!dev_list[i].num)
			continue;

		for (j = 1; j < *dev_list[i].num; j++)
			device_create(sound_class, NULL,
				      MKDEV(SOUND_MAJOR, dev_list[i].minor + (j*0x10)),
				      "%s%d", dev_list[i].name, j);
	}

	if (sound_nblocks >= 1024)
		printk(KERN_ERR "Sound warning: Deallocation table was too small.\n");
	
	return 0;
}

static void __exit oss_cleanup(void)
{
	int i, j;

	for (i = 0; i < sizeof (dev_list) / sizeof *dev_list; i++) {
		device_destroy(sound_class, MKDEV(SOUND_MAJOR, dev_list[i].minor));
		if (!dev_list[i].num)
			continue;
		for (j = 1; j < *dev_list[i].num; j++)
			device_destroy(sound_class, MKDEV(SOUND_MAJOR, dev_list[i].minor + (j*0x10)));
	}
	
	unregister_sound_special(1);
	unregister_sound_special(8);

	sound_stop_timer();

	sequencer_unload();

	for (i = 0; i < MAX_DMA_CHANNELS; i++)
		if (dma_alloc_map[i] != DMA_MAP_UNAVAIL) {
			printk(KERN_ERR "Sound: Hmm, DMA%d was left allocated - fixed\n", i);
			sound_free_dma(i);
		}

	for (i = 0; i < sound_nblocks; i++)
		vfree(sound_mem_blocks[i]);

}

module_init(oss_init);
module_exit(oss_cleanup);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OSS Sound subsystem");
MODULE_AUTHOR("Hannu Savolainen, et al.");


int sound_alloc_dma(int chn, char *deviceID)
{
	int err;

	if ((err = request_dma(chn, deviceID)) != 0)
		return err;

	dma_alloc_map[chn] = DMA_MAP_FREE;

	return 0;
}
EXPORT_SYMBOL(sound_alloc_dma);

int sound_open_dma(int chn, char *deviceID)
{
	if (!valid_dma(chn)) {
		printk(KERN_ERR "sound_open_dma: Invalid DMA channel %d\n", chn);
		return 1;
	}

	if (dma_alloc_map[chn] != DMA_MAP_FREE) {
		printk("sound_open_dma: DMA channel %d busy or not allocated (%d)\n", chn, dma_alloc_map[chn]);
		return 1;
	}
	dma_alloc_map[chn] = DMA_MAP_BUSY;
	return 0;
}
EXPORT_SYMBOL(sound_open_dma);

void sound_free_dma(int chn)
{
	if (dma_alloc_map[chn] == DMA_MAP_UNAVAIL) {
		/* printk( "sound_free_dma: Bad access to DMA channel %d\n",  chn); */
		return;
	}
	free_dma(chn);
	dma_alloc_map[chn] = DMA_MAP_UNAVAIL;
}
EXPORT_SYMBOL(sound_free_dma);

void sound_close_dma(int chn)
{
	if (dma_alloc_map[chn] != DMA_MAP_BUSY) {
		printk(KERN_ERR "sound_close_dma: Bad access to DMA channel %d\n", chn);
		return;
	}
	dma_alloc_map[chn] = DMA_MAP_FREE;
}
EXPORT_SYMBOL(sound_close_dma);

static void do_sequencer_timer(unsigned long dummy)
{
	sequencer_timer(0);
}


static DEFINE_TIMER(seq_timer, do_sequencer_timer, 0, 0);

void request_sound_timer(int count)
{
	extern unsigned long seq_time;

	if (count < 0) {
		seq_timer.expires = (-count) + jiffies;
		add_timer(&seq_timer);
		return;
	}
	count += seq_time;

	count -= jiffies;

	if (count < 1)
		count = 1;

	seq_timer.expires = (count) + jiffies;
	add_timer(&seq_timer);
}

void sound_stop_timer(void)
{
	del_timer(&seq_timer);
}

void conf_printf(char *name, struct address_info *hw_config)
{
#ifndef CONFIG_SOUND_TRACEINIT
	return;
#else
	printk("<%s> at 0x%03x", name, hw_config->io_base);

	if (hw_config->irq)
		printk(" irq %d", (hw_config->irq > 0) ? hw_config->irq : -hw_config->irq);

	if (hw_config->dma != -1 || hw_config->dma2 != -1)
	{
		printk(" dma %d", hw_config->dma);
		if (hw_config->dma2 != -1)
			printk(",%d", hw_config->dma2);
	}
	printk("\n");
#endif
}
EXPORT_SYMBOL(conf_printf);

void conf_printf2(char *name, int base, int irq, int dma, int dma2)
{
#ifndef CONFIG_SOUND_TRACEINIT
	return;
#else
	printk("<%s> at 0x%03x", name, base);

	if (irq)
		printk(" irq %d", (irq > 0) ? irq : -irq);

	if (dma != -1 || dma2 != -1)
	{
		  printk(" dma %d", dma);
		  if (dma2 != -1)
			  printk(",%d", dma2);
	}
	printk("\n");
#endif
}
EXPORT_SYMBOL(conf_printf2);

