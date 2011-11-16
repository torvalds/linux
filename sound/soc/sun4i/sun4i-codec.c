/*
 *   Driver for CODEC on M1 soundcard
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License.
 *
*
***************************************************************************************************/
#define DEBUG
#ifndef CONFIG_PM
#define CONFIG_PM
#endif
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <mach/dma.h>
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif
#include <asm/mach-types.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <linux/clk.h>
#include <linux/timer.h>
#include "sun4i-codec.h"
#include <mach/sys_config.h>
#include <mach/system.h>

#define SCRIPT_AUDIO_OK (0)
static int gpio_pa_shutdown = 0;
struct clk *codec_apbclk,*codec_pll2clk,*codec_moduleclk;

static volatile unsigned int capture_dmasrc = 0;
static volatile unsigned int capture_dmadst = 0;
static volatile unsigned int play_dmasrc = 0;
static volatile unsigned int play_dmadst = 0;

/* Structure/enum declaration ------------------------------- */
typedef struct codec_board_info {
	struct device	*dev;	     		/* parent device */
	struct resource	*codec_base_res;   /* resources found */
	struct resource	*codec_base_req;   /* resources found */

	spinlock_t	lock;
} codec_board_info_t;

/* ID for this card */
static struct sw_dma_client sw_codec_dma_client_play = {
	.name		= "CODEC PCM Stereo PLAY"
};

static struct sw_dma_client sw_codec_dma_client_capture = {
	.name		= "CODEC PCM Stereo CAPTURE"
};

static struct sw_pcm_dma_params sw_codec_pcm_stereo_play = {
	.client		= &sw_codec_dma_client_play,
	.channel	= DMACH_NADDA_PLAY,
	.dma_addr	= CODEC_BASSADDRESS + SW_DAC_TXDATA,//发送数据地址
	.dma_size	= 4,
};

static struct sw_pcm_dma_params sw_codec_pcm_stereo_capture = {
	.client		= &sw_codec_dma_client_capture,
	.channel	= DMACH_NADDA_CAPTURE,  //only support half full
	.dma_addr	= CODEC_BASSADDRESS + SW_ADC_RXDATA,//接收数据地址
	.dma_size	= 4,
};

struct sw_playback_runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t   dma_start;
	dma_addr_t   dma_pos;
	dma_addr_t	 dma_end;
	struct sw_pcm_dma_params	*params;
};

struct sw_capture_runtime_data {
	spinlock_t lock;
	int state;
	unsigned int dma_loaded;
	unsigned int dma_limit;
	unsigned int dma_period;
	dma_addr_t   dma_start;
	dma_addr_t   dma_pos;
	dma_addr_t	 dma_end;
	struct sw_pcm_dma_params	*params;
};

/*播放设备硬件定义*/
static struct snd_pcm_hardware sw_pcm_playback_hardware =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |SNDRV_PCM_RATE_11025 |\
				   SNDRV_PCM_RATE_22050| SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100| SNDRV_PCM_RATE_48000 |SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_192000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,//最大的缓冲区大小
	.period_bytes_min	= 1024*16,//最小周期大小
	.period_bytes_max	= 1024*32,//最大周期大小
	.periods_min		= 4,//最小周期数
	.periods_max		= 8,//最大周期数
	.fifo_size	     	= 32,//fifo字节数
};

/*录音设备硬件定义*/
static struct snd_pcm_hardware sw_pcm_capture_hardware =
{
	.info			= (SNDRV_PCM_INFO_INTERLEAVED |
				   SNDRV_PCM_INFO_BLOCK_TRANSFER |
				   SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
				   SNDRV_PCM_INFO_PAUSE | SNDRV_PCM_INFO_RESUME),
	.formats		= SNDRV_PCM_FMTBIT_S16_LE,
	.rates			= (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |SNDRV_PCM_RATE_11025 |\
				   SNDRV_PCM_RATE_22050| SNDRV_PCM_RATE_32000 |\
				   SNDRV_PCM_RATE_44100| SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 |SNDRV_PCM_RATE_192000 |\
				   SNDRV_PCM_RATE_KNOT),
	.rate_min		= 8000,
	.rate_max		= 192000,
	.channels_min		= 1,
	.channels_max		= 2,
	.buffer_bytes_max	= 128*1024,//最大的缓冲区大小
	.period_bytes_min	= 1024*16,//最小周期大小
	.period_bytes_max	= 1024*32,//最大周期大小
	.periods_min		= 4,//最小周期数
	.periods_max		= 8,//最大周期数
	.fifo_size	     	= 32,//fifo字节数
};

struct sw_codec{
	long samplerate;
	struct snd_card *card;
	struct snd_pcm *pcm;
};

static void codec_resume_events(struct work_struct *work);
struct workqueue_struct *resume_work_queue;
static DECLARE_WORK(codec_resume_work, codec_resume_events);

static unsigned int rates[] = {
	8000,11025,12000,16000,
	22050,24000,24000,32000,
	44100,48000,96000,192000
};

static struct snd_pcm_hw_constraint_list hw_constraints_rates = {
	.count	= ARRAY_SIZE(rates),
	.list	= rates,
	.mask	= 0,
};

/**
* codec_wrreg_bits - update codec register bits
* @reg: codec register
* @mask: register mask
* @value: new value
*
* Writes new register value.
* Return 1 for change else 0.
*/
int codec_wrreg_bits(unsigned short reg, unsigned int	mask,	unsigned int value)
{
	int change;
	unsigned int old, new;

	old	=	codec_rdreg(reg);
	new	=	(old & ~mask) | value;
	change = old != new;

	if (change){
		codec_wrreg(reg,new);
	}

	return change;
}

/**
*	snd_codec_info_volsw	-	single	mixer	info	callback
*	@kcontrol:	mixer control
*	@uinfo:	control	element	information
*	Callback to provide information about a single mixer control
*
* 	info()函数用于获得该control的详细信息，该函数必须填充传递给它的第二个参数snd_ctl_elem_info结构体
*
*	Returns 0 for success
*/
int snd_codec_info_volsw(struct snd_kcontrol *kcontrol,
		struct	snd_ctl_elem_info	*uinfo)
{
	struct	codec_mixer_control *mc	= (struct codec_mixer_control*)kcontrol->private_value;
	int	max	=	mc->max;
	unsigned int shift  = mc->shift;
	unsigned int rshift = mc->rshift;

