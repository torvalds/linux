/*
 * au1550_ac97.c  --  Sound driver for Alchemy Au1550 MIPS Internet Edge
 *                    Processor.
 *
 * Copyright 2004 Embedded Edge, LLC
 *	dan@embeddededge.com
 *
 * Mostly copied from the au1000.c driver and some from the
 * PowerMac dbdma driver.
 * We assume the processor can do memory coherent DMA.
 *
 * Ported to 2.6 by Matt Porter <mporter@kernel.crashing.org>
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
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/slab.h>
#include <linux/soundcard.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/bitops.h>
#include <linux/spinlock.h>
#include <linux/smp_lock.h>
#include <linux/ac97_codec.h>
#include <linux/mutex.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>
#include <asm/mach-au1x00/au1xxx_psc.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1xxx.h>

#undef OSS_DOCUMENTED_MIXER_SEMANTICS

/* misc stuff */
#define POLL_COUNT   0x50000
#define AC97_EXT_DACS (AC97_EXTID_SDAC | AC97_EXTID_CDAC | AC97_EXTID_LDAC)

/* The number of DBDMA ring descriptors to allocate.  No sense making
 * this too large....if you can't keep up with a few you aren't likely
 * to be able to with lots of them, either.
 */
#define NUM_DBDMA_DESCRIPTORS 4

#define err(format, arg...) printk(KERN_ERR format "\n" , ## arg)

/* Boot options
 * 0 = no VRA, 1 = use VRA if codec supports it
 */
static int      vra = 1;
module_param(vra, bool, 0);
MODULE_PARM_DESC(vra, "if 1 use VRA if codec supports it");

static struct au1550_state {
	/* soundcore stuff */
	int             dev_audio;

	struct ac97_codec *codec;
	unsigned        codec_base_caps; /* AC'97 reg 00h, "Reset Register" */
	unsigned        codec_ext_caps;  /* AC'97 reg 28h, "Extended Audio ID" */
	int             no_vra;		/* do not use VRA */

	spinlock_t      lock;
	struct mutex open_mutex;
	struct mutex sem;
	mode_t          open_mode;
	wait_queue_head_t open_wait;

	struct dmabuf {
		u32		dmanr;
		unsigned        sample_rate;
		unsigned	src_factor;
		unsigned        sample_size;
		int             num_channels;
		int		dma_bytes_per_sample;
		int		user_bytes_per_sample;
		int		cnt_factor;

		void		*rawbuf;
		unsigned        buforder;
		unsigned	numfrag;
		unsigned        fragshift;
		void		*nextIn;
		void		*nextOut;
		int		count;
		unsigned        total_bytes;
		unsigned        error;
		wait_queue_head_t wait;

		/* redundant, but makes calculations easier */
		unsigned	fragsize;
		unsigned	dma_fragsize;
		unsigned	dmasize;
		unsigned	dma_qcount;

		/* OSS stuff */
		unsigned        mapped:1;
		unsigned        ready:1;
		unsigned        stopped:1;
		unsigned        ossfragshift;
		int             ossmaxfrags;
		unsigned        subdivision;
	} dma_dac, dma_adc;
} au1550_state;

static unsigned
ld2(unsigned int x)
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

static void
au1550_delay(int msec)
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

static u16
rdcodec(struct ac97_codec *codec, u8 addr)
{
	struct au1550_state *s = (struct au1550_state *)codec->private_data;
	unsigned long   flags;
	u32             cmd, val;
	u16             data;
	int             i;

	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97STAT);
		au_sync();
		if (!(val & PSC_AC97STAT_CP))
			break;
	}
	if (i == POLL_COUNT)
		err("rdcodec: codec cmd pending expired!");

	cmd = (u32)PSC_AC97CDC_INDX(addr);
	cmd |= PSC_AC97CDC_RD;	/* read command */
	au_writel(cmd, PSC_AC97CDC);
	au_sync();

	/* now wait for the data
	*/
	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97STAT);
		au_sync();
		if (!(val & PSC_AC97STAT_CP))
			break;
	}
	if (i == POLL_COUNT) {
		err("rdcodec: read poll expired!");
		data = 0;
		goto out;
	}

	/* wait for command done?
	*/
	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97EVNT);
		au_sync();
		if (val & PSC_AC97EVNT_CD)
			break;
	}
	if (i == POLL_COUNT) {
		err("rdcodec: read cmdwait expired!");
		data = 0;
		goto out;
	}

	data = au_readl(PSC_AC97CDC) & 0xffff;
	au_sync();

	/* Clear command done event.
	*/
	au_writel(PSC_AC97EVNT_CD, PSC_AC97EVNT);
	au_sync();

 out:
	spin_unlock_irqrestore(&s->lock, flags);

	return data;
}


