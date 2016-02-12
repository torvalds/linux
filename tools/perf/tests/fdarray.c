#include <api/fd/array.h>
#include "util/debug.h"
#include "tests/tests.h"

static void fdarray__init_revents(struct fdarray *fda, short revents)
{
	int fd;

	fda->nr = fda->nr_alloc;

	for (fd = 0; fd < fda->nr; ++fd) {
		fda->entries[fd].fd	 = fda->nr - fd;
		fda->entries[fd].revents = revents;
	}
}

static int fdarray__fprintf_prefix(struct fdarray *fda, const char *prefix, FILE *fp)
{
	int printed = 0;

	if (!verbose)
		return 0;

	printed += fprintf(fp, "\n%s: ", prefix);
	return printed + fdarray__fprintf(fda, fp);
}

int test__fdarray__filter(int subtest __maybe_unused)
{
	int nr_fds, expected_fd[2], fd, err = TEST_FAIL;
	struct fdarray *fda = fdarray__new(5, 5);

	if (fda == NULL) {
		pr_debug("\nfdarray__new() failed!");
		goto out;
	}

	fdarray__init_revents(fda, POLLIN);
	nr_fds = fdarray__filter(fda, POLLHUP, NULL);
	if (nr_fds != fda->nr_alloc) {
		pr_debug("\nfdarray__filter()=%d != %d shouldn't have filtered anything",
			 nr_fds, fda->nr_alloc);
		goto out_delete;
	}

	fdarray__init_revents(fda, POLLHUP);
	nr_fds = fdarray__filter(fda, POLLHUP, NULL);
	if (nr_fds != 0) {
		pr_debug("\nfdarray__filter()=%d != %d, should have filtered all fds",
			 nr_fds, fda->nr_alloc);
		goto out_delete;
	}

	fdarray__init_revents(fda, POLLHUP);
	fda->entries[2].revents = POLLIN;
	expected_fd[0] = fda->entries[2].fd;

	pr_debug("\nfiltering all but fda->entries[2]:");
	fdarray__fprintf_prefix(fda, "before", stderr);
	nr_fds = fdarray__filter(fda, POLLHUP, NULL);
	fdarray__fprintf_prefix(fda, " after", stderr);
	if (nr_fds != 1) {
		pr_debug("\nfdarray__filter()=%d != 1, should have left just one event", nr_fds);
		goto out_delete;
	}

	if (fda->entries[0].fd != expected_fd[0]) {
		pr_debug("\nfda->entries[0].fd=%d != %d\n",
			 fda->entries[0].fd, expected_fd[0]);
		goto out_delete;
	}

	fdarray__init_revents(fda, POLLHUP);
	fda->entries[0].revents = POLLIN;
	expected_fd[0] = fda->entries[0].fd;
	fda->entries[3].revents = POLLIN;
	expected_fd[1] = fda->entries[3].fd;

	pr_debug("\nfiltering all but (fda->entries[0], fda->entries[3]):");
	fdarray__fprintf_prefix(fda, "before", stderr);
	nr_fds = fdarray__filter(fda, POLLHUP, NULL);
	fdarray__fprintf_prefix(fda, " after", stderr);
	if (nr_fds != 2) {
		pr_debug("\nfdarray__filter()=%d != 2, should have left just two events",
			 nr_fds);
		goto out_delete;
	}

	for (fd = 0; fd < 2; ++fd) {
		if (fda->entries[fd].fd != expected_fd[fd]) {
			pr_debug("\nfda->entries[%d].fd=%d != %d\n", fd,
				 fda->entries[fd].fd, expected_fd[fd]);
			goto out_delete;
		}
	}

	pr_debug("\n");

	err = 0;
out_delete:
	fdarray__delete(fda);
out:
	return err;
}

int test__fdarray__add(int subtest __maybe_unused)
{
	int err = TEST_FAIL;
	struct fdarray *fda = fdarray__new(2, 2);

	if (fda == NULL) {
		pr_debug("\nfdarray__new() failed!");
		goto out;
	}

#define FDA_CHECK(_idx, _fd, _revents)					   \
	if (fda->entries[_idx].fd != _fd) {				   \
		pr_debug("\n%d: fda->entries[%d](%d) != %d!",		   \
			 __LINE__, _idx, fda->entries[1].fd, _fd);	   \
		goto out_delete;					   \
	}								   \
	if (fda->entries[_idx].events != (_revents)) {			   \
		pr_debug("\n%d: fda->entries[%d].revents(%d) != %d!",	   \
			 __LINE__, _idx, fda->entries[_idx].fd, _revents); \
		goto out_delete;					   \
	}

#define FDA_ADD(_idx, _fd, _revents, _nr)				   \
	if (fdarray__add(fda, _fd, _revents) < 0) {			   \
		pr_debug("\n%d: fdarray__add(fda, %d, %d) failed!",	   \
			 __LINE__,_fd, _revents);			   \
		goto out_delete;					   \
	}								   \
	if (fda->nr != _nr) {						   \
		pr_debug("\n%d: fdarray__add(fda, %d, %d)=%d != %d",	   \
			 __LINE__,_fd, _revents, fda->nr, _nr);		   \
		goto out_delete;					   \
	}								   \
	FDA_CHECK(_idx, _fd, _revents)

	FDA_ADD(0, 1, POLLIN, 1);
	FDA_ADD(1, 2, POLLERR, 2);

	fdarray__fprintf_prefix(fda, "before growing array", stderr);

	FDA_ADD(2, 35, POLLHUP, 3);

	if (fda->entries == NULL) {
		pr_debug("\nfdarray__add(fda, 35, POLLHUP) should have allocated fda->pollfd!");
		goto out_delete;
	}

	fdarray__fprintf_prefix(fda, "after 3rd add", stderr);

	FDA_ADD(3, 88, POLLIN | POLLOUT, 4);

	fdarray__fprintf_prefix(fda, "after 4th add", stderr);

	FDA_CHECK(0, 1, POLLIN);
	FDA_CHECK(1, 2, POLLERR);
	FDA_CHECK(2, 35, POLLHUP);
	FDA_CHECK(3, 88, POLLIN | POLLOUT);

#undef FDA_ADD
#undef FDA_CHECK

	pr_debug("\n");

	err = 0;
out_delete:
	fdarray__delete(fda);
out:
	return err;
}
