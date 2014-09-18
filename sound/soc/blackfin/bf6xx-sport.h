/*
 * bf6xx_sport - Analog Devices BF6XX SPORT driver
 *
 * Copyright (c) 2012 Analog Devices Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _BF6XX_SPORT_H_
#define _BF6XX_SPORT_H_

#include <linux/platform_device.h>
#include <asm/bfin_sport3.h>

struct sport_device {
	struct platform_device *pdev;
	const unsigned short *pin_req;
	struct sport_register *tx_regs;
	struct sport_register *rx_regs;
	int tx_dma_chan;
	int rx_dma_chan;
	int tx_err_irq;
	int rx_err_irq;

	void (*tx_callback)(void *data);
	void *tx_data;
	void (*rx_callback)(void *data);
	void *rx_data;

	struct dmasg *tx_desc;
	struct dmasg *rx_desc;
	unsigned int tx_desc_size;
	unsigned int rx_desc_size;
	unsigned char *tx_buf;
	unsigned char *rx_buf;
	unsigned int tx_fragsize;
	unsigned int rx_fragsize;
	unsigned int tx_frags;
	unsigned int rx_frags;
	unsigned int wdsize;
};

struct sport_params {
	u32 spctl;
	u32 div;
};

struct sport_device *sport_create(struct platform_device *pdev);
void sport_delete(struct sport_device *sport);
int sport_set_tx_params(struct sport_device *sport,
		struct sport_params *params);
int sport_set_rx_params(struct sport_device *sport,
		struct sport_params *params);
void sport_tx_start(struct sport_device *sport);
void sport_rx_start(struct sport_device *sport);
void sport_tx_stop(struct sport_device *sport);
void sport_rx_stop(struct sport_device *sport);
void sport_set_tx_callback(struct sport_device *sport,
	void (*tx_callback)(void *), void *tx_data);
void sport_set_rx_callback(struct sport_device *sport,
	void (*rx_callback)(void *), void *rx_data);
int sport_config_tx_dma(struct sport_device *sport, void *buf,
	int fragcount, size_t fragsize);
int sport_config_rx_dma(struct sport_device *sport, void *buf,
	int fragcount, size_t fragsize);
unsigned long sport_curr_offset_tx(struct sport_device *sport);
unsigned long sport_curr_offset_rx(struct sport_device *sport);



#endif
