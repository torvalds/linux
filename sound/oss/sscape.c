/*
 * sound/oss/sscape.c
 *
 * Low level driver for Ensoniq SoundScape
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer   	: ioctl code reworked (vmalloc/vfree removed)
 * Sergey Smitienko	: ensoniq p'n'p support
 * Christoph Hellwig	: adapted to module_init/module_exit
 * Bartlomiej Zolnierkiewicz : added __init to attach_sscape()
 * Chris Rankin		: Specify that this module owns the coprocessor
 * Arnaldo C. de Melo	: added missing restore_flags in sscape_pnp_upload_file
 */

#include <linux/init.h>
#include <linux/module.h>

#include "sound_config.h"
#include "sound_firmware.h"

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
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/spinlock.h>

#include "coproc.h"

#include "ad1848.h"
#include "mpu401.h"

/*
 *    I/O ports
 */
#define MIDI_DATA       0
#define MIDI_CTRL       1
#define HOST_CTRL       2
#define TX_READY	0x02
#define RX_READY	0x01
#define HOST_DATA       3
#define ODIE_ADDR       4
#define ODIE_DATA       5

/*
 *    Indirect registers
 */

#define GA_INTSTAT_REG	0
#define GA_INTENA_REG	1
#define GA_DMAA_REG	2
#define GA_DMAB_REG	3
#define GA_INTCFG_REG	4
#define GA_DMACFG_REG	5
#define GA_CDCFG_REG	6
#define GA_SMCFGA_REG	7
#define GA_SMCFGB_REG	8
#define GA_HMCTL_REG	9

/*
 * DMA channel identifiers (A and B)
 */

#define SSCAPE_DMA_A	0
#define SSCAPE_DMA_B	1

#define PORT(name)	(devc->base+name)

/*
 * Host commands recognized by the OBP microcode
 */
 
#define CMD_GEN_HOST_ACK	0x80
#define CMD_GEN_MPU_ACK		0x81
#define CMD_GET_BOARD_TYPE	0x82
#define CMD_SET_CONTROL		0x88	/* Old firmware only */
#define CMD_GET_CONTROL		0x89	/* Old firmware only */
#define CTL_MASTER_VOL		0
#define CTL_MIC_MODE		2
#define CTL_SYNTH_VOL		4
#define CTL_WAVE_VOL		7
#define CMD_SET_EXTMIDI		0x8a
#define CMD_GET_EXTMIDI		0x8b
#define CMD_SET_MT32		0x8c
#define CMD_GET_MT32		0x8d

#define CMD_ACK			0x80

#define	IC_ODIE			1
#define	IC_OPUS			2

typedef struct sscape_info
{
	int	base, irq, dma;
	
	int	codec, codec_irq;	/* required to setup pnp cards*/
	int	codec_type;
	int	ic_type;
	char*	raw_buf;
	unsigned long	raw_buf_phys;
	int	buffsize;		/* -------------------------- */
	spinlock_t lock;
	int	ok;	/* Properly detected */
	int	failed;
	int	dma_allocated;
	int	codec_audiodev;
	int	opened;
	int	*osp;
	int	my_audiodev;
} sscape_info;

static struct sscape_info adev_info = {
	0
};

static struct sscape_info *devc = &adev_info;
static int sscape_mididev = -1;

/* Some older cards have assigned interrupt bits differently than new ones */
static char valid_interrupts_old[] = {
	9, 7, 5, 15
};

static char valid_interrupts_new[] = {
	9, 5, 7, 10
};

static char *valid_interrupts = valid_interrupts_new;

/*
 *	See the bottom of the driver. This can be set by spea =0/1.
 */
 
#ifdef REVEAL_SPEA
static char old_hardware = 1;
#else
static char old_hardware;
#endif

static void sleep(unsigned howlong)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(howlong);
}

static unsigned char sscape_read(struct sscape_info *devc, int reg)
{
	unsigned long flags;
	unsigned char val;

	spin_lock_irqsave(&devc->lock,flags);
	outb(reg, PORT(ODIE_ADDR));
	val = inb(PORT(ODIE_DATA));
	spin_unlock_irqrestore(&devc->lock,flags);
	return val;
}

static void __sscape_write(int reg, int data)
{
	outb(reg, PORT(ODIE_ADDR));
	outb(data, PORT(ODIE_DATA));
}

static void sscape_write(struct sscape_info *devc, int reg, int data)
{
	unsigned long flags;

	spin_lock_irqsave(&devc->lock,flags);
	__sscape_write(reg, data);
	spin_unlock_irqrestore(&devc->lock,flags);
}

static unsigned char sscape_pnp_read_codec(sscape_info* devc, unsigned char reg)
{
	unsigned char res;
	unsigned long flags;

	spin_lock_irqsave(&devc->lock,flags);
	outb( reg, devc -> codec);
	res = inb (devc -> codec + 1);
	spin_unlock_irqrestore(&devc->lock,flags);
	return res;

}

static void sscape_pnp_write_codec(sscape_info* devc, unsigned char reg, unsigned char data)
{
	unsigned long flags;
	
	spin_lock_irqsave(&devc->lock,flags);
	outb( reg, devc -> codec);
	outb( data, devc -> codec + 1);
	spin_unlock_irqrestore(&devc->lock,flags);
}

static void host_open(struct sscape_info *devc)
{
	outb((0x00), PORT(HOST_CTRL));	/* Put the board to the host mode */
}

static void host_close(struct sscape_info *devc)
{
	outb((0x03), PORT(HOST_CTRL));	/* Put the board to the MIDI mode */
}

static int host_write(struct sscape_info *devc, unsigned char *data, int count)
{
	unsigned long flags;
	int i, timeout_val;

	spin_lock_irqsave(&devc->lock,flags);
	/*
	 * Send the command and data bytes
	 */

	for (i = 0; i < count; i++)
	{
		for (timeout_val = 10000; timeout_val > 0; timeout_val--)
			if (inb(PORT(HOST_CTRL)) & TX_READY)
				break;

		if (timeout_val <= 0)
		{
				spin_unlock_irqrestore(&devc->lock,flags);
			    return 0;
		}
		outb(data[i], PORT(HOST_DATA));
	}
	spin_unlock_irqrestore(&devc->lock,flags);
	return 1;
}

