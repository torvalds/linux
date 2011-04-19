#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <asm/div64.h>
#include "alc5621.h"

#if REALTEK_HWDEP

#include <linux/ioctl.h>
#include <linux/types.h>

#endif

#define AUDIO_NAME "rt5621"
#define RT5621_VERSION "alsa 1.0.21 0.05"

#ifdef RT5621_DEBUG
#define dbg(format, arg...) \
	printk(KERN_DEBUG AUDIO_NAME ": " format "\n" , ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif
#define err(format, arg...) \
	printk(KERN_ERR AUDIO_NAME ": " format "\n" , ## arg)
#define info(format, arg...) \
	printk(KERN_INFO AUDIO_NAME ": " format "\n" , ## arg)
#define warn(format, arg...) \
	printk(KERN_WARNING AUDIO_NAME ": " format "\n" , ## arg)
	
static int caps_charge = 500;
module_param(caps_charge, int, 0);
MODULE_PARM_DESC(caps_charge, "RT5621 cap charge time (msecs)");

/* codec private data */
struct rt5621_priv {
	unsigned int sysclk;
};

static struct snd_soc_device *rt5621_socdev;

struct rt5621_reg{

	u8 reg_index;
	u16 reg_value;
};

static struct rt5621_reg init_data[] = {
	{RT5621_AUDIO_INTERFACE,        0x8000},    //set I2S codec to slave mode
	{RT5621_STEREO_DAC_VOL,         0x0808},    //default stereo DAC volume to 0db
	{RT5621_OUTPUT_MIXER_CTRL,      0x0740},    //default output mixer control	
	{RT5621_ADC_REC_MIXER,          0x3f3f},    //set record source is Mic1 by default
	{RT5621_MIC_CTRL,               0x0500},    //set Mic1,Mic2 boost 20db	
	{RT5621_SPK_OUT_VOL,            0x8080},    //default speaker volume to 0db 
	{RT5621_HP_OUT_VOL,             0x8888},    //default HP volume to -12db	
	{RT5621_ADD_CTRL_REG,           0x5f00},    //Class AB/D speaker ratio is 1VDD
	{RT5621_STEREO_AD_DA_CLK_CTRL,  0x066d},    //set Dac filter to 256fs
	{RT5621_HID_CTRL_INDEX,         0x46},	    //Class D setting
	{RT5621_HID_CTRL_DATA,          0xFFFF},    //power on Class D Internal register
};

#define RT5621_INIT_REG_NUM ARRAY_SIZE(init_data)

/*
 * rt5621 register cache
 * We can't read the RT5621 register space when we
 * are using 2 wire for device control, so we cache them instead.
 */
static const u16 rt5621_reg[0x80/2];


/* virtual HP mixers regs */
#define HPL_MIXER	0x80
#define HPR_MIXER	0x82
/*reg84*/
/*bit0,1:for hp pga power control
 *bit2,3:for aux pga power control
 */
#define MISC_FUNC_REG 0x84
static u16 reg80=0,reg82=0, reg84 = 0;


/*
 * read rt5621 register cache
 */
static inline unsigned int rt5621_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg < 1 || reg > (ARRAY_SIZE(rt5621_reg) + 1))
		return -1;
	return cache[reg/2];
}


/*
 * write rt5621 register cache
 */

static inline void rt5621_write_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg < 0 || reg > 0x7e)
		return;
	cache[reg/2] = value;
}



static int rt5621_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[3];

	if(reg>0x7E)
	{
		if(reg==HPL_MIXER)
			reg80=value;
		else if(reg==HPR_MIXER)
			reg82=value;
		else if (reg == MISC_FUNC_REG)
			reg84 = value;
		else
			return -EIO;
	
		return 0;	
	}	


	printk("rt5621 write reg=%x,value=%x\n",reg,value);
	data[0] = reg;
	data[1] = (0xFF00 & value) >> 8;
	data[2] = 0x00FF & value;

	if (codec->hw_write(codec->control_data, data, 3) == 3)
	{
		rt5621_write_reg_cache (codec, reg, value);		
		printk(KERN_INFO "rt5621 write reg=%x,value=%x\n",reg,value);
		return 0;
	}
	else
	{
		printk(KERN_ERR "rt5621 write faile\n");
		return -EIO;
	}
}


static unsigned int rt5621_read(struct snd_soc_codec *codec, unsigned int reg)
{
	u8 data[2]={0};
	unsigned int value=0x0;

	if(reg>0x7E)
	{
		if(reg==HPL_MIXER)
		 return	reg80;
		else if(reg==HPR_MIXER)
		 return	reg82;
		else if (reg == MISC_FUNC_REG)
			return reg84;
		else
		 return -EIO;
		 
		 return -EIO;	
	}


	data[0] = reg;
//flove031811_S
#if 0
      i2c_master_recv(codec->control_data, data, 2);
      
       value = (data[0]<<8) | data[1];         
       printk("rt5621_read reg%x=%x\n",reg,value);
#elif 1

	i2c_master_reg8_recv(codec->control_data,reg,data, 2,100 * 1000);

	      value = (data[0]<<8) | data[1];         
       printk("rt5621_read reg%x=%x\n",reg,value);
	   return value;

#else	
	if(codec->hw_write(codec->control_data, data, 1) ==1)
	{
		i2c_master_recv(codec->control_data, data, 2);

		value = (data[0]<<8) | data[1];		
		printk(KERN_DEBUG "rt5621 read reg%x=%x\n",reg,value);
		
		return value;
	}
	else
	{
		printk(KERN_ERR "rt5621 read faile\n");
		return -EIO;			
	}		
#endif
//flove031811_E	
}

#define rt5621_write_mask(c, reg, value, mask) snd_soc_update_bits(c, reg, mask, value)


#define rt5621_reset(c) rt5621_write(c, 0x0, 0)

static unsigned int rt5621_read_index(struct snd_soc_codec *codec, unsigned int index)
{
	unsigned int value;

	rt5621_write(codec, 0x6a, index);
	mdelay(1);
	value = rt5621_read(codec, 0x6c);
	return value;
}

static int rt5621_init_reg(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < RT5621_INIT_REG_NUM; i++)
	{
		rt5621_write(codec, init_data[i].reg_index, init_data[i].reg_value);
	}

	return 0;
}