	if(max	== 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;//the info of type
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = shift ==	rshift	?	1:	2;	//the info of elem count
	uinfo->value.integer.min = 0;				//the info of min value
	uinfo->value.integer.max = max;				//the info of max value
	return	0;
}

/**
*	snd_codec_get_volsw	-	single	mixer	get	callback
*	@kcontrol:	mixer	control
*	@ucontrol:	control	element	information
*
*	Callback to get the value of a single mixer control
*	get()函数用于得到control的目前值并返回用户空间
*	return 0 for success.
*/
int snd_codec_get_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{
	struct codec_mixer_control *mc= (struct codec_mixer_control*)kcontrol->private_value;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int	max = mc->max;
	/*fls(7) = 3,fls(1)=1,fls(0)=0,fls(15)=4,fls(3)=2,fls(23)=5*/
	unsigned int mask = (1 << fls(max)) -1;
	unsigned int invert = mc->invert;
	unsigned int reg = mc->reg;

	ucontrol->value.integer.value[0] =
		(codec_rdreg(reg)>>	shift) & mask;
	if(shift != rshift)
		ucontrol->value.integer.value[1] =
			(codec_rdreg(reg) >> rshift) & mask;

	/*将获得的值写入snd_ctl_elem_value*/
	if(invert){
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];
		if(shift != rshift)
			ucontrol->value.integer.value[1] =
				max - ucontrol->value.integer.value[1];
		}

		return 0;
}

/**
*	snd_codec_put_volsw	-	single	mixer put callback
*	@kcontrol:	mixer	control
*	@ucontrol:	control	element	information
*
*	put()用于从用户空间写入值，如果值被改变，该函数返回1，否则返回0.
*	Callback to put the value of a single mixer control
*
* return 0 for success.
*/
int snd_codec_put_volsw(struct	snd_kcontrol	*kcontrol,
	struct	snd_ctl_elem_value	*ucontrol)
{
	struct codec_mixer_control *mc= (struct codec_mixer_control*)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int rshift = mc->rshift;
	int max = mc->max;
	unsigned int mask = (1<<fls(max))-1;
	unsigned int invert = mc->invert;
	unsigned int	val, val2, val_mask;

	val = (ucontrol->value.integer.value[0] & mask);
	if(invert)
		val = max - val;
	val <<= shift;
	val_mask = mask << shift;
	if(shift != rshift){
		val2	= (ucontrol->value.integer.value[1] & mask);
		if(invert)
			val2	=	max	- val2;
		val_mask |= mask <<rshift;
		val |= val2 <<rshift;
	}
	pr_debug("Val = %d\n",val);
	return codec_wrreg_bits(reg,val_mask,val);
}

int codec_wr_control(u32 reg, u32 mask, u32 shift, u32 val)
{
	u32 reg_val;
	reg_val = val << shift;
	mask = mask << shift;
	codec_wrreg_bits(reg, mask, reg_val);
	return 0;
}

int codec_rd_control(u32 reg, u32 bit, u32 *val)
{
	return 0;
}

/**
*	codec_reset - reset the codec
* @codec	SoC Audio Codec
* Reset the codec, set the register of codec default value
* Return 0 for success
*/
static  int codec_init(void)
{
	int device_lr_change = 0;
	enum sw_ic_ver  codec_chip_ver = sw_get_ic_ver();
	//enable dac digital
	codec_wr_control(SW_DAC_DPC, 0x1, DAC_EN, 0x1);
	//codec version seting
	//codec_wr_control(SW_DAC_DPC ,  0x1, DAC_VERSION, 0x1);
	codec_wr_control(SW_DAC_FIFOC ,  0x1,28, 0x1);
	//set digital volume to maximum
	if(codec_chip_ver == MAGIC_VER_A){
		codec_wr_control(SW_DAC_DPC, 0x6, DIGITAL_VOL, 0x0);
	}
	//pa mute
	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x0);
	//enable PA
	codec_wr_control(SW_ADC_ACTL, 0x1, PA_ENABLE, 0x1);
	codec_wr_control(SW_DAC_FIFOC, 0x3, DRA_LEVEL,0x3);
	//enable headphone direct
//	codec_wr_control(SW_ADC_ACTL, 0x1, HP_DIRECT, 0x1);
	//set volume
	if(codec_chip_ver == MAGIC_VER_A){
		codec_wr_control(SW_DAC_ACTL, 0x6, VOLUME, 0x01);
	}else if(codec_chip_ver == MAGIC_VER_B){
		codec_wr_control(SW_DAC_ACTL, 0x6, VOLUME, 0x3b);
	}else{
		printk("[audio codec] chip version is unknown!\n");
		return -1;
	}
	if(SCRIPT_AUDIO_OK != script_parser_fetch("audio_para", "audio_lr_change", &device_lr_change, sizeof(device_lr_change)/sizeof(int))){
		pr_err("audiocodec_adap_awxx_init: script_parser_fetch err. \n");
	    return -1;
	}
	pr_debug("device_lr_change == %d. \n", device_lr_change);
	if(device_lr_change)
		codec_wr_control(SW_DAC_DEBUG ,  0x1, DAC_CHANNEL, 0x1);
	return 0;
}

static int codec_play_open(struct snd_pcm_substream *substream)
{
	codec_wr_control(SW_DAC_DPC ,  0x1, DAC_EN, 0x1);
	mdelay(100);
	codec_wr_control(SW_DAC_FIFOC ,0x1, DAC_FIFO_FLUSH, 0x1);
	//set TX FIFO send drq level
	codec_wr_control(SW_DAC_FIFOC ,0x4, TX_TRI_LEVEL, 0xf);
	if(substream->runtime->rate > 32000){
		codec_wr_control(SW_DAC_FIFOC ,  0x1,28, 0x0);
	}else{
		codec_wr_control(SW_DAC_FIFOC ,  0x1,28, 0x1);
	}
	//set TX FIFO MODE
	codec_wr_control(SW_DAC_FIFOC ,0x1, TX_FIFO_MODE, 0x1);
	//send last sample when dac fifo under run
	codec_wr_control(SW_DAC_FIFOC ,0x1, LAST_SE, 0x0);
	//enable dac analog
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_L, 0x1);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_R, 0x1);
	//enable dac to pa
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACPAS, 0x1);
	return 0;
}

static int codec_capture_open(void)
{
	 //enable mic1 pa
	 codec_wr_control(SW_ADC_ACTL, 0x1, MIC1_EN, 0x1);
	 //mic1 gain 32dB
	 codec_wr_control(SW_ADC_ACTL, 0x3,25,0x1);
	 //enable mic2 pa
	 //codec_wr_control(SW_ADC_ACTL, 0x1, MIC2_EN, 0x1);
	  //enable VMIC
	 codec_wr_control(SW_ADC_ACTL, 0x1, VMIC_EN, 0x1);
	 //select mic souce
	 pr_debug("ADC SELECT \n");
	 //codec_wr_control(SW_ADC_ACTL, 0x7, ADC_SELECT, 0x4);//2011-6-17 11:41:12 hx del
	 //enable adc digital
	 codec_wr_control(SW_ADC_FIFOC, 0x1,ADC_DIG_EN, 0x1);
	 //set RX FIFO mode
	 codec_wr_control(SW_ADC_FIFOC, 0x1, RX_FIFO_MODE, 0x1);
	 //flush RX FIFO
	 codec_wr_control(SW_ADC_FIFOC, 0x1, ADC_FIFO_FLUSH, 0x1);
	 //set RX FIFO rec drq level
	 codec_wr_control(SW_ADC_FIFOC, 0xf, RX_TRI_LEVEL, 0x7);
	 //enable adc1 analog
	 codec_wr_control(SW_ADC_ACTL, 0x3,  ADC_EN, 0x3);
	 mdelay(100);
	 return 0;
}

