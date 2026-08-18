// SPDX-License-Identifier: GPL-2.0-only
//
// rt766-sdca.c -- rt766 SDCA ALSA SoC audio driver
//
// Copyright(c) 2026 Realtek Semiconductor Corp.
//
//

#include <linux/bitops.h>
#include <sound/core.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <sound/initval.h>
#include <sound/jack.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_registers.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/sdw.h>
#include <sound/sdca.h>
#include <sound/sdca_asoc.h>
#include <sound/sdca_function.h>
#include <sound/sdca_hid.h>
#include <sound/sdca_regmap.h>
#include <sound/sdca_interrupts.h>
#include <linux/slab.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>
#include "rt766-sdca.h"
#include "rt-sdw-common.h"

static int rt766_sdca_btn_detect(struct sdca_interrupt *interrupt)
{
	struct rt766_sdca_priv *rt766 = interrupt->priv;
	unsigned char *buf = NULL;
	unsigned int offset, owner, length;
	unsigned int det_mode, idx, val;
	int ret;

	ret = regmap_read(rt766->regmap,
		RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_DETECTED_MODE),
		&det_mode);
	if (ret < 0)
		goto io_error;

	/* get current UMP message owner */
	ret = regmap_read(rt766->regmap,
		RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_CURRENTOWNER),
		&owner);
	if (ret < 0)
		goto io_error;

	/* if owner is device then there is no button event from device */
	if (owner == 1)
		return 0;

	if (det_mode) {
		/* read UMP message length */
		ret = regmap_read(rt766->regmap,
			RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_MESSAGELENGTH),
			&length);
		if (ret < 0)
			goto _end_btn_det_;

		/* read UMP message offset */
		ret = regmap_read(rt766->regmap,
			RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_MESSAGEOFFSET),
			&offset);
		if (ret < 0)
			goto _end_btn_det_;

		buf = devm_kzalloc(&rt766->slave->dev, length, GFP_KERNEL);
		if (!buf) {
			dev_err(&rt766->slave->dev, "%s: alloc buf failed\n", __func__);
			goto _end_btn_det_;
		}

		for (idx = 0; idx < length; idx++) {
			ret = regmap_read(rt766->regmap,
				RT766_BUF_ADDR_HID1 + offset + idx, &val);
			if (ret < 0)
				goto _end_btn_det_;
			buf[idx] = val & 0xff;
		}

		if (rt766->hid)
			hid_input_report(rt766->hid, HID_INPUT_REPORT,
				buf, length, 1);
	}

_end_btn_det_:
	if (buf)
		devm_kfree(&rt766->slave->dev, buf);

	/* Host is owner, so set back to device */
	if (owner == 0) {
		regmap_write(rt766->regmap,
			RT766_SDCA_CTL(HID, HID101, SDCA_CTL_HIDE_HIDTX_CURRENTOWNER), 0x01);
	}

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static irqreturn_t rt766_sdca_irq_btn_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct rt766_sdca_priv *rt766 = interrupt->priv;

	if (!rt766->hs_jack)
		return IRQ_HANDLED;

	if (!rt766->component->card || !rt766->component->card->instantiated)
		return IRQ_HANDLED;

	mutex_lock(&rt766->disable_irq_lock);
	if (!rt766->disable_irq)
		rt766_sdca_btn_detect(interrupt);
	mutex_unlock(&rt766->disable_irq_lock);
	return IRQ_HANDLED;
}

static int rt766_sdca_headset_detect(struct rt766_sdca_priv *rt766)
{
	unsigned int det_mode;
	int ret;

	/* get detected_mode */
	ret = regmap_read(rt766->regmap,
		RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_DETECTED_MODE),
		&det_mode);
	if (ret < 0)
		goto io_error;

	switch (det_mode) {
	case 0x00:
		rt766->jack_type = 0;
		break;
	case 0x03:
		rt766->jack_type = SND_JACK_HEADPHONE;
		break;
	case 0x05:
		rt766->jack_type = SND_JACK_HEADSET;
		break;
	}

	/* write selected_mode */
	if (det_mode) {
		ret = regmap_write(rt766->regmap,
			RT766_SDCA_CTL(UAJ, GE49, SDCA_CTL_GE_SELECTED_MODE),
			det_mode);
		if (ret < 0)
			goto io_error;
	}

	dev_dbg(&rt766->slave->dev,
		"%s, detected_mode=0x%x\n", __func__, det_mode);

	return 0;

io_error:
	pr_err_ratelimited("IO error in %s, ret %d\n", __func__, ret);
	return ret;
}

static irqreturn_t rt766_sdca_irq_jd_handler(int irq, void *data)
{
	struct sdca_interrupt *interrupt = data;
	struct rt766_sdca_priv *rt766 = interrupt->priv;

	if (!rt766->hs_jack)
		return IRQ_HANDLED;

	if (!rt766->component->card || !rt766->component->card->instantiated)
		return IRQ_HANDLED;

	mutex_lock(&rt766->disable_irq_lock);
	if (!rt766->disable_irq)
		rt766_sdca_headset_detect(rt766);
	mutex_unlock(&rt766->disable_irq_lock);

	dev_dbg(&rt766->slave->dev,
		"in %s, jack_type=%d\n", __func__, rt766->jack_type);

	snd_soc_jack_report(rt766->hs_jack, rt766->jack_type, SND_JACK_HEADSET);
	return IRQ_HANDLED;
}

static void rt766_sdca_destroy_hid_device(struct sdca_interrupt *interrupt)
{
	struct rt766_sdca_priv *rt766 = interrupt->priv;

	hid_destroy_device(rt766->hid);
}

