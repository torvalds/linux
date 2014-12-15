#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/soundcard.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>
#include <linux/major.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
//#include <sound/rt5631.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>

#include <linux/amlogic/amports/amaudio.h>

#include <mach/mod_gate.h>

#include "aml_pcm.h"
#include "aml_audio_hw.h"
#include "aml_platform.h"

//#define _AML_PCM_DEBUG_
//

#define USE_HRTIMER 0
#define HRTIMER_PERIOD (1000000000UL/48000)

#define AOUT_EVENT_IEC_60958_PCM                0x1
#define AOUT_EVENT_RAWDATA_AC_3                 0x2
#define AOUT_EVENT_RAWDATA_MPEG1                0x3
#define AOUT_EVENT_RAWDATA_MP3                  0x4
#define AOUT_EVENT_RAWDATA_MPEG2                0x5
#define AOUT_EVENT_RAWDATA_AAC                  0x6
#define AOUT_EVENT_RAWDATA_DTS                  0x7
#define AOUT_EVENT_RAWDATA_ATRAC                0x8
#define AOUT_EVENT_RAWDATA_ONE_BIT_AUDIO        0x9
#define AOUT_EVENT_RAWDATA_DOBLY_DIGITAL_PLUS   0xA
#define AOUT_EVENT_RAWDATA_DTS_HD               0xB
#define AOUT_EVENT_RAWDATA_MAT_MLP              0xC
#define AOUT_EVENT_RAWDATA_DST                  0xD
#define AOUT_EVENT_RAWDATA_WMA_PRO              0xE

extern int aout_notifier_call_chain(unsigned long val, void *v);
extern void	aml_alsa_hw_reprepare(void);

extern unsigned IEC958_mode_raw;
extern unsigned IEC958_mode_codec;

unsigned int aml_i2s_playback_start_addr = 0;
unsigned int aml_i2s_capture_start_addr  = 0;
unsigned int aml_pcm_playback_end_addr = 0;
unsigned int aml_pcm_capture_end_addr = 0;

unsigned int aml_i2s_playback_phy_start_addr = 0;
unsigned int aml_i2s_capture_phy_start_addr = 0;
unsigned int aml_pcm_playback_phy_start_addr = 0;
unsigned int aml_pcm_capture_phy_start_addr  = 0;
unsigned int aml_pcm_playback_phy_end_addr = 0;
unsigned int aml_pcm_capture_phy_end_addr = 0;

unsigned int aml_i2s_playback_enable = 1;
unsigned int aml_i2s_capture_buf_size = 0;

unsigned int aml_iec958_playback_start_addr = 0;
unsigned int aml_iec958_playback_start_phy = 0;
unsigned int aml_iec958_playback_size = 0;  // in bytes

unsigned int aml_i2s_alsa_write_addr = 0;

static  unsigned  playback_substream_handle = 0 ;
/*to keep the pcm status for clockgating*/
//static unsigned clock_gating_status = 0;
//static unsigned clock_gating_playback = 1;
//static unsigned clock_gating_capture = 2;
static int audio_type_info = -1;
static int audio_sr_info = -1;
extern unsigned audioin_mode;
static unsigned trigger_underun = 0;


static int audio_ch_info = -1;

//static struct rt5631_platform_data *rt5631_snd_pdata = NULL;
//static struct aml_pcm_work_t{
//	struct snd_pcm_substream *substream;
//	struct work_struct aml_codec_workqueue;
//}aml_pcm_work;

int codec_power=1;
unsigned int flag=0;
//static int num=0;

//static int codec_power_switch(struct snd_pcm_substream *substream, unsigned int status);

EXPORT_SYMBOL(aml_i2s_playback_start_addr);
EXPORT_SYMBOL(aml_i2s_capture_start_addr);
EXPORT_SYMBOL(aml_i2s_playback_enable);
EXPORT_SYMBOL(aml_i2s_capture_buf_size);
EXPORT_SYMBOL(aml_i2s_playback_phy_start_addr);
EXPORT_SYMBOL(aml_i2s_capture_phy_start_addr);
EXPORT_SYMBOL(aml_i2s_alsa_write_addr);
#if 0
static void aml_codec_power_switch_queue(struct work_struct* work)
{

#ifdef _AML_PCM_DEBUG_
	printk("***Entered %s:%s\n", __FILE__,__func__);
#endif
	// disable power down/up, which caused pop noise
	//codec_power_switch(substream, clock_gating_status);
}
#endif
/*--------------------------------------------------------------------------*\
 * Hardware definition
\*--------------------------------------------------------------------------*/
/* TODO: These values were taken from the AML platform driver, check
 *	 them against real values for AML
 */
static const struct snd_pcm_hardware aml_pcm_hardware = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED|
							SNDRV_PCM_INFO_BLOCK_TRANSFER|
							SNDRV_PCM_INFO_PAUSE,

	.formats		= SNDRV_PCM_FMTBIT_S16_LE|SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S32_LE,

	.period_bytes_min	= 64,
	.period_bytes_max	= 32 * 1024,
	.periods_min		= 2,
	.periods_max		= 1024,
	.buffer_bytes_max	= 128 * 1024,

	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 8,
	.fifo_size = 0,
};

static const struct snd_pcm_hardware aml_pcm_capture = {
	.info			= SNDRV_PCM_INFO_INTERLEAVED|
							SNDRV_PCM_INFO_BLOCK_TRANSFER|
							SNDRV_PCM_INFO_MMAP |
						 	SNDRV_PCM_INFO_MMAP_VALID |
						  SNDRV_PCM_INFO_PAUSE,

	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.period_bytes_min	= 64,
	.period_bytes_max	= 32 * 1024,
	.periods_min		= 2,
	.periods_max		= 1024,
	.buffer_bytes_max	= 64 * 1024,

	.rate_min = 8000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 8,
	.fifo_size = 0,
};

static unsigned int period_sizes[] = { 64, 128, 256, 512, 1024, 2048, 4096, 8192 };

static struct snd_pcm_hw_constraint_list hw_constraints_period_sizes = {
	.count = ARRAY_SIZE(period_sizes),
	.list = period_sizes,
	.mask = 0
};

/*--------------------------------------------------------------------------*\
 * audio clock gating
\*--------------------------------------------------------------------------*/
#if 0
static void aml_audio_clock_gating_disable(void)
{
#ifdef _AML_PCM_DEBUG_
			printk("***Entered %s:%s\n", __FILE__,__func__);
#endif
/*
	aml_clr_reg32_mask(P_HHI_GCLK_MPEG0, (1<<18));
	aml_clr_reg32_mask(P_HHI_GCLK_MPEG1, (1<<2)
										|(0xba<<6)
								    	);
	aml_clr_reg32_mask(P_HHI_GCLK_OTHER, (1<<18)
										|(0x6<<14)
								    	);
 */   aml_clr_reg32_mask( P_HHI_AUD_CLK_CNTL, (1 << 8));

	//printk("P_HHI_GCLK_MPEG0=disable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_MPEG0));
	//printk("P_HHI_GCLK_MPEG1=disable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_MPEG1));
	//printk("P_HHI_GCLK_OTHER=disable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_OTHER));
}

