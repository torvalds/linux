#include <stdbool.h>
#include <stdlib.h>
#include "tests.h"
#include "dso.h"
#include "debug.h"

static int test(const char *path, bool alloc_name, bool alloc_ext,
		bool kmod, bool comp, const char *name, const char *ext)
{
	struct kmod_path m;

	memset(&m, 0x0, sizeof(m));

	TEST_ASSERT_VAL("kmod_path__parse",
			!__kmod_path__parse(&m, path, alloc_name, alloc_ext));

	pr_debug("%s - alloc name %d, alloc ext %d, kmod %d, comp %d, name '%s', ext '%s'\n",
		 path, alloc_name, alloc_ext, m.kmod, m.comp, m.name, m.ext);

	TEST_ASSERT_VAL("wrong kmod", m.kmod == kmod);
	TEST_ASSERT_VAL("wrong comp", m.comp == comp);

	if (ext)
		TEST_ASSERT_VAL("wrong ext", m.ext && !strcmp(ext, m.ext));
	else
		TEST_ASSERT_VAL("wrong ext", !m.ext);

	if (name)
		TEST_ASSERT_VAL("wrong name", m.name && !strcmp(name, m.name));
	else
		TEST_ASSERT_VAL("wrong name", !m.name);

	free(m.name);
	free(m.ext);
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

#define T(path, an, ae, k, c, n, e) \
	TEST_ASSERT_VAL("failed", !test(path, an, ae, k, c, n, e))

#define M(path, c, e) \
	TEST_ASSERT_VAL("failed", !test_is_kernel_module(path, c, e))

int test__kmod_path__parse(struct test *t __maybe_unused, int subtest __maybe_unused)
{
	/* path                alloc_name  alloc_ext   kmod  comp   name     ext */
	T("/xxxx/xxxx/x-x.ko", true      , true      , true, false, "[x_x]", NULL);
	T("/xxxx/xxxx/x-x.ko", false     , true      , true, false, NULL   , NULL);
	T("/xxxx/xxxx/x-x.ko", true      , false     , true, false, "[x_x]", NULL);
	T("/xxxx/xxxx/x-x.ko", false     , false     , true, false, NULL   , NULL);
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_KERNEL, true);
	M("/xxxx/xxxx/x-x.ko", PERF_RECORD_MISC_USER, false);

#ifdef HAVE_ZLIB_SUPPORT
	/* path                alloc_name  alloc_ext   kmod  comp  name   ext */
	T("/xxxx/xxxx/x.ko.gz", true     , true      , true, true, "[x]", "gz");
	T("/xxxx/xxxx/x.ko.gz", false    , true      , true, true, NULL , "gz");
	T("/xxxx/xxxx/x.ko.gz", true     , false     , true, true, "[x]", NULL);
	T("/xxxx/xxxx/x.ko.gz", false    , false     , true, true, NULL , NULL);
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_KERNEL, true);
	M("/xxxx/xxxx/x.ko.gz", PERF_RECORD_MISC_USER, false);

	/* path              alloc_name  alloc_ext  kmod   comp  name    ext */
	T("/xxxx/xxxx/x.gz", true      , true     , false, true, "x.gz" ,"gz");
	T("/xxxx/xxxx/x.gz", false     , true     , false, true, NULL   ,"gz");
	T("/xxxx/xxxx/x.gz", true      , false    , false, true, "x.gz" , NULL);
	T("/xxxx/xxxx/x.gz", false     , false    , false, true, NULL   , NULL);
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_KERNEL, false);
	M("/xxxx/xxxx/x.gz", PERF_RECORD_MISC_USER, false);

	/* path   alloc_name  alloc_ext  kmod   comp  name     ext */
	T("x.gz", true      , true     , false, true, "x.gz", "gz");
	T("x.gz", false     , true     , false, true, NULL  , "gz");
	T("x.gz", true      , false    , false, true, "x.gz", NULL);
	T("x.gz", false     , false    , false, true, NULL  , NULL);
	M("x.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("x.gz", PERF_RECORD_MISC_KERNEL, false);
	M("x.gz", PERF_RECORD_MISC_USER, false);

	/* path      alloc_name  alloc_ext  kmod  comp  name  ext */
	T("x.ko.gz", true      , true     , true, true, "[x]", "gz");
	T("x.ko.gz", false     , true     , true, true, NULL , "gz");
	T("x.ko.gz", true      , false    , true, true, "[x]", NULL);
	T("x.ko.gz", false     , false    , true, true, NULL , NULL);
	M("x.ko.gz", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("x.ko.gz", PERF_RECORD_MISC_KERNEL, true);
	M("x.ko.gz", PERF_RECORD_MISC_USER, false);
#endif

	/* path            alloc_name  alloc_ext  kmod  comp   name             ext */
	T("[test_module]", true      , true     , true, false, "[test_module]", NULL);
	T("[test_module]", false     , true     , true, false, NULL           , NULL);
	T("[test_module]", true      , false    , true, false, "[test_module]", NULL);
	T("[test_module]", false     , false    , true, false, NULL           , NULL);
	M("[test_module]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("[test_module]", PERF_RECORD_MISC_KERNEL, true);
	M("[test_module]", PERF_RECORD_MISC_USER, false);

	/* path            alloc_name  alloc_ext  kmod  comp   name             ext */
	T("[test.module]", true      , true     , true, false, "[test.module]", NULL);
	T("[test.module]", false     , true     , true, false, NULL           , NULL);
	T("[test.module]", true      , false    , true, false, "[test.module]", NULL);
	T("[test.module]", false     , false    , true, false, NULL           , NULL);
	M("[test.module]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, true);
	M("[test.module]", PERF_RECORD_MISC_KERNEL, true);
	M("[test.module]", PERF_RECORD_MISC_USER, false);

	/* path     alloc_name  alloc_ext  kmod   comp   name      ext */
	T("[vdso]", true      , true     , false, false, "[vdso]", NULL);
	T("[vdso]", false     , true     , false, false, NULL    , NULL);
	T("[vdso]", true      , false    , false, false, "[vdso]", NULL);
	T("[vdso]", false     , false    , false, false, NULL    , NULL);
	M("[vdso]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vdso]", PERF_RECORD_MISC_KERNEL, false);
	M("[vdso]", PERF_RECORD_MISC_USER, false);

	/* path         alloc_name  alloc_ext  kmod   comp   name          ext */
	T("[vsyscall]", true      , true     , false, false, "[vsyscall]", NULL);
	T("[vsyscall]", false     , true     , false, false, NULL        , NULL);
	T("[vsyscall]", true      , false    , false, false, "[vsyscall]", NULL);
	T("[vsyscall]", false     , false    , false, false, NULL        , NULL);
	M("[vsyscall]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[vsyscall]", PERF_RECORD_MISC_KERNEL, false);
	M("[vsyscall]", PERF_RECORD_MISC_USER, false);

	/* path                alloc_name  alloc_ext  kmod   comp   name      ext */
	T("[kernel.kallsyms]", true      , true     , false, false, "[kernel.kallsyms]", NULL);
	T("[kernel.kallsyms]", false     , true     , false, false, NULL               , NULL);
	T("[kernel.kallsyms]", true      , false    , false, false, "[kernel.kallsyms]", NULL);
	T("[kernel.kallsyms]", false     , false    , false, false, NULL               , NULL);
	M("[kernel.kallsyms]", PERF_RECORD_MISC_CPUMODE_UNKNOWN, false);
	M("[kernel.kallsyms]", PERF_RECORD_MISC_KERNEL, false);
	M("[kernel.kallsyms]", PERF_RECORD_MISC_USER, false);

	return 0;
}
