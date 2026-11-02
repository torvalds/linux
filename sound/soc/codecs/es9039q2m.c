// SPDX-License-Identifier: GPL-2.0-only
//
// ESS Technology ES9039Q2M 32-bit 2-channel audio DAC
//
// Copyright (C) 2026 Karl Asseily <karl@asseily.com>
//
// Every register number, bit field and default in this file was taken from
// ES9039Q2M datasheet v0.2.3 and then verified by reading the defaults back off
// a live part over I2C.
//
// The part has two control personalities selected by the MODE pin: hardware
// mode (strapped by HW0/HW1/HW2, no bus at all) and software mode (I2C or SPI).
// This driver implements software mode over I2C, which MODE = GND selects.
//
// Three properties shape the driver:
//
//  - There is an ASRC in front of the DAC, so MCLK need not be synchronous with
//    BCLK or LRCK. A board may feed it a fixed oscillator, or may run it
//    synchronously and let the part generate BCLK and WS; both are supported.
//
//  - INPUT_SEL chooses PCM / DSD / DoP / S/PDIF. It does NOT choose I2S vs
//    left-justified - that is TDM_LJ_MODE in register 60, and there is no
//    right-justified mode to map onto at all.
//
//  - Several registers have non-zero reserved defaults (register 88 reads
//    0xb8 at reset), so every write here is read-modify-write.

#include <linux/bitfield.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/* ------------------------------------------------- read/write registers ---- */

#define ES9039_SYSTEM_CONFIG		0x00	/* reg 0 */
#define   ES9039_DAC_MODE		BIT(1)	/* CLEAR at reset - datapath off */
#define   ES9039_64FS_MODE		BIT(6)

/*
 * Register 1 selects which DECODERS are running, and it is separate from
 * INPUT_SEL in register 57, which only says which port to listen to. Both are
 * needed: on reset only ENABLE_TDM_DECODE is set, so selecting DoP as the input
 * while leaving bit 2 clear leaves the part hunting for a marker with the
 * marker decoder switched off. It then finds no valid DoP and mutes - silence,
 * DOP_VALID reading 0, and nothing anywhere saying why. Measured on hardware.
 */
#define ES9039_SYS_MODE			0x01	/* reg 1, reset 0xb1 */
#define   ES9039_ENABLE_TDM_DECODE	BIT(0)	/* set at reset */
#define   ES9039_ENABLE_DSD_DECODE	BIT(1)
#define   ES9039_ENABLE_DOP_DECODE	BIT(2)
#define   ES9039_ENABLE_SPDIF_DECODE	BIT(3)
#define   ES9039_SYNC_MODE		BIT(6)	/* 0 = ASYNC */
#define   ES9039_ENABLE_DAC_CLK		BIT(7)	/* set at reset */
/*
 * Every decoder the part has. hw_params() enables one of these and clears the
 * rest, so leaving S/PDIF out of the mask would have left its decoder running
 * alongside the selected one.
 */
#define   ES9039_DECODE_MASK		(ES9039_ENABLE_TDM_DECODE | \
					 ES9039_ENABLE_DSD_DECODE | \
					 ES9039_ENABLE_DOP_DECODE | \
					 ES9039_ENABLE_SPDIF_DECODE)

#define ES9039_AUTO_FS_DETECT		0x03	/* reg 3 */
#define   ES9039_AUTO_FS_DETECT_EN	BIT(7)

#define ES9039_CLOCK_CONFIG		0x04	/* reg 4, MASTER_BCK_DIV */

#define ES9039_INPUT_SEL		0x39	/* reg 57 */
#define   ES9039_AUTO_INPUT_SEL		BIT(0)
#define   ES9039_INPUT_SEL_MASK		GENMASK(2, 1)
#define     ES9039_INPUT_PCM		0x0
#define     ES9039_INPUT_DSD		0x1
#define     ES9039_INPUT_DOP		0x2
#define     ES9039_INPUT_SPDIF		0x3
#define   ES9039_PCM_MASTER_MODE	BIT(4)
#define   ES9039_DSD_MASTER_MODE	BIT(5)
#define   ES9039_DSD_FAULT_DETECT	BIT(6)	/* set at reset */

#define ES9039_MASTER_ENC		0x3a	/* reg 58 */
#define   ES9039_BCK_INV		BIT(6)

#define ES9039_TDM_CH_NUM		0x3b	/* reg 59, slots = value + 1 */
#define   ES9039_TDM_CH_NUM_MASK	GENMASK(4, 0)

#define ES9039_TDM_CONFIG1		0x3c	/* reg 60 */
#define   ES9039_TDM_VALID_EDGE		BIT(6)
#define   ES9039_TDM_LJ_MODE		BIT(7)	/* 0 = standard I2S */

#define ES9039_TDM_CONFIG2		0x3d	/* reg 61 */
#define   ES9039_TDM_BIT_WIDTH_MASK	GENMASK(6, 5)
#define     ES9039_WIDTH_32		0x0
#define     ES9039_WIDTH_24		0x1
#define     ES9039_WIDTH_16		0x2

#define ES9039_MONITOR_CFG		0x3e	/* reg 62 */
#define   ES9039_DISABLE_PCM_DC		BIT(3)
#define   ES9039_ENABLE_BCK_MONITOR	BIT(4)	/* set at reset */
#define   ES9039_ENABLE_WS_MONITOR	BIT(5)	/* set at reset */
#define   ES9039_DISABLE_DSD_MUTE	BIT(6)
#define   ES9039_DISABLE_DSD_DC		BIT(7)

#define ES9039_VOLUME_CH1		0x4a	/* reg 74, 0x00 = 0 dB */
#define ES9039_VOLUME_CH2		0x4b	/* reg 75, 0xff = -127.5 dB */
#define   ES9039_VOL_MAX		0xff

#define ES9039_VOL_RATE_UP		0x52	/* reg 82 */
#define ES9039_VOL_RATE_DOWN		0x53	/* reg 83 */

#define ES9039_DAC_MUTE			0x56	/* reg 86, 1 = muted */
#define   ES9039_MUTE_CH1		BIT(0)
#define   ES9039_MUTE_CH2		BIT(1)
#define   ES9039_MUTE_BOTH		(ES9039_MUTE_CH1 | ES9039_MUTE_CH2)

#define ES9039_DAC_INVERT		0x57	/* reg 87 */

#define ES9039_FILTER_SHAPE		0x58	/* reg 88, [7:3] reset to 10111 */
#define   ES9039_FILTER_SHAPE_MASK	GENMASK(2, 0)
#define     ES9039_FILTER_APODIZING	1	/* linear phase apodizing fast */

#define ES9039_IIR_SPDIF		0x59	/* reg 89 */
#define   ES9039_IIR_BW_MASK		GENMASK(2, 0)
#define   ES9039_VOLUME_HOLD		BIT(3)
#define   ES9039_SPDIF_SEL_MASK		GENMASK(7, 4)

#define ES9039_DAC_PATH			0x5a	/* reg 90 */
#define   ES9039_BYPASS_FIR2X		BIT(0)
#define   ES9039_BYPASS_FIR4X		BIT(1)
#define   ES9039_BYPASS_IIR		BIT(2)

#define ES9039_THD_C2			0x5b	/* regs 91-94: CH1 lo, CH2 hi */
#define ES9039_THD_C3			0x6b	/* regs 107-110 */

#define ES9039_AUTOMUTE_EN		0x7b	/* reg 123, both set at reset */
#define ES9039_AUTOMUTE_TIME		0x7c	/* regs 124-125 */
#define   ES9039_AUTOMUTE_TIME_MASK	GENMASK(10, 0)
#define   ES9039_MUTE_RAMP_TO_GND	BIT(11)	/* set at reset */
#define ES9039_AUTOMUTE_LEVEL		0x7e	/* regs 126-127 */
#define ES9039_AUTOMUTE_OFF_LEVEL	0x80	/* regs 128-129 */

#define ES9039_SOFT_RAMP		0x82	/* reg 130, valid 0..12 */
#define   ES9039_SOFT_RAMP_MASK		GENMASK(4, 0)
#define   ES9039_SOFT_RAMP_MAX		12

#define ES9039_NSMOD			0x83	/* reg 131 */
#define   ES9039_NSMOD_WIDE_BW_MASK	GENMASK(4, 1)
#define     ES9039_NSMOD_DEFAULT	0x4
#define     ES9039_NSMOD_WIDE		0xc

#define ES9039_PROG_RAM_CTRL		0x87	/* reg 135 */
#define   ES9039_PROG_COEFF_EN		BIT(0)
#define   ES9039_PROG_COEFF_WE		BIT(1)

#define ES9039_PROG_RAM_ADDR		0x89	/* reg 137 */
#define   ES9039_PROG_ADDR_MASK		GENMASK(6, 0)
#define   ES9039_PROG_STAGE_4X		BIT(7)

#define ES9039_PROG_RAM_DATA		0x8a	/* regs 138-140, 24-bit signed */

#define ES9039_LAST_RW			0x8e	/* reg 145 */

/* ----------------------------------------------------- readback registers -- */

