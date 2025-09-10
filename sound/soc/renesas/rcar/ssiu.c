// SPDX-License-Identifier: GPL-2.0
//
// Renesas R-Car SSIU support
//
// Copyright (c) 2015 Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>

#include "rsnd.h"

#define SSIU_NAME "ssiu"

struct rsnd_ssiu {
	struct rsnd_mod mod;
	u32 busif_status[8]; /* for BUSIF0 - BUSIF7 */
	unsigned int usrcnt;
	int id;
	int id_sub;
};

/* SSI_MODE */
#define TDM_EXT		(1 << 0)
#define TDM_SPLIT	(1 << 8)

#define rsnd_ssiu_nr(priv) ((priv)->ssiu_nr)
#define rsnd_mod_to_ssiu(_mod) container_of((_mod), struct rsnd_ssiu, mod)
#define for_each_rsnd_ssiu(pos, priv, i)				\
	for (i = 0;							\
	     (i < rsnd_ssiu_nr(priv)) &&				\
		     ((pos) = ((struct rsnd_ssiu *)(priv)->ssiu + i));	\
	     i++)

/*
 *	SSI	Gen2		Gen3		Gen4
 *	0	BUSIF0-3	BUSIF0-7	BUSIF0-7
 *	1	BUSIF0-3	BUSIF0-7
 *	2	BUSIF0-3	BUSIF0-7
 *	3	BUSIF0		BUSIF0-7
 *	4	BUSIF0		BUSIF0-7
 *	5	BUSIF0		BUSIF0
 *	6	BUSIF0		BUSIF0
 *	7	BUSIF0		BUSIF0
 *	8	BUSIF0		BUSIF0
 *	9	BUSIF0-3	BUSIF0-7
 *	total	22		52		8
 */
static const int gen2_id[] = { 0, 4,  8, 12, 13, 14, 15, 16, 17, 18 };
static const int gen3_id[] = { 0, 8, 16, 24, 32, 40, 41, 42, 43, 44 };
static const int gen4_id[] = { 0 };

/* enable busif buffer over/under run interrupt. */
#define rsnd_ssiu_busif_err_irq_enable(mod)  rsnd_ssiu_busif_err_irq_ctrl(mod, 1)
#define rsnd_ssiu_busif_err_irq_disable(mod) rsnd_ssiu_busif_err_irq_ctrl(mod, 0)
static void rsnd_ssiu_busif_err_irq_ctrl(struct rsnd_mod *mod, int enable)
{
	int id = rsnd_mod_id(mod);
	int shift, offset;
	int i;

	switch (id) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		shift  = id;
		offset = 0;
		break;
	case 9:
		shift  = 1;
		offset = 1;
		break;
	default:
		return;
	}

	for (i = 0; i < 4; i++) {
		enum rsnd_reg reg = SSI_SYS_INT_ENABLE((i * 2) + offset);
		u32 val = 0xf << (shift * 4);
		u32 sys_int_enable = rsnd_mod_read(mod, reg);

		if (enable)
			sys_int_enable |= val;
		else
			sys_int_enable &= ~val;
		rsnd_mod_write(mod, reg, sys_int_enable);
	}
}

bool rsnd_ssiu_busif_err_status_clear(struct rsnd_mod *mod)
{
	bool error = false;
	int id = rsnd_mod_id(mod);
	int shift, offset;
	int i;

	switch (id) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		shift  = id;
		offset = 0;
		break;
	case 9:
		shift  = 1;
		offset = 1;
		break;
	default:
		goto out;
	}

	for (i = 0; i < 4; i++) {
		u32 reg = SSI_SYS_STATUS(i * 2) + offset;
		u32 status = rsnd_mod_read(mod, reg);
		u32 val = 0xf << (shift * 4);

		status &= val;
		if (status) {
			struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
			struct device *dev = rsnd_priv_to_dev(priv);

			rsnd_print_irq_status(dev, "%s err status : 0x%08x\n",
					      rsnd_mod_name(mod), status);
			error = true;
		}
		rsnd_mod_write(mod, reg, val);
	}
