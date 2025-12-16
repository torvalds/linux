// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

SEC("lsm/file_permission")
__description("lsm bpf prog with -4095~0 retval. test 1")
__success
__naked int errno_zero_retval_test1(void *ctx)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_permission")
__description("lsm bpf prog with -4095~0 retval. test 2")
__success
__naked int errno_zero_retval_test2(void *ctx)
{
	asm volatile (
	"r0 = -4095;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_mprotect")
__description("lsm bpf prog with -4095~0 retval. test 4")
__failure __msg("R0 has smin=-4096 smax=-4096 should have been in [-4095, 0]")
__naked int errno_zero_retval_test4(void *ctx)
{
	asm volatile (
	"r0 = -4096;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_mprotect")
__description("lsm bpf prog with -4095~0 retval. test 5")
__failure __msg("R0 has smin=4096 smax=4096 should have been in [-4095, 0]")
__naked int errno_zero_retval_test5(void *ctx)
{
	asm volatile (
	"r0 = 4096;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_mprotect")
__description("lsm bpf prog with -4095~0 retval. test 6")
__failure __msg("R0 has smin=1 smax=1 should have been in [-4095, 0]")
__naked int errno_zero_retval_test6(void *ctx)
{
	asm volatile (
	"r0 = 1;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/audit_rule_known")
__description("lsm bpf prog with bool retval. test 1")
__success
__naked int bool_retval_test1(void *ctx)
{
	asm volatile (
	"r0 = 1;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/audit_rule_known")
__description("lsm bpf prog with bool retval. test 2")
__success
__success
__naked int bool_retval_test2(void *ctx)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/audit_rule_known")
__description("lsm bpf prog with bool retval. test 3")
__failure __msg("R0 has smin=-1 smax=-1 should have been in [0, 1]")
__naked int bool_retval_test3(void *ctx)
{
	asm volatile (
	"r0 = -1;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/audit_rule_known")
__description("lsm bpf prog with bool retval. test 4")
__failure __msg("R0 has smin=2 smax=2 should have been in [0, 1]")
__naked int bool_retval_test4(void *ctx)
{
	asm volatile (
	"r0 = 2;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_free_security")
__success
__description("lsm bpf prog with void retval. test 1")
__naked int void_retval_test1(void *ctx)
{
	asm volatile (
	"r0 = -4096;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/file_free_security")
__success
__description("lsm bpf prog with void retval. test 2")
__naked int void_retval_test2(void *ctx)
{
	asm volatile (
	"r0 = 4096;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/getprocattr")
__description("lsm disabled hook: getprocattr")
__failure __msg("points to disabled hook")
__naked int disabled_hook_test1(void *ctx)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/setprocattr")
__description("lsm disabled hook: setprocattr")
__failure __msg("points to disabled hook")
__naked int disabled_hook_test2(void *ctx)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/ismaclabel")
__description("lsm disabled hook: ismaclabel")
__failure __msg("points to disabled hook")
__naked int disabled_hook_test3(void *ctx)
{
	asm volatile (
	"r0 = 0;"
	"exit;"
	::: __clobber_all);
}

SEC("lsm/mmap_file")
__description("not null checking nullable pointer in bpf_lsm_mmap_file")
__failure __msg("R1 invalid mem access 'trusted_ptr_or_null_'")
int BPF_PROG(no_null_check, struct file *file)
{
	struct inode *inode;

	inode = file->f_inode;
	__sink(inode);

	return 0;
}

SEC("lsm/mmap_file")
__description("null checking nullable pointer in bpf_lsm_mmap_file")
__success
int BPF_PROG(null_check, struct file *file)
{
	struct inode *inode;

	if (file) {
		inode = file->f_inode;
		__sink(inode);
	}

	return 0;
}

char _license[] SEC("license") = "GPL";
