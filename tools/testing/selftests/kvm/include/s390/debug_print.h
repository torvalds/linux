/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Definition for kernel virtual machines on s390x
 *
 * Copyright IBM Corp. 2024
 *
 * Authors:
 *  Christoph Schlameuss <schlameuss@linux.ibm.com>
 */

#ifndef SELFTEST_KVM_DEBUG_PRINT_H
#define SELFTEST_KVM_DEBUG_PRINT_H

#include "asm/ptrace.h"
#include "kvm_util.h"
#include "sie.h"

static inline void print_hex_bytes(const char *name, u64 addr, size_t len)
{
	u64 pos;

	pr_debug("%s (%p)\n", name, (void *)addr);
	pr_debug("            0/0x00---------|");
	if (len > 8)
		pr_debug(" 8/0x08---------|");
	if (len > 16)
		pr_debug(" 16/0x10--------|");
	if (len > 24)
		pr_debug(" 24/0x18--------|");
	for (pos = 0; pos < len; pos += 8) {
		if ((pos % 32) == 0)
			pr_debug("\n %3lu 0x%.3lx ", pos, pos);
		pr_debug(" %16lx", *((u64 *)(addr + pos)));
	}
	pr_debug("\n");
}

static inline void print_hex(const char *name, u64 addr)
{
	print_hex_bytes(name, addr, 512);
}

static inline void print_psw(struct kvm_run *run, struct kvm_s390_sie_block *sie_block)
{
	pr_debug("flags:0x%x psw:0x%.16llx:0x%.16llx exit:%u %s\n",
		 run->flags,
		 run->psw_mask, run->psw_addr,
		 run->exit_reason, exit_reason_str(run->exit_reason));
	pr_debug("sie_block psw:0x%.16llx:0x%.16llx\n",
		 sie_block->psw_mask, sie_block->psw_addr);
}

static inline void print_run(struct kvm_run *run, struct kvm_s390_sie_block *sie_block)
{
	print_hex_bytes("run", (u64)run, 0x150);
	print_hex("sie_block", (u64)sie_block);
	print_psw(run, sie_block);
}

static inline void print_regs(struct kvm_run *run)
{
	struct kvm_sync_regs *sync_regs = &run->s.regs;

	print_hex_bytes("GPRS", (u64)sync_regs->gprs, 8 * NUM_GPRS);
	print_hex_bytes("ACRS", (u64)sync_regs->acrs, 4 * NUM_ACRS);
	print_hex_bytes("CRS", (u64)sync_regs->crs, 8 * NUM_CRS);
}

#endif /* SELFTEST_KVM_DEBUG_PRINT_H */
