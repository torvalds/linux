#include <errno.h>
#include <inttypes.h>
#include <api/fs/tracing_path.h>
#include <linux/err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "thread_map.h"
#include "evsel.h"
#include "debug.h"
#include "tests.h"

int test__openat_syscall_event(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	int err = -1, fd;
	struct perf_evsel *evsel;
	unsigned int nr_openat_calls = 111, i;
	struct thread_map *threads = thread_map__new(-1, getpid(), UINT_MAX);
	char sbuf[STRERR_BUFSIZE];
	char errbuf[BUFSIZ];

	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	evsel = perf_evsel__newtp("syscalls", "sys_enter_openat");
	if (IS_ERR(evsel)) {
		tracing_path__strerror_open_tp(errno, errbuf, sizeof(errbuf), "syscalls", "sys_enter_openat");
		pr_debug("%s\n", errbuf);
		goto out_thread_map_delete;
	}

	if (perf_evsel__open_per_thread(evsel, threads) < 0) {
		pr_debug("failed to open counter: %s, "
			 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_evsel_delete;
	}

	for (i = 0; i < nr_openat_calls; ++i) {
		fd = openat(0, "/etc/passwd", O_RDONLY);
		close(fd);
	}

	if (perf_evsel__read_on_cpu(evsel, 0, 0) < 0) {
		pr_debug("perf_evsel__read_on_cpu\n");
		goto out_close_fd;
	}

	if (perf_counts(evsel->counts, 0, 0)->val != nr_openat_calls) {
		pr_debug("perf_evsel__read_on_cpu: expected to intercept %d calls, got %" PRIu64 "\n",
			 nr_openat_calls, perf_counts(evsel->counts, 0, 0)->val);
		goto out_close_fd;
	}

	err = 0;
out_close_fd:
	perf_evsel__close_fd(evsel);
out_evsel_delete:
	perf_evsel__delete(evsel);
out_thread_map_delete:
	thread_map__put(threads);
	return err;
}
