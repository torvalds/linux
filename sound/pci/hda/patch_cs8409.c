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

/******************************************************************************
 *                        CS8409 Specific Functions
 ******************************************************************************/

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

static void cs8409_disable_i2c_clock_worker(struct work_struct *work);

static struct cs8409_spec *cs8409_alloc_spec(struct hda_codec *codec)
{
	struct cs8409_spec *spec;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;
	codec->spec = spec;
	spec->codec = codec;
	codec->power_save_node = 1;
	mutex_init(&spec->i2c_mux);
	INIT_DELAYED_WORK(&spec->i2c_clk_work, cs8409_disable_i2c_clock_worker);
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

/*
 * cs8409_enable_i2c_clock - Disable I2C clocks
 * @codec: the codec instance
 * Disable I2C clocks.
 * This must be called when the i2c mutex is unlocked.
 */
static void cs8409_disable_i2c_clock(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	mutex_lock(&spec->i2c_mux);
	if (spec->i2c_clck_enabled) {
		cs8409_vendor_coef_set(spec->codec, 0x0,
			       cs8409_vendor_coef_get(spec->codec, 0x0) & 0xfffffff7);
		spec->i2c_clck_enabled = 0;
	}
	mutex_unlock(&spec->i2c_mux);
}

/*
 * cs8409_disable_i2c_clock_worker - Worker that disable the I2C Clock after 25ms without use
 */
static void cs8409_disable_i2c_clock_worker(struct work_struct *work)
{
	struct cs8409_spec *spec = container_of(work, struct cs8409_spec, i2c_clk_work.work);

	cs8409_disable_i2c_clock(spec->codec);
}

/*
 * cs8409_enable_i2c_clock - Enable I2C clocks
 * @codec: the codec instance
 * Enable I2C clocks.
 * This must be called when the i2c mutex is locked.
 */
static void cs8409_enable_i2c_clock(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Cancel the disable timer, but do not wait for any running disable functions to finish.
	 * If the disable timer runs out before cancel, the delayed work thread will be blocked,
	 * waiting for the mutex to become unlocked. This mutex will be locked for the duration of
	 * any i2c transaction, so the disable function will run to completion immediately
	 * afterwards in the scenario. The next enable call will re-enable the clock, regardless.
	 */
	cancel_delayed_work(&spec->i2c_clk_work);

	if (!spec->i2c_clck_enabled) {
		cs8409_vendor_coef_set(codec, 0x0, cs8409_vendor_coef_get(codec, 0x0) | 0x8);
		spec->i2c_clck_enabled = 1;
	}
	queue_delayed_work(system_power_efficient_wq, &spec->i2c_clk_work, msecs_to_jiffies(25));
}

/**
 * cs8409_i2c_wait_complete - Wait for I2C transaction
 * @codec: the codec instance
 *
 * Wait for I2C transaction to complete.
 * Return -ETIMEDOUT if transaction wait times out.
 */
static int cs8409_i2c_wait_complete(struct hda_codec *codec)
{
	unsigned int retval;

	return read_poll_timeout(cs8409_vendor_coef_get, retval, retval & 0x18,
		CS42L42_I2C_SLEEP_US, CS42L42_I2C_TIMEOUT_US, false, codec, CS8409_I2C_STS);
}

/**
 * cs8409_set_i2c_dev_addr - Set i2c address for transaction
 * @codec: the codec instance
 * @addr: I2C Address
 */
static void cs8409_set_i2c_dev_addr(struct hda_codec *codec, unsigned int addr)
{
	struct cs8409_spec *spec = codec->spec;

	if (spec->dev_addr != addr) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_ADDR, addr);
		spec->dev_addr = addr;
	}
}

/**
 * cs8409_i2c_set_page - CS8409 I2C set page register.
 * @scodec: the codec instance
 * @i2c_reg: Page register
 *
 * Returns negative on error.
 */
static int cs8409_i2c_set_page(struct sub_codec *scodec, unsigned int i2c_reg)
{
	struct hda_codec *codec = scodec->codec;

	if (scodec->paged && (scodec->last_page != (i2c_reg >> 8))) {
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg >> 8);
		if (cs8409_i2c_wait_complete(codec) < 0)
			return -EIO;
		scodec->last_page = i2c_reg >> 8;
	}

	return 0;
}

