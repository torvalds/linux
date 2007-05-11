/*
 *	OSS driver for Linux 2.[46].x for
 *
 *	Trident 4D-Wave
 *	SiS 7018
 *	ALi 5451
 *	Tvia/IGST CyberPro 5050
 *
 *	Driver: Alan Cox <alan@redhat.com>
 *
 *  Built from:
 *	Low level code: <audio@tridentmicro.com> from ALSA
 *	Framework: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *	Extended by: Zach Brown <zab@redhat.com>  
 *
 *  Hacked up by:
 *	Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *	Ollie Lho <ollie@sis.com.tw> SiS 7018 Audio Core Support
 *	Ching-Ling Lee <cling-li@ali.com.tw> ALi 5451 Audio Core Support 
 *	Matt Wu <mattwu@acersoftech.com.cn> ALi 5451 Audio Core Support
 *	Peter Wächtler <pwaechtler@loewe-komp.de> CyberPro5050 support
 *      Muli Ben-Yehuda <mulix@mulix.org>
 *
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History
 *  v0.14.10j
 *  	January 3 2004 Eugene Teo <eugeneteo@eugeneteo.net>
 *  	minor cleanup to use pr_debug instead of TRDBG since it is already
 *  	defined in linux/kernel.h.
 *  v0.14.10i
 *      December 29 2003 Muli Ben-Yehuda <mulix@mulix.org>
 *      major cleanup for 2.6, fix a few error patch buglets
 *      with returning without properly cleaning up first,
 *      get rid of lock_kernel().
 *  v0.14.10h
 *	Sept 10 2002 Pascal Schmidt <der.eremit@email.de>
 *	added support for ALi 5451 joystick port
 *  v0.14.10g
 *	Sept 05 2002 Alan Cox <alan@redhat.com>
 *	adapt to new pci joystick attachment interface
 *  v0.14.10f
 *      July 24 2002 Muli Ben-Yehuda <mulix@actcom.co.il>
 *      patch from Eric Lemar (via Ian Soboroff): in suspend and resume, 
 *      fix wrong cast from pci_dev* to struct trident_card*. 
 *  v0.14.10e
 *      July 19 2002 Muli Ben-Yehuda <mulix@actcom.co.il>
 *      rewrite the DMA buffer allocation/deallcoation functions, to make it 
 *      modular and fix a bug where we would call free_pages on memory 
 *      obtained with pci_alloc_consistent. Also remove unnecessary #ifdef 
 *      CONFIG_PROC_FS and various other cleanups.
 *  v0.14.10d
 *      July 19 2002 Muli Ben-Yehuda <mulix@actcom.co.il>
 *      made several printk(KERN_NOTICE...) into TRDBG(...), to avoid spamming
 *      my syslog with hundreds of messages. 
 *  v0.14.10c
 *      July 16 2002 Muli Ben-Yehuda <mulix@actcom.co.il>
 *      Cleaned up Lei Hu's 0.4.10 driver to conform to Documentation/CodingStyle
 *      and the coding style used in the rest of the file. 
 *  v0.14.10b
 *      June 23 2002 Muli Ben-Yehuda <mulix@actcom.co.il>
 *      add a missing unlock_set_fmt, remove a superflous lock/unlock pair 
 *      with nothing in between. 
 *  v0.14.10a
 *      June 21 2002 Muli Ben-Yehuda <mulix@actcom.co.il> 
 *      use a debug macro instead of #ifdef CONFIG_DEBUG, trim to 80 columns 
 *      per line, use 'do {} while (0)' in statement macros. 
 *  v0.14.10
 *      June 6 2002 Lei Hu <Lei_hu@ali.com.tw>
 *      rewrite the part to read/write registers of audio codec for Ali5451 
 *  v0.14.9e
 *      January 2 2002 Vojtech Pavlik <vojtech@ucw.cz> added gameport
 *      support to avoid resource conflict with pcigame.c
 *  v0.14.9d
 *  	October 8 2001 Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *	use set_current_state, properly release resources on failure in
 *	trident_probe, get rid of check_region
 *  v0.14.9c
 *	August 10 2001 Peter Wächtler <pwaechtler@loewe-komp.de>
 *	added support for Tvia (formerly Integraphics/IGST) CyberPro5050
 *	this chip is often found in settop boxes (combined video+audio)
 *  v0.14.9b
 *	Switch to static inline not extern inline (gcc 3)
 *  v0.14.9a
 *	Aug 6 2001 Alan Cox
 *	0.14.9 crashed on rmmod due to a timer/bh left running. Simplified
 *	the existing logic (the BH doesn't help as ac97 is lock_irqsave)
 *	and used del_timer_sync to clean up
 *	Fixed a problem where the ALi change broke my generic card
 *  v0.14.9
 *	Jul 10 2001 Matt Wu
 *	Add H/W Volume Control
 *  v0.14.8a
 *	July 7 2001 Alan Cox
 *	Moved Matt Wu's ac97 register cache into the card structure
 *  v0.14.8
 *	Apr 30 2001 Matt Wu
 *	Set EBUF1 and EBUF2 to still mode
 *	Add dc97/ac97 reset function
 *	Fix power management: ali_restore_regs
 *  unreleased 
 *	Mar 09 2001 Matt Wu
 *	Add cache for ac97 access
 *  v0.14.7
 *	Feb 06 2001 Matt Wu
 *	Fix ac97 initialization
 *	Fix bug: an extra tail will be played when playing
 *	Jan 05 2001 Matt Wu
 *	Implement multi-channels and S/PDIF in support for ALi 1535+
 *  v0.14.6 
 *	Nov 1 2000 Ching-Ling Lee
 *	Fix the bug of memory leak when switching 5.1-channels to 2 channels.
 *	Add lock protection into dynamic changing format of data.
 *	Oct 18 2000 Ching-Ling Lee
 *	5.1-channels support for ALi
 *	June 28 2000 Ching-Ling Lee
 *	S/PDIF out/in(playback/record) support for ALi 1535+, using /proc to be selected by user
 *	Simple Power Management support for ALi
 *  v0.14.5 May 23 2000 Ollie Lho
 *  	Misc bug fix from the Net
 *  v0.14.4 May 20 2000 Aaron Holtzman
 *  	Fix kfree'd memory access in release
 *  	Fix race in open while looking for a free virtual channel slot
 *  	remove open_wait wq (which appears to be unused)
 *  v0.14.3 May 10 2000 Ollie Lho
 *	fixed a small bug in trident_update_ptr, xmms 1.0.1 no longer uses 100% CPU
 *  v0.14.2 Mar 29 2000 Ching-Ling Lee
 *	Add clear to silence advance in trident_update_ptr 
 *	fix invalid data of the end of the sound
 *  v0.14.1 Mar 24 2000 Ching-Ling Lee
 *	ALi 5451 support added, playback and recording O.K.
 *	ALi 5451 originally developed and structured based on sonicvibes, and
 *	suggested to merge into this file by Alan Cox.
 *  v0.14 Mar 15 2000 Ollie Lho
 *	5.1 channel output support with channel binding. What's the Matrix ?
 *  v0.13.1 Mar 10 2000 Ollie Lho
 *	few minor bugs on dual codec support, needs more testing
 *  v0.13 Mar 03 2000 Ollie Lho
 *	new pci_* for 2.4 kernel, back ported to 2.2
 *  v0.12 Feb 23 2000 Ollie Lho
 *	Preliminary Recording support
 *  v0.11.2 Feb 19 2000 Ollie Lho
 *	removed incomplete full-dulplex support
 *  v0.11.1 Jan 28 2000 Ollie Lho
 *	small bug in setting sample rate for 4d-nx (reported by Aaron)
 *  v0.11 Jan 27 2000 Ollie Lho
 *	DMA bug, scheduler latency, second try
 *  v0.10 Jan 24 2000 Ollie Lho
 *	DMA bug fixed, found kernel scheduling problem
 *  v0.09 Jan 20 2000 Ollie Lho
 *	Clean up of channel register access routine (prepare for channel binding)
 *  v0.08 Jan 14 2000 Ollie Lho
 *	Isolation of AC97 codec code
 *  v0.07 Jan 13 2000 Ollie Lho
 *	Get rid of ugly old low level access routines (e.g. CHRegs.lp****)
 *  v0.06 Jan 11 2000 Ollie Lho
 *	Preliminary support for dual (more ?) AC97 codecs
 *  v0.05 Jan 08 2000 Luca Montecchiani <m.luca@iname.com>
 *	adapt to 2.3.x new __setup/__init call
 *  v0.04 Dec 31 1999 Ollie Lho
 *	Multiple Open, using Middle Loop Interrupt to smooth playback
 *  v0.03 Dec 24 1999 Ollie Lho
 *	mem leak in prog_dmabuf and dealloc_dmabuf removed
 *  v0.02 Dec 15 1999 Ollie Lho
 *	SiS 7018 support added, playback O.K.
 *  v0.01 Alan Cox et. al.
 *	Initial Release in kernel 2.3.30, does not work
 * 
 *  ToDo
 *	Clean up of low level channel register access code. (done)
 *	Fix the bug on dma buffer management in update_ptr, read/write, drain_dac (done)
 *	Dual AC97 codecs support (done)
 *	Recording support (done)
 *	Mmap support
 *	"Channel Binding" ioctl extension (done)
 *	new pci device driver interface for 2.4 kernel (done)
 *
 *	Lock order (high->low)
 *		lock	-	hardware lock
 *		open_mutex - 	guard opens
 *		sem	-	guard dmabuf, write re-entry etc
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/ac97_codec.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/gameport.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/mm.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/dma.h>

#if defined(CONFIG_ALPHA_NAUTILUS) || defined(CONFIG_ALPHA_GENERIC)
#include <asm/hwrpb.h>
#endif

#include "trident.h"

#define DRIVER_VERSION "0.14.10j-2.6"

#if defined(CONFIG_GAMEPORT) || (defined(MODULE) && defined(CONFIG_GAMEPORT_MODULE))
#define SUPPORT_JOYSTICK 1
#endif

/* magic numbers to protect our data structures */
#define TRIDENT_CARD_MAGIC	0x5072696E	/* "Prin" */
#define TRIDENT_STATE_MAGIC	0x63657373	/* "cess" */

#define TRIDENT_DMA_MASK	0x3fffffff	/* DMA buffer mask for pci_alloc_consist */
#define ALI_DMA_MASK		0x7fffffff	/* ALI Tridents have 31-bit DMA. Wow. */

#define NR_HW_CH		32

/* maximum number of AC97 codecs connected, AC97 2.0 defined 4, but 7018 and 4D-NX only
   have 2 SDATA_IN lines (currently) */
#define NR_AC97		2

/* minor number of /dev/swmodem (temporary, experimental) */
#define SND_DEV_SWMODEM	7

static const unsigned ali_multi_channels_5_1[] = {
	/*ALI_SURR_LEFT_CHANNEL, ALI_SURR_RIGHT_CHANNEL, */
	ALI_CENTER_CHANNEL,
	ALI_LEF_CHANNEL,
	ALI_SURR_LEFT_CHANNEL,
	ALI_SURR_RIGHT_CHANNEL
};

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

static const char invalid_magic[] = KERN_CRIT "trident: invalid magic value in %s\n";

enum {
	TRIDENT_4D_DX = 0,
	TRIDENT_4D_NX,
	SIS_7018,
	ALI_5451,
	CYBER5050
};

static char *card_names[] = {
	"Trident 4DWave DX",
	"Trident 4DWave NX",
	"SiS 7018 PCI Audio",
	"ALi Audio Accelerator",
	"Tvia/IGST CyberPro 5050"
};

static struct pci_device_id trident_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_DX),
		PCI_CLASS_MULTIMEDIA_AUDIO << 8, 0xffff00, TRIDENT_4D_DX},
	{PCI_DEVICE(PCI_VENDOR_ID_TRIDENT, PCI_DEVICE_ID_TRIDENT_4DWAVE_NX),
		0, 0, TRIDENT_4D_NX},
	{PCI_DEVICE(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_7018), 0, 0, SIS_7018},
	{PCI_DEVICE(PCI_VENDOR_ID_ALI, PCI_DEVICE_ID_ALI_5451), 0, 0, ALI_5451},
	{PCI_DEVICE(PCI_VENDOR_ID_INTERG, PCI_DEVICE_ID_INTERG_5050),
		0, 0, CYBER5050},
	{0,}
};

MODULE_DEVICE_TABLE(pci, trident_pci_tbl);

/* "software" or virtual channel, an instance of opened /dev/dsp */
struct trident_state {
	unsigned int magic;
	struct trident_card *card;	/* Card info */

	/* file mode */
	mode_t open_mode;

	/* virtual channel number */
	int virt;

	struct dmabuf {
		/* wave sample stuff */
		unsigned int rate;
		unsigned char fmt, enable;

		/* hardware channel */
		struct trident_channel *channel;

		/* OSS buffer management stuff */
		void *rawbuf;
		dma_addr_t dma_handle;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;

		/* our buffer acts like a circular ring */
		unsigned hwptr;	/* where dma last started, updated by update_ptr */
		unsigned swptr;	/* where driver last clear/filled, updated by read/write */
		int count;	/* bytes to be comsumed or been generated by dma machine */
		unsigned total_bytes;	/* total bytes dmaed by hardware */

		unsigned error;	/* number of over/underruns */
                /* put process on wait queue when no more space in buffer */
		wait_queue_head_t wait;	

		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;

		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;
		unsigned endcleared:1;
		unsigned update_flag;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;

	} dmabuf;

	/* 5.1 channels */
	struct trident_state *other_states[4];
	int multi_channels_adjust_count;
	unsigned chans_num;
	unsigned long fmt_flag;
	/* Guard against mmap/write/read races */
	struct mutex sem;

};

/* hardware channels */
struct trident_channel {
	int num; /* channel number */
	u32 lba; /* Loop Begine Address, where dma buffer starts */
	u32 eso; /* End Sample Offset, wehre dma buffer ends */ 
	         /* (in the unit of samples) */
	u32 delta; /* delta value, sample rate / 48k for playback, */
	           /* 48k/sample rate for recording */
	u16 attribute; /* control where PCM data go and come  */
	u16 fm_vol;
	u32 control; /* signed/unsigned, 8/16 bits, mono/stereo */
};

struct trident_pcm_bank_address {
	u32 start;
	u32 stop;
	u32 aint;
	u32 aint_en;
};

static struct trident_pcm_bank_address bank_a_addrs = {
	T4D_START_A,
	T4D_STOP_A,
	T4D_AINT_A,
	T4D_AINTEN_A
};

static struct trident_pcm_bank_address bank_b_addrs = {
	T4D_START_B,
	T4D_STOP_B,
	T4D_AINT_B,
	T4D_AINTEN_B
};

struct trident_pcm_bank {
	/* register addresses to control bank operations */
	struct trident_pcm_bank_address *addresses;
	/* each bank has 32 channels */
	u32 bitmap;		/* channel allocation bitmap */
	struct trident_channel channels[32];
};

struct trident_card {
	unsigned int magic;

	/* We keep trident cards in a linked list */
	struct trident_card *next;

	/* single open lock mechanism, only used for recording */
	struct mutex open_mutex;

	/* The trident has a certain amount of cross channel interaction
	   so we use a single per card lock */
	spinlock_t lock;

	/* PCI device stuff */
	struct pci_dev *pci_dev;
	u16 pci_id;
	u8 revision;

	/* soundcore stuff */
	int dev_audio;

	/* structures for abstraction of hardware facilities, codecs, */ 
	/* banks and channels */
	struct ac97_codec *ac97_codec[NR_AC97];
	struct trident_pcm_bank banks[NR_BANKS];
	struct trident_state *states[NR_HW_CH];

	/* hardware resources */
	unsigned long iobase;
	u32 irq;

	/* Function support */
	struct trident_channel *(*alloc_pcm_channel) (struct trident_card *);
	struct trident_channel *(*alloc_rec_pcm_channel) (struct trident_card *);
	void (*free_pcm_channel) (struct trident_card *, unsigned int chan);
	void (*address_interrupt) (struct trident_card *);

	/* Added by Matt Wu 01-05-2001 for spdif in */
	int multi_channel_use_count;
	int rec_channel_use_count;
	u16 mixer_regs[64][NR_AC97];	/* Made card local by Alan */
	int mixer_regs_ready;

	/* Added for hardware volume control */
	int hwvolctl;
	struct timer_list timer;

	/* Game port support */
	struct gameport *gameport;
};

enum dmabuf_mode {
	DM_PLAYBACK = 0,
	DM_RECORD
};

/* table to map from CHANNELMASK to channel attribute for SiS 7018 */
static u16 mask2attr[] = {
	PCM_LR, PCM_LR, SURR_LR, CENTER_LFE,
	HSET, MIC, MODEM_LINE1, MODEM_LINE2,
	I2S_LR, SPDIF_LR
};

/* table to map from channel attribute to CHANNELMASK for SiS 7018 */
static int attr2mask[] = {
	DSP_BIND_MODEM1, DSP_BIND_MODEM2, DSP_BIND_FRONT, DSP_BIND_HANDSET,
	DSP_BIND_I2S, DSP_BIND_CENTER_LFE, DSP_BIND_SURR, DSP_BIND_SPDIF
};

/* Added by Matt Wu 01-05-2001 for spdif in */
static int ali_close_multi_channels(void);
static void ali_delay(struct trident_card *card, int interval);
static void ali_detect_spdif_rate(struct trident_card *card);

static void ali_ac97_write(struct ac97_codec *codec, u8 reg, u16 val);
static u16 ali_ac97_read(struct ac97_codec *codec, u8 reg);

static struct trident_card *devs;

static void trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val);
static u16 trident_ac97_get(struct ac97_codec *codec, u8 reg);

static int trident_open_mixdev(struct inode *inode, struct file *file);
static int trident_ioctl_mixdev(struct inode *inode, struct file *file, 
				unsigned int cmd, unsigned long arg);

