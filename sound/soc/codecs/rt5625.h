#ifndef _RT5625_H
#define _RT5625_H

#define RT5625_RESET						0X00			//RESET CODEC TO DEFAULT
#define RT5625_SPK_OUT_VOL					0X02			//SPEAKER OUT VOLUME
#define RT5625_HP_OUT_VOL					0X04			//HEADPHONE OUTPUT VOLUME
#define RT5625_AUX_OUT_VOL					0X06			//AUXOUT VOLUME
#define RT5625_PHONEIN_VOL					0X08			//PHONE INPUT VOLUME
#define RT5625_LINE_IN_VOL					0X0A			//LINE IN VOLUME
#define RT5625_STEREO_DAC_VOL				0X0C			//STEREO DAC VOLUME
#define RT5625_MIC_VOL						0X0E			//MICROPHONE VOLUME
#define RT5625_DAC_AND_MIC_CTRL				0X10			//STEREO DAC AND MIC ROUTING CONTROL
#define RT5625_ADC_REC_GAIN					0X12			//ADC RECORD GAIN
#define RT5625_ADC_REC_MIXER				0X14			//ADC RECORD MIXER CONTROL
#define RT5625_VOICE_DAC_OUT_VOL			0X18			//VOICE DAC OUTPUT VOLUME
#define RT5625_VODSP_PDM_CTL				0X1A			//VODSP & PDM CONTROL
#define RT5625_OUTPUT_MIXER_CTRL			0X1C			//OUTPUT MIXER CONTROL
#define RT5625_VODSP_CTL					0X1E			//VODSP CONTROL
#define RT5625_MIC_CTRL						0X22			//MICROPHONE CONTROL
#define RT5625_DMIC_CTRL					0x24
#define RT5625_PD_CTRL_STAT					0X26			//POWER DOWN CONTROL/STATUS
#define RT5625_DAC_ADC_VODAC_FUN_SEL		0X2E			//STEREO DAC,VOICE DAC,STEREO ADC FUNCTION SELECT
#define	RT5625_MAIN_SDP_CTRL				0X34			//MAIN SERIAL DATA PORT CONTROL(STEREO I2S)
#define RT5625_EXTEND_SDP_CTRL				0X36			//EXTEND SERIAL DATA PORT CONTROL(VOICE I2S/PCM)
#define	RT5625_PWR_MANAG_ADD1				0X3A			//POWER MANAGMENT ADDITION 1
#define RT5625_PWR_MANAG_ADD2				0X3C			//POWER MANAGMENT ADDITION 2
#define RT5625_PWR_MANAG_ADD3				0X3E			//POWER MANAGMENT ADDITION 3
#define RT5625_GEN_CTRL_REG1				0X40			//GENERAL PURPOSE CONTROL REGISTER 1
#define	RT5625_GEN_CTRL_REG2				0X42			//GENERAL PURPOSE CONTROL REGISTER 2
#define RT5625_PLL_CTRL						0X44			//PLL1 CONTROL
#define RT5625_PLL2_CTRL					0X46			//PLL2 CONTROL
#define RT5625_LDO_CTRL						0X48			//LDO CONTROL
#define RT5625_GPIO_PIN_CONFIG				0X4C			//GPIO PIN CONFIGURATION
#define RT5625_GPIO_PIN_POLARITY			0X4E			//GPIO PIN POLARITY	
#define RT5625_GPIO_PIN_STICKY				0X50			//GPIO PIN STICKY	
#define RT5625_GPIO_PIN_WAKEUP				0X52			//GPIO PIN WAKE UP
#define RT5625_GPIO_PIN_STATUS				0X54			//GPIO PIN STATUS
#define RT5625_GPIO_PIN_SHARING				0X56			//GPIO PIN SHARING
#define	RT5625_OVER_TEMP_CURR_STATUS		0X58			//OVER TEMPERATURE AND CURRENT STATUS
#define RT5625_SOFT_VOL_CTRL				0X5A			//SOFT VOLUME CONTROL SETTING
#define RT5625_GPIO_OUT_CTRL				0X5C			//GPIO OUTPUT PIN CONTRL
#define RT5625_MISC_CTRL					0X5E			//MISC CONTROL
#define	RT5625_STEREO_DAC_CLK_CTRL1			0X60			//STEREO DAC CLOCK CONTROL 1
#define RT5625_STEREO_DAC_CLK_CTRL2			0X62			//STEREO DAC CLOCK CONTROL 2
#define RT5625_VOICE_DAC_PCMCLK_CTRL1		0X64			//VOICE/PCM DAC CLOCK CONTROL 1
#define RT5625_PSEDUEO_SPATIAL_CTRL			0X68			//PSEDUEO STEREO /SPATIAL EFFECT BLOCK CONTROL
#define RT5625_PRIV_ADDR					0X6A			//PRIVATE ADDRESS
#define RT5625_PRIV_DATA					0X6C			//PRIVATE DATA 
#define RT5625_EQ_CTRL_ADC_HPF				0X6E			//EQ CONTROL AND STATUS /ADC HPF CONTROL
#define RT5625_VODSP_REG_ADDR 		    	0x70			//VODSP REGISTER ADDRESS
#define RT5625_VODSP_REG_DATA 		    	0x72			//VODSP REGISTER DATA
#define RT5625_VODSP_REG_CMD 			    0x74			//VODSP REGISTER COMMAND


/**************************************************************************************************
 *Bit define of Codec Register
 *************************************************************************************************/
//global definition
#define RT_L_MUTE						(0x1<<15)		//Mute Left Control
#define RT_L_ZC							(0x1<<14)		//Mute Left Zero-Cross Detector Control
#define RT_R_MUTE						(0x1<<7)		//Mute Right Control
#define RT_R_ZC							(0x1<<6)		//Mute Right Zero-Cross Detector Control
#define RT_M_HP_MIXER					(0x1<<15)		//Mute source to HP Mixer
#define RT_M_SPK_MIXER					(0x1<<14)		//Mute source to Speaker Mixer
#define RT_M_MONO_MIXER					(0x1<<13)		//Mute source to Mono Mixer

