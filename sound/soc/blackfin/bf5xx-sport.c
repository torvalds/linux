/*
 * File:         bf5xx_sport.c
 * Based on:
 * Author:       Roy Huang <roy.huang@analog.com>
 *
 * Created:      Tue Sep 21 10:52:42 CEST 2004
 * Description:
 *               Blackfin SPORT Driver
 *
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/bug.h>
#include <asm/portmux.h>
#include <asm/dma.h>
#include <asm/blackfin.h>
#include <asm/cacheflush.h>

#include "bf5xx-sport.h"
/* delay between frame sync pulse and first data bit in multichannel mode */
#define FRAME_DELAY (1<<12)

struct sport_device *sport_handle;
EXPORT_SYMBOL(sport_handle);
/* note: multichannel is in units of 8 channels,
 * tdm_count is # channels NOT / 8 ! */
int sport_set_multichannel(struct sport_device *sport,
		int tdm_count, u32 mask, int packed)
{
	pr_debug("%s tdm_count=%d mask:0x%08x packed=%d\n", __func__,
			tdm_count, mask, packed);

	if ((sport->regs->tcr1 & TSPEN) || (sport->regs->rcr1 & RSPEN))
		return -EBUSY;

	if (tdm_count & 0x7)
		return -EINVAL;

	if (tdm_count > 32)
		return -EINVAL; /* Only support less than 32 channels now */

	if (tdm_count) {
		sport->regs->mcmc1 = ((tdm_count>>3)-1) << 12;
		sport->regs->mcmc2 = FRAME_DELAY | MCMEN | \
				(packed ? (MCDTXPE|MCDRXPE) : 0);

		sport->regs->mtcs0 = mask;
		sport->regs->mrcs0 = mask;
		sport->regs->mtcs1 = 0;
		sport->regs->mrcs1 = 0;
		sport->regs->mtcs2 = 0;
		sport->regs->mrcs2 = 0;
		sport->regs->mtcs3 = 0;
		sport->regs->mrcs3 = 0;
	} else {
		sport->regs->mcmc1 = 0;
		sport->regs->mcmc2 = 0;

		sport->regs->mtcs0 = 0;
		sport->regs->mrcs0 = 0;
	}

	sport->regs->mtcs1 = 0; sport->regs->mtcs2 = 0; sport->regs->mtcs3 = 0;
	sport->regs->mrcs1 = 0; sport->regs->mrcs2 = 0; sport->regs->mrcs3 = 0;

	SSYNC();

	return 0;
}
EXPORT_SYMBOL(sport_set_multichannel);

int sport_config_rx(struct sport_device *sport, unsigned int rcr1,
		unsigned int rcr2, unsigned int clkdiv, unsigned int fsdiv)
{
	if ((sport->regs->tcr1 & TSPEN) || (sport->regs->rcr1 & RSPEN))
		return -EBUSY;

	sport->regs->rcr1 = rcr1;
	sport->regs->rcr2 = rcr2;
	sport->regs->rclkdiv = clkdiv;
	sport->regs->rfsdiv = fsdiv;

	SSYNC();

	return 0;
}
EXPORT_SYMBOL(sport_config_rx);

int sport_config_tx(struct sport_device *sport, unsigned int tcr1,
		unsigned int tcr2, unsigned int clkdiv, unsigned int fsdiv)
{
	if ((sport->regs->tcr1 & TSPEN) || (sport->regs->rcr1 & RSPEN))
		return -EBUSY;

	sport->regs->tcr1 = tcr1;
	sport->regs->tcr2 = tcr2;
	sport->regs->tclkdiv = clkdiv;
	sport->regs->tfsdiv = fsdiv;

	SSYNC();

	return 0;
}
EXPORT_SYMBOL(sport_config_tx);

static void setup_desc(struct dmasg *desc, void *buf, int fragcount,
		size_t fragsize, unsigned int cfg,
		unsigned int x_count, unsigned int ycount, size_t wdsize)
{

	int i;

	for (i = 0; i < fragcount; ++i) {
		desc[i].next_desc_addr  = (unsigned long)&(desc[i + 1]);
		desc[i].start_addr = (unsigned long)buf + i*fragsize;
		desc[i].cfg = cfg;
		desc[i].x_count = x_count;
		desc[i].x_modify = wdsize;
		desc[i].y_count = ycount;
		desc[i].y_modify = wdsize;
	}

	/* make circular */
	desc[fragcount-1].next_desc_addr = (unsigned long)desc;

	pr_debug("setup desc: desc0=%p, next0=%lx, desc1=%p,"
		"next1=%lx\nx_count=%x,y_count=%x,addr=0x%lx,cfs=0x%x\n",
		&(desc[0]), desc[0].next_desc_addr,
		&(desc[1]), desc[1].next_desc_addr,
		desc[0].x_count, desc[0].y_count,
		desc[0].start_addr, desc[0].cfg);
}

