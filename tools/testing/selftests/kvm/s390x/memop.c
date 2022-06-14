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

#include <linux/bits.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"

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

struct test_info {
	struct kvm_vm *vm;
	struct kvm_vcpu *vcpu;
};

#define PRINT_MEMOP false
static void print_memop(struct kvm_vcpu *vcpu, const struct kvm_s390_mem_op *ksmo)
{
	if (!PRINT_MEMOP)
		return;

	if (!vcpu)
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

static void memop_ioctl(struct test_info info, struct kvm_s390_mem_op *ksmo)
{
	struct kvm_vcpu *vcpu = info.vcpu;

	if (!vcpu)
		vm_ioctl(info.vm, KVM_S390_MEM_OP, ksmo);
	else
		vcpu_ioctl(vcpu, KVM_S390_MEM_OP, ksmo);
}

static int err_memop_ioctl(struct test_info info, struct kvm_s390_mem_op *ksmo)
{
	struct kvm_vcpu *vcpu = info.vcpu;

	if (!vcpu)
		return __vm_ioctl(info.vm, KVM_S390_MEM_OP, ksmo);
	else
		return __vcpu_ioctl(vcpu, KVM_S390_MEM_OP, ksmo);
}

#define MEMOP(err, info_p, mop_target_p, access_mode_p, buf_p, size_p, ...)	\
({										\
	struct test_info __info = (info_p);					\
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
			__desc.gaddr = addr_gva2gpa(__info.vm, __desc.gaddr_v);	\
		else								\
			__desc.gaddr = __desc.gaddr_v;				\
	}									\
	__ksmo = ksmo_from_desc(__desc);					\
	print_memop(__info.vcpu, &__ksmo);					\
	err##memop_ioctl(__info, &__ksmo);					\
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
#define INJECT .f_inject = 1

#define CHECK_N_DO(f, ...) ({ f(__VA_ARGS__, CHECK_ONLY); f(__VA_ARGS__); })

#define PAGE_SHIFT 12
#define PAGE_SIZE (1ULL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))
#define CR0_FETCH_PROTECTION_OVERRIDE	(1UL << (63 - 38))
#define CR0_STORAGE_PROTECTION_OVERRIDE	(1UL << (63 - 39))

static uint8_t mem1[65536];
static uint8_t mem2[65536];

struct test_default {
	struct kvm_vm *kvm_vm;
	struct test_info vm;
	struct test_info vcpu;
	struct kvm_run *run;
	int size;
};

static struct test_default test_default_init(void *guest_code)
{
	struct kvm_vcpu *vcpu;
	struct test_default t;

	t.size = min((size_t)kvm_check_cap(KVM_CAP_S390_MEM_OP), sizeof(mem1));
	t.kvm_vm = vm_create_with_one_vcpu(&vcpu, guest_code);
	t.vm = (struct test_info) { t.kvm_vm, NULL };
	t.vcpu = (struct test_info) { t.kvm_vm, vcpu };
	t.run = vcpu->run;
	return t;
}

enum stage {
	/* Synced state set by host, e.g. DAT */
	STAGE_INITED,
	/* Guest did nothing */
	STAGE_IDLED,
	/* Guest set storage keys (specifics up to test case) */
	STAGE_SKEYS_SET,
	/* Guest copied memory (locations up to test case) */
	STAGE_COPIED,
};

#define HOST_SYNC(info_p, stage)					\
({									\
	struct test_info __info = (info_p);				\
	struct kvm_vcpu *__vcpu = __info.vcpu;				\
	struct ucall uc;						\
	int __stage = (stage);						\
									\
	vcpu_run(__vcpu);						\
	get_ucall(__vcpu, &uc);						\
	ASSERT_EQ(uc.cmd, UCALL_SYNC);					\
	ASSERT_EQ(uc.args[1], __stage);					\
})									\

static void prepare_mem12(void)
{
	int i;

	for (i = 0; i < sizeof(mem1); i++)
		mem1[i] = rand();
	memset(mem2, 0xaa, sizeof(mem2));
}

#define ASSERT_MEM_EQ(p1, p2, size) \
	TEST_ASSERT(!memcmp(p1, p2, size), "Memory contents do not match!")

#define DEFAULT_WRITE_READ(copy_cpu, mop_cpu, mop_target_p, size, ...)		\
({										\
	struct test_info __copy_cpu = (copy_cpu), __mop_cpu = (mop_cpu);	\
	enum mop_target __target = (mop_target_p);				\
	uint32_t __size = (size);						\
										\
	prepare_mem12();							\
	CHECK_N_DO(MOP, __mop_cpu, __target, WRITE, mem1, __size,		\
			GADDR_V(mem1), ##__VA_ARGS__);				\
	HOST_SYNC(__copy_cpu, STAGE_COPIED);					\
	CHECK_N_DO(MOP, __mop_cpu, __target, READ, mem2, __size,		\
			GADDR_V(mem2), ##__VA_ARGS__);				\
	ASSERT_MEM_EQ(mem1, mem2, __size);					\
})

#define DEFAULT_READ(copy_cpu, mop_cpu, mop_target_p, size, ...)		\
({										\
	struct test_info __copy_cpu = (copy_cpu), __mop_cpu = (mop_cpu);	\
	enum mop_target __target = (mop_target_p);				\
	uint32_t __size = (size);						\
										\
	prepare_mem12();							\
	CHECK_N_DO(MOP, __mop_cpu, __target, WRITE, mem1, __size,		\
			GADDR_V(mem1));						\
	HOST_SYNC(__copy_cpu, STAGE_COPIED);					\
	CHECK_N_DO(MOP, __mop_cpu, __target, READ, mem2, __size, ##__VA_ARGS__);\
	ASSERT_MEM_EQ(mem1, mem2, __size);					\
})

static void guest_copy(void)
{
	GUEST_SYNC(STAGE_INITED);
	memcpy(&mem2, &mem1, sizeof(mem2));
	GUEST_SYNC(STAGE_COPIED);
}

static void test_copy(void)
{
	struct test_default t = test_default_init(guest_copy);

	HOST_SYNC(t.vcpu, STAGE_INITED);

	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, t.size);

	kvm_vm_free(t.kvm_vm);
}

static void set_storage_key_range(void *addr, size_t len, uint8_t key)
{
	uintptr_t _addr, abs, i;
	int not_mapped = 0;

	_addr = (uintptr_t)addr;
	for (i = _addr & PAGE_MASK; i < _addr + len; i += PAGE_SIZE) {
		abs = i;
		asm volatile (
			       "lra	%[abs], 0(0,%[abs])\n"
			"	jz	0f\n"
			"	llill	%[not_mapped],1\n"
			"	j	1f\n"
			"0:	sske	%[key], %[abs]\n"
			"1:"
			: [abs] "+&a" (abs), [not_mapped] "+r" (not_mapped)
			: [key] "r" (key)
			: "cc"
		);
		GUEST_ASSERT_EQ(not_mapped, 0);
	}
}

static void guest_copy_key(void)
{
	set_storage_key_range(mem1, sizeof(mem1), 0x90);
	set_storage_key_range(mem2, sizeof(mem2), 0x90);
	GUEST_SYNC(STAGE_SKEYS_SET);

	for (;;) {
		memcpy(&mem2, &mem1, sizeof(mem2));
		GUEST_SYNC(STAGE_COPIED);
	}
}

static void test_copy_key(void)
{
	struct test_default t = test_default_init(guest_copy_key);

	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vm, no key */
	DEFAULT_WRITE_READ(t.vcpu, t.vm, ABSOLUTE, t.size);

	/* vm/vcpu, machting key or key 0 */
	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, t.size, KEY(0));
	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, t.size, KEY(9));
	DEFAULT_WRITE_READ(t.vcpu, t.vm, ABSOLUTE, t.size, KEY(0));
	DEFAULT_WRITE_READ(t.vcpu, t.vm, ABSOLUTE, t.size, KEY(9));
	/*
	 * There used to be different code paths for key handling depending on
	 * if the region crossed a page boundary.
	 * There currently are not, but the more tests the merrier.
	 */
	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, 1, KEY(0));
	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, 1, KEY(9));
	DEFAULT_WRITE_READ(t.vcpu, t.vm, ABSOLUTE, 1, KEY(0));
	DEFAULT_WRITE_READ(t.vcpu, t.vm, ABSOLUTE, 1, KEY(9));

	/* vm/vcpu, mismatching keys on read, but no fetch protection */
	DEFAULT_READ(t.vcpu, t.vcpu, LOGICAL, t.size, GADDR_V(mem2), KEY(2));
	DEFAULT_READ(t.vcpu, t.vm, ABSOLUTE, t.size, GADDR_V(mem1), KEY(2));

	kvm_vm_free(t.kvm_vm);
}

