// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
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
#define TLV_ITEMS	3
#define TLV_MIN		0
#define TLV_STEP	1
#define TLV_MUTE	2

/* size of tplg abi in byte */
#define SOF_TPLG_ABI_SIZE 3

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
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_token_info *token_list = ipc_tplg_ops->token_list;
	const struct sof_topology_token *tokens;
	int i, j;

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

static inline int get_tlv_data(const int *p, int tlv[TLV_ITEMS])
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
static u32 vol_compute_gain(u32 value, int *tlv)
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
			       int tlv[TLV_ITEMS], int size)
{
	int j;

	/* init the volume table */
	scontrol->volume_table = kcalloc(size, sizeof(u32), GFP_KERNEL);
	if (!scontrol->volume_table)
		return -ENOMEM;

	/* populate the volume table */
	for (j = 0; j < size ; j++)
		scontrol->volume_table[j] = vol_compute_gain(j, tlv);

	return 0;
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
	{"ACP", SOF_DAI_AMD_BT},
	{"ACPSP", SOF_DAI_AMD_SP},
	{"ACPDMIC", SOF_DAI_AMD_DMIC},
	{"AFE", SOF_DAI_MEDIATEK_AFE},
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
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_token_info *token_list = ipc_tplg_ops->token_list;
	const struct sof_topology_token *tokens;
	int found = 0;
	int num_tokens, asize;
	int i, j;

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
	int i, j;

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
			tokens[j].get_token(elem->string, object, offset + tokens[j].offset);

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
			found += sof_parse_string_tokens(scomp, object, offset, tokens, count,
							 array);
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
	int tlv[TLV_ITEMS];
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
	if (!scontrol->name)
		return -ENOMEM;

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
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	struct snd_sof_control *scontrol = dobj->private;
	int ret = 0;

	dev_dbg(scomp->dev, "tplg: unload control name : %s\n", scontrol->name);

	if (ipc_tplg_ops->control_free) {
		ret = ipc_tplg_ops->control_free(sdev, scontrol);
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
	int i;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		dev_vdbg(scomp->dev, "tplg: check widget: %s stream: %s dai stream: %s\n",
			 w->name,  w->sname, rtd->dai_link->stream_name);

		if (!w->sname || !rtd->dai_link->stream_name)
			continue;

		/* does stream match DAI link ? */
		if (strcmp(w->sname, rtd->dai_link->stream_name))
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_out:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				/*
				 * Please create DAI widget in the right order
				 * to ensure BE will connect to the right DAI
				 * widget.
				 */
				if (!cpu_dai->capture_widget) {
					cpu_dai->capture_widget = w;
					break;
				}
			}
			if (i == rtd->num_cpus) {
				dev_err(scomp->dev, "error: can't find BE for DAI %s\n",
					w->name);

				return -EINVAL;
			}
			dai->name = rtd->dai_link->name;
			dev_dbg(scomp->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		case snd_soc_dapm_dai_in:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				/*
				 * Please create DAI widget in the right order
				 * to ensure BE will connect to the right DAI
				 * widget.
				 */
				if (!cpu_dai->playback_widget) {
					cpu_dai->playback_widget = w;
					break;
				}
			}
			if (i == rtd->num_cpus) {
				dev_err(scomp->dev, "error: can't find BE for DAI %s\n",
					w->name);

				return -EINVAL;
			}
			dai->name = rtd->dai_link->name;
			dev_dbg(scomp->dev, "tplg: connected widget %s -> DAI link %s\n",
				w->name, rtd->dai_link->name);
			break;
		default:
			break;
		}
	}

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
	struct snd_soc_dai *cpu_dai;
	int i;

	if (!w->sname)
		return;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		/* does stream match DAI link ? */
		if (!rtd->dai_link->stream_name ||
		    strcmp(w->sname, rtd->dai_link->stream_name))
			continue;

		switch (w->id) {
		case snd_soc_dapm_dai_out:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				if (cpu_dai->capture_widget == w) {
					cpu_dai->capture_widget = NULL;
					break;
				}
			}
			break;
		case snd_soc_dapm_dai_in:
			for_each_rtd_cpu_dais(rtd, i, cpu_dai) {
				if (cpu_dai->playback_widget == w) {
					cpu_dai->playback_widget = NULL;
					break;
				}
			}
			break;
		default:
			break;
		}
	}
}

