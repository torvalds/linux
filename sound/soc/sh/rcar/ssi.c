/*
 * Renesas R-Car SSIU/SSI support
 *
 * Copyright (C) 2013 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * Based on fsi.c
 * Kuninori Morimoto <morimoto.kuninori@renesas.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/delay.h>
#include "rsnd.h"
#define RSND_SSI_NAME_SIZE 16

/*
 * SSICR
 */
#define	FORCE		(1 << 31)	/* Fixed */
#define	DMEN		(1 << 28)	/* DMA Enable */
#define	UIEN		(1 << 27)	/* Underflow Interrupt Enable */
#define	OIEN		(1 << 26)	/* Overflow Interrupt Enable */
#define	IIEN		(1 << 25)	/* Idle Mode Interrupt Enable */
#define	DIEN		(1 << 24)	/* Data Interrupt Enable */
#define	CHNL_4		(1 << 22)	/* Channels */
#define	CHNL_6		(2 << 22)	/* Channels */
#define	CHNL_8		(3 << 22)	/* Channels */
#define	DWL_8		(0 << 19)	/* Data Word Length */
#define	DWL_16		(1 << 19)	/* Data Word Length */
#define	DWL_18		(2 << 19)	/* Data Word Length */
#define	DWL_20		(3 << 19)	/* Data Word Length */
#define	DWL_22		(4 << 19)	/* Data Word Length */
#define	DWL_24		(5 << 19)	/* Data Word Length */
#define	DWL_32		(6 << 19)	/* Data Word Length */

#define	SWL_32		(3 << 16)	/* R/W System Word Length */
#define	SCKD		(1 << 15)	/* Serial Bit Clock Direction */
#define	SWSD		(1 << 14)	/* Serial WS Direction */
#define	SCKP		(1 << 13)	/* Serial Bit Clock Polarity */
#define	SWSP		(1 << 12)	/* Serial WS Polarity */
#define	SDTA		(1 << 10)	/* Serial Data Alignment */
#define	PDTA		(1 <<  9)	/* Parallel Data Alignment */
#define	DEL		(1 <<  8)	/* Serial Data Delay */
#define	CKDV(v)		(v <<  4)	/* Serial Clock Division Ratio */
#define	TRMD		(1 <<  1)	/* Transmit/Receive Mode Select */
#define	EN		(1 <<  0)	/* SSI Module Enable */

/*
 * SSISR
 */
#define	UIRQ		(1 << 27)	/* Underflow Error Interrupt Status */
#define	OIRQ		(1 << 26)	/* Overflow Error Interrupt Status */
#define	IIRQ		(1 << 25)	/* Idle Mode Interrupt Status */
#define	DIRQ		(1 << 24)	/* Data Interrupt Status Flag */

/*
 * SSIWSR
 */
#define CONT		(1 << 8)	/* WS Continue Function */
#define WS_MODE		(1 << 0)	/* WS Mode */

#define SSI_NAME "ssi"

struct rsnd_ssi {
	struct rsnd_ssi *parent;
	struct rsnd_mod mod;
	struct rsnd_mod *dma;

	u32 flags;
	u32 cr_own;
	u32 cr_clk;
	u32 cr_mode;
	u32 wsr;
	int chan;
	int rate;
	int err;
	int irq;
	unsigned int usrcnt;
};

/* flags */
#define RSND_SSI_CLK_PIN_SHARE		(1 << 0)
#define RSND_SSI_NO_BUSIF		(1 << 1) /* SSI+DMA without BUSIF */

#define for_each_rsnd_ssi(pos, priv, i)					\
	for (i = 0;							\
	     (i < rsnd_ssi_nr(priv)) &&					\
		((pos) = ((struct rsnd_ssi *)(priv)->ssi + i));		\
	     i++)

#define rsnd_ssi_get(priv, id) ((struct rsnd_ssi *)(priv->ssi) + id)
#define rsnd_ssi_to_dma(mod) ((ssi)->dma)
#define rsnd_ssi_nr(priv) ((priv)->ssi_nr)
#define rsnd_mod_to_ssi(_mod) container_of((_mod), struct rsnd_ssi, mod)
#define rsnd_ssi_mode_flags(p) ((p)->flags)
#define rsnd_ssi_is_parent(ssi, io) ((ssi) == rsnd_io_to_mod_ssip(io))
#define rsnd_ssi_is_multi_slave(ssi, io) ((mod) != rsnd_io_to_mod_ssi(io))

