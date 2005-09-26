/*
 *      au1000.c  --  Sound driver for Alchemy Au1000 MIPS Internet Edge
 *                    Processor.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 * Module command line parameters:
 *
 *  Supported devices:
 *  /dev/dsp    standard OSS /dev/dsp device
 *  /dev/mixer  standard OSS /dev/mixer device
 *
 * Notes:
 *
 *  1. Much of the OSS buffer allocation, ioctl's, and mmap'ing are
 *     taken, slightly modified or not at all, from the ES1371 driver,
 *     so refer to the credits in es1371.c for those. The rest of the
 *     code (probe, open, read, write, the ISR, etc.) is new.
 *
 *  Revision history
 *    06.27.2001  Initial version
 *    03.20.2002  Added mutex locks around read/write methods, to prevent
 *                simultaneous access on SMP or preemptible kernels. Also
 *                removed the counter/pointer fragment aligning at the end
 *                of read/write methods [stevel].
 *    03.21.2002  Add support for coherent DMA on the audio read/write DMA
 *                channels [stevel].
 *
 */
#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/init.h>
#include <linux/page-flags.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>

/* --------------------------------------------------------------------- */

#undef OSS_DOCUMENTED_MIXER_SEMANTICS
#undef AU1000_DEBUG
#undef AU1000_VERBOSE_DEBUG

#define AU1000_MODULE_NAME "Au1000 audio"
#define PFX AU1000_MODULE_NAME

#ifdef AU1000_DEBUG
#define dbg(format, arg...) printk(KERN_DEBUG PFX ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) printk(KERN_ERR PFX ": " format "\n" , ## arg)
#define info(format, arg...) printk(KERN_INFO PFX ": " format "\n" , ## arg)
#define warn(format, arg...) printk(KERN_WARNING PFX ": " format "\n" , ## arg)


/* misc stuff */
#define POLL_COUNT   0x5000
#define AC97_EXT_DACS (AC97_EXTID_SDAC | AC97_EXTID_CDAC | AC97_EXTID_LDAC)

/* Boot options */
static int      vra = 0;	// 0 = no VRA, 1 = use VRA if codec supports it
MODULE_PARM(vra, "i");
MODULE_PARM_DESC(vra, "if 1 use VRA if codec supports it");


/* --------------------------------------------------------------------- */

struct au1000_state {
	/* soundcore stuff */
	int             dev_audio;

#ifdef AU1000_DEBUG
	/* debug /proc entry */
	struct proc_dir_entry *ps;
	struct proc_dir_entry *ac97_ps;
#endif				/* AU1000_DEBUG */

	struct ac97_codec codec;
	unsigned        codec_base_caps;// AC'97 reg 00h, "Reset Register"
	unsigned        codec_ext_caps;	// AC'97 reg 28h, "Extended Audio ID"
	int             no_vra;	// do not use VRA

	spinlock_t      lock;
	struct semaphore open_sem;
	struct semaphore sem;
	mode_t          open_mode;
	wait_queue_head_t open_wait;

	struct dmabuf {
		unsigned int    dmanr;	// DMA Channel number
		unsigned        sample_rate;	// Hz
		unsigned src_factor;     // SRC interp/decimation (no vra)
		unsigned        sample_size;	// 8 or 16
		int             num_channels;	// 1 = mono, 2 = stereo, 4, 6
		int dma_bytes_per_sample;// DMA bytes per audio sample frame
		int user_bytes_per_sample;// User bytes per audio sample frame
		int cnt_factor;          // user-to-DMA bytes per audio
		//  sample frame
		void           *rawbuf;
		dma_addr_t      dmaaddr;
		unsigned        buforder;
		unsigned numfrag;        // # of DMA fragments in DMA buffer
		unsigned        fragshift;
		void           *nextIn;	// ptr to next-in to DMA buffer
		void           *nextOut;// ptr to next-out from DMA buffer
		int             count;	// current byte count in DMA buffer
		unsigned        total_bytes;	// total bytes written or read
		unsigned        error;	// over/underrun
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;       // user perception of fragment size
		unsigned dma_fragsize;   // DMA (real) fragment size
		unsigned dmasize;        // Total DMA buffer size
		//   (mult. of DMA fragsize)
		/* OSS stuff */
		unsigned        mapped:1;
		unsigned        ready:1;
		unsigned        stopped:1;
		unsigned        ossfragshift;
		int             ossmaxfrags;
		unsigned        subdivision;
	} dma_dac      , dma_adc;
} au1000_state;

/* --------------------------------------------------------------------- */


static inline unsigned ld2(unsigned int x)
{
	unsigned        r = 0;

	if (x >= 0x10000) {
		x >>= 16;
		r += 16;
	}
	if (x >= 0x100) {
		x >>= 8;
		r += 8;
	}
	if (x >= 0x10) {
		x >>= 4;
		r += 4;
	}
	if (x >= 4) {
		x >>= 2;
		r += 2;
	}
	if (x >= 2)
		r++;
	return r;
}

/* --------------------------------------------------------------------- */

static void au1000_delay(int msec)
{
	unsigned long   tmo;
	signed long     tmo2;

	if (in_interrupt())
		return;

	tmo = jiffies + (msec * HZ) / 1000;
	for (;;) {
		tmo2 = tmo - jiffies;
		if (tmo2 <= 0)
			break;
		schedule_timeout(tmo2);
	}
}


/* --------------------------------------------------------------------- */

static u16 rdcodec(struct ac97_codec *codec, u8 addr)
{
	struct au1000_state *s = (struct au1000_state *)codec->private_data;
	unsigned long   flags;
	u32             cmd;
	u16             data;
	int             i;

	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++)
		if (!(au_readl(AC97C_STATUS) & AC97C_CP))
			break;
	if (i == POLL_COUNT)
		err("rdcodec: codec cmd pending expired!");

	cmd = (u32) addr & AC97C_INDEX_MASK;
	cmd |= AC97C_READ;	// read command
	au_writel(cmd, AC97C_CMD);

	/* now wait for the data */
	for (i = 0; i < POLL_COUNT; i++)
		if (!(au_readl(AC97C_STATUS) & AC97C_CP))
			break;
	if (i == POLL_COUNT) {
		err("rdcodec: read poll expired!");
		return 0;
	}

	data = au_readl(AC97C_CMD) & 0xffff;

	spin_unlock_irqrestore(&s->lock, flags);

	return data;
}


static void wrcodec(struct ac97_codec *codec, u8 addr, u16 data)
{
	struct au1000_state *s = (struct au1000_state *)codec->private_data;
	unsigned long   flags;
	u32             cmd;
	int             i;

	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++)
		if (!(au_readl(AC97C_STATUS) & AC97C_CP))
			break;
	if (i == POLL_COUNT)
		err("wrcodec: codec cmd pending expired!");

	cmd = (u32) addr & AC97C_INDEX_MASK;
	cmd &= ~AC97C_READ;	// write command
	cmd |= ((u32) data << AC97C_WD_BIT);	// OR in the data word
	au_writel(cmd, AC97C_CMD);

	spin_unlock_irqrestore(&s->lock, flags);
}

static void waitcodec(struct ac97_codec *codec)
{
	u16             temp;
	int             i;

	/* codec_wait is used to wait for a ready state after
	   an AC97C_RESET. */
	au1000_delay(10);

	// first poll the CODEC_READY tag bit
	for (i = 0; i < POLL_COUNT; i++)
		if (au_readl(AC97C_STATUS) & AC97C_READY)
			break;
	if (i == POLL_COUNT) {
		err("waitcodec: CODEC_READY poll expired!");
		return;
	}
	// get AC'97 powerdown control/status register
	temp = rdcodec(codec, AC97_POWER_CONTROL);

	// If anything is powered down, power'em up
	if (temp & 0x7f00) {
		// Power on
		wrcodec(codec, AC97_POWER_CONTROL, 0);
		au1000_delay(100);
		// Reread
		temp = rdcodec(codec, AC97_POWER_CONTROL);
	}
    
	// Check if Codec REF,ANL,DAC,ADC ready
	if ((temp & 0x7f0f) != 0x000f)
		err("codec reg 26 status (0x%x) not ready!!", temp);
}