static int rt766_sdca_irq_ctl(struct rt766_sdca_priv *rt766,
							  struct sdca_function_data *function,
							  struct snd_soc_component *component,
							  struct sdca_interrupt_info *info,
							  bool enabled)
{
	struct device *dev = &rt766->slave->dev;
	struct sdca_interrupt *interrupt;
	struct sdca_control *control;
	struct sdca_entity *entity;
	irq_handler_t handler;
	int i, j, irq, ret;

	for (i = 0; i < function->num_entities; i++) {
		entity = &function->entities[i];

		for (j = 0; j < entity->num_controls; j++) {
			control = &entity->controls[j];
			irq = control->interrupt_position;

			switch (SDCA_CTL_TYPE(entity->type, control->sel)) {
			case SDCA_CTL_TYPE_S(GE, DETECTED_MODE):
				handler = rt766_sdca_irq_jd_handler;
				break;
			case SDCA_CTL_TYPE_S(HIDE, HIDTX_CURRENTOWNER):
				handler = rt766_sdca_irq_btn_handler;
				break;
			default:
				continue;
			}

			interrupt = &info->irqs[irq];

			if (enabled) {
				ret = sdca_irq_data_populate(dev, rt766->regmap, component,
								function, entity, control,
								interrupt);
				if (ret)
					return ret;

				if (handler == rt766_sdca_irq_btn_handler) {
					ret = sdca_add_hid_device(interrupt);
					if (ret)
						return ret;

					interrupt->free_priv = rt766_sdca_destroy_hid_device;
					rt766->hid = interrupt->priv;
				}

				interrupt->priv = rt766;
				ret = sdca_irq_request(dev, info, irq, interrupt->name,
								handler, interrupt);
				if (ret) {
					dev_err(dev, "failed to request irq %s: %d\n",
						interrupt->name, ret);
					sdca_irq_cleanup_late(dev, function, info);
					return ret;
				}
				dev_dbg(dev, "Requesting IRQ %d InterruptName=%s\n", irq, interrupt->name);
			} else {
				sdca_irq_cleanup_late(dev, function, info);
				dev_dbg(dev, "Freeing IRQ %d\n", irq);
			}
		}
	}

	return 0;
}

static int rt766_sdca_set_jack_detect(struct snd_soc_component *component,
	struct snd_soc_jack *hs_jack, void *data)
{
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	int ret;

	if (!rt766->uaj_func_data) {
		dev_err(&rt766->slave->dev, "The SDCA UAJ function is not supported.\n");
		return -EINVAL;
	}

	rt766->hs_jack = hs_jack;

	if (!rt766->first_hw_init)
		return 0;

	ret = pm_runtime_resume_and_get(component->dev);
	if (ret < 0) {
		if (ret != -EACCES) {
			dev_err(component->dev, "%s: failed to resume %d\n", __func__, ret);
			return ret;
		}

		/* pm_runtime not enabled yet */
		dev_dbg(component->dev,	"%s: skipping jack init for now\n", __func__);
		return 0;
	}

	/* disable interrupts if hs_jack is not set */
	if (!rt766->hs_jack) {
		if (rt766->uaj_func_data)
			rt766_sdca_irq_ctl(rt766, rt766->uaj_func_data,
				rt766->component, rt766->irq_info, false);

		if (rt766->hid_func_data)
			rt766_sdca_irq_ctl(rt766, rt766->hid_func_data,
				rt766->component, rt766->irq_info, false);
	}

	pm_runtime_put_autosuspend(component->dev);

	return 0;
}

static int rt766_sdca_set_fu_ctl(struct rt766_sdca_priv *rt766, int func_num, int fu_num)
{
	unsigned int fu01_reg, fu02_reg;
	unsigned int ch_01, ch_02;
	unsigned int ch_mute;
	unsigned int fu_reg;
	int err, i;

	switch (fu_num) {
	case RT766_SDCA_ENT_USER_FU41:
		ch_01 = (rt766->fu41_dapm_mute || rt766->fu41_mixer_l_mute) ? 0x01 : 0x00;
		ch_02 = (rt766->fu41_dapm_mute || rt766->fu41_mixer_r_mute) ? 0x01 : 0x00;
		break;
	case RT766_SDCA_ENT_USER_FU36:
		ch_01 = (rt766->fu36_dapm_mute || rt766->fu36_mixer_l_mute) ? 0x01 : 0x00;
		ch_02 = (rt766->fu36_dapm_mute || rt766->fu36_mixer_r_mute) ? 0x01 : 0x00;
		break;
	case RT766_SDCA_ENT_USER_FU21:
		ch_01 = (rt766->fu21_dapm_mute || rt766->fu21_mixer_l_mute) ? 0x01 : 0x00;
		ch_02 = (rt766->fu21_dapm_mute || rt766->fu21_mixer_r_mute) ? 0x01 : 0x00;
		break;
	case RT766_SDCA_ENT_USER_FU113:
		for (i = 0; i < ARRAY_SIZE(rt766->fu113_mixer_mute); i++) {
			ch_mute = (rt766->fu113_dapm_mute || rt766->fu113_mixer_mute[i]) ? 0x01 : 0x00;
			fu_reg = SDW_SDCA_CTL(func_num, fu_num, SDCA_CTL_FU_MUTE, 1) + i;
			err = regmap_write(rt766->regmap, fu_reg, ch_mute);
			if (err < 0)
				return err;
		}
		return 0;
	}

	fu01_reg = SDW_SDCA_CTL(func_num, fu_num, SDCA_CTL_FU_MUTE, 1);
	fu02_reg = SDW_SDCA_CTL(func_num, fu_num, SDCA_CTL_FU_MUTE, 2);
	err = regmap_write(rt766->regmap, fu01_reg, ch_01);
	if (err < 0)
		return err;
	err = regmap_write(rt766->regmap, fu02_reg, ch_02);
	if (err < 0)
		return err;

	return 0;
}