static int codec_play_start(void)
{
	gpio_write_one_pin_value(gpio_pa_shutdown, 1, "audio_pa_ctrl");
	//flush TX FIFO
	codec_wr_control(SW_DAC_FIFOC ,0x1, DAC_FIFO_FLUSH, 0x1);
	//enable dac drq
	codec_wr_control(SW_DAC_FIFOC ,0x1, DAC_DRQ, 0x1);
	return 0;
}

static int codec_play_stop(void)
{
	//pa mute
	gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x0);
	mdelay(5);
	//disable dac drq
	codec_wr_control(SW_DAC_FIFOC ,0x1, DAC_DRQ, 0x0);
	//pa mute
	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x0);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_L, 0x0);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_R, 0x0);
	return 0;
}

static int codec_capture_start(void)
{
	//enable adc drq
	gpio_write_one_pin_value(gpio_pa_shutdown, 1, "audio_pa_ctrl");
	codec_wr_control(SW_ADC_FIFOC ,0x1, ADC_DRQ, 0x1);
	return 0;
}

static int codec_capture_stop(void)
{
	//disable adc drq
	codec_wr_control(SW_ADC_FIFOC ,0x1, ADC_DRQ, 0x0);
	//enable mic1 pa
	codec_wr_control(SW_ADC_ACTL, 0x1, MIC1_EN, 0x0);
	//enable mic2 pa
	//codec_wr_control(SW_ADC_ACTL, 0x1, MIC2_EN, 0x1);
	//enable VMIC
	codec_wr_control(SW_ADC_ACTL, 0x1, VMIC_EN, 0x0);
	//select mic souce
	pr_debug("ADC SELECT \n");
	//codec_wr_control(SW_ADC_ACTL, 0x7, ADC_SELECT, 0x2);
	//enable adc digital
	codec_wr_control(SW_ADC_FIFOC, 0x1,ADC_DIG_EN, 0x0);
	//set RX FIFO mode
	codec_wr_control(SW_ADC_FIFOC, 0x1, RX_FIFO_MODE, 0x0);
	//flush RX FIFO
	codec_wr_control(SW_ADC_FIFOC, 0x1, ADC_FIFO_FLUSH, 0x0);
	//enable adc1 analog
	codec_wr_control(SW_ADC_ACTL, 0x3,  ADC_EN, 0x0);
	return 0;
}

static int codec_dev_free(struct snd_device *device)
{
	return 0;
};

/*	对sun4i-codec.c各寄存器的各种设定，或读取。主要实现函数有三个.
* 	.info = snd_codec_info_volsw, .get = snd_codec_get_volsw,\.put = snd_codec_put_volsw,
*/
static const struct snd_kcontrol_new codec_snd_controls_b[] = {
	//FOR B VERSION
	CODEC_SINGLE("Master Playback Volume", SW_DAC_ACTL,0,0x3f,0),
	CODEC_SINGLE("Playback Switch", SW_DAC_ACTL,6,1,0),//全局输出开关
	CODEC_SINGLE("Capture Volume",SW_ADC_ACTL,20,7,0),//录音音量
	CODEC_SINGLE("Fm Volume",SW_DAC_ACTL,23,7,0),//Fm 音量
	CODEC_SINGLE("Line Volume",SW_DAC_ACTL,26,1,0),//Line音量
	CODEC_SINGLE("MicL Volume",SW_ADC_ACTL,25,3,0),//mic左音量
	CODEC_SINGLE("MicR Volume",SW_ADC_ACTL,23,3,0),//mic右音量
	CODEC_SINGLE("FmL Switch",SW_DAC_ACTL,17,1,0),//Fm左开关
	CODEC_SINGLE("FmR Switch",SW_DAC_ACTL,16,1,0),//Fm右开关
	CODEC_SINGLE("LineL Switch",SW_DAC_ACTL,19,1,0),//Line左开关
	CODEC_SINGLE("LineR Switch",SW_DAC_ACTL,18,1,0),//Line右开关
	CODEC_SINGLE("Ldac Left Mixer",SW_DAC_ACTL,15,1,0),
	CODEC_SINGLE("Rdac Right Mixer",SW_DAC_ACTL,14,1,0),
	CODEC_SINGLE("Ldac Right Mixer",SW_DAC_ACTL,13,1,0),
	CODEC_SINGLE("Mic Input Mux",SW_DAC_ACTL,9,15,0),//from bit 9 to bit 12.Mic（麦克风）输入静音
	CODEC_SINGLE("ADC Input Mux",SW_ADC_ACTL,17,7,0),//ADC输入静音
};

static const struct snd_kcontrol_new codec_snd_controls_a[] = {
	//For A VERSION
	CODEC_SINGLE("Master Playback Volume", SW_DAC_DPC,12,0x3f,0),//62 steps, 3e + 1 = 3f 主音量控制
	CODEC_SINGLE("Playback Switch", SW_DAC_ACTL,6,1,0),//全局输出开关
	CODEC_SINGLE("Capture Volume",SW_ADC_ACTL,20,7,0),//录音音量
	CODEC_SINGLE("Fm Volume",SW_DAC_ACTL,23,7,0),//Fm 音量
	CODEC_SINGLE("Line Volume",SW_DAC_ACTL,26,1,0),//Line音量
	CODEC_SINGLE("MicL Volume",SW_ADC_ACTL,25,3,0),//mic左音量
	CODEC_SINGLE("MicR Volume",SW_ADC_ACTL,23,3,0),//mic右音量
	CODEC_SINGLE("FmL Switch",SW_DAC_ACTL,17,1,0),//Fm左开关
	CODEC_SINGLE("FmR Switch",SW_DAC_ACTL,16,1,0),//Fm右开关
	CODEC_SINGLE("LineL Switch",SW_DAC_ACTL,19,1,0),//Line左开关
	CODEC_SINGLE("LineR Switch",SW_DAC_ACTL,18,1,0),//Line右开关
	CODEC_SINGLE("Ldac Left Mixer",SW_DAC_ACTL,15,1,0),
	CODEC_SINGLE("Rdac Right Mixer",SW_DAC_ACTL,14,1,0),
	CODEC_SINGLE("Ldac Right Mixer",SW_DAC_ACTL,13,1,0),
	CODEC_SINGLE("Mic Input Mux",SW_DAC_ACTL,9,15,0),//from bit 9 to bit 12.Mic（麦克风）输入静音
	CODEC_SINGLE("ADC Input Mux",SW_ADC_ACTL,17,7,0),//ADC输入静音
};