static void ali_ac97_set(struct trident_card *card, int secondary, u8 reg, u16 val);
static u16 ali_ac97_get(struct trident_card *card, int secondary, u8 reg);
static void ali_set_spdif_out_rate(struct trident_card *card, unsigned int rate);
static void ali_enable_special_channel(struct trident_state *stat);
static struct trident_channel *ali_alloc_rec_pcm_channel(struct trident_card *card);
static struct trident_channel *ali_alloc_pcm_channel(struct trident_card *card);
static void ali_free_pcm_channel(struct trident_card *card, unsigned int channel);
static int ali_setup_multi_channels(struct trident_card *card, int chan_nums);
static unsigned int ali_get_spdif_in_rate(struct trident_card *card);
static void ali_setup_spdif_in(struct trident_card *card);
static void ali_disable_spdif_in(struct trident_card *card);
static void ali_disable_special_channel(struct trident_card *card, int ch);
static void ali_setup_spdif_out(struct trident_card *card, int flag);
static int ali_write_5_1(struct trident_state *state,
			 const char __user *buffer, 
			 int cnt_for_multi_channel, unsigned int *copy_count, 
			 unsigned int *state_cnt);
static int ali_allocate_other_states_resources(struct trident_state *state, 
					       int chan_nums);
static void ali_free_other_states_resources(struct trident_state *state);

#define seek_offset(dma_ptr, buffer, cnt, offset, copy_count)	do { \
        (dma_ptr) += (offset);	  \
	(buffer) += (offset);	  \
        (cnt) -= (offset);	  \
	(copy_count) += (offset); \
} while (0)

static inline int lock_set_fmt(struct trident_state* state)
{
	if (test_and_set_bit(0, &state->fmt_flag))
		return -EFAULT;

	return 0;
}

static inline void unlock_set_fmt(struct trident_state* state)
{
	clear_bit(0, &state->fmt_flag);
}

static int
trident_enable_loop_interrupts(struct trident_card *card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));

	switch (card->pci_id) {
	case PCI_DEVICE_ID_SI_7018:
		global_control |= (ENDLP_IE | MIDLP_IE | BANK_B_EN);
		break;
	case PCI_DEVICE_ID_ALI_5451:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
	case PCI_DEVICE_ID_INTERG_5050:
		global_control |= (ENDLP_IE | MIDLP_IE);
		break;
	default:
		return 0;
	}

	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

	pr_debug("trident: Enable Loop Interrupts, globctl = 0x%08X\n",
		 inl(TRID_REG(card, T4D_LFO_GC_CIR)));

	return 1;
}

static int
trident_disable_loop_interrupts(struct trident_card *card)
{
	u32 global_control;

	global_control = inl(TRID_REG(card, T4D_LFO_GC_CIR));
	global_control &= ~(ENDLP_IE | MIDLP_IE);
	outl(global_control, TRID_REG(card, T4D_LFO_GC_CIR));

	pr_debug("trident: Disabled Loop Interrupts, globctl = 0x%08X\n",
		 global_control);

	return 1;
}

static void
trident_enable_voice_irq(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;

	reg = inl(TRID_REG(card, addr));
	reg |= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, addr));
	pr_debug("trident: enabled IRQ on channel %d, %s = 0x%08x(addr:%X)\n",
		 channel, addr == T4D_AINTEN_B ? "AINTEN_B" : "AINTEN_A",
		 reg, addr);
#endif /* DEBUG */
}

static void
trident_disable_voice_irq(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint_en;

	reg = inl(TRID_REG(card, addr));
	reg &= ~mask;
	outl(reg, TRID_REG(card, addr));

	/* Ack the channel in case the interrupt was set before we disable it. */
	outl(mask, TRID_REG(card, bank->addresses->aint));

#ifdef DEBUG
	reg = inl(TRID_REG(card, addr));
	pr_debug("trident: disabled IRQ on channel %d, %s = 0x%08x(addr:%X)\n",
		 channel, addr == T4D_AINTEN_B ? "AINTEN_B" : "AINTEN_A",
		 reg, addr);
#endif /* DEBUG */
}

static void
trident_start_voice(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->start;

#ifdef DEBUG
	u32 reg;
#endif /* DEBUG */

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, addr));
	pr_debug("trident: start voice on channel %d, %s = 0x%08x(addr:%X)\n",
		 channel, addr == T4D_START_B ? "START_B" : "START_A",
		 reg, addr);
#endif /* DEBUG */
}

static void
trident_stop_voice(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 addr = bank->addresses->stop;

#ifdef DEBUG
	u32 reg;
#endif /* DEBUG */

	outl(mask, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, addr));
	pr_debug("trident: stop voice on channel %d, %s = 0x%08x(addr:%X)\n",
		 channel, addr == T4D_STOP_B ? "STOP_B" : "STOP_A",
		 reg, addr);
#endif /* DEBUG */
}

static u32
trident_get_interrupt_mask(struct trident_card *card, unsigned int channel)
{
	struct trident_pcm_bank *bank = &card->banks[channel];
	u32 addr = bank->addresses->aint;
	return inl(TRID_REG(card, addr));
}

static int
trident_check_channel_interrupt(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	u32 reg = trident_get_interrupt_mask(card, channel >> 5);

#ifdef DEBUG
	if (reg & mask)
		pr_debug("trident: channel %d has interrupt, %s = 0x%08x\n",
			 channel, reg == T4D_AINT_B ? "AINT_B" : "AINT_A",
			 reg);
#endif /* DEBUG */
	return (reg & mask) ? 1 : 0;
}

static void
trident_ack_channel_interrupt(struct trident_card *card, unsigned int channel)
{
	unsigned int mask = 1 << (channel & 0x1f);
	struct trident_pcm_bank *bank = &card->banks[channel >> 5];
	u32 reg, addr = bank->addresses->aint;

	reg = inl(TRID_REG(card, addr));
	reg &= mask;
	outl(reg, TRID_REG(card, addr));

#ifdef DEBUG
	reg = inl(TRID_REG(card, T4D_AINT_B));
	pr_debug("trident: Ack channel %d interrupt, AINT_B = 0x%08x\n",
		 channel, reg);
#endif /* DEBUG */
}

static struct trident_channel *
trident_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	bank = &card->banks[BANK_B];

	for (idx = 31; idx >= 0; idx--) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx + 32;
			return channel;
		}
	}

	/* no more free channels available */
	printk(KERN_ERR "trident: no more channels available on Bank B.\n");
	return NULL;
}

static void
trident_free_pcm_channel(struct trident_card *card, unsigned int channel)
{
	int bank;
	unsigned char b;

	if (channel < 31 || channel > 63)
		return;

	if (card->pci_id == PCI_DEVICE_ID_TRIDENT_4DWAVE_DX || 
	    card->pci_id == PCI_DEVICE_ID_TRIDENT_4DWAVE_NX) {
		b = inb(TRID_REG(card, T4D_REC_CH));
		if ((b & ~0x80) == channel)
			outb(0x0, TRID_REG(card, T4D_REC_CH));
	}

	bank = channel >> 5;
	channel = channel & 0x1f;

	card->banks[bank].bitmap &= ~(1 << (channel));
}

static struct trident_channel *
cyber_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	/* The cyberpro 5050 has only 32 voices and one bank */
	/* .. at least they are not documented (if you want to call that 
	 * crap documentation), perhaps broken ? */

	bank = &card->banks[BANK_A];

	for (idx = 31; idx >= 0; idx--) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx;
			return channel;
		}
	}

	/* no more free channels available */
	printk(KERN_ERR "cyberpro5050: no more channels available on Bank A.\n");
	return NULL;
}

static void
cyber_free_pcm_channel(struct trident_card *card, unsigned int channel)
{
	if (channel > 31)
		return;
	card->banks[BANK_A].bitmap &= ~(1 << (channel));
}

static inline void
cyber_outidx(int port, int idx, int data)
{
	outb(idx, port);
	outb(data, port + 1);
}

static inline int
cyber_inidx(int port, int idx)
{
	outb(idx, port);
	return inb(port + 1);
}

static int
cyber_init_ritual(struct trident_card *card)
{
	/* some black magic, taken from SDK samples */
	/* remove this and nothing will work */
	int portDat;
	int ret = 0;
	unsigned long flags;

	/*
	 *      Keep interrupts off for the configure - we don't want to
	 *      clash with another cyberpro config event
	 */

	spin_lock_irqsave(&card->lock, flags);
	portDat = cyber_inidx(CYBER_PORT_AUDIO, CYBER_IDX_AUDIO_ENABLE);
	/* enable, if it was disabled */
	if ((portDat & CYBER_BMSK_AUENZ) != CYBER_BMSK_AUENZ_ENABLE) {
		printk(KERN_INFO "cyberpro5050: enabling audio controller\n");
		cyber_outidx(CYBER_PORT_AUDIO, CYBER_IDX_AUDIO_ENABLE, 
			     portDat | CYBER_BMSK_AUENZ_ENABLE);
		/* check again if hardware is enabled now */
		portDat = cyber_inidx(CYBER_PORT_AUDIO, CYBER_IDX_AUDIO_ENABLE);
	}
	if ((portDat & CYBER_BMSK_AUENZ) != CYBER_BMSK_AUENZ_ENABLE) {
		printk(KERN_ERR "cyberpro5050: initAudioAccess: no success\n");
		ret = -1;
	} else {
		cyber_outidx(CYBER_PORT_AUDIO, CYBER_IDX_IRQ_ENABLE, 
			     CYBER_BMSK_AUDIO_INT_ENABLE);
		cyber_outidx(CYBER_PORT_AUDIO, 0xbf, 0x01);
		cyber_outidx(CYBER_PORT_AUDIO, 0xba, 0x20);
		cyber_outidx(CYBER_PORT_AUDIO, 0xbb, 0x08);
		cyber_outidx(CYBER_PORT_AUDIO, 0xbf, 0x02);
		cyber_outidx(CYBER_PORT_AUDIO, 0xb3, 0x06);
		cyber_outidx(CYBER_PORT_AUDIO, 0xbf, 0x00);
	}
	spin_unlock_irqrestore(&card->lock, flags);
	return ret;
}

/*  called with spin lock held */

static int
trident_load_channel_registers(struct trident_card *card, u32 * data, 
			       unsigned int channel)
{
	int i;

	if (channel > 63)
		return 0;

	/* select hardware channel to write */
	outb(channel, TRID_REG(card, T4D_LFO_GC_CIR));

	/* Output the channel registers, but don't write register
	   three to an ALI chip. */
	for (i = 0; i < CHANNEL_REGS; i++) {
		if (i == 3 && card->pci_id == PCI_DEVICE_ID_ALI_5451)
			continue;
		outl(data[i], TRID_REG(card, CHANNEL_START + 4 * i));
	}
	if (card->pci_id == PCI_DEVICE_ID_ALI_5451 || 
	    card->pci_id == PCI_DEVICE_ID_INTERG_5050) {
		outl(ALI_EMOD_Still, TRID_REG(card, ALI_EBUF1));
		outl(ALI_EMOD_Still, TRID_REG(card, ALI_EBUF2));
	}
	return 1;
}

/* called with spin lock held */
static int
trident_write_voice_regs(struct trident_state *state)
{
	unsigned int data[CHANNEL_REGS + 1];
	struct trident_channel *channel;

	channel = state->dmabuf.channel;

	data[1] = channel->lba;
	data[4] = channel->control;

	switch (state->card->pci_id) {
	case PCI_DEVICE_ID_ALI_5451:
		data[0] = 0;	/* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = 0;
		break;
	case PCI_DEVICE_ID_SI_7018:
	case PCI_DEVICE_ID_INTERG_5050:
		data[0] = 0;	/* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = (channel->attribute << 16) | (channel->fm_vol & 0xffff);
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		data[0] = 0;	/* Current Sample Offset */
		data[2] = (channel->eso << 16) | (channel->delta & 0xffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		data[0] = (channel->delta << 24);
		data[2] = ((channel->delta << 16) & 0xff000000) | 
			(channel->eso & 0x00ffffff);
		data[3] = channel->fm_vol & 0xffff;
		break;
	default:
		return 0;
	}

	return trident_load_channel_registers(state->card, data, channel->num);
}

static int
compute_rate_play(u32 rate)
{
	int delta;
	/* We special case 44100 and 8000 since rounding with the equation
	   does not give us an accurate enough value. For 11025 and 22050
	   the equation gives us the best answer. All other frequencies will
	   also use the equation. JDW */
	if (rate == 44100)
		delta = 0xeb3;
	else if (rate == 8000)
		delta = 0x2ab;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = (((rate << 12) + rate) / 48000) & 0x0000ffff;
	return delta;
}

static int
compute_rate_rec(u32 rate)
{
	int delta;

	if (rate == 44100)
		delta = 0x116a;
	else if (rate == 8000)
		delta = 0x6000;
	else if (rate == 48000)
		delta = 0x1000;
	else
		delta = ((48000 << 12) / rate) & 0x0000ffff;

	return delta;
}

/* set playback sample rate */
static unsigned int
trident_set_dac_rate(struct trident_state *state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate_play(rate);

	trident_write_voice_regs(state);

	pr_debug("trident: called trident_set_dac_rate : rate = %d\n", rate);

	return rate;
}

/* set recording sample rate */
static unsigned int
trident_set_adc_rate(struct trident_state *state, unsigned int rate)
{
	struct dmabuf *dmabuf = &state->dmabuf;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	dmabuf->rate = rate;
	dmabuf->channel->delta = compute_rate_rec(rate);

	trident_write_voice_regs(state);

	pr_debug("trident: called trident_set_adc_rate : rate = %d\n", rate);

	return rate;
}

/* prepare channel attributes for playback */
static void
trident_play_setup(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	struct trident_channel *channel = dmabuf->channel;

	channel->lba = dmabuf->dma_handle;
	channel->delta = compute_rate_play(dmabuf->rate);

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id != PCI_DEVICE_ID_SI_7018) {
		channel->attribute = 0;
		if (state->card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			if ((channel->num == ALI_SPDIF_IN_CHANNEL) || 
			    (channel->num == ALI_PCM_IN_CHANNEL))
				ali_disable_special_channel(state->card, channel->num);
			else if ((inl(TRID_REG(state->card, ALI_GLOBAL_CONTROL)) 
				  & ALI_SPDIF_OUT_CH_ENABLE)
				 && (channel->num == ALI_SPDIF_OUT_CHANNEL)) {
				ali_set_spdif_out_rate(state->card, 
						       state->dmabuf.rate);
				state->dmabuf.channel->delta = 0x1000;
			}
		}
	}

	channel->fm_vol = 0x0;

	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;

	pr_debug("trident: trident_play_setup, LBA = 0x%08x, Delta = 0x%08x, "
		 "ESO = 0x%08x, Control = 0x%08x\n", channel->lba,
		 channel->delta, channel->eso, channel->control);

	trident_write_voice_regs(state);
}

