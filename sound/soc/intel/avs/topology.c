// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright(c) 2021 Intel Corporation. All rights reserved.
//
// Authors: Cezary Rojewski <cezary.rojewski@intel.com>
//          Amadeusz Slawinski <amadeuszx.slawinski@linux.intel.com>
//

#include <linux/uuid.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/soc-topology.h>
#include <uapi/sound/intel/avs/tokens.h>
#include "avs.h"
#include "topology.h"

/* Get pointer to vendor array at the specified offset. */
#define avs_tplg_vendor_array_at(array, offset) \
	((struct snd_soc_tplg_vendor_array *)((u8 *)array + offset))

/* Get pointer to vendor array that is next in line. */
#define avs_tplg_vendor_array_next(array) \
	(avs_tplg_vendor_array_at(array, le32_to_cpu((array)->size)))

/*
 * Scan provided block of tuples for the specified token. If found,
 * @offset is updated with position at which first matching token is
 * located.
 *
 * Returns 0 on success, -ENOENT if not found and error code otherwise.
 */
static int
avs_tplg_vendor_array_lookup(struct snd_soc_tplg_vendor_array *tuples,
			     u32 block_size, u32 token, u32 *offset)
{
	u32 pos = 0;

	while (block_size > 0) {
		struct snd_soc_tplg_vendor_value_elem *tuple;
		u32 tuples_size = le32_to_cpu(tuples->size);

		if (tuples_size > block_size)
			return -EINVAL;

		tuple = tuples->value;
		if (le32_to_cpu(tuple->token) == token) {
			*offset = pos;
			return 0;
		}

		block_size -= tuples_size;
		pos += tuples_size;
		tuples = avs_tplg_vendor_array_next(tuples);
	}

	return -ENOENT;
}

/*
 * See avs_tplg_vendor_array_lookup() for description.
 *
 * Behaves exactly like avs_tplg_vendor_lookup() but starts from the
 * next vendor array in line. Useful when searching for the finish line
 * of an arbitrary entry in a list of entries where each is composed of
 * several vendor tuples and a specific token marks the beginning of
 * a new entry block.
 */
static int
avs_tplg_vendor_array_lookup_next(struct snd_soc_tplg_vendor_array *tuples,
				  u32 block_size, u32 token, u32 *offset)
{
	u32 tuples_size = le32_to_cpu(tuples->size);
	int ret;

	if (tuples_size > block_size)
		return -EINVAL;

	tuples = avs_tplg_vendor_array_next(tuples);
	block_size -= tuples_size;

	ret = avs_tplg_vendor_array_lookup(tuples, block_size, token, offset);
	if (!ret)
		*offset += tuples_size;
	return ret;
}

/*
 * Scan provided block of tuples for the specified token which marks
 * the border of an entry block. Behavior is similar to
 * avs_tplg_vendor_array_lookup() except 0 is also returned if no
 * matching token has been found. In such case, returned @size is
 * assigned to @block_size as the entire block belongs to the current
 * entry.
 *
 * Returns 0 on success, error code otherwise.
 */
static int
avs_tplg_vendor_entry_size(struct snd_soc_tplg_vendor_array *tuples,
			   u32 block_size, u32 entry_id_token, u32 *size)
{
	int ret;

	ret = avs_tplg_vendor_array_lookup_next(tuples, block_size, entry_id_token, size);
	if (ret == -ENOENT) {
		*size = block_size;
		ret = 0;
	}

	return ret;
}

/*
 * Vendor tuple parsing descriptor.
 *
 * @token: vendor specific token that identifies tuple
 * @type: tuple type, one of SND_SOC_TPLG_TUPLE_TYPE_XXX
 * @offset: offset of a struct's field to initialize
 * @parse: parsing function, extracts and assigns value to object's field
 */
struct avs_tplg_token_parser {
	enum avs_tplg_token token;
	u32 type;
	u32 offset;
	int (*parse)(struct snd_soc_component *comp, void *elem, void *object, u32 offset);
};

static int
avs_parse_uuid_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;
	guid_t *val = (guid_t *)((u8 *)object + offset);

	guid_copy((guid_t *)val, (const guid_t *)&tuple->value);

	return 0;
}

static int
avs_parse_bool_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;
	bool *val = (bool *)((u8 *)object + offset);

	*val = le32_to_cpu(tuple->value);

	return 0;
}

static int
avs_parse_byte_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;
	u8 *val = ((u8 *)object + offset);

	*val = le32_to_cpu(tuple->value);

	return 0;
}

static int
avs_parse_short_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;
	u16 *val = (u16 *)((u8 *)object + offset);

	*val = le32_to_cpu(tuple->value);

	return 0;
}

