#include <stdbool.h>
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

#define T(path, an, ae, k, c, n, e) \
	TEST_ASSERT_VAL("failed", !test(path, an, ae, k, c, n, e))

int test__kmod_path__parse(void)
{
	/* path                alloc_name  alloc_ext   kmod  comp   name     ext */
	T("/xxxx/xxxx/x-x.ko", true      , true      , true, false, "[x_x]", NULL);
	T("/xxxx/xxxx/x-x.ko", false     , true      , true, false, NULL   , NULL);
	T("/xxxx/xxxx/x-x.ko", true      , false     , true, false, "[x_x]", NULL);
	T("/xxxx/xxxx/x-x.ko", false     , false     , true, false, NULL   , NULL);

	/* path                alloc_name  alloc_ext   kmod  comp  name   ext */
	T("/xxxx/xxxx/x.ko.gz", true     , true      , true, true, "[x]", "gz");
	T("/xxxx/xxxx/x.ko.gz", false    , true      , true, true, NULL , "gz");
	T("/xxxx/xxxx/x.ko.gz", true     , false     , true, true, "[x]", NULL);
	T("/xxxx/xxxx/x.ko.gz", false    , false     , true, true, NULL , NULL);

	/* path              alloc_name  alloc_ext  kmod   comp  name    ext */
	T("/xxxx/xxxx/x.gz", true      , true     , false, true, "x.gz" ,"gz");
	T("/xxxx/xxxx/x.gz", false     , true     , false, true, NULL   ,"gz");
	T("/xxxx/xxxx/x.gz", true      , false    , false, true, "x.gz" , NULL);
	T("/xxxx/xxxx/x.gz", false     , false    , false, true, NULL   , NULL);

	/* path   alloc_name  alloc_ext  kmod   comp  name     ext */
	T("x.gz", true      , true     , false, true, "x.gz", "gz");
	T("x.gz", false     , true     , false, true, NULL  , "gz");
	T("x.gz", true      , false    , false, true, "x.gz", NULL);
	T("x.gz", false     , false    , false, true, NULL  , NULL);

	/* path      alloc_name  alloc_ext  kmod  comp  name  ext */
	T("x.ko.gz", true      , true     , true, true, "[x]", "gz");
	T("x.ko.gz", false     , true     , true, true, NULL , "gz");
	T("x.ko.gz", true      , false    , true, true, "[x]", NULL);
	T("x.ko.gz", false     , false    , true, true, NULL , NULL);

	return 0;
}
