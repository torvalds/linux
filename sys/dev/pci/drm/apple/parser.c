// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io> */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/math.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <sound/pcm.h> // for sound format masks

#include "parser.h"
#include "trace.h"

#define DCP_PARSE_HEADER 0xd3

enum dcp_parse_type {
	DCP_TYPE_DICTIONARY = 1,
	DCP_TYPE_ARRAY = 2,
	DCP_TYPE_INT64 = 4,
	DCP_TYPE_STRING = 9,
	DCP_TYPE_BLOB = 10,
	DCP_TYPE_BOOL = 11
};

struct dcp_parse_tag {
	unsigned int size : 24;
	enum dcp_parse_type type : 5;
	unsigned int padding : 2;
	bool last : 1;
} __packed;

static const void *parse_bytes(struct dcp_parse_ctx *ctx, size_t count)
{
	const void *ptr = ctx->blob + ctx->pos;

	if (ctx->pos + count > ctx->len)
		return ERR_PTR(-EINVAL);

	ctx->pos += count;
	return ptr;
}

static const u32 *parse_u32(struct dcp_parse_ctx *ctx)
{
	return parse_bytes(ctx, sizeof(u32));
}

static const struct dcp_parse_tag *parse_tag(struct dcp_parse_ctx *ctx)
{
	const struct dcp_parse_tag *tag;

	/* Align to 32-bits */
	ctx->pos = round_up(ctx->pos, 4);

	tag = parse_bytes(ctx, sizeof(struct dcp_parse_tag));

	if (IS_ERR(tag))
		return tag;

	if (tag->padding)
		return ERR_PTR(-EINVAL);

	return tag;
}

static const struct dcp_parse_tag *parse_tag_of_type(struct dcp_parse_ctx *ctx,
					       enum dcp_parse_type type)
{
	const struct dcp_parse_tag *tag = parse_tag(ctx);

	if (IS_ERR(tag))
		return tag;

	if (tag->type != type)
		return ERR_PTR(-EINVAL);

	return tag;
}

static int skip(struct dcp_parse_ctx *handle)
{
	const struct dcp_parse_tag *tag = parse_tag(handle);
	int ret = 0;
	int i;

	if (IS_ERR(tag))
		return PTR_ERR(tag);

	switch (tag->type) {
	case DCP_TYPE_DICTIONARY:
		for (i = 0; i < tag->size; ++i) {
			ret |= skip(handle); /* key */
			ret |= skip(handle); /* value */
		}

		return ret;

	case DCP_TYPE_ARRAY:
		for (i = 0; i < tag->size; ++i)
			ret |= skip(handle);

		return ret;

	case DCP_TYPE_INT64:
		handle->pos += sizeof(s64);
		return 0;

	case DCP_TYPE_STRING:
	case DCP_TYPE_BLOB:
		handle->pos += tag->size;
		return 0;

	case DCP_TYPE_BOOL:
		return 0;

	default:
		return -EINVAL;
	}
}

static int skip_pair(struct dcp_parse_ctx *handle)
{
	int ret;

	ret = skip(handle);
	if (ret)
		return ret;

	return skip(handle);
}

static bool consume_string(struct dcp_parse_ctx *ctx, const char *specimen)
{
	const struct dcp_parse_tag *tag;
	const char *key;
	ctx->pos = round_up(ctx->pos, 4);

	if (ctx->pos + sizeof(*tag) + strlen(specimen) - 1 > ctx->len)
		return false;
	tag = ctx->blob + ctx->pos;
	key = ctx->blob + ctx->pos + sizeof(*tag);
	if (tag->padding)
		return false;

	if (tag->type != DCP_TYPE_STRING ||
	    tag->size != strlen(specimen) ||
	    strncmp(key, specimen, tag->size))
		return false;

	skip(ctx);
	return true;
}

