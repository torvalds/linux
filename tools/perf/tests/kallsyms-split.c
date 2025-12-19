// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include "util/dso.h"
#include "util/map.h"
#include "util/symbol.h"
#include "util/debug.h"
#include "util/machine.h"
#include "tests.h"

/*
 * This test is to check whether a bad symbol in a module won't split kallsyms maps.
 * The main_symbol[1-3] should belong to the main [kernel.kallsyms] map even if the
 * bad_symbol from the module is found in the middle.
 */
static char root_template[] = "/tmp/perf-test.XXXXXX";
static char *root_dir;

static const char proc_version[] = "Linux version X.Y.Z (just for perf test)\n";
static const char proc_modules[] = "module 4096 1 - Live 0xffffffffcd000000\n";
static const char proc_kallsyms[] =
	"ffffffffab200000 T _stext\n"
	"ffffffffab200010 T good_symbol\n"
	"ffffffffab200020 t bad_symbol\n"
	"ffffffffab200030 t main_symbol1\n"
	"ffffffffab200040 t main_symbol2\n"
	"ffffffffab200050 t main_symbol3\n"
	"ffffffffab200060 T _etext\n"
	"ffffffffcd000000 T start_module\t[module]\n"
	"ffffffffab200020 u bad_symbol\t[module]\n"
	"ffffffffcd000040 T end_module\t[module]\n";

static struct {
	const char *name;
	const char *contents;
	long len;
} proc_files[] = {
	{ "version", proc_version, sizeof(proc_version) - 1 },
	{ "modules", proc_modules, sizeof(proc_modules) - 1 },
	{ "kallsyms", proc_kallsyms, sizeof(proc_kallsyms) - 1 },
};

static void remove_proc_dir(int sig __maybe_unused)
{
	char buf[128];

	if (root_dir == NULL)
		return;

	for (unsigned i = 0; i < ARRAY_SIZE(proc_files); i++) {
		scnprintf(buf, sizeof(buf), "%s/proc/%s", root_dir, proc_files[i].name);
		remove(buf);
	}

	scnprintf(buf, sizeof(buf), "%s/proc", root_dir);
	rmdir(buf);

	rmdir(root_dir);
	root_dir = NULL;
}

static int create_proc_dir(void)
{
	char buf[128];

	root_dir = mkdtemp(root_template);
	if (root_dir == NULL)
		return -1;

	scnprintf(buf, sizeof(buf), "%s/proc", root_dir);
	if (mkdir(buf, 0700) < 0)
		goto err;

	for (unsigned i = 0; i < ARRAY_SIZE(proc_files); i++) {
		int fd, len;

		scnprintf(buf, sizeof(buf), "%s/proc/%s", root_dir, proc_files[i].name);
		fd = open(buf, O_RDWR | O_CREAT, 0600);
		if (fd < 0)
			goto err;

		len = write(fd, proc_files[i].contents, proc_files[i].len);
		close(fd);
		if (len != proc_files[i].len)
			goto err;
	}
	return 0;

err:
	remove_proc_dir(0);
	return -1;
}

static int test__kallsyms_split(struct test_suite *test __maybe_unused,
				int subtest __maybe_unused)
{
	struct machine m;
	struct map *map = NULL;
	int ret = TEST_FAIL;

	pr_debug("try to create fake root directory\n");
	if (create_proc_dir() < 0) {
		pr_debug("SKIP: cannot create a fake root directory\n");
		return TEST_SKIP;
	}

	signal(SIGINT, remove_proc_dir);
	signal(SIGPIPE, remove_proc_dir);
	signal(SIGSEGV, remove_proc_dir);
	signal(SIGTERM, remove_proc_dir);

	pr_debug("create kernel maps from the fake root directory\n");
	machine__init(&m, root_dir, HOST_KERNEL_ID);
	if (machine__create_kernel_maps(&m) < 0) {
		pr_debug("FAIL: failed to create kernel maps\n");
		goto out;
	}

	/* force to use /proc/kallsyms */
	symbol_conf.ignore_vmlinux = true;
	symbol_conf.ignore_vmlinux_buildid = true;
	symbol_conf.allow_aliases = true;

	if (map__load(machine__kernel_map(&m)) < 0) {
		pr_debug("FAIL: failed to load kallsyms\n");
		goto out;
	}

	pr_debug("kernel map loaded - check symbol and map\n");
	if (maps__nr_maps(machine__kernel_maps(&m)) != 2) {
		pr_debug("FAIL: it should have the kernel and a module, but has %u maps\n",
			 maps__nr_maps(machine__kernel_maps(&m)));
		goto out;
	}

	if (machine__find_kernel_symbol_by_name(&m, "main_symbol3", &map) == NULL) {
		pr_debug("FAIL: failed to find a symbol\n");
		goto out;
	}

	if (!RC_CHK_EQUAL(map, machine__kernel_map(&m))) {
		pr_debug("FAIL: the symbol is not in the kernel map\n");
		goto out;
	}
	ret = TEST_OK;

out:
	remove_proc_dir(0);
	machine__exit(&m);
	return ret;
}

DEFINE_SUITE("split kallsyms", kallsyms_split);
