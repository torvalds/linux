// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/firmware.h>
#include <linux/workqueue.h>
#include <sound/tlv.h>
#include <uapi/sound/sof/tokens.h>
#include "sof-priv.h"
#include "sof-audio.h"
#include "ops.h"

#define COMP_ID_UNASSIGNED		0xffffffff
/*
 * Constants used in the computation of linear volume gain
 * from dB gain 20th root of 10 in Q1.16 fixed-point notation
 */
#define VOL_TWENTIETH_ROOT_OF_TEN	73533
/* 40th root of 10 in Q1.16 fixed-point notation*/
#define VOL_FORTIETH_ROOT_OF_TEN	69419

/* 0.5 dB step value in topology TLV */
#define VOL_HALF_DB_STEP	50

/* TLV data items */
#define TLV_MIN		0
#define TLV_STEP	1
#define TLV_MUTE	2

/**
 * sof_update_ipc_object - Parse multiple sets of tokens within the token array associated with the
 *			    token ID.
 * @scomp: pointer to SOC component
 * @object: target IPC struct to save the parsed values
 * @token_id: token ID for the token array to be searched
 * @tuples: pointer to the tuples array
 * @num_tuples: number of tuples in the tuples array
 * @object_size: size of the object
 * @token_instance_num: number of times the same @token_id needs to be parsed i.e. the function
 *			looks for @token_instance_num of each token in the token array associated
 *			with the @token_id
 */
int sof_update_ipc_object(struct snd_soc_component *scomp, void *object, enum sof_tokens token_id,
			  struct snd_sof_tuple *tuples, int num_tuples,
			  size_t object_size, int token_instance_num)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	const struct sof_token_info *token_list;
	const struct sof_topology_token *tokens;
	int i, j;

	token_list = tplg_ops ? tplg_ops->token_list : NULL;
	/* nothing to do if token_list is NULL */
	if (!token_list)
		return 0;

	if (token_list[token_id].count < 0) {
		dev_err(scomp->dev, "Invalid token count for token ID: %d\n", token_id);
		return -EINVAL;
	}

	/* No tokens to match */
	if (!token_list[token_id].count)
		return 0;

	tokens = token_list[token_id].tokens;
	if (!tokens) {
		dev_err(scomp->dev, "Invalid tokens for token id: %d\n", token_id);
		return -EINVAL;
	}

	for (i = 0; i < token_list[token_id].count; i++) {
		int offset = 0;
		int num_tokens_matched = 0;

		for (j = 0; j < num_tuples; j++) {
			if (tokens[i].token == tuples[j].token) {
				switch (tokens[i].type) {
				case SND_SOC_TPLG_TUPLE_TYPE_WORD:
				{
					u32 *val = (u32 *)((u8 *)object + tokens[i].offset +
							   offset);

					*val = tuples[j].value.v;
					break;
				}
				case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
				case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
				{
					u16 *val = (u16 *)((u8 *)object + tokens[i].offset +
							    offset);

					*val = (u16)tuples[j].value.v;
					break;
				}
				case SND_SOC_TPLG_TUPLE_TYPE_STRING:
				{
					if (!tokens[i].get_token) {
						dev_err(scomp->dev,
							"get_token not defined for token %d in %s\n",
							tokens[i].token, token_list[token_id].name);
						return -EINVAL;
					}

					tokens[i].get_token((void *)tuples[j].value.s, object,
							    tokens[i].offset + offset);
					break;
				}
				default:
					break;
				}

				num_tokens_matched++;

				/* found all required sets of current token. Move to the next one */
				if (!(num_tokens_matched % token_instance_num))
					break;

				/* move to the next object */
				offset += object_size;
			}
		}
	}

	return 0;
}

static inline int get_tlv_data(const int *p, int tlv[SOF_TLV_ITEMS])
{
	/* we only support dB scale TLV type at the moment */
	if ((int)p[SNDRV_CTL_TLVO_TYPE] != SNDRV_CTL_TLVT_DB_SCALE)
		return -EINVAL;

	/* min value in topology tlv data is multiplied by 100 */
	tlv[TLV_MIN] = (int)p[SNDRV_CTL_TLVO_DB_SCALE_MIN] / 100;

	/* volume steps */
	tlv[TLV_STEP] = (int)(p[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] &
				TLV_DB_SCALE_MASK);

	/* mute ON/OFF */
	if ((p[SNDRV_CTL_TLVO_DB_SCALE_MUTE_AND_STEP] &
		TLV_DB_SCALE_MUTE) == 0)
		tlv[TLV_MUTE] = 0;
	else
		tlv[TLV_MUTE] = 1;

	return 0;
}

/*
 * Function to truncate an unsigned 64-bit number
 * by x bits and return 32-bit unsigned number. This
 * function also takes care of rounding while truncating
 */
static inline u32 vol_shift_64(u64 i, u32 x)
{
	/* do not truncate more than 32 bits */
	if (x > 32)
		x = 32;

	if (x == 0)
		return (u32)i;

	return (u32)(((i >> (x - 1)) + 1) >> 1);
}

/*
 * Function to compute a ^ exp where,
 * a is a fractional number represented by a fixed-point
 * integer with a fractional world length of "fwl"
 * exp is an integer
 * fwl is the fractional word length
 * Return value is a fractional number represented by a
 * fixed-point integer with a fractional word length of "fwl"
 */
static u32 vol_pow32(u32 a, int exp, u32 fwl)
{
	int i, iter;
	u32 power = 1 << fwl;
	u64 numerator;

	/* if exponent is 0, return 1 */
	if (exp == 0)
		return power;

	/* determine the number of iterations based on the exponent */
	if (exp < 0)
		iter = exp * -1;
	else
		iter = exp;

	/* mutiply a "iter" times to compute power */
	for (i = 0; i < iter; i++) {
		/*
		 * Product of 2 Qx.fwl fixed-point numbers yields a Q2*x.2*fwl
		 * Truncate product back to fwl fractional bits with rounding
		 */
		power = vol_shift_64((u64)power * a, fwl);
	}

	if (exp > 0) {
		/* if exp is positive, return the result */
		return power;
	}

	/* if exp is negative, return the multiplicative inverse */
	numerator = (u64)1 << (fwl << 1);
	do_div(numerator, power);

	return (u32)numerator;
}

/*
 * Function to calculate volume gain from TLV data.
 * This function can only handle gain steps that are multiples of 0.5 dB
 */
u32 vol_compute_gain(u32 value, int *tlv)
{
	int dB_gain;
	u32 linear_gain;
	int f_step;

	/* mute volume */
	if (value == 0 && tlv[TLV_MUTE])
		return 0;

	/*
	 * compute dB gain from tlv. tlv_step
	 * in topology is multiplied by 100
	 */
	dB_gain = tlv[TLV_MIN] + (value * tlv[TLV_STEP]) / 100;

	/*
	 * compute linear gain represented by fixed-point
	 * int with VOLUME_FWL fractional bits
	 */
	linear_gain = vol_pow32(VOL_TWENTIETH_ROOT_OF_TEN, dB_gain, VOLUME_FWL);

	/* extract the fractional part of volume step */
	f_step = tlv[TLV_STEP] - (tlv[TLV_STEP] / 100);

	/* if volume step is an odd multiple of 0.5 dB */
	if (f_step == VOL_HALF_DB_STEP && (value & 1))
		linear_gain = vol_shift_64((u64)linear_gain *
						  VOL_FORTIETH_ROOT_OF_TEN,
						  VOLUME_FWL);

	return linear_gain;
}

/*
 * Set up volume table for kcontrols from tlv data
 * "size" specifies the number of entries in the table
 */
static int set_up_volume_table(struct snd_sof_control *scontrol,
			       int tlv[SOF_TLV_ITEMS], int size)
{
	struct snd_soc_component *scomp = scontrol->scomp;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);

	if (tplg_ops && tplg_ops->control && tplg_ops->control->set_up_volume_table)
		return tplg_ops->control->set_up_volume_table(scontrol, tlv, size);

	dev_err(scomp->dev, "Mandatory op %s not set\n", __func__);
	return -EINVAL;
}

struct sof_dai_types {
	const char *name;
	enum sof_ipc_dai_type type;
};

static const struct sof_dai_types sof_dais[] = {
	{"SSP", SOF_DAI_INTEL_SSP},
	{"HDA", SOF_DAI_INTEL_HDA},
	{"DMIC", SOF_DAI_INTEL_DMIC},
	{"ALH", SOF_DAI_INTEL_ALH},
	{"SAI", SOF_DAI_IMX_SAI},
	{"ESAI", SOF_DAI_IMX_ESAI},
	{"ACPBT", SOF_DAI_AMD_BT},
	{"ACPSP", SOF_DAI_AMD_SP},
	{"ACPDMIC", SOF_DAI_AMD_DMIC},
	{"ACPHS", SOF_DAI_AMD_HS},
	{"AFE", SOF_DAI_MEDIATEK_AFE},
	{"ACPSP_VIRTUAL", SOF_DAI_AMD_SP_VIRTUAL},
	{"ACPHS_VIRTUAL", SOF_DAI_AMD_HS_VIRTUAL},
	{"MICFIL", SOF_DAI_IMX_MICFIL},
	{"ACP_SDW", SOF_DAI_AMD_SDW},

};

static enum sof_ipc_dai_type find_dai(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_dais); i++) {
		if (strcmp(name, sof_dais[i].name) == 0)
			return sof_dais[i].type;
	}

	return SOF_DAI_INTEL_NONE;
}

/*
 * Supported Frame format types and lookup, add new ones to end of list.
 */

struct sof_frame_types {
	const char *name;
	enum sof_ipc_frame frame;
};

static const struct sof_frame_types sof_frames[] = {
	{"s16le", SOF_IPC_FRAME_S16_LE},
	{"s24le", SOF_IPC_FRAME_S24_4LE},
	{"s32le", SOF_IPC_FRAME_S32_LE},
	{"float", SOF_IPC_FRAME_FLOAT},
};