#define ES9039_READBACK_BASE		0xe0	/* reg 224 */

#define ES9039_CHIP_ID			0xe1	/* reg 225 */
#define   ES9039_CHIP_ID_ES9039Q2M	0x63

#define ES9039_IRQ_SOURCES		0xea	/* regs 234-235, 16-bit */
#define   ES9039_SRC_VOL_MIN_MASK	GENMASK(1, 0)
#define   ES9039_SRC_AUTOMUTE_MASK	GENMASK(3, 2)
#define   ES9039_SRC_SS_RAMP_MASK	GENMASK(5, 4)
#define   ES9039_SRC_DOP_VALID		BIT(6)
#define   ES9039_SRC_BCK_WS_FAIL	BIT(7)
#define   ES9039_SRC_TDM_VALID		BIT(11)

#define ES9039_AUTO_FS_READ		0xef	/* reg 239 */
#define   ES9039_FS_DIV_MASK		GENMASK(5, 0)
#define   ES9039_FS_HALF_DIV		BIT(6)
#define   ES9039_FS_DIV_VALID		BIT(7)

#define ES9039_AUTOMUTE_READ		0xf2	/* reg 242 */

#define ES9039_INPUT_STREAM_READ	0xf5	/* reg 245 */
#define   ES9039_RD_INPUT_SEL_MASK	GENMASK(1, 0)
#define   ES9039_RD_DOP_VALID		BIT(2)
#define   ES9039_RD_TDM_VALID		BIT(3)
#define   ES9039_RD_SPDIF_VALID		BIT(4)

#define ES9039_MAX_REGISTER		0xfb	/* reg 251 */

/* Programmable oversampling FIR: 128 taps in the 2x stage, 32 in the 4x. */
#define ES9039_FIR2X_TAPS		128
#define ES9039_FIR4X_TAPS		32
#define ES9039_COEFF_BYTES		3
#define ES9039_FIR_STAGES		2	/* [0] = 2x stage, [1] = 4x */

/* In power-up order. Reversed on the way down. Datasheet figure 22. */
#define ES9039_NUM_SUPPLIES		4
#define ES9039_FIR_MAX_BYTES		(ES9039_FIR2X_TAPS * ES9039_COEFF_BYTES)

/* ------------------------------------------------------------------ private */

struct es9039q2m_priv {
	struct regmap *regmap;
	struct clk *mclk;
	unsigned int mclk_rate;
	unsigned int fmt;
	unsigned int stream_rate;	/* last rate from hw_params */
	unsigned int bclk_ratio;	/* bit clocks per frame, 0 = unknown */

	/*
	 * Serialises dop_auto against hw_params(), which reads it while
	 * deciding what to program. Without it a control write racing a
	 * stream start can leave the two DoP registers disagreeing.
	 */
	struct mutex lock;
	bool dop_auto;		/* let the part detect DoP itself */

	/*
	 * ES9039_DAC_MUTE has two owners and neither can hold it alone: the
	 * "Master Playback Switch" control, and mute_stream() around stream
	 * start and stop. Both record what they want here and the register is
	 * written from the union, so a user unmute cannot un-mute a stopped
	 * stream and a stream start cannot override a user mute.
	 *
	 * mute_stream starts FALSE and only a real mute_stream(1) sets it.
	 * Starting it true seemed the safe choice - come up muted - but it is
	 * not: if a card never reaches the DAI mute callbacks, the flag never
	 * clears, and because the control now records intent rather than
	 * writing the register there is nothing the user can do about it. The
	 * DAC is then muted for ever and the mixer looks fine. Measured on
	 * hardware: silent with the switch on, off, stream or no stream. Pop
	 * suppression at probe comes from writing the register
	 * directly instead, which is what the driver did before this rework
	 * and is a state either owner may legitimately lift.
	 */
	unsigned int mute_user;	/* ES9039_MUTE_CH* the user asked for */
	bool mute_stream;	/* ASoC has the stream muted */

	bool provider;		/* part drives BCK and WS */
	bool mclk_fixed;	/* MCLK rate cannot be changed, so ASRC it is */

	/*
	 * The four supply inputs, held IN POWER-UP ORDER - the array order is
	 * the datasheet's sequence and the enable loop walks it forwards, the
	 * disable loop backwards. DVDD is not among them: the part's 1.2 V
	 * digital rail is generated on chip and that pin wants a decoupling
	 * capacitor, not a regulator.
	 */
	struct regulator_bulk_data supplies[ES9039_NUM_SUPPLIES];

	/*
	 * Shadow of whatever was last uploaded to the programmable FIR RAM.
	 *
	 * That RAM cannot be read back - see the upload code below - so regmap
	 * cannot cache it and nothing else in the system knows its contents.
	 * A board that removes the part's supplies in system suspend therefore
	 * comes back with the RAM undefined while regcache_sync() restores the
	 * filter SELECTION from the cache, which would point the interpolator
	 * at whatever the RAM powered up holding. Keep a copy and re-upload.
	 */
	u8 fir_shadow[ES9039_FIR_STAGES][ES9039_FIR_MAX_BYTES];
	unsigned int fir_taps[ES9039_FIR_STAGES];	/* 0 = never uploaded */
};

/*
 * Multi-byte fields are little-endian across ascending register addresses:
 * register N holds bits [7:0], N+1 holds [15:8], and so on.
 */
static int es9039_read_le(struct regmap *map, unsigned int reg, int n, u32 *out)
{
	u8 buf[4];
	int ret, i;

	ret = regmap_bulk_read(map, reg, buf, n);
	if (ret)
		return ret;

	*out = 0;
	for (i = 0; i < n; i++)
		*out |= (u32)buf[i] << (8 * i);

	return 0;
}

static int es9039_write_le(struct regmap *map, unsigned int reg, int n, u32 val)
{
	u8 buf[4];
	int i;

	for (i = 0; i < n; i++)
		buf[i] = (val >> (8 * i)) & 0xff;

	return regmap_bulk_write(map, reg, buf, n);
}

/* ------------------------------------------------------------------ regmap */

static bool es9039q2m_writeable_reg(struct device *dev, unsigned int reg)
{
	return reg <= ES9039_LAST_RW;
}

static bool es9039q2m_readable_reg(struct device *dev, unsigned int reg)
{
	return reg <= ES9039_LAST_RW || reg >= ES9039_READBACK_BASE;
}

static bool es9039q2m_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg >= ES9039_READBACK_BASE;
}

static const struct regmap_config es9039q2m_regmap = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= ES9039_MAX_REGISTER,
	.writeable_reg	= es9039q2m_writeable_reg,
	.readable_reg	= es9039q2m_readable_reg,
	.volatile_reg	= es9039q2m_volatile_reg,
	.cache_type	= REGCACHE_MAPLE,
};

/* --------------------------------------------------- signed 16-bit controls */

struct es9039_s16_ctl {
	unsigned int reg;
	unsigned int shift;	/* 0 for CH1, 16 for CH2 within the 32-bit pair */
};

static int es9039_s16_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = S16_MIN;
	uinfo->value.integer.max = S16_MAX;
	return 0;
}

static int es9039_s16_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct es9039_s16_ctl *p = (void *)kcontrol->private_value;
	u32 v;
	int ret;

	ret = es9039_read_le(priv->regmap, p->reg + (p->shift / 8), 2, &v);
	if (ret)
		return ret;

	ucontrol->value.integer.value[0] = (s16)v;
	return 0;
}

static int es9039_s16_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct es9039_s16_ctl *p = (void *)kcontrol->private_value;
	long v = ucontrol->value.integer.value[0];
	u32 old;
	int ret;

	if (v < S16_MIN || v > S16_MAX)
		return -EINVAL;

	ret = es9039_read_le(priv->regmap, p->reg + (p->shift / 8), 2, &old);
	if (ret)
		return ret;

	if ((s16)old == (s16)v)
		return 0;

	ret = es9039_write_le(priv->regmap, p->reg + (p->shift / 8), 2,
			      (u16)v);
	if (ret)
		return ret;

	return 1;
}

#define ES9039_S16(xname, xreg, xshift)					\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= xname,						\
	.info	= es9039_s16_info,					\
	.get	= es9039_s16_get,					\
	.put	= es9039_s16_put,					\
	.private_value = (unsigned long)&(struct es9039_s16_ctl)	\
			 { .reg = xreg, .shift = xshift },		\
}

/* ------------------------------------------------- multi-register integers */

struct es9039_wide_ctl {
	unsigned int reg;
	unsigned int bytes;
	unsigned int mask;
	unsigned int max;
};

static int es9039_wide_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct es9039_wide_ctl *p = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = p->max;
	return 0;
}

static int es9039_wide_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct es9039_wide_ctl *p = (void *)kcontrol->private_value;
	u32 v;
	int ret;

	ret = es9039_read_le(priv->regmap, p->reg, p->bytes, &v);
	if (ret)
		return ret;

	ucontrol->value.integer.value[0] = v & p->mask;
	return 0;
}