out:
	return error;
}

static u32 *rsnd_ssiu_get_status(struct rsnd_mod *mod,
				 struct rsnd_dai_stream *io,
				 enum rsnd_mod_type type)
{
	struct rsnd_ssiu *ssiu = rsnd_mod_to_ssiu(mod);
	int busif = rsnd_mod_id_sub(mod);

	return &ssiu->busif_status[busif];
}

static int rsnd_ssiu_init(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	struct rsnd_dai *rdai = rsnd_io_to_rdai(io);
	u32 ssis = rsnd_ssi_multi_secondaries_runtime(io);
	int use_busif = rsnd_ssi_use_busif(io);
	int id = rsnd_mod_id(mod);
	int is_clk_master = rsnd_rdai_is_clk_master(rdai);
	u32 val1, val2;

	/* clear status */
	rsnd_ssiu_busif_err_status_clear(mod);

	/* Gen4 doesn't have SSI_MODE */
	if (rsnd_is_gen4(priv))
		goto ssi_mode_setting_end;

	/*
	 * SSI_MODE0
	 */
	rsnd_mod_bset(mod, SSI_MODE0, (1 << id), !use_busif << id);

	/*
	 * SSI_MODE1 / SSI_MODE2
	 *
	 * FIXME
	 * sharing/multi with SSI0 are mainly supported
	 */
	val1 = rsnd_mod_read(mod, SSI_MODE1);
	val2 = rsnd_mod_read(mod, SSI_MODE2);
	if (rsnd_ssi_is_pin_sharing(io)) {

		ssis |= (1 << id);

	} else if (ssis) {
		/*
		 * Multi SSI
		 *
		 * set synchronized bit here
		 */

		/* SSI4 is synchronized with SSI3 */
		if (ssis & (1 << 4))
			val1 |= (1 << 20);
		/* SSI012 are synchronized */
		if (ssis == 0x0006)
			val1 |= (1 << 4);
		/* SSI0129 are synchronized */
		if (ssis == 0x0206)
			val2 |= (1 << 4);
	}

	/* SSI1 is sharing pin with SSI0 */
	if (ssis & (1 << 1))
		val1 |= is_clk_master ? 0x2 : 0x1;

	/* SSI2 is sharing pin with SSI0 */
	if (ssis & (1 << 2))
		val1 |= is_clk_master ?	0x2 << 2 :
					0x1 << 2;
	/* SSI4 is sharing pin with SSI3 */
	if (ssis & (1 << 4))
		val1 |= is_clk_master ? 0x2 << 16 :
					0x1 << 16;
	/* SSI9 is sharing pin with SSI0 */
	if (ssis & (1 << 9))
		val2 |= is_clk_master ? 0x2 : 0x1;

	rsnd_mod_bset(mod, SSI_MODE1, 0x0013001f, val1);
	rsnd_mod_bset(mod, SSI_MODE2, 0x00000017, val2);

ssi_mode_setting_end:
	/*
	 * Enable busif buffer over/under run interrupt.
	 * It will be handled from ssi.c
	 * see
	 *	__rsnd_ssi_interrupt()
	 */
	rsnd_ssiu_busif_err_irq_enable(mod);

	return 0;
}

static int rsnd_ssiu_quit(struct rsnd_mod *mod,
			  struct rsnd_dai_stream *io,
			  struct rsnd_priv *priv)
{
	/* disable busif buffer over/under run interrupt. */
	rsnd_ssiu_busif_err_irq_disable(mod);

	return 0;
}

static struct rsnd_mod_ops rsnd_ssiu_ops_gen1 = {
	.name		= SSIU_NAME,
	.init		= rsnd_ssiu_init,
	.quit		= rsnd_ssiu_quit,
	.get_status	= rsnd_ssiu_get_status,
};

