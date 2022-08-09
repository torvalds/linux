// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "tests.h"
#include "dso.h"
#include "debug.h"
#include "event.h"

static int test(const char *path, bool alloc_name, bool kmod,
		int comp, const char *name)
{
	struct kmod_path m;

	memset(&m, 0x0, sizeof(m));

	TEST_ASSERT_VAL("kmod_path__parse",
			!__kmod_path__parse(&m, path, alloc_name));

	pr_debug("%s - alloc name %d, kmod %d, comp %d, name '%s'\n",
		 path, alloc_name, m.kmod, m.comp, m.name);

	TEST_ASSERT_VAL("wrong kmod", m.kmod == kmod);
	TEST_ASSERT_VAL("wrong comp", m.comp == comp);

	if (name)
		TEST_ASSERT_VAL("wrong name", m.name && !strcmp(name, m.name));
	else
		TEST_ASSERT_VAL("wrong name", !m.name);

	free(m.name);
	return 0;
}

static int test_is_kernel_module(const char *path, int cpumode, bool expect)
{
	TEST_ASSERT_VAL("is_kernel_module",
			(!!is_kernel_module(path, cpumode)) == (!!expect));
	pr_debug("%s (cpumode: %d) - is_kernel_module: %s\n",
			path, cpumode, expect ? "true" : "false");
	return 0;
}

#define T(path, an, k, c, n) \
	TEST_ASSERT_VAL("failed", !test(path, an, k, c, n))

#define M(path, c, e) \
	TEST_ASSERT_VAL("failed", !test_is_kernel_module(path, c, e))

static int test__kmod_path__parse(struct test_suite *t __maybe_unused, int subtest __maybe_unused)
{
	/* path                alloc_name  kmod  comp   name   */
	T("/xxxx/xxxx/x-x.ko", true      , true, 0    , "[x_x]");
	T("/xxxx/xxxx/x-x.ko", false     , true, 0    , NULL   );
	T("/xxxx/xxxx/x-x.ko", true      , true, 0    , "[x_x]");
	T("/xxxx/xxxx/x-x.ko", false     , true, 0    , NULL   );
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_KERNEL, true);
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_USER, false);

#ifdef HAVE_ZLIB_SUPPORT
	/* path                alloc_name   kmod  comp  name  */
	T("/xxxx/xxxx/x.ko.gz", true     , true, 1   , "[x]");
	T("/xxxx/xxxx/x.ko.gz", false    , true, 1   , NULL );
	T("/xxxx/xxxx/x.ko.gz", true     , true, 1   , "[x]");
	T("/xxxx/xxxx/x.ko.gz", false    , true, 1   , NULL );
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_KERNEL, true);
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_USER, false);

	/* path              alloc_name  kmod   comp  name  */
	T("/xxxx/xxxx/x.gz", true      , false, 1   , "x.gz");
	T("/xxxx/xxxx/x.gz", false     , false, 1   , NULL  );
	T("/xxxx/xxxx/x.gz", true      , false, 1   , "x.gz");
	T("/xxxx/xxxx/x.gz", false     , false, 1   , NULL  );
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_KERNEL, false);
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_USER, false);

	/* path   alloc_name  kmod   comp  name   */
	T("x.gz", true      , false, 1   , "x.gz");
	T("x.gz", false     , false, 1   , NULL  );
	T("x.gz", true      , false, 1   , "x.gz");
	T("x.gz", false     , false, 1   , NULL  );
	M("x.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("x.gz", PERF_RECORD_MISC_KERNEL, false);
	M("x.gz", PERF_RECORD_MISC_USER, false);

	/* path      alloc_name  kmod  comp  name  */
	T("x.ko.gz", true      , true, 1   , "[x]");
	T("x.ko.gz", false     , true, 1   , NULL );
	T("x.ko.gz", true      , true, 1   , "[x]");
	T("x.ko.gz", false     , true, 1   , NULL );
	M("x.ko.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("x.ko.gz", PERF_RECORD_MISC_KERNEL, true);
	M("x.ko.gz", PERF_RECORD_MISC_USER, false);
#endif

	/* path            alloc_name  kmod  comp   name           */
	T("[test_module]", true      , true, false, "[test_module]");
	T("[test_module]", false     , true, false, NULL           );
	T("[test_module]", true      , true, false, "[test_module]");
	T("[test_module]", false     , true, false, NULL           );
	M("[test_module]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("[test_module]", PERF_RECORD_MISC_KERNEL, true);
	M("[test_module]", PERF_RECORD_MISC_USER, false);

	/* path            alloc_name  kmod  comp   name           */
	T("[test.module]", true      , true, false, "[test.module]");
	T("[test.module]", false     , true, false, NULL           );
	T("[test.module]", true      , true, false, "[test.module]");
	T("[test.module]", false     , true, false, NULL           );
	M("[test.module]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("[test.module]", PERF_RECORD_MISC_KERNEL, true);
	M("[test.module]", PERF_RECORD_MISC_USER, false);

	/* path     alloc_name  kmod   comp   name    */
	T("[vdso]", true      , false, false, "[vdso]");
	T("[vdso]", false     , false, false, NULL    );
	T("[vdso]", true      , false, false, "[vdso]");
	T("[vdso]", false     , false, false, NULL    );
	M("[vdso]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vdso]", PERF_RECORD_MISC_KERNEL, false);
	M("[vdso]", PERF_RECORD_MISC_USER, false);

	T("[vdso32]", true      , false, false, "[vdso32]");
	T("[vdso32]", false     , false, false, NULL      );
	T("[vdso32]", true      , false, false, "[vdso32]");
	T("[vdso32]", false     , false, false, NULL      );
	M("[vdso32]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vdso32]", PERF_RECORD_MISC_KERNEL, false);
	M("[vdso32]", PERF_RECORD_MISC_USER, false);

	T("[vdsox32]", true      , false, false, "[vdsox32]");
	T("[vdsox32]", false     , false, false, NULL       );
	T("[vdsox32]", true      , false, false, "[vdsox32]");
	T("[vdsox32]", false     , false, false, NULL       );
	M("[vdsox32]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vdsox32]", PERF_RECORD_MISC_KERNEL, false);
	M("[vdsox32]", PERF_RECORD_MISC_USER, false);

	/* path         alloc_name  kmod   comp   name        */
	T("[vsyscall]", true      , false, false, "[vsyscall]");
	T("[vsyscall]", false     , false, false, NULL        );
	T("[vsyscall]", true      , false, false, "[vsyscall]");
	T("[vsyscall]", false     , false, false, NULL        );
	M("[vsyscall]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vsyscall]", PERF_RECORD_MISC_KERNEL, false);
	M("[vsyscall]", PERF_RECORD_MISC_USER, false);

	/* path                alloc_name  kmod   comp   name      */
	T("[kernel.kallsyms]", true      , false, false, "[kernel.kallsyms]");
	T("[kernel.kallsyms]", false     , false, false, NULL               );
	T("[kernel.kallsyms]", true      , false, false, "[kernel.kallsyms]");
	T("[kernel.kallsyms]", false     , false, false, NULL               );
	M("[kernel.kallsyms]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[kernel.kallsyms]", PERF_RECORD_MISC_KERNEL, false);
	M("[kernel.kallsyms]", PERF_RECORD_MISC_USER, false);

	return 0;
}

DEFINE_SUITE("kmod_path__parse", kmod_path__parse);