/* --------------------------------------------------------------------- */

/* stop the ADC before calling */
static void set_adc_rate(struct au1000_state *s, unsigned rate)
{
	struct dmabuf  *adc = &s->dma_adc;
	struct dmabuf  *dac = &s->dma_dac;
	unsigned        adc_rate, dac_rate;
	u16             ac97_extstat;

	if (s->no_vra) {
		// calc SRC factor
		adc->src_factor = ((96000 / rate) + 1) >> 1;
		adc->sample_rate = 48000 / adc->src_factor;
		return;
	}

	adc->src_factor = 1;

	ac97_extstat = rdcodec(&s->codec, AC97_EXTENDED_STATUS);

	rate = rate > 48000 ? 48000 : rate;

	// enable VRA
	wrcodec(&s->codec, AC97_EXTENDED_STATUS,
		ac97_extstat | AC97_EXTSTAT_VRA);
	// now write the sample rate
	wrcodec(&s->codec, AC97_PCM_LR_ADC_RATE, (u16) rate);
	// read it back for actual supported rate
	adc_rate = rdcodec(&s->codec, AC97_PCM_LR_ADC_RATE);

#ifdef AU1000_VERBOSE_DEBUG
	dbg("%s: set to %d Hz", __FUNCTION__, adc_rate);
#endif

	// some codec's don't allow unequal DAC and ADC rates, in which case
	// writing one rate reg actually changes both.
	dac_rate = rdcodec(&s->codec, AC97_PCM_FRONT_DAC_RATE);
	if (dac->num_channels > 2)
		wrcodec(&s->codec, AC97_PCM_SURR_DAC_RATE, dac_rate);
	if (dac->num_channels > 4)
		wrcodec(&s->codec, AC97_PCM_LFE_DAC_RATE, dac_rate);

	adc->sample_rate = adc_rate;
	dac->sample_rate = dac_rate;
}

/* stop the DAC before calling */
static void set_dac_rate(struct au1000_state *s, unsigned rate)
{
	struct dmabuf  *dac = &s->dma_dac;
	struct dmabuf  *adc = &s->dma_adc;
	unsigned        adc_rate, dac_rate;
	u16             ac97_extstat;

	if (s->no_vra) {
		// calc SRC factor
		dac->src_factor = ((96000 / rate) + 1) >> 1;
		dac->sample_rate = 48000 / dac->src_factor;
		return;
	}

	dac->src_factor = 1;

	ac97_extstat = rdcodec(&s->codec, AC97_EXTENDED_STATUS);

	rate = rate > 48000 ? 48000 : rate;

	// enable VRA
	wrcodec(&s->codec, AC97_EXTENDED_STATUS,
		ac97_extstat | AC97_EXTSTAT_VRA);
	// now write the sample rate
	wrcodec(&s->codec, AC97_PCM_FRONT_DAC_RATE, (u16) rate);
	// I don't support different sample rates for multichannel,
	// so make these channels the same.
	if (dac->num_channels > 2)
		wrcodec(&s->codec, AC97_PCM_SURR_DAC_RATE, (u16) rate);
	if (dac->num_channels > 4)
		wrcodec(&s->codec, AC97_PCM_LFE_DAC_RATE, (u16) rate);
	// read it back for actual supported rate
	dac_rate = rdcodec(&s->codec, AC97_PCM_FRONT_DAC_RATE);

#ifdef AU1000_VERBOSE_DEBUG
	dbg("%s: set to %d Hz", __FUNCTION__, dac_rate);
#endif

	// some codec's don't allow unequal DAC and ADC rates, in which case
	// writing one rate reg actually changes both.
	adc_rate = rdcodec(&s->codec, AC97_PCM_LR_ADC_RATE);

	dac->sample_rate = dac_rate;
	adc->sample_rate = adc_rate;
}

static void stop_dac(struct au1000_state *s)
{
	struct dmabuf  *db = &s->dma_dac;
	unsigned long   flags;

	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	disable_dma(db->dmanr);

	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}

static void  stop_adc(struct au1000_state *s)
{
	struct dmabuf  *db = &s->dma_adc;
	unsigned long   flags;

	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	disable_dma(db->dmanr);

	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}


static void set_xmit_slots(int num_channels)
{
	u32 ac97_config = au_readl(AC97C_CONFIG) & ~AC97C_XMIT_SLOTS_MASK;

	switch (num_channels) {
	case 1:		// mono
	case 2:		// stereo, slots 3,4
		ac97_config |= (0x3 << AC97C_XMIT_SLOTS_BIT);
		break;
	case 4:		// stereo with surround, slots 3,4,7,8
		ac97_config |= (0x33 << AC97C_XMIT_SLOTS_BIT);
		break;
	case 6:		// stereo with surround and center/LFE, slots 3,4,6,7,8,9
		ac97_config |= (0x7b << AC97C_XMIT_SLOTS_BIT);
		break;
	}

	au_writel(ac97_config, AC97C_CONFIG);
}

static void     set_recv_slots(int num_channels)
{
	u32 ac97_config = au_readl(AC97C_CONFIG) & ~AC97C_RECV_SLOTS_MASK;

	/*
	 * Always enable slots 3 and 4 (stereo). Slot 6 is
	 * optional Mic ADC, which I don't support yet.
	 */
	ac97_config |= (0x3 << AC97C_RECV_SLOTS_BIT);

	au_writel(ac97_config, AC97C_CONFIG);
}