static int sport_start(struct sport_device *sport)
{
	enable_dma(sport->dma_rx_chan);
	enable_dma(sport->dma_tx_chan);
	sport->regs->rcr1 |= RSPEN;
	sport->regs->tcr1 |= TSPEN;
	SSYNC();

	return 0;
}

static int sport_stop(struct sport_device *sport)
{
	sport->regs->tcr1 &= ~TSPEN;
	sport->regs->rcr1 &= ~RSPEN;
	SSYNC();

	disable_dma(sport->dma_rx_chan);
	disable_dma(sport->dma_tx_chan);
	return 0;
}

static inline int sport_hook_rx_dummy(struct sport_device *sport)
{
	struct dmasg *desc, temp_desc;
	unsigned long flags;

	BUG_ON(sport->dummy_rx_desc == NULL);
	BUG_ON(sport->curr_rx_desc == sport->dummy_rx_desc);

	/* Maybe the dummy buffer descriptor ring is damaged */
	sport->dummy_rx_desc->next_desc_addr = \
			(unsigned long)(sport->dummy_rx_desc+1);

	local_irq_save(flags);
	desc = (struct dmasg *)get_dma_next_desc_ptr(sport->dma_rx_chan);
	/* Copy the descriptor which will be damaged to backup */
	temp_desc = *desc;
	desc->x_count = 0xa;
	desc->y_count = 0;
	desc->next_desc_addr = (unsigned long)(sport->dummy_rx_desc);
	local_irq_restore(flags);
	/* Waiting for dummy buffer descriptor is already hooked*/
	while ((get_dma_curr_desc_ptr(sport->dma_rx_chan) -
			sizeof(struct dmasg)) !=
			(unsigned long)sport->dummy_rx_desc)
		;
	sport->curr_rx_desc = sport->dummy_rx_desc;
	/* Restore the damaged descriptor */
	*desc = temp_desc;

	return 0;
}

static inline int sport_rx_dma_start(struct sport_device *sport, int dummy)
{
	if (dummy) {
		sport->dummy_rx_desc->next_desc_addr = \
				(unsigned long) sport->dummy_rx_desc;
		sport->curr_rx_desc = sport->dummy_rx_desc;
	} else
		sport->curr_rx_desc = sport->dma_rx_desc;

	set_dma_next_desc_addr(sport->dma_rx_chan, \
			(unsigned long)(sport->curr_rx_desc));
	set_dma_x_count(sport->dma_rx_chan, 0);
	set_dma_x_modify(sport->dma_rx_chan, 0);
	set_dma_config(sport->dma_rx_chan, (DMAFLOW_LARGE | NDSIZE_9 | \
				WDSIZE_32 | WNR));
	set_dma_curr_addr(sport->dma_rx_chan, sport->curr_rx_desc->start_addr);
	SSYNC();

	return 0;
}

static inline int sport_tx_dma_start(struct sport_device *sport, int dummy)
{
	if (dummy) {
		sport->dummy_tx_desc->next_desc_addr = \
				(unsigned long) sport->dummy_tx_desc;
		sport->curr_tx_desc = sport->dummy_tx_desc;
	} else
		sport->curr_tx_desc = sport->dma_tx_desc;

	set_dma_next_desc_addr(sport->dma_tx_chan, \
			(unsigned long)(sport->curr_tx_desc));
	set_dma_x_count(sport->dma_tx_chan, 0);
	set_dma_x_modify(sport->dma_tx_chan, 0);
	set_dma_config(sport->dma_tx_chan,
			(DMAFLOW_LARGE | NDSIZE_9 | WDSIZE_32));
	set_dma_curr_addr(sport->dma_tx_chan, sport->curr_tx_desc->start_addr);
	SSYNC();

	return 0;
}