static void
wrcodec(struct ac97_codec *codec, u8 addr, u16 data)
{
	struct au1550_state *s = (struct au1550_state *)codec->private_data;
	unsigned long   flags;
	u32             cmd, val;
	int             i;

	spin_lock_irqsave(&s->lock, flags);

	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97STAT);
		au_sync();
		if (!(val & PSC_AC97STAT_CP))
			break;
	}
	if (i == POLL_COUNT)
		err("wrcodec: codec cmd pending expired!");

	cmd = (u32)PSC_AC97CDC_INDX(addr);
	cmd |= (u32)data;
	au_writel(cmd, PSC_AC97CDC);
	au_sync();

	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97STAT);
		au_sync();
		if (!(val & PSC_AC97STAT_CP))
			break;
	}
	if (i == POLL_COUNT)
		err("wrcodec: codec cmd pending expired!");

	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97EVNT);
		au_sync();
		if (val & PSC_AC97EVNT_CD)
			break;
	}
	if (i == POLL_COUNT)
		err("wrcodec: read cmdwait expired!");

	/* Clear command done event.
	*/
	au_writel(PSC_AC97EVNT_CD, PSC_AC97EVNT);
	au_sync();

	spin_unlock_irqrestore(&s->lock, flags);
}

static void
waitcodec(struct ac97_codec *codec)
{
	u16	temp;
	u32	val;
	int	i;

	/* codec_wait is used to wait for a ready state after
	 * an AC97C_RESET.
	 */
	au1550_delay(10);

	/* first poll the CODEC_READY tag bit
	*/
	for (i = 0; i < POLL_COUNT; i++) {
		val = au_readl(PSC_AC97STAT);
		au_sync();
		if (val & PSC_AC97STAT_CR)
			break;
	}
	if (i == POLL_COUNT) {
		err("waitcodec: CODEC_READY poll expired!");
		return;
	}

	/* get AC'97 powerdown control/status register
	*/
	temp = rdcodec(codec, AC97_POWER_CONTROL);

	/* If anything is powered down, power'em up
	*/
	if (temp & 0x7f00) {
		/* Power on
		*/
		wrcodec(codec, AC97_POWER_CONTROL, 0);
		au1550_delay(100);

		/* Reread
		*/
		temp = rdcodec(codec, AC97_POWER_CONTROL);
	}

	/* Check if Codec REF,ANL,DAC,ADC ready
	*/
	if ((temp & 0x7f0f) != 0x000f)
		err("codec reg 26 status (0x%x) not ready!!", temp);
}

/* stop the ADC before calling */
static void
set_adc_rate(struct au1550_state *s, unsigned rate)
{
	struct dmabuf  *adc = &s->dma_adc;
	struct dmabuf  *dac = &s->dma_dac;
	unsigned        adc_rate, dac_rate;
	u16             ac97_extstat;

	if (s->no_vra) {
		/* calc SRC factor
		*/
		adc->src_factor = ((96000 / rate) + 1) >> 1;
		adc->sample_rate = 48000 / adc->src_factor;
		return;
	}

	adc->src_factor = 1;

	ac97_extstat = rdcodec(s->codec, AC97_EXTENDED_STATUS);

	rate = rate > 48000 ? 48000 : rate;

	/* enable VRA
	*/
	wrcodec(s->codec, AC97_EXTENDED_STATUS,
		ac97_extstat | AC97_EXTSTAT_VRA);

	/* now write the sample rate
	*/
	wrcodec(s->codec, AC97_PCM_LR_ADC_RATE, (u16) rate);

	/* read it back for actual supported rate
	*/
	adc_rate = rdcodec(s->codec, AC97_PCM_LR_ADC_RATE);

	pr_debug("set_adc_rate: set to %d Hz\n", adc_rate);

	/* some codec's don't allow unequal DAC and ADC rates, in which case
	 * writing one rate reg actually changes both.
	 */
	dac_rate = rdcodec(s->codec, AC97_PCM_FRONT_DAC_RATE);
	if (dac->num_channels > 2)
		wrcodec(s->codec, AC97_PCM_SURR_DAC_RATE, dac_rate);
	if (dac->num_channels > 4)
		wrcodec(s->codec, AC97_PCM_LFE_DAC_RATE, dac_rate);

	adc->sample_rate = adc_rate;
	dac->sample_rate = dac_rate;
}