//Phone Input Volume(0x08)
#define M_PHONEIN_TO_HP_MIXER			(0x1<<15)		//Mute Phone In volume to HP mixer
#define M_PHONEIN_TO_SPK_MIXER			(0x1<<14)		//Mute Phone In volume to speaker mixer


//Mic Routing Control(0x10)
#define M_MIC1_TO_HP_MIXER				(0x1<<15)		//Mute MIC1 to HP mixer
#define M_MIC1_TO_SPK_MIXER				(0x1<<14)		//Mute MiC1 to SPK mixer
#define M_MIC1_TO_MONO_MIXER			(0x1<<13)		//Mute MIC1 to MONO mixer
#define M_MIC2_TO_HP_MIXER				(0x1<<11)		//Mute MIC2 to HP mixer
#define M_MIC2_TO_SPK_MIXER				(0x1<<10)		//Mute MiC2 to SPK mixer
#define M_MIC2_TO_MONO_MIXER			(0x1<<9)		//Mute MIC2 to MONO mixer
#define M_DAC_TO_HPL_MIXER				(0x1<<3)		//Mute DAC to HP left mixer
#define M_DAC_TO_HPR_MIXER				(0x1<<2)		//Mute DAC to HP right mixer
#define M_DAC_TO_SPK_MIXER				(0x1<<1)		//Mute DAC to SPK mixer
#define M_DAC_TO_MONO_MIXER				(0x1<<0)		//Mute DAC to MONO mixer



//ADC Record Gain(0x12)
#define M_ADC_L_TO_HP_MIXER				(0x1<<15)		//Mute left of ADC to HP Mixer
#define M_ADC_L_TO_MONO_MIXER			(0x1<<14)		//Mute left of ADC to MONO Mixer
#define ADC_L_ZC_DET					(0x1<<13)		//ADC Zero-Cross Detector Control
#define ADC_L_GAIN_MASK					(0x1f<<8)		//ADC Record Gain Left channel Mask
#define M_ADC_R_TO_HP_MIXER				(0x1<<7)		//Mute right of ADC to HP Mixer
#define M_ADC_R_TO_MONO_MIXER			(0x1<<6)		//Mute right of ADC to MONO Mixer
#define ADC_R_ZC_DET					(0x1<<5)		//ADC Zero-Cross Detector Control
#define ADC_R_GAIN_MASK					(0x1f<<0)		//ADC Record Gain Right channel Mask

//Voice DAC Output Volume(0x18)
#define M_V_DAC_TO_HP_MIXER				(0x1<<15)
#define M_V_DAC_TO_SPK_MIXER			(0x1<<14)
#define M_V_DAC_TO_MONO_MIXER			(0x1<<13)


//AEC & PDM Control(0x1A)
#define VODSP_SRC1_PWR					(0x1<<15)		//Enable SRC1 Power
#define VODSP_SRC2_PWR					(0x1<<13)		//Enable SRC2 Power

#define VODSP_SRC2_S_SEL_MASK			(0x1<<12)		
#define VODSP_SRC2_S_SEL_TXDP			(0x0<<12)		//SRC2 source select AEC_TXDP
#define VODSP_SRC2_S_SEL_TXDC			(0x1<<12)		//SRC2 source select AEC_TXDC

#define VODSP_RXDP_PWR					(0x1<<11)		//Enable AEC RXDP Power

#define VODSP_RXDP_S_SEL_MASK			(0x3<<9)		
#define VODSP_RXDP_S_SEL_SRC1			(0x0<<9)		//AEC RxDP source select SRC1 Output
#define VODSP_RXDP_S_SEL_ADCL			(0x1<<9)		//AEC RxDP source select ADC Left to AEC Digital Path
#define VODSP_RXDP_S_SEL_VOICE			(0x2<<9)		//AEC RxDP source select Voice to Stereo Digital Path
#define VODSP_RXDP_S_SEL_ADCR			(0x3<<9)		//AEC RxDP source select ADC Right to AEC Digital Path

#define VODSP_RXDC_PWR					(0x1<<8)		//Enable AEC RXDC Power

#define VOICE_PCM_S_SEL_MASK			(0x1<<7)		
#define VOICE_PCM_S_SEL_ADC_R			(0x0<<7)		//VSADC PCM interface source select ADC R
#define VOICE_PCM_S_SEL_AEC_TXDP		(0x1<<7)		//VSADC PCM interface source select AEC_TXDP

#define REC_S_SEL_MASK					(0x3<<4)		
#define REC_S_SEL_ADC					(0x0<<4)		//Main Stereo Record I2S source select ADC L/R
#define REC_S_SEL_VOICE					(0x1<<4)		//Main Stereo Record I2S source select Voice to Stereo Digital Path
#define REC_S_SEL_SRC2					(0x2<<4)		//Main Stereo Record I2S source select SRC2


//Output Mixer Control(0x1C)
#define	SPKOUT_N_SOUR_MASK				(0x3<<14)	
#define	SPKOUT_N_SOUR_LN				(0x2<<14)
#define	SPKOUT_N_SOUR_RP				(0x1<<14)
#define	SPKOUT_N_SOUR_RN				(0x0<<14)

#define SPKOUT_SEL_CLASS_D				(0x1<<13)
#define SPKOUT_SEL_CLASS_AB				(0x0<<13)

#define SPK_CLASS_AB_S_AMP				(0x0<<12)
#define SPK_CALSS_AB_W_AMP				(0x1<<12)

