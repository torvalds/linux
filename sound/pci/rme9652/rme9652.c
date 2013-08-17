/*
 *   ALSA driver for RME Digi9652 audio interfaces 
 *
 *	Copyright (c) 1999 IEM - Winfried Ritsch
 *      Copyright (c) 1999-2001  Paul Davis
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/module.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/info.h>
#include <sound/asoundef.h>
#include <sound/initval.h>

#include <asm/current.h>
#include <asm/io.h>

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;	/* Index 0-MAX */
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;	/* ID for this card */
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;	/* Enable this card */
static bool precise_ptr[SNDRV_CARDS];			/* Enable precise pointer */

module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for RME Digi9652 (Hammerfall) soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for RME Digi9652 (Hammerfall) soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable/disable specific RME96{52,36} soundcards.");
module_param_array(precise_ptr, bool, NULL, 0444);
MODULE_PARM_DESC(precise_ptr, "Enable precise pointer (doesn't work reliably).");
MODULE_AUTHOR("Paul Davis <pbd@op.net>, Winfried Ritsch");
MODULE_DESCRIPTION("RME Digi9652/Digi9636");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{RME,Hammerfall},"
		"{RME,Hammerfall-Light}}");

/* The Hammerfall has two sets of 24 ADAT + 2 S/PDIF channels, one for
   capture, one for playback. Both the ADAT and S/PDIF channels appear
   to the host CPU in the same block of memory. There is no functional
   difference between them in terms of access.
   
   The Hammerfall Light is identical to the Hammerfall, except that it
   has 2 sets 18 channels (16 ADAT + 2 S/PDIF) for capture and playback.
*/

#define RME9652_NCHANNELS       26
#define RME9636_NCHANNELS       18

/* Preferred sync source choices - used by "sync_pref" control switch */

#define RME9652_SYNC_FROM_SPDIF 0
#define RME9652_SYNC_FROM_ADAT1 1
#define RME9652_SYNC_FROM_ADAT2 2
#define RME9652_SYNC_FROM_ADAT3 3

/* Possible sources of S/PDIF input */

#define RME9652_SPDIFIN_OPTICAL 0	/* optical (ADAT1) */
#define RME9652_SPDIFIN_COAXIAL 1	/* coaxial (RCA) */
#define RME9652_SPDIFIN_INTERN  2	/* internal (CDROM) */

/* ------------- Status-Register bits --------------------- */

#define RME9652_IRQ	   (1<<0)	/* IRQ is High if not reset by irq_clear */
#define RME9652_lock_2	   (1<<1)	/* ADAT 3-PLL: 1=locked, 0=unlocked */
#define RME9652_lock_1	   (1<<2)	/* ADAT 2-PLL: 1=locked, 0=unlocked */
#define RME9652_lock_0	   (1<<3)	/* ADAT 1-PLL: 1=locked, 0=unlocked */
#define RME9652_fs48	   (1<<4)	/* sample rate is 0=44.1/88.2,1=48/96 Khz */
#define RME9652_wsel_rd	   (1<<5)	/* if Word-Clock is used and valid then 1 */
                                        /* bits 6-15 encode h/w buffer pointer position */
#define RME9652_sync_2	   (1<<16)	/* if ADAT-IN 3 in sync to system clock */
#define RME9652_sync_1	   (1<<17)	/* if ADAT-IN 2 in sync to system clock */
#define RME9652_sync_0	   (1<<18)	/* if ADAT-IN 1 in sync to system clock */
#define RME9652_DS_rd	   (1<<19)	/* 1=Double Speed Mode, 0=Normal Speed */
#define RME9652_tc_busy	   (1<<20)	/* 1=time-code copy in progress (960ms) */
#define RME9652_tc_out	   (1<<21)	/* time-code out bit */
#define RME9652_F_0	   (1<<22)	/* 000=64kHz, 100=88.2kHz, 011=96kHz  */
#define RME9652_F_1	   (1<<23)	/* 111=32kHz, 110=44.1kHz, 101=48kHz, */
#define RME9652_F_2	   (1<<24)	/* external Crystal Chip if ERF=1 */
#define RME9652_ERF	   (1<<25)	/* Error-Flag of SDPIF Receiver (1=No Lock) */
#define RME9652_buffer_id  (1<<26)	/* toggles by each interrupt on rec/play */
#define RME9652_tc_valid   (1<<27)	/* 1 = a signal is detected on time-code input */
#define RME9652_SPDIF_READ (1<<28)      /* byte available from Rev 1.5+ S/PDIF interface */

#define RME9652_sync	  (RME9652_sync_0|RME9652_sync_1|RME9652_sync_2)
#define RME9652_lock	  (RME9652_lock_0|RME9652_lock_1|RME9652_lock_2)
#define RME9652_F	  (RME9652_F_0|RME9652_F_1|RME9652_F_2)
#define rme9652_decode_spdif_rate(x) ((x)>>22)

/* Bit 6..15 : h/w buffer pointer */

#define RME9652_buf_pos	  0x000FFC0

/* Bits 31,30,29 are bits 5,4,3 of h/w pointer position on later
   Rev G EEPROMS and Rev 1.5 cards or later.
*/ 

#define RME9652_REV15_buf_pos(x) ((((x)&0xE0000000)>>26)|((x)&RME9652_buf_pos))

/* amount of io space we remap for register access. i'm not sure we
   even need this much, but 1K is nice round number :)
*/

#define RME9652_IO_EXTENT     1024

#define RME9652_init_buffer       0
#define RME9652_play_buffer       32	/* holds ptr to 26x64kBit host RAM */
#define RME9652_rec_buffer        36	/* holds ptr to 26x64kBit host RAM */
#define RME9652_control_register  64
#define RME9652_irq_clear         96
#define RME9652_time_code         100	/* useful if used with alesis adat */
#define RME9652_thru_base         128	/* 132...228 Thru for 26 channels */

/* Read-only registers */

/* Writing to any of the register locations writes to the status
   register. We'll use the first location as our point of access.
*/

#define RME9652_status_register    0

/* --------- Control-Register Bits ---------------- */


#define RME9652_start_bit	   (1<<0)	/* start record/play */
                                                /* bits 1-3 encode buffersize/latency */
#define RME9652_Master		   (1<<4)	/* Clock Mode Master=1,Slave/Auto=0 */
#define RME9652_IE		   (1<<5)	/* Interrupt Enable */
#define RME9652_freq		   (1<<6)       /* samplerate 0=44.1/88.2, 1=48/96 kHz */
#define RME9652_freq1		   (1<<7)       /* if 0, 32kHz, else always 1 */
#define RME9652_DS                 (1<<8)	/* Doule Speed 0=44.1/48, 1=88.2/96 Khz */
#define RME9652_PRO		   (1<<9)	/* S/PDIF out: 0=consumer, 1=professional */
#define RME9652_EMP		   (1<<10)	/*  Emphasis 0=None, 1=ON */
#define RME9652_Dolby		   (1<<11)	/*  Non-audio bit 1=set, 0=unset */
#define RME9652_opt_out	           (1<<12)	/* Use 1st optical OUT as SPDIF: 1=yes,0=no */
#define RME9652_wsel		   (1<<13)	/* use Wordclock as sync (overwrites master) */
#define RME9652_inp_0		   (1<<14)	/* SPDIF-IN: 00=optical (ADAT1),     */
#define RME9652_inp_1		   (1<<15)	/* 01=koaxial (Cinch), 10=Internal CDROM */
#define RME9652_SyncPref_ADAT2	   (1<<16)
#define RME9652_SyncPref_ADAT3	   (1<<17)
#define RME9652_SPDIF_RESET        (1<<18)      /* Rev 1.5+: h/w S/PDIF receiver */
#define RME9652_SPDIF_SELECT       (1<<19)
#define RME9652_SPDIF_CLOCK        (1<<20)
#define RME9652_SPDIF_WRITE        (1<<21)
#define RME9652_ADAT1_INTERNAL     (1<<22)      /* Rev 1.5+: if set, internal CD connector carries ADAT */

/* buffersize = 512Bytes * 2^n, where n is made from Bit2 ... Bit0 */

#define RME9652_latency            0x0e
#define rme9652_encode_latency(x)  (((x)&0x7)<<1)
#define rme9652_decode_latency(x)  (((x)>>1)&0x7)
#define rme9652_running_double_speed(s) ((s)->control_register & RME9652_DS)
#define RME9652_inp                (RME9652_inp_0|RME9652_inp_1)
#define rme9652_encode_spdif_in(x) (((x)&0x3)<<14)
#define rme9652_decode_spdif_in(x) (((x)>>14)&0x3)

#define RME9652_SyncPref_Mask      (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)
#define RME9652_SyncPref_ADAT1	   0
#define RME9652_SyncPref_SPDIF	   (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)

/* the size of a substream (1 mono data stream) */

#define RME9652_CHANNEL_BUFFER_SAMPLES  (16*1024)
#define RME9652_CHANNEL_BUFFER_BYTES    (4*RME9652_CHANNEL_BUFFER_SAMPLES)

/* the size of the area we need to allocate for DMA transfers. the
   size is the same regardless of the number of channels - the 
   9636 still uses the same memory area.

   Note that we allocate 1 more channel than is apparently needed
   because the h/w seems to write 1 byte beyond the end of the last
   page. Sigh.
*/

#define RME9652_DMA_AREA_BYTES ((RME9652_NCHANNELS+1) * RME9652_CHANNEL_BUFFER_BYTES)
#define RME9652_DMA_AREA_KILOBYTES (RME9652_DMA_AREA_BYTES/1024)

struct snd_rme9652 {
	int dev;

