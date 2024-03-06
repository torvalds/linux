// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2014 Analog Devices Inc.
 */

#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/control.h>
#include <sound/soc.h>

#include "sigmadsp.h"

#define SIGMA_MAGIC "ADISIGM"

#define SIGMA_FW_CHUNK_TYPE_DATA 0
#define SIGMA_FW_CHUNK_TYPE_CONTROL 1
#define SIGMA_FW_CHUNK_TYPE_SAMPLERATES 2

#define READBACK_CTRL_NAME "ReadBack"

struct sigmadsp_control {
	struct list_head head;
	uint32_t samplerates;
	unsigned int addr;
	unsigned int num_bytes;
	const char *name;
	struct snd_kcontrol *kcontrol;
	bool is_readback;
	bool cached;
	uint8_t cache[];
};

struct sigmadsp_data {
	struct list_head head;
	uint32_t samplerates;
	unsigned int addr;
	unsigned int length;
	uint8_t data[] __counted_by(length);
};

struct sigma_fw_chunk {
	__le32 length;
	__le32 tag;
	__le32 samplerates;
} __packed;

struct sigma_fw_chunk_data {
	struct sigma_fw_chunk chunk;
	__le16 addr;
	uint8_t data[];
} __packed;

struct sigma_fw_chunk_control {
	struct sigma_fw_chunk chunk;
	__le16 type;
	__le16 addr;
	__le16 num_bytes;
	const char name[];
} __packed;

struct sigma_fw_chunk_samplerate {
	struct sigma_fw_chunk chunk;
	__le32 samplerates[];
} __packed;

struct sigma_firmware_header {
	unsigned char magic[7];
	u8 version;
	__le32 crc;
} __packed;

enum {
	SIGMA_ACTION_WRITEXBYTES = 0,
	SIGMA_ACTION_WRITESINGLE,
	SIGMA_ACTION_WRITESAFELOAD,
	SIGMA_ACTION_END,
};

struct sigma_action {
	u8 instr;
	u8 len_hi;
	__le16 len;
	__be16 addr;
	unsigned char payload[];
} __packed;

static int sigmadsp_write(struct sigmadsp *sigmadsp, unsigned int addr,
	const uint8_t data[], size_t len)
{
	return sigmadsp->write(sigmadsp->control_data, addr, data, len);
}

static int sigmadsp_read(struct sigmadsp *sigmadsp, unsigned int addr,
	uint8_t data[], size_t len)
{
	return sigmadsp->read(sigmadsp->control_data, addr, data, len);
}

static int sigmadsp_ctrl_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *info)
{
	struct sigmadsp_control *ctrl = (void *)kcontrol->private_value;

	info->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	info->count = ctrl->num_bytes;

	return 0;
}

static int sigmadsp_ctrl_write(struct sigmadsp *sigmadsp,
	struct sigmadsp_control *ctrl, void *data)
{
	/* safeload loads up to 20 bytes in a atomic operation */
	if (ctrl->num_bytes <= 20 && sigmadsp->ops && sigmadsp->ops->safeload)
		return sigmadsp->ops->safeload(sigmadsp, ctrl->addr, data,
			ctrl->num_bytes);
	else
		return sigmadsp_write(sigmadsp, ctrl->addr, data,
			ctrl->num_bytes);
}

static int sigmadsp_ctrl_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct sigmadsp_control *ctrl = (void *)kcontrol->private_value;
	struct sigmadsp *sigmadsp = snd_kcontrol_chip(kcontrol);
	uint8_t *data;
	int ret = 0;

	mutex_lock(&sigmadsp->lock);

	data = ucontrol->value.bytes.data;

	if (!(kcontrol->vd[0].access & SNDRV_CTL_ELEM_ACCESS_INACTIVE))
		ret = sigmadsp_ctrl_write(sigmadsp, ctrl, data);

	if (ret == 0) {
		memcpy(ctrl->cache, data, ctrl->num_bytes);
		if (!ctrl->is_readback)
			ctrl->cached = true;
	}

	mutex_unlock(&sigmadsp->lock);

	return ret;
}