int sport_rx_start(struct sport_device *sport)
{
	unsigned long flags;
	pr_debug("%s enter\n", __func__);
	if (sport->rx_run)
		return -EBUSY;
	if (sport->tx_run) {
		/* tx is running, rx is not running */
		BUG_ON(sport->dma_rx_desc == NULL);
		BUG_ON(sport->curr_rx_desc != sport->dummy_rx_desc);
		local_irq_save(flags);
		while ((get_dma_curr_desc_ptr(sport->dma_rx_chan) -
			sizeof(struct dmasg)) !=
			(unsigned long)sport->dummy_rx_desc)
			;
		sport->dummy_rx_desc->next_desc_addr =
				(unsigned long)(sport->dma_rx_desc);
		local_irq_restore(flags);
		sport->curr_rx_desc = sport->dma_rx_desc;
	} else {
		sport_tx_dma_start(sport, 1);
		sport_rx_dma_start(sport, 0);
		sport_start(sport);
	}

	sport->rx_run = 1;

	return 0;
}
EXPORT_SYMBOL(sport_rx_start);

int sport_rx_stop(struct sport_device *sport)
{
	pr_debug("%s enter\n", __func__);

	if (!sport->rx_run)
		return 0;
	if (sport->tx_run) {
		/* TX dma is still running, hook the dummy buffer */
		sport_hook_rx_dummy(sport);
	} else {
		/* Both rx and tx dma will be stopped */
		sport_stop(sport);
		sport->curr_rx_desc = NULL;
		sport->curr_tx_desc = NULL;
	}

	sport->rx_run = 0;

	return 0;
}
EXPORT_SYMBOL(sport_rx_stop);

static inline int sport_hook_tx_dummy(struct sport_device *sport)
{
	struct dmasg *desc, temp_desc;
	unsigned long flags;

	BUG_ON(sport->dummy_tx_desc == NULL);
	BUG_ON(sport->curr_tx_desc == sport->dummy_tx_desc);

	sport->dummy_tx_desc->next_desc_addr = \
			(unsigned long)(sport->dummy_tx_desc+1);

	/* Shorten the time on last normal descriptor */
	local_irq_save(flags);
	desc = (struct dmasg *)get_dma_next_desc_ptr(sport->dma_tx_chan);
	/* Store the descriptor which will be damaged */
	temp_desc = *desc;
	desc->x_count = 0xa;
	desc->y_count = 0;
	desc->next_desc_addr = (unsigned long)(sport->dummy_tx_desc);
	local_irq_restore(flags);
	/* Waiting for dummy buffer descriptor is already hooked*/
	while ((get_dma_curr_desc_ptr(sport->dma_tx_chan) - \
			sizeof(struct dmasg)) != \
			(unsigned long)sport->dummy_tx_desc)
		;
	sport->curr_tx_desc = sport->dummy_tx_desc;
	/* Restore the damaged descriptor */
	*desc = temp_desc;

	return 0;
}

int sport_tx_start(struct sport_device *sport)
{
	unsigned flags;
	pr_debug("%s: tx_run:%d, rx_run:%d\n", __func__,
			sport->tx_run, sport->rx_run);
	if (sport->tx_run)
		return -EBUSY;
	if (sport->rx_run) {
		BUG_ON(sport->dma_tx_desc == NULL);
		BUG_ON(sport->curr_tx_desc != sport->dummy_tx_desc);
		/* Hook the normal buffer descriptor */
		local_irq_save(flags);
		while ((get_dma_curr_desc_ptr(sport->dma_tx_chan) -
			sizeof(struct dmasg)) !=
			(unsigned long)sport->dummy_tx_desc)
			;
		sport->dummy_tx_desc->next_desc_addr =
				(unsigned long)(sport->dma_tx_desc);
		local_irq_restore(flags);
		sport->curr_tx_desc = sport->dma_tx_desc;
	} else {

		sport_tx_dma_start(sport, 0);
		/* Let rx dma run the dummy buffer */
		sport_rx_dma_start(sport, 1);
		sport_start(sport);
	}
	sport->tx_run = 1;
	return 0;
}
EXPORT_SYMBOL(sport_tx_start);

int sport_tx_stop(struct sport_device *sport)
{
	if (!sport->tx_run)
		return 0;
	if (sport->rx_run) {
		/* RX is still running, hook the dummy buffer */
		sport_hook_tx_dummy(sport);
	} else {
		/* Both rx and tx dma stopped */
		sport_stop(sport);
		sport->curr_rx_desc = NULL;
		sport->curr_tx_desc = NULL;
	}

	sport->tx_run = 0;

	return 0;
}
EXPORT_SYMBOL(sport_tx_stop);