/* prepare channel attributes for recording */
static void
trident_rec_setup(struct trident_state *state)
{
	u16 w;
	u8 bval;

	struct trident_card *card = state->card;
	struct dmabuf *dmabuf = &state->dmabuf;
	struct trident_channel *channel = dmabuf->channel;
	unsigned int rate;

	/* Enable AC-97 ADC (capture) */
	switch (card->pci_id) {
	case PCI_DEVICE_ID_ALI_5451:
		ali_enable_special_channel(state);
		break;
	case PCI_DEVICE_ID_SI_7018:
		/* for 7018, the ac97 is always in playback/record (duplex) mode */
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		w = inb(TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		outb(w | 0x48, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		/* enable and set record channel */
		outb(0x80 | channel->num, TRID_REG(card, T4D_REC_CH));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		w = inw(TRID_REG(card, T4D_MISCINT));
		outw(w | 0x1000, TRID_REG(card, T4D_MISCINT));
		/* enable and set record channel */
		outb(0x80 | channel->num, TRID_REG(card, T4D_REC_CH));
		break;
	case PCI_DEVICE_ID_INTERG_5050:
		/* don't know yet, using special channel 22 in GC1(0xd4)? */
		break;
	default:
		return;
	}

	channel->lba = dmabuf->dma_handle;
	channel->delta = compute_rate_rec(dmabuf->rate);
	if ((card->pci_id == PCI_DEVICE_ID_ALI_5451) && 
	    (channel->num == ALI_SPDIF_IN_CHANNEL)) {
		rate = ali_get_spdif_in_rate(card);
		if (rate == 0) {
			printk(KERN_WARNING "trident: ALi 5451 "
			       "S/PDIF input setup error!\n");
			rate = 48000;
		}
		bval = inb(TRID_REG(card, ALI_SPDIF_CTRL));
		if (bval & 0x10) {
			outb(bval, TRID_REG(card, ALI_SPDIF_CTRL));
			printk(KERN_WARNING "trident: cleared ALi "
			       "5451 S/PDIF parity error flag.\n");
		}

		if (rate != 48000)
			channel->delta = ((rate << 12) / dmabuf->rate) & 0x0000ffff;
	}

	channel->eso = dmabuf->dmasize >> sample_shift[dmabuf->fmt];
	channel->eso -= 1;

	if (state->card->pci_id != PCI_DEVICE_ID_SI_7018) {
		channel->attribute = 0;
	}

	channel->fm_vol = 0x0;

	channel->control = CHANNEL_LOOP;
	if (dmabuf->fmt & TRIDENT_FMT_16BIT) {
		/* 16-bits */
		channel->control |= CHANNEL_16BITS;
		/* signed */
		channel->control |= CHANNEL_SIGNED;
	}
	if (dmabuf->fmt & TRIDENT_FMT_STEREO)
		/* stereo */
		channel->control |= CHANNEL_STEREO;

	pr_debug("trident: trident_rec_setup, LBA = 0x%08x, Delat = 0x%08x, "
		 "ESO = 0x%08x, Control = 0x%08x\n", channel->lba,
		 channel->delta, channel->eso, channel->control);

	trident_write_voice_regs(state);
}

/* get current playback/recording dma buffer pointer (byte offset from LBA),
   called with spinlock held! */
static inline unsigned
trident_get_dma_addr(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	u32 cso;

	if (!dmabuf->enable)
		return 0;

	outb(dmabuf->channel->num, TRID_REG(state->card, T4D_LFO_GC_CIR));

	switch (state->card->pci_id) {
	case PCI_DEVICE_ID_ALI_5451:
	case PCI_DEVICE_ID_SI_7018:
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
	case PCI_DEVICE_ID_INTERG_5050:
		/* 16 bits ESO, CSO for 7018 and DX */
		cso = inw(TRID_REG(state->card, CH_DX_CSO_ALPHA_FMS + 2));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* 24 bits ESO, CSO for NX */
		cso = inl(TRID_REG(state->card, CH_NX_DELTA_CSO)) & 0x00ffffff;
		break;
	default:
		return 0;
	}

	pr_debug("trident: trident_get_dma_addr: chip reported channel: %d, "
		 "cso = 0x%04x\n", dmabuf->channel->num, cso);

	/* ESO and CSO are in units of Samples, convert to byte offset */
	cso <<= sample_shift[dmabuf->fmt];

	return (cso % dmabuf->dmasize);
}

/* Stop recording (lock held) */
static inline void
__stop_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~ADC_RUNNING;
	trident_stop_voice(card, chan_num);
	trident_disable_voice_irq(card, chan_num);
}

static void
stop_adc(struct trident_state *state)
{
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_adc(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void
start_adc(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || 
	     dmabuf->count < (signed) dmabuf->dmasize) && 
	    dmabuf->ready) {
		dmabuf->enable |= ADC_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

/* stop playback (lock held) */
static inline void
__stop_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;

	dmabuf->enable &= ~DAC_RUNNING;
	trident_stop_voice(card, chan_num);
	if (state->chans_num == 6) {
		trident_stop_voice(card, state->other_states[0]->
				   dmabuf.channel->num);
		trident_stop_voice(card, state->other_states[1]->
				   dmabuf.channel->num);
		trident_stop_voice(card, state->other_states[2]->
				   dmabuf.channel->num);
		trident_stop_voice(card, state->other_states[3]->
				   dmabuf.channel->num);
	}
	trident_disable_voice_irq(card, chan_num);
}

static void
stop_dac(struct trident_state *state)
{
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	__stop_dac(state);
	spin_unlock_irqrestore(&card->lock, flags);
}

static void
start_dac(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned int chan_num = dmabuf->channel->num;
	struct trident_card *card = state->card;
	unsigned long flags;

	spin_lock_irqsave(&card->lock, flags);
	if ((dmabuf->mapped || dmabuf->count > 0) && dmabuf->ready) {
		dmabuf->enable |= DAC_RUNNING;
		trident_enable_voice_irq(card, chan_num);
		trident_start_voice(card, chan_num);
		if (state->chans_num == 6) {
			trident_start_voice(card, state->other_states[0]->
					    dmabuf.channel->num);
			trident_start_voice(card, state->other_states[1]->
					    dmabuf.channel->num);
			trident_start_voice(card, state->other_states[2]->
					    dmabuf.channel->num);
			trident_start_voice(card, state->other_states[3]->
					    dmabuf.channel->num);
		}
	}
	spin_unlock_irqrestore(&card->lock, flags);
}

#define DMABUF_DEFAULTORDER (15-PAGE_SHIFT)
#define DMABUF_MINORDER 1

/* alloc a DMA buffer of with a buffer of this order */
static int
alloc_dmabuf(struct dmabuf *dmabuf, struct pci_dev *pci_dev, int order)
{
	void *rawbuf = NULL;
	struct page *page, *pend;

	if (!(rawbuf = pci_alloc_consistent(pci_dev, PAGE_SIZE << order, 
					    &dmabuf->dma_handle)))
		return -ENOMEM;

	pr_debug("trident: allocated %ld (order = %d) bytes at %p\n",
		 PAGE_SIZE << order, order, rawbuf);

	dmabuf->ready = dmabuf->mapped = 0;
	dmabuf->rawbuf = rawbuf;
	dmabuf->buforder = order;

	/* now mark the pages as reserved; otherwise */ 
	/* remap_pfn_range doesn't do what we want */
	pend = virt_to_page(rawbuf + (PAGE_SIZE << order) - 1);
	for (page = virt_to_page(rawbuf); page <= pend; page++)
		SetPageReserved(page);

	return 0;
}

/* allocate the main DMA buffer, playback and recording buffer should be */
/* allocated separately */
static int
alloc_main_dmabuf(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	int order;
	int ret = -ENOMEM;

	/* alloc as big a chunk as we can, FIXME: is this necessary ?? */
	for (order = DMABUF_DEFAULTORDER; order >= DMABUF_MINORDER; order--) {
		if (!(ret = alloc_dmabuf(dmabuf, state->card->pci_dev, order)))
			return 0;
		/* else try again */
	}
	return ret;
}

/* deallocate a DMA buffer */
static void
dealloc_dmabuf(struct dmabuf *dmabuf, struct pci_dev *pci_dev)
{
	struct page *page, *pend;

	if (dmabuf->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(dmabuf->rawbuf + (PAGE_SIZE << dmabuf->buforder) - 1);
		for (page = virt_to_page(dmabuf->rawbuf); page <= pend; page++)
			ClearPageReserved(page);
		pci_free_consistent(pci_dev, PAGE_SIZE << dmabuf->buforder, 
				    dmabuf->rawbuf, dmabuf->dma_handle);
		dmabuf->rawbuf = NULL;
	}
	dmabuf->mapped = dmabuf->ready = 0;
}

static int
prog_dmabuf(struct trident_state *state, enum dmabuf_mode rec)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned bytepersec;
	struct trident_state *s = state;
	unsigned bufsize, dma_nums;
	unsigned long flags;
	int ret, i, order;

	if ((ret = lock_set_fmt(state)) < 0)
		return ret;

	if (state->chans_num == 6)
		dma_nums = 5;
	else
		dma_nums = 1;

	for (i = 0; i < dma_nums; i++) {
		if (i > 0) {
			s = state->other_states[i - 1];
			dmabuf = &s->dmabuf;
			dmabuf->fmt = state->dmabuf.fmt;
			dmabuf->rate = state->dmabuf.rate;
		}

		spin_lock_irqsave(&s->card->lock, flags);
		dmabuf->hwptr = dmabuf->swptr = dmabuf->total_bytes = 0;
		dmabuf->count = dmabuf->error = 0;
		spin_unlock_irqrestore(&s->card->lock, flags);

		/* allocate DMA buffer if not allocated yet */
		if (!dmabuf->rawbuf) {
			if (i == 0) {
				if ((ret = alloc_main_dmabuf(state))) {
					unlock_set_fmt(state);
					return ret;
				}
			} else {
				ret = -ENOMEM;
				order = state->dmabuf.buforder - 1;
				if (order >= DMABUF_MINORDER) {
					ret = alloc_dmabuf(dmabuf,
							   state->card->pci_dev,
							   order);
				}
				if (ret) {
					/* release the main DMA buffer */
					dealloc_dmabuf(&state->dmabuf, state->card->pci_dev);
					/* release the auxiliary DMA buffers */
					for (i -= 2; i >= 0; i--)
						dealloc_dmabuf(&state->other_states[i]->dmabuf, 
							       state->card->pci_dev);
					unlock_set_fmt(state);
					return ret;
				}
			}
		}
		/* FIXME: figure out all this OSS fragment stuff */
		bytepersec = dmabuf->rate << sample_shift[dmabuf->fmt];
		bufsize = PAGE_SIZE << dmabuf->buforder;
		if (dmabuf->ossfragshift) {
			if ((1000 << dmabuf->ossfragshift) < bytepersec)
				dmabuf->fragshift = ld2(bytepersec / 1000);
			else
				dmabuf->fragshift = dmabuf->ossfragshift;
		} else {
			/* lets hand out reasonable big ass buffers by default */
			dmabuf->fragshift = (dmabuf->buforder + PAGE_SHIFT - 2);
		}
		dmabuf->numfrag = bufsize >> dmabuf->fragshift;
		while (dmabuf->numfrag < 4 && dmabuf->fragshift > 3) {
			dmabuf->fragshift--;
			dmabuf->numfrag = bufsize >> dmabuf->fragshift;
		}
		dmabuf->fragsize = 1 << dmabuf->fragshift;
		if (dmabuf->ossmaxfrags >= 4 && dmabuf->ossmaxfrags < dmabuf->numfrag)
			dmabuf->numfrag = dmabuf->ossmaxfrags;
		dmabuf->fragsamples = dmabuf->fragsize >> sample_shift[dmabuf->fmt];
		dmabuf->dmasize = dmabuf->numfrag << dmabuf->fragshift;

		memset(dmabuf->rawbuf, (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80, 
		       dmabuf->dmasize);

		spin_lock_irqsave(&s->card->lock, flags);
		if (rec == DM_RECORD)
			trident_rec_setup(s);
		else /* DM_PLAYBACK */
			trident_play_setup(s);

		spin_unlock_irqrestore(&s->card->lock, flags);

		/* set the ready flag for the dma buffer */
		dmabuf->ready = 1;

		pr_debug("trident: prog_dmabuf(%d), sample rate = %d, "
			 "format = %d, numfrag = %d, fragsize = %d "
			 "dmasize = %d\n", dmabuf->channel->num,
			 dmabuf->rate, dmabuf->fmt, dmabuf->numfrag,
			 dmabuf->fragsize, dmabuf->dmasize);
	}
	unlock_set_fmt(state);
	return 0;
}


static inline int prog_dmabuf_record(struct trident_state* state)
{
	return prog_dmabuf(state, DM_RECORD);
}

static inline int prog_dmabuf_playback(struct trident_state* state)
{
	return prog_dmabuf(state, DM_PLAYBACK);
}

/* we are doing quantum mechanics here, the buffer can only be empty, half or full filled i.e.
   |------------|------------|   or   |xxxxxxxxxxxx|------------|   or   |xxxxxxxxxxxx|xxxxxxxxxxxx|
   but we almost always get this
   |xxxxxx------|------------|   or   |xxxxxxxxxxxx|xxxxx-------|
   so we have to clear the tail space to "silence"
   |xxxxxx000000|------------|   or   |xxxxxxxxxxxx|xxxxxx000000|
*/
static void
trident_clear_tail(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned swptr;
	unsigned char silence = (dmabuf->fmt & TRIDENT_FMT_16BIT) ? 0 : 0x80;
	unsigned int len;
	unsigned long flags;

	spin_lock_irqsave(&state->card->lock, flags);
	swptr = dmabuf->swptr;
	spin_unlock_irqrestore(&state->card->lock, flags);

	if (swptr == 0 || swptr == dmabuf->dmasize / 2 || 
	    swptr == dmabuf->dmasize)
		return;

	if (swptr < dmabuf->dmasize / 2)
		len = dmabuf->dmasize / 2 - swptr;
	else
		len = dmabuf->dmasize - swptr;

	memset(dmabuf->rawbuf + swptr, silence, len);
	if (state->card->pci_id != PCI_DEVICE_ID_ALI_5451) {
		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr += len;
		dmabuf->count += len;
		spin_unlock_irqrestore(&state->card->lock, flags);
	}

	/* restart the dma machine in case it is halted */
	start_dac(state);
}

static int
drain_dac(struct trident_state *state, int nonblock)
{
	DECLARE_WAITQUEUE(wait, current);
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned long tmo;
	int count;
	unsigned long diff = 0;

	if (dmabuf->mapped || !dmabuf->ready)
		return 0;

	add_wait_queue(&dmabuf->wait, &wait);
	for (;;) {
		/* It seems that we have to set the current state to TASK_INTERRUPTIBLE
		   every time to make the process really go to sleep */
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irqsave(&state->card->lock, flags);
		count = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (count <= 0)
			break;

		if (signal_pending(current))
			break;

		if (nonblock) {
			remove_wait_queue(&dmabuf->wait, &wait);
			set_current_state(TASK_RUNNING);
			return -EBUSY;
		}

		/* No matter how much data is left in the buffer, we have to wait until
		   CSO == ESO/2 or CSO == ESO when address engine interrupts */
		if (state->card->pci_id == PCI_DEVICE_ID_ALI_5451 || 
		    state->card->pci_id == PCI_DEVICE_ID_INTERG_5050) {
			diff = dmabuf->swptr - trident_get_dma_addr(state) + dmabuf->dmasize;
			diff = diff % (dmabuf->dmasize);
			tmo = (diff * HZ) / dmabuf->rate;
		} else {
			tmo = (dmabuf->dmasize * HZ) / dmabuf->rate;
		}
		tmo >>= sample_shift[dmabuf->fmt];
		if (!schedule_timeout(tmo ? tmo : 1) && tmo) {
			break;
		}
	}
	remove_wait_queue(&dmabuf->wait, &wait);
	set_current_state(TASK_RUNNING);
	if (signal_pending(current))
		return -ERESTARTSYS;

	return 0;
}

/* update buffer manangement pointers, especially, */ 
/* dmabuf->count and dmabuf->hwptr */
static void
trident_update_ptr(struct trident_state *state)
{
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned hwptr, swptr;
	int clear_cnt = 0;
	int diff;
	unsigned char silence;
	unsigned half_dmasize;

	/* update hardware pointer */
	hwptr = trident_get_dma_addr(state);
	diff = (dmabuf->dmasize + hwptr - dmabuf->hwptr) % dmabuf->dmasize;
	dmabuf->hwptr = hwptr;
	dmabuf->total_bytes += diff;

	/* error handling and process wake up for ADC */
	if (dmabuf->enable == ADC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count -= diff;
			if (dmabuf->count >= (signed) dmabuf->fragsize)
				wake_up(&dmabuf->wait);
		} else {
			dmabuf->count += diff;

			if (dmabuf->count < 0 || 
			    dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, */ 
				/* we have no way to recover it here, just */ 
				/* stop the machine and let the process */ 
				/* force hwptr and swptr to sync */
				__stop_adc(state);
				dmabuf->error++;
			}
			if (dmabuf->count < (signed) dmabuf->dmasize / 2)
				wake_up(&dmabuf->wait);
		}
	}

	/* error handling and process wake up for DAC */
	if (dmabuf->enable == DAC_RUNNING) {
		if (dmabuf->mapped) {
			dmabuf->count += diff;
			if (dmabuf->count >= (signed) dmabuf->fragsize)
				wake_up(&dmabuf->wait);
		} else {
			dmabuf->count -= diff;

			if (dmabuf->count < 0 || 
			    dmabuf->count > dmabuf->dmasize) {
				/* buffer underrun or buffer overrun, we have no way to recover
				   it here, just stop the machine and let the process force hwptr
				   and swptr to sync */
				__stop_dac(state);
				dmabuf->error++;
			} else if (!dmabuf->endcleared) {
				swptr = dmabuf->swptr;
				silence = (dmabuf->fmt & TRIDENT_FMT_16BIT ? 0 : 0x80);
				if (dmabuf->update_flag & ALI_ADDRESS_INT_UPDATE) {
					/* We must clear end data of 1/2 dmabuf if needed.
					   According to 1/2 algorithm of Address Engine Interrupt,
					   check the validation of the data of half dmasize. */
					half_dmasize = dmabuf->dmasize / 2;
					if ((diff = hwptr - half_dmasize) < 0)
						diff = hwptr;
					if ((dmabuf->count + diff) < half_dmasize) {
						//there is invalid data in the end of half buffer
						if ((clear_cnt = half_dmasize - swptr) < 0)
							clear_cnt += half_dmasize;
						//clear the invalid data
						memset(dmabuf->rawbuf + swptr, silence, clear_cnt);
						if (state->chans_num == 6) {
							clear_cnt = clear_cnt / 2;
							swptr = swptr / 2;
							memset(state->other_states[0]->dmabuf.rawbuf + swptr, 
							       silence, clear_cnt);
							memset(state->other_states[1]->dmabuf.rawbuf + swptr, 
							       silence, clear_cnt);
							memset(state->other_states[2]->dmabuf.rawbuf + swptr, 
							       silence, clear_cnt);
							memset(state->other_states[3]->dmabuf.rawbuf + swptr, 
							       silence, clear_cnt);
						}
						dmabuf->endcleared = 1;
					}
				} else if (dmabuf->count < (signed) dmabuf->fragsize) {
					clear_cnt = dmabuf->fragsize;
					if ((swptr + clear_cnt) > dmabuf->dmasize)
						clear_cnt = dmabuf->dmasize - swptr;
					memset(dmabuf->rawbuf + swptr, silence, clear_cnt);
					if (state->chans_num == 6) {
						clear_cnt = clear_cnt / 2;
						swptr = swptr / 2;
						memset(state->other_states[0]->dmabuf.rawbuf + swptr, 
						       silence, clear_cnt);
						memset(state->other_states[1]->dmabuf.rawbuf + swptr, 
						       silence, clear_cnt);
						memset(state->other_states[2]->dmabuf.rawbuf + swptr, 
						       silence, clear_cnt);
						memset(state->other_states[3]->dmabuf.rawbuf + swptr, 
						       silence, clear_cnt);
					}
					dmabuf->endcleared = 1;
				}
			}
			/* trident_update_ptr is called by interrupt handler or by process via
			   ioctl/poll, we only wake up the waiting process when we have more
			   than 1/2 buffer free (always true for interrupt handler) */
			if (dmabuf->count < (signed) dmabuf->dmasize / 2)
				wake_up(&dmabuf->wait);
		}
	}
	dmabuf->update_flag &= ~ALI_ADDRESS_INT_UPDATE;
}

static void
trident_address_interrupt(struct trident_card *card)
{
	int i;
	struct trident_state *state;
	unsigned int channel;

	/* Update the pointers for all channels we are running. */
	/* FIXME: should read interrupt status only once */
	for (i = 0; i < NR_HW_CH; i++) {
		channel = 63 - i;
		if (trident_check_channel_interrupt(card, channel)) {
			trident_ack_channel_interrupt(card, channel);
			if ((state = card->states[i]) != NULL) {
				trident_update_ptr(state);
			} else {
				printk(KERN_WARNING "trident: spurious channel " 
				       "irq %d.\n", channel);
				trident_stop_voice(card, channel);
				trident_disable_voice_irq(card, channel);
			}
		}
	}
}

static void
ali_hwvol_control(struct trident_card *card, int opt)
{
	u16 dwTemp, volume[2], mute, diff, *pVol[2];

	dwTemp = ali_ac97_read(card->ac97_codec[0], 0x02);
	mute = dwTemp & 0x8000;
	volume[0] = dwTemp & 0x001f;
	volume[1] = (dwTemp & 0x1f00) >> 8;
	if (volume[0] < volume[1]) {
		pVol[0] = &volume[0];
		pVol[1] = &volume[1];
	} else {
		pVol[1] = &volume[0];
		pVol[0] = &volume[1];
	}
	diff = *(pVol[1]) - *(pVol[0]);

	if (opt == 1) {		// MUTE
		dwTemp ^= 0x8000;
		ali_ac97_write(card->ac97_codec[0], 
			       0x02, dwTemp);
	} else if (opt == 2) {	// Down
		if (mute)
			return;
		if (*(pVol[1]) < 0x001f) {
			(*pVol[1])++;
			*(pVol[0]) = *(pVol[1]) - diff;
		}
		dwTemp &= 0xe0e0;
		dwTemp |= (volume[0]) | (volume[1] << 8);
		ali_ac97_write(card->ac97_codec[0], 0x02, dwTemp);
		card->ac97_codec[0]->mixer_state[0] = ((32 - volume[0]) * 25 / 8) | 
			(((32 - volume[1]) * 25 / 8) << 8);
	} else if (opt == 4) {	// Up
		if (mute)
			return;
		if (*(pVol[0]) > 0) {
			(*pVol[0])--;
			*(pVol[1]) = *(pVol[0]) + diff;
		}
		dwTemp &= 0xe0e0;
		dwTemp |= (volume[0]) | (volume[1] << 8);
		ali_ac97_write(card->ac97_codec[0], 0x02, dwTemp);
		card->ac97_codec[0]->mixer_state[0] = ((32 - volume[0]) * 25 / 8) | 
			(((32 - volume[1]) * 25 / 8) << 8);
	} else {
		/* Nothing needs doing */
	}
}

/*
 *	Re-enable reporting of vol change after 0.1 seconds
 */

static void
ali_timeout(unsigned long ptr)
{
	struct trident_card *card = (struct trident_card *) ptr;
	u16 temp = 0;

	/* Enable GPIO IRQ (MISCINT bit 18h) */
	temp = inw(TRID_REG(card, T4D_MISCINT + 2));
	temp |= 0x0004;
	outw(temp, TRID_REG(card, T4D_MISCINT + 2));
}

/*
 *	Set up the timer to clear the vol change notification
 */

static void
ali_set_timer(struct trident_card *card)
{
	/* Add Timer Routine to Enable GPIO IRQ */
	del_timer(&card->timer);	/* Never queue twice */
	card->timer.function = ali_timeout;
	card->timer.data = (unsigned long) card;
	card->timer.expires = jiffies + HZ / 10;
	add_timer(&card->timer);
}

/*
 *	Process a GPIO event
 */

static void
ali_queue_task(struct trident_card *card, int opt)
{
	u16 temp;

	/* Disable GPIO IRQ (MISCINT bit 18h) */
	temp = inw(TRID_REG(card, T4D_MISCINT + 2));
	temp &= (u16) (~0x0004);
	outw(temp, TRID_REG(card, T4D_MISCINT + 2));

	/* Adjust the volume */
	ali_hwvol_control(card, opt);

	/* Set the timer for 1/10th sec */
	ali_set_timer(card);
}

static void
cyber_address_interrupt(struct trident_card *card)
{
	int i, irq_status;
	struct trident_state *state;
	unsigned int channel;

	/* Update the pointers for all channels we are running. */
	/* FIXED: read interrupt status only once */
	irq_status = inl(TRID_REG(card, T4D_AINT_A));

	pr_debug("cyber_address_interrupt: irq_status 0x%X\n", irq_status);

	for (i = 0; i < NR_HW_CH; i++) {
		channel = 31 - i;
		if (irq_status & (1 << channel)) {
			/* clear bit by writing a 1, zeroes are ignored */
			outl((1 << channel), TRID_REG(card, T4D_AINT_A));

			pr_debug("cyber_interrupt: channel %d\n", channel);

			if ((state = card->states[i]) != NULL) {
				trident_update_ptr(state);
			} else {
				printk(KERN_WARNING "cyber5050: spurious " 
				       "channel irq %d.\n", channel);
				trident_stop_voice(card, channel);
				trident_disable_voice_irq(card, channel);
			}
		}
	}
}

static irqreturn_t
trident_interrupt(int irq, void *dev_id)
{
	struct trident_card *card = (struct trident_card *) dev_id;
	u32 event;
	u32 gpio;

	spin_lock(&card->lock);
	event = inl(TRID_REG(card, T4D_MISCINT));

	pr_debug("trident: trident_interrupt called, MISCINT = 0x%08x\n",
		 event);

	if (event & ADDRESS_IRQ) {
		card->address_interrupt(card);
	}

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		/* GPIO IRQ (H/W Volume Control) */
		event = inl(TRID_REG(card, T4D_MISCINT));
		if (event & (1 << 25)) {
			gpio = inl(TRID_REG(card, ALI_GPIO));
			if (!timer_pending(&card->timer))
				ali_queue_task(card, gpio & 0x07);
		}
		event = inl(TRID_REG(card, T4D_MISCINT));
		outl(event | (ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW), 
		     TRID_REG(card, T4D_MISCINT));
		spin_unlock(&card->lock);
		return IRQ_HANDLED;
	}

	/* manually clear interrupt status, bad hardware design, blame T^2 */
	outl((ST_TARGET_REACHED | MIXER_OVERFLOW | MIXER_UNDERFLOW), 
	     TRID_REG(card, T4D_MISCINT));
	spin_unlock(&card->lock);
	return IRQ_HANDLED;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting */ 
/* to be copied to the user's buffer.  it is filled by the dma machine and */ 
/* drained by this loop. */
static ssize_t
trident_read(struct file *file, char __user *buffer, size_t count, loff_t * ppos)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret = 0;
	unsigned long flags;
	unsigned swptr;
	int cnt;

	pr_debug("trident: trident_read called, count = %zd\n", count);

	VALIDATE_STATE(state);

	if (dmabuf->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;

	mutex_lock(&state->sem);
	if (!dmabuf->ready && (ret = prog_dmabuf_record(state)))
		goto out;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count > (signed) dmabuf->dmasize) {
			/* buffer overrun, we are recovering from */ 
			/* sleep_on_timeout, resync hwptr and swptr, */ 
			/* make process flush the buffer */
			dmabuf->count = dmabuf->dmasize;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count < cnt)
			cnt = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is empty, start the dma machine and */ 
			/* wait for data to be recorded */
			start_adc(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto out;
			}

			mutex_unlock(&state->sem);
			/* No matter how much space left in the buffer, */ 
			/* we have to wait until CSO == ESO/2 or CSO == ESO */ 
			/* when address engine interrupts */
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			/* There are two situations when sleep_on_timeout returns, one is when
			   the interrupt is serviced correctly and the process is waked up by
			   ISR ON TIME. Another is when timeout is expired, which means that
			   either interrupt is NOT serviced correctly (pending interrupt) or it
			   is TOO LATE for the process to be scheduled to run (scheduler latency)
			   which results in a (potential) buffer overrun. And worse, there is
			   NOTHING we can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
				pr_debug(KERN_ERR "trident: recording schedule timeout, "
					 "dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
					 dmabuf->dmasize, dmabuf->fragsize, dmabuf->count,
					 dmabuf->hwptr, dmabuf->swptr);

				/* a buffer overrun, we delay the recovery until next time the
				   while loop begin and we REALLY have space to record */
			}
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out;
			}
			mutex_lock(&state->sem);
			if (dmabuf->mapped) {
				if (!ret)
					ret = -ENXIO;
				goto out;
			}
			continue;
		}

		if (copy_to_user(buffer, dmabuf->rawbuf + swptr, cnt)) {
			if (!ret)
				ret = -EFAULT;
			goto out;
		}

		swptr = (swptr + cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count -= cnt;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(state);
	}