#define SPKOUT_INPUT_SEL_MASK			(0x3<<10)
#define SPKOUT_INPUT_SEL_MONOMIXER		(0x3<<10)
#define SPKOUT_INPUT_SEL_SPKMIXER		(0x2<<10)
#define SPKOUT_INPUT_SEL_HPMIXER		(0x1<<10)
#define SPKOUT_INPUT_SEL_VMID			(0x0<<10)

#define HPL_INPUT_SEL_HPLMIXER			(0x1<<9)
#define HPR_INPUT_SEL_HPRMIXER			(0x1<<8)	

#define AUXOUT_INPUT_SEL_MASK			(0x3<<6)
#define AUXOUT_INPUT_SEL_MONOMIXER		(0x3<<6)
#define AUXOUT_INPUT_SEL_SPKMIXER		(0x2<<6)
#define AUXOUT_INPUT_SEL_HPMIXER		(0x1<<6)
#define AUXOUT_INPUT_SEL_VMID			(0x0<<6)


//Voice DSP Control(0x1E)
#define VODSP_SYSCLK_S_SEL_MASK		 	(0x1<<15)
#define VODSP_SYSCLK_S_SEL_M_CLK	 	(0x0<<15)
#define VODSP_SYSCLK_S_SEL_V_CLK	 	(0x1<<15)

#define VODSP_LRCK_SEL_MASK				(0x1<<13)
#define VODSP_LRCK_SEL_8K				(0x0<<13)
#define VODSP_LRCK_SEL_16K				(0x1<<13)

#define VODSP_TEST_MODE_ENA				(0x1<<3)
#define VODSP_NO_BP_MODE_ENA			(0x1<<2)
#define VODSP_NO_PD_MODE_ENA			(0x1<<1)
#define VODSP_NO_RST_MODE_ENA			(0x1<<0)




//Micphone Control define(0x22)
#define MIC1		1
#define MIC2		2
#define MIC_BIAS_90_PRECNET_AVDD	1
#define	MIC_BIAS_75_PRECNET_AVDD	2

#define MIC1_BOOST_CONTROL_MASK		(0x3<<10)
#define MIC1_BOOST_CONTROL_BYPASS	(0x0<<10)
#define MIC1_BOOST_CONTROL_20DB		(0x1<<10)
#define MIC1_BOOST_CONTROL_30DB		(0x2<<10)
#define MIC1_BOOST_CONTROL_40DB		(0x3<<10)

#define MIC2_BOOST_CONTROL_MASK		(0x3<<8)
#define MIC2_BOOST_CONTROL_BYPASS	(0x0<<8)
#define MIC2_BOOST_CONTROL_20DB		(0x1<<8)
#define MIC2_BOOST_CONTROL_30DB		(0x2<<8)
#define MIC2_BOOST_CONTROL_40DB		(0x3<<8)

#define MIC1_BIAS_VOLT_CTRL_MASK	(0x1<<5)
#define MIC1_BIAS_VOLT_CTRL_90P		(0x0<<5)
#define MIC1_BIAS_VOLT_CTRL_75P		(0x1<<5)

#define MIC2_BIAS_VOLT_CTRL_MASK	(0x1<<4)
#define MIC2_BIAS_VOLT_CTRL_90P		(0x0<<4)
#define MIC2_BIAS_VOLT_CTRL_75P		(0x1<<4)

//PowerDown control of register(0x26)
//power management bits
#define RT_PWR_PR7					(0x1<<15)	//write this bit to power down the Speaker Amplifier
#define RT_PWR_PR6					(0x1<<14)	//write this bit to power down the Headphone Out and MonoOut
#define RT_PWR_PR5					(0x1<<13)	//write this bit to power down the internal clock(without PLL)
#define RT_PWR_PR3					(0x1<<11)	//write this bit to power down the mixer(vref/vrefout out off)
#define RT_PWR_PR2					(0x1<<10)	//write this bit to power down the mixer(vref/vrefout still on)
#define RT_PWR_PR1					(0x1<<9) 	//write this bit to power down the dac
#define RT_PWR_PR0					(0x1<<8) 	//write this bit to power down the adc
#define RT_PWR_REF					(0x1<<3)	//read only
#define RT_PWR_ANL					(0x1<<2)	//read only	
#define RT_PWR_DAC					(0x1<<1)	//read only
#define RT_PWR_ADC					(0x1)		//read only


//Stereo DAC/Voice DAC/Stereo ADC function(0x2E)
#define DAC_FUNC_SEL_MASK			(0x3<<12)
#define DAC_FUNC_SEL_DAC			(0x0<<12)		
#define DAC_FUNC_SEL_SRC2			(0x1<<12)
#define DAC_FUNC_SEL_VODSP_TXDP		(0x2<<12)
#define DAC_FUNC_SEL_VODSP_TXDC		(0x3<<12)

#define VODAC_SOUR_SEL_MASK			(0x7<<8)
#define VODAC_SOUR_SEL_VOICE		(0x0<<8)	
#define VODAC_SOUR_SEL_SRC2			(0x1<<8)
#define VODAC_SOUR_SEL_VODSP_TXDP	(0x2<<8)
#define VODAC_SOUR_SEL_VODSP_TXDC	(0x3<<8)

#define ADCR_FUNC_SEL_MASK			(0x3<<4)
#define ADCR_FUNC_SEL_ADC			(0x0<<4)
#define ADCR_FUNC_SEL_VOADC			(0x1<<4)
#define ADCR_FUNC_SEL_VODSP			(0x2<<4)
#define ADCR_FUNC_SEL_PDM			(0x3<<4)

#define ADCL_FUNC_SEL_MASK			(0x3<<0)
#define ADCL_FUNC_SEL_ADC			(0x0<<0)
#define ADCL_FUNC_SEL_VODSP			(0x1<<0)