static int
avs_parse_word_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;
	u32 *val = (u32 *)((u8 *)object + offset);

	*val = le32_to_cpu(tuple->value);

	return 0;
}

static int
avs_parse_string_token(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_string_elem *tuple = elem;
	char *val = (char *)((u8 *)object + offset);

	snprintf(val, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, "%s", tuple->string);

	return 0;
}

static int avs_parse_uuid_tokens(struct snd_soc_component *comp, void *object,
				 const struct avs_tplg_token_parser *parsers, int count,
				 struct snd_soc_tplg_vendor_array *tuples)
{
	struct snd_soc_tplg_vendor_uuid_elem *tuple;
	int ret, i, j;

	/* Parse element by element. */
	for (i = 0; i < le32_to_cpu(tuples->num_elems); i++) {
		tuple = &tuples->uuid[i];

		for (j = 0; j < count; j++) {
			/* Ignore non-UUID tokens. */
			if (parsers[j].type != SND_SOC_TPLG_TUPLE_TYPE_UUID ||
			    parsers[j].token != le32_to_cpu(tuple->token))
				continue;

			ret = parsers[j].parse(comp, tuple, object, parsers[j].offset);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int avs_parse_string_tokens(struct snd_soc_component *comp, void *object,
				   const struct avs_tplg_token_parser *parsers, int count,
				   struct snd_soc_tplg_vendor_array *tuples)
{
	struct snd_soc_tplg_vendor_string_elem *tuple;
	int ret, i, j;

	/* Parse element by element. */
	for (i = 0; i < le32_to_cpu(tuples->num_elems); i++) {
		tuple = &tuples->string[i];

		for (j = 0; j < count; j++) {
			/* Ignore non-string tokens. */
			if (parsers[j].type != SND_SOC_TPLG_TUPLE_TYPE_STRING ||
			    parsers[j].token != le32_to_cpu(tuple->token))
				continue;

			ret = parsers[j].parse(comp, tuple, object, parsers[j].offset);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int avs_parse_word_tokens(struct snd_soc_component *comp, void *object,
				 const struct avs_tplg_token_parser *parsers, int count,
				 struct snd_soc_tplg_vendor_array *tuples)
{
	struct snd_soc_tplg_vendor_value_elem *tuple;
	int ret, i, j;

	/* Parse element by element. */
	for (i = 0; i < le32_to_cpu(tuples->num_elems); i++) {
		tuple = &tuples->value[i];

		for (j = 0; j < count; j++) {
			/* Ignore non-integer tokens. */
			if (!(parsers[j].type == SND_SOC_TPLG_TUPLE_TYPE_WORD ||
			      parsers[j].type == SND_SOC_TPLG_TUPLE_TYPE_SHORT ||
			      parsers[j].type == SND_SOC_TPLG_TUPLE_TYPE_BYTE ||
			      parsers[j].type == SND_SOC_TPLG_TUPLE_TYPE_BOOL))
				continue;

			if (parsers[j].token != le32_to_cpu(tuple->token))
				continue;

			ret = parsers[j].parse(comp, tuple, object, parsers[j].offset);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int avs_parse_tokens(struct snd_soc_component *comp, void *object,
			    const struct avs_tplg_token_parser *parsers, size_t count,
			    struct snd_soc_tplg_vendor_array *tuples, int priv_size)
{
	int array_size, ret;

	while (priv_size > 0) {
		array_size = le32_to_cpu(tuples->size);

		if (array_size <= 0) {
			dev_err(comp->dev, "invalid array size 0x%x\n", array_size);
			return -EINVAL;
		}

		/* Make sure there is enough data before parsing. */
		priv_size -= array_size;
		if (priv_size < 0) {
			dev_err(comp->dev, "invalid array size 0x%x\n", array_size);
			return -EINVAL;
		}

		switch (le32_to_cpu(tuples->type)) {
		case SND_SOC_TPLG_TUPLE_TYPE_UUID:
			ret = avs_parse_uuid_tokens(comp, object, parsers, count, tuples);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_STRING:
			ret = avs_parse_string_tokens(comp, object, parsers, count, tuples);
			break;
		case SND_SOC_TPLG_TUPLE_TYPE_BOOL:
		case SND_SOC_TPLG_TUPLE_TYPE_BYTE:
		case SND_SOC_TPLG_TUPLE_TYPE_SHORT:
		case SND_SOC_TPLG_TUPLE_TYPE_WORD:
			ret = avs_parse_word_tokens(comp, object, parsers, count, tuples);
			break;
		default:
			dev_err(comp->dev, "unknown token type %d\n", tuples->type);
			ret = -EINVAL;
		}

		if (ret) {
			dev_err(comp->dev, "parsing %zu tokens of %d type failed: %d\n",
				count, tuples->type, ret);
			return ret;
		}

		tuples = avs_tplg_vendor_array_next(tuples);
	}

	return 0;
}

#define AVS_DEFINE_PTR_PARSER(name, type, member) \
static int \
avs_parse_##name##_ptr(struct snd_soc_component *comp, void *elem, void *object, u32 offset) \
{ \
	struct snd_soc_tplg_vendor_value_elem *tuple = elem;		\
	struct avs_soc_component *acomp = to_avs_soc_component(comp);	\
	type **val = (type **)(object + offset);			\
	u32 idx;							\
									\
	idx = le32_to_cpu(tuple->value);				\
	if (idx >= acomp->tplg->num_##member)				\
		return -EINVAL;						\
									\
	*val = &acomp->tplg->member[idx];				\
									\
	return 0;							\
}

AVS_DEFINE_PTR_PARSER(audio_format, struct avs_audio_format, fmts);
AVS_DEFINE_PTR_PARSER(modcfg_base, struct avs_tplg_modcfg_base, modcfgs_base);
AVS_DEFINE_PTR_PARSER(modcfg_ext, struct avs_tplg_modcfg_ext, modcfgs_ext);
AVS_DEFINE_PTR_PARSER(pplcfg, struct avs_tplg_pplcfg, pplcfgs);
AVS_DEFINE_PTR_PARSER(binding, struct avs_tplg_binding, bindings);

static int
parse_audio_format_bitfield(struct snd_soc_component *comp, void *elem, void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_value_elem *velem = elem;
	struct avs_audio_format *audio_format = object;

	switch (offset) {
	case AVS_TKN_AFMT_NUM_CHANNELS_U32:
		audio_format->num_channels = le32_to_cpu(velem->value);
		break;
	case AVS_TKN_AFMT_VALID_BIT_DEPTH_U32:
		audio_format->valid_bit_depth = le32_to_cpu(velem->value);
		break;
	case AVS_TKN_AFMT_SAMPLE_TYPE_U32:
		audio_format->sample_type = le32_to_cpu(velem->value);
		break;
	}

	return 0;
}

static int parse_link_formatted_string(struct snd_soc_component *comp, void *elem,
				       void *object, u32 offset)
{
	struct snd_soc_tplg_vendor_string_elem *tuple = elem;
	struct snd_soc_acpi_mach *mach = dev_get_platdata(comp->card->dev);
	char *val = (char *)((u8 *)object + offset);

	/*
	 * Dynamic naming - string formats, e.g.: ssp%d - supported only for
	 * topologies describing single device e.g.: an I2S codec on SSP0.
	 */
	if (hweight_long(mach->link_mask) != 1)
		return avs_parse_string_token(comp, elem, object, offset);

	snprintf(val, SNDRV_CTL_ELEM_ID_NAME_MAXLEN, tuple->string,
		 __ffs(mach->link_mask));

	return 0;
}

static int
parse_dictionary_header(struct snd_soc_component *comp,
			struct snd_soc_tplg_vendor_array *tuples,
			void **dict, u32 *num_entries, size_t entry_size,
			u32 num_entries_token)
{
	struct snd_soc_tplg_vendor_value_elem *tuple;

	/* Dictionary header consists of single tuple - entry count. */
	tuple = tuples->value;
	if (le32_to_cpu(tuple->token) != num_entries_token) {
		dev_err(comp->dev, "invalid dictionary header, expected: %d\n",
			num_entries_token);
		return -EINVAL;
	}

	*num_entries = le32_to_cpu(tuple->value);
	*dict = devm_kcalloc(comp->card->dev, *num_entries, entry_size, GFP_KERNEL);
	if (!*dict)
		return -ENOMEM;

	return 0;
}

static int
parse_dictionary_entries(struct snd_soc_component *comp,
			 struct snd_soc_tplg_vendor_array *tuples, u32 block_size,
			 void *dict, u32 num_entries, size_t entry_size,
			 u32 entry_id_token,
			 const struct avs_tplg_token_parser *parsers, size_t num_parsers)
{
	void *pos = dict;
	int i;

	for (i = 0; i < num_entries; i++) {
		u32 esize;
		int ret;

		ret = avs_tplg_vendor_entry_size(tuples, block_size,
						 entry_id_token, &esize);
		if (ret)
			return ret;

		ret = avs_parse_tokens(comp, pos, parsers, num_parsers, tuples, esize);
		if (ret < 0) {
			dev_err(comp->dev, "parse entry: %d of type: %d failed: %d\n",
				i, entry_id_token, ret);
			return ret;
		}

		pos += entry_size;
		block_size -= esize;
		tuples = avs_tplg_vendor_array_at(tuples, esize);
	}

	return 0;
}

static int parse_dictionary(struct snd_soc_component *comp,
			    struct snd_soc_tplg_vendor_array *tuples, u32 block_size,
			    void **dict, u32 *num_entries, size_t entry_size,
			    u32 num_entries_token, u32 entry_id_token,
			    const struct avs_tplg_token_parser *parsers, size_t num_parsers)
{
	int ret;

	ret = parse_dictionary_header(comp, tuples, dict, num_entries,
				      entry_size, num_entries_token);
	if (ret)
		return ret;

	block_size -= le32_to_cpu(tuples->size);
	/* With header parsed, move on to parsing entries. */
	tuples = avs_tplg_vendor_array_next(tuples);

	return parse_dictionary_entries(comp, tuples, block_size, *dict,
					*num_entries, entry_size,
					entry_id_token, parsers, num_parsers);
}

static const struct avs_tplg_token_parser library_parsers[] = {
	{
		.token = AVS_TKN_LIBRARY_NAME_STRING,
		.type = SND_SOC_TPLG_TUPLE_TYPE_STRING,
		.offset = offsetof(struct avs_tplg_library, name),
		.parse = avs_parse_string_token,
	},
};

static int avs_tplg_parse_libraries(struct snd_soc_component *comp,
				    struct snd_soc_tplg_vendor_array *tuples, u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;

	return parse_dictionary(comp, tuples, block_size, (void **)&tplg->libs,
				&tplg->num_libs, sizeof(*tplg->libs),
				AVS_TKN_MANIFEST_NUM_LIBRARIES_U32,
				AVS_TKN_LIBRARY_ID_U32,
				library_parsers, ARRAY_SIZE(library_parsers));
}

static const struct avs_tplg_token_parser audio_format_parsers[] = {
	{
		.token = AVS_TKN_AFMT_SAMPLE_RATE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_audio_format, sampling_freq),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_AFMT_BIT_DEPTH_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_audio_format, bit_depth),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_AFMT_CHANNEL_MAP_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_audio_format, channel_map),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_AFMT_CHANNEL_CFG_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_audio_format, channel_config),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_AFMT_INTERLEAVING_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_audio_format, interleaving),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_AFMT_NUM_CHANNELS_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = AVS_TKN_AFMT_NUM_CHANNELS_U32,
		.parse = parse_audio_format_bitfield,
	},
	{
		.token = AVS_TKN_AFMT_VALID_BIT_DEPTH_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = AVS_TKN_AFMT_VALID_BIT_DEPTH_U32,
		.parse = parse_audio_format_bitfield,
	},
	{
		.token = AVS_TKN_AFMT_SAMPLE_TYPE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = AVS_TKN_AFMT_SAMPLE_TYPE_U32,
		.parse = parse_audio_format_bitfield,
	},
};

static int avs_tplg_parse_audio_formats(struct snd_soc_component *comp,
					struct snd_soc_tplg_vendor_array *tuples,
					u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;

	return parse_dictionary(comp, tuples, block_size, (void **)&tplg->fmts,
				&tplg->num_fmts, sizeof(*tplg->fmts),
				AVS_TKN_MANIFEST_NUM_AFMTS_U32,
				AVS_TKN_AFMT_ID_U32,
				audio_format_parsers, ARRAY_SIZE(audio_format_parsers));
}

static const struct avs_tplg_token_parser modcfg_base_parsers[] = {
	{
		.token = AVS_TKN_MODCFG_BASE_CPC_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_base, cpc),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_BASE_IBS_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_base, ibs),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_BASE_OBS_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_base, obs),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_BASE_PAGES_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_base, is_pages),
		.parse = avs_parse_word_token,
	},
};

static int avs_tplg_parse_modcfgs_base(struct snd_soc_component *comp,
				       struct snd_soc_tplg_vendor_array *tuples,
				       u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;

	return parse_dictionary(comp, tuples, block_size, (void **)&tplg->modcfgs_base,
				&tplg->num_modcfgs_base, sizeof(*tplg->modcfgs_base),
				AVS_TKN_MANIFEST_NUM_MODCFGS_BASE_U32,
				AVS_TKN_MODCFG_BASE_ID_U32,
				modcfg_base_parsers, ARRAY_SIZE(modcfg_base_parsers));
}

static const struct avs_tplg_token_parser modcfg_ext_parsers[] = {
	{
		.token = AVS_TKN_MODCFG_EXT_TYPE_UUID,
		.type = SND_SOC_TPLG_TUPLE_TYPE_UUID,
		.offset = offsetof(struct avs_tplg_modcfg_ext, type),
		.parse = avs_parse_uuid_token,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_OUT_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.out_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_FEATURE_MASK_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.feature_mask),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_VINDEX_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.vindex),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_DMA_TYPE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.dma_type),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_DMABUFF_SIZE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.dma_buffer_size),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_CPR_BLOB_FMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, copier.blob_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_MICSEL_OUT_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, micsel.out_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_INTELWOV_CPC_LP_MODE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, wov.cpc_lp_mode),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_SRC_OUT_FREQ_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, src.out_freq),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_MUX_REF_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, mux.ref_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_MUX_OUT_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, mux.out_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_AEC_REF_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, aec.ref_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_AEC_OUT_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, aec.out_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MODCFG_AEC_CPC_LP_MODE_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, aec.cpc_lp_mode),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_ASRC_OUT_FREQ_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, asrc.out_freq),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_ASRC_MODE_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_modcfg_ext, asrc.mode),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_MODCFG_ASRC_DISABLE_JITTER_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_modcfg_ext, asrc.disable_jitter_buffer),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_OUT_CHAN_CFG_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.out_channel_config),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_SELECT_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients_select),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_0_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[0]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_1_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[1]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_2_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[2]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_3_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[3]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_4_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[4]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_5_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[5]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_6_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[6]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_COEFF_7_S32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.coefficients[7]),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_UPDOWN_MIX_CHAN_MAP_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_modcfg_ext, updown_mix.channel_map),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MODCFG_EXT_NUM_INPUT_PINS_U16,
		.type = SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		.offset = offsetof(struct avs_tplg_modcfg_ext, generic.num_input_pins),
		.parse = avs_parse_short_token,
	},
	{
		.token = AVS_TKN_MODCFG_EXT_NUM_OUTPUT_PINS_U16,
		.type = SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		.offset = offsetof(struct avs_tplg_modcfg_ext, generic.num_output_pins),
		.parse = avs_parse_short_token,
	},
};