	spinlock_t lock;
	int irq;
	unsigned long port;
	void __iomem *iobase;
	
	int precise_ptr;

	u32 control_register;	/* cached value */
	u32 thru_bits;		/* thru 1=on, 0=off channel 1=Bit1... channel 26= Bit26 */

	u32 creg_spdif;
	u32 creg_spdif_stream;

	char *card_name;		/* hammerfall or hammerfall light names */

        size_t hw_offsetmask;     	/* &-with status register to get real hw_offset */
	size_t prev_hw_offset;		/* previous hw offset */
	size_t max_jitter;		/* maximum jitter in frames for 
					   hw pointer */
	size_t period_bytes;		/* guess what this is */

	unsigned char ds_channels;
	unsigned char ss_channels;	/* different for hammerfall/hammerfall-light */

	struct snd_dma_buffer playback_dma_buf;
	struct snd_dma_buffer capture_dma_buf;

	unsigned char *capture_buffer;	/* suitably aligned address */
	unsigned char *playback_buffer;	/* suitably aligned address */

	pid_t capture_pid;
	pid_t playback_pid;

	struct snd_pcm_substream *capture_substream;
	struct snd_pcm_substream *playback_substream;
	int running;

        int passthru;                   /* non-zero if doing pass-thru */
        int hw_rev;                     /* h/w rev * 10 (i.e. 1.5 has hw_rev = 15) */

	int last_spdif_sample_rate;	/* so that we can catch externally ... */
	int last_adat_sample_rate;	/* ... induced rate changes            */

        char *channel_map;

	struct snd_card *card;
	struct snd_pcm *pcm;
	struct pci_dev *pci;
	struct snd_kcontrol *spdif_ctl;

};

/* These tables map the ALSA channels 1..N to the channels that we
   need to use in order to find the relevant channel buffer. RME
   refer to this kind of mapping as between "the ADAT channel and
   the DMA channel." We index it using the logical audio channel,
   and the value is the DMA channel (i.e. channel buffer number)
   where the data for that channel can be read/written from/to.
*/

static char channel_map_9652_ss[26] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	18, 19, 20, 21, 22, 23, 24, 25
};

static char channel_map_9636_ss[26] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 
	/* channels 16 and 17 are S/PDIF */
	24, 25,
	/* channels 18-25 don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_9652_ds[26] = {
	/* ADAT channels are remapped */
	1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23,
	/* channels 12 and 13 are S/PDIF */
	24, 25,
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static char channel_map_9636_ds[26] = {
	/* ADAT channels are remapped */
	1, 3, 5, 7, 9, 11, 13, 15,
	/* channels 8 and 9 are S/PDIF */
	24, 25
	/* others don't exist */
	-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

static int snd_hammerfall_get_buffer(struct pci_dev *pci, struct snd_dma_buffer *dmab, size_t size)
{
	dmab->dev.type = SNDRV_DMA_TYPE_DEV;
	dmab->dev.dev = snd_dma_pci_data(pci);
	if (snd_dma_get_reserved_buf(dmab, snd_dma_pci_buf_id(pci))) {
		if (dmab->bytes >= size)
			return 0;
	}
	if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, snd_dma_pci_data(pci),
				size, dmab) < 0)
		return -ENOMEM;
	return 0;
}

static void snd_hammerfall_free_buffer(struct snd_dma_buffer *dmab, struct pci_dev *pci)
{
	if (dmab->area) {
		dmab->dev.dev = NULL; /* make it anonymous */
		snd_dma_reserve_buf(dmab, snd_dma_pci_buf_id(pci));
	}
}


static DEFINE_PCI_DEVICE_TABLE(snd_rme9652_ids) = {
	{
		.vendor	   = 0x10ee,
		.device	   = 0x3fc4,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
	},	/* RME Digi9652 */
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, snd_rme9652_ids);

static inline void rme9652_write(struct snd_rme9652 *rme9652, int reg, int val)
{
	writel(val, rme9652->iobase + reg);
}

static inline unsigned int rme9652_read(struct snd_rme9652 *rme9652, int reg)
{
	return readl(rme9652->iobase + reg);
}

static inline int snd_rme9652_use_is_exclusive(struct snd_rme9652 *rme9652)
{
	unsigned long flags;
	int ret = 1;

	spin_lock_irqsave(&rme9652->lock, flags);
	if ((rme9652->playback_pid != rme9652->capture_pid) &&
	    (rme9652->playback_pid >= 0) && (rme9652->capture_pid >= 0)) {
		ret = 0;
	}
	spin_unlock_irqrestore(&rme9652->lock, flags);
	return ret;
}

static inline int rme9652_adat_sample_rate(struct snd_rme9652 *rme9652)
{
	if (rme9652_running_double_speed(rme9652)) {
		return (rme9652_read(rme9652, RME9652_status_register) &
			RME9652_fs48) ? 96000 : 88200;
	} else {
		return (rme9652_read(rme9652, RME9652_status_register) &
			RME9652_fs48) ? 48000 : 44100;
	}
}

static inline void rme9652_compute_period_size(struct snd_rme9652 *rme9652)
{
	unsigned int i;

	i = rme9652->control_register & RME9652_latency;
	rme9652->period_bytes = 1 << ((rme9652_decode_latency(i) + 8));
	rme9652->hw_offsetmask = 
		(rme9652->period_bytes * 2 - 1) & RME9652_buf_pos;
	rme9652->max_jitter = 80;
}

static snd_pcm_uframes_t rme9652_hw_pointer(struct snd_rme9652 *rme9652)
{
	int status;
	unsigned int offset, frag;
	snd_pcm_uframes_t period_size = rme9652->period_bytes / 4;
	snd_pcm_sframes_t delta;

	status = rme9652_read(rme9652, RME9652_status_register);
	if (!rme9652->precise_ptr)
		return (status & RME9652_buffer_id) ? period_size : 0;
	offset = status & RME9652_buf_pos;

	/* The hardware may give a backward movement for up to 80 frames
           Martin Kirst <martin.kirst@freenet.de> knows the details.
	*/

	delta = rme9652->prev_hw_offset - offset;
	delta &= 0xffff;
	if (delta <= (snd_pcm_sframes_t)rme9652->max_jitter * 4)
		offset = rme9652->prev_hw_offset;
	else
		rme9652->prev_hw_offset = offset;
	offset &= rme9652->hw_offsetmask;
	offset /= 4;
	frag = status & RME9652_buffer_id;

	if (offset < period_size) {
		if (offset > rme9652->max_jitter) {
			if (frag)
				printk(KERN_ERR "Unexpected hw_pointer position (bufid == 0): status: %x offset: %d\n", status, offset);
		} else if (!frag)
			return 0;
		offset -= rme9652->max_jitter;
		if ((int)offset < 0)
			offset += period_size * 2;
	} else {
		if (offset > period_size + rme9652->max_jitter) {
			if (!frag)
				printk(KERN_ERR "Unexpected hw_pointer position (bufid == 1): status: %x offset: %d\n", status, offset);
		} else if (frag)
			return period_size;
		offset -= rme9652->max_jitter;
	}

	return offset;
}

static inline void rme9652_reset_hw_pointer(struct snd_rme9652 *rme9652)
{
	int i;

	/* reset the FIFO pointer to zero. We do this by writing to 8
	   registers, each of which is a 32bit wide register, and set
	   them all to zero. Note that s->iobase is a pointer to
	   int32, not pointer to char.  
	*/

	for (i = 0; i < 8; i++) {
		rme9652_write(rme9652, i * 4, 0);
		udelay(10);
	}
	rme9652->prev_hw_offset = 0;
}

static inline void rme9652_start(struct snd_rme9652 *s)
{
	s->control_register |= (RME9652_IE | RME9652_start_bit);
	rme9652_write(s, RME9652_control_register, s->control_register);
}

static inline void rme9652_stop(struct snd_rme9652 *s)
{
	s->control_register &= ~(RME9652_start_bit | RME9652_IE);
	rme9652_write(s, RME9652_control_register, s->control_register);
}

static int rme9652_set_interrupt_interval(struct snd_rme9652 *s,
					  unsigned int frames)
{
	int restart = 0;
	int n;

	spin_lock_irq(&s->lock);

	if ((restart = s->running)) {
		rme9652_stop(s);
	}

	frames >>= 7;
	n = 0;
	while (frames) {
		n++;
		frames >>= 1;
	}

	s->control_register &= ~RME9652_latency;
	s->control_register |= rme9652_encode_latency(n);

	rme9652_write(s, RME9652_control_register, s->control_register);

	rme9652_compute_period_size(s);

	if (restart)
		rme9652_start(s);

	spin_unlock_irq(&s->lock);

	return 0;
}