out:
	mutex_unlock(&state->sem);
	return ret;
}

/* in this loop, dmabuf.count signifies the amount of data that is waiting to be dma to
   the soundcard.  it is drained by the dma machine and filled by this loop. */

static ssize_t
trident_write(struct file *file, const char __user *buffer, size_t count, loff_t * ppos)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	unsigned int state_cnt;
	unsigned int copy_count;
	int lret; /* for lock_set_fmt */

	pr_debug("trident: trident_write called, count = %zd\n", count);

	VALIDATE_STATE(state);

	/*
	 *      Guard against an mmap or ioctl while writing
	 */

	mutex_lock(&state->sem);

	if (dmabuf->mapped) {
		ret = -ENXIO;
		goto out;
	}
	if (!dmabuf->ready && (ret = prog_dmabuf_playback(state)))
		goto out;

	if (!access_ok(VERIFY_READ, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}

	ret = 0;

	while (count > 0) {
		spin_lock_irqsave(&state->card->lock, flags);
		if (dmabuf->count < 0) {
			/* buffer underrun, we are recovering from */ 
			/* sleep_on_timeout, resync hwptr and swptr */
			dmabuf->count = 0;
			dmabuf->swptr = dmabuf->hwptr;
		}
		swptr = dmabuf->swptr;
		cnt = dmabuf->dmasize - swptr;
		if (dmabuf->count + cnt > dmabuf->dmasize)
			cnt = dmabuf->dmasize - dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);

		if (cnt > count)
			cnt = count;
		if (cnt <= 0) {
			unsigned long tmo;
			/* buffer is full, start the dma machine and */ 
			/* wait for data to be played */
			start_dac(state);
			if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				goto out;
			}
			/* No matter how much data left in the buffer, */ 
			/* we have to wait until CSO == ESO/2 or CSO == ESO */ 
			/* when address engine interrupts */
			lock_set_fmt(state);
			tmo = (dmabuf->dmasize * HZ) / (dmabuf->rate * 2);
			tmo >>= sample_shift[dmabuf->fmt];
			unlock_set_fmt(state);
			mutex_unlock(&state->sem);

			/* There are two situations when sleep_on_timeout */ 
			/* returns, one is when the interrupt is serviced */ 
			/* correctly and the process is waked up by ISR */ 
			/* ON TIME. Another is when timeout is expired, which */ 
			/* means that either interrupt is NOT serviced */ 
			/* correctly (pending interrupt) or it is TOO LATE */ 
			/* for the process to be scheduled to run */ 
			/* (scheduler latency) which results in a (potential) */ 
			/* buffer underrun. And worse, there is NOTHING we */ 
			/* can do to prevent it. */
			if (!interruptible_sleep_on_timeout(&dmabuf->wait, tmo)) {
				pr_debug(KERN_ERR "trident: playback schedule "
					 "timeout, dmasz %u fragsz %u count %i "
					 "hwptr %u swptr %u\n", dmabuf->dmasize,
					 dmabuf->fragsize, dmabuf->count,
					 dmabuf->hwptr, dmabuf->swptr);

				/* a buffer underrun, we delay the recovery */ 
				/* until next time the while loop begin and */ 
				/* we REALLY have data to play */
			}
			if (signal_pending(current)) {
				if (!ret)
					ret = -ERESTARTSYS;
				goto out_nolock;
			}
			mutex_lock(&state->sem);
			if (dmabuf->mapped) {
				if (!ret)
					ret = -ENXIO;
				goto out;
			}
			continue;
		}
		if ((lret = lock_set_fmt(state)) < 0) {
			ret = lret;
			goto out;
		}

		if (state->chans_num == 6) {
			copy_count = 0;
			state_cnt = 0;
			if (ali_write_5_1(state, buffer, cnt, &copy_count, 
					  &state_cnt) == -EFAULT) {
				if (state_cnt) {
					swptr = (swptr + state_cnt) % dmabuf->dmasize;
					spin_lock_irqsave(&state->card->lock, flags);
					dmabuf->swptr = swptr;
					dmabuf->count += state_cnt;
					dmabuf->endcleared = 0;
					spin_unlock_irqrestore(&state->card->lock, flags);
				}
				ret += copy_count;
				if (!ret)
					ret = -EFAULT;
				unlock_set_fmt(state);
				goto out;
			}
		} else {
			if (copy_from_user(dmabuf->rawbuf + swptr, 
					   buffer, cnt)) {
				if (!ret)
					ret = -EFAULT;
				unlock_set_fmt(state);
				goto out;
			}
			state_cnt = cnt;
		}
		unlock_set_fmt(state);

		swptr = (swptr + state_cnt) % dmabuf->dmasize;

		spin_lock_irqsave(&state->card->lock, flags);
		dmabuf->swptr = swptr;
		dmabuf->count += state_cnt;
		dmabuf->endcleared = 0;
		spin_unlock_irqrestore(&state->card->lock, flags);

		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(state);
	}
out:
	mutex_unlock(&state->sem);
out_nolock:
	return ret;
}

/* No kernel lock - we have our own spinlock */
static unsigned int
trident_poll(struct file *file, struct poll_table_struct *wait)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(state);

	/*
	 *      Guard against a parallel poll and write causing multiple
	 *      prog_dmabuf events
	 */

	mutex_lock(&state->sem);

	if (file->f_mode & FMODE_WRITE) {
		if (!dmabuf->ready && prog_dmabuf_playback(state)) {
			mutex_unlock(&state->sem);
			return 0;
		}
		poll_wait(file, &dmabuf->wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!dmabuf->ready && prog_dmabuf_record(state)) {
			mutex_unlock(&state->sem);
			return 0;
		}
		poll_wait(file, &dmabuf->wait, wait);
	}

	mutex_unlock(&state->sem);

	spin_lock_irqsave(&state->card->lock, flags);
	trident_update_ptr(state);
	if (file->f_mode & FMODE_READ) {
		if (dmabuf->count >= (signed) dmabuf->fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (dmabuf->mapped) {
			if (dmabuf->count >= (signed) dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed) dmabuf->dmasize >= dmabuf->count + 
			    (signed) dmabuf->fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&state->card->lock, flags);

	return mask;
}

static int
trident_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	int ret = -EINVAL;
	unsigned long size;

	VALIDATE_STATE(state);

	/*
	 *      Lock against poll read write or mmap creating buffers. Also lock
	 *      a read or write against an mmap.
	 */

	mutex_lock(&state->sem);

	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf_playback(state)) != 0)
			goto out;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf_record(state)) != 0)
			goto out;
	} else
		goto out;

	ret = -EINVAL;
	if (vma->vm_pgoff != 0)
		goto out;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << dmabuf->buforder))
		goto out;
	ret = -EAGAIN;
	if (remap_pfn_range(vma, vma->vm_start,
			     virt_to_phys(dmabuf->rawbuf) >> PAGE_SHIFT,
			     size, vma->vm_page_prot))
		goto out;
	dmabuf->mapped = 1;
	ret = 0;
out:
	mutex_unlock(&state->sem);
	return ret;
}