static void start_dac(struct au1000_state *s)
{
	struct dmabuf  *db = &s->dma_dac;
	unsigned long   flags;
	unsigned long   buf1, buf2;

	if (!db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	au_readl(AC97C_STATUS);	// read status to clear sticky bits

	// reset Buffer 1 and 2 pointers to nextOut and nextOut+dma_fragsize
	buf1 = virt_to_phys(db->nextOut);
	buf2 = buf1 + db->dma_fragsize;
	if (buf2 >= db->dmaaddr + db->dmasize)
		buf2 -= db->dmasize;

	set_xmit_slots(db->num_channels);

	init_dma(db->dmanr);
	if (get_dma_active_buffer(db->dmanr) == 0) {
		clear_dma_done0(db->dmanr);	// clear DMA done bit
		set_dma_addr0(db->dmanr, buf1);
		set_dma_addr1(db->dmanr, buf2);
	} else {
		clear_dma_done1(db->dmanr);	// clear DMA done bit
		set_dma_addr1(db->dmanr, buf1);
		set_dma_addr0(db->dmanr, buf2);
	}
	set_dma_count(db->dmanr, db->dma_fragsize>>1);
	enable_dma_buffers(db->dmanr);

	start_dma(db->dmanr);

#ifdef AU1000_VERBOSE_DEBUG
	dump_au1000_dma_channel(db->dmanr);
#endif

	db->stopped = 0;

	spin_unlock_irqrestore(&s->lock, flags);
}

static void start_adc(struct au1000_state *s)
{
	struct dmabuf  *db = &s->dma_adc;
	unsigned long   flags;
	unsigned long   buf1, buf2;

	if (!db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	au_readl(AC97C_STATUS);	// read status to clear sticky bits

	// reset Buffer 1 and 2 pointers to nextIn and nextIn+dma_fragsize
	buf1 = virt_to_phys(db->nextIn);
	buf2 = buf1 + db->dma_fragsize;
	if (buf2 >= db->dmaaddr + db->dmasize)
		buf2 -= db->dmasize;

	set_recv_slots(db->num_channels);

	init_dma(db->dmanr);
	if (get_dma_active_buffer(db->dmanr) == 0) {
		clear_dma_done0(db->dmanr);	// clear DMA done bit
		set_dma_addr0(db->dmanr, buf1);
		set_dma_addr1(db->dmanr, buf2);
	} else {
		clear_dma_done1(db->dmanr);	// clear DMA done bit
		set_dma_addr1(db->dmanr, buf1);
		set_dma_addr0(db->dmanr, buf2);
	}
	set_dma_count(db->dmanr, db->dma_fragsize>>1);
	enable_dma_buffers(db->dmanr);

	start_dma(db->dmanr);

#ifdef AU1000_VERBOSE_DEBUG
	dump_au1000_dma_channel(db->dmanr);
#endif

	db->stopped = 0;

	spin_unlock_irqrestore(&s->lock, flags);
}

/* --------------------------------------------------------------------- */

#define DMABUF_DEFAULTORDER (17-PAGE_SHIFT)
#define DMABUF_MINORDER 1

extern inline void dealloc_dmabuf(struct au1000_state *s, struct dmabuf *db)
{
	struct page    *page, *pend;

	if (db->rawbuf) {
		/* undo marking the pages as reserved */
		pend = virt_to_page(db->rawbuf +
				    (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			ClearPageReserved(page);
		dma_free_noncoherent(NULL,
				PAGE_SIZE << db->buforder,
				db->rawbuf,
				db->dmaaddr);
	}
	db->rawbuf = db->nextIn = db->nextOut = NULL;
	db->mapped = db->ready = 0;
}

static int prog_dmabuf(struct au1000_state *s, struct dmabuf *db)
{
	int             order;
	unsigned user_bytes_per_sec;
	unsigned        bufs;
	struct page    *page, *pend;
	unsigned        rate = db->sample_rate;

	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		for (order = DMABUF_DEFAULTORDER;
		     order >= DMABUF_MINORDER; order--)
			if ((db->rawbuf = dma_alloc_noncoherent(NULL,
						PAGE_SIZE << order,
						&db->dmaaddr,
						0)))
				break;
		if (!db->rawbuf)
			return -ENOMEM;
		db->buforder = order;
		/* now mark the pages as reserved;
		   otherwise remap_pfn_range doesn't do what we want */
		pend = virt_to_page(db->rawbuf +
				    (PAGE_SIZE << db->buforder) - 1);
		for (page = virt_to_page(db->rawbuf); page <= pend; page++)
			SetPageReserved(page);
	}

	db->cnt_factor = 1;
	if (db->sample_size == 8)
		db->cnt_factor *= 2;
	if (db->num_channels == 1)
		db->cnt_factor *= 2;
	db->cnt_factor *= db->src_factor;

	db->count = 0;
	db->nextIn = db->nextOut = db->rawbuf;

	db->user_bytes_per_sample = (db->sample_size>>3) * db->num_channels;
	db->dma_bytes_per_sample = 2 * ((db->num_channels == 1) ?
					2 : db->num_channels);

	user_bytes_per_sec = rate * db->user_bytes_per_sample;
	bufs = PAGE_SIZE << db->buforder;
	if (db->ossfragshift) {
		if ((1000 << db->ossfragshift) < user_bytes_per_sec)
			db->fragshift = ld2(user_bytes_per_sec/1000);
		else
			db->fragshift = db->ossfragshift;
	} else {
		db->fragshift = ld2(user_bytes_per_sec / 100 /
				    (db->subdivision ? db->subdivision : 1));
		if (db->fragshift < 3)
			db->fragshift = 3;
	}

	db->fragsize = 1 << db->fragshift;
	db->dma_fragsize = db->fragsize * db->cnt_factor;
	db->numfrag = bufs / db->dma_fragsize;

	while (db->numfrag < 4 && db->fragshift > 3) {
		db->fragshift--;
		db->fragsize = 1 << db->fragshift;
		db->dma_fragsize = db->fragsize * db->cnt_factor;
		db->numfrag = bufs / db->dma_fragsize;
	}

	if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
		db->numfrag = db->ossmaxfrags;

	db->dmasize = db->dma_fragsize * db->numfrag;
	memset(db->rawbuf, 0, bufs);

#ifdef AU1000_VERBOSE_DEBUG
	dbg("rate=%d, samplesize=%d, channels=%d",
	    rate, db->sample_size, db->num_channels);
	dbg("fragsize=%d, cnt_factor=%d, dma_fragsize=%d",
	    db->fragsize, db->cnt_factor, db->dma_fragsize);
	dbg("numfrag=%d, dmasize=%d", db->numfrag, db->dmasize);
#endif

	db->ready = 1;
	return 0;
}

extern inline int prog_dmabuf_adc(struct au1000_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc);

}

extern inline int prog_dmabuf_dac(struct au1000_state *s)
{
	stop_dac(s);
	return prog_dmabuf(s, &s->dma_dac);
}


/* hold spinlock for the following */
static irqreturn_t dac_dma_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct au1000_state *s = (struct au1000_state *) dev_id;
	struct dmabuf  *dac = &s->dma_dac;
	unsigned long   newptr;
	u32 ac97c_stat, buff_done;

	ac97c_stat = au_readl(AC97C_STATUS);
#ifdef AU1000_VERBOSE_DEBUG
	if (ac97c_stat & (AC97C_XU | AC97C_XO | AC97C_TE))
		dbg("AC97C status = 0x%08x", ac97c_stat);
#endif

	if ((buff_done = get_dma_buffer_done(dac->dmanr)) == 0) {
		/* fastpath out, to ease interrupt sharing */
		return IRQ_HANDLED;
	}

	spin_lock(&s->lock);
	
	if (buff_done != (DMA_D0 | DMA_D1)) {
		dac->nextOut += dac->dma_fragsize;
		if (dac->nextOut >= dac->rawbuf + dac->dmasize)
			dac->nextOut -= dac->dmasize;

		/* update playback pointers */
		newptr = virt_to_phys(dac->nextOut) + dac->dma_fragsize;
		if (newptr >= dac->dmaaddr + dac->dmasize)
			newptr -= dac->dmasize;

		dac->count -= dac->dma_fragsize;
		dac->total_bytes += dac->dma_fragsize;

		if (dac->count <= 0) {
#ifdef AU1000_VERBOSE_DEBUG
			dbg("dac underrun");
#endif
			spin_unlock(&s->lock);
			stop_dac(s);
			spin_lock(&s->lock);
			dac->count = 0;
			dac->nextIn = dac->nextOut;
		} else if (buff_done == DMA_D0) {
			clear_dma_done0(dac->dmanr);	// clear DMA done bit
			set_dma_count0(dac->dmanr, dac->dma_fragsize>>1);
			set_dma_addr0(dac->dmanr, newptr);
			enable_dma_buffer0(dac->dmanr);	// reenable
		} else {
			clear_dma_done1(dac->dmanr);	// clear DMA done bit
			set_dma_count1(dac->dmanr, dac->dma_fragsize>>1);
			set_dma_addr1(dac->dmanr, newptr);
			enable_dma_buffer1(dac->dmanr);	// reenable
		}
	} else {
		// both done bits set, we missed an interrupt
		spin_unlock(&s->lock);
		stop_dac(s);
		spin_lock(&s->lock);

		dac->nextOut += 2*dac->dma_fragsize;
		if (dac->nextOut >= dac->rawbuf + dac->dmasize)
			dac->nextOut -= dac->dmasize;

		dac->count -= 2*dac->dma_fragsize;
		dac->total_bytes += 2*dac->dma_fragsize;

		if (dac->count > 0) {
			spin_unlock(&s->lock);
			start_dac(s);
			spin_lock(&s->lock);
		}
	}

	/* wake up anybody listening */
	if (waitqueue_active(&dac->wait))
		wake_up(&dac->wait);

	spin_unlock(&s->lock);

	return IRQ_HANDLED;
}


static irqreturn_t adc_dma_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct au1000_state *s = (struct au1000_state *) dev_id;
	struct dmabuf  *adc = &s->dma_adc;
	unsigned long   newptr;
	u32 ac97c_stat, buff_done;

	ac97c_stat = au_readl(AC97C_STATUS);
#ifdef AU1000_VERBOSE_DEBUG
	if (ac97c_stat & (AC97C_RU | AC97C_RO))
		dbg("AC97C status = 0x%08x", ac97c_stat);
#endif

	if ((buff_done = get_dma_buffer_done(adc->dmanr)) == 0) {
		/* fastpath out, to ease interrupt sharing */
		return IRQ_HANDLED;
	}

	spin_lock(&s->lock);
	
	if (buff_done != (DMA_D0 | DMA_D1)) {
		if (adc->count + adc->dma_fragsize > adc->dmasize) {
			// Overrun. Stop ADC and log the error
			spin_unlock(&s->lock);
			stop_adc(s);
			adc->error++;
			err("adc overrun");
			return IRQ_NONE;
		}

		adc->nextIn += adc->dma_fragsize;
		if (adc->nextIn >= adc->rawbuf + adc->dmasize)
			adc->nextIn -= adc->dmasize;

		/* update capture pointers */
		newptr = virt_to_phys(adc->nextIn) + adc->dma_fragsize;
		if (newptr >= adc->dmaaddr + adc->dmasize)
			newptr -= adc->dmasize;

		adc->count += adc->dma_fragsize;
		adc->total_bytes += adc->dma_fragsize;

		if (buff_done == DMA_D0) {
			clear_dma_done0(adc->dmanr);	// clear DMA done bit
			set_dma_count0(adc->dmanr, adc->dma_fragsize>>1);
			set_dma_addr0(adc->dmanr, newptr);
			enable_dma_buffer0(adc->dmanr);	// reenable
		} else {
			clear_dma_done1(adc->dmanr);	// clear DMA done bit
			set_dma_count1(adc->dmanr, adc->dma_fragsize>>1);
			set_dma_addr1(adc->dmanr, newptr);
			enable_dma_buffer1(adc->dmanr);	// reenable
		}
	} else {
		// both done bits set, we missed an interrupt
		spin_unlock(&s->lock);
		stop_adc(s);
		spin_lock(&s->lock);
		
		if (adc->count + 2*adc->dma_fragsize > adc->dmasize) {
			// Overrun. Log the error
			adc->error++;
			err("adc overrun");
			spin_unlock(&s->lock);
			return IRQ_NONE;
		}

		adc->nextIn += 2*adc->dma_fragsize;
		if (adc->nextIn >= adc->rawbuf + adc->dmasize)
			adc->nextIn -= adc->dmasize;

		adc->count += 2*adc->dma_fragsize;
		adc->total_bytes += 2*adc->dma_fragsize;
		
		spin_unlock(&s->lock);
		start_adc(s);
		spin_lock(&s->lock);
	}

	/* wake up anybody listening */
	if (waitqueue_active(&adc->wait))
		wake_up(&adc->wait);

	spin_unlock(&s->lock);

	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------- */

static loff_t au1000_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}