/**
 * cs8409_i2c_read - CS8409 I2C Read.
 * @scodec: the codec instance
 * @addr: Register to read
 *
 * Returns negative on error, otherwise returns read value in bits 0-7.
 */
static int cs8409_i2c_read(struct sub_codec *scodec, unsigned int addr)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	unsigned int read_data;

	if (scodec->suspended)
		return -EPERM;

	mutex_lock(&spec->i2c_mux);
	cs8409_enable_i2c_clock(codec);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	if (cs8409_i2c_set_page(scodec, addr))
		goto error;

	i2c_reg_data = (addr << 8) & 0x0ffff;
	cs8409_vendor_coef_set(codec, CS8409_I2C_QREAD, i2c_reg_data);
	if (cs8409_i2c_wait_complete(codec) < 0)
		goto error;

	/* Register in bits 15-8 and the data in 7-0 */
	read_data = cs8409_vendor_coef_get(codec, CS8409_I2C_QREAD);

	mutex_unlock(&spec->i2c_mux);
	return read_data & 0x0ff;

error:
	mutex_unlock(&spec->i2c_mux);
	codec_err(codec, "%s() Failed 0x%02x : 0x%04x\n", __func__, scodec->addr, addr);
	return -EIO;
}

/**
 * cs8409_i2c_bulk_read - CS8409 I2C Read Sequence.
 * @scodec: the codec instance
 * @seq: Register Sequence to read
 * @count: Number of registeres to read
 *
 * Returns negative on error, values are read into value element of cs8409_i2c_param sequence.
 */
static int cs8409_i2c_bulk_read(struct sub_codec *scodec, struct cs8409_i2c_param *seq, int count)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	int i;

	if (scodec->suspended)
		return -EPERM;

	mutex_lock(&spec->i2c_mux);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	for (i = 0; i < count; i++) {
		cs8409_enable_i2c_clock(codec);
		if (cs8409_i2c_set_page(scodec, seq[i].addr))
			goto error;

		i2c_reg_data = (seq[i].addr << 8) & 0x0ffff;
		cs8409_vendor_coef_set(codec, CS8409_I2C_QREAD, i2c_reg_data);

		if (cs8409_i2c_wait_complete(codec) < 0)
			goto error;

		seq[i].value = cs8409_vendor_coef_get(codec, CS8409_I2C_QREAD) & 0xff;
	}

	mutex_unlock(&spec->i2c_mux);

	return 0;

error:
	mutex_unlock(&spec->i2c_mux);
	codec_err(codec, "I2C Bulk Write Failed 0x%02x\n", scodec->addr);
	return -EIO;
}

/**
 * cs8409_i2c_write - CS8409 I2C Write.
 * @scodec: the codec instance
 * @addr: Register to write to
 * @value: Data to write
 *
 * Returns negative on error, otherwise returns 0.
 */
static int cs8409_i2c_write(struct sub_codec *scodec, unsigned int addr, unsigned int value)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;

	if (scodec->suspended)
		return -EPERM;

	mutex_lock(&spec->i2c_mux);

	cs8409_enable_i2c_clock(codec);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	if (cs8409_i2c_set_page(scodec, addr))
		goto error;

	i2c_reg_data = ((addr << 8) & 0x0ff00) | (value & 0x0ff);
	cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg_data);

	if (cs8409_i2c_wait_complete(codec) < 0)
		goto error;

	mutex_unlock(&spec->i2c_mux);
	return 0;

error:
	mutex_unlock(&spec->i2c_mux);
	codec_err(codec, "%s() Failed 0x%02x : 0x%04x\n", __func__, scodec->addr, addr);
	return -EIO;
}

/**
 * cs8409_i2c_bulk_write - CS8409 I2C Write Sequence.
 * @scodec: the codec instance
 * @seq: Register Sequence to write
 * @count: Number of registeres to write
 *
 * Returns negative on error.
 */
