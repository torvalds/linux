#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "test.h"

/* circular log buffer */

static char log_buf[0x80000];
static char *head = log_buf, *tail = log_buf;

static inline void advance(char **ptr)
{
	if ((unsigned int)(*ptr - log_buf) >= sizeof(log_buf))
		*ptr = log_buf;
	else
		*ptr = *ptr + 1;
}

static void log_char(char c)
{
	*tail = c;
	advance(&tail);
	if (tail == head)
		advance(&head);
}

char *lkl_test_get_log(void)
{
	unsigned int size = 0;
	char *i, *j, *log;

	i = head;
	while (i != tail) {
		size++;
		advance(&i);
	}

	log = malloc(size + 1);
	if (!log)
		return log;

	i = head;
	j = log;
	while (i != tail) {
		*j++ = *i;
		advance(&i);
	}
	*i = 0;

	return log;
}

static void print_log(void)
{
	char last;

	printf(" log: |\n");
	last = '\n';
	while (head != tail) {
		if (last == '\n')
			printf("  ");
		last = *head;
		putchar(last);
		advance(&head);
	}
	if (last != '\n')
		putchar('\n');
}

int lkl_test_run(const struct lkl_test *tests, int nr, const char *fmt, ...)
{
	int i, ret, status = TEST_SUCCESS;
	clock_t start, stop;
	char name[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(name, sizeof(name), fmt, args);
	va_end(args);

	printf("1..%d # %s\n", nr, name);
	for (i = 1; i <= nr; i++) {
		const struct lkl_test *t = &tests[i-1];
		unsigned long delta_us;

		printf("* %d %s\n", i, t->name);
		fflush(stdout);

		start = clock();

		ret = t->fn(t->arg1, t->arg2, t->arg3);

		stop = clock();

		switch (ret) {
		case TEST_SUCCESS:
			printf("ok %d %s\n", i, t->name);
			break;
		case TEST_SKIP:
			printf("ok %d %s # SKIP\n", i, t->name);
			break;
		case TEST_BAILOUT:
			status = TEST_BAILOUT;
			/* fall through */
		case TEST_FAILURE:
		default:
			if (status != TEST_BAILOUT)
				status = TEST_FAILURE;
			printf("not ok %d %s\n", i, t->name);
		}

		printf(" ---\n");
		delta_us = (stop - start) * 1000000 / CLOCKS_PER_SEC;
		printf(" time_us: %ld\n", delta_us);
		print_log();
		printf(" ...\n");

		if (status == TEST_BAILOUT) {
			printf("Bail out!\n");
			return TEST_FAILURE;
		}

		fflush(stdout);
	}

	return status;
}


void lkl_test_log(const char *str, int len)
{
	while (len--)
		log_char(*(str++));
}

int lkl_test_logf(const char *fmt, ...)
{
	char tmp[1024], *c;
	va_list args;
	unsigned int n;

	va_start(args, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, args);
	va_end(args);

	for (c = tmp; *c != 0; c++)
		log_char(*c);

	return n > sizeof(tmp) ? sizeof(tmp) : n;
}
