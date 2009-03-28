/*
 * Driver for the Atmel AC97C controller
 *
 * Copyright (C) 2005-2009 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */
#ifndef __INCLUDE_SOUND_ATMEL_AC97C_H
#define __INCLUDE_SOUND_ATMEL_AC97C_H

#include <linux/dw_dmac.h>

#define AC97C_CAPTURE	0x01
#define AC97C_PLAYBACK	0x02
#define AC97C_BOTH	(AC97C_CAPTURE | AC97C_PLAYBACK)

/**
 * struct atmel_ac97c_pdata - board specific AC97C configuration
 * @rx_dws: DMA slave interface to use for sound capture.
 * @tx_dws: DMA slave interface to use for sound playback.
 * @reset_pin: GPIO pin wired to the reset input on the external AC97 codec,
 *             optional to use, set to -ENODEV if not in use. AC97 layer will
 *             try to do a software reset of the external codec anyway.
 * @flags: Flags for which directions should be enabled.
 *
 * If the user do not want to use a DMA channel for playback or capture, i.e.
 * only one feature is required on the board. The slave for playback or capture
 * can be set to NULL. The AC97C driver will take use of this when setting up
 * the sound streams.
 */
struct ac97c_platform_data {
	struct dw_dma_slave	rx_dws;
	struct dw_dma_slave	tx_dws;
	unsigned int 		flags;
	int			reset_pin;
};

#endif /* __INCLUDE_SOUND_ATMEL_AC97C_H */
