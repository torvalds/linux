/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * wm8741.h  --  WM8423 ASoC driver
 *
 * Copyright 2010 Wolfson Microelectronics, plc
 *
 * Author: Ian Lartey <ian@opensource.wolfsonmicro.com>
 *
 * Based on wm8753.h
 */

#ifndef _WM8741_H
#define _WM8741_H

/*
 * Register values.
 */
#define WM8741_DACLLSB_ATTENUATION              0x00
#define WM8741_DACLMSB_ATTENUATION              0x01
#define WM8741_DACRLSB_ATTENUATION              0x02
#define WM8741_DACRMSB_ATTENUATION              0x03
#define WM8741_VOLUME_CONTROL                   0x04
#define WM8741_FORMAT_CONTROL                   0x05
#define WM8741_FILTER_CONTROL                   0x06
#define WM8741_MODE_CONTROL_1                   0x07
#define WM8741_MODE_CONTROL_2                   0x08
#define WM8741_RESET                            0x09
#define WM8741_ADDITIONAL_CONTROL_1             0x20

#define WM8741_REGISTER_COUNT                   11
#define WM8741_MAX_REGISTER                     0x20

/*
 * Field Definitions.
 */

/*
 * R0 (0x00) - DACLLSB_ATTENUATION
 */
#define WM8741_UPDATELL                         0x0020  /* UPDATELL */
#define WM8741_UPDATELL_MASK                    0x0020  /* UPDATELL */
#define WM8741_UPDATELL_SHIFT                        5  /* UPDATELL */
#define WM8741_UPDATELL_WIDTH                        1  /* UPDATELL */
#define WM8741_LAT_4_0_MASK                     0x001F  /* LAT[4:0] - [4:0] */
#define WM8741_LAT_4_0_SHIFT                         0  /* LAT[4:0] - [4:0] */
#define WM8741_LAT_4_0_WIDTH                         5  /* LAT[4:0] - [4:0] */

/*
 * R1 (0x01) - DACLMSB_ATTENUATION
 */
#define WM8741_UPDATELM                         0x0020  /* UPDATELM */
#define WM8741_UPDATELM_MASK                    0x0020  /* UPDATELM */
#define WM8741_UPDATELM_SHIFT                        5  /* UPDATELM */
#define WM8741_UPDATELM_WIDTH                        1  /* UPDATELM */
#define WM8741_LAT_9_5_0_MASK                   0x001F  /* LAT[9:5] - [4:0] */
#define WM8741_LAT_9_5_0_SHIFT                       0  /* LAT[9:5] - [4:0] */
#define WM8741_LAT_9_5_0_WIDTH                       5  /* LAT[9:5] - [4:0] */

/*
 * R2 (0x02) - DACRLSB_ATTENUATION
 */
#define WM8741_UPDATERL                         0x0020  /* UPDATERL */
#define WM8741_UPDATERL_MASK                    0x0020  /* UPDATERL */
#define WM8741_UPDATERL_SHIFT                        5  /* UPDATERL */
#define WM8741_UPDATERL_WIDTH                        1  /* UPDATERL */
#define WM8741_RAT_4_0_MASK                     0x001F  /* RAT[4:0] - [4:0] */
#define WM8741_RAT_4_0_SHIFT                         0  /* RAT[4:0] - [4:0] */
#define WM8741_RAT_4_0_WIDTH                         5  /* RAT[4:0] - [4:0] */

/*
 * R3 (0x03) - DACRMSB_ATTENUATION
 */
#define WM8741_UPDATERM                         0x0020  /* UPDATERM */
#define WM8741_UPDATERM_MASK                    0x0020  /* UPDATERM */
#define WM8741_UPDATERM_SHIFT                        5  /* UPDATERM */
#define WM8741_UPDATERM_WIDTH                        1  /* UPDATERM */
#define WM8741_RAT_9_5_0_MASK                   0x001F  /* RAT[9:5] - [4:0] */
#define WM8741_RAT_9_5_0_SHIFT                       0  /* RAT[9:5] - [4:0] */
#define WM8741_RAT_9_5_0_WIDTH                       5  /* RAT[9:5] - [4:0] */

/*
 * R4 (0x04) - VOLUME_CONTROL
 */
