#include <kunit/test.h>
#include <linux/slab.h>

#include "fp_printk.h"

static void should_return_2345_without_errors(struct kunit *test)
{
    char *destination = kmalloc(sizeof("23.45", GFP_KERNEL)); 
    fp_printk(2345, 2, destination);
    KUNIT_EXPECT_EQ(test, "23.45", destination);
    kfree(destination);
}

static void should_return_234_without_errors(struct kunit *test)
{
    char *destination = kmalloc(sizeof("2.45", GFP_KERNEL)); 
    fp_printk(245, 2, destination);
    KUNIT_EXPECT_EQ(test, "2.45", destination);
    kfree(destination);
}

static void should_return_2_without_errors_and_float_point(struct kunit *test)
{
    char *destination = kmalloc(sizeof("2", GFP_KERNEL)); 
    fp_printk(2, 0, destination);
    KUNIT_EXPECT_EQ(test, "2", destination);
    kfree(destination);
}

static void should_return_0_without_errors(struct kunit *test)
{
    char *destination = kmalloc(sizeof("0", GFP_KERNEL)); 
    fp_printk(0, 0, destination);
    KUNIT_EXPECT_EQ(test, "2", destination);
    kfree(destination);
}

static void should_return_12_without_errors_and_float_point(struct kunit *test)
{
    char *destination = kmalloc(sizeof("12", GFP_KERNEL)); 
    fp_printk(12, 0, destination);
    KUNIT_EXPECT_EQ(test, "2", destination);
    kfree(destination);
}

static void should_return_ERROR_if_parse_number_NULL(struct kunit *test)
{
    char *destination = kmalloc(sizeof("12", GFP_KERNEL)); 
    KUNIT_FAIL(test, "2", fp_printk(NULL, 0, destination));
    kfree(destination);
}

static void should_return_ERROR_if_parse_destination_NULL(struct kunit *test)
{
    char *destination = kmalloc(sizeof("12", GFP_KERNEL)); 
    KUNIT_FAIL(test, "0", fp_printk(12, 0, NULL));
    kfree(destination);
}

static struct kunit_case fp_printk_test_case[] = {
    KUNIT_CASE(should_return_ERROR_if_parse_destination_NULL),
    KUNIT_CASE(should_return_ERROR_if_parse_number_NULL),
    KUNIT_CASE(should_return_12_without_errors_and_float_point),
    KUNIT_CASE(should_return_0_without_errors),
    KUNIT_CASE(should_return_2_without_errors_and_float_point),
    KUNIT_CASE(should_return_234_without_errors),
    KUNIT_CASE(should_return_2345_without_errors),
    {/* sentinel */}
};

static struct kunit_suit fp_printk_test_suite = {
    .name = "fp_printk_test",
    .test_cases = fp_printk_test_case
};
kunit_test_suite(fp_printk_test_suite);

MODULE_AUTHOR("Guilherme Giacomo Simoes <trintaeoitogc@gmail.com>");
MODULE_LICENSE("GPL");