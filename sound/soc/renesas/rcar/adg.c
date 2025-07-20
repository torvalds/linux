// SPDX-License-Identifier: GPL-2.0
//
// Helper routines for R-Car sound ADG.
//
//  Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include "rsnd.h"

#define CLKA	0
#define CLKB	1
#define CLKC	2
#define CLKI	3
#define CLKINMAX 4

#define CLKOUT	0
#define CLKOUT1	1
#define CLKOUT2	2
#define CLKOUT3	3
#define CLKOUTMAX 4

#define BRGCKR_31	(1 << 31)
#define BRRx_MASK(x) (0x3FF & x)

static struct rsnd_mod_ops adg_ops = {
	.name = "adg",
};

#define ADG_HZ_441	0
#define ADG_HZ_48	1
#define ADG_HZ_SIZE	2

struct rsnd_adg {
	struct clk *adg;
	struct clk *clkin[CLKINMAX];
	struct clk *clkout[CLKOUTMAX];
	struct clk *null_clk;
	struct clk_onecell_data onecell;
	struct rsnd_mod mod;
	int clkin_rate[CLKINMAX];
	int clkin_size;
	int clkout_size;
	u32 ckr;
	u32 brga;
	u32 brgb;

	int brg_rate[ADG_HZ_SIZE]; /* BRGA / BRGB */
};

#define for_each_rsnd_clkin(pos, adg, i)	\
	for (i = 0;				\
	     (i < adg->clkin_size) &&		\
	     ((pos) = adg->clkin[i]);		\
	     i++)
#define for_each_rsnd_clkout(pos, adg, i)	\
	for (i = 0;				\
	     (i < adg->clkout_size) &&		\
	     ((pos) = adg->clkout[i]);	\
	     i++)
#define rsnd_priv_to_adg(priv) ((struct rsnd_adg *)(priv)->adg)

static const char * const clkin_name_gen4[] = {
	[CLKA]	= "clkin",
};

static const char * const clkin_name_gen2[] = {
	[CLKA]	= "clk_a",
	[CLKB]	= "clk_b",
	[CLKC]	= "clk_c",
	[CLKI]	= "clk_i",
};

static const char * const clkout_name_gen2[] = {
	[CLKOUT]  = "audio_clkout",
	[CLKOUT1] = "audio_clkout1",
	[CLKOUT2] = "audio_clkout2",
	[CLKOUT3] = "audio_clkout3",
};

static u32 rsnd_adg_calculate_brgx(unsigned long div)
{
	int i;

	if (!div)
		return 0;

	for (i = 3; i >= 0; i--) {
		int ratio = 2 << (i * 2);
		if (0 == (div % ratio))
			return (u32)((i << 8) | ((div / ratio) - 1));
	}

	return ~0;
}

static u32 rsnd_adg_ssi_ws_timing_gen2(struct rsnd_dai_stream *io)
{
	struct rsnd_mod *ssi_mod = rsnd_io_to_mod_ssi(io);
	int id = rsnd_mod_id(ssi_mod);
	int ws = id;

	if (rsnd_ssi_is_pin_sharing(io)) {
		switch (id) {
		case 1:
		case 2:
		case 9:
			ws = 0;
			break;
		case 4:
			ws = 3;
			break;
		case 8:
			ws = 7;
			break;
		}
	} else {
		/*
		 * SSI8 is not connected to ADG.
		 * Thus SSI9 is using ws = 8
		 */
		if (id == 9)
			ws = 8;
	}

	return (0x6 + ws) << 8;
}