static int rme9652_set_rate(struct snd_rme9652 *rme9652, int rate)
{
	int restart;
	int reject_if_open = 0;
	int xrate;

	if (!snd_rme9652_use_is_exclusive (rme9652)) {
		return -EBUSY;
	}

	/* Changing from a "single speed" to a "double speed" rate is
	   not allowed if any substreams are open. This is because
	   such a change causes a shift in the location of 
	   the DMA buffers and a reduction in the number of available
	   buffers. 

	   Note that a similar but essentially insoluble problem
	   exists for externally-driven rate changes. All we can do
	   is to flag rate changes in the read/write routines.
	 */

	spin_lock_irq(&rme9652->lock);
	xrate = rme9652_adat_sample_rate(rme9652);

	switch (rate) {
	case 44100:
		if (xrate > 48000) {
			reject_if_open = 1;
		}
		rate = 0;
		break;
	case 48000:
		if (xrate > 48000) {
			reject_if_open = 1;
		}
		rate = RME9652_freq;
		break;
	case 88200:
		if (xrate < 48000) {
			reject_if_open = 1;
		}
		rate = RME9652_DS;
		break;
	case 96000:
		if (xrate < 48000) {
			reject_if_open = 1;
		}
		rate = RME9652_DS | RME9652_freq;
		break;
	default:
		spin_unlock_irq(&rme9652->lock);
		return -EINVAL;
	}

	if (reject_if_open && (rme9652->capture_pid >= 0 || rme9652->playback_pid >= 0)) {
		spin_unlock_irq(&rme9652->lock);
		return -EBUSY;
	}

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}
	rme9652->control_register &= ~(RME9652_freq | RME9652_DS);
	rme9652->control_register |= rate;
	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	if (rate & RME9652_DS) {
		if (rme9652->ss_channels == RME9652_NCHANNELS) {
			rme9652->channel_map = channel_map_9652_ds;
		} else {
			rme9652->channel_map = channel_map_9636_ds;
		}
	} else {
		if (rme9652->ss_channels == RME9652_NCHANNELS) {
			rme9652->channel_map = channel_map_9652_ss;
		} else {
			rme9652->channel_map = channel_map_9636_ss;
		}
	}

	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static void rme9652_set_thru(struct snd_rme9652 *rme9652, int channel, int enable)
{
	int i;

	rme9652->passthru = 0;

	if (channel < 0) {

		/* set thru for all channels */

		if (enable) {
			for (i = 0; i < RME9652_NCHANNELS; i++) {
				rme9652->thru_bits |= (1 << i);
				rme9652_write(rme9652, RME9652_thru_base + i * 4, 1);
			}
		} else {
			for (i = 0; i < RME9652_NCHANNELS; i++) {
				rme9652->thru_bits &= ~(1 << i);
				rme9652_write(rme9652, RME9652_thru_base + i * 4, 0);
			}
		}

	} else {
		int mapped_channel;

		mapped_channel = rme9652->channel_map[channel];

		if (enable) {
			rme9652->thru_bits |= (1 << mapped_channel);
		} else {
			rme9652->thru_bits &= ~(1 << mapped_channel);
		}

		rme9652_write(rme9652,
			       RME9652_thru_base + mapped_channel * 4,
			       enable ? 1 : 0);			       
	}
}

static int rme9652_set_passthru(struct snd_rme9652 *rme9652, int onoff)
{
	if (onoff) {
		rme9652_set_thru(rme9652, -1, 1);

		/* we don't want interrupts, so do a
		   custom version of rme9652_start().
		*/

		rme9652->control_register =
			RME9652_inp_0 | 
			rme9652_encode_latency(7) |
			RME9652_start_bit;

		rme9652_reset_hw_pointer(rme9652);

		rme9652_write(rme9652, RME9652_control_register,
			      rme9652->control_register);
		rme9652->passthru = 1;
	} else {
		rme9652_set_thru(rme9652, -1, 0);
		rme9652_stop(rme9652);		
		rme9652->passthru = 0;
	}

	return 0;
}

static void rme9652_spdif_set_bit (struct snd_rme9652 *rme9652, int mask, int onoff)
{
	if (onoff) 
		rme9652->control_register |= mask;
	else 
		rme9652->control_register &= ~mask;
		
	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);
}

static void rme9652_spdif_write_byte (struct snd_rme9652 *rme9652, const int val)
{
	long mask;
	long i;

	for (i = 0, mask = 0x80; i < 8; i++, mask >>= 1) {
		if (val & mask)
			rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_WRITE, 1);
		else 
			rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_WRITE, 0);

		rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_CLOCK, 1);
		rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_CLOCK, 0);
	}
}

static int rme9652_spdif_read_byte (struct snd_rme9652 *rme9652)
{
	long mask;
	long val;
	long i;

	val = 0;

	for (i = 0, mask = 0x80;  i < 8; i++, mask >>= 1) {
		rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_CLOCK, 1);
		if (rme9652_read (rme9652, RME9652_status_register) & RME9652_SPDIF_READ)
			val |= mask;
		rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_CLOCK, 0);
	}

	return val;
}

static void rme9652_write_spdif_codec (struct snd_rme9652 *rme9652, const int address, const int data)
{
	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 1);
	rme9652_spdif_write_byte (rme9652, 0x20);
	rme9652_spdif_write_byte (rme9652, address);
	rme9652_spdif_write_byte (rme9652, data);
	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 0);
}


static int rme9652_spdif_read_codec (struct snd_rme9652 *rme9652, const int address)
{
	int ret;

	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 1);
	rme9652_spdif_write_byte (rme9652, 0x20);
	rme9652_spdif_write_byte (rme9652, address);
	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 0);
	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 1);

	rme9652_spdif_write_byte (rme9652, 0x21);
	ret = rme9652_spdif_read_byte (rme9652);
	rme9652_spdif_set_bit (rme9652, RME9652_SPDIF_SELECT, 0);

	return ret;
}

static void rme9652_initialize_spdif_receiver (struct snd_rme9652 *rme9652)
{
	/* XXX what unsets this ? */

	rme9652->control_register |= RME9652_SPDIF_RESET;

	rme9652_write_spdif_codec (rme9652, 4, 0x40);
	rme9652_write_spdif_codec (rme9652, 17, 0x13);
	rme9652_write_spdif_codec (rme9652, 6, 0x02);
}

static inline int rme9652_spdif_sample_rate(struct snd_rme9652 *s)
{
	unsigned int rate_bits;

	if (rme9652_read(s, RME9652_status_register) & RME9652_ERF) {
		return -1;	/* error condition */
	}
	
	if (s->hw_rev == 15) {

		int x, y, ret;
		
		x = rme9652_spdif_read_codec (s, 30);

		if (x != 0) 
			y = 48000 * 64 / x;
		else
			y = 0;

		if      (y > 30400 && y < 33600)  ret = 32000; 
		else if (y > 41900 && y < 46000)  ret = 44100;
		else if (y > 46000 && y < 50400)  ret = 48000;
		else if (y > 60800 && y < 67200)  ret = 64000;
		else if (y > 83700 && y < 92000)  ret = 88200;
		else if (y > 92000 && y < 100000) ret = 96000;
		else                              ret = 0;
		return ret;
	}

	rate_bits = rme9652_read(s, RME9652_status_register) & RME9652_F;

	switch (rme9652_decode_spdif_rate(rate_bits)) {
	case 0x7:
		return 32000;
		break;

	case 0x6:
		return 44100;
		break;

	case 0x5:
		return 48000;
		break;

	case 0x4:
		return 88200;
		break;

	case 0x3:
		return 96000;
		break;

	case 0x0:
		return 64000;
		break;

	default:
		snd_printk(KERN_ERR "%s: unknown S/PDIF input rate (bits = 0x%x)\n",
			   s->card_name, rate_bits);
		return 0;
		break;
	}
}

/*-----------------------------------------------------------------------------
  Control Interface
  ----------------------------------------------------------------------------*/

static u32 snd_rme9652_convert_from_aes(struct snd_aes_iec958 *aes)
{
	u32 val = 0;
	val |= (aes->status[0] & IEC958_AES0_PROFESSIONAL) ? RME9652_PRO : 0;
	val |= (aes->status[0] & IEC958_AES0_NONAUDIO) ? RME9652_Dolby : 0;
	if (val & RME9652_PRO)
		val |= (aes->status[0] & IEC958_AES0_PRO_EMPHASIS_5015) ? RME9652_EMP : 0;
	else
		val |= (aes->status[0] & IEC958_AES0_CON_EMPHASIS_5015) ? RME9652_EMP : 0;
	return val;
}

static void snd_rme9652_convert_to_aes(struct snd_aes_iec958 *aes, u32 val)
{
	aes->status[0] = ((val & RME9652_PRO) ? IEC958_AES0_PROFESSIONAL : 0) |
			 ((val & RME9652_Dolby) ? IEC958_AES0_NONAUDIO : 0);
	if (val & RME9652_PRO)
		aes->status[0] |= (val & RME9652_EMP) ? IEC958_AES0_PRO_EMPHASIS_5015 : 0;
	else
		aes->status[0] |= (val & RME9652_EMP) ? IEC958_AES0_CON_EMPHASIS_5015 : 0;
}

static int snd_rme9652_control_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme9652_control_spdif_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	snd_rme9652_convert_to_aes(&ucontrol->value.iec958, rme9652->creg_spdif);
	return 0;
}

static int snd_rme9652_control_spdif_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	u32 val;
	
	val = snd_rme9652_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irq(&rme9652->lock);
	change = val != rme9652->creg_spdif;
	rme9652->creg_spdif = val;
	spin_unlock_irq(&rme9652->lock);
	return change;
}

static int snd_rme9652_control_spdif_stream_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme9652_control_spdif_stream_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	snd_rme9652_convert_to_aes(&ucontrol->value.iec958, rme9652->creg_spdif_stream);
	return 0;
}

static int snd_rme9652_control_spdif_stream_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	u32 val;
	
	val = snd_rme9652_convert_from_aes(&ucontrol->value.iec958);
	spin_lock_irq(&rme9652->lock);
	change = val != rme9652->creg_spdif_stream;
	rme9652->creg_spdif_stream = val;
	rme9652->control_register &= ~(RME9652_PRO | RME9652_Dolby | RME9652_EMP);
	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register |= val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

static int snd_rme9652_control_spdif_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int snd_rme9652_control_spdif_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = kcontrol->private_value;
	return 0;
}

#define RME9652_ADAT1_IN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_adat1_in, \
  .get = snd_rme9652_get_adat1_in, \
  .put = snd_rme9652_put_adat1_in }