static int es9039_wide_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct es9039_wide_ctl *p = (void *)kcontrol->private_value;
	long v = ucontrol->value.integer.value[0];
	u32 old;
	int ret, i;

	if (v < 0 || v > p->max)
		return -EINVAL;

	ret = es9039_read_le(priv->regmap, p->reg, p->bytes, &old);
	if (ret)
		return ret;

	if ((old & p->mask) == (u32)v)
		return 0;

	/*
	 * Write one register at a time through regmap_update_bits() rather
	 * than reading the whole field and writing it back.
	 *
	 * The bits outside the field are not ours: ES9039_AUTOMUTE_TIME spans
	 * registers 124-125 while MUTE_RAMP_TO_GND is bit 3 of register 125
	 * and has a control of its own. A read-modify-write over the pair
	 * leaves a window in which that other control can write between our
	 * read and our write, and its change is then lost - the ALSA put path
	 * holds card->controls_rwsem for READ, so two controls really can run
	 * at once. Per-register update_bits closes the window inside regmap's
	 * own lock and never touches a bit outside p->mask.
	 */
	for (i = 0; i < p->bytes; i++) {
		u8 mask = (p->mask >> (8 * i)) & 0xff;

		if (!mask)
			continue;

		ret = regmap_update_bits(priv->regmap, p->reg + i, mask,
					 ((u32)v >> (8 * i)) & mask);
		if (ret)
			return ret;
	}

	return 1;
}

#define ES9039_WIDE(xname, xreg, xbytes, xmask, xmax)			\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= xname,						\
	.info	= es9039_wide_info,					\
	.get	= es9039_wide_get,					\
	.put	= es9039_wide_put,					\
	.private_value = (unsigned long)&(struct es9039_wide_ctl)	\
			 { .reg = xreg, .bytes = xbytes,		\
			   .mask = xmask, .max = xmax },		\
}

/* ------------------------------------------------ programmable FIR upload --
 *
 * Write-only, deliberately. The chip has a PROG_COEFF_OUT register (248-246)
 * described only as "Programmable FIR coefficient readback", but it is not a
 * RAM read port: it returns the LAST COEFFICIENT WRITTEN, whatever address is
 * selected in PROG_COEFF_ADDR. Measured over raw I2C with this driver out of
 * the path - two different coefficients written to
 * addresses 0 and 1, then read back with five different sequences (plain, with
 * a settle delay, with PROG_COEFF_EN set, with the address written twice, and
 * with a WE pulse after the address). All ten reads returned the value written
 * to address 1.
 *
 * A get() built on that register would return something with the shape of data
 * and none of its meaning, so there is no get(). If ESS documents a real
 * readback sequence, add one.
 */

/*
 * Per-control data rides in our own struct with the soc_bytes_ext EMBEDDED,
 * recovered by container_of. Not in soc_bytes_ext.dobj: that field belongs to
 * the topology subsystem and only exists under CONFIG_SND_SOC_TOPOLOGY, so a
 * driver stashing its own data there fails to build on any config without it.
 */
struct es9039_fir_ctl {
	struct soc_bytes_ext be;
	unsigned int taps;
	bool stage_4x;
};

/*
 * Push one stage's coefficients into the RAM. Callers hold priv->lock: both
 * FIR controls drive the same address, data and strobe registers, so two
 * uploads at once would interleave into each other's RAM, and resume re-uploads
 * through this same path.
 *
 * PROG_COEFF_WE is a per-coefficient strobe, not a gate held open across the
 * upload. The datasheet's sequence is address, data, raise WE, lower WE, once
 * per coefficient. Holding it high for the whole loop also appears to work on
 * ES9039Q2M silicon, but "appears to work" is not a specification.
 */
static int es9039_fir_upload(struct es9039q2m_priv *priv, const u8 *data,
			     unsigned int taps, bool stage_4x)
{
	unsigned int i;
	int ret = 0;

	for (i = 0; i < taps; i++) {
		ret = regmap_write(priv->regmap, ES9039_PROG_RAM_ADDR,
				   (stage_4x ? ES9039_PROG_STAGE_4X : 0) |
				   FIELD_PREP(ES9039_PROG_ADDR_MASK, i));
		if (ret)
			goto out;

		ret = regmap_bulk_write(priv->regmap, ES9039_PROG_RAM_DATA,
					&data[i * ES9039_COEFF_BYTES],
					ES9039_COEFF_BYTES);
		if (ret)
			goto out;

		ret = regmap_update_bits(priv->regmap, ES9039_PROG_RAM_CTRL,
					 ES9039_PROG_COEFF_WE,
					 ES9039_PROG_COEFF_WE);
		if (ret)
			goto out;

		ret = regmap_update_bits(priv->regmap, ES9039_PROG_RAM_CTRL,
					 ES9039_PROG_COEFF_WE, 0);
		if (ret)
			goto out;
	}

out:
	regmap_update_bits(priv->regmap, ES9039_PROG_RAM_CTRL,
			   ES9039_PROG_COEFF_WE, 0);
	return ret;
}

static int es9039_fir_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *bytes, unsigned int size)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct soc_bytes_ext *be = (void *)kcontrol->private_value;
	struct es9039_fir_ctl *p = container_of(be, struct es9039_fir_ctl, be);
	unsigned int idx = p->stage_4x ? 1 : 0;
	u8 *buf;
	int ret;

	if (size != p->taps * ES9039_COEFF_BYTES)
		return -EINVAL;

	buf = memdup_user(bytes, size);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	scoped_guard(mutex, &priv->lock) {
		ret = es9039_fir_upload(priv, buf, p->taps, p->stage_4x);
		if (!ret) {
			/*
			 * Keep a copy. The RAM cannot be read back, so this is
			 * the only record of what is in it, and resume has
			 * nothing else to restore from.
			 */
			memcpy(priv->fir_shadow[idx], buf, size);
			priv->fir_taps[idx] = p->taps;
		}
	}

	kfree(buf);
	if (ret)
		return ret;

	return 1;
}

static struct es9039_fir_ctl es9039_fir2x = {
	.be = { .max = ES9039_FIR2X_TAPS * ES9039_COEFF_BYTES,
		.put = es9039_fir_put },
	.taps = ES9039_FIR2X_TAPS,
};

static struct es9039_fir_ctl es9039_fir4x = {
	.be = { .max = ES9039_FIR4X_TAPS * ES9039_COEFF_BYTES,
		.put = es9039_fir_put },
	.taps = ES9039_FIR4X_TAPS,
	.stage_4x = true,
};

#define ES9039_FIR(xname, xctl)						\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= xname,						\
	.info	= snd_soc_bytes_info_ext,				\
	.tlv.c	= snd_soc_bytes_tlv_callback,				\
	.access	= SNDRV_CTL_ELEM_ACCESS_TLV_WRITE |			\
		  SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK,			\
	.private_value = (unsigned long)&(xctl).be,			\
}

/* ------------------------------------------------------- status (read-only) */

struct es9039_stat_ctl {
	unsigned int reg;
	unsigned int mask;
	unsigned int max;
};

static int es9039_stat_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct es9039_stat_ctl *p = (void *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = p->max;
	return 0;
}

static int es9039_stat_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	struct es9039_stat_ctl *p = (void *)kcontrol->private_value;
	unsigned int v;
	int ret;

	ret = regmap_read(priv->regmap, p->reg, &v);
	if (ret)
		return ret;

	ucontrol->value.integer.value[0] =
		(v & p->mask) >> (ffs(p->mask) - 1);
	return 0;
}

#define ES9039_STAT(xname, xreg, xmask, xmax)				\
{									\
	.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,				\
	.name	= xname,						\
	.access	= SNDRV_CTL_ELEM_ACCESS_READ |				\
		  SNDRV_CTL_ELEM_ACCESS_VOLATILE,			\
	.info	= es9039_stat_info,					\
	.get	= es9039_stat_get,					\
	.private_value = (unsigned long)&(struct es9039_stat_ctl)	\
			 { .reg = xreg, .mask = xmask, .max = xmax },	\
}

/*
 * Detected sample rate.
 *
 * When the part's own rate detector has a valid ratio, use it - it is measured
 * from the incoming frame clock and is the ground truth:
 *
 *   FS = Y * SYS_CLK / ((X + 1) * (128 >> 64FS_MODE))
 *
 * with X = IDAC_DIV_AUTO and Y = 2 when IDAC_HALF_DIV_AUTO reports a
 * half-integer multiple.
 *
 * That detector is UNAVAILABLE on any board running the DAC asynchronously -
 * register 3[7] AUTO_FS_DETECT carries the note "Cannot be used in ASYNC mode".
 * A board feeding a free-running oscillator and letting the ASRC absorb the
 * difference is precisely that case, and it is the preferable design, so the
 * detector reading 0 there is expected rather than a fault. Fall back to the
 * rate the stream was opened at, which is what a front panel wants to show.
 * Reports 0 only when nothing is playing and the chip has no lock either.
 */
static int es9039_rate_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1536000;
	return 0;
}