static void __rsnd_adg_get_timesel_ratio(struct rsnd_priv *priv,
				       struct rsnd_dai_stream *io,
				       unsigned int target_rate,
				       unsigned int *target_val,
				       unsigned int *target_en)
{
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	int sel;
	unsigned int val, en;
	unsigned int min, diff;
	unsigned int sel_rate[] = {
		adg->clkin_rate[CLKA],	/* 0000: CLKA */
		adg->clkin_rate[CLKB],	/* 0001: CLKB */
		adg->clkin_rate[CLKC],	/* 0010: CLKC */
		adg->brg_rate[ADG_HZ_441],	/* 0011: BRGA */
		adg->brg_rate[ADG_HZ_48],	/* 0100: BRGB */
	};

	min = ~0;
	val = 0;
	en = 0;
	for (sel = 0; sel < ARRAY_SIZE(sel_rate); sel++) {
		int idx = 0;
		int step = 2;
		int div;

		if (!sel_rate[sel])
			continue;

		for (div = 2; div <= 98304; div += step) {
			diff = abs(target_rate - sel_rate[sel] / div);
			if (min > diff) {
				val = (sel << 8) | idx;
				min = diff;
				en = 1 << (sel + 1); /* fixme */
			}

			/*
			 * step of 0_0000 / 0_0001 / 0_1101
			 * are out of order
			 */
			if ((idx > 2) && (idx % 2))
				step *= 2;
			if (idx == 0x1c) {
				div += step;
				step *= 2;
			}
			idx++;
		}
	}

	if (min == ~0) {
		dev_err(dev, "no Input clock\n");
		return;
	}

	*target_val = val;
	if (target_en)
		*target_en = en;
}

static void rsnd_adg_get_timesel_ratio(struct rsnd_priv *priv,
				       struct rsnd_dai_stream *io,
				       unsigned int in_rate,
				       unsigned int out_rate,
				       u32 *in, u32 *out, u32 *en)
{
	struct snd_pcm_runtime *runtime = rsnd_io_to_runtime(io);
	unsigned int target_rate;
	u32 *target_val;
	u32 _in;
	u32 _out;
	u32 _en;

	/* default = SSI WS */
	_in =
	_out = rsnd_adg_ssi_ws_timing_gen2(io);

	target_rate = 0;
	target_val = NULL;
	_en = 0;
	if (runtime->rate != in_rate) {
		target_rate = out_rate;
		target_val  = &_out;
	} else if (runtime->rate != out_rate) {
		target_rate = in_rate;
		target_val  = &_in;
	}

	if (target_rate)
		__rsnd_adg_get_timesel_ratio(priv, io,
					     target_rate,
					     target_val, &_en);

	if (in)
		*in = _in;
	if (out)
		*out = _out;
	if (en)
		*en = _en;
}

int rsnd_adg_set_cmd_timsel_gen2(struct rsnd_mod *cmd_mod,
				 struct rsnd_dai_stream *io)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(cmd_mod);
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct rsnd_mod *adg_mod = rsnd_mod_get(adg);
	int id = rsnd_mod_id(cmd_mod);
	int shift = (id % 2) ? 16 : 0;
	u32 mask, val;

	rsnd_adg_get_timesel_ratio(priv, io,
				   rsnd_src_get_in_rate(priv, io),
				   rsnd_src_get_out_rate(priv, io),
				   NULL, &val, NULL);

	val  = val	<< shift;
	mask = 0x0f1f	<< shift;

	rsnd_mod_bset(adg_mod, CMDOUT_TIMSEL, mask, val);

	return 0;
}

int rsnd_adg_set_src_timesel_gen2(struct rsnd_mod *src_mod,
				  struct rsnd_dai_stream *io,
				  unsigned int in_rate,
				  unsigned int out_rate)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(src_mod);
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct rsnd_mod *adg_mod = rsnd_mod_get(adg);
	u32 in, out;
	u32 mask, en;
	int id = rsnd_mod_id(src_mod);
	int shift = (id % 2) ? 16 : 0;

	rsnd_mod_make_sure(src_mod, RSND_MOD_SRC);

	rsnd_adg_get_timesel_ratio(priv, io,
				   in_rate, out_rate,
				   &in, &out, &en);

	in   = in	<< shift;
	out  = out	<< shift;
	mask = 0x0f1f	<< shift;

	rsnd_mod_bset(adg_mod, SRCIN_TIMSEL(id / 2),  mask, in);
	rsnd_mod_bset(adg_mod, SRCOUT_TIMSEL(id / 2), mask, out);

	if (en)
		rsnd_mod_bset(adg_mod, DIV_EN, en, en);

	return 0;
}

