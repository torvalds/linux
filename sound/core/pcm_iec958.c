// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PCM DRM helpers
 */
#include <linux/export.h>
#include <linux/types.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/pcm_iec958.h>

/**
 * snd_pcm_create_iec958_consumer_default - create default consumer format IEC958 channel status
 * @cs: channel status buffer, at least four bytes
 * @len: length of channel status buffer
 *
 * Create the consumer format channel status data in @cs of maximum size
 * @len. When relevant, the configuration-dependant bits will be set as
 * unspecified.
 *
 * Drivers should then call einter snd_pcm_fill_iec958_consumer() or
 * snd_pcm_fill_iec958_consumer_hw_params() to replace these unspecified
 * bits by their actual values.
 *
 * Drivers may wish to tweak the contents of the buffer after creation.
 *
 * Returns: length of buffer, or negative error code if something failed.
 */
int snd_pcm_create_iec958_consumer_default(u8 *cs, size_t len)
{
	if (len < 4)
		return -EINVAL;

	memset(cs, 0, len);

	cs[0] = IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS_NONE;
	cs[1] = IEC958_AES1_CON_GENERAL;
	cs[2] = IEC958_AES2_CON_SOURCE_UNSPEC | IEC958_AES2_CON_CHANNEL_UNSPEC;
	cs[3] = IEC958_AES3_CON_CLOCK_1000PPM | IEC958_AES3_CON_FS_NOTID;

	if (len > 4)
		cs[4] = IEC958_AES4_CON_WORDLEN_NOTID;

	return len;
}
EXPORT_SYMBOL_GPL(snd_pcm_create_iec958_consumer_default);

static int fill_iec958_consumer(uint rate, uint sample_width,
				u8 *cs, size_t len)
{
	if (len < 4)
		return -EINVAL;

	if ((cs[3] & IEC958_AES3_CON_FS) == IEC958_AES3_CON_FS_NOTID) {
		unsigned int fs;

		switch (rate) {
			case 32000:
				fs = IEC958_AES3_CON_FS_32000;
				break;
			case 44100:
				fs = IEC958_AES3_CON_FS_44100;
				break;
			case 48000:
				fs = IEC958_AES3_CON_FS_48000;
				break;
			case 88200:
				fs = IEC958_AES3_CON_FS_88200;
				break;
			case 96000:
				fs = IEC958_AES3_CON_FS_96000;
				break;
			case 176400:
				fs = IEC958_AES3_CON_FS_176400;
				break;
			case 192000:
				fs = IEC958_AES3_CON_FS_192000;
				break;
			default:
				return -EINVAL;
		}

		cs[3] &= ~IEC958_AES3_CON_FS;
		cs[3] |= fs;
	}

	if (len > 4 &&
	    (cs[4] & IEC958_AES4_CON_WORDLEN) == IEC958_AES4_CON_WORDLEN_NOTID) {
		unsigned int ws;

		switch (sample_width) {
		case 16:
			ws = IEC958_AES4_CON_WORDLEN_20_16;
			break;
		case 18:
			ws = IEC958_AES4_CON_WORDLEN_22_18;
			break;
		case 20:
			ws = IEC958_AES4_CON_WORDLEN_20_16 |
			     IEC958_AES4_CON_MAX_WORDLEN_24;
			break;
		case 24:
		case 32: /* Assume 24-bit width for 32-bit samples. */
			ws = IEC958_AES4_CON_WORDLEN_24_20 |
			     IEC958_AES4_CON_MAX_WORDLEN_24;
			break;

		default:
			return -EINVAL;
		}

		cs[4] &= ~IEC958_AES4_CON_WORDLEN;
		cs[4] |= ws;
	}

	return len;
}

/**
 * snd_pcm_fill_iec958_consumer - Fill consumer format IEC958 channel status
 * @runtime: pcm runtime structure with ->rate filled in
 * @cs: channel status buffer, at least four bytes
 * @len: length of channel status buffer
 *
 * Fill the unspecified bits in an IEC958 status bits array using the
 * parameters of the PCM runtime @runtime.
 *
 * Drivers may wish to tweak the contents of the buffer after its been
 * filled.
 *
 * Returns: length of buffer, or negative error code if something failed.
 */
int snd_pcm_fill_iec958_consumer(struct snd_pcm_runtime *runtime,
				 u8 *cs, size_t len)
{
	return fill_iec958_consumer(runtime->rate,
				    snd_pcm_format_width(runtime->format),
				    cs, len);
}
EXPORT_SYMBOL_GPL(snd_pcm_fill_iec958_consumer);

/**
 * snd_pcm_fill_iec958_consumer_hw_params - Fill consumer format IEC958 channel status
 * @params: the hw_params instance for extracting rate and sample format
 * @cs: channel status buffer, at least four bytes
 * @len: length of channel status buffer
 *
 * Fill the unspecified bits in an IEC958 status bits array using the
 * parameters of the PCM hardware parameters @params.
 *
 * Drivers may wish to tweak the contents of the buffer after its been
 * filled..
 *
 * Returns: length of buffer, or negative error code if something failed.
 */
int snd_pcm_fill_iec958_consumer_hw_params(struct snd_pcm_hw_params *params,
					   u8 *cs, size_t len)
{
	return fill_iec958_consumer(params_rate(params), params_width(params), cs, len);
}
EXPORT_SYMBOL_GPL(snd_pcm_fill_iec958_consumer_hw_params);

/**
 * snd_pcm_create_iec958_consumer - create consumer format IEC958 channel status
 * @runtime: pcm runtime structure with ->rate filled in
 * @cs: channel status buffer, at least four bytes
 * @len: length of channel status buffer
 *
 * Create the consumer format channel status data in @cs of maximum size
 * @len corresponding to the parameters of the PCM runtime @runtime.
 *
 * Drivers may wish to tweak the contents of the buffer after creation.
 *
 * Returns: length of buffer, or negative error code if something failed.
 */
int snd_pcm_create_iec958_consumer(struct snd_pcm_runtime *runtime, u8 *cs,
	size_t len)
{
	int ret;

	ret = snd_pcm_create_iec958_consumer_default(cs, len);
	if (ret < 0)
		return ret;

	return snd_pcm_fill_iec958_consumer(runtime, cs, len);
}
EXPORT_SYMBOL(snd_pcm_create_iec958_consumer);

/**
 * snd_pcm_create_iec958_consumer_hw_params - create IEC958 channel status
 * @params: the hw_params instance for extracting rate and sample format
 * @cs: channel status buffer, at least four bytes
 * @len: length of channel status buffer
 *
 * Create the consumer format channel status data in @cs of maximum size
 * @len corresponding to the parameters of the PCM runtime @runtime.
 *
 * Drivers may wish to tweak the contents of the buffer after creation.
 *
 * Returns: length of buffer, or negative error code if something failed.
 */
int snd_pcm_create_iec958_consumer_hw_params(struct snd_pcm_hw_params *params,
					     u8 *cs, size_t len)
{
	int ret;

	ret = snd_pcm_create_iec958_consumer_default(cs, len);
	if (ret < 0)
		return ret;

	return fill_iec958_consumer(params_rate(params), params_width(params), cs, len);
}
EXPORT_SYMBOL(snd_pcm_create_iec958_consumer_hw_params);
