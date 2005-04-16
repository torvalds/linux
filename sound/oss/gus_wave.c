/*
 * sound/gus_wave.c
 *
 * Driver for the Gravis UltraSound wave table synth.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer    : ioctl code reworked (vmalloc/vfree removed)
 * Frank van de Pol : Fixed GUS MAX interrupt handling. Enabled simultanious
 *                    usage of CS4231A codec, GUS wave and MIDI for GUS MAX.
 * Bartlomiej Zolnierkiewicz : added some __init/__exit
 */
 
#include <linux/init.h> 
#include <linux/config.h>
#include <linux/spinlock.h>

#define GUSPNP_AUTODETECT

#include "sound_config.h"
#include <linux/ultrasound.h>

#include "gus.h"
#include "gus_hw.h"

#define GUS_BANK_SIZE (((iw_mode) ? 256*1024*1024 : 256*1024))

#define MAX_SAMPLE	150
#define MAX_PATCH	256

#define NOT_SAMPLE	0xffff

struct voice_info
{
	unsigned long   orig_freq;
	unsigned long   current_freq;
	unsigned long   mode;
	int             fixed_pitch;
	int             bender;
	int             bender_range;
	int             panning;
	int             midi_volume;
	unsigned int    initial_volume;
	unsigned int    current_volume;
	int             loop_irq_mode, loop_irq_parm;
#define LMODE_FINISH		1
#define LMODE_PCM		2
#define LMODE_PCM_STOP		3
	int             volume_irq_mode, volume_irq_parm;
#define VMODE_HALT		1
#define VMODE_ENVELOPE		2
#define VMODE_START_NOTE	3

	int             env_phase;
	unsigned char   env_rate[6];
	unsigned char   env_offset[6];

	/*
	 * Volume computation parameters for gus_adagio_vol()
	 */
	int		main_vol, expression_vol, patch_vol;

	/* Variables for "Ultraclick" removal */
	int             dev_pending, note_pending, volume_pending,
	                sample_pending;
	char            kill_pending;
	long            offset_pending;

};

static struct voice_alloc_info *voice_alloc;
static struct address_info *gus_hw_config;
extern int      gus_base;
extern int      gus_irq, gus_dma;
extern int      gus_pnp_flag;
extern int      gus_no_wave_dma;
static int      gus_dma2 = -1;
static int      dual_dma_mode;
static long     gus_mem_size;
static long     free_mem_ptr;
static int      gus_busy;
static int      gus_no_dma;
static int      nr_voices;
static int      gus_devnum;
static int      volume_base, volume_scale, volume_method;
static int      gus_recmask = SOUND_MASK_MIC;
static int      recording_active;
static int      only_read_access;
static int      only_8_bits;

static int      iw_mode = 0;
int             gus_wave_volume = 60;
int             gus_pcm_volume = 80;
int             have_gus_max = 0;
static int      gus_line_vol = 100, gus_mic_vol;
static unsigned char mix_image = 0x00;

int             gus_timer_enabled = 0;

/*
 * Current version of this driver doesn't allow synth and PCM functions
 * at the same time. The active_device specifies the active driver
 */

static int      active_device;

#define GUS_DEV_WAVE		1	/* Wave table synth */
#define GUS_DEV_PCM_DONE	2	/* PCM device, transfer done */
#define GUS_DEV_PCM_CONTINUE	3	/* PCM device, transfer done ch. 1/2 */

static int      gus_audio_speed;
static int      gus_audio_channels;
static int      gus_audio_bits;
static int      gus_audio_bsize;
static char     bounce_buf[8 * 1024];	/* Must match value set to max_fragment */

static DECLARE_WAIT_QUEUE_HEAD(dram_sleeper);

/*
 * Variables and buffers for PCM output
 */

#define MAX_PCM_BUFFERS		(128*MAX_REALTIME_FACTOR)	/* Don't change */

static int      pcm_bsize, pcm_nblk, pcm_banksize;
static int      pcm_datasize[MAX_PCM_BUFFERS];
static volatile int pcm_head, pcm_tail, pcm_qlen;
static volatile int pcm_active;
static volatile int dma_active;
static int      pcm_opened;
static int      pcm_current_dev;
static int      pcm_current_block;
static unsigned long pcm_current_buf;
static int      pcm_current_count;
static int      pcm_current_intrflag;
DEFINE_SPINLOCK(gus_lock);

extern int     *gus_osp;

static struct voice_info voices[32];

static int      freq_div_table[] =
{
	44100,			/* 14 */
	41160,			/* 15 */
	38587,			/* 16 */
	36317,			/* 17 */
	34300,			/* 18 */
	32494,			/* 19 */
	30870,			/* 20 */
	29400,			/* 21 */
	28063,			/* 22 */
	26843,			/* 23 */
	25725,			/* 24 */
	24696,			/* 25 */
	23746,			/* 26 */
	22866,			/* 27 */
	22050,			/* 28 */
	21289,			/* 29 */
	20580,			/* 30 */
	19916,			/* 31 */
	19293			/* 32 */
};

static struct patch_info *samples;
static long     sample_ptrs[MAX_SAMPLE + 1];
static int      sample_map[32];
static int      free_sample;
static int      mixer_type;


static int      patch_table[MAX_PATCH];
static int      patch_map[32];

static struct synth_info gus_info = {
	"Gravis UltraSound", 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_GUS, 
	0, 16, 0, MAX_PATCH
};

static void     gus_poke(long addr, unsigned char data);
static void     compute_and_set_volume(int voice, int volume, int ramp_time);
extern unsigned short gus_adagio_vol(int vel, int mainv, int xpn, int voicev);
extern unsigned short gus_linear_vol(int vol, int mainvol);
static void     compute_volume(int voice, int volume);
static void     do_volume_irq(int voice);
static void     set_input_volumes(void);
static void     gus_tmr_install(int io_base);

#define	INSTANT_RAMP		-1	/* Instant change. No ramping */
#define FAST_RAMP		0	/* Fastest possible ramp */

static void reset_sample_memory(void)
{
	int i;

	for (i = 0; i <= MAX_SAMPLE; i++)
		sample_ptrs[i] = -1;
	for (i = 0; i < 32; i++)
		sample_map[i] = -1;
	for (i = 0; i < 32; i++)
		patch_map[i] = -1;

	gus_poke(0, 0);		/* Put a silent sample to the beginning */
	gus_poke(1, 0);
	free_mem_ptr = 2;

	free_sample = 0;

	for (i = 0; i < MAX_PATCH; i++)
		patch_table[i] = NOT_SAMPLE;
}

void gus_delay(void)
{
	int i;

	for (i = 0; i < 7; i++)
		inb(u_DRAMIO);
}

static void gus_poke(long addr, unsigned char data)
{				/* Writes a byte to the DRAM */
	outb((0x43), u_Command);
	outb((addr & 0xff), u_DataLo);
	outb(((addr >> 8) & 0xff), u_DataHi);

	outb((0x44), u_Command);
	outb(((addr >> 16) & 0xff), u_DataHi);
	outb((data), u_DRAMIO);
}

static unsigned char gus_peek(long addr)
{				/* Reads a byte from the DRAM */
	unsigned char   tmp;

	outb((0x43), u_Command);
	outb((addr & 0xff), u_DataLo);
	outb(((addr >> 8) & 0xff), u_DataHi);

	outb((0x44), u_Command);
	outb(((addr >> 16) & 0xff), u_DataHi);
	tmp = inb(u_DRAMIO);

	return tmp;
}

void gus_write8(int reg, unsigned int data)
{				/* Writes to an indirect register (8 bit) */
	outb((reg), u_Command);
	outb(((unsigned char) (data & 0xff)), u_DataHi);
}

static unsigned char gus_read8(int reg)
{				
	/* Reads from an indirect register (8 bit). Offset 0x80. */
	unsigned char   val;

	outb((reg | 0x80), u_Command);
	val = inb(u_DataHi);

	return val;
}

static unsigned char gus_look8(int reg)
{
	/* Reads from an indirect register (8 bit). No additional offset. */
	unsigned char   val;

	outb((reg), u_Command);
	val = inb(u_DataHi);

	return val;
}

static void gus_write16(int reg, unsigned int data)
{
	/* Writes to an indirect register (16 bit) */
	outb((reg), u_Command);

	outb(((unsigned char) (data & 0xff)), u_DataLo);
	outb(((unsigned char) ((data >> 8) & 0xff)), u_DataHi);
}

static unsigned short gus_read16(int reg)
{
	/* Reads from an indirect register (16 bit). Offset 0x80. */
	unsigned char   hi, lo;

	outb((reg | 0x80), u_Command);

	lo = inb(u_DataLo);
	hi = inb(u_DataHi);

	return ((hi << 8) & 0xff00) | lo;
}

static unsigned short gus_look16(int reg)
{		
	/* Reads from an indirect register (16 bit). No additional offset. */
	unsigned char   hi, lo;

	outb((reg), u_Command);

	lo = inb(u_DataLo);
	hi = inb(u_DataHi);

	return ((hi << 8) & 0xff00) | lo;
}

static void gus_write_addr(int reg, unsigned long address, int frac, int is16bit)
{
	/* Writes an 24 bit memory address */
	unsigned long   hold_address;

	if (is16bit)
	{
		if (iw_mode)
		{
			/* Interwave spesific address translations */
			address >>= 1;
		}
		else
		{
			/*
			 * Special processing required for 16 bit patches
			 */

			hold_address = address;
			address = address >> 1;
			address &= 0x0001ffffL;
			address |= (hold_address & 0x000c0000L);
		}
	}
	gus_write16(reg, (unsigned short) ((address >> 7) & 0xffff));
	gus_write16(reg + 1, (unsigned short) ((address << 9) & 0xffff)
		    + (frac << 5));
	/* Could writing twice fix problems with GUS_VOICE_POS()? Let's try. */
	gus_delay();
	gus_write16(reg, (unsigned short) ((address >> 7) & 0xffff));
	gus_write16(reg + 1, (unsigned short) ((address << 9) & 0xffff)
		    + (frac << 5));
}

static void gus_select_voice(int voice)
{
	if (voice < 0 || voice > 31)
		return;
	outb((voice), u_Voice);
}

static void gus_select_max_voices(int nvoices)
{
	if (iw_mode)
		nvoices = 32;
	if (nvoices < 14)
		nvoices = 14;
	if (nvoices > 32)
		nvoices = 32;

	voice_alloc->max_voice = nr_voices = nvoices;
	gus_write8(0x0e, (nvoices - 1) | 0xc0);
}

static void gus_voice_on(unsigned int mode)
{
	gus_write8(0x00, (unsigned char) (mode & 0xfc));
	gus_delay();
	gus_write8(0x00, (unsigned char) (mode & 0xfc));
}

static void gus_voice_off(void)
{
	gus_write8(0x00, gus_read8(0x00) | 0x03);
}

static void gus_voice_mode(unsigned int m)
{
	unsigned char   mode = (unsigned char) (m & 0xff);

	gus_write8(0x00, (gus_read8(0x00) & 0x03) |
		   (mode & 0xfc));	/* Don't touch last two bits */
	gus_delay();
	gus_write8(0x00, (gus_read8(0x00) & 0x03) | (mode & 0xfc));
}

static void gus_voice_freq(unsigned long freq)
{
	unsigned long   divisor = freq_div_table[nr_voices - 14];
	unsigned short  fc;

	/* Interwave plays at 44100 Hz with any number of voices */
	if (iw_mode)
		fc = (unsigned short) (((freq << 9) + (44100 >> 1)) / 44100);
	else
		fc = (unsigned short) (((freq << 9) + (divisor >> 1)) / divisor);
	fc = fc << 1;

	gus_write16(0x01, fc);
}

static void gus_voice_volume(unsigned int vol)
{
	gus_write8(0x0d, 0x03);	/* Stop ramp before setting volume */
	gus_write16(0x09, (unsigned short) (vol << 4));
}

static void gus_voice_balance(unsigned int balance)
{
	gus_write8(0x0c, (unsigned char) (balance & 0xff));
}

static void gus_ramp_range(unsigned int low, unsigned int high)
{
	gus_write8(0x07, (unsigned char) ((low >> 4) & 0xff));
	gus_write8(0x08, (unsigned char) ((high >> 4) & 0xff));
}

static void gus_ramp_rate(unsigned int scale, unsigned int rate)
{
	gus_write8(0x06, (unsigned char) (((scale & 0x03) << 6) | (rate & 0x3f)));
}

static void gus_rampon(unsigned int m)
{
	unsigned char   mode = (unsigned char) (m & 0xff);

	gus_write8(0x0d, mode & 0xfc);
	gus_delay();
	gus_write8(0x0d, mode & 0xfc);
}

