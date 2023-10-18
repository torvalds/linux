#include <kunit/test.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/int2fpstr.h>

static void should_return_2345_without_errors(struct kunit *test)
{
	char *destination = int2fpstr(2345, 2);
	KUNIT_EXPECT_EQ(test, "23.45", destination);
	kfree(destination);
}

static void should_return_2_without_errors_and_float_point(struct kunit *test)
{
	char *destination = int2fpstr(2, 0);
	KUNIT_EXPECT_EQ(test, "2", destination);
	kfree(destination);
}

static void should_return_0_without_errors(struct kunit *test)
{ char *destination = int2fpstr(0, 0);
	KUNIT_EXPECT_EQ(test, "0", destination);
	kfree(destination);
}

static void should_return_12_without_errors_and_float_point(struct kunit *test)
{
	char *destination = int2fpstr(12, 0);
	KUNIT_EXPECT_EQ(test, "12", destination);
	kfree(destination);
}

static struct kunit_case int_to_fp_str_test_case[] = {
	KUNIT_CASE(should_return_ERROR_if_parse_number_NULL),
	KUNIT_CASE(should_return_12_without_errors_and_float_point),
	KUNIT_CASE(should_return_0_without_errors),
	KUNIT_CASE(should_return_2_without_errors_and_float_point),
	KUNIT_CASE(should_return_2345_without_errors),
	{ /* sentinel */ }
};

static struct kunit_suite int_to_fp_str_test = {
	.name = "int2fpstr",
	.test_cases = int_to_fp_str_test_case
};

kunit_test_suite(int_to_fp_str_test);

MODULE_AUTHOR("Guilherme Giacomo Simoes <trintaeoitogc@gmail.com>");
MODULE_LICENSE("GPL");