static int host_read(struct sscape_info *devc)
{
	unsigned long flags;
	int timeout_val;
	unsigned char data;

	spin_lock_irqsave(&devc->lock,flags);
	/*
	 * Read a byte
	 */

	for (timeout_val = 10000; timeout_val > 0; timeout_val--)
		if (inb(PORT(HOST_CTRL)) & RX_READY)
			break;

	if (timeout_val <= 0)
	{
		spin_unlock_irqrestore(&devc->lock,flags);
		return -1;
	}
	data = inb(PORT(HOST_DATA));
	spin_unlock_irqrestore(&devc->lock,flags);
	return data;
}

#if 0 /* unused */
static int host_command1(struct sscape_info *devc, int cmd)
{
	unsigned char buf[10];
	buf[0] = (unsigned char) (cmd & 0xff);
	return host_write(devc, buf, 1);
}
#endif /* unused */


static int host_command2(struct sscape_info *devc, int cmd, int parm1)
{
	unsigned char buf[10];

	buf[0] = (unsigned char) (cmd & 0xff);
	buf[1] = (unsigned char) (parm1 & 0xff);

	return host_write(devc, buf, 2);
}

static int host_command3(struct sscape_info *devc, int cmd, int parm1, int parm2)
{
	unsigned char buf[10];

	buf[0] = (unsigned char) (cmd & 0xff);
	buf[1] = (unsigned char) (parm1 & 0xff);
	buf[2] = (unsigned char) (parm2 & 0xff);
	return host_write(devc, buf, 3);
}

static void set_mt32(struct sscape_info *devc, int value)
{
	host_open(devc);
	host_command2(devc, CMD_SET_MT32, value ? 1 : 0);
	if (host_read(devc) != CMD_ACK)
	{
		/* printk( "SNDSCAPE: Setting MT32 mode failed\n"); */
	}
	host_close(devc);
}

static void set_control(struct sscape_info *devc, int ctrl, int value)
{
	host_open(devc);
	host_command3(devc, CMD_SET_CONTROL, ctrl, value);
	if (host_read(devc) != CMD_ACK)
	{
		/* printk( "SNDSCAPE: Setting control (%d) failed\n",  ctrl); */
	}
	host_close(devc);
}

static void do_dma(struct sscape_info *devc, int dma_chan, unsigned long buf, int blk_size, int mode)
{
	unsigned char temp;

	if (dma_chan != SSCAPE_DMA_A)
	{
		printk(KERN_WARNING "soundscape: Tried to use DMA channel  != A. Why?\n");
		return;
	}
	audio_devs[devc->codec_audiodev]->flags &= ~DMA_AUTOMODE;
	DMAbuf_start_dma(devc->codec_audiodev, buf, blk_size, mode);
	audio_devs[devc->codec_audiodev]->flags |= DMA_AUTOMODE;

	temp = devc->dma << 4;	/* Setup DMA channel select bits */
	if (devc->dma <= 3)
		temp |= 0x80;	/* 8 bit DMA channel */

	temp |= 1;		/* Trigger DMA */
	sscape_write(devc, GA_DMAA_REG, temp);
	temp &= 0xfe;		/* Clear DMA trigger */
	sscape_write(devc, GA_DMAA_REG, temp);
}

static int verify_mpu(struct sscape_info *devc)
{
	/*
	 * The SoundScape board could be in three modes (MPU, 8250 and host).
	 * If the card is not in the MPU mode, enabling the MPU driver will
	 * cause infinite loop (the driver believes that there is always some
	 * received data in the buffer.
	 *
	 * Detect this by looking if there are more than 10 received MIDI bytes
	 * (0x00) in the buffer.
	 */

	int i;

	for (i = 0; i < 10; i++)
	{
		if (inb(devc->base + HOST_CTRL) & 0x80)
			return 1;

		if (inb(devc->base) != 0x00)
			return 1;
	}
	printk(KERN_WARNING "SoundScape: The device is not in the MPU-401 mode\n");
	return 0;
}

static int sscape_coproc_open(void *dev_info, int sub_device)
{
	if (sub_device == COPR_MIDI)
	{
		set_mt32(devc, 0);
		if (!verify_mpu(devc))
			return -EIO;
	}
	return 0;
}

static void sscape_coproc_close(void *dev_info, int sub_device)
{
	struct sscape_info *devc = dev_info;
	unsigned long   flags;

	spin_lock_irqsave(&devc->lock,flags);
	if (devc->dma_allocated)
	{
		__sscape_write(GA_DMAA_REG, 0x20);	/* DMA channel disabled */
		devc->dma_allocated = 0;
	}
	spin_unlock_irqrestore(&devc->lock,flags);
	return;
}

static void sscape_coproc_reset(void *dev_info)
{
}

