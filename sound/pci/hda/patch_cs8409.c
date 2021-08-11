// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * HD audio interface patch for Cirrus Logic CS8409 HDA bridge chip
 *
 * Copyright (C) 2021 Cirrus Logic, Inc. and
 *                    Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <sound/core.h>
#include <linux/mutex.h>

#include "patch_cs8409.h"

static int cs8409_parse_auto_config(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int err;
	int i;

	err = snd_hda_parse_pin_defcfg(codec, &spec->gen.autocfg, NULL, 0);
	if (err < 0)
		return err;

	err = snd_hda_gen_parse_auto_config(codec, &spec->gen.autocfg);
	if (err < 0)
		return err;

	/* keep the ADCs powered up when it's dynamically switchable */
	if (spec->gen.dyn_adc_switch) {
		unsigned int done = 0;

		for (i = 0; i < spec->gen.input_mux.num_items; i++) {
			int idx = spec->gen.dyn_adc_idx[i];

			if (done & (1 << idx))
				continue;
			snd_hda_gen_fix_pin_power(codec, spec->gen.adc_nids[idx]);
			done |= 1 << idx;
		}
	}

	return 0;
}

static struct cs8409_spec *cs8409_alloc_spec(struct hda_codec *codec)
{
	struct cs8409_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	codec->power_save_node = 1;
	snd_hda_gen_spec_init(&spec->gen);

	return spec;
}

static inline int cs8409_vendor_coef_get(struct hda_codec *codec, unsigned int idx)
{
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_COEF_INDEX, idx);
	return snd_hda_codec_read(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_GET_PROC_COEF, 0);
}

static inline void cs8409_vendor_coef_set(struct hda_codec *codec, unsigned int idx,
					  unsigned int coef)
{
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_COEF_INDEX, idx);
	snd_hda_codec_write(codec, CS8409_PIN_VENDOR_WIDGET, 0, AC_VERB_SET_PROC_COEF, coef);
}

/**
 * cs8409_enable_i2c_clock - Enable I2C clocks
 * @codec: the codec instance
 * @enable: Enable or disable I2C clocks
 *
 * Enable or Disable I2C clocks.
 */
static void cs8409_enable_i2c_clock(struct hda_codec *codec, unsigned int enable)
{
	unsigned int retval;
	unsigned int newval;

	retval = cs8409_vendor_coef_get(codec, 0x0);
	newval = (enable) ? (retval | 0x8) : (retval & 0xfffffff7);
	cs8409_vendor_coef_set(codec, 0x0, newval);
}

/**
 * cs8409_i2c_wait_complete - Wait for I2C transaction
 * @codec: the codec instance
 *
 * Wait for I2C transaction to complete.
 * Return -1 if transaction wait times out.
 */
static int cs8409_i2c_wait_complete(struct hda_codec *codec)
{
	int repeat = 5;
	unsigned int retval;

	do {
		retval = cs8409_vendor_coef_get(codec, CS8409_I2C_STS);
		if ((retval & 0x18) != 0x18) {
			usleep_range(2000, 4000);
			--repeat;
		} else
			return 0;

	} while (repeat);

	return -1;
}

/**
 * cs8409_i2c_read - CS8409 I2C Read.
 * @codec: the codec instance
 * @i2c_address: I2C Address
 * @i2c_reg: Register to read
 * @paged: Is a paged transaction
 *
 * CS8409 I2C Read.
 * Returns negative on error, otherwise returns read value in bits 0-7.
 */
static int cs8409_i2c_read(struct hda_codec *codec, unsigned int i2c_address, unsigned int i2c_reg,
			   unsigned int paged)
{
	unsigned int i2c_reg_data;
	unsigned int read_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs8409_vendor_coef_set(codec, CS8409_I2C_ADDR, i2c_address);

	if (paged) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec, "%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = (i2c_reg << 8) & 0x0ffff;
	cs8409_vendor_coef_set(codec, CS8409_I2C_QREAD, i2c_reg_data);
	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			  __func__, i2c_address, i2c_reg);
		return -EIO;
	}

	/* Register in bits 15-8 and the data in 7-0 */
	read_data = cs8409_vendor_coef_get(codec, CS8409_I2C_QREAD);

	cs8409_enable_i2c_clock(codec, 0);

	return read_data & 0x0ff;
}