static void gus_ramp_mode(unsigned int m)
{
	unsigned char mode = (unsigned char) (m & 0xff);

	gus_write8(0x0d, (gus_read8(0x0d) & 0x03) |
		   (mode & 0xfc));	/* Leave the last 2 bits alone */
	gus_delay();
	gus_write8(0x0d, (gus_read8(0x0d) & 0x03) | (mode & 0xfc));
}

static void gus_rampoff(void)
{
	gus_write8(0x0d, 0x03);
}

static void gus_set_voice_pos(int voice, long position)
{
	int sample_no;

	if ((sample_no = sample_map[voice]) != -1) {
		if (position < samples[sample_no].len) {
			if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
				voices[voice].offset_pending = position;
			else
				gus_write_addr(0x0a, sample_ptrs[sample_no] + position, 0,
				 samples[sample_no].mode & WAVE_16_BITS);
		}
	}
}

static void gus_voice_init(int voice)
{
	unsigned long   flags;

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);
	gus_voice_volume(0);
	gus_voice_off();
	gus_write_addr(0x0a, 0, 0, 0);	/* Set current position to 0 */
	gus_write8(0x00, 0x03);	/* Voice off */
	gus_write8(0x0d, 0x03);	/* Ramping off */
	voice_alloc->map[voice] = 0;
	voice_alloc->alloc_times[voice] = 0;
	spin_unlock_irqrestore(&gus_lock,flags);

}

static void gus_voice_init2(int voice)
{
	voices[voice].panning = 0;
	voices[voice].mode = 0;
	voices[voice].orig_freq = 20000;
	voices[voice].current_freq = 20000;
	voices[voice].bender = 0;
	voices[voice].bender_range = 200;
	voices[voice].initial_volume = 0;
	voices[voice].current_volume = 0;
	voices[voice].loop_irq_mode = 0;
	voices[voice].loop_irq_parm = 0;
	voices[voice].volume_irq_mode = 0;
	voices[voice].volume_irq_parm = 0;
	voices[voice].env_phase = 0;
	voices[voice].main_vol = 127;
	voices[voice].patch_vol = 127;
	voices[voice].expression_vol = 127;
	voices[voice].sample_pending = -1;
	voices[voice].fixed_pitch = 0;
}

static void step_envelope(int voice)
{
	unsigned        vol, prev_vol, phase;
	unsigned char   rate;
	unsigned long flags;

	if (voices[voice].mode & WAVE_SUSTAIN_ON && voices[voice].env_phase == 2)
	{
		spin_lock_irqsave(&gus_lock,flags);
		gus_select_voice(voice);
		gus_rampoff();
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
		/*
		 * Sustain phase begins. Continue envelope after receiving note off.
		 */
	}
	if (voices[voice].env_phase >= 5)
	{
		/* Envelope finished. Shoot the voice down */
		gus_voice_init(voice);
		return;
	}
	prev_vol = voices[voice].current_volume;
	phase = ++voices[voice].env_phase;
	compute_volume(voice, voices[voice].midi_volume);
	vol = voices[voice].initial_volume * voices[voice].env_offset[phase] / 255;
	rate = voices[voice].env_rate[phase];

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);

	gus_voice_volume(prev_vol);


	gus_write8(0x06, rate);	/* Ramping rate */

	voices[voice].volume_irq_mode = VMODE_ENVELOPE;

	if (((vol - prev_vol) / 64) == 0)	/* No significant volume change */
	{
		spin_unlock_irqrestore(&gus_lock,flags);
		step_envelope(voice);		/* Continue the envelope on the next step */
		return;
	}
	if (vol > prev_vol)
	{
		if (vol >= (4096 - 64))
			vol = 4096 - 65;
		gus_ramp_range(0, vol);
		gus_rampon(0x20);	/* Increasing volume, with IRQ */
	}
	else
	{
		if (vol <= 64)
			vol = 65;
		gus_ramp_range(vol, 4030);
		gus_rampon(0x60);	/* Decreasing volume, with IRQ */
	}
	voices[voice].current_volume = vol;
	spin_unlock_irqrestore(&gus_lock,flags);
}

static void init_envelope(int voice)
{
	voices[voice].env_phase = -1;
	voices[voice].current_volume = 64;

	step_envelope(voice);
}

static void start_release(int voice)
{
	if (gus_read8(0x00) & 0x03)
		return;		/* Voice already stopped */

	voices[voice].env_phase = 2;	/* Will be incremented by step_envelope */

	voices[voice].current_volume = voices[voice].initial_volume =
						gus_read16(0x09) >> 4;	/* Get current volume */

	voices[voice].mode &= ~WAVE_SUSTAIN_ON;
	gus_rampoff();
	step_envelope(voice);
}

static void gus_voice_fade(int voice)
{
	int instr_no = sample_map[voice], is16bits;
	unsigned long flags;

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);

	if (instr_no < 0 || instr_no > MAX_SAMPLE)
	{
		gus_write8(0x00, 0x03);	/* Hard stop */
		voice_alloc->map[voice] = 0;
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
	}
	is16bits = (samples[instr_no].mode & WAVE_16_BITS) ? 1 : 0;	/* 8 or 16 bits */

	if (voices[voice].mode & WAVE_ENVELOPES)
	{
		start_release(voice);
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
	}
	/*
	 * Ramp the volume down but not too quickly.
	 */
	if ((int) (gus_read16(0x09) >> 4) < 100)	/* Get current volume */
	{
		gus_voice_off();
		gus_rampoff();
		gus_voice_init(voice);
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
	}
	gus_ramp_range(65, 4030);
	gus_ramp_rate(2, 4);
	gus_rampon(0x40 | 0x20);	/* Down, once, with IRQ */
	voices[voice].volume_irq_mode = VMODE_HALT;
	spin_unlock_irqrestore(&gus_lock,flags);
}

static void gus_reset(void)
{
	int i;

	gus_select_max_voices(24);
	volume_base = 3071;
	volume_scale = 4;
	volume_method = VOL_METHOD_ADAGIO;

	for (i = 0; i < 32; i++)
	{
		gus_voice_init(i);	/* Turn voice off */
		gus_voice_init2(i);
	}
}

static void gus_initialize(void)
{
	unsigned long flags;
	unsigned char dma_image, irq_image, tmp;

	static unsigned char gus_irq_map[16] = 	{
		0, 0, 0, 3, 0, 2, 0, 4, 0, 1, 0, 5, 6, 0, 0, 7
	};

	static unsigned char gus_dma_map[8] = {
		0, 1, 0, 2, 0, 3, 4, 5
	};

	spin_lock_irqsave(&gus_lock,flags);
	gus_write8(0x4c, 0);	/* Reset GF1 */
	gus_delay();
	gus_delay();

	gus_write8(0x4c, 1);	/* Release Reset */
	gus_delay();
	gus_delay();

	/*
	 * Clear all interrupts
	 */

	gus_write8(0x41, 0);	/* DMA control */
	gus_write8(0x45, 0);	/* Timer control */
	gus_write8(0x49, 0);	/* Sample control */

	gus_select_max_voices(24);

	inb(u_Status);		/* Touch the status register */

	gus_look8(0x41);	/* Clear any pending DMA IRQs */
	gus_look8(0x49);	/* Clear any pending sample IRQs */
	gus_read8(0x0f);	/* Clear pending IRQs */

	gus_reset();		/* Resets all voices */

	gus_look8(0x41);	/* Clear any pending DMA IRQs */
	gus_look8(0x49);	/* Clear any pending sample IRQs */
	gus_read8(0x0f);	/* Clear pending IRQs */

	gus_write8(0x4c, 7);	/* Master reset | DAC enable | IRQ enable */

	/*
	 * Set up for Digital ASIC
	 */

	outb((0x05), gus_base + 0x0f);

	mix_image |= 0x02;	/* Disable line out (for a moment) */
	outb((mix_image), u_Mixer);

	outb((0x00), u_IRQDMAControl);

	outb((0x00), gus_base + 0x0f);

	/*
	 * Now set up the DMA and IRQ interface
	 *
	 * The GUS supports two IRQs and two DMAs.
	 *
	 * Just one DMA channel is used. This prevents simultaneous ADC and DAC.
	 * Adding this support requires significant changes to the dmabuf.c, dsp.c
	 * and audio.c also.
	 */

	irq_image = 0;
	tmp = gus_irq_map[gus_irq];
	if (!gus_pnp_flag && !tmp)
		printk(KERN_WARNING "Warning! GUS IRQ not selected\n");
	irq_image |= tmp;
	irq_image |= 0x40;	/* Combine IRQ1 (GF1) and IRQ2 (Midi) */

	dual_dma_mode = 1;
	if (gus_dma2 == gus_dma || gus_dma2 == -1)
	{
		dual_dma_mode = 0;
		dma_image = 0x40;	/* Combine DMA1 (DRAM) and IRQ2 (ADC) */

		tmp = gus_dma_map[gus_dma];
		if (!tmp)
			printk(KERN_WARNING "Warning! GUS DMA not selected\n");

		dma_image |= tmp;
	}
	else
	{
		/* Setup dual DMA channel mode for GUS MAX */

		dma_image = gus_dma_map[gus_dma];
		if (!dma_image)
			printk(KERN_WARNING "Warning! GUS DMA not selected\n");

		tmp = gus_dma_map[gus_dma2] << 3;
		if (!tmp)
		{
			printk(KERN_WARNING "Warning! Invalid GUS MAX DMA\n");
			tmp = 0x40;		/* Combine DMA channels */
			    dual_dma_mode = 0;
		}
		dma_image |= tmp;
	}

	/*
	 * For some reason the IRQ and DMA addresses must be written twice
	 */

	/*
	 * Doing it first time
	 */

	outb((mix_image), u_Mixer);	/* Select DMA control */
	outb((dma_image | 0x80), u_IRQDMAControl);	/* Set DMA address */

	outb((mix_image | 0x40), u_Mixer);	/* Select IRQ control */
	outb((irq_image), u_IRQDMAControl);	/* Set IRQ address */

	/*
	 * Doing it second time
	 */

	outb((mix_image), u_Mixer);	/* Select DMA control */
	outb((dma_image), u_IRQDMAControl);	/* Set DMA address */

	outb((mix_image | 0x40), u_Mixer);	/* Select IRQ control */
	outb((irq_image), u_IRQDMAControl);	/* Set IRQ address */

	gus_select_voice(0);	/* This disables writes to IRQ/DMA reg */

	mix_image &= ~0x02;	/* Enable line out */
	mix_image |= 0x08;	/* Enable IRQ */
	outb((mix_image), u_Mixer);	/*
					 * Turn mixer channels on
					 * Note! Mic in is left off.
					 */

	gus_select_voice(0);	/* This disables writes to IRQ/DMA reg */

	gusintr(gus_irq, (void *)gus_hw_config, NULL);	/* Serve pending interrupts */

	inb(u_Status);		/* Touch the status register */

	gus_look8(0x41);	/* Clear any pending DMA IRQs */
	gus_look8(0x49);	/* Clear any pending sample IRQs */

	gus_read8(0x0f);	/* Clear pending IRQs */

	if (iw_mode)
		gus_write8(0x19, gus_read8(0x19) | 0x01);
	spin_unlock_irqrestore(&gus_lock,flags);
}