#if !USE_DAPM_CONTROL
//*****************************************************************************
//
//function:Change audio codec power status
//
//*****************************************************************************
static int rt5621_ChangeCodecPowerStatus(struct snd_soc_codec *codec,int power_state)
{
	unsigned short int PowerDownState=0;

	switch(power_state)
	{
		case POWER_STATE_D0:			//FULL ON-----power on all power
			
			rt5621_write(codec,RT5621_PWR_MANAG_ADD1,~PowerDownState);
			rt5621_write(codec,RT5621_PWR_MANAG_ADD2,~PowerDownState);
			rt5621_write(codec,RT5621_PWR_MANAG_ADD3,~PowerDownState);

		break;	

		case POWER_STATE_D1:		//LOW ON-----


			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2 ,PWR_VREF |PWR_DAC_REF_CIR |PWR_L_DAC_CLK |PWR_R_DAC_CLK |PWR_L_HP_MIXER |PWR_R_HP_MIXER|
													 PWR_L_ADC_CLK_GAIN |PWR_R_ADC_CLK_GAIN |PWR_L_ADC_REC_MIXER |PWR_R_ADC_REC_MIXER|PWR_CLASS_AB
													,PWR_VREF |PWR_DAC_REF_CIR |PWR_L_DAC_CLK |PWR_R_DAC_CLK |PWR_L_HP_MIXER |PWR_R_HP_MIXER|
													 PWR_L_ADC_CLK_GAIN |PWR_R_ADC_CLK_GAIN |PWR_L_ADC_REC_MIXER |PWR_R_ADC_REC_MIXER|PWR_CLASS_AB);

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3 ,PWR_MAIN_BIAS|PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|PWR_SPK_OUT|
													 PWR_MIC1_FUN_CTRL|PWR_MIC1_BOOST_MIXER
													,PWR_MAIN_BIAS|PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|PWR_SPK_OUT|
													 PWR_MIC1_FUN_CTRL|PWR_MIC1_BOOST_MIXER);			


			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,PWR_MAIN_I2S_EN|PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP|PWR_MIC1_BIAS_EN
													,PWR_MAIN_I2S_EN|PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP|PWR_MIC1_BIAS_EN);
						
		break;

		case POWER_STATE_D1_PLAYBACK:	//Low on of Playback


			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2,PWR_VREF|PWR_DAC_REF_CIR|PWR_L_DAC_CLK|PWR_R_DAC_CLK|PWR_L_HP_MIXER|PWR_R_HP_MIXER|PWR_CLASS_AB|PWR_CLASS_D
											  			 ,PWR_VREF|PWR_DAC_REF_CIR|PWR_L_DAC_CLK|PWR_R_DAC_CLK|PWR_L_HP_MIXER|PWR_R_HP_MIXER|PWR_CLASS_AB|PWR_CLASS_D);

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3,PWR_MAIN_BIAS|PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|PWR_SPK_OUT 
											   			 ,PWR_MAIN_BIAS|PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|PWR_SPK_OUT);		

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1,PWR_MAIN_I2S_EN|PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP
											   			 ,PWR_MAIN_I2S_EN|PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP);
									

		break;

		case POWER_STATE_D1_RECORD:	//Low on of Record

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,PWR_MAIN_I2S_EN|PWR_MIC1_BIAS_EN
											   			  ,PWR_MAIN_I2S_EN|PWR_MIC1_BIAS_EN);											   
											
			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2 ,PWR_VREF|PWR_L_ADC_CLK_GAIN|PWR_R_ADC_CLK_GAIN|PWR_L_ADC_REC_MIXER|PWR_R_ADC_REC_MIXER
											   		      ,PWR_VREF|PWR_L_ADC_CLK_GAIN|PWR_R_ADC_CLK_GAIN|PWR_L_ADC_REC_MIXER|PWR_R_ADC_REC_MIXER);

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3 ,PWR_MAIN_BIAS|PWR_MIC2_BOOST_MIXER|PWR_MIC1_BOOST_MIXER
											   		      ,PWR_MAIN_BIAS|PWR_MIC2_BOOST_MIXER|PWR_MIC1_BOOST_MIXER);		
	
		break;

		case POWER_STATE_D2:		//STANDBY----

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,0,PWR_MAIN_I2S_EN|PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP|PWR_MIC1_BIAS_EN);											   


			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3 ,0,PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|PWR_SPK_OUT|PWR_MIC1_FUN_CTRL|PWR_MIC1_BOOST_MIXER);

														
			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2 ,0,PWR_DAC_REF_CIR |PWR_L_DAC_CLK |PWR_R_DAC_CLK |PWR_L_HP_MIXER |PWR_R_HP_MIXER|
													   		 PWR_L_ADC_CLK_GAIN |PWR_R_ADC_CLK_GAIN |PWR_L_ADC_REC_MIXER |PWR_R_ADC_REC_MIXER|PWR_CLASS_AB|PWR_CLASS_D);

		
		break;

		case POWER_STATE_D2_PLAYBACK:	//STANDBY of playback

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3 ,0,/*PWR_HP_R_OUT_VOL|PWR_HP_L_OUT_VOL|*/PWR_SPK_OUT);

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,0,PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP);											   
									
			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2 ,0,PWR_DAC_REF_CIR|PWR_L_DAC_CLK|PWR_R_DAC_CLK|PWR_L_HP_MIXER|PWR_R_HP_MIXER|PWR_CLASS_AB|PWR_CLASS_D);

		break;

		case POWER_STATE_D2_RECORD:		//STANDBY of record

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,0,PWR_MIC1_BIAS_EN);											   
											
			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2 ,0,PWR_L_ADC_CLK_GAIN|PWR_R_ADC_CLK_GAIN|PWR_L_ADC_REC_MIXER|PWR_R_ADC_REC_MIXER);

			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD3 ,0,PWR_MIC2_BOOST_MIXER|PWR_MIC1_BOOST_MIXER);		

		break;		

		case POWER_STATE_D3:		//SLEEP
		case POWER_STATE_D4:		//OFF----power off all power
			rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD1 ,0,PWR_HP_OUT_ENH_AMP|PWR_HP_OUT_AMP);		
			rt5621_write(codec,RT5621_PWR_MANAG_ADD3,0);
			rt5621_write(codec,RT5621_PWR_MANAG_ADD1,0);
			rt5621_write(codec,RT5621_PWR_MANAG_ADD2,0);
						
		break;	

		default:

		break;
	}

	return 0;
}


//*****************************************************************************
//
//function AudioOutEnable:Mute/Unmute audio out channel
//							WavOutPath:output channel
//							Mute :Mute/Unmute output channel											
//
//*****************************************************************************
static int rt5621_AudioOutEnable(struct snd_soc_codec *codec,unsigned short int WavOutPath,int Mute)
{
	int RetVal=0;	

	if(Mute)
	{
		switch(WavOutPath)
		{
			case RT_WAVOUT_ALL_ON:

				RetVal=rt5621_write_mask(codec,RT5621_SPK_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute Speaker right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_HP_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute headphone right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute Aux/Mono right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_STEREO_DAC_VOL,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER
															  ,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER);	//Mute DAC to HP,Speaker,Mono Mixer
		
			break;
		
			case RT_WAVOUT_HP:

				RetVal=rt5621_write_mask(codec,RT5621_HP_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute headphone right/left channel
			
			break;

			case RT_WAVOUT_SPK:
				
				RetVal=rt5621_write_mask(codec,RT5621_SPK_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute Speaker right/left channel			

			break;
			
			case RT_WAVOUT_AUXOUT:

				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL,RT_L_MUTE|RT_R_MUTE,RT_L_MUTE|RT_R_MUTE);	//Mute AuxOut right/left channel

			break;

			case RT_WAVOUT_MONO:

				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL,RT_L_MUTE,RT_L_MUTE);	//Mute MonoOut channel		

			break;

			case RT_WAVOUT_DAC:

				RetVal=rt5621_write_mask(codec,RT5621_STEREO_DAC_VOL,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER
															  ,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER);	//Mute DAC to HP,Speaker,Mono Mixer				
			break;

			default:

				return 0;

		}
	}
	else
	{
		switch(WavOutPath)
		{

			case RT_WAVOUT_ALL_ON:

				RetVal=rt5621_write_mask(codec,RT5621_SPK_OUT_VOL		,0,RT_L_MUTE|RT_R_MUTE);	//Mute Speaker right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_HP_OUT_VOL 		,0,RT_L_MUTE|RT_R_MUTE);	//Mute headphone right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL	,0,RT_L_MUTE|RT_R_MUTE);	//Mute Aux/Mono right/left channel
				RetVal=rt5621_write_mask(codec,RT5621_STEREO_DAC_VOL	,0,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER);	//Mute DAC to HP,Speaker,Mono Mixer
		
			break;
		
			case RT_WAVOUT_HP:

				RetVal=rt5621_write_mask(codec,RT5621_HP_OUT_VOL,0,RT_L_MUTE|RT_R_MUTE);	//UnMute headphone right/left channel	
					
			break;

			case RT_WAVOUT_SPK:
				
				RetVal=rt5621_write_mask(codec,RT5621_SPK_OUT_VOL,0,RT_L_MUTE|RT_R_MUTE);	//unMute Speaker right/left channel			

			break;
			
			case RT_WAVOUT_AUXOUT:

				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL,0,RT_L_MUTE|RT_R_MUTE);//unMute AuxOut right/left channel

			break;

			case RT_WAVOUT_MONO:

				RetVal=rt5621_write_mask(codec,RT5621_MONO_AUX_OUT_VOL,0,RT_L_MUTE);	//unMute MonoOut channel		

			break;

			case RT_WAVOUT_DAC:

				RetVal=rt5621_write_mask(codec,RT5621_STEREO_DAC_VOL,0,RT_M_HP_MIXER|RT_M_SPK_MIXER|RT_M_MONO_MIXER);	//unMute DAC to HP,Speaker,Mono Mixer

			default:
				return 0;
		}

	}
	
	return RetVal;
}


//*****************************************************************************
//
//function:Enable/Disable ADC input source control
//
//*****************************************************************************
static int Enable_ADC_Input_Source(struct snd_soc_codec *codec,unsigned short int ADC_Input_Sour,int Enable)
{
	int bRetVal=0;
	
	if(Enable)
	{
		//Enable ADC source 
		bRetVal=rt5621_write_mask(codec,RT5621_ADC_REC_MIXER,0,ADC_Input_Sour);
	}
	else
	{
		//Disable ADC source		
		bRetVal=rt5621_write_mask(codec,RT5621_ADC_REC_MIXER,ADC_Input_Sour,ADC_Input_Sour);
	}

	return bRetVal;
}
#endif