//Main Serial Data Port Control(0x34)			
#define MAIN_I2S_MODE_SEL			(0x1<<15)		//0:Master mode 1:Slave mode
#define MAIN_I2S_SADLRCK_CTRL		(0x1<<14)		//0:Disable,ADC and DAC use the same fs,1:Enable

#define MAIN_I2S_PCM_MODE			(0x1<<6)		//0:Normal SADLRCK/SDALRCK,1:Invert SADLRCK/SDALRCK 
//Data Length Slection
#define MAIN_I2S_DL_MASK			(0x3<<2)		//main i2s Data Length mask	
#define MAIN_I2S_DL_16				(0x0<<2)		//16 bits
#define MAIN_I2S_DL_20				(0x1<<2)		//20 bits
#define	MAIN_I2S_DL_24				(0x2<<2)		//24 bits
#define MAIN_I2S_DL_32				(0x3<<2)		//8 bits

//PCM Data Format Selection
#define MAIN_I2S_DF_MASK			(0x3)			//main i2s Data Format mask
#define MAIN_I2S_DF_I2S				(0x0)			//I2S FORMAT 
#define MAIN_I2S_DF_LEFT			(0x1)			//LEFT JUSTIFIED format
#define	MAIN_I2S_DF_PCM_A			(0x2)			//PCM Mode A
#define MAIN_I2S_DF_PCM_B				(0x3)			//PCM Mode B

//Extend Serial Data Port Control(0x36)
#define EXT_I2S_FUNC_ENABLE			(0x1<<15)		//Enable PCM interface on GPIO 1,3,4,5  0:GPIO function,1:Voice PCM interface
#define EXT_I2S_MODE_SEL			(0x1<<14)		//0:Master	,1:Slave
#define EXT_I2S_AUTO_CLK_CTRL		(0x1<<13)		//0:Disable,1:Enable
#define EXT_I2S_BCLK_POLARITY		(0x1<<7)		//0:Normal 1:Invert
#define EXT_I2S_PCM_MODE			(0x1<<6)		//0:Normal VSLRCK,1:Invert VSLRCK 
//Data Length Slection
#define EXT_I2S_DL_MASK				(0x3<<2)		//Extend i2s Data Length mask	
#define EXT_I2S_DL_32				(0x3<<2)		//8 bits
#define	EXT_I2S_DL_24				(0x2<<2)		//24 bits
#define EXT_I2S_DL_20				(0x1<<2)		//20 bits
#define EXT_I2S_DL_16				(0x0<<2)		//16 bits

//Voice Data Format
#define EXT_I2S_DF_MASK				(0x3)			//Extend i2s Data Format mask
#define EXT_I2S_DF_I2S				(0x0)			//I2S FORMAT 
#define EXT_I2S_DF_LEFT				(0x1)			//LEFT JUSTIFIED format
#define	EXT_I2S_DF_PCM_A			(0x2)			//PCM Mode A
#define EXT_I2S_DF_PCM_B			(0x3)			//PCM Mode B

//Power managment addition 1 (0x3A),0:Disable,1:Enable
#define PWR_DAC_DF2SE_L				(0x1<<15)
#define PWR_DAC_DF2SE_R				(0x1<<14)
#define PWR_ZC_DET_PD				(0x1<<13)
#define PWR_I2S_INTERFACE			(0x1<<11)
#define PWR_AMP_POWER				(0x1<<10)
#define PWR_HP_OUT_AMP				(0x1<<9)
#define PWR_HP_OUT_ENH_AMP			(0x1<<8)
#define PWR_VOICE_DF2SE				(0x1<<7)
#define PWR_SOFTGEN_EN				(0x1<<6)	
#define	PWR_MIC_BIAS1_DET			(0x1<<5)
#define	PWR_MIC_BIAS2_DET			(0x1<<4)
#define PWR_MIC_BIAS1				(0x1<<3)	
#define PWR_MIC_BIAS2				(0x1<<2)	
#define PWR_MAIN_BIAS				(0x1<<1)
#define PWR_DAC_REF					(0x1)


//Power managment addition 2(0x3C),0:Disable,1:Enable
#define PWR_PLL1					(0x1<<15)
#define PWR_PLL2					(0x1<<14)
#define PWR_MIXER_VREF				(0x1<<13)
#define PWR_TEMP_SENSOR				(0x1<<12)
#define PWR_AUX_ADC					(0x1<<11)
#define PWR_VOICE_CLOCK				(0x1<<10)
#define PWR_L_DAC_CLK				(0x1<<9)
#define PWR_R_DAC_CLK				(0x1<<8)
#define PWR_L_ADC_CLK				(0x1<<7)
#define PWR_R_ADC_CLK				(0x1<<6)
#define PWR_L_HP_MIXER				(0x1<<5)
#define PWR_R_HP_MIXER				(0x1<<4)
#define PWR_SPK_MIXER				(0x1<<3)
#define PWR_MONO_MIXER				(0x1<<2)
#define PWR_L_ADC_REC_MIXER			(0x1<<1)
#define PWR_R_ADC_REC_MIXER			(0x1)


//Power managment addition 3(0x3E),0:Disable,1:Enable
#define PWR_OSC_EN					(0x1<<15)
#define PWR_AUXOUT_VOL				(0x1<<14)
#define PWR_SPK_OUT					(0x1<<13)
#define PWR_SPK_OUT_N				(0x1<<12)
#define PWR_HP_L_OUT_VOL			(0x1<<11)
#define PWR_HP_R_OUT_VOL			(0x1<<10)
#define PWR_VODSP_INTERFACE			(0x1<<9)
#define PWR_I2C_FOR_VODSP			(0x1<<8)
#define PWR_LINE_IN_L				(0x1<<7)
#define PWR_LINE_IN_R				(0x1<<6)
#define PWR_PHONE_VOL				(0x1<<5)
#define PWR_PHONE_ADMIXER			(0x1<<4)
#define PWR_MIC1_VOL_CTRL			(0x1<<3)
#define PWR_MIC2_VOL_CTRL			(0x1<<2)
#define PWR_MIC1_BOOST				(0x1<<1)
#define PWR_MIC2_BOOST				(0x1)