static void rsnd_adg_set_ssi_clk(struct rsnd_mod *ssi_mod, u32 val)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct rsnd_mod *adg_mod = rsnd_mod_get(adg);
	struct device *dev = rsnd_priv_to_dev(priv);
	int id = rsnd_mod_id(ssi_mod);
	int shift = (id % 4) * 8;
	u32 mask = 0xFF << shift;

	rsnd_mod_make_sure(ssi_mod, RSND_MOD_SSI);

	val = val << shift;

	/*
	 * SSI 8 is not connected to ADG.
	 * it works with SSI 7
	 */
	if (id == 8)
		return;

	rsnd_mod_bset(adg_mod, AUDIO_CLK_SEL(id / 4), mask, val);

	dev_dbg(dev, "AUDIO_CLK_SEL is 0x%x\n", val);
}

int rsnd_adg_clk_query(struct rsnd_priv *priv, unsigned int rate)
{
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct clk *clk;
	int i;
	int sel_table[] = {
		[CLKA] = 0x1,
		[CLKB] = 0x2,
		[CLKC] = 0x3,
		[CLKI] = 0x0,
	};

	/*
	 * find suitable clock from
	 * AUDIO_CLKA/AUDIO_CLKB/AUDIO_CLKC/AUDIO_CLKI.
	 */
	for_each_rsnd_clkin(clk, adg, i)
		if (rate == adg->clkin_rate[i])
			return sel_table[i];

	/*
	 * find divided clock from BRGA/BRGB
	 */
	if (rate == adg->brg_rate[ADG_HZ_441])
		return 0x10;

	if (rate == adg->brg_rate[ADG_HZ_48])
		return 0x20;

	return -EIO;
}

int rsnd_adg_ssi_clk_stop(struct rsnd_mod *ssi_mod)
{
	rsnd_adg_set_ssi_clk(ssi_mod, 0);

	return 0;
}

int rsnd_adg_ssi_clk_try_start(struct rsnd_mod *ssi_mod, unsigned int rate)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(ssi_mod);
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct rsnd_mod *adg_mod = rsnd_mod_get(adg);
	int data;
	u32 ckr = 0;

	data = rsnd_adg_clk_query(priv, rate);
	if (data < 0)
		return data;

	rsnd_adg_set_ssi_clk(ssi_mod, data);

	ckr = adg->ckr & ~BRGCKR_31;
	if (0 == (rate % 8000))
		ckr |= BRGCKR_31; /* use BRGB output = 48kHz */
	if (ckr != adg->ckr) {
		rsnd_mod_bset(adg_mod, BRGCKR, 0x80770000, adg->ckr);
		adg->ckr = ckr;
	}

	dev_dbg(dev, "CLKOUT is based on BRG%c (= %dHz)\n",
		(ckr) ? 'B' : 'A',
		(ckr) ?	adg->brg_rate[ADG_HZ_48] :
			adg->brg_rate[ADG_HZ_441]);

	return 0;
}

int rsnd_adg_clk_control(struct rsnd_priv *priv, int enable)
{
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct rsnd_mod *adg_mod = rsnd_mod_get(adg);
	struct clk *clk;
	int ret = 0, i;

	if (enable) {
		ret = clk_prepare_enable(adg->adg);
		if (ret < 0)
			return ret;

		rsnd_mod_bset(adg_mod, BRGCKR, 0x80770000, adg->ckr);
		rsnd_mod_write(adg_mod, BRRA,  adg->brga);
		rsnd_mod_write(adg_mod, BRRB,  adg->brgb);
	}

	for_each_rsnd_clkin(clk, adg, i) {
		if (enable) {
			ret = clk_prepare_enable(clk);

			/*
			 * We shouldn't use clk_get_rate() under
			 * atomic context. Let's keep it when
			 * rsnd_adg_clk_enable() was called
			 */
			if (ret < 0)
				break;

			adg->clkin_rate[i] = clk_get_rate(clk);
		} else {
			if (adg->clkin_rate[i])
				clk_disable_unprepare(clk);

			adg->clkin_rate[i] = 0;
		}
	}

	/*
	 * rsnd_adg_clk_enable() might return error (_disable() will not).
	 * We need to rollback in such case
	 */
	if (ret < 0)
		rsnd_adg_clk_disable(priv);

	/* disable adg */
	if (!enable)
		clk_disable_unprepare(adg->adg);

	return ret;
}