//static const char *rt5621_spkl_pga[] = {"Vmid","HPL mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_spkn_source_sel[] = {"RN", "RP", "LN"};
static const char *rt5621_spk_pga[] = {"Vmid","HP mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_hpl_pga[]  = {"Vmid","HPL mixer"};
static const char *rt5621_hpr_pga[]  = {"Vmid","HPR mixer"};
static const char *rt5621_mono_pga[] = {"Vmid","HP mixer","SPK mixer","Mono Mixer"};
static const char *rt5621_amp_type_sel[] = {"Class AB","Class D"};
static const char *rt5621_mic_boost_sel[] = {"Bypass","20db","30db","40db"};

static const struct soc_enum rt5621_enum[] = {
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 14, 3, rt5621_spkn_source_sel), /* spkn source from hp mixer */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 10, 4, rt5621_spk_pga), /* spk input sel 1 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 9, 2, rt5621_hpl_pga), /* hp left input sel 2 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 8, 2, rt5621_hpr_pga), /* hp right input sel 3 */	
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL, 6, 4, rt5621_mono_pga), /* mono input sel 4 */
SOC_ENUM_SINGLE(RT5621_MIC_CTRL			, 10,4, rt5621_mic_boost_sel), /*Mic1 boost sel 5 */
SOC_ENUM_SINGLE(RT5621_MIC_CTRL			, 8,4,rt5621_mic_boost_sel), /*Mic2 boost sel 6 */
SOC_ENUM_SINGLE(RT5621_OUTPUT_MIXER_CTRL,13,2,rt5621_amp_type_sel), /*Speaker AMP sel 7 */
};

static int rt5621_amp_sel_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned short val;
	unsigned short mask, bitmask;

	for (bitmask = 1; bitmask < e->max; bitmask <<= 1)
		;
	if (ucontrol->value.enumerated.item[0] > e->max - 1)
		return -EINVAL;
	val = ucontrol->value.enumerated.item[0] << e->shift_l;
	mask = (bitmask - 1) << e->shift_l;
	if (e->shift_l != e->shift_r) {
		if (ucontrol->value.enumerated.item[1] > e->max - 1)
			return -EINVAL;
		val |= ucontrol->value.enumerated.item[1] << e->shift_r;
		mask |= (bitmask - 1) << e->shift_r;
	}

	 snd_soc_update_bits(codec, e->reg, mask, val);
	 val &= (0x1 << 13);
	 if (val == 0)
	 {
		 snd_soc_update_bits(codec, 0x3c, 0x0000, 0x4000);       /*power off classd*/
		 snd_soc_update_bits(codec, 0x3c, 0x8000, 0x8000);       /*power on classab*/
	 }
	 else
	{
	 	 snd_soc_update_bits(codec, 0x3c, 0x0000, 0x8000);       /*power off classab*/
		 snd_soc_update_bits(codec, 0x3c, 0x4000, 0x4000);       /*power on classd*/
	 }
	return 0;
}