static inline int compute_wdsize(size_t wdsize)
{
	switch (wdsize) {
	case 1:
		return WDSIZE_8;
	case 2:
		return WDSIZE_16;
	case 4:
	default:
		return WDSIZE_32;
	}
}

int sport_config_rx_dma(struct sport_device *sport, void *buf,
		int fragcount, size_t fragsize)
{
	unsigned int x_count;
	unsigned int y_count;
	unsigned int cfg;
	dma_addr_t addr;

	pr_debug("%s buf:%p, frag:%d, fragsize:0x%lx\n", __func__, \
			buf, fragcount, fragsize);

	x_count = fragsize / sport->wdsize;
	y_count = 0;

	/* for fragments larger than 64k words we use 2d dma,
	 * denote fragecount as two numbers' mutliply and both of them
	 * are less than 64k.*/
	if (x_count >= 0x10000) {
		int i, count = x_count;

		for (i = 16; i > 0; i--) {
			x_count = 1 << i;
			if ((count & (x_count - 1)) == 0) {
				y_count = count >> i;
				if (y_count < 0x10000)
					break;
			}
		}
		if (i == 0)
			return -EINVAL;
	}
	pr_debug("%s(x_count:0x%x, y_count:0x%x)\n", __func__,
			x_count, y_count);

	if (sport->dma_rx_desc)
		dma_free_coherent(NULL, sport->rx_desc_bytes,
					sport->dma_rx_desc, 0);

	/* Allocate a new descritor ring as current one. */
	sport->dma_rx_desc = dma_alloc_coherent(NULL, \
			fragcount * sizeof(struct dmasg), &addr, 0);
	sport->rx_desc_bytes = fragcount * sizeof(struct dmasg);

	if (!sport->dma_rx_desc) {
		pr_err("Failed to allocate memory for rx desc\n");
		return -ENOMEM;
	}

	sport->rx_buf = buf;
	sport->rx_fragsize = fragsize;
	sport->rx_frags = fragcount;

	cfg     = 0x7000 | DI_EN | compute_wdsize(sport->wdsize) | WNR | \
		  (DESC_ELEMENT_COUNT << 8); /* large descriptor mode */

	if (y_count != 0)
		cfg |= DMA2D;

	setup_desc(sport->dma_rx_desc, buf, fragcount, fragsize,
			cfg|DMAEN, x_count, y_count, sport->wdsize);

	return 0;
}
EXPORT_SYMBOL(sport_config_rx_dma);

int sport_config_tx_dma(struct sport_device *sport, void *buf, \
		int fragcount, size_t fragsize)
{
	unsigned int x_count;
	unsigned int y_count;
	unsigned int cfg;
	dma_addr_t addr;

	pr_debug("%s buf:%p, fragcount:%d, fragsize:0x%lx\n",
			__func__, buf, fragcount, fragsize);

	x_count = fragsize/sport->wdsize;
	y_count = 0;

	/* for fragments larger than 64k words we use 2d dma,
	 * denote fragecount as two numbers' mutliply and both of them
	 * are less than 64k.*/
	if (x_count >= 0x10000) {
		int i, count = x_count;

		for (i = 16; i > 0; i--) {
			x_count = 1 << i;
			if ((count & (x_count - 1)) == 0) {
				y_count = count >> i;
				if (y_count < 0x10000)
					break;
			}
		}
		if (i == 0)
			return -EINVAL;
	}
	pr_debug("%s x_count:0x%x, y_count:0x%x\n", __func__,
			x_count, y_count);


	if (sport->dma_tx_desc) {
		dma_free_coherent(NULL, sport->tx_desc_bytes, \
				sport->dma_tx_desc, 0);
	}

	sport->dma_tx_desc = dma_alloc_coherent(NULL, \
			fragcount * sizeof(struct dmasg), &addr, 0);
	sport->tx_desc_bytes = fragcount * sizeof(struct dmasg);
	if (!sport->dma_tx_desc) {
		pr_err("Failed to allocate memory for tx desc\n");
		return -ENOMEM;
	}

	sport->tx_buf = buf;
	sport->tx_fragsize = fragsize;
	sport->tx_frags = fragcount;
	cfg     = 0x7000 | DI_EN | compute_wdsize(sport->wdsize) | \
		  (DESC_ELEMENT_COUNT << 8); /* large descriptor mode */

	if (y_count != 0)
		cfg |= DMA2D;

	setup_desc(sport->dma_tx_desc, buf, fragcount, fragsize,
			cfg|DMAEN, x_count, y_count, sport->wdsize);

	return 0;
}
EXPORT_SYMBOL(sport_config_tx_dma);

