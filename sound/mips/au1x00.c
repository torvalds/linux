/*
 * BRIEF MODULE DESCRIPTION
 *  Driver for AMD Au1000 MIPS Processor, AC'97 Sound Port
 *
 * Copyright 2004 Cooper Street Innovations Inc.
 * Author: Charles Eidsness	<charles@cooper-street.com>
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
 * History:
 *
 * 2004-09-09 Charles Eidsness	-- Original verion -- based on
 * 				  sa11xx-uda1341.c ALSA driver and the
 *				  au1000.c OSS driver.
 * 2004-09-09 Matt Porter	-- Added support for ALSA 1.0.6
 *
 */

#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1000_dma.h>

MODULE_AUTHOR("Charles Eidsness <charles@cooper-street.com>");
MODULE_DESCRIPTION("Au1000 AC'97 ALSA Driver");
MODULE_LICENSE("GPL");
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
MODULE_SUPPORTED_DEVICE("{{AMD,Au1000 AC'97}}");
#else
MODULE_CLASSES("{sound}");
MODULE_DEVICES("{{AMD,Au1000 AC'97}}");
#endif

#define PLAYBACK 0
#define CAPTURE 1
#define AC97_SLOT_3 0x01
#define AC97_SLOT_4 0x02
#define AC97_SLOT_6 0x08
#define AC97_CMD_IRQ 31
#define READ 0
#define WRITE 1
#define READ_WAIT 2
#define RW_DONE 3

DECLARE_WAIT_QUEUE_HEAD(ac97_command_wq);

typedef struct au1000_period au1000_period_t;
struct au1000_period
{
	u32 start;
	u32 relative_end;	/*realtive to start of buffer*/
	au1000_period_t * next;
};

/*Au1000 AC97 Port Control Reisters*/
typedef struct au1000_ac97_reg au1000_ac97_reg_t;
struct au1000_ac97_reg {
	u32 volatile config;
	u32 volatile status;
	u32 volatile data;
	u32 volatile cmd;
	u32 volatile cntrl;
};

typedef struct audio_stream audio_stream_t;
struct audio_stream {
	snd_pcm_substream_t * substream;
	int dma;
	spinlock_t dma_lock;
	au1000_period_t * buffer;
	unsigned long period_size;
};

typedef struct snd_card_au1000 {
	snd_card_t *card;
	au1000_ac97_reg_t volatile *ac97_ioport;

	struct resource *ac97_res_port;
	spinlock_t ac97_lock;
	ac97_t *ac97;

	snd_pcm_t *pcm;
	audio_stream_t *stream[2];	/* playback & capture */
} au1000_t;

static au1000_t *au1000 = NULL;

/*--------------------------- Local Functions --------------------------------*/
static void
au1000_set_ac97_xmit_slots(long xmit_slots)
{
	u32 volatile ac97_config;

	spin_lock(&au1000->ac97_lock);
	ac97_config = au1000->ac97_ioport->config;
	ac97_config = ac97_config & ~AC97C_XMIT_SLOTS_MASK;
	ac97_config |= (xmit_slots << AC97C_XMIT_SLOTS_BIT);
	au1000->ac97_ioport->config = ac97_config;
	spin_unlock(&au1000->ac97_lock);
}

static void
au1000_set_ac97_recv_slots(long recv_slots)
{
	u32 volatile ac97_config;

	spin_lock(&au1000->ac97_lock);
	ac97_config = au1000->ac97_ioport->config;
	ac97_config = ac97_config & ~AC97C_RECV_SLOTS_MASK;
	ac97_config |= (recv_slots << AC97C_RECV_SLOTS_BIT);
	au1000->ac97_ioport->config = ac97_config;
	spin_unlock(&au1000->ac97_lock);
}


static void
au1000_dma_stop(audio_stream_t *stream)
{
	unsigned long   flags;
	au1000_period_t * pointer;
	au1000_period_t * pointer_next;

	if (stream->buffer != NULL) {
		spin_lock_irqsave(&stream->dma_lock, flags);
		disable_dma(stream->dma);
		spin_unlock_irqrestore(&stream->dma_lock, flags);

		pointer = stream->buffer;
		pointer_next = stream->buffer->next;

		do {
			kfree(pointer);
			pointer = pointer_next;
			pointer_next = pointer->next;
		} while (pointer != stream->buffer);

		stream->buffer = NULL;
	}
}