static void guest_copy_key_fetch_prot(void)
{
	/*
	 * For some reason combining the first sync with override enablement
	 * results in an exception when calling HOST_SYNC.
	 */
	GUEST_SYNC(STAGE_INITED);
	/* Storage protection override applies to both store and fetch. */
	set_storage_key_range(mem1, sizeof(mem1), 0x98);
	set_storage_key_range(mem2, sizeof(mem2), 0x98);
	GUEST_SYNC(STAGE_SKEYS_SET);

	for (;;) {
		memcpy(&mem2, &mem1, sizeof(mem2));
		GUEST_SYNC(STAGE_COPIED);
	}
}

static void test_copy_key_storage_prot_override(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot);

	HOST_SYNC(t.vcpu, STAGE_INITED);
	t.run->s.regs.crs[0] |= CR0_STORAGE_PROTECTION_OVERRIDE;
	t.run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vcpu, mismatching keys, storage protection override in effect */
	DEFAULT_WRITE_READ(t.vcpu, t.vcpu, LOGICAL, t.size, KEY(2));

	kvm_vm_free(t.kvm_vm);
}

static void test_copy_key_fetch_prot(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot);

	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vm/vcpu, matching key, fetch protection in effect */
	DEFAULT_READ(t.vcpu, t.vcpu, LOGICAL, t.size, GADDR_V(mem2), KEY(9));
	DEFAULT_READ(t.vcpu, t.vm, ABSOLUTE, t.size, GADDR_V(mem2), KEY(9));

	kvm_vm_free(t.kvm_vm);
}