static int rsnd_ssiu_init_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	struct rsnd_ssiu *ssiu = rsnd_mod_to_ssiu(mod);
	u32 has_hdmi0 = rsnd_flags_has(io, RSND_STREAM_HDMI0);
	u32 has_hdmi1 = rsnd_flags_has(io, RSND_STREAM_HDMI1);
	int ret;
	u32 mode = 0;

	ret = rsnd_ssiu_init(mod, io, priv);
	if (ret < 0)
		return ret;

	ssiu->usrcnt++;

	/*
	 * TDM Extend/Split Mode
	 * see
	 *	rsnd_ssi_config_init()
	 */
	if (rsnd_runtime_is_tdm(io))
		mode = TDM_EXT;
	else if (rsnd_runtime_is_tdm_split(io))
		mode = TDM_SPLIT;

	rsnd_mod_write(mod, SSI_MODE, mode);

	if (rsnd_ssi_use_busif(io)) {
		int id = rsnd_mod_id(mod);
		int busif = rsnd_mod_id_sub(mod);
		enum rsnd_reg adinr_reg, mode_reg, dalign_reg;

		if ((id == 9) && (busif >= 4)) {
			adinr_reg = SSI9_BUSIF_ADINR(busif);
			mode_reg = SSI9_BUSIF_MODE(busif);
			dalign_reg = SSI9_BUSIF_DALIGN(busif);
		} else {
			adinr_reg = SSI_BUSIF_ADINR(busif);
			mode_reg = SSI_BUSIF_MODE(busif);
			dalign_reg = SSI_BUSIF_DALIGN(busif);
		}

		rsnd_mod_write(mod, adinr_reg,
			       rsnd_get_adinr_bit(mod, io) |
			       (rsnd_io_is_play(io) ?
				rsnd_runtime_channel_after_ctu(io) :
				rsnd_runtime_channel_original(io)));
		rsnd_mod_write(mod, mode_reg,
			       rsnd_get_busif_shift(io, mod) | 1);
		rsnd_mod_write(mod, dalign_reg,
			       rsnd_get_dalign(mod, io));
	}

	if (has_hdmi0 || has_hdmi1) {
		enum rsnd_mod_type rsnd_ssi_array[] = {
			RSND_MOD_SSIM1,
			RSND_MOD_SSIM2,
			RSND_MOD_SSIM3,
		};
		struct rsnd_mod *ssi_mod = rsnd_io_to_mod_ssi(io);
		struct rsnd_mod *pos;
		u32 val;
		int i;

		i = rsnd_mod_id(ssi_mod);

		/* output all same SSI as default */
		val =	i << 16 |
			i << 20 |
			i << 24 |
			i << 28 |
			i;

		for_each_rsnd_mod_array(i, pos, io, rsnd_ssi_array) {
			int shift = (i * 4) + 20;

			val	= (val & ~(0xF << shift)) |
				rsnd_mod_id(pos) << shift;
		}

		if (has_hdmi0)
			rsnd_mod_write(mod, HDMI0_SEL, val);
		if (has_hdmi1)
			rsnd_mod_write(mod, HDMI1_SEL, val);
	}

	return 0;
}

static int rsnd_ssiu_start_gen2(struct rsnd_mod *mod,
				struct rsnd_dai_stream *io,
				struct rsnd_priv *priv)
{
	int busif = rsnd_mod_id_sub(mod);

	if (!rsnd_ssi_use_busif(io))
		return 0;

	rsnd_mod_bset(mod, SSI_CTRL, 1 << (busif * 4), 1 << (busif * 4));

	if (rsnd_ssi_multi_secondaries_runtime(io))
		rsnd_mod_write(mod, SSI_CONTROL, 0x1);

	return 0;
}

static int rsnd_ssiu_stop_gen2(struct rsnd_mod *mod,
			       struct rsnd_dai_stream *io,
			       struct rsnd_priv *priv)
{
	struct rsnd_ssiu *ssiu = rsnd_mod_to_ssiu(mod);
	int busif = rsnd_mod_id_sub(mod);

	if (!rsnd_ssi_use_busif(io))
		return 0;

	rsnd_mod_bset(mod, SSI_CTRL, 1 << (busif * 4), 0);