static const struct avs_tplg_token_parser pin_format_parsers[] = {
	{
		.token = AVS_TKN_PIN_FMT_INDEX_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pin_format, pin_index),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_PIN_FMT_IOBS_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pin_format, iobs),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_PIN_FMT_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pin_format, fmt),
		.parse = avs_parse_audio_format_ptr,
	},
};

static int avs_tplg_parse_modcfg_ext(struct snd_soc_component *comp,
				     struct avs_tplg_modcfg_ext *cfg,
				     struct snd_soc_tplg_vendor_array *tuples,
				     u32 block_size)
{
	u32 esize;
	int ret;

	/* See where pin block starts. */
	ret = avs_tplg_vendor_entry_size(tuples, block_size,
					 AVS_TKN_PIN_FMT_INDEX_U32, &esize);
	if (ret)
		return ret;

	ret = avs_parse_tokens(comp, cfg, modcfg_ext_parsers,
			       ARRAY_SIZE(modcfg_ext_parsers), tuples, esize);
	if (ret)
		return ret;

	block_size -= esize;
	/* Parse trailing in/out pin formats if any. */
	if (block_size) {
		struct avs_tplg_pin_format *pins;
		u32 num_pins;

		num_pins = cfg->generic.num_input_pins + cfg->generic.num_output_pins;
		if (!num_pins)
			return -EINVAL;

		pins = devm_kcalloc(comp->card->dev, num_pins, sizeof(*pins), GFP_KERNEL);
		if (!pins)
			return -ENOMEM;

		tuples = avs_tplg_vendor_array_at(tuples, esize);
		ret = parse_dictionary_entries(comp, tuples, block_size,
					       pins, num_pins, sizeof(*pins),
					       AVS_TKN_PIN_FMT_INDEX_U32,
					       pin_format_parsers,
					       ARRAY_SIZE(pin_format_parsers));
		if (ret)
			return ret;
		cfg->generic.pin_fmts = pins;
	}

	return 0;
}

