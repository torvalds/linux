// SPDX-License-Identifier: GPL-2.0

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <test_progs.h>


#include "test_pinning_devmap.skel.h"

void test_pinning_devmap_reuse(void)
{
	const char *pinpath1 = "/sys/fs/bpf/pinmap1";
	const char *pinpath2 = "/sys/fs/bpf/pinmap2";
	struct test_pinning_devmap *skel1 = NULL, *skel2 = NULL;
	int err;
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);

	/* load the object a first time */
	skel1 = test_pinning_devmap__open_and_load();
	if (!ASSERT_OK_PTR(skel1, "skel_load1"))
		goto out;

	/* load the object a second time, re-using the pinned map */
	skel2 = test_pinning_devmap__open_and_load();
	if (!ASSERT_OK_PTR(skel2, "skel_load2"))
		goto out;

	/* we can close the reference safely without
	 * the map's refcount falling to 0
	 */
	test_pinning_devmap__destroy(skel1);
	skel1 = NULL;

	/* now, swap the pins */
	err = renameat2(0, pinpath1, 0, pinpath2, RENAME_EXCHANGE);
	if (!ASSERT_OK(err, "swap pins"))
		goto out;

	/* load the object again, this time the re-use should fail */
	skel1 = test_pinning_devmap__open_and_load();
	if (!ASSERT_ERR_PTR(skel1, "skel_load3"))
		goto out;

out:
	unlink(pinpath1);
	unlink(pinpath2);
	test_pinning_devmap__destroy(skel1);
	test_pinning_devmap__destroy(skel2);
}