int rsnd_ssi_use_busif(struct rsnd_dai_stream *io)
{
	struct rsnd_mod *mod = rsnd_io_to_mod_ssi(io);
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	int use_busif = 0;

	if (!rsnd_ssi_is_dma_mode(mod))
		return 0;

	if (!(rsnd_ssi_mode_flags(ssi) & RSND_SSI_NO_BUSIF))
		use_busif = 1;
	if (rsnd_io_to_mod_src(io))
		use_busif = 1;

	return use_busif;
}

static void rsnd_ssi_status_clear(struct rsnd_mod *mod)
{
	rsnd_mod_write(mod, SSISR, 0);
}

static u32 rsnd_ssi_status_get(struct rsnd_mod *mod)
{
	return rsnd_mod_read(mod, SSISR);
}

static void rsnd_ssi_status_check(struct rsnd_mod *mod,
				  u32 bit)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	u32 status;
	int i;

	for (i = 0; i < 1024; i++) {
		status = rsnd_ssi_status_get(mod);
		if (status & bit)
			return;

		udelay(50);
	}

	dev_warn(dev, "status check failed\n");
}

static int rsnd_ssi_irq_enable(struct rsnd_mod *ssi_mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);

	if (rsnd_is_gen1(priv))
		return 0;

	/* enable SSI interrupt if Gen2 */
	rsnd_mod_write(ssi_mod, SSI_INT_ENABLE,
		       rsnd_ssi_is_dma_mode(ssi_mod) ?
		       0x0e000000 : 0x0f000000);

	return 0;
}

static int rsnd_ssi_irq_disable(struct rsnd_mod *ssi_mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);

	if (rsnd_is_gen1(priv))
		return 0;

	/* disable SSI interrupt if Gen2 */
	rsnd_mod_write(ssi_mod, SSI_INT_ENABLE, 0x00000000);

	return 0;
}

u32 rsnd_ssi_multi_slaves(struct rsnd_dai_stream *io)
{
	struct rsnd_mod *mod;
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	enum rsnd_mod_type types[] = {
		RSND_MOD_SSIM1,
		RSND_MOD_SSIM2,
		RSND_MOD_SSIM3,
	};
	int i, mask;

	switch (runtime->channels) {
	case 2: /* Multi channel is not needed for Stereo */
		return 0;
	case 6:
		break;
	default:
		dev_err(dev, "unsupported channel\n");
		return 0;
	}

	mask = 0;
	for (i = 0; i < ARRAY_SIZE(types); i++) {
		mod = rsnd_io_to_mod(io, types[i]);
		if (!mod)
			continue;

		mask |= 1 << rsnd_mod_id(mod);
	}

	return mask;
}

static int rsnd_ssi_master_clk_start(struct rsnd_ssi *ssi,
				     struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_io_to_priv(io);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	struct rsnd_mod *mod = rsnd_mod_get(ssi);
	struct rsnd_mod *ssi_parent_mod = rsnd_io_to_mod_ssip(io);
	int slots = rsnd_get_slot_width(io);
	int j, ret;
	int ssi_clk_mul_table[] = {
		1, 2, 4, 8, 16, 6, 12,
	};
	unsigned int main_rate;
	unsigned int rate = rsnd_src_get_ssi_rate(priv, io, runtime);

	if (!rsnd_rdai_is_clk_master(rdai))
		return 0;

	if (ssi_parent_mod && !rsnd_ssi_is_parent(mod, io))
		return 0;

	if (rsnd_ssi_is_multi_slave(mod, io))
		return 0;

	if (ssi->usrcnt > 1) {
		if (ssi->rate != rate) {
			dev_err(dev, "SSI parent/child should use same rate\n");
			return -EINVAL;
		}

		return 0;
	}

	/*
	 * Find best clock, and try to start ADG
	 */
	for (j = 0; j < ARRAY_SIZE(ssi_clk_mul_table); j++) {

		/*
		 * this driver is assuming that
		 * system word is 32bit x slots
		 * see rsnd_ssi_init()
		 */
		main_rate = rate * 32 * slots * ssi_clk_mul_table[j];

		ret = rsnd_adg_ssi_clk_try_start(mod, main_rate);
		if (0 == ret) {
			ssi->cr_clk	= FORCE | SWL_32 |
				SCKD | SWSD | CKDV(j);
			ssi->wsr = CONT;

			ssi->rate = rate;

			dev_dbg(dev, "%s[%d] outputs %u Hz\n",
				rsnd_mod_name(mod),
				rsnd_mod_id(mod), rate);

			return 0;
		}
	}

	dev_err(dev, "unsupported clock rate\n");
	return -EIO;
}