static void
au1000_dma_start(audio_stream_t *stream)
{
	snd_pcm_substream_t *substream = stream->substream;
	snd_pcm_runtime_t *runtime = substream->runtime;

	unsigned long flags, dma_start;
	int i;
	au1000_period_t * pointer;

	if (stream->buffer == NULL) {
		dma_start = virt_to_phys(runtime->dma_area);

		stream->period_size = frames_to_bytes(runtime,
			runtime->period_size);
		stream->buffer = kmalloc(sizeof(au1000_period_t), GFP_KERNEL);
		pointer = stream->buffer;
		for (i = 0 ; i < runtime->periods ; i++) {
			pointer->start = (u32)(dma_start +
				(i * stream->period_size));
			pointer->relative_end = (u32)
				(((i+1) * stream->period_size) - 0x1);
			if ( i < runtime->periods - 1) {
				pointer->next = kmalloc(sizeof(au1000_period_t)
					, GFP_KERNEL);
				pointer = pointer->next;
			}
		}
		pointer->next = stream->buffer;

		spin_lock_irqsave(&stream->dma_lock, flags);
		init_dma(stream->dma);
		if (get_dma_active_buffer(stream->dma) == 0) {
			clear_dma_done0(stream->dma);
			set_dma_addr0(stream->dma, stream->buffer->start);
			set_dma_count0(stream->dma, stream->period_size >> 1);
			set_dma_addr1(stream->dma, stream->buffer->next->start);
			set_dma_count1(stream->dma, stream->period_size >> 1);
		} else {
			clear_dma_done1(stream->dma);
			set_dma_addr1(stream->dma, stream->buffer->start);
			set_dma_count1(stream->dma, stream->period_size >> 1);
			set_dma_addr0(stream->dma, stream->buffer->next->start);
			set_dma_count0(stream->dma, stream->period_size >> 1);
		}
		enable_dma_buffers(stream->dma);
		start_dma(stream->dma);
		spin_unlock_irqrestore(&stream->dma_lock, flags);
	}
}

static irqreturn_t
au1000_dma_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	audio_stream_t *stream = (audio_stream_t *) dev_id;
	snd_pcm_substream_t *substream = stream->substream;

	spin_lock(&stream->dma_lock);
	switch (get_dma_buffer_done(stream->dma)) {
	case DMA_D0:
		stream->buffer = stream->buffer->next;
		clear_dma_done0(stream->dma);
		set_dma_addr0(stream->dma, stream->buffer->next->start);
		set_dma_count0(stream->dma, stream->period_size >> 1);
		enable_dma_buffer0(stream->dma);
		break;
	case DMA_D1:
		stream->buffer = stream->buffer->next;
		clear_dma_done1(stream->dma);
		set_dma_addr1(stream->dma, stream->buffer->next->start);
		set_dma_count1(stream->dma, stream->period_size >> 1);
		enable_dma_buffer1(stream->dma);
		break;
	case (DMA_D0 | DMA_D1):
		spin_unlock(&stream->dma_lock);
		printk(KERN_ERR "DMA %d missed interrupt.\n",stream->dma);
		au1000_dma_stop(stream);
		au1000_dma_start(stream);
		spin_lock(&stream->dma_lock);
		break;
	case (~DMA_D0 & ~DMA_D1):
		printk(KERN_ERR "DMA %d empty irq.\n",stream->dma);
	}
	spin_unlock(&stream->dma_lock);
	snd_pcm_period_elapsed(substream);
	return IRQ_HANDLED;
}

/*-------------------------- PCM Audio Streams -------------------------------*/

static unsigned int rates[] = {8000, 11025, 16000, 22050};
static snd_pcm_hw_constraint_list_t hw_constraints_rates = {
	.count	=  sizeof(rates) / sizeof(rates[0]),
	.list	= rates,
	.mask	= 0,
};

static snd_pcm_hardware_t snd_au1000 =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED | \
				SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_11025 |
				SNDRV_PCM_RATE_16000 | SNDRV_PCM_RATE_22050),
	.rate_min		= 8000,
	.rate_max		= 22050,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,
	.period_bytes_min	= 32,
	.period_bytes_max	= 16*1024,
	.periods_min		= 8,
	.periods_max		= 255,
	.fifo_size		= 16,
};

static int
snd_au1000_playback_open(snd_pcm_substream_t * substream)
{
	au1000->stream[PLAYBACK]->substream = substream;
	au1000->stream[PLAYBACK]->buffer = NULL;
	substream->private_data = au1000->stream[PLAYBACK];
	substream->runtime->hw = snd_au1000;
	return (snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates) < 0);
}

