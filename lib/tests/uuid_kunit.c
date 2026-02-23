// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*
 * Test cases for lib/uuid.c module.
 */

#include <kunit/test.h>
#include <linux/uuid.h>

struct test_uuid_data {
	const char *uuid;
	guid_t le;
	uuid_t be;
};

static const struct test_uuid_data test_uuid_test_data[] = {
	{
		.uuid = "c33f4995-3701-450e-9fbf-206a2e98e576",
		.le = GUID_INIT(0xc33f4995, 0x3701, 0x450e, 0x9f, 0xbf, 0x20, 0x6a, 0x2e, 0x98, 0xe5, 0x76),
		.be = UUID_INIT(0xc33f4995, 0x3701, 0x450e, 0x9f, 0xbf, 0x20, 0x6a, 0x2e, 0x98, 0xe5, 0x76),
	},
	{
		.uuid = "64b4371c-77c1-48f9-8221-29f054fc023b",
		.le = GUID_INIT(0x64b4371c, 0x77c1, 0x48f9, 0x82, 0x21, 0x29, 0xf0, 0x54, 0xfc, 0x02, 0x3b),
		.be = UUID_INIT(0x64b4371c, 0x77c1, 0x48f9, 0x82, 0x21, 0x29, 0xf0, 0x54, 0xfc, 0x02, 0x3b),
	},
	{
		.uuid = "0cb4ddff-a545-4401-9d06-688af53e7f84",
		.le = GUID_INIT(0x0cb4ddff, 0xa545, 0x4401, 0x9d, 0x06, 0x68, 0x8a, 0xf5, 0x3e, 0x7f, 0x84),
		.be = UUID_INIT(0x0cb4ddff, 0xa545, 0x4401, 0x9d, 0x06, 0x68, 0x8a, 0xf5, 0x3e, 0x7f, 0x84),
	},
};

static const char * const test_uuid_wrong_data[] = {
	"c33f4995-3701-450e-9fbf206a2e98e576 ",	/* no hyphen(s) */
	"64b4371c-77c1-48f9-8221-29f054XX023b",	/* invalid character(s) */
	"0cb4ddff-a545-4401-9d06-688af53e",	/* not enough data */
};

static void uuid_test_guid_valid(struct kunit *test)
{
	unsigned int i;
	const struct test_uuid_data *data;
	guid_t le;

	for (i = 0; i < ARRAY_SIZE(test_uuid_test_data); i++) {
		data = &test_uuid_test_data[i];
		KUNIT_EXPECT_EQ(test, guid_parse(data->uuid, &le), 0);
		KUNIT_EXPECT_TRUE(test, guid_equal(&data->le, &le));
	}
}

static void uuid_test_uuid_valid(struct kunit *test)
{
	unsigned int i;
	const struct test_uuid_data *data;
	uuid_t be;

	for (i = 0; i < ARRAY_SIZE(test_uuid_test_data); i++) {
		data = &test_uuid_test_data[i];
		KUNIT_EXPECT_EQ(test, uuid_parse(data->uuid, &be), 0);
		KUNIT_EXPECT_TRUE(test, uuid_equal(&data->be, &be));
	}
}

static void uuid_test_guid_invalid(struct kunit *test)
{
	unsigned int i;
	const char *uuid;
	guid_t le;

	for (i = 0; i < ARRAY_SIZE(test_uuid_wrong_data); i++) {
		uuid = test_uuid_wrong_data[i];
		KUNIT_EXPECT_EQ(test, guid_parse(uuid, &le), -EINVAL);
	}
}

static void uuid_test_uuid_invalid(struct kunit *test)
{
	unsigned int i;
	const char *uuid;
	uuid_t be;

	for (i = 0; i < ARRAY_SIZE(test_uuid_wrong_data); i++) {
		uuid = test_uuid_wrong_data[i];
		KUNIT_EXPECT_EQ(test, uuid_parse(uuid, &be), -EINVAL);
	}
}

static struct kunit_case uuid_test_cases[] = {
	KUNIT_CASE(uuid_test_guid_valid),
	KUNIT_CASE(uuid_test_uuid_valid),
	KUNIT_CASE(uuid_test_guid_invalid),
	KUNIT_CASE(uuid_test_uuid_invalid),
	{},
};

static struct kunit_suite uuid_test_suite = {
	.name = "uuid",
	.test_cases = uuid_test_cases,
};

kunit_test_suite(uuid_test_suite);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Test cases for lib/uuid.c module");
MODULE_LICENSE("Dual BSD/GPL");