/**
 * cs8409_i2c_write - CS8409 I2C Write.
 * @codec: the codec instance
 * @i2c_address: I2C Address
 * @i2c_reg: Register to write to
 * @i2c_data: Data to write
 * @paged: Is a paged transaction
 *
 * CS8409 I2C Write.
 * Returns negative on error, otherwise returns 0.
 */
static int cs8409_i2c_write(struct hda_codec *codec, unsigned int i2c_address, unsigned int i2c_reg,
			    unsigned int i2c_data, unsigned int paged)
{
	unsigned int i2c_reg_data;

	cs8409_enable_i2c_clock(codec, 1);
	cs8409_vendor_coef_set(codec, CS8409_I2C_ADDR, i2c_address);

	if (paged) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0) {
			codec_err(codec, "%s() Paged Transaction Failed 0x%02x : 0x%04x\n",
				__func__, i2c_address, i2c_reg);
			return -EIO;
		}
	}

	i2c_reg_data = ((i2c_reg << 8) & 0x0ff00) | (i2c_data & 0x0ff);
	cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg_data);

	if (cs8409_i2c_wait_complete(codec) < 0) {
		codec_err(codec, "%s() Transaction Failed 0x%02x : 0x%04x\n",
			__func__, i2c_address, i2c_reg);
		return -EIO;
	}

	cs8409_enable_i2c_clock(codec, 0);

	return 0;
}

static int cs8409_cs42l42_volume_info(struct snd_kcontrol *kctrl, struct snd_ctl_elem_info *uinfo)
{
	u16 nid = get_amp_nid(kctrl);
	u8 chs = get_amp_channels(kctrl);

	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = chs == 3 ? 2 : 1;
		uinfo->value.integer.min = CS8409_CS42L42_HP_VOL_REAL_MIN;
		uinfo->value.integer.max = CS8409_CS42L42_HP_VOL_REAL_MAX;
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
		uinfo->count = chs == 3 ? 2 : 1;
		uinfo->value.integer.min = CS8409_CS42L42_AMIC_VOL_REAL_MIN;
		uinfo->value.integer.max = CS8409_CS42L42_AMIC_VOL_REAL_MAX;
		break;
	default:
		break;
	}
	return 0;
}

static void cs8409_cs42l42_update_volume(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int data;

	mutex_lock(&spec->cs8409_i2c_mux);
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHA, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[0] = -data;
	else
		spec->cs42l42_hp_volume[0] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHB, 1);
	if (data >= 0)
		spec->cs42l42_hp_volume[1] = -data;
	else
		spec->cs42l42_hp_volume[1] = CS8409_CS42L42_HP_VOL_REAL_MIN;
	data = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_AMIC_VOLUME, 1);
	if (data >= 0)
		spec->cs42l42_hs_mic_volume[0] = -data;
	else
		spec->cs42l42_hs_mic_volume[0] = CS8409_CS42L42_AMIC_VOL_REAL_MIN;
	mutex_unlock(&spec->cs8409_i2c_mux);
	spec->cs42l42_volume_init = 1;
}

static int cs8409_cs42l42_volume_get(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kctrl);
	int chs = get_amp_channels(kctrl);
	long *valp = uctrl->value.integer.value;

	if (!spec->cs42l42_volume_init) {
		snd_hda_power_up(codec);
		cs8409_cs42l42_update_volume(codec);
		snd_hda_power_down(codec);
	}
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		if (chs & BIT(0))
			*valp++ = spec->cs42l42_hp_volume[0];
		if (chs & BIT(1))
			*valp++ = spec->cs42l42_hp_volume[1];
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		if (chs & BIT(0))
			*valp++ = spec->cs42l42_hs_mic_volume[0];
		break;
	default:
		break;
	}
	return 0;
}

