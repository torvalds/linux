// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the seq_buf API
 *
 * Copyright (C) 2025, Google LLC.
 */

#include <kunit/test.h>
#include <linux/console.h>
#include <linux/seq_buf.h>
#include <linux/string.h>

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

static void seq_buf_putmem_hex_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 24);
	const u8 data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
#ifdef __BIG_ENDIAN
	const char *expected = "0001020304050607 0809 ";
#else
	const char *expected = "0706050403020100 0908 ";
#endif

	KUNIT_EXPECT_EQ(test, seq_buf_putmem_hex(&s, data, sizeof(data)), 0);
	KUNIT_EXPECT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), strlen(expected));
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), expected);
}

static void seq_buf_putmem_hex_overflow_test(struct kunit *test)
{
	DECLARE_SEQ_BUF(s, 20);
	const u8 data[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
#ifdef __BIG_ENDIAN
	const char *expected = "0001020304050607 ";
#else
	const char *expected = "0706050403020100 ";
#endif

	KUNIT_EXPECT_EQ(test, seq_buf_putmem_hex(&s, data, sizeof(data)), -1);
	KUNIT_EXPECT_TRUE(test, seq_buf_has_overflowed(&s));
	KUNIT_EXPECT_EQ(test, seq_buf_used(&s), 20);
	KUNIT_EXPECT_STREQ(test, seq_buf_str(&s), expected);
}

/*
 * Counters for the console that seq_buf_do_printk_test() registers while it
 * runs. Only records carrying the marker are counted, so unrelated kernel
 * messages do not disturb them.
 *
 * An empty record carries nothing to recognize it by, so count one only
 * where the flaw puts it: directly after a record of ours, with nothing in
 * between. That still misreads a bare line feed printed by another CPU in
 * exactly that gap, but no longer counts one printed at any point while the
 * console happens to be registered.
 */
#define SEQ_BUF_PRINTK_MARKER	"sbdpkx"

static unsigned int seq_buf_printk_marked;
static unsigned int seq_buf_printk_empty;
static bool seq_buf_printk_last_was_ours;

static void seq_buf_printk_capture(struct console *con,
				   const char *s, unsigned int count)
{
	const char *text = s;
	const char *prefix;

	/*
	 * Skip what printk() puts in front of the message: a timestamp,
	 * and the caller id as well under CONFIG_PRINTK_CALLER, so strip
	 * every bracketed group rather than just the first.
	 */
	while (count && text[0] == '[') {
		prefix = memchr(text, ']', count);
		if (!prefix)
			break;
		count -= prefix + 1 - text;
		text = prefix + 1;
		if (count && text[0] == ' ') {
			text++;
			count--;
		}
	}

	if (strnstr(text, SEQ_BUF_PRINTK_MARKER, count)) {
		seq_buf_printk_marked++;
		seq_buf_printk_last_was_ours = true;
		return;
	}

	if (seq_buf_printk_last_was_ours &&
	    (count == 0 || (count == 1 && text[0] == '\n')))
		seq_buf_printk_empty++;

	seq_buf_printk_last_was_ours = false;
}

static void seq_buf_printk_run(struct console *capture, struct seq_buf *s)
{
	seq_buf_printk_marked = 0;
	seq_buf_printk_empty = 0;
	seq_buf_printk_last_was_ours = false;

	/*
	 * register_console() will not take an unmatched console without
	 * CON_ENABLED, and unregister_console() clears it, so set it on
	 * every run to keep the test repeatable.
	 */
	capture->flags = CON_ENABLED;
	register_console(capture);
	seq_buf_do_printk(s, KERN_INFO);
	unregister_console(capture);
}

static void seq_buf_do_printk_test(struct kunit *test)
{
	/*
	 * A registered console is a global object: printk() reaches it
	 * through the console list from any CPU, and the console code writes
	 * back into it, so keep it out of this function's stack frame the
	 * way every other console in the tree does.
	 */
	static struct console capture = {
		.name = "sbufcap",
		.write = seq_buf_printk_capture,
		.index = -1,
	};
	DECLARE_SEQ_BUF(s, 8);
	DECLARE_SEQ_BUF(t, 16);
	DECLARE_SEQ_BUF(u, 8);

	/*
	 * Fill the buffer exactly, so that the NUL takes the place of the
	 * last byte and the string ends with the line feed before it.
	 */
	seq_buf_puts(&s, SEQ_BUF_PRINTK_MARKER);
	seq_buf_putc(&s, '\n');
	seq_buf_putc(&s, '!');
	KUNIT_ASSERT_FALSE(test, seq_buf_has_overflowed(&s));
	KUNIT_ASSERT_EQ(test, seq_buf_used(&s), 8);
	KUNIT_ASSERT_EQ(test, strlen(seq_buf_str(&s)), 7);

	seq_buf_printk_run(&capture, &s);

	/* The one line that was written, and nothing after it. */
	KUNIT_EXPECT_EQ(test, seq_buf_printk_marked, 1);
	KUNIT_EXPECT_EQ(test, seq_buf_printk_empty, 0);

	/* Check that lines without a trailing newline are shown. */
	seq_buf_puts(&t, SEQ_BUF_PRINTK_MARKER "\n" SEQ_BUF_PRINTK_MARKER);
	KUNIT_ASSERT_FALSE(test, seq_buf_has_overflowed(&t));

	seq_buf_printk_run(&capture, &t);

	KUNIT_EXPECT_EQ(test, seq_buf_printk_marked, 2);
	KUNIT_EXPECT_EQ(test, seq_buf_printk_empty, 0);

	/*
	 * The buffer above was exactly full, where "len" equals the size. A
	 * buffer that actually overflowed reaches the same bug by the other
	 * route the old test had, with "len" one past the size.
	 */
	seq_buf_puts(&u, SEQ_BUF_PRINTK_MARKER "\n");
	KUNIT_EXPECT_EQ(test, seq_buf_puts(&u, "yy"), -1);
	KUNIT_ASSERT_TRUE(test, seq_buf_has_overflowed(&u));
	KUNIT_ASSERT_EQ(test, u.len, u.size + 1);

	seq_buf_printk_run(&capture, &u);

	KUNIT_EXPECT_EQ(test, seq_buf_printk_marked, 1);
	KUNIT_EXPECT_EQ(test, seq_buf_printk_empty, 0);
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
	KUNIT_CASE(seq_buf_putmem_hex_test),
	KUNIT_CASE(seq_buf_putmem_hex_overflow_test),
	KUNIT_CASE(seq_buf_do_printk_test),
	{}
};

static struct kunit_suite seq_buf_test_suite = {
	.name = "seq_buf",
	.test_cases = seq_buf_test_cases,
};

kunit_test_suite(seq_buf_test_suite);

MODULE_DESCRIPTION("Runtime test cases for seq_buf string API");
MODULE_LICENSE("GPL");
