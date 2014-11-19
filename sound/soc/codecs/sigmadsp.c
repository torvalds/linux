/*
 * Load Analog Devices SigmaStudio firmware files
 *
 * Copyright 2009-2014 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/crc32.h>
#include <linux/firmware.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <sound/soc.h>

#include "sigmadsp.h"

#define SIGMA_MAGIC "ADISIGM"

struct sigmadsp_data {
	struct list_head head;
	unsigned int addr;
	unsigned int length;
	uint8_t data[];
};

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

		data = kzalloc(sizeof(*data) + len - 2, GFP_KERNEL);
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
	struct sigmadsp_data *data, *_data;

	list_for_each_entry_safe(data, _data, &sigmadsp->data_list, head)
		kfree(data);

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
	default:
		dev_err(sigmadsp->dev,
			"Failed to load firmware: Invalid version %d. Supported firmware versions: 1\n",
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

	INIT_LIST_HEAD(&sigmadsp->data_list);

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
	sigmadsp->component = component;

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
	struct sigmadsp_data *data;
	int ret;

	if (sigmadsp->current_samplerate == samplerate)
		return 0;

	list_for_each_entry(data, &sigmadsp->data_list, head) {
		ret = sigmadsp_write(sigmadsp, data->addr, data->data,
			data->length);
		if (ret)
			goto err;
	}

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
	return 0;
}
EXPORT_SYMBOL_GPL(sigmadsp_restrict_params);

MODULE_LICENSE("GPL");