static int cs8409_cs42l42_volume_put(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	hda_nid_t nid = get_amp_nid(kctrl);
	int chs = get_amp_channels(kctrl);
	long *valp = uctrl->value.integer.value;
	int change = 0;
	char vol;

	snd_hda_power_up(codec);
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		mutex_lock(&spec->cs8409_i2c_mux);
		if (chs & BIT(0)) {
			vol = -(*valp);
			change = cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
						  CS8409_CS42L42_REG_HS_VOLUME_CHA, vol, 1);
			valp++;
		}
		if (chs & BIT(1)) {
			vol = -(*valp);
			change |= cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
						   CS8409_CS42L42_REG_HS_VOLUME_CHB, vol, 1);
		}
		mutex_unlock(&spec->cs8409_i2c_mux);
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		mutex_lock(&spec->cs8409_i2c_mux);
		if (chs & BIT(0)) {
			change = cs8409_i2c_write(codec, CS42L42_I2C_ADDR,
						  CS8409_CS42L42_REG_AMIC_VOLUME, (char)*valp, 1);
			valp++;
		}
		mutex_unlock(&spec->cs8409_i2c_mux);
		break;
	default:
		break;
	}
	cs8409_cs42l42_update_volume(codec);
	snd_hda_power_down(codec);
	return change;
}

static const DECLARE_TLV_DB_SCALE(cs8409_cs42l42_hp_db_scale,
				  CS8409_CS42L42_HP_VOL_REAL_MIN * 100, 100, 1);

static const DECLARE_TLV_DB_SCALE(cs8409_cs42l42_amic_db_scale,
				  CS8409_CS42L42_AMIC_VOL_REAL_MIN * 100, 100, 1);

static const struct snd_kcontrol_new cs8409_cs42l42_hp_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Headphone Playback Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_hp_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(CS8409_CS42L42_HP_PIN_NID, 3, 0, HDA_OUTPUT) |
			 HDA_AMP_VAL_MIN_MUTE
};

static const struct snd_kcontrol_new cs8409_cs42l42_amic_volume_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.index = 0,
	.name = "Mic Capture Volume",
	.subdevice = (HDA_SUBDEV_AMP_FLAG | HDA_SUBDEV_NID_FLAG),
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE | SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = cs8409_cs42l42_volume_info,
	.get = cs8409_cs42l42_volume_get,
	.put = cs8409_cs42l42_volume_put,
	.tlv = { .p = cs8409_cs42l42_amic_db_scale },
	.private_value = HDA_COMPOSE_AMP_VAL(CS8409_CS42L42_AMIC_PIN_NID, 1, 0, HDA_INPUT) |
			 HDA_AMP_VAL_MIN_MUTE
};

/* Assert/release RTS# line to CS42L42 */
static void cs8409_cs42l42_reset(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Assert RTS# line */
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, 0);
	/* wait ~10ms */
	usleep_range(10000, 15000);
	/* Release RTS# line */
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, CS8409_CS42L42_RESET);
	/* wait ~10ms */
	usleep_range(10000, 15000);

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1309, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130A, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130F, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

/* Configure CS42L42 slave codec for jack autodetect */
static void cs8409_cs42l42_enable_jack_detect(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Set TIP_SENSE_EN for analog front-end of tip sense.
	 * Additionally set HSBIAS_SENSE_EN for some variants.
	 */
	if (codec->fixup_id == CS8409_WARLOCK || codec->fixup_id == CS8409_BULLSEYE)
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b70, 0x0020, 1);
	else
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b70, 0x00a0, 1);

	/* Clear WAKE# */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b71, 0x00C1, 1);
	/* Wait ~2.5ms */
	usleep_range(2500, 3000);
	/* Set mode WAKE# output follows the combination logic directly */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b71, 0x00C0, 1);
	/* Clear interrupts status */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130f, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b7b, 1);
	/* Enable interrupt */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1320, 0x03, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b79, 0x00, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);
}

/* Enable and run CS42L42 slave codec jack auto detect */
static void cs8409_cs42l42_run_jack_detect(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Clear interrupts */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b77, 1);

	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1102, 0x87, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1f06, 0x86, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1b74, 0x07, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x131b, 0x01, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1120, 0x80, 1);
	/* Wait ~110ms*/
	usleep_range(110000, 200000);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x111f, 0x77, 1);
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1120, 0xc0, 1);
	/* Wait ~10ms */
	usleep_range(10000, 25000);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

