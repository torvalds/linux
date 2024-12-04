/*
 * Test cases for lib/uuid.c module.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
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
    "c33f4995-3701-450e-9fbf206a2e98e576 ",  /* no hyphen(s) */
    "64b4371c-77c1-48f9-8221-29f054XX023b",  /* invalid character(s) */
    "0cb4ddff-a545-4401-9d06-688af53e",      /* not enough data */
};

static unsigned total_tests __initdata;
static unsigned failed_tests __initdata;

static void __init test_uuid_failed(const char *prefix, bool wrong, bool be,
                                    const char *data, const char *actual)
{
    pr_err("%s test #%u %s %s data: '%s' (expected: '%s')\n",
           prefix,
           total_tests,
           wrong ? "passed on wrong" : "failed on",
           be ? "BE" : "LE",
           data,
           actual ? actual : "(null)");
    failed_tests++;
}

static void __init test_uuid_conversion(const char *uuid_str, const void *expected, bool is_be)
{
    void *parsed;
    if (is_be) {
        if (uuid_parse(uuid_str, (uuid_t *)parsed)) {
            pr_err("Failed to parse UUID: %s\n", uuid_str);
            test_uuid_failed("conversion", false, true, uuid_str, NULL);
        }
        if (!uuid_equal((uuid_t *)expected, (uuid_t *)parsed)) {
            test_uuid_failed("cmp", false, true, uuid_str, parsed);
        }
    } else {
        if (guid_parse(uuid_str, (guid_t *)parsed)) {
            pr_err("Failed to parse GUID: %s\n", uuid_str);
            test_uuid_failed("conversion", false, false, uuid_str, NULL);
        }
        if (!guid_equal((guid_t *)expected, (guid_t *)parsed)) {
            test_uuid_failed("cmp", false, false, uuid_str, parsed);
        }
    }
}

static void __init test_uuid_test(const struct test_uuid_data *data)
{
    /* Test for LE */
    total_tests++;
    test_uuid_conversion(data->uuid, &data->le, false);

    /* Test for BE */
    total_tests++;
    test_uuid_conversion(data->uuid, &data->be, true);
}

static void __init test_uuid_wrong(const char *data)
{
    guid_t le;
    uuid_t be;

    /* LE */
    total_tests++;
    if (!guid_parse(data, &le))
        test_uuid_failed("negative", true, false, data, NULL);

    /* BE */
    total_tests++;
    if (!uuid_parse(data, &be))
        test_uuid_failed("negative", true, true, data, NULL);
}

static int __init test_uuid_init(void)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(test_uuid_test_data); i++) {
        test_uuid_test(&test_uuid_test_data[i]);
    }

    for (i = 0; i < ARRAY_SIZE(test_uuid_wrong_data); i++) {
        test_uuid_wrong(test_uuid_wrong_data[i]);
    }

    if (failed_tests == 0)
        pr_info("All %u tests passed\n", total_tests);
    else
        pr_err("Failed %u out of %u tests\n", failed_tests, total_tests);

    return failed_tests ? -EINVAL : 0;
}

module_init(test_uuid_init);

static void __exit test_uuid_exit(void)
{
    /* No specific cleanup required */
}

module_exit(test_uuid_exit);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_DESCRIPTION("Test cases for lib/uuid.c module");
MODULE_LICENSE("Dual BSD/GPL");