static void aml_audio_clock_gating_enable(void)
{
#ifdef _AML_PCM_DEBUG_
			printk("***Entered %s:%s\n", __FILE__,__func__);
#endif
/*	aml_set_reg32_mask(P_HHI_GCLK_MPEG0, (1<<18));
	aml_set_reg32_mask(P_HHI_GCLK_MPEG1, (1<<2)
								    	|(0xba<<6)
								   		 );
	aml_set_reg32_mask(P_HHI_GCLK_OTHER, (1<<18)
								    	|(0x6<<14)
								    	);
*/    aml_set_reg32_mask( P_HHI_AUD_CLK_CNTL, (1 << 8));
	//printk("P_HHI_GCLK_MPEG0=enable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_MPEG0));
	//printk("P_HHI_GCLK_MPEG1=enable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_MPEG1));
	//printk("P_HHI_GCLK_OTHER=enable--%#x\n\n", aml_read_reg32(P_HHI_GCLK_OTHER));
}
#endif

#if 0
static void aml_clock_gating(unsigned int status)
{
//printk("-----status=%d\n\n",status);
	if(status){
		aml_audio_clock_gating_enable();
	}
	else{
		aml_audio_clock_gating_disable();
	}
}
#endif
/*--------------------------------------------------------------------------*\
 * audio codec power management
\*--------------------------------------------------------------------------*/
#if 0
static int codec_power_switch(struct snd_pcm_substream *substream, unsigned int status)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
#ifdef _AML_PCM_DEBUG_
			printk("***Entered %s:%s\n", __FILE__,__func__);
#endif
	if(status && codec_dai->driver->ops->startup)
			codec_dai->driver->ops->startup(substream, codec_dai);

	if((flag)&& (!status) && (codec_dai->driver->ops->shutdown))
			codec_dai->driver->ops->shutdown(substream, codec_dai);
	return 0;
}
#endif
/*--------------------------------------------------------------------------*\
 * Helper functions
\*--------------------------------------------------------------------------*/
static int aml_pcm_preallocate_dma_buffer(struct snd_pcm *pcm,
	int stream)
{
	struct snd_pcm_substream *substream = pcm->streams[stream].substream;
	struct snd_dma_buffer *buf = &substream->dma_buffer;

	size_t size = 0;

    if(pcm->device == 0)
    {
	    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		    size = aml_pcm_hardware.buffer_bytes_max;
		    buf->dev.type = SNDRV_DMA_TYPE_DEV;
		    buf->dev.dev = pcm->card->dev;
		    buf->private_data = NULL;
            /* one size for i2s output, another for 958, and 128 for alignment */
		    buf->area = dma_alloc_coherent(pcm->card->dev, size+4096,
					  &buf->addr, GFP_KERNEL);
		    printk("aml-pcm %d:"
		    "playback preallocate_dma_buffer: area=%p, addr=%p, size=%d\n", stream,
		    (void *) buf->area,
		    (void *) buf->addr,
		    size);

            /* alloc iec958 buffer */

            aml_i2s_playback_start_addr = (unsigned int)buf->area;
		    aml_pcm_playback_end_addr = (unsigned int)buf->area + size;

			aml_pcm_playback_phy_start_addr = buf->addr;
			aml_pcm_playback_phy_end_addr = buf->addr+size;
			aml_i2s_playback_phy_start_addr = aml_pcm_playback_phy_start_addr;

        /* alloc iec958 buffer */
        aml_iec958_playback_start_addr = (unsigned int)dma_alloc_coherent(pcm->card->dev, size*4,
           (dma_addr_t *)(&aml_iec958_playback_start_phy), GFP_KERNEL);
        if(aml_iec958_playback_start_addr == 0){
          printk("aml-pcm %d: alloc iec958 buffer failed\n", stream);
          return -ENOMEM;
        }
        aml_iec958_playback_size = size*4;
        printk("iec958 %d: preallocate dma buffer start=%p, size=%x\n", stream, (void*)aml_iec958_playback_start_addr, size*4);
	}else{
		size = aml_pcm_capture.buffer_bytes_max;
		buf->dev.type = SNDRV_DMA_TYPE_DEV;
		buf->dev.dev = pcm->card->dev;
		buf->private_data = NULL;
		buf->area = dma_alloc_coherent(pcm->card->dev, size*2,
					  &buf->addr, GFP_KERNEL);
		    printk("aml-pcm %d:"
		    "capture preallocate_dma_buffer: area=%p, addr=%p, size=%d\n", stream,
		    (void *) buf->area,
		    (void *) buf->addr,
		    size);

            aml_i2s_capture_start_addr = (unsigned int)buf->area;
		    aml_pcm_capture_end_addr = (unsigned int)buf->area+size;
		    aml_i2s_capture_buf_size = size;
		    aml_pcm_capture_phy_start_addr = buf->addr;
		    aml_pcm_capture_phy_end_addr = buf->addr+size;
			aml_i2s_capture_phy_start_addr = aml_pcm_capture_phy_start_addr;

	    }

	    if (!buf->area)
		    return -ENOMEM;

	    buf->bytes = size;
	    return 0;
    }
    else
    {
	    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		    size = aml_pcm_hardware.buffer_bytes_max;
		    buf->dev.type = SNDRV_DMA_TYPE_DEV;
		    buf->dev.dev = pcm->card->dev;
		    buf->private_data = NULL;
            /* one size for i2s output, another for 958, and 128 for alignment */
		    //buf->area = dma_alloc_coherent(pcm->card->dev, size+4096,
					  //&buf->addr, GFP_KERNEL);
            buf->area = (unsigned char *)aml_i2s_playback_start_addr;
            buf->addr = aml_pcm_playback_phy_start_addr;
		    printk("aml-pcm %d:"
		    "dev>0 playback preallocate_dma_buffer: area=%p, addr=%p, size=%d\n", stream,
		    (void *) buf->area,
		    (void *) buf->addr,
		    size);

        }else{
		    size = aml_pcm_capture.buffer_bytes_max;
		    buf->dev.type = SNDRV_DMA_TYPE_DEV;
		    buf->dev.dev = pcm->card->dev;
		    buf->private_data = NULL;
		    //buf->area = dma_alloc_coherent(pcm->card->dev, size*2,
		    //			  &buf->addr, GFP_KERNEL);
		    buf->area = (unsigned char *)aml_i2s_capture_start_addr;
            buf->addr = aml_pcm_capture_phy_start_addr;
		    printk("aml-pcm %d:"
		    "dev>0 capture preallocate_dma_buffer: area=%p, addr=%p, size=%d\n", stream,
		    (void *) buf->area,
		    (void *) buf->addr,
		    size);
	    }

	    if (!buf->area)
		    return -ENOMEM;

	    buf->bytes = size;
	    return 0;

    }
}
/*--------------------------------------------------------------------------*\
 * ISR
\*--------------------------------------------------------------------------*/


/*--------------------------------------------------------------------------*\
 * PCM operations
\*--------------------------------------------------------------------------*/
static int aml_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aml_runtime_data *prtd = runtime->private_data;
//	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	audio_stream_t *s = &prtd->s;

	/* this may get called several times by oss emulation
	 * with different params */

	snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
	runtime->dma_bytes = params_buffer_bytes(params);
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		aml_iec958_playback_size = runtime->dma_bytes*4;
	s->I2S_addr = runtime->dma_addr;

    /*
     * Both capture and playback need to reset the last ptr to the start address,
       playback and capture use different address calculate, so we reset the different
       start address to the last ptr
   * */
    if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
        /* s->last_ptr must initialized as dma buffer's start addr */
        s->last_ptr = runtime->dma_addr;
    }else{

	s->last_ptr = 0;
    }

	return 0;
}