static int avs_tplg_parse_modcfgs_ext(struct snd_soc_component *comp,
				      struct snd_soc_tplg_vendor_array *tuples,
				      u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;
	int ret, i;

	ret = parse_dictionary_header(comp, tuples, (void **)&tplg->modcfgs_ext,
				      &tplg->num_modcfgs_ext,
				      sizeof(*tplg->modcfgs_ext),
				      AVS_TKN_MANIFEST_NUM_MODCFGS_EXT_U32);
	if (ret)
		return ret;

	block_size -= le32_to_cpu(tuples->size);
	/* With header parsed, move on to parsing entries. */
	tuples = avs_tplg_vendor_array_next(tuples);

	for (i = 0; i < tplg->num_modcfgs_ext; i++) {
		struct avs_tplg_modcfg_ext *cfg = &tplg->modcfgs_ext[i];
		u32 esize;

		ret = avs_tplg_vendor_entry_size(tuples, block_size,
						 AVS_TKN_MODCFG_EXT_ID_U32, &esize);
		if (ret)
			return ret;

		ret = avs_tplg_parse_modcfg_ext(comp, cfg, tuples, esize);
		if (ret)
			return ret;

		block_size -= esize;
		tuples = avs_tplg_vendor_array_at(tuples, esize);
	}

	return 0;
}