static int
trident_ioctl(struct inode *inode, struct file *file, 
	      unsigned int cmd, unsigned long arg)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct dmabuf *dmabuf = &state->dmabuf;
	unsigned long flags;
	audio_buf_info abinfo;
	count_info cinfo;
	int val, mapped, ret = 0;
	struct trident_card *card = state->card;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	VALIDATE_STATE(state);


	mapped = ((file->f_mode & (FMODE_WRITE | FMODE_READ)) && dmabuf->mapped);

	pr_debug("trident: trident_ioctl, command = %2d, arg = 0x%08x\n",
		 _IOC_NR(cmd), arg ? *p : 0);

	switch (cmd) {
	case OSS_GETVERSION:
		ret = put_user(SOUND_VERSION, p);
		break;

	case SNDCTL_DSP_RESET:
		/* FIXME: spin_lock ? */
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			synchronize_irq(card->irq);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			synchronize_irq(card->irq);
			dmabuf->ready = 0;
			dmabuf->swptr = dmabuf->hwptr = 0;
			dmabuf->count = dmabuf->total_bytes = 0;
		}
		break;

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			ret = drain_dac(state, file->f_flags & O_NONBLOCK);
		break;

	case SNDCTL_DSP_SPEED:	/* set smaple rate */
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if (val >= 0) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				trident_set_dac_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				spin_lock_irqsave(&state->card->lock, flags);
				trident_set_adc_rate(state, val);
				spin_unlock_irqrestore(&state->card->lock, flags);
			}
		}
		ret = put_user(dmabuf->rate, p);
		break;

	case SNDCTL_DSP_STEREO:	/* set stereo or mono channel */
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if ((ret = lock_set_fmt(state)) < 0)
			return ret;

		if (file->f_mode & FMODE_WRITE) {
			stop_dac(state);
			dmabuf->ready = 0;
			if (val)
				dmabuf->fmt |= TRIDENT_FMT_STEREO;
			else
				dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(state);
			dmabuf->ready = 0;
			if (val)
				dmabuf->fmt |= TRIDENT_FMT_STEREO;
			else
				dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
		}
		unlock_set_fmt(state);
		break;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf_playback(state)))
				ret = val;
			else
				ret = put_user(dmabuf->fragsize, p);
			break;
		}
		if (file->f_mode & FMODE_READ) {
			if ((val = prog_dmabuf_record(state)))
				ret = val;
			else
				ret = put_user(dmabuf->fragsize, p);
			break;
		}
		/* neither READ nor WRITE? is this even possible? */
		ret = -EINVAL;
		break;


	case SNDCTL_DSP_GETFMTS: /* Returns a mask of supported sample format */
		ret = put_user(AFMT_S16_LE | AFMT_U16_LE | AFMT_S8 | 
			       AFMT_U8, p);
		break;

	case SNDCTL_DSP_SETFMT:	/* Select sample format */
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if ((ret = lock_set_fmt(state)) < 0)
			return ret;

		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;
				if (val == AFMT_S16_LE)
					dmabuf->fmt |= TRIDENT_FMT_16BIT;
				else
					dmabuf->fmt &= ~TRIDENT_FMT_16BIT;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				if (val == AFMT_S16_LE)
					dmabuf->fmt |= TRIDENT_FMT_16BIT;
				else
					dmabuf->fmt &= ~TRIDENT_FMT_16BIT;
			}
		}
		unlock_set_fmt(state);
		ret = put_user((dmabuf->fmt & TRIDENT_FMT_16BIT) ? AFMT_S16_LE : 
			       AFMT_U8, p);
		break;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if (val != 0) {
			if ((ret = lock_set_fmt(state)) < 0)
				return ret;

			if (file->f_mode & FMODE_WRITE) {
				stop_dac(state);
				dmabuf->ready = 0;

				//prevent from memory leak
				if ((state->chans_num > 2) && (state->chans_num != val)) {
					ali_free_other_states_resources(state);
					state->chans_num = 1;
				}

				if (val >= 2) {

					dmabuf->fmt |= TRIDENT_FMT_STEREO;
					if ((val == 6) && (state->card->pci_id == PCI_DEVICE_ID_ALI_5451)) {
						if (card->rec_channel_use_count > 0) {
							printk(KERN_ERR "trident: Record is "
							       "working on the card!\n");
							ret = -EBUSY;
							unlock_set_fmt(state);
							break;
						}

						ret = ali_setup_multi_channels(state->card, 6);
						if (ret < 0) {
							unlock_set_fmt(state);
							break;
						}
						mutex_lock(&state->card->open_mutex);
						ret = ali_allocate_other_states_resources(state, 6);
						if (ret < 0) {
							mutex_unlock(&state->card->open_mutex);
							unlock_set_fmt(state);
							break;
						}
						state->card->multi_channel_use_count++;
						mutex_unlock(&state->card->open_mutex);
					} else
						val = 2;	/*yield to 2-channels */
				} else
					dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
				state->chans_num = val;
			}
			if (file->f_mode & FMODE_READ) {
				stop_adc(state);
				dmabuf->ready = 0;
				if (val >= 2) {
					if (!((file->f_mode & FMODE_WRITE) && 
					      (val == 6)))
						val = 2;
					dmabuf->fmt |= TRIDENT_FMT_STEREO;
				} else
					dmabuf->fmt &= ~TRIDENT_FMT_STEREO;
				state->chans_num = val;
			}
			unlock_set_fmt(state);
		}
		ret = put_user(val, p);
		break;

	case SNDCTL_DSP_POST:
		/* Cause the working fragment to be output */
		break;

	case SNDCTL_DSP_SUBDIVIDE:
		if (dmabuf->subdivision) {
			ret = -EINVAL;
			break;
		}
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if (val != 1 && val != 2 && val != 4) {
			ret = -EINVAL;
			break;
		}
		dmabuf->subdivision = val;
		break;

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}

		dmabuf->ossfragshift = val & 0xffff;
		dmabuf->ossmaxfrags = (val >> 16) & 0xffff;
		if (dmabuf->ossfragshift < 4)
			dmabuf->ossfragshift = 4;
		if (dmabuf->ossfragshift > 15)
			dmabuf->ossfragshift = 15;
		if (dmabuf->ossmaxfrags < 4)
			dmabuf->ossmaxfrags = 4;

		break;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE)) {
			ret = -EINVAL;
			break;
		}
		if (!dmabuf->ready && (val = prog_dmabuf_playback(state)) != 0) {
			ret = val;
			break;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->dmasize - dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		ret = copy_to_user(argp, &abinfo, sizeof (abinfo)) ? 
			-EFAULT : 0;
		break;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ)) {
			ret = -EINVAL;
			break;
		}
		if (!dmabuf->ready && (val = prog_dmabuf_record(state)) != 0) {
			ret = val;
			break;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		abinfo.fragsize = dmabuf->fragsize;
		abinfo.bytes = dmabuf->count;
		abinfo.fragstotal = dmabuf->numfrag;
		abinfo.fragments = abinfo.bytes >> dmabuf->fragshift;
		spin_unlock_irqrestore(&state->card->lock, flags);
		ret = copy_to_user(argp, &abinfo, sizeof (abinfo)) ? 
			-EFAULT : 0;
		break;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		break;

	case SNDCTL_DSP_GETCAPS:
		ret = put_user(DSP_CAP_REALTIME | DSP_CAP_TRIGGER | 
			       DSP_CAP_MMAP | DSP_CAP_BIND, p);
		break;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if ((file->f_mode & FMODE_READ) && dmabuf->enable)
			val |= PCM_ENABLE_INPUT;
		if ((file->f_mode & FMODE_WRITE) && dmabuf->enable)
			val |= PCM_ENABLE_OUTPUT;
		ret = put_user(val, p);
		break;

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!dmabuf->ready && 
				    (ret = prog_dmabuf_record(state)))
					break;
				start_adc(state);
			} else
				stop_adc(state);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!dmabuf->ready && 
				    (ret = prog_dmabuf_playback(state)))
					break;
				start_dac(state);
			} else
				stop_dac(state);
		}
		break;

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ)) {
			ret = -EINVAL;
			break;
		}
		if (!dmabuf->ready && (val = prog_dmabuf_record(state))
		    != 0) {
			ret = val;
			break;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize - 1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		ret = copy_to_user(argp, &cinfo, sizeof (cinfo)) ? 
			-EFAULT : 0;
		break;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE)) {
			ret = -EINVAL;
			break;
		}
		if (!dmabuf->ready && (val = prog_dmabuf_playback(state))
		    != 0) {
			ret = val;
			break;
		}

		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		cinfo.bytes = dmabuf->total_bytes;
		cinfo.blocks = dmabuf->count >> dmabuf->fragshift;
		cinfo.ptr = dmabuf->hwptr;
		if (dmabuf->mapped)
			dmabuf->count &= dmabuf->fragsize - 1;
		spin_unlock_irqrestore(&state->card->lock, flags);
		ret = copy_to_user(argp, &cinfo, sizeof (cinfo)) ? 
			-EFAULT : 0;
		break;

	case SNDCTL_DSP_SETDUPLEX:
		ret = -EINVAL;
		break;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE)) {
			ret = -EINVAL;
			break;
		}
		if (!dmabuf->ready && (val = prog_dmabuf_playback(state)) != 0) {
			ret = val;
			break;
		}
		spin_lock_irqsave(&state->card->lock, flags);
		trident_update_ptr(state);
		val = dmabuf->count;
		spin_unlock_irqrestore(&state->card->lock, flags);
		ret = put_user(val, p);
		break;

	case SOUND_PCM_READ_RATE:
		ret = put_user(dmabuf->rate, p);
		break;

	case SOUND_PCM_READ_CHANNELS:
		ret = put_user((dmabuf->fmt & TRIDENT_FMT_STEREO) ? 2 : 1, 
			       p);
		break;

	case SOUND_PCM_READ_BITS:
		ret = put_user((dmabuf->fmt & TRIDENT_FMT_16BIT) ? AFMT_S16_LE : 
			       AFMT_U8, p);
		break;

	case SNDCTL_DSP_GETCHANNELMASK:
		ret = put_user(DSP_BIND_FRONT | DSP_BIND_SURR | 
			       DSP_BIND_CENTER_LFE,  p);
		break;

	case SNDCTL_DSP_BIND_CHANNEL:
		if (state->card->pci_id != PCI_DEVICE_ID_SI_7018) {
			ret = -EINVAL;
			break;
		}

		if (get_user(val, p)) {
			ret = -EFAULT;
			break;
		}
		if (val == DSP_BIND_QUERY) {
			val = dmabuf->channel->attribute | 0x3c00;
			val = attr2mask[val >> 8];
		} else {
			dmabuf->ready = 0;
			if (file->f_mode & FMODE_READ)
				dmabuf->channel->attribute = (CHANNEL_REC | 
							      SRC_ENABLE);
			if (file->f_mode & FMODE_WRITE)
				dmabuf->channel->attribute = (CHANNEL_SPC_PB | 
							      SRC_ENABLE);
			dmabuf->channel->attribute |= mask2attr[ffs(val)];
		}
		ret = put_user(val, p);
		break;

	case SNDCTL_DSP_MAPINBUF:
	case SNDCTL_DSP_MAPOUTBUF:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_WRITE_FILTER:
	case SOUND_PCM_READ_FILTER:
	default:
		ret = -EINVAL;
		break;

	}
	return ret;
}

static int
trident_open(struct inode *inode, struct file *file)
{
	int i = 0;
	int minor = iminor(inode);
	struct trident_card *card = devs;
	struct trident_state *state = NULL;
	struct dmabuf *dmabuf = NULL;

	/* Added by Matt Wu 01-05-2001 */
	/* TODO: there's some redundacy here wrt the check below */
	/* for multi_use_count > 0. Should we return -EBUSY or find */
	/* a different card? for now, don't break current behaviour */
	/* -- mulix */
	if (file->f_mode & FMODE_READ) {
		if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			if (card->multi_channel_use_count > 0)
				return -EBUSY;
		}
	}

	/* find an available virtual channel (instance of /dev/dsp) */
	while (card != NULL) {
		mutex_lock(&card->open_mutex);
		if (file->f_mode & FMODE_READ) {
			/* Skip opens on cards that are in 6 channel mode */
			if (card->multi_channel_use_count > 0) {
				mutex_unlock(&card->open_mutex);
				card = card->next;
				continue;
			}
		}
		for (i = 0; i < NR_HW_CH; i++) {
			if (card->states[i] == NULL) {
				state = card->states[i] = kzalloc(sizeof(*state), GFP_KERNEL);
				if (state == NULL) {
					mutex_unlock(&card->open_mutex);
					return -ENOMEM;
				}
				mutex_init(&state->sem);
				dmabuf = &state->dmabuf;
				goto found_virt;
			}
		}
		mutex_unlock(&card->open_mutex);
		card = card->next;
	}
	/* no more virtual channel avaiable */
	if (!state) {
		return -ENODEV;
	}
      found_virt:
	/* found a free virtual channel, allocate hardware channels */
	if (file->f_mode & FMODE_READ)
		dmabuf->channel = card->alloc_rec_pcm_channel(card);
	else
		dmabuf->channel = card->alloc_pcm_channel(card);

	if (dmabuf->channel == NULL) {
		kfree(card->states[i]);
		card->states[i] = NULL;
		return -ENODEV;
	}

	/* initialize the virtual channel */
	state->virt = i;
	state->card = card;
	state->magic = TRIDENT_STATE_MAGIC;
	init_waitqueue_head(&dmabuf->wait);
	file->private_data = state;

	/* set default sample format. According to OSS Programmer's */ 
	/* Guide  /dev/dsp should be default to unsigned 8-bits, mono, */ 
	/* with sample rate 8kHz and /dev/dspW will accept 16-bits sample */
	if (file->f_mode & FMODE_WRITE) {
		dmabuf->fmt &= ~TRIDENT_FMT_MASK;
		if ((minor & 0x0f) == SND_DEV_DSP16)
			dmabuf->fmt |= TRIDENT_FMT_16BIT;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags = 0;
		dmabuf->subdivision = 0;
		if (card->pci_id == PCI_DEVICE_ID_SI_7018) {
			/* set default channel attribute to normal playback */
			dmabuf->channel->attribute = CHANNEL_PB;
		}
		trident_set_dac_rate(state, 8000);
	}

	if (file->f_mode & FMODE_READ) {
		/* FIXME: Trident 4d can only record in signed 16-bits stereo, */ 
		/* 48kHz sample, to be dealed with in trident_set_adc_rate() ?? */
		dmabuf->fmt &= ~TRIDENT_FMT_MASK;
		if ((minor & 0x0f) == SND_DEV_DSP16)
			dmabuf->fmt |= TRIDENT_FMT_16BIT;
		dmabuf->ossfragshift = 0;
		dmabuf->ossmaxfrags = 0;
		dmabuf->subdivision = 0;
		if (card->pci_id == PCI_DEVICE_ID_SI_7018) {
			/* set default channel attribute to 0x8a80, record from
			   PCM L/R FIFO and mono = (left + right + 1)/2 */
			dmabuf->channel->attribute = (CHANNEL_REC | PCM_LR | 
						      MONO_MIX);
		}
		trident_set_adc_rate(state, 8000);

		/* Added by Matt Wu 01-05-2001 */
		if (card->pci_id == PCI_DEVICE_ID_ALI_5451)
			card->rec_channel_use_count++;
	}

	state->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	mutex_unlock(&card->open_mutex);

	pr_debug("trident: open virtual channel %d, hard channel %d\n",
		 state->virt, dmabuf->channel->num);

	return nonseekable_open(inode, file);
}

static int
trident_release(struct inode *inode, struct file *file)
{
	struct trident_state *state = (struct trident_state *)file->private_data;
	struct trident_card *card;
	struct dmabuf *dmabuf;

	VALIDATE_STATE(state);

	card = state->card;
	dmabuf = &state->dmabuf;

	if (file->f_mode & FMODE_WRITE) {
		trident_clear_tail(state);
		drain_dac(state, file->f_flags & O_NONBLOCK);
	}

	pr_debug("trident: closing virtual channel %d, hard channel %d\n",
		 state->virt, dmabuf->channel->num);

	/* stop DMA state machine and free DMA buffers/channels */
	mutex_lock(&card->open_mutex);

	if (file->f_mode & FMODE_WRITE) {
		stop_dac(state);
		dealloc_dmabuf(&state->dmabuf, state->card->pci_dev);
		state->card->free_pcm_channel(state->card, dmabuf->channel->num);

		/* Added by Matt Wu */
		if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			if (state->chans_num > 2) {
				if (card->multi_channel_use_count-- < 0)
					card->multi_channel_use_count = 0;
				if (card->multi_channel_use_count == 0)
					ali_close_multi_channels();
				ali_free_other_states_resources(state);
			}
		}
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(state);
		dealloc_dmabuf(&state->dmabuf, state->card->pci_dev);
		state->card->free_pcm_channel(state->card, dmabuf->channel->num);

		/* Added by Matt Wu */
		if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			if (card->rec_channel_use_count-- < 0)
				card->rec_channel_use_count = 0;
		}
	}

	card->states[state->virt] = NULL;
	kfree(state);

	/* we're covered by the open_mutex */
	mutex_unlock(&card->open_mutex);

	return 0;
}

static /*const */ struct file_operations trident_audio_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = trident_read,
	.write = trident_write,
	.poll = trident_poll,
	.ioctl = trident_ioctl,
	.mmap = trident_mmap,
	.open = trident_open,
	.release = trident_release,
};

/* trident specific AC97 functions */
/* Write AC97 codec registers */
static void
trident_ac97_set(struct ac97_codec *codec, u8 reg, u16 val)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count = 0xffff;
	unsigned long flags;
	u32 data;

	data = ((u32) val) << 16;

	switch (card->pci_id) {
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_WRITE;
		mask = SI_AC97_BUSY_WRITE | SI_AC97_AUDIO_BUSY;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR0_AC97_W;
		mask = busy = DX_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		address = NX_ACR1_AC97_W;
		mask = NX_AC97_BUSY_WRITE;
		if (codec->id)
			mask |= NX_AC97_WRITE_SECONDARY;
		busy = NX_AC97_BUSY_WRITE;
		break;
	case PCI_DEVICE_ID_INTERG_5050:
		address = SI_AC97_WRITE;
		mask = busy = SI_AC97_BUSY_WRITE;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		break;
	}

	spin_lock_irqsave(&card->lock, flags);
	do {
		if ((inw(TRID_REG(card, address)) & busy) == 0)
			break;
	} while (count--);

	data |= (mask | (reg & AC97_REG_ADDR));

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC write timed out.\n");
		spin_unlock_irqrestore(&card->lock, flags);
		return;
	}

	outl(data, TRID_REG(card, address));
	spin_unlock_irqrestore(&card->lock, flags);
}

/* Read AC97 codec registers */
static u16
trident_ac97_get(struct ac97_codec *codec, u8 reg)
{
	struct trident_card *card = (struct trident_card *)codec->private_data;
	unsigned int address, mask, busy;
	unsigned short count = 0xffff;
	unsigned long flags;
	u32 data;

	switch (card->pci_id) {
	default:
	case PCI_DEVICE_ID_SI_7018:
		address = SI_AC97_READ;
		mask = SI_AC97_BUSY_READ | SI_AC97_AUDIO_BUSY;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		busy = SI_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		address = DX_ACR1_AC97_R;
		mask = busy = DX_AC97_BUSY_READ;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		if (codec->id)
			address = NX_ACR3_AC97_R_SECONDARY;
		else
			address = NX_ACR2_AC97_R_PRIMARY;
		mask = NX_AC97_BUSY_READ;
		busy = NX_AC97_BUSY_READ | NX_AC97_BUSY_DATA;
		break;
	case PCI_DEVICE_ID_INTERG_5050:
		address = SI_AC97_READ;
		mask = busy = SI_AC97_BUSY_READ;
		if (codec->id)
			mask |= SI_AC97_SECONDARY;
		break;
	}

	data = (mask | (reg & AC97_REG_ADDR));

	spin_lock_irqsave(&card->lock, flags);
	outl(data, TRID_REG(card, address));
	do {
		data = inl(TRID_REG(card, address));
		if ((data & busy) == 0)
			break;
	} while (count--);
	spin_unlock_irqrestore(&card->lock, flags);

	if (count == 0) {
		printk(KERN_ERR "trident: AC97 CODEC read timed out.\n");
		data = 0;
	}
	return ((u16) (data >> 16));
}