/* stop the DAC before calling */
static void
set_dac_rate(struct au1550_state *s, unsigned rate)
{
	struct dmabuf  *dac = &s->dma_dac;
	struct dmabuf  *adc = &s->dma_adc;
	unsigned        adc_rate, dac_rate;
	u16             ac97_extstat;

	if (s->no_vra) {
		/* calc SRC factor
		*/
		dac->src_factor = ((96000 / rate) + 1) >> 1;
		dac->sample_rate = 48000 / dac->src_factor;
		return;
	}

	dac->src_factor = 1;

	ac97_extstat = rdcodec(s->codec, AC97_EXTENDED_STATUS);

	rate = rate > 48000 ? 48000 : rate;

	/* enable VRA
	*/
	wrcodec(s->codec, AC97_EXTENDED_STATUS,
		ac97_extstat | AC97_EXTSTAT_VRA);

	/* now write the sample rate
	*/
	wrcodec(s->codec, AC97_PCM_FRONT_DAC_RATE, (u16) rate);

	/* I don't support different sample rates for multichannel,
	 * so make these channels the same.
	 */
	if (dac->num_channels > 2)
		wrcodec(s->codec, AC97_PCM_SURR_DAC_RATE, (u16) rate);
	if (dac->num_channels > 4)
		wrcodec(s->codec, AC97_PCM_LFE_DAC_RATE, (u16) rate);
	/* read it back for actual supported rate
	*/
	dac_rate = rdcodec(s->codec, AC97_PCM_FRONT_DAC_RATE);

	pr_debug("set_dac_rate: set to %d Hz\n", dac_rate);

	/* some codec's don't allow unequal DAC and ADC rates, in which case
	 * writing one rate reg actually changes both.
	 */
	adc_rate = rdcodec(s->codec, AC97_PCM_LR_ADC_RATE);

	dac->sample_rate = dac_rate;
	adc->sample_rate = adc_rate;
}

