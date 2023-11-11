// SPDX-License-Identifier: GPL-2.0
/*
 * bpf-script-example.c
 * Test basic LLVM building
 */
#ifndef LINUX_VERSION_CODE
# error Need LINUX_VERSION_CODE
# error Example: for 4.2 kernel, put 'clang-opt="-DLINUX_VERSION_CODE=0x40200" into llvm section of ~/.perfconfig'
#endif
#define BPF_ANY 0
#define BPF_MAP_TYPE_ARRAY 2
#define BPF_FUNC_map_lookup_elem 1
#define BPF_FUNC_map_update_elem 2

static void *(*bpf_map_lookup_elem)(void *map, void *key) =
	(void *) BPF_FUNC_map_lookup_elem;
static void *(*bpf_map_update_elem)(void *map, void *key, void *value, int flags) =
	(void *) BPF_FUNC_map_update_elem;

/*
 * Following macros are taken from tools/lib/bpf/bpf_helpers.h,
 * and are used to create BTF defined maps. It is easier to take
 * 2 simple macros, than being able to include above header in
 * runtime.
 *
 * __uint - defines integer attribute of BTF map definition,
 * Such attributes are represented using a pointer to an array,
 * in which dimensionality of array encodes specified integer
 * value.
 *
 * __type - defines pointer variable with typeof(val) type for
 * attributes like key or value, which will be defined by the
 * size of the type.
 */
#define __uint(name, val) int (*name)[val]
#define __type(name, val) typeof(val) *name

#define SEC(NAME) __attribute__((section(NAME), used))
struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, int);
} flip_table SEC(".maps");

SEC("func=do_epoll_wait")
int bpf_func__SyS_epoll_pwait(void *ctx)
{
	int ind =0;
	int *flag = bpf_map_lookup_elem(&flip_table, &ind);
	int new_flag;
	if (!flag)
		return 0;
	/* flip flag and store back */
	new_flag = !*flag;
	bpf_map_update_elem(&flip_table, &ind, &new_flag, BPF_ANY);
	return new_flag;
}
char _license[] SEC("license") = "GPL";
int _version SEC("version") = LINUX_VERSION_CODE;
