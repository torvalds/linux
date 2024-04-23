// SPDX-License-Identifier: GPL-2.0-only
/*
 * amx tests
 *
 * Copyright (C) 2021, Intel, Inc.
 *
 * Tests for amx #NM exception and save/restore.
 */
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
#define PALETTE_TABLE_INDEX		1
#define MAX_TILES			16
#define RESERVED_BYTES			14

#define XSAVE_HDR_OFFSET		512

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

static inline void __xsavec(struct xstate *xstate, uint64_t rfbm)
{
	uint32_t rfbm_lo = rfbm;
	uint32_t rfbm_hi = rfbm >> 32;

	asm volatile("xsavec (%%rdi)"
		     : : "D" (xstate), "a" (rfbm_lo), "d" (rfbm_hi)
		     : "memory");
}

static void check_xtile_info(void)
{
	GUEST_ASSERT(this_cpu_has_p(X86_PROPERTY_XSTATE_MAX_SIZE_XCR0));
	GUEST_ASSERT(this_cpu_property(X86_PROPERTY_XSTATE_MAX_SIZE_XCR0) <= XSAVE_SIZE);

	xtile.xsave_offset = this_cpu_property(X86_PROPERTY_XSTATE_TILE_OFFSET);
	GUEST_ASSERT(xtile.xsave_offset == 2816);
	xtile.xsave_size = this_cpu_property(X86_PROPERTY_XSTATE_TILE_SIZE);
	GUEST_ASSERT(xtile.xsave_size == 8192);
	GUEST_ASSERT(sizeof(struct tile_data) >= xtile.xsave_size);

	GUEST_ASSERT(this_cpu_has_p(X86_PROPERTY_AMX_MAX_PALETTE_TABLES));
	GUEST_ASSERT(this_cpu_property(X86_PROPERTY_AMX_MAX_PALETTE_TABLES) >=
		     PALETTE_TABLE_INDEX);

	GUEST_ASSERT(this_cpu_has_p(X86_PROPERTY_AMX_NR_TILE_REGS));
	xtile.max_names = this_cpu_property(X86_PROPERTY_AMX_NR_TILE_REGS);
	GUEST_ASSERT(xtile.max_names == 8);
	xtile.bytes_per_tile = this_cpu_property(X86_PROPERTY_AMX_BYTES_PER_TILE);
	GUEST_ASSERT(xtile.bytes_per_tile == 1024);
	xtile.bytes_per_row = this_cpu_property(X86_PROPERTY_AMX_BYTES_PER_ROW);
	GUEST_ASSERT(xtile.bytes_per_row == 64);
	xtile.max_rows = this_cpu_property(X86_PROPERTY_AMX_MAX_ROWS);
	GUEST_ASSERT(xtile.max_rows == 16);
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

static void init_regs(void)
{
	uint64_t cr4, xcr0;

	GUEST_ASSERT(this_cpu_has(X86_FEATURE_XSAVE));

	/* turn on CR4.OSXSAVE */
	cr4 = get_cr4();
	cr4 |= X86_CR4_OSXSAVE;
	set_cr4(cr4);
	GUEST_ASSERT(this_cpu_has(X86_FEATURE_OSXSAVE));

	xcr0 = xgetbv(0);
	xcr0 |= XFEATURE_MASK_XTILE;
	xsetbv(0x0, xcr0);
	GUEST_ASSERT((xgetbv(0) & XFEATURE_MASK_XTILE) == XFEATURE_MASK_XTILE);
}

static void __attribute__((__flatten__)) guest_code(struct tile_config *amx_cfg,
						    struct tile_data *tiledata,
						    struct xstate *xstate)
{
	init_regs();
	check_xtile_info();
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
	/*
	 * After XSAVEC, XTILEDATA is cleared in the xstate_bv but is set in
	 * the xcomp_bv.
	 */
	xstate->header.xstate_bv = XFEATURE_MASK_XTILE_DATA;
	__xsavec(xstate, XFEATURE_MASK_XTILE_DATA);
	GUEST_ASSERT(!(xstate->header.xstate_bv & XFEATURE_MASK_XTILE_DATA));
	GUEST_ASSERT(xstate->header.xcomp_bv & XFEATURE_MASK_XTILE_DATA);

	/* xfd=0x40000, disable amx tiledata */
	wrmsr(MSR_IA32_XFD, XFEATURE_MASK_XTILE_DATA);

	/*
	 * XTILEDATA is cleared in xstate_bv but set in xcomp_bv, this property
	 * remains the same even when amx tiledata is disabled by IA32_XFD.
	 */
	xstate->header.xstate_bv = XFEATURE_MASK_XTILE_DATA;
	__xsavec(xstate, XFEATURE_MASK_XTILE_DATA);
	GUEST_ASSERT(!(xstate->header.xstate_bv & XFEATURE_MASK_XTILE_DATA));
	GUEST_ASSERT((xstate->header.xcomp_bv & XFEATURE_MASK_XTILE_DATA));

	GUEST_SYNC(6);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD) == XFEATURE_MASK_XTILE_DATA);
	set_tilecfg(amx_cfg);
	__ldtilecfg(amx_cfg);
	/* Trigger #NM exception */
	__tileloadd(tiledata);
	GUEST_SYNC(10);

	GUEST_DONE();
}

void guest_nm_handler(struct ex_regs *regs)
{
	/* Check if #NM is triggered by XFEATURE_MASK_XTILE_DATA */
	GUEST_SYNC(7);
	GUEST_ASSERT(!(get_cr0() & X86_CR0_TS));
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD_ERR) == XFEATURE_MASK_XTILE_DATA);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD) == XFEATURE_MASK_XTILE_DATA);
	GUEST_SYNC(8);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD_ERR) == XFEATURE_MASK_XTILE_DATA);
	GUEST_ASSERT(rdmsr(MSR_IA32_XFD) == XFEATURE_MASK_XTILE_DATA);
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
	struct kvm_x86_state *state;
	int xsave_restore_size;
	vm_vaddr_t amx_cfg, tiledata, xstate;
	struct ucall uc;
	u32 amx_offset;
	int ret;

	/*
	 * Note, all off-by-default features must be enabled before anything
	 * caches KVM_GET_SUPPORTED_CPUID, e.g. before using kvm_cpu_has().
	 */
	vm_xsave_require_permission(XFEATURE_MASK_XTILE_DATA);

	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XFD));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XSAVE));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_AMX_TILE));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XTILECFG));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XTILEDATA));
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_XTILEDATA_XFD));

	/* Create VM */
	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	TEST_ASSERT(kvm_cpu_has_p(X86_PROPERTY_XSTATE_MAX_SIZE),
		    "KVM should enumerate max XSAVE size when XSAVE is supported");
	xsave_restore_size = kvm_cpu_property(X86_PROPERTY_XSTATE_MAX_SIZE);

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

	/* XSAVE state for guest_code */
	xstate = vm_vaddr_alloc_pages(vm, DIV_ROUND_UP(XSAVE_SIZE, PAGE_SIZE));
	memset(addr_gva2hva(vm, xstate), 0, PAGE_SIZE * DIV_ROUND_UP(XSAVE_SIZE, PAGE_SIZE));
	vcpu_args_set(vcpu, 3, amx_cfg, tiledata, xstate);

	for (;;) {
		vcpu_run(vcpu);
		TEST_ASSERT_KVM_EXIT_REASON(vcpu, KVM_EXIT_IO);

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
				TEST_ASSERT(ret == 0, "memcmp failed, ret=%d", ret);
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
