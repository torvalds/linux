// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit test of proc sysctl.
 */

#include <kunit/test.h>
#include <linux/sysctl.h>

#define KUNIT_PROC_READ 0
#define KUNIT_PROC_WRITE 1

static int i_zero;
static int i_one_hundred = 100;

/*
 * Test that proc_dointvec will not try to use a NULL .data field even when the
 * length is non-zero.
 */
static void sysctl_test_api_dointvec_null_tbl_data(struct kunit *test)
{
	struct ctl_table null_data_table = {
		.procname = "foo",
		/*
		 * Here we are testing that proc_dointvec behaves correctly when
		 * we give it a NULL .data field. Normally this would point to a
		 * piece of memory where the value would be stored.
		 */
		.data		= NULL,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	/*
	 * proc_dointvec expects a buffer in user space, so we allocate one. We
	 * also need to cast it to __user so sparse doesn't get mad.
	 */
	void __user *buffer = (void __user *)kunit_kzalloc(test, sizeof(int),
							   GFP_USER);
	size_t len;
	loff_t pos;

	/*
	 * We don't care what the starting length is since proc_dointvec should
	 * not try to read because .data is NULL.
	 */
	len = 1234;
	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&null_data_table,
					       KUNIT_PROC_READ, buffer, &len,
					       &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);

	/*
	 * See above.
	 */
	len = 1234;
	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&null_data_table,
					       KUNIT_PROC_WRITE, buffer, &len,
					       &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);
}

/*
 * Similar to the previous test, we create a struct ctrl_table that has a .data
 * field that proc_dointvec cannot do anything with; however, this time it is
 * because we tell proc_dointvec that the size is 0.
 */
static void sysctl_test_api_dointvec_table_maxlen_unset(struct kunit *test)
{
	int data = 0;
	struct ctl_table data_maxlen_unset_table = {
		.procname = "foo",
		.data		= &data,
		/*
		 * So .data is no longer NULL, but we tell proc_dointvec its
		 * length is 0, so it still shouldn't try to use it.
		 */
		.maxlen		= 0,
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	void __user *buffer = (void __user *)kunit_kzalloc(test, sizeof(int),
							   GFP_USER);
	size_t len;
	loff_t pos;

	/*
	 * As before, we don't care what buffer length is because proc_dointvec
	 * cannot do anything because its internal .data buffer has zero length.
	 */
	len = 1234;
	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&data_maxlen_unset_table,
					       KUNIT_PROC_READ, buffer, &len,
					       &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);

	/*
	 * See previous comment.
	 */
	len = 1234;
	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&data_maxlen_unset_table,
					       KUNIT_PROC_WRITE, buffer, &len,
					       &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);
}

/*
 * Here we provide a valid struct ctl_table, but we try to read and write from
 * it using a buffer of zero length, so it should still fail in a similar way as
 * before.
 */
static void sysctl_test_api_dointvec_table_len_is_zero(struct kunit *test)
{
	int data = 0;
	/* Good table. */
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	void __user *buffer = (void __user *)kunit_kzalloc(test, sizeof(int),
							   GFP_USER);
	/*
	 * However, now our read/write buffer has zero length.
	 */
	size_t len = 0;
	loff_t pos;

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_READ, buffer,
					       &len, &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_WRITE, buffer,
					       &len, &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);
}

/*
 * Test that proc_dointvec refuses to read when the file position is non-zero.
 */
static void sysctl_test_api_dointvec_table_read_but_position_set(
		struct kunit *test)
{
	int data = 0;
	/* Good table. */
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	void __user *buffer = (void __user *)kunit_kzalloc(test, sizeof(int),
							   GFP_USER);
	/*
	 * We don't care about our buffer length because we start off with a
	 * non-zero file position.
	 */
	size_t len = 1234;
	/*
	 * proc_dointvec should refuse to read into the buffer since the file
	 * pos is non-zero.
	 */
	loff_t pos = 1;

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_READ, buffer,
					       &len, &pos));
	KUNIT_EXPECT_EQ(test, (size_t)0, len);
}

/*
 * Test that we can read a two digit number in a sufficiently size buffer.
 * Nothing fancy.
 */
static void sysctl_test_dointvec_read_happy_single_positive(struct kunit *test)
{
	int data = 0;
	/* Good table. */
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	size_t len = 4;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;
	/* Store 13 in the data field. */
	*((int *)table.data) = 13;

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_READ,
					       user_buffer, &len, &pos));
	KUNIT_ASSERT_EQ(test, (size_t)3, len);
	buffer[len] = '\0';
	/* And we read 13 back out. */
	KUNIT_EXPECT_STREQ(test, "13\n", buffer);
}

/*
 * Same as previous test, just now with negative numbers.
 */