#define WM8741_AMUTE                            0x0080  /* AMUTE */
#define WM8741_AMUTE_MASK                       0x0080  /* AMUTE */
#define WM8741_AMUTE_SHIFT                           7  /* AMUTE */
#define WM8741_AMUTE_WIDTH                           1  /* AMUTE */
#define WM8741_ZFLAG_MASK                       0x0060  /* ZFLAG - [6:5] */
#define WM8741_ZFLAG_SHIFT                           5  /* ZFLAG - [6:5] */
#define WM8741_ZFLAG_WIDTH                           2  /* ZFLAG - [6:5] */
#define WM8741_IZD                              0x0010  /* IZD */
#define WM8741_IZD_MASK                         0x0010  /* IZD */
#define WM8741_IZD_SHIFT                             4  /* IZD */
#define WM8741_IZD_WIDTH                             1  /* IZD */
#define WM8741_SOFT                             0x0008  /* SOFT MUTE */
#define WM8741_SOFT_MASK                        0x0008  /* SOFT MUTE */
#define WM8741_SOFT_SHIFT                            3  /* SOFT MUTE */
#define WM8741_SOFT_WIDTH                            1  /* SOFT MUTE */
#define WM8741_ATC                              0x0004  /* ATC */
#define WM8741_ATC_MASK                         0x0004  /* ATC */
#define WM8741_ATC_SHIFT                             2  /* ATC */
#define WM8741_ATC_WIDTH                             1  /* ATC */
#define WM8741_ATT2DB                           0x0002  /* ATT2DB */
#define WM8741_ATT2DB_MASK                      0x0002  /* ATT2DB */
#define WM8741_ATT2DB_SHIFT                          1  /* ATT2DB */
#define WM8741_ATT2DB_WIDTH                          1  /* ATT2DB */
#define WM8741_VOL_RAMP                         0x0001  /* VOL_RAMP */
#define WM8741_VOL_RAMP_MASK                    0x0001  /* VOL_RAMP */
#define WM8741_VOL_RAMP_SHIFT                        0  /* VOL_RAMP */
#define WM8741_VOL_RAMP_WIDTH                        1  /* VOL_RAMP */

/*
 * R5 (0x05) - FORMAT_CONTROL
 */
#define WM8741_PWDN                             0x0080  /* PWDN */
#define WM8741_PWDN_MASK                        0x0080  /* PWDN */
#define WM8741_PWDN_SHIFT                            7  /* PWDN */
#define WM8741_PWDN_WIDTH                            1  /* PWDN */
#define WM8741_REV                              0x0040  /* REV */
#define WM8741_REV_MASK                         0x0040  /* REV */
#define WM8741_REV_SHIFT                             6  /* REV */
#define WM8741_REV_WIDTH                             1  /* REV */
#define WM8741_BCP                              0x0020  /* BCP */
#define WM8741_BCP_MASK                         0x0020  /* BCP */
#define WM8741_BCP_SHIFT                             5  /* BCP */
#define WM8741_BCP_WIDTH                             1  /* BCP */
#define WM8741_LRP                              0x0010  /* LRP */
#define WM8741_LRP_MASK                         0x0010  /* LRP */
#define WM8741_LRP_SHIFT                             4  /* LRP */
#define WM8741_LRP_WIDTH                             1  /* LRP */
#define WM8741_FMT_MASK                         0x000C  /* FMT - [3:2] */
#define WM8741_FMT_SHIFT                             2  /* FMT - [3:2] */
#define WM8741_FMT_WIDTH                             2  /* FMT - [3:2] */
#define WM8741_IWL_MASK                         0x0003  /* IWL - [1:0] */
#define WM8741_IWL_SHIFT                             0  /* IWL - [1:0] */
#define WM8741_IWL_WIDTH                             2  /* IWL - [1:0] */

/*
 * R6 (0x06) - FILTER_CONTROL
 */
#define WM8741_ZFLAG_HI                         0x0080  /* ZFLAG_HI */
#define WM8741_ZFLAG_HI_MASK                    0x0080  /* ZFLAG_HI */
#define WM8741_ZFLAG_HI_SHIFT                        7  /* ZFLAG_HI */
#define WM8741_ZFLAG_HI_WIDTH                        1  /* ZFLAG_HI */
#define WM8741_DEEMPH_MASK                      0x0060  /* DEEMPH - [6:5] */
#define WM8741_DEEMPH_SHIFT                          5  /* DEEMPH - [6:5] */
#define WM8741_DEEMPH_WIDTH                          2  /* DEEMPH - [6:5] */
#define WM8741_DSDFILT_MASK                     0x0018  /* DSDFILT - [4:3] */
#define WM8741_DSDFILT_SHIFT                         3  /* DSDFILT - [4:3] */
#define WM8741_DSDFILT_WIDTH                         2  /* DSDFILT - [4:3] */
#define WM8741_FIRSEL_MASK                      0x0007  /* FIRSEL - [2:0] */
#define WM8741_FIRSEL_SHIFT                          0  /* FIRSEL - [2:0] */
#define WM8741_FIRSEL_WIDTH                          3  /* FIRSEL - [2:0] */