static void
stop_dac(struct au1550_state *s)
{
	struct dmabuf  *db = &s->dma_dac;
	u32		stat;
	unsigned long   flags;

	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	au_writel(PSC_AC97PCR_TP, PSC_AC97PCR);
	au_sync();

	/* Wait for Transmit Busy to show disabled.
	*/
	do {
		stat = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((stat & PSC_AC97STAT_TB) != 0);

	au1xxx_dbdma_reset(db->dmanr);

	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}

static void
stop_adc(struct au1550_state *s)
{
	struct dmabuf  *db = &s->dma_adc;
	unsigned long   flags;
	u32		stat;

	if (db->stopped)
		return;

	spin_lock_irqsave(&s->lock, flags);

	au_writel(PSC_AC97PCR_RP, PSC_AC97PCR);
	au_sync();

	/* Wait for Receive Busy to show disabled.
	*/
	do {
		stat = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((stat & PSC_AC97STAT_RB) != 0);

	au1xxx_dbdma_reset(db->dmanr);

	db->stopped = 1;

	spin_unlock_irqrestore(&s->lock, flags);
}


static void
set_xmit_slots(int num_channels)
{
	u32	ac97_config, stat;

	ac97_config = au_readl(PSC_AC97CFG);
	au_sync();
	ac97_config &= ~(PSC_AC97CFG_TXSLOT_MASK | PSC_AC97CFG_DE_ENABLE);
	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	switch (num_channels) {
	case 6:		/* stereo with surround and center/LFE,
			 * slots 3,4,6,7,8,9
			 */
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(6);
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(9);

	case 4:		/* stereo with surround, slots 3,4,7,8 */
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(7);
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(8);

	case 2:		/* stereo, slots 3,4 */
	case 1:		/* mono */
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(3);
		ac97_config |= PSC_AC97CFG_TXSLOT_ENA(4);
	}

	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	ac97_config |= PSC_AC97CFG_DE_ENABLE;
	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	/* Wait for Device ready.
	*/
	do {
		stat = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((stat & PSC_AC97STAT_DR) == 0);
}

static void
set_recv_slots(int num_channels)
{
	u32	ac97_config, stat;

	ac97_config = au_readl(PSC_AC97CFG);
	au_sync();
	ac97_config &= ~(PSC_AC97CFG_RXSLOT_MASK | PSC_AC97CFG_DE_ENABLE);
	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	/* Always enable slots 3 and 4 (stereo). Slot 6 is
	 * optional Mic ADC, which we don't support yet.
	 */
	ac97_config |= PSC_AC97CFG_RXSLOT_ENA(3);
	ac97_config |= PSC_AC97CFG_RXSLOT_ENA(4);

	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	ac97_config |= PSC_AC97CFG_DE_ENABLE;
	au_writel(ac97_config, PSC_AC97CFG);
	au_sync();

	/* Wait for Device ready.
	*/
	do {
		stat = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((stat & PSC_AC97STAT_DR) == 0);
}

/* Hold spinlock for both start_dac() and start_adc() calls */
static void
start_dac(struct au1550_state *s)
{
	struct dmabuf  *db = &s->dma_dac;

	if (!db->stopped)
		return;

	set_xmit_slots(db->num_channels);
	au_writel(PSC_AC97PCR_TC, PSC_AC97PCR);
	au_sync();
	au_writel(PSC_AC97PCR_TS, PSC_AC97PCR);
	au_sync();

	au1xxx_dbdma_start(db->dmanr);

	db->stopped = 0;
}

static void
start_adc(struct au1550_state *s)
{
	struct dmabuf  *db = &s->dma_adc;
	int	i;

	if (!db->stopped)
		return;

	/* Put two buffers on the ring to get things started.
	*/
	for (i=0; i<2; i++) {
		au1xxx_dbdma_put_dest(db->dmanr, db->nextIn, db->dma_fragsize);

		db->nextIn += db->dma_fragsize;
		if (db->nextIn >= db->rawbuf + db->dmasize)
			db->nextIn -= db->dmasize;
	}

	set_recv_slots(db->num_channels);
	au1xxx_dbdma_start(db->dmanr);
	au_writel(PSC_AC97PCR_RC, PSC_AC97PCR);
	au_sync();
	au_writel(PSC_AC97PCR_RS, PSC_AC97PCR);
	au_sync();

	db->stopped = 0;
}

static int
prog_dmabuf(struct au1550_state *s, struct dmabuf *db)
{
	unsigned user_bytes_per_sec;
	unsigned        bufs;
	unsigned        rate = db->sample_rate;

	if (!db->rawbuf) {
		db->ready = db->mapped = 0;
		db->buforder = 5;	/* 32 * PAGE_SIZE */
		db->rawbuf = kmalloc((PAGE_SIZE << db->buforder), GFP_KERNEL);
		if (!db->rawbuf)
			return -ENOMEM;
	}

	db->cnt_factor = 1;
	if (db->sample_size == 8)
		db->cnt_factor *= 2;
	if (db->num_channels == 1)
		db->cnt_factor *= 2;
	db->cnt_factor *= db->src_factor;

	db->count = 0;
	db->dma_qcount = 0;
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

	pr_debug("prog_dmabuf: rate=%d, samplesize=%d, channels=%d\n",
	    rate, db->sample_size, db->num_channels);
	pr_debug("prog_dmabuf: fragsize=%d, cnt_factor=%d, dma_fragsize=%d\n",
	    db->fragsize, db->cnt_factor, db->dma_fragsize);
	pr_debug("prog_dmabuf: numfrag=%d, dmasize=%d\n", db->numfrag, db->dmasize);

	db->ready = 1;
	return 0;
}

static int
prog_dmabuf_adc(struct au1550_state *s)
{
	stop_adc(s);
	return prog_dmabuf(s, &s->dma_adc);

}

static int
prog_dmabuf_dac(struct au1550_state *s)
{
	stop_dac(s);
	return prog_dmabuf(s, &s->dma_dac);
}


static void
dac_dma_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct au1550_state *s = (struct au1550_state *) dev_id;
	struct dmabuf  *db = &s->dma_dac;
	u32	ac97c_stat;

	spin_lock(&s->lock);

	ac97c_stat = au_readl(PSC_AC97STAT);
	if (ac97c_stat & (AC97C_XU | AC97C_XO | AC97C_TE))
		pr_debug("AC97C status = 0x%08x\n", ac97c_stat);
	db->dma_qcount--;

	if (db->count >= db->fragsize) {
		if (au1xxx_dbdma_put_source(db->dmanr, db->nextOut,
							db->fragsize) == 0) {
			err("qcount < 2 and no ring room!");
		}
		db->nextOut += db->fragsize;
		if (db->nextOut >= db->rawbuf + db->dmasize)
			db->nextOut -= db->dmasize;
		db->count -= db->fragsize;
		db->total_bytes += db->dma_fragsize;
		db->dma_qcount++;
	}

	/* wake up anybody listening */
	if (waitqueue_active(&db->wait))
		wake_up(&db->wait);

	spin_unlock(&s->lock);
}


static void
adc_dma_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct	au1550_state *s = (struct au1550_state *)dev_id;
	struct	dmabuf  *dp = &s->dma_adc;
	u32	obytes;
	char	*obuf;

	spin_lock(&s->lock);

	/* Pull the buffer from the dma queue.
	*/
	au1xxx_dbdma_get_dest(dp->dmanr, (void *)(&obuf), &obytes);

	if ((dp->count + obytes) > dp->dmasize) {
		/* Overrun. Stop ADC and log the error
		*/
		spin_unlock(&s->lock);
		stop_adc(s);
		dp->error++;
		err("adc overrun");
		return;
	}

	/* Put a new empty buffer on the destination DMA.
	*/
	au1xxx_dbdma_put_dest(dp->dmanr, dp->nextIn, dp->dma_fragsize);

	dp->nextIn += dp->dma_fragsize;
	if (dp->nextIn >= dp->rawbuf + dp->dmasize)
		dp->nextIn -= dp->dmasize;

	dp->count += obytes;
	dp->total_bytes += obytes;

	/* wake up anybody listening
	*/
	if (waitqueue_active(&dp->wait))
		wake_up(&dp->wait);

	spin_unlock(&s->lock);
}

static loff_t
au1550_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}


static int
au1550_open_mixdev(struct inode *inode, struct file *file)
{
	file->private_data = &au1550_state;
	return 0;
}

static int
au1550_release_mixdev(struct inode *inode, struct file *file)
{
	return 0;
}

static int
mixdev_ioctl(struct ac97_codec *codec, unsigned int cmd,
                        unsigned long arg)
{
	return codec->mixer_ioctl(codec, cmd, arg);
}

static int
au1550_ioctl_mixdev(struct inode *inode, struct file *file,
			       unsigned int cmd, unsigned long arg)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
	struct ac97_codec *codec = s->codec;

	return mixdev_ioctl(codec, cmd, arg);
}