static void sysctl_test_dointvec_read_happy_single_negative(struct kunit *test)
{
	int data = 0;
	/* Good table. */
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	size_t len = 5;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;
	*((int *)table.data) = -16;

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_READ,
					       user_buffer, &len, &pos));
	KUNIT_ASSERT_EQ(test, (size_t)4, len);
	buffer[len] = '\0';
	KUNIT_EXPECT_STREQ(test, "-16\n", (char *)buffer);
}

/*
 * Test that a simple positive write works.
 */
static void sysctl_test_dointvec_write_happy_single_positive(struct kunit *test)
{
	int data = 0;
	/* Good table. */
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	char input[] = "9";
	size_t len = sizeof(input) - 1;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;

	memcpy(buffer, input, len);

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_WRITE,
					       user_buffer, &len, &pos));
	KUNIT_EXPECT_EQ(test, sizeof(input) - 1, len);
	KUNIT_EXPECT_EQ(test, sizeof(input) - 1, (size_t)pos);
	KUNIT_EXPECT_EQ(test, 9, *((int *)table.data));
}

/*
 * Same as previous test, but now with negative numbers.
 */
static void sysctl_test_dointvec_write_happy_single_negative(struct kunit *test)
{
	int data = 0;
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	char input[] = "-9";
	size_t len = sizeof(input) - 1;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;

	memcpy(buffer, input, len);

	KUNIT_EXPECT_EQ(test, 0, proc_dointvec(&table, KUNIT_PROC_WRITE,
					       user_buffer, &len, &pos));
	KUNIT_EXPECT_EQ(test, sizeof(input) - 1, len);
	KUNIT_EXPECT_EQ(test, sizeof(input) - 1, (size_t)pos);
	KUNIT_EXPECT_EQ(test, -9, *((int *)table.data));
}

/*
 * Test that writing a value smaller than the minimum possible value is not
 * allowed.
 */
static void sysctl_test_api_dointvec_write_single_less_int_min(
		struct kunit *test)
{
	int data = 0;
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	size_t max_len = 32, len = max_len;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, max_len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;
	unsigned long abs_of_less_than_min = (unsigned long)INT_MAX
					     - (INT_MAX + INT_MIN) + 1;

	/*
	 * We use this rigmarole to create a string that contains a value one
	 * less than the minimum accepted value.
	 */
	KUNIT_ASSERT_LT(test,
			(size_t)snprintf(buffer, max_len, "-%lu",
					 abs_of_less_than_min),
			max_len);

	KUNIT_EXPECT_EQ(test, -EINVAL, proc_dointvec(&table, KUNIT_PROC_WRITE,
						     user_buffer, &len, &pos));
	KUNIT_EXPECT_EQ(test, max_len, len);
	KUNIT_EXPECT_EQ(test, 0, *((int *)table.data));
}

/*
 * Test that writing the maximum possible value works.
 */
static void sysctl_test_api_dointvec_write_single_greater_int_max(
		struct kunit *test)
{
	int data = 0;
	struct ctl_table table = {
		.procname = "foo",
		.data		= &data,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
		.extra1		= &i_zero,
		.extra2         = &i_one_hundred,
	};
	size_t max_len = 32, len = max_len;
	loff_t pos = 0;
	char *buffer = kunit_kzalloc(test, max_len, GFP_USER);
	char __user *user_buffer = (char __user *)buffer;
	unsigned long greater_than_max = (unsigned long)INT_MAX + 1;

	KUNIT_ASSERT_GT(test, greater_than_max, (unsigned long)INT_MAX);
	KUNIT_ASSERT_LT(test, (size_t)snprintf(buffer, max_len, "%lu",
					       greater_than_max),
			max_len);
	KUNIT_EXPECT_EQ(test, -EINVAL, proc_dointvec(&table, KUNIT_PROC_WRITE,
						     user_buffer, &len, &pos));
	KUNIT_ASSERT_EQ(test, max_len, len);
	KUNIT_EXPECT_EQ(test, 0, *((int *)table.data));
}

static struct kunit_case sysctl_test_cases[] = {
	KUNIT_CASE(sysctl_test_api_dointvec_null_tbl_data),
	KUNIT_CASE(sysctl_test_api_dointvec_table_maxlen_unset),
	KUNIT_CASE(sysctl_test_api_dointvec_table_len_is_zero),
	KUNIT_CASE(sysctl_test_api_dointvec_table_read_but_position_set),
	KUNIT_CASE(sysctl_test_dointvec_read_happy_single_positive),
	KUNIT_CASE(sysctl_test_dointvec_read_happy_single_negative),
	KUNIT_CASE(sysctl_test_dointvec_write_happy_single_positive),
	KUNIT_CASE(sysctl_test_dointvec_write_happy_single_negative),
	KUNIT_CASE(sysctl_test_api_dointvec_write_single_less_int_min),
	KUNIT_CASE(sysctl_test_api_dointvec_write_single_greater_int_max),
	{}
};

static struct kunit_suite sysctl_test_suite = {
	.name = "sysctl_test",
	.test_cases = sysctl_test_cases,
};

kunit_test_suite(sysctl_test_suite);