//General Purpose Control Register 1(0x40)
#define GP_CLK_FROM_PLL				(0x1<<15)	
#define GP_CLK_FROM_MCLK			(0x0<<15)	

#define GP_DAC_HI_PA_ENA			(0x1<<10)	//Enable DAC High Pass Filter

#define GP_EXTCLK_S_SEL_PLL2		(0x1<<6)
#define GP_EXTCLK_S_SEL_PLL1		(0x0<<6)	

#define GP_EXTCLK_DIR_SEL_OUTPUT	(0x1<<5)
#define GP_EXTCLK_DIR_SEL_INTPUT	(0x0<<5)

#define GP_VOSYS_S_SEL_PLL2			(0x0<<4)
#define GP_VOSYS_S_SEL_EXTCLK		(0x1<<4)

#define GP_SPK_AMP_CTRL_MASK		(0x7<<1)
#define GP_SPK_AMP_CTRL_RATIO_225	(0x0<<1)		//2.25 Vdd
#define GP_SPK_AMP_CTRL_RATIO_200	(0x1<<1)		//2.00 Vdd
#define GP_SPK_AMP_CTRL_RATIO_175	(0x2<<1)		//1.75 Vdd
#define GP_SPK_AMP_CTRL_RATIO_150	(0x3<<1)		//1.50 Vdd
#define GP_SPK_AMP_CTRL_RATIO_125	(0x4<<1)		//1.25 Vdd	
#define GP_SPK_AMP_CTRL_RATIO_100	(0x5<<1)		//1.00 Vdd

//General Purpose Control Register 2(0x42)
#define GP2_PLL1_SOUR_SEL_MASK		(0x3<<12)
#define GP2_PLL1_SOUR_SEL_MCLK		(0x0<<12)
#define GP2_PLL1_SOUR_SEL_BCLK		(0x2<<12)
#define GP2_PLL1_SOUR_SEL_VBCLK		(0x3<<12)

//PLL Control(0x44)
#define PLL_M_CODE_MASK				0xF				//PLL M code mask
#define PLL_K_CODE_MASK				(0x7<<4)		//PLL K code mask
#define PLL_BYPASS_N				(0x1<<7)		//bypass PLL N code
#define PLL_N_CODE_MASK				(0xFF<<8)		//PLL N code mask

#define PLL_CTRL_M_VAL(m)		((m)&0xf)
#define PLL_CTRL_K_VAL(k)		(((k)&0x7)<<4)
#define PLL_CTRL_N_VAL(n)		(((n)&0xff)<<8)

//PLL2 CONTROL
#define PLL2_ENA					(0x1<<15)
#define PLL_2_RATIO_8X				(0x0)
#define PLL_2_RATIO_16X				(0x1)

//LDO Control(0x48)
#define LDO_ENABLE					(0x1<<15)

#define LDO_OUT_VOL_CTRL_MASK		(0xf<<0)
#define LDO_OUT_VOL_CTRL_1_55V		(0xf<<0)
#define LDO_OUT_VOL_CTRL_1_50V		(0xe<<0)
#define LDO_OUT_VOL_CTRL_1_45V		(0xd<<0)
#define LDO_OUT_VOL_CTRL_1_40V		(0xc<<0)
#define LDO_OUT_VOL_CTRL_1_35V		(0xb<<0)
#define LDO_OUT_VOL_CTRL_1_30V		(0xa<<0)
#define LDO_OUT_VOL_CTRL_1_25V		(0x9<<0)
#define LDO_OUT_VOL_CTRL_1_20V		(0x8<<0)
#define LDO_OUT_VOL_CTRL_1_15V		(0x7<<0)
#define LDO_OUT_VOL_CTRL_1_05V		(0x6<<0)
#define LDO_OUT_VOL_CTRL_1_00V		(0x5<<0)
#define LDO_OUT_VOL_CTRL_0_95V		(0x4<<0)
#define LDO_OUT_VOL_CTRL_0_90V		(0x3<<0)
#define LDO_OUT_VOL_CTRL_0_85V		(0x2<<0)
#define LDO_OUT_VOL_CTRL_0_80V		(0x1<<0)
#define LDO_OUT_VOL_CTRL_0_75V		(0x0<<0)



//GPIO Pin Configuration(0x4C)
#define GPIO_1						(0x1<<1)
#define	GPIO_2						(0x1<<2)
#define	GPIO_3						(0x1<<3)
#define GPIO_4						(0x1<<4)
#define GPIO_5						(0x1<<5)


////INTERRUPT CONTROL(0x5E)
#define DISABLE_FAST_VREG			(0x1<<15)

#define AVC_TARTGET_SEL_MASK		(0x3<<12)
#define	AVC_TARTGET_SEL_NONE		(0x0<<12)
#define	AVC_TARTGET_SEL_R 			(0x1<<12)
#define	AVC_TARTGET_SEL_L			(0x2<<12)
#define	AVC_TARTGET_SEL_BOTH		(0x3<<12)

#define HP_DEPOP_MODE2_EN			(0x1<<8)
#define HP_DEPOP_MODE1_EN			(0x1<<9)
#define HP_L_M_UM_DEPOP_EN			(0x1<<7)
#define HP_R_M_UM_DEPOP_EN			(0x1<<6)
#define M_UM_DEPOP_EN				(0x1<<5)

