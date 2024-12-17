/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#include <iostream>
#include <unistd.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <bpf/btf.h>

#ifndef _Bool
#define _Bool bool
#endif
#include "test_core_extern.skel.h"
#include "struct_ops_module.skel.h"

template <typename T>
class Skeleton {
private:
	T *skel;
public:
	Skeleton(): skel(nullptr) { }

	~Skeleton() { if (skel) T::destroy(skel); }

	int open(const struct bpf_object_open_opts *opts = nullptr)
	{
		int err;

		if (skel)
			return -EBUSY;

		skel = T::open(opts);
		err = libbpf_get_error(skel);
		if (err) {
			skel = nullptr;
			return err;
		}

		return 0;
	}

	int load() { return T::load(skel); }

	int attach() { return T::attach(skel); }

	void detach() { return T::detach(skel); }

	const T* operator->() const { return skel; }

	T* operator->() { return skel; }

	const T *get() const { return skel; }
};

static void dump_printf(void *ctx, const char *fmt, va_list args)
{
}

static void try_skeleton_template()
{
	Skeleton<test_core_extern> skel;
	std::string prog_name;
	int err;
	LIBBPF_OPTS(bpf_object_open_opts, opts);

	err = skel.open(&opts);
	if (err) {
		fprintf(stderr, "Skeleton open failed: %d\n", err);
		return;
	}

	skel->data->kern_ver = 123;
	skel->data->int_val = skel->data->ushort_val;

	err = skel.load();
	if (err) {
		fprintf(stderr, "Skeleton load failed: %d\n", err);
		return;
	}

	if (!skel->kconfig->CONFIG_BPF_SYSCALL)
		fprintf(stderr, "Seems like CONFIG_BPF_SYSCALL isn't set?!\n");

	err = skel.attach();
	if (err) {
		fprintf(stderr, "Skeleton attach failed: %d\n", err);
		return;
	}

	prog_name = bpf_program__name(skel->progs.handle_sys_enter);
	if (prog_name != "handle_sys_enter")
		fprintf(stderr, "Unexpected program name: %s\n", prog_name.c_str());

	bpf_link__destroy(skel->links.handle_sys_enter);
	skel->links.handle_sys_enter = bpf_program__attach(skel->progs.handle_sys_enter);

	skel.detach();

	/* destructor will destroy underlying skeleton */
}

int main(int argc, char *argv[])
{
	struct btf_dump_opts opts = { };
	struct test_core_extern *skel;
	struct struct_ops_module *skel2;
	struct btf *btf;
	int fd;

	try_skeleton_template();

	/* libbpf.h */
	libbpf_set_print(NULL);

	/* bpf.h */
	bpf_prog_get_fd_by_id(0);

	/* btf.h */
	btf = btf__new(NULL, 0);
	if (!libbpf_get_error(btf))
		btf_dump__new(btf, dump_printf, nullptr, &opts);

	/* BPF skeleton */
	skel = test_core_extern__open_and_load();
	test_core_extern__destroy(skel);

	skel2 = struct_ops_module__open_and_load();
	struct_ops_module__destroy(skel2);

	fd = bpf_enable_stats(BPF_STATS_RUN_TIME);
	if (fd < 0)
		std::cout << "FAILED to enable stats: " << fd << std::endl;
	else
		::close(fd);

	std::cout << "DONE!" << std::endl;

	return 0;
}
