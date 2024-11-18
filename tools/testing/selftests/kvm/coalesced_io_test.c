// SPDX-License-Identifier: GPL-2.0
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/sizes.h>

#include <kvm_util.h>
#include <processor.h>

#include "ucall_common.h"

struct kvm_coalesced_io {
	struct kvm_coalesced_mmio_ring *ring;
	uint32_t ring_size;
	uint64_t mmio_gpa;
	uint64_t *mmio;

	/*
	 * x86-only, but define pio_port for all architectures to minimize the
	 * amount of #ifdeffery and complexity, without having to sacrifice
	 * verbose error messages.
	 */
	uint8_t pio_port;
};

static struct kvm_coalesced_io kvm_builtin_io_ring;

#ifdef __x86_64__
static const int has_pio = 1;
#else
static const int has_pio = 0;
#endif

static void guest_code(struct kvm_coalesced_io *io)
{
	int i, j;

	for (;;) {
		for (j = 0; j < 1 + has_pio; j++) {
			/*
			 * KVM always leaves one free entry, i.e. exits to
			 * userspace before the last entry is filled.
			 */
			for (i = 0; i < io->ring_size - 1; i++) {
#ifdef __x86_64__
				if (i & 1)
					outl(io->pio_port, io->pio_port + i);
				else
#endif
					WRITE_ONCE(*io->mmio, io->mmio_gpa + i);
			}
#ifdef __x86_64__
			if (j & 1)
				outl(io->pio_port, io->pio_port + i);
			else
#endif
				WRITE_ONCE(*io->mmio, io->mmio_gpa + i);
		}
		GUEST_SYNC(0);

		WRITE_ONCE(*io->mmio, io->mmio_gpa + i);
#ifdef __x86_64__
		outl(io->pio_port, io->pio_port + i);
#endif
	}
}

static void vcpu_run_and_verify_io_exit(struct kvm_vcpu *vcpu,
					struct kvm_coalesced_io *io,
					uint32_t ring_start,
					uint32_t expected_exit)
{
	const bool want_pio = expected_exit == KVM_EXIT_IO;
	struct kvm_coalesced_mmio_ring *ring = io->ring;
	struct kvm_run *run = vcpu->run;
	uint32_t pio_value;

	WRITE_ONCE(ring->first, ring_start);
	WRITE_ONCE(ring->last, ring_start);

	vcpu_run(vcpu);

	/*
	 * Annoyingly, reading PIO data is safe only for PIO exits, otherwise
	 * data_offset is garbage, e.g. an MMIO gpa.
	 */
	if (run->exit_reason == KVM_EXIT_IO)
		pio_value = *(uint32_t *)((void *)run + run->io.data_offset);
	else
		pio_value = 0;

	TEST_ASSERT((!want_pio && (run->exit_reason == KVM_EXIT_MMIO && run->mmio.is_write &&
				   run->mmio.phys_addr == io->mmio_gpa && run->mmio.len == 8 &&
				   *(uint64_t *)run->mmio.data == io->mmio_gpa + io->ring_size - 1)) ||
		    (want_pio  && (run->exit_reason == KVM_EXIT_IO && run->io.port == io->pio_port &&
				   run->io.direction == KVM_EXIT_IO_OUT && run->io.count == 1 &&
				   pio_value == io->pio_port + io->ring_size - 1)),
		    "For start = %u, expected exit on %u-byte %s write 0x%llx = %lx, got exit_reason = %u (%s)\n  "
		    "(MMIO addr = 0x%llx, write = %u, len = %u, data = %lx)\n  "
		    "(PIO port = 0x%x, write = %u, len = %u, count = %u, data = %x",
		    ring_start, want_pio ? 4 : 8, want_pio ? "PIO" : "MMIO",
		    want_pio ? (unsigned long long)io->pio_port : io->mmio_gpa,
		    (want_pio ? io->pio_port : io->mmio_gpa) + io->ring_size - 1, run->exit_reason,
		    run->exit_reason == KVM_EXIT_MMIO ? "MMIO" : run->exit_reason == KVM_EXIT_IO ? "PIO" : "other",
		    run->mmio.phys_addr, run->mmio.is_write, run->mmio.len, *(uint64_t *)run->mmio.data,
		    run->io.port, run->io.direction, run->io.size, run->io.count, pio_value);
}