static int sigmadsp_ctrl_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct sigmadsp_control *ctrl = (void *)kcontrol->private_value;
	struct sigmadsp *sigmadsp = snd_kcontrol_chip(kcontrol);
	int ret = 0;

	mutex_lock(&sigmadsp->lock);

	if (!ctrl->cached) {
		ret = sigmadsp_read(sigmadsp, ctrl->addr, ctrl->cache,
			ctrl->num_bytes);
	}

	if (ret == 0) {
		if (!ctrl->is_readback)
			ctrl->cached = true;
		memcpy(ucontrol->value.bytes.data, ctrl->cache,
			ctrl->num_bytes);
	}

	mutex_unlock(&sigmadsp->lock);

	return ret;
}

static void sigmadsp_control_free(struct snd_kcontrol *kcontrol)
{
	struct sigmadsp_control *ctrl = (void *)kcontrol->private_value;

	ctrl->kcontrol = NULL;
}

static bool sigma_fw_validate_control_name(const char *name, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++) {
		/* Normal ASCII characters are valid */
		if (name[i] < ' ' || name[i] > '~')
			return false;
	}

	return true;
}

static int sigma_fw_load_control(struct sigmadsp *sigmadsp,
	const struct sigma_fw_chunk *chunk, unsigned int length)
{
	const struct sigma_fw_chunk_control *ctrl_chunk;
	struct sigmadsp_control *ctrl;
	unsigned int num_bytes;
	size_t name_len;
	char *name;
	int ret;

	if (length <= sizeof(*ctrl_chunk))
		return -EINVAL;

	ctrl_chunk = (const struct sigma_fw_chunk_control *)chunk;

	name_len = length - sizeof(*ctrl_chunk);
	if (name_len >= SNDRV_CTL_ELEM_ID_NAME_MAXLEN)
		name_len = SNDRV_CTL_ELEM_ID_NAME_MAXLEN - 1;

	/* Make sure there are no non-displayable characaters in the string */
	if (!sigma_fw_validate_control_name(ctrl_chunk->name, name_len))
		return -EINVAL;

	num_bytes = le16_to_cpu(ctrl_chunk->num_bytes);
	ctrl = kzalloc(sizeof(*ctrl) + num_bytes, GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	name = kmemdup_nul(ctrl_chunk->name, name_len, GFP_KERNEL);
	if (!name) {
		ret = -ENOMEM;
		goto err_free_ctrl;
	}
	ctrl->name = name;

	/*
	 * Readbacks doesn't work with non-volatile controls, since the
	 * firmware updates the control value without driver interaction. Mark
	 * the readbacks to ensure that the values are not cached.
	 */
	if (ctrl->name && strncmp(ctrl->name, READBACK_CTRL_NAME,
				  (sizeof(READBACK_CTRL_NAME) - 1)) == 0)
		ctrl->is_readback = true;

	ctrl->addr = le16_to_cpu(ctrl_chunk->addr);
	ctrl->num_bytes = num_bytes;
	ctrl->samplerates = le32_to_cpu(chunk->samplerates);

	list_add_tail(&ctrl->head, &sigmadsp->ctrl_list);

	return 0;

err_free_ctrl:
	kfree(ctrl);

	return ret;
}

static int sigma_fw_load_data(struct sigmadsp *sigmadsp,
	const struct sigma_fw_chunk *chunk, unsigned int length)
{
	const struct sigma_fw_chunk_data *data_chunk;
	struct sigmadsp_data *data;

	if (length <= sizeof(*data_chunk))
		return -EINVAL;

	data_chunk = (struct sigma_fw_chunk_data *)chunk;

	length -= sizeof(*data_chunk);

	data = kzalloc(struct_size(data, data, length), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->addr = le16_to_cpu(data_chunk->addr);
	data->length = length;
	data->samplerates = le32_to_cpu(chunk->samplerates);
	memcpy(data->data, data_chunk->data, length);
	list_add_tail(&data->head, &sigmadsp->data_list);

	return 0;
}

static int sigma_fw_load_samplerates(struct sigmadsp *sigmadsp,
	const struct sigma_fw_chunk *chunk, unsigned int length)
{
	const struct sigma_fw_chunk_samplerate *rate_chunk;
	unsigned int num_rates;
	unsigned int *rates;
	unsigned int i;

	rate_chunk = (const struct sigma_fw_chunk_samplerate *)chunk;

	num_rates = (length - sizeof(*rate_chunk)) / sizeof(__le32);

	if (num_rates > 32 || num_rates == 0)
		return -EINVAL;

	/* We only allow one samplerates block per file */
	if (sigmadsp->rate_constraints.count)
		return -EINVAL;

	rates = kcalloc(num_rates, sizeof(*rates), GFP_KERNEL);
	if (!rates)
		return -ENOMEM;

	for (i = 0; i < num_rates; i++)
		rates[i] = le32_to_cpu(rate_chunk->samplerates[i]);

	sigmadsp->rate_constraints.count = num_rates;
	sigmadsp->rate_constraints.list = rates;

	return 0;
}

static int sigmadsp_fw_load_v2(struct sigmadsp *sigmadsp,
	const struct firmware *fw)
{
	struct sigma_fw_chunk *chunk;
	unsigned int length, pos;
	int ret;

	/*
	 * Make sure that there is at least one chunk to avoid integer
	 * underflows later on. Empty firmware is still valid though.
	 */
	if (fw->size < sizeof(*chunk) + sizeof(struct sigma_firmware_header))
		return 0;

	pos = sizeof(struct sigma_firmware_header);

	while (pos < fw->size - sizeof(*chunk)) {
		chunk = (struct sigma_fw_chunk *)(fw->data + pos);

		length = le32_to_cpu(chunk->length);

		if (length > fw->size - pos || length < sizeof(*chunk))
			return -EINVAL;

		switch (le32_to_cpu(chunk->tag)) {
		case SIGMA_FW_CHUNK_TYPE_DATA:
			ret = sigma_fw_load_data(sigmadsp, chunk, length);
			break;
		case SIGMA_FW_CHUNK_TYPE_CONTROL:
			ret = sigma_fw_load_control(sigmadsp, chunk, length);
			break;
		case SIGMA_FW_CHUNK_TYPE_SAMPLERATES:
			ret = sigma_fw_load_samplerates(sigmadsp, chunk, length);
			break;
		default:
			dev_warn(sigmadsp->dev, "Unknown chunk type: %d\n",
				chunk->tag);
			ret = 0;
			break;
		}

		if (ret)
			return ret;

		/*
		 * This can not overflow since if length is larger than the
		 * maximum firmware size (0x4000000) we'll error out earilier.
		 */
		pos += ALIGN(length, sizeof(__le32));
	}

	return 0;
}

static inline u32 sigma_action_len(struct sigma_action *sa)
{
	return (sa->len_hi << 16) | le16_to_cpu(sa->len);
}

static size_t sigma_action_size(struct sigma_action *sa)
{
	size_t payload = 0;

	switch (sa->instr) {
	case SIGMA_ACTION_WRITEXBYTES:
	case SIGMA_ACTION_WRITESINGLE:
	case SIGMA_ACTION_WRITESAFELOAD:
		payload = sigma_action_len(sa);
		break;
	default:
		break;
	}

	payload = ALIGN(payload, 2);

	return payload + sizeof(struct sigma_action);
}

/*
 * Returns a negative error value in case of an error, 0 if processing of
 * the firmware should be stopped after this action, 1 otherwise.
 */
static int process_sigma_action(struct sigmadsp *sigmadsp,
	struct sigma_action *sa)
{
	size_t len = sigma_action_len(sa);
	struct sigmadsp_data *data;

	pr_debug("%s: instr:%i addr:%#x len:%zu\n", __func__,
		sa->instr, sa->addr, len);

	switch (sa->instr) {
	case SIGMA_ACTION_WRITEXBYTES:
	case SIGMA_ACTION_WRITESINGLE:
	case SIGMA_ACTION_WRITESAFELOAD:
		if (len < 3)
			return -EINVAL;

		data = kzalloc(struct_size(data, data, size_sub(len, 2)),
			       GFP_KERNEL);
		if (!data)
			return -ENOMEM;

		data->addr = be16_to_cpu(sa->addr);
		data->length = len - 2;
		memcpy(data->data, sa->payload, data->length);
		list_add_tail(&data->head, &sigmadsp->data_list);
		break;
	case SIGMA_ACTION_END:
		return 0;
	default:
		return -EINVAL;
	}

	return 1;
}

static int sigmadsp_fw_load_v1(struct sigmadsp *sigmadsp,
	const struct firmware *fw)
{
	struct sigma_action *sa;
	size_t size, pos;
	int ret;

	pos = sizeof(struct sigma_firmware_header);

	while (pos + sizeof(*sa) <= fw->size) {
		sa = (struct sigma_action *)(fw->data + pos);

		size = sigma_action_size(sa);
		pos += size;
		if (pos > fw->size || size == 0)
			break;

		ret = process_sigma_action(sigmadsp, sa);

		pr_debug("%s: action returned %i\n", __func__, ret);

		if (ret <= 0)
			return ret;
	}

	if (pos != fw->size)
		return -EINVAL;

	return 0;
}

static void sigmadsp_firmware_release(struct sigmadsp *sigmadsp)
{
	struct sigmadsp_control *ctrl, *_ctrl;
	struct sigmadsp_data *data, *_data;

	list_for_each_entry_safe(ctrl, _ctrl, &sigmadsp->ctrl_list, head) {
		kfree(ctrl->name);
		kfree(ctrl);
	}

	list_for_each_entry_safe(data, _data, &sigmadsp->data_list, head)
		kfree(data);

	INIT_LIST_HEAD(&sigmadsp->ctrl_list);
	INIT_LIST_HEAD(&sigmadsp->data_list);
}

static void devm_sigmadsp_release(struct device *dev, void *res)
{
	sigmadsp_firmware_release((struct sigmadsp *)res);
}

static int sigmadsp_firmware_load(struct sigmadsp *sigmadsp, const char *name)
{
	const struct sigma_firmware_header *ssfw_head;
	const struct firmware *fw;
	int ret;
	u32 crc;

	/* first load the blob */
	ret = request_firmware(&fw, name, sigmadsp->dev);
	if (ret) {
		pr_debug("%s: request_firmware() failed with %i\n", __func__, ret);
		goto done;
	}

	/* then verify the header */
	ret = -EINVAL;

	/*
	 * Reject too small or unreasonable large files. The upper limit has been
	 * chosen a bit arbitrarily, but it should be enough for all practical
	 * purposes and having the limit makes it easier to avoid integer
	 * overflows later in the loading process.
	 */
	if (fw->size < sizeof(*ssfw_head) || fw->size >= 0x4000000) {
		dev_err(sigmadsp->dev, "Failed to load firmware: Invalid size\n");
		goto done;
	}

	ssfw_head = (void *)fw->data;
	if (memcmp(ssfw_head->magic, SIGMA_MAGIC, ARRAY_SIZE(ssfw_head->magic))) {
		dev_err(sigmadsp->dev, "Failed to load firmware: Invalid magic\n");
		goto done;
	}

	crc = crc32(0, fw->data + sizeof(*ssfw_head),
			fw->size - sizeof(*ssfw_head));
	pr_debug("%s: crc=%x\n", __func__, crc);
	if (crc != le32_to_cpu(ssfw_head->crc)) {
		dev_err(sigmadsp->dev, "Failed to load firmware: Wrong crc checksum: expected %x got %x\n",
			le32_to_cpu(ssfw_head->crc), crc);
		goto done;
	}

	switch (ssfw_head->version) {
	case 1:
		ret = sigmadsp_fw_load_v1(sigmadsp, fw);
		break;
	case 2:
		ret = sigmadsp_fw_load_v2(sigmadsp, fw);
		break;
	default:
		dev_err(sigmadsp->dev,
			"Failed to load firmware: Invalid version %d. Supported firmware versions: 1, 2\n",
			ssfw_head->version);
		ret = -EINVAL;
		break;
	}

	if (ret)
		sigmadsp_firmware_release(sigmadsp);

done:
	release_firmware(fw);

	return ret;
}

static int sigmadsp_init(struct sigmadsp *sigmadsp, struct device *dev,
	const struct sigmadsp_ops *ops, const char *firmware_name)
{
	sigmadsp->ops = ops;
	sigmadsp->dev = dev;

	INIT_LIST_HEAD(&sigmadsp->ctrl_list);
	INIT_LIST_HEAD(&sigmadsp->data_list);
	mutex_init(&sigmadsp->lock);

	return sigmadsp_firmware_load(sigmadsp, firmware_name);
}

/**
 * devm_sigmadsp_init() - Initialize SigmaDSP instance
 * @dev: The parent device
 * @ops: The sigmadsp_ops to use for this instance
 * @firmware_name: Name of the firmware file to load
 *
 * Allocates a SigmaDSP instance and loads the specified firmware file.
 *
 * Returns a pointer to a struct sigmadsp on success, or a PTR_ERR() on error.
 */
struct sigmadsp *devm_sigmadsp_init(struct device *dev,
	const struct sigmadsp_ops *ops, const char *firmware_name)
{
	struct sigmadsp *sigmadsp;
	int ret;

	sigmadsp = devres_alloc(devm_sigmadsp_release, sizeof(*sigmadsp),
		GFP_KERNEL);
	if (!sigmadsp)
		return ERR_PTR(-ENOMEM);

	ret = sigmadsp_init(sigmadsp, dev, ops, firmware_name);
	if (ret) {
		devres_free(sigmadsp);
		return ERR_PTR(ret);
	}

	devres_add(dev, sigmadsp);

	return sigmadsp;
}
EXPORT_SYMBOL_GPL(devm_sigmadsp_init);

static int sigmadsp_rate_to_index(struct sigmadsp *sigmadsp, unsigned int rate)
{
	unsigned int i;

	for (i = 0; i < sigmadsp->rate_constraints.count; i++) {
		if (sigmadsp->rate_constraints.list[i] == rate)
			return i;
	}

	return -EINVAL;
}

static unsigned int sigmadsp_get_samplerate_mask(struct sigmadsp *sigmadsp,
	unsigned int samplerate)
{
	int samplerate_index;

	if (samplerate == 0)
		return 0;

	if (sigmadsp->rate_constraints.count) {
		samplerate_index = sigmadsp_rate_to_index(sigmadsp, samplerate);
		if (samplerate_index < 0)
			return 0;

		return BIT(samplerate_index);
	} else {
		return ~0;
	}
}

static bool sigmadsp_samplerate_valid(unsigned int supported,
	unsigned int requested)
{
	/* All samplerates are supported */
	if (!supported)
		return true;

	return supported & requested;
}

static int sigmadsp_alloc_control(struct sigmadsp *sigmadsp,
	struct sigmadsp_control *ctrl, unsigned int samplerate_mask)
{
	struct snd_kcontrol_new template;
	struct snd_kcontrol *kcontrol;

	memset(&template, 0, sizeof(template));
	template.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	template.name = ctrl->name;
	template.info = sigmadsp_ctrl_info;
	template.get = sigmadsp_ctrl_get;
	template.put = sigmadsp_ctrl_put;
	template.private_value = (unsigned long)ctrl;
	template.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	if (!sigmadsp_samplerate_valid(ctrl->samplerates, samplerate_mask))
		template.access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;

	kcontrol = snd_ctl_new1(&template, sigmadsp);
	if (!kcontrol)
		return -ENOMEM;

	kcontrol->private_free = sigmadsp_control_free;
	ctrl->kcontrol = kcontrol;

	return snd_ctl_add(sigmadsp->component->card->snd_card, kcontrol);
}

static void sigmadsp_activate_ctrl(struct sigmadsp *sigmadsp,
	struct sigmadsp_control *ctrl, unsigned int samplerate_mask)
{
	struct snd_card *card = sigmadsp->component->card->snd_card;
	bool active;
	int changed;

	active = sigmadsp_samplerate_valid(ctrl->samplerates, samplerate_mask);
	if (!ctrl->kcontrol)
		return;
	changed = snd_ctl_activate_id(card, &ctrl->kcontrol->id, active);
	if (active && changed > 0) {
		mutex_lock(&sigmadsp->lock);
		if (ctrl->cached)
			sigmadsp_ctrl_write(sigmadsp, ctrl, ctrl->cache);
		mutex_unlock(&sigmadsp->lock);
	}
}

/**
 * sigmadsp_attach() - Attach a sigmadsp instance to a ASoC component
 * @sigmadsp: The sigmadsp instance to attach
 * @component: The component to attach to
 *
 * Typically called in the components probe callback.
 *
 * Note, once this function has been called the firmware must not be released
 * until after the ALSA snd_card that the component belongs to has been
 * disconnected, even if sigmadsp_attach() returns an error.
 */
int sigmadsp_attach(struct sigmadsp *sigmadsp,
	struct snd_soc_component *component)
{
	struct sigmadsp_control *ctrl;
	unsigned int samplerate_mask;
	int ret;

	sigmadsp->component = component;

	samplerate_mask = sigmadsp_get_samplerate_mask(sigmadsp,
		sigmadsp->current_samplerate);

	list_for_each_entry(ctrl, &sigmadsp->ctrl_list, head) {
		ret = sigmadsp_alloc_control(sigmadsp, ctrl, samplerate_mask);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(sigmadsp_attach);

/**
 * sigmadsp_setup() - Setup the DSP for the specified samplerate
 * @sigmadsp: The sigmadsp instance to configure
 * @samplerate: The samplerate the DSP should be configured for
 *
 * Loads the appropriate firmware program and parameter memory (if not already
 * loaded) and enables the controls for the specified samplerate. Any control
 * parameter changes that have been made previously will be restored.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int sigmadsp_setup(struct sigmadsp *sigmadsp, unsigned int samplerate)
{
	struct sigmadsp_control *ctrl;
	unsigned int samplerate_mask;
	struct sigmadsp_data *data;
	int ret;

	if (sigmadsp->current_samplerate == samplerate)
		return 0;

	samplerate_mask = sigmadsp_get_samplerate_mask(sigmadsp, samplerate);
	if (samplerate_mask == 0)
		return -EINVAL;

	list_for_each_entry(data, &sigmadsp->data_list, head) {
		if (!sigmadsp_samplerate_valid(data->samplerates,
		    samplerate_mask))
			continue;
		ret = sigmadsp_write(sigmadsp, data->addr, data->data,
			data->length);
		if (ret)
			goto err;
	}

	list_for_each_entry(ctrl, &sigmadsp->ctrl_list, head)
		sigmadsp_activate_ctrl(sigmadsp, ctrl, samplerate_mask);

	sigmadsp->current_samplerate = samplerate;

	return 0;
err:
	sigmadsp_reset(sigmadsp);

	return ret;
}
EXPORT_SYMBOL_GPL(sigmadsp_setup);

/**
 * sigmadsp_reset() - Notify the sigmadsp instance that the DSP has been reset
 * @sigmadsp: The sigmadsp instance to reset
 *
 * Should be called whenever the DSP has been reset and parameter and program
 * memory need to be re-loaded.
 */
void sigmadsp_reset(struct sigmadsp *sigmadsp)
{
	struct sigmadsp_control *ctrl;

	list_for_each_entry(ctrl, &sigmadsp->ctrl_list, head)
		sigmadsp_activate_ctrl(sigmadsp, ctrl, false);

	sigmadsp->current_samplerate = 0;
}
EXPORT_SYMBOL_GPL(sigmadsp_reset);

/**
 * sigmadsp_restrict_params() - Applies DSP firmware specific constraints
 * @sigmadsp: The sigmadsp instance
 * @substream: The substream to restrict
 *
 * Applies samplerate constraints that may be required by the firmware Should
 * typically be called from the CODEC/component drivers startup callback.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int sigmadsp_restrict_params(struct sigmadsp *sigmadsp,
	struct snd_pcm_substream *substream)
{
	if (sigmadsp->rate_constraints.count == 0)
		return 0;

	return snd_pcm_hw_constraint_list(substream->runtime, 0,
		SNDRV_PCM_HW_PARAM_RATE, &sigmadsp->rate_constraints);
}
EXPORT_SYMBOL_GPL(sigmadsp_restrict_params);

MODULE_LICENSE("GPL");
