#include <linux/compiler.h>
#include "tests.h"
#include "debug.h"
#include "util.h"

int test__is_printable_array(int subtest __maybe_unused)
{
	char buf1[] = { 'k', 'r', 4, 'v', 'a', 0 };
	char buf2[] = { 'k', 'r', 'a', 'v', 4, 0 };
	struct {
		char		*buf;
		unsigned int	 len;
		int		 ret;
	} t[] = {
		{ (char *) "krava",	sizeof("krava"),	1 },
		{ (char *) "krava",	sizeof("krava") - 1,	0 },
		{ (char *) "",		sizeof(""),		1 },
		{ (char *) "",		0,			0 },
		{ NULL,			0,			0 },
		{ buf1,			sizeof(buf1),		0 },
		{ buf2,			sizeof(buf2),		0 },
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(t); i++) {
		int ret;

		ret = is_printable_array((char *) t[i].buf, t[i].len);
		if (ret != t[i].ret) {
			pr_err("failed: test %u\n", i);
			return TEST_FAIL;
		}
	}

	return TEST_OK;
}