static void cs8409_cs42l42_reg_setup(struct hda_codec *codec)
{
	const struct cs8409_i2c_param *seq = cs42l42_init_reg_seq;
	struct cs8409_spec *spec = codec->spec;

	mutex_lock(&spec->cs8409_i2c_mux);

	for (; seq->addr; seq++)
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, seq->addr, seq->reg, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

}

/*
 * In the case of CS8409 we do not have unsolicited events from NID's 0x24
 * and 0x34 where hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via gpio 4 to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void cs8409_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;
	int status_changed = 0;
	int reg_cdc_status;
	int reg_hs_status;
	int reg_ts_status;
	int type;
	struct hda_jack_tbl *jk;

	/* jack_unsol_event() will be called every time gpio line changing state.
	 * In this case gpio4 line goes up as a result of reading interrupt status
	 * registers in previous cs8409_jack_unsol_event() call.
	 * We don't need to handle this event, ignoring...
	 */
	if (res & CS8409_CS42L42_INT)
		return;

	mutex_lock(&spec->cs8409_i2c_mux);

	/* Read jack detect status registers */
	reg_cdc_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1308, 1);
	reg_hs_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1124, 1);
	reg_ts_status = cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x130f, 1);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_read(codec, CS42L42_I2C_ADDR, 0x1b7b, 1);

	mutex_unlock(&spec->cs8409_i2c_mux);

	/* If status values are < 0, read error has occurred. */
	if (reg_cdc_status < 0 || reg_hs_status < 0 || reg_ts_status < 0)
		return;

	/* HSDET_AUTO_DONE */
	if (reg_cdc_status & CS42L42_HSDET_AUTO_DONE) {

		type = ((reg_hs_status & CS42L42_HSTYPE_MASK) + 1);
		/* CS42L42 reports optical jack as type 4
		 * We don't handle optical jack
		 */
		if (type != 4) {
			if (!spec->cs42l42_hp_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 1;
			}
			/* type = 3 has no mic */
			if ((!spec->cs42l42_mic_jack_in) && (type != 3)) {
				status_changed = 1;
				spec->cs42l42_mic_jack_in = 1;
			}
		} else {
			if (spec->cs42l42_hp_jack_in || spec->cs42l42_mic_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 0;
				spec->cs42l42_mic_jack_in = 0;
			}
		}

	} else {
		/* TIP_SENSE INSERT/REMOVE */
		switch (reg_ts_status) {
		case CS42L42_JACK_INSERTED:
			cs8409_cs42l42_run_jack_detect(codec);
			break;

		case CS42L42_JACK_REMOVED:
			if (spec->cs42l42_hp_jack_in || spec->cs42l42_mic_jack_in) {
				status_changed = 1;
				spec->cs42l42_hp_jack_in = 0;
				spec->cs42l42_mic_jack_in = 0;
			}
			break;

		default:
			/* jack in transition */
			status_changed = 0;
			break;
		}
	}

	if (status_changed) {

		snd_hda_set_pin_ctl(codec, CS8409_CS42L42_SPK_PIN_NID,
				    spec->cs42l42_hp_jack_in ? 0 : PIN_OUT);

		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_HP_PIN_NID, 0);
		if (jk) {
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							AC_UNSOL_RES_TAG);
		}
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_AMIC_PIN_NID, 0);
		if (jk) {
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							 AC_UNSOL_RES_TAG);
		}
	}
}

/* Enable/Disable Unsolicited Response for gpio(s) 3,4 */
static void cs8409_enable_ur(struct hda_codec *codec, int flag)
{
	/* GPIO4 INT# and GPIO3 WAKE# */
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK,
			    flag ? CS8409_CS42L42_INT : 0);

	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_UNSOLICITED_ENABLE,
			    flag ? AC_UNSOL_ENABLED : 0);

}