static int
snd_au1000_capture_open(snd_pcm_substream_t * substream)
{
	au1000->stream[CAPTURE]->substream = substream;
	au1000->stream[CAPTURE]->buffer = NULL;
	substream->private_data = au1000->stream[CAPTURE];
	substream->runtime->hw = snd_au1000;
	return (snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates) < 0);

}

static int
snd_au1000_playback_close(snd_pcm_substream_t * substream)
{
	au1000->stream[PLAYBACK]->substream = NULL;
	return 0;
}

static int
snd_au1000_capture_close(snd_pcm_substream_t * substream)
{
	au1000->stream[CAPTURE]->substream = NULL;
	return 0;
}

static int
snd_au1000_hw_params(snd_pcm_substream_t * substream,
					snd_pcm_hw_params_t * hw_params)
{
	return snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
}

static int
snd_au1000_hw_free(snd_pcm_substream_t * substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int
snd_au1000_playback_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (runtime->channels == 1 )
		au1000_set_ac97_xmit_slots(AC97_SLOT_4);
	else
		au1000_set_ac97_xmit_slots(AC97_SLOT_3 | AC97_SLOT_4);
	snd_ac97_set_rate(au1000->ac97, AC97_PCM_FRONT_DAC_RATE, runtime->rate);
	return 0;
}

static int
snd_au1000_capture_prepare(snd_pcm_substream_t * substream)
{
	snd_pcm_runtime_t *runtime = substream->runtime;

	if (runtime->channels == 1 )
		au1000_set_ac97_recv_slots(AC97_SLOT_4);
	else
		au1000_set_ac97_recv_slots(AC97_SLOT_3 | AC97_SLOT_4);
	snd_ac97_set_rate(au1000->ac97, AC97_PCM_LR_ADC_RATE, runtime->rate);
	return 0;
}

static int
snd_au1000_trigger(snd_pcm_substream_t * substream, int cmd)
{
	audio_stream_t *stream = substream->private_data;
	int err = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		au1000_dma_start(stream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		au1000_dma_stop(stream);
		break;
	default:
		err = -EINVAL;
		break;
	}
	return err;
}

static snd_pcm_uframes_t
snd_au1000_pointer(snd_pcm_substream_t * substream)
{
	audio_stream_t *stream = substream->private_data;
	snd_pcm_runtime_t *runtime = substream->runtime;
	unsigned long flags;
	long location;

	spin_lock_irqsave(&stream->dma_lock, flags);
	location = get_dma_residue(stream->dma);
	spin_unlock_irqrestore(&stream->dma_lock, flags);
	location = stream->buffer->relative_end - location;
	if (location == -1)
		location = 0;
	return bytes_to_frames(runtime,location);
}

static snd_pcm_ops_t snd_card_au1000_playback_ops = {
	.open			= snd_au1000_playback_open,
	.close			= snd_au1000_playback_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	        = snd_au1000_hw_params,
	.hw_free	        = snd_au1000_hw_free,
	.prepare		= snd_au1000_playback_prepare,
	.trigger		= snd_au1000_trigger,
	.pointer		= snd_au1000_pointer,
};

static snd_pcm_ops_t snd_card_au1000_capture_ops = {
	.open			= snd_au1000_capture_open,
	.close			= snd_au1000_capture_close,
	.ioctl			= snd_pcm_lib_ioctl,
	.hw_params	        = snd_au1000_hw_params,
	.hw_free	        = snd_au1000_hw_free,
	.prepare		= snd_au1000_capture_prepare,
	.trigger		= snd_au1000_trigger,
	.pointer		= snd_au1000_pointer,
};