int __init snd_chip_codec_mixer_new(struct snd_card *card)
{
  	/*
  	*	每个alsa预定义的组件在构造时需调用snd_device_new()，而每个组件的析构方法则在函数集中被包含
  	*	对于PCM、AC97此类预定义组件，我们不需要关心它们的析构，而对于自定义的组件，则需要填充snd_device_ops
  	*	中的析构函数指针dev_free，这样，当snd_card_free()被调用时，组件将被自动释放。
  	*/
  	static struct snd_device_ops ops = {
  		.dev_free	=	codec_dev_free,
  	};
  	unsigned char *clnt = "codec";
	int idx, err;
	/*
	*	snd_ctl_new1函数用于创建一个snd_kcontrol并返回其指针，
	*	snd_ctl_add函数用于将创建的snd_kcontrol添加到对应的card中。
	*/
	enum sw_ic_ver  codec_chip_ver = sw_get_ic_ver();

	if(codec_chip_ver == MAGIC_VER_A){
		for (idx = 0; idx < ARRAY_SIZE(codec_snd_controls_a); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&codec_snd_controls_a[idx],clnt))) < 0) {
				return err;
			}
		}
	}else if(codec_chip_ver == MAGIC_VER_B){
		for (idx = 0; idx < ARRAY_SIZE(codec_snd_controls_b); idx++) {
			if ((err = snd_ctl_add(card, snd_ctl_new1(&codec_snd_controls_b[idx],clnt))) < 0) {
				return err;
			}
		}
	}else{
		printk("[audio codec] chip version is unknown!\n");
		return -1;
	}

	/*
	*	当card被创建后，设备（组件）能够被创建并关联于该card。第一个参数是snd_card_create
	*	创建的card指针，第二个参数type指的是device-level即设备类型，形式为SNDRV_DEV_XXX,包括
	*	SNDRV_DEV_CODEC、SNDRV_DEV_CONTROL、SNDRV_DEV_PCM、SNDRV_DEV_RAWMIDI等、用户自定义的
	*	设备的device-level是SNDRV_DEV_LOWLEVEL，ops参数是1个函数集（snd_device_ops结构体）的
	*	指针，device_data是设备数据指针，snd_device_new本身不会分配设备数据的内存，因此事先应
	*	分配。在这里在snd_card_create分配。
	*/
	if ((err = snd_device_new(card, SNDRV_DEV_CODEC, clnt, &ops)) < 0) {
		return err;
	}

	strcpy(card->mixername, "codec Mixer");

	return 0;
}

static void sw_pcm_enqueue(struct snd_pcm_substream *substream)
{
	int play_ret = 0, capture_ret = 0;
	struct sw_playback_runtime_data *play_prtd = NULL;
	struct sw_capture_runtime_data *capture_prtd = NULL;
	dma_addr_t play_pos = 0, capture_pos = 0;
	unsigned long play_len = 0, capture_len = 0;
	unsigned int play_limit = 0, capture_limit = 0;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		play_prtd = substream->runtime->private_data;
		play_pos = play_prtd->dma_pos;
		play_len = play_prtd->dma_period;
		play_limit = play_prtd->dma_limit;
		while(play_prtd->dma_loaded < play_limit){
			if((play_pos + play_len) > play_prtd->dma_end){
				play_len  = play_prtd->dma_end - play_pos;
			}
			play_ret = sw_dma_enqueue(play_prtd->params->channel, substream, __bus_to_virt(play_pos), play_len);
			if(play_ret == 0){
				play_prtd->dma_loaded++;
				play_pos += play_prtd->dma_period;
				if(play_pos >= play_prtd->dma_end)
					play_pos = play_prtd->dma_start;
			}else{
				break;
			}
		}
		play_prtd->dma_pos = play_pos;
	}else{
		capture_prtd = substream->runtime->private_data;
		capture_pos = capture_prtd->dma_pos;
		capture_len = capture_prtd->dma_period;
		capture_limit = capture_prtd->dma_limit;
		while(capture_prtd->dma_loaded < capture_limit){
			if((capture_pos + capture_len) > capture_prtd->dma_end){
				capture_len  = capture_prtd->dma_end - capture_pos;
			}
			capture_ret = sw_dma_enqueue(capture_prtd->params->channel, substream, __bus_to_virt(capture_pos), capture_len);
			if(capture_ret == 0){
			capture_prtd->dma_loaded++;
			capture_pos += capture_prtd->dma_period;
			if(capture_pos >= capture_prtd->dma_end)
			capture_pos = capture_prtd->dma_start;
			}else{
				break;
			}
		}
		capture_prtd->dma_pos = capture_pos;
	}
}

static void sw_audio_capture_buffdone(struct sw_dma_chan *channel,
		                                  void *dev_id, int size,
		                                  enum sw_dma_buffresult result)
{
	struct sw_capture_runtime_data *capture_prtd;
	struct snd_pcm_substream *substream = dev_id;

	if (result == SW_RES_ABORT || result == SW_RES_ERR)
		return;

	capture_prtd = substream->runtime->private_data;
		if (substream){
			snd_pcm_period_elapsed(substream);
		}

	spin_lock(&capture_prtd->lock);
	{
		capture_prtd->dma_loaded--;
		sw_pcm_enqueue(substream);
	}
	spin_unlock(&capture_prtd->lock);
}

static void sw_audio_play_buffdone(struct sw_dma_chan *channel,
		                                  void *dev_id, int size,
		                                  enum sw_dma_buffresult result)
{
	struct sw_playback_runtime_data *play_prtd;
	struct snd_pcm_substream *substream = dev_id;

	if (result == SW_RES_ABORT || result == SW_RES_ERR)
		return;

	play_prtd = substream->runtime->private_data;
	if (substream){
		snd_pcm_period_elapsed(substream);
	}

	spin_lock(&play_prtd->lock);
	{
		play_prtd->dma_loaded--;
		sw_pcm_enqueue(substream);
	}
	spin_unlock(&play_prtd->lock);
}