/* Caller must free the result */
static char *parse_string(struct dcp_parse_ctx *handle)
{
	const struct dcp_parse_tag *tag = parse_tag_of_type(handle, DCP_TYPE_STRING);
	const char *in;
	char *out;

	if (IS_ERR(tag))
		return (void *)tag;

	in = parse_bytes(handle, tag->size);
	if (IS_ERR(in))
		return (void *)in;

	out = kmalloc(tag->size + 1, GFP_KERNEL);

	memcpy(out, in, tag->size);
	out[tag->size] = '\0';
	return out;
}

static int parse_int(struct dcp_parse_ctx *handle, s64 *value)
{
	const void *tag = parse_tag_of_type(handle, DCP_TYPE_INT64);
	const s64 *in;

	if (IS_ERR(tag))
		return PTR_ERR(tag);

	in = parse_bytes(handle, sizeof(s64));

	if (IS_ERR(in))
		return PTR_ERR(in);

	memcpy(value, in, sizeof(*value));
	return 0;
}

static int parse_bool(struct dcp_parse_ctx *handle, bool *b)
{
	const struct dcp_parse_tag *tag = parse_tag_of_type(handle, DCP_TYPE_BOOL);

	if (IS_ERR(tag))
		return PTR_ERR(tag);

	*b = !!tag->size;
	return 0;
}

static int parse_blob(struct dcp_parse_ctx *handle, size_t size, u8 const **blob)
{
	const struct dcp_parse_tag *tag = parse_tag_of_type(handle, DCP_TYPE_BLOB);
	const u8 *out;

	if (IS_ERR(tag))
		return PTR_ERR(tag);

	if (tag->size < size)
		return -EINVAL;

	out = parse_bytes(handle, tag->size);

	if (IS_ERR(out))
		return PTR_ERR(out);

	*blob = out;
	return 0;
}

struct iterator {
	struct dcp_parse_ctx *handle;
	u32 idx, len;
};

static int iterator_begin(struct dcp_parse_ctx *handle, struct iterator *it,
			  bool dict)
{
	const struct dcp_parse_tag *tag;
	enum dcp_parse_type type = dict ? DCP_TYPE_DICTIONARY : DCP_TYPE_ARRAY;

	*it = (struct iterator) {
		.handle = handle,
		.idx = 0
	};

	tag = parse_tag_of_type(it->handle, type);
	if (IS_ERR(tag))
		return PTR_ERR(tag);

	it->len = tag->size;
	return 0;
}

#define dcp_parse_foreach_in_array(handle, it)                                 \
	for (iterator_begin(handle, &it, false); it.idx < it.len; ++it.idx)
#define dcp_parse_foreach_in_dict(handle, it)                                  \
	for (iterator_begin(handle, &it, true); it.idx < it.len; ++it.idx)

int parse(const void *blob, size_t size, struct dcp_parse_ctx *ctx)
{
	const u32 *header;

	*ctx = (struct dcp_parse_ctx) {
		.blob = blob,
		.len = size,
		.pos = 0,
	};

	header = parse_u32(ctx);
	if (IS_ERR(header))
		return PTR_ERR(header);

	if (*header != DCP_PARSE_HEADER)
		return -EINVAL;

	return 0;
}

static int parse_dimension(struct dcp_parse_ctx *handle, struct dimension *dim)
{
	struct iterator it;
	int ret = 0;

	dcp_parse_foreach_in_dict(handle, it) {
		char *key = parse_string(it.handle);

		if (IS_ERR(key))
			ret = PTR_ERR(key);
		else if (!strcmp(key, "Active"))
			ret = parse_int(it.handle, &dim->active);
		else if (!strcmp(key, "Total"))
			ret = parse_int(it.handle, &dim->total);
		else if (!strcmp(key, "FrontPorch"))
			ret = parse_int(it.handle, &dim->front_porch);
		else if (!strcmp(key, "SyncWidth"))
			ret = parse_int(it.handle, &dim->sync_width);
		else if (!strcmp(key, "PreciseSyncRate"))
			ret = parse_int(it.handle, &dim->precise_sync_rate);
		else
			skip(it.handle);

		if (!IS_ERR_OR_NULL(key))
			kfree(key);

		if (ret)
			return ret;
	}

	return 0;
}