static int cs8409_i2c_bulk_write(struct sub_codec *scodec, const struct cs8409_i2c_param *seq,
				 int count)
{
	struct hda_codec *codec = scodec->codec;
	struct cs8409_spec *spec = codec->spec;
	unsigned int i2c_reg_data;
	int i;

	if (scodec->suspended)
		return -EPERM;

	mutex_lock(&spec->i2c_mux);
	cs8409_set_i2c_dev_addr(codec, scodec->addr);

	for (i = 0; i < count; i++) {
		cs8409_enable_i2c_clock(codec);
		if (cs8409_i2c_set_page(scodec, seq[i].addr))
			goto error;

		i2c_reg_data = ((seq[i].addr << 8) & 0x0ff00) | (seq[i].value & 0x0ff);
		cs8409_vendor_coef_set(codec, CS8409_I2C_QWRITE, i2c_reg_data);

		if (cs8409_i2c_wait_complete(codec) < 0)
			goto error;
	}

	mutex_unlock(&spec->i2c_mux);

	return 0;

error:
	mutex_unlock(&spec->i2c_mux);
	codec_err(codec, "I2C Bulk Write Failed 0x%02x\n", scodec->addr);
	return -EIO;
}

static int cs8409_init(struct hda_codec *codec)
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

/* Enable/Disable Unsolicited Response */
static void cs8409_enable_ur(struct hda_codec *codec, int flag)
{
	struct cs8409_spec *spec = codec->spec;
	unsigned int ur_gpios = 0;
	int i;

	for (i = 0; i < spec->num_scodecs; i++)
		ur_gpios |= spec->scodecs[i]->irq_mask;

	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_UNSOLICITED_RSP_MASK,
			    flag ? ur_gpios : 0);

	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_UNSOLICITED_ENABLE,
			    flag ? AC_UNSOL_ENABLED : 0);
}

static void cs8409_fix_caps(struct hda_codec *codec, unsigned int nid)
{
	int caps;

	/* CS8409 is simple HDA bridge and intended to be used with a remote
	 * companion codec. Most of input/output PIN(s) have only basic
	 * capabilities. Receive and Transmit NID(s) have only OUTC and INC
	 * capabilities and no presence detect capable (PDC) and call to
	 * snd_hda_gen_build_controls() will mark them as non detectable
	 * phantom jacks. However, a companion codec may be
	 * connected to these pins which supports jack detect
	 * capabilities. We have to override pin capabilities,
	 * otherwise they will not be created as input devices.
	 */
	caps = snd_hdac_read_parm(&codec->core, nid, AC_PAR_PIN_CAP);
	if (caps >= 0)
		snd_hdac_override_parm(&codec->core, nid, AC_PAR_PIN_CAP,
				       (caps | (AC_PINCAP_IMP_SENSE | AC_PINCAP_PRES_DETECT)));

	snd_hda_override_wcaps(codec, nid, (get_wcaps(codec, nid) | AC_WCAP_UNSOL_CAP));
}

/******************************************************************************
 *                        CS42L42 Specific Functions
 ******************************************************************************/

int cs42l42_volume_info(struct snd_kcontrol *kctrl, struct snd_ctl_elem_info *uinfo)
{
	unsigned int ofs = get_amp_offset(kctrl);
	u8 chs = get_amp_channels(kctrl);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->value.integer.step = 1;
	uinfo->count = chs == 3 ? 2 : 1;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		uinfo->value.integer.min = CS42L42_HP_VOL_REAL_MIN;
		uinfo->value.integer.max = CS42L42_HP_VOL_REAL_MAX;
		break;
	case CS42L42_VOL_ADC:
		uinfo->value.integer.min = CS42L42_AMIC_VOL_REAL_MIN;
		uinfo->value.integer.max = CS42L42_AMIC_VOL_REAL_MAX;
		break;
	default:
		break;
	}

	return 0;
}

int cs42l42_volume_get(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[get_amp_index(kctrl)];
	int chs = get_amp_channels(kctrl);
	unsigned int ofs = get_amp_offset(kctrl);
	long *valp = uctrl->value.integer.value;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		if (chs & BIT(0))
			*valp++ = cs42l42->vol[ofs];
		if (chs & BIT(1))
			*valp = cs42l42->vol[ofs+1];
		break;
	case CS42L42_VOL_ADC:
		if (chs & BIT(0))
			*valp = cs42l42->vol[ofs];
		break;
	default:
		break;
	}

	return 0;
}

