// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test for s390x KVM_S390_MEM_OP
 *
 * Copyright (C) 2019, Red Hat, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "test_util.h"
#include "kvm_util.h"

enum mop_target {
	LOGICAL,
	SIDA,
	ABSOLUTE,
	INVALID,
};

enum mop_access_mode {
	READ,
	WRITE,
};

struct mop_desc {
	uintptr_t gaddr;
	uintptr_t gaddr_v;
	uint64_t set_flags;
	unsigned int f_check : 1;
	unsigned int f_inject : 1;
	unsigned int f_key : 1;
	unsigned int _gaddr_v : 1;
	unsigned int _set_flags : 1;
	unsigned int _sida_offset : 1;
	unsigned int _ar : 1;
	uint32_t size;
	enum mop_target target;
	enum mop_access_mode mode;
	void *buf;
	uint32_t sida_offset;
	uint8_t ar;
	uint8_t key;
};

static struct kvm_s390_mem_op ksmo_from_desc(struct mop_desc desc)
{
	struct kvm_s390_mem_op ksmo = {
		.gaddr = (uintptr_t)desc.gaddr,
		.size = desc.size,
		.buf = ((uintptr_t)desc.buf),
		.reserved = "ignored_ignored_ignored_ignored"
	};

	switch (desc.target) {
	case LOGICAL:
		if (desc.mode == READ)
			ksmo.op = KVM_S390_MEMOP_LOGICAL_READ;
		if (desc.mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
		break;
	case SIDA:
		if (desc.mode == READ)
			ksmo.op = KVM_S390_MEMOP_SIDA_READ;
		if (desc.mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_SIDA_WRITE;
		break;
	case ABSOLUTE:
		if (desc.mode == READ)
			ksmo.op = KVM_S390_MEMOP_ABSOLUTE_READ;
		if (desc.mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_ABSOLUTE_WRITE;
		break;
	case INVALID:
		ksmo.op = -1;
	}
	if (desc.f_check)
		ksmo.flags |= KVM_S390_MEMOP_F_CHECK_ONLY;
	if (desc.f_inject)
		ksmo.flags |= KVM_S390_MEMOP_F_INJECT_EXCEPTION;
	if (desc._set_flags)
		ksmo.flags = desc.set_flags;
	if (desc.f_key) {
		ksmo.flags |= KVM_S390_MEMOP_F_SKEY_PROTECTION;
		ksmo.key = desc.key;
	}
	if (desc._ar)
		ksmo.ar = desc.ar;
	else
		ksmo.ar = 0;
	if (desc._sida_offset)
		ksmo.sida_offset = desc.sida_offset;

	return ksmo;
}

/* vcpu dummy id signifying that vm instead of vcpu ioctl is to occur */
const uint32_t VM_VCPU_ID = (uint32_t)-1;

struct test_vcpu {
	struct kvm_vm *vm;
	uint32_t id;
};

#define PRINT_MEMOP false
static void print_memop(uint32_t vcpu_id, const struct kvm_s390_mem_op *ksmo)
{
	if (!PRINT_MEMOP)
		return;

	if (vcpu_id == VM_VCPU_ID)
		printf("vm memop(");
	else
		printf("vcpu memop(");
	switch (ksmo->op) {
	case KVM_S390_MEMOP_LOGICAL_READ:
		printf("LOGICAL, READ, ");
		break;
	case KVM_S390_MEMOP_LOGICAL_WRITE:
		printf("LOGICAL, WRITE, ");
		break;
	case KVM_S390_MEMOP_SIDA_READ:
		printf("SIDA, READ, ");
		break;
	case KVM_S390_MEMOP_SIDA_WRITE:
		printf("SIDA, WRITE, ");
		break;
	case KVM_S390_MEMOP_ABSOLUTE_READ:
		printf("ABSOLUTE, READ, ");
		break;
	case KVM_S390_MEMOP_ABSOLUTE_WRITE:
		printf("ABSOLUTE, WRITE, ");
		break;
	}
	printf("gaddr=%llu, size=%u, buf=%llu, ar=%u, key=%u",
	       ksmo->gaddr, ksmo->size, ksmo->buf, ksmo->ar, ksmo->key);
	if (ksmo->flags & KVM_S390_MEMOP_F_CHECK_ONLY)
		printf(", CHECK_ONLY");
	if (ksmo->flags & KVM_S390_MEMOP_F_INJECT_EXCEPTION)
		printf(", INJECT_EXCEPTION");
	if (ksmo->flags & KVM_S390_MEMOP_F_SKEY_PROTECTION)
		printf(", SKEY_PROTECTION");
	puts(")");
}

static void memop_ioctl(struct test_vcpu vcpu, struct kvm_s390_mem_op *ksmo)
{
	if (vcpu.id == VM_VCPU_ID)
		vm_ioctl(vcpu.vm, KVM_S390_MEM_OP, ksmo);
	else
		vcpu_ioctl(vcpu.vm, vcpu.id, KVM_S390_MEM_OP, ksmo);
}

static int err_memop_ioctl(struct test_vcpu vcpu, struct kvm_s390_mem_op *ksmo)
{
	if (vcpu.id == VM_VCPU_ID)
		return _vm_ioctl(vcpu.vm, KVM_S390_MEM_OP, ksmo);
	else
		return _vcpu_ioctl(vcpu.vm, vcpu.id, KVM_S390_MEM_OP, ksmo);
}

#define MEMOP(err, vcpu_p, mop_target_p, access_mode_p, buf_p, size_p, ...)	\
({										\
	struct test_vcpu __vcpu = (vcpu_p);					\
	struct mop_desc __desc = {						\
		.target = (mop_target_p),					\
		.mode = (access_mode_p),					\
		.buf = (buf_p),							\
		.size = (size_p),						\
		__VA_ARGS__							\
	};									\
	struct kvm_s390_mem_op __ksmo;						\
										\
	if (__desc._gaddr_v) {							\
		if (__desc.target == ABSOLUTE)					\
			__desc.gaddr = addr_gva2gpa(__vcpu.vm, __desc.gaddr_v);	\
		else								\
			__desc.gaddr = __desc.gaddr_v;				\
	}									\
	__ksmo = ksmo_from_desc(__desc);					\
	print_memop(__vcpu.id, &__ksmo);					\
	err##memop_ioctl(__vcpu, &__ksmo);					\
})

#define MOP(...) MEMOP(, __VA_ARGS__)
#define ERR_MOP(...) MEMOP(err_, __VA_ARGS__)

#define GADDR(a) .gaddr = ((uintptr_t)a)
#define GADDR_V(v) ._gaddr_v = 1, .gaddr_v = ((uintptr_t)v)
#define CHECK_ONLY .f_check = 1
#define SET_FLAGS(f) ._set_flags = 1, .set_flags = (f)
#define SIDA_OFFSET(o) ._sida_offset = 1, .sida_offset = (o)
#define AR(a) ._ar = 1, .ar = (a)
#define KEY(a) .f_key = 1, .key = (a)

#define VCPU_ID 1

static uint8_t mem1[65536];
static uint8_t mem2[65536];

struct test_default {
	struct kvm_vm *kvm_vm;
	struct test_vcpu vcpu;
	struct kvm_run *run;
	int size;
};

static struct test_default test_default_init(void *guest_code)
{
	struct test_default t;

	t.size = min((size_t)kvm_check_cap(KVM_CAP_S390_MEM_OP), sizeof(mem1));
	t.kvm_vm = vm_create_default(VCPU_ID, 0, guest_code);
	t.vcpu = (struct test_vcpu) { t.kvm_vm, VCPU_ID };
	t.run = vcpu_state(t.kvm_vm, VCPU_ID);
	return t;
}

static void guest_copy(void)
{
	memcpy(&mem2, &mem1, sizeof(mem2));
	GUEST_SYNC(0);
}

static void test_copy(void)
{
	struct test_default t = test_default_init(guest_copy);
	int i;

	for (i = 0; i < sizeof(mem1); i++)
		mem1[i] = i * i + i;

	/* Set the first array */
	MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size,
	    GADDR(addr_gva2gpa(t.kvm_vm, (uintptr_t)mem1)));

	/* Let the guest code copy the first array to the second */
	vcpu_run(t.kvm_vm, VCPU_ID);
	TEST_ASSERT(t.run->exit_reason == KVM_EXIT_S390_SIEIC,
		    "Unexpected exit reason: %u (%s)\n",
		    t.run->exit_reason,
		    exit_reason_str(t.run->exit_reason));

	memset(mem2, 0xaa, sizeof(mem2));

	/* Get the second array */
	MOP(t.vcpu, LOGICAL, READ, mem2, t.size, GADDR_V(mem2));

	TEST_ASSERT(!memcmp(mem1, mem2, t.size),
		    "Memory contents do not match!");

	kvm_vm_free(t.kvm_vm);
}

static void guest_idle(void)
{
	for (;;)
		GUEST_SYNC(0);
}

static void test_errors(void)
{
	struct test_default t = test_default_init(guest_idle);
	int rv;

	/* Bad size: */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, -1, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == E2BIG, "ioctl allows insane sizes");

	/* Zero size: */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, 0, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && (errno == EINVAL || errno == ENOMEM),
		    "ioctl allows 0 as size");