static const struct snd_kcontrol_new rt5621_snd_controls[] = {
SOC_DOUBLE("Speaker Playback Volume", 	RT5621_SPK_OUT_VOL, 8, 0, 31, 1),	
SOC_DOUBLE("Speaker Playback Switch", 	RT5621_SPK_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("Headphone Playback Volume", RT5621_HP_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Headphone Playback Switch", RT5621_HP_OUT_VOL,15, 7, 1, 1),
SOC_DOUBLE("AUX Playback Volume", 		RT5621_MONO_AUX_OUT_VOL, 8, 0, 31, 1),
SOC_DOUBLE("AUX Playback Switch", 		RT5621_MONO_AUX_OUT_VOL, 15, 7, 1, 1),
SOC_DOUBLE("PCM Playback Volume", 		RT5621_STEREO_DAC_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Line In Volume", 			RT5621_LINE_IN_VOL, 8, 0, 31, 1),
SOC_SINGLE("Mic 1 Volume", 				RT5621_MIC_VOL, 8, 31, 1),
SOC_SINGLE("Mic 2 Volume", 				RT5621_MIC_VOL, 0, 31, 1),
SOC_ENUM("Mic 1 Boost", 				rt5621_enum[5]),
SOC_ENUM("Mic 2 Boost", 				rt5621_enum[6]),
SOC_ENUM_EXT("Speaker Amp Type",			rt5621_enum[7], snd_soc_get_enum_double, rt5621_amp_sel_put),
SOC_DOUBLE("AUX In Volume", 			RT5621_AUXIN_VOL, 8, 0, 31, 1),
SOC_DOUBLE("Capture Volume", 			RT5621_ADC_REC_GAIN, 7, 0, 31, 0),
};



/* add non dapm controls */
static int rt5621_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(rt5621_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				snd_soc_cnew(&rt5621_snd_controls[i],codec, NULL));
		if (err < 0)
			return err;
	}
	return 0;
}

void hp_depop_mode2(struct snd_soc_codec *codec)
{
	rt5621_write_mask(codec, 0x3e, 0x8000, 0x8000);
	rt5621_write_mask(codec, 0x04, 0x8080, 0x8080);
	rt5621_write_mask(codec, 0x3a, 0x0100, 0x0100);
	rt5621_write_mask(codec, 0x3c, 0x2000, 0x2000);
	rt5621_write_mask(codec, 0x3e, 0x0600, 0x0600);
	rt5621_write_mask(codec, 0x5e, 0x0200, 0x0200);
	schedule_timeout_uninterruptible(msecs_to_jiffies(300));
}

void aux_depop_mode2(struct snd_soc_codec *codec)
{
	rt5621_write_mask(codec, 0x3e, 0x8000, 0x8000);
	rt5621_write_mask(codec, 0x06, 0x8080, 0x8080);
	rt5621_write_mask(codec, 0x3a, 0x0100, 0x0100);
	rt5621_write_mask(codec, 0x3c, 0x2000, 0x2000);
	rt5621_write_mask(codec, 0x3e, 0x6000, 0x6000);
	rt5621_write_mask(codec, 0x5e, 0x0020, 0x0200);
	schedule_timeout_uninterruptible(msecs_to_jiffies(300));
	rt5621_write_mask(codec, 0x3a, 0x0002, 0x0002);
	rt5621_write_mask(codec, 0x3a, 0x0001, 0x0001);	
}
#if USE_DAPM_CONTROL

/*
 * _DAPM_ Controls
 */

/* We have to create a fake left and right HP mixers because
 * the codec only has a single control that is shared by both channels.
 * This makes it impossible to determine the audio path using the current
 * register map, thus we add a new (virtual) register to help determine the
 * audio route within the device.
 */
 static int mixer_event (struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{

	u16 l, r, lineIn,mic1,mic2, aux, pcm;

	l = rt5621_read(w->codec, HPL_MIXER);
	r = rt5621_read(w->codec, HPR_MIXER);
	lineIn = rt5621_read(w->codec, RT5621_LINE_IN_VOL);
	mic2 = rt5621_read(w->codec, RT5621_MIC_ROUTING_CTRL);
	aux = rt5621_read(w->codec,RT5621_AUXIN_VOL);
	pcm = rt5621_read(w->codec, RT5621_STEREO_DAC_VOL);


	if (event & SND_SOC_DAPM_PRE_REG)
		return 0;

	if (l & 0x1 || r & 0x1)
		rt5621_write(w->codec, RT5621_STEREO_DAC_VOL, pcm & 0x7fff);
	else
		rt5621_write(w->codec, RT5621_STEREO_DAC_VOL, pcm | 0x8000);

	if (l & 0x2 || r & 0x2)
		rt5621_write(w->codec, RT5621_MIC_ROUTING_CTRL, mic2 & 0xff7f);
	else
		rt5621_write(w->codec, RT5621_MIC_ROUTING_CTRL, mic2 | 0x0080);

	mic1 = rt5621_read(w->codec, RT5621_MIC_ROUTING_CTRL);
	if (l & 0x4 || r & 0x4)
		rt5621_write(w->codec, RT5621_MIC_ROUTING_CTRL, mic1 & 0x7fff);
	else
		rt5621_write(w->codec, RT5621_MIC_ROUTING_CTRL, mic1 | 0x8000);

	if (l & 0x8 || r & 0x8)
		rt5621_write(w->codec, RT5621_AUXIN_VOL, aux & 0x7fff);
	else
		rt5621_write(w->codec, RT5621_AUXIN_VOL, aux | 0x8000);

	if (l & 0x10 || r & 0x10)
		rt5621_write(w->codec, RT5621_LINE_IN_VOL, lineIn & 0x7fff);
	else
		rt5621_write(w->codec, RT5621_LINE_IN_VOL, lineIn | 0x8000);

	return 0;
}


static int hp_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int reg = rt5621_read(codec, MISC_FUNC_REG);
	
	if (((reg & 0x03) != 0x00) && ((reg & 0x03) != 0x03))
		return 0;
	
	switch (event)
	{
		case SND_SOC_DAPM_POST_PMU:
			hp_depop_mode2(codec);
			rt5621_write_mask(codec, 0x04, 0x0000, 0x8080);
			rt5621_write_mask(codec, 0x3a, 0x0020, 0x0020);
			break;
		case SND_SOC_DAPM_POST_PMD:
			rt5621_write_mask(codec, 0x04, 0x8080, 0x8080);
			rt5621_write_mask(codec, 0x3a, 0x0000, 0x0010);
			rt5621_write_mask(codec, 0x3a, 0x0000, 0x0020);
			rt5621_write_mask(codec, 0x3e, 0x0000, 0x0600);
			break;	
	}
	return 0;
}

static int aux_event(struct snd_soc_dapm_widget *w, 
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int reg = rt5621_read(codec, MISC_FUNC_REG);
	
	if (((reg & 0x0c) != 0x00) && ((reg & 0x0c) != 0x0c))
		return 0;
	
	switch (event)
	{
		case SND_SOC_DAPM_POST_PMU:
			aux_depop_mode2(codec);
			rt5621_write_mask(codec, 0x06, 0x0000, 0x8080);
			break;
		case SND_SOC_DAPM_POST_PMD:
			rt5621_write_mask(codec, 0x06, 0x8080, 0x8080);
			rt5621_write_mask(codec, 0x3a, 0x0000, 0x0001);
			rt5621_write_mask(codec, 0x3a, 0x0000, 0x0002);
			rt5621_write_mask(codec, 0x3e, 0x0000, 0x6000);		
			break;
	}
	return 0;
}

/* Left Headphone Mixers */
static const struct snd_kcontrol_new rt5621_hpl_mixer_controls[] = {
SOC_DAPM_SINGLE("LineIn Playback Switch", HPL_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("AUXIN Playback Switch", HPL_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("Mic1 Playback Switch", HPL_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("Mic2 Playback Switch", HPL_MIXER, 1, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", HPL_MIXER, 0, 1, 0),
SOC_DAPM_SINGLE("RecordL Playback Switch", RT5621_ADC_REC_GAIN, 15, 1,1),
};

/* Right Headphone Mixers */
static const struct snd_kcontrol_new rt5621_hpr_mixer_controls[] = {
SOC_DAPM_SINGLE("LineIn Playback Switch", HPR_MIXER, 4, 1, 0),
SOC_DAPM_SINGLE("AUXIN Playback Switch", HPR_MIXER, 3, 1, 0),
SOC_DAPM_SINGLE("Mic1 Playback Switch", HPR_MIXER, 2, 1, 0),
SOC_DAPM_SINGLE("Mic2 Playback Switch", HPR_MIXER, 1, 1, 0),
SOC_DAPM_SINGLE("PCM Playback Switch", HPR_MIXER, 0, 1, 0),
SOC_DAPM_SINGLE("RecordR Playback Switch", RT5621_ADC_REC_GAIN, 14, 1,1),
};

//Left Record Mixer
static const struct snd_kcontrol_new rt5621_captureL_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5621_ADC_REC_MIXER, 14, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5621_ADC_REC_MIXER, 13, 1, 1),
SOC_DAPM_SINGLE("LineInL Capture Switch",RT5621_ADC_REC_MIXER,12, 1, 1),
SOC_DAPM_SINGLE("AUXIN Capture Switch", RT5621_ADC_REC_MIXER, 11, 1, 1),
SOC_DAPM_SINGLE("HPMixerL Capture Switch", RT5621_ADC_REC_MIXER,10, 1, 1),
SOC_DAPM_SINGLE("SPKMixer Capture Switch",RT5621_ADC_REC_MIXER,9, 1, 1),
SOC_DAPM_SINGLE("MonoMixer Capture Switch",RT5621_ADC_REC_MIXER,8, 1, 1),
};


//Right Record Mixer
static const struct snd_kcontrol_new rt5621_captureR_mixer_controls[] = {
SOC_DAPM_SINGLE("Mic1 Capture Switch", RT5621_ADC_REC_MIXER, 6, 1, 1),
SOC_DAPM_SINGLE("Mic2 Capture Switch", RT5621_ADC_REC_MIXER, 5, 1, 1),
SOC_DAPM_SINGLE("LineInR Capture Switch",RT5621_ADC_REC_MIXER,4, 1, 1),
SOC_DAPM_SINGLE("AUXIN Capture Switch", RT5621_ADC_REC_MIXER, 3, 1, 1),
SOC_DAPM_SINGLE("HPMixerR Capture Switch", RT5621_ADC_REC_MIXER,2, 1, 1),
SOC_DAPM_SINGLE("SPKMixer Capture Switch",RT5621_ADC_REC_MIXER,1, 1, 1),
SOC_DAPM_SINGLE("MonoMixer Capture Switch",RT5621_ADC_REC_MIXER,0, 1, 1),
};

/* Speaker Mixer */
static const struct snd_kcontrol_new rt5621_speaker_mixer_controls[] = {
SOC_DAPM_SINGLE("LineIn Playback Switch", RT5621_LINE_IN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("AUXIN Playback Switch", RT5621_AUXIN_VOL, 14, 1, 1),
SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5621_MIC_ROUTING_CTRL, 14, 1, 1),
SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5621_MIC_ROUTING_CTRL, 6, 1, 1),
SOC_DAPM_SINGLE("PCM Playback Switch", RT5621_STEREO_DAC_VOL, 14, 1, 1),
};


/* Mono Mixer */
static const struct snd_kcontrol_new rt5621_mono_mixer_controls[] = {
SOC_DAPM_SINGLE("LineIn Playback Switch", RT5621_LINE_IN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("Mic1 Playback Switch", RT5621_MIC_ROUTING_CTRL, 13, 1, 1),
SOC_DAPM_SINGLE("Mic2 Playback Switch", RT5621_MIC_ROUTING_CTRL, 5, 1, 1),
SOC_DAPM_SINGLE("AUXIN Playback Switch", RT5621_AUXIN_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("PCM Playback Switch", RT5621_STEREO_DAC_VOL, 13, 1, 1),
SOC_DAPM_SINGLE("RecordL Playback Switch", RT5621_ADC_REC_GAIN, 13, 1,1),
SOC_DAPM_SINGLE("RecordR Playback Switch", RT5621_ADC_REC_GAIN, 12, 1,1),
};

/* mono output mux */
static const struct snd_kcontrol_new rt5621_mono_mux_controls =
SOC_DAPM_ENUM("Route", rt5621_enum[4]);

/* speaker left output mux */
static const struct snd_kcontrol_new rt5621_hp_spk_mux_controls =
SOC_DAPM_ENUM("Route", rt5621_enum[1]);


/* headphone left output mux */
static const struct snd_kcontrol_new rt5621_hpl_out_mux_controls =
SOC_DAPM_ENUM("Route", rt5621_enum[2]);

/* headphone right output mux */
static const struct snd_kcontrol_new rt5621_hpr_out_mux_controls =
SOC_DAPM_ENUM("Route", rt5621_enum[3]);

static const struct snd_soc_dapm_widget rt5621_dapm_widgets[] = {
SND_SOC_DAPM_MUX("Mono Out Mux", SND_SOC_NOPM, 0, 0,
	&rt5621_mono_mux_controls),
SND_SOC_DAPM_MUX("Speaker Out Mux", SND_SOC_NOPM, 0, 0,
	&rt5621_hp_spk_mux_controls),
SND_SOC_DAPM_MUX("Left Headphone Out Mux", SND_SOC_NOPM, 0, 0,
	&rt5621_hpl_out_mux_controls),
SND_SOC_DAPM_MUX("Right Headphone Out Mux", SND_SOC_NOPM, 0, 0,
	&rt5621_hpr_out_mux_controls),
	
SND_SOC_DAPM_MIXER_E("Left HP Mixer",RT5621_PWR_MANAG_ADD2, 5, 0,
	&rt5621_hpl_mixer_controls[0], ARRAY_SIZE(rt5621_hpl_mixer_controls),
	mixer_event, SND_SOC_DAPM_POST_REG),	
SND_SOC_DAPM_MIXER_E("Right HP Mixer",RT5621_PWR_MANAG_ADD2, 4, 0,
	&rt5621_hpr_mixer_controls[0], ARRAY_SIZE(rt5621_hpr_mixer_controls),
	mixer_event, SND_SOC_DAPM_POST_REG),	
SND_SOC_DAPM_MIXER("Mono Mixer", RT5621_PWR_MANAG_ADD2, 2, 0,
	&rt5621_mono_mixer_controls[0], ARRAY_SIZE(rt5621_mono_mixer_controls)),
	
SND_SOC_DAPM_MIXER("Speaker Mixer", RT5621_PWR_MANAG_ADD2,3,0,
	&rt5621_speaker_mixer_controls[0], ARRAY_SIZE(rt5621_speaker_mixer_controls)),	
	
SND_SOC_DAPM_MIXER("Left Record Mixer", RT5621_PWR_MANAG_ADD2,1,0,
	&rt5621_captureL_mixer_controls[0],	ARRAY_SIZE(rt5621_captureL_mixer_controls)),	
SND_SOC_DAPM_MIXER("Right Record Mixer", RT5621_PWR_MANAG_ADD2,0,0,
	&rt5621_captureR_mixer_controls[0],	ARRAY_SIZE(rt5621_captureR_mixer_controls)),	
	
SND_SOC_DAPM_DAC("Left DAC", "Left HiFi Playback", RT5621_PWR_MANAG_ADD2,9, 0),
SND_SOC_DAPM_DAC("Right DAC", "Right HiFi Playback", RT5621_PWR_MANAG_ADD2, 8, 0),

SND_SOC_DAPM_MIXER("IIS Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("HP Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_MIXER("Line Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),
SND_SOC_DAPM_MIXER("AUXIN Mixer", SND_SOC_NOPM, 0, 0, NULL, 0),

SND_SOC_DAPM_ADC("Left ADC", "Left HiFi Capture", RT5621_PWR_MANAG_ADD2, 7, 0),
SND_SOC_DAPM_ADC("Right ADC", "Right HiFi Capture", RT5621_PWR_MANAG_ADD2, 6, 0),

SND_SOC_DAPM_PGA_E("Left Headphone", MISC_FUNC_REG, 0, 0, NULL, 0, 
	hp_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("Right Headphone", MISC_FUNC_REG, 1, 0, NULL, 0, 
	hp_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("Speaker PGA", RT5621_PWR_MANAG_ADD3, 12, 0, NULL, 0),

SND_SOC_DAPM_PGA_E("AUXL Out", MISC_FUNC_REG, 2, 0, NULL, 0,
	aux_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),
SND_SOC_DAPM_PGA_E("AUXR Out", MISC_FUNC_REG, 3, 0, NULL, 0,
	aux_event, SND_SOC_DAPM_POST_PMD | SND_SOC_DAPM_POST_PMU),

SND_SOC_DAPM_PGA("Left Line In", RT5621_PWR_MANAG_ADD3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right Line In", RT5621_PWR_MANAG_ADD3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Left AUX In", RT5621_PWR_MANAG_ADD3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("Right AUX In", RT5621_PWR_MANAG_ADD3, 4, 0, NULL, 0),

SND_SOC_DAPM_PGA("Mic 1 PGA", RT5621_PWR_MANAG_ADD3, 3, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic 2 PGA", RT5621_PWR_MANAG_ADD3, 2, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic 1 Pre Amp", RT5621_PWR_MANAG_ADD3, 1, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic 2 Pre Amp", RT5621_PWR_MANAG_ADD3, 0, 0, NULL, 0),

SND_SOC_DAPM_MICBIAS("Mic Bias1", RT5621_PWR_MANAG_ADD1, 11, 0),

SND_SOC_DAPM_OUTPUT("AUXL"),
SND_SOC_DAPM_OUTPUT("AUXR"),
SND_SOC_DAPM_OUTPUT("HPL"),
SND_SOC_DAPM_OUTPUT("HPR"),
SND_SOC_DAPM_OUTPUT("SPK"),

SND_SOC_DAPM_INPUT("LINEL"),
SND_SOC_DAPM_INPUT("LINER"),
SND_SOC_DAPM_INPUT("AUXINL"),
SND_SOC_DAPM_INPUT("AUXINR"),
SND_SOC_DAPM_INPUT("MIC1"),
SND_SOC_DAPM_INPUT("MIC2"),
SND_SOC_DAPM_VMID("VMID"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* left HP mixer */
	{"Left HP Mixer"	,	"LineIn Playback Switch"		,	"Left Line In"},
	{"Left HP Mixer"	,	"AUXIN Playback Switch"		,	"Left AUX In"},
	{"Left HP Mixer"	,	"Mic1 Playback Switch"			,	"Mic 1 PGA"},
	{"Left HP Mixer"	,	"Mic2 Playback Switch"			,	"Mic 2 PGA"},
	{"Left HP Mixer"	,	"PCM Playback Switch"			,	"Left DAC"},
	{"Left HP Mixer"	,	"RecordL Playback Switch"		,	"Left Record Mixer"},
	
	/* right HP mixer */
	{"Right HP Mixer"	,	"LineIn Playback Switch"		,	"Right Line In"},
	{"Right HP Mixer"	,	"AUXIN Playback Switch"		,	"Right AUX In"},
	{"Right HP Mixer"	,	"Mic1 Playback Switch"			,	"Mic 1 PGA"},
	{"Right HP Mixer"	,	"Mic2 Playback Switch"			,	"Mic 2 PGA"},
	{"Right HP Mixer"	,	"PCM Playback Switch"			,	"Right DAC"},
	{"Right HP Mixer"	,	"RecordR Playback Switch"		,	"Right Record Mixer"},
	
	/* virtual mixer - mixes left & right channels for spk and mono */
	{"IIS Mixer"		,	NULL						,	"Left DAC"},
	{"IIS Mixer"		,	NULL						,	"Right DAC"},
	{"Line Mixer"		,	NULL						,	"Right Line In"},
	{"Line Mixer"		,	NULL						,	"Left Line In"},
	{"AUXIN Mixer"	,	NULL						,	"Left AUX In"},
	{"AUXIN Mixer"	,	NULL						,	"Right AUX In"},
	{"HP Mixer"		,	NULL						,	"Left HP Mixer"},
	{"HP Mixer"		,	NULL						,	"Right HP Mixer"},
	
	/* speaker mixer */
	{"Speaker Mixer"	,	"LineIn Playback Switch"		,	"Line Mixer"},
	{"Speaker Mixer"	,	"AUXIN Playback Switch"		,	"AUXIN Mixer"},
	{"Speaker Mixer"	,	"Mic1 Playback Switch"			,	"Mic 1 PGA"},
	{"Speaker Mixer"	,	"Mic2 Playback Switch"			,	"Mic 2 PGA"},
	{"Speaker Mixer"	,	"PCM Playback Switch"			,	"IIS Mixer"},


	/* mono mixer */
	{"Mono Mixer"		,	"LineIn Playback Switch"		,	"Line Mixer"},
	{"Mono Mixer"		,	"Mic1 Playback Switch"			,	"Mic 1 PGA"},
	{"Mono Mixer"		,	"Mic2 Playback Switch"			,	"Mic 2 PGA"},
	{"Mono Mixer"		,	"PCM Playback Switch"			,	"IIS Mixer"},
	{"Mono Mixer"		,	"AUXIN Playback Switch"		,	"AUXIN Mixer"},
	{"Mono Mixer"		,	"RecordL Playback Switch"		,	"Left Record Mixer"},
	{"Mono Mixer"		,	"RecordR Playback Switch"		,	"Right Record Mixer"},
	
	/*Left record mixer */
	{"Left Record Mixer"	,	"Mic1 Capture Switch"		,	"Mic 1 Pre Amp"},
	{"Left Record Mixer"	,	"Mic2 Capture Switch"		,	"Mic 2 Pre Amp"},
	{"Left Record Mixer"	, 	"LineInL Capture Switch"	,	"LINEL"},
	{"Left Record Mixer"	,	"AUXIN Capture Switch"		,	"AUXINL"},
	{"Left Record Mixer"	, 	"HPMixerL Capture Switch"	,	"Left HP Mixer"},
	{"Left Record Mixer"	, 	"SPKMixer Capture Switch"	,	"Speaker Mixer"},
	{"Left Record Mixer"	, 	"MonoMixer Capture Switch"	,	"Mono Mixer"},
	
	/*Right record mixer */
	{"Right Record Mixer"	, 	"Mic1 Capture Switch"		,	"Mic 1 Pre Amp"},
	{"Right Record Mixer"	,	 "Mic2 Capture Switch"		,	"Mic 2 Pre Amp"},
	{"Right Record Mixer"	, 	"LineInR Capture Switch"	,	"LINER"},
	{"Right Record Mixer"	, 	"AUXIN Capture Switch"		,	"AUXINR"},
	{"Right Record Mixer"	, 	"HPMixerR Capture Switch"	,	"Right HP Mixer"},
	{"Right Record Mixer"	, 	"SPKMixer Capture Switch"	,	"Speaker Mixer"},
	{"Right Record Mixer"	,	 "MonoMixer Capture Switch"	,	"Mono Mixer"},	

	/* headphone left mux */
	{"Left Headphone Out Mux"	,	 "HPL mixer"			,	 "Left HP Mixer"},

	/* headphone right mux */
	{"Right Headphone Out Mux", "HPR mixer", "Right HP Mixer"},

	/* speaker mux */
	{"Speaker Out Mux", "HP mixer", "HP Mixer"},
	{"Speaker Out Mux", "SPK mixer", "Speaker Mixer"},
	{"Speaker Out Mux", "Mono Mixer", "Mono Mixer"},

	/* mono mux */
	{"Mono Out Mux", "HP mixer", "HP Mixer"},
	{"Mono Out Mux", "SPK mixer", "Speaker Mixer"},
	{"Mono Out Mux", "Mono Mixer", "Mono Mixer"},
	
	/* output pga */
	{"HPL", NULL, "Left Headphone"},
	{"Left Headphone", NULL, "Left Headphone Out Mux"},

	{"HPR", NULL, "Right Headphone"},
	{"Right Headphone", NULL, "Right Headphone Out Mux"},

	{"SPK", NULL, "Speaker PGA"},
	{"Speaker PGA", NULL, "Speaker Out Mux"},

	{"AUXL", NULL, "AUXL Out"},
	{"AUXL Out", NULL, "Mono Out Mux"},

	{"AUXR", NULL, "AUXR Out"},
	{"AUXR Out", NULL, "Mono Out Mux"},

	/* input pga */
	{"Left Line In", NULL, "LINEL"},
	{"Right Line In", NULL, "LINER"},
	
	{"Left AUX In", NULL, "AUXINL"},
	{"Right AUX In", NULL, "AUXINR"},
	
	{"Mic 1 Pre Amp", NULL, "MIC1"},
	{"Mic 2 Pre Amp", NULL, "MIC2"},	
	{"Mic 1 PGA", NULL, "Mic 1 Pre Amp"},
	{"Mic 2 PGA", NULL, "Mic 2 Pre Amp"},

	/* left ADC */
	{"Left ADC", NULL, "Left Record Mixer"},

	/* right ADC */
	{"Right ADC", NULL, "Right Record Mixer"},
	
};

static int rt5621_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, rt5621_dapm_widgets,
				ARRAY_SIZE(rt5621_dapm_widgets));
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));
	snd_soc_dapm_new_widgets(codec);

	return 0;
}

#else

static int rt5621_pcm_hw_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *codec_dai)
{

	struct snd_soc_codec *codec = codec_dai->codec;
	int stream = substream->stream;

	switch (stream)
	{
		case SNDRV_PCM_STREAM_PLAYBACK:

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D1_PLAYBACK);	//power on dac to hp and speaker out
						
			rt5621_AudioOutEnable(codec,RT_WAVOUT_SPK,0);	//unmute speaker out
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_HP,0);	//unmute hp out

			break;
		case SNDRV_PCM_STREAM_CAPTURE:

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D1_RECORD);	//power on input to adc

			Enable_ADC_Input_Source(codec,RT_WAVIN_L_MIC1|RT_WAVIN_R_MIC1,1);	//enable record	source from mic1

			break;			
	}
	
	return 0;
}

#endif
/* PLL divisors */
struct _pll_div {
	u32 pll_in;
	u32 pll_out;
	u16 regvalue;
};

static const struct _pll_div codec_pll_div[] = {
	
	{  2048000,  8192000,	0x0ea0},		
	{  3686400,  8192000,	0x4e27},	
	{ 12000000,  8192000,	0x456b},   
	{ 13000000,  8192000,	0x495f},
	{ 13100000,	 8192000,	0x0320},	
	{  2048000,  11289600,	0xf637},
	{  3686400,  11289600,	0x2f22},	
	{ 12000000,  11289600,	0x3e2f},   
	{ 13000000,  11289600,	0x4d5b},
	{ 13100000,	 11289600,	0x363b},	
	{  2048000,  16384000,	0x1ea0},
	{  3686400,  16384000,	0x9e27},	
	{ 12000000,  16384000,	0x452b},   
	{ 13000000,  16384000,	0x542f},
	{ 13100000,	 16384000,	0x03a0},	
	{  2048000,  16934400,	0xe625},
	{  3686400,  16934400,	0x9126},	
	{ 12000000,  16934400,	0x4d2c},   
	{ 13000000,  16934400,	0x742f},
	{ 13100000,	 16934400,	0x3c27},			
	{  2048000,  22579200,	0x2aa0},
	{  3686400,  22579200,	0x2f20},	
	{ 12000000,  22579200,	0x7e2f},   
	{ 13000000,  22579200,	0x742f},
	{ 13100000,	 22579200,	0x3c27},		
	{  2048000,  24576000,	0x2ea0},
	{  3686400,  24576000,	0xee27},	
	{ 12000000,  24576000,	0x2915},   
	{ 13000000,  24576000,	0x772e},
	{ 13100000,	 24576000,	0x0d20},	
};

static const struct _pll_div codec_bclk_pll_div[] = {

	{  1536000,	 24576000,	0x3ea0},	
	{  3072000,	 24576000,	0x1ea0},
	{  512000, 	 24576000,  0x8e90},
	{  256000,   24576000,  0xbe80},
		{  2822400,	 11289600,	0x1ee0},	//flove040711
	{  3072000,	 12288000,	0x1ee0},	//flove040711	
};


static int rt5621_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	int i;
	int ret = -EINVAL;
	struct snd_soc_codec *codec = codec_dai->codec;

	if (pll_id < RT5621_PLL_FR_MCLK || pll_id > RT5621_PLL_FR_BCLK)
		return -EINVAL;

	//rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2, 0x0000,0x1000);	//disable PLL power	
	
	if (!freq_in || !freq_out) {

		return 0;
	}		

	if (RT5621_PLL_FR_MCLK == pll_id) {
	for (i = 0; i < ARRAY_SIZE(codec_pll_div); i++) {
			
		if (codec_pll_div[i].pll_in == freq_in && codec_pll_div[i].pll_out == freq_out)
			{
				rt5621_write_mask(codec, RT5621_GLOBAL_CLK_CTRL_REG, 0x0000, 0x4000);			 	
			 	rt5621_write(codec,RT5621_PLL_CTRL,codec_pll_div[i].regvalue);//set PLL parameter 	
			 	rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2, 0x1000,0x1000);	//enable PLL power	
				ret = 0;
			}
	}
	}
	else if (RT5621_PLL_FR_BCLK == pll_id)
	{
		for (i = 0; i < ARRAY_SIZE(codec_bclk_pll_div); i++)
		{
			if ((freq_in == codec_bclk_pll_div[i].pll_in) && (freq_out == codec_bclk_pll_div[i].pll_out))
			{
				rt5621_write_mask(codec, RT5621_GLOBAL_CLK_CTRL_REG, 0x4000, 0x4000);
				rt5621_write(codec,RT5621_PLL_CTRL,codec_bclk_pll_div[i].regvalue);//set PLL parameter 
				rt5621_write_mask(codec,RT5621_PWR_MANAG_ADD2, 0x1000,0x1000);	//enable PLL power	
				ret = 0;
			}
		}
	}

	rt5621_write_mask(codec,RT5621_GLOBAL_CLK_CTRL_REG,0x8000,0x8000);//Codec sys-clock from PLL 	
	return ret;
}


struct _coeff_div {
	u32 mclk;
	u32 rate;
	u16 fs;
	u16 regvalue;
};

/* codec hifi mclk (after PLL) clock divider coefficients */
static const struct _coeff_div coeff_div[] = {
	/* 8k */
	{ 8192000,  8000, 256*4, 0x2a2d},
	{12288000,  8000, 384*4, 0x2c2f},

	/* 11.025k */
	{11289600, 11025, 256*4, 0x2a2d},
	{16934400, 11025, 384*4, 0x2c2f},

	/* 16k */
	{12288000, 16000, 384*2, 0x1c2f},
	{16384000, 16000, 256*4, 0x2a2d},
	{24576000, 16000, 384*4, 0x2c2f},

	/* 22.05k */	
	{11289600, 22050, 256*2, 0x1a2d},
	{16934400, 22050, 384*2, 0x1c2f},

	/* 32k */
	{12288000, 32000, 384  , 0x0c2f},
	{16384000, 32000, 256*2, 0x1a2d},
	{24576000, 32000, 384*2, 0x1c2f},

	/* 44.1k */
	{11289600, 44100, 256*1, 0x0a2d},
	{22579200, 44100, 256*2, 0x1a2d},
	{45158400, 44100, 256*4, 0x2a2d},	

	/* 48k */
	{12288000, 48000, 256*1, 0x0a2d},
	{24576000, 48000, 256*2, 0x1a2d},
	{49152000, 48000, 256*4, 0x2a2d},

};



static int get_coeff(int mclk, int rate)
{
	int i;
	
	printk("get_coeff mclk=%d,rate=%d\n",mclk,rate);

	for (i = 0; i < ARRAY_SIZE(coeff_div); i++) {
		if (coeff_div[i].rate == rate && coeff_div[i].mclk == mclk)
			return i;
	}
	return -EINVAL;
}




/*
 * Clock after PLL and dividers
 */
 /*in this driver, you have to set sysclk to be 24576000,
 * but you don't need to give a clk to be 24576000, our 
 * internal pll will generate this clock! so it won't make
 * you any difficult.
 */
static int rt5621_set_dai_sysclk(struct snd_soc_dai *codec_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct rt5621_priv *rt5621 = codec->private_data;

	switch (freq) {
	case 24576000:
		rt5621->sysclk = freq;
		return 0;
	}
	return 0;
}


static int rt5621_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		iface = 0x0000;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		iface = 0x8000;
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0000;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		iface |= 0x0001;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0002;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x0003;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		iface |= 0x4003;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		iface |= 0x0000;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	default:
		return -EINVAL;
	}

	rt5621_write(codec,RT5621_AUDIO_INTERFACE,iface);
	return 0;
}