static int sscape_download_boot(struct sscape_info *devc, unsigned char *block, int size, int flag)
{
	unsigned long flags;
	unsigned char temp;
	volatile int done, timeout_val;
	static unsigned char codec_dma_bits;

	if (flag & CPF_FIRST)
	{
		/*
		 * First block. Have to allocate DMA and to reset the board
		 * before continuing.
		 */

		spin_lock_irqsave(&devc->lock,flags);
		codec_dma_bits = sscape_read(devc, GA_CDCFG_REG);

		if (devc->dma_allocated == 0)
			devc->dma_allocated = 1;

		spin_unlock_irqrestore(&devc->lock,flags);

		sscape_write(devc, GA_HMCTL_REG, 
			(temp = sscape_read(devc, GA_HMCTL_REG)) & 0x3f);	/*Reset */

		for (timeout_val = 10000; timeout_val > 0; timeout_val--)
			sscape_read(devc, GA_HMCTL_REG);	/* Delay */

		/* Take board out of reset */
		sscape_write(devc, GA_HMCTL_REG,
			(temp = sscape_read(devc, GA_HMCTL_REG)) | 0x80);
	}
	/*
	 * Transfer one code block using DMA
	 */
	if (audio_devs[devc->codec_audiodev]->dmap_out->raw_buf == NULL)
	{
		printk(KERN_WARNING "soundscape: DMA buffer not available\n");
		return 0;
	}
	memcpy(audio_devs[devc->codec_audiodev]->dmap_out->raw_buf, block, size);

	spin_lock_irqsave(&devc->lock,flags);
	
	/******** INTERRUPTS DISABLED NOW ********/
	
	do_dma(devc, SSCAPE_DMA_A,
	       audio_devs[devc->codec_audiodev]->dmap_out->raw_buf_phys,
	       size, DMA_MODE_WRITE);

	/*
	 * Wait until transfer completes.
	 */
	
	done = 0;
	timeout_val = 30;
	while (!done && timeout_val-- > 0)
	{
		int resid;

		if (HZ / 50)
			sleep(HZ / 50);
		clear_dma_ff(devc->dma);
		if ((resid = get_dma_residue(devc->dma)) == 0)
			done = 1;
	}

	spin_unlock_irqrestore(&devc->lock,flags);
	if (!done)
		return 0;

	if (flag & CPF_LAST)
	{
		/*
		 * Take the board out of reset
		 */
		outb((0x00), PORT(HOST_CTRL));
		outb((0x00), PORT(MIDI_CTRL));

		temp = sscape_read(devc, GA_HMCTL_REG);
		temp |= 0x40;
		sscape_write(devc, GA_HMCTL_REG, temp);	/* Kickstart the board */

		/*
		 * Wait until the ODB wakes up
		 */
		spin_lock_irqsave(&devc->lock,flags);
		done = 0;
		timeout_val = 5 * HZ;
		while (!done && timeout_val-- > 0)
		{
			unsigned char x;
			
			sleep(1);
			x = inb(PORT(HOST_DATA));
			if (x == 0xff || x == 0xfe)		/* OBP startup acknowledge */
			{
				DDB(printk("Soundscape: Acknowledge = %x\n", x));
				done = 1;
			}
		}
		sscape_write(devc, GA_CDCFG_REG, codec_dma_bits);

		spin_unlock_irqrestore(&devc->lock,flags);
		if (!done)
		{
			printk(KERN_ERR "soundscape: The OBP didn't respond after code download\n");
			return 0;
		}
		spin_lock_irqsave(&devc->lock,flags);
		done = 0;
		timeout_val = 5 * HZ;
		while (!done && timeout_val-- > 0)
		{
			sleep(1);
			if (inb(PORT(HOST_DATA)) == 0xfe)	/* Host startup acknowledge */
				done = 1;
		}
		spin_unlock_irqrestore(&devc->lock,flags);
		if (!done)
		{
			printk(KERN_ERR "soundscape: OBP Initialization failed.\n");
			return 0;
		}
		printk(KERN_INFO "SoundScape board initialized OK\n");
		set_control(devc, CTL_MASTER_VOL, 100);
		set_control(devc, CTL_SYNTH_VOL, 100);

#ifdef SSCAPE_DEBUG3
		/*
		 * Temporary debugging aid. Print contents of the registers after
		 * downloading the code.
		 */
		{
			int i;

			for (i = 0; i < 13; i++)
				printk("I%d = %02x (new value)\n", i, sscape_read(devc, i));
		}
#endif

	}
	return 1;
}

static int download_boot_block(void *dev_info, copr_buffer * buf)
{
	if (buf->len <= 0 || buf->len > sizeof(buf->data))
		return -EINVAL;

	if (!sscape_download_boot(devc, buf->data, buf->len, buf->flags))
	{
		printk(KERN_ERR "soundscape: Unable to load microcode block to the OBP.\n");
		return -EIO;
	}
	return 0;
}

static int sscape_coproc_ioctl(void *dev_info, unsigned int cmd, void __user *arg, int local)
{
	copr_buffer *buf;
	int err;

	switch (cmd) 
	{
		case SNDCTL_COPR_RESET:
			sscape_coproc_reset(dev_info);
			return 0;

		case SNDCTL_COPR_LOAD:
			buf = (copr_buffer *) vmalloc(sizeof(copr_buffer));
			if (buf == NULL)
				return -ENOSPC;
			if (copy_from_user(buf, arg, sizeof(copr_buffer))) 
			{
				vfree(buf);
				return -EFAULT;
			}
			err = download_boot_block(dev_info, buf);
			vfree(buf);
			return err;
		
		default:
			return -EINVAL;
	}
}

static coproc_operations sscape_coproc_operations =
{
	"SoundScape M68K",
	THIS_MODULE,
	sscape_coproc_open,
	sscape_coproc_close,
	sscape_coproc_ioctl,
	sscape_coproc_reset,
	&adev_info
};

static struct resource *sscape_ports;
static int sscape_is_pnp;

static void __init attach_sscape(struct address_info *hw_config)
{
#ifndef SSCAPE_REGS
	/*
	 * Config register values for Spea/V7 Media FX and Ensoniq S-2000.
	 * These values are card
	 * dependent. If you have another SoundScape based card, you have to
	 * find the correct values. Do the following:
	 *  - Compile this driver with SSCAPE_DEBUG1 defined.
	 *  - Shut down and power off your machine.
	 *  - Boot with DOS so that the SSINIT.EXE program is run.
	 *  - Warm boot to {Linux|SYSV|BSD} and write down the lines displayed
	 *    when detecting the SoundScape.
	 *  - Modify the following list to use the values printed during boot.
	 *    Undefine the SSCAPE_DEBUG1
	 */
#define SSCAPE_REGS { \
/* I0 */	0x00, \
/* I1 */	0xf0, /* Note! Ignored. Set always to 0xf0 */ \
/* I2 */	0x20, /* Note! Ignored. Set always to 0x20 */ \
/* I3 */	0x20, /* Note! Ignored. Set always to 0x20 */ \
/* I4 */	0xf5, /* Ignored */ \
/* I5 */	0x10, \
/* I6 */	0x00, \
/* I7 */	0x2e, /* I7 MEM config A. Likely to vary between models */ \
/* I8 */	0x00, /* I8 MEM config B. Likely to vary between models */ \
/* I9 */	0x40 /* Ignored */ \
	}