	/* Bad flags: */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR_V(mem1), SET_FLAGS(-1));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows all flags");

	/* Bad operation: */
	rv = ERR_MOP(t.vcpu, INVALID, WRITE, mem1, t.size, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");

	/* Bad guest address: */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR((void *)~0xfffUL), CHECK_ONLY);
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory access");

	/* Bad host address: */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, 0, t.size, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == EFAULT,
		    "ioctl does not report bad host memory address");

	/* Bad access register: */
	t.run->psw_mask &= ~(3UL << (63 - 17));
	t.run->psw_mask |= 1UL << (63 - 17);  /* Enable AR mode */
	vcpu_run(t.kvm_vm, VCPU_ID);              /* To sync new state to SIE block */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR_V(mem1), AR(17));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows ARs > 15");
	t.run->psw_mask &= ~(3UL << (63 - 17));   /* Disable AR mode */
	vcpu_run(t.kvm_vm, VCPU_ID);                  /* Run to sync new state */

	/* Check that the SIDA calls are rejected for non-protected guests */
	rv = ERR_MOP(t.vcpu, SIDA, READ, mem1, 8, GADDR(0), SIDA_OFFSET(0x1c0));
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_READ in non-protected mode");
	rv = ERR_MOP(t.vcpu, SIDA, WRITE, mem1, 8, GADDR(0), SIDA_OFFSET(0x1c0));
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_WRITE in non-protected mode");

	kvm_vm_free(t.kvm_vm);
}

int main(int argc, char *argv[])
{
	int memop_cap;

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	memop_cap = kvm_check_cap(KVM_CAP_S390_MEM_OP);
	if (!memop_cap) {
		print_skip("CAP_S390_MEM_OP not supported");
		exit(KSFT_SKIP);
	}

	test_copy();
	test_errors();

	return 0;
}