struct color_mode {
	s64 colorimetry;
	s64 depth;
	s64 dynamic_range;
	s64 eotf;
	s64 id;
	s64 pixel_encoding;
	s64 score;
};

static int fill_color_mode(struct dcp_color_mode *color,
			   struct color_mode *cmode)
{
	if (color->score >= cmode->score)
		return 0;

	if (cmode->colorimetry < 0 || cmode->colorimetry >= DCP_COLORIMETRY_COUNT)
		return -EINVAL;
	if (cmode->depth < 8 || cmode->depth > 12)
		return -EINVAL;
	if (cmode->dynamic_range < 0 || cmode->dynamic_range >= DCP_COLOR_YCBCR_RANGE_COUNT)
		return -EINVAL;
	if (cmode->eotf < 0 || cmode->eotf >= DCP_EOTF_COUNT)
		return -EINVAL;
	if (cmode->pixel_encoding < 0 || cmode->pixel_encoding >= DCP_COLOR_FORMAT_COUNT)
		return -EINVAL;

	color->score = cmode->score;
	color->id = cmode->id;
	color->eotf = cmode->eotf;
	color->format = cmode->pixel_encoding;
	color->colorimetry = cmode->colorimetry;
	color->range = cmode->dynamic_range;
	color->depth = cmode->depth;

	return 0;
}

static int parse_color_modes(struct dcp_parse_ctx *handle,
			     struct dcp_display_mode *out)
{
	struct iterator outer_it;
	int ret = 0;
	out->sdr_444.score = -1;
	out->sdr_rgb.score = -1;
	out->best.score = -1;

	dcp_parse_foreach_in_array(handle, outer_it) {
		struct iterator it;
		bool is_virtual = true;
		struct color_mode cmode;

		dcp_parse_foreach_in_dict(handle, it) {
			char *key = parse_string(it.handle);

			if (IS_ERR(key))
				ret = PTR_ERR(key);
			else if (!strcmp(key, "Colorimetry"))
				ret = parse_int(it.handle, &cmode.colorimetry);
			else if (!strcmp(key, "Depth"))
				ret = parse_int(it.handle, &cmode.depth);
			else if (!strcmp(key, "DynamicRange"))
				ret = parse_int(it.handle, &cmode.dynamic_range);
			else if (!strcmp(key, "EOTF"))
				ret = parse_int(it.handle, &cmode.eotf);
			else if (!strcmp(key, "ID"))
				ret = parse_int(it.handle, &cmode.id);
			else if (!strcmp(key, "IsVirtual"))
				ret = parse_bool(it.handle, &is_virtual);
			else if (!strcmp(key, "PixelEncoding"))
				ret = parse_int(it.handle, &cmode.pixel_encoding);
			else if (!strcmp(key, "Score"))
				ret = parse_int(it.handle, &cmode.score);
			else
				skip(it.handle);

			if (!IS_ERR_OR_NULL(key))
				kfree(key);

			if (ret)
				return ret;
		}

		/* Skip virtual or partial entries */
		if (is_virtual || cmode.score < 0 || cmode.id < 0)
			continue;

		trace_iomfb_color_mode(handle->dcp, cmode.id, cmode.score,
				       cmode.depth, cmode.colorimetry,
				       cmode.eotf, cmode.dynamic_range,
				       cmode.pixel_encoding);

		if (cmode.eotf == DCP_EOTF_SDR_GAMMA) {
			if (cmode.pixel_encoding == DCP_COLOR_FORMAT_RGB &&
				cmode.depth <= 10)
				fill_color_mode(&out->sdr_rgb, &cmode);
			else if (cmode.pixel_encoding == DCP_COLOR_FORMAT_YCBCR444 &&
				cmode.depth <= 10)
				fill_color_mode(&out->sdr_444, &cmode);
			fill_color_mode(&out->sdr, &cmode);
		}
		fill_color_mode(&out->best, &cmode);
	}