int cs42l42_volume_put(struct snd_kcontrol *kctrl, struct snd_ctl_elem_value *uctrl)
{
	struct hda_codec *codec = snd_kcontrol_chip(kctrl);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[get_amp_index(kctrl)];
	int chs = get_amp_channels(kctrl);
	unsigned int ofs = get_amp_offset(kctrl);
	long *valp = uctrl->value.integer.value;

	switch (ofs) {
	case CS42L42_VOL_DAC:
		if (chs & BIT(0)) {
			cs42l42->vol[ofs] = *valp;
			cs8409_i2c_write(cs42l42, CS42L42_REG_HS_VOL_CHA,
					 -(cs42l42->vol[ofs]) & CS42L42_REG_HS_VOL_MASK);
		}
		if (chs & BIT(1)) {
			ofs++;
			valp++;
			cs42l42->vol[ofs] = *valp;
			cs8409_i2c_write(cs42l42, CS42L42_REG_HS_VOL_CHB,
					 -(cs42l42->vol[ofs]) & CS42L42_REG_HS_VOL_MASK);
		}
		break;
	case CS42L42_VOL_ADC:
		if (chs & BIT(0)) {
			cs42l42->vol[ofs] = *valp;
			cs8409_i2c_write(cs42l42, CS42L42_REG_AMIC_VOL,
					 cs42l42->vol[ofs] & CS42L42_REG_AMIC_VOL_MASK);
		}
		break;
	default:
		break;
	}

	return 0;
}

/* Configure CS42L42 slave codec for jack autodetect */
static void cs42l42_enable_jack_detect(struct sub_codec *cs42l42)
{
	cs8409_i2c_write(cs42l42, 0x1b70, cs42l42->hsbias_hiz);
	/* Clear WAKE# */
	cs8409_i2c_write(cs42l42, 0x1b71, 0x00C1);
	/* Wait ~2.5ms */
	usleep_range(2500, 3000);
	/* Set mode WAKE# output follows the combination logic directly */
	cs8409_i2c_write(cs42l42, 0x1b71, 0x00C0);
	/* Clear interrupts status */
	cs8409_i2c_read(cs42l42, 0x130f);
	/* Enable interrupt */
	cs8409_i2c_write(cs42l42, 0x1320, 0xF3);
}

/* Enable and run CS42L42 slave codec jack auto detect */
static void cs42l42_run_jack_detect(struct sub_codec *cs42l42)
{
	/* Clear interrupts */
	cs8409_i2c_read(cs42l42, 0x1308);
	cs8409_i2c_read(cs42l42, 0x1b77);
	cs8409_i2c_write(cs42l42, 0x1320, 0xFF);
	cs8409_i2c_read(cs42l42, 0x130f);

	cs8409_i2c_write(cs42l42, 0x1102, 0x87);
	cs8409_i2c_write(cs42l42, 0x1f06, 0x86);
	cs8409_i2c_write(cs42l42, 0x1b74, 0x07);
	cs8409_i2c_write(cs42l42, 0x131b, 0xFD);
	cs8409_i2c_write(cs42l42, 0x1120, 0x80);
	/* Wait ~110ms*/
	usleep_range(110000, 200000);
	cs8409_i2c_write(cs42l42, 0x111f, 0x77);
	cs8409_i2c_write(cs42l42, 0x1120, 0xc0);
	/* Wait ~10ms */
	usleep_range(10000, 25000);
}

static int cs42l42_handle_tip_sense(struct sub_codec *cs42l42, unsigned int reg_ts_status)
{
	int status_changed = 0;

	/* TIP_SENSE INSERT/REMOVE */
	switch (reg_ts_status) {
	case CS42L42_JACK_INSERTED:
		if (!cs42l42->hp_jack_in) {
			if (cs42l42->no_type_dect) {
				status_changed = 1;
				cs42l42->hp_jack_in = 1;
				cs42l42->mic_jack_in = 0;
			} else {
				cs42l42_run_jack_detect(cs42l42);
			}
		}
		break;

	case CS42L42_JACK_REMOVED:
		if (cs42l42->hp_jack_in || cs42l42->mic_jack_in) {
			status_changed = 1;
			cs42l42->hp_jack_in = 0;
			cs42l42->mic_jack_in = 0;
		}
		break;
	default:
		/* jack in transition */
		break;
	}

	return status_changed;
}