static struct clk *rsnd_adg_create_null_clk(struct rsnd_priv *priv,
					    const char * const name,
					    const char *parent)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct clk *clk;

	clk = clk_register_fixed_rate(dev, name, parent, 0, 0);
	if (IS_ERR_OR_NULL(clk)) {
		dev_err(dev, "create null clk error\n");
		return ERR_CAST(clk);
	}

	return clk;
}

static struct clk *rsnd_adg_null_clk_get(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;

	if (!adg->null_clk) {
		static const char * const name = "rsnd_adg_null";

		adg->null_clk = rsnd_adg_create_null_clk(priv, name, NULL);
	}

	return adg->null_clk;
}

static void rsnd_adg_null_clk_clean(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;

	if (adg->null_clk)
		clk_unregister_fixed_rate(adg->null_clk);
}

static int rsnd_adg_get_clkin(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct clk *clk;
	const char * const *clkin_name;
	int clkin_size;
	int i;

	clkin_name = clkin_name_gen2;
	clkin_size = ARRAY_SIZE(clkin_name_gen2);
	if (rsnd_is_gen4(priv)) {
		clkin_name = clkin_name_gen4;
		clkin_size = ARRAY_SIZE(clkin_name_gen4);
	}

	/*
	 * get adg
	 * No "adg" is not error
	 */
	clk = devm_clk_get(dev, "adg");
	if (IS_ERR_OR_NULL(clk))
		clk = rsnd_adg_null_clk_get(priv);
	adg->adg = clk;

	/* get clkin */
	for (i = 0; i < clkin_size; i++) {
		clk = devm_clk_get(dev, clkin_name[i]);

		if (IS_ERR_OR_NULL(clk))
			clk = rsnd_adg_null_clk_get(priv);
		if (IS_ERR_OR_NULL(clk))
			goto err;

		adg->clkin[i] = clk;
	}

	adg->clkin_size = clkin_size;

	return 0;

err:
	dev_err(dev, "adg clock IN get failed\n");

	rsnd_adg_null_clk_clean(priv);

	return -EIO;
}

static void rsnd_adg_unregister_clkout(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;
	struct clk *clk;
	int i;

	for_each_rsnd_clkout(clk, adg, i)
		clk_unregister_fixed_rate(clk);
}

