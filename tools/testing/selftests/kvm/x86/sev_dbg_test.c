// SPDX-License-Identifier: GPL-2.0-only
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "sev.h"

#define BUFFER_SIZE	(PAGE_SIZE * 2)

static u8 *data;
static u8 src[BUFFER_SIZE] __aligned(PAGE_SIZE);
static u8 dst[BUFFER_SIZE] __aligned(PAGE_SIZE);

static void validate_dst(int i, int nr_bytes, u8 pattern)
{
	for ( ; i < nr_bytes; i++)
		TEST_ASSERT(dst[i] == pattern,
			    "Expected 0x%x at byte %u, got 0x%x",
			    pattern, i, dst[i]);
}

static void validate_buffers(void)
{
	int i;

	for (i = 0; i < BUFFER_SIZE; i++)
		TEST_ASSERT(src[i] == dst[i],
			    "Expected src[%u] (0x%x) == dst[%u] (0x%x)",
			    i, src[i], i, dst[i]);
}

static void ____test_sev_dbg(struct kvm_vm *vm, int i, int j, int nr_bytes)
{
	u8 pattern = guest_random_u32(&guest_rng);

	if (i + nr_bytes > BUFFER_SIZE || j + nr_bytes > BUFFER_SIZE)
		return;

	memset(&src[i], pattern, nr_bytes);
	sev_encrypt_memory(vm, &data[j], &src[i], nr_bytes);
	sev_decrypt_memory(vm, &dst[i], &data[j], nr_bytes);
	validate_buffers();
	validate_dst(i, nr_bytes, pattern);
}

static void __test_sev_dbg(struct kvm_vm *vm, int nr_bytes)
{
	/*
	 * In a perfect world, all sizes at all combinations within the buffers
	 * would be tested.  In reality, even this much testing is quite slow.
	 * Target sizes and offsets around the chunk (16 bytes) and page (4096
	 * bytes) sizes.
	 */
	int x[] = { 1, 8, 15, 16, 23 };
	int p = PAGE_SIZE - 24;
	int i, j;

	____test_sev_dbg(vm, 0, 0, nr_bytes);

	for (i = 0; i < ARRAY_SIZE(x); i++) {
		for (j = 0; j < ARRAY_SIZE(x); j++) {
			____test_sev_dbg(vm, x[i], x[j], nr_bytes);
			____test_sev_dbg(vm, x[i], p + x[j], nr_bytes);
			____test_sev_dbg(vm, p + x[i], x[j], nr_bytes);
			____test_sev_dbg(vm, p + x[i], p + x[j], nr_bytes);
		}
	}
}

static void test_sev_dbg(u32 type, u64 policy)
{
	int sizes[] = { 1, 8, 15, 16, 17, 32, 33 };
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	int i;

	if (!(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(type)))
		return;

	vm = vm_sev_create_with_one_vcpu(type, NULL, &vcpu);

	data = addr_gva2hva(vm, vm_alloc(vm, BUFFER_SIZE, KVM_UTIL_MIN_VADDR));
	memset(data, 0xaa, BUFFER_SIZE);

	vm_sev_launch(vm, policy, NULL);

	sev_decrypt_memory(vm, dst, data, BUFFER_SIZE);
	validate_dst(0, BUFFER_SIZE, 0xaa);

	memset(src, 0x55, BUFFER_SIZE);
	sev_encrypt_memory(vm, data, src, BUFFER_SIZE);
	sev_decrypt_memory(vm, dst, data, BUFFER_SIZE);
	validate_dst(0, BUFFER_SIZE, 0x55);

	__test_sev_dbg(vm, PAGE_SIZE);

	for (i = 0; i < ARRAY_SIZE(sizes); i++) {
		__test_sev_dbg(vm, sizes[i]);
		__test_sev_dbg(vm, PAGE_SIZE - sizes[i]);
		__test_sev_dbg(vm, PAGE_SIZE + sizes[i]);
		__test_sev_dbg(vm, BUFFER_SIZE - sizes[i]);
	}

	kvm_vm_free(vm);
}

int main(int argc, char *argv[])
{
	TEST_REQUIRE(kvm_cpu_has(X86_FEATURE_SEV));

	/* Note, KVM doesn't support {de,en}crypt commands for SNP. */
	test_sev_dbg(KVM_X86_SEV_VM, 0);
	test_sev_dbg(KVM_X86_SEV_ES_VM, SEV_POLICY_ES);
	return 0;
}
