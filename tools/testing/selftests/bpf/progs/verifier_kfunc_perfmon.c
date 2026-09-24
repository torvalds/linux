// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

void *user_ptr;
char dynptr_buf[8];
u64 kaddr;

extern struct kmem_cache *bpf_get_kmem_cache(u64 addr) __ksym;

SEC("socket")
__success
__caps_unpriv(CAP_BPF)
__failure_unpriv
__msg_unpriv("bpf_rdonly_cast is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
int rdonly_cast_noperfmon(void *ctx)
{
	char *p = bpf_rdonly_cast(0, 0);

	return p[0x7fff];
}

SEC("socket")
__success
__caps_unpriv(CAP_BPF)
__failure_unpriv
__msg_unpriv("bpf_probe_read_kernel_dynptr is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
int probe_read_kernel_dynptr_noperfmon(void *ctx)
{
	struct bpf_dynptr dptr;

	bpf_dynptr_from_mem(dynptr_buf, sizeof(dynptr_buf), 0, &dptr);
	bpf_probe_read_kernel_dynptr(&dptr, 0, sizeof(dynptr_buf), user_ptr);
	return 0;
}

SEC("socket")
__success
__caps_unpriv(CAP_BPF)
__failure_unpriv
__msg_unpriv("bpf_stream_vprintk is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
int stream_vprintk_noperfmon(void *ctx)
{
	bpf_stream_printk(BPF_STDOUT, "%pB", (void *)kaddr);
	return 0;
}

SEC("socket")
__success
__caps_unpriv(CAP_BPF)
__failure_unpriv
__msg_unpriv("bpf_get_kmem_cache is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
int get_kmem_cache_noperfmon(void *ctx)
{
	return !!bpf_get_kmem_cache(kaddr);
}

__weak int subprog_untrusted_read(void *p __arg_untrusted)
{
	return *(char *)p;
}

SEC("socket")
__success
__caps_unpriv(CAP_BPF)
__failure_unpriv
__msg_unpriv("rdonly_untrusted_mem access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN")
int arg_untrusted_read_noperfmon(void *ctx)
{
	return subprog_untrusted_read(0);
}

char _license[] SEC("license") = "GPL";