static int rsnd_adg_get_clkout(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg = priv->adg;
	struct clk *clk;
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *np = dev->of_node;
	struct property *prop;
	u32 ckr, brgx, brga, brgb;
	u32 req_rate[ADG_HZ_SIZE] = {};
	uint32_t count = 0;
	unsigned long req_Hz[ADG_HZ_SIZE];
	int clkout_size;
	int i, req_size;
	int approximate = 0;
	const char *parent_clk_name = NULL;
	const char * const *clkout_name;
	int brg_table[] = {
		[CLKA] = 0x0,
		[CLKB] = 0x1,
		[CLKC] = 0x4,
		[CLKI] = 0x2,
	};

	ckr = 0;
	brga = 0xff; /* default */
	brgb = 0xff; /* default */

	/*
	 * ADG supports BRRA/BRRB output only
	 * this means all clkout0/1/2/3 will be same rate
	 */
	prop = of_find_property(np, "clock-frequency", NULL);
	if (!prop)
		goto rsnd_adg_get_clkout_end;

	req_size = prop->length / sizeof(u32);
	if (req_size > ADG_HZ_SIZE) {
		dev_err(dev, "too many clock-frequency\n");
		return -EINVAL;
	}

	of_property_read_u32_array(np, "clock-frequency", req_rate, req_size);
	req_Hz[ADG_HZ_48]  = 0;
	req_Hz[ADG_HZ_441] = 0;
	for (i = 0; i < req_size; i++) {
		if (0 == (req_rate[i] % 44100))
			req_Hz[ADG_HZ_441] = req_rate[i];
		if (0 == (req_rate[i] % 48000))
			req_Hz[ADG_HZ_48] = req_rate[i];
	}

	/*
	 * This driver is assuming that AUDIO_CLKA/AUDIO_CLKB/AUDIO_CLKC
	 * have 44.1kHz or 48kHz base clocks for now.
	 *
	 * SSI itself can divide parent clock by 1/1 - 1/16
	 * see
	 *	rsnd_adg_ssi_clk_try_start()
	 *	rsnd_ssi_master_clk_start()
	 */

	/*
	 * [APPROXIMATE]
	 *
	 * clk_i (internal clock) can't create accurate rate, it will be approximate rate.
	 *
	 * <Note>
	 *
	 * clk_i needs x2 of required maximum rate.
	 * see
	 *	- Minimum division of BRRA/BRRB
	 *	- rsnd_ssi_clk_query()
	 *
	 * Sample Settings for TDM 8ch, 32bit width
	 *
	 *	8(ch) x 32(bit) x 44100(Hz) x 2<Note> = 22579200
	 *	8(ch) x 32(bit) x 48000(Hz) x 2<Note> = 24576000
	 *
	 *	clock-frequency = <22579200 24576000>;
	 */
	for_each_rsnd_clkin(clk, adg, i) {
		u32 rate, div;

		rate = clk_get_rate(clk);

		if (0 == rate) /* not used */
			continue;

		/* BRGA */

		if (i == CLKI)
			/* see [APPROXIMATE] */
			rate = (clk_get_rate(clk) / req_Hz[ADG_HZ_441]) * req_Hz[ADG_HZ_441];
		if (!adg->brg_rate[ADG_HZ_441] && req_Hz[ADG_HZ_441] && (0 == rate % 44100)) {
			div = rate / req_Hz[ADG_HZ_441];
			brgx = rsnd_adg_calculate_brgx(div);
			if (BRRx_MASK(brgx) == brgx) {
				brga = brgx;
				adg->brg_rate[ADG_HZ_441] = rate / div;
				ckr |= brg_table[i] << 20;
				if (req_Hz[ADG_HZ_441])
					parent_clk_name = __clk_get_name(clk);
				if (i == CLKI)
					approximate = 1;
			}
		}

		/* BRGB */

		if (i == CLKI)
			/* see [APPROXIMATE] */
			rate = (clk_get_rate(clk) / req_Hz[ADG_HZ_48]) * req_Hz[ADG_HZ_48];
		if (!adg->brg_rate[ADG_HZ_48] && req_Hz[ADG_HZ_48] && (0 == rate % 48000)) {
			div = rate / req_Hz[ADG_HZ_48];
			brgx = rsnd_adg_calculate_brgx(div);
			if (BRRx_MASK(brgx) == brgx) {
				brgb = brgx;
				adg->brg_rate[ADG_HZ_48] = rate / div;
				ckr |= brg_table[i] << 16;
				if (req_Hz[ADG_HZ_48])
					parent_clk_name = __clk_get_name(clk);
				if (i == CLKI)
					approximate = 1;
			}
		}
	}

	if (!(adg->brg_rate[ADG_HZ_48]  && req_Hz[ADG_HZ_48]) &&
	    !(adg->brg_rate[ADG_HZ_441] && req_Hz[ADG_HZ_441]))
		goto rsnd_adg_get_clkout_end;

	if (approximate)
		dev_info(dev, "It uses CLK_I as approximate rate");

	clkout_name = clkout_name_gen2;
	clkout_size = ARRAY_SIZE(clkout_name_gen2);
	if (rsnd_is_gen4(priv))
		clkout_size = 1; /* reuse clkout_name_gen2[] */

	/*
	 * ADG supports BRRA/BRRB output only.
	 * this means all clkout0/1/2/3 will be * same rate
	 */

	of_property_read_u32(np, "#clock-cells", &count);
	/*
	 * for clkout
	 */
	if (!count) {
		clk = clk_register_fixed_rate(dev, clkout_name[CLKOUT],
					      parent_clk_name, 0, req_rate[0]);
		if (IS_ERR_OR_NULL(clk))
			goto err;

		adg->clkout[CLKOUT] = clk;
		adg->clkout_size = 1;
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
	}
	/*
	 * for clkout0/1/2/3
	 */
	else {
		for (i = 0; i < clkout_size; i++) {
			clk = clk_register_fixed_rate(dev, clkout_name[i],
						      parent_clk_name, 0,
						      req_rate[0]);
			if (IS_ERR_OR_NULL(clk))
				goto err;

			adg->clkout[i] = clk;
		}
		adg->onecell.clks	= adg->clkout;
		adg->onecell.clk_num	= clkout_size;
		adg->clkout_size	= clkout_size;
		of_clk_add_provider(np, of_clk_src_onecell_get,
				    &adg->onecell);
	}

rsnd_adg_get_clkout_end:
	if (0 == (req_rate[0] % 8000))
		ckr |= BRGCKR_31; /* use BRGB output = 48kHz */

	adg->ckr = ckr;
	adg->brga = brga;
	adg->brgb = brgb;

	return 0;

err:
	dev_err(dev, "adg clock OUT get failed\n");

	rsnd_adg_unregister_clkout(priv);

	return -EIO;
}

