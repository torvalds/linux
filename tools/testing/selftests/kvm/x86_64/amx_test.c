// SPDX-License-Identifier: GPL-2.0-only
/*
 * amx tests
 *
 * Copyright (C) 2021, Intel, Inc.
 *
 * Tests for amx #NM exception and save/restore.
 */

#define _GNU_SOURCE /* for program_invocation_short_name */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>

#include "test_util.h"

#include "kvm_util.h"
#include "processor.h"
#include "vmx.h"

#ifndef __x86_64__
# error This test is 64-bit only
#endif

#define NUM_TILES			8
#define TILE_SIZE			1024
#define XSAVE_SIZE			((NUM_TILES * TILE_SIZE) + PAGE_SIZE)

/* Tile configuration associated: */
#define MAX_TILES			16
#define RESERVED_BYTES			14

#define XFEATURE_XTILECFG		17
#define XFEATURE_XTILEDATA		18
#define XFEATURE_MASK_XTILECFG		(1 << XFEATURE_XTILECFG)
#define XFEATURE_MASK_XTILEDATA		(1 << XFEATURE_XTILEDATA)
#define XFEATURE_MASK_XTILE		(XFEATURE_MASK_XTILECFG | XFEATURE_MASK_XTILEDATA)

#define TILE_CPUID			0x1d
#define XSTATE_CPUID			0xd
#define TILE_PALETTE_CPUID_SUBLEAVE	0x1
#define XSTATE_USER_STATE_SUBLEAVE	0x0

#define XSAVE_HDR_OFFSET		512

struct xsave_data {
	u8 area[XSAVE_SIZE];
} __aligned(64);

struct tile_config {
	u8  palette_id;
	u8  start_row;
	u8  reserved[RESERVED_BYTES];
	u16 colsb[MAX_TILES];
	u8  rows[MAX_TILES];
};

struct tile_data {
	u8 data[NUM_TILES * TILE_SIZE];
};

struct xtile_info {
	u16 bytes_per_tile;
	u16 bytes_per_row;
	u16 max_names;
	u16 max_rows;
	u32 xsave_offset;
	u32 xsave_size;
};

static struct xtile_info xtile;

static inline u64 __xgetbv(u32 index)
{
	u32 eax, edx;

	asm volatile("xgetbv;"
		     : "=a" (eax), "=d" (edx)
		     : "c" (index));
	return eax + ((u64)edx << 32);
}

static inline void __xsetbv(u32 index, u64 value)
{
	u32 eax = value;
	u32 edx = value >> 32;

	asm volatile("xsetbv" :: "a" (eax), "d" (edx), "c" (index));
}

static inline void __ldtilecfg(void *cfg)
{
	asm volatile(".byte 0xc4,0xe2,0x78,0x49,0x00"
		     : : "a"(cfg));
}

static inline void __tileloadd(void *tile)
{
	asm volatile(".byte 0xc4,0xe2,0x7b,0x4b,0x04,0x10"
		     : : "a"(tile), "d"(0));
}

static inline void __tilerelease(void)
{
	asm volatile(".byte 0xc4, 0xe2, 0x78, 0x49, 0xc0" ::);
}

static inline void __xsavec(struct xsave_data *data, uint64_t rfbm)
{
	uint32_t rfbm_lo = rfbm;
	uint32_t rfbm_hi = rfbm >> 32;

	asm volatile("xsavec (%%rdi)"
		     : : "D" (data), "a" (rfbm_lo), "d" (rfbm_hi)
		     : "memory");
}

static inline void check_cpuid_xsave(void)
{
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_XSAVE));
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));
}

static bool check_xsave_supports_xtile(void)
{
	return __xgetbv(0) & XFEATURE_MASK_XTILE;
}

static bool enum_xtile_config(void)
{
	u32 eax, ebx, ecx, edx;

	__cpuid(TILE_CPUID, TILE_PALETTE_CPUID_SUBLEAVE, &eax, &ebx, &ecx, &edx);
	if (!eax || !ebx || !ecx)
		return false;

	xtile.max_names = ebx >> 16;
	if (xtile.max_names < NUM_TILES)
		return false;

	xtile.bytes_per_tile = eax >> 16;
	if (xtile.bytes_per_tile < TILE_SIZE)
		return false;

	xtile.bytes_per_row = ebx;
	xtile.max_rows = ecx;

	return true;
}

