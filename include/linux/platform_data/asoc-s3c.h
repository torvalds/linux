/*
 * Copyright (c) 2009 Samsung Electronics Co. Ltd
 * Author: Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* The machine init code calls s3c*_ac97_setup_gpio with
 * one of these defines in order to select appropriate bank
 * of GPIO for AC97 pins
 */
#define S3C64XX_AC97_GPD  0
#define S3C64XX_AC97_GPE  1
extern void s3c64xx_ac97_setup_gpio(int);

struct samsung_i2s {
/* If the Primary DAI has 5.1 Channels */
#define QUIRK_PRI_6CHAN		(1 << 0)
/* If the I2S block has a Stereo Overlay Channel */
#define QUIRK_SEC_DAI		(1 << 1)
/*
 * If the I2S block has no internal prescalar or MUX (I2SMOD[10] bit)
 * The Machine driver must provide suitably set clock to the I2S block.
 */
#define QUIRK_NO_MUXPSR		(1 << 2)
#define QUIRK_NEED_RSTCLR	(1 << 3)
#define QUIRK_SUPPORTS_TDM	(1 << 4)
#define QUIRK_SUPPORTS_IDMA	(1 << 5)
	/* Quirks of the I2S controller */
	u32 quirks;
	dma_addr_t idma_addr;
};

/**
 * struct s3c_audio_pdata - common platform data for audio device drivers
 * @cfg_gpio: Callback function to setup mux'ed pins in I2S/PCM/AC97 mode
 */
struct s3c_audio_pdata {
	int (*cfg_gpio)(struct platform_device *);
	void *dma_playback;
	void *dma_capture;
	void *dma_play_sec;
	void *dma_capture_mic;
	union {
		struct samsung_i2s i2s;
	} type;
};