static snd_pcm_uframes_t snd_sw_codec_pointer(struct snd_pcm_substream *substream)
{
	unsigned long play_res = 0, capture_res = 0;
	struct sw_playback_runtime_data *play_prtd = NULL;
	struct sw_capture_runtime_data *capture_prtd = NULL;
    if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
    	play_prtd = substream->runtime->private_data;
   		spin_lock(&play_prtd->lock);
		sw_dma_getcurposition(DMACH_NADDA_PLAY, (dma_addr_t*)&play_dmasrc, (dma_addr_t*)&play_dmadst);
		play_res = play_dmasrc + play_prtd->dma_period - play_prtd->dma_start;
		spin_unlock(&play_prtd->lock);
		if (play_res >= snd_pcm_lib_buffer_bytes(substream)) {
			if (play_res == snd_pcm_lib_buffer_bytes(substream))
				play_res = 0;
		}
		return bytes_to_frames(substream->runtime, play_res);
    }else{
    	capture_prtd = substream->runtime->private_data;
    	spin_lock(&capture_prtd->lock);
    	sw_dma_getcurposition(DMACH_NADDA_CAPTURE, (dma_addr_t*)&capture_dmasrc, (dma_addr_t*)&capture_dmadst);
    	capture_res = capture_dmadst + capture_prtd->dma_period - capture_prtd->dma_start;
    	spin_unlock(&capture_prtd->lock);
    	if (capture_res >= snd_pcm_lib_buffer_bytes(substream)) {
			if (capture_res == snd_pcm_lib_buffer_bytes(substream))
				capture_res = 0;
		}
		return bytes_to_frames(substream->runtime, capture_res);
    }
}

static int sw_codec_pcm_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params)
{
    int play_ret = 0, capture_ret = 0;
    struct snd_pcm_runtime *play_runtime = NULL, *capture_runtime = NULL;
    struct sw_playback_runtime_data *play_prtd = NULL;
    struct sw_capture_runtime_data *capture_prtd = NULL;
    unsigned long play_totbytes = 0, capture_totbytes = 0;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
	  	play_runtime = substream->runtime;
		play_prtd = play_runtime->private_data;
		play_totbytes = params_buffer_bytes(params);
		snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
		if(play_prtd->params == NULL){
			play_prtd->params = &sw_codec_pcm_stereo_play;
			play_ret = sw_dma_request(play_prtd->params->channel, play_prtd->params->client, NULL);
			if(play_ret < 0){
				printk(KERN_ERR "failed to get dma channel. ret == %d\n", play_ret);
				return play_ret;
			}
			sw_dma_set_buffdone_fn(play_prtd->params->channel, sw_audio_play_buffdone);
			snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
			play_runtime->dma_bytes = play_totbytes;
   			spin_lock_irq(&play_prtd->lock);
			play_prtd->dma_loaded = 0;
			play_prtd->dma_limit = play_runtime->hw.periods_min;
			play_prtd->dma_period = params_period_bytes(params);
			play_prtd->dma_start = play_runtime->dma_addr;

			play_dmasrc = play_prtd->dma_start;
			play_prtd->dma_pos = play_prtd->dma_start;
			play_prtd->dma_end = play_prtd->dma_start + play_totbytes;

			spin_unlock_irq(&play_prtd->lock);
		}
	}else if(substream->stream == SNDRV_PCM_STREAM_CAPTURE){
		capture_runtime = substream->runtime;
		capture_prtd = capture_runtime->private_data;
		capture_totbytes = params_buffer_bytes(params);
		snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
		if(capture_prtd->params == NULL){
			capture_prtd->params = &sw_codec_pcm_stereo_capture;
			capture_ret = sw_dma_request(capture_prtd->params->channel, capture_prtd->params->client, NULL);

			if(capture_ret < 0){
				printk(KERN_ERR "failed to get dma channel. capture_ret == %d\n", capture_ret);
				return capture_ret;
			}
			sw_dma_set_buffdone_fn(capture_prtd->params->channel, sw_audio_capture_buffdone);
			snd_pcm_set_runtime_buffer(substream, &substream->dma_buffer);
			capture_runtime->dma_bytes = capture_totbytes;
			spin_lock_irq(&capture_prtd->lock);
			capture_prtd->dma_loaded = 0;
			capture_prtd->dma_limit = capture_runtime->hw.periods_min;
			capture_prtd->dma_period = params_period_bytes(params);
			capture_prtd->dma_start = capture_runtime->dma_addr;

			capture_dmadst = capture_prtd->dma_start;
			capture_prtd->dma_pos = capture_prtd->dma_start;
			capture_prtd->dma_end = capture_prtd->dma_start + capture_totbytes;

			spin_unlock_irq(&capture_prtd->lock);
		}
	}else{
		return -EINVAL;
	}
	return 0;
}

static int snd_sw_codec_hw_free(struct snd_pcm_substream *substream)
{
	struct sw_playback_runtime_data *play_prtd = NULL;
	struct sw_capture_runtime_data *capture_prtd = NULL;
   	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
 		play_prtd = substream->runtime->private_data;
 		/* TODO - do we need to ensure DMA flushed */
		if(play_prtd->params)
	  	sw_dma_ctrl(play_prtd->params->channel, SW_DMAOP_FLUSH);
		snd_pcm_set_runtime_buffer(substream, NULL);
		if (play_prtd->params) {
			sw_dma_free(play_prtd->params->channel, play_prtd->params->client);
			play_prtd->params = NULL;
		}
   	}else{
		capture_prtd = substream->runtime->private_data;
   		/* TODO - do we need to ensure DMA flushed */
		if(capture_prtd->params)
	  	sw_dma_ctrl(capture_prtd->params->channel, SW_DMAOP_FLUSH);
		snd_pcm_set_runtime_buffer(substream, NULL);
		if (capture_prtd->params) {
			sw_dma_free(capture_prtd->params->channel, capture_prtd->params->client);
			capture_prtd->params = NULL;
		}
   	}
	return 0;
}