static unsigned int rme9652_adat1_in(struct snd_rme9652 *rme9652)
{
	if (rme9652->control_register & RME9652_ADAT1_INTERNAL)
		return 1; 
	return 0;
}

static int rme9652_set_adat1_input(struct snd_rme9652 *rme9652, int internal)
{
	int restart = 0;

	if (internal) {
		rme9652->control_register |= RME9652_ADAT1_INTERNAL;
	} else {
		rme9652->control_register &= ~RME9652_ADAT1_INTERNAL;
	}

	/* XXX do we actually need to stop the card when we do this ? */

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	return 0;
}

static int snd_rme9652_info_adat1_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[2] = {"ADAT1", "Internal"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_rme9652_get_adat1_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.enumerated.item[0] = rme9652_adat1_in(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_adat1_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;
	
	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0] % 2;
	spin_lock_irq(&rme9652->lock);
	change = val != rme9652_adat1_in(rme9652);
	if (change)
		rme9652_set_adat1_input(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

#define RME9652_SPDIF_IN(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_spdif_in, \
  .get = snd_rme9652_get_spdif_in, .put = snd_rme9652_put_spdif_in }

static unsigned int rme9652_spdif_in(struct snd_rme9652 *rme9652)
{
	return rme9652_decode_spdif_in(rme9652->control_register &
				       RME9652_inp);
}

static int rme9652_set_spdif_input(struct snd_rme9652 *rme9652, int in)
{
	int restart = 0;

	rme9652->control_register &= ~RME9652_inp;
	rme9652->control_register |= rme9652_encode_spdif_in(in);

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	return 0;
}

static int snd_rme9652_info_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {"ADAT1", "Coaxial", "Internal"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_rme9652_get_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.enumerated.item[0] = rme9652_spdif_in(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_spdif_in(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;
	
	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;
	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irq(&rme9652->lock);
	change = val != rme9652_spdif_in(rme9652);
	if (change)
		rme9652_set_spdif_input(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

#define RME9652_SPDIF_OUT(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_spdif_out, \
  .get = snd_rme9652_get_spdif_out, .put = snd_rme9652_put_spdif_out }

static int rme9652_spdif_out(struct snd_rme9652 *rme9652)
{
	return (rme9652->control_register & RME9652_opt_out) ? 1 : 0;
}

static int rme9652_set_spdif_output(struct snd_rme9652 *rme9652, int out)
{
	int restart = 0;

	if (out) {
		rme9652->control_register |= RME9652_opt_out;
	} else {
		rme9652->control_register &= ~RME9652_opt_out;
	}

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	return 0;
}

#define snd_rme9652_info_spdif_out	snd_ctl_boolean_mono_info

static int snd_rme9652_get_spdif_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.integer.value[0] = rme9652_spdif_out(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_spdif_out(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;
	
	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;
	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&rme9652->lock);
	change = (int)val != rme9652_spdif_out(rme9652);
	rme9652_set_spdif_output(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

#define RME9652_SYNC_MODE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_sync_mode, \
  .get = snd_rme9652_get_sync_mode, .put = snd_rme9652_put_sync_mode }

static int rme9652_sync_mode(struct snd_rme9652 *rme9652)
{
	if (rme9652->control_register & RME9652_wsel) {
		return 2;
	} else if (rme9652->control_register & RME9652_Master) {
		return 1;
	} else {
		return 0;
	}
}

static int rme9652_set_sync_mode(struct snd_rme9652 *rme9652, int mode)
{
	int restart = 0;

	switch (mode) {
	case 0:
		rme9652->control_register &=
		    ~(RME9652_Master | RME9652_wsel);
		break;
	case 1:
		rme9652->control_register =
		    (rme9652->control_register & ~RME9652_wsel) | RME9652_Master;
		break;
	case 2:
		rme9652->control_register |=
		    (RME9652_Master | RME9652_wsel);
		break;
	}

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	return 0;
}

static int snd_rme9652_info_sync_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[3] = {"AutoSync", "Master", "Word Clock"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_rme9652_get_sync_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.enumerated.item[0] = rme9652_sync_mode(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_sync_mode(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;
	
	val = ucontrol->value.enumerated.item[0] % 3;
	spin_lock_irq(&rme9652->lock);
	change = (int)val != rme9652_sync_mode(rme9652);
	rme9652_set_sync_mode(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

#define RME9652_SYNC_PREF(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_sync_pref, \
  .get = snd_rme9652_get_sync_pref, .put = snd_rme9652_put_sync_pref }

static int rme9652_sync_pref(struct snd_rme9652 *rme9652)
{
	switch (rme9652->control_register & RME9652_SyncPref_Mask) {
	case RME9652_SyncPref_ADAT1:
		return RME9652_SYNC_FROM_ADAT1;
	case RME9652_SyncPref_ADAT2:
		return RME9652_SYNC_FROM_ADAT2;
	case RME9652_SyncPref_ADAT3:
		return RME9652_SYNC_FROM_ADAT3;
	case RME9652_SyncPref_SPDIF:
		return RME9652_SYNC_FROM_SPDIF;
	}
	/* Not reachable */
	return 0;
}

static int rme9652_set_sync_pref(struct snd_rme9652 *rme9652, int pref)
{
	int restart;

	rme9652->control_register &= ~RME9652_SyncPref_Mask;
	switch (pref) {
	case RME9652_SYNC_FROM_ADAT1:
		rme9652->control_register |= RME9652_SyncPref_ADAT1;
		break;
	case RME9652_SYNC_FROM_ADAT2:
		rme9652->control_register |= RME9652_SyncPref_ADAT2;
		break;
	case RME9652_SYNC_FROM_ADAT3:
		rme9652->control_register |= RME9652_SyncPref_ADAT3;
		break;
	case RME9652_SYNC_FROM_SPDIF:
		rme9652->control_register |= RME9652_SyncPref_SPDIF;
		break;
	}

	if ((restart = rme9652->running)) {
		rme9652_stop(rme9652);
	}

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	if (restart) {
		rme9652_start(rme9652);
	}

	return 0;
}

static int snd_rme9652_info_sync_pref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {"IEC958 In", "ADAT1 In", "ADAT2 In", "ADAT3 In"};
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = rme9652->ss_channels == RME9652_NCHANNELS ? 4 : 3;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_rme9652_get_sync_pref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.enumerated.item[0] = rme9652_sync_pref(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_sync_pref(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change, max;
	unsigned int val;
	
	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;
	max = rme9652->ss_channels == RME9652_NCHANNELS ? 4 : 3;
	val = ucontrol->value.enumerated.item[0] % max;
	spin_lock_irq(&rme9652->lock);
	change = (int)val != rme9652_sync_pref(rme9652);
	rme9652_set_sync_pref(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return change;
}

static int snd_rme9652_info_thru(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = rme9652->ss_channels;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_rme9652_get_thru(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	unsigned int k;
	u32 thru_bits = rme9652->thru_bits;

	for (k = 0; k < rme9652->ss_channels; ++k) {
		ucontrol->value.integer.value[k] = !!(thru_bits & (1 << k));
	}
	return 0;
}

static int snd_rme9652_put_thru(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int chn;
	u32 thru_bits = 0;

	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;

	for (chn = 0; chn < rme9652->ss_channels; ++chn) {
		if (ucontrol->value.integer.value[chn])
			thru_bits |= 1 << chn;
	}
	
	spin_lock_irq(&rme9652->lock);
	change = thru_bits ^ rme9652->thru_bits;
	if (change) {
		for (chn = 0; chn < rme9652->ss_channels; ++chn) {
			if (!(change & (1 << chn)))
				continue;
			rme9652_set_thru(rme9652,chn,thru_bits&(1<<chn));
		}
	}
	spin_unlock_irq(&rme9652->lock);
	return !!change;
}

#define RME9652_PASSTHRU(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .info = snd_rme9652_info_passthru, \
  .put = snd_rme9652_put_passthru, \
  .get = snd_rme9652_get_passthru }

#define snd_rme9652_info_passthru	snd_ctl_boolean_mono_info

static int snd_rme9652_get_passthru(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);

	spin_lock_irq(&rme9652->lock);
	ucontrol->value.integer.value[0] = rme9652->passthru;
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static int snd_rme9652_put_passthru(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int val;
	int err = 0;

	if (!snd_rme9652_use_is_exclusive(rme9652))
		return -EBUSY;

	val = ucontrol->value.integer.value[0] & 1;
	spin_lock_irq(&rme9652->lock);
	change = (ucontrol->value.integer.value[0] != rme9652->passthru);
	if (change)
		err = rme9652_set_passthru(rme9652, val);
	spin_unlock_irq(&rme9652->lock);
	return err ? err : change;
}

/* Read-only switches */

#define RME9652_SPDIF_RATE(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_rme9652_info_spdif_rate, \
  .get = snd_rme9652_get_spdif_rate }

static int snd_rme9652_info_spdif_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 96000;
	return 0;
}

static int snd_rme9652_get_spdif_rate(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	spin_lock_irq(&rme9652->lock);
	ucontrol->value.integer.value[0] = rme9652_spdif_sample_rate(rme9652);
	spin_unlock_irq(&rme9652->lock);
	return 0;
}

#define RME9652_ADAT_SYNC(xname, xindex, xidx) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_rme9652_info_adat_sync, \
  .get = snd_rme9652_get_adat_sync, .private_value = xidx }

static int snd_rme9652_info_adat_sync(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {"No Lock", "Lock", "No Lock Sync", "Lock Sync"};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_rme9652_get_adat_sync(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	unsigned int mask1, mask2, val;
	
	switch (kcontrol->private_value) {
	case 0: mask1 = RME9652_lock_0; mask2 = RME9652_sync_0; break;	
	case 1: mask1 = RME9652_lock_1; mask2 = RME9652_sync_1; break;	
	case 2: mask1 = RME9652_lock_2; mask2 = RME9652_sync_2; break;	
	default: return -EINVAL;
	}
	val = rme9652_read(rme9652, RME9652_status_register);
	ucontrol->value.enumerated.item[0] = (val & mask1) ? 1 : 0;
	ucontrol->value.enumerated.item[0] |= (val & mask2) ? 2 : 0;
	return 0;
}

#define RME9652_TC_VALID(xname, xindex) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = xindex, \
  .access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE, \
  .info = snd_rme9652_info_tc_valid, \
  .get = snd_rme9652_get_tc_valid }

#define snd_rme9652_info_tc_valid	snd_ctl_boolean_mono_info

static int snd_rme9652_get_tc_valid(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_rme9652 *rme9652 = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = 
		(rme9652_read(rme9652, RME9652_status_register) & RME9652_tc_valid) ? 1 : 0;
	return 0;
}

#ifdef ALSA_HAS_STANDARD_WAY_OF_RETURNING_TIMECODE

/* FIXME: this routine needs a port to the new control API --jk */

static int snd_rme9652_get_tc_value(void *private_data,
				    snd_kswitch_t *kswitch,
				    snd_switch_t *uswitch)
{
	struct snd_rme9652 *s = (struct snd_rme9652 *) private_data;
	u32 value;
	int i;

	uswitch->type = SNDRV_SW_TYPE_DWORD;

	if ((rme9652_read(s, RME9652_status_register) &
	     RME9652_tc_valid) == 0) {
		uswitch->value.data32[0] = 0;
		return 0;
	}

	/* timecode request */

	rme9652_write(s, RME9652_time_code, 0);

	/* XXX bug alert: loop-based timing !!!! */

	for (i = 0; i < 50; i++) {
		if (!(rme9652_read(s, i * 4) & RME9652_tc_busy))
			break;
	}

	if (!(rme9652_read(s, i * 4) & RME9652_tc_busy)) {
		return -EIO;
	}

	value = 0;

	for (i = 0; i < 32; i++) {
		value >>= 1;

		if (rme9652_read(s, i * 4) & RME9652_tc_out)
			value |= 0x80000000;
	}

	if (value > 2 * 60 * 48000) {
		value -= 2 * 60 * 48000;
	} else {
		value = 0;
	}

	uswitch->value.data32[0] = value;

	return 0;
}

#endif				/* ALSA_HAS_STANDARD_WAY_OF_RETURNING_TIMECODE */

static struct snd_kcontrol_new snd_rme9652_controls[] = {
{
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =		snd_rme9652_control_spdif_info,
	.get =		snd_rme9652_control_spdif_get,
	.put =		snd_rme9652_control_spdif_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_INACTIVE,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PCM_STREAM),
	.info =		snd_rme9652_control_spdif_stream_info,
	.get =		snd_rme9652_control_spdif_stream_get,
	.put =		snd_rme9652_control_spdif_stream_put,
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,CON_MASK),
	.info =		snd_rme9652_control_spdif_mask_info,
	.get =		snd_rme9652_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_CON_EMPHASIS,	                                                                                      
},
{
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,PRO_MASK),
	.info =		snd_rme9652_control_spdif_mask_info,
	.get =		snd_rme9652_control_spdif_mask_get,
	.private_value = IEC958_AES0_NONAUDIO |
			IEC958_AES0_PROFESSIONAL |
			IEC958_AES0_PRO_EMPHASIS,
},
RME9652_SPDIF_IN("IEC958 Input Connector", 0),
RME9652_SPDIF_OUT("IEC958 Output also on ADAT1", 0),
RME9652_SYNC_MODE("Sync Mode", 0),
RME9652_SYNC_PREF("Preferred Sync Source", 0),
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Channels Thru",
	.index = 0,
	.info = snd_rme9652_info_thru,
	.get = snd_rme9652_get_thru,
	.put = snd_rme9652_put_thru,
},
RME9652_SPDIF_RATE("IEC958 Sample Rate", 0),
RME9652_ADAT_SYNC("ADAT1 Sync Check", 0, 0),
RME9652_ADAT_SYNC("ADAT2 Sync Check", 0, 1),
RME9652_TC_VALID("Timecode Valid", 0),
RME9652_PASSTHRU("Passthru", 0)
};

static struct snd_kcontrol_new snd_rme9652_adat3_check =
RME9652_ADAT_SYNC("ADAT3 Sync Check", 0, 2);

static struct snd_kcontrol_new snd_rme9652_adat1_input =
RME9652_ADAT1_IN("ADAT1 Input Source", 0);

static int snd_rme9652_create_controls(struct snd_card *card, struct snd_rme9652 *rme9652)
{
	unsigned int idx;
	int err;
	struct snd_kcontrol *kctl;

	for (idx = 0; idx < ARRAY_SIZE(snd_rme9652_controls); idx++) {
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_rme9652_controls[idx], rme9652))) < 0)
			return err;
		if (idx == 1)	/* IEC958 (S/PDIF) Stream */
			rme9652->spdif_ctl = kctl;
	}

	if (rme9652->ss_channels == RME9652_NCHANNELS)
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_rme9652_adat3_check, rme9652))) < 0)
			return err;

	if (rme9652->hw_rev >= 15)
		if ((err = snd_ctl_add(card, kctl = snd_ctl_new1(&snd_rme9652_adat1_input, rme9652))) < 0)
			return err;

	return 0;
}