static int cs42l42_jack_unsol_event(struct sub_codec *cs42l42)
{
	int status_changed = 0;
	int reg_cdc_status;
	int reg_hs_status;
	int reg_ts_status;
	int type;

	/* Read jack detect status registers */
	reg_cdc_status = cs8409_i2c_read(cs42l42, 0x1308);
	reg_hs_status = cs8409_i2c_read(cs42l42, 0x1124);
	reg_ts_status = cs8409_i2c_read(cs42l42, 0x130f);

	/* If status values are < 0, read error has occurred. */
	if (reg_cdc_status < 0 || reg_hs_status < 0 || reg_ts_status < 0)
		return -EIO;

	/* HSDET_AUTO_DONE */
	if (reg_cdc_status & CS42L42_HSDET_AUTO_DONE) {

		/* Disable HSDET_AUTO_DONE */
		cs8409_i2c_write(cs42l42, 0x131b, 0xFF);

		type = ((reg_hs_status & CS42L42_HSTYPE_MASK) + 1);

		if (cs42l42->no_type_dect) {
			status_changed = cs42l42_handle_tip_sense(cs42l42, reg_ts_status);
		} else if (type == 4) {
			/* Type 4 not supported	*/
			status_changed = cs42l42_handle_tip_sense(cs42l42, CS42L42_JACK_REMOVED);
		} else {
			if (!cs42l42->hp_jack_in) {
				status_changed = 1;
				cs42l42->hp_jack_in = 1;
			}
			/* type = 3 has no mic */
			if ((!cs42l42->mic_jack_in) && (type != 3)) {
				status_changed = 1;
				cs42l42->mic_jack_in = 1;
			}
		}
		/* Re-Enable Tip Sense Interrupt */
		cs8409_i2c_write(cs42l42, 0x1320, 0xF3);
	} else {
		status_changed = cs42l42_handle_tip_sense(cs42l42, reg_ts_status);
	}

	return status_changed;
}

static void cs42l42_resume(struct sub_codec *cs42l42)
{
	struct hda_codec *codec = cs42l42->codec;
	unsigned int gpio_data;
	struct cs8409_i2c_param irq_regs[] = {
		{ 0x1308, 0x00 },
		{ 0x1309, 0x00 },
		{ 0x130A, 0x00 },
		{ 0x130F, 0x00 },
	};

	/* Bring CS42L42 out of Reset */
	gpio_data = snd_hda_codec_read(codec, CS8409_PIN_AFG, 0, AC_VERB_GET_GPIO_DATA, 0);
	gpio_data |= cs42l42->reset_gpio;
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, gpio_data);
	usleep_range(10000, 15000);

	cs42l42->suspended = 0;

	/* Initialize CS42L42 companion codec */
	cs8409_i2c_bulk_write(cs42l42, cs42l42->init_seq, cs42l42->init_seq_num);

	/* Clear interrupts, by reading interrupt status registers */
	cs8409_i2c_bulk_read(cs42l42, irq_regs, ARRAY_SIZE(irq_regs));

	/* Restore Volumes after Resume */
	cs8409_i2c_write(cs42l42, CS42L42_REG_HS_VOL_CHA,
			 -(cs42l42->vol[1]) & CS42L42_REG_HS_VOL_MASK);
	cs8409_i2c_write(cs42l42, CS42L42_REG_HS_VOL_CHB,
			 -(cs42l42->vol[2]) & CS42L42_REG_HS_VOL_MASK);
	cs8409_i2c_write(cs42l42, CS42L42_REG_AMIC_VOL,
			 cs42l42->vol[0] & CS42L42_REG_AMIC_VOL_MASK);

	if (cs42l42->full_scale_vol)
		cs8409_i2c_write(cs42l42, 0x2001, 0x01);

	cs42l42_enable_jack_detect(cs42l42);
}

#ifdef CONFIG_PM
static void cs42l42_suspend(struct sub_codec *cs42l42)
{
	struct hda_codec *codec = cs42l42->codec;
	unsigned int gpio_data;

	/* Power down CS42L42 ASP/EQ/MIX/HP */
	cs8409_i2c_write(cs42l42, 0x1101, 0xfe);
	cs42l42->suspended = 1;
	cs42l42->last_page = 0;

	/* Put CS42L42 into Reset */
	gpio_data = snd_hda_codec_read(codec, CS8409_PIN_AFG, 0, AC_VERB_GET_GPIO_DATA, 0);
	gpio_data &= ~cs42l42->reset_gpio;
	snd_hda_codec_write(codec, CS8409_PIN_AFG, 0, AC_VERB_SET_GPIO_DATA, gpio_data);
}
#endif

