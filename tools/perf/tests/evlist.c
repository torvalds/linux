#include "util/evlist.h"
#include "util/debug.h"
#include "tests/tests.h"

static void perf_evlist__init_pollfd(struct perf_evlist *evlist,
				     int nr_fds_alloc, short revents)
{
	int fd;

	evlist->nr_fds = nr_fds_alloc;

	for (fd = 0; fd < nr_fds_alloc; ++fd) {
		evlist->pollfd[fd].fd	   = nr_fds_alloc - fd;
		evlist->pollfd[fd].revents = revents;
	}
}

static int perf_evlist__fprintf_pollfd(struct perf_evlist *evlist,
				       const char *prefix, FILE *fp)
{
	int printed = 0, fd;

	if (!verbose)
		return 0;

	printed += fprintf(fp, "\n%s: %3d [ ", prefix, evlist->nr_fds);
	for (fd = 0; fd < evlist->nr_fds; ++fd)
		printed += fprintf(fp, "%s%d", fd ? ", " : "", evlist->pollfd[fd].fd);
	printed += fprintf(fp, " ]");
	return printed;
}

int test__perf_evlist__filter_pollfd(void)
{
	const int nr_fds_alloc = 5;
	int nr_fds, expected_fd[2], fd;
	struct pollfd pollfd[nr_fds_alloc];
	struct perf_evlist evlist_alloc = {
		.pollfd	= pollfd,
	}, *evlist = &evlist_alloc;

	perf_evlist__init_pollfd(evlist, nr_fds_alloc, POLLIN);
	nr_fds = perf_evlist__filter_pollfd(evlist, POLLHUP);
	if (nr_fds != nr_fds_alloc) {
		pr_debug("\nperf_evlist__filter_pollfd()=%d != %d shouldn't have filtered anything",
			 nr_fds, nr_fds_alloc);
		return TEST_FAIL;
	}

	perf_evlist__init_pollfd(evlist, nr_fds_alloc, POLLHUP);
	nr_fds = perf_evlist__filter_pollfd(evlist, POLLHUP);
	if (nr_fds != 0) {
		pr_debug("\nperf_evlist__filter_pollfd()=%d != %d, should have filtered all fds",
			 nr_fds, nr_fds_alloc);
		return TEST_FAIL;
	}

	perf_evlist__init_pollfd(evlist, nr_fds_alloc, POLLHUP);
	pollfd[2].revents = POLLIN;
	expected_fd[0] = pollfd[2].fd;

	pr_debug("\nfiltering all but pollfd[2]:");
	perf_evlist__fprintf_pollfd(evlist, "before", stderr);
	nr_fds = perf_evlist__filter_pollfd(evlist, POLLHUP);
	perf_evlist__fprintf_pollfd(evlist, " after", stderr);
	if (nr_fds != 1) {
		pr_debug("\nperf_evlist__filter_pollfd()=%d != 1, should have left just one event",
			 nr_fds);
		return TEST_FAIL;
	}

	if (pollfd[0].fd != expected_fd[0]) {
		pr_debug("\npollfd[0].fd=%d != %d\n", pollfd[0].fd, expected_fd[0]);
		return TEST_FAIL;
	}

	perf_evlist__init_pollfd(evlist, nr_fds_alloc, POLLHUP);
	pollfd[0].revents = POLLIN;
	expected_fd[0] = pollfd[0].fd;
	pollfd[3].revents = POLLIN;
	expected_fd[1] = pollfd[3].fd;

	pr_debug("\nfiltering all but (pollfd[0], pollfd[3]):");
	perf_evlist__fprintf_pollfd(evlist, "before", stderr);
	nr_fds = perf_evlist__filter_pollfd(evlist, POLLHUP);
	perf_evlist__fprintf_pollfd(evlist, " after", stderr);
	if (nr_fds != 2) {
		pr_debug("\nperf_evlist__filter_pollfd()=%d != 2, should have left just two events",
			 nr_fds);
		return TEST_FAIL;
	}

	for (fd = 0; fd < 2; ++fd) {
		if (pollfd[fd].fd != expected_fd[fd]) {
			pr_debug("\npollfd[%d].fd=%d != %d\n", fd, pollfd[fd].fd, expected_fd[fd]);
			return TEST_FAIL;
		}
	}

	pr_debug("\n");

	return 0;
}