static int au1000_open_mixdev(struct inode *inode, struct file *file)
{
	file->private_data = &au1000_state;
	return nonseekable_open(inode, file);
}

static int au1000_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}

static int mixdev_ioctl(struct ac97_codec *codec, unsigned int cmd,
                        unsigned long arg)
{
	return codec->mixer_ioctl(codec, cmd, arg);
}

static int au1000_ioctl_mixdev(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	struct ac97_codec *codec = &s->codec;

	return mixdev_ioctl(codec, cmd, arg);
}

static /*const */ struct file_operations au1000_mixer_fops = {
	.owner		= THIS_MODULE,
	.llseek		= au1000_llseek,
	.ioctl		= au1000_ioctl_mixdev,
	.open		= au1000_open_mixdev,
	.release	= au1000_release_mixdev,
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct au1000_state *s, int nonblock)
{
	unsigned long   flags;
	int             count, tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready || s->dma_dac.stopped)
		return 0;

	for (;;) {
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
			break;
		if (nonblock)
			return -EBUSY;
		tmo = 1000 * count / (s->no_vra ?
				      48000 : s->dma_dac.sample_rate);
		tmo /= s->dma_dac.dma_bytes_per_sample;
		au1000_delay(tmo);
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

/* --------------------------------------------------------------------- */

static inline u8 S16_TO_U8(s16 ch)
{
	return (u8) (ch >> 8) + 0x80;
}
static inline s16 U8_TO_S16(u8 ch)
{
	return (s16) (ch - 0x80) << 8;
}

/*
 * Translates user samples to dma buffer suitable for AC'97 DAC data:
 *     If mono, copy left channel to right channel in dma buffer.
 *     If 8 bit samples, cvt to 16-bit before writing to dma buffer.
 *     If interpolating (no VRA), duplicate every audio frame src_factor times.
 */
static int translate_from_user(struct dmabuf *db,
			       char* dmabuf,
			       char* userbuf,
			       int dmacount)
{
	int             sample, i;
	int             interp_bytes_per_sample;
	int             num_samples;
	int             mono = (db->num_channels == 1);
	char            usersample[12];
	s16             ch, dmasample[6];

	if (db->sample_size == 16 && !mono && db->src_factor == 1) {
		// no translation necessary, just copy
		if (copy_from_user(dmabuf, userbuf, dmacount))
			return -EFAULT;
		return dmacount;
	}

	interp_bytes_per_sample = db->dma_bytes_per_sample * db->src_factor;
	num_samples = dmacount / interp_bytes_per_sample;

	for (sample = 0; sample < num_samples; sample++) {
		if (copy_from_user(usersample, userbuf,
				   db->user_bytes_per_sample)) {
			dbg("%s: fault", __FUNCTION__);
			return -EFAULT;
		}

		for (i = 0; i < db->num_channels; i++) {
			if (db->sample_size == 8)
				ch = U8_TO_S16(usersample[i]);
			else
				ch = *((s16 *) (&usersample[i * 2]));
			dmasample[i] = ch;
			if (mono)
				dmasample[i + 1] = ch;	// right channel
		}

		// duplicate every audio frame src_factor times
		for (i = 0; i < db->src_factor; i++)
			memcpy(dmabuf, dmasample, db->dma_bytes_per_sample);

		userbuf += db->user_bytes_per_sample;
		dmabuf += interp_bytes_per_sample;
	}

	return num_samples * interp_bytes_per_sample;
}

/*
 * Translates AC'97 ADC samples to user buffer:
 *     If mono, send only left channel to user buffer.
 *     If 8 bit samples, cvt from 16 to 8 bit before writing to user buffer.
 *     If decimating (no VRA), skip over src_factor audio frames.
 */
static int translate_to_user(struct dmabuf *db,
			     char* userbuf,
			     char* dmabuf,
			     int dmacount)
{
	int             sample, i;
	int             interp_bytes_per_sample;
	int             num_samples;
	int             mono = (db->num_channels == 1);
	char            usersample[12];

	if (db->sample_size == 16 && !mono && db->src_factor == 1) {
		// no translation necessary, just copy
		if (copy_to_user(userbuf, dmabuf, dmacount))
			return -EFAULT;
		return dmacount;
	}

	interp_bytes_per_sample = db->dma_bytes_per_sample * db->src_factor;
	num_samples = dmacount / interp_bytes_per_sample;

	for (sample = 0; sample < num_samples; sample++) {
		for (i = 0; i < db->num_channels; i++) {
			if (db->sample_size == 8)
				usersample[i] =
					S16_TO_U8(*((s16 *) (&dmabuf[i * 2])));
			else
				*((s16 *) (&usersample[i * 2])) =
					*((s16 *) (&dmabuf[i * 2]));
		}

		if (copy_to_user(userbuf, usersample,
				 db->user_bytes_per_sample)) {
			dbg("%s: fault", __FUNCTION__);
			return -EFAULT;
		}

		userbuf += db->user_bytes_per_sample;
		dmabuf += interp_bytes_per_sample;
	}

	return num_samples * interp_bytes_per_sample;
}

/*
 * Copy audio data to/from user buffer from/to dma buffer, taking care
 * that we wrap when reading/writing the dma buffer. Returns actual byte
 * count written to or read from the dma buffer.
 */
static int copy_dmabuf_user(struct dmabuf *db, char* userbuf,
			    int count, int to_user)
{
	char           *bufptr = to_user ? db->nextOut : db->nextIn;
	char           *bufend = db->rawbuf + db->dmasize;
	int             cnt, ret;

	if (bufptr + count > bufend) {
		int             partial = (int) (bufend - bufptr);
		if (to_user) {
			if ((cnt = translate_to_user(db, userbuf,
						     bufptr, partial)) < 0)
				return cnt;
			ret = cnt;
			if ((cnt = translate_to_user(db, userbuf + partial,
						     db->rawbuf,
						     count - partial)) < 0)
				return cnt;
			ret += cnt;
		} else {
			if ((cnt = translate_from_user(db, bufptr, userbuf,
						       partial)) < 0)
				return cnt;
			ret = cnt;
			if ((cnt = translate_from_user(db, db->rawbuf,
						       userbuf + partial,
						       count - partial)) < 0)
				return cnt;
			ret += cnt;
		}
	} else {
		if (to_user)
			ret = translate_to_user(db, userbuf, bufptr, count);
		else
			ret = translate_from_user(db, bufptr, userbuf, count);
	}

	return ret;
}


static ssize_t au1000_read(struct file *file, char *buffer,
			   size_t count, loff_t *ppos)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	struct dmabuf  *db = &s->dma_adc;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t         ret;
	unsigned long   flags;
	int             cnt, usercnt, avail;

	if (db->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	ret = 0;

	count *= db->cnt_factor;

	down(&s->sem);
	add_wait_queue(&db->wait, &wait);

	while (count > 0) {
		// wait for samples in ADC dma buffer
		do {
			if (db->stopped)
				start_adc(s);
			spin_lock_irqsave(&s->lock, flags);
			avail = db->count;
			if (avail <= 0)
				__set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&s->lock, flags);
			if (avail <= 0) {
				if (file->f_flags & O_NONBLOCK) {
					if (!ret)
						ret = -EAGAIN;
					goto out;
				}
				up(&s->sem);
				schedule();
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					goto out2;
				}
				down(&s->sem);
			}
		} while (avail <= 0);

		// copy from nextOut to user
		if ((cnt = copy_dmabuf_user(db, buffer,
					    count > avail ?
					    avail : count, 1)) < 0) {
			if (!ret)
				ret = -EFAULT;
			goto out;
		}

		spin_lock_irqsave(&s->lock, flags);
		db->count -= cnt;
		db->nextOut += cnt;
		if (db->nextOut >= db->rawbuf + db->dmasize)
			db->nextOut -= db->dmasize;
		spin_unlock_irqrestore(&s->lock, flags);

		count -= cnt;
		usercnt = cnt / db->cnt_factor;
		buffer += usercnt;
		ret += usercnt;
	}			// while (count > 0)

