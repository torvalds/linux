// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <util/symbol.h>
#include <linux/filter.h>
#include "tests.h"
#include "debug.h"
#include "probe-file.h"
#include "build-id.h"
#include "util.h"

/* To test SDT event, we need libelf support to scan elf binary */
#if defined(HAVE_SDT_EVENT) && defined(HAVE_LIBELF_SUPPORT)

#include <sys/sdt.h>

static int target_function(void)
{
	DTRACE_PROBE(perf, test_target);
	return TEST_OK;
}

/* Copied from builtin-buildid-cache.c */
static int build_id_cache__add_file(const char *filename)
{
	char sbuild_id[SBUILD_ID_SIZE];
	struct build_id bid;
	int err;

	err = filename__read_build_id(filename, &bid);
	if (err < 0) {
		pr_debug("Failed to read build id of %s\n", filename);
		return err;
	}

	build_id__sprintf(&bid, sbuild_id);
	err = build_id_cache__add_s(sbuild_id, filename, NULL, false, false);
	if (err < 0)
		pr_debug("Failed to add build id cache of %s\n", filename);
	return err;
}

static char *get_self_path(void)
{
	char *buf = calloc(PATH_MAX, sizeof(char));

	if (buf && readlink("/proc/self/exe", buf, PATH_MAX - 1) < 0) {
		pr_debug("Failed to get correct path of perf\n");
		free(buf);
		return NULL;
	}
	return buf;
}

static int search_cached_probe(const char *target,
			       const char *group, const char *event)
{
	struct probe_cache *cache = probe_cache__new(target, NULL);
	int ret = 0;

	if (!cache) {
		pr_debug("Failed to open probe cache of %s\n", target);
		return -EINVAL;
	}

	if (!probe_cache__find_by_name(cache, group, event)) {
		pr_debug("Failed to find %s:%s in the cache\n", group, event);
		ret = -ENOENT;
	}
	probe_cache__delete(cache);

	return ret;
}

int test__sdt_event(struct test *test __maybe_unused, int subtests __maybe_unused)
{
	int ret = TEST_FAIL;
	char __tempdir[] = "./test-buildid-XXXXXX";
	char *tempdir = NULL, *myself = get_self_path();

	if (myself == NULL || mkdtemp(__tempdir) == NULL) {
		pr_debug("Failed to make a tempdir for build-id cache\n");
		goto error;
	}
	/* Note that buildid_dir must be an absolute path */
	tempdir = realpath(__tempdir, NULL);
	if (tempdir == NULL)
		goto error_rmdir;

	/* At first, scan itself */
	set_buildid_dir(tempdir);
	if (build_id_cache__add_file(myself) < 0)
		goto error_rmdir;

	/* Open a cache and make sure the SDT is stored */
	if (search_cached_probe(myself, "sdt_perf", "test_target") < 0)
		goto error_rmdir;

	/* TBD: probing on the SDT event and collect logs */

	/* Call the target and get an event */
	ret = target_function();

error_rmdir:
	/* Cleanup temporary buildid dir */
	rm_rf(__tempdir);
error:
	free(tempdir);
	free(myself);
	return ret;
}
#else
int test__sdt_event(struct test *test __maybe_unused, int subtests __maybe_unused)
{
	pr_debug("Skip SDT event test because SDT support is not compiled\n");
	return TEST_SKIP;
}
#endif
