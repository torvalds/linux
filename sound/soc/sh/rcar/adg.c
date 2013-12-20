/*
 * Helper routines for R-Car sound ADG.
 *
 *  Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/sh_clk.h>
#include "rsnd.h"

#define CLKA	0
#define CLKB	1
#define CLKC	2
#define CLKI	3
#define CLKMAX	4

struct rsnd_adg {
	struct clk *clk[CLKMAX];

	int rate_of_441khz_div_6;
	int rate_of_48khz_div_6;
	u32 ckr;
};

#define for_each_rsnd_clk(pos, adg, i)		\
	for (i = 0, (pos) = adg->clk[i];	\
	     i < CLKMAX;			\
	     i++, (pos) = adg->clk[i])
#define rsnd_priv_to_adg(priv) ((struct rsnd_adg *)(priv)->adg)

static void rsnd_adg_set_ssi_clk(struct rsnd_mod *mod, u32 val)
{
	int id = rsnd_mod_id(mod);
	int shift = (id % 4) * 8;
	u32 mask = 0xFF << shift;

	val = val << shift;

	/*
	 * SSI 8 is not connected to ADG.
	 * it works with SSI 7
	 */
	if (id == 8)
		return;

	switch (id / 4) {
	case 0:
		rsnd_mod_bset(mod, AUDIO_CLK_SEL0, mask, val);
		break;
	case 1:
		rsnd_mod_bset(mod, AUDIO_CLK_SEL1, mask, val);
		break;
	case 2:
		rsnd_mod_bset(mod, AUDIO_CLK_SEL2, mask, val);
		break;
	}
}

int rsnd_adg_ssi_clk_stop(struct rsnd_mod *mod)
{
	/*
	 * "mod" = "ssi" here.
	 * we can get "ssi id" from mod
	 */
	rsnd_adg_set_ssi_clk(mod, 0);

	return 0;
}

int rsnd_adg_ssi_clk_try_start(struct rsnd_mod *mod, unsigned int rate)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct clk *clk;
	int i;
	u32 data;
	int sel_table[] = {
		[CLKA] = 0x1,
		[CLKB] = 0x2,
		[CLKC] = 0x3,
		[CLKI] = 0x0,
	};

	dev_dbg(dev, "request clock = %d\n", rate);

	/*
	 * find suitable clock from
	 * AUDIO_CLKA/AUDIO_CLKB/AUDIO_CLKC/AUDIO_CLKI.
	 */
	data = 0;
	for_each_rsnd_clk(clk, adg, i) {
		if (rate == clk_get_rate(clk)) {
			data = sel_table[i];
			goto found_clock;
		}
	}

	/*
	 * find 1/6 clock from BRGA/BRGB
	 */
	if (rate == adg->rate_of_441khz_div_6) {
		data = 0x10;
		goto found_clock;
	}

	if (rate == adg->rate_of_48khz_div_6) {
		data = 0x20;
		goto found_clock;
	}

	return -EIO;

found_clock:

	/* see rsnd_adg_ssi_clk_init() */
	rsnd_mod_bset(mod, SSICKR, 0x00FF0000, adg->ckr);
	rsnd_mod_write(mod, BRRA,  0x00000002); /* 1/6 */
	rsnd_mod_write(mod, BRRB,  0x00000002); /* 1/6 */

	/*
	 * This "mod" = "ssi" here.
	 * we can get "ssi id" from mod
	 */
	rsnd_adg_set_ssi_clk(mod, data);

	dev_dbg(dev, "ADG: ssi%d selects clk%d = %d",
		rsnd_mod_id(mod), i, rate);

	return 0;
}

static void rsnd_adg_ssi_clk_init(struct rsnd_priv *priv, struct rsnd_adg *adg)
{
	struct clk *clk;
	unsigned long rate;
	u32 ckr;
	int i;
	int brg_table[] = {
		[CLKA] = 0x0,
		[CLKB] = 0x1,
		[CLKC] = 0x4,
		[CLKI] = 0x2,
	};

	/*
	 * This driver is assuming that AUDIO_CLKA/AUDIO_CLKB/AUDIO_CLKC
	 * have 44.1kHz or 48kHz base clocks for now.
	 *
	 * SSI itself can divide parent clock by 1/1 - 1/16
	 * So,  BRGA outputs 44.1kHz base parent clock 1/32,
	 * and, BRGB outputs 48.0kHz base parent clock 1/32 here.
	 * see
	 *	rsnd_adg_ssi_clk_try_start()
	 */
	ckr = 0;
	adg->rate_of_441khz_div_6 = 0;
	adg->rate_of_48khz_div_6  = 0;
	for_each_rsnd_clk(clk, adg, i) {
		rate = clk_get_rate(clk);

		if (0 == rate) /* not used */
			continue;

		/* RBGA */
		if (!adg->rate_of_441khz_div_6 && (0 == rate % 44100)) {
			adg->rate_of_441khz_div_6 = rate / 6;
			ckr |= brg_table[i] << 20;
		}

		/* RBGB */
		if (!adg->rate_of_48khz_div_6 && (0 == rate % 48000)) {
			adg->rate_of_48khz_div_6 = rate / 6;
			ckr |= brg_table[i] << 16;
		}
	}

	adg->ckr = ckr;
}

int rsnd_adg_probe(struct platform_device *pdev,
		   struct rcar_snd_info *info,
		   struct rsnd_priv *priv)
{
	struct rsnd_adg *adg;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct clk *clk;
	int i;

	adg = devm_kzalloc(dev, sizeof(*adg), GFP_KERNEL);
	if (!adg) {
		dev_err(dev, "ADG allocate failed\n");
		return -ENOMEM;
	}

	adg->clk[CLKA] = clk_get(NULL, "audio_clk_a");
	adg->clk[CLKB] = clk_get(NULL, "audio_clk_b");
	adg->clk[CLKC] = clk_get(NULL, "audio_clk_c");
	adg->clk[CLKI] = clk_get(NULL, "audio_clk_internal");
	for_each_rsnd_clk(clk, adg, i) {
		if (IS_ERR(clk)) {
			dev_err(dev, "Audio clock failed\n");
			return -EIO;
		}
	}

	rsnd_adg_ssi_clk_init(priv, adg);

	priv->adg = adg;

	dev_dbg(dev, "adg probed\n");

	return 0;
}

void rsnd_adg_remove(struct platform_device *pdev,
		     struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;
	struct clk *clk;
	int i;

	for_each_rsnd_clk(clk, adg, i)
		clk_put(clk);
}
