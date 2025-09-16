// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kvm.h>
#include <linux/psp-sev.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"
#include "svm_util.h"
#include "kselftest.h"

#define SVM_SEV_FEAT_DEBUG_SWAP 32u

/*
 * Some features may have hidden dependencies, or may only work
 * for certain VM types.  Err on the side of safety and don't
 * expect that all supported features can be passed one by one
 * to KVM_SEV_INIT2.
 *
 * (Well, right now there's only one...)
 */
#define KNOWN_FEATURES SVM_SEV_FEAT_DEBUG_SWAP

int kvm_fd;
u64 supported_vmsa_features;
bool have_sev_es;
bool have_snp;

static int __sev_ioctl(int vm_fd, int cmd_id, void *data)
{
	struct kvm_sev_cmd cmd = {
		.id = cmd_id,
		.data = (uint64_t)data,
		.sev_fd = open_sev_dev_path_or_exit(),
	};
	int ret;

	ret = ioctl(vm_fd, KVM_MEMORY_ENCRYPT_OP, &cmd);
	TEST_ASSERT(ret < 0 || cmd.error == SEV_RET_SUCCESS,
		    "%d failed: fw error: %d\n",
		    cmd_id, cmd.error);

	return ret;
}

static void test_init2(unsigned long vm_type, struct kvm_sev_init *init)
{
	struct kvm_vm *vm;
	int ret;

	vm = vm_create_barebones_type(vm_type);
	ret = __sev_ioctl(vm->fd, KVM_SEV_INIT2, init);
	TEST_ASSERT(ret == 0,
		    "KVM_SEV_INIT2 return code is %d (expected 0), errno: %d",
		    ret, errno);
	kvm_vm_free(vm);
}

static void test_init2_invalid(unsigned long vm_type, struct kvm_sev_init *init, const char *msg)
{
	struct kvm_vm *vm;
	int ret;

	vm = vm_create_barebones_type(vm_type);
	ret = __sev_ioctl(vm->fd, KVM_SEV_INIT2, init);
	TEST_ASSERT(ret == -1 && errno == EINVAL,
		    "KVM_SEV_INIT2 should fail, %s.",
		    msg);
	kvm_vm_free(vm);
}

void test_vm_types(void)
{
	test_init2(KVM_X86_SEV_VM, &(struct kvm_sev_init){});

	/*
	 * TODO: check that unsupported types cannot be created.  Probably
	 * a separate selftest.
	 */
	if (have_sev_es)
		test_init2(KVM_X86_SEV_ES_VM, &(struct kvm_sev_init){});

	if (have_snp)
		test_init2(KVM_X86_SNP_VM, &(struct kvm_sev_init){});

	test_init2_invalid(0, &(struct kvm_sev_init){},
			   "VM type is KVM_X86_DEFAULT_VM");
	if (kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SW_PROTECTED_VM))
		test_init2_invalid(KVM_X86_SW_PROTECTED_VM, &(struct kvm_sev_init){},
				   "VM type is KVM_X86_SW_PROTECTED_VM");
}

void test_flags(uint32_t vm_type)
{
	int i;

	for (i = 0; i < 32; i++)
		test_init2_invalid(vm_type,
			&(struct kvm_sev_init){ .flags = BIT(i) },
			"invalid flag");
}

void test_features(uint32_t vm_type, uint64_t supported_features)
{
	int i;

	for (i = 0; i < 64; i++) {
		if (!(supported_features & BIT_ULL(i)))
			test_init2_invalid(vm_type,
				&(struct kvm_sev_init){ .vmsa_features = BIT_ULL(i) },
				"unknown feature");
		else if (KNOWN_FEATURES & BIT_ULL(i))
			test_init2(vm_type,
				&(struct kvm_sev_init){ .vmsa_features = BIT_ULL(i) });
	}
}

int main(int argc, char *argv[])
{
	int kvm_fd = open_kvm_dev_path_or_exit();
	bool have_sev;

	TEST_REQUIRE(__kvm_has_device_attr(kvm_fd, KVM_X86_GRP_SEV,
					   KVM_X86_SEV_VMSA_FEATURES) == 0);
	kvm_device_attr_get(kvm_fd, KVM_X86_GRP_SEV,
			    KVM_X86_SEV_VMSA_FEATURES,
			    &supported_vmsa_features);

	have_sev = kvm_cpu_has(X86_FEATURE_SEV);
	TEST_ASSERT(have_sev == !!(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SEV_VM)),
		    "sev: KVM_CAP_VM_TYPES (%x) does not match cpuid (checking %x)",
		    kvm_check_cap(KVM_CAP_VM_TYPES), 1 << KVM_X86_SEV_VM);

	TEST_REQUIRE(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SEV_VM));
	have_sev_es = kvm_cpu_has(X86_FEATURE_SEV_ES);

	TEST_ASSERT(have_sev_es == !!(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SEV_ES_VM)),
		    "sev-es: KVM_CAP_VM_TYPES (%x) does not match cpuid (checking %x)",
		    kvm_check_cap(KVM_CAP_VM_TYPES), 1 << KVM_X86_SEV_ES_VM);

	have_snp = kvm_cpu_has(X86_FEATURE_SEV_SNP);
	TEST_ASSERT(have_snp == !!(kvm_check_cap(KVM_CAP_VM_TYPES) & BIT(KVM_X86_SNP_VM)),
		    "sev-snp: KVM_CAP_VM_TYPES (%x) indicates SNP support (bit %d), but CPUID does not",
		    kvm_check_cap(KVM_CAP_VM_TYPES), KVM_X86_SNP_VM);

	test_vm_types();

	test_flags(KVM_X86_SEV_VM);
	if (have_sev_es)
		test_flags(KVM_X86_SEV_ES_VM);
	if (have_snp)
		test_flags(KVM_X86_SNP_VM);

	test_features(KVM_X86_SEV_VM, 0);
	if (have_sev_es)
		test_features(KVM_X86_SEV_ES_VM, supported_vmsa_features);
	if (have_snp)
		test_features(KVM_X86_SNP_VM, supported_vmsa_features);

	return 0;
}