static void __init pnp_mem_init(void)
{
#include "iwmem.h"
#define CHUNK_SIZE (256*1024)
#define BANK_SIZE (4*1024*1024)
#define CHUNKS_PER_BANK (BANK_SIZE/CHUNK_SIZE)

	int bank, chunk, addr, total = 0;
	int bank_sizes[4];
	int i, j, bits = -1, testbits = -1, nbanks = 0;

	/*
	 * This routine determines what kind of RAM is installed in each of the four
	 * SIMM banks and configures the DRAM address decode logic accordingly.
	 */

	/*
	 *    Place the chip into enhanced mode
	 */
	gus_write8(0x19, gus_read8(0x19) | 0x01);
	gus_write8(0x53, gus_look8(0x53) & ~0x02);	/* Select DRAM I/O access */

	/*
	 * Set memory configuration to 4 DRAM banks of 4M in each (16M total).
	 */

	gus_write16(0x52, (gus_look16(0x52) & 0xfff0) | 0x000c);

	/*
	 * Perform the DRAM size detection for each bank individually.
	 */
	for (bank = 0; bank < 4; bank++)
	{
		int size = 0;

		addr = bank * BANK_SIZE;

		/* Clean check points of each chunk */
		for (chunk = 0; chunk < CHUNKS_PER_BANK; chunk++)
		{
			gus_poke(addr + chunk * CHUNK_SIZE + 0L, 0x00);
			gus_poke(addr + chunk * CHUNK_SIZE + 1L, 0x00);
		}

		/* Write a value to each chunk point and verify the result */
		for (chunk = 0; chunk < CHUNKS_PER_BANK; chunk++)
		{
			gus_poke(addr + chunk * CHUNK_SIZE + 0L, 0x55);
			gus_poke(addr + chunk * CHUNK_SIZE + 1L, 0xAA);

			if (gus_peek(addr + chunk * CHUNK_SIZE + 0L) == 0x55 &&
				gus_peek(addr + chunk * CHUNK_SIZE + 1L) == 0xAA)
			{
				/* OK. There is RAM. Now check for possible shadows */
				int ok = 1, chunk2;

				for (chunk2 = 0; ok && chunk2 < chunk; chunk2++)
					if (gus_peek(addr + chunk2 * CHUNK_SIZE + 0L) ||
							gus_peek(addr + chunk2 * CHUNK_SIZE + 1L))
						ok = 0;	/* Addressing wraps */

				if (ok)
					size = (chunk + 1) * CHUNK_SIZE;
			}
			gus_poke(addr + chunk * CHUNK_SIZE + 0L, 0x00);
			gus_poke(addr + chunk * CHUNK_SIZE + 1L, 0x00);
		}
		bank_sizes[bank] = size;
		if (size)
			nbanks = bank + 1;
		DDB(printk("Interwave: Bank %d, size=%dk\n", bank, size / 1024));
	}

	if (nbanks == 0)	/* No RAM - Give up */
	{
		printk(KERN_ERR "Sound: An Interwave audio chip detected but no DRAM\n");
		printk(KERN_ERR "Sound: Unable to work with this card.\n");
		gus_write8(0x19, gus_read8(0x19) & ~0x01);
		gus_mem_size = 0;
		return;
	}

	/*
	 * Now we know how much DRAM there is in each bank. The next step is
	 * to find a DRAM size encoding (0 to 12) which is best for the combination
	 * we have.
	 *
	 * First try if any of the possible alternatives matches exactly the amount
	 * of memory we have.
	 */

	for (i = 0; bits == -1 && i < 13; i++)
	{
		bits = i;

		for (j = 0; bits != -1 && j < 4; j++)
			if (mem_decode[i][j] != bank_sizes[j])
				bits = -1;	/* No hit */
	}

	/*
	 * If necessary, try to find a combination where other than the last
	 * bank matches our configuration and the last bank is left oversized.
	 * In this way we don't leave holes in the middle of memory.
	 */

	if (bits == -1)		/* No luck yet */
	{
		for (i = 0; bits == -1 && i < 13; i++)
		{
			bits = i;

			for (j = 0; bits != -1 && j < nbanks - 1; j++)
				if (mem_decode[i][j] != bank_sizes[j])
					bits = -1;	/* No hit */
			if (mem_decode[i][nbanks - 1] < bank_sizes[nbanks - 1])
				bits = -1;	/* The last bank is too small */
		}
	}
	/*
 	 * The last resort is to search for a combination where the banks are
 	 * smaller than the actual SIMMs. This leaves some memory in the banks
 	 * unused but doesn't leave holes in the DRAM address space.
 	 */
 	if (bits == -1)		/* No luck yet */
 	{
 		for (i = 0; i < 13; i++)
 		{
 			testbits = i;
 			for (j = 0; testbits != -1 && j < nbanks - 1; j++)
 				if (mem_decode[i][j] > bank_sizes[j]) {
 					testbits = -1;
 				}
 			if(testbits > bits) bits = testbits;
 		}
 		if (bits != -1)
 		{
			printk(KERN_INFO "Interwave: Can't use all installed RAM.\n");
			printk(KERN_INFO "Interwave: Try reordering SIMMS.\n");
		}
		printk(KERN_INFO "Interwave: Can't find working DRAM encoding.\n");
		printk(KERN_INFO "Interwave: Defaulting to 256k. Try reordering SIMMS.\n");
		bits = 0;
	}
	DDB(printk("Interwave: Selecting DRAM addressing mode %d\n", bits));

	for (bank = 0; bank < 4; bank++)
	{
		DDB(printk("  Bank %d, mem=%dk (limit %dk)\n", bank, bank_sizes[bank] / 1024, mem_decode[bits][bank] / 1024));

		if (bank_sizes[bank] > mem_decode[bits][bank])
			total += mem_decode[bits][bank];
		else
			total += bank_sizes[bank];
	}

	DDB(printk("Total %dk of DRAM (enhanced mode)\n", total / 1024));

	/*
	 *    Set the memory addressing mode.
	 */
	gus_write16(0x52, (gus_look16(0x52) & 0xfff0) | bits);

/*      Leave the chip into enhanced mode. Disable LFO  */
	gus_mem_size = total;
	iw_mode = 1;
	gus_write8(0x19, (gus_read8(0x19) | 0x01) & ~0x02);
}

int __init gus_wave_detect(int baseaddr)
{
	unsigned long   i, max_mem = 1024L;
	unsigned long   loc;
	unsigned char   val;

	if (!request_region(baseaddr, 16, "GUS"))
		return 0;
	if (!request_region(baseaddr + 0x100, 12, "GUS")) { /* 0x10c-> is MAX */
		release_region(baseaddr, 16);
		return 0;
	}

	gus_base = baseaddr;

	gus_write8(0x4c, 0);	/* Reset GF1 */
	gus_delay();
	gus_delay();

	gus_write8(0x4c, 1);	/* Release Reset */
	gus_delay();
	gus_delay();

#ifdef GUSPNP_AUTODETECT
	val = gus_look8(0x5b);	/* Version number register */
	gus_write8(0x5b, ~val);	/* Invert all bits */

	if ((gus_look8(0x5b) & 0xf0) == (val & 0xf0))	/* No change */
	{
		if ((gus_look8(0x5b) & 0x0f) == ((~val) & 0x0f))	/* Change */
		{
			DDB(printk("Interwave chip version %d detected\n", (val & 0xf0) >> 4));
			gus_pnp_flag = 1;
		}
		else
		{
			DDB(printk("Not an Interwave chip (%x)\n", gus_look8(0x5b)));
			gus_pnp_flag = 0;
		}
	}
	gus_write8(0x5b, val);	/* Restore all bits */
#endif

	if (gus_pnp_flag)
		pnp_mem_init();
	if (iw_mode)
		return 1;

	/* See if there is first block there.... */
	gus_poke(0L, 0xaa);
	if (gus_peek(0L) != 0xaa) {
		release_region(baseaddr + 0x100, 12);
		release_region(baseaddr, 16);
		return 0;
	}

	/* Now zero it out so that I can check for mirroring .. */
	gus_poke(0L, 0x00);
	for (i = 1L; i < max_mem; i++)
	{
		int n, failed;

		/* check for mirroring ... */
		if (gus_peek(0L) != 0)
			break;
		loc = i << 10;

		for (n = loc - 1, failed = 0; n <= loc; n++)
		{
			gus_poke(loc, 0xaa);
			if (gus_peek(loc) != 0xaa)
				failed = 1;
			gus_poke(loc, 0x55);
			if (gus_peek(loc) != 0x55)
				failed = 1;
		}
		if (failed)
			break;
	}
	gus_mem_size = i << 10;
	return 1;
}

static int guswave_ioctl(int dev, unsigned int cmd, void __user *arg)
{

	switch (cmd) 
	{
		case SNDCTL_SYNTH_INFO:
			gus_info.nr_voices = nr_voices;
			if (copy_to_user(arg, &gus_info, sizeof(gus_info)))
				return -EFAULT;
			return 0;

		case SNDCTL_SEQ_RESETSAMPLES:
			reset_sample_memory();
			return 0;

		case SNDCTL_SEQ_PERCMODE:
			return 0;

		case SNDCTL_SYNTH_MEMAVL:
			return (gus_mem_size == 0) ? 0 : gus_mem_size - free_mem_ptr - 32;

		default:
			return -EINVAL;
	}
}

static int guswave_set_instr(int dev, int voice, int instr_no)
{
	int sample_no;

	if (instr_no < 0 || instr_no > MAX_PATCH)
		instr_no = 0;	/* Default to acoustic piano */

	if (voice < 0 || voice > 31)
		return -EINVAL;

	if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
	{
		voices[voice].sample_pending = instr_no;
		return 0;
	}
	sample_no = patch_table[instr_no];
	patch_map[voice] = -1;

	if (sample_no == NOT_SAMPLE)
	{
/*		printk("GUS: Undefined patch %d for voice %d\n", instr_no, voice);*/
		return -EINVAL;	/* Patch not defined */
	}
	if (sample_ptrs[sample_no] == -1)	/* Sample not loaded */
	{
/*		printk("GUS: Sample #%d not loaded for patch %d (voice %d)\n", sample_no, instr_no, voice);*/
		return -EINVAL;
	}
	sample_map[voice] = sample_no;
	patch_map[voice] = instr_no;
	return 0;
}

static int guswave_kill_note(int dev, int voice, int note, int velocity)
{
	unsigned long flags;

	spin_lock_irqsave(&gus_lock,flags);
	/* voice_alloc->map[voice] = 0xffff; */
	if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
	{
		voices[voice].kill_pending = 1;
		spin_unlock_irqrestore(&gus_lock,flags);
	}
	else
	{
		spin_unlock_irqrestore(&gus_lock,flags);
		gus_voice_fade(voice);
	}

	return 0;
}

static void guswave_aftertouch(int dev, int voice, int pressure)
{
}

static void guswave_panning(int dev, int voice, int value)
{
	if (voice >= 0 || voice < 32)
		voices[voice].panning = value;
}

static void guswave_volume_method(int dev, int mode)
{
	if (mode == VOL_METHOD_LINEAR || mode == VOL_METHOD_ADAGIO)
		volume_method = mode;
}

static void compute_volume(int voice, int volume)
{
	if (volume < 128)
		voices[voice].midi_volume = volume;

	switch (volume_method)
	{
		case VOL_METHOD_ADAGIO:
			voices[voice].initial_volume =
				gus_adagio_vol(voices[voice].midi_volume, voices[voice].main_vol,
					voices[voice].expression_vol,
					voices[voice].patch_vol);
			break;

		case VOL_METHOD_LINEAR:	/* Totally ignores patch-volume and expression */
			voices[voice].initial_volume = gus_linear_vol(volume, voices[voice].main_vol);
			break;

		default:
			voices[voice].initial_volume = volume_base +
				(voices[voice].midi_volume * volume_scale);
	}

	if (voices[voice].initial_volume > 4030)
		voices[voice].initial_volume = 4030;
}

static void compute_and_set_volume(int voice, int volume, int ramp_time)
{
	int curr, target, rate;
	unsigned long flags;

	compute_volume(voice, volume);
	voices[voice].current_volume = voices[voice].initial_volume;

	spin_lock_irqsave(&gus_lock,flags);
	/*
	 * CAUTION! Interrupts disabled. Enable them before returning
	 */

	gus_select_voice(voice);

	curr = gus_read16(0x09) >> 4;
	target = voices[voice].initial_volume;

	if (ramp_time == INSTANT_RAMP)
	{
		gus_rampoff();
		gus_voice_volume(target);
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
	}
	if (ramp_time == FAST_RAMP)
		rate = 63;
	else
		rate = 16;
	gus_ramp_rate(0, rate);

	if ((target - curr) / 64 == 0)	/* Close enough to target. */
	{
		gus_rampoff();
		gus_voice_volume(target);
		spin_unlock_irqrestore(&gus_lock,flags);
		return;
	}
	if (target > curr)
	{
		if (target > (4095 - 65))
			target = 4095 - 65;
		gus_ramp_range(curr, target);
		gus_rampon(0x00);	/* Ramp up, once, no IRQ */
	}
	else
	{
		if (target < 65)
			target = 65;

		gus_ramp_range(target, curr);
		gus_rampon(0x40);	/* Ramp down, once, no irq */
	}
	spin_unlock_irqrestore(&gus_lock,flags);
}

static void dynamic_volume_change(int voice)
{
	unsigned char status;
	unsigned long flags;

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);
	status = gus_read8(0x00);	/* Get voice status */
	spin_unlock_irqrestore(&gus_lock,flags);

	if (status & 0x03)
		return;		/* Voice was not running */

	if (!(voices[voice].mode & WAVE_ENVELOPES))
	{
		compute_and_set_volume(voice, voices[voice].midi_volume, 1);
		return;
	}
	
	/*
	 * Voice is running and has envelopes.
	 */

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);
	status = gus_read8(0x0d);	/* Ramping status */
	spin_unlock_irqrestore(&gus_lock,flags);

	if (status & 0x03)	/* Sustain phase? */
	{
		compute_and_set_volume(voice, voices[voice].midi_volume, 1);
		return;
	}
	if (voices[voice].env_phase < 0)
		return;

	compute_volume(voice, voices[voice].midi_volume);

}