#ifdef CONFIG_PM
/* Manage PDREF, when transition to D3hot */
static int cs8409_suspend(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	cs8409_enable_ur(codec, 0);

	mutex_lock(&spec->cs8409_i2c_mux);
	/* Power down CS42L42 ASP/EQ/MIX/HP */
	cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x1101, 0xfe, 1);
	mutex_unlock(&spec->cs8409_i2c_mux);
	/* Assert CS42L42 RTS# line */
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, 0);

	snd_hda_shutup_pins(codec);

	return 0;
}
#endif

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void cs8409_cs42l42_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = cs8409_cs42l42_hw_cfg;
	const struct cs8409_cir_param *seq_bullseye = cs8409_cs42l42_bullseye_atn;
	struct cs8409_spec *spec = codec->spec;

	if (spec->gpio_mask) {
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_MASK,
			spec->gpio_mask);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DIRECTION,
			spec->gpio_dir);
		snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA,
			spec->gpio_data);
	}

	for (; seq->nid; seq++)
		cs8409_vendor_coef_set(codec, seq->cir, seq->coeff);

	if (codec->fixup_id == CS8409_BULLSEYE)
		for (; seq_bullseye->nid; seq_bullseye++)
			cs8409_vendor_coef_set(codec, seq_bullseye->cir, seq_bullseye->coeff);

	/* Reset CS42L42 */
	cs8409_cs42l42_reset(codec);

	/* Initialise CS42L42 companion codec */
	cs8409_cs42l42_reg_setup(codec);

	if (codec->fixup_id == CS8409_WARLOCK || codec->fixup_id == CS8409_CYBORG) {
		/* FULL_SCALE_VOL = 0 for Warlock / Cyborg */
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, 0x2001, 0x01, 1);
		mutex_unlock(&spec->cs8409_i2c_mux);
		/* DMIC1_MO=00b, DMIC1/2_SR=1 */
		cs8409_vendor_coef_set(codec, 0x09, 0x0003);
	}

	/* Restore Volumes after Resume */
	if (spec->cs42l42_volume_init) {
		mutex_lock(&spec->cs8409_i2c_mux);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHA,
				 -spec->cs42l42_hp_volume[0], 1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_HS_VOLUME_CHB,
				 -spec->cs42l42_hp_volume[1], 1);
		cs8409_i2c_write(codec, CS42L42_I2C_ADDR, CS8409_CS42L42_REG_AMIC_VOLUME,
				 spec->cs42l42_hs_mic_volume[0], 1);
		mutex_unlock(&spec->cs8409_i2c_mux);
	}

	cs8409_cs42l42_update_volume(codec);

	cs8409_cs42l42_enable_jack_detect(codec);

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static int cs8409_cs42l42_init(struct hda_codec *codec)
{
	int ret = snd_hda_gen_init(codec);

	if (!ret)
		snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_INIT);

	return ret;
}

static int cs8409_build_controls(struct hda_codec *codec)
{
	int err;

	err = snd_hda_gen_build_controls(codec);
	if (err < 0)
		return err;
	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_BUILD);

	return 0;
}

static const struct hda_codec_ops cs8409_cs42l42_patch_ops = {
	.build_controls = cs8409_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs8409_cs42l42_init,
	.free = snd_hda_gen_free,
	.unsol_event = cs8409_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs8409_suspend,
#endif
};