static const struct avs_tplg_token_parser pplcfg_parsers[] = {
	{
		.token = AVS_TKN_PPLCFG_REQ_SIZE_U16,
		.type = SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		.offset = offsetof(struct avs_tplg_pplcfg, req_size),
		.parse = avs_parse_short_token,
	},
	{
		.token = AVS_TKN_PPLCFG_PRIORITY_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_pplcfg, priority),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_PPLCFG_LOW_POWER_BOOL,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BOOL,
		.offset = offsetof(struct avs_tplg_pplcfg, lp),
		.parse = avs_parse_bool_token,
	},
	{
		.token = AVS_TKN_PPLCFG_ATTRIBUTES_U16,
		.type = SND_SOC_TPLG_TUPLE_TYPE_SHORT,
		.offset = offsetof(struct avs_tplg_pplcfg, attributes),
		.parse = avs_parse_short_token,
	},
	{
		.token = AVS_TKN_PPLCFG_TRIGGER_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pplcfg, trigger),
		.parse = avs_parse_word_token,
	},
};

static int avs_tplg_parse_pplcfgs(struct snd_soc_component *comp,
				  struct snd_soc_tplg_vendor_array *tuples,
				  u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;

	return parse_dictionary(comp, tuples, block_size, (void **)&tplg->pplcfgs,
				&tplg->num_pplcfgs, sizeof(*tplg->pplcfgs),
				AVS_TKN_MANIFEST_NUM_PPLCFGS_U32,
				AVS_TKN_PPLCFG_ID_U32,
				pplcfg_parsers, ARRAY_SIZE(pplcfg_parsers));
}