static enum sof_ipc_frame find_format(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sof_frames); i++) {
		if (strcmp(name, sof_frames[i].name) == 0)
			return sof_frames[i].frame;
	}

	/* use s32le if nothing is specified */
	return SOF_IPC_FRAME_S32_LE;
}

int get_token_u32(void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = le32_to_cpu(velem->value);
	return 0;
}

int get_token_u16(void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	u16 *val = (u16 *)((u8 *)object + offset);

	*val = (u16)le32_to_cpu(velem->value);
	return 0;
}

int get_token_uuid(void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_uuid_elem *velem = elem;
	u8 *dst = (u8 *)object + offset;

	memcpy(dst, velem->uuid, UUID_SIZE);

	return 0;
}

/*
 * The string gets from topology will be stored in heap, the owner only
 * holds a char* member point to the heap.
 */
int get_token_string(void *elem, void *object, u32 offset)
{
	/* "dst" here points to the char* member of the owner */
	char **dst = (char **)((u8 *)object + offset);

	*dst = kstrdup(elem, GFP_KERNEL);
	if (!*dst)
		return -ENOMEM;
	return 0;
};

int get_token_comp_format(void *elem, void *object, u32 offset)
{
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_format((const char *)elem);
	return 0;
}

int get_token_dai_type(void *elem, void *object, u32 offset)
{
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = find_dai((const char *)elem);
	return 0;
}

/* PCM */
static const struct sof_topology_token stream_tokens[] = {
	{SOF_TKN_STREAM_PLAYBACK_COMPATIBLE_D0I3, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_pcm, stream[0].d0i3_compatible)},
	{SOF_TKN_STREAM_CAPTURE_COMPATIBLE_D0I3, SND_SOC_TPLG_TUPLE_TYPE_BOOL, get_token_u16,
		offsetof(struct snd_sof_pcm, stream[1].d0i3_compatible)},
};

/* Leds */
static const struct sof_topology_token led_tokens[] = {
	{SOF_TKN_MUTE_LED_USE, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct snd_sof_led_control, use_led)},
	{SOF_TKN_MUTE_LED_DIRECTION, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct snd_sof_led_control, direction)},
};

static const struct sof_topology_token comp_pin_tokens[] = {
	{SOF_TKN_COMP_NUM_INPUT_PINS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct snd_sof_widget, num_input_pins)},
	{SOF_TKN_COMP_NUM_OUTPUT_PINS, SND_SOC_TPLG_TUPLE_TYPE_WORD, get_token_u32,
		offsetof(struct snd_sof_widget, num_output_pins)},
};

static const struct sof_topology_token comp_input_pin_binding_tokens[] = {
	{SOF_TKN_COMP_INPUT_PIN_BINDING_WNAME, SND_SOC_TPLG_TUPLE_TYPE_STRING,
		get_token_string, 0},
};

static const struct sof_topology_token comp_output_pin_binding_tokens[] = {
	{SOF_TKN_COMP_OUTPUT_PIN_BINDING_WNAME, SND_SOC_TPLG_TUPLE_TYPE_STRING,
		get_token_string, 0},
};

/**
 * sof_parse_uuid_tokens - Parse multiple sets of UUID tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @offset: offset within the object pointer
 * @tokens: array of struct sof_topology_token containing the tokens to be matched
 * @num_tokens: number of tokens in tokens array
 * @array: source pointer to consecutive vendor arrays in topology
 *
 * This function parses multiple sets of string type tokens in vendor arrays
 */
static int sof_parse_uuid_tokens(struct snd_soc_component *scomp,
				  void *object, size_t offset,
				  const struct sof_topology_token *tokens, int num_tokens,
				  struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_uuid_elem *elem;
	int found = 0;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->uuid[i];

		/* search for token */
		for (j = 0; j < num_tokens; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_UUID)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			tokens[j].get_token(elem, object,
					    offset + tokens[j].offset);

			found++;
		}
	}

	return found;
}

/**
 * sof_copy_tuples - Parse tokens and copy them to the @tuples array
 * @sdev: pointer to struct snd_sof_dev
 * @array: source pointer to consecutive vendor arrays in topology
 * @array_size: size of @array
 * @token_id: Token ID associated with a token array
 * @token_instance_num: number of times the same @token_id needs to be parsed i.e. the function
 *			looks for @token_instance_num of each token in the token array associated
 *			with the @token_id
 * @tuples: tuples array to copy the matched tuples to
 * @tuples_size: size of @tuples
 * @num_copied_tuples: pointer to the number of copied tuples in the tuples array
 *
 */
static int sof_copy_tuples(struct snd_sof_dev *sdev, struct snd_soc_tplg_vendor_array *array,
			   int array_size, u32 token_id, int token_instance_num,
			   struct snd_sof_tuple *tuples, int tuples_size, int *num_copied_tuples)
{
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	const struct sof_token_info *token_list;
	const struct sof_topology_token *tokens;
	int found = 0;
	int num_tokens, asize;
	int i, j;

	token_list = tplg_ops ? tplg_ops->token_list : NULL;
	/* nothing to do if token_list is NULL */
	if (!token_list)
		return 0;

	if (!tuples || !num_copied_tuples) {
		dev_err(sdev->dev, "Invalid tuples array\n");
		return -EINVAL;
	}

	tokens = token_list[token_id].tokens;
	num_tokens = token_list[token_id].count;

	if (!tokens) {
		dev_err(sdev->dev, "No token array defined for token ID: %d\n", token_id);
		return -EINVAL;
	}

	/* check if there's space in the tuples array for new tokens */
	if (*num_copied_tuples >= tuples_size) {
		dev_err(sdev->dev, "No space in tuples array for new tokens from %s",
			token_list[token_id].name);
		return -EINVAL;
	}

	while (array_size > 0 && found < num_tokens * token_instance_num) {
		asize = le32_to_cpu(array->size);

		/* validate asize */
		if (asize < 0) {
			dev_err(sdev->dev, "Invalid array size 0x%x\n", asize);
			return -EINVAL;
		}

		/* make sure there is enough data before parsing */
		array_size -= asize;
		if (array_size < 0) {
			dev_err(sdev->dev, "Invalid array size 0x%x\n", asize);
			return -EINVAL;
		}

		/* parse element by element */
		for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
			/* search for token */
			for (j = 0; j < num_tokens; j++) {
				/* match token type */
				if (!(tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
				      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT ||
				      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BYTE ||
				      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BOOL ||
				      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_STRING))
					continue;

				if (tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_STRING) {
					struct snd_soc_tplg_vendor_string_elem *elem;

					elem = &array->string[i];

					/* match token id */
					if (tokens[j].token != le32_to_cpu(elem->token))
						continue;

					tuples[*num_copied_tuples].token = tokens[j].token;
					tuples[*num_copied_tuples].value.s = elem->string;
				} else {
					struct snd_soc_tplg_vendor_value_elem *elem;

					elem = &array->value[i];

					/* match token id */
					if (tokens[j].token != le32_to_cpu(elem->token))
						continue;

					tuples[*num_copied_tuples].token = tokens[j].token;
					tuples[*num_copied_tuples].value.v =
						le32_to_cpu(elem->value);
				}
				found++;
				(*num_copied_tuples)++;

				/* stop if there's no space for any more new tuples */
				if (*num_copied_tuples == tuples_size)
					return 0;
			}

			/* stop when we've found the required token instances */
			if (found == num_tokens * token_instance_num)
				return 0;
		}

		/* next array */
		array = (struct snd_soc_tplg_vendor_array *)((u8 *)array + asize);
	}

	return 0;
}

/**
 * sof_parse_string_tokens - Parse multiple sets of tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @offset: offset within the object pointer
 * @tokens: array of struct sof_topology_token containing the tokens to be matched
 * @num_tokens: number of tokens in tokens array
 * @array: source pointer to consecutive vendor arrays in topology
 *
 * This function parses multiple sets of string type tokens in vendor arrays
 */
static int sof_parse_string_tokens(struct snd_soc_component *scomp,
				   void *object, int offset,
				   const struct sof_topology_token *tokens, int num_tokens,
				   struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_string_elem *elem;
	int found = 0;
	int i, j, ret;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->string[i];

		/* search for token */
		for (j = 0; j < num_tokens; j++) {
			/* match token type */
			if (tokens[j].type != SND_SOC_TPLG_TUPLE_TYPE_STRING)
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* matched - now load token */
			ret = tokens[j].get_token(elem->string, object, offset + tokens[j].offset);
			if (ret < 0)
				return ret;

			found++;
		}
	}

	return found;
}

/**
 * sof_parse_word_tokens - Parse multiple sets of tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @offset: offset within the object pointer
 * @tokens: array of struct sof_topology_token containing the tokens to be matched
 * @num_tokens: number of tokens in tokens array
 * @array: source pointer to consecutive vendor arrays in topology
 *
 * This function parses multiple sets of word type tokens in vendor arrays
 */
static int sof_parse_word_tokens(struct snd_soc_component *scomp,
				  void *object, int offset,
				  const struct sof_topology_token *tokens, int num_tokens,
				  struct snd_soc_tplg_vendor_array *array)
{
	struct snd_soc_tplg_vendor_value_elem *elem;
	int found = 0;
	int i, j;

	/* parse element by element */
	for (i = 0; i < le32_to_cpu(array->num_elems); i++) {
		elem = &array->value[i];

		/* search for token */
		for (j = 0; j < num_tokens; j++) {
			/* match token type */
			if (!(tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BYTE ||
			      tokens[j].type == SND_SOC_TPLG_TUPLE_TYPE_BOOL))
				continue;

			/* match token id */
			if (tokens[j].token != le32_to_cpu(elem->token))
				continue;

			/* load token */
			tokens[j].get_token(elem, object, offset + tokens[j].offset);

			found++;
		}
	}

