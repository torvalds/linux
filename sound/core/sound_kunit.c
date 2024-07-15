// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Sound core KUnit test
 * Author: Ivan Orlov <ivan.orlov0322@gmail.com>
 */

#include <kunit/test.h>
#include <sound/core.h>
#include <sound/pcm.h>

#define SILENCE_BUFFER_MAX_FRAMES 260
#define SILENCE_BUFFER_SIZE (sizeof(u64) * SILENCE_BUFFER_MAX_FRAMES)
#define SILENCE(...) { __VA_ARGS__ }
#define DEFINE_FORMAT(fmt, pbits, wd, endianness, signd, silence_arr) {		\
	.format = SNDRV_PCM_FORMAT_##fmt, .physical_bits = pbits,		\
	.width = wd, .le = endianness, .sd = signd, .silence = silence_arr,	\
	.name = #fmt,								\
}

#define WRONG_FORMAT_1 (__force snd_pcm_format_t)((__force int)SNDRV_PCM_FORMAT_LAST + 1)
#define WRONG_FORMAT_2 (__force snd_pcm_format_t)-1

#define VALID_NAME "ValidName"
#define NAME_W_SPEC_CHARS "In%v@1id name"
#define NAME_W_SPACE "Test name"
#define NAME_W_SPACE_REMOVED "Testname"

#define TEST_FIRST_COMPONENT "Component1"
#define TEST_SECOND_COMPONENT "Component2"

struct snd_format_test_data {
	snd_pcm_format_t format;
	int physical_bits;
	int width;
	int le;
	int sd;
	unsigned char silence[8];
	unsigned char *name;
};

struct avail_test_data {
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t hw_ptr;
	snd_pcm_uframes_t appl_ptr;
	snd_pcm_uframes_t expected_avail;
};

