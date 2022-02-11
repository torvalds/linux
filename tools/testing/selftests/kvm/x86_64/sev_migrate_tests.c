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
#include "../lib/kvm_util_internal.h"

#define SEV_POLICY_ES 0b100

#define NR_MIGRATE_TEST_VCPUS 4
#define NR_MIGRATE_TEST_VMS 3
#define NR_LOCK_TESTING_THREADS 3
#define NR_LOCK_TESTING_ITERATIONS 10000

static int __sev_ioctl(int vm_fd, int cmd_id, void *data, __u32 *fw_error)
{
	struct kvm_sev_cmd cmd = {
		.id = cmd_id,
		.data = (uint64_t)data,
		.sev_fd = open_sev_dev_path_or_exit(),
	};
	int ret;

	ret = ioctl(vm_fd, KVM_MEMORY_ENCRYPT_OP, &cmd);
	*fw_error = cmd.error;
	return ret;
}

static void sev_ioctl(int vm_fd, int cmd_id, void *data)
{
	int ret;
	__u32 fw_error;

	ret = __sev_ioctl(vm_fd, cmd_id, data, &fw_error);
	TEST_ASSERT(ret == 0 && fw_error == SEV_RET_SUCCESS,
		    "%d failed: return code: %d, errno: %d, fw error: %d",
		    cmd_id, ret, errno, fw_error);
}

static struct kvm_vm *sev_vm_create(bool es)
{
	struct kvm_vm *vm;
	struct kvm_sev_launch_start start = { 0 };
	int i;

	vm = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);
	sev_ioctl(vm->fd, es ? KVM_SEV_ES_INIT : KVM_SEV_INIT, NULL);
	for (i = 0; i < NR_MIGRATE_TEST_VCPUS; ++i)
		vm_vcpu_add(vm, i);
	if (es)
		start.policy |= SEV_POLICY_ES;
	sev_ioctl(vm->fd, KVM_SEV_LAUNCH_START, &start);
	if (es)
		sev_ioctl(vm->fd, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL);
	return vm;
}

static struct kvm_vm *aux_vm_create(bool with_vcpus)
{
	struct kvm_vm *vm;
	int i;

	vm = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);
	if (!with_vcpus)
		return vm;

	for (i = 0; i < NR_MIGRATE_TEST_VCPUS; ++i)
		vm_vcpu_add(vm, i);

	return vm;
}

static int __sev_migrate_from(int dst_fd, int src_fd)
{
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_VM_MOVE_ENC_CONTEXT_FROM,
		.args = { src_fd }
	};

	return ioctl(dst_fd, KVM_ENABLE_CAP, &cap);
}


static void sev_migrate_from(int dst_fd, int src_fd)
{
	int ret;

	ret = __sev_migrate_from(dst_fd, src_fd);
	TEST_ASSERT(!ret, "Migration failed, ret: %d, errno: %d\n", ret, errno);
}

static void test_sev_migrate_from(bool es)
{
	struct kvm_vm *src_vm;
	struct kvm_vm *dst_vms[NR_MIGRATE_TEST_VMS];
	int i, ret;

	src_vm = sev_vm_create(es);
	for (i = 0; i < NR_MIGRATE_TEST_VMS; ++i)
		dst_vms[i] = aux_vm_create(true);

	/* Initial migration from the src to the first dst. */
	sev_migrate_from(dst_vms[0]->fd, src_vm->fd);

	for (i = 1; i < NR_MIGRATE_TEST_VMS; i++)
		sev_migrate_from(dst_vms[i]->fd, dst_vms[i - 1]->fd);

	/* Migrate the guest back to the original VM. */
	ret = __sev_migrate_from(src_vm->fd, dst_vms[NR_MIGRATE_TEST_VMS - 1]->fd);
	TEST_ASSERT(ret == -1 && errno == EIO,
		    "VM that was migrated from should be dead. ret %d, errno: %d\n", ret,
		    errno);

	kvm_vm_free(src_vm);
	for (i = 0; i < NR_MIGRATE_TEST_VMS; ++i)
		kvm_vm_free(dst_vms[i]);
}

struct locking_thread_input {
	struct kvm_vm *vm;
	int source_fds[NR_LOCK_TESTING_THREADS];
};

static void *locking_test_thread(void *arg)
{
	int i, j;
	struct locking_thread_input *input = (struct locking_thread_input *)arg;

	for (i = 0; i < NR_LOCK_TESTING_ITERATIONS; ++i) {
		j = i % NR_LOCK_TESTING_THREADS;
		__sev_migrate_from(input->vm->fd, input->source_fds[j]);
	}

	return NULL;
}