/* rewrite ac97 read and write mixer register by hulei for ALI*/
static int
acquirecodecaccess(struct trident_card *card)
{
	u16 wsemamask = 0x6000;	/* bit 14..13 */
	u16 wsemabits;
	u16 wcontrol;
	int block = 0;
	int ncount = 25;
	while (1) {
		wcontrol = inw(TRID_REG(card, ALI_AC97_WRITE));
		wsemabits = wcontrol & wsemamask;

		if (wsemabits == 0x4000)
			return 1;	/* 0x4000 is audio ,then success */
		if (ncount-- < 0)
			break;
		if (wsemabits == 0) {
		      unlock:
			outl(((u32) (wcontrol & 0x1eff) | 0x00004000), 
			     TRID_REG(card, ALI_AC97_WRITE));
			continue;
		}
		udelay(20);
	}
	if (!block) {
		pr_debug("accesscodecsemaphore: try unlock\n");
		block = 1;
		goto unlock;
	}
	return 0;
}

static void
releasecodecaccess(struct trident_card *card)
{
	unsigned long wcontrol;
	wcontrol = inl(TRID_REG(card, ALI_AC97_WRITE));
	outl((wcontrol & 0xffff1eff), TRID_REG(card, ALI_AC97_WRITE));
}

static int
waitforstimertick(struct trident_card *card)
{
	unsigned long chk1, chk2;
	unsigned int wcount = 0xffff;
	chk1 = inl(TRID_REG(card, ALI_STIMER));

	while (1) {
		chk2 = inl(TRID_REG(card, ALI_STIMER));
		if ((wcount > 0) && chk1 != chk2)
			return 1;
		if (wcount <= 0)
			break;
		udelay(50);
	}
	return 0;
}

/* Read AC97 codec registers for ALi*/
static u16
ali_ac97_get(struct trident_card *card, int secondary, u8 reg)
{
	unsigned int address, mask;
	unsigned int ncount;
	unsigned long aud_reg;
	u32 data;
	u16 wcontrol;
	unsigned long flags;

	if (!card)
		BUG();

	address = ALI_AC97_READ;
	if (card->revision == ALI_5451_V02) {
		address = ALI_AC97_WRITE;
	}
	mask = ALI_AC97_READ_ACTION | ALI_AC97_AUDIO_BUSY;
	if (secondary)
		mask |= ALI_AC97_SECONDARY;

	spin_lock_irqsave(&card->lock, flags);

	if (!acquirecodecaccess(card))
		printk(KERN_ERR "access codec fail\n");

	wcontrol = inw(TRID_REG(card, ALI_AC97_WRITE));
	wcontrol &= 0xfe00;
	wcontrol |= (0x8000 | reg);
	outw(wcontrol, TRID_REG(card, ALI_AC97_WRITE));

	data = (mask | (reg & AC97_REG_ADDR));

	if (!waitforstimertick(card)) {
		printk(KERN_ERR "ali_ac97_read: BIT_CLOCK is dead\n");
		goto releasecodec;
	}

	udelay(20);

	ncount = 10;

	while (1) {
		if ((inw(TRID_REG(card, ALI_AC97_WRITE)) & ALI_AC97_BUSY_READ) 
		    != 0)
			break;
		if (ncount <= 0)
			break;
		if (ncount-- == 1) {
			pr_debug("ali_ac97_read :try clear busy flag\n");
			aud_reg = inl(TRID_REG(card, ALI_AC97_WRITE));
			outl((aud_reg & 0xffff7fff), 
			     TRID_REG(card, ALI_AC97_WRITE));
		}
		udelay(10);
	}

	data = inl(TRID_REG(card, address));

	spin_unlock_irqrestore(&card->lock, flags);

	return ((u16) (data >> 16));

      releasecodec:
	releasecodecaccess(card);
	spin_unlock_irqrestore(&card->lock, flags);
	printk(KERN_ERR "ali_ac97_read: AC97 CODEC read timed out.\n");
	return 0;
}

/* Write AC97 codec registers for hulei*/
static void
ali_ac97_set(struct trident_card *card, int secondary, u8 reg, u16 val)
{
	unsigned int address, mask;
	unsigned int ncount;
	u32 data;
	u16 wcontrol;
	unsigned long flags;

	data = ((u32) val) << 16;

	if (!card)
		BUG();

	address = ALI_AC97_WRITE;
	mask = ALI_AC97_WRITE_ACTION | ALI_AC97_AUDIO_BUSY;
	if (secondary)
		mask |= ALI_AC97_SECONDARY;
	if (card->revision == ALI_5451_V02)
		mask |= ALI_AC97_WRITE_MIXER_REGISTER;

	spin_lock_irqsave(&card->lock, flags);
	if (!acquirecodecaccess(card))
		printk(KERN_ERR "ali_ac97_write: access codec fail\n");

	wcontrol = inw(TRID_REG(card, ALI_AC97_WRITE));
	wcontrol &= 0xff00;
	wcontrol |= (0x8100 | reg); /* bit 8=1: (ali1535 )reserved/ */ 
	                            /* ali1535+ write */
	outl((data | wcontrol), TRID_REG(card, ALI_AC97_WRITE));

	if (!waitforstimertick(card)) {
		printk(KERN_ERR "BIT_CLOCK is dead\n");
		goto releasecodec;
	}

	ncount = 10;
	while (1) {
		wcontrol = inw(TRID_REG(card, ALI_AC97_WRITE));
		if (!(wcontrol & 0x8000))
			break;
		if (ncount <= 0)
			break;
		if (ncount-- == 1) {
			pr_debug("ali_ac97_set :try clear busy flag!!\n");
			outw(wcontrol & 0x7fff, 
			     TRID_REG(card, ALI_AC97_WRITE));
		}
		udelay(10);
	}

      releasecodec:
	releasecodecaccess(card);
	spin_unlock_irqrestore(&card->lock, flags);
	return;
}

static void
ali_enable_special_channel(struct trident_state *stat)
{
	struct trident_card *card = stat->card;
	unsigned long s_channels;

	s_channels = inl(TRID_REG(card, ALI_GLOBAL_CONTROL));
	s_channels |= (1 << stat->dmabuf.channel->num);
	outl(s_channels, TRID_REG(card, ALI_GLOBAL_CONTROL));
}

static u16
ali_ac97_read(struct ac97_codec *codec, u8 reg)
{
	int id;
	u16 data;
	struct trident_card *card = NULL;

	/* Added by Matt Wu */
	if (!codec)
		BUG();

	card = (struct trident_card *) codec->private_data;

	if (!card->mixer_regs_ready)
		return ali_ac97_get(card, codec->id, reg);

	/*
	 *      FIXME: need to stop this caching some registers
	 */
	if (codec->id)
		id = 1;
	else
		id = 0;

	data = card->mixer_regs[reg / 2][id];
	return data;
}

static void
ali_ac97_write(struct ac97_codec *codec, u8 reg, u16 val)
{
	int id;
	struct trident_card *card;

	/*  Added by Matt Wu */
	if (!codec)
		BUG();

	card = (struct trident_card *) codec->private_data;

	if (!card->mixer_regs_ready) {
		ali_ac97_set(card, codec->id, reg, val);
		return;
	}

	if (codec->id)
		id = 1;
	else
		id = 0;

	card->mixer_regs[reg / 2][id] = val;
	ali_ac97_set(card, codec->id, reg, val);
}

/*
flag:	ALI_SPDIF_OUT_TO_SPDIF_OUT
	ALI_PCM_TO_SPDIF_OUT
*/

static void
ali_setup_spdif_out(struct trident_card *card, int flag)
{
	unsigned long spdif;
	unsigned char ch;

	char temp;
	struct pci_dev *pci_dev = NULL;

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533,
				 pci_dev);
	if (pci_dev == NULL)
		return;
	pci_read_config_byte(pci_dev, 0x61, &temp);
	temp |= 0x40;
	pci_write_config_byte(pci_dev, 0x61, temp);
	pci_read_config_byte(pci_dev, 0x7d, &temp);
	temp |= 0x01;
	pci_write_config_byte(pci_dev, 0x7d, temp);
	pci_read_config_byte(pci_dev, 0x7e, &temp);
	temp &= (~0x20);
	temp |= 0x10;
	pci_write_config_byte(pci_dev, 0x7e, temp);

	pci_dev_put(pci_dev);

	ch = inb(TRID_REG(card, ALI_SCTRL));
	outb(ch | ALI_SPDIF_OUT_ENABLE, TRID_REG(card, ALI_SCTRL));
	ch = inb(TRID_REG(card, ALI_SPDIF_CTRL));
	outb(ch & ALI_SPDIF_OUT_CH_STATUS, TRID_REG(card, ALI_SPDIF_CTRL));

	if (flag & ALI_SPDIF_OUT_TO_SPDIF_OUT) {
		spdif = inw(TRID_REG(card, ALI_GLOBAL_CONTROL));
		spdif |= ALI_SPDIF_OUT_CH_ENABLE;
		spdif &= ALI_SPDIF_OUT_SEL_SPDIF;
		outw(spdif, TRID_REG(card, ALI_GLOBAL_CONTROL));
		spdif = inw(TRID_REG(card, ALI_SPDIF_CS));
		if (flag & ALI_SPDIF_OUT_NON_PCM)
			spdif |= 0x0002;
		else
			spdif &= (~0x0002);
		outw(spdif, TRID_REG(card, ALI_SPDIF_CS));
	} else {
		spdif = inw(TRID_REG(card, ALI_GLOBAL_CONTROL));
		spdif |= ALI_SPDIF_OUT_SEL_PCM;
		outw(spdif, TRID_REG(card, ALI_GLOBAL_CONTROL));
	}
}

static void
ali_disable_special_channel(struct trident_card *card, int ch)
{
	unsigned long sc;

	sc = inl(TRID_REG(card, ALI_GLOBAL_CONTROL));
	sc &= ~(1 << ch);
	outl(sc, TRID_REG(card, ALI_GLOBAL_CONTROL));
}

static void
ali_disable_spdif_in(struct trident_card *card)
{
	unsigned long spdif;

	spdif = inl(TRID_REG(card, ALI_GLOBAL_CONTROL));
	spdif &= (~ALI_SPDIF_IN_SUPPORT);
	outl(spdif, TRID_REG(card, ALI_GLOBAL_CONTROL));

	ali_disable_special_channel(card, ALI_SPDIF_IN_CHANNEL);
}

static void
ali_setup_spdif_in(struct trident_card *card)
{
	unsigned long spdif;

	//Set SPDIF IN Supported
	spdif = inl(TRID_REG(card, ALI_GLOBAL_CONTROL));
	spdif |= ALI_SPDIF_IN_SUPPORT;
	outl(spdif, TRID_REG(card, ALI_GLOBAL_CONTROL));

	//Set SPDIF IN Rec
	spdif = inl(TRID_REG(card, ALI_GLOBAL_CONTROL));
	spdif |= ALI_SPDIF_IN_CH_ENABLE;
	outl(spdif, TRID_REG(card, ALI_GLOBAL_CONTROL));

	spdif = inb(TRID_REG(card, ALI_SPDIF_CTRL));
	spdif |= ALI_SPDIF_IN_CH_STATUS;
	outb(spdif, TRID_REG(card, ALI_SPDIF_CTRL));
/*
	spdif = inb(TRID_REG(card, ALI_SPDIF_CTRL));
	spdif |= ALI_SPDIF_IN_FUNC_ENABLE;
	outb(spdif, TRID_REG(card, ALI_SPDIF_CTRL));
*/
}

static void
ali_delay(struct trident_card *card, int interval)
{
	unsigned long begintimer, currenttimer;

	begintimer = inl(TRID_REG(card, ALI_STIMER));
	currenttimer = inl(TRID_REG(card, ALI_STIMER));

	while (currenttimer < begintimer + interval)
		currenttimer = inl(TRID_REG(card, ALI_STIMER));
}

static void
ali_detect_spdif_rate(struct trident_card *card)
{
	u16 wval = 0;
	u16 count = 0;
	u8 bval = 0, R1 = 0, R2 = 0;

	bval = inb(TRID_REG(card, ALI_SPDIF_CTRL));
	bval |= 0x02;
	outb(bval, TRID_REG(card, ALI_SPDIF_CTRL));

	bval = inb(TRID_REG(card, ALI_SPDIF_CTRL + 1));
	bval |= 0x1F;
	outb(bval, TRID_REG(card, ALI_SPDIF_CTRL + 1));

	while (((R1 < 0x0B) || (R1 > 0x0E)) && (R1 != 0x12) && 
	       count <= 50000) {
		count++;

		ali_delay(card, 6);

		bval = inb(TRID_REG(card, ALI_SPDIF_CTRL + 1));
		R1 = bval & 0x1F;
	}

	if (count > 50000) {
		printk(KERN_WARNING "trident: Error in "
		       "ali_detect_spdif_rate!\n");
		return;
	}

	count = 0;

	while (count <= 50000) {
		count++;

		ali_delay(card, 6);

		bval = inb(TRID_REG(card, ALI_SPDIF_CTRL + 1));
		R2 = bval & 0x1F;

		if (R2 != R1)
			R1 = R2;
		else
			break;
	}

	if (count > 50000) {
		printk(KERN_WARNING "trident: Error in "
		       "ali_detect_spdif_rate!\n");
		return;
	}

	switch (R2) {
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
		wval = inw(TRID_REG(card, ALI_SPDIF_CTRL + 2));
		wval &= 0xE0F0;
		wval |= (u16) 0x09 << 8 | (u16) 0x05;
		outw(wval, TRID_REG(card, ALI_SPDIF_CTRL + 2));

		bval = inb(TRID_REG(card, ALI_SPDIF_CS + 3)) & 0xF0;
		outb(bval | 0x02, TRID_REG(card, ALI_SPDIF_CS + 3));
		break;

	case 0x12:
		wval = inw(TRID_REG(card, ALI_SPDIF_CTRL + 2));
		wval &= 0xE0F0;
		wval |= (u16) 0x0E << 8 | (u16) 0x08;
		outw(wval, TRID_REG(card, ALI_SPDIF_CTRL + 2));

		bval = inb(TRID_REG(card, ALI_SPDIF_CS + 3)) & 0xF0;
		outb(bval | 0x03, TRID_REG(card, ALI_SPDIF_CS + 3));
		break;

	default:
		break;
	}

}

static unsigned int
ali_get_spdif_in_rate(struct trident_card *card)
{
	u32 dwRate = 0;
	u8 bval = 0;

	ali_detect_spdif_rate(card);

	bval = inb(TRID_REG(card, ALI_SPDIF_CTRL));
	bval &= 0x7F;
	bval |= 0x40;
	outb(bval, TRID_REG(card, ALI_SPDIF_CTRL));

	bval = inb(TRID_REG(card, ALI_SPDIF_CS + 3));
	bval &= 0x0F;

	switch (bval) {
	case 0:
		dwRate = 44100;
		break;
	case 1:
		dwRate = 48000;
		break;
	case 2:
		dwRate = 32000;
		break;
	default:
		// Error occurs
		break;
	}

	return dwRate;

}

static int
ali_close_multi_channels(void)
{
	char temp = 0;
	struct pci_dev *pci_dev = NULL;

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533,
				 pci_dev);
	if (pci_dev == NULL)
		return -1;

	pci_read_config_byte(pci_dev, 0x59, &temp);
	temp &= ~0x80;
	pci_write_config_byte(pci_dev, 0x59, temp);

	pci_dev_put(pci_dev);

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M7101,
				 NULL);
	if (pci_dev == NULL)
		return -1;

	pci_read_config_byte(pci_dev, 0xB8, &temp);
	temp &= ~0x20;
	pci_write_config_byte(pci_dev, 0xB8, temp);

	pci_dev_put(pci_dev);

	return 0;
}

static int
ali_setup_multi_channels(struct trident_card *card, int chan_nums)
{
	unsigned long dwValue;
	char temp = 0;
	struct pci_dev *pci_dev = NULL;

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533,
				 pci_dev);
	if (pci_dev == NULL)
		return -1;
	pci_read_config_byte(pci_dev, 0x59, &temp);
	temp |= 0x80;
	pci_write_config_byte(pci_dev, 0x59, temp);

	pci_dev_put(pci_dev);

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M7101,
				 NULL);
	if (pci_dev == NULL)
		return -1;
	pci_read_config_byte(pci_dev, (int) 0xB8, &temp);
	temp |= 0x20;
	pci_write_config_byte(pci_dev, (int) 0xB8, (u8) temp);

	pci_dev_put(pci_dev);

	if (chan_nums == 6) {
		dwValue = inl(TRID_REG(card, ALI_SCTRL)) | 0x000f0000;
		outl(dwValue, TRID_REG(card, ALI_SCTRL));
		mdelay(4);
		dwValue = inl(TRID_REG(card, ALI_SCTRL));
		if (dwValue & 0x2000000) {
			ali_ac97_write(card->ac97_codec[0], 0x02, 8080);
			ali_ac97_write(card->ac97_codec[0], 0x36, 0);
			ali_ac97_write(card->ac97_codec[0], 0x38, 0);
			/*
			 *      On a board with a single codec you won't get the
			 *      surround. On other boards configure it.
			 */
			if (card->ac97_codec[1] != NULL) {
				ali_ac97_write(card->ac97_codec[1], 0x36, 0);
				ali_ac97_write(card->ac97_codec[1], 0x38, 0);
				ali_ac97_write(card->ac97_codec[1], 0x02, 0x0606);
				ali_ac97_write(card->ac97_codec[1], 0x18, 0x0303);
				ali_ac97_write(card->ac97_codec[1], 0x74, 0x3);
			}
			return 1;
		}
	}
	return -EINVAL;
}

static void
ali_free_pcm_channel(struct trident_card *card, unsigned int channel)
{
	int bank;

	if (channel > 31)
		return;

	bank = channel >> 5;
	channel = channel & 0x1f;

	card->banks[bank].bitmap &= ~(1 << (channel));
}