#define ERR_PROT_MOP(...)							\
({										\
	int rv;									\
										\
	rv = ERR_MOP(__VA_ARGS__);						\
	TEST_ASSERT(rv == 4, "Should result in protection exception");		\
})

static void guest_error_key(void)
{
	GUEST_SYNC(STAGE_INITED);
	set_storage_key_range(mem1, PAGE_SIZE, 0x18);
	set_storage_key_range(mem1 + PAGE_SIZE, sizeof(mem1) - PAGE_SIZE, 0x98);
	GUEST_SYNC(STAGE_SKEYS_SET);
	GUEST_SYNC(STAGE_IDLED);
}

static void test_errors_key(void)
{
	struct test_default t = test_default_init(guest_error_key);

	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vm/vcpu, mismatching keys, fetch protection in effect */
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR_V(mem1), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, READ, mem2, t.size, GADDR_V(mem2), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, WRITE, mem1, t.size, GADDR_V(mem1), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, READ, mem2, t.size, GADDR_V(mem2), KEY(2));

	kvm_vm_free(t.kvm_vm);
}

static void test_termination(void)
{
	struct test_default t = test_default_init(guest_error_key);
	uint64_t prefix;
	uint64_t teid;
	uint64_t teid_mask = BIT(63 - 56) | BIT(63 - 60) | BIT(63 - 61);
	uint64_t psw[2];

	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vcpu, mismatching keys after first page */
	ERR_PROT_MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR_V(mem1), KEY(1), INJECT);
	/*
	 * The memop injected a program exception and the test needs to check the
	 * Translation-Exception Identification (TEID). It is necessary to run
	 * the guest in order to be able to read the TEID from guest memory.
	 * Set the guest program new PSW, so the guest state is not clobbered.
	 */
	prefix = t.run->s.regs.prefix;
	psw[0] = t.run->psw_mask;
	psw[1] = t.run->psw_addr;
	MOP(t.vm, ABSOLUTE, WRITE, psw, sizeof(psw), GADDR(prefix + 464));
	HOST_SYNC(t.vcpu, STAGE_IDLED);
	MOP(t.vm, ABSOLUTE, READ, &teid, sizeof(teid), GADDR(prefix + 168));
	/* Bits 56, 60, 61 form a code, 0 being the only one allowing for termination */
	ASSERT_EQ(teid & teid_mask, 0);

	kvm_vm_free(t.kvm_vm);
}

static void test_errors_key_storage_prot_override(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot);

	HOST_SYNC(t.vcpu, STAGE_INITED);
	t.run->s.regs.crs[0] |= CR0_STORAGE_PROTECTION_OVERRIDE;
	t.run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vm, mismatching keys, storage protection override not applicable to vm */
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, WRITE, mem1, t.size, GADDR_V(mem1), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, READ, mem2, t.size, GADDR_V(mem2), KEY(2));

	kvm_vm_free(t.kvm_vm);
}

const uint64_t last_page_addr = -PAGE_SIZE;

static void guest_copy_key_fetch_prot_override(void)
{
	int i;
	char *page_0 = 0;

	GUEST_SYNC(STAGE_INITED);
	set_storage_key_range(0, PAGE_SIZE, 0x18);
	set_storage_key_range((void *)last_page_addr, PAGE_SIZE, 0x0);
	asm volatile ("sske %[key],%[addr]\n" :: [addr] "r"(0), [key] "r"(0x18) : "cc");
	GUEST_SYNC(STAGE_SKEYS_SET);

	for (;;) {
		for (i = 0; i < PAGE_SIZE; i++)
			page_0[i] = mem1[i];
		GUEST_SYNC(STAGE_COPIED);
	}
}