	return 0;
}

/*
 * Calculate the pixel clock for a mode given the 16:16 fixed-point refresh
 * rate. The pixel clock is the refresh rate times the pixel count. DRM
 * specifies the clock in kHz. The intermediate result may overflow a u32, so
 * use a u64 where required.
 */
static u32 calculate_clock(struct dimension *horiz, struct dimension *vert)
{
	u32 pixels = horiz->total * vert->total;
	u64 clock = mul_u32_u32(pixels, vert->precise_sync_rate);

	return DIV_ROUND_CLOSEST_ULL(clock >> 16, 1000);
}

static int parse_mode(struct dcp_parse_ctx *handle,
		      struct dcp_display_mode *out, s64 *score, int width_mm,
		      int height_mm, unsigned notch_height)
{
	int ret = 0;
	struct iterator it;
	struct dimension horiz, vert;
	s64 id = -1;
	s64 best_color_mode = -1;
	bool is_virtual = false;
	struct drm_display_mode *mode = &out->mode;

	dcp_parse_foreach_in_dict(handle, it) {
		char *key = parse_string(it.handle);

		if (IS_ERR(key))
			ret = PTR_ERR(key);
		else if (is_virtual)
			skip(it.handle);
		else if (!strcmp(key, "HorizontalAttributes"))
			ret = parse_dimension(it.handle, &horiz);
		else if (!strcmp(key, "VerticalAttributes"))
			ret = parse_dimension(it.handle, &vert);
		else if (!strcmp(key, "ColorModes"))
			ret = parse_color_modes(it.handle, out);
		else if (!strcmp(key, "ID"))
			ret = parse_int(it.handle, &id);
		else if (!strcmp(key, "IsVirtual"))
			ret = parse_bool(it.handle, &is_virtual);
		else if (!strcmp(key, "Score"))
			ret = parse_int(it.handle, score);
		else
			skip(it.handle);

		if (!IS_ERR_OR_NULL(key))
			kfree(key);

		if (ret) {
			trace_iomfb_parse_mode_fail(id, &horiz, &vert, best_color_mode, is_virtual, *score);
			return ret;
		}
	}
	if (out->sdr_rgb.score >= 0)
		best_color_mode = out->sdr_rgb.id;
	else if (out->sdr_444.score >= 0)
		best_color_mode = out->sdr_444.id;
	else if (out->sdr.score >= 0)
		best_color_mode = out->sdr.id;
	else if (out->best.score >= 0)
		best_color_mode = out->best.id;

	trace_iomfb_parse_mode_success(id, &horiz, &vert, best_color_mode,
				       is_virtual, *score);

	/*
	 * Reject modes without valid color mode.
	 */
	if (best_color_mode < 0)
		return -EINVAL;

	/*
	 * We need to skip virtual modes. In some cases, virtual modes are "too
	 * big" for the monitor and can cause breakage. It is unclear why the
	 * DCP reports these modes at all. Treat as a recoverable error.
	 */
	if (is_virtual)
		return -EINVAL;

	/*
	 * HACK:
	 * Ignore the 120 Hz mode on j314/j316 (identified by resolution).
	 * DCP limits normal swaps to 60 Hz anyway and the 120 Hz mode might
	 * cause choppiness with X11.
	 * Just downscoring it and thus making the 60 Hz mode the preferred mode
	 * seems not enough for some user space.
	 */
	if (vert.precise_sync_rate >> 16 == 120 &&
	    ((horiz.active == 3024 && vert.active == 1964) ||
	     (horiz.active == 3456 && vert.active == 2234)))
		return -EINVAL;

	/*
	 * HACK: reject refresh modes with a pixel clock above 926484,480 kHz
	 *       (bandwidth limit reported by dcp). This allows 4k 100Hz and
	 *       5k 60Hz but not much beyond.
	 *       DSC setup seems to require additional steps
	 */
	if (calculate_clock(&horiz, &vert) > 926484) {
		pr_info("dcp: rejecting mode %lldx%lld@%lld.%03lld (pixel clk:%d)\n",
			horiz.active, vert.active, vert.precise_sync_rate >> 16,
			((1000 * vert.precise_sync_rate) >> 16) % 1000,
			calculate_clock(&horiz, &vert));
		return -EINVAL;
	}

	vert.active -= notch_height;
	vert.sync_width += notch_height;

	/* From here we must succeed. Start filling out the mode. */
	*mode = (struct drm_display_mode) {
		.type = DRM_MODE_TYPE_DRIVER,
		.clock = calculate_clock(&horiz, &vert),

		.vdisplay = vert.active,
		.vsync_start = vert.active + vert.front_porch,
		.vsync_end = vert.active + vert.front_porch + vert.sync_width,
		.vtotal = vert.total,

		.hdisplay = horiz.active,
		.hsync_start = horiz.active + horiz.front_porch,
		.hsync_end = horiz.active + horiz.front_porch +
			     horiz.sync_width,
		.htotal = horiz.total,

		.width_mm = width_mm,
		.height_mm = height_mm,
	};

	drm_mode_set_name(mode);

	out->timing_mode_id = id;
	out->color_mode_id = best_color_mode;

	trace_iomfb_timing_mode(handle->dcp, id, *score, horiz.active,
				vert.active, vert.precise_sync_rate,
				best_color_mode);

	return 0;
}