/* bind PCM ID to host component ID */
static int spcm_bind(struct snd_soc_component *scomp, struct snd_sof_pcm *spcm,
		     int dir)
{
	struct snd_sof_widget *host_widget;

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

static int sof_widget_parse_tokens(struct snd_soc_component *scomp, struct snd_sof_widget *swidget,
				   struct snd_soc_tplg_dapm_widget *tw,
				   enum sof_tokens *object_token_list, int count)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_token_info *token_list = ipc_tplg_ops->token_list;
	struct snd_soc_tplg_private *private = &tw->priv;
	int num_tuples = 0;
	size_t size;
	int ret, i;

	if (count > 0 && !object_token_list) {
		dev_err(scomp->dev, "No token list for widget %s\n", swidget->widget->name);
		return -EINVAL;
	}

	/* calculate max size of tuples array */
	for (i = 0; i < count; i++)
		num_tuples += token_list[object_token_list[i]].count;

	/* allocate memory for tuples array */
	size = sizeof(struct snd_sof_tuple) * num_tuples;
	swidget->tuples = kzalloc(size, GFP_KERNEL);
	if (!swidget->tuples)
		return -ENOMEM;

	/* parse token list for widget */
	for (i = 0; i < count; i++) {
		if (object_token_list[i] >= SOF_TOKEN_COUNT) {
			dev_err(scomp->dev, "Invalid token id %d for widget %s\n",
				object_token_list[i], swidget->widget->name);
			ret = -EINVAL;
			goto err;
		}

		/* parse and save UUID in swidget */
		if (object_token_list[i] == SOF_COMP_EXT_TOKENS) {
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
		}

		/* copy one set of tuples per token ID into swidget->tuples */
		ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
				      object_token_list[i], 1, swidget->tuples,
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

/* external widget init - used for any driver specific init */
static int sof_widget_ready(struct snd_soc_component *scomp, int index,
			    struct snd_soc_dapm_widget *w,
			    struct snd_soc_tplg_dapm_widget *tw)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_ipc_tplg_widget_ops *widget_ops = ipc_tplg_ops->widget;
	struct snd_sof_widget *swidget;
	struct snd_sof_dai *dai;
	enum sof_tokens *token_list;
	int token_list_size;
	int ret = 0;

	swidget = kzalloc(sizeof(*swidget), GFP_KERNEL);
	if (!swidget)
		return -ENOMEM;

	swidget->scomp = scomp;
	swidget->widget = w;
	swidget->comp_id = sdev->next_comp_id++;
	swidget->complete = 0;
	swidget->id = w->id;
	swidget->pipeline_id = index;
	swidget->private = NULL;

	dev_dbg(scomp->dev, "tplg: ready widget id %d pipe %d type %d name : %s stream %s\n",
		swidget->comp_id, index, swidget->id, tw->name,
		strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
			? tw->sname : "none");

	token_list = widget_ops[w->id].token_list;
	token_list_size = widget_ops[w->id].token_list_size;

	/* handle any special case widgets */
	switch (w->id) {
	case snd_soc_dapm_dai_in:
	case snd_soc_dapm_dai_out:
		dai = kzalloc(sizeof(*dai), GFP_KERNEL);
		if (!dai) {
			kfree(swidget);
			return -ENOMEM;

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

	if (sof_debug_check_flag(SOF_DBG_DISABLE_MULTICORE)) {
		swidget->core = SOF_DSP_PRIMARY_CORE;
	} else {
		int core = sof_get_token_value(SOF_TKN_COMP_CORE_ID, swidget->tuples,
					       swidget->num_tuples);

		if (core >= 0)
			swidget->core = core;
	}

	/* check token parsing reply */
	if (ret < 0) {
		dev_err(scomp->dev,
			"error: failed to add widget id %d type %d name : %s stream %s\n",
			tw->shift, swidget->id, tw->name,
			strnlen(tw->sname, SNDRV_CTL_ELEM_ID_NAME_MAXLEN) > 0
				? tw->sname : "none");
		kfree(swidget);
		return ret;
	}

	/* bind widget to external event */
	if (tw->event_type) {
		if (widget_ops[w->id].bind_event) {
			ret = widget_ops[w->id].bind_event(scomp, swidget,
							   le16_to_cpu(tw->event_type));
			if (ret) {
				dev_err(scomp->dev, "widget event binding failed for %s\n",
					swidget->widget->name);
				kfree(swidget->private);
				kfree(swidget->tuples);
				kfree(swidget);
				return ret;
			}
		}
	}

	w->dobj.private = swidget;
	list_add(&swidget->list, &sdev->widget_list);
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
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_ipc_tplg_widget_ops *widget_ops = ipc_tplg_ops->widget;
	const struct snd_kcontrol_new *kc;
	struct snd_soc_dapm_widget *widget;
	struct snd_sof_control *scontrol;
	struct snd_sof_widget *swidget;
	struct soc_mixer_control *sm;
	struct soc_bytes_ext *sbe;
	struct snd_sof_dai *dai;
	struct soc_enum *se;
	int ret = 0;
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
	if (widget_ops[swidget->id].ipc_free)
		widget_ops[swidget->id].ipc_free(swidget);

	kfree(swidget->tuples);

	/* remove and free swidget object */
	list_del(&swidget->list);
	kfree(swidget);

	return ret;
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

	dev_vdbg(scomp->dev, "tplg: pcm %s stream tokens: playback d0i3:%d\n",
		 spcm->pcm.pcm_name, spcm->stream[stream].d0i3_compatible);

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

	dev_vdbg(scomp->dev, "tplg: pcm %s stream tokens: capture d0i3:%d\n",
		 spcm->pcm.pcm_name, spcm->stream[stream].d0i3_compatible);

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
	struct snd_sof_pcm *spcm = dobj->private;

	/* free PCM DMA pages */
	if (spcm->pcm.playback)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].page_table);

	if (spcm->pcm.capture)
		snd_dma_free_pages(&spcm->stream[SNDRV_PCM_STREAM_CAPTURE].page_table);

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
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_token_info *token_list = ipc_tplg_ops->token_list;
	struct snd_soc_tplg_private *private = &cfg->priv;
	struct snd_sof_dai_link *slink;
	size_t size;
	u32 token_id = 0;
	int num_tuples = 0;
	int ret, num_sets;

	if (!link->platforms) {
		dev_err(scomp->dev, "error: no platforms\n");
		return -EINVAL;
	}
	link->platforms->name = dev_name(scomp->dev);

	/*
	 * Set nonatomic property for FE dai links as their trigger action
	 * involves IPC's.
	 */
	if (!link->no_pcm) {
		link->nonatomic = true;

		/*
		 * set default trigger order for all links. Exceptions to
		 * the rule will be handled in sof_pcm_dai_link_fixup()
		 * For playback, the sequence is the following: start FE,
		 * start BE, stop BE, stop FE; for Capture the sequence is
		 * inverted start BE, start FE, stop FE, stop BE
		 */
		link->trigger[SNDRV_PCM_STREAM_PLAYBACK] =
					SND_SOC_DPCM_TRIGGER_PRE;
		link->trigger[SNDRV_PCM_STREAM_CAPTURE] =
					SND_SOC_DPCM_TRIGGER_POST;

		/* nothing more to do for FE dai links */
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
	slink->hw_configs = kmemdup(cfg->hw_config,
				    sizeof(*slink->hw_configs) * slink->num_hw_configs,
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
	default:
		break;
	}

	/* allocate memory for tuples array */
	size = sizeof(struct snd_sof_tuple) * num_tuples;
	slink->tuples = kzalloc(size, GFP_KERNEL);
	if (!slink->tuples) {
		kfree(slink->hw_configs);
		kfree(slink);
		return -ENOMEM;
	}

	/* parse one set of DAI link tokens */
	ret = sof_copy_tuples(sdev, private->array, le32_to_cpu(private->size),
			      SOF_DAI_LINK_TOKENS, 1, slink->tuples,
			      num_tuples, &slink->num_tuples);
	if (ret < 0) {
		dev_err(scomp->dev, "failed to parse %s for dai link %s\n",
			token_list[SOF_DAI_LINK_TOKENS].name, link->name);
		goto err;
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
 * sof_set_pipe_widget - Set pipe_widget for a component
 * @sdev: pointer to struct snd_sof_dev
 * @pipe_widget: pointer to struct snd_sof_widget of type snd_soc_dapm_scheduler
 * @swidget: pointer to struct snd_sof_widget that has the same pipeline ID as @pipe_widget
 *
 * Return: 0 if successful, -EINVAL on error.
 * The function checks if @swidget is associated with any volatile controls. If so, setting
 * the dynamic_pipeline_widget is disallowed.
 */
static int sof_set_pipe_widget(struct snd_sof_dev *sdev, struct snd_sof_widget *pipe_widget,
			       struct snd_sof_widget *swidget)
{
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

	/* set the pipe_widget and apply the dynamic_pipeline_widget_flag */
	swidget->pipe_widget = pipe_widget;
	swidget->dynamic_pipeline_widget = pipe_widget->dynamic_pipeline_widget;

	return 0;
}

/* completion - called at completion of firmware loading */
static int sof_complete(struct snd_soc_component *scomp)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_widget *swidget, *comp_swidget;
	const struct sof_ipc_tplg_ops *ipc_tplg_ops = sdev->ipc->ops->tplg;
	const struct sof_ipc_tplg_widget_ops *widget_ops = ipc_tplg_ops->widget;
	struct snd_sof_control *scontrol;
	int ret;

	/* first update all control IPC structures based on the IPC version */
	if (ipc_tplg_ops->control_setup)
		list_for_each_entry(scontrol, &sdev->kcontrol_list, list) {
			ret = ipc_tplg_ops->control_setup(sdev, scontrol);
			if (ret < 0) {
				dev_err(sdev->dev, "failed updating IPC struct for control %s\n",
					scontrol->name);
				return ret;
			}
		}

	/*
	 * then update all widget IPC structures. If any of the ipc_setup callbacks fail, the
	 * topology will be removed and all widgets will be unloaded resulting in freeing all
	 * associated memories.
	 */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (widget_ops[swidget->id].ipc_setup) {
			ret = widget_ops[swidget->id].ipc_setup(swidget);
			if (ret < 0) {
				dev_err(sdev->dev, "failed updating IPC struct for %s\n",
					swidget->widget->name);
				return ret;
			}
		}
	}

	/* set the pipe_widget and apply the dynamic_pipeline_widget_flag */
	list_for_each_entry(swidget, &sdev->widget_list, list) {
		switch (swidget->id) {
		case snd_soc_dapm_scheduler:
			/*
			 * Apply the dynamic_pipeline_widget flag and set the pipe_widget field
			 * for all widgets that have the same pipeline ID as the scheduler widget
			 */
			list_for_each_entry(comp_swidget, &sdev->widget_list, list)
				if (comp_swidget->pipeline_id == swidget->pipeline_id) {
					ret = sof_set_pipe_widget(sdev, swidget, comp_swidget);
					if (ret < 0)
						return ret;
				}
			break;
		default:
			break;
		}
	}

	/* verify topology components loading including dynamic pipelines */
	if (sof_debug_check_flag(SOF_DBG_VERIFY_TPLG)) {
		if (ipc_tplg_ops->set_up_all_pipelines && ipc_tplg_ops->tear_down_all_pipelines) {
			ret = ipc_tplg_ops->set_up_all_pipelines(sdev, true);
			if (ret < 0) {
				dev_err(sdev->dev, "Failed to set up all topology pipelines: %d\n",
					ret);
				return ret;
			}

			ret = ipc_tplg_ops->tear_down_all_pipelines(sdev, true);
			if (ret < 0) {
				dev_err(sdev->dev, "Failed to tear down topology pipelines: %d\n",
					ret);
				return ret;
			}
		}
	}

	/* set up static pipelines */
	if (ipc_tplg_ops->set_up_all_pipelines)
		return ipc_tplg_ops->set_up_all_pipelines(sdev, false);

	return 0;
}

/* manifest - optional to inform component of manifest */
static int sof_manifest(struct snd_soc_component *scomp, int index,
			struct snd_soc_tplg_manifest *man)
{
	u32 size;
	u32 abi_version;

	size = le32_to_cpu(man->priv.size);

	/* backward compatible with tplg without ABI info */
	if (!size) {
		dev_dbg(scomp->dev, "No topology ABI info\n");
		return 0;
	}

	if (size != SOF_TPLG_ABI_SIZE) {
		dev_err(scomp->dev, "error: invalid topology ABI size\n");
		return -EINVAL;
	}

	dev_info(scomp->dev,
		 "Topology: ABI %d:%d:%d Kernel ABI %d:%d:%d\n",
		 man->priv.data[0], man->priv.data[1],
		 man->priv.data[2], SOF_ABI_MAJOR, SOF_ABI_MINOR,
		 SOF_ABI_PATCH);

	abi_version = SOF_ABI_VER(man->priv.data[0],
				  man->priv.data[1],
				  man->priv.data[2]);

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, abi_version)) {
		dev_err(scomp->dev, "error: incompatible topology ABI version\n");
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_MINOR(abi_version) > SOF_ABI_MINOR) {
		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_STRICT_ABI_CHECKS)) {
			dev_warn(scomp->dev, "warn: topology ABI is more recent than kernel\n");
		} else {
			dev_err(scomp->dev, "error: topology ABI is more recent than kernel\n");
			return -EINVAL;
		}
	}

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

static struct snd_soc_tplg_ops sof_tplg_ops = {
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

int snd_sof_load_topology(struct snd_soc_component *scomp, const char *file)
{
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

	ret = snd_soc_tplg_component_load(scomp, &sof_tplg_ops, fw);
	if (ret < 0) {
		dev_err(scomp->dev, "error: tplg component load failed %d\n",
			ret);
		ret = -EINVAL;
	}

	release_firmware(fw);
	return ret;
}
EXPORT_SYMBOL(snd_sof_load_topology);