static bool enum_xsave_tile(void)
{
	u32 eax, ebx, ecx, edx;

	__cpuid(XSTATE_CPUID, XFEATURE_XTILEDATA, &eax, &ebx, &ecx, &edx);
	if (!eax || !ebx)
		return false;

	xtile.xsave_offset = ebx;
	xtile.xsave_size = eax;

	return true;
}

static bool check_xsave_size(void)
{
	u32 eax, ebx, ecx, edx;
	bool valid = false;

	__cpuid(XSTATE_CPUID, XSTATE_USER_STATE_SUBLEAVE, &eax, &ebx, &ecx, &edx);
	if (ebx && ebx <= XSAVE_SIZE)
		valid = true;

	return valid;
}

static bool check_xtile_info(void)
{
	bool ret = false;

	if (!check_xsave_size())
		return ret;

	if (!enum_xsave_tile())
		return ret;

	if (!enum_xtile_config())
		return ret;

	if (sizeof(struct tile_data) >= xtile.xsave_size)
		ret = true;

	return ret;
}

static void set_tilecfg(struct tile_config *cfg)
{
	int i;

	/* Only palette id 1 */
	cfg->palette_id = 1;
	for (i = 0; i < xtile.max_names; i++) {
		cfg->colsb[i] = xtile.bytes_per_row;
		cfg->rows[i] = xtile.max_rows;
	}
}

static void set_xstatebv(void *data, uint64_t bv)
{
	*(uint64_t *)(data + XSAVE_HDR_OFFSET) = bv;
}

static u64 get_xstatebv(void *data)
{
	return *(u64 *)(data + XSAVE_HDR_OFFSET);
}

static void init_regs(void)
{
	uint64_t cr4, xcr0;

	/* turn on CR4.OSXSAVE */
	cr4 = get_cr4();
	cr4 |= X86_CR4_OSXSAVE;
	set_cr4(cr4);

	xcr0 = __xgetbv(0);
	xcr0 |= XFEATURE_MASK_XTILE;
	__xsetbv(0x0, xcr0);
}

static void __attribute__((__flatten__)) guest_code(struct tile_config *amx_cfg,
						    struct tile_data *tiledata,
						    struct xsave_data *xsave_data)
{
	init_regs();
	check_cpuid_xsave();
	GUEST_ASSERT(check_xsave_supports_xtile());
	GUEST_ASSERT(check_xtile_info());

	/* check xtile configs */
	GUEST_ASSERT(xtile.xsave_offset == 2816);
	GUEST_ASSERT(xtile.xsave_size == 8192);
	GUEST_ASSERT(xtile.max_names == 8);
	GUEST_ASSERT(xtile.bytes_per_tile == 1024);
	GUEST_ASSERT(xtile.bytes_per_row == 64);
	GUEST_ASSERT(xtile.max_rows == 16);
	GUEST_SYNC(1);

	/* xfd=0, enable amx */
	wrmsr(MSR_IA32_XFD, 0);
	GUEST_SYNC(2);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD) == 0);
	set_tilecfg(amx_cfg);
	__ldtilecfg(amx_cfg);
	GUEST_SYNC(3);
	/* Check save/restore when trap to userspace */
	__tileloadd(tiledata);
	GUEST_SYNC(4);
	__tilerelease();
	GUEST_SYNC(5);
	/* bit 18 not in the XCOMP_BV after xsavec() */
	set_xstatebv(xsave_data, XFEATURE_MASK_XTILEDATA);
	__xsavec(xsave_data, XFEATURE_MASK_XTILEDATA);
	GUEST_ASSERT((get_xstatebv(xsave_data) & XFEATURE_MASK_XTILEDATA) == 0);

	/* xfd=0x40000, disable amx tiledata */
	wrmsr(MSR_IA32_XFD, XFEATURE_MASK_XTILEDATA);
	GUEST_SYNC(6);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD) == XFEATURE_MASK_XTILEDATA);
	set_tilecfg(amx_cfg);
	__ldtilecfg(amx_cfg);
	/* Trigger #NM exception */
	__tileloadd(tiledata);
	GUEST_SYNC(10);

	GUEST_DONE();
}

void guest_nm_handler(struct ex_regs *regs)
{
	/* Check if #NM is triggered by XFEATURE_MASK_XTILEDATA */
	GUEST_SYNC(7);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD_ERR) == XFEATURE_MASK_XTILEDATA);
	GUEST_SYNC(8);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD_ERR) == XFEATURE_MASK_XTILEDATA);
	/* Clear xfd_err */
	wrmsr(MSR_IA32_XFD_ERR, 0);
	/* xfd=0, enable amx */
	wrmsr(MSR_IA32_XFD, 0);
	GUEST_SYNC(9);
}