static int rt5621_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params,
	struct snd_soc_dai *codec_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev ->card->codec;
	struct rt5621_priv *rt5621 = codec->private_data;
	u16 iface=rt5621_read(codec,RT5621_AUDIO_INTERFACE)&0xfff3;
	int coeff = get_coeff(rt5621->sysclk, params_rate(params));

	printk("rt5621_pcm_hw_params\n");
	if (coeff < 0)
		coeff = get_coeff(24576000, params_rate(params));	  /*if not set sysclk, default to be 24.576MHz*/

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		iface |= 0x0000;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0004;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0008;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x000c;
		break;
	}

	/* set iface & srate */
	rt5621_write(codec, RT5621_AUDIO_INTERFACE, iface);

	if (coeff >= 0)
		rt5621_write(codec, RT5621_STEREO_AD_DA_CLK_CTRL, coeff_div[coeff].regvalue);
//	else
//	{
//		printk(KERN_ERR "cant find matched sysclk and rate config\n");
//		return -EINVAL;
		
//	}
	return 0;
}

#if !USE_DAPM_CONTROL
static int rt5621_set_bias_level(struct snd_soc_codec *codec, 
			enum snd_soc_bias_level level)
{
	switch (level) {
		case SND_SOC_BIAS_ON:
			break;
		case SND_SOC_BIAS_PREPARE:
			break;
		case SND_SOC_BIAS_STANDBY:
			break;
		case SND_SOC_BIAS_OFF:

			rt5621_write_mask(codec, 0x02, 0x8080, 0x8080);
			rt5621_write_mask(codec, 0x04, 0x8080, 0x8080);
			rt5621_write(codec, 0x3e, 0x0000);
			rt5621_write(codec, 0x3c, 0x0000);
			rt5621_write(codec, 0x3a, 0x0000);
			break;		
	}
	codec->bias_level = level;
	return 0;
}
#else
static int rt5621_set_bias_level(struct snd_soc_codec *codec, 
			enum snd_soc_bias_level level)
{
	switch (level) {
		case SND_SOC_BIAS_ON:
			break;
		case SND_SOC_BIAS_PREPARE:
			break;
		case SND_SOC_BIAS_STANDBY:

			break;
		case SND_SOC_BIAS_OFF:

			rt5621_write_mask(codec, 0x02, 0x8080, 0x8080);
			rt5621_write_mask(codec, 0x04, 0x8080, 0x8080);
			rt5621_write(codec, 0x3e, 0x0000);
			rt5621_write(codec, 0x3c, 0x0000);
			rt5621_write(codec, 0x3a, 0x0000);
			break;		
	}
	codec->bias_level = level;
	return 0;
}
#endif