#endif

	unsigned long   flags;
	static unsigned char regs[10] = SSCAPE_REGS;

	int i, irq_bits = 0xff;

	if (old_hardware)
	{
		valid_interrupts = valid_interrupts_old;
		conf_printf("Ensoniq SoundScape (old)", hw_config);
	}
	else
		conf_printf("Ensoniq SoundScape", hw_config);

	for (i = 0; i < 4; i++)
	{
		if (hw_config->irq == valid_interrupts[i])
		{
			irq_bits = i;
			break;
		}
	}
	if (hw_config->irq > 15 || (regs[4] = irq_bits == 0xff))
	{
		printk(KERN_ERR "Invalid IRQ%d\n", hw_config->irq);
		release_region(devc->base, 2);
		release_region(devc->base + 2, 6);
		if (sscape_is_pnp)
			release_region(devc->codec, 2);
		return;
	}
	
	if (!sscape_is_pnp) {
	
		spin_lock_irqsave(&devc->lock,flags);
		/* Host interrupt enable */
		sscape_write(devc, 1, 0xf0);	/* All interrupts enabled */
		/* DMA A status/trigger register */
		sscape_write(devc, 2, 0x20);	/* DMA channel disabled */
		/* DMA B status/trigger register */
		sscape_write(devc, 3, 0x20);	/* DMA channel disabled */
		/* Host interrupt config reg */
		sscape_write(devc, 4, 0xf0 | (irq_bits << 2) | irq_bits);
		/* Don't destroy CD-ROM DMA config bits (0xc0) */
		sscape_write(devc, 5, (regs[5] & 0x3f) | (sscape_read(devc, 5) & 0xc0));
		/* CD-ROM config (WSS codec actually) */
		sscape_write(devc, 6, regs[6]);
		sscape_write(devc, 7, regs[7]);
		sscape_write(devc, 8, regs[8]);
		/* Master control reg. Don't modify CR-ROM bits. Disable SB emul */
		sscape_write(devc, 9, (sscape_read(devc, 9) & 0xf0) | 0x08);
		spin_unlock_irqrestore(&devc->lock,flags);
	}
#ifdef SSCAPE_DEBUG2
	/*
	 * Temporary debugging aid. Print contents of the registers after
	 * changing them.
	 */
	{
		int i;

		for (i = 0; i < 13; i++)
			printk("I%d = %02x (new value)\n", i, sscape_read(devc, i));
	}
#endif

	if (probe_mpu401(hw_config, sscape_ports))
		hw_config->always_detect = 1;
	hw_config->name = "SoundScape";

	hw_config->irq *= -1;	/* Negative value signals IRQ sharing */
	attach_mpu401(hw_config, THIS_MODULE);
	hw_config->irq *= -1;	/* Restore it */

	if (hw_config->slots[1] != -1)	/* The MPU driver installed itself */
	{
		sscape_mididev = hw_config->slots[1];
		midi_devs[hw_config->slots[1]]->coproc = &sscape_coproc_operations;
	}
	sscape_write(devc, GA_INTENA_REG, 0x80);	/* Master IRQ enable */
	devc->ok = 1;
	devc->failed = 0;
}

static int detect_ga(sscape_info * devc)
{
	unsigned char save;

	DDB(printk("Entered Soundscape detect_ga(%x)\n", devc->base));

	/*
	 * First check that the address register of "ODIE" is
	 * there and that it has exactly 4 writable bits.
	 * First 4 bits
	 */
	
	if ((save = inb(PORT(ODIE_ADDR))) & 0xf0)
	{
		DDB(printk("soundscape: Detect error A\n"));
		return 0;
	}
	outb((0x00), PORT(ODIE_ADDR));
	if (inb(PORT(ODIE_ADDR)) != 0x00)
	{
		DDB(printk("soundscape: Detect error B\n"));
		return 0;
	}
	outb((0xff), PORT(ODIE_ADDR));
	if (inb(PORT(ODIE_ADDR)) != 0x0f)
	{
		DDB(printk("soundscape: Detect error C\n"));
		return 0;
	}
	outb((save), PORT(ODIE_ADDR));

	/*
	 * Now verify that some indirect registers return zero on some bits.
	 * This may break the driver with some future revisions of "ODIE" but...
	 */

	if (sscape_read(devc, 0) & 0x0c)
	{
		DDB(printk("soundscape: Detect error D (%x)\n", sscape_read(devc, 0)));
		return 0;
	}
	if (sscape_read(devc, 1) & 0x0f)
	{
		DDB(printk("soundscape: Detect error E\n"));
		return 0;
	}
	if (sscape_read(devc, 5) & 0x0f)
	{
		DDB(printk("soundscape: Detect error F\n"));
		return 0;
	}
	return 1;
}

static	int sscape_read_host_ctrl(sscape_info* devc)
{
	return host_read(devc);
}

static	void sscape_write_host_ctrl2(sscape_info *devc, int a, int b)
{
	host_command2(devc, a, b);
}

