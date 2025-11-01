// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the seq_buf API
 *
 * Copyright (C) 2025, Google LLC.
 */

#include <kunit/test.h>
#include <linux/seq_buf.h>

static void seq_buf_init_test(struct kunit *test)
{
	char buf[32];
	struct seq_buf s;

	seq_buf_init(&s, buf, sizeof(buf));

	KUNIT_EXPECT_EQ(test, s.size, 32);
	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_buffer_left(&s), 32);
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 0);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_declare_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 24);

	KUNIT_EXPECT_EQ(test, s.size, 24);
	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_buffer_left(&s), 24);
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 0);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_clear_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 128);

	seq_buf_puts(&s, "hello");
	KUNIT_EXPECT_EQ(test, s.len, 5);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello");

	seq_buf_clear(&s);

	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_puts_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 16);

	seq_buf_puts(&s, "hello");
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 5);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello");

	seq_buf_puts(&s, " world");
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 11);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello world");
}

static void seq_buf_puts_overflow_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 10);

	seq_buf_puts(&s, "123456789");
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 9);

	seq_buf_puts(&s, "0");
	KUNIT_EXPECT_TRUE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 10);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "123456789");

	seq_buf_clear(&s);
	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_putc_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 4);

	seq_buf_putc(&s, 'a');
	seq_buf_putc(&s, 'b');
	seq_buf_putc(&s, 'c');

	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 3);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "abc");

	seq_buf_putc(&s, 'd');
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 4);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "abc");

	seq_buf_putc(&s, 'e');
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 4);
	KUNIT_EXPECT_TRUE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "abc");

	seq_buf_clear(&s);
	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_printf_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 32);

	seq_buf_printf(&s, "hello %s", "world");
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 11);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello world");

	seq_buf_printf(&s, " %d", 123);
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 15);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello world 123");
}

static void seq_buf_printf_overflow_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 16);

	seq_buf_printf(&s, "%lu", 1234567890UL);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 10);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "1234567890");

	seq_buf_printf(&s, "%s", "abcdefghij");
	KUNIT_EXPECT_TRUE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 16);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "1234567890abcde");

	seq_buf_clear(&s);
	KUNIT_EXPECT_EQ(test, s.len, 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "");
}

static void seq_buf_get_buf_commit_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 16);
	char *buf;
	size_t len;

	len = seq_buf_get_buf(&s, &buf);
	KUNIT_EXPECT_EQ(test, len, 16);
	KUNIT_EXPECT_PTR_NE(test, buf, NULL);

	memcpy(buf, "hello", 5);
	seq_buf_commit(&s, 5);

	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 5);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello");

	len = seq_buf_get_buf(&s, &buf);
	KUNIT_EXPECT_EQ(test, len, 11);
	KUNIT_EXPECT_PTR_NE(test, buf, NULL);

	memcpy(buf, " worlds!", 8);
	seq_buf_commit(&s, 6);

	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 11);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), "hello world");

	len = seq_buf_get_buf(&s, &buf);
	KUNIT_EXPECT_EQ(test, len, 5);
	KUNIT_EXPECT_PTR_NE(test, buf, NULL);

	seq_buf_commit(&s, -1);
	KUNIT_EXPECT_TRUE(test, seq_buf_has_overflowed(&s));
}

static struct kunit_case seq_buf_test_cases[] = {
	KUNIT_CASE(seq_buf_init_test),
	KUNIT_CASE(seq_buf_declare_test),
	KUNIT_CASE(seq_buf_clear_test),
	KUNIT_CASE(seq_buf_puts_test),
	KUNIT_CASE(seq_buf_puts_overflow_test),
	KUNIT_CASE(seq_buf_putc_test),
	KUNIT_CASE(seq_buf_printf_test),
	KUNIT_CASE(seq_buf_printf_overflow_test),
	KUNIT_CASE(seq_buf_get_buf_commit_test),
	{}
};

static struct kunit_suite seq_buf_test_suite = {
	.name = "seq_buf",
	.test_cases = seq_buf_test_cases,
};

kunit_test_suite(seq_buf_test_suite);

MODULE_DESCRIPTION("Runtime test cases for seq_buf string API");
MODULE_LICENSE("GPL");