/*------------------------------------------------------------
   /proc interface 
 ------------------------------------------------------------*/

static void
snd_rme9652_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_rme9652 *rme9652 = (struct snd_rme9652 *) entry->private_data;
	u32 thru_bits = rme9652->thru_bits;
	int show_auto_sync_source = 0;
	int i;
	unsigned int status;
	int x;

	status = rme9652_read(rme9652, RME9652_status_register);

	snd_iprintf(buffer, "%s (Card #%d)\n", rme9652->card_name, rme9652->card->number + 1);
	snd_iprintf(buffer, "Buffers: capture %p playback %p\n",
		    rme9652->capture_buffer, rme9652->playback_buffer);
	snd_iprintf(buffer, "IRQ: %d Registers bus: 0x%lx VM: 0x%lx\n",
		    rme9652->irq, rme9652->port, (unsigned long)rme9652->iobase);
	snd_iprintf(buffer, "Control register: %x\n", rme9652->control_register);

	snd_iprintf(buffer, "\n");

	x = 1 << (6 + rme9652_decode_latency(rme9652->control_register & 
					     RME9652_latency));

	snd_iprintf(buffer, "Latency: %d samples (2 periods of %lu bytes)\n", 
		    x, (unsigned long) rme9652->period_bytes);
	snd_iprintf(buffer, "Hardware pointer (frames): %ld\n",
		    rme9652_hw_pointer(rme9652));
	snd_iprintf(buffer, "Passthru: %s\n",
		    rme9652->passthru ? "yes" : "no");

	if ((rme9652->control_register & (RME9652_Master | RME9652_wsel)) == 0) {
		snd_iprintf(buffer, "Clock mode: autosync\n");
		show_auto_sync_source = 1;
	} else if (rme9652->control_register & RME9652_wsel) {
		if (status & RME9652_wsel_rd) {
			snd_iprintf(buffer, "Clock mode: word clock\n");
		} else {
			snd_iprintf(buffer, "Clock mode: word clock (no signal)\n");
		}
	} else {
		snd_iprintf(buffer, "Clock mode: master\n");
	}

	if (show_auto_sync_source) {
		switch (rme9652->control_register & RME9652_SyncPref_Mask) {
		case RME9652_SyncPref_ADAT1:
			snd_iprintf(buffer, "Pref. sync source: ADAT1\n");
			break;
		case RME9652_SyncPref_ADAT2:
			snd_iprintf(buffer, "Pref. sync source: ADAT2\n");
			break;
		case RME9652_SyncPref_ADAT3:
			snd_iprintf(buffer, "Pref. sync source: ADAT3\n");
			break;
		case RME9652_SyncPref_SPDIF:
			snd_iprintf(buffer, "Pref. sync source: IEC958\n");
			break;
		default:
			snd_iprintf(buffer, "Pref. sync source: ???\n");
		}
	}

	if (rme9652->hw_rev >= 15)
		snd_iprintf(buffer, "\nADAT1 Input source: %s\n",
			    (rme9652->control_register & RME9652_ADAT1_INTERNAL) ?
			    "Internal" : "ADAT1 optical");

	snd_iprintf(buffer, "\n");

	switch (rme9652_decode_spdif_in(rme9652->control_register & 
					RME9652_inp)) {
	case RME9652_SPDIFIN_OPTICAL:
		snd_iprintf(buffer, "IEC958 input: ADAT1\n");
		break;
	case RME9652_SPDIFIN_COAXIAL:
		snd_iprintf(buffer, "IEC958 input: Coaxial\n");
		break;
	case RME9652_SPDIFIN_INTERN:
		snd_iprintf(buffer, "IEC958 input: Internal\n");
		break;
	default:
		snd_iprintf(buffer, "IEC958 input: ???\n");
		break;
	}

	if (rme9652->control_register & RME9652_opt_out) {
		snd_iprintf(buffer, "IEC958 output: Coaxial & ADAT1\n");
	} else {
		snd_iprintf(buffer, "IEC958 output: Coaxial only\n");
	}

	if (rme9652->control_register & RME9652_PRO) {
		snd_iprintf(buffer, "IEC958 quality: Professional\n");
	} else {
		snd_iprintf(buffer, "IEC958 quality: Consumer\n");
	}

	if (rme9652->control_register & RME9652_EMP) {
		snd_iprintf(buffer, "IEC958 emphasis: on\n");
	} else {
		snd_iprintf(buffer, "IEC958 emphasis: off\n");
	}

	if (rme9652->control_register & RME9652_Dolby) {
		snd_iprintf(buffer, "IEC958 Dolby: on\n");
	} else {
		snd_iprintf(buffer, "IEC958 Dolby: off\n");
	}

	i = rme9652_spdif_sample_rate(rme9652);

	if (i < 0) {
		snd_iprintf(buffer,
			    "IEC958 sample rate: error flag set\n");
	} else if (i == 0) {
		snd_iprintf(buffer, "IEC958 sample rate: undetermined\n");
	} else {
		snd_iprintf(buffer, "IEC958 sample rate: %d\n", i);
	}

	snd_iprintf(buffer, "\n");

	snd_iprintf(buffer, "ADAT Sample rate: %dHz\n",
		    rme9652_adat_sample_rate(rme9652));

	/* Sync Check */

	x = status & RME9652_sync_0;
	if (status & RME9652_lock_0) {
		snd_iprintf(buffer, "ADAT1: %s\n", x ? "Sync" : "Lock");
	} else {
		snd_iprintf(buffer, "ADAT1: No Lock\n");
	}

	x = status & RME9652_sync_1;
	if (status & RME9652_lock_1) {
		snd_iprintf(buffer, "ADAT2: %s\n", x ? "Sync" : "Lock");
	} else {
		snd_iprintf(buffer, "ADAT2: No Lock\n");
	}

	x = status & RME9652_sync_2;
	if (status & RME9652_lock_2) {
		snd_iprintf(buffer, "ADAT3: %s\n", x ? "Sync" : "Lock");
	} else {
		snd_iprintf(buffer, "ADAT3: No Lock\n");
	}

	snd_iprintf(buffer, "\n");

	snd_iprintf(buffer, "Timecode signal: %s\n",
		    (status & RME9652_tc_valid) ? "yes" : "no");

	/* thru modes */

	snd_iprintf(buffer, "Punch Status:\n\n");

	for (i = 0; i < rme9652->ss_channels; i++) {
		if (thru_bits & (1 << i)) {
			snd_iprintf(buffer, "%2d:  on ", i + 1);
		} else {
			snd_iprintf(buffer, "%2d: off ", i + 1);
		}

		if (((i + 1) % 8) == 0) {
			snd_iprintf(buffer, "\n");
		}
	}

	snd_iprintf(buffer, "\n");
}