struct dcp_display_mode *enumerate_modes(struct dcp_parse_ctx *handle,
					 unsigned int *count, int width_mm,
					 int height_mm, unsigned notch_height)
{
	struct iterator it;
	int ret;
	struct dcp_display_mode *mode, *modes;
	struct dcp_display_mode *best_mode = NULL;
	s64 score, best_score = -1;

	ret = iterator_begin(handle, &it, false);

	if (ret)
		return ERR_PTR(ret);

	/* Start with a worst case allocation */
	modes = kmalloc_array(it.len, sizeof(*modes), GFP_KERNEL);
	*count = 0;

	if (!modes)
		return ERR_PTR(-ENOMEM);

	for (; it.idx < it.len; ++it.idx) {
		mode = &modes[*count];
		ret = parse_mode(it.handle, mode, &score, width_mm, height_mm, notch_height);

		/* Errors for a single mode are recoverable -- just skip it. */
		if (ret)
			continue;

		/* Process a successful mode */
		(*count)++;

		if (score > best_score) {
			best_score = score;
			best_mode = mode;
		}
	}

	if (best_mode != NULL)
		best_mode->mode.type |= DRM_MODE_TYPE_PREFERRED;

	return modes;
}

int parse_display_attributes(struct dcp_parse_ctx *handle, int *width_mm,
			     int *height_mm)
{
	int ret = 0;
	struct iterator it;
	s64 width_cm = 0, height_cm = 0;

	dcp_parse_foreach_in_dict(handle, it) {
		char *key = parse_string(it.handle);

		if (IS_ERR(key))
			ret = PTR_ERR(key);
		else if (!strcmp(key, "MaxHorizontalImageSize"))
			ret = parse_int(it.handle, &width_cm);
		else if (!strcmp(key, "MaxVerticalImageSize"))
			ret = parse_int(it.handle, &height_cm);
		else
			skip(it.handle);

		if (!IS_ERR_OR_NULL(key))
			kfree(key);

		if (ret)
			return ret;
	}

	/* 1cm = 10mm */
	*width_mm = 10 * width_cm;
	*height_mm = 10 * height_cm;

	return 0;
}