static int aml_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct aml_runtime_data *prtd = substream->runtime->private_data;
	struct aml_pcm_dma_params *params = prtd->params;
	if (params != NULL) {

	}

	return 0;
}
/*
the I2S hw  and IEC958 PCM output initation,958 initation here,
for the case that only use our ALSA driver for PCM s/pdif output.
*/
static void  aml_hw_i2s_init(struct snd_pcm_runtime *runtime)
{


		switch(runtime->format){
		case SNDRV_PCM_FORMAT_S32_LE:
			I2S_MODE = AIU_I2S_MODE_PCM32;
		// IEC958_MODE = AIU_958_MODE_PCM32;
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			I2S_MODE = AIU_I2S_MODE_PCM24;
		// IEC958_MODE = AIU_958_MODE_PCM24;
			break;
		case SNDRV_PCM_FORMAT_S16_LE:
			I2S_MODE = AIU_I2S_MODE_PCM16;
		// IEC958_MODE = AIU_958_MODE_PCM16;
			break;
		}
		audio_set_i2s_mode(I2S_MODE);
		audio_set_aiubuf(runtime->dma_addr, runtime->dma_bytes, runtime->channels);
		memset((void*)runtime->dma_area,0,runtime->dma_bytes + 4096);
		/* update the i2s hw buffer end addr as android may update that */
		aml_pcm_playback_phy_end_addr = aml_pcm_playback_phy_start_addr+runtime->dma_bytes;
		printk("I2S hw init,i2s mode %d\n",I2S_MODE);

}
/*add audio_hdmi_init_ready check ,as hdmi audio may init fails:
AIU_HDMI_CLK_DATA_CTRL can be writen sucessfully when audio PLL OFF.
so notify HDMI to set audio parameter every time when HDMI AUDIO not ready
*/

static void audio_notify_hdmi_info(int audio_type, void *v){
    	struct snd_pcm_substream *substream =(struct snd_pcm_substream*)v;
	if(substream->runtime->rate != audio_sr_info || audio_type_info != audio_type || !audio_hdmi_init_ready()
		|| substream->runtime->channels != audio_ch_info)
	{
		printk("audio info changed.notify to hdmi: type %d,sr %d,ch %d\n",audio_type,substream->runtime->rate,
			substream->runtime->channels);
		aout_notifier_call_chain(audio_type,v);
	}
	audio_sr_info = substream->runtime->rate;
	audio_ch_info = substream->runtime->channels;
	audio_type_info = audio_type;

}
#if 0
static void iec958_notify_hdmi_info(void)
{
	unsigned audio_type = AOUT_EVENT_IEC_60958_PCM;
	if(playback_substream_handle){
		if(IEC958_mode_codec == 2) //dd
			audio_type = AOUT_EVENT_RAWDATA_AC_3;
		else if(IEC958_mode_codec == 4)//dd+
			audio_type = AOUT_EVENT_RAWDATA_DOBLY_DIGITAL_PLUS;
		else if(IEC958_mode_codec == 1|| IEC958_mode_codec == 3)//dts
			audio_type = AOUT_EVENT_RAWDATA_DTS;
		else
			audio_type = AOUT_EVENT_IEC_60958_PCM;
		printk("iec958 nodify hdmi audio type %d\n",	audio_type);
		audio_notify_hdmi_info(audio_type, (struct snd_pcm_substream *)playback_substream_handle);
	}
	else{
		printk("substream for playback NULL\n");
	}

}
#endif
/*
special call by the audiodsp,add these code,as there are three cases for 958 s/pdif output
1)NONE-PCM  raw output ,only available when ac3/dts audio,when raw output mode is selected by user.
2)PCM  output for  all audio, when pcm mode is selected by user .
3)PCM  output for audios except ac3/dts,when raw output mode is selected by user
*/
static void aml_hw_iec958_init(struct snd_pcm_substream *substream)
{
    _aiu_958_raw_setting_t set;
    _aiu_958_channel_status_t chstat;
    unsigned start,size;
    unsigned sr = 48000;	
	memset((void*)(&set), 0, sizeof(set));
	memset((void*)(&chstat), 0, sizeof(chstat));
	set.chan_stat = &chstat;
	if(substream){
		struct snd_pcm_runtime *runtime = substream->runtime;
		sr  = runtime->rate;
	}
   	/* case 1,raw mode enabled */
	if(IEC958_mode_codec){
	  if(IEC958_mode_codec == 1){ //dts, use raw sync-word mode
	    	IEC958_MODE = AIU_958_MODE_RAW;
		printk("iec958 mode RAW\n");
	  }
	  else{ //ac3,use the same pcm mode as i2s configuration
		IEC958_MODE = AIU_958_MODE_PCM_RAW;
		printk("iec958 mode %s\n",(I2S_MODE == AIU_I2S_MODE_PCM32)?"PCM32_RAW":((I2S_MODE == AIU_I2S_MODE_PCM24)?"PCM24_RAW":"PCM16_RAW"));
	  }
	}else{	/* case 2,3 */
	  if(I2S_MODE == AIU_I2S_MODE_PCM32)
	  	IEC958_MODE = AIU_958_MODE_PCM32;
	  else if(I2S_MODE == AIU_I2S_MODE_PCM24)
	  	IEC958_MODE = AIU_958_MODE_PCM24;
	  else
	  	IEC958_MODE = AIU_958_MODE_PCM16;
  	  printk("iec958 mode %s\n",(I2S_MODE == AIU_I2S_MODE_PCM32)?"PCM32":((I2S_MODE == AIU_I2S_MODE_PCM24)?"PCM24":"PCM16"));
	}

	if(IEC958_MODE == AIU_958_MODE_PCM16 || IEC958_MODE == AIU_958_MODE_PCM24 ||
	  IEC958_MODE == AIU_958_MODE_PCM32){
	    set.chan_stat->chstat0_l = 0x0100;
		set.chan_stat->chstat0_r = 0x0100;
        start = (aml_pcm_playback_phy_start_addr);
        size = aml_pcm_playback_phy_end_addr - aml_pcm_playback_phy_start_addr;
		audio_set_958outbuf(start, size, 0);
	  }else{
		set.chan_stat->chstat0_l = 0x1902;//NONE-PCM
		set.chan_stat->chstat0_r = 0x1902;
        // start = ((aml_pcm_playback_phy_end_addr + 4096)&(~127));
        // size  = aml_pcm_playback_phy_end_addr - aml_pcm_playback_phy_start_addr;
        start = aml_iec958_playback_start_phy;
        size = aml_iec958_playback_size;
		audio_set_958outbuf(start, size, (IEC958_MODE == AIU_958_MODE_RAW)?1:0);
		memset((void*)aml_iec958_playback_start_addr,0,size);
	}
	  /* set the channel status bit for sample rate */
	printk("aml_hw_iec958_init audio sr %d \n",  sr);
	if(IEC958_mode_codec == 4){
		if(sr == 32000){
			set.chan_stat->chstat1_l = 0x300;
			set.chan_stat->chstat1_r = 0x300;
		}
		else if(sr == 44100){
			set.chan_stat->chstat1_l = 0xc00;
			set.chan_stat->chstat1_r = 0xc00;			
		}
		else{
			set.chan_stat->chstat1_l = 0Xe00;
			set.chan_stat->chstat1_r = 0Xe00;			
		}		
	}
	else{  
		if(sr == 32000){
			set.chan_stat->chstat1_l = 0x300;
			set.chan_stat->chstat1_r = 0x300;
		}
		else if(sr == 44100){
			set.chan_stat->chstat1_l = 0;
			set.chan_stat->chstat1_r = 0;			
		}
		else{
			set.chan_stat->chstat1_l = 0X200;
			set.chan_stat->chstat1_r = 0X200;			
		}
	}
	audio_set_958_mode(IEC958_MODE, &set);
	if(IEC958_mode_codec == 4)  //dd+
		WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 0, 4, 2); // 4x than i2s
	else