static const struct snd_format_test_data valid_fmt[] = {
	DEFINE_FORMAT(S8, 8, 8, -1, 1, SILENCE()),
	DEFINE_FORMAT(U8, 8, 8, -1, 0, SILENCE(0x80)),
	DEFINE_FORMAT(S16_LE, 16, 16, 1, 1, SILENCE()),
	DEFINE_FORMAT(S16_BE, 16, 16, 0, 1, SILENCE()),
	DEFINE_FORMAT(U16_LE, 16, 16, 1, 0, SILENCE(0x00, 0x80)),
	DEFINE_FORMAT(U16_BE, 16, 16, 0, 0, SILENCE(0x80, 0x00)),
	DEFINE_FORMAT(S24_LE, 32, 24, 1, 1, SILENCE()),
	DEFINE_FORMAT(S24_BE, 32, 24, 0, 1, SILENCE()),
	DEFINE_FORMAT(U24_LE, 32, 24, 1, 0, SILENCE(0x00, 0x00, 0x80)),
	DEFINE_FORMAT(U24_BE, 32, 24, 0, 0, SILENCE(0x00, 0x80, 0x00, 0x00)),
	DEFINE_FORMAT(S32_LE, 32, 32, 1, 1, SILENCE()),
	DEFINE_FORMAT(S32_BE, 32, 32, 0, 1, SILENCE()),
	DEFINE_FORMAT(U32_LE, 32, 32, 1, 0, SILENCE(0x00, 0x00, 0x00, 0x80)),
	DEFINE_FORMAT(U32_BE, 32, 32, 0, 0, SILENCE(0x80, 0x00, 0x00, 0x00)),
	DEFINE_FORMAT(FLOAT_LE, 32, 32, 1, -1, SILENCE()),
	DEFINE_FORMAT(FLOAT_BE, 32, 32, 0, -1, SILENCE()),
	DEFINE_FORMAT(FLOAT64_LE, 64, 64, 1, -1, SILENCE()),
	DEFINE_FORMAT(FLOAT64_BE, 64, 64, 0, -1, SILENCE()),
	DEFINE_FORMAT(IEC958_SUBFRAME_LE, 32, 32, 1, -1, SILENCE()),
	DEFINE_FORMAT(IEC958_SUBFRAME_BE, 32, 32, 0, -1, SILENCE()),
	DEFINE_FORMAT(MU_LAW, 8, 8, -1, -1, SILENCE(0x7f)),
	DEFINE_FORMAT(A_LAW, 8, 8, -1, -1, SILENCE(0x55)),
	DEFINE_FORMAT(IMA_ADPCM, 4, 4, -1, -1, SILENCE()),
	DEFINE_FORMAT(G723_24, 3, 3, -1, -1, SILENCE()),
	DEFINE_FORMAT(G723_40, 5, 5, -1, -1, SILENCE()),
	DEFINE_FORMAT(DSD_U8, 8, 8, 1, 0, SILENCE(0x69)),
	DEFINE_FORMAT(DSD_U16_LE, 16, 16, 1, 0, SILENCE(0x69, 0x69)),
	DEFINE_FORMAT(DSD_U32_LE, 32, 32, 1, 0, SILENCE(0x69, 0x69, 0x69, 0x69)),
	DEFINE_FORMAT(DSD_U16_BE, 16, 16, 0, 0, SILENCE(0x69, 0x69)),
	DEFINE_FORMAT(DSD_U32_BE, 32, 32, 0, 0, SILENCE(0x69, 0x69, 0x69, 0x69)),
	DEFINE_FORMAT(S20_LE, 32, 20, 1, 1, SILENCE()),
	DEFINE_FORMAT(S20_BE, 32, 20, 0, 1, SILENCE()),
	DEFINE_FORMAT(U20_LE, 32, 20, 1, 0, SILENCE(0x00, 0x00, 0x08, 0x00)),
	DEFINE_FORMAT(U20_BE, 32, 20, 0, 0, SILENCE(0x00, 0x08, 0x00, 0x00)),
	DEFINE_FORMAT(S24_3LE, 24, 24, 1, 1, SILENCE()),
	DEFINE_FORMAT(S24_3BE, 24, 24, 0, 1, SILENCE()),
	DEFINE_FORMAT(U24_3LE, 24, 24, 1, 0, SILENCE(0x00, 0x00, 0x80)),
	DEFINE_FORMAT(U24_3BE, 24, 24, 0, 0, SILENCE(0x80, 0x00, 0x00)),
	DEFINE_FORMAT(S20_3LE, 24, 20, 1, 1, SILENCE()),
	DEFINE_FORMAT(S20_3BE, 24, 20, 0, 1, SILENCE()),
	DEFINE_FORMAT(U20_3LE, 24, 20, 1, 0, SILENCE(0x00, 0x00, 0x08)),
	DEFINE_FORMAT(U20_3BE, 24, 20, 0, 0, SILENCE(0x08, 0x00, 0x00)),
	DEFINE_FORMAT(S18_3LE, 24, 18, 1, 1, SILENCE()),
	DEFINE_FORMAT(S18_3BE, 24, 18, 0, 1, SILENCE()),
	DEFINE_FORMAT(U18_3LE, 24, 18, 1, 0, SILENCE(0x00, 0x00, 0x02)),
	DEFINE_FORMAT(U18_3BE, 24, 18, 0, 0, SILENCE(0x02, 0x00, 0x00)),
	DEFINE_FORMAT(G723_24_1B, 8, 3, -1, -1, SILENCE()),
	DEFINE_FORMAT(G723_40_1B, 8, 5, -1, -1, SILENCE()),
};

static void test_phys_format_size(struct kunit *test)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(valid_fmt); i++) {
		KUNIT_EXPECT_EQ(test, snd_pcm_format_physical_width(valid_fmt[i].format),
				valid_fmt[i].physical_bits);
	}

	KUNIT_EXPECT_EQ(test, snd_pcm_format_physical_width(WRONG_FORMAT_1), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_physical_width(WRONG_FORMAT_2), -EINVAL);
}

static void test_format_width(struct kunit *test)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(valid_fmt); i++) {
		KUNIT_EXPECT_EQ(test, snd_pcm_format_width(valid_fmt[i].format),
				valid_fmt[i].width);
	}

	KUNIT_EXPECT_EQ(test, snd_pcm_format_width(WRONG_FORMAT_1), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_width(WRONG_FORMAT_2), -EINVAL);
}