	return found;
}

/**
 * sof_parse_token_sets - Parse multiple sets of tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @tokens: token definition array describing what tokens to parse
 * @count: number of tokens in definition array
 * @array: source pointer to consecutive vendor arrays in topology
 * @array_size: total size of @array
 * @token_instance_num: number of times the same tokens needs to be parsed i.e. the function
 *			looks for @token_instance_num of each token in the @tokens
 * @object_size: offset to next target ipc struct with multiple sets
 *
 * This function parses multiple sets of tokens in vendor arrays into
 * consecutive ipc structs.
 */
static int sof_parse_token_sets(struct snd_soc_component *scomp,
				void *object, const struct sof_topology_token *tokens,
				int count, struct snd_soc_tplg_vendor_array *array,
				int array_size, int token_instance_num, size_t object_size)
{
	size_t offset = 0;
	int found = 0;
	int total = 0;
	int asize;
	int ret;

	while (array_size > 0 && total < count * token_instance_num) {
		asize = le32_to_cpu(array->size);

		/* validate asize */
		if (asize < 0) { /* FIXME: A zero-size array makes no sense */
			dev_err(scomp->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* make sure there is enough data before parsing */
		array_size -= asize;
		if (array_size < 0) {
			dev_err(scomp->dev, "error: invalid array size 0x%x\n",
				asize);
			return -EINVAL;
		}

		/* call correct parser depending on type */
		switch (le32_to_cpu(array->type)) {
		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			found += sof_parse_uuid_tokens(scomp, object, offset, tokens, count,
						       array);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:

			ret = sof_parse_string_tokens(scomp, object, offset, tokens, count,
						      array);
			if (ret < 0) {
				dev_err(scomp->dev, "error: no memory to copy string token\n");
				return ret;
			}

			found += ret;
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
		case SND_SOC_TPLG_TUPLE_TYPE_BYTE:
		case SND_SOC_TPLG_TUPLE_TYPE_WORD:
		case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
			found += sof_parse_word_tokens(scomp, object, offset, tokens, count,
						       array);
			break;
		default:
			dev_err(scomp->dev, "error: unknown token type %d\n",
				array->type);
			return -EINVAL;
		}

		/* next array */
		array = (struct snd_soc_tplg_vendor_array *)((u8 *)array
			+ asize);

		/* move to next target struct */
		if (found >= count) {
			offset += object_size;
			total += found;
			found = 0;
		}
	}

	return 0;
}

/**
 * sof_parse_tokens - Parse one set of tokens
 * @scomp: pointer to soc component
 * @object: target ipc struct for parsed values
 * @tokens: token definition array describing what tokens to parse
 * @num_tokens: number of tokens in definition array
 * @array: source pointer to consecutive vendor arrays in topology
 * @array_size: total size of @array
 *
 * This function parses a single set of tokens in vendor arrays into
 * consecutive ipc structs.
 */
static int sof_parse_tokens(struct snd_soc_component *scomp,  void *object,
			    const struct sof_topology_token *tokens, int num_tokens,
			    struct snd_soc_tplg_vendor_array *array,
			    int array_size)

{
	/*
	 * sof_parse_tokens is used when topology contains only a single set of
	 * identical tuples arrays. So additional parameters to
	 * sof_parse_token_sets are sets = 1 (only 1 set) and
	 * object_size = 0 (irrelevant).
	 */
	return sof_parse_token_sets(scomp, object, tokens, num_tokens, array,
				    array_size, 1, 0);
}

/*
 * Standard Kcontrols.
 */

static int sof_control_load_volume(struct snd_soc_component *scomp,
				   struct snd_sof_control *scontrol,
				   struct snd_kcontrol_new *kc,
				   struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_mixer_control *mc =
		container_of(hdr, struct snd_soc_tplg_mixer_control, hdr);
	int tlv[SOF_TLV_ITEMS];
	unsigned int mask;
	int ret;

	/* validate topology data */
	if (le32_to_cpu(mc->num_channels) > SND_SOC_TPLG_MAX_CHAN)
		return -EINVAL;

	/*
	 * If control has more than 2 channels we need to override the info. This is because even if
	 * ASoC layer has defined topology's max channel count to SND_SOC_TPLG_MAX_CHAN = 8, the
	 * pre-defined dapm control types (and related functions) creating the actual control
	 * restrict the channels only to mono or stereo.
	 */
	if (le32_to_cpu(mc->num_channels) > 2)
		kc->info = snd_sof_volume_info;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->min_volume_step = le32_to_cpu(mc->min);
	scontrol->max_volume_step = le32_to_cpu(mc->max);
	scontrol->num_channels = le32_to_cpu(mc->num_channels);

	scontrol->max = le32_to_cpu(mc->max);
	if (le32_to_cpu(mc->max) == 1)
		goto skip;

	/* extract tlv data */
	if (!kc->tlv.p || get_tlv_data(kc->tlv.p, tlv) < 0) {
		dev_err(scomp->dev, "error: invalid TLV data\n");
		return -EINVAL;
	}

	/* set up volume table */
	ret = set_up_volume_table(scontrol, tlv, le32_to_cpu(mc->max) + 1);
	if (ret < 0) {
		dev_err(scomp->dev, "error: setting up volume table\n");
		return ret;
	}

skip:
	/* set up possible led control from mixer private data */
	ret = sof_parse_tokens(scomp, &scontrol->led_ctl, led_tokens,
			       ARRAY_SIZE(led_tokens), mc->priv.array,
			       le32_to_cpu(mc->priv.size));
	if (ret != 0) {
		dev_err(scomp->dev, "error: parse led tokens failed %d\n",
			le32_to_cpu(mc->priv.size));
		goto err;
	}

	if (scontrol->led_ctl.use_led) {
		mask = scontrol->led_ctl.direction ? SNDRV_CTL_ELEM_ACCESS_MIC_LED :
							SNDRV_CTL_ELEM_ACCESS_SPK_LED;
		scontrol->access &= ~SNDRV_CTL_ELEM_ACCESS_LED_MASK;
		scontrol->access |= mask;
		kc->access &= ~SNDRV_CTL_ELEM_ACCESS_LED_MASK;
		kc->access |= mask;
		sdev->led_present = true;
	}

	dev_dbg(scomp->dev, "tplg: load kcontrol index %d chans %d\n",
		scontrol->comp_id, scontrol->num_channels);

	return 0;

err:
	if (le32_to_cpu(mc->max) > 1)
		kfree(scontrol->volume_table);

	return ret;
}

static int sof_control_load_enum(struct snd_soc_component *scomp,
				 struct snd_sof_control *scontrol,
				 struct snd_kcontrol_new *kc,
				 struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_enum_control *ec =
		container_of(hdr, struct snd_soc_tplg_enum_control, hdr);

	/* validate topology data */
	if (le32_to_cpu(ec->num_channels) > SND_SOC_TPLG_MAX_CHAN)
		return -EINVAL;

	scontrol->comp_id = sdev->next_comp_id;
	scontrol->num_channels = le32_to_cpu(ec->num_channels);

	dev_dbg(scomp->dev, "tplg: load kcontrol index %d chans %d comp_id %d\n",
		scontrol->comp_id, scontrol->num_channels, scontrol->comp_id);

	return 0;
}

static int sof_control_load_bytes(struct snd_soc_component *scomp,
				  struct snd_sof_control *scontrol,
				  struct snd_kcontrol_new *kc,
				  struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_tplg_bytes_control *control =
		container_of(hdr, struct snd_soc_tplg_bytes_control, hdr);
	struct soc_bytes_ext *sbe = (struct soc_bytes_ext *)kc->private_value;
	size_t priv_size = le32_to_cpu(control->priv.size);

	scontrol->max_size = sbe->max;
	scontrol->comp_id = sdev->next_comp_id;

	dev_dbg(scomp->dev, "tplg: load kcontrol index %d\n", scontrol->comp_id);

	/* copy the private data */
	if (priv_size > 0) {
		scontrol->priv = kmemdup(control->priv.data, priv_size, GFP_KERNEL);
		if (!scontrol->priv)
			return -ENOMEM;

		scontrol->priv_size = priv_size;
	}

	return 0;
}

/* external kcontrol init - used for any driver specific init */
static int sof_control_load(struct snd_soc_component *scomp, int index,
			    struct snd_kcontrol_new *kc,
			    struct snd_soc_tplg_ctl_hdr *hdr)
{
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct soc_enum *se;
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_soc_dobj *dobj;
	struct snd_sof_control *scontrol;
	int ret;

	dev_dbg(scomp->dev, "tplg: load control type %d name : %s\n",
		hdr->type, hdr->name);

	scontrol = kzalloc(sizeof(*scontrol), GFP_KERNEL);
	if (!scontrol)
		return -ENOMEM;

	scontrol->name = kstrdup(hdr->name, GFP_KERNEL);
	if (!scontrol->name) {
		kfree(scontrol);
		return -ENOMEM;
	}

	scontrol->scomp = scomp;
	scontrol->access = kc->access;
	scontrol->info_type = le32_to_cpu(hdr->ops.info);
	scontrol->index = kc->index;

	switch (le32_to_cpu(hdr->ops.info)) {
	case SND_SOC_TPLG_CTL_VOLSW:
	case SND_SOC_TPLG_CTL_VOLSW_SX:
	case SND_SOC_TPLG_CTL_VOLSW_XR_SX:
		sm = (struct soc_mixer_control *)kc->private_value;
		dobj = &sm->dobj;
		ret = sof_control_load_volume(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_TPLG_CTL_BYTES:
		sbe = (struct soc_bytes_ext *)kc->private_value;
		dobj = &sbe->dobj;
		ret = sof_control_load_bytes(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_TPLG_CTL_ENUM:
	case SND_SOC_TPLG_CTL_ENUM_VALUE:
		se = (struct soc_enum *)kc->private_value;
		dobj = &se->dobj;
		ret = sof_control_load_enum(scomp, scontrol, kc, hdr);
		break;
	case SND_SOC_TPLG_CTL_RANGE:
	case SND_SOC_TPLG_CTL_STROBE:
	case SND_SOC_TPLG_DAPM_CTL_VOLSW:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_DOUBLE:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VIRT:
	case SND_SOC_TPLG_DAPM_CTL_ENUM_VALUE:
	case SND_SOC_TPLG_DAPM_CTL_PIN:
	default:
		dev_warn(scomp->dev, "control type not supported %d:%d:%d\n",
			 hdr->ops.get, hdr->ops.put, hdr->ops.info);
		kfree(scontrol->name);
		kfree(scontrol);
		return 0;
	}

	if (ret < 0) {
		kfree(scontrol->name);
		kfree(scontrol);
		return ret;
	}

	scontrol->led_ctl.led_value = -1;

	dobj->private = scontrol;
	list_add(&scontrol->list, &sdev->kcontrol_list);
	return 0;
}

static int sof_control_unload(struct snd_soc_component *scomp,
			      struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	struct snd_sof_control *scontrol = dobj->private;
	int ret = 0;

	dev_dbg(scomp->dev, "tplg: unload control name : %s\n", scontrol->name);

	if (tplg_ops && tplg_ops->control_free) {
		ret = tplg_ops->control_free(sdev, scontrol);
		if (ret < 0)
			dev_err(scomp->dev, "failed to free control: %s\n", scontrol->name);
	}

	/* free all data before returning in case of error too */
	kfree(scontrol->ipc_control_data);
	kfree(scontrol->priv);
	kfree(scontrol->name);
	list_del(&scontrol->list);
	kfree(scontrol);

	return ret;
}

/*
 * DAI Topology
 */

static int sof_connect_dai_widget(struct snd_soc_component *scomp,
				  struct snd_soc_dapm_widget *w,
				  struct snd_soc_tplg_dapm_widget *tw,
				  struct snd_sof_dai *dai)
{
	struct snd_soc_card *card = scomp->card;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *cpu_dai;
	int stream;
	int i;

	if (!w->sname) {
		dev_err(scomp->dev, "Widget %s does not have stream\n", w->name);
		return -EINVAL;
	}

	if (w->id == snd_soc_dapm_dai_out)
		stream = SNDRV_PCM_STREAM_CAPTURE;
	else if (w->id == snd_soc_dapm_dai_in)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		goto end;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		/* does stream match DAI link ? */
		if (!rtd->dai_link->stream_name ||
		    !strstr(rtd->dai_link->stream_name, w->sname))
			continue;

		for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
			/*
			 * Please create DAI widget in the right order
			 * to ensure BE will connect to the right DAI
			 * widget.
			 */
			if (!snd_soc_dai_get_widget(cpu_dai, stream)) {
				snd_soc_dai_set_widget(cpu_dai, stream, w);
				break;
			}
		}
		if (i == rtd->dai_link->num_cpus) {
			dev_err(scomp->dev, "error: can't find BE for DAI %s\n", w->name);

			return -EINVAL;
		}

		dai->name = rtd->dai_link->name;
		dev_dbg(scomp->dev, "tplg: connected widget %s -> DAI link %s\n",
			w->name, rtd->dai_link->name);
	}
end:
	/* check we have a connection */
	if (!dai->name) {
		dev_err(scomp->dev, "error: can't connect DAI %s stream %s\n",
			w->name, w->sname);
		return -EINVAL;
	}

	return 0;
}

static void sof_disconnect_dai_widget(struct snd_soc_component *scomp,
				      struct snd_soc_dapm_widget *w)
{
	struct snd_soc_card *card = scomp->card;
	struct snd_soc_pcm_runtime *rtd;
	const char *sname = w->sname;
	struct snd_soc_dai *cpu_dai;
	int i, stream;

	if (!sname)
		return;

	if (w->id == snd_soc_dapm_dai_out)
		stream = SNDRV_PCM_STREAM_CAPTURE;
	else if (w->id == snd_soc_dapm_dai_in)
		stream = SNDRV_PCM_STREAM_PLAYBACK;
	else
		return;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		/* does stream match DAI link ? */
		if (!rtd->dai_link->stream_name ||
		    !strstr(rtd->dai_link->stream_name, sname))
			continue;

		for_each_rtd_cpu_dais(rtd, i, cpu_dai)
			if (snd_soc_dai_get_widget(cpu_dai, stream) == w) {
				snd_soc_dai_set_widget(cpu_dai, stream, NULL);
				break;
			}
	}
}

/* bind PCM ID to host component ID */
static int spcm_bind(struct snd_soc_component *scomp, struct snd_sof_pcm *spcm,
		     int dir)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *host_widget;

	if (sdev->dspless_mode_selected)
		return 0;

	host_widget = snd_sof_find_swidget_sname(scomp,
						 spcm->pcm.caps[dir].name,
						 dir);
	if (!host_widget) {
		dev_err(scomp->dev, "can't find host comp to bind pcm\n");
		return -EINVAL;
	}

	spcm->stream[dir].comp_id = host_widget->comp_id;

	return 0;
}