static void rsnd_ssi_master_clk_stop(struct rsnd_ssi *ssi,
				     struct rsnd_dai_stream *io)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	struct rsnd_mod *mod = rsnd_mod_get(ssi);
	struct rsnd_mod *ssi_parent_mod = rsnd_io_to_mod_ssip(io);

	if (!rsnd_rdai_is_clk_master(rdai))
		return;

	if (ssi_parent_mod && !rsnd_ssi_is_parent(mod, io))
		return;

	if (ssi->usrcnt > 1)
		return;

	ssi->cr_clk	= 0;
	ssi->rate	= 0;

	rsnd_adg_ssi_clk_stop(mod);
}

static int rsnd_ssi_config_init(struct rsnd_ssi *ssi,
				struct rsnd_dai_stream *io)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	u32 cr_own;
	u32 cr_mode;
	u32 wsr;
	int is_tdm;

	is_tdm = (rsnd_get_slot_width(io) >= 6) ? 1 : 0;

	/*
	 * always use 32bit system word.
	 * see also rsnd_ssi_master_clk_enable()
	 */
	cr_own = FORCE | SWL_32 | PDTA;

	if (rdai->bit_clk_inv)
		cr_own |= SCKP;
	if (rdai->frm_clk_inv ^ is_tdm)
		cr_own |= SWSP;
	if (rdai->data_alignment)
		cr_own |= SDTA;
	if (rdai->sys_delay)
		cr_own |= DEL;
	if (rsnd_io_is_play(io))
		cr_own |= TRMD;

	switch (runtime->sample_bits) {
	case 16:
		cr_own |= DWL_16;
		break;
	case 32:
		cr_own |= DWL_24;
		break;
	default:
		return -EINVAL;
	}

	if (rsnd_ssi_is_dma_mode(rsnd_mod_get(ssi))) {
		cr_mode = UIEN | OIEN |	/* over/under run */
			  DMEN;		/* DMA : enable DMA */
	} else {
		cr_mode = DIEN;		/* PIO : enable Data interrupt */
	}

	/*
	 * TDM Extend Mode
	 * see
	 *	rsnd_ssiu_init_gen2()
	 */
	wsr = ssi->wsr;
	if (is_tdm) {
		wsr	|= WS_MODE;
		cr_own	|= CHNL_8;
	}

	ssi->cr_own	= cr_own;
	ssi->cr_mode	= cr_mode;
	ssi->wsr	= wsr;

	return 0;
}

/*
 *	SSI mod common functions
 */
static int rsnd_ssi_init(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	int ret;

	ssi->usrcnt++;

	rsnd_mod_power_on(mod);

	ret = rsnd_ssi_master_clk_start(ssi, io);
	if (ret < 0)
		return ret;

	if (rsnd_ssi_is_parent(mod, io))
		return 0;

	ret = rsnd_ssi_config_init(ssi, io);
	if (ret < 0)
		return ret;

	ssi->err	= -1; /* ignore 1st error */

	/* clear error status */
	rsnd_ssi_status_clear(mod);

	rsnd_ssi_irq_enable(mod);

	return 0;
}

static int rsnd_ssi_quit(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	struct device *dev = rsnd_priv_to_dev(priv);

	if (!ssi->usrcnt) {
		dev_err(dev, "%s[%d] usrcnt error\n",
			rsnd_mod_name(mod), rsnd_mod_id(mod));
		return -EIO;
	}

	if (!rsnd_ssi_is_parent(mod, io)) {
		if (ssi->err > 0)
			dev_warn(dev, "%s[%d] under/over flow err = %d\n",
				 rsnd_mod_name(mod), rsnd_mod_id(mod),
				 ssi->err);

		ssi->cr_own	= 0;
		ssi->err	= 0;

		rsnd_ssi_irq_disable(mod);
	}

	rsnd_ssi_master_clk_stop(ssi, io);

	rsnd_mod_power_off(mod);

	ssi->usrcnt--;

	return 0;
}

static int rsnd_ssi_hw_params(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct snd_pcm_substream *substream,
			      struct snd_pcm_hw_params *params)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	int chan = params_channels(params);

	/*
	 * Already working.
	 * It will happen if SSI has parent/child connection.
	 */
	if (ssi->usrcnt > 1) {
		/*
		 * it is error if child <-> parent SSI uses
		 * different channels.
		 */
		if (ssi->chan != chan)
			return -EIO;
	}

	ssi->chan = chan;

	return 0;
}

