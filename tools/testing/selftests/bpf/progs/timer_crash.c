// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>

struct map_elem {
	struct bpf_timer timer;
	struct bpf_spin_lock lock;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct map_elem);
} amap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct map_elem);
} hmap SEC(".maps");

int pid = 0;
int crash_map = 0; /* 0 for amap, 1 for hmap */

SEC("fentry/do_nanosleep")
int sys_enter(void *ctx)
{
	struct map_elem *e, value = {};
	void *map = crash_map ? (void *)&hmap : (void *)&amap;

	if (bpf_get_current_task_btf()->tgid != pid)
		return 0;

	*(void **)&value = (void *)0xdeadcaf3;

	bpf_map_update_elem(map, &(int){0}, &value, 0);
	/* For array map, doing bpf_map_update_elem will do a
	 * check_and_free_timer_in_array, which will trigger the crash if timer
	 * pointer was overwritten, for hmap we need to use bpf_timer_cancel.
	 */
	if (crash_map == 1) {
		e = bpf_map_lookup_elem(map, &(int){0});
		if (!e)
			return 0;
		bpf_timer_cancel(&e->timer);
	}
	return 0;
}

char _license[] SEC("license") = "GPL";