static int sof_get_token_value(u32 token_id, struct snd_sof_tuple *tuples, int num_tuples)
{
	int i;

	if (!tuples)
		return -EINVAL;

	for (i = 0; i < num_tuples; i++) {
		if (tuples[i].token == token_id)
			return tuples[i].value.v;
	}

	return -EINVAL;
}

static int sof_widget_parse_tokens(struct snd_soc_component *scomp, struct snd_sof_widget *swidget,
				   struct snd_soc_tplg_dapm_widget *tw,
				   enum sof_tokens *object_token_list, int count)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	struct snd_soc_tplg_private *private = &tw->priv;
	const struct sof_token_info *token_list;
	int num_tuples = 0;
	int ret, i;

	token_list = tplg_ops ? tplg_ops->token_list : NULL;
	/* nothing to do if token_list is NULL */
	if (!token_list)
		return 0;

	if (count > 0 && !object_token_list) {
		dev_err(scomp->dev, "No token list for widget %s\n", swidget->widget->name);
		return -EINVAL;
	}

	/* calculate max size of tuples array */
	for (i = 0; i < count; i++)
		num_tuples += token_list[object_token_list[i]].count;

	/* allocate memory for tuples array */
	swidget->tuples = kcalloc(num_tuples, sizeof(*swidget->tuples), GFP_KERNEL);
	if (!swidget->tuples)
		return -ENOMEM;

	/* parse token list for widget */
	for (i = 0; i < count; i++) {
		int num_sets = 1;

		if (object_token_list[i] >= SOF_TOKEN_COUNT) {
			dev_err(scomp->dev, "Invalid token id %d for widget %s\n",
				object_token_list[i], swidget->widget->name);
			ret = -EINVAL;
			goto err;
		}

		switch (object_token_list[i]) {
		case SOF_COMP_EXT_TOKENS:
			/* parse and save UUID in swidget */
			ret = sof_parse_tokens(scomp, swidget,
					       token_list[object_token_list[i]].tokens,
					       token_list[object_token_list[i]].count,
					       private->array, le32_to_cpu(private->size));
			if (ret < 0) {
				dev_err(scomp->dev, "Failed parsing %s for widget %s\n",
					token_list[object_token_list[i]].name,
					swidget->widget->name);
				goto err;
			}

			continue;
		case SOF_IN_AUDIO_FORMAT_TOKENS:
			num_sets = sof_get_token_value(SOF_TKN_COMP_NUM_INPUT_AUDIO_FORMATS,
						       swidget->tuples, swidget->num_tuples);
			if (num_sets < 0) {
				dev_err(sdev->dev, "Invalid input audio format count for %s\n",
					swidget->widget->name);
				ret = num_sets;
				goto err;
			}
			break;
		case SOF_OUT_AUDIO_FORMAT_TOKENS:
			num_sets = sof_get_token_value(SOF_TKN_COMP_NUM_OUTPUT_AUDIO_FORMATS,
						       swidget->tuples, swidget->num_tuples);
			if (num_sets < 0) {
				dev_err(sdev->dev, "Invalid output audio format count for %s\n",
					swidget->widget->name);
				ret = num_sets;
				goto err;
			}
			break;
		default:
			break;
		}

		if (num_sets > 1) {
			struct snd_sof_tuple *new_tuples;

			num_tuples += token_list[object_token_list[i]].count * (num_sets - 1);
			new_tuples = krealloc(swidget->tuples,
					      sizeof(*new_tuples) * num_tuples, GFP_KERNEL);
			if (!new_tuples) {
				ret = -ENOMEM;
				goto err;
			}

			swidget->tuples = new_tuples;
		}

		/* copy one set of tuples per token ID into swidget->tuples */
		ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
				      object_token_list[i], num_sets, swidget->tuples,
				      num_tuples, &swidget->num_tuples);
		if (ret < 0) {
			dev_err(scomp->dev, "Failed parsing %s for widget %s err: %d\n",
				token_list[object_token_list[i]].name, swidget->widget->name, ret);
			goto err;
		}
	}

	return 0;
err:
	kfree(swidget->tuples);
	return ret;
}

static void sof_free_pin_binding(struct snd_sof_widget *swidget,
				 bool pin_type)
{
	char **pin_binding;
	u32 num_pins;
	int i;

	if (pin_type == SOF_PIN_TYPE_INPUT) {
		pin_binding = swidget->input_pin_binding;
		num_pins = swidget->num_input_pins;
	} else {
		pin_binding = swidget->output_pin_binding;
		num_pins = swidget->num_output_pins;
	}

	if (pin_binding) {
		for (i = 0; i < num_pins; i++)
			kfree(pin_binding[i]);
	}

	kfree(pin_binding);
}