static int es9039_rate_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	unsigned int fsreg, sysreg, div, y, den;
	int ret;

	ucontrol->value.integer.value[0] = priv->stream_rate;

	if (!priv->mclk_rate)
		return 0;

	ret = regmap_read(priv->regmap, ES9039_AUTO_FS_READ, &fsreg);
	if (ret)
		return ret;

	if (!(fsreg & ES9039_FS_DIV_VALID))
		return 0;	/* async mode: keep the stream rate set above */

	ret = regmap_read(priv->regmap, ES9039_SYSTEM_CONFIG, &sysreg);
	if (ret)
		return ret;

	div = FIELD_GET(ES9039_FS_DIV_MASK, fsreg) + 1;
	y   = (fsreg & ES9039_FS_HALF_DIV) ? 2 : 1;
	den = div * ((sysreg & ES9039_64FS_MODE) ? 64 : 128);

	ucontrol->value.integer.value[0] =
		DIV_ROUND_CLOSEST(priv->mclk_rate * y, den);
	return 0;
}

static const char * const es9039_stream_texts[] = {
	"PCM", "DSD", "DoP", "S/PDIF",
};

static int es9039_stream_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(es9039_stream_texts),
				 es9039_stream_texts);
}

static int es9039_stream_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	unsigned int v;
	int ret;

	ret = regmap_read(priv->regmap, ES9039_INPUT_STREAM_READ, &v);
	if (ret)
		return ret;

	ucontrol->value.enumerated.item[0] =
		FIELD_GET(ES9039_RD_INPUT_SEL_MASK, v);
	return 0;
}

/* ----------------------------------------------------------------- controls */

static const DECLARE_TLV_DB_SCALE(es9039_vol_tlv, -12750, 50, 1);

static const char * const es9039_filter_texts[] = {
	"Minimum Phase",
	"Linear Phase Apodizing Fast Roll-Off",
	"Linear Phase Fast Roll-Off",
	"Linear Phase Fast Roll-Off Low Ripple",
	"Linear Phase Slow Roll-Off",
	"Minimum Phase Fast Roll-Off",
	"Minimum Phase Slow Roll-Off",
	"Minimum Phase Slow Roll-Off Low Dispersion",
};

static SOC_ENUM_SINGLE_DECL(es9039_filter_enum, ES9039_FILTER_SHAPE, 0,
			    es9039_filter_texts);

/* IIR_BW is a multiple of the datapath bandwidth, not a frequency. */
static const char * const es9039_iir_texts[] = {
	"Reserved", "BW x8", "BW x4", "BW x2", "BW", "BW /2", "BW /4", "BW /8",
};

static SOC_ENUM_SINGLE_DECL(es9039_iir_enum, ES9039_IIR_SPDIF, 0,
			    es9039_iir_texts);

static const char * const es9039_nsmod_texts[] = {
	"Default", "Wide Bandwidth",
};

static const unsigned int es9039_nsmod_values[] = {
	ES9039_NSMOD_DEFAULT, ES9039_NSMOD_WIDE,
};

static SOC_VALUE_ENUM_SINGLE_DECL(es9039_nsmod_enum, ES9039_NSMOD, 1,
				  GENMASK(3, 0), es9039_nsmod_texts,
				  es9039_nsmod_values);

static int es9039_dop_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = priv->dop_auto;
	return 0;
}

/*
 * Automatic DoP detection is two bits in two registers, and they are one
 * setting: AUTO_INPUT_SEL lets the part choose a decoder from the data, and
 * ENABLE_DOP_DECODE is what gives it a DoP decoder to choose. Enabling the
 * first without the second leaves the part hunting for a marker it has no
 * decoder for, whereupon it finds no valid DoP and mutes.
 *
 * Everything that changes the setting goes through here, so the pair can
 * never be written apart. Callers hold priv->lock.
 */
static int es9039_apply_dop(struct es9039q2m_priv *priv, bool on)
{
	int ret;

	ret = regmap_update_bits(priv->regmap, ES9039_SYS_MODE,
				 ES9039_ENABLE_DOP_DECODE,
				 on ? ES9039_ENABLE_DOP_DECODE : 0);
	if (ret)
		return ret;

	return regmap_update_bits(priv->regmap, ES9039_INPUT_SEL,
				  ES9039_AUTO_INPUT_SEL,
				  on ? ES9039_AUTO_INPUT_SEL : 0);
}

/* Caller holds priv->lock. */
static int es9039_apply_mute(struct es9039q2m_priv *priv)
{
	unsigned int val = priv->mute_user;

	if (priv->mute_stream)
		val = ES9039_MUTE_BOTH;

	return regmap_update_bits(priv->regmap, ES9039_DAC_MUTE,
				  ES9039_MUTE_BOTH, val);
}

/*
 * The control is inverted - 1 means playing - so a zero here is a mute
 * request. It reports priv->mute_user rather than the register, because the
 * register also carries the stream mute and the user did not ask for that.
 */
static int es9039_mute_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);

	guard(mutex)(&priv->lock);

	ucontrol->value.integer.value[0] = !(priv->mute_user & ES9039_MUTE_CH1);
	ucontrol->value.integer.value[1] = !(priv->mute_user & ES9039_MUTE_CH2);

	return 0;
}

static int es9039_mute_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	unsigned int val = 0, old;
	int ret;

	if (!ucontrol->value.integer.value[0])
		val |= ES9039_MUTE_CH1;
	if (!ucontrol->value.integer.value[1])
		val |= ES9039_MUTE_CH2;

	guard(mutex)(&priv->lock);

	if (val == priv->mute_user)
		return 0;

	/*
	 * Commit to the register first. If the write fails and the shadow has
	 * already moved, get() reports a mute the part is not in and the
	 * val == mute_user test above turns a retry with the same value into a
	 * no-op, leaving the control permanently wrong.
	 */
	old = priv->mute_user;
	priv->mute_user = val;

	ret = es9039_apply_mute(priv);
	if (ret) {
		priv->mute_user = old;
		return ret;
	}

	return 1;
}

static int es9039_dop_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	bool on = !!ucontrol->value.integer.value[0], old;
	int ret;

	guard(mutex)(&priv->lock);

	if (on == priv->dop_auto)
		return 0;

	/*
	 * hw_params() programs the decoder and INPUT_SEL from dop_auto, so
	 * changing it under a running stream would apply half the setting now
	 * and the rest at the next open. Refuse instead of half-applying.
	 */
	if (priv->stream_rate)
		return -EBUSY;

	/* Same reasoning as es9039_mute_put(): the shadow follows the write. */
	old = priv->dop_auto;
	priv->dop_auto = on;

	ret = es9039_apply_dop(priv, on);
	if (ret) {
		priv->dop_auto = old;
		return ret;
	}

	return 1;
}

/*
 * The two channel volumes live in separate registers, so a stereo change is
 * two I2C writes and the channels are briefly at different levels in between.
 * VOLUME_HOLD exists for exactly this: while it is set the part accepts writes
 * to registers 74-75 without applying them, and clearing it applies both at
 * once. Raise it, let the generic handler do the writes, drop it again.
 *
 * It is dropped unconditionally, including on the error path, because a stuck
 * VOLUME_HOLD would silently freeze the volume control.
 */
static int es9039_vol_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_kcontrol_chip(kcontrol);
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(c);
	int ret;

	/*
	 * The hold has to cover the pair of channel writes, so it has to be
	 * serialised. Two callers interleaving would let the second write its
	 * volumes after the first had already dropped VOLUME_HOLD, which is
	 * exactly the torn stereo update the bit exists to prevent.
	 */
	guard(mutex)(&priv->lock);

	ret = regmap_update_bits(priv->regmap, ES9039_IIR_SPDIF,
				 ES9039_VOLUME_HOLD, ES9039_VOLUME_HOLD);
	if (ret)
		return ret;

	ret = snd_soc_put_volsw(kcontrol, ucontrol);

	regmap_update_bits(priv->regmap, ES9039_IIR_SPDIF,
			   ES9039_VOLUME_HOLD, 0);

	return ret;
}

