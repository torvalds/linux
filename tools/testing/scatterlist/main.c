#include <stdio.h>
#include <assert.h>

#include <linux/scatterlist.h>

#define MAX_PAGES (64)

struct test {
	int alloc_ret;
	unsigned num_pages;
	unsigned *pfn;
	unsigned *pfn_app;
	unsigned size;
	unsigned int max_seg;
	unsigned int expected_segments;
};

static void set_pages(struct page **pages, const unsigned *array, unsigned num)
{
	unsigned int i;

	assert(num < MAX_PAGES);
	for (i = 0; i < num; i++)
		pages[i] = (struct page *)(unsigned long)
			   ((1 + array[i]) * PAGE_SIZE);
}

#define pfn(...) (unsigned []){ __VA_ARGS__ }

static void fail(struct test *test, struct sg_table *st, const char *cond)
{
	unsigned int i;

	fprintf(stderr, "Failed on '%s'!\n\n", cond);

	printf("size = %u, max segment = %u, expected nents = %u\nst->nents = %u, st->orig_nents= %u\n",
	       test->size, test->max_seg, test->expected_segments, st->nents,
	       st->orig_nents);

	printf("%u input PFNs:", test->num_pages);
	for (i = 0; i < test->num_pages; i++)
		printf(" %x", test->pfn[i]);
	printf("\n");

	exit(1);
}

#define VALIDATE(cond, st, test) \
	if (!(cond)) \
		fail((test), (st), #cond);

int main(void)
{
	const unsigned int sgmax = UINT_MAX;
	struct test *test, tests[] = {
		{ -EINVAL, 1, pfn(0), NULL, PAGE_SIZE, 0, 1 },
		{ 0, 1, pfn(0), NULL, PAGE_SIZE, PAGE_SIZE + 1, 1 },
		{ 0, 1, pfn(0), NULL, PAGE_SIZE, sgmax, 1 },
		{ 0, 1, pfn(0), NULL, 1, sgmax, 1 },
		{ 0, 2, pfn(0, 1), NULL, 2 * PAGE_SIZE, sgmax, 1 },
		{ 0, 2, pfn(1, 0), NULL, 2 * PAGE_SIZE, sgmax, 2 },
		{ 0, 3, pfn(0, 1, 2), NULL, 3 * PAGE_SIZE, sgmax, 1 },
		{ 0, 3, pfn(0, 1, 2), NULL, 3 * PAGE_SIZE, sgmax, 1 },
		{ 0, 3, pfn(0, 1, 2), pfn(3, 4, 5), 3 * PAGE_SIZE, sgmax, 1 },
		{ 0, 3, pfn(0, 1, 2), pfn(4, 5, 6), 3 * PAGE_SIZE, sgmax, 2 },
		{ 0, 3, pfn(0, 2, 1), NULL, 3 * PAGE_SIZE, sgmax, 3 },
		{ 0, 3, pfn(0, 1, 3), NULL, 3 * PAGE_SIZE, sgmax, 2 },
		{ 0, 3, pfn(1, 2, 4), NULL, 3 * PAGE_SIZE, sgmax, 2 },
		{ 0, 3, pfn(1, 3, 4), NULL, 3 * PAGE_SIZE, sgmax, 2 },
		{ 0, 4, pfn(0, 1, 3, 4), NULL, 4 * PAGE_SIZE, sgmax, 2 },
		{ 0, 5, pfn(0, 1, 3, 4, 5), NULL, 5 * PAGE_SIZE, sgmax, 2 },
		{ 0, 5, pfn(0, 1, 3, 4, 6), NULL, 5 * PAGE_SIZE, sgmax, 3 },
		{ 0, 5, pfn(0, 1, 2, 3, 4), NULL, 5 * PAGE_SIZE, sgmax, 1 },
		{ 0, 5, pfn(0, 1, 2, 3, 4), NULL, 5 * PAGE_SIZE, 2 * PAGE_SIZE,
		  3 },
		{ 0, 6, pfn(0, 1, 2, 3, 4, 5), NULL, 6 * PAGE_SIZE,
		  2 * PAGE_SIZE, 3 },
		{ 0, 6, pfn(0, 2, 3, 4, 5, 6), NULL, 6 * PAGE_SIZE,
		  2 * PAGE_SIZE, 4 },
		{ 0, 6, pfn(0, 1, 3, 4, 5, 6), pfn(7, 8, 9, 10, 11, 12),
		  6 * PAGE_SIZE, 12 * PAGE_SIZE, 2 },
		{ 0, 0, NULL, NULL, 0, 0, 0 },
	};
	unsigned int i;

	for (i = 0, test = tests; test->expected_segments; test++, i++) {
		int left_pages = test->pfn_app ? test->num_pages : 0;
		struct page *pages[MAX_PAGES];
		struct sg_table st;
		struct scatterlist *sg = NULL;
		int ret;

		set_pages(pages, test->pfn, test->num_pages);

		if (test->pfn_app) {
			sg = sg_alloc_append_table_from_pages(
				&st, pages, test->num_pages, 0, test->size,
				test->max_seg, NULL, left_pages, GFP_KERNEL);
			assert(PTR_ERR_OR_ZERO(sg) == test->alloc_ret);
		} else {
			ret = sg_alloc_table_from_pages_segment(
				&st, pages, test->num_pages, 0, test->size,
				test->max_seg, GFP_KERNEL);
			assert(ret == test->alloc_ret);
		}

		if (test->alloc_ret)
			continue;

		if (test->pfn_app) {
			set_pages(pages, test->pfn_app, test->num_pages);
			sg = sg_alloc_append_table_from_pages(
				&st, pages, test->num_pages, 0, test->size,
				test->max_seg, sg, 0, GFP_KERNEL);

			assert(PTR_ERR_OR_ZERO(sg) == test->alloc_ret);
		}

		VALIDATE(st.nents == test->expected_segments, &st, test);
		if (!test->pfn_app)
			VALIDATE(st.orig_nents == test->expected_segments, &st,
				 test);

		sg_free_table(&st);
	}

	assert(i == (sizeof(tests) / sizeof(tests[0])) - 1);

	return 0;
}
