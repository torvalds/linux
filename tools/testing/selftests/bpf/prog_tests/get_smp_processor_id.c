// SPDX-License-Identifier: GPL-2.0

#include <test_progs.h>
#include "bpf/libbpf_internal.h"
#include "get_smp_processor_id.skel.h"

void test_get_smp_processor_id(void)
{
	LIBBPF_OPTS(bpf_test_run_opts, opts,
		    .flags = BPF_F_TEST_RUN_ON_CPU,
		    .cpu = 0,
	);
	struct get_smp_processor_id *skel;
	int prog_fd, err, online_cpu_nr, i;
	bool *online = NULL;

	err = parse_cpu_mask_file("/sys/devices/system/cpu/online",
				  &online, &online_cpu_nr);
	if (!ASSERT_OK(err, "parse_cpu_mask_file"))
		return;

	skel = get_smp_processor_id__open_and_load();
	if (!ASSERT_OK_PTR(skel, "get_smp_processor_id__open_and_load"))
		goto cleanup;

	prog_fd = bpf_program__fd(skel->progs.call_bpf_get_smp_processor_id);

	for (i = 0; i < online_cpu_nr; i++) {
		if (!online[i])
			continue;

		opts.cpu = i;
		skel->bss->cpu_nr_result = -1;

		err = bpf_prog_test_run_opts(prog_fd, &opts);
		if (!ASSERT_OK(err, "bpf_prog_test_run_opts"))
			goto cleanup;

		ASSERT_EQ(skel->bss->cpu_nr_result, opts.cpu, "cpu_nr_result");
	}

cleanup:
	free(online);
	get_smp_processor_id__destroy(skel);
}
