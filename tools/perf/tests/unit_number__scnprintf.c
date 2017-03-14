#include <linux/compiler.h>
#include <linux/types.h>
#include "tests.h"
#include "util.h"
#include "debug.h"

int test__unit_number__scnprint(int subtest __maybe_unused)
{
	struct {
		u64		 n;
		const char	*str;
	} test[] = {
		{ 1,			"1B"	},
		{ 10*1024,		"10K"	},
		{ 20*1024*1024,		"20M"	},
		{ 30*1024*1024*1024ULL,	"30G"	},
		{ 0,			"0B"	},
		{ 0,			NULL	},
	};
	unsigned i = 0;

	while (test[i].str) {
		char buf[100];

		unit_number__scnprintf(buf, sizeof(buf), test[i].n);

		pr_debug("n %" PRIu64 ", str '%s', buf '%s'\n",
			 test[i].n, test[i].str, buf);

		if (strcmp(test[i].str, buf))
			return TEST_FAIL;

		i++;
	}

	return TEST_OK;
}