out:
	up(&s->sem);
out2:
	remove_wait_queue(&db->wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t au1000_write(struct file *file, const char *buffer,
	     		    size_t count, loff_t * ppos)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	struct dmabuf  *db = &s->dma_dac;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t         ret = 0;
	unsigned long   flags;
	int             cnt, usercnt, avail;

#ifdef AU1000_VERBOSE_DEBUG
	dbg("write: count=%d", count);
#endif

	if (db->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	count *= db->cnt_factor;

	down(&s->sem);	
	add_wait_queue(&db->wait, &wait);

	while (count > 0) {
		// wait for space in playback buffer
		do {
			spin_lock_irqsave(&s->lock, flags);
			avail = (int) db->dmasize - db->count;
			if (avail <= 0)
				__set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irqrestore(&s->lock, flags);
			if (avail <= 0) {
				if (file->f_flags & O_NONBLOCK) {
					if (!ret)
						ret = -EAGAIN;
					goto out;
				}
				up(&s->sem);
				schedule();
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					goto out2;
				}
				down(&s->sem);
			}
		} while (avail <= 0);

		// copy from user to nextIn
		if ((cnt = copy_dmabuf_user(db, (char *) buffer,
					    count > avail ?
					    avail : count, 0)) < 0) {
			if (!ret)
				ret = -EFAULT;
			goto out;
		}

		spin_lock_irqsave(&s->lock, flags);
		db->count += cnt;
		db->nextIn += cnt;
		if (db->nextIn >= db->rawbuf + db->dmasize)
			db->nextIn -= db->dmasize;
		spin_unlock_irqrestore(&s->lock, flags);
		if (db->stopped)
			start_dac(s);

		count -= cnt;
		usercnt = cnt / db->cnt_factor;
		buffer += usercnt;
		ret += usercnt;
	}			// while (count > 0)

out:
	up(&s->sem);
out2:
	remove_wait_queue(&db->wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}


/* No kernel lock - we have our own spinlock */
static unsigned int au1000_poll(struct file *file,
				struct poll_table_struct *wait)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	unsigned long   flags;
	unsigned int    mask = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (!s->dma_dac.ready)
			return 0;
		poll_wait(file, &s->dma_dac.wait, wait);
	}
	if (file->f_mode & FMODE_READ) {
		if (!s->dma_adc.ready)
			return 0;
		poll_wait(file, &s->dma_adc.wait, wait);
	}

	spin_lock_irqsave(&s->lock, flags);
	
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.dma_fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >=
			    (signed)s->dma_dac.dma_fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed) s->dma_dac.dmasize >=
			    s->dma_dac.count + (signed)s->dma_dac.dma_fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

static int au1000_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	struct dmabuf  *db;
	unsigned long   size;
	int ret = 0;

	dbg("%s", __FUNCTION__);
    
	lock_kernel();
	down(&s->sem);
	if (vma->vm_flags & VM_WRITE)
		db = &s->dma_dac;
	else if (vma->vm_flags & VM_READ)
		db = &s->dma_adc;
	else {
		ret = -EINVAL;
		goto out;
	}
	if (vma->vm_pgoff != 0) {
		ret = -EINVAL;
		goto out;
	}
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder)) {
		ret = -EINVAL;
		goto out;
	}
	if (remap_pfn_range(vma, vma->vm_start, virt_to_phys(db->rawbuf),
			     size, vma->vm_page_prot)) {
		ret = -EAGAIN;
		goto out;
	}
	vma->vm_flags &= ~VM_IO;
	db->mapped = 1;
out:
	up(&s->sem);
	unlock_kernel();
	return ret;
}


#ifdef AU1000_VERBOSE_DEBUG
static struct ioctl_str_t {
	unsigned int    cmd;
	const char     *str;
} ioctl_str[] = {
	{SNDCTL_DSP_RESET, "SNDCTL_DSP_RESET"},
	{SNDCTL_DSP_SYNC, "SNDCTL_DSP_SYNC"},
	{SNDCTL_DSP_SPEED, "SNDCTL_DSP_SPEED"},
	{SNDCTL_DSP_STEREO, "SNDCTL_DSP_STEREO"},
	{SNDCTL_DSP_GETBLKSIZE, "SNDCTL_DSP_GETBLKSIZE"},
	{SNDCTL_DSP_SAMPLESIZE, "SNDCTL_DSP_SAMPLESIZE"},
	{SNDCTL_DSP_CHANNELS, "SNDCTL_DSP_CHANNELS"},
	{SOUND_PCM_WRITE_CHANNELS, "SOUND_PCM_WRITE_CHANNELS"},
	{SOUND_PCM_WRITE_FILTER, "SOUND_PCM_WRITE_FILTER"},
	{SNDCTL_DSP_POST, "SNDCTL_DSP_POST"},
	{SNDCTL_DSP_SUBDIVIDE, "SNDCTL_DSP_SUBDIVIDE"},
	{SNDCTL_DSP_SETFRAGMENT, "SNDCTL_DSP_SETFRAGMENT"},
	{SNDCTL_DSP_GETFMTS, "SNDCTL_DSP_GETFMTS"},
	{SNDCTL_DSP_SETFMT, "SNDCTL_DSP_SETFMT"},
	{SNDCTL_DSP_GETOSPACE, "SNDCTL_DSP_GETOSPACE"},
	{SNDCTL_DSP_GETISPACE, "SNDCTL_DSP_GETISPACE"},
	{SNDCTL_DSP_NONBLOCK, "SNDCTL_DSP_NONBLOCK"},
	{SNDCTL_DSP_GETCAPS, "SNDCTL_DSP_GETCAPS"},
	{SNDCTL_DSP_GETTRIGGER, "SNDCTL_DSP_GETTRIGGER"},
	{SNDCTL_DSP_SETTRIGGER, "SNDCTL_DSP_SETTRIGGER"},
	{SNDCTL_DSP_GETIPTR, "SNDCTL_DSP_GETIPTR"},
	{SNDCTL_DSP_GETOPTR, "SNDCTL_DSP_GETOPTR"},
	{SNDCTL_DSP_MAPINBUF, "SNDCTL_DSP_MAPINBUF"},
	{SNDCTL_DSP_MAPOUTBUF, "SNDCTL_DSP_MAPOUTBUF"},
	{SNDCTL_DSP_SETSYNCRO, "SNDCTL_DSP_SETSYNCRO"},
	{SNDCTL_DSP_SETDUPLEX, "SNDCTL_DSP_SETDUPLEX"},
	{SNDCTL_DSP_GETODELAY, "SNDCTL_DSP_GETODELAY"},
	{SNDCTL_DSP_GETCHANNELMASK, "SNDCTL_DSP_GETCHANNELMASK"},
	{SNDCTL_DSP_BIND_CHANNEL, "SNDCTL_DSP_BIND_CHANNEL"},
	{OSS_GETVERSION, "OSS_GETVERSION"},
	{SOUND_PCM_READ_RATE, "SOUND_PCM_READ_RATE"},
	{SOUND_PCM_READ_CHANNELS, "SOUND_PCM_READ_CHANNELS"},
	{SOUND_PCM_READ_BITS, "SOUND_PCM_READ_BITS"},
	{SOUND_PCM_READ_FILTER, "SOUND_PCM_READ_FILTER"}
};
#endif