int parse_epic_service_init(struct dcp_parse_ctx *handle, const char **name,
			    const char **class, s64 *unit)
{
	int ret = 0;
	struct iterator it;
	bool parsed_unit = false;
	bool parsed_name = false;
	bool parsed_class = false;

	*name = ERR_PTR(-ENOENT);
	*class = ERR_PTR(-ENOENT);

	dcp_parse_foreach_in_dict(handle, it) {
		char *key = parse_string(it.handle);

		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
			break;
		}

		if (!strcmp(key, "EPICName")) {
			*name = parse_string(it.handle);
			if (IS_ERR(*name))
				ret = PTR_ERR(*name);
			else
				parsed_name = true;
		} else if (!strcmp(key, "EPICProviderClass")) {
			*class = parse_string(it.handle);
			if (IS_ERR(*class))
				ret = PTR_ERR(*class);
			else
				parsed_class = true;
		} else if (!strcmp(key, "EPICUnit")) {
			ret = parse_int(it.handle, unit);
			if (!ret)
				parsed_unit = true;
		} else {
			skip(it.handle);
		}

		kfree(key);
		if (ret)
			break;
	}

	if (!parsed_unit || !parsed_name || !parsed_class)
		ret = -ENOENT;

	if (ret) {
		if (!IS_ERR(*name)) {
			kfree(*name);
			*name = ERR_PTR(ret);
		}
		if (!IS_ERR(*class)) {
			kfree(*class);
			*class = ERR_PTR(ret);
		}
	}

	return ret;
}

static int parse_sample_rate_bit(struct dcp_parse_ctx *handle, unsigned int *ratebit)
{
	s64 rate;
	int ret = parse_int(handle, &rate);

	if (ret)
		return ret;

	*ratebit = snd_pcm_rate_to_rate_bit(rate);
	if (*ratebit == SNDRV_PCM_RATE_KNOT) {
		/*
		 * The rate wasn't recognized, and unless we supply
		 * a supplementary constraint, the SNDRV_PCM_RATE_KNOT bit
		 * will allow any rate. So clear it.
		 */
		*ratebit = 0;
	}

	return 0;
}

static int parse_sample_fmtbit(struct dcp_parse_ctx *handle, u64 *fmtbit)
{
	s64 sample_size;
	int ret = parse_int(handle, &sample_size);

	if (ret)
		return ret;

	switch (sample_size) {
	case 16:
		*fmtbit = SNDRV_PCM_FMTBIT_S16;
		break;
	case 20:
		*fmtbit = SNDRV_PCM_FMTBIT_S20;
		break;
	case 24:
		*fmtbit = SNDRV_PCM_FMTBIT_S24;
		break;
	case 32:
		*fmtbit = SNDRV_PCM_FMTBIT_S32;
		break;
	default:
		*fmtbit = 0;
		break;
	}

	return 0;
}

static struct {
	const char *label;
	u8 type;
} chan_position_names[] = {
	{ "Front Left", SNDRV_CHMAP_FL },
	{ "Front Right", SNDRV_CHMAP_FR },
	{ "Rear Left", SNDRV_CHMAP_RL },
	{ "Rear Right", SNDRV_CHMAP_RR },
	{ "Front Center", SNDRV_CHMAP_FC },
	{ "Low Frequency Effects", SNDRV_CHMAP_LFE },
	{ "Rear Center", SNDRV_CHMAP_RC },
	{ "Front Left Center", SNDRV_CHMAP_FLC },
	{ "Front Right Center", SNDRV_CHMAP_FRC },
	{ "Rear Left Center", SNDRV_CHMAP_RLC },
	{ "Rear Right Center", SNDRV_CHMAP_RRC },
	{ "Front Left Wide", SNDRV_CHMAP_FLW },
	{ "Front Right Wide", SNDRV_CHMAP_FRW },
	{ "Front Left High", SNDRV_CHMAP_FLH },
	{ "Front Center High", SNDRV_CHMAP_FCH },
	{ "Front Right High", SNDRV_CHMAP_FRH },
	{ "Top Center", SNDRV_CHMAP_TC },
};