static int __devinit
snd_au1000_pcm_new(void)
{
	snd_pcm_t *pcm;
	int err;
	unsigned long flags;

	if ((err = snd_pcm_new(au1000->card, "AU1000 AC97 PCM", 0, 1, 1, &pcm)) < 0)
		return err;

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), 128*1024, 128*1024);

	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
		&snd_card_au1000_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
		&snd_card_au1000_capture_ops);

	pcm->private_data = au1000;
	pcm->info_flags = 0;
	strcpy(pcm->name, "Au1000 AC97 PCM");

	flags = claim_dma_lock();
	if ((au1000->stream[PLAYBACK]->dma = request_au1000_dma(DMA_ID_AC97C_TX,
			"AC97 TX", au1000_dma_interrupt, SA_INTERRUPT,
			au1000->stream[PLAYBACK])) < 0) {
		release_dma_lock(flags);
		return -EBUSY;
	}
	if ((au1000->stream[CAPTURE]->dma = request_au1000_dma(DMA_ID_AC97C_RX,
			"AC97 RX", au1000_dma_interrupt, SA_INTERRUPT,
			au1000->stream[CAPTURE])) < 0){
		release_dma_lock(flags);
		return -EBUSY;
	}
	/* enable DMA coherency in read/write DMA channels */
	set_dma_mode(au1000->stream[PLAYBACK]->dma,
		     get_dma_mode(au1000->stream[PLAYBACK]->dma) & ~DMA_NC);
	set_dma_mode(au1000->stream[CAPTURE]->dma,
		     get_dma_mode(au1000->stream[CAPTURE]->dma) & ~DMA_NC);
	release_dma_lock(flags);
	spin_lock_init(&au1000->stream[PLAYBACK]->dma_lock);
	spin_lock_init(&au1000->stream[CAPTURE]->dma_lock);
	au1000->pcm = pcm;
	return 0;
}


/*-------------------------- AC97 CODEC Control ------------------------------*/

static unsigned short
snd_au1000_ac97_read(ac97_t *ac97, unsigned short reg)
{
	u32 volatile cmd;
	u16 volatile data;
	int             i;
	spin_lock(&au1000->ac97_lock);
/* would rather use the interupt than this polling but it works and I can't
get the interupt driven case to work efficiently */
	for (i = 0; i < 0x5000; i++)
		if (!(au1000->ac97_ioport->status & AC97C_CP))
			break;
	if (i == 0x5000)
		printk(KERN_ERR "au1000 AC97: AC97 command read timeout\n");

	cmd = (u32) reg & AC97C_INDEX_MASK;
	cmd |= AC97C_READ;
	au1000->ac97_ioport->cmd = cmd;

	/* now wait for the data */
	for (i = 0; i < 0x5000; i++)
		if (!(au1000->ac97_ioport->status & AC97C_CP))
			break;
	if (i == 0x5000) {
		printk(KERN_ERR "au1000 AC97: AC97 command read timeout\n");
		return 0;
	}

	data = au1000->ac97_ioport->cmd & 0xffff;
	spin_unlock(&au1000->ac97_lock);

	return data;

}


static void
snd_au1000_ac97_write(ac97_t *ac97, unsigned short reg, unsigned short val)
{
	u32 cmd;
	int i;
	spin_lock(&au1000->ac97_lock);
/* would rather use the interupt than this polling but it works and I can't
get the interupt driven case to work efficiently */
	for (i = 0; i < 0x5000; i++)
		if (!(au1000->ac97_ioport->status & AC97C_CP))
			break;
	if (i == 0x5000)
		printk(KERN_ERR "au1000 AC97: AC97 command write timeout\n");

	cmd = (u32) reg & AC97C_INDEX_MASK;
	cmd &= ~AC97C_READ;
	cmd |= ((u32) val << AC97C_WD_BIT);
	au1000->ac97_ioport->cmd = cmd;
	spin_unlock(&au1000->ac97_lock);
}
static void
snd_au1000_ac97_free(ac97_t *ac97)
{
	au1000->ac97 = NULL;
}

static int __devinit
snd_au1000_ac97_new(void)
{
	int err;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
	ac97_bus_t *pbus;
	ac97_template_t ac97;
 	static ac97_bus_ops_t ops = {
		.write = snd_au1000_ac97_write,
		.read = snd_au1000_ac97_read,
	};
#else
	ac97_bus_t bus, *pbus;
	ac97_t ac97;
#endif

	if ((au1000->ac97_res_port = request_region(AC97C_CONFIG,
	       		sizeof(au1000_ac97_reg_t), "Au1x00 AC97")) == NULL) {
		snd_printk(KERN_ERR "ALSA AC97: can't grap AC97 port\n");
		return -EBUSY;
	}
	au1000->ac97_ioport = (au1000_ac97_reg_t *) au1000->ac97_res_port->start;

	spin_lock_init(&au1000->ac97_lock);

	spin_lock(&au1000->ac97_lock);

	/* configure pins for AC'97
	TODO: move to board_setup.c */
	au_writel(au_readl(SYS_PINFUNC) & ~0x02, SYS_PINFUNC);

	/* Initialise Au1000's AC'97 Control Block */
	au1000->ac97_ioport->cntrl = AC97C_RS | AC97C_CE;
	udelay(10);
	au1000->ac97_ioport->cntrl = AC97C_CE;
	udelay(10);

	/* Initialise External CODEC -- cold reset */
	au1000->ac97_ioport->config = AC97C_RESET;
	udelay(10);
	au1000->ac97_ioport->config = 0x0;
	mdelay(5);

	spin_unlock(&au1000->ac97_lock);

	/* Initialise AC97 middle-layer */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,8)
	if ((err = snd_ac97_bus(au1000->card, 0, &ops, au1000, &pbus)) < 0)
 		return err;