#if defined(DEBUG) || defined(CONFIG_DEBUG_FS)
__printf(3, 4)
static void dbg_msg(struct device *dev, struct seq_file *m,
				   const char *fmt, ...)
{
	char msg[128];
	va_list args;

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	if (m)
		seq_puts(m, msg);
	else
		dev_dbg(dev, "%s", msg);
}

void rsnd_adg_clk_dbg_info(struct rsnd_priv *priv, struct seq_file *m)
{
	struct rsnd_adg *adg = rsnd_priv_to_adg(priv);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct clk *clk;
	int i;

	for_each_rsnd_clkin(clk, adg, i)
		dbg_msg(dev, m, "%-18s : %pa : %ld\n",
			__clk_get_name(clk), clk, clk_get_rate(clk));

	dbg_msg(dev, m, "BRGCKR = 0x%08x, BRRA/BRRB = 0x%x/0x%x\n",
		adg->ckr, adg->brga, adg->brgb);
	dbg_msg(dev, m, "BRGA (for 44100 base) = %d\n", adg->brg_rate[ADG_HZ_441]);
	dbg_msg(dev, m, "BRGB (for 48000 base) = %d\n", adg->brg_rate[ADG_HZ_48]);

	/*
	 * Actual CLKOUT will be exchanged in rsnd_adg_ssi_clk_try_start()
	 * by BRGCKR::BRGCKR_31
	 */
	for_each_rsnd_clkout(clk, adg, i)
		dbg_msg(dev, m, "%-18s : %pa : %ld\n",
			__clk_get_name(clk), clk, clk_get_rate(clk));
}
#else
#define rsnd_adg_clk_dbg_info(priv, m)
#endif

int rsnd_adg_probe(struct rsnd_priv *priv)
{
	struct rsnd_adg *adg;
	struct device *dev = rsnd_priv_to_dev(priv);
	int ret;

	adg = devm_kzalloc(dev, sizeof(*adg), GFP_KERNEL);
	if (!adg)
		return -ENOMEM;

	ret = rsnd_mod_init(priv, &adg->mod, &adg_ops,
		      NULL, 0, 0);
	if (ret)
		return ret;

	priv->adg = adg;

	ret = rsnd_adg_get_clkin(priv);
	if (ret)
		return ret;

	ret = rsnd_adg_get_clkout(priv);
	if (ret)
		return ret;

	ret = rsnd_adg_clk_enable(priv);
	if (ret)
		return ret;

	rsnd_adg_clk_dbg_info(priv, NULL);

	return 0;
}

void rsnd_adg_remove(struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *np = dev->of_node;

	rsnd_adg_unregister_clkout(priv);

	of_clk_del_provider(np);

	rsnd_adg_clk_disable(priv);

	/* It should be called after rsnd_adg_clk_disable() */
	rsnd_adg_null_clk_clean(priv);
}