/* setup dummy dma descriptor ring, which don't generate interrupts,
 * the x_modify is set to 0 */
static int sport_config_rx_dummy(struct sport_device *sport)
{
	struct dmasg *desc;
	unsigned config;

	pr_debug("%s entered\n", __func__);
#if L1_DATA_A_LENGTH != 0
	desc = (struct dmasg *) l1_data_sram_alloc(2 * sizeof(*desc));
#else
	{
		dma_addr_t addr;
		desc = dma_alloc_coherent(NULL, 2 * sizeof(*desc), &addr, 0);
	}
#endif
	if (desc == NULL) {
		pr_err("Failed to allocate memory for dummy rx desc\n");
		return -ENOMEM;
	}
	memset(desc, 0, 2 * sizeof(*desc));
	sport->dummy_rx_desc = desc;
	desc->start_addr = (unsigned long)sport->dummy_buf;
	config = DMAFLOW_LARGE | NDSIZE_9 | compute_wdsize(sport->wdsize)
		 | WNR | DMAEN;
	desc->cfg = config;
	desc->x_count = sport->dummy_count/sport->wdsize;
	desc->x_modify = sport->wdsize;
	desc->y_count = 0;
	desc->y_modify = 0;
	memcpy(desc+1, desc, sizeof(*desc));
	desc->next_desc_addr = (unsigned long)(desc+1);
	desc[1].next_desc_addr = (unsigned long)desc;
	return 0;
}

static int sport_config_tx_dummy(struct sport_device *sport)
{
	struct dmasg *desc;
	unsigned int config;

	pr_debug("%s entered\n", __func__);

#if L1_DATA_A_LENGTH != 0
	desc = (struct dmasg *) l1_data_sram_alloc(2 * sizeof(*desc));
#else
	{
		dma_addr_t addr;
		desc = dma_alloc_coherent(NULL, 2 * sizeof(*desc), &addr, 0);
	}
#endif
	if (!desc) {
		pr_err("Failed to allocate memory for dummy tx desc\n");
		return -ENOMEM;
	}
	memset(desc, 0, 2 * sizeof(*desc));
	sport->dummy_tx_desc = desc;
	desc->start_addr = (unsigned long)sport->dummy_buf + \
		sport->dummy_count;
	config = DMAFLOW_LARGE | NDSIZE_9 |
		 compute_wdsize(sport->wdsize) | DMAEN;
	desc->cfg = config;
	desc->x_count = sport->dummy_count/sport->wdsize;
	desc->x_modify = sport->wdsize;
	desc->y_count = 0;
	desc->y_modify = 0;
	memcpy(desc+1, desc, sizeof(*desc));
	desc->next_desc_addr = (unsigned long)(desc+1);
	desc[1].next_desc_addr = (unsigned long)desc;
	return 0;
}

unsigned long sport_curr_offset_rx(struct sport_device *sport)
{
	unsigned long curr = get_dma_curr_addr(sport->dma_rx_chan);

	return (unsigned char *)curr - sport->rx_buf;
}
EXPORT_SYMBOL(sport_curr_offset_rx);

unsigned long sport_curr_offset_tx(struct sport_device *sport)
{
	unsigned long curr = get_dma_curr_addr(sport->dma_tx_chan);

	return (unsigned char *)curr - sport->tx_buf;
}
EXPORT_SYMBOL(sport_curr_offset_tx);

void sport_incfrag(struct sport_device *sport, int *frag, int tx)
{
	++(*frag);
	if (tx == 1 && *frag == sport->tx_frags)
		*frag = 0;

	if (tx == 0 && *frag == sport->rx_frags)
		*frag = 0;
}
EXPORT_SYMBOL(sport_incfrag);

void sport_decfrag(struct sport_device *sport, int *frag, int tx)
{
	--(*frag);
	if (tx == 1 && *frag == 0)
		*frag = sport->tx_frags;

	if (tx == 0 && *frag == 0)
		*frag = sport->rx_frags;
}
EXPORT_SYMBOL(sport_decfrag);