static const struct snd_kcontrol_new es9039q2m_controls[] = {
	/* --- level --- */
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Master Playback Volume",
		.access	= SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info	= snd_soc_info_volsw,
		.get	= snd_soc_get_volsw,
		.put	= es9039_vol_put,
		.tlv.p	= es9039_vol_tlv,
		.private_value = SOC_DOUBLE_R_VALUE(ES9039_VOLUME_CH1,
						    ES9039_VOLUME_CH2, 0, 0,
						    ES9039_VOL_MAX, 1),
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Master Playback Switch",
		.info	= snd_soc_info_volsw,
		.get	= es9039_mute_get,
		.put	= es9039_mute_put,
		.private_value = SOC_DOUBLE_VALUE(ES9039_DAC_MUTE, 0, 1, 0, 1,
						  1, 0),
	},
	SOC_DOUBLE("DAC Invert Switch", ES9039_DAC_INVERT, 0, 1, 1, 0),
	SOC_SINGLE("Volume Ramp Up Rate", ES9039_VOL_RATE_UP, 0, 255, 0),
	SOC_SINGLE("Volume Ramp Down Rate", ES9039_VOL_RATE_DOWN, 0, 255, 0),
	SOC_SINGLE("Soft Ramp Time", ES9039_SOFT_RAMP, 0,
		   ES9039_SOFT_RAMP_MAX, 0),

	/* --- reconstruction filter --- */
	SOC_ENUM("Filter Shape", es9039_filter_enum),
	SOC_ENUM("IIR Bandwidth", es9039_iir_enum),
	SOC_ENUM("Modulator Bandwidth", es9039_nsmod_enum),
	SOC_SINGLE("IIR Filter Bypass Switch", ES9039_DAC_PATH, 2, 1, 0),
	SOC_SINGLE("FIR 2x Bypass Switch", ES9039_DAC_PATH, 0, 1, 0),
	SOC_SINGLE("FIR 4x Bypass Switch", ES9039_DAC_PATH, 1, 1, 0),
	SOC_SINGLE("Custom FIR Switch", ES9039_PROG_RAM_CTRL, 0, 1, 0),
	ES9039_FIR("FIR 2x Coefficients", es9039_fir2x),
	ES9039_FIR("FIR 4x Coefficients", es9039_fir4x),

	/*
	 * Not "... Volume", because these do not set a level. They are signed
	 * correction coefficients for the second and third harmonic: they
	 * cancel distortion the analogue stage adds, and turning one up does
	 * not make anything louder. Useful values come from measuring a given
	 * board's distortion on an analyser and solving for them; zero, the
	 * reset value, is the only honest default until someone has.
	 */
	/* --- distortion compensation --- */
	ES9039_S16("THD Compensation C2 CH1", ES9039_THD_C2, 0),
	ES9039_S16("THD Compensation C2 CH2", ES9039_THD_C2, 16),
	ES9039_S16("THD Compensation C3 CH1", ES9039_THD_C3, 0),
	ES9039_S16("THD Compensation C3 CH2", ES9039_THD_C3, 16),

	/* --- automute --- */
	SOC_DOUBLE("Automute Switch", ES9039_AUTOMUTE_EN, 0, 1, 1, 0),
	ES9039_WIDE("Automute Time", ES9039_AUTOMUTE_TIME, 2,
		    ES9039_AUTOMUTE_TIME_MASK, 2047),
	ES9039_WIDE("Automute Level", ES9039_AUTOMUTE_LEVEL, 2, 0xffff, 65535),
	ES9039_WIDE("Automute Off Level", ES9039_AUTOMUTE_OFF_LEVEL, 2,
		    0xffff, 65535),
	SOC_SINGLE("Mute Ramp To Ground Switch", ES9039_AUTOMUTE_TIME + 1,
		   3, 1, 0),
	SOC_SINGLE("DSD DC Automute Switch", ES9039_MONITOR_CFG, 7, 1, 1),
	SOC_SINGLE("DSD Mute Pattern Switch", ES9039_MONITOR_CFG, 6, 1, 1),
	SOC_SINGLE("PCM DC Automute Switch", ES9039_MONITOR_CFG, 3, 1, 1),

	/* --- stream --- */
	/*
	 * On by default. DoP is designed to be detected, not announced: the
	 * player just sends it and a DoP-aware DAC notices the marker, which
	 * is why a DAC that does not notice plays it as near-silence rather
	 * than noise. Players rely on that, and none of them can reach into
	 * ALSA to flip a mode first.
	 *
	 * Left switchable because automatic detection is a pattern match, and
	 * anyone worried about PCM material that happens to look like DoP can
	 * turn it off and get strictly PCM.
	 */
	SOC_SINGLE_BOOL_EXT("DoP Auto Detect Switch", 0,
			    es9039_dop_get, es9039_dop_put),

	/* --- status, read-only --- */
	ES9039_STAT("Automute Active CH1", ES9039_AUTOMUTE_READ, BIT(0), 1),
	ES9039_STAT("Automute Active CH2", ES9039_AUTOMUTE_READ, BIT(1), 1),
	ES9039_STAT("DoP Valid", ES9039_INPUT_STREAM_READ,
		    ES9039_RD_DOP_VALID, 1),
	ES9039_STAT("TDM Data Valid", ES9039_INPUT_STREAM_READ,
		    ES9039_RD_TDM_VALID, 1),
	ES9039_STAT("SPDIF Valid", ES9039_INPUT_STREAM_READ,
		    ES9039_RD_SPDIF_VALID, 1),
	/*
	 * Register 235-234 is one 16-bit word and BCK_WS_FAIL_SOURCE is bit
	 * 7 of it, so it sits in the low byte at ES9039_IRQ_SOURCES. This
	 * read the byte above with the same mask, which is bit 15 - reserved
	 * - so the control could never have reported a fault. The named
	 * constant now carries the bit rather than a bare BIT(7).
	 *
	 * The flag is meaningful only with the BCK and WS monitors enabled,
	 * which they are at reset and the driver leaves alone.
	 */
	ES9039_STAT("Clock Fault", ES9039_IRQ_SOURCES,
		    ES9039_SRC_BCK_WS_FAIL, 1),
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Detected Sample Rate",
		.access	= SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info	= es9039_rate_info,
		.get	= es9039_rate_get,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		.name	= "Detected Input Format",
		.access	= SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info	= es9039_stream_info,
		.get	= es9039_stream_get,
	},
};

/* --------------------------------------------------------------------- DAPM */

static const struct snd_soc_dapm_widget es9039q2m_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("AOUTL"),
	SND_SOC_DAPM_OUTPUT("AOUTR"),
};

static const struct snd_soc_dapm_route es9039q2m_routes[] = {
	{ "DAC",   NULL, "Playback" },
	{ "AOUTL", NULL, "DAC" },
	{ "AOUTR", NULL, "DAC" },
};

/* ---------------------------------------------------------------------- DAI */

/*
 * MCLK limits, all from the datasheet:
 *
 *  - 50 MHz absolute maximum ("Max MCLK Frequency", electrical specification).
 *
 *  - Table 7 note 1: MCLK >= 128 * FS synchronous, MCLK > 130 * FS
 *    asynchronous. The gap is small but not academic - with a 24.576 MHz
 *    clock, 128 * FS puts 192 kHz exactly at the limit while 130 * FS puts the
 *    ceiling at 189 kHz. Note that the asynchronous bound is strict.
 *
 *  - Register 0[6] ENABLE_64FS_MODE runs the interpolation path at 64FS and is
 *    "used only for PCM high sample rates such as 768kHz with a 49.152MHz or
 *    384kHz with 24.576MHz clock" - both of which are MCLK = 64 * FS. Since
 *    128 * 768000 is 98.304 MHz, well over the ceiling, 64FS mode is the only
 *    way 705.6 and 768 kHz are reachable at all.
 */
#define ES9039_MAX_MCLK			50000000
#define ES9039_64FS_MCLK_FS		64
#define ES9039_SYNC_MIN_MCLK_FS		128
#define ES9039_ASYNC_MIN_MCLK_FS	130

/*
 * MCLK/FS ratios to try on a settable clock, highest first. 256 leaves the
 * part inside the window its automatic clock gearing aims for - register 5[2]:
 * "MCLK will be geared down until 128FS <= SYS_CLK < 256FS" - and 64 is 64FS
 * mode, which only becomes the choice when nothing larger fits under 50 MHz.
 */
static const unsigned int es9039_mclk_ratios[] = { 512, 256, 128, 64 };

/*
 * Synchronous means the frame clock and MCLK come from the same place. That is
 * true when the part generates BCK and WS itself, and it is true when the MCLK
 * we were given can be set to a multiple of the sample rate. Only a fixed
 * oscillator feeding a consumer-mode part is genuinely asynchronous, and only
 * then does the ASRC have anything to do.
 */
static bool es9039_is_sync(struct es9039q2m_priv *priv)
{
	return priv->provider || !priv->mclk_fixed;
}

/*
 * 64FS mode is an exact MCLK/FS ratio, not a rate threshold, and it only makes
 * sense synchronously: at 64 * FS an asynchronous part would sit far below its
 * own 130 * FS floor, so a fixed oscillator that happens to land on 64 * FS is
 * a coincidence rather than a mode.
 */
static bool es9039_is_64fs(struct es9039q2m_priv *priv, unsigned int mclk,
			   unsigned int rate)
{
	return es9039_is_sync(priv) && rate &&
	       mclk == ES9039_64FS_MCLK_FS * rate;
}

/* The lowest MCLK this rate may run at outside 64FS mode. */
static unsigned int es9039_min_mclk(struct es9039q2m_priv *priv,
				    unsigned int rate)
{
	if (es9039_is_sync(priv))
		return ES9039_SYNC_MIN_MCLK_FS * rate;

	/* Asynchronous is strictly greater than 130 * FS, not equal to it. */
	return ES9039_ASYNC_MIN_MCLK_FS * rate + 1;
}