static /*const */ struct file_operations au1550_mixer_fops = {
	owner:THIS_MODULE,
	llseek:au1550_llseek,
	ioctl:au1550_ioctl_mixdev,
	open:au1550_open_mixdev,
	release:au1550_release_mixdev,
};

static int
drain_dac(struct au1550_state *s, int nonblock)
{
	unsigned long   flags;
	int             count, tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready || s->dma_dac.stopped)
		return 0;

	for (;;) {
		spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		if (count <= s->dma_dac.fragsize)
			break;
		if (signal_pending(current))
			break;
		if (nonblock)
			return -EBUSY;
		tmo = 1000 * count / (s->no_vra ?
				      48000 : s->dma_dac.sample_rate);
		tmo /= s->dma_dac.dma_bytes_per_sample;
		au1550_delay(tmo);
	}
	if (signal_pending(current))
		return -ERESTARTSYS;
	return 0;
}

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
static int
translate_from_user(struct dmabuf *db, char* dmabuf, char* userbuf,
							       int dmacount)
{
	int             sample, i;
	int             interp_bytes_per_sample;
	int             num_samples;
	int             mono = (db->num_channels == 1);
	char            usersample[12];
	s16             ch, dmasample[6];

	if (db->sample_size == 16 && !mono && db->src_factor == 1) {
		/* no translation necessary, just copy
		*/
		if (copy_from_user(dmabuf, userbuf, dmacount))
			return -EFAULT;
		return dmacount;
	}

	interp_bytes_per_sample = db->dma_bytes_per_sample * db->src_factor;
	num_samples = dmacount / interp_bytes_per_sample;

	for (sample = 0; sample < num_samples; sample++) {
		if (copy_from_user(usersample, userbuf,
				   db->user_bytes_per_sample)) {
			return -EFAULT;
		}

		for (i = 0; i < db->num_channels; i++) {
			if (db->sample_size == 8)
				ch = U8_TO_S16(usersample[i]);
			else
				ch = *((s16 *) (&usersample[i * 2]));
			dmasample[i] = ch;
			if (mono)
				dmasample[i + 1] = ch;	/* right channel */
		}

		/* duplicate every audio frame src_factor times
		*/
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
static int
translate_to_user(struct dmabuf *db, char* userbuf, char* dmabuf,
							     int dmacount)
{
	int             sample, i;
	int             interp_bytes_per_sample;
	int             num_samples;
	int             mono = (db->num_channels == 1);
	char            usersample[12];

	if (db->sample_size == 16 && !mono && db->src_factor == 1) {
		/* no translation necessary, just copy
		*/
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
static int
copy_dmabuf_user(struct dmabuf *db, char* userbuf, int count, int to_user)
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


static ssize_t
au1550_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
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

	mutex_lock(&s->sem);
	add_wait_queue(&db->wait, &wait);

	while (count > 0) {
		/* wait for samples in ADC dma buffer
		*/
		do {
			spin_lock_irqsave(&s->lock, flags);
			if (db->stopped)
				start_adc(s);
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
				mutex_unlock(&s->sem);
				schedule();
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					goto out2;
				}
				mutex_lock(&s->sem);
			}
		} while (avail <= 0);

		/* copy from nextOut to user
		*/
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
	}			/* while (count > 0) */

out:
	mutex_unlock(&s->sem);
out2:
	remove_wait_queue(&db->wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}

static ssize_t
au1550_write(struct file *file, const char *buffer, size_t count, loff_t * ppos)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
	struct dmabuf  *db = &s->dma_dac;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t         ret = 0;
	unsigned long   flags;
	int             cnt, usercnt, avail;

	pr_debug("write: count=%d\n", count);

	if (db->mapped)
		return -ENXIO;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;

	count *= db->cnt_factor;

	mutex_lock(&s->sem);
	add_wait_queue(&db->wait, &wait);

	while (count > 0) {
		/* wait for space in playback buffer
		*/
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
				mutex_unlock(&s->sem);
				schedule();
				if (signal_pending(current)) {
					if (!ret)
						ret = -ERESTARTSYS;
					goto out2;
				}
				mutex_lock(&s->sem);
			}
		} while (avail <= 0);

		/* copy from user to nextIn
		*/
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

		/* If the data is available, we want to keep two buffers
		 * on the dma queue.  If the queue count reaches zero,
		 * we know the dma has stopped.
		 */
		while ((db->dma_qcount < 2) && (db->count >= db->fragsize)) {
			if (au1xxx_dbdma_put_source(db->dmanr, db->nextOut,
							db->fragsize) == 0) {
				err("qcount < 2 and no ring room!");
			}
			db->nextOut += db->fragsize;
			if (db->nextOut >= db->rawbuf + db->dmasize)
				db->nextOut -= db->dmasize;
			db->total_bytes += db->dma_fragsize;
			if (db->dma_qcount == 0)
				start_dac(s);
			db->dma_qcount++;
		}
		spin_unlock_irqrestore(&s->lock, flags);

		count -= cnt;
		usercnt = cnt / db->cnt_factor;
		buffer += usercnt;
		ret += usercnt;
	}			/* while (count > 0) */