// Need to hold a spin-lock before calling this!
static int dma_count_done(struct dmabuf *db)
{
	if (db->stopped)
		return 0;

	return db->dma_fragsize - get_dma_residue(db->dmanr);
}


static int au1000_ioctl(struct inode *inode, struct file *file,
                        unsigned int cmd, unsigned long arg)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;
	unsigned long   flags;
	audio_buf_info  abinfo;
	count_info      cinfo;
	int             count;
	int             val, mapped, ret, diff;

	mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

#ifdef AU1000_VERBOSE_DEBUG
	for (count=0; count<sizeof(ioctl_str)/sizeof(ioctl_str[0]); count++) {
		if (ioctl_str[count].cmd == cmd)
			break;
	}
	if (count < sizeof(ioctl_str) / sizeof(ioctl_str[0]))
		dbg("ioctl %s, arg=0x%lx", ioctl_str[count].str, arg);
	else
		dbg("ioctl 0x%x unknown, arg=0x%lx", cmd, arg);
#endif

	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *) arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, file->f_flags & O_NONBLOCK);
		return 0;

	case SNDCTL_DSP_SETDUPLEX:
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME |
				DSP_CAP_TRIGGER | DSP_CAP_MMAP, (int *)arg);

	case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq();
			s->dma_dac.count = s->dma_dac.total_bytes = 0;
			s->dma_dac.nextIn = s->dma_dac.nextOut =
				s->dma_dac.rawbuf;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq();
			s->dma_adc.count = s->dma_adc.total_bytes = 0;
			s->dma_adc.nextIn = s->dma_adc.nextOut =
				s->dma_adc.rawbuf;
		}
		return 0;

	case SNDCTL_DSP_SPEED:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				set_dac_rate(s, val);
			}
			if (s->open_mode & FMODE_READ)
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			if (s->open_mode & FMODE_WRITE)
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
		}
		return put_user((file->f_mode & FMODE_READ) ?
				s->dma_adc.sample_rate :
				s->dma_dac.sample_rate,
				(int *)arg);

	case SNDCTL_DSP_STEREO:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.num_channels = val ? 2 : 1;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.num_channels = val ? 2 : 1;
			if (s->codec_ext_caps & AC97_EXT_DACS) {
				// disable surround and center/lfe in AC'97
				u16 ext_stat = rdcodec(&s->codec,
						       AC97_EXTENDED_STATUS);
				wrcodec(&s->codec, AC97_EXTENDED_STATUS,
					ext_stat | (AC97_EXTSTAT_PRI |
						    AC97_EXTSTAT_PRJ |
						    AC97_EXTSTAT_PRK));
			}
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SNDCTL_DSP_CHANNELS:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (val != 0) {
			if (file->f_mode & FMODE_READ) {
				if (val < 0 || val > 2)
					return -EINVAL;
				stop_adc(s);
				s->dma_adc.num_channels = val;
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				switch (val) {
				case 1:
				case 2:
					break;
				case 3:
				case 5:
					return -EINVAL;
				case 4:
					if (!(s->codec_ext_caps &
					      AC97_EXTID_SDAC))
						return -EINVAL;
					break;
				case 6:
					if ((s->codec_ext_caps &
					     AC97_EXT_DACS) != AC97_EXT_DACS)
						return -EINVAL;
					break;
				default:
					return -EINVAL;
				}

				stop_dac(s);
				if (val <= 2 &&
				    (s->codec_ext_caps & AC97_EXT_DACS)) {
					// disable surround and center/lfe
					// channels in AC'97
					u16             ext_stat =
						rdcodec(&s->codec,
							AC97_EXTENDED_STATUS);
					wrcodec(&s->codec,
						AC97_EXTENDED_STATUS,
						ext_stat | (AC97_EXTSTAT_PRI |
							    AC97_EXTSTAT_PRJ |
							    AC97_EXTSTAT_PRK));
				} else if (val >= 4) {
					// enable surround, center/lfe
					// channels in AC'97
					u16             ext_stat =
						rdcodec(&s->codec,
							AC97_EXTENDED_STATUS);
					ext_stat &= ~AC97_EXTSTAT_PRJ;
					if (val == 6)
						ext_stat &=
							~(AC97_EXTSTAT_PRI |
							  AC97_EXTSTAT_PRK);
					wrcodec(&s->codec,
						AC97_EXTENDED_STATUS,
						ext_stat);
				}

				s->dma_dac.num_channels = val;
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		}
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_GETFMTS:	/* Returns a mask */
		return put_user(AFMT_S16_LE | AFMT_U8, (int *) arg);

	case SNDCTL_DSP_SETFMT:	/* Selects ONE fmt */
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (val != AFMT_QUERY) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				if (val == AFMT_S16_LE)
					s->dma_adc.sample_size = 16;
				else {
					val = AFMT_U8;
					s->dma_adc.sample_size = 8;
				}
				if ((ret = prog_dmabuf_adc(s)))
					return ret;
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				if (val == AFMT_S16_LE)
					s->dma_dac.sample_size = 16;
				else {
					val = AFMT_U8;
					s->dma_dac.sample_size = 8;
				}
				if ((ret = prog_dmabuf_dac(s)))
					return ret;
			}
		} else {
			if (file->f_mode & FMODE_READ)
				val = (s->dma_adc.sample_size == 16) ?
					AFMT_S16_LE : AFMT_U8;
			else
				val = (s->dma_dac.sample_size == 16) ?
					AFMT_S16_LE : AFMT_U8;
		}
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_POST:
		return 0;

	case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		spin_lock_irqsave(&s->lock, flags);
		if (file->f_mode & FMODE_READ && !s->dma_adc.stopped)
			val |= PCM_ENABLE_INPUT;
		if (file->f_mode & FMODE_WRITE && !s->dma_dac.stopped)
			val |= PCM_ENABLE_OUTPUT;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *) arg);

	case SNDCTL_DSP_SETTRIGGER:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT)
				start_adc(s);
			else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT)
				start_dac(s);
			else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		abinfo.fragsize = s->dma_dac.fragsize;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		count -= dma_count_done(&s->dma_dac);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		abinfo.bytes = (s->dma_dac.dmasize - count) /
			s->dma_dac.cnt_factor;
		abinfo.fragstotal = s->dma_dac.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;