static int es9039q2m_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);
	unsigned int max_rate;

	/*
	 * A clock whose rate can be set imposes no ceiling here: hw_params()
	 * raises MCLK to suit the rate, and fails cleanly if it cannot.
	 * Constraining from the rate MCLK merely happens to be idling at would
	 * reject rates the board can carry perfectly well.
	 */
	if (!priv->mclk_rate || !priv->mclk_fixed)
		return 0;

	if (!es9039_is_sync(priv)) {
		max_rate = priv->mclk_rate / ES9039_ASYNC_MIN_MCLK_FS;
	} else if (priv->mclk_rate % ES9039_64FS_MCLK_FS == 0) {
		/*
		 * 64FS mode reaches exactly one rate above the ordinary
		 * ceiling - MCLK / 64 - so admit it here and leave
		 * hw_params() to reject anything in between, where neither
		 * 64 * FS nor 128 * FS is satisfied.
		 */
		max_rate = priv->mclk_rate / ES9039_64FS_MCLK_FS;
	} else {
		max_rate = priv->mclk_rate / ES9039_SYNC_MIN_MCLK_FS;
	}

	return snd_pcm_hw_constraint_minmax(substream->runtime,
					    SNDRV_PCM_HW_PARAM_RATE,
					    8000, max_rate);
}

/*
 * Put the clocking where it needs to be for this rate, and tell the part which
 * of its two timing worlds it is living in.
 *
 * With a settable MCLK the useful thing is to pull it to an exact multiple of
 * the sample rate: the ASRC then has nothing to correct, which is the better
 * arrangement whenever the board can manage it. With a fixed oscillator there
 * is nothing to pull, and the ASRC earns its keep.
 */
static int es9039_setup_clocking(struct es9039q2m_priv *priv,
				 struct snd_soc_dai *dai, unsigned int rate)
{
	unsigned int ratio, min_mclk, div, i, target = 0;
	bool sixtyfour;
	int ret;

	if (!priv->mclk_rate)
		return 0;

	ratio = priv->bclk_ratio ? priv->bclk_ratio : 64;

	if (!priv->mclk_fixed) {
		/*
		 * Take the highest ratio that still fits under the 50 MHz
		 * ceiling rather than always asking for 256 * FS, which would
		 * put 384 kHz at 98.304 MHz and 768 kHz at 196.608 MHz. A
		 * settable clock is synchronous by definition here - see
		 * es9039_is_sync() - so only the synchronous floors apply.
		 */
		for (i = 0; i < ARRAY_SIZE(es9039_mclk_ratios); i++) {
			unsigned int mult = es9039_mclk_ratios[i];

			if (mult * rate > ES9039_MAX_MCLK)
				continue;
			if (mult < ES9039_SYNC_MIN_MCLK_FS &&
			    mult != ES9039_64FS_MCLK_FS)
				continue;
			/*
			 * Master mode divides MCLK down to BCK by a whole
			 * number, so a ratio that is not a multiple of the
			 * frame size cannot produce the bit clock.
			 */
			if (priv->provider && mult % ratio)
				continue;

			target = mult * rate;
			break;
		}

		if (!target) {
			dev_err(dai->dev,
				"no mclk ratio for %u Hz within %u Hz\n",
				rate, ES9039_MAX_MCLK);
			return -EINVAL;
		}

		ret = clk_set_rate(priv->mclk, target);
		if (ret)
			return ret;

		/* Take what the clock settled on, not what was asked for. */
		priv->mclk_rate = clk_get_rate(priv->mclk);
	}

	if (priv->mclk_rate > ES9039_MAX_MCLK) {
		dev_err(dai->dev, "mclk %u Hz is above the %u Hz maximum\n",
			priv->mclk_rate, ES9039_MAX_MCLK);
		return -EINVAL;
	}

	sixtyfour = es9039_is_64fs(priv, priv->mclk_rate, rate);
	min_mclk = es9039_min_mclk(priv, rate);

	if (!sixtyfour && priv->mclk_rate < min_mclk) {
		dev_err(dai->dev,
			"mclk %u Hz is below the %u Hz %u Hz needs in this mode\n",
			priv->mclk_rate, min_mclk, rate);
		return -EINVAL;
	}

	/*
	 * Register 0[6]. Decided from the MCLK actually in use rather than from
	 * the sample rate alone, because the part cares about the ratio: 384 kHz
	 * from 24.576 MHz needs 64FS mode exactly as 768 kHz from 49.152 MHz
	 * does. The mode also forces a minimum phase filter regardless of
	 * FILTER_SHAPE.
	 */
	ret = regmap_update_bits(priv->regmap, ES9039_SYSTEM_CONFIG,
				 ES9039_64FS_MODE,
				 sixtyfour ? ES9039_64FS_MODE : 0);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, ES9039_SYS_MODE,
				 ES9039_SYNC_MODE,
				 es9039_is_sync(priv) ? ES9039_SYNC_MODE : 0);
	if (ret)
		return ret;

	if (!priv->provider)
		return 0;

	/*
	 * Register 4: BCK = MCLK / (MASTER_BCK_DIV + 1), and WS follows from
	 * the frame length. An MCLK that is not a whole multiple of the bit
	 * clock cannot produce the requested rate at all, so say so rather
	 * than emitting something close.
	 */
	if (priv->mclk_rate % (ratio * rate)) {
		dev_err(dai->dev,
			"mclk %u Hz cannot produce %u * %u Hz bit clock\n",
			priv->mclk_rate, ratio, rate);
		return -EINVAL;
	}

	div = priv->mclk_rate / (ratio * rate);
	if (div < 1 || div > 256) {
		dev_err(dai->dev, "master bck divider %u out of range\n", div);
		return -EINVAL;
	}

	return regmap_write(priv->regmap, ES9039_CLOCK_CONFIG, div - 1);
}

static int es9039q2m_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);
	unsigned int cfg1 = 0, enc = 0;
	int ret;

	/*
	 * Both directions are useful and which is right is a board decision,
	 * not a driver one. Driving BCK and WS from a good MCLK is the
	 * conventional arrangement and keeps everything synchronous; taking
	 * them from a consumer-mode host and letting the ASRC absorb the
	 * mismatch is what a board with a fixed oscillator wants. The part can
	 * do either, so the driver offers either.
	 */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_CBC_CFC:
		priv->provider = false;
		break;
	case SND_SOC_DAIFMT_CBP_CFP:
		if (!priv->mclk)
			return -EINVAL;
		priv->provider = true;
		break;
	default:
		/* The part cannot split them: it drives both or neither. */
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, ES9039_INPUT_SEL,
				 ES9039_PCM_MASTER_MODE,
				 priv->provider ? ES9039_PCM_MASTER_MODE : 0);
	if (ret)
		return ret;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		cfg1 |= ES9039_TDM_LJ_MODE;
		break;
	default:
		/* Register 60 offers I2S or LJ. There is no RJ mode. */
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		enc |= ES9039_BCK_INV;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_update_bits(priv->regmap, ES9039_TDM_CONFIG1,
				 ES9039_TDM_LJ_MODE, cfg1);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, ES9039_MASTER_ENC,
				 ES9039_BCK_INV, enc);
	if (ret)
		return ret;

	priv->fmt = fmt;
	return 0;
}

/*
 * TDM_BIT_WIDTH describes the SLOT width on the wire, not the sample size. Those
 * are routinely different: 16-bit samples are usually carried left-justified in
 * 32-bit slots. Getting this wrong misaligns the channel boundaries and the
 * result is one channel, or noise.
 *
 * ASoC does not hand the codec the bit clock ratio unless a machine driver sets
 * it, so take it when offered and otherwise assume 32-bit slots - by far the
 * most common arrangement, and what the RK3588 I2S does unconditionally
 * (rockchip_i2s.c sets bclk_ratio = 64 at probe and never varies it with
 * format).
 */
static int es9039q2m_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);

	priv->bclk_ratio = ratio;
	return 0;
}