static void test_format_signed(struct kunit *test)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(valid_fmt); i++) {
		KUNIT_EXPECT_EQ(test, snd_pcm_format_signed(valid_fmt[i].format),
				valid_fmt[i].sd < 0 ? -EINVAL : valid_fmt[i].sd);
		KUNIT_EXPECT_EQ(test, snd_pcm_format_unsigned(valid_fmt[i].format),
				valid_fmt[i].sd < 0 ? -EINVAL : 1 - valid_fmt[i].sd);
	}

	KUNIT_EXPECT_EQ(test, snd_pcm_format_width(WRONG_FORMAT_1), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_width(WRONG_FORMAT_2), -EINVAL);
}

static void test_format_endianness(struct kunit *test)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(valid_fmt); i++) {
		KUNIT_EXPECT_EQ(test, snd_pcm_format_little_endian(valid_fmt[i].format),
				valid_fmt[i].le < 0 ? -EINVAL : valid_fmt[i].le);
		KUNIT_EXPECT_EQ(test, snd_pcm_format_big_endian(valid_fmt[i].format),
				valid_fmt[i].le < 0 ? -EINVAL : 1 - valid_fmt[i].le);
	}

	KUNIT_EXPECT_EQ(test, snd_pcm_format_little_endian(WRONG_FORMAT_1), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_little_endian(WRONG_FORMAT_2), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_big_endian(WRONG_FORMAT_1), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_big_endian(WRONG_FORMAT_2), -EINVAL);
}

static void _test_fill_silence(struct kunit *test, const struct snd_format_test_data *data,
			       u8 *buffer, size_t samples_count)
{
	size_t sample_bytes = data->physical_bits >> 3;
	u32 i;

	KUNIT_ASSERT_EQ(test, snd_pcm_format_set_silence(data->format, buffer, samples_count), 0);
	for (i = 0; i < samples_count * sample_bytes; i++)
		KUNIT_EXPECT_EQ(test, buffer[i], data->silence[i % sample_bytes]);
}

static void test_format_fill_silence(struct kunit *test)
{
	static const u32 buf_samples[] = { 10, 20, 32, 64, 129, SILENCE_BUFFER_MAX_FRAMES };
	u8 *buffer;
	u32 i, j;

	buffer = kunit_kzalloc(test, SILENCE_BUFFER_SIZE, GFP_KERNEL);

	for (i = 0; i < ARRAY_SIZE(buf_samples); i++) {
		for (j = 0; j < ARRAY_SIZE(valid_fmt); j++)
			_test_fill_silence(test, &valid_fmt[j], buffer, buf_samples[i]);
	}

	KUNIT_EXPECT_EQ(test, snd_pcm_format_set_silence(WRONG_FORMAT_1, buffer, 20), -EINVAL);
	KUNIT_EXPECT_EQ(test, snd_pcm_format_set_silence(SNDRV_PCM_FORMAT_LAST, buffer, 0), 0);
}

static snd_pcm_uframes_t calculate_boundary(snd_pcm_uframes_t buffer_size)
{
	snd_pcm_uframes_t boundary = buffer_size;

	while (boundary * 2 <= 0x7fffffffUL - buffer_size)
		boundary *= 2;
	return boundary;
}

static const struct avail_test_data p_avail_data[] = {
	/* buf_size + hw_ptr < appl_ptr => avail = buf_size + hw_ptr - appl_ptr + boundary */
	{ 128, 1000, 1129, 1073741824UL - 1 },
	/*
	 * buf_size + hw_ptr - appl_ptr >= boundary =>
	 * => avail = buf_size + hw_ptr - appl_ptr - boundary
	 */
	{ 128, 1073741824UL, 10, 118 },
	/* standard case: avail = buf_size + hw_ptr - appl_ptr */
	{ 128, 1000, 1001, 127 },
};

static void test_playback_avail(struct kunit *test)
{
	struct snd_pcm_runtime *r = kunit_kzalloc(test, sizeof(*r), GFP_KERNEL);
	u32 i;

	r->status = kunit_kzalloc(test, sizeof(*r->status), GFP_KERNEL);
	r->control = kunit_kzalloc(test, sizeof(*r->control), GFP_KERNEL);

	for (i = 0; i < ARRAY_SIZE(p_avail_data); i++) {
		r->buffer_size = p_avail_data[i].buffer_size;
		r->boundary = calculate_boundary(r->buffer_size);
		r->status->hw_ptr = p_avail_data[i].hw_ptr;
		r->control->appl_ptr = p_avail_data[i].appl_ptr;
		KUNIT_EXPECT_EQ(test, snd_pcm_playback_avail(r), p_avail_data[i].expected_avail);
	}
}