#ifdef AU1000_VERBOSE_DEBUG
		dbg("bytes=%d, fragments=%d", abinfo.bytes, abinfo.fragments);
#endif
		return copy_to_user((void *) arg, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		abinfo.fragsize = s->dma_adc.fragsize;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_adc.count;
		count += dma_count_done(&s->dma_adc);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		abinfo.bytes = count / s->dma_adc.cnt_factor;
		abinfo.fragstotal = s->dma_adc.numfrag;
		abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;
		return copy_to_user((void *) arg, &abinfo,
				    sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_NONBLOCK:
		file->f_flags |= O_NONBLOCK;
		return 0;

	case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		count -= dma_count_done(&s->dma_dac);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		count /= s->dma_dac.cnt_factor;
		return put_user(count, (int *) arg);

	case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		cinfo.bytes = s->dma_adc.total_bytes;
		count = s->dma_adc.count;
		if (!s->dma_adc.stopped) {
			diff = dma_count_done(&s->dma_adc);
			count += diff;
			cinfo.bytes += diff;
			cinfo.ptr =  virt_to_phys(s->dma_adc.nextIn) + diff -
				s->dma_adc.dmaaddr;
		} else
			cinfo.ptr = virt_to_phys(s->dma_adc.nextIn) -
				s->dma_adc.dmaaddr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= (s->dma_adc.dma_fragsize-1);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_adc.fragshift;
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		cinfo.bytes = s->dma_dac.total_bytes;
		count = s->dma_dac.count;
		if (!s->dma_dac.stopped) {
			diff = dma_count_done(&s->dma_dac);
			count -= diff;
			cinfo.bytes += diff;
			cinfo.ptr = virt_to_phys(s->dma_dac.nextOut) + diff -
				s->dma_dac.dmaaddr;
		} else
			cinfo.ptr = virt_to_phys(s->dma_dac.nextOut) -
				s->dma_dac.dmaaddr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= (s->dma_dac.dma_fragsize-1);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_dac.fragshift;
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE)
			return put_user(s->dma_dac.fragsize, (int *) arg);
		else
			return put_user(s->dma_adc.fragsize, (int *) arg);

	case SNDCTL_DSP_SETFRAGMENT:
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
		if (get_user(val, (int *) arg))
			return -EFAULT;
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.subdivision = val;
			if ((ret = prog_dmabuf_adc(s)))
				return ret;
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.subdivision = val;
			if ((ret = prog_dmabuf_dac(s)))
				return ret;
		}
		return 0;

	case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ?
				s->dma_adc.sample_rate :
				s->dma_dac.sample_rate,
				(int *)arg);

	case SOUND_PCM_READ_CHANNELS:
		if (file->f_mode & FMODE_READ)
			return put_user(s->dma_adc.num_channels, (int *)arg);
		else
			return put_user(s->dma_dac.num_channels, (int *)arg);

	case SOUND_PCM_READ_BITS:
		if (file->f_mode & FMODE_READ)
			return put_user(s->dma_adc.sample_size, (int *)arg);
		else
			return put_user(s->dma_dac.sample_size, (int *)arg);

	case SOUND_PCM_WRITE_FILTER:
	case SNDCTL_DSP_SETSYNCRO:
	case SOUND_PCM_READ_FILTER:
		return -EINVAL;
	}

	return mixdev_ioctl(&s->codec, cmd, arg);
}


static int  au1000_open(struct inode *inode, struct file *file)
{
	int             minor = iminor(inode);
	DECLARE_WAITQUEUE(wait, current);
	struct au1000_state *s = &au1000_state;
	int             ret;

#ifdef AU1000_VERBOSE_DEBUG
	if (file->f_flags & O_NONBLOCK)
		dbg("%s: non-blocking", __FUNCTION__);
	else
		dbg("%s: blocking", __FUNCTION__);
#endif
	
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		up(&s->open_sem);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}

	stop_dac(s);
	stop_adc(s);

	if (file->f_mode & FMODE_READ) {
		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags =
			s->dma_adc.subdivision = s->dma_adc.total_bytes = 0;
		s->dma_adc.num_channels = 1;
		s->dma_adc.sample_size = 8;
		set_adc_rate(s, 8000);
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->dma_adc.sample_size = 16;
	}

	if (file->f_mode & FMODE_WRITE) {
		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags =
			s->dma_dac.subdivision = s->dma_dac.total_bytes = 0;
		s->dma_dac.num_channels = 1;
		s->dma_dac.sample_size = 8;
		set_dac_rate(s, 8000);
		if ((minor & 0xf) == SND_DEV_DSP16)
			s->dma_dac.sample_size = 16;
	}

	if (file->f_mode & FMODE_READ) {
		if ((ret = prog_dmabuf_adc(s)))
			return ret;
	}
	if (file->f_mode & FMODE_WRITE) {
		if ((ret = prog_dmabuf_dac(s)))
			return ret;
	}

	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	up(&s->open_sem);
	init_MUTEX(&s->sem);
	return nonseekable_open(inode, file);
}

static int au1000_release(struct inode *inode, struct file *file)
{
	struct au1000_state *s = (struct au1000_state *)file->private_data;

	lock_kernel();
	
	if (file->f_mode & FMODE_WRITE) {
		unlock_kernel();
		drain_dac(s, file->f_flags & O_NONBLOCK);
		lock_kernel();
	}

	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		dealloc_dmabuf(s, &s->dma_dac);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		dealloc_dmabuf(s, &s->dma_adc);
	}
	s->open_mode &= ((~file->f_mode) & (FMODE_READ|FMODE_WRITE));
	up(&s->open_sem);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const */ struct file_operations au1000_audio_fops = {
	.owner		= THIS_MODULE,
	.llseek		= au1000_llseek,
	.read		= au1000_read,
	.write		= au1000_write,
	.poll		= au1000_poll,
	.ioctl		= au1000_ioctl,
	.mmap		= au1000_mmap,
	.open		= au1000_open,
	.release	= au1000_release,
};


/* --------------------------------------------------------------------- */


/* --------------------------------------------------------------------- */

/*
 * for debugging purposes, we'll create a proc device that dumps the
 * CODEC chipstate
 */

#ifdef AU1000_DEBUG
static int proc_au1000_dump(char *buf, char **start, off_t fpos,
			    int length, int *eof, void *data)
{
	struct au1000_state *s = &au1000_state;
	int             cnt, len = 0;

	/* print out header */
	len += sprintf(buf + len, "\n\t\tAU1000 Audio Debug\n\n");

	// print out digital controller state
	len += sprintf(buf + len, "AU1000 Audio Controller registers\n");
	len += sprintf(buf + len, "---------------------------------\n");
	len += sprintf (buf + len, "AC97C_CONFIG = %08x\n",
			au_readl(AC97C_CONFIG));
	len += sprintf (buf + len, "AC97C_STATUS = %08x\n",
			au_readl(AC97C_STATUS));
	len += sprintf (buf + len, "AC97C_CNTRL  = %08x\n",
			au_readl(AC97C_CNTRL));

	/* print out CODEC state */
	len += sprintf(buf + len, "\nAC97 CODEC registers\n");
	len += sprintf(buf + len, "----------------------\n");
	for (cnt = 0; cnt <= 0x7e; cnt += 2)
		len += sprintf(buf + len, "reg %02x = %04x\n",
			       cnt, rdcodec(&s->codec, cnt));

	if (fpos >= len) {
		*start = buf;
		*eof = 1;
		return 0;
	}
	*start = buf + fpos;
	if ((len -= fpos) > length)
		return length;
	*eof = 1;
	return len;

}
#endif /* AU1000_DEBUG */

/* --------------------------------------------------------------------- */

MODULE_AUTHOR("Monta Vista Software, stevel@mvista.com");
MODULE_DESCRIPTION("Au1000 Audio Driver");

/* --------------------------------------------------------------------- */