static int
ali_allocate_other_states_resources(struct trident_state *state, int chan_nums)
{
	struct trident_card *card = state->card;
	struct trident_state *s;
	int i, state_count = 0;
	struct trident_pcm_bank *bank;
	struct trident_channel *channel;
	unsigned long num;

	bank = &card->banks[BANK_A];

	if (chan_nums != 6)
		return 0;

	for (i = 0; (i < ALI_CHANNELS) && (state_count != 4); i++) {
		if (card->states[i])
			continue;

		num = ali_multi_channels_5_1[state_count];
		if (!(bank->bitmap & (1 << num))) {
			bank->bitmap |= 1 << num;
			channel = &bank->channels[num];
			channel->num = num;
		} else {
			state_count--;
			for (; state_count >= 0; state_count--) {
				kfree(state->other_states[state_count]);
				num = ali_multi_channels_5_1[state_count];
					ali_free_pcm_channel(card, num);
			}
			return -EBUSY;
		}
		s = card->states[i] = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!s) {
			num = ali_multi_channels_5_1[state_count];
			ali_free_pcm_channel(card, num);
			state_count--;
			for (; state_count >= 0; state_count--) {
				num = ali_multi_channels_5_1[state_count];
				ali_free_pcm_channel(card, num);
				kfree(state->other_states[state_count]);
			}
			return -ENOMEM;
		}

		s->dmabuf.channel = channel;
		s->dmabuf.ossfragshift = s->dmabuf.ossmaxfrags =
			s->dmabuf.subdivision = 0;
		init_waitqueue_head(&s->dmabuf.wait);
		s->magic = card->magic;
		s->card = card;
		s->virt = i;
		ali_enable_special_channel(s);
		state->other_states[state_count++] = s;
	}

	if (state_count != 4) {
		state_count--;
		for (; state_count >= 0; state_count--) {
			kfree(state->other_states[state_count]);
			num = ali_multi_channels_5_1[state_count];
			ali_free_pcm_channel(card, num);
		}
		return -EBUSY;
	}
	return 0;
}

#ifdef CONFIG_PM
/* save registers for ALi Power Management */
static struct ali_saved_registers {
	unsigned long global_regs[ALI_GLOBAL_REGS];
	unsigned long channel_regs[ALI_CHANNELS][ALI_CHANNEL_REGS];
	unsigned mixer_regs[ALI_MIXER_REGS];
} ali_registers;

static void
ali_save_regs(struct trident_card *card)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&card->lock, flags);

	ali_registers.global_regs[0x2c] = inl(TRID_REG(card, T4D_MISCINT));
	//ali_registers.global_regs[0x20] = inl(TRID_REG(card,T4D_START_A));    
	ali_registers.global_regs[0x21] = inl(TRID_REG(card, T4D_STOP_A));

	//disable all IRQ bits
	outl(ALI_DISABLE_ALL_IRQ, TRID_REG(card, T4D_MISCINT));

	for (i = 1; i < ALI_MIXER_REGS; i++)
		ali_registers.mixer_regs[i] = ali_ac97_read(card->ac97_codec[0], 
							    i * 2);

	for (i = 0; i < ALI_GLOBAL_REGS; i++) {
		if ((i * 4 == T4D_MISCINT) || (i * 4 == T4D_STOP_A))
			continue;
		ali_registers.global_regs[i] = inl(TRID_REG(card, i * 4));
	}

	for (i = 0; i < ALI_CHANNELS; i++) {
		outb(i, TRID_REG(card, T4D_LFO_GC_CIR));
		for (j = 0; j < ALI_CHANNEL_REGS; j++)
			ali_registers.channel_regs[i][j] = inl(TRID_REG(card, 
									j * 4 + 0xe0));
	}

	//Stop all HW channel
	outl(ALI_STOP_ALL_CHANNELS, TRID_REG(card, T4D_STOP_A));

	spin_unlock_irqrestore(&card->lock, flags);
}

static void
ali_restore_regs(struct trident_card *card)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&card->lock, flags);

	for (i = 1; i < ALI_MIXER_REGS; i++)
		ali_ac97_write(card->ac97_codec[0], i * 2, 
			       ali_registers.mixer_regs[i]);

	for (i = 0; i < ALI_CHANNELS; i++) {
		outb(i, TRID_REG(card, T4D_LFO_GC_CIR));
		for (j = 0; j < ALI_CHANNEL_REGS; j++)
			outl(ali_registers.channel_regs[i][j], 
			     TRID_REG(card, j * 4 + 0xe0));
	}

	for (i = 0; i < ALI_GLOBAL_REGS; i++) {
		if ((i * 4 == T4D_MISCINT) || (i * 4 == T4D_STOP_A) || 
		    (i * 4 == T4D_START_A))
			continue;
		outl(ali_registers.global_regs[i], TRID_REG(card, i * 4));
	}

	//start HW channel
	outl(ali_registers.global_regs[0x20], TRID_REG(card, T4D_START_A));
	//restore IRQ enable bits
	outl(ali_registers.global_regs[0x2c], TRID_REG(card, T4D_MISCINT));

	spin_unlock_irqrestore(&card->lock, flags);
}

static int
trident_suspend(struct pci_dev *dev, pm_message_t unused)
{
	struct trident_card *card = pci_get_drvdata(dev);

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		ali_save_regs(card);
	}
	return 0;
}

static int
trident_resume(struct pci_dev *dev)
{
	struct trident_card *card = pci_get_drvdata(dev);

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		ali_restore_regs(card);
	}
	return 0;
}
#endif

static struct trident_channel *
ali_alloc_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	bank = &card->banks[BANK_A];

	if (inl(TRID_REG(card, ALI_GLOBAL_CONTROL)) & 
	    (ALI_SPDIF_OUT_CH_ENABLE)) {
		idx = ALI_SPDIF_OUT_CHANNEL;
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx;
			return channel;
		}
	}

	for (idx = ALI_PCM_OUT_CHANNEL_FIRST; idx <= ALI_PCM_OUT_CHANNEL_LAST; 
	     idx++) {
		if (!(bank->bitmap & (1 << idx))) {
			struct trident_channel *channel = &bank->channels[idx];
			bank->bitmap |= 1 << idx;
			channel->num = idx;
			return channel;
		}
	}

	/* no more free channels avaliable */
#if 0 
	printk(KERN_ERR "ali: no more channels available on Bank A.\n");
#endif /* 0 */ 
	return NULL;
}

static struct trident_channel *
ali_alloc_rec_pcm_channel(struct trident_card *card)
{
	struct trident_pcm_bank *bank;
	int idx;

	if (inl(TRID_REG(card, ALI_GLOBAL_CONTROL)) & ALI_SPDIF_IN_SUPPORT)
		idx = ALI_SPDIF_IN_CHANNEL;
	else
		idx = ALI_PCM_IN_CHANNEL;

	bank = &card->banks[BANK_A];

	if (!(bank->bitmap & (1 << idx))) {
		struct trident_channel *channel = &bank->channels[idx];
		bank->bitmap |= 1 << idx;
		channel->num = idx;
		return channel;
	}

	/* no free recordable channels avaliable */
#if 0 
	printk(KERN_ERR "ali: no recordable channels available on Bank A.\n");
#endif /* 0 */ 
	return NULL;
}

static void
ali_set_spdif_out_rate(struct trident_card *card, unsigned int rate)
{
	unsigned char ch_st_sel;
	unsigned short status_rate;

	switch (rate) {
	case 44100:
		status_rate = 0;
		break;
	case 32000:
		status_rate = 0x300;
		break;
	case 48000:
	default:
		status_rate = 0x200;
		break;
	}

	/* select spdif_out */ 
	ch_st_sel = inb(TRID_REG(card, ALI_SPDIF_CTRL)) & ALI_SPDIF_OUT_CH_STATUS;

	ch_st_sel |= 0x80;	/* select right */ 
	outb(ch_st_sel, TRID_REG(card, ALI_SPDIF_CTRL));
	outb(status_rate | 0x20, TRID_REG(card, ALI_SPDIF_CS + 2));

	ch_st_sel &= (~0x80);	/* select left */ 
	outb(ch_st_sel, TRID_REG(card, ALI_SPDIF_CTRL));
	outw(status_rate | 0x10, TRID_REG(card, ALI_SPDIF_CS + 2));
}

static void
ali_address_interrupt(struct trident_card *card)
{
	int i, channel;
	struct trident_state *state;
	u32 mask, channel_mask;

	mask = trident_get_interrupt_mask(card, 0);
	for (i = 0; i < NR_HW_CH; i++) {
		if ((state = card->states[i]) == NULL)
			continue;
		channel = state->dmabuf.channel->num;
		if ((channel_mask = 1 << channel) & mask) {
			mask &= ~channel_mask;
			trident_ack_channel_interrupt(card, channel);
			udelay(100);
			state->dmabuf.update_flag |= ALI_ADDRESS_INT_UPDATE;
			trident_update_ptr(state);
		}
	}
	if (mask) {
		for (i = 0; i < NR_HW_CH; i++) {
			if (mask & (1 << i)) {
				printk("ali: spurious channel irq %d.\n", i);
				trident_ack_channel_interrupt(card, i);
				trident_stop_voice(card, i);
				trident_disable_voice_irq(card, i);
			}
		}
	}
}