#if OVERCLOCK == 1 || IEC958_OVERCLOCK == 1
		WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 3, 4, 2);//512fs divide 4 == 128fs
#else
		WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 1, 4, 2); //256fs divide 2 == 128fs
#endif
//	iec958_notify_hdmi_info();


}

void	aml_alsa_hw_reprepare(void)
{
	/* diable 958 module before call initiation */
	//audio_hw_958_enable(0);
  	//aml_hw_iec958_init((struct snd_pcm_substream *)playback_substream_handle);
  	trigger_underun = 1;

}


static int aml_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aml_runtime_data *prtd = runtime->private_data;
	audio_stream_t *s = &prtd->s;
    int iec958 = 0;

	if(prtd == 0)
		return 0;

	switch(runtime->rate){
		case 192000:
			s->sample_rate	=	AUDIO_CLK_FREQ_192;
			break;
		case 176400:
			s->sample_rate	=	AUDIO_CLK_FREQ_1764;
			break;
		case 96000:
			s->sample_rate	=	AUDIO_CLK_FREQ_96;
			break;
		case 88200:
			s->sample_rate	=	AUDIO_CLK_FREQ_882;
			break;
		case 48000:
			s->sample_rate	=	AUDIO_CLK_FREQ_48;
            iec958 = 2;
			break;
		case 44100:
			s->sample_rate	=	AUDIO_CLK_FREQ_441;
            iec958 = 0;
			break;
		case 32000:
			s->sample_rate	=	AUDIO_CLK_FREQ_32;
            iec958 = 3;
			break;
		case 8000:
			s->sample_rate	=	AUDIO_CLK_FREQ_8;
			break;
		case 11025:
			s->sample_rate	=	AUDIO_CLK_FREQ_11;
			break;
		case 16000:
			s->sample_rate	=	AUDIO_CLK_FREQ_16;
			break;
		case 22050:
			s->sample_rate	=	AUDIO_CLK_FREQ_22;
			break;
		case 12000:
			s->sample_rate	=	AUDIO_CLK_FREQ_12;
			break;
		case 24000:
			s->sample_rate	=	AUDIO_CLK_FREQ_22;
			break;
		default:
			s->sample_rate	=	AUDIO_CLK_FREQ_441;
			break;
	};
	// iec958 and i2s clock are separated since M8
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
	audio_set_clk(s->sample_rate, AUDIO_CLK_256FS);
	audio_util_set_dac_format(AUDIO_ALGOUT_DAC_FORMAT_DSP);
#else
	audio_set_i2s_clk(s->sample_rate, AUDIO_CLK_256FS);
	audio_set_958_clk(s->sample_rate, AUDIO_CLK_256FS);
	audio_util_set_dac_i2s_format(AUDIO_ALGOUT_DAC_FORMAT_DSP);
	audio_util_set_dac_958_format(AUDIO_ALGOUT_DAC_FORMAT_DSP);
#endif

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
			trigger_underun = 0;
			aml_hw_i2s_init(runtime);
		  	aml_hw_iec958_init(substream);
	}
	else{
			//printk("aml_pcm_prepare SNDRV_PCM_STREAM_CAPTURE: dma_addr=%x, dma_bytes=%x\n", runtime->dma_addr, runtime->dma_bytes);
			audio_in_i2s_set_buf(runtime->dma_addr, runtime->dma_bytes*2,audioin_mode);
			memset((void*)runtime->dma_area,0,runtime->dma_bytes*2);
            {
			  int * ppp = (int*)(runtime->dma_area+runtime->dma_bytes*2-8);
			  ppp[0] = 0x78787878;
			  ppp[1] = 0x78787878;
            }
	}
    if( IEC958_MODE == AIU_958_MODE_PCM_RAW){
		if(IEC958_mode_codec == 4 ){ // need Over clock for dd+
		    WRITE_MPEG_REG_BITS(AIU_CLK_CTRL, 0, 4, 2);	// 4x than i2s
		    audio_notify_hdmi_info(AOUT_EVENT_RAWDATA_DOBLY_DIGITAL_PLUS, substream);
		}else if(IEC958_mode_codec == 3 ||IEC958_mode_codec == 1 ){ // no-over clock for dts pcm mode
		    audio_notify_hdmi_info(AOUT_EVENT_RAWDATA_DTS, substream);
		}
		else  //dd
			audio_notify_hdmi_info(AOUT_EVENT_RAWDATA_AC_3, substream);

    }else if(IEC958_mode_codec == 1){
        audio_notify_hdmi_info(AOUT_EVENT_RAWDATA_DTS, substream);
    }else{
				audio_notify_hdmi_info(AOUT_EVENT_IEC_60958_PCM, substream);
    }

#if 0
	printk("Audio Parameters:\n");
	printk("\tsample rate: %d\n", runtime->rate);
	printk("\tchannel: %d\n", runtime->channels);
	printk("\tsample bits: %d\n", runtime->sample_bits);
  printk("\tformat: %s\n", snd_pcm_format_name(runtime->format));
	printk("\tperiod size: %ld\n", runtime->period_size);
	printk("\tperiods: %d\n", runtime->periods);
  printk("\tiec958 mode: %d, raw=%d, codec=%d\n", IEC958_MODE, IEC958_mode_raw, IEC958_mode_codec);
#endif

	return 0;
}