static int sof_parse_pin_binding(struct snd_sof_widget *swidget,
				 struct snd_soc_tplg_private *priv, bool pin_type)
{
	const struct sof_topology_token *pin_binding_token;
	char *pin_binding[SOF_WIDGET_MAX_NUM_PINS];
	int token_count;
	u32 num_pins;
	char **pb;
	int ret;
	int i;

	if (pin_type == SOF_PIN_TYPE_INPUT) {
		num_pins = swidget->num_input_pins;
		pin_binding_token = comp_input_pin_binding_tokens;
		token_count = ARRAY_SIZE(comp_input_pin_binding_tokens);
	} else {
		num_pins = swidget->num_output_pins;
		pin_binding_token = comp_output_pin_binding_tokens;
		token_count = ARRAY_SIZE(comp_output_pin_binding_tokens);
	}

	memset(pin_binding, 0, SOF_WIDGET_MAX_NUM_PINS * sizeof(char *));
	ret = sof_parse_token_sets(swidget->scomp, pin_binding, pin_binding_token,
				   token_count, priv->array, le32_to_cpu(priv->size),
				   num_pins, sizeof(char *));
	if (ret < 0)
		goto err;

	/* copy pin binding array to swidget only if it is defined in topology */
	if (pin_binding[0]) {
		pb = kmemdup_array(pin_binding, num_pins, sizeof(char *), GFP_KERNEL);
		if (!pb) {
			ret = -ENOMEM;
			goto err;
		}
		if (pin_type == SOF_PIN_TYPE_INPUT)
			swidget->input_pin_binding = pb;
		else
			swidget->output_pin_binding = pb;
	}

	return 0;

err:
	for (i = 0; i < num_pins; i++)
		kfree(pin_binding[i]);

	return ret;
}

static int get_w_no_wname_in_long_name(void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	struct snd_soc_dapm_widget *w = object;

	w->no_wname_in_kcontrol_name = !!le32_to_cpu(velem->value);
	return 0;
}

static const struct sof_topology_token dapm_widget_tokens[] = {
	{SOF_TKN_COMP_NO_WNAME_IN_KCONTROL_NAME, SND_SOC_TPLG_TUPLE_TYPE_BOOL,
	 get_w_no_wname_in_long_name, 0}
};

/* external widget init - used for any driver specific init */
static int sof_widget_ready(struct snd_soc_component *scomp, int index,
			    struct snd_soc_dapm_widget *w,
			    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	const struct sof_ipc_tplg_widget_ops *widget_ops;
	struct snd_soc_tplg_private *priv = &tw->priv;
	enum sof_tokens *token_list = NULL;
	struct snd_sof_widget *swidget;
	struct snd_sof_dai *dai;
	int token_list_size = 0;
	int ret = 0;

	swidget = kzalloc(sizeof(*swidget), GFP_KERNEL);
	if (!swidget)
		return -ENOMEM;

	swidget->scomp = scomp;
	swidget->widget = w;
	swidget->comp_id = sdev->next_comp_id++;
	swidget->id = w->id;
	swidget->pipeline_id = index;
	swidget->private = NULL;
	mutex_init(&swidget->setup_mutex);

	ida_init(&swidget->output_queue_ida);
	ida_init(&swidget->input_queue_ida);

	ret = sof_parse_tokens(scomp, w, dapm_widget_tokens, ARRAY_SIZE(dapm_widget_tokens),
			       priv->array, le32_to_cpu(priv->size));
	if (ret < 0) {
		dev_err(scomp->dev, "failed to parse dapm widget tokens for %s\n",
			w->name);
		goto widget_free;
	}

	ret = sof_parse_tokens(scomp, swidget, comp_pin_tokens,
			       ARRAY_SIZE(comp_pin_tokens), priv->array,
			       le32_to_cpu(priv->size));
	if (ret < 0) {
		dev_err(scomp->dev, "failed to parse component pin tokens for %s\n",
			w->name);
		goto widget_free;
	}

	if (swidget->num_input_pins > SOF_WIDGET_MAX_NUM_PINS ||
	    swidget->num_output_pins > SOF_WIDGET_MAX_NUM_PINS) {
		dev_err(scomp->dev, "invalid pins for %s: [input: %d, output: %d]\n",
			swidget->widget->name, swidget->num_input_pins, swidget->num_output_pins);
		ret = -EINVAL;
		goto widget_free;
	}

	if (swidget->num_input_pins > 1) {
		ret = sof_parse_pin_binding(swidget, priv, SOF_PIN_TYPE_INPUT);
		/* on parsing error, pin binding is not allocated, nothing to free. */
		if (ret < 0) {
			dev_err(scomp->dev, "failed to parse input pin binding for %s\n",
				w->name);
			goto widget_free;
		}
	}

	if (swidget->num_output_pins > 1) {
		ret = sof_parse_pin_binding(swidget, priv, SOF_PIN_TYPE_OUTPUT);
		/* on parsing error, pin binding is not allocated, nothing to free. */
		if (ret < 0) {
			dev_err(scomp->dev, "failed to parse output pin binding for %s\n",
				w->name);
			goto widget_free;
		}
	}

	dev_dbg(scomp->dev,
		"tplg: widget %d (%s) is ready [type: %d, pipe: %d, pins: %d / %d, stream: %s]\n",
		swidget->comp_id, w->name, swidget->id, index,
		swidget->num_input_pins, swidget->num_output_pins,
		strnlen(w->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0 ? w->sname : "none");

	widget_ops = tplg_ops ? tplg_ops->widget : NULL;
	if (widget_ops) {
		token_list = widget_ops[w->id].token_list;
		token_list_size = widget_ops[w->id].token_list_size;
	}

	/* handle any special case widgets */
	switch (w->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = kzalloc(sizeof(*dai), GFP_KERNEL);
		if (!dai) {
			ret = -ENOMEM;
			goto widget_free;
		}

		ret = sof_widget_parse_tokens(scomp, swidget, tw, token_list, token_list_size);
		if (!ret)
			ret = sof_connect_dai_widget(scomp, w, tw, dai);
		if (ret < 0) {
			kfree(dai);
			break;
		}
		list_add(&dai->list, &sdev->dai_list);
		swidget->private = dai;
		break;
	case snd_soc_dapm_effect:
		/* check we have some tokens - we need at least process type */
		if (le32_to_cpu(tw->priv.size) == 0) {
			dev_err(scomp->dev, "error: process tokens not found\n");
			ret = -EINVAL;
			break;
		}
		ret = sof_widget_parse_tokens(scomp, swidget, tw, token_list, token_list_size);
		break;
	case snd_soc_dapm_pga:
		if (!le32_to_cpu(tw->num_kcontrols)) {
			dev_err(scomp->dev, "invalid kcontrol count %d for volume\n",
				tw->num_kcontrols);
			ret = -EINVAL;
			break;
		}

		fallthrough;
	case snd_soc_dapm_mixer:
	case snd_soc_dapm_buffer:
	case snd_soc_dapm_scheduler:
	case snd_soc_dapm_aif_out:
	case snd_soc_dapm_aif_in:
	case snd_soc_dapm_src:
	case snd_soc_dapm_asrc:
	case snd_soc_dapm_siggen:
	case snd_soc_dapm_mux:
	case snd_soc_dapm_demux:
		ret = sof_widget_parse_tokens(scomp, swidget, tw,  token_list, token_list_size);
		break;
	case snd_soc_dapm_switch:
	case snd_soc_dapm_dai_link:
	case snd_soc_dapm_kcontrol:
	default:
		dev_dbg(scomp->dev, "widget type %d name %s not handled\n", swidget->id, tw->name);
		break;
	}

	/* check token parsing reply */
	if (ret < 0) {
		dev_err(scomp->dev,
			"failed to add widget type %d name : %s stream %s\n",
			swidget->id, tw->name, strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
							? tw->sname : "none");
		goto widget_free;
	}

	if (sof_debug_check_flag(SOF_DBG_DISABLE_MULTICORE)) {
		swidget->core = SOF_DSP_PRIMARY_CORE;
	} else {
		int core = sof_get_token_value(SOF_TKN_COMP_CORE_ID, swidget->tuples,
					       swidget->num_tuples);

		if (core >= 0)
			swidget->core = core;
	}

	/* bind widget to external event */
	if (tw->event_type) {
		if (widget_ops && widget_ops[w->id].bind_event) {
			ret = widget_ops[w->id].bind_event(scomp, swidget,
							   le16_to_cpu(tw->event_type));
			if (ret) {
				dev_err(scomp->dev, "widget event binding failed for %s\n",
					swidget->widget->name);
				goto free;
			}
		}
	}

	/* create and add pipeline for scheduler type widgets */
	if (w->id == snd_soc_dapm_scheduler) {
		struct snd_sof_pipeline *spipe;

		spipe = kzalloc(sizeof(*spipe), GFP_KERNEL);
		if (!spipe) {
			ret = -ENOMEM;
			goto free;
		}

		spipe->pipe_widget = swidget;
		swidget->spipe = spipe;
		list_add(&spipe->list, &sdev->pipeline_list);
	}

	w->dobj.private = swidget;
	list_add(&swidget->list, &sdev->widget_list);
	return ret;
free:
	kfree(swidget->private);
	kfree(swidget->tuples);
widget_free:
	kfree(swidget);
	return ret;
}

static int sof_route_unload(struct snd_soc_component *scomp,
			    struct snd_soc_dobj *dobj)
{
	struct snd_sof_route *sroute;

	sroute = dobj->private;
	if (!sroute)
		return 0;

	/* free sroute and its private data */
	kfree(sroute->private);
	list_del(&sroute->list);
	kfree(sroute);

	return 0;
}

static int sof_widget_unload(struct snd_soc_component *scomp,
			     struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	const struct sof_ipc_tplg_widget_ops *widget_ops;
	const struct snd_kcontrol_new *kc;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct snd_sof_dai *dai;
	struct soc_enum *se;
	int i;

	swidget = dobj->private;
	if (!swidget)
		return 0;

	widget = swidget->widget;

	switch (swidget->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = swidget->private;

		if (dai)
			list_del(&dai->list);

		sof_disconnect_dai_widget(scomp, widget);

		break;
	case snd_soc_dapm_scheduler:
	{
		struct snd_sof_pipeline *spipe = swidget->spipe;

		list_del(&spipe->list);
		kfree(spipe);
		swidget->spipe = NULL;
		break;
	}
	default:
		break;
	}
	for (i = 0; i < widget->num_kcontrols; i++) {
		kc = &widget->kcontrol_news[i];
		switch (widget->dobj.widget.kcontrol_type[i]) {
		case SND_SOC_TPLG_TYPE_MIXER:
			sm = (struct soc_mixer_control *)kc->private_value;
			scontrol = sm->dobj.private;
			if (sm->max > 1)
				kfree(scontrol->volume_table);
			break;
		case SND_SOC_TPLG_TYPE_ENUM:
			se = (struct soc_enum *)kc->private_value;
			scontrol = se->dobj.private;
			break;
		case SND_SOC_TPLG_TYPE_BYTES:
			sbe = (struct soc_bytes_ext *)kc->private_value;
			scontrol = sbe->dobj.private;
			break;
		default:
			dev_warn(scomp->dev, "unsupported kcontrol_type\n");
			goto out;
		}
		kfree(scontrol->ipc_control_data);
		list_del(&scontrol->list);
		kfree(scontrol->name);
		kfree(scontrol);
	}

out:
	/* free IPC related data */
	widget_ops = tplg_ops ? tplg_ops->widget : NULL;
	if (widget_ops && widget_ops[swidget->id].ipc_free)
		widget_ops[swidget->id].ipc_free(swidget);

	ida_destroy(&swidget->output_queue_ida);
	ida_destroy(&swidget->input_queue_ida);

	sof_free_pin_binding(swidget, SOF_PIN_TYPE_INPUT);
	sof_free_pin_binding(swidget, SOF_PIN_TYPE_OUTPUT);

	kfree(swidget->tuples);

	/* remove and free swidget object */
	list_del(&swidget->list);
	kfree(swidget);

	return 0;
}

/*
 * DAI HW configuration.
 */

/* FE DAI - used for any driver specific init */
static int sof_dai_load(struct snd_soc_component *scomp, int index,
			struct snd_soc_dai_driver *dai_drv,
			struct snd_soc_tplg_pcm *pcm, struct snd_soc_dai *dai)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_pcm_ops *ipc_pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_soc_tplg_stream_caps *caps;
	struct snd_soc_tplg_private *private = &pcm->priv;
	struct snd_sof_pcm *spcm;
	int stream;
	int ret;

	/* nothing to do for BEs atm */
	if (!pcm)
		return 0;

	spcm = kzalloc(sizeof(*spcm), GFP_KERNEL);
	if (!spcm)
		return -ENOMEM;

	spcm->scomp = scomp;

	for_each_pcm_streams(stream) {
		spcm->stream[stream].comp_id = COMP_ID_UNASSIGNED;
		if (pcm->compress)
			snd_sof_compr_init_elapsed_work(&spcm->stream[stream].period_elapsed_work);
		else
			snd_sof_pcm_init_elapsed_work(&spcm->stream[stream].period_elapsed_work);
	}

	spcm->pcm = *pcm;
	dev_dbg(scomp->dev, "tplg: load pcm %s\n", pcm->dai_name);

	/* perform pcm set op */
	if (ipc_pcm_ops && ipc_pcm_ops->pcm_setup) {
		ret = ipc_pcm_ops->pcm_setup(sdev, spcm);
		if (ret < 0) {
			kfree(spcm);
			return ret;
		}
	}

	dai_drv->dobj.private = spcm;
	list_add(&spcm->list, &sdev->pcm_list);

	ret = sof_parse_tokens(scomp, spcm, stream_tokens,
			       ARRAY_SIZE(stream_tokens), private->array,
			       le32_to_cpu(private->size));
	if (ret) {
		dev_err(scomp->dev, "error: parse stream tokens failed %d\n",
			le32_to_cpu(private->size));
		return ret;
	}

	/* do we need to allocate playback PCM DMA pages */
	if (!spcm->pcm.playback)
		goto capture;

	stream = SNDRV_PCM_STREAM_PLAYBACK;

	caps = &spcm->pcm.caps[stream];

	/* allocate playback page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(scomp->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);

		return ret;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(scomp, spcm, stream);
	if (ret) {
		dev_err(scomp->dev,
			"error: can't bind pcm to host\n");
		goto free_playback_tables;
	}

capture:
	stream = SNDRV_PCM_STREAM_CAPTURE;

	/* do we need to allocate capture PCM DMA pages */
	if (!spcm->pcm.capture)
		return ret;

	caps = &spcm->pcm.caps[stream];

	/* allocate capture page table buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, sdev->dev,
				  PAGE_SIZE, &spcm->stream[stream].page_table);
	if (ret < 0) {
		dev_err(scomp->dev, "error: can't alloc page table for %s %d\n",
			caps->name, ret);
		goto free_playback_tables;
	}

	/* bind pcm to host comp */
	ret = spcm_bind(scomp, spcm, stream);
	if (ret) {
		dev_err(scomp->dev,
			"error: can't bind pcm to host\n");
		snd_dma_free_pages(&spcm->stream[stream].page_table);
		goto free_playback_tables;
	}

	return ret;

free_playback_tables:
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	return ret;
}