static int cs8409_cs42l42_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
				    unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	/* CS8409 pins have no AC_PINSENSE_PRESENCE
	 * capabilities. We have to intercept 2 calls for pins 0x24 and 0x34
	 * and return correct pin sense values for read_pin_sense() call from
	 * hda_jack based on CS42L42 jack detect status.
	 */
	switch (nid) {
	case CS8409_CS42L42_HP_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (spec->cs42l42_hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;

	case CS8409_CS42L42_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (spec->cs42l42_mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;

	default:
		break;
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

void cs8409_cs42l42_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	struct cs8409_spec *spec = codec->spec;
	int caps;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, cs8409_cs42l42_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = cs8409_cs42l42_exec_verb;

		mutex_init(&spec->cs8409_i2c_mux);

		codec->patch_ops = cs8409_cs42l42_patch_ops;

		spec->gen.suppress_auto_mute = 1;
		spec->gen.no_primary_hp = 1;
		spec->gen.suppress_vmaster = 1;

		/* GPIO 5 out, 3,4 in */
		spec->gpio_dir = CS8409_CS42L42_RESET;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		spec->cs42l42_hp_jack_in = 0;
		spec->cs42l42_mic_jack_in = 0;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, cs8409_cs42l42_init_verbs);

		/* CS8409 is simple HDA bridge and intended to be used with a remote
		 * companion codec. Most of input/output PIN(s) have only basic
		 * capabilities. NID(s) 0x24 and 0x34 have only OUTC and INC
		 * capabilities and no presence detect capable (PDC) and call to
		 * snd_hda_gen_build_controls() will mark them as non detectable
		 * phantom jacks. However, in this configuration companion codec
		 * CS42L42 is connected to these pins and it has jack detect
		 * capabilities. We have to override pin capabilities,
		 * otherwise they will not be created as input devices.
		 */
		caps = snd_hdac_read_parm(&codec->core, CS8409_CS42L42_HP_PIN_NID,
				AC_PAR_PIN_CAP);
		if (caps >= 0)
			snd_hdac_override_parm(&codec->core,
				CS8409_CS42L42_HP_PIN_NID, AC_PAR_PIN_CAP,
				(caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

		caps = snd_hdac_read_parm(&codec->core, CS8409_CS42L42_AMIC_PIN_NID,
				AC_PAR_PIN_CAP);
		if (caps >= 0)
			snd_hdac_override_parm(&codec->core,
				CS8409_CS42L42_AMIC_PIN_NID, AC_PAR_PIN_CAP,
				(caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

		snd_hda_override_wcaps(codec, CS8409_CS42L42_HP_PIN_NID,
			(get_wcaps(codec, CS8409_CS42L42_HP_PIN_NID) | AC_WCAP_UNSOL_CAP));

		snd_hda_override_wcaps(codec, CS8409_CS42L42_AMIC_PIN_NID,
			(get_wcaps(codec, CS8409_CS42L42_AMIC_PIN_NID) | AC_WCAP_UNSOL_CAP));
		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Set initial DMIC volume to -26 dB */
		snd_hda_codec_amp_init_stereo(codec, CS8409_CS42L42_DMIC_ADC_PIN_NID,
					      HDA_INPUT, 0, 0xff, 0x19);
		snd_hda_gen_add_kctl(&spec->gen, NULL, &cs8409_cs42l42_hp_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, NULL, &cs8409_cs42l42_amic_volume_mixer);
		/* Disable Unsolicited Response during boot */
		cs8409_enable_ur(codec, 0);
		cs8409_cs42l42_hw_init(codec);
		snd_hda_codec_set_name(codec, "CS8409/CS42L42");
		break;
	case HDA_FIXUP_ACT_INIT:
		cs8409_cs42l42_hw_init(codec);
		fallthrough;
	case HDA_FIXUP_ACT_BUILD:
		/* Run jack auto detect first time on boot
		 * after controls have been added, to check if jack has
		 * been already plugged in.
		 * Run immediately after init.
		 */
		cs8409_cs42l42_run_jack_detect(codec);
		usleep_range(100000, 150000);
		break;
	default:
		break;
	}
}

static int patch_cs8409(struct hda_codec *codec)
{
	int err;

	if (!cs8409_alloc_spec(codec))
		return -ENOMEM;

	snd_hda_pick_fixup(codec, cs8409_models, cs8409_fixup_tbl, cs8409_fixups);

	codec_dbg(codec, "Picked ID=%d, VID=%08x, DEV=%08x\n", codec->fixup_id,
			 codec->bus->pci->subsystem_vendor,
			 codec->bus->pci->subsystem_device);

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PRE_PROBE);

	err = cs8409_parse_auto_config(codec);
	if (err < 0) {
		snd_hda_gen_free(codec);
		return err;
	}

	snd_hda_apply_fixup(codec, HDA_FIXUP_ACT_PROBE);
	return 0;
}

static const struct hda_device_id snd_hda_id_cs8409[] = {
	HDA_CODEC_ENTRY(0x10138409, "CS8409", patch_cs8409),
	{} /* terminator */
};
MODULE_DEVICE_TABLE(hdaudio, snd_hda_id_cs8409);

static struct hda_codec_driver cs8409_driver = {
	.id = snd_hda_id_cs8409,
};
module_hda_codec_driver(cs8409_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cirrus Logic HDA bridge");