static int es9039q2m_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);
	unsigned int input_sel, decode, width, slot_bits, isel;
	bool auto_sel;
	int ret;

	/*
	 * Held for the whole call: dop_auto decides both INPUT_SEL and
	 * SYS_MODE below, and a kcontrol write landing between the two would
	 * leave automatic detection enabled with no DoP decoder behind it.
	 */
	guard(mutex)(&priv->lock);

	priv->stream_rate = params_rate(params);

	ret = es9039_setup_clocking(priv, dai, priv->stream_rate);
	if (ret)
		return ret;

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_DSD_U8:
	case SNDRV_PCM_FORMAT_DSD_U16_LE:
	case SNDRV_PCM_FORMAT_DSD_U32_LE:
		/*
		 * Forced, not auto-detected: the datasheet requires DSD data on
		 * DATA1 and DATA2 for AUTO_INPUT_SEL to identify it, which a
		 * two-channel I2S link does not provide.
		 */
		input_sel = ES9039_INPUT_DSD;
		decode = ES9039_ENABLE_DSD_DECODE;
		auto_sel = false;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S24_3LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		/*
		 * DoP arrives inside ordinary PCM frames and is indistinguishable
		 * from PCM until the part finds the marker, so both decoders run
		 * and AUTO_INPUT_SEL picks between them. INPUT_SEL is programmed
		 * anyway as the fallback the part uses when auto-detection is
		 * switched off.
		 */
		input_sel = ES9039_INPUT_PCM;
		decode = ES9039_ENABLE_TDM_DECODE |
			 (priv->dop_auto ? ES9039_ENABLE_DOP_DECODE : 0);
		auto_sel = priv->dop_auto;
		break;
	default:
		return -EINVAL;
	}

	slot_bits = priv->bclk_ratio ?
		    priv->bclk_ratio / params_channels(params) : 32;

	switch (slot_bits) {
	case 16:
		width = ES9039_WIDTH_16;
		break;
	case 24:
		width = ES9039_WIDTH_24;
		break;
	case 32:
		width = ES9039_WIDTH_32;
		break;
	default:
		dev_err(dai->dev, "unsupported slot width %u\n", slot_bits);
		return -EINVAL;
	}

	/*
	 * AUTO_INPUT_SEL belongs in the value as well as the mask. It was in
	 * the mask alone, so every hw_params quietly cleared it and undid what
	 * the component probe had set - which is why enabling auto-detection
	 * by hand mid-stream worked while enabling it in probe() did not.
	 * INPUT_SEL is still programmed underneath: it is what the part falls
	 * back to when auto-detection is switched off.
	 */
	isel = FIELD_PREP(ES9039_INPUT_SEL_MASK, input_sel);
	if (auto_sel)
		isel |= ES9039_AUTO_INPUT_SEL;

	ret = regmap_update_bits(priv->regmap, ES9039_INPUT_SEL,
				 ES9039_AUTO_INPUT_SEL | ES9039_INPUT_SEL_MASK,
				 isel);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, ES9039_TDM_CONFIG2,
				 ES9039_TDM_BIT_WIDTH_MASK,
				 FIELD_PREP(ES9039_TDM_BIT_WIDTH_MASK, width));
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, ES9039_TDM_CH_NUM,
				 ES9039_TDM_CH_NUM_MASK,
				 params_channels(params) - 1);
	if (ret)
		return ret;

	/* Switch the right decoder on for this stream, and the others off. */
	return regmap_update_bits(priv->regmap, ES9039_SYS_MODE,
				  ES9039_DECODE_MASK, decode);
}

static int es9039q2m_mute_stream(struct snd_soc_dai *dai, int mute, int dir)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);
	bool old;
	int ret;

	guard(mutex)(&priv->lock);

	old = priv->mute_stream;
	priv->mute_stream = mute;

	ret = es9039_apply_mute(priv);
	if (ret)
		priv->mute_stream = old;

	return ret;
}

/*
 * stream_rate doubles as "a stream is open", which es9039_dop_put() needs in
 * order to refuse a change it could only half-apply.
 */
static void es9039q2m_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(dai->component);

	guard(mutex)(&priv->lock);

	priv->stream_rate = 0;
}

static const struct snd_soc_dai_ops es9039q2m_dai_ops = {
	.startup	 = es9039q2m_startup,
	.shutdown	 = es9039q2m_shutdown,
	.set_fmt	 = es9039q2m_set_fmt,
	.set_bclk_ratio	 = es9039q2m_set_bclk_ratio,
	.hw_params	 = es9039q2m_hw_params,
	.mute_stream	 = es9039q2m_mute_stream,
	.no_capture_mute = 1,
};

#define ES9039_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE     | \
			 SNDRV_PCM_FMTBIT_S24_LE     | \
			 SNDRV_PCM_FMTBIT_S24_3LE    | \
			 SNDRV_PCM_FMTBIT_S32_LE     | \
			 SNDRV_PCM_FMTBIT_DSD_U8     | \
			 SNDRV_PCM_FMTBIT_DSD_U16_LE | \
			 SNDRV_PCM_FMTBIT_DSD_U32_LE)

static struct snd_soc_dai_driver es9039q2m_dai = {
	.name = "es9039q2m-hifi",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 2,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_768000,
		.formats	= ES9039_FORMATS,
	},
	.ops = &es9039q2m_dai_ops,
};

/* ---------------------------------------------------------------- component */

static int es9039q2m_component_probe(struct snd_soc_component *component)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(component);
	int ret;

	/*
	 * Come up muted. The volume registers default to 0 dB, and an unmuted
	 * DAC arriving into an already-powered analogue stage is how you get a
	 * thump. ASoC unmutes via mute_stream() when a stream starts.
	 */
	priv->mute_user = 0;
	priv->mute_stream = false;
	ret = regmap_update_bits(priv->regmap, ES9039_DAC_MUTE,
				 ES9039_MUTE_BOTH, ES9039_MUTE_BOTH);
	if (ret)
		return ret;

	/*
	 * Enable the datapath. SYSTEM_CONFIG[1] is clear at reset, so in
	 * software mode nothing turns the DAC on at power-up: the analogue
	 * output stays dead until this bit is set.
	 *
	 * After the mute above and never before it. Bringing the datapath up
	 * into an unmuted part whose volume registers still sit at their 0 dB
	 * reset value is exactly the thump the mute is there to prevent.
	 *
	 * Note when testing that a warm reboot does not reset this part -
	 * registers and coefficient RAM both survive one, and only removing
	 * power clears them - so the previous boot's value can hide whether
	 * this write happened at all.
	 */
	ret = regmap_update_bits(priv->regmap, ES9039_SYSTEM_CONFIG,
				 ES9039_DAC_MODE, ES9039_DAC_MODE);
	if (ret)
		return ret;

	/*
	 * VOLUME_HOLD set means "do not apply volume register writes", so its
	 * resting state has to be clear or the volume control would appear
	 * dead. es9039_vol_put() raises it around the pair of writes and drops
	 * it again, which is how both channels are made to move together.
	 */
	ret = regmap_update_bits(priv->regmap, ES9039_IIR_SPDIF,
				 ES9039_VOLUME_HOLD, 0);
	if (ret)
		return ret;

	/*
	 * Let the part identify DoP for itself. Register 57[0] AUTO_INPUT_SEL
	 * makes it choose between PCM and DoP from the data, which is the only
	 * way DoP can work in practice: players send DoP-encoded PCM and expect
	 * the DAC to notice, and none of them can flip an ALSA control first.
	 * Without this the part is told "PCM", never looks for the marker, and
	 * renders a DoP stream as the near-silent hiss the DoP marker design
	 * deliberately degrades to. Verified on hardware by enabling this
	 * mid-stream and watching reg 245 flip from PCM to DoP with DOP_VALID
	 * set.
	 *
	 * The datasheet's "data must be provided on the DATA2 pin" applies to
	 * identifying DSD, whose two channels arrive on separate data lines.
	 * DoP is ordinary stereo I2S on one line and detects correctly without
	 * it, which the same measurement establishes.
	 */
	priv->dop_auto = true;
	scoped_guard(mutex, &priv->lock)
		ret = es9039_apply_dop(priv, true);
	if (ret)
		return ret;

	/*
	 * Board defaults, applied once at probe so the part is deterministic
	 * from cold instead of inheriting whatever its reset value happens to
	 * be. Both stay user-settable through their kcontrols; these are
	 * defaults, not policy.
	 *
	 * Reconstruction filter: linear phase apodizing fast roll-off. It keeps
	 * the sharp cut and flat passband of the plain fast linear-phase filter
	 * while suppressing pre-ringing, and an apodizing response also
	 * suppresses pre-ringing already baked into the source material by the
	 * recording chain - which none of the other seven addresses. The cost
	 * is a little stopband rejection right at the band edge, well above
	 * where it can matter. Chosen this way because a blind A/B listening
	 * test found no audible difference between any of the eight,
	 * so the tie is broken on theory rather than on preference.
	 *
	 * Modulator: wide bandwidth, which is ESS's own recommendation. It
	 * moves the modulator's noise further out of band and improves
	 * linearity at high frequencies.
	 */
	ret = regmap_update_bits(priv->regmap, ES9039_FILTER_SHAPE,
				 ES9039_FILTER_SHAPE_MASK,
				 ES9039_FILTER_APODIZING);
	if (ret)
		return ret;

	ret = regmap_update_bits(priv->regmap, ES9039_NSMOD,
				 ES9039_NSMOD_WIDE_BW_MASK,
				 FIELD_PREP(ES9039_NSMOD_WIDE_BW_MASK,
					    ES9039_NSMOD_WIDE));
	if (ret)
		return ret;

	/* Let the part work out the incoming rate; the ASRC does the rest. */
	return regmap_update_bits(priv->regmap, ES9039_AUTO_FS_DETECT,
				  ES9039_AUTO_FS_DETECT_EN,
				  ES9039_AUTO_FS_DETECT_EN);
}

/*
 * System suspend.
 *
 * Where the part is clocked from the SoC, system suspend stops its clock as a
 * side effect and there is nothing for a driver to do. Where the board feeds
 * it a free-running oscillator instead - which the ASRC in front of the DAC
 * makes an attractive design, since MCLK then need not track BCLK or LRCK -
 * nothing in the system can gate that clock, and the part stays fully clocked
 * for the whole of suspend.
 *
 * Clearing ENABLE_DAC_CLK gates the part's internal clock tree, which is the
 * only part of its consumption software can reach. The analogue supplies are
 * the board's business, and not every board is able to switch them.
 *
 * The write deliberately bypasses the cache. The cached value must keep
 * ENABLE_DAC_CLK set, so that regcache_sync() on resume restores whatever
 * state userspace last left the part in without this code having to remember
 * anything itself.
 */