static int sof_dai_unload(struct snd_soc_component *scomp,
			  struct snd_soc_dobj *dobj)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_pcm_ops *ipc_pcm_ops = sof_ipc_get_ops(sdev, pcm);
	struct snd_sof_pcm *spcm = dobj->private;

	/* free PCM DMA pages */
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	if (spcm->pcm.capture)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_CAPTURE].page_table);

	/* perform pcm free op */
	if (ipc_pcm_ops && ipc_pcm_ops->pcm_free)
		ipc_pcm_ops->pcm_free(sdev, spcm);

	/* remove from list and free spcm */
	list_del(&spcm->list);
	kfree(spcm);

	return 0;
}

static const struct sof_topology_token common_dai_link_tokens[] = {
	{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type,
		offsetof(struct snd_sof_dai_link, type)},
};

/* DAI link - used for any driver specific init */
static int sof_link_load(struct snd_soc_component *scomp, int index, struct snd_soc_dai_link *link,
			 struct snd_soc_tplg_link_config *cfg)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	struct snd_soc_tplg_private *private = &cfg->priv;
	const struct sof_token_info *token_list;
	struct snd_sof_dai_link *slink;
	u32 token_id = 0;
	int num_tuples = 0;
	int ret, num_sets;

	if (!link->platforms) {
		dev_err(scomp->dev, "error: no platforms\n");
		return -EINVAL;
	}
	link->platforms->name = dev_name(scomp->dev);

	if (tplg_ops && tplg_ops->link_setup) {
		ret = tplg_ops->link_setup(sdev, link);
		if (ret < 0)
			return ret;
	}

	/* Set nonatomic property for FE dai links as their trigger action involves IPC's */
	if (!link->no_pcm) {
		link->nonatomic = true;
		return 0;
	}

	/* check we have some tokens - we need at least DAI type */
	if (le32_to_cpu(private->size) == 0) {
		dev_err(scomp->dev, "error: expected tokens for DAI, none found\n");
		return -EINVAL;
	}

	slink = kzalloc(sizeof(*slink), GFP_KERNEL);
	if (!slink)
		return -ENOMEM;

	slink->num_hw_configs = le32_to_cpu(cfg->num_hw_configs);
	slink->hw_configs = kmemdup_array(cfg->hw_config,
					  slink->num_hw_configs, sizeof(*slink->hw_configs),
					  GFP_KERNEL);
	if (!slink->hw_configs) {
		kfree(slink);
		return -ENOMEM;
	}

	slink->default_hw_cfg_id = le32_to_cpu(cfg->default_hw_config_id);
	slink->link = link;

	dev_dbg(scomp->dev, "tplg: %d hw_configs found, default id: %d for dai link %s!\n",
		slink->num_hw_configs, slink->default_hw_cfg_id, link->name);

	ret = sof_parse_tokens(scomp, slink, common_dai_link_tokens,
			       ARRAY_SIZE(common_dai_link_tokens),
			       private->array, le32_to_cpu(private->size));
	if (ret < 0) {
		dev_err(scomp->dev, "Failed tp parse common DAI link tokens\n");
		kfree(slink->hw_configs);
		kfree(slink);
		return ret;
	}

	token_list = tplg_ops ? tplg_ops->token_list : NULL;
	if (!token_list)
		goto out;

	/* calculate size of tuples array */
	num_tuples += token_list[SOF_DAI_LINK_TOKENS].count;
	num_sets = slink->num_hw_configs;
	switch (slink->type) {
	case SOF_DAI_INTEL_SSP:
		token_id = SOF_SSP_TOKENS;
		num_tuples += token_list[SOF_SSP_TOKENS].count * slink->num_hw_configs;
		break;
	case SOF_DAI_INTEL_DMIC:
		token_id = SOF_DMIC_TOKENS;
		num_tuples += token_list[SOF_DMIC_TOKENS].count;

		/* Allocate memory for max PDM controllers */
		num_tuples += token_list[SOF_DMIC_PDM_TOKENS].count * SOF_DAI_INTEL_DMIC_NUM_CTRL;
		break;
	case SOF_DAI_INTEL_HDA:
		token_id = SOF_HDA_TOKENS;
		num_tuples += token_list[SOF_HDA_TOKENS].count;
		break;
	case SOF_DAI_INTEL_ALH:
		token_id = SOF_ALH_TOKENS;
		num_tuples += token_list[SOF_ALH_TOKENS].count;
		break;
	case SOF_DAI_IMX_SAI:
		token_id = SOF_SAI_TOKENS;
		num_tuples += token_list[SOF_SAI_TOKENS].count;
		break;
	case SOF_DAI_IMX_ESAI:
		token_id = SOF_ESAI_TOKENS;
		num_tuples += token_list[SOF_ESAI_TOKENS].count;
		break;
	case SOF_DAI_MEDIATEK_AFE:
		token_id = SOF_AFE_TOKENS;
		num_tuples += token_list[SOF_AFE_TOKENS].count;
		break;
	case SOF_DAI_AMD_DMIC:
		token_id = SOF_ACPDMIC_TOKENS;
		num_tuples += token_list[SOF_ACPDMIC_TOKENS].count;
		break;
	case SOF_DAI_AMD_BT:
	case SOF_DAI_AMD_SP:
	case SOF_DAI_AMD_HS:
	case SOF_DAI_AMD_SP_VIRTUAL:
	case SOF_DAI_AMD_HS_VIRTUAL:
		token_id = SOF_ACPI2S_TOKENS;
		num_tuples += token_list[SOF_ACPI2S_TOKENS].count;
		break;
	case SOF_DAI_IMX_MICFIL:
		token_id = SOF_MICFIL_TOKENS;
		num_tuples += token_list[SOF_MICFIL_TOKENS].count;
		break;
	case SOF_DAI_AMD_SDW:
		token_id = SOF_ACP_SDW_TOKENS;
		num_tuples += token_list[SOF_ACP_SDW_TOKENS].count;
		break;
	default:
		break;
	}

	/* allocate memory for tuples array */
	slink->tuples = kcalloc(num_tuples, sizeof(*slink->tuples), GFP_KERNEL);
	if (!slink->tuples) {
		kfree(slink->hw_configs);
		kfree(slink);
		return -ENOMEM;
	}

	if (token_list[SOF_DAI_LINK_TOKENS].tokens) {
		/* parse one set of DAI link tokens */
		ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
				      SOF_DAI_LINK_TOKENS, 1, slink->tuples,
				      num_tuples, &slink->num_tuples);
		if (ret < 0) {
			dev_err(scomp->dev, "failed to parse %s for dai link %s\n",
				token_list[SOF_DAI_LINK_TOKENS].name, link->name);
			goto err;
		}
	}

	/* nothing more to do if there are no DAI type-specific tokens defined */
	if (!token_id || !token_list[token_id].tokens)
		goto out;

	/* parse "num_sets" sets of DAI-specific tokens */
	ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
			      token_id, num_sets, slink->tuples, num_tuples, &slink->num_tuples);
	if (ret < 0) {
		dev_err(scomp->dev, "failed to parse %s for dai link %s\n",
			token_list[token_id].name, link->name);
		goto err;
	}

	/* for DMIC, also parse all sets of DMIC PDM tokens based on active PDM count */
	if (token_id == SOF_DMIC_TOKENS) {
		num_sets = sof_get_token_value(SOF_TKN_INTEL_DMIC_NUM_PDM_ACTIVE,
					       slink->tuples, slink->num_tuples);

		if (num_sets < 0) {
			dev_err(sdev->dev, "Invalid active PDM count for %s\n", link->name);
			ret = num_sets;
			goto err;
		}

		ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
				      SOF_DMIC_PDM_TOKENS, num_sets, slink->tuples,
				      num_tuples, &slink->num_tuples);
		if (ret < 0) {
			dev_err(scomp->dev, "failed to parse %s for dai link %s\n",
				token_list[SOF_DMIC_PDM_TOKENS].name, link->name);
			goto err;
		}
	}