static int aml_pcm_trigger(struct snd_pcm_substream *substream,
	int cmd)
{
	struct snd_pcm_runtime *rtd = substream->runtime;
	struct aml_runtime_data *prtd = rtd->private_data;
	audio_stream_t *s = &prtd->s;
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	 //witch_mod_gate_by_type(MOD_AUDIO, 1);
#endif

#if USE_HRTIMER == 0
	  del_timer_sync(&prtd->timer);
#endif
	  spin_lock(&s->lock);
#if USE_HRTIMER == 0
	  prtd->timer.expires = jiffies + 1;
	  del_timer(&prtd->timer);
	  add_timer(&prtd->timer);
#endif
	  // TODO
	  if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
	        //printk("aml_pcm_trigger: playback start\n");
			//clock_gating_status |= clock_gating_playback;
			//aml_clock_gating(clock_gating_status);
			//codec_power_switch(substream, clock_gating_status);
		    audio_enable_ouput(1);
	  }else{
	  		//printk("aml_pcm_trigger: capture start\n");
			//clock_gating_status |= clock_gating_capture;
			//aml_clock_gating(clock_gating_status);
			//codec_power_switch(substream, clock_gating_status);
			audio_in_i2s_enable(1);
	      {
		  int * ppp = (int*)(rtd->dma_area+rtd->dma_bytes*2-8);
		  ppp[0] = 0x78787878;
		  ppp[1] = 0x78787878;
	      }

	  }

	  s->active = 1;
	  s->xrun_num = 0;
	  spin_unlock(&s->lock);
	  break;		/* SNDRV_PCM_TRIGGER_START */
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		// TODO
	    spin_lock(&s->lock);
	    s->active = 0;
		s->xrun_num = 0;
	    if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
            //printk("aml_pcm_trigger: playback stop\n");
	  		audio_enable_ouput(0);
		//	clock_gating_status &= ~clock_gating_playback;
			//aml_clock_gating(clock_gating_status);
			//codec_power_switch(substream, clock_gating_status);
	    }else{
            //printk("aml_pcm_trigger: capture stop\n");
		//	clock_gating_status &= ~clock_gating_capture;

			audio_in_i2s_enable(0);
	    }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	//  switch_mod_gate_by_type(MOD_AUDIO, 0);
#endif

	    spin_unlock(&s->lock);
	    break;

	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		// TODO
	    spin_lock(&s->lock);
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
	  //switch_mod_gate_by_type(MOD_AUDIO, 1);
#endif
	    s->active = 1;
		s->xrun_num = 0;
	    if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
            //printk("aml_pcm_trigger: playback resume\n");
			audio_enable_ouput(1);
		//	clock_gating_status |= clock_gating_playback;
			//aml_clock_gating(clock_gating_status);
			//codec_power_switch(substream, clock_gating_status);
	    }else{
            //printk("aml_pcm_trigger: capture resume\n");
	        audio_in_i2s_enable(1);
		//	clock_gating_status |= clock_gating_capture;
			//aml_clock_gating(clock_gating_status);
			//codec_power_switch(substream, clock_gating_status);
		{
		    int * ppp = (int*)(rtd->dma_area+rtd->dma_bytes*2-8);
		    ppp[0] = 0x78787878;
		    ppp[1] = 0x78787878;
	        }
	    }
	    spin_unlock(&s->lock);
	    break;
	default:
		ret = -EINVAL;
	}
/*	if(clock_gating_status&clock_gating_playback){
		if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			aml_pcm_work.substream = substream;
	}
	else
		aml_pcm_work.substream = substream;


	if(clock_gating_status)
	{
		schedule_work(&aml_pcm_work.aml_codec_workqueue);
	}
	*/
	//schedule_work(&aml_pcm_work.aml_codec_workqueue);
	return ret;
}

static snd_pcm_uframes_t aml_pcm_pointer(
	struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aml_runtime_data *prtd = runtime->private_data;
	audio_stream_t *s = &prtd->s;

	unsigned int addr, ptr;

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
			ptr = read_i2s_rd_ptr();
	    addr = ptr - s->I2S_addr;
	    return bytes_to_frames(runtime, addr);
	}else{
			ptr = audio_in_i2s_wr_ptr();
			addr = ptr - s->I2S_addr;
			return bytes_to_frames(runtime, addr)/2;
	}

	return 0;
}

#if USE_HRTIMER != 0
static enum hrtimer_restart aml_pcm_hrtimer_callback(struct hrtimer* timer)
{
  struct aml_runtime_data* prtd =  container_of(timer, struct aml_runtime_data, hrtimer);
  audio_stream_t* s = &prtd->s;
  struct snd_pcm_substream* substream = prtd->substream;
  struct snd_pcm_runtime* runtime= substream->runtime;

  unsigned int last_ptr, size;
//  unsigned long flag;
  //printk("------------->hrtimer start\n");
  if(s->active == 0){
    hrtimer_forward_now(timer, ns_to_ktime(HRTIMER_PERIOD));
    return HRTIMER_RESTART;
  }
  //spin_lock_irqsave(&s->lock, flag);

  if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
      last_ptr = read_i2s_rd_ptr();
      if(last_ptr < s->last_ptr){
        size = runtime->dma_bytes + last_ptr - s->last_ptr;
      }else{
        size = last_ptr - s->last_ptr;
      }
      s->last_ptr = last_ptr;
      s->size += bytes_to_frames(substream->runtime, size);
      if(s->size >= runtime->period_size){
        s->size %= runtime->period_size;
        snd_pcm_period_elapsed(substream);
      }
  }else{
      last_ptr = (audio_in_i2s_wr_ptr() - s->I2S_addr) /2;
      if(last_ptr < s->last_ptr){
        size = runtime->dma_bytes + last_ptr - s->last_ptr;
      }else{
        size = last_ptr - s->last_ptr;
      }
      s->last_ptr = last_ptr;
      s->size += bytes_to_frames(runtime, size);
      if(s->size >= runtime->period_size){
        s->size %= runtime->period_size;
        snd_pcm_period_elapsed(substream);
      }
  }
  //spin_unlock_irqrestore(&s->lock, flag);
  hrtimer_forward_now(timer, ns_to_ktime(HRTIMER_PERIOD));
  return HRTIMER_RESTART;
}
#endif
static void aml_pcm_timer_callback(unsigned long data)
{
    struct snd_pcm_substream *substream = (struct snd_pcm_substream *)data;
    struct snd_pcm_runtime *runtime = substream->runtime;
    struct aml_runtime_data *prtd = runtime->private_data;
    audio_stream_t *s = &prtd->s;

    unsigned int last_ptr = 0, size = 0;
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
				if(s->active == 1){
						spin_lock(&s->lock);
						last_ptr = read_i2s_rd_ptr();
						if (last_ptr < s->last_ptr) {
				        size = runtime->dma_bytes + last_ptr - (s->last_ptr);
				    } else {
				        size = last_ptr - (s->last_ptr);
				    }
    				s->last_ptr = last_ptr;
    				s->size += bytes_to_frames(substream->runtime, size);
    				if (s->size >= runtime->period_size) {
				        s->size %= runtime->period_size;
				        spin_unlock(&s->lock);
				        snd_pcm_period_elapsed(substream);
				        spin_lock(&s->lock);
				    }
				    mod_timer(&prtd->timer, jiffies + 1);
					//codec_power = 1;
   					spin_unlock(&s->lock);
				}else{

						 mod_timer(&prtd->timer, jiffies + 1);
						// codec_power = 0;

				}

		}else{
				if(s->active == 1){
						spin_lock(&s->lock);
						last_ptr = (audio_in_i2s_wr_ptr() - s->I2S_addr) / 2;
						if (last_ptr < s->last_ptr) {
				        size = runtime->dma_bytes + last_ptr - (s->last_ptr);
				    } else if(last_ptr == s->last_ptr){
				        if(s->xrun_num ++ > 100){
							printk(KERN_INFO "alsa capture long time no data, quit xrun !\n");
							s->xrun_num = 0;
							s->size = runtime->period_size;
				        }
					} else {
				        size = last_ptr - (s->last_ptr);
				    }
    				s->last_ptr = last_ptr;
    				s->size += bytes_to_frames(substream->runtime, size);
    				if (s->size >= runtime->period_size) {
				        s->size %= runtime->period_size;
				        spin_unlock(&s->lock);
				        snd_pcm_period_elapsed(substream);
				        spin_lock(&s->lock);
				    }
				    mod_timer(&prtd->timer, jiffies + 1);
   					spin_unlock(&s->lock);
				}else{
						 mod_timer(&prtd->timer, jiffies + 1);
				}
		}
	/*	if((codec_power==0) && (num==500))
		{
			num=0;
	   		flag=1;
			schedule_work(&aml_pcm_work.aml_codec_workqueue);
		}
	   else if((codec_power==1) && (num <= 500))
	   	{
	   		num=0;
			flag = 0;
	   	}
	   else if((codec_power==0) && (num<500))
	   	{
	   	    if(flag==1)
	   	    {}
			else
			{
				num++;
			}
	   	} */
}