static int snd_sw_codec_prepare(struct	snd_pcm_substream	*substream)
{
	struct dma_hw_conf *codec_play_dma_conf = NULL, *codec_capture_dma_conf = NULL;
	int play_ret = 0, capture_ret = 0;
	unsigned int reg_val;
	struct sw_playback_runtime_data *play_prtd = NULL;
	struct sw_capture_runtime_data *capture_prtd = NULL;
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		switch(substream->runtime->rate){
			case 44100:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);

				break;
			case 22050:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(2<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 11025:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(4<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 48000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 96000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(7<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 192000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(6<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 32000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(1<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 24000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(2<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 16000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(3<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 12000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(4<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 8000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(5<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			default:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
		}

		switch(substream->runtime->channels){
			case 1:
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val |=(1<<6);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			case 2:
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(1<<6);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
			default:
				reg_val = readl(baseaddr + SW_DAC_FIFOC);
				reg_val &=~(1<<6);
				writel(reg_val, baseaddr + SW_DAC_FIFOC);
				break;
		}
	}else{
		switch(substream->runtime->rate){
			case 44100:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);

				break;
			case 22050:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(2<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 11025:
				clk_set_rate(codec_pll2clk, 22579200);
				clk_set_rate(codec_moduleclk, 22579200);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(4<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 48000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 32000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(1<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 24000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(2<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 16000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(3<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 12000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(4<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			case 8000:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(5<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
			default:
				clk_set_rate(codec_pll2clk, 24576000);
				clk_set_rate(codec_moduleclk, 24576000);
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(7<<29);
				reg_val |=(0<<29);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
				break;
		}

		switch(substream->runtime->channels){
			case 1:
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val |=(1<<7);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
			break;
			case 2:
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(1<<7);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
			break;
			default:
				reg_val = readl(baseaddr + SW_ADC_FIFOC);
				reg_val &=~(1<<7);
				writel(reg_val, baseaddr + SW_ADC_FIFOC);
			break;
		}
	}
   if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		codec_play_dma_conf = kmalloc(sizeof(struct dma_hw_conf), GFP_KERNEL);
		if (!codec_play_dma_conf){
		   play_ret =  - ENOMEM;
		   printk("Can't audio malloc dma play configure memory\n");
		   return play_ret;
		}
   	 	play_prtd = substream->runtime->private_data;
   	 	/* return if this is a bufferless transfer e.g.
	  	* codec <--> BT codec or GSM modem -- lg FIXME */
   	 	if (!play_prtd->params)
		return 0;
   	 	//open the dac channel register
		codec_play_open(substream);
	  	codec_play_dma_conf->drqsrc_type  = D_DRQSRC_SDRAM;
		codec_play_dma_conf->drqdst_type  = DRQ_TYPE_AUDIO;
		codec_play_dma_conf->xfer_type    = DMAXFER_D_BHALF_S_BHALF;
		codec_play_dma_conf->address_type = DMAADDRT_D_FIX_S_INC;
		codec_play_dma_conf->dir          = SW_DMA_WDEV;
		codec_play_dma_conf->reload       = 0;
		codec_play_dma_conf->hf_irq       = SW_DMA_IRQ_FULL;
		codec_play_dma_conf->from         = play_prtd->dma_start;
		codec_play_dma_conf->to           = play_prtd->params->dma_addr;
	  	play_ret = sw_dma_config(play_prtd->params->channel, codec_play_dma_conf);
	  	/* flush the DMA channel */
		sw_dma_ctrl(play_prtd->params->channel, SW_DMAOP_FLUSH);
		play_prtd->dma_loaded = 0;
		play_prtd->dma_pos = play_prtd->dma_start;
		/* enqueue dma buffers */
		sw_pcm_enqueue(substream);
		return play_ret;
	}else {
		codec_capture_dma_conf = kmalloc(sizeof(struct dma_hw_conf), GFP_KERNEL);
		if (!codec_capture_dma_conf){
		   capture_ret =  - ENOMEM;
		   printk("Can't audio malloc dma capture configure memory\n");
		   return capture_ret;
		}
		capture_prtd = substream->runtime->private_data;
   	 	/* return if this is a bufferless transfer e.g.
	  	 * codec <--> BT codec or GSM modem -- lg FIXME */
   	 	if (!capture_prtd->params)
		return 0;
	   	//open the adc channel register
	   	codec_capture_open();
	   	//set the dma
	   	codec_capture_dma_conf->drqsrc_type  = DRQ_TYPE_AUDIO;
		codec_capture_dma_conf->drqdst_type  = D_DRQSRC_SDRAM;
		codec_capture_dma_conf->xfer_type    = DMAXFER_D_BHALF_S_BHALF;
		codec_capture_dma_conf->address_type = DMAADDRT_D_INC_S_FIX;
		codec_capture_dma_conf->dir          = SW_DMA_RDEV;
		codec_capture_dma_conf->reload       = 0;
		codec_capture_dma_conf->hf_irq       = SW_DMA_IRQ_FULL;
		codec_capture_dma_conf->from         = capture_prtd->params->dma_addr;
		codec_capture_dma_conf->to           = capture_prtd->dma_start;
	  	capture_ret = sw_dma_config(capture_prtd->params->channel, codec_capture_dma_conf);
	  	/* flush the DMA channel */
		sw_dma_ctrl(capture_prtd->params->channel, SW_DMAOP_FLUSH);
		capture_prtd->dma_loaded = 0;
		capture_prtd->dma_pos = capture_prtd->dma_start;

		/* enqueue dma buffers */
		sw_pcm_enqueue(substream);
		return capture_ret;
	}
}

static int snd_sw_codec_trigger(struct snd_pcm_substream *substream, int cmd)
{
	int play_ret = 0, capture_ret = 0;
	struct sw_playback_runtime_data *play_prtd = NULL;
	struct sw_capture_runtime_data *capture_prtd = NULL;
	if(substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
		play_prtd = substream->runtime->private_data;
		spin_lock(&play_prtd->lock);
		switch (cmd) {
			case SNDRV_PCM_TRIGGER_START:
			case SNDRV_PCM_TRIGGER_RESUME:
			case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
				play_prtd->state |= ST_RUNNING;
				codec_play_start();
				sw_dma_ctrl(play_prtd->params->channel, SW_DMAOP_START);
				if(substream->runtime->rate >=192000){
				}else if(substream->runtime->rate > 22050){
					mdelay(2);
				}else{
					mdelay(7);
				}
				//pa unmute
				codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x1);
				break;
			case SNDRV_PCM_TRIGGER_SUSPEND:
				codec_play_stop();
				break;
			case SNDRV_PCM_TRIGGER_STOP:
				play_prtd->state &= ~ST_RUNNING;
				codec_play_stop();
				sw_dma_ctrl(play_prtd->params->channel, SW_DMAOP_STOP);

				break;
			case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
				play_prtd->state &= ~ST_RUNNING;
				sw_dma_ctrl(play_prtd->params->channel, SW_DMAOP_STOP);
				break;
			default:
				printk("error:%s,%d\n", __func__, __LINE__);
				play_ret = -EINVAL;
				break;
			}
		spin_unlock(&play_prtd->lock);
	}else{
		capture_prtd = substream->runtime->private_data;
		spin_lock(&capture_prtd->lock);
		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			capture_prtd->state |= ST_RUNNING;
			codec_capture_start();
			mdelay(1);
			codec_wr_control(SW_ADC_FIFOC, 0x1, ADC_FIFO_FLUSH, 0x1);
			sw_dma_ctrl(capture_prtd->params->channel, SW_DMAOP_START);
			break;
		case SNDRV_PCM_TRIGGER_SUSPEND:
			codec_capture_stop();
			break;
		case SNDRV_PCM_TRIGGER_STOP:
			capture_prtd->state &= ~ST_RUNNING;
			codec_capture_stop();
			sw_dma_ctrl(capture_prtd->params->channel, SW_DMAOP_STOP);
			break;
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			capture_prtd->state &= ~ST_RUNNING;
			sw_dma_ctrl(capture_prtd->params->channel, SW_DMAOP_STOP);
			break;
		default:
			printk("error:%s,%d\n", __func__, __LINE__);
			capture_ret = -EINVAL;
			break;
		}
		spin_unlock(&capture_prtd->lock);
	}
	return 0;
}

static int snd_swcard_capture_open(struct snd_pcm_substream *substream)
{
	/*获得PCM运行时信息指针*/
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	struct sw_capture_runtime_data *capture_prtd;

	capture_prtd = kzalloc(sizeof(struct sw_capture_runtime_data), GFP_KERNEL);
	if (capture_prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&capture_prtd->lock);

	runtime->private_data = capture_prtd;

	runtime->hw = sw_pcm_capture_hardware;

	/* ensure that buffer size is a multiple of period size */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates)) < 0)
		return err;

	return 0;
}

static int snd_swcard_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	kfree(runtime->private_data);
	return 0;
}

static int snd_swcard_playback_open(struct snd_pcm_substream *substream)
{
	/*获得PCM运行时信息指针*/
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;
	struct sw_playback_runtime_data *play_prtd;

	play_prtd = kzalloc(sizeof(struct sw_playback_runtime_data), GFP_KERNEL);
	if (play_prtd == NULL)
		return -ENOMEM;

	spin_lock_init(&play_prtd->lock);

	runtime->private_data = play_prtd;

	runtime->hw = sw_pcm_playback_hardware;

	/* ensure that buffer size is a multiple of period size */
	if ((err = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	if ((err = snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE, &hw_constraints_rates)) < 0)
		return err;

	return 0;
}

static int snd_swcard_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	kfree(runtime->private_data);
	return 0;
}

static struct snd_pcm_ops sw_pcm_playback_ops = {
	.open			= snd_swcard_playback_open,//打开
	.close			= snd_swcard_playback_close,//关闭
	.ioctl			= snd_pcm_lib_ioctl,//I/O控制
	.hw_params	    = sw_codec_pcm_hw_params,//硬件参数
	.hw_free	    = snd_sw_codec_hw_free,//资源释放
	.prepare		= snd_sw_codec_prepare,//准备
	.trigger		= snd_sw_codec_trigger,//在pcm被开始、停止或暂停时调用
	.pointer		= snd_sw_codec_pointer,//当前缓冲区的硬件位置
};

static struct snd_pcm_ops sw_pcm_capture_ops = {
	.open			= snd_swcard_capture_open,//打开
	.close			= snd_swcard_capture_close,//关闭
	.ioctl			= snd_pcm_lib_ioctl,//I/O控制
	.hw_params	    = sw_codec_pcm_hw_params,//硬件参数
	.hw_free	    = snd_sw_codec_hw_free,//资源释放
	.prepare		= snd_sw_codec_prepare,//准备
	.trigger		= snd_sw_codec_trigger,//在pcm被开始、停止或暂停时调用
	.pointer		= snd_sw_codec_pointer,//当前缓冲区的硬件位置
};

static int __init snd_card_sw_codec_pcm(struct sw_codec *sw_codec, int device)
{
	struct snd_pcm *pcm;
	int err;
	/*创建PCM实例*/
	if ((err = snd_pcm_new(sw_codec->card, "M1 PCM", device, 1, 1, &pcm)) < 0){
		printk("error,the func is: %s,the line is:%d\n", __func__, __LINE__);
		return err;
	}

	/*
	 * this sets up our initial buffers and sets the dma_type to isa.
	 * isa works but I'm not sure why (or if) it's the right choice
	 * this may be too large, trying it for now
	 */

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, 64*1024);
	/*
	*	设置PCM操作，第1个参数是snd_pcm的指针，第2 个参数是SNDRV_PCM_STREAM_PLAYBACK
	*	或SNDRV_ PCM_STREAM_CAPTURE，而第3 个参数是PCM 操作结构体snd_pcm_ops
	*/
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &sw_pcm_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &sw_pcm_capture_ops);
	pcm->private_data = sw_codec;//置pcm->private_data为芯片特定数据
	pcm->info_flags = 0;
	strcpy(pcm->name, "sun4i PCM");
	/* setup DMA controller */

	return 0;
}

void snd_sw_codec_free(struct snd_card *card)
{

}

static void codec_resume_events(struct work_struct *work)
{
	printk("%s,%d\n",__func__,__LINE__);
	codec_wr_control(SW_DAC_DPC ,  0x1, DAC_EN, 0x1);
	msleep(20);
	//enable PA
	codec_wr_control(SW_ADC_ACTL, 0x1, PA_ENABLE, 0x1);
	msleep(550);
    //enable dac analog
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_L, 0x1);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_R, 0x1);
	//mdelay(20);
	//codec_wr_control(SW_ADC_ACTL, 0x1, HP_DIRECT, 0x1);

	//enable dac to pa
	 //mdelay(20);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACPAS, 0x1);
	//mdelay(30);
		//pa unmute
//	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x1);
    msleep(50);
	printk("====pa turn on===\n");
	gpio_write_one_pin_value(gpio_pa_shutdown, 1, "audio_pa_ctrl");
	/*for clk test*/
	//printk("[codec resume reg]\n");
	//printk("codec_module CLK:0xf1c20140 is:%x\n", *(volatile int *)0xf1c20140);
	//printk("codec_pll2 CLK:0xf1c20008 is:%x\n", *(volatile int *)0xf1c20008);
	//printk("codec_apb CLK:0xf1c20068 is:%x\n", *(volatile int *)0xf1c20068);
	//printk("[codec resume reg]\n");
}

static int __init sw_codec_probe(struct platform_device *pdev)
{
	int err;
	int ret;
	struct snd_card *card;
	struct sw_codec *chip;
	struct codec_board_info  *db;
    printk("enter sun4i Audio codec!!!\n");
	/* register the soundcard */
	ret = snd_card_create(0, "sun4i-codec", THIS_MODULE, sizeof(struct sw_codec),
			      &card);
	if (ret != 0) {
		return -ENOMEM;
	}
	/*从private_data中取出分配的内存大小*/
	chip = card->private_data;
	/*声卡芯片的专用数据*/
	card->private_free = snd_sw_codec_free;//card私有数据释放
	chip->card = card;
	chip->samplerate = AUDIO_RATE_DEFAULT;

	/*
	*	mixer,注册control(mixer)接口
	*	创建一个control至少要实现snd_kcontrol_new中的info(),get()和put()这三个成员函数
	*/
	if ((err = snd_chip_codec_mixer_new(card)))
		goto nodev;

	/*
	*	PCM,录音放音相关，注册PCM接口
	*/
	if ((err = snd_card_sw_codec_pcm(chip, 0)) < 0)
	    goto nodev;

	strcpy(card->driver, "sun4i-CODEC");
	strcpy(card->shortname, "sun4i-CODEC");
	sprintf(card->longname, "sun4i-CODEC  Audio Codec");

	snd_card_set_dev(card, &pdev->dev);

	//注册card
	if ((err = snd_card_register(card)) == 0) {
		printk( KERN_INFO "sun4i audio support initialized\n" );
		platform_set_drvdata(pdev, card);
	}else{
      return err;
	}

	db = kzalloc(sizeof(*db), GFP_KERNEL);
	if (!db)
		return -ENOMEM;
  	/* codec_apbclk */
	codec_apbclk = clk_get(NULL,"apb_audio_codec");
	if (-1 == clk_enable(codec_apbclk)) {
		printk("codec_apbclk failed; \n");
	}
	/* codec_pll2clk */
	codec_pll2clk = clk_get(NULL,"audio_pll");

	/* codec_moduleclk */
	codec_moduleclk = clk_get(NULL,"audio_codec");

	if (clk_set_parent(codec_moduleclk, codec_pll2clk)) {
		printk("try to set parent of codec_moduleclk to codec_pll2clk failed!\n");
	}
	if (clk_set_rate(codec_moduleclk, 24576000)) {
		printk("set codec_moduleclk clock freq 24576000 failed!\n");
	}
	if (-1 == clk_enable(codec_moduleclk)){
		printk("open codec_moduleclk failed; \n");
	}
	db->codec_base_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	db->dev = &pdev->dev;

	if (db->codec_base_res == NULL) {
		ret = -ENOENT;
		printk("codec insufficient resources\n");
		goto out;
	}
	 /* codec address remap */
	 db->codec_base_req = request_mem_region(db->codec_base_res->start, 0x40,
					   pdev->name);
	 if (db->codec_base_req == NULL) {
		 ret = -EIO;
		 printk("cannot claim codec address reg area\n");
		 goto out;
	 }
	 baseaddr = ioremap(db->codec_base_res->start, 0x40);

	 pr_debug("baseaddr = %p\n",baseaddr);

	 if (baseaddr == NULL) {
		 ret = -EINVAL;
		 dev_err(db->dev,"failed to ioremap codec address reg\n");
		 goto out;
	 }

	 kfree(db);
	 gpio_pa_shutdown = gpio_request_ex("audio_para", "audio_pa_ctrl");
	 if(!gpio_pa_shutdown) {
		printk("audio codec_wakeup request gpio fail!\n");
		goto out;
	}
	 gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
	 codec_init();
	 gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
	 resume_work_queue = create_singlethread_workqueue("codec_resume");
	 if (resume_work_queue == NULL) {
        printk("[su4i-codec] try to create workqueue for codec failed!\n");
		ret = -ENOMEM;
		goto err_resume_work_queue;
	}
	 printk("sun4i Audio codec successfully loaded..\n");
	 return 0;
     err_resume_work_queue:
	 out:
		 dev_err(db->dev, "not found (%d).\n", ret);

	 nodev:
		snd_card_free(card);
		return err;
}

/*	suspend state,先disable左右声道，然后静音，再disable pa(放大器)，
 *	disable 耳机，disable dac->pa，最后disable DAC
 * 	顺序不可调，否则刚关闭声卡的时候可能出现噪音
 */
static int snd_sw_codec_suspend(struct platform_device *pdev,pm_message_t state)
{
	printk("[audio codec]:suspend start5000\n");
	gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
	mdelay(50);
	codec_wr_control(SW_ADC_ACTL, 0x1, PA_ENABLE, 0x0);
	mdelay(100);
	//pa mute
	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x0);
	mdelay(500);
    //disable dac analog
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_L, 0x0);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_R, 0x0);

	//disable dac to pa
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACPAS, 0x0);
	codec_wr_control(SW_DAC_DPC ,  0x1, DAC_EN, 0x0);

	clk_disable(codec_moduleclk);
	printk("[audio codec]:suspend end\n");
	return 0;
}


