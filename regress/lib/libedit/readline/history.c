/*	$OpenBSD: history.c,v 1.7 2017/07/05 15:31:45 bluhm Exp $	*/
/*
 * Copyright (c) 2016 Bastian Maerkisch <bmaerkisch@web.de>
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute these tests for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THESE TESTS ARE PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef READLINE
#include <readline/history.h>
#else
#include <readline/readline.h>
#endif


/*
 * Test infrastructure function.
 * At the beginning of each test, call as "msg(__func__);".
 * Upon failure, call as "msg(fmt, ...);".
 * At the end of each test, call as "return msg(NULL);".
 */
int
msg(const char *fmt, ...)
{
	static const char *testname = NULL;
	static int failed = 0;
	va_list ap;

	if (testname == NULL) {
		using_history();
		unstifle_history();
		testname = fmt;
		return 0;
	}

	if (fmt == NULL) {
		clear_history();
		unstifle_history();
		testname = NULL;
		if (failed == 0)
			return 0;
		failed = 0;
		return 1;
	}

	fprintf(stderr, "%s: ", testname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	failed = 1;
	return 1;
}

void
check_current(const char *descr, const char *want)
{
	HIST_ENTRY *he;

	he = current_history();

	if (want == NULL) {
		if (he != NULL)
			msg("Failed to move beyond the newest entry.");
		return;
	}

	if (he == NULL)
		msg("%s is NULL.", descr);
	else if (strcmp(he->line, want) != 0)
		msg("%s is \"%s\" instead of \"%s\".", descr, he->line, want);
}

void
check_get(int idx, const char *want)
{
	HIST_ENTRY *he;

	if ((he = history_get(history_base + idx)) == NULL)
		msg("Get %d+%d returned NULL.", history_base, idx);
	else if (he->line == NULL)
		msg("Get %d+%d returned line == NULL.", history_base, idx);
	else if (strcmp(he->line, want) != 0)
		msg("Get %d+%d returned \"%s\" instead of \"%s\".",
		    history_base, idx, he->line, want);
}


/* Fails if previous and next are interchanged. */
int
test_movement_direction(void)
{
	msg(__func__);
	add_history("111");
	add_history("222");

	while (previous_history() != NULL);
	check_current("Oldest entry", "111");

	/*
	 * Move to the most recent end of the history.
	 * This moves past the newest entry.
	 */
	while (next_history() != NULL);
	check_current(NULL, NULL);

	return msg(NULL);
}


/* Fails if the position is counted from the recent end. */
int
test_where(void)
{
	int		 ret;

	msg(__func__);

	/*
	 * Adding four elements since set_pos(0) doesn't work
	 * for some versions of libedit.
	 */
	add_history("111");
	add_history("222");
	add_history("333");
	add_history("444");

	/* Set the pointer to the element "222". */
	history_set_pos(1);
	if ((ret = where_history()) != 1)
		msg("Where returned %d instead of 1.", ret);

	return msg(NULL);
}


/*
 * Fails if the argument of history_get()
 * does not refer to the zero-based index + history_base.
 */
int
test_get(void)
{
	msg(__func__);
	add_history("111");
	add_history("222");
	add_history("333");
	add_history("444");

	/* Try to retrieve second element. */
	check_get(1, "222");

	return msg(NULL);
}


/* Fails if set_pos returns 0 for success and -1 for failure. */
int
test_set_pos_return_values(void)
{
	int		 ret;

	msg(__func__);
	add_history("111");
	add_history("222");

	/* This should fail. */
	if ((ret = history_set_pos(-1)) != 0)
		msg("Set_pos(-1) returned %d instead of 0.", ret);

	/*
	 * This should succeed.
	 * Note that we do not use the index 0 here, since that
	 * actually fails for some versions of libedit.
	 */
	if ((ret = history_set_pos(1)) != 1)
		msg("Set_pos(1) returned %d instead of 1.", ret);

	return msg(NULL);
}


/* Fails if the index is one-based. */
int
test_set_pos_index(void)
{
	msg(__func__);
	add_history("111");
	add_history("222");

	/* Do not test return value here since that might be broken, too. */
	history_set_pos(0);
	check_current("Entry 0", "111");

	history_set_pos(1);
	check_current("Entry 1", "222");

	return msg(NULL);
}


/* Fails if remove does not renumber. */
int
test_remove(void)
{
	msg(__func__);
	add_history("111");
	add_history("222");
	add_history("333");
	add_history("444");

	/* Remove the second item "222"; the index is zero-based. */
	remove_history(1);

	/*
	 * Try to get the new second element using history_get.
	 * The argument of get is based on history_base.
	 */
	check_get(1, "333");

	/*
	 * Try to get the second element using set_pos/current.
	 * The index is zero-based.
	 */
	history_set_pos(1);
	check_current("Entry 1", "333");

	/* Remove the new second item "333". */
	remove_history(1);
	check_get(1, "444");

	return msg(NULL);
}


/* Fails if stifle doesn't discard existing entries. */
int
test_stifle_size(void)
{
	msg(__func__);
	add_history("111");
	add_history("222");
	add_history("333");

	/* Reduce the size of the history. */
	stifle_history(2);
	if (history_length != 2)
		msg("Length is %d instead of 2.", history_length);

	return msg(NULL);
}


/* Fails if add doesn't increase history_base if the history is full. */
int
test_stifle_base(void)
{
	msg(__func__);
	stifle_history(2);

	/* Add one more than the maximum size. */
	add_history("111");
	add_history("222");
	add_history("333");

	/* The history_base should have changed. */
	if (history_base != 2)
		msg("Base is %d instead of 2.", history_base);

	return msg(NULL);
}


int
main(void)
{
	int		 fail = 0;

	fail += test_movement_direction();
	fail += test_where();
	fail += test_get();
	fail += test_set_pos_return_values();
	fail += test_set_pos_index();
	fail += test_remove();
	fail += test_stifle_size();
	fail += test_stifle_base();

	if (fail)
		errx(1, "%d test(s) failed.", fail);
	return 0;
}
