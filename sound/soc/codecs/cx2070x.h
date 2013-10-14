/*
 * ALSA SoC CX2070X Channel codec driver
 *
 * Copyright:   (C) 2009/2010 Conexant Systems
 *
 * Based on sound/soc/codecs/tlv320aic2x.c by Vladimir Barinov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _CX2070X_H
#define _CX2070X_H


#define CONFIG_SND_DIGICOLOR_SOC_CHANNEL_VER_4_30F 1

#ifdef CONFIG_SND_SOC_CNXT_FW_UPDATE
#define CONFIG_SND_CX2070X_LOAD_FW 1
#endif
//#define CONFIG_SND_CX2070X_USE_FW_H 1
//#define CONFIG_CNXT_USING_SPI_BUS 1

#ifdef CONFIG_SND_SOC_CNXT_JACKSENSE
#define CONFIG_SND_CX2070X_GPIO_JACKSENSE 1
#endif 
//#define CONFIG_SND_CX2070X_GPIO_RESET    1
#define CONFIG_SND_CXLIFEGUARD 1
//#define CONFIG_CXNT_SOFTWOARE_SIMULATION      1
#define  DBG_MONITOR_REG  1

//#define GPIO_HP_JACKSENSE 178 //Tegra 250
//.#define JACK_SENSE_GPIO_PIN    178 // Tegra
//#define CODEC_RESET_GPIO_PIN   184 //  Tegra

#define JACK_SENSE_GPIO_PIN   151 //s5pc110 GPH2_5
#define CODEC_RESET_GPIO_PIN  157 //s5pc110 reset pin.
#define FOR_MID 0

#if (defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE) ) && !defined(CONFIG_CNXT_USING_SPI_BUS)
#define USING_I2C 1
#endif 

#if defined(CONFIG_SPI_MASTER) && defined(CONFIG_CNXT_USING_SPI_BUS)
#define USING_SPI 1
#endif 


enum Cx_INPUT_SEL{
    Cx_INPUT_SEL_BY_GPIO = 0,
    Cx_INPUT_SEL_MIC,
    Cx_INPUT_SEL_LINE,
    Cx_INPUT_SEL_DPORT2,
};

enum Cx_OUTPUT_SEL{
    Cx_OUTPUT_SEL_BY_GPIO = 0,
    Cx_OUTPUT_SEL_SPK,
    Cx_OUTPUT_SEL_LINE,
    Cx_OUTPUT_SEL_HP,
    Cx_OUTPUT_SEL_DPORT2,
};

enum {
	OFF,
	RCV,
	SPK_PATH,
	HP_PATH,
	HP_NO_MIC,
	BT,
	SPK_HP,
	RING_SPK,
	RING_HP,
	RING_HP_NO_MIC,
	RING_SPK_HP,
};

enum {
	MIC_OFF,
	Main_Mic,
	Hands_Free_Mic,
	BT_Sco_Mic,
};

#define CX2070X_I2C_DRIVER_NAME	"cx2070x-i2c"
#define CX2070X_SPI_DRIVER_NAME	"cx2070x-spi"
#define CX2070X_FIRMWARE_FILENAME "cnxt/cx2070x.fw"
#define AUDDRV_VERSION(major0,major1, minor, build ) ((major0)<<24|(major1)<<16| (minor)<<8 |(build))

#endif 