static void cs8409_free(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;

	/* Cancel i2c clock disable timer, and disable clock if left enabled */
	cancel_delayed_work_sync(&spec->i2c_clk_work);
	cs8409_disable_i2c_clock(codec);

	snd_hda_gen_free(codec);
}

/******************************************************************************
 *                   BULLSEYE / WARLOCK / CYBORG Specific Functions
 *                               CS8409/CS42L42
 ******************************************************************************/

/*
 * In the case of CS8409 we do not have unsolicited events from NID's 0x24
 * and 0x34 where hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via gpio 4 to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void cs8409_cs42l42_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];
	struct hda_jack_tbl *jk;

	/* jack_unsol_event() will be called every time gpio line changing state.
	 * In this case gpio4 line goes up as a result of reading interrupt status
	 * registers in previous cs8409_jack_unsol_event() call.
	 * We don't need to handle this event, ignoring...
	 */
	if (res & cs42l42->irq_mask)
		return;

	if (cs42l42_jack_unsol_event(cs42l42)) {
		snd_hda_set_pin_ctl(codec, CS8409_CS42L42_SPK_PIN_NID,
				    cs42l42->hp_jack_in ? 0 : PIN_OUT);
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_HP_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							AC_UNSOL_RES_TAG);
		/* Report jack*/
		jk = snd_hda_jack_tbl_get_mst(codec, CS8409_CS42L42_AMIC_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec, (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
							 AC_UNSOL_RES_TAG);
	}
}

#ifdef CONFIG_PM
/* Manage PDREF, when transition to D3hot */
static int cs8409_cs42l42_suspend(struct hda_codec *codec)
{
	struct cs8409_spec *spec = codec->spec;
	int i;

	cs8409_enable_ur(codec, 0);

	for (i = 0; i < spec->num_scodecs; i++)
		cs42l42_suspend(spec->scodecs[i]);

	/* Cancel i2c clock disable timer, and disable clock if left enabled */
	cancel_delayed_work_sync(&spec->i2c_clk_work);
	cs8409_disable_i2c_clock(codec);

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
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

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

	if (codec->fixup_id == CS8409_BULLSEYE) {
		for (; seq_bullseye->nid; seq_bullseye++)
			cs8409_vendor_coef_set(codec, seq_bullseye->cir, seq_bullseye->coeff);
	}

	/* DMIC1_MO=00b, DMIC1/2_SR=1 */
	if (codec->fixup_id == CS8409_WARLOCK || codec->fixup_id == CS8409_CYBORG)
		cs8409_vendor_coef_set(codec, 0x09, 0x0003);

	cs42l42_resume(cs42l42);

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static const struct hda_codec_ops cs8409_cs42l42_patch_ops = {
	.build_controls = cs8409_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs8409_init,
	.free = cs8409_free,
	.unsol_event = cs8409_cs42l42_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs8409_cs42l42_suspend,
#endif
};

static int cs8409_cs42l42_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
				    unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

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
			*res = (cs42l42->hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	case CS8409_CS42L42_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
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

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, cs8409_cs42l42_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = cs8409_cs42l42_exec_verb;

		spec->scodecs[CS8409_CODEC0] = &cs8409_cs42l42_codec;
		spec->num_scodecs = 1;
		spec->scodecs[CS8409_CODEC0]->codec = codec;
		codec->patch_ops = cs8409_cs42l42_patch_ops;

		spec->gen.suppress_auto_mute = 1;
		spec->gen.no_primary_hp = 1;
		spec->gen.suppress_vmaster = 1;

		/* GPIO 5 out, 3,4 in */
		spec->gpio_dir = spec->scodecs[CS8409_CODEC0]->reset_gpio;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, cs8409_cs42l42_init_verbs);

		cs8409_fix_caps(codec, CS8409_CS42L42_HP_PIN_NID);
		cs8409_fix_caps(codec, CS8409_CS42L42_AMIC_PIN_NID);

		/* Set TIP_SENSE_EN for analog front-end of tip sense.
		 * Additionally set HSBIAS_SENSE_EN and Full Scale volume for some variants.
		 */
		switch (codec->fixup_id) {
		case CS8409_WARLOCK:
			spec->scodecs[CS8409_CODEC0]->hsbias_hiz = 0x0020;
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = 1;
			break;
		case CS8409_BULLSEYE:
			spec->scodecs[CS8409_CODEC0]->hsbias_hiz = 0x0020;
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = 0;
			break;
		case CS8409_CYBORG:
			spec->scodecs[CS8409_CODEC0]->hsbias_hiz = 0x00a0;
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = 1;
			break;
		default:
			spec->scodecs[CS8409_CODEC0]->hsbias_hiz = 0x0003;
			spec->scodecs[CS8409_CODEC0]->full_scale_vol = 1;
			break;
		}

		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Fix Sample Rate to 48kHz */
		spec->gen.stream_analog_playback = &cs42l42_48k_pcm_analog_playback;
		spec->gen.stream_analog_capture = &cs42l42_48k_pcm_analog_capture;
		/* Set initial DMIC volume to -26 dB */
		snd_hda_codec_amp_init_stereo(codec, CS8409_CS42L42_DMIC_ADC_PIN_NID,
					      HDA_INPUT, 0, 0xff, 0x19);
		snd_hda_gen_add_kctl(&spec->gen, "Headphone Playback Volume",
				&cs42l42_dac_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, "Mic Capture Volume",
				&cs42l42_adc_volume_mixer);
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
		cs42l42_run_jack_detect(spec->scodecs[CS8409_CODEC0]);
		usleep_range(100000, 150000);
		break;
	default:
		break;
	}
}

