// SPDX-License-Identifier: GPL-2.0
/* Copyright 2026 Google LLC */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct bpf_ws_lock;

struct bpf_ws_lock *bpf_wakeup_sources_read_lock(void) __ksym;
void bpf_wakeup_sources_read_unlock(struct bpf_ws_lock *lock) __ksym;
void *bpf_wakeup_sources_get_head(void) __ksym;

SEC("syscall")
__failure __msg("BPF_EXIT instruction in main prog would lead to reference leak")
int wakeup_source_lock_no_unlock(void *ctx)
{
	struct bpf_ws_lock *lock;

	lock = bpf_wakeup_sources_read_lock();
	if (!lock)
		return 0;

	return 0;
}

SEC("syscall")
__failure __msg("access beyond struct")
int wakeup_source_access_lock_fields(void *ctx)
{
	struct bpf_ws_lock *lock;
	int val;

	lock = bpf_wakeup_sources_read_lock();
	if (!lock)
		return 0;

	val = *(int *)lock;

	bpf_wakeup_sources_read_unlock(lock);
	return val;
}

SEC("syscall")
__failure __msg("release kfunc bpf_wakeup_sources_read_unlock expects referenced PTR_TO_BTF_ID passed to R1")
int wakeup_source_unlock_no_lock(void *ctx)
{
	struct bpf_ws_lock *lock = (void *)0x1;

	bpf_wakeup_sources_read_unlock(lock);

	return 0;
}

SEC("syscall")
__failure __msg("Possibly NULL pointer passed to trusted")
int wakeup_source_unlock_null(void *ctx)
{
	bpf_wakeup_sources_read_unlock(NULL);

	return 0;
}

SEC("syscall")
__failure __msg("R0 invalid mem access 'scalar'")
int wakeup_source_unsafe_dereference(void *ctx)
{
	struct list_head *head = bpf_wakeup_sources_get_head();

	if (head->next)
		return 1;

	return 0;
}

char _license[] SEC("license") = "GPL";