static void append_chmap(struct snd_pcm_chmap_elem *chmap, u8 type)
{
	if (!chmap || chmap->channels >= ARRAY_SIZE(chmap->map))
		return;

	chmap->map[chmap->channels] = type;
	chmap->channels++;
}

static int parse_chmap(struct dcp_parse_ctx *handle, struct snd_pcm_chmap_elem *chmap)
{
	struct iterator it;
	int i, ret;

	if (!chmap) {
		skip(handle);
		return 0;
	}

	chmap->channels = 0;

	dcp_parse_foreach_in_array(handle, it) {
		for (i = 0; i < ARRAY_SIZE(chan_position_names); i++)
			if (consume_string(it.handle, chan_position_names[i].label))
				break;

		if (i == ARRAY_SIZE(chan_position_names)) {
			ret = skip(it.handle);
			if (ret)
				return ret;

			append_chmap(chmap, SNDRV_CHMAP_UNKNOWN);
			continue;
		}

		append_chmap(chmap, chan_position_names[i].type);
	}

	return 0;
}

static int parse_chan_layout_element(struct dcp_parse_ctx *handle,
				     unsigned int *nchans_out,
				     struct snd_pcm_chmap_elem *chmap)
{
	struct iterator it;
	int ret;
	s64 nchans = 0;

	dcp_parse_foreach_in_dict(handle, it) {
		if (consume_string(it.handle, "ActiveChannelCount"))
			ret = parse_int(it.handle, &nchans);
		else if (consume_string(it.handle, "ChannelLayout"))
			ret = parse_chmap(it.handle, chmap);
		else
			ret = skip_pair(it.handle);

		if (ret)
			return ret;
	}

	if (nchans_out)
		*nchans_out = nchans;

	return 0;
}

static int parse_nchans_mask(struct dcp_parse_ctx *handle, unsigned int *mask)
{
	struct iterator it;
	int ret;

	*mask = 0;

	dcp_parse_foreach_in_array(handle, it) {
		int nchans;

		ret = parse_chan_layout_element(it.handle, &nchans, NULL);
		if (ret)
			return ret;
		*mask |= 1 << nchans;
	}

	return 0;
}

static int parse_avep_element(struct dcp_parse_ctx *handle,
			      struct dcp_sound_format_mask *sieve,
			      struct dcp_sound_format_mask *hits)
{
	struct dcp_sound_format_mask mask = {0, 0, 0};
	struct iterator it;
	int ret;

	dcp_parse_foreach_in_dict(handle, it) {
		if (consume_string(handle, "StreamSampleRate"))
			ret = parse_sample_rate_bit(it.handle, &mask.rates);
		else if (consume_string(handle, "SampleSize"))
			ret = parse_sample_fmtbit(it.handle, &mask.formats);
		else if (consume_string(handle, "AudioChannelLayoutElements"))
			ret = parse_nchans_mask(it.handle, &mask.nchans);
		else
			ret = skip_pair(it.handle);

		if (ret)
			return ret;
	}

	trace_avep_sound_mode(handle->dcp, mask.rates, mask.formats, mask.nchans);

	if (!(mask.rates & sieve->rates) || !(mask.formats & sieve->formats) ||
		!(mask.nchans & sieve->nchans))
	    return 0;

	if (hits) {
		hits->rates |= mask.rates;
		hits->formats |= mask.formats;
		hits->nchans |= mask.nchans;
	}

	return 1;
}

static int parse_mode_in_avep_element(struct dcp_parse_ctx *handle,
				      unsigned int selected_nchans,
				      struct snd_pcm_chmap_elem *chmap,
				      struct dcp_sound_cookie *cookie)
{
	struct iterator it;
	struct dcp_parse_ctx save_handle;
	int ret;