static int aml_pcm_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct aml_runtime_data *prtd = NULL;
	int ret = 0;
	void *buffer = NULL;
	unsigned int buffersize = 0;
	audio_stream_t *s = NULL;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		playback_substream_handle = (unsigned long)substream;
		snd_soc_set_runtime_hwparams(substream, &aml_pcm_hardware);
		buffersize = aml_pcm_hardware.period_bytes_max;
	}else{
		snd_soc_set_runtime_hwparams(substream, &aml_pcm_capture);
		buffersize = aml_pcm_capture.period_bytes_max;
	}

    /* ensure that peroid size is a multiple of 32bytes */
	ret = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, &hw_constraints_period_sizes);
	if (ret < 0)
	{
		printk("set period bytes constraint error\n");
		goto out;
	}

	/* ensure that buffer size is a multiple of period size */
	ret = snd_pcm_hw_constraint_integer(runtime,
						SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
	{
		printk("set period error\n");
		goto out;
	}

	prtd = kzalloc(sizeof(struct aml_runtime_data), GFP_KERNEL);
	if (prtd == NULL) {
		printk("alloc aml_runtime_data error\n");
		ret = -ENOMEM;
		goto out;
	}
	prtd->substream = substream;
	runtime->private_data = prtd;
	if(!prtd->buf){
		buffer = kzalloc(buffersize, GFP_KERNEL);
		if (buffer==NULL){
			printk("alloc aml_runtime_data buffer error\n");
			kfree(prtd);
			ret = -ENOMEM;
			goto out;
		}
		prtd->buf = buffer;
	}
#if USE_HRTIMER == 0
	prtd->timer.function = &aml_pcm_timer_callback;
	prtd->timer.data = (unsigned long)substream;
	init_timer(&prtd->timer);
#else
    hrtimer_init(&prtd->hrtimer,CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    prtd->hrtimer.function = aml_pcm_hrtimer_callback;
    hrtimer_start(&prtd->hrtimer, ns_to_ktime(HRTIMER_PERIOD), HRTIMER_MODE_REL);


    printk("hrtimer inited..\n");
#endif
	

	spin_lock_init(&prtd->s.lock);
	s = &prtd->s;
	s->xrun_num = 0;
 out:
	return ret;
}

static int aml_pcm_close(struct snd_pcm_substream *substream)
{
	struct aml_runtime_data *prtd = substream->runtime->private_data;

#if USE_HRTIMER == 0
	del_timer_sync(&prtd->timer);
#else
    hrtimer_cancel(&prtd->hrtimer);
#endif
	if(prtd->buf){
		kfree(prtd->buf);
		prtd->buf = NULL;
	}
	if(prtd){
 		kfree(prtd);
		prtd = NULL;
	}

	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		playback_substream_handle = 0;
	return 0;
}


static int aml_pcm_copy_playback(struct snd_pcm_runtime *runtime, int channel,
		    snd_pcm_uframes_t pos,
		    void __user *buf, snd_pcm_uframes_t count)
{
    int res = 0;
    int n;
    int i = 0, j = 0;
    int  align = runtime->channels * 32 / runtime->byte_align;
    char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, pos);
    struct aml_runtime_data *prtd = runtime->private_data;
    void *ubuf = prtd->buf;
	aml_i2s_alsa_write_addr = frames_to_bytes(runtime, pos);
    n = frames_to_bytes(runtime, count);
    if(aml_i2s_playback_enable == 0)
      return res;
    if(trigger_underun){
		printk("trigger underun \n");
		return -EFAULT;
    }
    res = copy_from_user(ubuf, buf, n);
    if (res) return -EFAULT;
    if(access_ok(VERIFY_READ, buf, frames_to_bytes(runtime, count))){
	  if(runtime->format == SNDRV_PCM_FORMAT_S16_LE && I2S_MODE == AIU_I2S_MODE_PCM16){
        int16_t * tfrom, *to, *left, *right;
        tfrom = (int16_t*)ubuf;
        to = (int16_t*)hwbuf;

        left = to;
		right = to + 16;
		if (pos % align) {
		    printk("audio data unligned: pos=%d, n=%d, align=%d\n", (int)pos, n, align);
		}
		for (j = 0; j < n; j += 64) {
		    for (i = 0; i < 16; i++) {
	          *left++ = (*tfrom++) ;
	          *right++ = (*tfrom++);
		    }
		    left += 16;
		    right += 16;
		 }
      }else if(runtime->format == SNDRV_PCM_FORMAT_S24_LE && I2S_MODE == AIU_I2S_MODE_PCM24){
        int32_t *tfrom, *to, *left, *right;
        tfrom = (int32_t*)ubuf;
        to = (int32_t*) hwbuf;

        left = to;
        right = to + 8;

        if(pos % align){
          printk("audio data unaligned: pos=%d, n=%d, align=%d\n", (int)pos, n, align);
        }
        for(j=0; j< n; j+= 64){
          for(i=0; i<8; i++){
            *left++  =  (*tfrom ++);
            *right++  = (*tfrom ++);
          }
          left += 8;
          right += 8;
        }

      }else if(runtime->format == SNDRV_PCM_FORMAT_S32_LE && I2S_MODE == AIU_I2S_MODE_PCM32){
        int32_t *tfrom, *to, *left, *right;
        tfrom = (int32_t*)ubuf;
        to = (int32_t*) hwbuf;

        left = to;
        right = to + 8;

        if(pos % align){
          printk("audio data unaligned: pos=%d, n=%d, align=%d\n", (int)pos, n, align);
        }

		if(runtime->channels == 8){
			int32_t *lf, *cf, *rf, *ls, *rs, *lef, *sbl, *sbr;
			lf  = to;
			cf  = to + 8*1;
			rf  = to + 8*2;
			ls  = to + 8*3;
			rs  = to + 8*4;
			lef = to + 8*5;
			sbl = to + 8*6;
			sbr = to + 8*7;
			for (j = 0; j < n; j += 256) {
		    	for (i = 0; i < 8; i++) {
	         		*lf++  = (*tfrom ++)>>8;
	          		*cf++  = (*tfrom ++)>>8;
					*rf++  = (*tfrom ++)>>8;
					*ls++  = (*tfrom ++)>>8;
					*rs++  = (*tfrom ++)>>8;
					*lef++ = (*tfrom ++)>>8;
					*sbl++ = (*tfrom ++)>>8;
					*sbr++ = (*tfrom ++)>>8;
		    	}
		    	lf  += 7*8;
		    	cf  += 7*8;
				rf  += 7*8;
				ls  += 7*8;
				rs  += 7*8;
				lef += 7*8;
				sbl += 7*8;
				sbr += 7*8;
		 	}
		}
		else {
        for(j=0; j< n; j+= 64){
          for(i=0; i<8; i++){
            *left++  =  (*tfrom ++)>>8;
            *right++  = (*tfrom ++)>>8;
          }
          left += 8;
          right += 8;
        }
      	}
      }

	}else{
	  res = -EFAULT;
	}

	return res;
}