out:
	mutex_unlock(&s->sem);
out2:
	remove_wait_queue(&db->wait, &wait);
	set_current_state(TASK_RUNNING);
	return ret;
}


/* No kernel lock - we have our own spinlock */
static unsigned int
au1550_poll(struct file *file, struct poll_table_struct *wait)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
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

static int
au1550_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
	struct dmabuf  *db;
	unsigned long   size;
	int ret = 0;

	lock_kernel();
	mutex_lock(&s->sem);
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
	if (remap_pfn_range(vma, vma->vm_start, page_to_pfn(virt_to_page(db->rawbuf)),
			     size, vma->vm_page_prot)) {
		ret = -EAGAIN;
		goto out;
	}
	vma->vm_flags &= ~VM_IO;
	db->mapped = 1;
out:
	mutex_unlock(&s->sem);
	unlock_kernel();
	return ret;
}

#ifdef DEBUG
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

static int
dma_count_done(struct dmabuf *db)
{
	if (db->stopped)
		return 0;

	return db->dma_fragsize - au1xxx_get_dma_residue(db->dmanr);
}


static int
au1550_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
							unsigned long arg)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;
	unsigned long   flags;
	audio_buf_info  abinfo;
	count_info      cinfo;
	int             count;
	int             val, mapped, ret, diff;

	mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);

#ifdef DEBUG
	for (count=0; count<sizeof(ioctl_str)/sizeof(ioctl_str[0]); count++) {
		if (ioctl_str[count].cmd == cmd)
			break;
	}
	if (count < sizeof(ioctl_str) / sizeof(ioctl_str[0]))
		pr_debug("ioctl %s, arg=0x%lxn", ioctl_str[count].str, arg);
	else
		pr_debug("ioctl 0x%x unknown, arg=0x%lx\n", cmd, arg);
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
				/* disable surround and center/lfe in AC'97
				*/
				u16 ext_stat = rdcodec(s->codec,
						       AC97_EXTENDED_STATUS);
				wrcodec(s->codec, AC97_EXTENDED_STATUS,
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
					/* disable surround and center/lfe
					 * channels in AC'97
					 */
					u16             ext_stat =
						rdcodec(s->codec,
							AC97_EXTENDED_STATUS);
					wrcodec(s->codec,
						AC97_EXTENDED_STATUS,
						ext_stat | (AC97_EXTSTAT_PRI |
							    AC97_EXTSTAT_PRJ |
							    AC97_EXTSTAT_PRK));
				} else if (val >= 4) {
					/* enable surround, center/lfe
					 * channels in AC'97
					 */
					u16             ext_stat =
						rdcodec(s->codec,
							AC97_EXTENDED_STATUS);
					ext_stat &= ~AC97_EXTSTAT_PRJ;
					if (val == 6)
						ext_stat &=
							~(AC97_EXTSTAT_PRI |
							  AC97_EXTSTAT_PRK);
					wrcodec(s->codec,
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
			if (val & PCM_ENABLE_INPUT) {
				spin_lock_irqsave(&s->lock, flags);
				start_adc(s);
				spin_unlock_irqrestore(&s->lock, flags);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				spin_lock_irqsave(&s->lock, flags);
				start_dac(s);
				spin_unlock_irqrestore(&s->lock, flags);
			} else
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
		pr_debug("ioctl SNDCTL_DSP_GETOSPACE: bytes=%d, fragments=%d\n", abinfo.bytes, abinfo.fragments);
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
				virt_to_phys(s->dma_adc.rawbuf);
		} else
			cinfo.ptr = virt_to_phys(s->dma_adc.nextIn) -
				virt_to_phys(s->dma_adc.rawbuf);
		if (s->dma_adc.mapped)
			s->dma_adc.count &= (s->dma_adc.dma_fragsize-1);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_adc.fragshift;
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo));

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
				virt_to_phys(s->dma_dac.rawbuf);
		} else
			cinfo.ptr = virt_to_phys(s->dma_dac.nextOut) -
				virt_to_phys(s->dma_dac.rawbuf);
		if (s->dma_dac.mapped)
			s->dma_dac.count &= (s->dma_dac.dma_fragsize-1);
		spin_unlock_irqrestore(&s->lock, flags);
		if (count < 0)
			count = 0;
		cinfo.blocks = count >> s->dma_dac.fragshift;
		return copy_to_user((void *) arg, &cinfo, sizeof(cinfo));

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

	return mixdev_ioctl(s->codec, cmd, arg);
}


