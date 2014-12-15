#ifndef _TAS5711_H
#define _TAS5711_H

#define DDX_I2C_ADDR								0x36

#define DDX_CLOCK_CTL                               0x00
#define DDX_DEVICE_ID                               0x01
#define DDX_ERROR_STATUS                            0x02
#define DDX_SYS_CTL_1                               0x03
#define DDX_SERIAL_DATA_INTERFACE                   0x04
#define DDX_SYS_CTL_2                               0x05
#define DDX_SOFT_MUTE                               0x06
#define DDX_MASTER_VOLUME                           0x07
#define DDX_CHANNEL1_VOL							0x08
#define DDX_CHANNEL2_VOL							0x09
#define DDX_CHANNEL3_VOL							0x0A
#define DDX_VOLUME_CONFIG                           0x0E

#define DDX_MODULATION_LIMIT                        0x10
#define DDX_IC_DELAY_CHANNEL_1                      0x11
#define DDX_IC_DELAY_CHANNEL_2                      0x12
#define DDX_IC_DELAY_CHANNEL_3                      0x13
#define DDX_IC_DELAY_CHANNEL_4                      0x14
#define DDX_PWM_SHUTDOWN_GROUP                      0x19
#define DDX_START_STOP_PERIOD                       0x1A
#define DDX_OSC_TRIM                                0x1B
#define DDX_BKND_ERR                                0x1C
#define DDX_NUM_BYTE_REG                            0x1D

#define DDX_INPUT_MUX                               0x20
#define DDX_CH4_SOURCE_SELECT                       0x21

#define DDX_PWM_MUX                                 0x25

#define DDX_CH1_BQ_0								0x29
#define DDX_CH1_BQ_1                                0x2A
#define DDX_CH1_BQ_2                                0x2B
#define DDX_CH1_BQ_3                                0x2C
#define DDX_CH1_BQ_4                                0x2D
#define DDX_CH1_BQ_5                                0x2E
#define DDX_CH1_BQ_6                                0x2F

#define DDX_CH2_BQ_0								0x30
#define DDX_CH2_BQ_1								0x31
#define DDX_CH2_BQ_2								0x32
#define DDX_CH2_BQ_3								0x33
#define DDX_CH2_BQ_4								0x34
#define DDX_CH2_BQ_5								0x35
#define DDX_CH2_BQ_6								0x36

#define DDX_DRC1_AE                                 0x3A
#define DDX_DRC1_AA                                 0x3B
#define DDX_DRC1_AD                                 0x3C
#define DDX_DRC2_AE                                 0x3D
#define DDX_DRC2_AA                                 0x3E
#define DDX_DRC2_AD                                 0x3F
#define DDX_DRC1_T                                  0x40
#define DDX_DRC1_K                                  0x41
#define DDX_DRC1_O                                  0x42
#define DDX_DRC2_T                                  0x43
#define DDX_DRC2_K                                  0x44
#define DDX_DRC2_O                                  0x45
#define DDX_DRC_CTL                                 0x46

#define DDX_BANKSWITCH_AND_EQCTL                    0x50
#define DDX_CH_1_OUTPUT_MIXER                       0x51
#define DDX_CH_2_OUTPUT_MIXER                       0x52
#define DDX_CH_1_INPUT_MIXER                        0x53
#define DDX_CH_2_INPUT_MIXER                        0x54
#define DDX_CH_3_INPUT_MIXER                        0x55
#define DDX_OUTPUT_POST_SCALE                       0x56
#define DDX_OUTPUT_PRE_SCALE                        0x57

#define DDX_CH1_BQ_7                                0x58
#define DDX_CH1_BQ_8                                0x59
#define DDX_SUBCHANNEL_BQ_0							0x5A
#define DDX_SUBCHANNEL_BQ_1							0x5B
#define DDX_CH2_BQ_7                                0x5C
#define DDX_CH2_BQ_8                                0x5D
#define DDX_PSEUDO_CH2_BQ_0                         0x5E

#define DDX_CH_4_OUTPUT_MIXER                       0x60
#define DDX_CH_4_INPUT_MIXER                        0x61
#define DDX_CH_IDF_POST_SCALE                       0x62
#define DDX_CH_DEV_ADDR_ENABLE                      0xF8
#define DDX_CH_DEV_ADDR_UPDATE                      0xF9




#define DDX_DRC_BYTES                               (8)
#define DDX_BQ_BYTES								(20)
extern int tas5711_add_i2c_device(struct platform_device *pdev);
extern void TAS5711_SetLeftVolume(unsigned char LeftVolume);
extern unsigned char TAS5711_GetLeftVolume(void);
extern void TAS5711_SetRightVolume(unsigned char RightVolume);
extern unsigned char TAS5711_GetRightVolume(void);
extern void TAS5711_SetMasterMute(unsigned char Mute);
extern unsigned char TAS5711_GetMasterMute(void);
extern void TAS5711_SetSubWooferBQ(void);
extern void TAS5711_SetSubWooferVolume(unsigned char SubWooferVolume);
extern unsigned char TAS5711_GetSubWooferVolume(void);
extern void TAS5711_SetSubwooferMute(unsigned char Mute);
extern unsigned char TAS5711_GetSubwooferMute(void);
extern void TAS5711_SetHardMute(unsigned char Mute);
extern unsigned char TAS5711_GetHardMute(void);


#endif