static int sport_check_status(struct sport_device *sport,
		unsigned int *sport_stat,
		unsigned int *rx_stat,
		unsigned int *tx_stat)
{
	int status = 0;

	if (sport_stat) {
		SSYNC();
		status = sport->regs->stat;
		if (status & (TOVF|TUVF|ROVF|RUVF))
			sport->regs->stat = (status & (TOVF|TUVF|ROVF|RUVF));
		SSYNC();
		*sport_stat = status;
	}

	if (rx_stat) {
		SSYNC();
		status = get_dma_curr_irqstat(sport->dma_rx_chan);
		if (status & (DMA_DONE|DMA_ERR))
			clear_dma_irqstat(sport->dma_rx_chan);
		SSYNC();
		*rx_stat = status;
	}

	if (tx_stat) {
		SSYNC();
		status = get_dma_curr_irqstat(sport->dma_tx_chan);
		if (status & (DMA_DONE|DMA_ERR))
			clear_dma_irqstat(sport->dma_tx_chan);
		SSYNC();
		*tx_stat = status;
	}

	return 0;
}

int  sport_dump_stat(struct sport_device *sport, char *buf, size_t len)
{
	int ret;

	ret = snprintf(buf, len,
			"sts: 0x%04x\n"
			"rx dma %d sts: 0x%04x tx dma %d sts: 0x%04x\n",
			sport->regs->stat,
			sport->dma_rx_chan,
			get_dma_curr_irqstat(sport->dma_rx_chan),
			sport->dma_tx_chan,
			get_dma_curr_irqstat(sport->dma_tx_chan));
	buf += ret;
	len -= ret;

	ret += snprintf(buf, len,
			"curr_rx_desc:0x%p, curr_tx_desc:0x%p\n"
			"dma_rx_desc:0x%p, dma_tx_desc:0x%p\n"
			"dummy_rx_desc:0x%p, dummy_tx_desc:0x%p\n",
			sport->curr_rx_desc, sport->curr_tx_desc,
			sport->dma_rx_desc, sport->dma_tx_desc,
			sport->dummy_rx_desc, sport->dummy_tx_desc);

	return ret;
}