static int
au1550_open(struct inode *inode, struct file *file)
{
	int             minor = MINOR(inode->i_rdev);
	DECLARE_WAITQUEUE(wait, current);
	struct au1550_state *s = &au1550_state;
	int             ret;

#ifdef DEBUG
	if (file->f_flags & O_NONBLOCK)
		pr_debug("open: non-blocking\n");
	else
		pr_debug("open: blocking\n");
#endif

	file->private_data = s;
	/* wait for device to become free */
	mutex_lock(&s->open_mutex);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			mutex_unlock(&s->open_mutex);
			return -EBUSY;
		}
		add_wait_queue(&s->open_wait, &wait);
		__set_current_state(TASK_INTERRUPTIBLE);
		mutex_unlock(&s->open_mutex);
		schedule();
		remove_wait_queue(&s->open_wait, &wait);
		set_current_state(TASK_RUNNING);
		if (signal_pending(current))
			return -ERESTARTSYS;
		mutex_lock(&s->open_mutex);
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
	mutex_unlock(&s->open_mutex);
	mutex_init(&s->sem);
	return 0;
}

static int
au1550_release(struct inode *inode, struct file *file)
{
	struct au1550_state *s = (struct au1550_state *)file->private_data;

	lock_kernel();

	if (file->f_mode & FMODE_WRITE) {
		unlock_kernel();
		drain_dac(s, file->f_flags & O_NONBLOCK);
		lock_kernel();
	}

	mutex_lock(&s->open_mutex);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
		kfree(s->dma_dac.rawbuf);
		s->dma_dac.rawbuf = NULL;
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
		kfree(s->dma_adc.rawbuf);
		s->dma_adc.rawbuf = NULL;
	}
	s->open_mode &= ((~file->f_mode) & (FMODE_READ|FMODE_WRITE));
	mutex_unlock(&s->open_mutex);
	wake_up(&s->open_wait);
	unlock_kernel();
	return 0;
}

static /*const */ struct file_operations au1550_audio_fops = {
	owner:		THIS_MODULE,
	llseek:		au1550_llseek,
	read:		au1550_read,
	write:		au1550_write,
	poll:		au1550_poll,
	ioctl:		au1550_ioctl,
	mmap:		au1550_mmap,
	open:		au1550_open,
	release:	au1550_release,
};

MODULE_AUTHOR("Advanced Micro Devices (AMD), dan@embeddededge.com");
MODULE_DESCRIPTION("Au1550 AC97 Audio Driver");
MODULE_LICENSE("GPL");