static void guswave_controller(int dev, int voice, int ctrl_num, int value)
{
	unsigned long   flags;
	unsigned long   freq;

	if (voice < 0 || voice > 31)
		return;

	switch (ctrl_num)
	{
		case CTRL_PITCH_BENDER:
			voices[voice].bender = value;

			if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
			{
				freq = compute_finetune(voices[voice].orig_freq, value, voices[voice].bender_range, 0);
				voices[voice].current_freq = freq;

				spin_lock_irqsave(&gus_lock,flags);
				gus_select_voice(voice);
				gus_voice_freq(freq);
				spin_unlock_irqrestore(&gus_lock,flags);
			}
			break;

		case CTRL_PITCH_BENDER_RANGE:
			voices[voice].bender_range = value;
			break;
		case CTL_EXPRESSION:
			value /= 128;
		case CTRL_EXPRESSION:
			if (volume_method == VOL_METHOD_ADAGIO)
			{
				voices[voice].expression_vol = value;
				if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
					dynamic_volume_change(voice);
			}
			break;

		case CTL_PAN:
			voices[voice].panning = (value * 2) - 128;
			break;

		case CTL_MAIN_VOLUME:
			value = (value * 100) / 16383;

		case CTRL_MAIN_VOLUME:
			voices[voice].main_vol = value;
			if (voices[voice].volume_irq_mode != VMODE_START_NOTE)
				dynamic_volume_change(voice);
			break;

		default:
			break;
	}
}

static int guswave_start_note2(int dev, int voice, int note_num, int volume)
{
	int sample, best_sample, best_delta, delta_freq;
	int is16bits, samplep, patch, pan;
	unsigned long   note_freq, base_note, freq, flags;
	unsigned char   mode = 0;

	if (voice < 0 || voice > 31)
	{
/*		printk("GUS: Invalid voice\n");*/
		return -EINVAL;
	}
	if (note_num == 255)
	{
		if (voices[voice].mode & WAVE_ENVELOPES)
		{
			voices[voice].midi_volume = volume;
			dynamic_volume_change(voice);
			return 0;
		}
		compute_and_set_volume(voice, volume, 1);
		return 0;
	}
	if ((patch = patch_map[voice]) == -1)
		return -EINVAL;
	if ((samplep = patch_table[patch]) == NOT_SAMPLE)
	{
		return -EINVAL;
	}
	note_freq = note_to_freq(note_num);

	/*
	 * Find a sample within a patch so that the note_freq is between low_note
	 * and high_note.
	 */
	sample = -1;

	best_sample = samplep;
	best_delta = 1000000;
	while (samplep != 0 && samplep != NOT_SAMPLE && sample == -1)
	{
		delta_freq = note_freq - samples[samplep].base_note;
		if (delta_freq < 0)
			delta_freq = -delta_freq;
		if (delta_freq < best_delta)
		{
			best_sample = samplep;
			best_delta = delta_freq;
		}
		if (samples[samplep].low_note <= note_freq &&
			note_freq <= samples[samplep].high_note)
		{
			sample = samplep;
		}
		else
			samplep = samples[samplep].key;	/* Link to next sample */
	  }
	if (sample == -1)
		sample = best_sample;

	if (sample == -1)
	{
/*		printk("GUS: Patch %d not defined for note %d\n", patch, note_num);*/
		return 0;	/* Should play default patch ??? */
	}
	is16bits = (samples[sample].mode & WAVE_16_BITS) ? 1 : 0;
	voices[voice].mode = samples[sample].mode;
	voices[voice].patch_vol = samples[sample].volume;

	if (iw_mode)
		gus_write8(0x15, 0x00);		/* RAM, Reset voice deactivate bit of SMSI */

	if (voices[voice].mode & WAVE_ENVELOPES)
	{
		int i;

		for (i = 0; i < 6; i++)
		{
			voices[voice].env_rate[i] = samples[sample].env_rate[i];
			voices[voice].env_offset[i] = samples[sample].env_offset[i];
		}
	}
	sample_map[voice] = sample;

	if (voices[voice].fixed_pitch)	/* Fixed pitch */
	{
		  freq = samples[sample].base_freq;
	}
	else
	{
		base_note = samples[sample].base_note / 100;
		note_freq /= 100;

		freq = samples[sample].base_freq * note_freq / base_note;
	}

	voices[voice].orig_freq = freq;

	/*
	 * Since the pitch bender may have been set before playing the note, we
	 * have to calculate the bending now.
	 */

	freq = compute_finetune(voices[voice].orig_freq, voices[voice].bender,
				voices[voice].bender_range, 0);
	voices[voice].current_freq = freq;

	pan = (samples[sample].panning + voices[voice].panning) / 32;
	pan += 7;
	if (pan < 0)
		pan = 0;
	if (pan > 15)
		pan = 15;

	if (samples[sample].mode & WAVE_16_BITS)
	{
		mode |= 0x04;	/* 16 bits */
		if ((sample_ptrs[sample] / GUS_BANK_SIZE) !=
			((sample_ptrs[sample] + samples[sample].len) / GUS_BANK_SIZE))
				printk(KERN_ERR "GUS: Sample address error\n");
	}
	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);
	gus_voice_off();
	gus_rampoff();

	spin_unlock_irqrestore(&gus_lock,flags);

	if (voices[voice].mode & WAVE_ENVELOPES)
	{
		compute_volume(voice, volume);
		init_envelope(voice);
	}
	else
	{
		compute_and_set_volume(voice, volume, 0);
	}

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);

	if (samples[sample].mode & WAVE_LOOP_BACK)
		gus_write_addr(0x0a, sample_ptrs[sample] + samples[sample].len -
			voices[voice].offset_pending, 0, is16bits);	/* start=end */
	else
		gus_write_addr(0x0a, sample_ptrs[sample] + voices[voice].offset_pending, 0, is16bits);	/* Sample start=begin */

	if (samples[sample].mode & WAVE_LOOPING)
	{
		mode |= 0x08;

		if (samples[sample].mode & WAVE_BIDIR_LOOP)
			mode |= 0x10;

		if (samples[sample].mode & WAVE_LOOP_BACK)
		{
			gus_write_addr(0x0a, sample_ptrs[sample] + samples[sample].loop_end -
					   voices[voice].offset_pending,
					   (samples[sample].fractions >> 4) & 0x0f, is16bits);
			mode |= 0x40;
		}
		gus_write_addr(0x02, sample_ptrs[sample] + samples[sample].loop_start,
			samples[sample].fractions & 0x0f, is16bits);	/* Loop start location */
		gus_write_addr(0x04, sample_ptrs[sample] + samples[sample].loop_end,
			(samples[sample].fractions >> 4) & 0x0f, is16bits);	/* Loop end location */
	}
	else
	{
		mode |= 0x20;	/* Loop IRQ at the end */
		voices[voice].loop_irq_mode = LMODE_FINISH;	/* Ramp down at the end */
		voices[voice].loop_irq_parm = 1;
		gus_write_addr(0x02, sample_ptrs[sample], 0, is16bits);	/* Loop start location */
		gus_write_addr(0x04, sample_ptrs[sample] + samples[sample].len - 1,
			(samples[sample].fractions >> 4) & 0x0f, is16bits);	/* Loop end location */
	}
	gus_voice_freq(freq);
	gus_voice_balance(pan);
	gus_voice_on(mode);
	spin_unlock_irqrestore(&gus_lock,flags);

	return 0;
}

/*
 * New guswave_start_note by Andrew J. Robinson attempts to minimize clicking
 * when the note playing on the voice is changed.  It uses volume
 * ramping.
 */

static int guswave_start_note(int dev, int voice, int note_num, int volume)
{
	unsigned long flags;
	int mode;
	int ret_val = 0;

	spin_lock_irqsave(&gus_lock,flags);
	if (note_num == 255)
	{
		if (voices[voice].volume_irq_mode == VMODE_START_NOTE)
		{
			voices[voice].volume_pending = volume;
		}
		else
		{
			ret_val = guswave_start_note2(dev, voice, note_num, volume);
		}
	}
	else
	{
		gus_select_voice(voice);
		mode = gus_read8(0x00);
		if (mode & 0x20)
			gus_write8(0x00, mode & 0xdf);	/* No interrupt! */

		voices[voice].offset_pending = 0;
		voices[voice].kill_pending = 0;
		voices[voice].volume_irq_mode = 0;
		voices[voice].loop_irq_mode = 0;

		if (voices[voice].sample_pending >= 0)
		{
			spin_unlock_irqrestore(&gus_lock,flags);	/* Run temporarily with interrupts enabled */
			guswave_set_instr(voices[voice].dev_pending, voice, voices[voice].sample_pending);
			voices[voice].sample_pending = -1;
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);	/* Reselect the voice (just to be sure) */
		}
		if ((mode & 0x01) || (int) ((gus_read16(0x09) >> 4) < (unsigned) 2065))
		{
			ret_val = guswave_start_note2(dev, voice, note_num, volume);
		}
		else
		{
			voices[voice].dev_pending = dev;
			voices[voice].note_pending = note_num;
			voices[voice].volume_pending = volume;
			voices[voice].volume_irq_mode = VMODE_START_NOTE;

			gus_rampoff();
			gus_ramp_range(2000, 4065);
			gus_ramp_rate(0, 63);	/* Fastest possible rate */
			gus_rampon(0x20 | 0x40);	/* Ramp down, once, irq */
		}
	}
	spin_unlock_irqrestore(&gus_lock,flags);
	return ret_val;
}

static void guswave_reset(int dev)
{
	int i;

	for (i = 0; i < 32; i++)
	{
		gus_voice_init(i);
		gus_voice_init2(i);
	}
}

static int guswave_open(int dev, int mode)
{
	int err;

	if (gus_busy)
		return -EBUSY;

	voice_alloc->timestamp = 0;

	if (gus_no_wave_dma) {
		gus_no_dma = 1;
	} else {
		if ((err = DMAbuf_open_dma(gus_devnum)) < 0)
		{
			/* printk( "GUS: Loading samples without DMA\n"); */
			gus_no_dma = 1;	/* Upload samples using PIO */
		}
		else
			gus_no_dma = 0;
	}

	init_waitqueue_head(&dram_sleeper);
	gus_busy = 1;
	active_device = GUS_DEV_WAVE;

	gusintr(gus_irq, (void *)gus_hw_config, NULL);	/* Serve pending interrupts */
	gus_initialize();
	gus_reset();
	gusintr(gus_irq, (void *)gus_hw_config, NULL);	/* Serve pending interrupts */

	return 0;
}

static void guswave_close(int dev)
{
	gus_busy = 0;
	active_device = 0;
	gus_reset();

	if (!gus_no_dma)
		DMAbuf_close_dma(gus_devnum);
}