static void __devinit snd_rme9652_proc_init(struct snd_rme9652 *rme9652)
{
	struct snd_info_entry *entry;

	if (! snd_card_proc_new(rme9652->card, "rme9652", &entry))
		snd_info_set_text_ops(entry, rme9652, snd_rme9652_proc_read);
}

static void snd_rme9652_free_buffers(struct snd_rme9652 *rme9652)
{
	snd_hammerfall_free_buffer(&rme9652->capture_dma_buf, rme9652->pci);
	snd_hammerfall_free_buffer(&rme9652->playback_dma_buf, rme9652->pci);
}

static int snd_rme9652_free(struct snd_rme9652 *rme9652)
{
	if (rme9652->irq >= 0)
		rme9652_stop(rme9652);
	snd_rme9652_free_buffers(rme9652);

	if (rme9652->irq >= 0)
		free_irq(rme9652->irq, (void *)rme9652);
	if (rme9652->iobase)
		iounmap(rme9652->iobase);
	if (rme9652->port)
		pci_release_regions(rme9652->pci);

	pci_disable_device(rme9652->pci);
	return 0;
}

static int __devinit snd_rme9652_initialize_memory(struct snd_rme9652 *rme9652)
{
	unsigned long pb_bus, cb_bus;

	if (snd_hammerfall_get_buffer(rme9652->pci, &rme9652->capture_dma_buf, RME9652_DMA_AREA_BYTES) < 0 ||
	    snd_hammerfall_get_buffer(rme9652->pci, &rme9652->playback_dma_buf, RME9652_DMA_AREA_BYTES) < 0) {
		if (rme9652->capture_dma_buf.area)
			snd_dma_free_pages(&rme9652->capture_dma_buf);
		printk(KERN_ERR "%s: no buffers available\n", rme9652->card_name);
		return -ENOMEM;
	}

	/* Align to bus-space 64K boundary */

	cb_bus = ALIGN(rme9652->capture_dma_buf.addr, 0x10000ul);
	pb_bus = ALIGN(rme9652->playback_dma_buf.addr, 0x10000ul);

	/* Tell the card where it is */

	rme9652_write(rme9652, RME9652_rec_buffer, cb_bus);
	rme9652_write(rme9652, RME9652_play_buffer, pb_bus);

	rme9652->capture_buffer = rme9652->capture_dma_buf.area + (cb_bus - rme9652->capture_dma_buf.addr);
	rme9652->playback_buffer = rme9652->playback_dma_buf.area + (pb_bus - rme9652->playback_dma_buf.addr);

	return 0;
}

static void snd_rme9652_set_defaults(struct snd_rme9652 *rme9652)
{
	unsigned int k;

	/* ASSUMPTION: rme9652->lock is either held, or
	   there is no need to hold it (e.g. during module
	   initialization).
	 */

	/* set defaults:

	   SPDIF Input via Coax 
	   autosync clock mode
	   maximum latency (7 = 8192 samples, 64Kbyte buffer,
	   which implies 2 4096 sample, 32Kbyte periods).
	   
	   if rev 1.5, initialize the S/PDIF receiver.

	 */

	rme9652->control_register =
	    RME9652_inp_0 | rme9652_encode_latency(7);

	rme9652_write(rme9652, RME9652_control_register, rme9652->control_register);

	rme9652_reset_hw_pointer(rme9652);
	rme9652_compute_period_size(rme9652);

	/* default: thru off for all channels */

	for (k = 0; k < RME9652_NCHANNELS; ++k)
		rme9652_write(rme9652, RME9652_thru_base + k * 4, 0);

	rme9652->thru_bits = 0;
	rme9652->passthru = 0;

	/* set a default rate so that the channel map is set up */

	rme9652_set_rate(rme9652, 48000);
}

static irqreturn_t snd_rme9652_interrupt(int irq, void *dev_id)
{
	struct snd_rme9652 *rme9652 = (struct snd_rme9652 *) dev_id;

	if (!(rme9652_read(rme9652, RME9652_status_register) & RME9652_IRQ)) {
		return IRQ_NONE;
	}

	rme9652_write(rme9652, RME9652_irq_clear, 0);

	if (rme9652->capture_substream) {
		snd_pcm_period_elapsed(rme9652->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
	}

	if (rme9652->playback_substream) {
		snd_pcm_period_elapsed(rme9652->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream);
	}
	return IRQ_HANDLED;
}

static snd_pcm_uframes_t snd_rme9652_hw_pointer(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	return rme9652_hw_pointer(rme9652);
}

static char *rme9652_channel_buffer_location(struct snd_rme9652 *rme9652,
					     int stream,
					     int channel)

{
	int mapped_channel;

	if (snd_BUG_ON(channel < 0 || channel >= RME9652_NCHANNELS))
		return NULL;
        
	if ((mapped_channel = rme9652->channel_map[channel]) < 0) {
		return NULL;
	}
	
	if (stream == SNDRV_PCM_STREAM_CAPTURE) {
		return rme9652->capture_buffer +
			(mapped_channel * RME9652_CHANNEL_BUFFER_BYTES);
	} else {
		return rme9652->playback_buffer +
			(mapped_channel * RME9652_CHANNEL_BUFFER_BYTES);
	}
}

static int snd_rme9652_playback_copy(struct snd_pcm_substream *substream, int channel,
				     snd_pcm_uframes_t pos, void __user *src, snd_pcm_uframes_t count)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > RME9652_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf = rme9652_channel_buffer_location (rme9652,
						       substream->pstr->stream,
						       channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	if (copy_from_user(channel_buf + pos * 4, src, count * 4))
		return -EFAULT;
	return count;
}

static int snd_rme9652_capture_copy(struct snd_pcm_substream *substream, int channel,
				    snd_pcm_uframes_t pos, void __user *dst, snd_pcm_uframes_t count)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	char *channel_buf;

	if (snd_BUG_ON(pos + count > RME9652_CHANNEL_BUFFER_BYTES / 4))
		return -EINVAL;

	channel_buf = rme9652_channel_buffer_location (rme9652,
						       substream->pstr->stream,
						       channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	if (copy_to_user(dst, channel_buf + pos * 4, count * 4))
		return -EFAULT;
	return count;
}

static int snd_rme9652_hw_silence(struct snd_pcm_substream *substream, int channel,
				  snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	char *channel_buf;

	channel_buf = rme9652_channel_buffer_location (rme9652,
						       substream->pstr->stream,
						       channel);
	if (snd_BUG_ON(!channel_buf))
		return -EIO;
	memset(channel_buf + pos * 4, 0, count * 4);
	return count;
}

static int snd_rme9652_reset(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = rme9652->capture_substream;
	else
		other = rme9652->playback_substream;
	if (rme9652->running)
		runtime->status->hw_ptr = rme9652_hw_pointer(rme9652);
	else
		runtime->status->hw_ptr = 0;
	if (other) {
		struct snd_pcm_substream *s;
		struct snd_pcm_runtime *oruntime = other->runtime;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s == other) {
				oruntime->status->hw_ptr = runtime->status->hw_ptr;
				break;
			}
		}
	}
	return 0;
}