static int __devinit
au1550_probe(void)
{
	struct au1550_state *s = &au1550_state;
	int             val;

	memset(s, 0, sizeof(struct au1550_state));

	init_waitqueue_head(&s->dma_adc.wait);
	init_waitqueue_head(&s->dma_dac.wait);
	init_waitqueue_head(&s->open_wait);
	mutex_init(&s->open_mutex);
	spin_lock_init(&s->lock);

	s->codec = ac97_alloc_codec();
	if(s->codec == NULL) {
		err("Out of memory");
		return -1;
	}
	s->codec->private_data = s;
	s->codec->id = 0;
	s->codec->codec_read = rdcodec;
	s->codec->codec_write = wrcodec;
	s->codec->codec_wait = waitcodec;

	if (!request_mem_region(CPHYSADDR(AC97_PSC_SEL),
			    0x30, "Au1550 AC97")) {
		err("AC'97 ports in use");
	}

	/* Allocate the DMA Channels
	*/
	if ((s->dma_dac.dmanr = au1xxx_dbdma_chan_alloc(DBDMA_MEM_CHAN,
	    DBDMA_AC97_TX_CHAN, dac_dma_interrupt, (void *)s)) == 0) {
		err("Can't get DAC DMA");
		goto err_dma1;
	}
	au1xxx_dbdma_set_devwidth(s->dma_dac.dmanr, 16);
	if (au1xxx_dbdma_ring_alloc(s->dma_dac.dmanr,
					NUM_DBDMA_DESCRIPTORS) == 0) {
		err("Can't get DAC DMA descriptors");
		goto err_dma1;
	}

	if ((s->dma_adc.dmanr = au1xxx_dbdma_chan_alloc(DBDMA_AC97_RX_CHAN,
	    DBDMA_MEM_CHAN, adc_dma_interrupt, (void *)s)) == 0) {
		err("Can't get ADC DMA");
		goto err_dma2;
	}
	au1xxx_dbdma_set_devwidth(s->dma_adc.dmanr, 16);
	if (au1xxx_dbdma_ring_alloc(s->dma_adc.dmanr,
					NUM_DBDMA_DESCRIPTORS) == 0) {
		err("Can't get ADC DMA descriptors");
		goto err_dma2;
	}

	pr_info("DAC: DMA%d, ADC: DMA%d", DBDMA_AC97_TX_CHAN, DBDMA_AC97_RX_CHAN);

	/* register devices */

	if ((s->dev_audio = register_sound_dsp(&au1550_audio_fops, -1)) < 0)
		goto err_dev1;
	if ((s->codec->dev_mixer =
	     register_sound_mixer(&au1550_mixer_fops, -1)) < 0)
		goto err_dev2;

	/* The GPIO for the appropriate PSC was configured by the
	 * board specific start up.
	 *
	 * configure PSC for AC'97
	 */
	au_writel(0, AC97_PSC_CTRL);	/* Disable PSC */
	au_sync();
	au_writel((PSC_SEL_CLK_SERCLK | PSC_SEL_PS_AC97MODE), AC97_PSC_SEL);
	au_sync();

	/* cold reset the AC'97
	*/
	au_writel(PSC_AC97RST_RST, PSC_AC97RST);
	au_sync();
	au1550_delay(10);
	au_writel(0, PSC_AC97RST);
	au_sync();

	/* need to delay around 500msec(bleech) to give
	   some CODECs enough time to wakeup */
	au1550_delay(500);

	/* warm reset the AC'97 to start the bitclk
	*/
	au_writel(PSC_AC97RST_SNC, PSC_AC97RST);
	au_sync();
	udelay(100);
	au_writel(0, PSC_AC97RST);
	au_sync();

	/* Enable PSC
	*/
	au_writel(PSC_CTRL_ENABLE, AC97_PSC_CTRL);
	au_sync();

	/* Wait for PSC ready.
	*/
	do {
		val = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((val & PSC_AC97STAT_SR) == 0);

	/* Configure AC97 controller.
	 * Deep FIFO, 16-bit sample, DMA, make sure DMA matches fifo size.
	 */
	val = PSC_AC97CFG_SET_LEN(16);
	val |= PSC_AC97CFG_RT_FIFO8 | PSC_AC97CFG_TT_FIFO8;

	/* Enable device so we can at least
	 * talk over the AC-link.
	 */
	au_writel(val, PSC_AC97CFG);
	au_writel(PSC_AC97MSK_ALLMASK, PSC_AC97MSK);
	au_sync();
	val |= PSC_AC97CFG_DE_ENABLE;
	au_writel(val, PSC_AC97CFG);
	au_sync();

	/* Wait for Device ready.
	*/
	do {
		val = au_readl(PSC_AC97STAT);
		au_sync();
	} while ((val & PSC_AC97STAT_DR) == 0);

	/* codec init */
	if (!ac97_probe_codec(s->codec))
		goto err_dev3;

	s->codec_base_caps = rdcodec(s->codec, AC97_RESET);
	s->codec_ext_caps = rdcodec(s->codec, AC97_EXTENDED_ID);
	pr_info("AC'97 Base/Extended ID = %04x/%04x",
	     s->codec_base_caps, s->codec_ext_caps);

	if (!(s->codec_ext_caps & AC97_EXTID_VRA)) {
		/* codec does not support VRA
		*/
		s->no_vra = 1;
	} else if (!vra) {
		/* Boot option says disable VRA
		*/
		u16 ac97_extstat = rdcodec(s->codec, AC97_EXTENDED_STATUS);
		wrcodec(s->codec, AC97_EXTENDED_STATUS,
			ac97_extstat & ~AC97_EXTSTAT_VRA);
		s->no_vra = 1;
	}
	if (s->no_vra)
		pr_info("no VRA, interpolating and decimating");

	/* set mic to be the recording source */
	val = SOUND_MASK_MIC;
	mixdev_ioctl(s->codec, SOUND_MIXER_WRITE_RECSRC,
		     (unsigned long) &val);

	return 0;

 err_dev3:
	unregister_sound_mixer(s->codec->dev_mixer);
 err_dev2:
	unregister_sound_dsp(s->dev_audio);
 err_dev1:
	au1xxx_dbdma_chan_free(s->dma_adc.dmanr);
 err_dma2:
	au1xxx_dbdma_chan_free(s->dma_dac.dmanr);
 err_dma1:
	release_mem_region(CPHYSADDR(AC97_PSC_SEL), 0x30);

	ac97_release_codec(s->codec);
	return -1;
}

static void __devinit
au1550_remove(void)
{
	struct au1550_state *s = &au1550_state;

	if (!s)
		return;
	synchronize_irq();
	au1xxx_dbdma_chan_free(s->dma_adc.dmanr);
	au1xxx_dbdma_chan_free(s->dma_dac.dmanr);
	release_mem_region(CPHYSADDR(AC97_PSC_SEL), 0x30);
	unregister_sound_dsp(s->dev_audio);
	unregister_sound_mixer(s->codec->dev_mixer);
	ac97_release_codec(s->codec);
}

static int __init
init_au1550(void)
{
	return au1550_probe();
}

static void __exit
cleanup_au1550(void)
{
	au1550_remove();
}

module_init(init_au1550);
module_exit(cleanup_au1550);

#ifndef MODULE

static int __init
au1550_setup(char *options)
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

__setup("au1550_audio=", au1550_setup);

#endif /* MODULE */