/* Updating the values of counters of other_states' DMAs without lock 
protection is no harm because all DMAs of multi-channels and interrupt
depend on a master state's DMA, and changing the counters of the master
state DMA is protected by a spinlock.
*/
static int
ali_write_5_1(struct trident_state *state, const char __user *buf, 
	      int cnt_for_multi_channel, unsigned int *copy_count, 
	      unsigned int *state_cnt)
{

	struct dmabuf *dmabuf = &state->dmabuf;
	struct dmabuf *dmabuf_temp;
	const char __user *buffer = buf;
	unsigned swptr, other_dma_nums, sample_s;
	unsigned int i, loop;

	other_dma_nums = 4;
	sample_s = sample_size[dmabuf->fmt] >> 1;
	swptr = dmabuf->swptr;

	if ((i = state->multi_channels_adjust_count) > 0) {
		if (i == 1) {
			if (copy_from_user(dmabuf->rawbuf + swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);
			i--;
			(*state_cnt) += sample_s;
			state->multi_channels_adjust_count++;
		} else
			i = i - (state->chans_num - other_dma_nums);
		for (; (i < other_dma_nums) && (cnt_for_multi_channel > 0); i++) {
			dmabuf_temp = &state->other_states[i]->dmabuf;
			if (copy_from_user(dmabuf_temp->rawbuf + dmabuf_temp->swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(dmabuf_temp->swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);
		}
		if (cnt_for_multi_channel == 0)
			state->multi_channels_adjust_count += i;
	}
	if (cnt_for_multi_channel > 0) {
		loop = cnt_for_multi_channel / (state->chans_num * sample_s);
		for (i = 0; i < loop; i++) {
			if (copy_from_user(dmabuf->rawbuf + swptr, buffer, 
					   sample_s * 2))
				return -EFAULT;
			seek_offset(swptr, buffer, cnt_for_multi_channel, 
				    sample_s * 2, *copy_count);
			(*state_cnt) += (sample_s * 2);

			dmabuf_temp = &state->other_states[0]->dmabuf;
			if (copy_from_user(dmabuf_temp->rawbuf + dmabuf_temp->swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(dmabuf_temp->swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);

			dmabuf_temp = &state->other_states[1]->dmabuf;
			if (copy_from_user(dmabuf_temp->rawbuf + dmabuf_temp->swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(dmabuf_temp->swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);

			dmabuf_temp = &state->other_states[2]->dmabuf;
			if (copy_from_user(dmabuf_temp->rawbuf + dmabuf_temp->swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(dmabuf_temp->swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);

			dmabuf_temp = &state->other_states[3]->dmabuf;
			if (copy_from_user(dmabuf_temp->rawbuf + dmabuf_temp->swptr, 
					   buffer, sample_s))
				return -EFAULT;
			seek_offset(dmabuf_temp->swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);
		}

		if (cnt_for_multi_channel > 0) {
			state->multi_channels_adjust_count = cnt_for_multi_channel / sample_s;

			if (copy_from_user(dmabuf->rawbuf + swptr, buffer, sample_s))
				return -EFAULT;
			seek_offset(swptr, buffer, cnt_for_multi_channel, 
				    sample_s, *copy_count);
			(*state_cnt) += sample_s;

			if (cnt_for_multi_channel > 0) {
				if (copy_from_user(dmabuf->rawbuf + swptr, 
						   buffer, sample_s))
					return -EFAULT;
				seek_offset(swptr, buffer, cnt_for_multi_channel, 
					    sample_s, *copy_count);
				(*state_cnt) += sample_s;

				if (cnt_for_multi_channel > 0) {
					int diff = state->chans_num - other_dma_nums;
					loop = state->multi_channels_adjust_count - diff;
					for (i = 0; i < loop; i++) {
						dmabuf_temp = &state->other_states[i]->dmabuf;
						if (copy_from_user(dmabuf_temp->rawbuf + 
								   dmabuf_temp->swptr, 
								   buffer, sample_s))
							return -EFAULT;
						seek_offset(dmabuf_temp->swptr, buffer, 
							    cnt_for_multi_channel, 
							    sample_s, *copy_count);
					}
				}
			}
		} else
			state->multi_channels_adjust_count = 0;
	}
	for (i = 0; i < other_dma_nums; i++) {
		dmabuf_temp = &state->other_states[i]->dmabuf;
		dmabuf_temp->swptr = dmabuf_temp->swptr % dmabuf_temp->dmasize;
	}
	return *state_cnt;
}

static void
ali_free_other_states_resources(struct trident_state *state)
{
	int i;
	struct trident_card *card = state->card;
	struct trident_state *s;
	unsigned other_states_count;

	other_states_count = state->chans_num - 2; /* except PCM L/R channels */
	for (i = 0; i < other_states_count; i++) {
		s = state->other_states[i];
		dealloc_dmabuf(&s->dmabuf, card->pci_dev);
		ali_disable_special_channel(s->card, s->dmabuf.channel->num);
		state->card->free_pcm_channel(s->card, s->dmabuf.channel->num);
		card->states[s->virt] = NULL;
		kfree(s);
	}
}

static struct proc_dir_entry *res;

static int
ali_write_proc(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	struct trident_card *card = (struct trident_card *) data;
	unsigned long flags;
	char c;

	if (count < 0)
		return -EINVAL;
	if (count == 0)
		return 0;
	if (get_user(c, buffer))
		return -EFAULT;

	spin_lock_irqsave(&card->lock, flags);
	switch (c) {
	case '0':
		ali_setup_spdif_out(card, ALI_PCM_TO_SPDIF_OUT);
		ali_disable_special_channel(card, ALI_SPDIF_OUT_CHANNEL);
		break;
	case '1':
		ali_setup_spdif_out(card, ALI_SPDIF_OUT_TO_SPDIF_OUT | 
				    ALI_SPDIF_OUT_PCM);
		break;
	case '2':
		ali_setup_spdif_out(card, ALI_SPDIF_OUT_TO_SPDIF_OUT | 
				    ALI_SPDIF_OUT_NON_PCM);
		break;
	case '3':
		ali_disable_spdif_in(card);	//default
		break;
	case '4':
		ali_setup_spdif_in(card);
		break;
	}
	spin_unlock_irqrestore(&card->lock, flags);

	return count;
}

/* OSS /dev/mixer file operation methods */
static int
trident_open_mixdev(struct inode *inode, struct file *file)
{
	int i = 0;
	int minor = iminor(inode);
	struct trident_card *card = devs;

	for (card = devs; card != NULL; card = card->next)
		for (i = 0; i < NR_AC97; i++)
			if (card->ac97_codec[i] != NULL && 
			    card->ac97_codec[i]->dev_mixer == minor)
				goto match;

	if (!card) {
		return -ENODEV;
	}
      match:
	file->private_data = card->ac97_codec[i];

	return nonseekable_open(inode, file);
}

static int
trident_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, 
		     unsigned long arg)
{
	struct ac97_codec *codec = (struct ac97_codec *) file->private_data;

	return codec->mixer_ioctl(codec, cmd, arg);
}

static /*const */ struct file_operations trident_mixer_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.ioctl = trident_ioctl_mixdev,
	.open = trident_open_mixdev,
};

static int
ali_reset_5451(struct trident_card *card)
{
	struct pci_dev *pci_dev = NULL;
	unsigned int dwVal;
	unsigned short wCount, wReg;

	pci_dev = pci_get_device(PCI_VENDOR_ID_AL, PCI_DEVICE_ID_AL_M1533,
				 pci_dev);
	if (pci_dev == NULL)
		return -1;

	pci_read_config_dword(pci_dev, 0x7c, &dwVal);
	pci_write_config_dword(pci_dev, 0x7c, dwVal | 0x08000000);
	udelay(5000);
	pci_read_config_dword(pci_dev, 0x7c, &dwVal);
	pci_write_config_dword(pci_dev, 0x7c, dwVal & 0xf7ffffff);
	udelay(5000);
	pci_dev_put(pci_dev);

	pci_dev = card->pci_dev;
	if (pci_dev == NULL)
		return -1;

	pci_read_config_dword(pci_dev, 0x44, &dwVal);
	pci_write_config_dword(pci_dev, 0x44, dwVal | 0x000c0000);
	udelay(500);
	pci_read_config_dword(pci_dev, 0x44, &dwVal);
	pci_write_config_dword(pci_dev, 0x44, dwVal & 0xfffbffff);
	udelay(5000);

	/* TODO: recognize if we have a PM capable codec and only do this */
	/* if the codec is PM capable */
	wCount = 2000;
	while (wCount--) {
		wReg = ali_ac97_get(card, 0, AC97_POWER_CONTROL);
		if ((wReg & 0x000f) == 0x000f)
			return 0;
		udelay(5000);
	}
	/* This is non fatal if you have a non PM capable codec.. */
	return 0;
}

/* AC97 codec initialisation. */
static int __devinit
trident_ac97_init(struct trident_card *card)
{
	int num_ac97 = 0;
	unsigned long ready_2nd = 0;
	struct ac97_codec *codec;
	int i = 0;

	/* initialize controller side of AC link, and find out if secondary codes
	   really exist */
	switch (card->pci_id) {
	case PCI_DEVICE_ID_ALI_5451:
		if (ali_reset_5451(card)) {
			printk(KERN_ERR "trident_ac97_init: error "
			       "resetting 5451.\n");
			return -1;
		}
		outl(0x80000001, TRID_REG(card, ALI_GLOBAL_CONTROL));
		outl(0x00000000, TRID_REG(card, T4D_AINTEN_A));
		outl(0xffffffff, TRID_REG(card, T4D_AINT_A));
		outl(0x00000000, TRID_REG(card, T4D_MUSICVOL_WAVEVOL));
		outb(0x10, TRID_REG(card, ALI_MPUR2));
		ready_2nd = inl(TRID_REG(card, ALI_SCTRL));
		ready_2nd &= 0x3fff;
		outl(ready_2nd | PCMOUT | 0x8000, TRID_REG(card, ALI_SCTRL));
		ready_2nd = inl(TRID_REG(card, ALI_SCTRL));
		ready_2nd &= SI_AC97_SECONDARY_READY;
		if (card->revision < ALI_5451_V02)
			ready_2nd = 0;
		break;
	case PCI_DEVICE_ID_SI_7018:
		/* disable AC97 GPIO interrupt */
		outl(0x00, TRID_REG(card, SI_AC97_GPIO));
		/* when power up the AC link is in cold reset mode so stop it */
		outl(PCMOUT | SURROUT | CENTEROUT | LFEOUT | SECONDARY_ID, 
		     TRID_REG(card, SI_SERIAL_INTF_CTRL));
		/* it take a long time to recover from a cold reset */ 
		/* (especially when you have more than one codec) */
		udelay(2000);
		ready_2nd = inl(TRID_REG(card, SI_SERIAL_INTF_CTRL));
		ready_2nd &= SI_AC97_SECONDARY_READY;
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_DX:
		/* playback on */
		outl(DX_AC97_PLAYBACK, TRID_REG(card, DX_ACR2_AC97_COM_STAT));
		break;
	case PCI_DEVICE_ID_TRIDENT_4DWAVE_NX:
		/* enable AC97 Output Slot 3,4 (PCM Left/Right Playback) */
		outl(NX_AC97_PCM_OUTPUT, TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd = inl(TRID_REG(card, NX_ACR0_AC97_COM_STAT));
		ready_2nd &= NX_AC97_SECONDARY_READY;
		break;
	case PCI_DEVICE_ID_INTERG_5050:
		/* disable AC97 GPIO interrupt */
		outl(0x00, TRID_REG(card, SI_AC97_GPIO));
		/* when power up, the AC link is in cold reset mode, so stop it */
		outl(PCMOUT | SURROUT | CENTEROUT | LFEOUT, 
		     TRID_REG(card, SI_SERIAL_INTF_CTRL));
		/* it take a long time to recover from a cold reset (especially */ 
		/* when you have more than one codec) */
		udelay(2000);
		ready_2nd = inl(TRID_REG(card, SI_SERIAL_INTF_CTRL));
		ready_2nd &= SI_AC97_SECONDARY_READY;
		break;
	}

	for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
		if ((codec = ac97_alloc_codec()) == NULL)
			return -ENOMEM;

		/* initialize some basic codec information, other fields */ 
		/* will be filled in ac97_probe_codec */
		codec->private_data = card;
		codec->id = num_ac97;

		if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
			codec->codec_read = ali_ac97_read;
			codec->codec_write = ali_ac97_write;
		} else {
			codec->codec_read = trident_ac97_get;
			codec->codec_write = trident_ac97_set;
		}

		if (ac97_probe_codec(codec) == 0)
			break;

		codec->dev_mixer = register_sound_mixer(&trident_mixer_fops, -1);
		if (codec->dev_mixer < 0) {
			printk(KERN_ERR "trident: couldn't register mixer!\n");
			ac97_release_codec(codec);
			break;
		}

		card->ac97_codec[num_ac97] = codec;

		/* if there is no secondary codec at all, don't probe any more */
		if (!ready_2nd)
			break;
	}

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		for (num_ac97 = 0; num_ac97 < NR_AC97; num_ac97++) {
			if (card->ac97_codec[num_ac97] == NULL)
				break;
			for (i = 0; i < 64; i++) {
				u16 reg = ali_ac97_get(card, num_ac97, i * 2);
				card->mixer_regs[i][num_ac97] = reg;
			}
		}
	}
	return num_ac97 + 1;
}

#ifdef SUPPORT_JOYSTICK
/* Gameport functions for the cards ADC gameport */

static unsigned char trident_game_read(struct gameport *gameport)
{
	struct trident_card *card = gameport->port_data;

	return inb(TRID_REG(card, T4D_GAME_LEG));
}

static void trident_game_trigger(struct gameport *gameport)
{
	struct trident_card *card = gameport->port_data;

	outb(0xff, TRID_REG(card, T4D_GAME_LEG));
}

static int trident_game_cooked_read(struct gameport *gameport,
				    int *axes, int *buttons)
{
	struct trident_card *card = gameport->port_data;
	int i;

	*buttons = (~inb(TRID_REG(card, T4D_GAME_LEG)) >> 4) & 0xf;

	for (i = 0; i < 4; i++) {
		axes[i] = inw(TRID_REG(card, T4D_GAME_AXD) + i * sizeof (u16));
		if (axes[i] == 0xffff)
			axes[i] = -1;
	}

	return 0;
}

static int trident_game_open(struct gameport *gameport, int mode)
{
	struct trident_card *card = gameport->port_data;

	switch (mode) {
	case GAMEPORT_MODE_COOKED:
		outb(0x80, TRID_REG(card, T4D_GAME_CR));
		msleep(20);
		return 0;
	case GAMEPORT_MODE_RAW:
		outb(0x00, TRID_REG(card, T4D_GAME_CR));
		return 0;
	default:
		return -1;
	}

	return 0;
}

static int __devinit trident_register_gameport(struct trident_card *card)
{
	struct gameport *gp;

	card->gameport = gp = gameport_allocate_port();
	if (!gp) {
		printk(KERN_ERR "trident: can not allocate memory for gameport\n");
		return -ENOMEM;
	}

	gameport_set_name(gp, "Trident 4DWave");
	gameport_set_phys(gp, "pci%s/gameport0", pci_name(card->pci_dev));
	gp->read = trident_game_read;
	gp->trigger = trident_game_trigger;
	gp->cooked_read = trident_game_cooked_read;
	gp->open = trident_game_open;
	gp->fuzz = 64;
	gp->port_data = card;

	gameport_register_port(gp);

	return 0;
}

static inline void trident_unregister_gameport(struct trident_card *card)
{
	if (card->gameport)
		gameport_unregister_port(card->gameport);
}

#else
static inline int trident_register_gameport(struct trident_card *card) { return -ENOSYS; }
static inline void trident_unregister_gameport(struct trident_card *card) { }
#endif /* SUPPORT_JOYSTICK */

/* install the driver, we do not allocate hardware channel nor DMA buffer */ 
/* now, they are defered until "ACCESS" time (in prog_dmabuf called by */ 
/* open/read/write/ioctl/mmap) */
static int __devinit
trident_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	unsigned long iobase;
	struct trident_card *card;
	u8 bits;
	u8 revision;
	int i = 0;
	u16 temp;
	struct pci_dev *pci_dev_m1533 = NULL;
	int rc = -ENODEV;
	u64 dma_mask;

	if (pci_enable_device(pci_dev))
		goto out;

	if (pci_dev->device == PCI_DEVICE_ID_ALI_5451)
		dma_mask = ALI_DMA_MASK;
	else
		dma_mask = TRIDENT_DMA_MASK;
	if (pci_set_dma_mask(pci_dev, dma_mask)) {
		printk(KERN_ERR "trident: architecture does not support" 
		       " %s PCI busmaster DMA\n", 
		       pci_dev->device == PCI_DEVICE_ID_ALI_5451 ? 
		       "32-bit" : "30-bit");
		goto out;
	}
	pci_read_config_byte(pci_dev, PCI_CLASS_REVISION, &revision);

	if (pci_id->device == PCI_DEVICE_ID_INTERG_5050)
		iobase = pci_resource_start(pci_dev, 1);
	else
		iobase = pci_resource_start(pci_dev, 0);

	if (!request_region(iobase, 256, card_names[pci_id->driver_data])) {
		printk(KERN_ERR "trident: can't allocate I/O space at "
		       "0x%4.4lx\n", iobase);
		goto out;
	}

	rc = -ENOMEM;
	if ((card = kzalloc(sizeof(*card), GFP_KERNEL)) == NULL) {
		printk(KERN_ERR "trident: out of memory\n");
		goto out_release_region;
	}

	init_timer(&card->timer);
	card->iobase = iobase;
	card->pci_dev = pci_dev_get(pci_dev);
	card->pci_id = pci_id->device;
	card->revision = revision;
	card->irq = pci_dev->irq;
	card->next = devs;
	card->magic = TRIDENT_CARD_MAGIC;
	card->banks[BANK_A].addresses = &bank_a_addrs;
	card->banks[BANK_A].bitmap = 0UL;
	card->banks[BANK_B].addresses = &bank_b_addrs;
	card->banks[BANK_B].bitmap = 0UL;

	mutex_init(&card->open_mutex);
	spin_lock_init(&card->lock);
	init_timer(&card->timer);

	devs = card;

	pci_set_master(pci_dev);

	printk(KERN_INFO "trident: %s found at IO 0x%04lx, IRQ %d\n", 
	       card_names[pci_id->driver_data], card->iobase, card->irq);

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		/* ALi channel Management */
		card->alloc_pcm_channel = ali_alloc_pcm_channel;
		card->alloc_rec_pcm_channel = ali_alloc_rec_pcm_channel;
		card->free_pcm_channel = ali_free_pcm_channel;

		card->address_interrupt = ali_address_interrupt;

		/* Added by Matt Wu 01-05-2001 for spdif in */
		card->multi_channel_use_count = 0;
		card->rec_channel_use_count = 0;

		/* ALi SPDIF OUT function */
		if (card->revision == ALI_5451_V02) {
			ali_setup_spdif_out(card, ALI_PCM_TO_SPDIF_OUT);
			res = create_proc_entry("ALi5451", 0, NULL);
			if (res) {
				res->write_proc = ali_write_proc;
				res->data = card;
			}
		}

		/* Add H/W Volume Control By Matt Wu Jul. 06, 2001 */
		card->hwvolctl = 0;
		pci_dev_m1533 = pci_find_device(PCI_VENDOR_ID_AL, 
						PCI_DEVICE_ID_AL_M1533, 
						pci_dev_m1533);
		rc = -ENODEV;
		if (pci_dev_m1533 == NULL)
			goto out_proc_fs;
		pci_read_config_byte(pci_dev_m1533, 0x63, &bits);
		if (bits & (1 << 5))
			card->hwvolctl = 1;
		if (card->hwvolctl) {
			/* Clear m1533 pci cfg 78h bit 30 to zero, which makes
			   GPIO11/12/13 work as ACGP_UP/DOWN/MUTE. */
			pci_read_config_byte(pci_dev_m1533, 0x7b, &bits);
			bits &= 0xbf;	/*clear bit 6 */
			pci_write_config_byte(pci_dev_m1533, 0x7b, bits);
		}
	} else if (card->pci_id == PCI_DEVICE_ID_INTERG_5050) {
		card->alloc_pcm_channel = cyber_alloc_pcm_channel;
		card->alloc_rec_pcm_channel = cyber_alloc_pcm_channel;
		card->free_pcm_channel = cyber_free_pcm_channel;
		card->address_interrupt = cyber_address_interrupt;
		cyber_init_ritual(card);
	} else {
		card->alloc_pcm_channel = trident_alloc_pcm_channel;
		card->alloc_rec_pcm_channel = trident_alloc_pcm_channel;
		card->free_pcm_channel = trident_free_pcm_channel;
		card->address_interrupt = trident_address_interrupt;
	}

	/* claim our irq */
	rc = -ENODEV;
	if (request_irq(card->irq, &trident_interrupt, IRQF_SHARED,
			card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "trident: unable to allocate irq %d\n", 
		       card->irq);
		goto out_proc_fs;
	}
	/* register /dev/dsp */
	if ((card->dev_audio = register_sound_dsp(&trident_audio_fops, -1)) < 0) {
		printk(KERN_ERR "trident: couldn't register DSP device!\n");
		goto out_free_irq;
	}
	card->mixer_regs_ready = 0;
	/* initialize AC97 codec and register /dev/mixer */
	if (trident_ac97_init(card) <= 0) {
		/* unregister audio devices */
		for (i = 0; i < NR_AC97; i++) {
			if (card->ac97_codec[i] != NULL) {
				struct ac97_codec* codec = card->ac97_codec[i];
				unregister_sound_mixer(codec->dev_mixer);
				ac97_release_codec(codec);
			}
		}
		goto out_unregister_sound_dsp;
	}
	card->mixer_regs_ready = 1;
	outl(0x00, TRID_REG(card, T4D_MUSICVOL_WAVEVOL));

	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		/* Add H/W Volume Control By Matt Wu Jul. 06, 2001 */
		if (card->hwvolctl) {
			/* Enable GPIO IRQ (MISCINT bit 18h) */
			temp = inw(TRID_REG(card, T4D_MISCINT + 2));
			temp |= 0x0004;
			outw(temp, TRID_REG(card, T4D_MISCINT + 2));

			/* Enable H/W Volume Control GLOVAL CONTROL bit 0 */
			temp = inw(TRID_REG(card, ALI_GLOBAL_CONTROL));
			temp |= 0x0001;
			outw(temp, TRID_REG(card, ALI_GLOBAL_CONTROL));

		}
		if (card->revision == ALI_5451_V02)
			ali_close_multi_channels();
		/* edited by HMSEO for GT sound */
#if defined(CONFIG_ALPHA_NAUTILUS) || defined(CONFIG_ALPHA_GENERIC)
		{
			u16 ac97_data;
			extern struct hwrpb_struct *hwrpb;

			if ((hwrpb->sys_type) == 201) {
				printk(KERN_INFO "trident: Running on Alpha system "
				       "type Nautilus\n");
				ac97_data = ali_ac97_get(card, 0, AC97_POWER_CONTROL);
				ali_ac97_set(card, 0, AC97_POWER_CONTROL, 
					     ac97_data | ALI_EAPD_POWER_DOWN);
			}
		}
#endif				/* CONFIG_ALPHA_NAUTILUS || CONFIG_ALPHA_GENERIC */
		/* edited by HMSEO for GT sound */
	}
	rc = 0;
	pci_set_drvdata(pci_dev, card);

	/* Enable Address Engine Interrupts */
	trident_enable_loop_interrupts(card);

	/* Register gameport */
	trident_register_gameport(card);

out:
	return rc;

out_unregister_sound_dsp:
	unregister_sound_dsp(card->dev_audio);
out_free_irq:
	free_irq(card->irq, card);
out_proc_fs:
	pci_dev_put(card->pci_dev);
	if (res) {
		remove_proc_entry("ALi5451", NULL);
		res = NULL;
	}
	kfree(card);
	devs = NULL;
out_release_region:
	release_region(iobase, 256);
	return rc; 
}

static void __devexit
trident_remove(struct pci_dev *pci_dev)
{
	int i;
	struct trident_card *card = pci_get_drvdata(pci_dev);

	/*
	 *      Kill running timers before unload. We can't have them
	 *      going off after rmmod!
	 */
	if (card->hwvolctl)
		del_timer_sync(&card->timer);

	/* ALi S/PDIF and Power Management */
	if (card->pci_id == PCI_DEVICE_ID_ALI_5451) {
		ali_setup_spdif_out(card, ALI_PCM_TO_SPDIF_OUT);
		ali_disable_special_channel(card, ALI_SPDIF_OUT_CHANNEL);
		ali_disable_spdif_in(card);
		remove_proc_entry("ALi5451", NULL);
	}

	/* Unregister gameport */
	trident_unregister_gameport(card);

	/* Kill interrupts, and SP/DIF */
	trident_disable_loop_interrupts(card);

	/* free hardware resources */
	free_irq(card->irq, card);
	release_region(card->iobase, 256);

	/* unregister audio devices */
	for (i = 0; i < NR_AC97; i++)
		if (card->ac97_codec[i] != NULL) {
			unregister_sound_mixer(card->ac97_codec[i]->dev_mixer);
			ac97_release_codec(card->ac97_codec[i]);
		}
	unregister_sound_dsp(card->dev_audio);

	pci_set_drvdata(pci_dev, NULL);
	pci_dev_put(card->pci_dev);
	kfree(card);
}

MODULE_AUTHOR("Alan Cox, Aaron Holtzman, Ollie Lho, Ching Ling Lee, Muli Ben-Yehuda");
MODULE_DESCRIPTION("Trident 4DWave/SiS 7018/ALi 5451 and Tvia/IGST CyberPro5050 PCI "
		   "Audio Driver");
MODULE_LICENSE("GPL");

#define TRIDENT_MODULE_NAME "trident"

static struct pci_driver trident_pci_driver = {
	.name = TRIDENT_MODULE_NAME,
	.id_table = trident_pci_tbl,
	.probe = trident_probe,
	.remove = __devexit_p(trident_remove),
#ifdef CONFIG_PM
	.suspend = trident_suspend,
	.resume = trident_resume
#endif
};

static int __init
trident_init_module(void)
{
	printk(KERN_INFO "Trident 4DWave/SiS 7018/ALi 5451,Tvia CyberPro " 
	       "5050 PCI Audio, version " DRIVER_VERSION ", " __TIME__ " " 
	       __DATE__ "\n");

	return pci_register_driver(&trident_pci_driver);
}

static void __exit
trident_cleanup_module(void)
{
	pci_unregister_driver(&trident_pci_driver);
}

module_init(trident_init_module);
module_exit(trident_cleanup_module);