static int guswave_load_patch(int dev, int format, const char __user *addr,
		   int offs, int count, int pmgr_flag)
{
	struct patch_info patch;
	int instr;
	long sizeof_patch;

	unsigned long blk_sz, blk_end, left, src_offs, target;

	sizeof_patch = (long) &patch.data[0] - (long) &patch;	/* Header size */

	if (format != GUS_PATCH)
	{
/*		printk("GUS Error: Invalid patch format (key) 0x%x\n", format);*/
		return -EINVAL;
	}
	if (count < sizeof_patch)
	{
/*		  printk("GUS Error: Patch header too short\n");*/
		  return -EINVAL;
	}
	count -= sizeof_patch;

	if (free_sample >= MAX_SAMPLE)
	{
/*		  printk("GUS: Sample table full\n");*/
		  return -ENOSPC;
	}
	/*
	 * Copy the header from user space but ignore the first bytes which have
	 * been transferred already.
	 */

	if (copy_from_user(&((char *) &patch)[offs], &(addr)[offs],
			   sizeof_patch - offs))
		return -EFAULT;

	if (patch.mode & WAVE_ROM)
		return -EINVAL;
	if (gus_mem_size == 0)
		return -ENOSPC;

	instr = patch.instr_no;

	if (instr < 0 || instr > MAX_PATCH)
	{
/*		printk(KERN_ERR "GUS: Invalid patch number %d\n", instr);*/
		return -EINVAL;
	}
	if (count < patch.len)
	{
/*		printk(KERN_ERR "GUS Warning: Patch record too short (%d<%d)\n", count, (int) patch.len);*/
		patch.len = count;
	}
	if (patch.len <= 0 || patch.len > gus_mem_size)
	{
/*		printk(KERN_ERR "GUS: Invalid sample length %d\n", (int) patch.len);*/
		return -EINVAL;
	}
	if (patch.mode & WAVE_LOOPING)
	{
		if (patch.loop_start < 0 || patch.loop_start >= patch.len)
		{
/*			printk(KERN_ERR "GUS: Invalid loop start\n");*/
			return -EINVAL;
		}
		if (patch.loop_end < patch.loop_start || patch.loop_end > patch.len)
		{
/*			printk(KERN_ERR "GUS: Invalid loop end\n");*/
			return -EINVAL;
		}
	}
	free_mem_ptr = (free_mem_ptr + 31) & ~31;	/* 32 byte alignment */

	if (patch.mode & WAVE_16_BITS)
	{
		/*
		 * 16 bit samples must fit one 256k bank.
		 */
		if (patch.len >= GUS_BANK_SIZE)
		{
/*			 printk("GUS: Sample (16 bit) too long %d\n", (int) patch.len);*/
			return -ENOSPC;
		}
		if ((free_mem_ptr / GUS_BANK_SIZE) !=
			((free_mem_ptr + patch.len) / GUS_BANK_SIZE))
		{
			unsigned long   tmp_mem =	
				/* Align to 256K */
					((free_mem_ptr / GUS_BANK_SIZE) + 1) * GUS_BANK_SIZE;

			if ((tmp_mem + patch.len) > gus_mem_size)
				return -ENOSPC;

			free_mem_ptr = tmp_mem;		/* This leaves unusable memory */
		}
	}
	if ((free_mem_ptr + patch.len) > gus_mem_size)
		return -ENOSPC;

	sample_ptrs[free_sample] = free_mem_ptr;

	/*
	 * Tremolo is not possible with envelopes
	 */

	if (patch.mode & WAVE_ENVELOPES)
		patch.mode &= ~WAVE_TREMOLO;

	if (!(patch.mode & WAVE_FRACTIONS))
	{
		  patch.fractions = 0;
	}
	memcpy((char *) &samples[free_sample], &patch, sizeof_patch);

	/*
	 * Link this_one sample to the list of samples for patch 'instr'.
	 */

	samples[free_sample].key = patch_table[instr];
	patch_table[instr] = free_sample;

	/*
	 * Use DMA to transfer the wave data to the DRAM
	 */

	left = patch.len;
	src_offs = 0;
	target = free_mem_ptr;

	while (left)		/* Not completely transferred yet */
	{
		blk_sz = audio_devs[gus_devnum]->dmap_out->bytes_in_use;
		if (blk_sz > left)
			blk_sz = left;

		/*
		 * DMA cannot cross bank (256k) boundaries. Check for that.
		 */
		 
		blk_end = target + blk_sz;

		if ((target / GUS_BANK_SIZE) != (blk_end / GUS_BANK_SIZE))
		{
			/* Split the block */
			blk_end &= ~(GUS_BANK_SIZE - 1);
			blk_sz = blk_end - target;
		}
		if (gus_no_dma)
		{
			/*
			 * For some reason the DMA is not possible. We have to use PIO.
			 */
			long i;
			unsigned char data;

			for (i = 0; i < blk_sz; i++)
			{
				get_user(*(unsigned char *) &data, (unsigned char __user *) &((addr)[sizeof_patch + i]));
				if (patch.mode & WAVE_UNSIGNED)
					if (!(patch.mode & WAVE_16_BITS) || (i & 0x01))
						data ^= 0x80;	/* Convert to signed */
				gus_poke(target + i, data);
			}
		}
		else
		{
			unsigned long address, hold_address;
			unsigned char dma_command;
			unsigned long flags;

			if (audio_devs[gus_devnum]->dmap_out->raw_buf == NULL)
			{
				printk(KERN_ERR "GUS: DMA buffer == NULL\n");
				return -ENOSPC;
			}
			/*
			 * OK, move now. First in and then out.
			 */

			if (copy_from_user(audio_devs[gus_devnum]->dmap_out->raw_buf,
					   &(addr)[sizeof_patch + src_offs],
					   blk_sz))
				return -EFAULT;

			spin_lock_irqsave(&gus_lock,flags);
			gus_write8(0x41, 0);	/* Disable GF1 DMA */
			DMAbuf_start_dma(gus_devnum, audio_devs[gus_devnum]->dmap_out->raw_buf_phys,
				blk_sz, DMA_MODE_WRITE);

			/*
			 * Set the DRAM address for the wave data
			 */

			if (iw_mode)
			{
				/* Different address translation in enhanced mode */

				unsigned char   hi;

				if (gus_dma > 4)
					address = target >> 1;	/* Convert to 16 bit word address */
				else
					address = target;

				hi = (unsigned char) ((address >> 16) & 0xf0);
				hi += (unsigned char) (address & 0x0f);

				gus_write16(0x42, (address >> 4) & 0xffff);	/* DMA address (low) */
				gus_write8(0x50, hi);
			}
			else
			{
				address = target;
				if (audio_devs[gus_devnum]->dmap_out->dma > 3)
				{
					hold_address = address;
					address = address >> 1;
					address &= 0x0001ffffL;
					address |= (hold_address & 0x000c0000L);
				}
				gus_write16(0x42, (address >> 4) & 0xffff);	/* DRAM DMA address */
			}

			/*
			 * Start the DMA transfer
			 */

			dma_command = 0x21;		/* IRQ enable, DMA start */
			if (patch.mode & WAVE_UNSIGNED)
				dma_command |= 0x80;	/* Invert MSB */
			if (patch.mode & WAVE_16_BITS)
				dma_command |= 0x40;	/* 16 bit _DATA_ */
			if (audio_devs[gus_devnum]->dmap_out->dma > 3)
				dma_command |= 0x04;	/* 16 bit DMA _channel_ */
			
			/*
			 * Sleep here until the DRAM DMA done interrupt is served
			 */
			active_device = GUS_DEV_WAVE;
			gus_write8(0x41, dma_command);	/* Lets go luteet (=bugs) */

			spin_unlock_irqrestore(&gus_lock,flags); /* opens a race */
			if (!interruptible_sleep_on_timeout(&dram_sleeper, HZ))
				printk("GUS: DMA Transfer timed out\n");
		}

		/*
		 * Now the next part
		 */

		left -= blk_sz;
		src_offs += blk_sz;
		target += blk_sz;

		gus_write8(0x41, 0);	/* Stop DMA */
	}

	free_mem_ptr += patch.len;
	free_sample++;
	return 0;
}