int main(int argc, char *argv[])
{
	struct kvm_regs regs1, regs2;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct kvm_run *run;
	struct kvm_x86_state *state;
	int xsave_restore_size;
	vm_vaddr_t amx_cfg, tiledata, xsavedata;
	struct ucall uc;
	u32 amx_offset;
	int stage, ret;

	vm_xsave_require_permission(XSTATE_XTILE_DATA_BIT);

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_AMX_TILE));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XTILECFG));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XTILEDATA));

	/* Get xsave/restore max size */
	xsave_restore_size = kvm_get_supported_cpuid_entry(0xd)->ecx;

	run = vcpu->run;
	vcpu_regs_get(vcpu, &regs1);

	/* Register #NM handler */
	vm_init_descriptor_tables(vm);
	vcpu_init_descriptor_tables(vcpu);
	vm_install_exception_handler(vm, NM_VECTOR, guest_nm_handler);

	/* amx cfg for guest_code */
	amx_cfg = vm_vaddr_alloc_page(vm);
	memset(addr_gva2hva(vm, amx_cfg), 0x0, getpagesize());

	/* amx tiledata for guest_code */
	tiledata = vm_vaddr_alloc_pages(vm, 2);
	memset(addr_gva2hva(vm, tiledata), rand() | 1, 2 * getpagesize());

	/* xsave data for guest_code */
	xsavedata = vm_vaddr_alloc_pages(vm, 3);
	memset(addr_gva2hva(vm, xsavedata), 0, 3 * getpagesize());
	vcpu_args_set(vcpu, 3, amx_cfg, tiledata, xsavedata);

	for (stage = 1; ; stage++) {
		vcpu_run(vcpu);
		TEST_ASSERT(run->exit_reason == KVM_EXIT_IO,
			    "Stage %d: unexpected exit reason: %u (%s),\n",
			    stage, run->exit_reason,
			    exit_reason_str(run->exit_reason));

		switch (get_ucall(vcpu, &uc)) {
		case UCALL_ABORT:
			REPORT_GUEST_ASSERT(uc);
			/* NOT REACHED */
		case UCALL_SYNC:
			switch (uc.args[1]) {
			case 1:
			case 2:
			case 3:
			case 5:
			case 6:
			case 7:
			case 8:
				fprintf(stderr, "GUEST_SYNC(%ld)\n", uc.args[1]);
				break;
			case 4:
			case 10:
				fprintf(stderr,
				"GUEST_SYNC(%ld), check save/restore status\n", uc.args[1]);

				/* Compacted mode, get amx offset by xsave area
				 * size subtract 8K amx size.
				 */
				amx_offset = xsave_restore_size - NUM_TILES*TILE_SIZE;
				state = vcpu_save_state(vcpu);
				void *amx_start = (void *)state->xsave + amx_offset;
				void *tiles_data = (void *)addr_gva2hva(vm, tiledata);
				/* Only check TMM0 register, 1 tile */
				ret = memcmp(amx_start, tiles_data, TILE_SIZE);
				TEST_ASSERT(ret == 0, "memcmp failed, ret=%d\n", ret);
				kvm_x86_state_cleanup(state);
				break;
			case 9:
				fprintf(stderr,
				"GUEST_SYNC(%ld), #NM exception and enable amx\n", uc.args[1]);
				break;
			}
			break;
		case UCALL_DONE:
			fprintf(stderr, "UCALL_DONE\n");
			goto done;
		default:
			TEST_FAIL("Unknown ucall %lu", uc.cmd);
		}

		state = vcpu_save_state(vcpu);
		memset(&regs1, 0, sizeof(regs1));
		vcpu_regs_get(vcpu, &regs1);

		kvm_vm_release(vm);

		/* Restore state in a new VM.  */
		vcpu = vm_recreate_with_one_vcpu(vm);
		vcpu_load_state(vcpu, state);
		run = vcpu->run;
		kvm_x86_state_cleanup(state);

		memset(&regs2, 0, sizeof(regs2));
		vcpu_regs_get(vcpu, &regs2);
		TEST_ASSERT(!memcmp(&regs1, &regs2, sizeof(regs2)),
			    "Unexpected register values after vcpu_load_state; rdi: %lx rsi: %lx",
			    (ulong) regs2.rdi, (ulong) regs2.rsi);
	}
done:
	kvm_vm_free(vm);
}
