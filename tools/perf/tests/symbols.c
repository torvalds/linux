// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/string.h>
#include <sys/mman.h>
#include <limits.h>
#include "debug.h"
#include "dso.h"
#include "machine.h"
#include "thread.h"
#include "symbol.h"
#include "map.h"
#include "util.h"
#include "tests.h"

struct test_info {
	struct machine *machine;
	struct thread *thread;
};

static int init_test_info(struct test_info *ti)
{
	ti->machine = machine__new_host();
	if (!ti->machine) {
		pr_debug("machine__new_host() failed!\n");
		return TEST_FAIL;
	}

	/* Create a dummy thread */
	ti->thread = machine__findnew_thread(ti->machine, 100, 100);
	if (!ti->thread) {
		pr_debug("machine__findnew_thread() failed!\n");
		return TEST_FAIL;
	}

	return TEST_OK;
}

static void exit_test_info(struct test_info *ti)
{
	thread__put(ti->thread);
	machine__delete(ti->machine);
}

static void get_test_dso_filename(char *filename, size_t max_sz)
{
	if (dso_to_test)
		strlcpy(filename, dso_to_test, max_sz);
	else
		perf_exe(filename, max_sz);
}

static int create_map(struct test_info *ti, char *filename, struct map **map_p)
{
	/* Create a dummy map at 0x100000 */
	*map_p = map__new(ti->machine, 0x100000, 0xffffffff, 0, NULL,
			  PROT_EXEC, 0, NULL, filename, ti->thread);
	if (!*map_p) {
		pr_debug("Failed to create map!");
		return TEST_FAIL;
	}

	return TEST_OK;
}

static int test_dso(struct dso *dso)
{
	struct symbol *last_sym = NULL;
	struct rb_node *nd;
	int ret = TEST_OK;

	/* dso__fprintf() prints all the symbols */
	if (verbose > 1)
		dso__fprintf(dso, stderr);

	for (nd = rb_first_cached(&dso->symbols); nd; nd = rb_next(nd)) {
		struct symbol *sym = rb_entry(nd, struct symbol, rb_node);

		if (sym->type != STT_FUNC && sym->type != STT_GNU_IFUNC)
			continue;

		/* Check for overlapping function symbols */
		if (last_sym && sym->start < last_sym->end) {
			pr_debug("Overlapping symbols:\n");
			symbol__fprintf(last_sym, stderr);
			symbol__fprintf(sym, stderr);
			ret = TEST_FAIL;
		}
		/* Check for zero-length function symbol */
		if (sym->start == sym->end) {
			pr_debug("Zero-length symbol:\n");
			symbol__fprintf(sym, stderr);
			ret = TEST_FAIL;
		}
		last_sym = sym;
	}

	return ret;
}

static int test_file(struct test_info *ti, char *filename)
{
	struct map *map = NULL;
	int ret, nr;
	struct dso *dso;

	pr_debug("Testing %s\n", filename);

	ret = create_map(ti, filename, &map);
	if (ret != TEST_OK)
		return ret;

	dso = map__dso(map);
	nr = dso__load(dso, map);
	if (nr < 0) {
		pr_debug("dso__load() failed!\n");
		ret = TEST_FAIL;
		goto out_put;
	}

	if (nr == 0) {
		pr_debug("DSO has no symbols!\n");
		ret = TEST_SKIP;
		goto out_put;
	}

	ret = test_dso(dso);
out_put:
	map__put(map);

	return ret;
}

static int test__symbols(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	char filename[PATH_MAX];
	struct test_info ti;
	int ret;

	ret = init_test_info(&ti);
	if (ret != TEST_OK)
		return ret;

	get_test_dso_filename(filename, sizeof(filename));

	ret = test_file(&ti, filename);

	exit_test_info(&ti);

	return ret;
}

DEFINE_SUITE("Symbols", symbols);