static void test_copy_key_fetch_prot_override(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot_override);
	vm_vaddr_t guest_0_page, guest_last_page;

	guest_0_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, 0);
	guest_last_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, last_page_addr);
	if (guest_0_page != 0 || guest_last_page != last_page_addr) {
		print_skip("did not allocate guest pages at required positions");
		goto out;
	}

	HOST_SYNC(t.vcpu, STAGE_INITED);
	t.run->s.regs.crs[0] |= CR0_FETCH_PROTECTION_OVERRIDE;
	t.run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vcpu, mismatching keys on fetch, fetch protection override applies */
	prepare_mem12();
	MOP(t.vcpu, LOGICAL, WRITE, mem1, PAGE_SIZE, GADDR_V(mem1));
	HOST_SYNC(t.vcpu, STAGE_COPIED);
	CHECK_N_DO(MOP, t.vcpu, LOGICAL, READ, mem2, 2048, GADDR_V(guest_0_page), KEY(2));
	ASSERT_MEM_EQ(mem1, mem2, 2048);

	/*
	 * vcpu, mismatching keys on fetch, fetch protection override applies,
	 * wraparound
	 */
	prepare_mem12();
	MOP(t.vcpu, LOGICAL, WRITE, mem1, 2 * PAGE_SIZE, GADDR_V(guest_last_page));
	HOST_SYNC(t.vcpu, STAGE_COPIED);
	CHECK_N_DO(MOP, t.vcpu, LOGICAL, READ, mem2, PAGE_SIZE + 2048,
		   GADDR_V(guest_last_page), KEY(2));
	ASSERT_MEM_EQ(mem1, mem2, 2048);

out:
	kvm_vm_free(t.kvm_vm);
}

static void test_errors_key_fetch_prot_override_not_enabled(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot_override);
	vm_vaddr_t guest_0_page, guest_last_page;

	guest_0_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, 0);
	guest_last_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, last_page_addr);
	if (guest_0_page != 0 || guest_last_page != last_page_addr) {
		print_skip("did not allocate guest pages at required positions");
		goto out;
	}
	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vcpu, mismatching keys on fetch, fetch protection override not enabled */
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, READ, mem2, 2048, GADDR_V(0), KEY(2));

out:
	kvm_vm_free(t.kvm_vm);
}

static void test_errors_key_fetch_prot_override_enabled(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot_override);
	vm_vaddr_t guest_0_page, guest_last_page;

	guest_0_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, 0);
	guest_last_page = vm_vaddr_alloc(t.kvm_vm, PAGE_SIZE, last_page_addr);
	if (guest_0_page != 0 || guest_last_page != last_page_addr) {
		print_skip("did not allocate guest pages at required positions");
		goto out;
	}
	HOST_SYNC(t.vcpu, STAGE_INITED);
	t.run->s.regs.crs[0] |= CR0_FETCH_PROTECTION_OVERRIDE;
	t.run->kvm_dirty_regs = KVM_SYNC_CRS;
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/*
	 * vcpu, mismatching keys on fetch,
	 * fetch protection override does not apply because memory range acceeded
	 */
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, READ, mem2, 2048 + 1, GADDR_V(0), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, READ, mem2, PAGE_SIZE + 2048 + 1,
		   GADDR_V(guest_last_page), KEY(2));
	/* vm, fetch protected override does not apply */
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, READ, mem2, 2048, GADDR(0), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, READ, mem2, 2048, GADDR_V(guest_0_page), KEY(2));

out:
	kvm_vm_free(t.kvm_vm);
}

static void guest_idle(void)
{
	GUEST_SYNC(STAGE_INITED); /* for consistency's sake */
	for (;;)
		GUEST_SYNC(STAGE_IDLED);
}

static void _test_errors_common(struct test_info info, enum mop_target target, int size)
{
	int rv;

	/* Bad size: */
	rv = ERR_MOP(info, target, WRITE, mem1, -1, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == E2BIG, "ioctl allows insane sizes");

	/* Zero size: */
	rv = ERR_MOP(info, target, WRITE, mem1, 0, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && (errno == EINVAL || errno == ENOMEM),
		    "ioctl allows 0 as size");

	/* Bad flags: */
	rv = ERR_MOP(info, target, WRITE, mem1, size, GADDR_V(mem1), SET_FLAGS(-1));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows all flags");

	/* Bad guest address: */
	rv = ERR_MOP(info, target, WRITE, mem1, size, GADDR((void *)~0xfffUL), CHECK_ONLY);
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory access");

	/* Bad host address: */
	rv = ERR_MOP(info, target, WRITE, 0, size, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == EFAULT,
		    "ioctl does not report bad host memory address");

	/* Bad key: */
	rv = ERR_MOP(info, target, WRITE, mem1, size, GADDR_V(mem1), KEY(17));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows invalid key");
}