/*	resume state,先unmute，
 *	再enable DAC，enable L/R DAC,enable PA，
 * 	enable 耳机，enable dac to pa
 *	顺序不可调，否则刚打开声卡的时候可能出现噪音
 */
static int snd_sw_codec_resume(struct platform_device *pdev)
{
	printk("[audio codec]:resume start\n");
	if (-1 == clk_enable(codec_moduleclk)){
		printk("open codec_moduleclk failed; \n");
	}

	queue_work(resume_work_queue, &codec_resume_work);
	printk("[audio codec]:resume end\n");
	return 0;
}

static int __devexit sw_codec_remove(struct platform_device *devptr)
{
	clk_disable(codec_moduleclk);
	//释放codec_pll2clk时钟句柄
	clk_put(codec_pll2clk);
	//释放codec_apbclk时钟句柄
	clk_put(codec_apbclk);

	snd_card_free(platform_get_drvdata(devptr));
	platform_set_drvdata(devptr, NULL);
	return 0;
}

static void sw_codec_shutdown(struct platform_device *devptr)
{
	gpio_write_one_pin_value(gpio_pa_shutdown, 0, "audio_pa_ctrl");
	mdelay(50);
	codec_wr_control(SW_ADC_ACTL, 0x1, PA_ENABLE, 0x0);
	mdelay(100);
	//pa mute
	codec_wr_control(SW_DAC_ACTL, 0x1, PA_MUTE, 0x0);
	mdelay(500);
    //disable dac analog
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_L, 0x0);
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACAEN_R, 0x0);

	//disable dac to pa
	codec_wr_control(SW_DAC_ACTL, 0x1, 	DACPAS, 0x0);
	codec_wr_control(SW_DAC_DPC ,  0x1, DAC_EN, 0x0);

	clk_disable(codec_moduleclk);
}