//Stereo DAC Clock Control 1(0x60)
#define STEREO_BCLK_DIV1_MASK		(0xF<<12)
#define STEREO_BCLK_DIV1_1			(0x0<<12)
#define STEREO_BCLK_DIV1_2			(0x1<<12)
#define STEREO_BCLK_DIV1_3			(0x2<<12)
#define STEREO_BCLK_DIV1_4			(0x3<<12)
#define STEREO_BCLK_DIV1_5			(0x4<<12)
#define STEREO_BCLK_DIV1_6			(0x5<<12)
#define STEREO_BCLK_DIV1_7			(0x6<<12)
#define STEREO_BCLK_DIV1_8			(0x7<<12)
#define STEREO_BCLK_DIV1_9			(0x8<<12)
#define STEREO_BCLK_DIV1_10			(0x9<<12)
#define STEREO_BCLK_DIV1_11			(0xA<<12)
#define STEREO_BCLK_DIV1_12			(0xB<<12)
#define STEREO_BCLK_DIV1_13			(0xC<<12)
#define STEREO_BCLK_DIV1_14			(0xD<<12)
#define STEREO_BCLK_DIV1_15			(0xE<<12)
#define STEREO_BCLK_DIV1_16			(0xF<<12)

#define STEREO_BCLK_DIV2_MASK		(0x7<<8)
#define STEREO_BCLK_DIV2_2			(0x0<<8)
#define STEREO_BCLK_DIV2_4			(0x1<<8)
#define STEREO_BCLK_DIV2_8			(0x2<<8)
#define STEREO_BCLK_DIV2_16			(0x3<<8)
#define STEREO_BCLK_DIV2_32			(0x4<<8)

#define STEREO_AD_LRCK_DIV1_MASK	(0xF<<4)
#define STEREO_AD_LRCK_DIV1_1		(0x0<<4)
#define STEREO_AD_LRCK_DIV1_2		(0x1<<4)
#define STEREO_AD_LRCK_DIV1_3		(0x2<<4)
#define STEREO_AD_LRCK_DIV1_4		(0x3<<4)
#define STEREO_AD_LRCK_DIV1_5		(0x4<<4)
#define STEREO_AD_LRCK_DIV1_6		(0x5<<4)
#define STEREO_AD_LRCK_DIV1_7		(0x6<<4)
#define STEREO_AD_LRCK_DIV1_8		(0x7<<4)
#define STEREO_AD_LRCK_DIV1_9		(0x8<<4)
#define STEREO_AD_LRCK_DIV1_10		(0x9<<4)
#define STEREO_AD_LRCK_DIV1_11		(0xA<<4)
#define STEREO_AD_LRCK_DIV1_12		(0xB<<4)
#define STEREO_AD_LRCK_DIV1_13		(0xC<<4)
#define STEREO_AD_LRCK_DIV1_14		(0xD<<4)
#define STEREO_AD_LRCK_DIV1_15		(0xE<<4)
#define STEREO_AD_LRCK_DIV1_16		(0xF<<4)

#define STEREO_AD_LRCK_DIV2_MASK	(0x7<<1)
#define STEREO_AD_LRCK_DIV2_2		(0x0<<1)
#define STEREO_AD_LRCK_DIV2_4		(0x1<<1)
#define STEREO_AD_LRCK_DIV2_8		(0x2<<1)
#define STEREO_AD_LRCK_DIV2_16		(0x3<<1)
#define STEREO_AD_LRCK_DIV2_32		(0x4<<1)

#define STEREO_DA_LRCK_DIV_MASK		(1)
#define STEREO_DA_LRCK_DIV_32		(0)
#define STEREO_DA_LRCK_DIV_64		(1)

//Stereo DAC Clock Control 2(0x62)
#define STEREO_DA_FILTER_DIV1_MASK	(0xF<<12)
#define STEREO_DA_FILTER_DIV1_1		(0x0<<12)
#define STEREO_DA_FILTER_DIV1_2		(0x1<<12)
#define STEREO_DA_FILTER_DIV1_3		(0x2<<12)
#define STEREO_DA_FILTER_DIV1_4		(0x3<<12)
#define STEREO_DA_FILTER_DIV1_5		(0x4<<12)
#define STEREO_DA_FILTER_DIV1_6		(0x5<<12)
#define STEREO_DA_FILTER_DIV1_7		(0x6<<12)
#define STEREO_DA_FILTER_DIV1_8		(0x7<<12)
#define STEREO_DA_FILTER_DIV1_9		(0x8<<12)
#define STEREO_DA_FILTER_DIV1_10	(0x9<<12)
#define STEREO_DA_FILTER_DIV1_11	(0xA<<12)
#define STEREO_DA_FILTER_DIV1_12	(0xB<<12)
#define STEREO_DA_FILTER_DIV1_13	(0xC<<12)
#define STEREO_DA_FILTER_DIV1_14	(0xD<<12)
#define STEREO_DA_FILTER_DIV1_15	(0xE<<12)
#define STEREO_DA_FILTER_DIV1_16	(0xF<<12)

#define STEREO_DA_FILTER_DIV2_MASK	(0x7<<9)
#define STEREO_DA_FILTER_DIV2_2		(0x0<<9)
#define STEREO_DA_FILTER_DIV2_4		(0x1<<9)
#define STEREO_DA_FILTER_DIV2_8		(0x2<<9)
#define STEREO_DA_FILTER_DIV2_16	(0x3<<9)
#define STEREO_DA_FILTER_DIV2_32	(0x4<<9)

