#include <linux/compiler.h>
#include "../tests.h"

typedef struct _buf {
	char data1;
	char reserved[55];
	char data2;
} buf __attribute__((aligned(64)));

static buf buf1 = {
	/* to have this in the data section */
	.reserved[0] = 1,
};

static int datasym(int argc __maybe_unused, const char **argv __maybe_unused)
{
	for (;;) {
		buf1.data1++;
		buf1.data2 += buf1.data1;
	}
	return 0;
}

DEFINE_WORKLOAD(datasym);