static unsigned int aml_get_in_wr_ptr(void){
	return (audio_in_i2s_wr_ptr() - aml_i2s_capture_phy_start_addr);
}

static int aml_pcm_copy_capture(struct snd_pcm_runtime *runtime, int channel,
		    snd_pcm_uframes_t pos,
		    void __user *buf, snd_pcm_uframes_t count)
{
		unsigned int *tfrom, *left, *right;
		unsigned short *to;
		int res = 0;
		int n;
    int i = 0, j = 0;
    unsigned int t1, t2;
    char *hwbuf = runtime->dma_area + frames_to_bytes(runtime, pos)*2;
    unsigned int buffersize = (unsigned int)runtime->buffer_size*8;  //512*4*4*2
    unsigned int hw_ptr = aml_get_in_wr_ptr();
    unsigned int alsa_read_ptr = frames_to_bytes(runtime, pos)*2;
    int size = (buffersize + hw_ptr - alsa_read_ptr)%buffersize;
    unsigned char r_shift = 8;
    struct aml_runtime_data *prtd = runtime->private_data;
    void *ubuf = prtd->buf;
    if(audioin_mode&SPDIFIN_MODE) //spdif in
    {
    	r_shift = 12;
    }
    to = (unsigned short *)ubuf;//tmp buf;
    tfrom = (unsigned int *)hwbuf;	// 32bit buffer
    n = frames_to_bytes(runtime, count);
    if(n > 32*1024){
      printk("Too many datas to read\n");
      return -EINVAL;
    }
	if(size < 2*n){
		printk("Alsa ptr is too close to HW ptr, Reset ALSA!\n");
		return -EPIPE;
	}
		if(access_ok(VERIFY_WRITE, buf, frames_to_bytes(runtime, count))){
				left = tfrom;
		    right = tfrom + 8;
		    if (pos % 8) {
		        printk("audio data unligned\n");
		    }
		    if((n*2)%64){
		    		printk("audio data unaligned 64 bytes\n");
		    }
		    for (j = 0; j < n*2 ; j += 64) {
		        for (i = 0; i < 8; i++) {
		        	t1 = (*left++);
		        	t2 = (*right++);
	                *to++ = (unsigned short)((t1>>r_shift)&0xffff);
	                *to++ = (unsigned short)((t2>>r_shift)&0xffff);
		         }
		        left += 8;
		        right += 8;
		    }
		}
        res = copy_to_user(buf, ubuf, n);
		return res;
}

static int aml_pcm_copy(struct snd_pcm_substream *substream, int channel,
		    snd_pcm_uframes_t pos,
		    void __user *buf, snd_pcm_uframes_t count)
{
    struct snd_pcm_runtime *runtime = substream->runtime;
    int ret = 0;

 	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
 		ret = aml_pcm_copy_playback(runtime, channel,pos, buf, count);
 	}else{
 		ret = aml_pcm_copy_capture(runtime, channel,pos, buf, count);
 	}
    return ret;
}

int aml_pcm_silence(struct snd_pcm_substream *substream, int channel,
		       snd_pcm_uframes_t pos, snd_pcm_uframes_t count)
{
		char* ppos;
		int n;
		struct snd_pcm_runtime *runtime = substream->runtime;

		n = frames_to_bytes(runtime, count);
		ppos = runtime->dma_area + frames_to_bytes(runtime, pos);
		memset(ppos, 0, n);
		return 0;
}

static struct snd_pcm_ops aml_pcm_ops = {
	.open		= aml_pcm_open,
	.close		= aml_pcm_close,
	.ioctl		= snd_pcm_lib_ioctl,
	.hw_params	= aml_pcm_hw_params,
	.hw_free	= aml_pcm_hw_free,
	.prepare	= aml_pcm_prepare,
	.trigger	= aml_pcm_trigger,
	.pointer	= aml_pcm_pointer,
	.copy 		= aml_pcm_copy,
	.silence	=	aml_pcm_silence,
};


/*--------------------------------------------------------------------------*\
 * ASoC platform driver
\*--------------------------------------------------------------------------*/
static u64 aml_pcm_dmamask = 0xffffffff;

static int aml_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
       struct snd_soc_card *card = rtd->card;
       struct snd_pcm *pcm =rtd->pcm ;
	if (!card->dev->dma_mask)
		card->dev->dma_mask = &aml_pcm_dmamask;
	if (!card->dev->coherent_dma_mask)
		card->dev->coherent_dma_mask = 0xffffffff;

	if (pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream) {
		ret = aml_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_PLAYBACK);
		if (ret)
			goto out;
	}

	if (pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream) {
		pr_debug("aml-pcm:"
				"Allocating PCM capture DMA buffer\n");
		ret = aml_pcm_preallocate_dma_buffer(pcm,
			SNDRV_PCM_STREAM_CAPTURE);
		if (ret)
			goto out;
	}

 out:
	return ret;
}

static void aml_pcm_free_dma_buffers(struct snd_pcm *pcm)
{
	struct snd_pcm_substream *substream;
	struct snd_dma_buffer *buf;
	int stream;
	for (stream = 0; stream < 2; stream++) {
		substream = pcm->streams[stream].substream;
		if (!substream)
			continue;

		buf = &substream->dma_buffer;
		if (!buf->area)
			continue;
		dma_free_coherent(pcm->card->dev, buf->bytes,
				  buf->area, buf->addr);
		buf->area = NULL;
	}
    aml_i2s_playback_start_addr = 0;
    aml_i2s_capture_start_addr  = 0;

    if(aml_iec958_playback_start_addr){
      dma_free_coherent(pcm->card->dev, aml_iec958_playback_size, ( void *)aml_iec958_playback_start_addr, aml_iec958_playback_start_phy);
      aml_iec958_playback_start_addr = 0;
    }
}

#if 0
static int aml_pcm_suspend(struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct aml_runtime_data *prtd;
	struct aml_pcm_dma_params *params;
	if (!runtime)
		return 0;

	prtd = runtime->private_data;
	params = prtd->params;

	/* disable the PDC and save the PDC registers */
	// TODO
	printk("aml pcm suspend\n");

	return 0;
}

static int aml_pcm_resume(struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = dai->runtime;
	struct aml_runtime_data *prtd;
	struct aml_pcm_dma_params *params;
	if (!runtime)
		return 0;

	prtd = runtime->private_data;
	params = prtd->params;

	/* restore the PDC registers and enable the PDC */
	// TODO
	printk("aml pcm resume\n");
	return 0;
}
#else
#define aml_pcm_suspend	NULL
#define aml_pcm_resume	NULL
#endif