#if !USE_DAPM_CONTROL

static void rt5621_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *codec_dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	int stream = substream->stream;
	
	switch (stream)
	{
		case SNDRV_PCM_STREAM_PLAYBACK:

			rt5621_AudioOutEnable(codec,RT_WAVOUT_SPK,1);	//mute speaker out
			
			rt5621_AudioOutEnable(codec,RT_WAVOUT_HP,1);	//mute hp out

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D2_PLAYBACK);	//power off dac to hp and speaker out
						


			break;
		case SNDRV_PCM_STREAM_CAPTURE:

			Enable_ADC_Input_Source(codec,RT_WAVIN_L_MIC1|RT_WAVIN_R_MIC1,0);	//disable record	source from mic1

			rt5621_ChangeCodecPowerStatus(codec,POWER_STATE_D2_RECORD);
			

			break;			
	}	
}

#endif


#define RT5621_HIFI_RATES SNDRV_PCM_RATE_8000_48000

#define RT5621_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

struct snd_soc_dai_ops rt5621_hifi_ops = {
	.hw_params = rt5621_pcm_hw_params,	
	.set_fmt = rt5621_set_dai_fmt,
	.set_sysclk = rt5621_set_dai_sysclk,
	.set_pll = rt5621_set_dai_pll,
#if !USE_DAPM_CONTROL
	.prepare = rt5621_pcm_hw_prepare,
	.shutdown = rt5621_shutdown,
#endif
	
};

