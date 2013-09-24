#include "util.h"

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#include "machine.h"
#include "symbol.h"
#include "tests.h"

static char *test_file(int size)
{
	static char buf_templ[] = "/tmp/test-XXXXXX";
	char *templ = buf_templ;
	int fd, i;
	unsigned char *buf;

	fd = mkstemp(templ);
	if (fd < 0) {
		perror("mkstemp failed");
		return NULL;
	}

	buf = malloc(size);
	if (!buf) {
		close(fd);
		return NULL;
	}

	for (i = 0; i < size; i++)
		buf[i] = (unsigned char) ((int) i % 10);

	if (size != write(fd, buf, size))
		templ = NULL;

	close(fd);
	return templ;
}

#define TEST_FILE_SIZE (DSO__DATA_CACHE_SIZE * 20)

struct test_data_offset {
	off_t offset;
	u8 data[10];
	int size;
};

struct test_data_offset offsets[] = {
	/* Fill first cache page. */
	{
		.offset = 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read first cache page. */
	{
		.offset = 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Fill cache boundary pages. */
	{
		.offset = DSO__DATA_CACHE_SIZE - DSO__DATA_CACHE_SIZE % 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read cache boundary pages. */
	{
		.offset = DSO__DATA_CACHE_SIZE - DSO__DATA_CACHE_SIZE % 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Fill final cache page. */
	{
		.offset = TEST_FILE_SIZE - 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read final cache page. */
	{
		.offset = TEST_FILE_SIZE - 10,
		.data   = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 },
		.size   = 10,
	},
	/* Read final cache page. */
	{
		.offset = TEST_FILE_SIZE - 3,
		.data   = { 7, 8, 9, 0, 0, 0, 0, 0, 0, 0 },
		.size   = 3,
	},
};

int test__dso_data(void)
{
	struct machine machine;
	struct dso *dso;
	char *file = test_file(TEST_FILE_SIZE);
	size_t i;

	TEST_ASSERT_VAL("No test file", file);

	memset(&machine, 0, sizeof(machine));

	dso = dso__new((const char *)file);

	/* Basic 10 bytes tests. */
	for (i = 0; i < ARRAY_SIZE(offsets); i++) {
		struct test_data_offset *data = &offsets[i];
		ssize_t size;
		u8 buf[10];

		memset(buf, 0, 10);
		size = dso__data_read_offset(dso, &machine, data->offset,
				     buf, 10);

		TEST_ASSERT_VAL("Wrong size", size == data->size);
		TEST_ASSERT_VAL("Wrong data", !memcmp(buf, data->data, 10));
	}

	/* Read cross multiple cache pages. */
	{
		ssize_t size;
		int c;
		u8 *buf;

		buf = malloc(TEST_FILE_SIZE);
		TEST_ASSERT_VAL("ENOMEM\n", buf);

		/* First iteration to fill caches, second one to read them. */
		for (c = 0; c < 2; c++) {
			memset(buf, 0, TEST_FILE_SIZE);
			size = dso__data_read_offset(dso, &machine, 10,
						     buf, TEST_FILE_SIZE);

			TEST_ASSERT_VAL("Wrong size",
				size == (TEST_FILE_SIZE - 10));

			for (i = 0; i < (size_t)size; i++)
				TEST_ASSERT_VAL("Wrong data",
					buf[i] == (i % 10));
		}

		free(buf);
	}

	dso__delete(dso);
	unlink(file);
	return 0;
}
