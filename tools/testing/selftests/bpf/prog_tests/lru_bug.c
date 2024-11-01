// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

#include "lru_bug.skel.h"

void test_lru_bug(void)
{
	struct lru_bug *skel;
	int ret;

	skel = lru_bug__open_and_load();
	if (!ASSERT_OK_PTR(skel, "lru_bug__open_and_load"))
		return;
	ret = lru_bug__attach(skel);
	if (!ASSERT_OK(ret, "lru_bug__attach"))
		goto end;
	usleep(1);
	ASSERT_OK(skel->data->result, "prealloc_lru_pop doesn't call check_and_init_map_value");
end:
	lru_bug__destroy(skel);
}