static int sscape_alloc_dma(sscape_info *devc)
{
	char *start_addr, *end_addr;
	int dma_pagesize;
	int sz, size;
	struct page *page;

	if (devc->raw_buf != NULL) return 0;	/* Already done */
	dma_pagesize = (devc->dma < 4) ? (64 * 1024) : (128 * 1024);
	devc->raw_buf = NULL;
	devc->buffsize = 8192*4;
	if (devc->buffsize > dma_pagesize) devc->buffsize = dma_pagesize;
	start_addr = NULL;
	/*
	 * Now loop until we get a free buffer. Try to get smaller buffer if
	 * it fails. Don't accept smaller than 8k buffer for performance
	 * reasons.
	 */
	while (start_addr == NULL && devc->buffsize > PAGE_SIZE) {
		for (sz = 0, size = PAGE_SIZE; size < devc->buffsize; sz++, size <<= 1);
		devc->buffsize = PAGE_SIZE * (1 << sz);
		start_addr = (char *) __get_free_pages(GFP_ATOMIC|GFP_DMA, sz);
		if (start_addr == NULL) devc->buffsize /= 2;
	}

	if (start_addr == NULL) {
		printk(KERN_ERR "sscape pnp init error: Couldn't allocate DMA buffer\n");
		return 0;
	} else {
		/* make some checks */
		end_addr = start_addr + devc->buffsize - 1;		
		/* now check if it fits into the same dma-pagesize */

		if (((long) start_addr & ~(dma_pagesize - 1)) != ((long) end_addr & ~(dma_pagesize - 1))
		    || end_addr >= (char *) (MAX_DMA_ADDRESS)) {
			printk(KERN_ERR "sscape pnp: Got invalid address 0x%lx for %db DMA-buffer\n", (long) start_addr, devc->buffsize);
			return 0;
		}
	}
	devc->raw_buf = start_addr;
	devc->raw_buf_phys = virt_to_bus(start_addr);

	for (page = virt_to_page(start_addr); page <= virt_to_page(end_addr); page++)
		SetPageReserved(page);
	return 1;
}

static void sscape_free_dma(sscape_info *devc)
{
	int sz, size;
	unsigned long start_addr, end_addr;
	struct page *page;

	if (devc->raw_buf == NULL) return;
	for (sz = 0, size = PAGE_SIZE; size < devc->buffsize; sz++, size <<= 1);
	start_addr = (unsigned long) devc->raw_buf;
	end_addr = start_addr + devc->buffsize;

	for (page = virt_to_page(start_addr); page <= virt_to_page(end_addr); page++)
		ClearPageReserved(page);

	free_pages((unsigned long) devc->raw_buf, sz);
	devc->raw_buf = NULL;
}

/* Intel version !!!!!!!!! */

static int sscape_start_dma(int chan, unsigned long physaddr, int count, int dma_mode)
{
	unsigned long flags;

	flags = claim_dma_lock();
	disable_dma(chan);
	clear_dma_ff(chan);
	set_dma_mode(chan, dma_mode);
	set_dma_addr(chan, physaddr);
	set_dma_count(chan, count);
	enable_dma(chan);
	release_dma_lock(flags);
	return 0;
}

static void sscape_pnp_start_dma(sscape_info* devc, int arg )
{
	int reg;
	if (arg == 0) reg = 2;
	else reg = 3;

	sscape_write(devc, reg, sscape_read( devc, reg) | 0x01);
	sscape_write(devc, reg, sscape_read( devc, reg) & 0xFE);
}

static int sscape_pnp_wait_dma (sscape_info* devc, int arg )
{
	int		reg;
	unsigned long	i;
	unsigned char	d;

	if (arg == 0) reg = 2;
	else reg = 3;

	sleep ( 1 );
	i = 0;
	do {
		d = sscape_read(devc, reg) & 1;
		if ( d == 1)  break;
		i++;
	} while (i < 500000);
	d = sscape_read(devc, reg) & 1; 
	return d;
}

static	int	sscape_pnp_alloc_dma(sscape_info* devc)
{
	/* printk(KERN_INFO "sscape: requesting dma\n"); */
	if (request_dma(devc -> dma, "sscape")) return 0;
	/* printk(KERN_INFO "sscape: dma channel allocated\n"); */
	if (!sscape_alloc_dma(devc)) {
		free_dma(devc -> dma);
		return 0;
	};
	return 1;
}

static	void	sscape_pnp_free_dma(sscape_info* devc)
{
	sscape_free_dma( devc);
	free_dma(devc -> dma );	
	/* printk(KERN_INFO "sscape: dma released\n"); */
}

static	int	sscape_pnp_upload_file(sscape_info* devc, char* fn)
{	
	int	     	done = 0;
	int	     	timeout_val;
	char*	     	data,*dt;
	int	     	len,l;
	unsigned long	flags;

	sscape_write( devc, 9, sscape_read(devc, 9 )  & 0x3F );
	sscape_write( devc, 2, (devc -> dma << 4) | 0x80 );
	sscape_write( devc, 3, 0x20 );
	sscape_write( devc, 9, sscape_read( devc, 9 )  | 0x80 );
	
	len = mod_firmware_load(fn, &data);
	if (len == 0) {
		    printk(KERN_ERR "sscape: file not found: %s\n", fn);
		    return 0;
	}
	dt = data;
	spin_lock_irqsave(&devc->lock,flags);
	while ( len > 0 ) {
		if (len > devc -> buffsize) l = devc->buffsize;
		else l = len;
		len -= l;		
		memcpy(devc->raw_buf, dt, l); dt += l;
		sscape_start_dma(devc->dma, devc->raw_buf_phys, l, 0x48);
		sscape_pnp_start_dma ( devc, 0 );
		if (sscape_pnp_wait_dma ( devc, 0 ) == 0) {
			spin_unlock_irqrestore(&devc->lock,flags);
			return 0;
		}
	}
	
	spin_unlock_irqrestore(&devc->lock,flags);
	vfree(data);
	
	outb(0, devc -> base + 2);
	outb(0, devc -> base);

	sscape_write ( devc, 9, sscape_read( devc, 9 ) | 0x40);

	timeout_val = 5 * HZ; 
	while (!done && timeout_val-- > 0)
	{
		unsigned char x;
		sleep(1);
		x = inb( devc -> base + 3);
		if (x == 0xff || x == 0xfe)		/* OBP startup acknowledge */
		{
			//printk(KERN_ERR "Soundscape: Acknowledge = %x\n", x);
			done = 1;
		}
	}
	timeout_val = 5 * HZ;
	done = 0;
	while (!done && timeout_val-- > 0)
	{
		unsigned char x;
		sleep(1);
		x = inb( devc -> base + 3);
		if (x == 0xfe)		/* OBP startup acknowledge */
		{
			//printk(KERN_ERR "Soundscape: Acknowledge = %x\n", x);
			done = 1;
		}
	}

	if ( !done ) printk(KERN_ERR "soundscape: OBP Initialization failed.\n");

	sscape_write( devc, 2, devc->ic_type == IC_ODIE ? 0x70 : 0x40);
	sscape_write( devc, 3, (devc -> dma << 4) + 0x80);
	return 1;
}