out:
	link->dobj.private = slink;
	list_add(&slink->list, &sdev->dai_link_list);

	return 0;

err:
	kfree(slink->tuples);
	kfree(slink->hw_configs);
	kfree(slink);

	return ret;
}

static int sof_link_unload(struct snd_soc_component *scomp, struct snd_soc_dobj *dobj)
{
	struct snd_sof_dai_link *slink = dobj->private;

	if (!slink)
		return 0;

	slink->link->platforms->name = NULL;

	kfree(slink->tuples);
	list_del(&slink->list);
	kfree(slink->hw_configs);
	kfree(slink);
	dobj->private = NULL;

	return 0;
}

/* DAI link - used for any driver specific init */
static int sof_route_load(struct snd_soc_component *scomp, int index,
			  struct snd_soc_dapm_route *route)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *source_swidget, *sink_swidget;
	struct snd_soc_dobj *dobj = &route->dobj;
	struct snd_sof_route *sroute;
	int ret = 0;

	/* allocate memory for sroute and connect */
	sroute = kzalloc(sizeof(*sroute), GFP_KERNEL);
	if (!sroute)
		return -ENOMEM;

	sroute->scomp = scomp;
	dev_dbg(scomp->dev, "sink %s control %s source %s\n",
		route->sink, route->control ? route->control : "none",
		route->source);

	/* source component */
	source_swidget = snd_sof_find_swidget(scomp, (char *)route->source);
	if (!source_swidget) {
		dev_err(scomp->dev, "error: source %s not found\n",
			route->source);
		ret = -EINVAL;
		goto err;
	}

	/*
	 * Virtual widgets of type output/out_drv may be added in topology
	 * for compatibility. These are not handled by the FW.
	 * So, don't send routes whose source/sink widget is of such types
	 * to the DSP.
	 */
	if (source_swidget->id == snd_soc_dapm_out_drv ||
	    source_swidget->id == snd_soc_dapm_output)
		goto err;

	/* sink component */
	sink_swidget = snd_sof_find_swidget(scomp, (char *)route->sink);
	if (!sink_swidget) {
		dev_err(scomp->dev, "error: sink %s not found\n",
			route->sink);
		ret = -EINVAL;
		goto err;
	}

	/*
	 * Don't send routes whose sink widget is of type
	 * output or out_drv to the DSP
	 */
	if (sink_swidget->id == snd_soc_dapm_out_drv ||
	    sink_swidget->id == snd_soc_dapm_output)
		goto err;

	sroute->route = route;
	dobj->private = sroute;
	sroute->src_widget = source_swidget;
	sroute->sink_widget = sink_swidget;

	/* add route to route list */
	list_add(&sroute->list, &sdev->route_list);

	return 0;
err:
	kfree(sroute);
	return ret;
}

/**
 * sof_set_widget_pipeline - Set pipeline for a component
 * @sdev: pointer to struct snd_sof_dev
 * @spipe: pointer to struct snd_sof_pipeline
 * @swidget: pointer to struct snd_sof_widget that has the same pipeline ID as @pipe_widget
 *
 * Return: 0 if successful, -EINVAL on error.
 * The function checks if @swidget is associated with any volatile controls. If so, setting
 * the dynamic_pipeline_widget is disallowed.
 */
static int sof_set_widget_pipeline(struct snd_sof_dev *sdev, struct snd_sof_pipeline *spipe,
				   struct snd_sof_widget *swidget)
{
	struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
	struct snd_sof_control *scontrol;

	if (pipe_widget->dynamic_pipeline_widget) {
		/* dynamic widgets cannot have volatile kcontrols */
		list_for_each_entry(scontrol, &sdev->kcontrol_list, list)
			if (scontrol->comp_id == swidget->comp_id &&
			    (scontrol->access & SNDRV_CTL_ELEM_ACCESS_VOLATILE)) {
				dev_err(sdev->dev,
					"error: volatile control found for dynamic widget %s\n",
					swidget->widget->name);
				return -EINVAL;
			}
	}

	/* set the pipeline and apply the dynamic_pipeline_widget_flag */
	swidget->spipe = spipe;
	swidget->dynamic_pipeline_widget = pipe_widget->dynamic_pipeline_widget;

	return 0;
}

/* completion - called at completion of firmware loading */
static int sof_complete(struct snd_soc_component *scomp)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);
	const struct sof_ipc_tplg_widget_ops *widget_ops;
	struct snd_sof_control *scontrol;
	struct snd_sof_pipeline *spipe;
	int ret;

	widget_ops = tplg_ops ? tplg_ops->widget : NULL;

	/* first update all control IPC structures based on the IPC version */
	if (tplg_ops && tplg_ops->control_setup)
		list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
			ret = tplg_ops->control_setup(sdev, scontrol);
			if (ret < 0) {
				dev_err(sdev->dev, "failed updating IPC struct for control %s\n",
					scontrol->name);
				return ret;
			}
		}

	/* set up the IPC structures for the pipeline widgets */
	list_for_each_entry(spipe, &sdev->pipeline_list, list) {
		struct snd_sof_widget *pipe_widget = spipe->pipe_widget;
		struct snd_sof_widget *swidget;

		pipe_widget->instance_id = -EINVAL;

		/* Update the scheduler widget's IPC structure */
		if (widget_ops && widget_ops[pipe_widget->id].ipc_setup) {
			ret = widget_ops[pipe_widget->id].ipc_setup(pipe_widget);
			if (ret < 0) {
				dev_err(sdev->dev, "failed updating IPC struct for %s\n",
					pipe_widget->widget->name);
				return ret;
			}
		}

		/* set the pipeline and update the IPC structure for the non scheduler widgets */
		list_for_each_entry(swidget, &sdev->widget_list, list)
			if (swidget->widget->id != snd_soc_dapm_scheduler &&
			    swidget->pipeline_id == pipe_widget->pipeline_id) {
				ret = sof_set_widget_pipeline(sdev, spipe, swidget);
				if (ret < 0)
					return ret;

				if (widget_ops && widget_ops[swidget->id].ipc_setup) {
					ret = widget_ops[swidget->id].ipc_setup(swidget);
					if (ret < 0) {
						dev_err(sdev->dev,
							"failed updating IPC struct for %s\n",
							swidget->widget->name);
						return ret;
					}
				}
			}
	}

	/* verify topology components loading including dynamic pipelines */
	if (sof_debug_check_flag(SOF_DBG_VERIFY_TPLG)) {
		if (tplg_ops && tplg_ops->set_up_all_pipelines &&
		    tplg_ops->tear_down_all_pipelines) {
			ret = tplg_ops->set_up_all_pipelines(sdev, true);
			if (ret < 0) {
				dev_err(sdev->dev, "Failed to set up all topology pipelines: %d\n",
					ret);
				return ret;
			}

			ret = tplg_ops->tear_down_all_pipelines(sdev, true);
			if (ret < 0) {
				dev_err(sdev->dev, "Failed to tear down topology pipelines: %d\n",
					ret);
				return ret;
			}
		}
	}

	/* set up static pipelines */
	if (tplg_ops && tplg_ops->set_up_all_pipelines)
		return tplg_ops->set_up_all_pipelines(sdev, false);

	return 0;
}