/*
 * R7 (0x07) - MODE_CONTROL_1
 */
#define WM8741_MODE8X                           0x0080  /* MODE8X */
#define WM8741_MODE8X_MASK                      0x0080  /* MODE8X */
#define WM8741_MODE8X_SHIFT                          7  /* MODE8X */
#define WM8741_MODE8X_WIDTH                          1  /* MODE8X */
#define WM8741_OSR_MASK                         0x0060  /* OSR - [6:5] */
#define WM8741_OSR_SHIFT                             5  /* OSR - [6:5] */
#define WM8741_OSR_WIDTH                             2  /* OSR - [6:5] */
#define WM8741_SR_MASK                          0x001C  /* SR - [4:2] */
#define WM8741_SR_SHIFT                              2  /* SR - [4:2] */
#define WM8741_SR_WIDTH                              3  /* SR - [4:2] */
#define WM8741_MODESEL_MASK                     0x0003  /* MODESEL - [1:0] */
#define WM8741_MODESEL_SHIFT                         0  /* MODESEL - [1:0] */
#define WM8741_MODESEL_WIDTH                         2  /* MODESEL - [1:0] */

/*
 * R8 (0x08) - MODE_CONTROL_2
 */
#define WM8741_DSD_GAIN                         0x0040  /* DSD_GAIN */
#define WM8741_DSD_GAIN_MASK                    0x0040  /* DSD_GAIN */
#define WM8741_DSD_GAIN_SHIFT                        6  /* DSD_GAIN */
#define WM8741_DSD_GAIN_WIDTH                        1  /* DSD_GAIN */
#define WM8741_SDOUT                            0x0020  /* SDOUT */
#define WM8741_SDOUT_MASK                       0x0020  /* SDOUT */
#define WM8741_SDOUT_SHIFT                           5  /* SDOUT */
#define WM8741_SDOUT_WIDTH                           1  /* SDOUT */
#define WM8741_DOUT                             0x0010  /* DOUT */
#define WM8741_DOUT_MASK                        0x0010  /* DOUT */
#define WM8741_DOUT_SHIFT                            4  /* DOUT */
#define WM8741_DOUT_WIDTH                            1  /* DOUT */
#define WM8741_DIFF_MASK                        0x000C  /* DIFF - [3:2] */
#define WM8741_DIFF_SHIFT                            2  /* DIFF - [3:2] */
#define WM8741_DIFF_WIDTH                            2  /* DIFF - [3:2] */
#define WM8741_DITHER_MASK                      0x0003  /* DITHER - [1:0] */
#define WM8741_DITHER_SHIFT                          0  /* DITHER - [1:0] */
#define WM8741_DITHER_WIDTH                          2  /* DITHER - [1:0] */

/* DIFF field values */
#define WM8741_DIFF_MODE_STEREO                      0  /* stereo normal */
#define WM8741_DIFF_MODE_STEREO_REVERSED             2  /* stereo reversed */
#define WM8741_DIFF_MODE_MONO_LEFT                   1  /* mono left */
#define WM8741_DIFF_MODE_MONO_RIGHT                  3  /* mono right */

/*
 * R32 (0x20) - ADDITONAL_CONTROL_1
 */
#define WM8741_DSD_LEVEL                        0x0002  /* DSD_LEVEL */
#define WM8741_DSD_LEVEL_MASK                   0x0002  /* DSD_LEVEL */
#define WM8741_DSD_LEVEL_SHIFT                       1  /* DSD_LEVEL */
#define WM8741_DSD_LEVEL_WIDTH                       1  /* DSD_LEVEL */
#define WM8741_DSD_NO_NOTCH                     0x0001  /* DSD_NO_NOTCH */
#define WM8741_DSD_NO_NOTCH_MASK                0x0001  /* DSD_NO_NOTCH */
#define WM8741_DSD_NO_NOTCH_SHIFT                    0  /* DSD_NO_NOTCH */
#define WM8741_DSD_NO_NOTCH_WIDTH                    1  /* DSD_NO_NOTCH */

#define  WM8741_SYSCLK 0

struct wm8741_platform_data {
	u32 diff_mode;   /* Differential Output Mode */
};

#endif