static int snd_rme9652_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	int err;
	pid_t this_pid;
	pid_t other_pid;

	spin_lock_irq(&rme9652->lock);

	if (substream->pstr->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		rme9652->control_register &= ~(RME9652_PRO | RME9652_Dolby | RME9652_EMP);
		rme9652_write(rme9652, RME9652_control_register, rme9652->control_register |= rme9652->creg_spdif_stream);
		this_pid = rme9652->playback_pid;
		other_pid = rme9652->capture_pid;
	} else {
		this_pid = rme9652->capture_pid;
		other_pid = rme9652->playback_pid;
	}

	if ((other_pid > 0) && (this_pid != other_pid)) {

		/* The other stream is open, and not by the same
		   task as this one. Make sure that the parameters
		   that matter are the same.
		 */

		if ((int)params_rate(params) !=
		    rme9652_adat_sample_rate(rme9652)) {
			spin_unlock_irq(&rme9652->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
			return -EBUSY;
		}

		if (params_period_size(params) != rme9652->period_bytes / 4) {
			spin_unlock_irq(&rme9652->lock);
			_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
			return -EBUSY;
		}

		/* We're fine. */

		spin_unlock_irq(&rme9652->lock);
 		return 0;

	} else {
		spin_unlock_irq(&rme9652->lock);
	}

	/* how to make sure that the rate matches an externally-set one ?
	 */

	if ((err = rme9652_set_rate(rme9652, params_rate(params))) < 0) {
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_RATE);
		return err;
	}

	if ((err = rme9652_set_interrupt_interval(rme9652, params_period_size(params))) < 0) {
		_snd_pcm_hw_param_setempty(params, SNDRV_PCM_HW_PARAM_PERIOD_SIZE);
		return err;
	}

	return 0;
}

static int snd_rme9652_channel_info(struct snd_pcm_substream *substream,
				    struct snd_pcm_channel_info *info)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	int chn;

	if (snd_BUG_ON(info->channel >= RME9652_NCHANNELS))
		return -EINVAL;

	if ((chn = rme9652->channel_map[info->channel]) < 0) {
		return -EINVAL;
	}

	info->offset = chn * RME9652_CHANNEL_BUFFER_BYTES;
	info->first = 0;
	info->step = 32;
	return 0;
}

static int snd_rme9652_ioctl(struct snd_pcm_substream *substream,
			     unsigned int cmd, void *arg)
{
	switch (cmd) {
	case SNDRV_PCM_IOCTL1_RESET:
	{
		return snd_rme9652_reset(substream);
	}
	case SNDRV_PCM_IOCTL1_CHANNEL_INFO:
	{
		struct snd_pcm_channel_info *info = arg;
		return snd_rme9652_channel_info(substream, info);
	}
	default:
		break;
	}

	return snd_pcm_lib_ioctl(substream, cmd, arg);
}

static void rme9652_silence_playback(struct snd_rme9652 *rme9652)
{
	memset(rme9652->playback_buffer, 0, RME9652_DMA_AREA_BYTES);
}

static int snd_rme9652_trigger(struct snd_pcm_substream *substream,
			       int cmd)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	struct snd_pcm_substream *other;
	int running;
	spin_lock(&rme9652->lock);
	running = rme9652->running;
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		running |= 1 << substream->stream;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		running &= ~(1 << substream->stream);
		break;
	default:
		snd_BUG();
		spin_unlock(&rme9652->lock);
		return -EINVAL;
	}
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		other = rme9652->capture_substream;
	else
		other = rme9652->playback_substream;

	if (other) {
		struct snd_pcm_substream *s;
		snd_pcm_group_for_each_entry(s, substream) {
			if (s == other) {
				snd_pcm_trigger_done(s, substream);
				if (cmd == SNDRV_PCM_TRIGGER_START)
					running |= 1 << s->stream;
				else
					running &= ~(1 << s->stream);
				goto _ok;
			}
		}
		if (cmd == SNDRV_PCM_TRIGGER_START) {
			if (!(running & (1 << SNDRV_PCM_STREAM_PLAYBACK)) &&
			    substream->stream == SNDRV_PCM_STREAM_CAPTURE)
				rme9652_silence_playback(rme9652);
		} else {
			if (running &&
			    substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				rme9652_silence_playback(rme9652);
		}
	} else {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) 
			rme9652_silence_playback(rme9652);
	}
 _ok:
	snd_pcm_trigger_done(substream, substream);
	if (!rme9652->running && running)
		rme9652_start(rme9652);
	else if (rme9652->running && !running)
		rme9652_stop(rme9652);
	rme9652->running = running;
	spin_unlock(&rme9652->lock);

	return 0;
}

static int snd_rme9652_prepare(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	unsigned long flags;
	int result = 0;

	spin_lock_irqsave(&rme9652->lock, flags);
	if (!rme9652->running)
		rme9652_reset_hw_pointer(rme9652);
	spin_unlock_irqrestore(&rme9652->lock, flags);
	return result;
}

static struct snd_pcm_hardware snd_rme9652_playback_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START |
				 SNDRV_PCM_INFO_DOUBLE),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		(SNDRV_PCM_RATE_44100 | 
				 SNDRV_PCM_RATE_48000 | 
				 SNDRV_PCM_RATE_88200 | 
				 SNDRV_PCM_RATE_96000),
	.rate_min =		44100,
	.rate_max =		96000,
	.channels_min =		10,
	.channels_max =		26,
	.buffer_bytes_max =	RME9652_CHANNEL_BUFFER_BYTES * 26,
	.period_bytes_min =	(64 * 4) * 10,
	.period_bytes_max =	(8192 * 4) * 26,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_rme9652_capture_subinfo =
{
	.info =			(SNDRV_PCM_INFO_MMAP |
				 SNDRV_PCM_INFO_MMAP_VALID |
				 SNDRV_PCM_INFO_NONINTERLEAVED |
				 SNDRV_PCM_INFO_SYNC_START),
	.formats =		SNDRV_PCM_FMTBIT_S32_LE,
	.rates =		(SNDRV_PCM_RATE_44100 | 
				 SNDRV_PCM_RATE_48000 | 
				 SNDRV_PCM_RATE_88200 | 
				 SNDRV_PCM_RATE_96000),
	.rate_min =		44100,
	.rate_max =		96000,
	.channels_min =		10,
	.channels_max =		26,
	.buffer_bytes_max =	RME9652_CHANNEL_BUFFER_BYTES *26,
	.period_bytes_min =	(64 * 4) * 10,
	.period_bytes_max =	(8192 * 4) * 26,
	.periods_min =		2,
	.periods_max =		2,
	.fifo_size =		0,
};

static unsigned int period_sizes[] = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

static struct snd_pcm_hw_constraint_list hw_constraints_period_sizes = {
	.count = ARRAY_SIZE(period_sizes),
	.list = period_sizes,
	.mask = 0
};

static int snd_rme9652_hw_rule_channels(struct snd_pcm_hw_params *params,
					struct snd_pcm_hw_rule *rule)
{
	struct snd_rme9652 *rme9652 = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	unsigned int list[2] = { rme9652->ds_channels, rme9652->ss_channels };
	return snd_interval_list(c, 2, list, 0);
}

static int snd_rme9652_hw_rule_channels_rate(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct snd_rme9652 *rme9652 = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (r->min > 48000) {
		struct snd_interval t = {
			.min = rme9652->ds_channels,
			.max = rme9652->ds_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	} else if (r->max < 88200) {
		struct snd_interval t = {
			.min = rme9652->ss_channels,
			.max = rme9652->ss_channels,
			.integer = 1,
		};
		return snd_interval_refine(c, &t);
	}
	return 0;
}

static int snd_rme9652_hw_rule_rate_channels(struct snd_pcm_hw_params *params,
					     struct snd_pcm_hw_rule *rule)
{
	struct snd_rme9652 *rme9652 = rule->private;
	struct snd_interval *c = hw_param_interval(params, SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_interval *r = hw_param_interval(params, SNDRV_PCM_HW_PARAM_RATE);
	if (c->min >= rme9652->ss_channels) {
		struct snd_interval t = {
			.min = 44100,
			.max = 48000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	} else if (c->max <= rme9652->ds_channels) {
		struct snd_interval t = {
			.min = 88200,
			.max = 96000,
			.integer = 1,
		};
		return snd_interval_refine(r, &t);
	}
	return 0;
}

static int snd_rme9652_playback_open(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irq(&rme9652->lock);

	snd_pcm_set_sync(substream);

        runtime->hw = snd_rme9652_playback_subinfo;
	runtime->dma_area = rme9652->playback_buffer;
	runtime->dma_bytes = RME9652_DMA_AREA_BYTES;

	if (rme9652->capture_substream == NULL) {
		rme9652_stop(rme9652);
		rme9652_set_thru(rme9652, -1, 0);
	}

	rme9652->playback_pid = current->pid;
	rme9652->playback_substream = substream;

	spin_unlock_irq(&rme9652->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_period_sizes);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_rme9652_hw_rule_channels, rme9652,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_rme9652_hw_rule_channels_rate, rme9652,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_rme9652_hw_rule_rate_channels, rme9652,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);

	rme9652->creg_spdif_stream = rme9652->creg_spdif;
	rme9652->spdif_ctl->vd[0].access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(rme9652->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &rme9652->spdif_ctl->id);
	return 0;
}

static int snd_rme9652_playback_release(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);

	spin_lock_irq(&rme9652->lock);

	rme9652->playback_pid = -1;
	rme9652->playback_substream = NULL;

	spin_unlock_irq(&rme9652->lock);

	rme9652->spdif_ctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	snd_ctl_notify(rme9652->card, SNDRV_CTL_EVENT_MASK_VALUE |
		       SNDRV_CTL_EVENT_MASK_INFO, &rme9652->spdif_ctl->id);
	return 0;
}


static int snd_rme9652_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	spin_lock_irq(&rme9652->lock);

	snd_pcm_set_sync(substream);

	runtime->hw = snd_rme9652_capture_subinfo;
	runtime->dma_area = rme9652->capture_buffer;
	runtime->dma_bytes = RME9652_DMA_AREA_BYTES;

	if (rme9652->playback_substream == NULL) {
		rme9652_stop(rme9652);
		rme9652_set_thru(rme9652, -1, 0);
	}

	rme9652->capture_pid = current->pid;
	rme9652->capture_substream = substream;

	spin_unlock_irq(&rme9652->lock);

	snd_pcm_hw_constraint_msbits(runtime, 0, 32, 24);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_SIZE, &hw_constraints_period_sizes);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_rme9652_hw_rule_channels, rme9652,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
			     snd_rme9652_hw_rule_channels_rate, rme9652,
			     SNDRV_PCM_HW_PARAM_RATE, -1);
	snd_pcm_hw_rule_add(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
			     snd_rme9652_hw_rule_rate_channels, rme9652,
			     SNDRV_PCM_HW_PARAM_CHANNELS, -1);
	return 0;
}