static u32 rsnd_ssi_record_error(struct rsnd_ssi *ssi)
{
	struct rsnd_mod *mod = rsnd_mod_get(ssi);
	u32 status = rsnd_ssi_status_get(mod);

	/* under/over flow error */
	if (status & (UIRQ | OIRQ))
		ssi->err++;

	return status;
}

static int __rsnd_ssi_start(struct rsnd_mod *mod,
			    struct rsnd_dai_stream *io,
			    struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	u32 cr;

	cr  =	ssi->cr_own	|
		ssi->cr_clk	|
		ssi->cr_mode;

	/*
	 * EN will be set via SSIU :: SSI_CONTROL
	 * if Multi channel mode
	 */
	if (!rsnd_ssi_multi_slaves(io))
		cr |= EN;

	rsnd_mod_write(mod, SSICR, cr);
	rsnd_mod_write(mod, SSIWSR, ssi->wsr);

	return 0;
}

static int rsnd_ssi_start(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	/*
	 * no limit to start
	 * see also
	 *	rsnd_ssi_stop
	 *	rsnd_ssi_interrupt
	 */
	return __rsnd_ssi_start(mod, io, priv);
}

static int __rsnd_ssi_stop(struct rsnd_mod *mod,
			   struct rsnd_dai_stream *io,
			   struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	u32 cr;

	/*
	 * disable all IRQ,
	 * and, wait all data was sent
	 */
	cr  =	ssi->cr_own	|
		ssi->cr_clk;

	rsnd_mod_write(mod, SSICR, cr | EN);
	rsnd_ssi_status_check(mod, DIRQ);

	/*
	 * disable SSI,
	 * and, wait idle state
	 */
	rsnd_mod_write(mod, SSICR, cr);	/* disabled all */
	rsnd_ssi_status_check(mod, IIRQ);

	return 0;
}

static int rsnd_ssi_stop(struct rsnd_mod *mod,
			 struct rsnd_dai_stream *io,
			 struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);

	/*
	 * don't stop if not last user
	 * see also
	 *	rsnd_ssi_start
	 *	rsnd_ssi_interrupt
	 */
	if (ssi->usrcnt > 1)
		return 0;

	return __rsnd_ssi_stop(mod, io, priv);
}

static void __rsnd_ssi_interrupt(struct rsnd_mod *mod,
				 struct rsnd_dai_stream *io)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	int is_dma = rsnd_ssi_is_dma_mode(mod);
	u32 status;
	bool elapsed = false;

	spin_lock(&priv->lock);

	/* ignore all cases if not working */
	if (!rsnd_io_is_working(io))
		goto rsnd_ssi_interrupt_out;

	status = rsnd_ssi_record_error(ssi);

	/* PIO only */
	if (!is_dma && (status & DIRQ)) {
		struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
		u32 *buf = (u32 *)(runtime->dma_area +
				   rsnd_dai_pointer_offset(io, 0));

		/*
		 * 8/16/32 data can be assesse to TDR/RDR register
		 * directly as 32bit data
		 * see rsnd_ssi_init()
		 */
		if (rsnd_io_is_play(io))
			rsnd_mod_write(mod, SSITDR, *buf);
		else
			*buf = rsnd_mod_read(mod, SSIRDR);

		elapsed = rsnd_dai_pointer_update(io, sizeof(*buf));
	}

	/* DMA only */
	if (is_dma && (status & (UIRQ | OIRQ))) {
		/*
		 * restart SSI
		 */
		dev_dbg(dev, "%s[%d] restart\n",
			rsnd_mod_name(mod), rsnd_mod_id(mod));

		__rsnd_ssi_stop(mod, io, priv);
		__rsnd_ssi_start(mod, io, priv);
	}

	if (ssi->err > 1024) {
		rsnd_ssi_irq_disable(mod);

		dev_warn(dev, "no more %s[%d] restart\n",
			 rsnd_mod_name(mod), rsnd_mod_id(mod));
	}

	rsnd_ssi_status_clear(mod);
rsnd_ssi_interrupt_out:
	spin_unlock(&priv->lock);

	if (elapsed)
		rsnd_dai_period_elapsed(io);
}

static irqreturn_t rsnd_ssi_interrupt(int irq, void *data)
{
	struct rsnd_mod *mod = data;

	rsnd_mod_interrupt(mod, __rsnd_ssi_interrupt);

	return IRQ_HANDLED;
}

/*
 *		SSI PIO
 */