	if (--ssiu->usrcnt)
		return 0;

	if (rsnd_ssi_multi_secondaries_runtime(io))
		rsnd_mod_write(mod, SSI_CONTROL, 0);

	return 0;
}

static int rsnd_ssiu_id(struct rsnd_mod *mod)
{
	struct rsnd_ssiu *ssiu = rsnd_mod_to_ssiu(mod);

	/* see rsnd_ssiu_probe() */
	return ssiu->id;
}

static int rsnd_ssiu_id_sub(struct rsnd_mod *mod)
{
	struct rsnd_ssiu *ssiu = rsnd_mod_to_ssiu(mod);

	/* see rsnd_ssiu_probe() */
	return ssiu->id_sub;
}

static struct dma_chan *rsnd_ssiu_dma_req(struct rsnd_dai_stream *io,
					  struct rsnd_mod *mod)
{
	struct rsnd_priv *priv = rsnd_mod_to_priv(mod);
	int is_play = rsnd_io_is_play(io);
	char *name;

	/*
	 * It should use "rcar_sound,ssiu" on DT.
	 * But, we need to keep compatibility for old version.
	 *
	 * If it has "rcar_sound.ssiu", it will be used.
	 * If not, "rcar_sound.ssi" will be used.
	 * see
	 *	rsnd_ssi_dma_req()
	 *	rsnd_dma_of_path()
	 */

	name = is_play ? "rx" : "tx";

	return rsnd_dma_request_channel(rsnd_ssiu_of_node(priv),
					SSIU_NAME, mod, name);
}

#ifdef CONFIG_DEBUG_FS
static void rsnd_ssiu_debug_info(struct seq_file *m,
				 struct rsnd_dai_stream *io,
				struct rsnd_mod *mod)
{
	rsnd_debugfs_mod_reg_show(m, mod, RSND_BASE_SSIU,
				  rsnd_mod_id(mod) * 0x80, 0x80);
}
#define DEBUG_INFO .debug_info = rsnd_ssiu_debug_info
#else
#define DEBUG_INFO
#endif

static struct rsnd_mod_ops rsnd_ssiu_ops_gen2 = {
	.name		= SSIU_NAME,
	.dma_req	= rsnd_ssiu_dma_req,
	.init		= rsnd_ssiu_init_gen2,
	.quit		= rsnd_ssiu_quit,
	.start		= rsnd_ssiu_start_gen2,
	.stop		= rsnd_ssiu_stop_gen2,
	.get_status	= rsnd_ssiu_get_status,
	DEBUG_INFO
};

static struct rsnd_mod *rsnd_ssiu_mod_get(struct rsnd_priv *priv, int id)
{
	if (WARN_ON(id < 0 || id >= rsnd_ssiu_nr(priv)))
		id = 0;

	return rsnd_mod_get((struct rsnd_ssiu *)(priv->ssiu) + id);
}

static void rsnd_parse_connect_ssiu_compatible(struct rsnd_priv *priv,
					       struct rsnd_dai_stream *io)
{
	struct rsnd_mod *ssi_mod = rsnd_io_to_mod_ssi(io);
	struct rsnd_ssiu *ssiu;
	int is_dma_mode;
	int i;

	if (!ssi_mod)
		return;

	is_dma_mode = rsnd_ssi_is_dma_mode(ssi_mod);

	/* select BUSIF0 */
	for_each_rsnd_ssiu(ssiu, priv, i) {
		struct rsnd_mod *mod = rsnd_mod_get(ssiu);

		if (is_dma_mode &&
		    (rsnd_mod_id(ssi_mod) == rsnd_mod_id(mod)) &&
		    (rsnd_mod_id_sub(mod) == 0)) {
			rsnd_dai_connect(mod, io, mod->type);
			return;
		}
	}
}

void rsnd_parse_connect_ssiu(struct rsnd_dai *rdai,
			     struct device_node *playback,
			     struct device_node *capture)
{
	struct rsnd_priv *priv = rsnd_rdai_to_priv(rdai);
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *node = rsnd_ssiu_of_node(priv);
	struct rsnd_dai_stream *io_p = &rdai->playback;
	struct rsnd_dai_stream *io_c = &rdai->capture;