static int rt766_sdca_fu41_playback_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt766->fu41_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt766->fu41_mixer_r_mute;
	return 0;
}

static int rt766_sdca_fu41_playback_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	int err;

	if (rt766->fu41_mixer_l_mute == !ucontrol->value.integer.value[0] &&
		rt766->fu41_mixer_r_mute == !ucontrol->value.integer.value[1])
		return 0;

	rt766->fu41_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt766->fu41_mixer_r_mute = !ucontrol->value.integer.value[1];

	err = rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU41);
	if (err < 0)
		return err;

	return 1;
}

static int rt766_sdca_fu36_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt766->fu36_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt766->fu36_mixer_r_mute;
	return 0;
}

static int rt766_sdca_fu36_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	int err;

	if (rt766->fu36_mixer_l_mute == !ucontrol->value.integer.value[0] &&
		rt766->fu36_mixer_r_mute == !ucontrol->value.integer.value[1])
		return 0;

	rt766->fu36_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt766->fu36_mixer_r_mute = !ucontrol->value.integer.value[1];
	err = rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU36);
	if (err < 0)
		return err;

	return 1;
}

static int rt766_sdca_fu21_playback_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = !rt766->fu21_mixer_l_mute;
	ucontrol->value.integer.value[1] = !rt766->fu21_mixer_r_mute;
	return 0;
}

static int rt766_sdca_fu21_playback_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	int err;

	if (rt766->fu21_mixer_l_mute == !ucontrol->value.integer.value[0] &&
		rt766->fu21_mixer_r_mute == !ucontrol->value.integer.value[1])
		return 0;

	rt766->fu21_mixer_l_mute = !ucontrol->value.integer.value[0];
	rt766->fu21_mixer_r_mute = !ucontrol->value.integer.value[1];

	err = rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_AMP, RT766_SDCA_ENT_USER_FU21);
	if (err < 0)
		return err;

	return 1;
}

static int rt766_sdca_fu113_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt766->fu113_dapm_mute = false;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_MIC, RT766_SDCA_ENT_USER_FU113);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt766->fu113_dapm_mute = true;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_MIC, RT766_SDCA_ENT_USER_FU113);
		break;
	}
	return 0;
}

static int rt766_sdca_dmic_set_gain_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	struct rt_sdca_dmic_kctrl_priv *p =
		(struct rt_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	const unsigned int interval_offset = 0xc0;
	unsigned int regvalue, ctl, i;

	/* check all channels */
	for (i = 0; i < p->count; i++) {
		regmap_read(rt766->regmap, p->reg_base + i, &regvalue);
		ctl = p->max - (((0x1e00 - regvalue) & 0xffff) / interval_offset);

		ucontrol->value.integer.value[i] = ctl;
	}

	return 0;
}

static int rt766_sdca_dmic_set_gain_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt_sdca_dmic_kctrl_priv *p =
		(struct rt_sdca_dmic_kctrl_priv *)kcontrol->private_value;
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	const unsigned int interval_offset = 0xc0;
	unsigned int gain_val[4];
	unsigned int i, changed = 0;
	unsigned int regvalue[4];
	int err;

	/* check all channels */
	for (i = 0; i < p->count; i++) {
		regmap_read(rt766->regmap, p->reg_base + i, &regvalue[i]);

		gain_val[i] = ucontrol->value.integer.value[i];
		if (gain_val[i] > p->max)
			gain_val[i] = p->max;

		gain_val[i] = 0x1e00 - ((p->max - gain_val[i]) * interval_offset);
		gain_val[i] &= 0xffff;

		if (regvalue[i] != gain_val[i])
			changed = 1;
	}

	if (!changed)
		return 0;

	for (i = 0; i < p->count; i++) {
		err = regmap_write(rt766->regmap, p->reg_base + i, gain_val[i]);
		if (err < 0)
			dev_err(&rt766->slave->dev, "0x%08x can't be set\n", p->reg_base + i);
	}

	return changed;
}

static int rt766_sdca_dmic_fu113_capture_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	unsigned int i;

	for (i = 0; i < 4; i++)
		ucontrol->value.integer.value[i] = !rt766->fu113_mixer_mute[i];
	return 0;
}

static int rt766_sdca_dmic_fu113_capture_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	int err, changed = 0, i;

	for (i = 0; i < 4; i++) {
		if (rt766->fu113_mixer_mute[i] != !ucontrol->value.integer.value[i])
			changed = 1;
		rt766->fu113_mixer_mute[i] = !ucontrol->value.integer.value[i];
	}

	err = rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_MIC, RT766_SDCA_ENT_USER_FU113);
	if (err < 0)
		return err;
	return changed;
}

static int rt766_sdca_fu41_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt766->fu41_dapm_mute = false;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU41);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt766->fu41_dapm_mute = true;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU41);
		break;
	}
	return 0;
}