static void rsnd_ssi_parent_attach(struct rsnd_mod *mod,
				   struct rsnd_dai_stream *io,
				   struct rsnd_priv *priv)
{
	if (!__rsnd_ssi_is_pin_sharing(mod))
		return;

	switch (rsnd_mod_id(mod)) {
	case 1:
	case 2:
		rsnd_dai_connect(rsnd_ssi_mod_get(priv, 0), io, RSND_MOD_SSIP);
		break;
	case 4:
		rsnd_dai_connect(rsnd_ssi_mod_get(priv, 3), io, RSND_MOD_SSIP);
		break;
	case 8:
		rsnd_dai_connect(rsnd_ssi_mod_get(priv, 7), io, RSND_MOD_SSIP);
		break;
	}
}

static int rsnd_ssi_common_probe(struct rsnd_mod *mod,
				 struct rsnd_dai_stream *io,
				 struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	int ret;

	/*
	 * SSIP/SSIU/IRQ are not needed on
	 * SSI Multi slaves
	 */
	if (rsnd_ssi_is_multi_slave(mod, io))
		return 0;

	rsnd_ssi_parent_attach(mod, io, priv);

	ret = rsnd_ssiu_attach(io, mod);
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ssi->irq,
			       rsnd_ssi_interrupt,
			       IRQF_SHARED,
			       dev_name(dev), mod);

	return ret;
}

static struct rsnd_mod_ops rsnd_ssi_pio_ops = {
	.name	= SSI_NAME,
	.probe	= rsnd_ssi_common_probe,
	.init	= rsnd_ssi_init,
	.quit	= rsnd_ssi_quit,
	.start	= rsnd_ssi_start,
	.stop	= rsnd_ssi_stop,
	.hw_params = rsnd_ssi_hw_params,
};

static int rsnd_ssi_dma_probe(struct rsnd_mod *mod,
			      struct rsnd_dai_stream *io,
			      struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	int dma_id = 0; /* not needed */
	int ret;

	/*
	 * SSIP/SSIU/IRQ/DMA are not needed on
	 * SSI Multi slaves
	 */
	if (rsnd_ssi_is_multi_slave(mod, io))
		return 0;

	ret = rsnd_ssi_common_probe(mod, io, priv);
	if (ret)
		return ret;

	ssi->dma = rsnd_dma_attach(io, mod, dma_id);
	if (IS_ERR(ssi->dma))
		return PTR_ERR(ssi->dma);

	return ret;
}

static int rsnd_ssi_dma_remove(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);
	struct device *dev = rsnd_priv_to_dev(priv);
	int irq = ssi->irq;

	/* PIO will request IRQ again */
	devm_free_irq(dev, irq, mod);

	return 0;
}

static int rsnd_ssi_fallback(struct rsnd_mod *mod,
			     struct rsnd_dai_stream *io,
			     struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);

	/*
	 * fallback to PIO
	 *
	 * SSI .probe might be called again.
	 * see
	 *	rsnd_rdai_continuance_probe()
	 */
	mod->ops = &rsnd_ssi_pio_ops;

	dev_info(dev, "%s[%d] fallback to PIO mode\n",
		 rsnd_mod_name(mod), rsnd_mod_id(mod));

	return 0;
}

static struct dma_chan *rsnd_ssi_dma_req(struct rsnd_dai_stream *io,
					 struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	int is_play = rsnd_io_is_play(io);
	char *name;

	if (rsnd_ssi_use_busif(io))
		name = is_play ? "rxu" : "txu";
	else
		name = is_play ? "rx" : "tx";

	return rsnd_dma_request_channel(rsnd_ssi_of_node(priv),
					mod, name);
}

static struct rsnd_mod_ops rsnd_ssi_dma_ops = {
	.name	= SSI_NAME,
	.dma_req = rsnd_ssi_dma_req,
	.probe	= rsnd_ssi_dma_probe,
	.remove	= rsnd_ssi_dma_remove,
	.init	= rsnd_ssi_init,
	.quit	= rsnd_ssi_quit,
	.start	= rsnd_ssi_start,
	.stop	= rsnd_ssi_stop,
	.fallback = rsnd_ssi_fallback,
	.hw_params = rsnd_ssi_hw_params,
};

int rsnd_ssi_is_dma_mode(struct rsnd_mod *mod)
{
	return mod->ops == &rsnd_ssi_dma_ops;
}


/*
 *		Non SSI
 */
static struct rsnd_mod_ops rsnd_ssi_non_ops = {
	.name	= SSI_NAME,
};

