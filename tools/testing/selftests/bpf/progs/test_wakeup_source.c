// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"
#include "wakeup_source.h"

#define MAX_LOOP_ITER 1000
#define RB_SIZE (16384 * 4)

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, RB_SIZE);
} rb SEC(".maps");

struct bpf_ws_lock;
struct bpf_ws_lock *bpf_wakeup_sources_read_lock(void) __ksym;
void bpf_wakeup_sources_read_unlock(struct bpf_ws_lock *lock) __ksym;
void *bpf_wakeup_sources_get_head(void) __ksym;

SEC("syscall")
__success __retval(0)
int iterate_wakeupsources(void *ctx)
{
	struct list_head *head = bpf_wakeup_sources_get_head();
	struct list_head *pos = head;
	struct bpf_ws_lock *lock;
	int i;

	lock = bpf_wakeup_sources_read_lock();
	if (!lock)
		return 0;

	bpf_for(i, 0, MAX_LOOP_ITER) {
		if (bpf_core_read(&pos, sizeof(pos), &pos->next) || !pos || pos == head)
			break;

		struct wakeup_event_t *e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);

		if (!e)
			break;

		struct wakeup_source *ws = bpf_core_cast(
				(void *)pos - bpf_core_field_offset(struct wakeup_source, entry),
				struct wakeup_source);
		s64 active_time = 0;
		bool active = BPF_CORE_READ_BITFIELD(ws, active);
		bool autosleep_enable = BPF_CORE_READ_BITFIELD(ws, autosleep_enabled);
		s64 last_time = ws->last_time;
		s64 max_time = ws->max_time;
		s64 prevent_sleep_time = ws->prevent_sleep_time;
		s64 total_time = ws->total_time;

		if (active) {
			s64 curr_time = bpf_ktime_get_ns();
			s64 prevent_time = ws->start_prevent_time;

			if (curr_time > last_time)
				active_time = curr_time - last_time;

			total_time += active_time;
			if (active_time > max_time)
				max_time = active_time;
			if (autosleep_enable && curr_time > prevent_time)
				prevent_sleep_time += curr_time - prevent_time;
		}

		e->active_count = ws->active_count;
		e->active_time_ns = active_time;
		e->event_count = ws->event_count;
		e->expire_count = ws->expire_count;
		e->last_time_ns = last_time;
		e->max_time_ns = max_time;
		e->prevent_sleep_time_ns = prevent_sleep_time;
		e->total_time_ns = total_time;
		e->wakeup_count = ws->wakeup_count;

		if (bpf_probe_read_kernel_str(
				e->name, WAKEUP_NAME_LEN, ws->name) < 0)
			e->name[0] = '\0';

		bpf_ringbuf_submit(e, 0);
	}

	bpf_wakeup_sources_read_unlock(lock);
	return 0;
}

char _license[] SEC("license") = "GPL";