static void __init sscape_pnp_init_hw(sscape_info* devc)
{	
	unsigned char midi_irq = 0, sb_irq = 0;
	unsigned i;
	static	char code_file_name[23] = "/sndscape/sndscape.cox";
	
	int sscape_joystic_enable	= 0x7f;
	int sscape_mic_enable		= 0;
	int sscape_ext_midi		= 0;		

	if ( !sscape_pnp_alloc_dma(devc) ) {
		printk(KERN_ERR "sscape: faild to allocate dma\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		if ( devc -> irq   == valid_interrupts[i] ) 
			midi_irq = i;
		if ( devc -> codec_irq == valid_interrupts[i] ) 
			sb_irq = i;
	}

	sscape_write( devc, 5, 0x50);
	sscape_write( devc, 7, 0x2e);
	sscape_write( devc, 8, 0x00);

	sscape_write( devc, 2, devc->ic_type == IC_ODIE ? 0x70 : 0x40);
	sscape_write( devc, 3, ( devc -> dma << 4) | 0x80);

	sscape_write (devc, 4, 0xF0 | (midi_irq<<2) | midi_irq);

	i = 0x10; //sscape_read(devc, 9) & (devc->ic_type == IC_ODIE ? 0xf0 : 0xc0);
	if (sscape_joystic_enable) i |= 8;
	
	sscape_write (devc, 9, i);
	sscape_write (devc, 6, 0x80);
	sscape_write (devc, 1, 0x80);

	if (devc -> codec_type == 2) {
		sscape_pnp_write_codec( devc, 0x0C, 0x50);
		sscape_pnp_write_codec( devc, 0x10, sscape_pnp_read_codec( devc, 0x10) & 0x3F);
		sscape_pnp_write_codec( devc, 0x11, sscape_pnp_read_codec( devc, 0x11) | 0xC0);
		sscape_pnp_write_codec( devc, 29, 0x20);
	}

	if (sscape_pnp_upload_file(devc, "/sndscape/scope.cod") == 0 ) {
		printk(KERN_ERR "sscape: faild to upload file /sndscape/scope.cod\n");
		sscape_pnp_free_dma(devc);
		return;
	}

	i = sscape_read_host_ctrl( devc );
	
	if ( (i & 0x0F) >  7 ) {
		printk(KERN_ERR "sscape: scope.cod faild\n");
		sscape_pnp_free_dma(devc);
		return;
	}
	if ( i & 0x10 ) sscape_write( devc, 7, 0x2F);
	code_file_name[21] = (char) ( i & 0x0F) + 0x30;
	if (sscape_pnp_upload_file( devc, code_file_name) == 0) {
		printk(KERN_ERR "sscape: faild to upload file %s\n", code_file_name);
		sscape_pnp_free_dma(devc);
		return;
	}
	
	if (devc->ic_type != IC_ODIE) {
		sscape_pnp_write_codec( devc, 10, (sscape_pnp_read_codec(devc, 10) & 0x7f) |
		 ( sscape_mic_enable == 0 ? 0x00 : 0x80) );
	}
	sscape_write_host_ctrl2( devc, 0x84, 0x64 );  /* MIDI volume */
	sscape_write_host_ctrl2( devc, 0x86, 0x64 );  /* MIDI volume?? */
	sscape_write_host_ctrl2( devc, 0x8A, sscape_ext_midi);

	sscape_pnp_write_codec ( devc, 6, 0x3f ); //WAV_VOL
	sscape_pnp_write_codec ( devc, 7, 0x3f ); //WAV_VOL
	sscape_pnp_write_codec ( devc, 2, 0x1F ); //WD_CDXVOLL
	sscape_pnp_write_codec ( devc, 3, 0x1F ); //WD_CDXVOLR

	if (devc -> codec_type == 1) {
		sscape_pnp_write_codec ( devc, 4, 0x1F );
		sscape_pnp_write_codec ( devc, 5, 0x1F );
		sscape_write_host_ctrl2( devc, 0x88, sscape_mic_enable);
	} else {
		int t;
		sscape_pnp_write_codec ( devc, 0x10, 0x1F << 1);
		sscape_pnp_write_codec ( devc, 0x11, 0xC0 | (0x1F << 1));

		t = sscape_pnp_read_codec( devc, 0x00) & 0xDF;
		if ( (sscape_mic_enable == 0)) t |= 0;
		else t |= 0x20;
		sscape_pnp_write_codec ( devc, 0x00, t);
		t = sscape_pnp_read_codec( devc, 0x01) & 0xDF;
		if ( (sscape_mic_enable == 0) ) t |= 0;
		else t |= 0x20;
		sscape_pnp_write_codec ( devc, 0x01, t);
		sscape_pnp_write_codec ( devc, 0x40 | 29 , 0x20);
		outb(0, devc -> codec);
	}
	if (devc -> ic_type == IC_OPUS ) {
		int i = sscape_read( devc, 9 );
		sscape_write( devc, 9, i | 3 );
		sscape_write( devc, 3, 0x40);

		if (request_region(0x228, 1, "sscape setup junk")) {
			outb(0, 0x228);
			release_region(0x228,1);
		}
		sscape_write( devc, 3, (devc -> dma << 4) | 0x80);
		sscape_write( devc, 9, i );
	}
	
	host_close ( devc );
	sscape_pnp_free_dma(devc);
}

static int __init detect_sscape_pnp(sscape_info* devc)
{
	long	 i, irq_bits = 0xff;
	unsigned int d;

	DDB(printk("Entered detect_sscape_pnp(%x)\n", devc->base));

	if (!request_region(devc->codec, 2, "sscape codec")) {
		printk(KERN_ERR "detect_sscape_pnp: port %x is not free\n", devc->codec);	
		return 0;
	}

	if ((inb(devc->base + 2) & 0x78) != 0)
		goto fail;

	d = inb ( devc -> base + 4) & 0xF0;
	if (d & 0x80)
		goto fail;
	
	if (d == 0) {
		devc->codec_type = 1;
		devc->ic_type = IC_ODIE;
	} else if ( (d & 0x60) != 0) {
		devc->codec_type = 2;
		devc->ic_type = IC_OPUS;
	} else if ( (d & 0x40) != 0) {	/* WTF? */
		devc->codec_type = 2;
		devc->ic_type = IC_ODIE;
	} else
		goto fail;
	
	sscape_is_pnp = 1;
		
	outb(0xFA, devc -> base+4);
	if  ((inb( devc -> base+4) & 0x9F) != 0x0A)
		goto fail;
	outb(0xFE, devc -> base+4);
	if  ( (inb(devc -> base+4) & 0x9F) != 0x0E)
		goto fail;
	if  ( (inb(devc -> base+5) & 0x9F) != 0x0E)
		goto fail;

	if (devc->codec_type == 2) {
		if (devc->codec != devc->base + 8) {
			printk("soundscape warning: incorrect codec port specified\n");
			goto fail;
		}
		d = 0x10 | (sscape_read(devc, 9)  & 0xCF);
		sscape_write(devc, 9, d);
		sscape_write(devc, 6, 0x80);
	} else {
		//todo: check codec is not base + 8
	}

	d  = (sscape_read(devc, 9) & 0x3F) | 0xC0;
	sscape_write(devc, 9, d);

	for (i = 0; i < 550000; i++)
		if ( !(inb(devc -> codec) & 0x80) ) break;

	d = inb(devc -> codec);
	if (d & 0x80)
		goto fail;
	if ( inb(devc -> codec + 2) == 0xFF)
		goto fail;

	sscape_write(devc, 9, sscape_read(devc, 9)  & 0x3F );

	d  = inb(devc -> codec) & 0x80;
	if ( d == 0) {
		printk(KERN_INFO "soundscape: hardware detected\n");
		valid_interrupts = valid_interrupts_new;
	} else	{
		printk(KERN_INFO "soundscape: board looks like media fx\n");
		valid_interrupts = valid_interrupts_old;
		old_hardware = 1;
	}

	sscape_write( devc, 9, 0xC0 | (sscape_read(devc, 9)  & 0x3F) );

	for (i = 0; i < 550000; i++)
		if ( !(inb(devc -> codec) & 0x80)) 
			break;
		
	sscape_pnp_init_hw(devc);

	for (i = 0; i < 4; i++)
	{
		if (devc->codec_irq == valid_interrupts[i]) {
			irq_bits = i;
			break;
		}
	}	
	sscape_write(devc, GA_INTENA_REG, 0x00);
	sscape_write(devc, GA_DMACFG_REG, 0x50);
	sscape_write(devc, GA_DMAA_REG, 0x70);
	sscape_write(devc, GA_DMAB_REG, 0x20);
	sscape_write(devc, GA_INTCFG_REG, 0xf0);
	sscape_write(devc, GA_CDCFG_REG, 0x89 | (devc->dma << 4) | (irq_bits << 1));

	sscape_pnp_write_codec( devc, 0, sscape_pnp_read_codec( devc, 0) | 0x20);
	sscape_pnp_write_codec( devc, 0, sscape_pnp_read_codec( devc, 1) | 0x20);

	return 1;
fail:
	release_region(devc->codec, 2);
	return 0;
}

static int __init probe_sscape(struct address_info *hw_config)
{
	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->dma = hw_config->dma;
	devc->osp = hw_config->osp;

#ifdef SSCAPE_DEBUG1
	/*
	 * Temporary debugging aid. Print contents of the registers before
	 * changing them.
	 */
	{
		int i;

		for (i = 0; i < 13; i++)
			printk("I%d = %02x (old value)\n", i, sscape_read(devc, i));
	}
#endif
	devc->failed = 1;

	sscape_ports = request_region(devc->base, 2, "mpu401");
	if (!sscape_ports)
		return 0;

	if (!request_region(devc->base + 2, 6, "SoundScape")) {
		release_region(devc->base, 2);
		return 0;
	}

	if (!detect_ga(devc)) {
		if (detect_sscape_pnp(devc))
			return 1;
		release_region(devc->base, 2);
		release_region(devc->base + 2, 6);
		return 0;
	}

	if (old_hardware)	/* Check that it's really an old Spea/Reveal card. */
	{
		unsigned char   tmp;
		int             cc;

		if (!((tmp = sscape_read(devc, GA_HMCTL_REG)) & 0xc0))
		{
			sscape_write(devc, GA_HMCTL_REG, tmp | 0x80);
			for (cc = 0; cc < 200000; ++cc)
				inb(devc->base + ODIE_ADDR);
		}
	}
	return 1;
}

static int __init init_ss_ms_sound(struct address_info *hw_config)
{
	int i, irq_bits = 0xff;
	int ad_flags = 0;
	struct resource *ports;
	
	if (devc->failed)
	{
		printk(KERN_ERR "soundscape: Card not detected\n");
		return 0;
	}
	if (devc->ok == 0)
	{
		printk(KERN_ERR "soundscape: Invalid initialization order.\n");
		return 0;
	}
	for (i = 0; i < 4; i++)
	{
		if (hw_config->irq == valid_interrupts[i])
		{
			irq_bits = i;
			break;
		}
	}
	if (irq_bits == 0xff) {
		printk(KERN_ERR "soundscape: Invalid MSS IRQ%d\n", hw_config->irq);
		return 0;
	}
	
	if (old_hardware)
		ad_flags = 0x12345677;	/* Tell that we may have a CS4248 chip (Spea-V7 Media FX) */
	else if (sscape_is_pnp)
		ad_flags = 0x87654321;  /* Tell that we have a soundscape pnp with 1845 chip */

	ports = request_region(hw_config->io_base, 4, "ad1848");
	if (!ports) {
		printk(KERN_ERR "soundscape: ports busy\n");
		return 0;
	}

	if (!ad1848_detect(ports, &ad_flags, hw_config->osp)) {
		release_region(hw_config->io_base, 4);
		return 0;
	}

 	if (!sscape_is_pnp)  /*pnp is already setup*/
 	{
 		/*
     		 * Setup the DMA polarity.
 	    	 */
 		sscape_write(devc, GA_DMACFG_REG, 0x50);
 	
 		/*
 		 * Take the gate-array off of the DMA channel.
 		 */
 		sscape_write(devc, GA_DMAB_REG, 0x20);
 	
 		/*
 		 * Init the AD1848 (CD-ROM) config reg.
 		 */
 		sscape_write(devc, GA_CDCFG_REG, 0x89 | (hw_config->dma << 4) | (irq_bits << 1));
 	}
 	
 	if (hw_config->irq == devc->irq)
 		printk(KERN_WARNING "soundscape: Warning! The WSS mode can't share IRQ with MIDI\n");
 				
	hw_config->slots[0] = ad1848_init(
			sscape_is_pnp ? "SoundScape" : "SoundScape PNP",
			ports,
			hw_config->irq,
			hw_config->dma,
			hw_config->dma,
			0,
			devc->osp,
			THIS_MODULE);

 					  
	if (hw_config->slots[0] != -1)	/* The AD1848 driver installed itself */
	{
		audio_devs[hw_config->slots[0]]->coproc = &sscape_coproc_operations;
		devc->codec_audiodev = hw_config->slots[0];
		devc->my_audiodev = hw_config->slots[0];

		/* Set proper routings here (what are they) */
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_LINE);
	}
		
#ifdef SSCAPE_DEBUG5
	/*
	 * Temporary debugging aid. Print contents of the registers
	 * after the AD1848 device has been initialized.
	 */
	{
		int i;

		for (i = 0; i < 13; i++)
			printk("I%d = %02x\n", i, sscape_read(devc, i));
	}
#endif
	return 1;
}