/*
 *		ssi mod function
 */
static void rsnd_ssi_connect(struct rsnd_mod *mod,
			     struct rsnd_dai_stream *io)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	enum rsnd_mod_type types[] = {
		RSND_MOD_SSI,
		RSND_MOD_SSIM1,
		RSND_MOD_SSIM2,
		RSND_MOD_SSIM3,
	};
	enum rsnd_mod_type type;
	int i;

	/* try SSI -> SSIM1 -> SSIM2 -> SSIM3 */
	for (i = 0; i < ARRAY_SIZE(types); i++) {
		type = types[i];
		if (!rsnd_io_to_mod(io, type)) {
			rsnd_dai_connect(mod, io, type);
			rsnd_set_slot(rdai, 2 * (i + 1), (i + 1));
			return;
		}
	}
}

void rsnd_parse_connect_ssi(struct rsnd_dai *rdai,
			    struct device_node *playback,
			    struct device_node *capture)
{
	struct rsnd_priv *priv = rsnd_rdai_to_priv(rdai);
	struct device_node *node;
	struct device_node *np;
	struct rsnd_mod *mod;
	int i;

	node = rsnd_ssi_of_node(priv);
	if (!node)
		return;

	i = 0;
	for_each_child_of_node(node, np) {
		mod = rsnd_ssi_mod_get(priv, i);
		if (np == playback)
			rsnd_ssi_connect(mod, &rdai->playback);
		if (np == capture)
			rsnd_ssi_connect(mod, &rdai->capture);
		i++;
	}

	of_node_put(node);
}

struct rsnd_mod *rsnd_ssi_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_ssi_nr(priv)))
		id = 0;

	return rsnd_mod_get(rsnd_ssi_get(priv, id));
}

int __rsnd_ssi_is_pin_sharing(struct rsnd_mod *mod)
{
	struct rsnd_ssi *ssi = rsnd_mod_to_ssi(mod);

	return !!(rsnd_ssi_mode_flags(ssi) & RSND_SSI_CLK_PIN_SHARE);
}

int rsnd_ssi_probe(struct rsnd_priv *priv)
{
	struct device_node *node;
	struct device_node *np;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mod_ops *ops;
	struct clk *clk;
	struct rsnd_ssi *ssi;
	char name[RSND_SSI_NAME_SIZE];
	int i, nr, ret;

	node = rsnd_ssi_of_node(priv);
	if (!node)
		return -EINVAL;

	nr = of_get_child_count(node);
	if (!nr) {
		ret = -EINVAL;
		goto rsnd_ssi_probe_done;
	}

	ssi	= devm_kzalloc(dev, sizeof(*ssi) * nr, GFP_KERNEL);
	if (!ssi) {
		ret = -ENOMEM;
		goto rsnd_ssi_probe_done;
	}

	priv->ssi	= ssi;
	priv->ssi_nr	= nr;

	i = 0;
	for_each_child_of_node(node, np) {
		ssi = rsnd_ssi_get(priv, i);

		snprintf(name, RSND_SSI_NAME_SIZE, "%s.%d",
			 SSI_NAME, i);

		clk = devm_clk_get(dev, name);
		if (IS_ERR(clk)) {
			ret = PTR_ERR(clk);
			goto rsnd_ssi_probe_done;
		}

		if (of_get_property(np, "shared-pin", NULL))
			ssi->flags |= RSND_SSI_CLK_PIN_SHARE;

		if (of_get_property(np, "no-busif", NULL))
			ssi->flags |= RSND_SSI_NO_BUSIF;

		ssi->irq = irq_of_parse_and_map(np, 0);
		if (!ssi->irq) {
			ret = -EINVAL;
			goto rsnd_ssi_probe_done;
		}

		ops = &rsnd_ssi_non_ops;
		if (of_get_property(np, "pio-transfer", NULL))
			ops = &rsnd_ssi_pio_ops;
		else
			ops = &rsnd_ssi_dma_ops;

		ret = rsnd_mod_init(priv, rsnd_mod_get(ssi), ops, clk,
				    RSND_MOD_SSI, i);
		if (ret)
			goto rsnd_ssi_probe_done;

		i++;
	}

	ret = 0;

rsnd_ssi_probe_done:
	of_node_put(node);

	return ret;
}

void rsnd_ssi_remove(struct rsnd_priv *priv)
{
	struct rsnd_ssi *ssi;
	int i;

	for_each_rsnd_ssi(ssi, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(ssi));
	}
}
