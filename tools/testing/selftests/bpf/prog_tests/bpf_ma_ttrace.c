// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "bpf_ma_ttrace.skel.h"

#define NR_ELEMS 4096

/*
 * The first free_bulk() starts RCU tasks trace GP. The rest of the elements are
 * deleted while it's in flight. They should be freed without further alloc or
 * free from this map.
 */
void test_bpf_ma_ttrace(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts);
	struct bpf_ma_ttrace *skel;
	__u32 cnt = NR_ELEMS;
	long *vals = NULL;
	int *keys = NULL;
	int i, err, fd, nr_cpus;

	skel = bpf_ma_ttrace__open_and_load();
	if (!ASSERT_OK_PTR(skel, "open_and_load"))
		return;
	nr_cpus = libbpf_num_possible_cpus();
	if (!ASSERT_GT(nr_cpus, 0, "nr_cpus"))
		goto out;
	skel->bss->nr_cpus = nr_cpus;

	keys = calloc(NR_ELEMS, sizeof(*keys));
	vals = calloc(NR_ELEMS, sizeof(*vals));
	if (!ASSERT_OK_PTR(keys, "keys") || !ASSERT_OK_PTR(vals, "vals"))
		goto out;
	for (i = 0; i < NR_ELEMS; i++)
		keys[i] = i;

	fd = bpf_map__fd(skel->maps.htab);
	err = bpf_map_update_batch(fd, keys, vals, &cnt, NULL);
	if (!ASSERT_OK(err, "update_batch") || !ASSERT_EQ(cnt, NR_ELEMS, "update_cnt"))
		goto out;
	err = bpf_map_delete_batch(fd, keys, &cnt, NULL);
	if (!ASSERT_OK(err, "delete_batch") || !ASSERT_EQ(cnt, NR_ELEMS, "delete_cnt"))
		goto out;

	/* Wait for all __free_rcu() callbacks to finish */
	for (i = 0; i < 300; i++) {
		err = bpf_prog_test_run_opts(bpf_program__fd(skel->progs.check_ttrace), &opts);
		if (!ASSERT_OK(err, "test_run") || !ASSERT_OK(opts.retval, "retval"))
			goto out;
		if (!skel->bss->in_progress)
			break;
		usleep(100000);
	}
	ASSERT_EQ(skel->bss->nr_caches, nr_cpus, "nr_caches");
	ASSERT_EQ(skel->bss->in_progress, 0, "in_progress");
	ASSERT_EQ(skel->bss->not_freed, 0, "not_freed");
out:
	free(keys);
	free(vals);
	bpf_ma_ttrace__destroy(skel);
}