static int snd_rme9652_capture_release(struct snd_pcm_substream *substream)
{
	struct snd_rme9652 *rme9652 = snd_pcm_substream_chip(substream);

	spin_lock_irq(&rme9652->lock);

	rme9652->capture_pid = -1;
	rme9652->capture_substream = NULL;

	spin_unlock_irq(&rme9652->lock);
	return 0;
}

static struct snd_pcm_ops snd_rme9652_playback_ops = {
	.open =		snd_rme9652_playback_open,
	.close =	snd_rme9652_playback_release,
	.ioctl =	snd_rme9652_ioctl,
	.hw_params =	snd_rme9652_hw_params,
	.prepare =	snd_rme9652_prepare,
	.trigger =	snd_rme9652_trigger,
	.pointer =	snd_rme9652_hw_pointer,
	.copy =		snd_rme9652_playback_copy,
	.silence =	snd_rme9652_hw_silence,
};

static struct snd_pcm_ops snd_rme9652_capture_ops = {
	.open =		snd_rme9652_capture_open,
	.close =	snd_rme9652_capture_release,
	.ioctl =	snd_rme9652_ioctl,
	.hw_params =	snd_rme9652_hw_params,
	.prepare =	snd_rme9652_prepare,
	.trigger =	snd_rme9652_trigger,
	.pointer =	snd_rme9652_hw_pointer,
	.copy =		snd_rme9652_capture_copy,
};

static int __devinit snd_rme9652_create_pcm(struct snd_card *card,
					 struct snd_rme9652 *rme9652)
{
	struct snd_pcm *pcm;
	int err;

	if ((err = snd_pcm_new(card,
			       rme9652->card_name,
			       0, 1, 1, &pcm)) < 0) {
		return err;
	}

	rme9652->pcm = pcm;
	pcm->private_data = rme9652;
	strcpy(pcm->name, rme9652->card_name);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_rme9652_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_rme9652_capture_ops);

	pcm->info_flags = SNDRV_PCM_INFO_JOINT_DUPLEX;

	return 0;
}

static int __devinit snd_rme9652_create(struct snd_card *card,
				     struct snd_rme9652 *rme9652,
				     int precise_ptr)
{
	struct pci_dev *pci = rme9652->pci;
	int err;
	int status;
	unsigned short rev;

	rme9652->irq = -1;
	rme9652->card = card;

	pci_read_config_word(rme9652->pci, PCI_CLASS_REVISION, &rev);

	switch (rev & 0xff) {
	case 3:
	case 4:
	case 8:
	case 9:
		break;

	default:
		/* who knows? */
		return -ENODEV;
	}

	if ((err = pci_enable_device(pci)) < 0)
		return err;

	spin_lock_init(&rme9652->lock);

	if ((err = pci_request_regions(pci, "rme9652")) < 0)
		return err;
	rme9652->port = pci_resource_start(pci, 0);
	rme9652->iobase = ioremap_nocache(rme9652->port, RME9652_IO_EXTENT);
	if (rme9652->iobase == NULL) {
		snd_printk(KERN_ERR "unable to remap region 0x%lx-0x%lx\n", rme9652->port, rme9652->port + RME9652_IO_EXTENT - 1);
		return -EBUSY;
	}
	
	if (request_irq(pci->irq, snd_rme9652_interrupt, IRQF_SHARED,
			KBUILD_MODNAME, rme9652)) {
		snd_printk(KERN_ERR "unable to request IRQ %d\n", pci->irq);
		return -EBUSY;
	}
	rme9652->irq = pci->irq;
	rme9652->precise_ptr = precise_ptr;

	/* Determine the h/w rev level of the card. This seems like
	   a particularly kludgy way to encode it, but its what RME
	   chose to do, so we follow them ...
	*/

	status = rme9652_read(rme9652, RME9652_status_register);
	if (rme9652_decode_spdif_rate(status&RME9652_F) == 1) {
		rme9652->hw_rev = 15;
	} else {
		rme9652->hw_rev = 11;
	}

	/* Differentiate between the standard Hammerfall, and the
	   "Light", which does not have the expansion board. This
	   method comes from information received from Mathhias
	   Clausen at RME. Display the EEPROM and h/w revID where
	   relevant.  
	*/

	switch (rev) {
	case 8: /* original eprom */
		strcpy(card->driver, "RME9636");
		if (rme9652->hw_rev == 15) {
			rme9652->card_name = "RME Digi9636 (Rev 1.5)";
		} else {
			rme9652->card_name = "RME Digi9636";
		}
		rme9652->ss_channels = RME9636_NCHANNELS;
		break;
	case 9: /* W36_G EPROM */
		strcpy(card->driver, "RME9636");
		rme9652->card_name = "RME Digi9636 (Rev G)";
		rme9652->ss_channels = RME9636_NCHANNELS;
		break;
	case 4: /* W52_G EPROM */
		strcpy(card->driver, "RME9652");
		rme9652->card_name = "RME Digi9652 (Rev G)";
		rme9652->ss_channels = RME9652_NCHANNELS;
		break;
	case 3: /* original eprom */
		strcpy(card->driver, "RME9652");
		if (rme9652->hw_rev == 15) {
			rme9652->card_name = "RME Digi9652 (Rev 1.5)";
		} else {
			rme9652->card_name = "RME Digi9652";
		}
		rme9652->ss_channels = RME9652_NCHANNELS;
		break;
	}

	rme9652->ds_channels = (rme9652->ss_channels - 2) / 2 + 2;

	pci_set_master(rme9652->pci);

	if ((err = snd_rme9652_initialize_memory(rme9652)) < 0) {
		return err;
	}

	if ((err = snd_rme9652_create_pcm(card, rme9652)) < 0) {
		return err;
	}

	if ((err = snd_rme9652_create_controls(card, rme9652)) < 0) {
		return err;
	}

	snd_rme9652_proc_init(rme9652);

	rme9652->last_spdif_sample_rate = -1;
	rme9652->last_adat_sample_rate = -1;
	rme9652->playback_pid = -1;
	rme9652->capture_pid = -1;
	rme9652->capture_substream = NULL;
	rme9652->playback_substream = NULL;

	snd_rme9652_set_defaults(rme9652);

	if (rme9652->hw_rev == 15) {
		rme9652_initialize_spdif_receiver (rme9652);
	}

	return 0;
}

static void snd_rme9652_card_free(struct snd_card *card)
{
	struct snd_rme9652 *rme9652 = (struct snd_rme9652 *) card->private_data;

	if (rme9652)
		snd_rme9652_free(rme9652);
}

static int __devinit snd_rme9652_probe(struct pci_dev *pci,
				       const struct pci_device_id *pci_id)
{
	static int dev;
	struct snd_rme9652 *rme9652;
	struct snd_card *card;
	int err;

	if (dev >= SNDRV_CARDS)
		return -ENODEV;
	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	err = snd_card_create(index[dev], id[dev], THIS_MODULE,
			      sizeof(struct snd_rme9652), &card);

	if (err < 0)
		return err;

	rme9652 = (struct snd_rme9652 *) card->private_data;
	card->private_free = snd_rme9652_card_free;
	rme9652->dev = dev;
	rme9652->pci = pci;
	snd_card_set_dev(card, &pci->dev);

	if ((err = snd_rme9652_create(card, rme9652, precise_ptr[dev])) < 0) {
		snd_card_free(card);
		return err;
	}

	strcpy(card->shortname, rme9652->card_name);

	sprintf(card->longname, "%s at 0x%lx, irq %d",
		card->shortname, rme9652->port, rme9652->irq);

	
	if ((err = snd_card_register(card)) < 0) {
		snd_card_free(card);
		return err;
	}
	pci_set_drvdata(pci, card);
	dev++;
	return 0;
}

static void __devexit snd_rme9652_remove(struct pci_dev *pci)
{
	snd_card_free(pci_get_drvdata(pci));
	pci_set_drvdata(pci, NULL);
}

static struct pci_driver driver = {
	.name	  = KBUILD_MODNAME,
	.id_table = snd_rme9652_ids,
	.probe	  = snd_rme9652_probe,
	.remove	  = __devexit_p(snd_rme9652_remove),
};

static int __init alsa_card_hammerfall_init(void)
{
	return pci_register_driver(&driver);
}

static void __exit alsa_card_hammerfall_exit(void)
{
	pci_unregister_driver(&driver);
}

module_init(alsa_card_hammerfall_init)
module_exit(alsa_card_hammerfall_exit)