static const struct avs_tplg_token_parser binding_parsers[] = {
	{
		.token = AVS_TKN_BINDING_TARGET_TPLG_NAME_STRING,
		.type = SND_SOC_TPLG_TUPLE_TYPE_STRING,
		.offset = offsetof(struct avs_tplg_binding, target_tplg_name),
		.parse = parse_link_formatted_string,
	},
	{
		.token = AVS_TKN_BINDING_TARGET_PATH_TMPL_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_binding, target_path_tmpl_id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_BINDING_TARGET_PPL_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_binding, target_ppl_id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_BINDING_TARGET_MOD_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_binding, target_mod_id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_BINDING_TARGET_MOD_PIN_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_binding, target_mod_pin),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_BINDING_MOD_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_binding, mod_id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_BINDING_MOD_PIN_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_binding, mod_pin),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_BINDING_IS_SINK_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_binding, is_sink),
		.parse = avs_parse_byte_token,
	},
};

static int avs_tplg_parse_bindings(struct snd_soc_component *comp,
				   struct snd_soc_tplg_vendor_array *tuples,
				   u32 block_size)
{
	struct avs_soc_component *acomp = to_avs_soc_component(comp);
	struct avs_tplg *tplg = acomp->tplg;

	return parse_dictionary(comp, tuples, block_size, (void **)&tplg->bindings,
				&tplg->num_bindings, sizeof(*tplg->bindings),
				AVS_TKN_MANIFEST_NUM_BINDINGS_U32,
				AVS_TKN_BINDING_ID_U32,
				binding_parsers, ARRAY_SIZE(binding_parsers));
}

static const struct avs_tplg_token_parser module_parsers[] = {
	{
		.token = AVS_TKN_MOD_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_module, id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_MOD_MODCFG_BASE_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_module, cfg_base),
		.parse = avs_parse_modcfg_base_ptr,
	},
	{
		.token = AVS_TKN_MOD_IN_AFMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_module, in_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_MOD_CORE_ID_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_module, core_id),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_MOD_PROC_DOMAIN_U8,
		.type = SND_SOC_TPLG_TUPLE_TYPE_BYTE,
		.offset = offsetof(struct avs_tplg_module, domain),
		.parse = avs_parse_byte_token,
	},
	{
		.token = AVS_TKN_MOD_MODCFG_EXT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_module, cfg_ext),
		.parse = avs_parse_modcfg_ext_ptr,
	},
};

static struct avs_tplg_module *
avs_tplg_module_create(struct snd_soc_component *comp, struct avs_tplg_pipeline *owner,
		       struct snd_soc_tplg_vendor_array *tuples, u32 block_size)
{
	struct avs_tplg_module *module;
	int ret;

	module = devm_kzalloc(comp->card->dev, sizeof(*module), GFP_KERNEL);
	if (!module)
		return ERR_PTR(-ENOMEM);

	ret = avs_parse_tokens(comp, module, module_parsers,
			       ARRAY_SIZE(module_parsers), tuples, block_size);
	if (ret < 0)
		return ERR_PTR(ret);