struct snd_soc_dai rt5621_dai = { 
	
		.name = "RT5621",
		.playback = {
			.stream_name = "HiFi Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5621_HIFI_RATES,
			.formats = RT5621_FORMATS,},
		.capture = {
			.stream_name = "HiFi Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT5621_HIFI_RATES,
			.formats = RT5621_FORMATS,},

		.ops = &rt5621_hifi_ops,
};


EXPORT_SYMBOL_GPL(rt5621_dai);

static ssize_t rt5621_index_reg_show(struct device *dev, 
	struct device_attribute *attr, char *buf)
{
	struct snd_soc_device *socdev = dev_get_drvdata(dev);
	struct snd_soc_codec *codec = socdev ->card->codec;
	int count = 0;
	int value;
	int i; 
	
	count += sprintf(buf, "%s index register\n", codec->name);

	for (i = 0; i < 0x60; i++) {
		count += sprintf(buf + count, "index-%2x    ", i);
		if (count >= PAGE_SIZE - 1)
			break;
		value = rt5621_read_index(codec, i);
		count += snprintf(buf + count, PAGE_SIZE - count, "0x%4x", value);

		if (count >= PAGE_SIZE - 1)
			break;

		count += snprintf(buf + count, PAGE_SIZE - count, "\n");
		if (count >= PAGE_SIZE - 1)
			break;
	}

	if (count >= PAGE_SIZE)
			count = PAGE_SIZE - 1;
		
	return count;
	
}

static DEVICE_ATTR(index_reg, 0444, rt5621_index_reg_show, NULL);

#if defined(CONFIG_SND_HWDEP)
#if REALTEK_HWDEP

#define RT_CE_CODEC_HWDEP_NAME "rt56xx hwdep "

static int rt56xx_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
	printk("enter %s\n", __func__);
	return 0;
}

static int rt56xx_hwdep_release(struct snd_hwdep *hw, struct file *file)
{
	printk("enter %s\n", __func__);
	return 0;
}


static int rt56xx_hwdep_ioctl_common(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct rt56xx_cmd rt56xx;
	struct rt56xx_cmd __user *_rt56xx = arg;
	struct rt56xx_reg_state *buf;
	struct rt56xx_reg_state *p;
	struct snd_soc_codec *codec = hw->private_data;

	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx)))
		return -EFAULT;
	buf = kmalloc(sizeof(*buf) * rt56xx.number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;
	if (copy_from_user(buf, rt56xx.buf, sizeof(*buf) * rt56xx.number)) {
		goto err;
	}
	switch (cmd) {
		case RT_READ_CODEC_REG_IOCTL:
			for (p = buf; p < buf + rt56xx.number; p++)
			{
				p->reg_value = codec->read(codec, p->reg_index);
			}
			if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * rt56xx.number))
				goto err;
				
			break;
		case RT_WRITE_CODEC_REG_IOCTL:
			for (p = buf; p < buf + rt56xx.number; p++)
				codec->write(codec, p->reg_index, p->reg_value);
			break;
	}

	kfree(buf);
	return 0;

err:
	kfree(buf);
	return -EFAULT;
	
}

static int rt56xx_codec_dump_reg(struct snd_hwdep *hw, struct file *file, unsigned long arg)
{
	struct rt56xx_cmd rt56xx;
	struct rt56xx_cmd __user *_rt56xx = arg;
	struct rt56xx_reg_state *buf;
	struct snd_soc_codec *codec = hw->private_data;
	int number = codec->reg_cache_size;
	int i;

	printk(KERN_DEBUG "enter %s, number = %d\n", __func__, number);	
	if (copy_from_user(&rt56xx, _rt56xx, sizeof(rt56xx)))
		return -EFAULT;
	
	buf = kmalloc(sizeof(*buf) * number, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	for (i = 0; i < number; i++)
	{
		buf[i].reg_index = i << 1;
		buf[i].reg_value = codec->read(codec, buf[i].reg_index);
	}
	if (copy_to_user(rt56xx.buf, buf, sizeof(*buf) * i))
		goto err;
	rt56xx.number = number;
	if (copy_to_user(_rt56xx, &rt56xx, sizeof(rt56xx)))
		goto err;
	kfree(buf);
	return 0;
err:
	kfree(buf);
	return -EFAULT;
	
}

static int rt56xx_hwdep_ioctl(struct snd_hwdep *hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == RT_READ_ALL_CODEC_REG_IOCTL)
	{
		return rt56xx_codec_dump_reg(hw, file, arg);
	}
	else
	{
		return rt56xx_hwdep_ioctl_common(hw, file, cmd, arg);
	}
}

