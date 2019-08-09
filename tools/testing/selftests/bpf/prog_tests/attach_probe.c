// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>

ssize_t get_base_addr() {
	size_t start;
	char buf[256];
	FILE *f;

	f = fopen("/proc/self/maps", "r");
	if (!f)
		return -errno;

	while (fscanf(f, "%zx-%*x %s %*s\n", &start, buf) == 2) {
		if (strcmp(buf, "r-xp") == 0) {
			fclose(f);
			return start;
		}
	}

	fclose(f);
	return -EINVAL;
}

void test_attach_probe(void)
{
	const char *kprobe_name = "kprobe/sys_nanosleep";
	const char *kretprobe_name = "kretprobe/sys_nanosleep";
	const char *uprobe_name = "uprobe/trigger_func";
	const char *uretprobe_name = "uretprobe/trigger_func";
	const int kprobe_idx = 0, kretprobe_idx = 1;
	const int uprobe_idx = 2, uretprobe_idx = 3;
	const char *file = "./test_attach_probe.o";
	struct bpf_program *kprobe_prog, *kretprobe_prog;
	struct bpf_program *uprobe_prog, *uretprobe_prog;
	struct bpf_object *obj;
	int err, prog_fd, duration = 0, res;
	struct bpf_link *kprobe_link = NULL;
	struct bpf_link *kretprobe_link = NULL;
	struct bpf_link *uprobe_link = NULL;
	struct bpf_link *uretprobe_link = NULL;
	int results_map_fd;
	size_t uprobe_offset;
	ssize_t base_addr;

	base_addr = get_base_addr();
	if (CHECK(base_addr < 0, "get_base_addr",
		  "failed to find base addr: %zd", base_addr))
		return;
	uprobe_offset = (size_t)&get_base_addr - base_addr;

	/* load programs */
	err = bpf_prog_load(file, BPF_PROG_TYPE_KPROBE, &obj, &prog_fd);
	if (CHECK(err, "obj_load", "err %d errno %d\n", err, errno))
		return;

	kprobe_prog = bpf_object__find_program_by_title(obj, kprobe_name);
	if (CHECK(!kprobe_prog, "find_probe",
		  "prog '%s' not found\n", kprobe_name))
		goto cleanup;
	kretprobe_prog = bpf_object__find_program_by_title(obj, kretprobe_name);
	if (CHECK(!kretprobe_prog, "find_probe",
		  "prog '%s' not found\n", kretprobe_name))
		goto cleanup;
	uprobe_prog = bpf_object__find_program_by_title(obj, uprobe_name);
	if (CHECK(!uprobe_prog, "find_probe",
		  "prog '%s' not found\n", uprobe_name))
		goto cleanup;
	uretprobe_prog = bpf_object__find_program_by_title(obj, uretprobe_name);
	if (CHECK(!uretprobe_prog, "find_probe",
		  "prog '%s' not found\n", uretprobe_name))
		goto cleanup;

	/* load maps */
	results_map_fd = bpf_find_map(__func__, obj, "results_map");
	if (CHECK(results_map_fd < 0, "find_results_map",
		  "err %d\n", results_map_fd))
		goto cleanup;

	kprobe_link = bpf_program__attach_kprobe(kprobe_prog,
						 false /* retprobe */,
						 SYS_NANOSLEEP_KPROBE_NAME);
	if (CHECK(IS_ERR(kprobe_link), "attach_kprobe",
		  "err %ld\n", PTR_ERR(kprobe_link))) {
		kprobe_link = NULL;
		goto cleanup;
	}
	kretprobe_link = bpf_program__attach_kprobe(kretprobe_prog,
						    true /* retprobe */,
						    SYS_NANOSLEEP_KPROBE_NAME);
	if (CHECK(IS_ERR(kretprobe_link), "attach_kretprobe",
		  "err %ld\n", PTR_ERR(kretprobe_link))) {
		kretprobe_link = NULL;
		goto cleanup;
	}
	uprobe_link = bpf_program__attach_uprobe(uprobe_prog,
						 false /* retprobe */,
						 0 /* self pid */,
						 "/proc/self/exe",
						 uprobe_offset);
	if (CHECK(IS_ERR(uprobe_link), "attach_uprobe",
		  "err %ld\n", PTR_ERR(uprobe_link))) {
		uprobe_link = NULL;
		goto cleanup;
	}
	uretprobe_link = bpf_program__attach_uprobe(uretprobe_prog,
						    true /* retprobe */,
						    -1 /* any pid */,
						    "/proc/self/exe",
						    uprobe_offset);
	if (CHECK(IS_ERR(uretprobe_link), "attach_uretprobe",
		  "err %ld\n", PTR_ERR(uretprobe_link))) {
		uretprobe_link = NULL;
		goto cleanup;
	}

	/* trigger & validate kprobe && kretprobe */
	usleep(1);

	err = bpf_map_lookup_elem(results_map_fd, &kprobe_idx, &res);
	if (CHECK(err, "get_kprobe_res",
		  "failed to get kprobe res: %d\n", err))
		goto cleanup;
	if (CHECK(res != kprobe_idx + 1, "check_kprobe_res",
		  "wrong kprobe res: %d\n", res))
		goto cleanup;

	err = bpf_map_lookup_elem(results_map_fd, &kretprobe_idx, &res);
	if (CHECK(err, "get_kretprobe_res",
		  "failed to get kretprobe res: %d\n", err))
		goto cleanup;
	if (CHECK(res != kretprobe_idx + 1, "check_kretprobe_res",
		  "wrong kretprobe res: %d\n", res))
		goto cleanup;

	/* trigger & validate uprobe & uretprobe */
	get_base_addr();

	err = bpf_map_lookup_elem(results_map_fd, &uprobe_idx, &res);
	if (CHECK(err, "get_uprobe_res",
		  "failed to get uprobe res: %d\n", err))
		goto cleanup;
	if (CHECK(res != uprobe_idx + 1, "check_uprobe_res",
		  "wrong uprobe res: %d\n", res))
		goto cleanup;

	err = bpf_map_lookup_elem(results_map_fd, &uretprobe_idx, &res);
	if (CHECK(err, "get_uretprobe_res",
		  "failed to get uretprobe res: %d\n", err))
		goto cleanup;
	if (CHECK(res != uretprobe_idx + 1, "check_uretprobe_res",
		  "wrong uretprobe res: %d\n", res))
		goto cleanup;

cleanup:
	bpf_link__destroy(kprobe_link);
	bpf_link__destroy(kretprobe_link);
	bpf_link__destroy(uprobe_link);
	bpf_link__destroy(uretprobe_link);
	bpf_object__close(obj);
}