#ifdef CONFIG_DEBUG_FS

//static struct dentry *debugfs_root;
//static struct dentry *debugfs_regs;
//static struct dentry *debugfs_mems;

static int regs_open_file(struct inode *inode, struct file *file)
{
	return 0;
}

/**
 *	cat regs
 */
static ssize_t regs_read_file(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	ssize_t ret;
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = sprintf(buf, "Usage: \n"
										 "	echo base reg val >regs\t(set the register)\n"
										 "	echo base reg >regs\t(show the register)\n"
										 "	base -> c(cbus), x(aix), p(apb), h(ahb) \n"
									);

	if (ret >= 0)
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);

	return ret;
}

static int read_regs(char base, int reg)
{
	int val = 0;
	switch(base){
		case 'c':
			val = READ_CBUS_REG(reg);
			break;
		case 'x':
			val = READ_AXI_REG(reg);
			break;
		case 'p':
			val = READ_APB_REG(reg);
			break;
		case 'h':
			//val = READ_AHB_REG(reg);
			break;
		default:
			break;
	};
	printk("\tReg %x = %x\n", reg, val);
	return val;
}

static void write_regs(char base, int reg, int val)
{
	switch(base){
		case 'c':
			WRITE_CBUS_REG(reg, val);
			break;
		case 'x':
			WRITE_AXI_REG(reg, val);
			break;
		case 'p':
			WRITE_APB_REG(reg, val);
			break;
		case 'h':
			//WRITE_AHB_REG(reg, val);
			break;
		default:
			break;
	};
	printk("Write reg:%x = %x\n", reg, val);
}
static ssize_t regs_write_file(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[32];
	int buf_size = 0;
	char *start = buf;
	unsigned long reg, value;
	char base;

	buf_size = min(count, (sizeof(buf)-1));

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;
	while (*start == ' ')
		start++;

	base = *start;
	start ++;
	if(!(base =='c' || base == 'x' || base == 'p' || base == 'h')){
		return -EINVAL;
	}

	while (*start == ' ')
		start++;

	reg = simple_strtoul(start, &start, 16);

	while (*start == ' ')
		start++;

	if (strict_strtoul(start, 16, &value))
	{
			read_regs(base, reg);
			return -EINVAL;
	}

	write_regs(base, reg, value);

	return buf_size;
}

static const struct file_operations regs_fops = {
	.open = regs_open_file,
	.read = regs_read_file,
	.write = regs_write_file,
};

static int mems_open_file(struct inode *inode, struct file *file)
{
	return 0;
}
static ssize_t mems_read_file(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	ssize_t ret;
	char *buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = sprintf(buf, "Usage: \n"
										 "	echo vmem >mems\t(read 64 bytes from vmem)\n"
										 "	echo vmem val >mems (write int value to vmem\n"
									);

	if (ret >= 0)
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, ret);
	kfree(buf);

	return ret;
}

static ssize_t mems_write_file(struct file *file,
		const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[256];
	int buf_size = 0;
	char *start = buf;
	unsigned long mem, value;
	int i=0;
	unsigned* addr = 0;

	buf_size = min(count, (sizeof(buf)-1));

	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	buf[buf_size] = 0;

	while (*start == ' ')
		start++;

	mem = simple_strtoul(start, &start, 16);

	while (*start == ' ')
		start++;

	if (strict_strtoul(start, 16, &value))
	{
			addr = (unsigned*)mem;
			printk("%p: ", addr);
			for(i = 0; i< 8; i++){
				printk("%08x, ", addr[i]);
			}
			printk("\n");
			return -EINVAL;
	}
	addr = (unsigned*)mem;
	printk("%p: %08x\n", addr, *addr);
	*addr = value;
	printk("%p: %08x^\n", addr, *addr);

	return buf_size;
}
static const struct file_operations mems_fops={
	.open = mems_open_file,
	.read = mems_read_file,
	.write = mems_write_file,
};
#if 0
static void aml_pcm_init_debugfs(void)
{
		debugfs_root = debugfs_create_dir("aml",NULL);
		if (IS_ERR(debugfs_root) || !debugfs_root) {
			printk("aml: Failed to create debugfs directory\n");
			debugfs_root = NULL;
		}

		debugfs_regs = debugfs_create_file("regs", 0644, debugfs_root, NULL, &regs_fops);
		if(!debugfs_regs){
			printk("aml: Failed to create debugfs file\n");
		}

		debugfs_mems = debugfs_create_file("mems", 0644, debugfs_root, NULL, &mems_fops);
		if(!debugfs_mems){
			printk("aml: Failed to create debugfs file\n");
		}
}
static void aml_pcm_cleanup_debugfs(void)
{
	debugfs_remove_recursive(debugfs_root);
}
#endif
#else
static void aml_pcm_init_debugfs(void)
{
}
static void aml_pcm_cleanup_debugfs(void)
{
}
#endif

struct aml_audio_interface aml_i2s_interface = {
    .id = AML_AUDIO_I2S,
    .name = "I2S",
    .pcm_ops = &aml_pcm_ops,
    .pcm_new = aml_pcm_new,
    .pcm_free =  aml_pcm_free_dma_buffers,
};
#if 0
struct snd_soc_platform_driver aml_soc_platform = {
	.ops 	= &aml_pcm_ops,
	.pcm_new	= aml_pcm_new,
	.pcm_free	= aml_pcm_free_dma_buffers,
	.suspend	= aml_pcm_suspend,
	.resume		= aml_pcm_resume,
};

EXPORT_SYMBOL_GPL(aml_soc_platform);

static int aml_soc_platform_probe(struct platform_device *pdev)
{
	INIT_WORK(&aml_pcm_work.aml_codec_workqueue, aml_codec_power_switch_queue);
	/* get audioin cfg data from board */
	if(pdev->dev.platform_data){
		audioin_mode = *(unsigned *)pdev->dev.platform_data;
		printk("AML soc audio in mode =============   %d \n",audioin_mode);
	}
	return snd_soc_register_platform(&pdev->dev, &aml_soc_platform);
}

static int aml_soc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id amlogic_audio_dt_match[]={
	{	.compatible = "amlogic,aml-audio",
	},
	{},
};
#else
#define amlogic_audio_dt_match NULL
#endif

static struct platform_driver aml_pcm_driver = {
	.driver = {
			.name = "aml-audio",
			.owner = THIS_MODULE,
			.of_match_table = amlogic_audio_dt_match,
	},

	.probe = aml_soc_platform_probe,
	.remove = aml_soc_platform_remove,
};

static int __init aml_alsa_audio_init(void)
{
	aml_pcm_init_debugfs();
	return platform_driver_register(&aml_pcm_driver);
}

static void __exit aml_alsa_audio_exit(void)
{
	aml_pcm_cleanup_debugfs();
    platform_driver_unregister(&aml_pcm_driver);
}

module_init(aml_alsa_audio_init);
module_exit(aml_alsa_audio_exit);

MODULE_AUTHOR("AMLogic, Inc.");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AML audio driver for ALSA");
#endif