#define STEREO_AD_FILTER_DIV1_MASK	(0xF<<4)
#define STEREO_AD_FILTER_DIV1_1		(0x0<<4)
#define STEREO_AD_FILTER_DIV1_2		(0x1<<4)
#define STEREO_AD_FILTER_DIV1_3		(0x2<<4)
#define STEREO_AD_FILTER_DIV1_4		(0x3<<4)
#define STEREO_AD_FILTER_DIV1_5		(0x4<<4)
#define STEREO_AD_FILTER_DIV1_6		(0x5<<4)
#define STEREO_AD_FILTER_DIV1_7		(0x6<<4)
#define STEREO_AD_FILTER_DIV1_8		(0x7<<4)
#define STEREO_AD_FILTER_DIV1_9		(0x8<<4)
#define STEREO_AD_FILTER_DIV1_10	(0x9<<4)
#define STEREO_AD_FILTER_DIV1_11	(0xA<<4)
#define STEREO_AD_FILTER_DIV1_12	(0xB<<4)
#define STEREO_AD_FILTER_DIV1_13	(0xC<<4)
#define STEREO_AD_FILTER_DIV1_14	(0xD<<4)
#define STEREO_AD_FILTER_DIV1_15	(0xE<<4)
#define STEREO_AD_FILTER_DIV1_16	(0xF<<4)

#define STEREO_AD_FILTER_DIV2_MASK	(0x7<<1)
#define STEREO_AD_FILTER_DIV2_1		(0x0<<1)
#define STEREO_AD_FILTER_DIV2_2		(0x1<<1)
#define STEREO_AD_FILTER_DIV2_4		(0x2<<1)
#define STEREO_AD_FILTER_DIV2_8		(0x3<<1)
#define STEREO_AD_FILTER_DIV2_16	(0x4<<1)
#define STEREO_AD_FILTER_DIV2_32	(0x5<<1)


//Voice DAC PCM Clock Control 1(0x64)
#define VOICE_BCLK_DIV1_MASK		(0xF<<12)
#define VOICE_BCLK_DIV1_1			(0x0<<12)
#define VOICE_BCLK_DIV1_2			(0x1<<12)
#define VOICE_BCLK_DIV1_3			(0x2<<12)
#define VOICE_BCLK_DIV1_4			(0x3<<12)
#define VOICE_BCLK_DIV1_5			(0x4<<12)
#define VOICE_BCLK_DIV1_6			(0x5<<12)
#define VOICE_BCLK_DIV1_7			(0x6<<12)
#define VOICE_BCLK_DIV1_8			(0x7<<12)
#define VOICE_BCLK_DIV1_9			(0x8<<12)
#define VOICE_BCLK_DIV1_10			(0x9<<12)
#define VOICE_BCLK_DIV1_11			(0xA<<12)
#define VOICE_BCLK_DIV1_12			(0xB<<12)
#define VOICE_BCLK_DIV1_13			(0xC<<12)
#define VOICE_BCLK_DIV1_14			(0xD<<12)
#define VOICE_BCLK_DIV1_15			(0xE<<12)
#define VOICE_BCLK_DIV1_16			(0xF<<12)

#define VOICE_BCLK_DIV2_MASK		(0x7<<8)
#define VOICE_BCLK_DIV2_2			(0x0<<8)
#define VOICE_BCLK_DIV2_4			(0x1<<8)
#define VOICE_BCLK_DIV2_8			(0x2<<8)
#define VOICE_BCLK_DIV2_16			(0x3<<8)
#define VOICE_BCLK_DIV2_32			(0x4<<8)

#define VOICE_AD_LRCK_DIV1_MASK	(0xF<<4)
#define VOICE_AD_LRCK_DIV1_1		(0x0<<4)
#define VOICE_AD_LRCK_DIV1_2		(0x1<<4)
#define VOICE_AD_LRCK_DIV1_3		(0x2<<4)
#define VOICE_AD_LRCK_DIV1_4		(0x3<<4)
#define VOICE_AD_LRCK_DIV1_5		(0x4<<4)
#define VOICE_AD_LRCK_DIV1_6		(0x5<<4)
#define VOICE_AD_LRCK_DIV1_7		(0x6<<4)
#define VOICE_AD_LRCK_DIV1_8		(0x7<<4)
#define VOICE_AD_LRCK_DIV1_9		(0x8<<4)
#define VOICE_AD_LRCK_DIV1_10		(0x9<<4)
#define VOICE_AD_LRCK_DIV1_11		(0xA<<4)
#define VOICE_AD_LRCK_DIV1_12		(0xB<<4)
#define VOICE_AD_LRCK_DIV1_13		(0xC<<4)
#define VOICE_AD_LRCK_DIV1_14		(0xD<<4)
#define VOICE_AD_LRCK_DIV1_15		(0xE<<4)
#define VOICE_AD_LRCK_DIV1_16		(0xF<<4)

#define VOICE_AD_LRCK_DIV2_MASK	(0x7<<1)
#define VOICE_AD_LRCK_DIV2_2		(0x0<<1)
#define VOICE_AD_LRCK_DIV2_4		(0x1<<1)
#define VOICE_AD_LRCK_DIV2_8		(0x2<<1)
#define VOICE_AD_LRCK_DIV2_16		(0x3<<1)
#define VOICE_AD_LRCK_DIV2_32		(0x4<<1)

#define VOICE_DA_LRCK_DIV_MASK		(1)
#define VOICE_DA_LRCK_DIV_32		(0)
#define VOICE_DA_LRCK_DIV_64		(1)


//Psedueo Stereo & Spatial Effect Block Control(0x68)
#define SPATIAL_CTRL_EN				(0x1<<15)
#define ALL_PASS_FILTER_EN			(0x1<<14)
#define PSEUDO_STEREO_EN			(0x1<<13)
#define STEREO_EXPENSION_EN			(0x1<<12)

#define SPATIAL_3D_GAIN1_MASK			(0x3<<10)
#define SPATIAL_3D_GAIN1_1_0			(0x0<<10)
#define SPATIAL_3D_GAIN1_1_5			(0x1<<10)
#define SPATIAL_3D_GAIN1_2_0			(0x2<<10)