static irqreturn_t rx_handler(int irq, void *dev_id)
{
	unsigned int rx_stat;
	struct sport_device *sport = dev_id;

	pr_debug("%s enter\n", __func__);
	sport_check_status(sport, NULL, &rx_stat, NULL);
	if (!(rx_stat & DMA_DONE))
		pr_err("rx dma is already stopped\n");

	if (sport->rx_callback) {
		sport->rx_callback(sport->rx_data);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t tx_handler(int irq, void *dev_id)
{
	unsigned int tx_stat;
	struct sport_device *sport = dev_id;
	pr_debug("%s enter\n", __func__);
	sport_check_status(sport, NULL, NULL, &tx_stat);
	if (!(tx_stat & DMA_DONE)) {
		pr_err("tx dma is already stopped\n");
		return IRQ_HANDLED;
	}
	if (sport->tx_callback) {
		sport->tx_callback(sport->tx_data);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static irqreturn_t err_handler(int irq, void *dev_id)
{
	unsigned int status = 0;
	struct sport_device *sport = dev_id;

	pr_debug("%s\n", __func__);
	if (sport_check_status(sport, &status, NULL, NULL)) {
		pr_err("error checking status ??");
		return IRQ_NONE;
	}

	if (status & (TOVF|TUVF|ROVF|RUVF)) {
		pr_info("sport status error:%s%s%s%s\n",
				status & TOVF ? " TOVF" : "",
				status & TUVF ? " TUVF" : "",
				status & ROVF ? " ROVF" : "",
				status & RUVF ? " RUVF" : "");
		if (status & TOVF || status & TUVF) {
			disable_dma(sport->dma_tx_chan);
			if (sport->tx_run)
				sport_tx_dma_start(sport, 0);
			else
				sport_tx_dma_start(sport, 1);
			enable_dma(sport->dma_tx_chan);
		} else {
			disable_dma(sport->dma_rx_chan);
			if (sport->rx_run)
				sport_rx_dma_start(sport, 0);
			else
				sport_rx_dma_start(sport, 1);
			enable_dma(sport->dma_rx_chan);
		}
	}
	status = sport->regs->stat;
	if (status & (TOVF|TUVF|ROVF|RUVF))
		sport->regs->stat = (status & (TOVF|TUVF|ROVF|RUVF));
	SSYNC();

	if (sport->err_callback)
		sport->err_callback(sport->err_data);

	return IRQ_HANDLED;
}

int sport_set_rx_callback(struct sport_device *sport,
		       void (*rx_callback)(void *), void *rx_data)
{
	BUG_ON(rx_callback == NULL);
	sport->rx_callback = rx_callback;
	sport->rx_data = rx_data;

	return 0;
}
EXPORT_SYMBOL(sport_set_rx_callback);

int sport_set_tx_callback(struct sport_device *sport,
		void (*tx_callback)(void *), void *tx_data)
{
	BUG_ON(tx_callback == NULL);
	sport->tx_callback = tx_callback;
	sport->tx_data = tx_data;

	return 0;
}
EXPORT_SYMBOL(sport_set_tx_callback);

int sport_set_err_callback(struct sport_device *sport,
		void (*err_callback)(void *), void *err_data)
{
	BUG_ON(err_callback == NULL);
	sport->err_callback = err_callback;
	sport->err_data = err_data;

	return 0;
}
EXPORT_SYMBOL(sport_set_err_callback);

struct sport_device *sport_init(struct sport_param *param, unsigned wdsize,
		unsigned dummy_count, void *private_data)
{
	int ret;
	struct sport_device *sport;
	pr_debug("%s enter\n", __func__);
	BUG_ON(param == NULL);
	BUG_ON(wdsize == 0 || dummy_count == 0);
	sport = kmalloc(sizeof(struct sport_device), GFP_KERNEL);
	if (!sport) {
		pr_err("Failed to allocate for sport device\n");
		return NULL;
	}

	memset(sport, 0, sizeof(struct sport_device));
	sport->dma_rx_chan = param->dma_rx_chan;
	sport->dma_tx_chan = param->dma_tx_chan;
	sport->err_irq = param->err_irq;
	sport->regs = param->regs;
	sport->private_data = private_data;

	if (request_dma(sport->dma_rx_chan, "SPORT RX Data") == -EBUSY) {
		pr_err("Failed to request RX dma %d\n", \
				sport->dma_rx_chan);
		goto __init_err1;
	}
	if (set_dma_callback(sport->dma_rx_chan, rx_handler, sport) != 0) {
		pr_err("Failed to request RX irq %d\n", \
				sport->dma_rx_chan);
		goto __init_err2;
	}

	if (request_dma(sport->dma_tx_chan, "SPORT TX Data") == -EBUSY) {
		pr_err("Failed to request TX dma %d\n", \
				sport->dma_tx_chan);
		goto __init_err2;
	}

	if (set_dma_callback(sport->dma_tx_chan, tx_handler, sport) != 0) {
		pr_err("Failed to request TX irq %d\n", \
				sport->dma_tx_chan);
		goto __init_err3;
	}

	if (request_irq(sport->err_irq, err_handler, IRQF_SHARED, "SPORT err",
			sport) < 0) {
		pr_err("Failed to request err irq:%d\n", \
				sport->err_irq);
		goto __init_err3;
	}

	pr_err("dma rx:%d tx:%d, err irq:%d, regs:%p\n",
			sport->dma_rx_chan, sport->dma_tx_chan,
			sport->err_irq, sport->regs);

	sport->wdsize = wdsize;
	sport->dummy_count = dummy_count;

#if L1_DATA_A_LENGTH != 0
	sport->dummy_buf = l1_data_sram_alloc(dummy_count * 2);
#else
	sport->dummy_buf = kmalloc(dummy_count * 2, GFP_KERNEL);
#endif
	if (sport->dummy_buf == NULL) {
		pr_err("Failed to allocate dummy buffer\n");
		goto __error;
	}

	memset(sport->dummy_buf, 0, dummy_count * 2);
	ret = sport_config_rx_dummy(sport);
	if (ret) {
		pr_err("Failed to config rx dummy ring\n");
		goto __error;
	}
	ret = sport_config_tx_dummy(sport);
	if (ret) {
		pr_err("Failed to config tx dummy ring\n");
		goto __error;
	}

	return sport;
__error:
	free_irq(sport->err_irq, sport);
__init_err3:
	free_dma(sport->dma_tx_chan);
__init_err2:
	free_dma(sport->dma_rx_chan);
__init_err1:
	kfree(sport);
	return NULL;
}
EXPORT_SYMBOL(sport_init);

void sport_done(struct sport_device *sport)
{
	if (sport == NULL)
		return;

	sport_stop(sport);
	if (sport->dma_rx_desc)
		dma_free_coherent(NULL, sport->rx_desc_bytes,
			sport->dma_rx_desc, 0);
	if (sport->dma_tx_desc)
		dma_free_coherent(NULL, sport->tx_desc_bytes,
			sport->dma_tx_desc, 0);

#if L1_DATA_A_LENGTH != 0
	l1_data_sram_free(sport->dummy_rx_desc);
	l1_data_sram_free(sport->dummy_tx_desc);
	l1_data_sram_free(sport->dummy_buf);
#else
	dma_free_coherent(NULL, 2*sizeof(struct dmasg),
		sport->dummy_rx_desc, 0);
	dma_free_coherent(NULL, 2*sizeof(struct dmasg),
		sport->dummy_tx_desc, 0);
	kfree(sport->dummy_buf);
#endif
	free_dma(sport->dma_rx_chan);
	free_dma(sport->dma_tx_chan);
	free_irq(sport->err_irq, sport);

	kfree(sport);
		sport = NULL;
}
EXPORT_SYMBOL(sport_done);
/*
* It is only used to send several bytes when dma is not enabled
 * sport controller is configured but not enabled.
 * Multichannel cannot works with pio mode */
/* Used by ac97 to write and read codec register */
int sport_send_and_recv(struct sport_device *sport, u8 *out_data, \
		u8 *in_data, int len)
{
	unsigned short dma_config;
	unsigned short status;
	unsigned long flags;
	unsigned long wait = 0;

	pr_debug("%s enter, out_data:%p, in_data:%p len:%d\n", \
			__func__, out_data, in_data, len);
	pr_debug("tcr1:0x%04x, tcr2:0x%04x, tclkdiv:0x%04x, tfsdiv:0x%04x\n"
			"mcmc1:0x%04x, mcmc2:0x%04x\n",
			sport->regs->tcr1, sport->regs->tcr2,
			sport->regs->tclkdiv, sport->regs->tfsdiv,
			sport->regs->mcmc1, sport->regs->mcmc2);
	flush_dcache_range((unsigned)out_data, (unsigned)(out_data + len));

	/* Enable tx dma */
	dma_config = (RESTART | WDSIZE_16 | DI_EN);
	set_dma_start_addr(sport->dma_tx_chan, (unsigned long)out_data);
	set_dma_x_count(sport->dma_tx_chan, len/2);
	set_dma_x_modify(sport->dma_tx_chan, 2);
	set_dma_config(sport->dma_tx_chan, dma_config);
	enable_dma(sport->dma_tx_chan);

	if (in_data != NULL) {
		invalidate_dcache_range((unsigned)in_data, \
				(unsigned)(in_data + len));
		/* Enable rx dma */
		dma_config = (RESTART | WDSIZE_16 | WNR | DI_EN);
		set_dma_start_addr(sport->dma_rx_chan, (unsigned long)in_data);
		set_dma_x_count(sport->dma_rx_chan, len/2);
		set_dma_x_modify(sport->dma_rx_chan, 2);
		set_dma_config(sport->dma_rx_chan, dma_config);
		enable_dma(sport->dma_rx_chan);
	}

	local_irq_save(flags);
	sport->regs->tcr1 |= TSPEN;
	sport->regs->rcr1 |= RSPEN;
	SSYNC();

	status = get_dma_curr_irqstat(sport->dma_tx_chan);
	while (status & DMA_RUN) {
		udelay(1);
		status = get_dma_curr_irqstat(sport->dma_tx_chan);
		pr_debug("DMA status:0x%04x\n", status);
		if (wait++ > 100)
			goto __over;
	}
	status = sport->regs->stat;
	wait = 0;

	while (!(status & TXHRE)) {
		pr_debug("sport status:0x%04x\n", status);
		udelay(1);
		status = *(unsigned short *)&sport->regs->stat;
		if (wait++ > 1000)
			goto __over;
	}
	/* Wait for the last byte sent out */
	udelay(20);
	pr_debug("sport status:0x%04x\n", status);

__over:
	sport->regs->tcr1 &= ~TSPEN;
	sport->regs->rcr1 &= ~RSPEN;
	SSYNC();
	disable_dma(sport->dma_tx_chan);
	/* Clear the status */
	clear_dma_irqstat(sport->dma_tx_chan);
	if (in_data != NULL) {
		disable_dma(sport->dma_rx_chan);
		clear_dma_irqstat(sport->dma_rx_chan);
	}
	SSYNC();
	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL(sport_send_and_recv);

MODULE_AUTHOR("Roy Huang");
MODULE_DESCRIPTION("SPORT driver for ADI Blackfin");
MODULE_LICENSE("GPL");