/******************************************************************************
 *                          Dolphin Specific Functions
 *                               CS8409/ 2 X CS42L42
 ******************************************************************************/

/*
 * In the case of CS8409 we do not have unsolicited events when
 * hs mic and hp are connected. Companion codec CS42L42 will
 * generate interrupt via irq_mask to notify jack events. We have to overwrite
 * generic snd_hda_jack_unsol_event(), read CS42L42 jack detect status registers
 * and then notify status via generic snd_hda_jack_unsol_event() call.
 */
static void dolphin_jack_unsol_event(struct hda_codec *codec, unsigned int res)
{
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	struct hda_jack_tbl *jk;

	cs42l42 = spec->scodecs[CS8409_CODEC0];
	if (!cs42l42->suspended && (~res & cs42l42->irq_mask) &&
	    cs42l42_jack_unsol_event(cs42l42)) {
		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_HP_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);

		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_AMIC_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);
	}

	cs42l42 = spec->scodecs[CS8409_CODEC1];
	if (!cs42l42->suspended && (~res & cs42l42->irq_mask) &&
	    cs42l42_jack_unsol_event(cs42l42)) {
		jk = snd_hda_jack_tbl_get_mst(codec, DOLPHIN_LO_PIN_NID, 0);
		if (jk)
			snd_hda_jack_unsol_event(codec,
						 (jk->tag << AC_UNSOL_RES_TAG_SHIFT) &
						  AC_UNSOL_RES_TAG);
	}
}

/* Vendor specific HW configuration
 * PLL, ASP, I2C, SPI, GPIOs, DMIC etc...
 */
static void dolphin_hw_init(struct hda_codec *codec)
{
	const struct cs8409_cir_param *seq = dolphin_hw_cfg;
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42;
	int i;

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

	for (i = 0; i < spec->num_scodecs; i++) {
		cs42l42 = spec->scodecs[i];
		cs42l42_resume(cs42l42);
	}

	/* Enable Unsolicited Response */
	cs8409_enable_ur(codec, 1);
}

static const struct hda_codec_ops cs8409_dolphin_patch_ops = {
	.build_controls = cs8409_build_controls,
	.build_pcms = snd_hda_gen_build_pcms,
	.init = cs8409_init,
	.free = cs8409_free,
	.unsol_event = dolphin_jack_unsol_event,
#ifdef CONFIG_PM
	.suspend = cs8409_cs42l42_suspend,
#endif
};