static int es9039q2m_suspend(struct snd_soc_component *component)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(component);
	int ret;

	guard(mutex)(&priv->lock);

	regcache_cache_bypass(priv->regmap, true);
	ret = regmap_update_bits(priv->regmap, ES9039_SYS_MODE,
				 ES9039_ENABLE_DAC_CLK, 0);
	regcache_cache_bypass(priv->regmap, false);

	if (ret)
		dev_warn(component->dev,
			 "could not gate the DAC clock for suspend: %d\n", ret);

	regcache_mark_dirty(priv->regmap);
	regcache_cache_only(priv->regmap, true);

	return 0;
}

static int es9039q2m_resume(struct snd_soc_component *component)
{
	struct es9039q2m_priv *priv = snd_soc_component_get_drvdata(component);
	unsigned int i;
	int ret;

	guard(mutex)(&priv->lock);

	regcache_cache_only(priv->regmap, false);

	/*
	 * Mute the part before anything else, and write it straight to the
	 * hardware rather than through the cache.
	 *
	 * Where the board kept the supplies up this is a no-op - the part still
	 * holds the mute state it went to sleep with. Where the board removed
	 * them it is not: the part comes back at reset defaults, which are
	 * DAC_MODE clear, both channels UNMUTED, and both volume registers at
	 * 0x00, their 0 dB setting. regcache_sync() walks registers in ascending
	 * address order, so it would set DAC_MODE in register 0 - lighting the
	 * datapath up unmuted at full scale - and only restore the real mute in
	 * register 86, long after. That is the thump the probe path mutes to
	 * avoid, arriving by the other door.
	 *
	 * The cache is bypassed so the cached mute state is untouched; the sync
	 * below puts it back, after the volume registers at 74 and 75.
	 */
	regcache_cache_bypass(priv->regmap, true);
	ret = regmap_update_bits(priv->regmap, ES9039_DAC_MUTE,
				 ES9039_MUTE_BOTH, ES9039_MUTE_BOTH);
	regcache_cache_bypass(priv->regmap, false);

	if (ret)
		dev_warn(component->dev,
			 "could not mute before resync: %d\n", ret);

	ret = regcache_sync(priv->regmap);
	if (ret)
		dev_err(component->dev,
			"failed to restore registers on resume: %d\n", ret);

	/*
	 * Re-upload the programmable FIR RAM. regcache_sync() cannot restore
	 * it - it is write-only, so there is no cache of it to sync - and a
	 * board that removed the part's supplies comes back with that RAM
	 * undefined while the sync has just restored the filter SELECTION from
	 * the cache. Selecting the programmable filter would then point the
	 * interpolator at whatever the RAM powered up holding. After the sync,
	 * so the selection is already in place. Stages never uploaded have
	 * fir_taps == 0 and keep the part's own defaults.
	 */
	for (i = 0; i < ES9039_FIR_STAGES; i++) {
		int err;

		if (!priv->fir_taps[i])
			continue;

		err = es9039_fir_upload(priv, priv->fir_shadow[i],
					priv->fir_taps[i], i == 1);
		if (err)
			dev_err(component->dev,
				"failed to restore FIR stage %u on resume: %d\n",
				i, err);
	}

	return ret;
}

static const struct snd_soc_component_driver es9039q2m_component = {
	.probe			= es9039q2m_component_probe,
	.suspend		= es9039q2m_suspend,
	.resume			= es9039q2m_resume,
	.controls		= es9039q2m_controls,
	.num_controls		= ARRAY_SIZE(es9039q2m_controls),
	.dapm_widgets		= es9039q2m_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(es9039q2m_widgets),
	.dapm_routes		= es9039q2m_routes,
	.num_dapm_routes	= ARRAY_SIZE(es9039q2m_routes),
	.idle_bias_on		= 1,
	.endianness		= 1,
};

/* ------------------------------------------------------------------- power */

/*
 * The part has four supply inputs and the datasheet is specific about the
 * order: figure 22 brings AVDD up first, VCCA about 200 us later, then the two
 * output-stage references, and only then is the part enabled. Power-down is
 * the exact reverse.
 *
 * That ordering is the whole reason these are enabled one at a time rather
 * than with regulator_bulk_enable(), which makes no ordering guarantee at all.
 * The bulk API is still used to GET them, where order is irrelevant. On a
 * board that ties all four to one always-on rail the sequence costs nothing;
 * on one that switches them separately, bringing an output stage up before its
 * reference is precisely what the sequence exists to prevent.
 *
 * The datasheet gives no settling time between the last supply and the first
 * register access, so none is invented here.
 */
static const char * const es9039_supply_names[ES9039_NUM_SUPPLIES] = {
	"avdd", "vcca", "avcc-dac1", "avcc-dac2",
};

static int es9039_power_on(struct es9039q2m_priv *priv)
{
	int i, ret;

	for (i = 0; i < ES9039_NUM_SUPPLIES; i++) {
		ret = regulator_enable(priv->supplies[i].consumer);
		if (ret)
			goto err;

		/* AVDD leads VCCA. It is the only interval the part specifies. */
		if (i == 0)
			fsleep(200);
	}

	return 0;

err:
	while (--i >= 0)
		regulator_disable(priv->supplies[i].consumer);

	return ret;
}

static void es9039_power_off(void *data)
{
	struct es9039q2m_priv *priv = data;
	int i;

	for (i = ES9039_NUM_SUPPLIES - 1; i >= 0; i--)
		regulator_disable(priv->supplies[i].consumer);
}

/* --------------------------------------------------------------------- I2C */

static int es9039q2m_i2c_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct es9039q2m_priv *priv;
	unsigned int id;
	int i, ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(i2c, &es9039q2m_regmap);
	if (IS_ERR(priv->regmap))
		return dev_err_probe(dev, PTR_ERR(priv->regmap),
				     "failed to init regmap\n");

	for (i = 0; i < ES9039_NUM_SUPPLIES; i++)
		priv->supplies[i].supply = es9039_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ES9039_NUM_SUPPLIES, priv->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "failed to get the supplies\n");

	ret = es9039_power_on(priv);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable the supplies\n");

	ret = devm_add_action_or_reset(dev, es9039_power_off, priv);
	if (ret)
		return ret;

	priv->mclk = devm_clk_get_optional_enabled(dev, "mclk");
	if (IS_ERR(priv->mclk))
		return dev_err_probe(dev, PTR_ERR(priv->mclk),
				     "failed to get mclk\n");

	if (priv->mclk) {
		priv->mclk_rate = clk_get_rate(priv->mclk);
		if (priv->mclk_rate > 50000000)
			return dev_err_probe(dev, -EINVAL,
					     "mclk %u Hz exceeds the 50 MHz maximum\n",
					     priv->mclk_rate);

		/*
		 * A fixed-rate clock answers every rounding question with the
		 * one rate it has. That is the board saying "this is an
		 * oscillator, use the ASRC"; anything else can be pulled to
		 * suit the sample rate and run synchronously.
		 */
		priv->mclk_fixed =
			clk_round_rate(priv->mclk, priv->mclk_rate / 2) ==
			priv->mclk_rate;
	}

	ret = devm_mutex_init(dev, &priv->lock);
	if (ret)
		return ret;

	i2c_set_clientdata(i2c, priv);

	ret = regmap_read(priv->regmap, ES9039_CHIP_ID, &id);
	if (ret)
		return dev_err_probe(dev, ret, "no response at 0x%02x\n",
				     i2c->addr);

	if (id != ES9039_CHIP_ID_ES9039Q2M)
		return dev_err_probe(dev, -ENODEV,
				     "unexpected chip id 0x%02x, want 0x%02x\n",
				     id, ES9039_CHIP_ID_ES9039Q2M);

	dev_info(dev, "ES9039Q2M at 0x%02x, mclk %u Hz\n",
		 i2c->addr, priv->mclk_rate);

	return devm_snd_soc_register_component(dev, &es9039q2m_component,
					       &es9039q2m_dai, 1);
}

static const struct of_device_id es9039q2m_of_match[] = {
	{ .compatible = "esstech,es9039q2m" },
	{ }
};
MODULE_DEVICE_TABLE(of, es9039q2m_of_match);

static const struct i2c_device_id es9039q2m_i2c_id[] = {
	{ "es9039q2m" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, es9039q2m_i2c_id);

static struct i2c_driver es9039q2m_i2c_driver = {
	.driver = {
		.name		= "es9039q2m",
		.of_match_table	= es9039q2m_of_match,
	},
	.probe		= es9039q2m_i2c_probe,
	.id_table	= es9039q2m_i2c_id,
};
module_i2c_driver(es9039q2m_i2c_driver);

MODULE_DESCRIPTION("ASoC ES9039Q2M driver");
MODULE_AUTHOR("Karl Asseily <karl@asseily.com>");
MODULE_LICENSE("GPL");