	dcp_parse_foreach_in_dict(handle, it) {
		if (consume_string(it.handle, "AudioChannelLayoutElements")) {
			struct iterator inner_it;
			int nchans;

			dcp_parse_foreach_in_array(it.handle, inner_it) {
				save_handle = *it.handle;
				ret = parse_chan_layout_element(inner_it.handle,
								&nchans, NULL);
				if (ret)
					return ret;

				if (nchans != selected_nchans)
					continue;

				/*
				 * Now that we know this layout matches the
				 * selected channel number, reread the element
				 * and fill in the channel map.
				 */
				*inner_it.handle = save_handle;
				ret = parse_chan_layout_element(inner_it.handle,
								NULL, chmap);
				if (ret)
					return ret;
			}
		} else if (consume_string(it.handle, "ElementData")) {
			const u8 *blob;

			ret = parse_blob(it.handle, sizeof(*cookie), &blob);
			if (ret)
				return ret;

			if (cookie)
				memcpy(cookie, blob, sizeof(*cookie));
		} else {
			ret = skip_pair(it.handle);
			if (ret)
				return ret;
		}
	}

	return 0;
}

int parse_sound_constraints(struct dcp_parse_ctx *handle,
			    struct dcp_sound_format_mask *sieve,
			    struct dcp_sound_format_mask *hits)
{
	int ret;
	struct iterator it;

	if (hits) {
		hits->rates = 0;
		hits->formats = 0;
		hits->nchans = 0;
	}

	dcp_parse_foreach_in_array(handle, it) {
		ret = parse_avep_element(it.handle, sieve, hits);

		if (ret < 0)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(parse_sound_constraints);

int parse_sound_mode(struct dcp_parse_ctx *handle,
		     struct dcp_sound_format_mask *sieve,
		     struct snd_pcm_chmap_elem *chmap,
		     struct dcp_sound_cookie *cookie)
{
	struct dcp_parse_ctx save_handle;
	struct iterator it;
	int ret;

	dcp_parse_foreach_in_array(handle, it) {
		save_handle = *it.handle;
		ret = parse_avep_element(it.handle, sieve, NULL);

		if (!ret)
			continue;

		if (ret < 0)
			return ret;

		ret = parse_mode_in_avep_element(&save_handle, __ffs(sieve->nchans),
						 chmap, cookie);
		if (ret < 0)
			return ret;
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(parse_sound_mode);

int parse_system_log_mnits(struct dcp_parse_ctx *handle, struct dcp_system_ev_mnits *entry)
{
	struct iterator it;
	int ret;
	s64 mnits = -1;
	s64 idac = -1;
	s64 timestamp = -1;
	bool type_match = false;

	dcp_parse_foreach_in_dict(handle, it) {
		char *key = parse_string(it.handle);
		if (IS_ERR(key)) {
			ret = PTR_ERR(key);
		} else if (!strcmp(key, "mNits")) {
			ret = parse_int(it.handle, &mnits);
		} else if (!strcmp(key, "iDAC")) {
			ret = parse_int(it.handle, &idac);
		} else if (!strcmp(key, "logEvent")) {
			const char * value = parse_string(it.handle);
			if (!IS_ERR_OR_NULL(value)) {
				type_match = strcmp(value, "Display (Event Forward)") == 0;
				kfree(value);
			}
		} else if (!strcmp(key, "timestamp")) {
			ret = parse_int(it.handle, &timestamp);
		} else {
			skip(it.handle);
		}

		if (!IS_ERR_OR_NULL(key))
			kfree(key);

		if (ret) {
			pr_err("dcp parser: failed to parse mNits sys event\n");
			return ret;
		}
	}

	if (!type_match ||  mnits < 0 || idac < 0 || timestamp < 0)
		return -EINVAL;

	entry->millinits = mnits;
	entry->idac = idac;
	entry->timestamp = timestamp;

	return 0;
}