static void vcpu_run_and_verify_coalesced_io(struct kvm_vcpu *vcpu,
					     struct kvm_coalesced_io *io,
					     uint32_t ring_start,
					     uint32_t expected_exit)
{
	struct kvm_coalesced_mmio_ring *ring = io->ring;
	int i;

	vcpu_run_and_verify_io_exit(vcpu, io, ring_start, expected_exit);

	TEST_ASSERT((ring->last + 1) % io->ring_size == ring->first,
		    "Expected ring to be full (minus 1), first = %u, last = %u, max = %u, start = %u",
		    ring->first, ring->last, io->ring_size, ring_start);

	for (i = 0; i < io->ring_size - 1; i++) {
		uint32_t idx = (ring->first + i) % io->ring_size;
		struct kvm_coalesced_mmio *entry = &ring->coalesced_mmio[idx];

#ifdef __x86_64__
		if (i & 1)
			TEST_ASSERT(entry->phys_addr == io->pio_port &&
				    entry->len == 4 && entry->pio &&
				    *(uint32_t *)entry->data == io->pio_port + i,
				    "Wanted 4-byte port I/O 0x%x = 0x%x in entry %u, got %u-byte %s 0x%llx = 0x%x",
				    io->pio_port, io->pio_port + i, i,
				    entry->len, entry->pio ? "PIO" : "MMIO",
				    entry->phys_addr, *(uint32_t *)entry->data);
		else
#endif
			TEST_ASSERT(entry->phys_addr == io->mmio_gpa &&
				    entry->len == 8 && !entry->pio,
				    "Wanted 8-byte MMIO to 0x%lx = %lx in entry %u, got %u-byte %s 0x%llx = 0x%lx",
				    io->mmio_gpa, io->mmio_gpa + i, i,
				    entry->len, entry->pio ? "PIO" : "MMIO",
				    entry->phys_addr, *(uint64_t *)entry->data);
	}
}

static void test_coalesced_io(struct kvm_vcpu *vcpu,
			      struct kvm_coalesced_io *io, uint32_t ring_start)
{
	struct kvm_coalesced_mmio_ring *ring = io->ring;

	kvm_vm_register_coalesced_io(vcpu->vm, io->mmio_gpa, 8, false /* pio */);
#ifdef __x86_64__
	kvm_vm_register_coalesced_io(vcpu->vm, io->pio_port, 8, true /* pio */);
#endif

	vcpu_run_and_verify_coalesced_io(vcpu, io, ring_start, KVM_EXIT_MMIO);
#ifdef __x86_64__
	vcpu_run_and_verify_coalesced_io(vcpu, io, ring_start, KVM_EXIT_IO);
#endif

	/*
	 * Verify ucall, which may use non-coalesced MMIO or PIO, generates an
	 * immediate exit.
	 */
	WRITE_ONCE(ring->first, ring_start);
	WRITE_ONCE(ring->last, ring_start);
	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_SYNC);
	TEST_ASSERT_EQ(ring->first, ring_start);
	TEST_ASSERT_EQ(ring->last, ring_start);

	/* Verify that non-coalesced MMIO/PIO generates an exit to userspace. */
	kvm_vm_unregister_coalesced_io(vcpu->vm, io->mmio_gpa, 8, false /* pio */);
	vcpu_run_and_verify_io_exit(vcpu, io, ring_start, KVM_EXIT_MMIO);

#ifdef __x86_64__
	kvm_vm_unregister_coalesced_io(vcpu->vm, io->pio_port, 8, true /* pio */);
	vcpu_run_and_verify_io_exit(vcpu, io, ring_start, KVM_EXIT_IO);
#endif
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int i;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_COALESCED_MMIO));

#ifdef __x86_64__
	TEST_REQUIRE(kvm_has_cap(KVM_CAP_COALESCED_PIO));
#endif

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	kvm_builtin_io_ring = (struct kvm_coalesced_io) {
		/*
		 * The I/O ring is a kernel-allocated page whose address is
		 * relative to each vCPU's run page, with the page offset
		 * provided by KVM in the return of KVM_CAP_COALESCED_MMIO.
		 */
		.ring = (void *)vcpu->run +
			(kvm_check_cap(KVM_CAP_COALESCED_MMIO) * getpagesize()),

		/*
		 * The size of the I/O ring is fixed, but KVM defines the sized
		 * based on the kernel's PAGE_SIZE.  Thus, userspace must query
		 * the host's page size at runtime to compute the ring size.
		 */
		.ring_size = (getpagesize() - sizeof(struct kvm_coalesced_mmio_ring)) /
			     sizeof(struct kvm_coalesced_mmio),

		/*
		 * Arbitrary address+port (MMIO mustn't overlap memslots), with
		 * the MMIO GPA identity mapped in the guest.
		 */
		.mmio_gpa = 4ull * SZ_1G,
		.mmio = (uint64_t *)(4ull * SZ_1G),
		.pio_port = 0x80,
	};

	virt_map(vm, (uint64_t)kvm_builtin_io_ring.mmio, kvm_builtin_io_ring.mmio_gpa, 1);

	sync_global_to_guest(vm, kvm_builtin_io_ring);
	vcpu_args_set(vcpu, 1, &kvm_builtin_io_ring);

	for (i = 0; i < kvm_builtin_io_ring.ring_size; i++)
		test_coalesced_io(vcpu, &kvm_builtin_io_ring, i);

	kvm_vm_free(vm);
	return 0;
}