static void guswave_hw_control(int dev, unsigned char *event_rec)
{
	int voice, cmd;
	unsigned short p1, p2;
	unsigned int plong;
	unsigned long flags;

	cmd = event_rec[2];
	voice = event_rec[3];
	p1 = *(unsigned short *) &event_rec[4];
	p2 = *(unsigned short *) &event_rec[6];
	plong = *(unsigned int *) &event_rec[4];

	if ((voices[voice].volume_irq_mode == VMODE_START_NOTE) &&
		(cmd != _GUS_VOICESAMPLE) && (cmd != _GUS_VOICE_POS))
		do_volume_irq(voice);

	switch (cmd)
	{
		case _GUS_NUMVOICES:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_select_max_voices(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICESAMPLE:
			guswave_set_instr(dev, voice, p1);
			break;

		case _GUS_VOICEON:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			p1 &= ~0x20;	/* Don't allow interrupts */
			gus_voice_on(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEOFF:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_voice_off();
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEFADE:
			gus_voice_fade(voice);
			break;

		case _GUS_VOICEMODE:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			p1 &= ~0x20;	/* Don't allow interrupts */
			gus_voice_mode(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEBALA:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_voice_balance(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEFREQ:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_voice_freq(plong);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEVOL:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_voice_volume(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOICEVOL2:	/* Just update the software voice level */
			voices[voice].initial_volume = voices[voice].current_volume = p1;
			break;

		case _GUS_RAMPRANGE:
			if (voices[voice].mode & WAVE_ENVELOPES)
				break;	/* NO-NO */
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_ramp_range(p1, p2);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_RAMPRATE:
			if (voices[voice].mode & WAVE_ENVELOPES)
				break;	/* NJET-NJET */
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_ramp_rate(p1, p2);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_RAMPMODE:
			if (voices[voice].mode & WAVE_ENVELOPES)
				break;	/* NO-NO */
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			p1 &= ~0x20;	/* Don't allow interrupts */
			gus_ramp_mode(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_RAMPON:
			if (voices[voice].mode & WAVE_ENVELOPES)
				break;	/* EI-EI */
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			p1 &= ~0x20;	/* Don't allow interrupts */
			gus_rampon(p1);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_RAMPOFF:
			if (voices[voice].mode & WAVE_ENVELOPES)
				break;	/* NEJ-NEJ */
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_rampoff();
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		case _GUS_VOLUME_SCALE:
			volume_base = p1;
			volume_scale = p2;
			break;

		case _GUS_VOICE_POS:
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_set_voice_pos(voice, plong);
			spin_unlock_irqrestore(&gus_lock,flags);
			break;

		default:
			break;
	}
}

static int gus_audio_set_speed(int speed)
{
	if (speed <= 0)
		speed = gus_audio_speed;

	if (speed < 4000)
		speed = 4000;

	if (speed > 44100)
		speed = 44100;

	gus_audio_speed = speed;

	if (only_read_access)
	{
		/* Compute nearest valid recording speed  and return it */

		/* speed = (9878400 / (gus_audio_speed + 2)) / 16; */
		speed = (((9878400 + gus_audio_speed / 2) / (gus_audio_speed + 2)) + 8) / 16;
		speed = (9878400 / (speed * 16)) - 2;
	}
	return speed;
}

static int gus_audio_set_channels(int channels)
{
	if (!channels)
		return gus_audio_channels;
	if (channels > 2)
		channels = 2;
	if (channels < 1)
		channels = 1;
	gus_audio_channels = channels;
	return channels;
}

static int gus_audio_set_bits(int bits)
{
	if (!bits)
		return gus_audio_bits;

	if (bits != 8 && bits != 16)
		bits = 8;

	if (only_8_bits)
		bits = 8;

	gus_audio_bits = bits;
	return bits;
}

static int gus_audio_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int val;

	switch (cmd) 
	{
		case SOUND_PCM_WRITE_RATE:
			if (get_user(val, (int __user*)arg))
				return -EFAULT;
			val = gus_audio_set_speed(val);
			break;

		case SOUND_PCM_READ_RATE:
			val = gus_audio_speed;
			break;

		case SNDCTL_DSP_STEREO:
			if (get_user(val, (int __user *)arg))
				return -EFAULT;
			val = gus_audio_set_channels(val + 1) - 1;
			break;

		case SOUND_PCM_WRITE_CHANNELS:
			if (get_user(val, (int __user *)arg))
				return -EFAULT;
			val = gus_audio_set_channels(val);
			break;

		case SOUND_PCM_READ_CHANNELS:
			val = gus_audio_channels;
			break;
		
		case SNDCTL_DSP_SETFMT:
			if (get_user(val, (int __user *)arg))
				return -EFAULT;
			val = gus_audio_set_bits(val);
			break;
		
		case SOUND_PCM_READ_BITS:
			val = gus_audio_bits;
			break;
		
		case SOUND_PCM_WRITE_FILTER:		/* NOT POSSIBLE */
		case SOUND_PCM_READ_FILTER:
			val = -EINVAL;
			break;
		default:
			return -EINVAL;
	}
	return put_user(val, (int __user *)arg);
}

static void gus_audio_reset(int dev)
{
	if (recording_active)
	{
		gus_write8(0x49, 0x00);	/* Halt recording */
		set_input_volumes();
	}
}

static int saved_iw_mode;	/* A hack hack hack */

static int gus_audio_open(int dev, int mode)
{
	if (gus_busy)
		return -EBUSY;

	if (gus_pnp_flag && mode & OPEN_READ)
	{
/*		printk(KERN_ERR "GUS: Audio device #%d is playback only.\n", dev);*/
		return -EIO;
	}
	gus_initialize();

	gus_busy = 1;
	active_device = 0;

	saved_iw_mode = iw_mode;
	if (iw_mode)
	{
		/* There are some problems with audio in enhanced mode so disable it */
		gus_write8(0x19, gus_read8(0x19) & ~0x01);	/* Disable enhanced mode */
		iw_mode = 0;
	}

	gus_reset();
	reset_sample_memory();
	gus_select_max_voices(14);

	pcm_active = 0;
	dma_active = 0;
	pcm_opened = 1;
	if (mode & OPEN_READ)
	{
		recording_active = 1;
		set_input_volumes();
	}
	only_read_access = !(mode & OPEN_WRITE);
	only_8_bits = mode & OPEN_READ;
	if (only_8_bits)
		audio_devs[dev]->format_mask = AFMT_U8;
	else
		audio_devs[dev]->format_mask = AFMT_U8 | AFMT_S16_LE;

	return 0;
}

static void gus_audio_close(int dev)
{
	iw_mode = saved_iw_mode;
	gus_reset();
	gus_busy = 0;
	pcm_opened = 0;
	active_device = 0;

	if (recording_active)
	{
		gus_write8(0x49, 0x00);	/* Halt recording */
		set_input_volumes();
	}
	recording_active = 0;
}

static void gus_audio_update_volume(void)
{
	unsigned long flags;
	int voice;

	if (pcm_active && pcm_opened)
		for (voice = 0; voice < gus_audio_channels; voice++)
		{
			spin_lock_irqsave(&gus_lock,flags);
			gus_select_voice(voice);
			gus_rampoff();
			gus_voice_volume(1530 + (25 * gus_pcm_volume));
			gus_ramp_range(65, 1530 + (25 * gus_pcm_volume));
			spin_unlock_irqrestore(&gus_lock,flags);
		}
}

static void play_next_pcm_block(void)
{
	unsigned long flags;
	int speed = gus_audio_speed;
	int this_one, is16bits, chn;
	unsigned long dram_loc;
	unsigned char mode[2], ramp_mode[2];

	if (!pcm_qlen)
		return;

	this_one = pcm_head;

	for (chn = 0; chn < gus_audio_channels; chn++)
	{
		mode[chn] = 0x00;
		ramp_mode[chn] = 0x03;	/* Ramping and rollover off */

		if (chn == 0)
		{
			mode[chn] |= 0x20;	/* Loop IRQ */
			voices[chn].loop_irq_mode = LMODE_PCM;
		}
		if (gus_audio_bits != 8)
		{
			is16bits = 1;
			mode[chn] |= 0x04;	/* 16 bit data */
		}
		else
			is16bits = 0;

		dram_loc = this_one * pcm_bsize;
		dram_loc += chn * pcm_banksize;

		if (this_one == (pcm_nblk - 1))	/* Last fragment of the DRAM buffer */
		{
			mode[chn] |= 0x08;	/* Enable loop */
			ramp_mode[chn] = 0x03;	/* Disable rollover bit */
		}
		else
		{
			if (chn == 0)
				ramp_mode[chn] = 0x04;	/* Enable rollover bit */
		}
		spin_lock_irqsave(&gus_lock,flags);
		gus_select_voice(chn);
		gus_voice_freq(speed);

		if (gus_audio_channels == 1)
			gus_voice_balance(7);		/* mono */
		else if (chn == 0)
			gus_voice_balance(0);		/* left */
		else
			gus_voice_balance(15);		/* right */

		if (!pcm_active)	/* Playback not already active */
		{
			/*
			 * The playback was not started yet (or there has been a pause).
			 * Start the voice (again) and ask for a rollover irq at the end of
			 * this_one block. If this_one one is last of the buffers, use just
			 * the normal loop with irq.
			 */

			gus_voice_off();
			gus_rampoff();
			gus_voice_volume(1530 + (25 * gus_pcm_volume));
			gus_ramp_range(65, 1530 + (25 * gus_pcm_volume));

			gus_write_addr(0x0a, chn * pcm_banksize, 0, is16bits);	/* Starting position */
			gus_write_addr(0x02, chn * pcm_banksize, 0, is16bits);	/* Loop start */

			if (chn != 0)
				gus_write_addr(0x04, pcm_banksize + (pcm_bsize * pcm_nblk) - 1,
						   0, is16bits);	/* Loop end location */
		}
		if (chn == 0)
			gus_write_addr(0x04, dram_loc + pcm_bsize - 1,
					 0, is16bits);	/* Loop end location */
		else
			mode[chn] |= 0x08;	/* Enable looping */
		spin_unlock_irqrestore(&gus_lock,flags);
	}
	for (chn = 0; chn < gus_audio_channels; chn++)
	{
		spin_lock_irqsave(&gus_lock,flags);
		gus_select_voice(chn);
		gus_write8(0x0d, ramp_mode[chn]);
		if (iw_mode)
			gus_write8(0x15, 0x00);	/* Reset voice deactivate bit of SMSI */
		gus_voice_on(mode[chn]);
		spin_unlock_irqrestore(&gus_lock,flags);
	}
	pcm_active = 1;
}

static void gus_transfer_output_block(int dev, unsigned long buf,
			  int total_count, int intrflag, int chn)
{
	/*
	 * This routine transfers one block of audio data to the DRAM. In mono mode
	 * it's called just once. When in stereo mode, this_one routine is called
	 * once for both channels.
	 *
	 * The left/mono channel data is transferred to the beginning of dram and the
	 * right data to the area pointed by gus_page_size.
	 */

	int this_one, count;
	unsigned long flags;
	unsigned char dma_command;
	unsigned long address, hold_address;

	spin_lock_irqsave(&gus_lock,flags);

	count = total_count / gus_audio_channels;

	if (chn == 0)
	{
		if (pcm_qlen >= pcm_nblk)
			printk(KERN_WARNING "GUS Warning: PCM buffers out of sync\n");

		this_one = pcm_current_block = pcm_tail;
		pcm_qlen++;
		pcm_tail = (pcm_tail + 1) % pcm_nblk;
		pcm_datasize[this_one] = count;
	}
	else
		this_one = pcm_current_block;

	gus_write8(0x41, 0);	/* Disable GF1 DMA */
	DMAbuf_start_dma(dev, buf + (chn * count), count, DMA_MODE_WRITE);

	address = this_one * pcm_bsize;
	address += chn * pcm_banksize;

	if (audio_devs[dev]->dmap_out->dma > 3)
	{
		hold_address = address;
		address = address >> 1;
		address &= 0x0001ffffL;
		address |= (hold_address & 0x000c0000L);
	}
	gus_write16(0x42, (address >> 4) & 0xffff);	/* DRAM DMA address */

	dma_command = 0x21;	/* IRQ enable, DMA start */

	if (gus_audio_bits != 8)
		dma_command |= 0x40;	/* 16 bit _DATA_ */
	else
		dma_command |= 0x80;	/* Invert MSB */

	if (audio_devs[dev]->dmap_out->dma > 3)
		dma_command |= 0x04;	/* 16 bit DMA channel */

	gus_write8(0x41, dma_command);	/* Kick start */

	if (chn == (gus_audio_channels - 1))	/* Last channel */
	{
		/*
		 * Last (right or mono) channel data
		 */
		dma_active = 1;	/* DMA started. There is a unacknowledged buffer */
		active_device = GUS_DEV_PCM_DONE;
		if (!pcm_active && (pcm_qlen > 1 || count < pcm_bsize))
		{
			play_next_pcm_block();
		}
	}
	else
	{
		/*
		 * Left channel data. The right channel
		 * is transferred after DMA interrupt
		 */
		active_device = GUS_DEV_PCM_CONTINUE;
	}

	spin_unlock_irqrestore(&gus_lock,flags);
}

static void gus_uninterleave8(char *buf, int l)
{
/* This routine uninterleaves 8 bit stereo output (LRLRLR->LLLRRR) */
	int i, p = 0, halfsize = l / 2;
	char *buf2 = buf + halfsize, *src = bounce_buf;

	memcpy(bounce_buf, buf, l);

	for (i = 0; i < halfsize; i++)
	{
		buf[i] = src[p++];	/* Left channel */
		buf2[i] = src[p++];	/* Right channel */
	}
}

static void gus_uninterleave16(short *buf, int l)
{
/* This routine uninterleaves 16 bit stereo output (LRLRLR->LLLRRR) */
	int i, p = 0, halfsize = l / 2;
	short *buf2 = buf + halfsize, *src = (short *) bounce_buf;

	memcpy(bounce_buf, (char *) buf, l * 2);

	for (i = 0; i < halfsize; i++)
	{
		buf[i] = src[p++];	/* Left channel */
		buf2[i] = src[p++];	/* Right channel */
	}
}

static void gus_audio_output_block(int dev, unsigned long buf, int total_count,
		       int intrflag)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	dmap->flags |= DMA_NODMA | DMA_NOTIMEOUT;

	pcm_current_buf = buf;
	pcm_current_count = total_count;
	pcm_current_intrflag = intrflag;
	pcm_current_dev = dev;
	if (gus_audio_channels == 2)
	{
		char *b = dmap->raw_buf + (buf - dmap->raw_buf_phys);

		if (gus_audio_bits == 8)
			gus_uninterleave8(b, total_count);
		else
			gus_uninterleave16((short *) b, total_count / 2);
	}
	gus_transfer_output_block(dev, buf, total_count, intrflag, 0);
}

static void gus_audio_start_input(int dev, unsigned long buf, int count,
		      int intrflag)
{
	unsigned long flags;
	unsigned char mode;

	spin_lock_irqsave(&gus_lock,flags);

	DMAbuf_start_dma(dev, buf, count, DMA_MODE_READ);
	mode = 0xa0;		/* DMA IRQ enabled, invert MSB */

	if (audio_devs[dev]->dmap_in->dma > 3)
		mode |= 0x04;	/* 16 bit DMA channel */
	if (gus_audio_channels > 1)
		mode |= 0x02;	/* Stereo */
	mode |= 0x01;		/* DMA enable */

	gus_write8(0x49, mode);
	spin_unlock_irqrestore(&gus_lock,flags);
}

static int gus_audio_prepare_for_input(int dev, int bsize, int bcount)
{
	unsigned int rate;

	gus_audio_bsize = bsize;
	audio_devs[dev]->dmap_in->flags |= DMA_NODMA;
	rate = (((9878400 + gus_audio_speed / 2) / (gus_audio_speed + 2)) + 8) / 16;

	gus_write8(0x48, rate & 0xff);	/* Set sampling rate */

	if (gus_audio_bits != 8)
	{
/*		printk("GUS Error: 16 bit recording not supported\n");*/
		return -EINVAL;
	}
	return 0;
}

static int gus_audio_prepare_for_output(int dev, int bsize, int bcount)
{
	int i;

	long mem_ptr, mem_size;

	audio_devs[dev]->dmap_out->flags |= DMA_NODMA | DMA_NOTIMEOUT;
	mem_ptr = 0;
	mem_size = gus_mem_size / gus_audio_channels;

	if (mem_size > (256 * 1024))
		mem_size = 256 * 1024;

	pcm_bsize = bsize / gus_audio_channels;
	pcm_head = pcm_tail = pcm_qlen = 0;

	pcm_nblk = 2;		/* MAX_PCM_BUFFERS; */
	if ((pcm_bsize * pcm_nblk) > mem_size)
		pcm_nblk = mem_size / pcm_bsize;

	for (i = 0; i < pcm_nblk; i++)
		pcm_datasize[i] = 0;

	pcm_banksize = pcm_nblk * pcm_bsize;

	if (gus_audio_bits != 8 && pcm_banksize == (256 * 1024))
		pcm_nblk--;
	gus_write8(0x41, 0);	/* Disable GF1 DMA */
	return 0;
}

static int gus_local_qlen(int dev)
{
	return pcm_qlen;
}


static struct audio_driver gus_audio_driver =
{
	.owner			= THIS_MODULE,
	.open			= gus_audio_open,
	.close			= gus_audio_close,
	.output_block		= gus_audio_output_block,
	.start_input		= gus_audio_start_input,
	.ioctl			= gus_audio_ioctl,
	.prepare_for_input	= gus_audio_prepare_for_input,
	.prepare_for_output	= gus_audio_prepare_for_output,
	.halt_io		= gus_audio_reset,
	.local_qlen		= gus_local_qlen,
};

static void guswave_setup_voice(int dev, int voice, int chn)
{
	struct channel_info *info = &synth_devs[dev]->chn_info[chn];

	guswave_set_instr(dev, voice, info->pgm_num);
	voices[voice].expression_vol = info->controllers[CTL_EXPRESSION];	/* Just MSB */
	voices[voice].main_vol = (info->controllers[CTL_MAIN_VOLUME] * 100) / (unsigned) 128;
	voices[voice].panning = (info->controllers[CTL_PAN] * 2) - 128;
	voices[voice].bender = 0;
	voices[voice].bender_range = info->bender_range;

	if (chn == 9)
		voices[voice].fixed_pitch = 1;
}

static void guswave_bender(int dev, int voice, int value)
{
	int freq;
	unsigned long   flags;

	voices[voice].bender = value - 8192;
	freq = compute_finetune(voices[voice].orig_freq, value - 8192, voices[voice].bender_range, 0);
	voices[voice].current_freq = freq;

	spin_lock_irqsave(&gus_lock,flags);
	gus_select_voice(voice);
	gus_voice_freq(freq);
	spin_unlock_irqrestore(&gus_lock,flags);
}

static int guswave_alloc(int dev, int chn, int note, struct voice_alloc_info *alloc)
{
	int i, p, best = -1, best_time = 0x7fffffff;

	p = alloc->ptr;
	/*
	 * First look for a completely stopped voice
	 */

	for (i = 0; i < alloc->max_voice; i++)
	{
		if (alloc->map[p] == 0)
		{
			alloc->ptr = p;
			return p;
		}
		if (alloc->alloc_times[p] < best_time)
		{
			best = p;
			best_time = alloc->alloc_times[p];
		}
		p = (p + 1) % alloc->max_voice;
	}

	/*
	 * Then look for a releasing voice
	 */

	for (i = 0; i < alloc->max_voice; i++)
	{
		if (alloc->map[p] == 0xffff)
		{
			alloc->ptr = p;
			return p;
		}
		p = (p + 1) % alloc->max_voice;
	}
	if (best >= 0)
		p = best;

	alloc->ptr = p;
	return p;
}

static struct synth_operations guswave_operations =
{
	.owner		= THIS_MODULE,
	.id		= "GUS",
	.info		= &gus_info,
	.midi_dev	= 0,
	.synth_type	= SYNTH_TYPE_SAMPLE,
	.synth_subtype	= SAMPLE_TYPE_GUS,
	.open		= guswave_open,
	.close		= guswave_close,
	.ioctl		= guswave_ioctl,
	.kill_note	= guswave_kill_note,
	.start_note	= guswave_start_note,
	.set_instr	= guswave_set_instr,
	.reset		= guswave_reset,
	.hw_control	= guswave_hw_control,
	.load_patch	= guswave_load_patch,
	.aftertouch	= guswave_aftertouch,
	.controller	= guswave_controller,
	.panning	= guswave_panning,
	.volume_method	= guswave_volume_method,
	.bender		= guswave_bender,
	.alloc_voice	= guswave_alloc,
	.setup_voice	= guswave_setup_voice
};

static void set_input_volumes(void)
{
	unsigned long flags;
	unsigned char mask = 0xff & ~0x06;	/* Just line out enabled */

	if (have_gus_max)	/* Don't disturb GUS MAX */
		return;

	spin_lock_irqsave(&gus_lock,flags);

	/*
	 *    Enable channels having vol > 10%
	 *      Note! bit 0x01 means the line in DISABLED while 0x04 means
	 *            the mic in ENABLED.
	 */
	if (gus_line_vol > 10)
		mask &= ~0x01;
	if (gus_mic_vol > 10)
		mask |= 0x04;

	if (recording_active)
	{
		/*
		 *    Disable channel, if not selected for recording
		 */
		if (!(gus_recmask & SOUND_MASK_LINE))
			mask |= 0x01;
		if (!(gus_recmask & SOUND_MASK_MIC))
			mask &= ~0x04;
	}
	mix_image &= ~0x07;
	mix_image |= mask & 0x07;
	outb((mix_image), u_Mixer);

	spin_unlock_irqrestore(&gus_lock,flags);
}

#define MIX_DEVS	(SOUND_MASK_MIC|SOUND_MASK_LINE| \
			 SOUND_MASK_SYNTH|SOUND_MASK_PCM)

int gus_default_mixer_ioctl(int dev, unsigned int cmd, void __user *arg)
{
	int vol, val;

	if (((cmd >> 8) & 0xff) != 'M')
		return -EINVAL;

	if (!access_ok(VERIFY_WRITE, arg, sizeof(int)))
		return -EFAULT;

	if (_SIOC_DIR(cmd) & _SIOC_WRITE) 
	{
		if (__get_user(val, (int __user *) arg))
			return -EFAULT;

		switch (cmd & 0xff) 
		{
			case SOUND_MIXER_RECSRC:
				gus_recmask = val & MIX_DEVS;
				if (!(gus_recmask & (SOUND_MASK_MIC | SOUND_MASK_LINE)))
					gus_recmask = SOUND_MASK_MIC;
				/* Note! Input volumes are updated during next open for recording */
				val = gus_recmask;
				break;

			case SOUND_MIXER_MIC:
				vol = val & 0xff;
				if (vol < 0)
					vol = 0;
				if (vol > 100)
					vol = 100;
				gus_mic_vol = vol;
				set_input_volumes();
				val = vol | (vol << 8);
				break;
				
			case SOUND_MIXER_LINE:
				vol = val & 0xff;
				if (vol < 0)
					vol = 0;
				if (vol > 100)
					vol = 100;
				gus_line_vol = vol;
				set_input_volumes();
				val = vol | (vol << 8);
				break;

			case SOUND_MIXER_PCM:
				gus_pcm_volume = val & 0xff;
				if (gus_pcm_volume < 0)
					gus_pcm_volume = 0;
				if (gus_pcm_volume > 100)
					gus_pcm_volume = 100;
				gus_audio_update_volume();
				val = gus_pcm_volume | (gus_pcm_volume << 8);
				break;

			case SOUND_MIXER_SYNTH:
				gus_wave_volume = val & 0xff;
				if (gus_wave_volume < 0)
					gus_wave_volume = 0;
				if (gus_wave_volume > 100)
					gus_wave_volume = 100;
				if (active_device == GUS_DEV_WAVE) 
				{
					int voice;
					for (voice = 0; voice < nr_voices; voice++)
					dynamic_volume_change(voice);	/* Apply the new vol */
				}
				val = gus_wave_volume | (gus_wave_volume << 8);
				break;

			default:
				return -EINVAL;
		}
	}
	else
	{
		switch (cmd & 0xff) 
		{
			/*
			 * Return parameters
			 */
			case SOUND_MIXER_RECSRC:
				val = gus_recmask;
				break;
					
			case SOUND_MIXER_DEVMASK:
				val = MIX_DEVS;
				break;

			case SOUND_MIXER_STEREODEVS:
				val = 0;
				break;

			case SOUND_MIXER_RECMASK:
				val = SOUND_MASK_MIC | SOUND_MASK_LINE;
				break;

			case SOUND_MIXER_CAPS:
				val = 0;
				break;

			case SOUND_MIXER_MIC:
				val = gus_mic_vol | (gus_mic_vol << 8);
				break;

			case SOUND_MIXER_LINE:
				val = gus_line_vol | (gus_line_vol << 8);
				break;

			case SOUND_MIXER_PCM:
				val = gus_pcm_volume | (gus_pcm_volume << 8);
				break;

			case SOUND_MIXER_SYNTH:
				val = gus_wave_volume | (gus_wave_volume << 8);
				break;

			default:
				return -EINVAL;
		}
	}
	return __put_user(val, (int __user *)arg);
}

static struct mixer_operations gus_mixer_operations =
{
	.owner	= THIS_MODULE,
	.id	= "GUS",
	.name	= "Gravis Ultrasound",
	.ioctl	= gus_default_mixer_ioctl
};

static int __init gus_default_mixer_init(void)
{
	int n;

	if ((n = sound_alloc_mixerdev()) != -1)
	{	
		/*
		 * Don't install if there is another
		 * mixer
		 */
		mixer_devs[n] = &gus_mixer_operations;
	}
	if (have_gus_max)
	{
		/*
		 *  Enable all mixer channels on the GF1 side. Otherwise recording will
		 *  not be possible using GUS MAX.
		 */
		mix_image &= ~0x07;
		mix_image |= 0x04;	/* All channels enabled */
		outb((mix_image), u_Mixer);
	}
	return n;
}

void __init gus_wave_init(struct address_info *hw_config)
{
	unsigned long flags;
	unsigned char val;
	char *model_num = "2.4";
	char tmp[64];
	int gus_type = 0x24;	/* 2.4 */

	int irq = hw_config->irq, dma = hw_config->dma, dma2 = hw_config->dma2;
	int sdev;

	hw_config->slots[0] = -1;	/* No wave */
	hw_config->slots[1] = -1;	/* No ad1848 */
	hw_config->slots[4] = -1;	/* No audio */
	hw_config->slots[5] = -1;	/* No mixer */

	if (!gus_pnp_flag)
	{
		if (irq < 0 || irq > 15)
		{
			printk(KERN_ERR "ERROR! Invalid IRQ#%d. GUS Disabled", irq);
			return;
		}
	}
	
	if (dma < 0 || dma > 7 || dma == 4)
	{
		printk(KERN_ERR "ERROR! Invalid DMA#%d. GUS Disabled", dma);
		return;
	}
	gus_irq = irq;
	gus_dma = dma;
	gus_dma2 = dma2;
	gus_hw_config = hw_config;

	if (gus_dma2 == -1)
		gus_dma2 = dma;

	/*
	 * Try to identify the GUS model.
	 *
	 *  Versions < 3.6 don't have the digital ASIC. Try to probe it first.
	 */

	spin_lock_irqsave(&gus_lock,flags);
	outb((0x20), gus_base + 0x0f);
	val = inb(gus_base + 0x0f);
	spin_unlock_irqrestore(&gus_lock,flags);

	if (gus_pnp_flag || (val != 0xff && (val & 0x06)))	/* Should be 0x02?? */
	{
		int             ad_flags = 0;

		if (gus_pnp_flag)
			ad_flags = 0x12345678;	/* Interwave "magic" */
		/*
		 * It has the digital ASIC so the card is at least v3.4.
		 * Next try to detect the true model.
		 */

		if (gus_pnp_flag)	/* Hack hack hack */
			val = 10;
		else
			val = inb(u_MixSelect);

		/*
		 * Value 255 means pre-3.7 which don't have mixer.
		 * Values 5 thru 9 mean v3.7 which has a ICS2101 mixer.
		 * 10 and above is GUS MAX which has the CS4231 codec/mixer.
		 *
		 */

		if (val == 255 || val < 5)
		{
			model_num = "3.4";
			gus_type = 0x34;
		}
		else if (val < 10)
		{
			model_num = "3.7";
			gus_type = 0x37;
			mixer_type = ICS2101;
			request_region(u_MixSelect, 1, "GUS mixer");
		}
		else
		{
			struct resource *ports;
			ports = request_region(gus_base + 0x10c, 4, "ad1848");
			model_num = "MAX";
			gus_type = 0x40;
			mixer_type = CS4231;
#ifdef CONFIG_SOUND_GUSMAX
			{
				unsigned char   max_config = 0x40;	/* Codec enable */

				if (gus_dma2 == -1)
					gus_dma2 = gus_dma;

				if (gus_dma > 3)
					max_config |= 0x10;		/* 16 bit capture DMA */

				if (gus_dma2 > 3)
					max_config |= 0x20;		/* 16 bit playback DMA */

				max_config |= (gus_base >> 4) & 0x0f;	/* Extract the X from 2X0 */

				outb((max_config), gus_base + 0x106);	/* UltraMax control */
			}

			if (!ports)
				goto no_cs4231;

			if (ad1848_detect(ports, &ad_flags, hw_config->osp))
			{
				char           *name = "GUS MAX";
				int             old_num_mixers = num_mixers;

				if (gus_pnp_flag)
					name = "GUS PnP";

				gus_mic_vol = gus_line_vol = gus_pcm_volume = 100;
				gus_wave_volume = 90;
				have_gus_max = 1;
				if (hw_config->name)
					name = hw_config->name;

				hw_config->slots[1] = ad1848_init(name, ports,
							-irq, gus_dma2,	/* Playback DMA */
							gus_dma,	/* Capture DMA */
							1,		/* Share DMA channels with GF1 */
							hw_config->osp,
							THIS_MODULE);

				if (num_mixers > old_num_mixers)
				{
					/* GUS has it's own mixer map */
					AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_SYNTH);
					AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_CD);
					AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
				}
			}
			else {
				release_region(gus_base + 0x10c, 4);
			no_cs4231:
				printk(KERN_WARNING "GUS: No CS4231 ??");
			}
#else
			printk(KERN_ERR "GUS MAX found, but not compiled in\n");
#endif
		}
	}
	else
	{
		/*
		 * ASIC not detected so the card must be 2.2 or 2.4.
		 * There could still be the 16-bit/mixer daughter card.
		 */
	}

	if (hw_config->name)
		snprintf(tmp, sizeof(tmp), "%s (%dk)", hw_config->name,
			 (int) gus_mem_size / 1024);
	else if (gus_pnp_flag)
		snprintf(tmp, sizeof(tmp), "Gravis UltraSound PnP (%dk)",
			 (int) gus_mem_size / 1024);
	else
		snprintf(tmp, sizeof(tmp), "Gravis UltraSound %s (%dk)", model_num,
			 (int) gus_mem_size / 1024);


	samples = (struct patch_info *)vmalloc((MAX_SAMPLE + 1) * sizeof(*samples));
	if (samples == NULL)
	{
		printk(KERN_WARNING "gus_init: Cant allocate memory for instrument tables\n");
		return;
	}
	conf_printf(tmp, hw_config);
	strlcpy(gus_info.name, tmp, sizeof(gus_info.name));

	if ((sdev = sound_alloc_synthdev()) == -1)
		printk(KERN_WARNING "gus_init: Too many synthesizers\n");
	else
	{
		voice_alloc = &guswave_operations.alloc;
		if (iw_mode)
			guswave_operations.id = "IWAVE";
		hw_config->slots[0] = sdev;
		synth_devs[sdev] = &guswave_operations;
		sequencer_init();
		gus_tmr_install(gus_base + 8);
	}

	reset_sample_memory();

	gus_initialize();
	
	if ((gus_mem_size > 0) && !gus_no_wave_dma)
	{
		hw_config->slots[4] = -1;
		if ((gus_devnum = sound_install_audiodrv(AUDIO_DRIVER_VERSION,
					"Ultrasound",
					&gus_audio_driver,
					sizeof(struct audio_driver),
					NEEDS_RESTART |
		                   	((!iw_mode && dma2 != dma && dma2 != -1) ?
						DMA_DUPLEX : 0),
					AFMT_U8 | AFMT_S16_LE,
					NULL, dma, dma2)) < 0)
		{
			return;
		}

		hw_config->slots[4] = gus_devnum;
		audio_devs[gus_devnum]->min_fragment = 9;	/* 512k */
		audio_devs[gus_devnum]->max_fragment = 11;	/* 8k (must match size of bounce_buf */
		audio_devs[gus_devnum]->mixer_dev = -1;	/* Next mixer# */
		audio_devs[gus_devnum]->flags |= DMA_HARDSTOP;
	}
	
	/*
	 *  Mixer dependent initialization.
	 */

	switch (mixer_type)
	{
		case ICS2101:
			gus_mic_vol = gus_line_vol = gus_pcm_volume = 100;
			gus_wave_volume = 90;
			request_region(u_MixSelect, 1, "GUS mixer");
			hw_config->slots[5] = ics2101_mixer_init();
			audio_devs[gus_devnum]->mixer_dev = hw_config->slots[5];	/* Next mixer# */
			return;

		case CS4231:
			/* Initialized elsewhere (ad1848.c) */
		default:
			hw_config->slots[5] = gus_default_mixer_init();
			audio_devs[gus_devnum]->mixer_dev = hw_config->slots[5];	/* Next mixer# */
			return;
	}
}

void __exit gus_wave_unload(struct address_info *hw_config)
{
#ifdef CONFIG_SOUND_GUSMAX
	if (have_gus_max)
	{
		ad1848_unload(gus_base + 0x10c,
				-gus_irq,
				gus_dma2,	/* Playback DMA */
				gus_dma,	/* Capture DMA */
				1);	/* Share DMA channels with GF1 */
	}
#endif

	if (mixer_type == ICS2101)
	{
		release_region(u_MixSelect, 1);
	}
	if (hw_config->slots[0] != -1)
		sound_unload_synthdev(hw_config->slots[0]);
	if (hw_config->slots[1] != -1)
		sound_unload_audiodev(hw_config->slots[1]);
	if (hw_config->slots[2] != -1)
		sound_unload_mididev(hw_config->slots[2]);
	if (hw_config->slots[4] != -1)
		sound_unload_audiodev(hw_config->slots[4]);
	if (hw_config->slots[5] != -1)
		sound_unload_mixerdev(hw_config->slots[5]);
	
	vfree(samples);
	samples=NULL;
}
/* called in interrupt context */
static void do_loop_irq(int voice)
{
	unsigned char   tmp;
	int             mode, parm;

	spin_lock(&gus_lock);
	gus_select_voice(voice);

	tmp = gus_read8(0x00);
	tmp &= ~0x20;		/*
				 * Disable wave IRQ for this_one voice
				 */
	gus_write8(0x00, tmp);

	if (tmp & 0x03)		/* Voice stopped */
		voice_alloc->map[voice] = 0;

	mode = voices[voice].loop_irq_mode;
	voices[voice].loop_irq_mode = 0;
	parm = voices[voice].loop_irq_parm;

	switch (mode)
	{
		case LMODE_FINISH:	/*
					 * Final loop finished, shoot volume down
					 */

			if ((int) (gus_read16(0x09) >> 4) < 100)	/*
									 * Get current volume
									 */
			{
				gus_voice_off();
				gus_rampoff();
				gus_voice_init(voice);
				break;
			}
			gus_ramp_range(65, 4065);
			gus_ramp_rate(0, 63);		/*
							 * Fastest possible rate
							 */
			gus_rampon(0x20 | 0x40);	/*
							 * Ramp down, once, irq
							 */
			voices[voice].volume_irq_mode = VMODE_HALT;
			break;

		case LMODE_PCM_STOP:
			pcm_active = 0;	/* Signal to the play_next_pcm_block routine */
		case LMODE_PCM:
		{
			pcm_qlen--;
			pcm_head = (pcm_head + 1) % pcm_nblk;
			if (pcm_qlen && pcm_active)
			{
				play_next_pcm_block();
			}
			else
			{
				/* Underrun. Just stop the voice */
				gus_select_voice(0);	/* Left channel */
				gus_voice_off();
				gus_rampoff();
				gus_select_voice(1);	/* Right channel */
				gus_voice_off();
				gus_rampoff();
				pcm_active = 0;
			}

			/*
			 * If the queue was full before this interrupt, the DMA transfer was
			 * suspended. Let it continue now.
			 */
			
			if (audio_devs[gus_devnum]->dmap_out->qlen > 0)
				DMAbuf_outputintr(gus_devnum, 0);
		}
		break;

		default:
			break;
	}
	spin_unlock(&gus_lock);
}

static void do_volume_irq(int voice)
{
	unsigned char tmp;
	int mode, parm;
	unsigned long flags;

	spin_lock_irqsave(&gus_lock,flags);

	gus_select_voice(voice);
	tmp = gus_read8(0x0d);
	tmp &= ~0x20;		/*
				 * Disable volume ramp IRQ
				 */
	gus_write8(0x0d, tmp);

	mode = voices[voice].volume_irq_mode;
	voices[voice].volume_irq_mode = 0;
	parm = voices[voice].volume_irq_parm;

	switch (mode)
	{
		case VMODE_HALT:	/* Decay phase finished */
			if (iw_mode)
				gus_write8(0x15, 0x02);	/* Set voice deactivate bit of SMSI */
			spin_unlock_irqrestore(&gus_lock,flags);
			gus_voice_init(voice);
			break;

		case VMODE_ENVELOPE:
			gus_rampoff();
			spin_unlock_irqrestore(&gus_lock,flags);
			step_envelope(voice);
			break;

		case VMODE_START_NOTE:
			spin_unlock_irqrestore(&gus_lock,flags);
			guswave_start_note2(voices[voice].dev_pending, voice,
				      voices[voice].note_pending, voices[voice].volume_pending);
			if (voices[voice].kill_pending)
				guswave_kill_note(voices[voice].dev_pending, voice,
					  voices[voice].note_pending, 0);

			if (voices[voice].sample_pending >= 0)
			{
				guswave_set_instr(voices[voice].dev_pending, voice,
					voices[voice].sample_pending);
				voices[voice].sample_pending = -1;
			}
			break;

		default:
			spin_unlock_irqrestore(&gus_lock,flags);
	}
}
/* called in irq context */
void gus_voice_irq(void)
{
	unsigned long wave_ignore = 0, volume_ignore = 0;
	unsigned long voice_bit;

	unsigned char src, voice;

	while (1)
	{
		src = gus_read8(0x0f);	/*
					 * Get source info
					 */
		voice = src & 0x1f;
		src &= 0xc0;

		if (src == (0x80 | 0x40))
			return;	/*
				 * No interrupt
				 */

		voice_bit = 1 << voice;

		if (!(src & 0x80))	/*
					 * Wave IRQ pending
					 */
			if (!(wave_ignore & voice_bit) && (int) voice < nr_voices)	/*
											 * Not done
											 * yet
											 */
			{
				wave_ignore |= voice_bit;
				do_loop_irq(voice);
			}
		if (!(src & 0x40))	/*
					 * Volume IRQ pending
					 */
			if (!(volume_ignore & voice_bit) && (int) voice < nr_voices)	/*
											   * Not done
											   * yet
											 */
			{
				volume_ignore |= voice_bit;
				do_volume_irq(voice);
			}
	}
}

void guswave_dma_irq(void)
{
	unsigned char   status;

	status = gus_look8(0x41);	/* Get DMA IRQ Status */
	if (status & 0x40)	/* DMA interrupt pending */
		switch (active_device)
		{
			case GUS_DEV_WAVE:
				wake_up(&dram_sleeper);
				break;

			case GUS_DEV_PCM_CONTINUE:	/* Left channel data transferred */
				gus_write8(0x41, 0);	/* Disable GF1 DMA */
				gus_transfer_output_block(pcm_current_dev, pcm_current_buf,
						pcm_current_count,
						pcm_current_intrflag, 1);
				break;

			case GUS_DEV_PCM_DONE:	/* Right or mono channel data transferred */
				gus_write8(0x41, 0);	/* Disable GF1 DMA */
				if (pcm_qlen < pcm_nblk)
				{
					dma_active = 0;
					if (gus_busy)
					{
						if (audio_devs[gus_devnum]->dmap_out->qlen > 0)
							DMAbuf_outputintr(gus_devnum, 0);
					}
				}
				break;

			default:
				break;
	}
	status = gus_look8(0x49);	/*
					 * Get Sampling IRQ Status
					 */
	if (status & 0x40)	/*
				 * Sampling Irq pending
				 */
	{
		DMAbuf_inputintr(gus_devnum);
	}
}

/*
 * Timer stuff
 */

static volatile int select_addr, data_addr;
static volatile int curr_timer;

void gus_timer_command(unsigned int addr, unsigned int val)
{
	int i;

	outb(((unsigned char) (addr & 0xff)), select_addr);

	for (i = 0; i < 2; i++)
		inb(select_addr);

	outb(((unsigned char) (val & 0xff)), data_addr);

	for (i = 0; i < 2; i++)
		inb(select_addr);
}

static void arm_timer(int timer, unsigned int interval)
{
	curr_timer = timer;

	if (timer == 1)
	{
		gus_write8(0x46, 256 - interval);	/* Set counter for timer 1 */
		gus_write8(0x45, 0x04);			/* Enable timer 1 IRQ */
		gus_timer_command(0x04, 0x01);		/* Start timer 1 */
	}
	else
	{
		gus_write8(0x47, 256 - interval);	/* Set counter for timer 2 */
		gus_write8(0x45, 0x08);			/* Enable timer 2 IRQ */
		gus_timer_command(0x04, 0x02);		/* Start timer 2 */
	}

	gus_timer_enabled = 1;
}

static unsigned int gus_tmr_start(int dev, unsigned int usecs_per_tick)
{
	int timer_no, resolution;
	int divisor;

	if (usecs_per_tick > (256 * 80))
	{
		timer_no = 2;
		resolution = 320;	/* usec */
	}
	else
	{
		timer_no = 1;
		resolution = 80;	/* usec */
	}
	divisor = (usecs_per_tick + (resolution / 2)) / resolution;
	arm_timer(timer_no, divisor);

	return divisor * resolution;
}

static void gus_tmr_disable(int dev)
{
	gus_write8(0x45, 0);	/* Disable both timers */
	gus_timer_enabled = 0;
}

static void gus_tmr_restart(int dev)
{
	if (curr_timer == 1)
		gus_write8(0x45, 0x04);		/* Start timer 1 again */
	else
		gus_write8(0x45, 0x08);		/* Start timer 2 again */
	gus_timer_enabled = 1;
}

static struct sound_lowlev_timer gus_tmr =
{
	0,
	1,
	gus_tmr_start,
	gus_tmr_disable,
	gus_tmr_restart
};

static void gus_tmr_install(int io_base)
{
	struct sound_lowlev_timer *tmr;

	select_addr = io_base;
	data_addr = io_base + 1;

	tmr = &gus_tmr;

#ifdef THIS_GETS_FIXED
	sound_timer_init(&gus_tmr, "GUS");
#endif
}