static int rt766_sdca_pde_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event, int func_num, int pde_num, const char *pde_ent)
{
	struct snd_soc_component *component = snd_soc_dapm_to_component(w->dapm);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	struct sdca_function_data *func_data;
	unsigned char ps0 = 0x0, ps3 = 0x3;
	const struct sdca_entity *entity;
	unsigned int pde_req_reg;
	int from_ps, to_ps;
	int ret;

	pde_req_reg = SDW_SDCA_CTL(func_num, pde_num, SDCA_CTL_PDE_REQUESTED_PS, 0);

	switch (func_num) {
	case RT766_FUNC_NUM_UAJ:
		func_data = rt766->uaj_func_data;
		break;
	case RT766_FUNC_NUM_AMP:
		func_data = rt766->sa_func_data;
		break;
	case RT766_FUNC_NUM_MIC:
		func_data = rt766->sm_func_data;
		break;
	default:
		dev_err(component->dev, "%s: unsupported func_num %d\n",
			__func__, func_num);
		return -EINVAL;
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt766->regmap, pde_req_reg, ps0);
		from_ps = ps3;
		to_ps = ps0;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt766->regmap, pde_req_reg, ps3);
		from_ps = ps0;
		to_ps = ps3;
		break;
	}

	entity = sdca_find_entity_by_label(func_data, pde_ent);
	if (!entity) {
		dev_err(component->dev, "%s: failed to find entity %s\n",
			__func__, pde_ent);
		return -EINVAL;
	}

	ret = sdca_asoc_pde_poll_actual_ps(rt766->regmap,
				   func_num,
				   pde_num,
				   from_ps, to_ps,
				   entity->pde.max_delay,
				   entity->pde.num_max_delay);
	if (ret)
		dev_err(component->dev, "%s: PDE transition %x -> %x failed, err=%d\n",
			__func__, from_ps, to_ps, ret);

	return ret;
}

static int rt766_sdca_pde47_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	return rt766_sdca_pde_event(w, kcontrol, event,
		RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_PDE47, "PDE 47");
}

static int rt766_sdca_fu36_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt766->fu36_dapm_mute = false;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU36);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt766->fu36_dapm_mute = true;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_USER_FU36);
		break;
	}
	return 0;
}

static int rt766_sdca_pde34_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	return rt766_sdca_pde_event(w, kcontrol, event,
		RT766_FUNC_NUM_UAJ, RT766_SDCA_ENT_PDE34, "PDE 34");
}

static int rt766_sdca_fu21_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *component =
		snd_soc_dapm_to_component(w->dapm);
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		rt766->fu21_dapm_mute = false;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_AMP, RT766_SDCA_ENT_USER_FU21);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt766->fu21_dapm_mute = true;
		rt766_sdca_set_fu_ctl(rt766, RT766_FUNC_NUM_AMP, RT766_SDCA_ENT_USER_FU21);
		break;
	}
	return 0;
}

static int rt766_sdca_pde23_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	return rt766_sdca_pde_event(w, kcontrol, event,
		RT766_FUNC_NUM_AMP, RT766_SDCA_ENT_PDE23, "PDE 23");
}

static int rt766_sdca_pde11_event(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	return rt766_sdca_pde_event(w, kcontrol, event,
		RT766_FUNC_NUM_MIC, RT766_SDCA_ENT_PDE11, "PDE 11");
}

static int rt766_dmic_fu_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *uinfo)
{
	struct rt_sdca_dmic_kctrl_priv *p =
		(struct rt_sdca_dmic_kctrl_priv *)kcontrol->private_value;

	if (p->max == 1)
		uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	else
		uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = p->count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = p->max;
	return 0;
}

static const char * const rt766_rx_data_ch_select[] = {
	"L,R",
	"R,L",
	"L,L",
	"R,R",
	"L,L+R",
	"R,L+R",
	"L+R,L",
	"L+R,R",
	"L+R,L+R",
};

static SOC_ENUM_SINGLE_DECL(rt766_rx_data_ch_enum,
	RT766_SDCA_CTL(AMP, PPU21, SDCA_CTL_PPU_POSTURENUMBER), 0,
	rt766_rx_data_ch_select);

static const DECLARE_TLV_DB_SCALE(hp_vol_tlv, -9525, 75, 0);
static const DECLARE_TLV_DB_SCALE(spk_vol_tlv, -6525, 75, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, -1725, 75, 0);
static const DECLARE_TLV_DB_SCALE(boost_vol_tlv, -200, 200, 0);

#define RT766_P75DB_STEP		0xC0	/* 0.75 dB in Q7.8 format */
#define RT766_2DB_STEP			0x200	/* 2 dB in Q7.8 format */
#define RT766_HP_VOL_MIN		(-127)	/* -95.25 dB / 0.75 dB step */
#define RT766_HS_VOL_MIN		(-23)	/* -17.25 dB / 0.75 dB step */
#define RT766_HS_BOOST_VOL_MIN		(-1)	/* -2 dB / 2 dB step */
#define RT766_SPK_VOL_MIN		(-87)	/* -65.25 dB / 0.75 dB step */
#define RT766_P_VOL_MAX			0	/* 0 dB / 0.75 dB step */
#define RT766_HS_VOL_MAX		40	/* 30 dB / 0.75 dB step */
#define RT766_HS_BOOST_VOL_MAX		20	/* 40 dB / 2 dB step */

