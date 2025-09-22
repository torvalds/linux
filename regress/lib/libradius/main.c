#include "incs.h"

#define TEST_MAX 100

struct test_entry {
	testfunc func;
	const char *name;
} entries[TEST_MAX];

int ntests = 0;

int test_entry_cmp(const void *a, const void *b)
{
	return strcmp(
		((struct test_entry *)a)->name,
		((struct test_entry *)b)->name);
}

int main(void)
{
	srandom_deterministic(time(NULL));

	qsort(entries, ntests, sizeof(struct test_entry), test_entry_cmp);

	for (int i = 0; i < ntests; i++) {
		fprintf(stderr, "running test %s\n", entries[i].name);
		entries[i].func();
	}

	fprintf(stderr, "tests are successfully completed.\n");
	return 0;
}

void check_failed(const char *expr, const char *file, int line)
{
	fprintf(stderr, "CHECK FAILED: %s at file %s line %d\n", expr, file, line);
	exit(1);
}

void add_test(testfunc fn, const char *name)
{
	entries[ntests].func = fn;
	entries[ntests].name = name;
	ntests++;
}
