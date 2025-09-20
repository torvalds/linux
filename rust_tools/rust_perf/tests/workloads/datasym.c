#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <linux/compiler.h>
#include "../tests.h"

typedef struct _buf {
	char data1;
	char reserved[55];
	char data2;
} buf __attribute__((aligned(64)));

/* volatile to try to avoid the compiler seeing reserved as unused. */
static volatile buf workload_datasym_buf1 = {
	/* to have this in the data section */
	.reserved[0] = 1,
};

static volatile sig_atomic_t done;

static void sighandler(int sig __maybe_unused)
{
	done = 1;
}

static int datasym(int argc, const char **argv)
{
	int sec = 1;

	if (argc > 0)
		sec = atoi(argv[0]);

	signal(SIGINT, sighandler);
	signal(SIGALRM, sighandler);
	alarm(sec);

	while (!done) {
		workload_datasym_buf1.data1++;
		if (workload_datasym_buf1.data1 == 123) {
			/*
			 * Add some 'noise' in the loop to work around errata
			 * 1694299 on Arm N1.
			 *
			 * Bias exists in SPE sampling which can cause the load
			 * and store instructions to be skipped entirely. This
			 * comes and goes randomly depending on the offset the
			 * linker places the datasym loop at in the Perf binary.
			 * With an extra branch in the middle of the loop that
			 * isn't always taken, the instruction stream is no
			 * longer a continuous repeating pattern that interacts
			 * badly with the bias.
			 */
			workload_datasym_buf1.data1++;
		}
		workload_datasym_buf1.data2 += workload_datasym_buf1.data1;
	}
	return 0;
}

DEFINE_WORKLOAD(datasym);