	module->owner = owner;
	INIT_LIST_HEAD(&module->node);

	return module;
}

static const struct avs_tplg_token_parser pipeline_parsers[] = {
	{
		.token = AVS_TKN_PPL_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pipeline, id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_PPL_PPLCFG_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pipeline, cfg),
		.parse = avs_parse_pplcfg_ptr,
	},
	{
		.token = AVS_TKN_PPL_NUM_BINDING_IDS_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_pipeline, num_bindings),
		.parse = avs_parse_word_token,
	},
};

static const struct avs_tplg_token_parser bindings_parsers[] = {
	{
		.token = AVS_TKN_PPL_BINDING_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = 0, /* to treat pipeline->bindings as dictionary */
		.parse = avs_parse_binding_ptr,
	},
};

static struct avs_tplg_pipeline *
avs_tplg_pipeline_create(struct snd_soc_component *comp, struct avs_tplg_path *owner,
			 struct snd_soc_tplg_vendor_array *tuples, u32 block_size)
{
	struct avs_tplg_pipeline *pipeline;
	u32 modblk_size, offset;
	int ret;

	pipeline = devm_kzalloc(comp->card->dev, sizeof(*pipeline), GFP_KERNEL);
	if (!pipeline)
		return ERR_PTR(-ENOMEM);

	pipeline->owner = owner;
	INIT_LIST_HEAD(&pipeline->mod_list);

	/* Pipeline header MUST be followed by at least one module. */
	ret = avs_tplg_vendor_array_lookup(tuples, block_size,
					   AVS_TKN_MOD_ID_U32, &offset);
	if (!ret && !offset)
		ret = -EINVAL;
	if (ret)
		return ERR_PTR(ret);

	/* Process header which precedes module sections. */
	ret = avs_parse_tokens(comp, pipeline, pipeline_parsers,
			       ARRAY_SIZE(pipeline_parsers), tuples, offset);
	if (ret < 0)
		return ERR_PTR(ret);

	block_size -= offset;
	tuples = avs_tplg_vendor_array_at(tuples, offset);

	/* Optionally, binding sections follow module ones. */
	ret = avs_tplg_vendor_array_lookup_next(tuples, block_size,
						AVS_TKN_PPL_BINDING_ID_U32, &offset);
	if (ret) {
		if (ret != -ENOENT)
			return ERR_PTR(ret);

		/* Does header information match actual block layout? */
		if (pipeline->num_bindings)
			return ERR_PTR(-EINVAL);

		modblk_size = block_size;
	} else {
		pipeline->bindings = devm_kcalloc(comp->card->dev, pipeline->num_bindings,
						  sizeof(*pipeline->bindings), GFP_KERNEL);
		if (!pipeline->bindings)
			return ERR_PTR(-ENOMEM);

		modblk_size = offset;
	}

	block_size -= modblk_size;
	do {
		struct avs_tplg_module *module;
		u32 esize;

		ret = avs_tplg_vendor_entry_size(tuples, modblk_size,
						 AVS_TKN_MOD_ID_U32, &esize);
		if (ret)
			return ERR_PTR(ret);

		module = avs_tplg_module_create(comp, pipeline, tuples, esize);
		if (IS_ERR(module)) {
			dev_err(comp->dev, "parse module failed: %ld\n",
				PTR_ERR(module));
			return ERR_CAST(module);
		}

		list_add_tail(&module->node, &pipeline->mod_list);
		modblk_size -= esize;
		tuples = avs_tplg_vendor_array_at(tuples, esize);
	} while (modblk_size > 0);

	/* What's left is optional range of bindings. */
	ret = parse_dictionary_entries(comp, tuples, block_size, pipeline->bindings,
				       pipeline->num_bindings, sizeof(*pipeline->bindings),
				       AVS_TKN_PPL_BINDING_ID_U32,
				       bindings_parsers, ARRAY_SIZE(bindings_parsers));
	if (ret)
		return ERR_PTR(ret);

	return pipeline;
}

static const struct avs_tplg_token_parser path_parsers[] = {
	{
		.token = AVS_TKN_PATH_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_path, id),
		.parse = avs_parse_word_token,
	},
	{
		.token = AVS_TKN_PATH_FE_FMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_path, fe_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
	{
		.token = AVS_TKN_PATH_BE_FMT_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_path, be_fmt),
		.parse = avs_parse_audio_format_ptr,
	},
};

