#ifndef _AML_SYNO9629_CODEC_H
#define _AML_SYNO9629_CODEC_H

#define APB_BASE					0x4000

// data path reset register. bit 0 rstdacdpz,bit 1  rstadcdpz  
// when i2s reconfiguration, reset data path recommanded
#define ADAC_RESET                		0x00
//bit 0 latch
#define ADAC_LATCH                		0x01
/* 
[3:0]: 0=256*Fs; 1=384*Fs; 2=12M; 3=24M; 4=12.288M; 5=24.576M; 6=11.2896M; 7=22.5792M; 
		8=18.432M; 9=36.864M; 10=16.9344M; 11=33.8688M; >=12:Reserved.
*/
#define ADC_MCLK_SEL                		0x02
#define DAC_MCLK_SEL                		0x03
//
// [3:0]: 0=8k; 1=11.025k; 2=12; 3=16k; 4=22.05k; 5=24k; 6=32k, 7=44.1k, 8=48k; 9=88.2k; 10=96k; 11=192k; >=12:Reserved.
#define ADC_I2S_FS_SEL      				0x0c
#define DAC_I2S_FS_SEL      				0x0d
/*
i2smode[2:0]:default:1h
Data format select:
000: Right justified
001: I2S justified
010: Left justified
011: Burst 1 format
100: Burst 2 format
101: Mono Burst 1 format
110: Mono Burst 2 format
111: Reserved
i2smsmode[3] default:0
I2S slave/master:
0: slave mode
1: master mode
*/
#define ADAC_I2S_MODE_SEL   			0x0e

/*
acodec standby/sleep control. [0] ensleep .default:0xAEh
*/
#define ADAC_STAND_SLEEP_CTRL      		0x11
/*
Power consumption management;cfgiref[3:0] default:0
*/
#define ADAC_POWER_CUM_CTRL     		0x12
/*
[0] pdz Overall power-down signal
[2] pdmbias1z Microphone Bias 1 power-down signal
*/
#define ADAC_POWER_CTRL0				0x15
/*
[0] pdadclz Left ADC power-down signal
[1] pdadcrz Right ADC power-down signal
[2]pdpgalz Left PGA power-down signal
[3] pdpgarz Right PGA power-down signal
*/
#define ADAC_POWER_CTRL1				0x16
/*
R24 (18h) 0 pddaclz Left DAC power-down signal 0h
R24 (18h) 1 pddacrz Right DAC power-down signal 0h
R24 (18h) 4 pdhs1rz Right headset power-down signal 0h
R24 (18h) 5 pdhs1lz Left headset power-down signal 0h
R24 (18h) 6 pdldr1rz Right single-end line driver 1 power-down signal 0h
R24 (18h) 7 pdldr1lz Left single-end line driver 1 power-down signal
*/
#define ADAC_POWER_CTRL3				0x18

/*
R29 (1Dh)
[0]
recmute[0]
Recording left channel digital mute:
0: un-mute
1: mute
0h
R29 (1Dh)
[1]
recmute[1]
Recording right channel digital mute:
0: un-mute
1: mute
*/
#define ADAC_MUTE_CTRL0       		        0x1d
/*
R31 (1Fh)
[4]
hs1mute[0]
Headset left channel analog mute:
0: un-mute
1: mute
0h
R31 (1Fh)
[5]
hs1mute[1]
Headset right channel analog mute:
0: un-mute
1: mute
*/
#define ADAC_MUTE_CTRL2			        0x1f
/*
R33 (21h)
[2]
ldr1outmute[0]
Playback left channel analog mute:
0: un-mute
1: mute
0h
R33 (21h)
[3]
ldr1outmute[1]
Playback right channel analog mute:
0: un-mute
1: mute
*/
#define ADAC_MUTE_CTRL4        		        0x21
/*
Recording digital master volume control:
The least significant 8 bits control the left channel:
00000000: 30dB
00000001 to 0010011: 28.5dB to 1.5dB
00010100: 0dB
00010101 to 1010011: -1.5dB to ¨C94.5dB
01010100: -96dB
01010101 to 01111111: Reserved
The most significant 8 bits control the right channel:
00000000: 30dB
00000001 to 0010011: 28.5dB to 1.5dB
00010100: 0dB
00010101 to 1010011: -1.5dB to ¨C94.5dB
01010100: -96dB
01010101 to 1111111: Reserved
*/
#define ADC_REC_MVOL_LSB_CTRL  		0x24
#define ADC_REC_MVOL_MSB_CTRL  		0x25
/*
Input PGA volume control:
The least significant 8 bits control the left channel:
00000000: -6 dB
00000001: -4.5 dB
¡K: ¡K
00000100: 0 dB
¡K: ¡K
00010001: 19.5 dB
00010010: 21 dB
„d00010011: Reserved
The most significant 8 bits control the right channel:
00000000: -6 dB
00000001: -4.5 dB
¡K: ¡K
00000100: 0 dB
¡K: ¡K
00010001: 19.5 dB
00010010: 21 dB
„d00010011: Reserved
*/
#define ADC_PGA_VOL_LSB_CTRL  			0x26
#define ADC_PGA_VOL_MSB_CTRL  		0x27

#define DAC_PLYBACK_MVOL_LSB_CTRL  	0x34
#define DAC_PLYBACK_MVOL_MSB_CTRL  	0x35
#define DAC_HS_VOL_LSB_CTRL  			0x38
#define DAC_HS_VOL_MSB_CTRL  			0x39
#define ADC_REC_INPUT_CH_LSB_SEL			0x47
#define ADC_REC_INPUT_CH_MSB_SEL			0x48
#define DAC_PLYBACK_MIXER_LSB_CTRL		0x59
#define DAC_PLYBACK_MIXER_MSB_CTRL		0x5a

#define ADAC_REC_CH_SEL_LSB			0x47
#define ADAC_REC_CH_SEL_MSB			0x48
#define ADAC_DIGITAL_TEST_MODE_SEL 	0xd2
#define ADC_REC_PATH_MIXER_SEL 		0xd3 
#define DAC_PLYBACK_DIG_MIXER_SEL 	0xd4
/*
VCM ramp up settle register
*/
 #define ADAC_VCM_RAMP_CTRL      		0xa2
/*
VCM pre-charge time control definition
*/
#define ADAC_DIG_ASS_TEST_REG1      				0xf0
/*
Digital-assisted analog test reg. Bypass power up/down sequences
*/
#define ADAC_DIG_ASS_TEST_REG2  	     				0xf1
#define ADAC_DIG_ASS_TEST_REG3  	     				0xf2


/*
Digital-assisted analog test reg. 4
*/
#define ADAC_DIG_ASS_TEST_REG4  					0xf3
#define ADAC_DIG_ASS_TEST_REG5  					0xf3
/*
Pure analog test reg. 3
*/
#define ADAC_ANALOG_TEST_REG3 					0xfb

#define ADAC_VCM_REG1		0x80
#define ADAC_VCM_REG2		0x81

#define ADAC_TEST_REG1	0xe0
#define ADAC_TEST_REG2	0xe1
#define ADAC_TEST_REG3	0xe2
#define ADAC_TEST_REG4	0xe3

#define ADAC_MAXREG	256

#define NO_CLOCK_TO_CODEC   0
#define PCMOUT_TO_DAC           1
#define AIU_I2SOUT_TO_DAC    2



#endif