static void test_sev_migrate_locking(void)
{
	struct locking_thread_input input[NR_LOCK_TESTING_THREADS];
	pthread_t pt[NR_LOCK_TESTING_THREADS];
	int i;

	for (i = 0; i < NR_LOCK_TESTING_THREADS; ++i) {
		input[i].vm = sev_vm_create(/* es= */ false);
		input[0].source_fds[i] = input[i].vm->fd;
	}
	for (i = 1; i < NR_LOCK_TESTING_THREADS; ++i)
		memcpy(input[i].source_fds, input[0].source_fds,
		       sizeof(input[i].source_fds));

	for (i = 0; i < NR_LOCK_TESTING_THREADS; ++i)
		pthread_create(&pt[i], NULL, locking_test_thread, &input[i]);

	for (i = 0; i < NR_LOCK_TESTING_THREADS; ++i)
		pthread_join(pt[i], NULL);
	for (i = 0; i < NR_LOCK_TESTING_THREADS; ++i)
		kvm_vm_free(input[i].vm);
}

static void test_sev_migrate_parameters(void)
{
	struct kvm_vm *sev_vm, *sev_es_vm, *vm_no_vcpu, *vm_no_sev,
		*sev_es_vm_no_vmsa;
	int ret;

	sev_vm = sev_vm_create(/* es= */ false);
	sev_es_vm = sev_vm_create(/* es= */ true);
	vm_no_vcpu = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);
	vm_no_sev = aux_vm_create(true);
	sev_es_vm_no_vmsa = vm_create(VM_MODE_DEFAULT, 0, O_RDWR);
	sev_ioctl(sev_es_vm_no_vmsa->fd, KVM_SEV_ES_INIT, NULL);
	vm_vcpu_add(sev_es_vm_no_vmsa, 1);

	ret = __sev_migrate_from(sev_vm->fd, sev_es_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"Should not be able migrate to SEV enabled VM. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_migrate_from(sev_es_vm->fd, sev_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"Should not be able migrate to SEV-ES enabled VM. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_migrate_from(vm_no_vcpu->fd, sev_es_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"SEV-ES migrations require same number of vCPUS. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_migrate_from(vm_no_vcpu->fd, sev_es_vm_no_vmsa->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"SEV-ES migrations require UPDATE_VMSA. ret %d, errno: %d\n",
		ret, errno);

	ret = __sev_migrate_from(vm_no_vcpu->fd, vm_no_sev->fd);
	TEST_ASSERT(ret == -1 && errno == EINVAL,
		    "Migrations require SEV enabled. ret %d, errno: %d\n", ret,
		    errno);

	kvm_vm_free(sev_vm);
	kvm_vm_free(sev_es_vm);
	kvm_vm_free(sev_es_vm_no_vmsa);
	kvm_vm_free(vm_no_vcpu);
	kvm_vm_free(vm_no_sev);
}

static int __sev_mirror_create(int dst_fd, int src_fd)
{
	struct kvm_enable_cap cap = {
		.cap = KVM_CAP_VM_COPY_ENC_CONTEXT_FROM,
		.args = { src_fd }
	};

	return ioctl(dst_fd, KVM_ENABLE_CAP, &cap);
}


static void sev_mirror_create(int dst_fd, int src_fd)
{
	int ret;

	ret = __sev_mirror_create(dst_fd, src_fd);
	TEST_ASSERT(!ret, "Copying context failed, ret: %d, errno: %d\n", ret, errno);
}

static void verify_mirror_allowed_cmds(int vm_fd)
{
	struct kvm_sev_guest_status status;

	for (int cmd_id = KVM_SEV_INIT; cmd_id < KVM_SEV_NR_MAX; ++cmd_id) {
		int ret;
		__u32 fw_error;

		/*
		 * These commands are allowed for mirror VMs, all others are
		 * not.
		 */
		switch (cmd_id) {
		case KVM_SEV_LAUNCH_UPDATE_VMSA:
		case KVM_SEV_GUEST_STATUS:
		case KVM_SEV_DBG_DECRYPT:
		case KVM_SEV_DBG_ENCRYPT:
			continue;
		default:
			break;
		}

		/*
		 * These commands should be disallowed before the data
		 * parameter is examined so NULL is OK here.
		 */
		ret = __sev_ioctl(vm_fd, cmd_id, NULL, &fw_error);
		TEST_ASSERT(
			ret == -1 && errno == EINVAL,
			"Should not be able call command: %d. ret: %d, errno: %d\n",
			cmd_id, ret, errno);
	}

	sev_ioctl(vm_fd, KVM_SEV_GUEST_STATUS, &status);
}

static void test_sev_mirror(bool es)
{
	struct kvm_vm *src_vm, *dst_vm;
	int i;

	src_vm = sev_vm_create(es);
	dst_vm = aux_vm_create(false);

	sev_mirror_create(dst_vm->fd, src_vm->fd);

	/* Check that we can complete creation of the mirror VM.  */
	for (i = 0; i < NR_MIGRATE_TEST_VCPUS; ++i)
		vm_vcpu_add(dst_vm, i);

	if (es)
		sev_ioctl(dst_vm->fd, KVM_SEV_LAUNCH_UPDATE_VMSA, NULL);

	verify_mirror_allowed_cmds(dst_vm->fd);

	kvm_vm_free(src_vm);
	kvm_vm_free(dst_vm);
}

