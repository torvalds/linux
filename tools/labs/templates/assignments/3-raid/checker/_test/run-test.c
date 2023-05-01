#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include "run-test.h"

/* Enable/disable exiting when program fails. */
//#define EXIT_IF_FAIL

static size_t test_index;
static size_t total_points = 0;

static void test_do_fail(size_t points)
{
	printf("failed  [  0/%3zu]\n", max_points);
#ifdef EXIT_IF_FAIL
	exit(EXIT_FAILURE);
#endif
}

static void test_do_pass(size_t points)
{
	total_points += points;
	printf("passed  [%3zu/%3zu]\n", points, max_points);
}

void basic_test(int condition)
{
	size_t i;
	char *description = test_array[test_index].description;
	size_t desc_len = strlen(description);
	size_t points = test_array[test_index].points;

	printf("(%3zu) %s", test_index + 1, description);
	for (i = 0; i < 56 - desc_len; i++)
		printf(".");
	if (condition)
		test_do_pass(points);
	else
		test_do_fail(points);
}

static void print_test_total(void)
{
	size_t i;

	for (i = 0; i < 62; i++)
		printf(" ");
	printf("Total:  [%3zu/%3zu]\n", total_points, max_points);
}

static void run_test(void)
{
	test_array[test_index].function();
}

int main(int argc, char **argv)
{
	size_t num_tests = get_num_tests();

	if (argc > 2) {
		fprintf(stderr, "Usage: %s [test_number]\n", argv[0]);
		fprintf(stderr, "  1 <= test_number <= %zu\n", num_tests);
		exit(EXIT_FAILURE);
	}

	/* Randomize time quantums. */
	srand(time(NULL));

	/* In case of no arguments run all tests. */
	if (argc == 1) {
		init_world();
		for (test_index = 0; test_index < num_tests; test_index++)
			run_test();
		print_test_total();
		cleanup_world();
		return 0;
	}

	/* If provided, argument is test index. */
	test_index = strtoul(argv[1], NULL, 10);
	if (errno == EINVAL || errno == ERANGE) {
		fprintf(stderr, "%s is not a number\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	if (test_index == 0 || test_index > num_tests) {
		fprintf(stderr, "Error: Test index is out of range "
				"(1 <= test_index <= %zu).\n", num_tests);
		exit(EXIT_FAILURE);
	}

	/* test_index is one less than what the user provides. */
	test_index--;

	/* Run test_index test. */
	init_world();
	run_test();
	cleanup_world();

	return 0;
}