static int realtek_ce_init_hwdep(struct snd_soc_codec *codec)
{
	struct snd_hwdep *hw;
	struct snd_card *card = codec->card;
	int err;

	if ((err = snd_hwdep_new(card, RT_CE_CODEC_HWDEP_NAME, 0, &hw)) < 0)
		return err;
	
	strcpy(hw->name, RT_CE_CODEC_HWDEP_NAME);
	hw->private_data = codec;
	hw->ops.open = rt56xx_hwdep_open;
	hw->ops.release = rt56xx_hwdep_release;
	hw->ops.ioctl = rt56xx_hwdep_ioctl;
	return 0;
}

#endif
#endif

static void rt5621_work(struct work_struct *work)
{
	struct snd_soc_codec *codec =
		container_of(work, struct snd_soc_codec, delayed_work.work);
	
	rt5621_set_bias_level(codec, codec->bias_level);
}


static int rt5621_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev ->card->codec;

	/* we only need to suspend if we are a valid card */
	if(!codec->card)
		return 0;
		
	rt5621_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	return 0;
}

static int rt5621_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev ->card->codec;
	int i;
	u8 data[3];
	u16 *cache = codec->reg_cache;

	/* we only need to resume if we are a valid card */
	if(!codec->card)
		return 0;

	/* Sync reg_cache with the hardware */	

	for (i = 0; i < ARRAY_SIZE(rt5621_reg); i++) {
		if (i == RT5621_RESET)
			continue;
		data[0] =i << 1;	
		data[1] = (0xFF00 & cache[i]) >> 8;
		data[2] = 0x00FF & cache[i];	
		codec->hw_write(codec->control_data, data, 3);
	}	

	rt5621_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	
	/* charge rt5621 caps */
	
	if (codec->suspend_bias_level == SND_SOC_BIAS_ON) {
		rt5621_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
		codec->bias_level = SND_SOC_BIAS_ON;
		schedule_delayed_work(&codec->delayed_work,
					msecs_to_jiffies(caps_charge));
	}
	return 0;
}


/*
 * initialise the RT5621 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int rt5621_init(struct snd_soc_device *socdev)
{
	struct snd_soc_codec *codec = socdev ->card->codec;
	int  ret = 0;

	printk(KERN_INFO "alsa version is 1.0.21, codec driver version is 0.04\n");
	codec->name = "RT5621";
	codec->owner = THIS_MODULE;
	codec->read = rt5621_read;
	codec->write = rt5621_write;
	codec->set_bias_level = rt5621_set_bias_level;
	codec->dai = &rt5621_dai;
	codec->num_dai = 1;
	codec->reg_cache_step = 2;
	codec->reg_cache_size = ARRAY_SIZE(rt5621_reg) * 2;
	codec->reg_cache = kmemdup(rt5621_reg, sizeof(rt5621_reg), GFP_KERNEL);

	if (codec->reg_cache == NULL)
		return -ENOMEM;
		
	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		printk(KERN_ERR "rt5621: failed to create pcms\n");
		goto pcm_err;
	}


	rt5621_reset(codec);
	rt5621_write(codec, RT5621_PWR_MANAG_ADD3, 0x8000);//enable Main bias
	rt5621_write(codec, RT5621_PWR_MANAG_ADD2, 0x2000);//enable Vref

	hp_depop_mode2(codec);

	rt5621_init_reg(codec);
	rt5621_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	codec->bias_level = SND_SOC_BIAS_STANDBY;
	schedule_delayed_work(&codec->delayed_work,
		msecs_to_jiffies(caps_charge));
	
	rt5621_add_controls(codec);

	#if USE_DAPM_CONTROL

		rt5621_add_widgets(codec);	

	#endif

	#if defined(CONFIG_SND_HWDEP)
	#if REALTEK_HWDEP

		realtek_ce_init_hwdep(codec);

	#endif
	#endif

	ret = snd_soc_init_card(socdev);

	if (ret < 0) {
      		printk(KERN_ERR "rt5621: failed to register card\n");
			goto card_err;
    	}
	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	kfree(codec->reg_cache);
	return ret;
}

static int rt5621_i2c_probe(struct i2c_client *i2c, const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = rt5621_socdev;
	struct snd_soc_codec *codec = socdev ->card->codec;
	int ret;

	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = rt5621_init(socdev);
	if (ret < 0)
		pr_err("failed to initialise rt5621\n");

	return ret;
}


static int rt5621_i2c_remove(struct i2c_client *client)
{
	struct snd_soc_codec *codec = i2c_get_clientdata(client);
	kfree(codec->reg_cache);
	return 0;
}

static const struct i2c_device_id rt5621_i2c_id[] = {
		{"ALC5621", 0},
		{}
};
MODULE_DEVICE_TABLE(i2c, rt5621_i2c_id);
static struct i2c_driver rt5621_i2c_driver = {
	.driver = {
		.name = "RT5621 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    rt5621_i2c_probe,
	.remove =   rt5621_i2c_remove,
	.id_table = rt5621_i2c_id,
};

static int rt5621_add_i2c_device(struct platform_device *pdev,
				 const struct rt5621_setup_data *setup)
{
#if 0
	struct i2c_board_info info;
	struct i2c_adapter *adapter;
	struct i2c_client *client;
#endif
	int ret;
	ret = i2c_add_driver(&rt5621_i2c_driver);
	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c driver\n");
		return ret;
	}
#if 0
	memset(&info, 0, sizeof(struct i2c_board_info));
	info.addr = setup->i2c_address;
	strlcpy(info.type, "rt5621", I2C_NAME_SIZE);

	adapter = i2c_get_adapter(setup->i2c_bus);
	if (!adapter) {
		dev_err(&pdev->dev, "can't get i2c adapter %d\n",
			setup->i2c_bus);
		goto err_driver;
	}

	client = i2c_new_device(adapter, &info);
	i2c_put_adapter(adapter);
	if (!client) {
		dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
			(unsigned int)info.addr);
		goto err_driver;
	}
#endif
	return 0;

#if 0
err_driver:
	i2c_del_driver(&rt5621_i2c_driver);
	return -ENODEV;
#endif
}


static int rt5621_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct rt5621_setup_data *setup = socdev->codec_data;
	struct snd_soc_codec *codec;
	struct rt5621_priv *rt5621;
	int ret;

	pr_info("RT5621 Audio Codec %s\n", RT5621_VERSION);

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	rt5621 = kzalloc(sizeof(struct rt5621_priv), GFP_KERNEL);
	if (rt5621 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}
	codec->private_data = rt5621;
	socdev ->card->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	rt5621_socdev = socdev;
	INIT_DELAYED_WORK(&codec->delayed_work, rt5621_work);
	ret = device_create_file(&pdev->dev, &dev_attr_index_reg);
	if (ret < 0)
		printk(KERN_WARNING "asoc: failed to add index_reg sysfs files\n");

	ret = -ENODEV;
//	if (setup->i2c_address) {
		codec->hw_write = (hw_write_t)i2c_master_send;
		ret = rt5621_add_i2c_device(pdev, setup);
//	}
	if (ret != 0) {
		kfree(codec->private_data);
		kfree(codec);
	}
	return ret;
}

static int run_delayed_work(struct delayed_work *dwork)
{
	int ret;

	/* cancel any work waiting to be queued. */
	ret = cancel_delayed_work(dwork);

	/* if there was any work waiting then we run it now and
	 * wait for it's completion */
	if (ret) {
		schedule_delayed_work(dwork, 0);
		flush_scheduled_work();
	}
	return ret;
}

static int rt5621_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev ->card->codec;

	if (codec->control_data)
		rt5621_set_bias_level(codec, SND_SOC_BIAS_OFF);
	run_delayed_work(&codec->delayed_work);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	device_remove_file(&pdev->dev, &dev_attr_index_reg);
	i2c_unregister_device(codec->control_data);
	i2c_del_driver(&rt5621_i2c_driver);
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_rt5621 = {
	.probe = 	rt5621_probe,
	.remove = 	rt5621_remove,
	.suspend = 	rt5621_suspend,
	.resume =	rt5621_resume,
};

EXPORT_SYMBOL_GPL(soc_codec_dev_rt5621);

static int __init rt5621_modinit(void)
{
	return snd_soc_register_dai(&rt5621_dai);
}

static void __exit rt5621_exit(void)
{
	snd_soc_unregister_dai(&rt5621_dai);
}

module_init(rt5621_modinit);
module_exit(rt5621_exit);
MODULE_LICENSE("GPL");