static int __devinit au1000_probe(void)
{
	struct au1000_state *s = &au1000_state;
	int             val;
#ifdef AU1000_DEBUG
	char            proc_str[80];
#endif

	memset(s, 0, sizeof(struct au1000_state));

	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	init_MUTEX(&s->open_sem);
	spin_lock_init(&s->lock);
	s->codec.private_data = s;
	s->codec.id = 0;
	s->codec.codec_read = rdcodec;
	s->codec.codec_write = wrcodec;
	s->codec.codec_wait = waitcodec;

	if (!request_mem_region(CPHYSADDR(AC97C_CONFIG),
			    0x14, AU1000_MODULE_NAME)) {
		err("AC'97 ports in use");
		return -1;
	}
	// Allocate the DMA Channels
	if ((s->dma_dac.dmanr = request_au1000_dma(DMA_ID_AC97C_TX,
						   "audio DAC",
						   dac_dma_interrupt,
						   SA_INTERRUPT, s)) < 0) {
		err("Can't get DAC DMA");
		goto err_dma1;
	}
	if ((s->dma_adc.dmanr = request_au1000_dma(DMA_ID_AC97C_RX,
						   "audio ADC",
						   adc_dma_interrupt,
						   SA_INTERRUPT, s)) < 0) {
		err("Can't get ADC DMA");
		goto err_dma2;
	}

	info("DAC: DMA%d/IRQ%d, ADC: DMA%d/IRQ%d",
	     s->dma_dac.dmanr, get_dma_done_irq(s->dma_dac.dmanr),
	     s->dma_adc.dmanr, get_dma_done_irq(s->dma_adc.dmanr));

	// enable DMA coherency in read/write DMA channels
	set_dma_mode(s->dma_dac.dmanr,
		     get_dma_mode(s->dma_dac.dmanr) & ~DMA_NC);
	set_dma_mode(s->dma_adc.dmanr,
		     get_dma_mode(s->dma_adc.dmanr) & ~DMA_NC);

	/* register devices */

	if ((s->dev_audio = register_sound_dsp(&au1000_audio_fops, -1)) < 0)
		goto err_dev1;
	if ((s->codec.dev_mixer =
	     register_sound_mixer(&au1000_mixer_fops, -1)) < 0)
		goto err_dev2;

#ifdef AU1000_DEBUG
	/* intialize the debug proc device */
	s->ps = create_proc_read_entry(AU1000_MODULE_NAME, 0, NULL,
				       proc_au1000_dump, NULL);
#endif /* AU1000_DEBUG */

	// configure pins for AC'97
	au_writel(au_readl(SYS_PINFUNC) & ~0x02, SYS_PINFUNC);

	// Assert reset for 10msec to the AC'97 controller, and enable clock
	au_writel(AC97C_RS | AC97C_CE, AC97C_CNTRL);
	au1000_delay(10);
	au_writel(AC97C_CE, AC97C_CNTRL);
	au1000_delay(10);	// wait for clock to stabilize

	/* cold reset the AC'97 */
	au_writel(AC97C_RESET, AC97C_CONFIG);
	au1000_delay(10);
	au_writel(0, AC97C_CONFIG);
	/* need to delay around 500msec(bleech) to give
	   some CODECs enough time to wakeup */
	au1000_delay(500);

	/* warm reset the AC'97 to start the bitclk */
	au_writel(AC97C_SG | AC97C_SYNC, AC97C_CONFIG);
	udelay(100);
	au_writel(0, AC97C_CONFIG);

	/* codec init */
	if (!ac97_probe_codec(&s->codec))
		goto err_dev3;

	s->codec_base_caps = rdcodec(&s->codec, AC97_RESET);
	s->codec_ext_caps = rdcodec(&s->codec, AC97_EXTENDED_ID);
	info("AC'97 Base/Extended ID = %04x/%04x",
	     s->codec_base_caps, s->codec_ext_caps);

	/*
	 * On the Pb1000, audio playback is on the AUX_OUT
	 * channel (which defaults to LNLVL_OUT in AC'97
	 * rev 2.2) so make sure this channel is listed
	 * as supported (soundcard.h calls this channel
	 * ALTPCM). ac97_codec.c does not handle detection
	 * of this channel correctly.
	 */
	s->codec.supported_mixers |= SOUND_MASK_ALTPCM;
	/*
	 * Now set AUX_OUT's default volume.
	 */
	val = 0x4343;
	mixdev_ioctl(&s->codec, SOUND_MIXER_WRITE_ALTPCM,
		     (unsigned long) &val);
	
	if (!(s->codec_ext_caps & AC97_EXTID_VRA)) {
		// codec does not support VRA
		s->no_vra = 1;
	} else if (!vra) {
		// Boot option says disable VRA
		u16 ac97_extstat = rdcodec(&s->codec, AC97_EXTENDED_STATUS);
		wrcodec(&s->codec, AC97_EXTENDED_STATUS,
			ac97_extstat & ~AC97_EXTSTAT_VRA);
		s->no_vra = 1;
	}
	if (s->no_vra)
		info("no VRA, interpolating and decimating");

	/* set mic to be the recording source */
	val = SOUND_MASK_MIC;
	mixdev_ioctl(&s->codec, SOUND_MIXER_WRITE_RECSRC,
		     (unsigned long) &val);

#ifdef AU1000_DEBUG
	sprintf(proc_str, "driver/%s/%d/ac97", AU1000_MODULE_NAME,
		s->codec.id);
	s->ac97_ps = create_proc_read_entry (proc_str, 0, NULL,
					     ac97_read_proc, &s->codec);
#endif

#ifdef CONFIG_MIPS_XXS1500
	/* deassert eapd */
	wrcodec(&s->codec, AC97_POWER_CONTROL,
			rdcodec(&s->codec, AC97_POWER_CONTROL) & ~0x8000);
	/* mute a number of signals which seem to be causing problems
	 * if not muted.
	 */
	wrcodec(&s->codec, AC97_PCBEEP_VOL, 0x8000);
	wrcodec(&s->codec, AC97_PHONE_VOL, 0x8008);
	wrcodec(&s->codec, AC97_MIC_VOL, 0x8008);
	wrcodec(&s->codec, AC97_LINEIN_VOL, 0x8808);
	wrcodec(&s->codec, AC97_CD_VOL, 0x8808);
	wrcodec(&s->codec, AC97_VIDEO_VOL, 0x8808);
	wrcodec(&s->codec, AC97_AUX_VOL, 0x8808);
	wrcodec(&s->codec, AC97_PCMOUT_VOL, 0x0808);
	wrcodec(&s->codec, AC97_GENERAL_PURPOSE, 0x2000);
#endif

	return 0;

 err_dev3:
	unregister_sound_mixer(s->codec.dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	free_au1000_dma(s->dma_adc.dmanr);
 err_dma2:
	free_au1000_dma(s->dma_dac.dmanr);
 err_dma1:
	release_mem_region(CPHYSADDR(AC97C_CONFIG), 0x14);
	return -1;
}

static void au1000_remove(void)
{
	struct au1000_state *s = &au1000_state;

	if (!s)
		return;
#ifdef AU1000_DEBUG
	if (s->ps)
		remove_proc_entry(AU1000_MODULE_NAME, NULL);
#endif /* AU1000_DEBUG */
	synchronize_irq();
	free_au1000_dma(s->dma_adc.dmanr);
	free_au1000_dma(s->dma_dac.dmanr);
	release_mem_region(CPHYSADDR(AC97C_CONFIG), 0x14);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->codec.dev_mixer);
}

static int __init init_au1000(void)
{
	info("stevel@mvista.com, built " __TIME__ " on " __DATE__);
	return au1000_probe();
}

static void __exit cleanup_au1000(void)
{
	info("unloading");
	au1000_remove();
}

module_init(init_au1000);
module_exit(cleanup_au1000);

/* --------------------------------------------------------------------- */

#ifndef MODULE

static int __init au1000_setup(char *options)
{
	char           *this_opt;

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ","))) {
		if (!*this_opt)
			continue;
		if (!strncmp(this_opt, "vra", 3)) {
			vra = 1;
		}
	}

	return 1;
}

__setup("au1000_audio=", au1000_setup);

#endif /* MODULE */