static struct resource sw_codec_resource[] = {
	[0] = {
    	.start = CODEC_BASSADDRESS,
        .end   = CODEC_BASSADDRESS + 0x40,
		.flags = IORESOURCE_MEM,
	},
};

/*data relating*/
static struct platform_device sw_device_codec = {
	.name = "sun4i-codec",
	.id = -1,
	.num_resources = ARRAY_SIZE(sw_codec_resource),
	.resource = sw_codec_resource,
};

/*method relating*/
static struct platform_driver sw_codec_driver = {
	.probe		= sw_codec_probe,
	.remove		= sw_codec_remove,
	.shutdown   = sw_codec_shutdown,
#ifdef CONFIG_PM
	.suspend	= snd_sw_codec_suspend,
	.resume		= snd_sw_codec_resume,
#endif
	.driver		= {
		.name	= "sun4i-codec",
	},
};

static int __init sw_codec_init(void)
{
	int err = 0;
	if((platform_device_register(&sw_device_codec))<0)
		return err;

	if ((err = platform_driver_register(&sw_codec_driver)) < 0)
		return err;

	return 0;
}

static void __exit sw_codec_exit(void)
{
	platform_driver_unregister(&sw_codec_driver);
}

module_init(sw_codec_init);
module_exit(sw_codec_exit);

MODULE_DESCRIPTION("sun4i CODEC ALSA codec driver");
MODULE_AUTHOR("software");
MODULE_LICENSE("GPL");