#else
	memset(&bus, 0, sizeof(bus));
	bus.write = snd_au1000_ac97_write;
	bus.read = snd_au1000_ac97_read;
	if ((err = snd_ac97_bus(au1000->card, &bus, &pbus)) < 0)
		return err;
#endif
	memset(&ac97, 0, sizeof(ac97));
	ac97.private_data = au1000;
	ac97.private_free = snd_au1000_ac97_free;
	if ((err = snd_ac97_mixer(pbus, &ac97, &au1000->ac97)) < 0)
		return err;
	return 0;

}

/*------------------------------ Setup / Destroy ----------------------------*/

void
snd_au1000_free(snd_card_t *card)
{

	if (au1000->ac97_res_port) {
		/* put internal AC97 block into reset */
		au1000->ac97_ioport->cntrl = AC97C_RS;
		au1000->ac97_ioport = NULL;
		release_and_free_resource(au1000->ac97_res_port);
	}

	if (au1000->stream[PLAYBACK]->dma >= 0)
		free_au1000_dma(au1000->stream[PLAYBACK]->dma);

	if (au1000->stream[CAPTURE]->dma >= 0)
		free_au1000_dma(au1000->stream[CAPTURE]->dma);

	kfree(au1000->stream[PLAYBACK]);
	au1000->stream[PLAYBACK] = NULL;
	kfree(au1000->stream[CAPTURE]);
	au1000->stream[CAPTURE] = NULL;
	kfree(au1000);
	au1000 = NULL;

}

static int __init
au1000_init(void)
{
	int err;

	au1000 = kmalloc(sizeof(au1000_t), GFP_KERNEL);
	if (au1000 == NULL)
		return -ENOMEM;
	au1000->stream[PLAYBACK] = kmalloc(sizeof(audio_stream_t), GFP_KERNEL);
	if (au1000->stream[PLAYBACK] == NULL)
		return -ENOMEM;
	au1000->stream[CAPTURE] = kmalloc(sizeof(audio_stream_t), GFP_KERNEL);
	if (au1000->stream[CAPTURE] == NULL)
		return -ENOMEM;
	/* so that snd_au1000_free will work as intended */
	au1000->stream[PLAYBACK]->dma = -1;
	au1000->stream[CAPTURE]->dma = -1;
 	au1000->ac97_res_port = NULL;

	au1000->card = snd_card_new(-1, "AC97", THIS_MODULE, sizeof(au1000_t));
	if (au1000->card == NULL) {
		snd_au1000_free(au1000->card);
		return -ENOMEM;
	}

	au1000->card->private_data = (au1000_t *)au1000;
	au1000->card->private_free = snd_au1000_free;

	if ((err = snd_au1000_ac97_new()) < 0 ) {
		snd_card_free(au1000->card);
		return err;
	}

	if ((err = snd_au1000_pcm_new()) < 0) {
		snd_card_free(au1000->card);
		return err;
	}

	strcpy(au1000->card->driver, "AMD-Au1000-AC97");
	strcpy(au1000->card->shortname, "Au1000-AC97");
	sprintf(au1000->card->longname, "AMD Au1000--AC97 ALSA Driver");

	if ((err = snd_card_set_generic_dev(au1000->card)) < 0) {
		snd_card_free(au1000->card);
		return err;
	}

	if ((err = snd_card_register(au1000->card)) < 0) {
		snd_card_free(au1000->card);
		return err;
	}

	printk( KERN_INFO "ALSA AC97: Driver Initialized\n" );
	return 0;
}

static void __exit au1000_exit(void)
{
	snd_card_free(au1000->card);
}

module_init(au1000_init);
module_exit(au1000_exit);