static const struct snd_kcontrol_new rt766_sdca_controls[] = {
	SOC_DOUBLE_EXT("FU41 Playback Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt766_sdca_fu41_playback_get, rt766_sdca_fu41_playback_put),
	SDCA_DOUBLE_Q78_TLV("FU41 Playback Volume",
		RT766_VOLUME_REG(UAJ, USER_FU41, 1),
		RT766_VOLUME_REG(UAJ, USER_FU41, 2),
		RT766_HP_VOL_MIN, RT766_P_VOL_MAX, RT766_P75DB_STEP, hp_vol_tlv),
	SOC_DOUBLE_EXT("FU36 Capture Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt766_sdca_fu36_capture_get, rt766_sdca_fu36_capture_put),
	SDCA_DOUBLE_Q78_TLV("FU36 Capture Volume",
		RT766_VOLUME_REG(UAJ, USER_FU36, 1),
		RT766_VOLUME_REG(UAJ, USER_FU36, 2),
		RT766_HS_VOL_MIN, RT766_HS_VOL_MAX, RT766_P75DB_STEP, mic_vol_tlv),
	SDCA_DOUBLE_Q78_TLV("FU33 Boost Volume",
		RT766_GAIN_REG(UAJ, PLATFORM_FU33, 1),
		RT766_GAIN_REG(UAJ, PLATFORM_FU33, 2),
		RT766_HS_BOOST_VOL_MIN, RT766_HS_BOOST_VOL_MAX, RT766_2DB_STEP, boost_vol_tlv),

	SOC_DOUBLE_EXT("FU21 Playback Switch", SND_SOC_NOPM, 0, 1, 1, 0,
		rt766_sdca_fu21_playback_get, rt766_sdca_fu21_playback_put),
	SDCA_DOUBLE_Q78_TLV("FU21 Playback Volume",
		RT766_VOLUME_REG(AMP, USER_FU21, 1),
		RT766_VOLUME_REG(AMP, USER_FU21, 2),
		RT766_SPK_VOL_MIN, RT766_P_VOL_MAX, RT766_P75DB_STEP, spk_vol_tlv),
	SOC_ENUM("RX Channel Select", rt766_rx_data_ch_enum),

	RT_SDCA_FU_CTRL("FU113 Capture Switch",
		RT766_MUTE_REG(MIC, USER_FU113, 1), 1, 1, 4, rt766_dmic_fu_info,
		rt766_sdca_dmic_fu113_capture_get, rt766_sdca_dmic_fu113_capture_put),
	RT_SDCA_EXT_TLV("FU113 Capture Volume",
		RT766_VOLUME_REG(MIC, USER_FU113, 1),
		rt766_sdca_dmic_set_gain_get, rt766_sdca_dmic_set_gain_put,
		4, 0x3f, mic_vol_tlv, rt766_dmic_fu_info),
};

static const struct snd_soc_dapm_widget rt766_sdca_dapm_widgets[] = {
	/* UAJ */
	SND_SOC_DAPM_OUTPUT("HP"),
	SND_SOC_DAPM_INPUT("MIC2"),
	SND_SOC_DAPM_SUPPLY("PDE 47", SND_SOC_NOPM, 0, 0,
		rt766_sdca_pde47_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 34", SND_SOC_NOPM, 0, 0,
		rt766_sdca_pde34_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("FU 41", NULL, SND_SOC_NOPM, 0, 0,
		rt766_sdca_fu41_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 36", NULL, SND_SOC_NOPM, 0, 0,
		rt766_sdca_fu36_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_AIF_IN("DP3RX", "DP3 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("DP12TX", "DP12 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* AMP */
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
	SND_SOC_DAPM_DAC_E("FU 21", NULL, SND_SOC_NOPM, 0, 0,
		rt766_sdca_fu21_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_SUPPLY("PDE 23", SND_SOC_NOPM, 0, 0,
		rt766_sdca_pde23_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_AIF_IN("DP1RX", "DP1 Playback", 0, SND_SOC_NOPM, 0, 0),

	/* DMIC */
	SND_SOC_DAPM_INPUT("DMIC1"),
	SND_SOC_DAPM_INPUT("DMIC2"),
	SND_SOC_DAPM_SUPPLY("PDE 11", SND_SOC_NOPM, 0, 0,
		rt766_sdca_pde11_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_ADC_E("FU 113", NULL, SND_SOC_NOPM, 0, 0,
		rt766_sdca_fu113_event,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_AIF_OUT("DP8TX", "DP8 Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route rt766_sdca_audio_map[] = {
	{ "FU 41", NULL, "DP3RX" },
	{ "DP12TX", NULL, "FU 36" },
	{ "FU 36", NULL, "PDE 34" },
	{ "FU 36", NULL, "MIC2" },
	{ "HP", NULL, "PDE 47" },
	{ "HP", NULL, "FU 41" },

	{ "FU 21", NULL, "DP1RX" },
	{ "FU 21", NULL, "PDE 23" },
	{ "SPOL", NULL, "FU 21" },
	{ "SPOR", NULL, "FU 21" },

	{"DP8TX", NULL, "FU 113"},
	{"FU 113", NULL, "PDE 11"},
	{"FU 113", NULL, "DMIC1"},
	{"FU 113", NULL, "DMIC2"},
};

static int rt766_sdca_probe(struct snd_soc_component *component)
{
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	struct device *dev = &rt766->slave->dev;
	int ret;

	rt766->component = component;

	ret = pm_runtime_resume(component->dev);
	if (ret < 0 && ret != -EACCES)
		return ret;

	if (rt766->uaj_func_data) {
		dev_dbg(dev, "%s : irq %d\n", __func__, rt766->slave->irq);

		rt766->irq_info = devm_sdca_irq_allocate(dev, rt766->regmap, rt766->slave->irq);
		if (IS_ERR(rt766->irq_info))
			return PTR_ERR(rt766->irq_info);

		ret = rt766_sdca_irq_ctl(rt766, rt766->uaj_func_data,
			component, rt766->irq_info, true);
		if (ret < 0) {
			dev_err(dev, "Failed to request UAJ SDCA IRQ: %d\n", ret);
			return ret;
		}

		if (rt766->hid_func_data) {
			ret = rt766_sdca_irq_ctl(rt766, rt766->hid_func_data,
				component, rt766->irq_info, true);
			if (ret < 0) {
				dev_err(dev, "Failed to request HID SDCA IRQ: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static void rt766_sdca_remove(struct snd_soc_component *component)
{
	struct rt766_sdca_priv *rt766  = snd_soc_component_get_drvdata(component);

	sdca_irq_cleanup_late(component->dev, rt766->uaj_func_data, rt766->irq_info);
	sdca_irq_cleanup_late(component->dev, rt766->hid_func_data, rt766->irq_info);
}

static const struct snd_soc_component_driver soc_sdca_dev_rt766 = {
	.probe = rt766_sdca_probe,
	.remove = rt766_sdca_remove,
	.controls = rt766_sdca_controls,
	.num_controls = ARRAY_SIZE(rt766_sdca_controls),
	.dapm_widgets = rt766_sdca_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt766_sdca_dapm_widgets),
	.dapm_routes = rt766_sdca_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rt766_sdca_audio_map),
	.set_jack = rt766_sdca_set_jack_detect,
	.endianness = 1,
};

static int rt766_sdca_set_sdw_stream(struct snd_soc_dai *dai, void *sdw_stream,
				int direction)
{
	snd_soc_dai_dma_data_set(dai, direction, sdw_stream);

	return 0;
}

static void rt766_sdca_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	snd_soc_dai_set_dma_data(dai, substream, NULL);
}

static int rt766_sdca_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_config stream_config;
	struct sdw_port_config port_config;
	enum sdw_data_direction direction;
	struct sdw_stream_runtime *sdw_stream;
	unsigned int sampling_rate;
	int retval, port;

	dev_dbg(dai->dev, "%s %s id %d", __func__, dai->name, dai->id);
	sdw_stream = snd_soc_dai_get_dma_data(dai, substream);

	if (!sdw_stream)
		return -EINVAL;

	if (!rt766->slave)
		return -EINVAL;

	/* SoundWire specific configuration */
	snd_sdw_params_to_config(substream, params, &stream_config, &port_config);

	/* SoundWire specific configuration */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		direction = SDW_DATA_DIR_RX;
		if (dai->id == RT766_AIF1)
			port = 3;
		else if (dai->id == RT766_AIF2)
			port = 1;
		else
			return -EINVAL;
	} else {
		direction = SDW_DATA_DIR_TX;
		if (dai->id == RT766_AIF1)
			port = 12;
		else if (dai->id == RT766_AIF3)
			port = 8;
		else
			return -EINVAL;
	}

	port_config.num = port;
	retval = sdw_stream_add_slave(rt766->slave, &stream_config,
					&port_config, 1, sdw_stream);
	if (retval) {
		dev_err(dai->dev, "%s: Unable to configure port\n", __func__);
		return retval;
	}

	if (params_channels(params) > 16) {
		dev_err(component->dev, "%s: Unsupported channels %d\n",
			__func__, params_channels(params));
		return -EINVAL;
	}

	/* sampling rate configuration */
	switch (params_rate(params)) {
	case 44100:
		sampling_rate = RT766_SDCA_RATE_44100HZ;
		break;
	case 48000:
		sampling_rate = RT766_SDCA_RATE_48000HZ;
		break;
	case 96000:
		sampling_rate = RT766_SDCA_RATE_96000HZ;
		break;
	case 192000:
		sampling_rate = RT766_SDCA_RATE_192000HZ;
		break;
	default:
		dev_err(component->dev, "%s: Rate %d is not supported\n",
			__func__, params_rate(params));
		return -EINVAL;
	}

	/* set sampling frequency */
	switch (dai->id) {
	case RT766_AIF1:
		regmap_write(rt766->regmap,
			RT766_SDCA_CTL(UAJ, CS41, SDCA_CTL_CS_SAMPLERATEINDEX),
			sampling_rate);
		regmap_write(rt766->regmap,
			RT766_SDCA_CTL(UAJ, CS36, SDCA_CTL_CS_SAMPLERATEINDEX),
			sampling_rate);
		break;
	case RT766_AIF2:
		regmap_write(rt766->regmap,
			RT766_SDCA_CTL(AMP, CS21, SDCA_CTL_CS_SAMPLERATEINDEX),
			sampling_rate);
		break;
	case RT766_AIF3:
		regmap_write(rt766->regmap,
			RT766_SDCA_CTL(MIC, CS113, SDCA_CTL_CS_SAMPLERATEINDEX),
			sampling_rate);
		break;
	default:
		dev_err(component->dev, "%s: Wrong DAI id\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int rt766_sdca_pcm_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct rt766_sdca_priv *rt766 = snd_soc_component_get_drvdata(component);
	struct sdw_stream_runtime *sdw_stream =
		snd_soc_dai_get_dma_data(dai, substream);

	if (!rt766->slave)
		return -EINVAL;

	sdw_stream_remove_slave(rt766->slave, sdw_stream);
	return 0;
}

#define RT766_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
			SNDRV_PCM_RATE_192000)
#define RT766_DAC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)
#define RT766_ADC_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
			SNDRV_PCM_FMTBIT_S32_LE)

static const struct snd_soc_dai_ops rt766_sdca_ops = {
	.hw_params	= rt766_sdca_pcm_hw_params,
	.hw_free	= rt766_sdca_pcm_hw_free,
	.set_stream	= rt766_sdca_set_sdw_stream,
	.shutdown	= rt766_sdca_shutdown,
};

static struct snd_soc_dai_driver rt766_sdca_dai[] = {
	{
		.name = "rt766-sdca-aif1",
		.id = RT766_AIF1,
		.playback = {
			.stream_name = "DP3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT766_STEREO_RATES,
			.formats = RT766_DAC_FORMATS,
		},
		.capture = {
			.stream_name = "DP12 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT766_STEREO_RATES,
			.formats = RT766_ADC_FORMATS,
		},
		.ops = &rt766_sdca_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "rt766-sdca-aif2",
		.id = RT766_AIF2,
		.playback = {
			.stream_name = "DP1 Playback",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT766_STEREO_RATES,
			.formats = RT766_DAC_FORMATS,
		},
		.ops = &rt766_sdca_ops,
	},
	{
		.name = "rt766-sdca-aif3",
		.id = RT766_AIF3,
		.capture = {
			.stream_name = "DP8 Capture",
			.channels_min = 1,
			.channels_max = 4,
			.rates = RT766_STEREO_RATES,
			.formats = RT766_ADC_FORMATS,
		},
		.ops = &rt766_sdca_ops,
	}
};

static unsigned int rt766_find_dt_rates(struct device *dev, struct sdca_function_data *function,
							const char *label)
{
	struct snd_soc_pcm_stream stream;
	struct sdca_entity *entity;
	int i, ret;

	for (i = 0; i < function->num_entities; i++) {
		entity = &function->entities[i];

		if (strcmp(entity->label, label))
			continue;

		/* Can't check earlier as only terminals have an iot member. */
		if (!entity->iot.is_dataport)
			continue;

		ret = sdca_asoc_populate_rate_format(dev, function, entity, &stream);
		if (ret < 0) {
			dev_dbg(dev, "%s: failed to parse rates for entity %s\n",
				__func__, entity->label);
			return 0;
		}

		dev_dbg(dev, "%s: %s supports rates 0x%08x\n", __func__, entity->label, stream.rates);
	}

	return stream.rates;
}

int rt766_sdca_init(struct device *dev, struct regmap *regmap, struct sdw_slave *slave)
{
	struct sdca_function_data *func_data_ptr;
	struct snd_soc_dai_driver *dai_drv;
	struct rt766_sdca_priv *rt766;
	unsigned int rates;
	int ret;
	int i;

	rt766 = devm_kzalloc(dev, sizeof(*rt766), GFP_KERNEL);
	if (!rt766)
		return -ENOMEM;

	dev_set_drvdata(dev, rt766);
	rt766->slave = slave;
	rt766->regmap = regmap;

	regcache_cache_only(rt766->regmap, true);

	ret = devm_mutex_init(dev, &rt766->disable_irq_lock);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize mutex\n");
		return ret;
	}

	/*
	 * Mark hw_init to false
	 * HW init will be performed when device reports present
	 */
	rt766->hw_init = false;
	rt766->first_hw_init = false;
	rt766->fu41_dapm_mute = true;
	rt766->fu41_mixer_l_mute = rt766->fu41_mixer_r_mute = false;
	rt766->fu36_dapm_mute = true;
	rt766->fu36_mixer_l_mute = rt766->fu36_mixer_r_mute = true;
	rt766->fu21_dapm_mute = true;
	rt766->fu21_mixer_l_mute = rt766->fu21_mixer_r_mute = false;
	rt766->fu113_dapm_mute = true;
	rt766->fu113_mixer_mute[0] = rt766->fu113_mixer_mute[1] =
		rt766->fu113_mixer_mute[2] = rt766->fu113_mixer_mute[3] = true;

	dai_drv = devm_kzalloc(dev, sizeof(struct snd_soc_dai_driver) * ARRAY_SIZE(rt766_sdca_dai), GFP_KERNEL);
	if (!dai_drv) {
		dev_err(dev, "Failed to allocate memory for DAI driver\n");
		ret = -ENOMEM;
		goto _sdw_init_err_;
	}
	memcpy(dai_drv, rt766_sdca_dai, sizeof(struct snd_soc_dai_driver) * ARRAY_SIZE(rt766_sdca_dai));

	/* get SDCA function data */
	dev_dbg(dev, "SDCA functions found: %d", slave->sdca_data.num_functions);
	for (i = 0; i < slave->sdca_data.num_functions; i++) {
		func_data_ptr = devm_kzalloc(dev, sizeof(*func_data_ptr), GFP_KERNEL);
		if (!func_data_ptr) {
			dev_err(dev, "Failed to allocate memory for function data\n");
			ret = -ENOMEM;
			goto _free_dai_drv_;
		}

		func_data_ptr->desc = &slave->sdca_data.function[i];
		ret = sdca_parse_function(dev, func_data_ptr);
		if (ret) {
			devm_kfree(dev, func_data_ptr);
			goto _free_dai_drv_;
		}
		dev_dbg(dev, "Function type=%d, num_entities=%d",
			slave->sdca_data.function[i].type, func_data_ptr->num_entities);

		switch (slave->sdca_data.function[i].type) {
		case SDCA_FUNCTION_TYPE_UAJ:
			rt766->uaj_func_data = func_data_ptr;
			/*
			 * Some machines may only support a subset of the sample rates supported by the codec.
			 * Therefore, we need to parse the supported sample rates from the DisCo table and
			 * configure them in the DAI. If the DisCo table does not provide sample rate information,
			 * we will fall back to the default supported rates defined in the codec driver.
			 */
			rates = rt766_find_dt_rates(dev, func_data_ptr, "IT 41");
			if (rates)
				dai_drv[RT766_DAI_UAJ].playback.rates = rates;

			rates = rt766_find_dt_rates(dev, func_data_ptr, "OT 36");
			if (rates)
				dai_drv[RT766_DAI_UAJ].capture.rates = rates;
			break;
		case SDCA_FUNCTION_TYPE_SMART_AMP:
			rt766->sa_func_data = func_data_ptr;
			rates = rt766_find_dt_rates(dev, func_data_ptr, "IT 21");
			if (rates)
				dai_drv[RT766_DAI_AMP].playback.rates = rates;
			break;
		case SDCA_FUNCTION_TYPE_SMART_MIC:
			rt766->sm_func_data = func_data_ptr;
			rates = rt766_find_dt_rates(dev, func_data_ptr, "OT 113");
			if (rates)
				dai_drv[RT766_DAI_MIC].capture.rates = rates;
			break;
		case SDCA_FUNCTION_TYPE_HID:
			rt766->hid_func_data = func_data_ptr;
			break;
		default:
			dev_dbg(dev, "Unexpected SDCA function type found: %d",
				slave->sdca_data.function[i].type);
		}
	}

	ret =  devm_snd_soc_register_component(dev,
			&soc_sdca_dev_rt766, dai_drv, ARRAY_SIZE(rt766_sdca_dai));
	if (ret < 0)
		goto _free_dai_drv_;

	/* set autosuspend parameters */
	pm_runtime_set_autosuspend_delay(dev, 3000);
	pm_runtime_use_autosuspend(dev);

	/* make sure the device does not suspend immediately */
	pm_runtime_mark_last_busy(dev);

	pm_runtime_enable(dev);
	dev_dbg(dev, "%s\n", __func__);
	return 0;

_free_dai_drv_:
	if (dai_drv)
		devm_kfree(dev, dai_drv);

_sdw_init_err_:
	return ret;
}

static int rt766_func_initialize(struct rt766_sdca_priv *rt766, struct sdca_function_data *func_data)
{
	struct device *dev = &rt766->slave->dev;
	unsigned int func_status_reg;
	unsigned int func_status;
	int ret;

	switch (func_data->desc->type) {
	case SDCA_FUNCTION_TYPE_UAJ:
		func_status_reg = RT766_FUNC_STATUS_REG(UAJ);
		break;
	case SDCA_FUNCTION_TYPE_SMART_AMP:
		func_status_reg = RT766_FUNC_STATUS_REG(AMP);
		break;
	case SDCA_FUNCTION_TYPE_SMART_MIC:
		func_status_reg = RT766_FUNC_STATUS_REG(MIC);
		break;
	case SDCA_FUNCTION_TYPE_HID:
		func_status_reg = RT766_FUNC_STATUS_REG(HID);
		break;
	default:
		dev_dbg(dev, "Unexpected SDCA function type found: %d",
			func_data->desc->type);
		return -EINVAL;
	}

	regmap_read(rt766->regmap, func_status_reg, &func_status);
	dev_dbg(dev, "%s, %s func_status=0x%x\n", __func__, func_data->desc->name, func_status);

	if ((func_status & SDCA_CTL_ENTITY_0_FUNCTION_NEEDS_INITIALIZATION) || (!rt766->first_hw_init)) {
		ret = sdca_regmap_write_init(dev, rt766->regmap, func_data);
		if (ret) {
			dev_err(dev, "%s initialization table update failed\n", func_data->desc->name);
			goto _func_init_err_;
		}

		regmap_write(rt766->regmap, func_status_reg,
			SDCA_CTL_ENTITY_0_FUNCTION_NEEDS_INITIALIZATION);
	}

	return 0;

_func_init_err_:
	dev_err(dev, "%s: %s init writes failed, err=%d", __func__, func_data->desc->name, ret);
	return ret;
}

int rt766_sdca_io_init(struct device *dev, struct sdw_slave *slave)
{
	struct rt766_sdca_priv *rt766 = dev_get_drvdata(dev);
	unsigned int val;

	rt766->disable_irq = false;

	if (rt766->hw_init)
		return 0;

	regcache_cache_only(rt766->regmap, false);
	if (rt766->first_hw_init) {
		regcache_cache_bypass(rt766->regmap, true);
	} else {
		/*
		 *  PM runtime status is marked as 'active' only when a Slave reports as Attached
		 */

		/* update count of parent 'active' children */
		pm_runtime_set_active(&slave->dev);
	}

	pm_runtime_get_noresume(&slave->dev);

	regmap_read(rt766->regmap, RT766_BOND_LATCH_ID, &val);
	dev_dbg(&slave->dev, "%s bond ID=0x%x (%s)\n", __func__, val, (val == 0x1) ? "RT767" : "RT766");

	/* check function status and initialize if needed */
	if (rt766->uaj_func_data)
		rt766_func_initialize(rt766, rt766->uaj_func_data);
	if (rt766->sa_func_data)
		rt766_func_initialize(rt766, rt766->sa_func_data);
	if (rt766->sm_func_data)
		rt766_func_initialize(rt766, rt766->sm_func_data);
	if (rt766->hid_func_data)
		rt766_func_initialize(rt766, rt766->hid_func_data);

	if (rt766->first_hw_init) {
		regcache_cache_bypass(rt766->regmap, false);
		regcache_mark_dirty(rt766->regmap);
	} else {
		rt766->first_hw_init = true;
	}

	/* Mark Slave initialization complete */
	rt766->hw_init = true;

	dev_dbg(&slave->dev, "%s hw_init complete\n", __func__);

	pm_runtime_put_autosuspend(&slave->dev);

	return 0;
}

MODULE_DESCRIPTION("ASoC RT766 SDCA SDW driver");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("SND_SOC_SDCA");