/* manifest - optional to inform component of manifest */
static int sof_manifest(struct snd_soc_component *scomp, int index,
			struct snd_soc_tplg_manifest *man)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *tplg_ops = sof_ipc_get_ops(sdev, tplg);

	if (tplg_ops && tplg_ops->parse_manifest)
		return tplg_ops->parse_manifest(scomp, index, man);

	return 0;
}

/* vendor specific kcontrol handlers available for binding */
static const struct snd_soc_tplg_kcontrol_ops sof_io_ops[] = {
	{SOF_TPLG_KCTL_VOL_ID, snd_sof_volume_get, snd_sof_volume_put},
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_bytes_get, snd_sof_bytes_put},
	{SOF_TPLG_KCTL_ENUM_ID, snd_sof_enum_get, snd_sof_enum_put},
	{SOF_TPLG_KCTL_SWITCH_ID, snd_sof_switch_get, snd_sof_switch_put},
};

/* vendor specific bytes ext handlers available for binding */
static const struct snd_soc_tplg_bytes_ext_ops sof_bytes_ext_ops[] = {
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_bytes_ext_get, snd_sof_bytes_ext_put},
	{SOF_TPLG_KCTL_BYTES_VOLATILE_RO, snd_sof_bytes_ext_volatile_get},
};

static const struct snd_soc_tplg_ops sof_tplg_ops = {
	/* external kcontrol init - used for any driver specific init */
	.control_load	= sof_control_load,
	.control_unload	= sof_control_unload,

	/* external kcontrol init - used for any driver specific init */
	.dapm_route_load	= sof_route_load,
	.dapm_route_unload	= sof_route_unload,

	/* external widget init - used for any driver specific init */
	/* .widget_load is not currently used */
	.widget_ready	= sof_widget_ready,
	.widget_unload	= sof_widget_unload,

	/* FE DAI - used for any driver specific init */
	.dai_load	= sof_dai_load,
	.dai_unload	= sof_dai_unload,

	/* DAI link - used for any driver specific init */
	.link_load	= sof_link_load,
	.link_unload	= sof_link_unload,

	/* completion - called at completion of firmware loading */
	.complete	= sof_complete,

	/* manifest - optional to inform component of manifest */
	.manifest	= sof_manifest,

	/* vendor specific kcontrol handlers available for binding */
	.io_ops		= sof_io_ops,
	.io_ops_count	= ARRAY_SIZE(sof_io_ops),

	/* vendor specific bytes ext handlers available for binding */
	.bytes_ext_ops	= sof_bytes_ext_ops,
	.bytes_ext_ops_count	= ARRAY_SIZE(sof_bytes_ext_ops),
};

static int snd_sof_dspless_kcontrol(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static const struct snd_soc_tplg_kcontrol_ops sof_dspless_io_ops[] = {
	{SOF_TPLG_KCTL_VOL_ID, snd_sof_dspless_kcontrol, snd_sof_dspless_kcontrol},
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_dspless_kcontrol, snd_sof_dspless_kcontrol},
	{SOF_TPLG_KCTL_ENUM_ID, snd_sof_dspless_kcontrol, snd_sof_dspless_kcontrol},
	{SOF_TPLG_KCTL_SWITCH_ID, snd_sof_dspless_kcontrol, snd_sof_dspless_kcontrol},
};

static int snd_sof_dspless_bytes_ext_get(struct snd_kcontrol *kcontrol,
					 unsigned int __user *binary_data,
					 unsigned int size)
{
	return 0;
}

static int snd_sof_dspless_bytes_ext_put(struct snd_kcontrol *kcontrol,
					 const unsigned int __user *binary_data,
					 unsigned int size)
{
	return 0;
}

static const struct snd_soc_tplg_bytes_ext_ops sof_dspless_bytes_ext_ops[] = {
	{SOF_TPLG_KCTL_BYTES_ID, snd_sof_dspless_bytes_ext_get, snd_sof_dspless_bytes_ext_put},
	{SOF_TPLG_KCTL_BYTES_VOLATILE_RO, snd_sof_dspless_bytes_ext_get},
};

/* external widget init - used for any driver specific init */
static int sof_dspless_widget_ready(struct snd_soc_component *scomp, int index,
				    struct snd_soc_dapm_widget *w,
				    struct snd_soc_tplg_dapm_widget *tw)
{
	if (WIDGET_IS_DAI(w->id)) {
		static const struct sof_topology_token dai_tokens[] = {
			{SOF_TKN_DAI_TYPE, SND_SOC_TPLG_TUPLE_TYPE_STRING, get_token_dai_type, 0}};
		struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
		struct snd_soc_tplg_private *priv = &tw->priv;
		struct snd_sof_widget *swidget;
		struct snd_sof_dai *sdai;
		int ret;

		swidget = kzalloc(sizeof(*swidget), GFP_KERNEL);
		if (!swidget)
			return -ENOMEM;

		sdai = kzalloc(sizeof(*sdai), GFP_KERNEL);
		if (!sdai) {
			kfree(swidget);
			return -ENOMEM;
		}

		ret = sof_parse_tokens(scomp, &sdai->type, dai_tokens, ARRAY_SIZE(dai_tokens),
				       priv->array, le32_to_cpu(priv->size));
		if (ret < 0) {
			dev_err(scomp->dev, "Failed to parse DAI tokens for %s\n", tw->name);
			kfree(swidget);
			kfree(sdai);
			return ret;
		}

		ret = sof_connect_dai_widget(scomp, w, tw, sdai);
		if (ret) {
			kfree(swidget);
			kfree(sdai);
			return ret;
		}

		swidget->scomp = scomp;
		swidget->widget = w;
		swidget->private = sdai;
		mutex_init(&swidget->setup_mutex);
		w->dobj.private = swidget;
		list_add(&swidget->list, &sdev->widget_list);
	}

	return 0;
}

static int sof_dspless_widget_unload(struct snd_soc_component *scomp,
				     struct snd_soc_dobj *dobj)
{
	struct snd_soc_dapm_widget *w = container_of(dobj, struct snd_soc_dapm_widget, dobj);

	if (WIDGET_IS_DAI(w->id)) {
		struct snd_sof_widget *swidget = dobj->private;

		sof_disconnect_dai_widget(scomp, w);

		if (!swidget)
			return 0;

		/* remove and free swidget object */
		list_del(&swidget->list);
		kfree(swidget->private);
		kfree(swidget);
	}

	return 0;
}

static int sof_dspless_link_load(struct snd_soc_component *scomp, int index,
				 struct snd_soc_dai_link *link,
				 struct snd_soc_tplg_link_config *cfg)
{
	link->platforms->name = dev_name(scomp->dev);

	/* Set nonatomic property for FE dai links for FE-BE compatibility */
	if (!link->no_pcm)
		link->nonatomic = true;

	return 0;
}

static const struct snd_soc_tplg_ops sof_dspless_tplg_ops = {
	/* external widget init - used for any driver specific init */
	.widget_ready	= sof_dspless_widget_ready,
	.widget_unload	= sof_dspless_widget_unload,

	/* FE DAI - used for any driver specific init */
	.dai_load	= sof_dai_load,
	.dai_unload	= sof_dai_unload,

	/* DAI link - used for any driver specific init */
	.link_load	= sof_dspless_link_load,

	/* vendor specific kcontrol handlers available for binding */
	.io_ops		= sof_dspless_io_ops,
	.io_ops_count	= ARRAY_SIZE(sof_dspless_io_ops),

	/* vendor specific bytes ext handlers available for binding */
	.bytes_ext_ops = sof_dspless_bytes_ext_ops,
	.bytes_ext_ops_count = ARRAY_SIZE(sof_dspless_bytes_ext_ops),
};

int snd_sof_load_topology(struct snd_soc_component *scomp, const char *file)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct firmware *fw;
	int ret;

	dev_dbg(scomp->dev, "loading topology:%s\n", file);

	ret = request_firmware(&fw, file, scomp->dev);
	if (ret < 0) {
		dev_err(scomp->dev, "error: tplg request firmware %s failed err: %d\n",
			file, ret);
		dev_err(scomp->dev,
			"you may need to download the firmware from https://github.com/thesofproject/sof-bin/\n");
		return ret;
	}

	if (sdev->dspless_mode_selected)
		ret = snd_soc_tplg_component_load(scomp, &sof_dspless_tplg_ops, fw);
	else
		ret = snd_soc_tplg_component_load(scomp, &sof_tplg_ops, fw);

	if (ret < 0) {
		dev_err(scomp->dev, "error: tplg component load failed %d\n",
			ret);
		ret = -EINVAL;
	}

	release_firmware(fw);

	if (ret >= 0 && sdev->led_present)
		ret = snd_ctl_led_request();

	return ret;
}
EXPORT_SYMBOL(snd_sof_load_topology);