static void test_errors(void)
{
	struct test_default t = test_default_init(guest_idle);
	int rv;

	HOST_SYNC(t.vcpu, STAGE_INITED);

	_test_errors_common(t.vcpu, LOGICAL, t.size);
	_test_errors_common(t.vm, ABSOLUTE, t.size);

	/* Bad operation: */
	rv = ERR_MOP(t.vcpu, INVALID, WRITE, mem1, t.size, GADDR_V(mem1));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");
	/* virtual addresses are not translated when passing INVALID */
	rv = ERR_MOP(t.vm, INVALID, WRITE, mem1, PAGE_SIZE, GADDR(0));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows bad operations");

	/* Bad access register: */
	t.run->psw_mask &= ~(3UL << (63 - 17));
	t.run->psw_mask |= 1UL << (63 - 17);  /* Enable AR mode */
	HOST_SYNC(t.vcpu, STAGE_IDLED); /* To sync new state to SIE block */
	rv = ERR_MOP(t.vcpu, LOGICAL, WRITE, mem1, t.size, GADDR_V(mem1), AR(17));
	TEST_ASSERT(rv == -1 && errno == EINVAL, "ioctl allows ARs > 15");
	t.run->psw_mask &= ~(3UL << (63 - 17));   /* Disable AR mode */
	HOST_SYNC(t.vcpu, STAGE_IDLED); /* Run to sync new state */

	/* Check that the SIDA calls are rejected for non-protected guests */
	rv = ERR_MOP(t.vcpu, SIDA, READ, mem1, 8, GADDR(0), SIDA_OFFSET(0x1c0));
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_READ in non-protected mode");
	rv = ERR_MOP(t.vcpu, SIDA, WRITE, mem1, 8, GADDR(0), SIDA_OFFSET(0x1c0));
	TEST_ASSERT(rv == -1 && errno == EINVAL,
		    "ioctl does not reject SIDA_WRITE in non-protected mode");

	kvm_vm_free(t.kvm_vm);
}

struct testdef {
	const char *name;
	void (*test)(void);
	int extension;
} testlist[] = {
	{
		.name = "simple copy",
		.test = test_copy,
	},
	{
		.name = "generic error checks",
		.test = test_errors,
	},
	{
		.name = "copy with storage keys",
		.test = test_copy_key,
		.extension = 1,
	},
	{
		.name = "copy with key storage protection override",
		.test = test_copy_key_storage_prot_override,
		.extension = 1,
	},
	{
		.name = "copy with key fetch protection",
		.test = test_copy_key_fetch_prot,
		.extension = 1,
	},
	{
		.name = "copy with key fetch protection override",
		.test = test_copy_key_fetch_prot_override,
		.extension = 1,
	},
	{
		.name = "error checks with key",
		.test = test_errors_key,
		.extension = 1,
	},
	{
		.name = "termination",
		.test = test_termination,
		.extension = 1,
	},
	{
		.name = "error checks with key storage protection override",
		.test = test_errors_key_storage_prot_override,
		.extension = 1,
	},
	{
		.name = "error checks without key fetch prot override",
		.test = test_errors_key_fetch_prot_override_not_enabled,
		.extension = 1,
	},
	{
		.name = "error checks with key fetch prot override",
		.test = test_errors_key_fetch_prot_override_enabled,
		.extension = 1,
	},
};

int main(int argc, char *argv[])
{
	int extension_cap, idx;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_MEM_OP));

	setbuf(stdout, NULL);	/* Tell stdout not to buffer its content */

	ksft_print_header();

	ksft_set_plan(ARRAY_SIZE(testlist));

	extension_cap = kvm_check_cap(KVM_CAP_S390_MEM_OP_EXTENSION);
	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		if (extension_cap >= testlist[idx].extension) {
			testlist[idx].test();
			ksft_test_result_pass("%s\n", testlist[idx].name);
		} else {
			ksft_test_result_skip("%s - extension level %d not supported\n",
					      testlist[idx].name,
					      testlist[idx].extension);
		}
	}

	ksft_finished();	/* Print results and exit() accordingly */
}