static const struct avail_test_data c_avail_data[] = {
	/* hw_ptr - appl_ptr < 0 => avail = hw_ptr - appl_ptr + boundary */
	{ 128, 1000, 1001, 1073741824UL - 1 },
	/* standard case: avail = hw_ptr - appl_ptr */
	{ 128, 1001, 1000, 1 },
};

static void test_capture_avail(struct kunit *test)
{
	struct snd_pcm_runtime *r = kunit_kzalloc(test, sizeof(*r), GFP_KERNEL);
	u32 i;

	r->status = kunit_kzalloc(test, sizeof(*r->status), GFP_KERNEL);
	r->control = kunit_kzalloc(test, sizeof(*r->control), GFP_KERNEL);

	for (i = 0; i < ARRAY_SIZE(c_avail_data); i++) {
		r->buffer_size = c_avail_data[i].buffer_size;
		r->boundary = calculate_boundary(r->buffer_size);
		r->status->hw_ptr = c_avail_data[i].hw_ptr;
		r->control->appl_ptr = c_avail_data[i].appl_ptr;
		KUNIT_EXPECT_EQ(test, snd_pcm_capture_avail(r), c_avail_data[i].expected_avail);
	}
}

static void test_card_set_id(struct kunit *test)
{
	struct snd_card *card = kunit_kzalloc(test, sizeof(*card), GFP_KERNEL);

	snd_card_set_id(card, VALID_NAME);
	KUNIT_EXPECT_STREQ(test, card->id, VALID_NAME);

	/* clear the first id character so we can set it again */
	card->id[0] = '\0';
	snd_card_set_id(card, NAME_W_SPEC_CHARS);
	KUNIT_EXPECT_STRNEQ(test, card->id, NAME_W_SPEC_CHARS);

	card->id[0] = '\0';
	snd_card_set_id(card, NAME_W_SPACE);
	kunit_info(test, "%s", card->id);
	KUNIT_EXPECT_STREQ(test, card->id, NAME_W_SPACE_REMOVED);
}

static void test_pcm_format_name(struct kunit *test)
{
	u32 i;
	const char *name;

	for (i = 0; i < ARRAY_SIZE(valid_fmt); i++) {
		name = snd_pcm_format_name(valid_fmt[i].format);
		KUNIT_ASSERT_NOT_NULL_MSG(test, name, "Don't have name for %s", valid_fmt[i].name);
		KUNIT_EXPECT_STREQ(test, name, valid_fmt[i].name);
	}

	KUNIT_ASSERT_STREQ(test, snd_pcm_format_name(WRONG_FORMAT_1), "Unknown");
	KUNIT_ASSERT_STREQ(test, snd_pcm_format_name(WRONG_FORMAT_2), "Unknown");
}

static void test_card_add_component(struct kunit *test)
{
	struct snd_card *card = kunit_kzalloc(test, sizeof(*card), GFP_KERNEL);

	snd_component_add(card, TEST_FIRST_COMPONENT);
	KUNIT_ASSERT_STREQ(test, card->components, TEST_FIRST_COMPONENT);

	snd_component_add(card, TEST_SECOND_COMPONENT);
	KUNIT_ASSERT_STREQ(test, card->components, TEST_FIRST_COMPONENT " " TEST_SECOND_COMPONENT);
}

static struct kunit_case sound_utils_cases[] = {
	KUNIT_CASE(test_phys_format_size),
	KUNIT_CASE(test_format_width),
	KUNIT_CASE(test_format_endianness),
	KUNIT_CASE(test_format_signed),
	KUNIT_CASE(test_format_fill_silence),
	KUNIT_CASE(test_playback_avail),
	KUNIT_CASE(test_capture_avail),
	KUNIT_CASE(test_card_set_id),
	KUNIT_CASE(test_pcm_format_name),
	KUNIT_CASE(test_card_add_component),
	{},
};

static struct kunit_suite sound_utils_suite = {
	.name = "sound-core-test",
	.test_cases = sound_utils_cases,
};

kunit_test_suite(sound_utils_suite);
MODULE_DESCRIPTION("Sound core KUnit test");
MODULE_AUTHOR("Ivan Orlov");
MODULE_LICENSE("GPL");