static struct avs_tplg_path *
avs_tplg_path_create(struct snd_soc_component *comp, struct avs_tplg_path_template *owner,
		     struct snd_soc_tplg_vendor_array *tuples, u32 block_size,
		     const struct avs_tplg_token_parser *parsers, u32 num_parsers)
{
	struct avs_tplg_pipeline *pipeline;
	struct avs_tplg_path *path;
	u32 offset;
	int ret;

	path = devm_kzalloc(comp->card->dev, sizeof(*path), GFP_KERNEL);
	if (!path)
		return ERR_PTR(-ENOMEM);

	path->owner = owner;
	INIT_LIST_HEAD(&path->ppl_list);
	INIT_LIST_HEAD(&path->node);

	/* Path header MAY be followed by one or more pipelines. */
	ret = avs_tplg_vendor_array_lookup(tuples, block_size,
					   AVS_TKN_PPL_ID_U32, &offset);
	if (ret == -ENOENT)
		offset = block_size;
	else if (ret)
		return ERR_PTR(ret);
	else if (!offset)
		return ERR_PTR(-EINVAL);

	/* Process header which precedes pipeline sections. */
	ret = avs_parse_tokens(comp, path, parsers, num_parsers, tuples, offset);
	if (ret < 0)
		return ERR_PTR(ret);

	block_size -= offset;
	tuples = avs_tplg_vendor_array_at(tuples, offset);
	while (block_size > 0) {
		u32 esize;

		ret = avs_tplg_vendor_entry_size(tuples, block_size,
						 AVS_TKN_PPL_ID_U32, &esize);
		if (ret)
			return ERR_PTR(ret);

		pipeline = avs_tplg_pipeline_create(comp, path, tuples, esize);
		if (IS_ERR(pipeline)) {
			dev_err(comp->dev, "parse pipeline failed: %ld\n",
				PTR_ERR(pipeline));
			return ERR_CAST(pipeline);
		}

		list_add_tail(&pipeline->node, &path->ppl_list);
		block_size -= esize;
		tuples = avs_tplg_vendor_array_at(tuples, esize);
	}

	return path;
}

static const struct avs_tplg_token_parser path_tmpl_parsers[] = {
	{
		.token = AVS_TKN_PATH_TMPL_ID_U32,
		.type = SND_SOC_TPLG_TUPLE_TYPE_WORD,
		.offset = offsetof(struct avs_tplg_path_template, id),
		.parse = avs_parse_word_token,
	},
};

static int parse_path_template(struct snd_soc_component *comp,
			       struct snd_soc_tplg_vendor_array *tuples, u32 block_size,
			       struct avs_tplg_path_template *template,
			       const struct avs_tplg_token_parser *tmpl_tokens, u32 num_tmpl_tokens,
			       const struct avs_tplg_token_parser *path_tokens, u32 num_path_tokens)
{
	struct avs_tplg_path *path;
	u32 offset;
	int ret;

	/* Path template header MUST be followed by at least one path variant. */
	ret = avs_tplg_vendor_array_lookup(tuples, block_size,
					   AVS_TKN_PATH_ID_U32, &offset);
	if (ret)
		return ret;

	/* Process header which precedes path variants sections. */
	ret = avs_parse_tokens(comp, template, tmpl_tokens, num_tmpl_tokens, tuples, offset);
	if (ret < 0)
		return ret;

	block_size -= offset;
	tuples = avs_tplg_vendor_array_at(tuples, offset);
	do {
		u32 esize;

		ret = avs_tplg_vendor_entry_size(tuples, block_size,
						 AVS_TKN_PATH_ID_U32, &esize);
		if (ret)
			return ret;

		path = avs_tplg_path_create(comp, template, tuples, esize, path_tokens,
					    num_path_tokens);
		if (IS_ERR(path)) {
			dev_err(comp->dev, "parse path failed: %ld\n", PTR_ERR(path));
			return PTR_ERR(path);
		}

		list_add_tail(&path->node, &template->path_list);
		block_size -= esize;
		tuples = avs_tplg_vendor_array_at(tuples, esize);
	} while (block_size > 0);

	return 0;
}

static struct avs_tplg_path_template *
avs_tplg_path_template_create(struct snd_soc_component *comp, struct avs_tplg *owner,
			      struct snd_soc_tplg_vendor_array *tuples, u32 block_size)
{
	struct avs_tplg_path_template *template;
	int ret;

	template = devm_kzalloc(comp->card->dev, sizeof(*template), GFP_KERNEL);
	if (!template)
		return ERR_PTR(-ENOMEM);

	template->owner = owner; /* Used to access component tplg is assigned to. */
	INIT_LIST_HEAD(&template->path_list);
	INIT_LIST_HEAD(&template->node);

	ret = parse_path_template(comp, tuples, block_size, template, path_tmpl_parsers,
				  ARRAY_SIZE(path_tmpl_parsers), path_parsers,
				  ARRAY_SIZE(path_parsers));
	if (ret)
		return ERR_PTR(ret);

	return template;
}
