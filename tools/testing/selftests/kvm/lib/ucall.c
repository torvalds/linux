// SPDX-License-Identifier: GPL-2.0
/*
 * ucall support. A ucall is a "hypercall to userspace".
 *
 * Copyright (C) 2018, Red Hat, Inc.
 */
#include "kvm_util.h"
#include "kvm_util_internal.h"

#define UCALL_PIO_PORT ((uint16_t)0x1000)

static ucall_type_t ucall_type;
static vm_vaddr_t *ucall_exit_mmio_addr;

static bool ucall_mmio_init(struct kvm_vm *vm, vm_paddr_t gpa)
{
	if (kvm_userspace_memory_region_find(vm, gpa, gpa + 1))
		return false;

	virt_pg_map(vm, gpa, gpa, 0);

	ucall_exit_mmio_addr = (vm_vaddr_t *)gpa;
	sync_global_to_guest(vm, ucall_exit_mmio_addr);

	return true;
}

void ucall_init(struct kvm_vm *vm, ucall_type_t type, void *arg)
{
	ucall_type = type;
	sync_global_to_guest(vm, ucall_type);

	if (type == UCALL_PIO)
		return;

	if (type == UCALL_MMIO) {
		vm_paddr_t gpa, start, end, step, offset;
		unsigned bits;
		bool ret;

		if (arg) {
			gpa = (vm_paddr_t)arg;
			ret = ucall_mmio_init(vm, gpa);
			TEST_ASSERT(ret, "Can't set ucall mmio address to %lx", gpa);
			return;
		}

		/*
		 * Find an address within the allowed physical and virtual address
		 * spaces, that does _not_ have a KVM memory region associated with
		 * it. Identity mapping an address like this allows the guest to
		 * access it, but as KVM doesn't know what to do with it, it
		 * will assume it's something userspace handles and exit with
		 * KVM_EXIT_MMIO. Well, at least that's how it works for AArch64.
		 * Here we start with a guess that the addresses around 5/8th
		 * of the allowed space are unmapped and then work both down and
		 * up from there in 1/16th allowed space sized steps.
		 *
		 * Note, we need to use VA-bits - 1 when calculating the allowed
		 * virtual address space for an identity mapping because the upper
		 * half of the virtual address space is the two's complement of the
		 * lower and won't match physical addresses.
		 */
		bits = vm->va_bits - 1;
		bits = vm->pa_bits < bits ? vm->pa_bits : bits;
		end = 1ul << bits;
		start = end * 5 / 8;
		step = end / 16;
		for (offset = 0; offset < end - start; offset += step) {
			if (ucall_mmio_init(vm, start - offset))
				return;
			if (ucall_mmio_init(vm, start + offset))
				return;
		}
		TEST_ASSERT(false, "Can't find a ucall mmio address");
	}
}

void ucall_uninit(struct kvm_vm *vm)
{
	ucall_type = 0;
	sync_global_to_guest(vm, ucall_type);
	ucall_exit_mmio_addr = 0;
	sync_global_to_guest(vm, ucall_exit_mmio_addr);
}

static void ucall_pio_exit(struct ucall *uc)
{
#ifdef __x86_64__
	asm volatile("in %[port], %%al"
		: : [port] "d" (UCALL_PIO_PORT), "D" (uc) : "rax");
#endif
}

static void ucall_mmio_exit(struct ucall *uc)
{
	*ucall_exit_mmio_addr = (vm_vaddr_t)uc;
}

void ucall(uint64_t cmd, int nargs, ...)
{
	struct ucall uc = {
		.cmd = cmd,
	};
	va_list va;
	int i;

	nargs = nargs <= UCALL_MAX_ARGS ? nargs : UCALL_MAX_ARGS;

	va_start(va, nargs);
	for (i = 0; i < nargs; ++i)
		uc.args[i] = va_arg(va, uint64_t);
	va_end(va);

	switch (ucall_type) {
	case UCALL_PIO:
		ucall_pio_exit(&uc);
		break;
	case UCALL_MMIO:
		ucall_mmio_exit(&uc);
		break;
	};
}

uint64_t get_ucall(struct kvm_vm *vm, uint32_t vcpu_id, struct ucall *uc)
{
	struct kvm_run *run = vcpu_state(vm, vcpu_id);

	memset(uc, 0, sizeof(*uc));

#ifdef __x86_64__
	if (ucall_type == UCALL_PIO && run->exit_reason == KVM_EXIT_IO &&
	    run->io.port == UCALL_PIO_PORT) {
		struct kvm_regs regs;
		vcpu_regs_get(vm, vcpu_id, &regs);
		memcpy(uc, addr_gva2hva(vm, (vm_vaddr_t)regs.rdi), sizeof(*uc));
		return uc->cmd;
	}
#endif
	if (ucall_type == UCALL_MMIO && run->exit_reason == KVM_EXIT_MMIO &&
	    run->mmio.phys_addr == (uint64_t)ucall_exit_mmio_addr) {
		vm_vaddr_t gva;
		TEST_ASSERT(run->mmio.is_write && run->mmio.len == 8,
			    "Unexpected ucall exit mmio address access");
		gva = *(vm_vaddr_t *)run->mmio.data;
		memcpy(uc, addr_gva2hva(vm, gva), sizeof(*uc));
	}

	return uc->cmd;
}