static void test_sev_mirror_parameters(void)
{
	struct kvm_vm *sev_vm, *sev_es_vm, *vm_no_vcpu, *vm_with_vcpu;
	int ret;

	sev_vm = sev_vm_create(/* es= */ false);
	sev_es_vm = sev_vm_create(/* es= */ true);
	vm_with_vcpu = aux_vm_create(true);
	vm_no_vcpu = aux_vm_create(false);

	ret = __sev_mirror_create(sev_vm->fd, sev_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"Should not be able copy context to self. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_mirror_create(sev_vm->fd, sev_es_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"Should not be able copy context to SEV enabled VM. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_mirror_create(sev_es_vm->fd, sev_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"Should not be able copy context to SEV-ES enabled VM. ret: %d, errno: %d\n",
		ret, errno);

	ret = __sev_mirror_create(vm_no_vcpu->fd, vm_with_vcpu->fd);
	TEST_ASSERT(ret == -1 && errno == EINVAL,
		    "Copy context requires SEV enabled. ret %d, errno: %d\n", ret,
		    errno);

	ret = __sev_mirror_create(vm_with_vcpu->fd, sev_vm->fd);
	TEST_ASSERT(
		ret == -1 && errno == EINVAL,
		"SEV copy context requires no vCPUS on the destination. ret: %d, errno: %d\n",
		ret, errno);

	kvm_vm_free(sev_vm);
	kvm_vm_free(sev_es_vm);
	kvm_vm_free(vm_with_vcpu);
	kvm_vm_free(vm_no_vcpu);
}

static void test_sev_move_copy(void)
{
	struct kvm_vm *dst_vm, *dst2_vm, *dst3_vm, *sev_vm, *mirror_vm,
		      *dst_mirror_vm, *dst2_mirror_vm, *dst3_mirror_vm;

	sev_vm = sev_vm_create(/* es= */ false);
	dst_vm = aux_vm_create(true);
	dst2_vm = aux_vm_create(true);
	dst3_vm = aux_vm_create(true);
	mirror_vm = aux_vm_create(false);
	dst_mirror_vm = aux_vm_create(false);
	dst2_mirror_vm = aux_vm_create(false);
	dst3_mirror_vm = aux_vm_create(false);

	sev_mirror_create(mirror_vm->fd, sev_vm->fd);

	sev_migrate_from(dst_mirror_vm->fd, mirror_vm->fd);
	sev_migrate_from(dst_vm->fd, sev_vm->fd);

	sev_migrate_from(dst2_vm->fd, dst_vm->fd);
	sev_migrate_from(dst2_mirror_vm->fd, dst_mirror_vm->fd);

	sev_migrate_from(dst3_mirror_vm->fd, dst2_mirror_vm->fd);
	sev_migrate_from(dst3_vm->fd, dst2_vm->fd);

	kvm_vm_free(dst_vm);
	kvm_vm_free(sev_vm);
	kvm_vm_free(dst2_vm);
	kvm_vm_free(dst3_vm);
	kvm_vm_free(mirror_vm);
	kvm_vm_free(dst_mirror_vm);
	kvm_vm_free(dst2_mirror_vm);
	kvm_vm_free(dst3_mirror_vm);

	/*
	 * Run similar test be destroy mirrors before mirrored VMs to ensure
	 * destruction is done safely.
	 */
	sev_vm = sev_vm_create(/* es= */ false);
	dst_vm = aux_vm_create(true);
	mirror_vm = aux_vm_create(false);
	dst_mirror_vm = aux_vm_create(false);

	sev_mirror_create(mirror_vm->fd, sev_vm->fd);

	sev_migrate_from(dst_mirror_vm->fd, mirror_vm->fd);
	sev_migrate_from(dst_vm->fd, sev_vm->fd);

	kvm_vm_free(mirror_vm);
	kvm_vm_free(dst_mirror_vm);
	kvm_vm_free(dst_vm);
	kvm_vm_free(sev_vm);
}

int main(int argc, char *argv[])
{
	if (kvm_check_cap(KVM_CAP_VM_MOVE_ENC_CONTEXT_FROM)) {
		test_sev_migrate_from(/* es= */ false);
		test_sev_migrate_from(/* es= */ true);
		test_sev_migrate_locking();
		test_sev_migrate_parameters();
		if (kvm_check_cap(KVM_CAP_VM_COPY_ENC_CONTEXT_FROM))
			test_sev_move_copy();
	}
	if (kvm_check_cap(KVM_CAP_VM_COPY_ENC_CONTEXT_FROM)) {
		test_sev_mirror(/* es= */ false);
		test_sev_mirror(/* es= */ true);
		test_sev_mirror_parameters();
	}
	return 0;
}