static void __exit unload_sscape(struct address_info *hw_config)
{
	release_region(devc->base + 2, 6);
	unload_mpu401(hw_config);
	if (sscape_is_pnp)
		release_region(devc->codec, 2);
}

static void __exit unload_ss_ms_sound(struct address_info *hw_config)
{
	ad1848_unload(hw_config->io_base,
		      hw_config->irq,
		      devc->dma,
		      devc->dma,
		      0);
	sound_unload_audiodev(hw_config->slots[0]);
}

static struct address_info cfg;
static struct address_info cfg_mpu;

static int __initdata spea = -1;
static int mss = 0;
static int __initdata dma = -1;
static int __initdata irq = -1;
static int __initdata io = -1;
static int __initdata mpu_irq = -1;
static int __initdata mpu_io = -1;

module_param(dma, int, 0);
module_param(irq, int, 0);
module_param(io, int, 0);
module_param(spea, int, 0);		/* spea=0/1 set the old_hardware */
module_param(mpu_irq, int, 0);
module_param(mpu_io, int, 0);
module_param(mss, int, 0);

static int __init init_sscape(void)
{
	printk(KERN_INFO "Soundscape driver Copyright (C) by Hannu Savolainen 1993-1996\n");
	
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.io_base = io;

	cfg_mpu.irq = mpu_irq;
	cfg_mpu.io_base = mpu_io;
	/* WEH - Try to get right dma channel */
        cfg_mpu.dma = dma;
	
	devc->codec = cfg.io_base;
	devc->codec_irq = cfg.irq;
	devc->codec_type = 0;
	devc->ic_type = 0;
	devc->raw_buf = NULL;
	spin_lock_init(&devc->lock);

	if (cfg.dma == -1 || cfg.irq == -1 || cfg.io_base == -1) {
		printk(KERN_ERR "DMA, IRQ, and IO port must be specified.\n");
		return -EINVAL;
	}
	
	if (cfg_mpu.irq == -1 && cfg_mpu.io_base != -1) {
		printk(KERN_ERR "MPU_IRQ must be specified if MPU_IO is set.\n");
		return -EINVAL;
	}
	
	if(spea != -1) {
		old_hardware = spea;
		printk(KERN_INFO "Forcing %s hardware support.\n",
			spea?"new":"old");
	}	
	if (probe_sscape(&cfg_mpu) == 0)
		return -ENODEV;

	attach_sscape(&cfg_mpu);
	
	mss = init_ss_ms_sound(&cfg);

	return 0;
}

static void __exit cleanup_sscape(void)
{
	if (mss)
		unload_ss_ms_sound(&cfg);
	unload_sscape(&cfg_mpu);
}

module_init(init_sscape);
module_exit(cleanup_sscape);

#ifndef MODULE
static int __init setup_sscape(char *str)
{
	/* io, irq, dma, mpu_io, mpu_irq */
	int ints[6];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	mpu_io	= ints[4];
	mpu_irq	= ints[5];

	return 1;
}

__setup("sscape=", setup_sscape);
#endif
MODULE_LICENSE("GPL");
