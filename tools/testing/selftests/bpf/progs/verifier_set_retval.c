// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

SEC("lsm_cgroup/socket_create")
__description("lsm_cgroup bpf_set_retval success")
__success
int BPF_PROG(lsm_cgroup_set_retval_zero_valid, int family, int type, int protocol, int kern)
{
	bpf_set_retval(0);
	return 0;
}

SEC("lsm_cgroup/socket_create")
__description("lsm_cgroup bpf_set_retval valid errno")
__success
int BPF_PROG(lsm_cgroup_set_retval_negative_valid, int family, int type, int protocol, int kern)
{
	bpf_set_retval(-12);
	return 0;
}

SEC("lsm_cgroup/socket_create")
__description("lsm_cgroup bpf_set_retval invalid negative value")
__failure __msg("should have been in [-4095, 0]")
int BPF_PROG(lsm_cgroup_set_retval_negative_invalid, int family, int type, int protocol, int kern)
{
	bpf_set_retval(-4096);
	return 0;
}

SEC("lsm_cgroup/socket_create")
__description("lsm_cgroup bpf_set_retval invalid positive value")
__failure __msg("should have been in [-4095, 0]")
int BPF_PROG(lsm_cgroup_set_retval_positive_invalid, int family, int type, int protocol, int kern)
{
	bpf_set_retval(1);
	return 0;
}

SEC("cgroup/dev")
__description("cgroup_device bpf_set_retval success")
__success
int cgroup_dev_set_retval_0(struct bpf_cgroup_dev_ctx *ctx)
{
	bpf_set_retval(0);
	return 1;
}

SEC("cgroup/dev")
__description("cgroup_device bpf_set_retval valid errno")
__success
int cgroup_dev_set_retval_neg_maxerrno(struct bpf_cgroup_dev_ctx *ctx)
{
	bpf_set_retval(-4095);
	return 1;
}

SEC("cgroup/dev")
__description("cgroup_device bpf_set_retval invalid positive value")
__failure __msg("should have been in [-4095, 0]")
int cgroup_dev_set_retval_1(struct bpf_cgroup_dev_ctx *ctx)
{
	bpf_set_retval(1);
	return 1;
}

SEC("cgroup/dev")
__description("cgroup_device bpf_set_retval invalid negative value")
__failure __msg("should have been in [-4095, 0]")
int cgroup_dev_set_retval_neg_4096(struct bpf_cgroup_dev_ctx *ctx)
{
	bpf_set_retval(-4096);
	return 1;
}

SEC("cgroup/dev")
__description("bpf_set_retval bounds check survives state pruning")
__failure __msg("should have been in [-4095, 0]")
__naked int cgroup_dev_set_retval_pruning_bypass(struct bpf_cgroup_dev_ctx *ctx)
{
	asm volatile (
		"call %[bpf_get_prandom_u32];"
		"if r0 != 0 goto 1f;"
		"r0 = r0;"
		"r0 = r0;"
		"r0 = r0;"
		"r0 = r0;"
		"goto 2f;"
	"1:"
		"call %[bpf_get_prandom_u32];"
	"2:"
		"r1 = r0;"
		"call %[bpf_set_retval];"
		"r0 = 1;"
		"exit;"
		:
		: __imm(bpf_get_prandom_u32),
		  __imm(bpf_set_retval)
		: __clobber_common
	);
}

char _license[] SEC("license") = "GPL";