#define SPATIAL_3D_RATIO1_MASK			(0x3<<8)
#define SPATIAL_3D_RATIO1_0_0			(0x0<<8)
#define SPATIAL_3D_RATIO1_0_66			(0x1<<8)
#define SPATIAL_3D_RATIO1_1_0			(0x2<<8)

#define SPATIAL_3D_GAIN2_MASK			(0x3<<6)
#define SPATIAL_3D_GAIN2_1_0			(0x0<<6)
#define SPATIAL_3D_GAIN2_1_5			(0x1<<6)
#define SPATIAL_3D_GAIN2_2_0			(0x2<<6)

#define SPATIAL_3D_RATIO2_MASK			(0x3<<4)
#define SPATIAL_3D_RATIO2_0_0			(0x0<<4)
#define SPATIAL_3D_RATIO2_0_66			(0x1<<4)
#define SPATIAL_3D_RATIO2_1_0			(0x2<<4)

#define APF_MASK					(0x3)
#define APF_FOR_48K					(0x3)
#define APF_FOR_44_1K				(0x2)
#define APF_FOR_32K					(0x1)

//EQ Control and Status /ADC HPF Control(0x6E)
#define EN_HW_EQ_BLK			(0x1<<15)		//HW EQ block control

#define EQ_SOUR_SEL_DAC			(0x0<<14)
#define EQ_SOUR_SEL_ADC			(0x1<<14)

#define EQ_CHANGE_EN			(0x1<<7)		//EQ parameter update control
#define EN_HW_EQ_HPF			(0x1<<4)		//EQ High Pass Filter Control
#define EN_HW_EQ_BP3			(0x1<<3)		//EQ Band-3 Control
#define EN_HW_EQ_BP2			(0x1<<2)		//EQ Band-2 Control
#define EN_HW_EQ_BP1			(0x1<<1)		//EQ Band-1 Control
#define EN_HW_EQ_LPF			(0x1<<0)		//EQ Low Pass Filter Control


//AEC register command(0x74)

#define VODSP_BUSY					(0x1<<15)	//VODSP I2C busy flag

#define VODSP_S_FROM_VODSP_RD		(0x0<<14)
#define VODSP_S_FROM_MX72			(0x1<<14)

#define VODSP_CLK_SEL_MASK			(0x3<<12)	//VODSP CLK select Mask
#define VODSP_CLK_SEL_12_288M		(0x0<<12)	//VODSP CLK select 12.288Mhz
#define VODSP_CLK_SEL_6_144M		(0x1<<12)	//VODSP CLK select 6.144Mhz
#define VODSP_CLK_SEL_3_072M		(0x2<<12)	//VODSP CLK select 3.072Mhz
#define VODSP_CLK_SEL_2_048M		(0x3<<12)	//VODSP CLK select 2.0488Mhz

#define VODSP_READ_ENABLE			(0x1<<9)	//VODSP Read Enable
#define VODSP_WRITE_ENABLE			(0x1<<8)	//VODSP Write Enable

#define VODSP_CMD_MASK				(0xFF<<0)
#define VODSP_CMD_MW				(0x3B<<0)		//Memory Write
#define VODSP_CMD_MR				(0x37<<0)		//Memory Read
#define VODSP_CMD_RR				(0x60<<0)		//Register Read
#define VODSP_CMD_RW				(0x68<<0)		//Register Write


/*************************************************************************************************
  *Index register of codec
  *************************************************************************************************/
/*Index(0x20) for Auto Volume Control*/ 
#define	AVC_CH_SEL_MASK				(0x1<<7)
#define AVC_CH_SEL_L_CH				(0x0<<7)
#define AVC_CH_SEL_R_CH				(0x1<<7)
#define ENABLE_AVC_GAIN_CTRL		(0x1<<15)
//*************************************************************************************************
//*************************************************************************************************

#define REALTEK_HWDEP 0

#if REALTEK_HWDEP

struct rt56xx_reg_state
{
	unsigned int reg_index;
	unsigned int reg_value;
};

struct rt56xx_cmd
{
	size_t number;
	struct rt56xx_reg_state __user *buf;		
};


enum 
{
	RT_READ_CODEC_REG_IOCTL = _IOR('R', 0x01, struct rt56xx_cmd),
	RT_READ_ALL_CODEC_REG_IOCTL = _IOR('R', 0x02, struct rt56xx_cmd),
	RT_WRITE_CODEC_REG_IOCTL = _IOW('R', 0x03, struct rt56xx_cmd),
};

#endif


enum pll_sel
{
	RT5625_PLL1_FROM_MCLK = 0,
	RT5625_PLL1_FROM_BCLK,
	RT5625_PLL1_FROM_VBCLK,
};

enum AEC_MODE
{
	PCM_IN_PCM_OUT = 0,
	ANALOG_IN_ANALOG_OUT,
	DAC_IN_ADC_OUT,
	VODSP_AEC_DISABLE		
};

enum
{
	PCM_MASTER_MODE_A=0,
	PCM_MASTER_MODE_B,
	PCM_SLAVE_MODE_A,
	PCM_SLAVE_MODE_B,
};


enum RT5625_FUNC_SEL
{
	RT5625_AEC_DISABLE =0,
	RT5625_AEC_PCM_IN_OUT,
	RT5625_AEC_IIS_IN_OUT,
	RT5625_AEC_ANALOG_IN_OUT,

};


struct rt5625_setup_data {
	int i2c_bus;
	int i2c_address;
};

typedef struct 
{ 
	unsigned short int VoiceDSPIndex;
	unsigned short int VoiceDSPValue;

}Voice_DSP_Reg;

extern struct snd_soc_dai rt5625_dai[];
extern struct snd_soc_codec_device soc_codec_dev_rt5625;

#endif