static int dolphin_exec_verb(struct hdac_device *dev, unsigned int cmd, unsigned int flags,
			     unsigned int *res)
{
	struct hda_codec *codec = container_of(dev, struct hda_codec, core);
	struct cs8409_spec *spec = codec->spec;
	struct sub_codec *cs42l42 = spec->scodecs[CS8409_CODEC0];

	unsigned int nid = ((cmd >> 20) & 0x07f);
	unsigned int verb = ((cmd >> 8) & 0x0fff);

	/* CS8409 pins have no AC_PINSENSE_PRESENCE
	 * capabilities. We have to intercept calls for CS42L42 pins
	 * and return correct pin sense values for read_pin_sense() call from
	 * hda_jack based on CS42L42 jack detect status.
	 */
	switch (nid) {
	case DOLPHIN_HP_PIN_NID:
	case DOLPHIN_LO_PIN_NID:
		if (nid == DOLPHIN_LO_PIN_NID)
			cs42l42 = spec->scodecs[CS8409_CODEC1];
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->hp_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	case DOLPHIN_AMIC_PIN_NID:
		if (verb == AC_VERB_GET_PIN_SENSE) {
			*res = (cs42l42->mic_jack_in) ? AC_PINSENSE_PRESENCE : 0;
			return 0;
		}
		break;
	default:
		break;
	}

	return spec->exec_verb(dev, cmd, flags, res);
}

void dolphin_fixups(struct hda_codec *codec, const struct hda_fixup *fix, int action)
{
	struct cs8409_spec *spec = codec->spec;
	struct snd_kcontrol_new *kctrl;
	int i;

	switch (action) {
	case HDA_FIXUP_ACT_PRE_PROBE:
		snd_hda_add_verbs(codec, dolphin_init_verbs);
		/* verb exec op override */
		spec->exec_verb = codec->core.exec_verb;
		codec->core.exec_verb = dolphin_exec_verb;

		spec->scodecs[CS8409_CODEC0] = &dolphin_cs42l42_0;
		spec->scodecs[CS8409_CODEC0]->codec = codec;
		spec->scodecs[CS8409_CODEC1] = &dolphin_cs42l42_1;
		spec->scodecs[CS8409_CODEC1]->codec = codec;
		spec->num_scodecs = 2;

		codec->patch_ops = cs8409_dolphin_patch_ops;

		/* GPIO 1,5 out, 0,4 in */
		spec->gpio_dir = spec->scodecs[CS8409_CODEC0]->reset_gpio |
				 spec->scodecs[CS8409_CODEC1]->reset_gpio;
		spec->gpio_data = 0;
		spec->gpio_mask = 0x03f;

		/* Basic initial sequence for specific hw configuration */
		snd_hda_sequence_write(codec, dolphin_init_verbs);

		snd_hda_jack_add_kctl(codec, DOLPHIN_LO_PIN_NID, "Line Out", true,
				      SND_JACK_HEADPHONE, NULL);

		cs8409_fix_caps(codec, DOLPHIN_HP_PIN_NID);
		cs8409_fix_caps(codec, DOLPHIN_LO_PIN_NID);
		cs8409_fix_caps(codec, DOLPHIN_AMIC_PIN_NID);

		break;
	case HDA_FIXUP_ACT_PROBE:
		/* Fix Sample Rate to 48kHz */
		spec->gen.stream_analog_playback = &cs42l42_48k_pcm_analog_playback;
		spec->gen.stream_analog_capture = &cs42l42_48k_pcm_analog_capture;
		snd_hda_gen_add_kctl(&spec->gen, "Headphone Playback Volume",
				     &cs42l42_dac_volume_mixer);
		snd_hda_gen_add_kctl(&spec->gen, "Mic Capture Volume", &cs42l42_adc_volume_mixer);
		kctrl = snd_hda_gen_add_kctl(&spec->gen, "Line Out Playback Volume",
					     &cs42l42_dac_volume_mixer);
		/* Update Line Out kcontrol template */
		kctrl->private_value = HDA_COMPOSE_AMP_VAL_OFS(DOLPHIN_HP_PIN_NID, 3, CS8409_CODEC1,
				       HDA_OUTPUT, CS42L42_VOL_DAC) | HDA_AMP_VAL_MIN_MUTE;
		cs8409_enable_ur(codec, 0);
		dolphin_hw_init(codec);
		snd_hda_codec_set_name(codec, "CS8409/CS42L42");
		break;
	case HDA_FIXUP_ACT_INIT:
		dolphin_hw_init(codec);
		fallthrough;
	case HDA_FIXUP_ACT_BUILD:
		/* Run jack auto detect first time on boot
		 * after controls have been added, to check if jack has
		 * been already plugged in.
		 * Run immediately after init.
		 */
		for (i = 0; i < spec->num_scodecs; i++) {
			cs42l42_run_jack_detect(spec->scodecs[i]);
			usleep_range(100000, 150000);
		}

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
		cs8409_free(codec);
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