	/* use rcar_sound,ssiu if exist */
	if (node) {
		int i = 0;

		for_each_child_of_node_scoped(node, np) {
			struct rsnd_mod *mod;

			i = rsnd_node_fixed_index(dev, np, SSIU_NAME, i);
			if (i < 0)
				break;

			mod = rsnd_ssiu_mod_get(priv, i);

			if (np == playback)
				rsnd_dai_connect(mod, io_p, mod->type);
			if (np == capture)
				rsnd_dai_connect(mod, io_c, mod->type);
			i++;
		}

		of_node_put(node);
	}

	/* Keep DT compatibility */
	if (!rsnd_io_to_mod_ssiu(io_p))
		rsnd_parse_connect_ssiu_compatible(priv, io_p);
	if (!rsnd_io_to_mod_ssiu(io_c))
		rsnd_parse_connect_ssiu_compatible(priv, io_c);
}

int rsnd_ssiu_probe(struct rsnd_priv *priv)
{
	struct device *dev = rsnd_priv_to_dev(priv);
	struct device_node *node;
	struct rsnd_ssiu *ssiu;
	struct rsnd_mod_ops *ops;
	const int *list = NULL;
	int i, nr;

	/*
	 * Keep DT compatibility.
	 * if it has "rcar_sound,ssiu", use it.
	 * if not, use "rcar_sound,ssi"
	 * see
	 *	rsnd_ssiu_bufsif_to_id()
	 */
	node = rsnd_ssiu_of_node(priv);
	if (node)
		nr = rsnd_node_count(priv, node, SSIU_NAME);
	else
		nr = priv->ssi_nr;

	if (!nr)
		return -EINVAL;

	ssiu	= devm_kcalloc(dev, nr, sizeof(*ssiu), GFP_KERNEL);
	if (!ssiu)
		return -ENOMEM;

	priv->ssiu	= ssiu;
	priv->ssiu_nr	= nr;

	if (rsnd_is_gen1(priv))
		ops = &rsnd_ssiu_ops_gen1;
	else
		ops = &rsnd_ssiu_ops_gen2;

	/* Keep compatibility */
	nr = 0;
	if ((node) &&
	    (ops == &rsnd_ssiu_ops_gen2)) {
		ops->id		= rsnd_ssiu_id;
		ops->id_sub	= rsnd_ssiu_id_sub;

		if (rsnd_is_gen2(priv)) {
			list	= gen2_id;
			nr	= ARRAY_SIZE(gen2_id);
		} else if (rsnd_is_gen3(priv)) {
			list	= gen3_id;
			nr	= ARRAY_SIZE(gen3_id);
		} else if (rsnd_is_gen4(priv)) {
			list	= gen4_id;
			nr	= ARRAY_SIZE(gen4_id);
		} else {
			dev_err(dev, "unknown SSIU\n");
			return -ENODEV;
		}
	}

	for_each_rsnd_ssiu(ssiu, priv, i) {
		int ret;

		if (node) {
			int j;

			/*
			 * see
			 *	rsnd_ssiu_get_id()
			 *	rsnd_ssiu_get_id_sub()
			 */
			for (j = 0; j < nr; j++) {
				if (list[j] > i)
					break;
				ssiu->id	= j;
				ssiu->id_sub	= i - list[ssiu->id];
			}
		} else {
			ssiu->id = i;
		}

		ret = rsnd_mod_init(priv, rsnd_mod_get(ssiu),
				    ops, NULL, RSND_MOD_SSIU, i);
		if (ret)
			return ret;
	}

	return 0;
}

void rsnd_ssiu_remove(struct rsnd_priv *priv)
{
	struct rsnd_ssiu *ssiu;
	int i;

	for_each_rsnd_ssiu(ssiu, priv, i) {
		rsnd_mod_quit(rsnd_mod_get(ssiu));
	}
}
