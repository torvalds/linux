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
#include <pthread.h>

#include <linux/bits.h>

#include "test_util.h"
#include "kvm_util.h"
#include "kselftest.h"
#include "ucall_common.h"
#include "processor.h"

enum mop_target {
	LOGICAL,
	SIDA,
	ABSOLUTE,
	INVALID,
};

enum mop_access_mode {
	READ,
	WRITE,
	CMPXCHG,
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
	void *old;
	uint8_t old_value[16];
	bool *cmpxchg_success;
	uint8_t ar;
	uint8_t key;
};

const uint8_t NO_KEY = 0xff;

static struct kvm_s390_mem_op ksmo_from_desc(struct mop_desc *desc)
{
	struct kvm_s390_mem_op ksmo = {
		.gaddr = (uintptr_t)desc->gaddr,
		.size = desc->size,
		.buf = ((uintptr_t)desc->buf),
		.reserved = "ignored_ignored_ignored_ignored"
	};

	switch (desc->target) {
	case LOGICAL:
		if (desc->mode == READ)
			ksmo.op = KVM_S390_MEMOP_LOGICAL_READ;
		if (desc->mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_LOGICAL_WRITE;
		break;
	case SIDA:
		if (desc->mode == READ)
			ksmo.op = KVM_S390_MEMOP_SIDA_READ;
		if (desc->mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_SIDA_WRITE;
		break;
	case ABSOLUTE:
		if (desc->mode == READ)
			ksmo.op = KVM_S390_MEMOP_ABSOLUTE_READ;
		if (desc->mode == WRITE)
			ksmo.op = KVM_S390_MEMOP_ABSOLUTE_WRITE;
		if (desc->mode == CMPXCHG) {
			ksmo.op = KVM_S390_MEMOP_ABSOLUTE_CMPXCHG;
			ksmo.old_addr = (uint64_t)desc->old;
			memcpy(desc->old_value, desc->old, desc->size);
		}
		break;
	case INVALID:
		ksmo.op = -1;
	}
	if (desc->f_check)
		ksmo.flags |= KVM_S390_MEMOP_F_CHECK_ONLY;
	if (desc->f_inject)
		ksmo.flags |= KVM_S390_MEMOP_F_INJECT_EXCEPTION;
	if (desc->_set_flags)
		ksmo.flags = desc->set_flags;
	if (desc->f_key && desc->key != NO_KEY) {
		ksmo.flags |= KVM_S390_MEMOP_F_SKEY_PROTECTION;
		ksmo.key = desc->key;
	}
	if (desc->_ar)
		ksmo.ar = desc->ar;
	else
		ksmo.ar = 0;
	if (desc->_sida_offset)
		ksmo.sida_offset = desc->sida_offset;

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
	case KVM_S390_MEMOP_ABSOLUTE_CMPXCHG:
		printf("ABSOLUTE, CMPXCHG, ");
		break;
	}
	printf("gaddr=%llu, size=%u, buf=%llu, ar=%u, key=%u, old_addr=%llx",
	       ksmo->gaddr, ksmo->size, ksmo->buf, ksmo->ar, ksmo->key,
	       ksmo->old_addr);
	if (ksmo->flags & KVM_S390_MEMOP_F_CHECK_ONLY)
		printf(", CHECK_ONLY");
	if (ksmo->flags & KVM_S390_MEMOP_F_INJECT_EXCEPTION)
		printf(", INJECT_EXCEPTION");
	if (ksmo->flags & KVM_S390_MEMOP_F_SKEY_PROTECTION)
		printf(", SKEY_PROTECTION");
	puts(")");
}

static int err_memop_ioctl(struct test_info info, struct kvm_s390_mem_op *ksmo,
			   struct mop_desc *desc)
{
	struct kvm_vcpu *vcpu = info.vcpu;

	if (!vcpu)
		return __vm_ioctl(info.vm, KVM_S390_MEM_OP, ksmo);
	else
		return __vcpu_ioctl(vcpu, KVM_S390_MEM_OP, ksmo);
}

static void memop_ioctl(struct test_info info, struct kvm_s390_mem_op *ksmo,
			struct mop_desc *desc)
{
	int r;

	r = err_memop_ioctl(info, ksmo, desc);
	if (ksmo->op == KVM_S390_MEMOP_ABSOLUTE_CMPXCHG) {
		if (desc->cmpxchg_success) {
			int diff = memcmp(desc->old_value, desc->old, desc->size);
			*desc->cmpxchg_success = !diff;
		}
	}
	TEST_ASSERT(!r, __KVM_IOCTL_ERROR("KVM_S390_MEM_OP", r));
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
	__ksmo = ksmo_from_desc(&__desc);					\
	print_memop(__info.vcpu, &__ksmo);					\
	err##memop_ioctl(__info, &__ksmo, &__desc);				\
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
#define CMPXCHG_OLD(o) .old = (o)
#define CMPXCHG_SUCCESS(s) .cmpxchg_success = (s)

#define CHECK_N_DO(f, ...) ({ f(__VA_ARGS__, CHECK_ONLY); f(__VA_ARGS__); })

#define CR0_FETCH_PROTECTION_OVERRIDE	(1UL << (63 - 38))
#define CR0_STORAGE_PROTECTION_OVERRIDE	(1UL << (63 - 39))

static uint8_t __aligned(PAGE_SIZE) mem1[65536];
static uint8_t __aligned(PAGE_SIZE) mem2[65536];

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
	/* End of guest code reached */
	STAGE_DONE,
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
	if (uc.cmd == UCALL_ABORT) {					\
		REPORT_GUEST_ASSERT(uc);				\
	}								\
	TEST_ASSERT_EQ(uc.cmd, UCALL_SYNC);				\
	TEST_ASSERT_EQ(uc.args[1], __stage);				\
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

static void default_write_read(struct test_info copy_cpu, struct test_info mop_cpu,
			       enum mop_target mop_target, uint32_t size, uint8_t key)
{
	prepare_mem12();
	CHECK_N_DO(MOP, mop_cpu, mop_target, WRITE, mem1, size,
		   GADDR_V(mem1), KEY(key));
	HOST_SYNC(copy_cpu, STAGE_COPIED);
	CHECK_N_DO(MOP, mop_cpu, mop_target, READ, mem2, size,
		   GADDR_V(mem2), KEY(key));
	ASSERT_MEM_EQ(mem1, mem2, size);
}

static void default_read(struct test_info copy_cpu, struct test_info mop_cpu,
			 enum mop_target mop_target, uint32_t size, uint8_t key)
{
	prepare_mem12();
	CHECK_N_DO(MOP, mop_cpu, mop_target, WRITE, mem1, size, GADDR_V(mem1));
	HOST_SYNC(copy_cpu, STAGE_COPIED);
	CHECK_N_DO(MOP, mop_cpu, mop_target, READ, mem2, size,
		   GADDR_V(mem2), KEY(key));
	ASSERT_MEM_EQ(mem1, mem2, size);
}

static void default_cmpxchg(struct test_default *test, uint8_t key)
{
	for (int size = 1; size <= 16; size *= 2) {
		for (int offset = 0; offset < 16; offset += size) {
			uint8_t __aligned(16) new[16] = {};
			uint8_t __aligned(16) old[16];
			bool succ;

			prepare_mem12();
			default_write_read(test->vcpu, test->vcpu, LOGICAL, 16, NO_KEY);

			memcpy(&old, mem1, 16);
			MOP(test->vm, ABSOLUTE, CMPXCHG, new + offset,
			    size, GADDR_V(mem1 + offset),
			    CMPXCHG_OLD(old + offset),
			    CMPXCHG_SUCCESS(&succ), KEY(key));
			HOST_SYNC(test->vcpu, STAGE_COPIED);
			MOP(test->vm, ABSOLUTE, READ, mem2, 16, GADDR_V(mem2));
			TEST_ASSERT(succ, "exchange of values should succeed");
			memcpy(mem1 + offset, new + offset, size);
			ASSERT_MEM_EQ(mem1, mem2, 16);

			memcpy(&old, mem1, 16);
			new[offset]++;
			old[offset]++;
			MOP(test->vm, ABSOLUTE, CMPXCHG, new + offset,
			    size, GADDR_V(mem1 + offset),
			    CMPXCHG_OLD(old + offset),
			    CMPXCHG_SUCCESS(&succ), KEY(key));
			HOST_SYNC(test->vcpu, STAGE_COPIED);
			MOP(test->vm, ABSOLUTE, READ, mem2, 16, GADDR_V(mem2));
			TEST_ASSERT(!succ, "exchange of values should not succeed");
			ASSERT_MEM_EQ(mem1, mem2, 16);
			ASSERT_MEM_EQ(&old, mem1, 16);
		}
	}
}

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

	default_write_read(t.vcpu, t.vcpu, LOGICAL, t.size, NO_KEY);

	kvm_vm_free(t.kvm_vm);
}

static void test_copy_access_register(void)
{
	struct test_default t = test_default_init(guest_copy);

	HOST_SYNC(t.vcpu, STAGE_INITED);

	prepare_mem12();
	t.run->psw_mask &= ~(3UL << (63 - 17));
	t.run->psw_mask |= 1UL << (63 - 17);  /* Enable AR mode */

	/*
	 * Primary address space gets used if an access register
	 * contains zero. The host makes use of AR[1] so is a good
	 * candidate to ensure the guest AR (of zero) is used.
	 */
	CHECK_N_DO(MOP, t.vcpu, LOGICAL, WRITE, mem1, t.size,
		   GADDR_V(mem1), AR(1));
	HOST_SYNC(t.vcpu, STAGE_COPIED);

	CHECK_N_DO(MOP, t.vcpu, LOGICAL, READ, mem2, t.size,
		   GADDR_V(mem2), AR(1));
	ASSERT_MEM_EQ(mem1, mem2, t.size);

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
	default_write_read(t.vcpu, t.vm, ABSOLUTE, t.size, NO_KEY);

	/* vm/vcpu, machting key or key 0 */
	default_write_read(t.vcpu, t.vcpu, LOGICAL, t.size, 0);
	default_write_read(t.vcpu, t.vcpu, LOGICAL, t.size, 9);
	default_write_read(t.vcpu, t.vm, ABSOLUTE, t.size, 0);
	default_write_read(t.vcpu, t.vm, ABSOLUTE, t.size, 9);
	/*
	 * There used to be different code paths for key handling depending on
	 * if the region crossed a page boundary.
	 * There currently are not, but the more tests the merrier.
	 */
	default_write_read(t.vcpu, t.vcpu, LOGICAL, 1, 0);
	default_write_read(t.vcpu, t.vcpu, LOGICAL, 1, 9);
	default_write_read(t.vcpu, t.vm, ABSOLUTE, 1, 0);
	default_write_read(t.vcpu, t.vm, ABSOLUTE, 1, 9);

	/* vm/vcpu, mismatching keys on read, but no fetch protection */
	default_read(t.vcpu, t.vcpu, LOGICAL, t.size, 2);
	default_read(t.vcpu, t.vm, ABSOLUTE, t.size, 2);

	kvm_vm_free(t.kvm_vm);
}

static void test_cmpxchg_key(void)
{
	struct test_default t = test_default_init(guest_copy_key);

	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	default_cmpxchg(&t, NO_KEY);
	default_cmpxchg(&t, 0);
	default_cmpxchg(&t, 9);

	kvm_vm_free(t.kvm_vm);
}

static __uint128_t cut_to_size(int size, __uint128_t val)
{
	switch (size) {
	case 1:
		return (uint8_t)val;
	case 2:
		return (uint16_t)val;
	case 4:
		return (uint32_t)val;
	case 8:
		return (uint64_t)val;
	case 16:
		return val;
	}
	GUEST_FAIL("Invalid size = %u", size);
	return 0;
}

static bool popcount_eq(__uint128_t a, __uint128_t b)
{
	unsigned int count_a, count_b;

	count_a = __builtin_popcountl((uint64_t)(a >> 64)) +
		  __builtin_popcountl((uint64_t)a);
	count_b = __builtin_popcountl((uint64_t)(b >> 64)) +
		  __builtin_popcountl((uint64_t)b);
	return count_a == count_b;
}

static __uint128_t rotate(int size, __uint128_t val, int amount)
{
	unsigned int bits = size * 8;

	amount = (amount + bits) % bits;
	val = cut_to_size(size, val);
	if (!amount)
		return val;
	return (val << (bits - amount)) | (val >> amount);
}

const unsigned int max_block = 16;

static void choose_block(bool guest, int i, int *size, int *offset)
{
	unsigned int rand;

	rand = i;
	if (guest) {
		rand = rand * 19 + 11;
		*size = 1 << ((rand % 3) + 2);
		rand = rand * 19 + 11;
		*offset = (rand % max_block) & ~(*size - 1);
	} else {
		rand = rand * 17 + 5;
		*size = 1 << (rand % 5);
		rand = rand * 17 + 5;
		*offset = (rand % max_block) & ~(*size - 1);
	}
}

static __uint128_t permutate_bits(bool guest, int i, int size, __uint128_t old)
{
	unsigned int rand;
	int amount;
	bool swap;

	rand = i;
	rand = rand * 3 + 1;
	if (guest)
		rand = rand * 3 + 1;
	swap = rand % 2 == 0;
	if (swap) {
		int i, j;
		__uint128_t new;
		uint8_t byte0, byte1;

		rand = rand * 3 + 1;
		i = rand % size;
		rand = rand * 3 + 1;
		j = rand % size;
		if (i == j)
			return old;
		new = rotate(16, old, i * 8);
		byte0 = new & 0xff;
		new &= ~0xff;
		new = rotate(16, new, -i * 8);
		new = rotate(16, new, j * 8);
		byte1 = new & 0xff;
		new = (new & ~0xff) | byte0;
		new = rotate(16, new, -j * 8);
		new = rotate(16, new, i * 8);
		new = new | byte1;
		new = rotate(16, new, -i * 8);
		return new;
	}
	rand = rand * 3 + 1;
	amount = rand % (size * 8);
	return rotate(size, old, amount);
}

static bool _cmpxchg(int size, void *target, __uint128_t *old_addr, __uint128_t new)
{
	bool ret;

	switch (size) {
	case 4: {
			uint32_t old = *old_addr;

			asm volatile ("cs %[old],%[new],%[address]"
			    : [old] "+d" (old),
			      [address] "+Q" (*(uint32_t *)(target))
			    : [new] "d" ((uint32_t)new)
			    : "cc"
			);
			ret = old == (uint32_t)*old_addr;
			*old_addr = old;
			return ret;
		}
	case 8: {
			uint64_t old = *old_addr;

			asm volatile ("csg %[old],%[new],%[address]"
			    : [old] "+d" (old),
			      [address] "+Q" (*(uint64_t *)(target))
			    : [new] "d" ((uint64_t)new)
			    : "cc"
			);
			ret = old == (uint64_t)*old_addr;
			*old_addr = old;
			return ret;
		}
	case 16: {
			__uint128_t old = *old_addr;

			asm volatile ("cdsg %[old],%[new],%[address]"
			    : [old] "+d" (old),
			      [address] "+Q" (*(__uint128_t *)(target))
			    : [new] "d" (new)
			    : "cc"
			);
			ret = old == *old_addr;
			*old_addr = old;
			return ret;
		}
	}
	GUEST_FAIL("Invalid size = %u", size);
	return 0;
}

const unsigned int cmpxchg_iter_outer = 100, cmpxchg_iter_inner = 10000;

static void guest_cmpxchg_key(void)
{
	int size, offset;
	__uint128_t old, new;

	set_storage_key_range(mem1, max_block, 0x10);
	set_storage_key_range(mem2, max_block, 0x10);
	GUEST_SYNC(STAGE_SKEYS_SET);

	for (int i = 0; i < cmpxchg_iter_outer; i++) {
		do {
			old = 1;
		} while (!_cmpxchg(16, mem1, &old, 0));
		for (int j = 0; j < cmpxchg_iter_inner; j++) {
			choose_block(true, i + j, &size, &offset);
			do {
				new = permutate_bits(true, i + j, size, old);
			} while (!_cmpxchg(size, mem2 + offset, &old, new));
		}
	}

	GUEST_SYNC(STAGE_DONE);
}

static void *run_guest(void *data)
{
	struct test_info *info = data;

	HOST_SYNC(*info, STAGE_DONE);
	return NULL;
}

static char *quad_to_char(__uint128_t *quad, int size)
{
	return ((char *)quad) + (sizeof(*quad) - size);
}

static void test_cmpxchg_key_concurrent(void)
{
	struct test_default t = test_default_init(guest_cmpxchg_key);
	int size, offset;
	__uint128_t old, new;
	bool success;
	pthread_t thread;

	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);
	prepare_mem12();
	MOP(t.vcpu, LOGICAL, WRITE, mem1, max_block, GADDR_V(mem2));
	pthread_create(&thread, NULL, run_guest, &t.vcpu);

	for (int i = 0; i < cmpxchg_iter_outer; i++) {
		do {
			old = 0;
			new = 1;
			MOP(t.vm, ABSOLUTE, CMPXCHG, &new,
			    sizeof(new), GADDR_V(mem1),
			    CMPXCHG_OLD(&old),
			    CMPXCHG_SUCCESS(&success), KEY(1));
		} while (!success);
		for (int j = 0; j < cmpxchg_iter_inner; j++) {
			choose_block(false, i + j, &size, &offset);
			do {
				new = permutate_bits(false, i + j, size, old);
				MOP(t.vm, ABSOLUTE, CMPXCHG, quad_to_char(&new, size),
				    size, GADDR_V(mem2 + offset),
				    CMPXCHG_OLD(quad_to_char(&old, size)),
				    CMPXCHG_SUCCESS(&success), KEY(1));
			} while (!success);
		}
	}

	pthread_join(thread, NULL);

	MOP(t.vcpu, LOGICAL, READ, mem2, max_block, GADDR_V(mem2));
	TEST_ASSERT(popcount_eq(*(__uint128_t *)mem1, *(__uint128_t *)mem2),
		    "Must retain number of set bits");

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
	default_write_read(t.vcpu, t.vcpu, LOGICAL, t.size, 2);

	kvm_vm_free(t.kvm_vm);
}

static void test_copy_key_fetch_prot(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot);

	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	/* vm/vcpu, matching key, fetch protection in effect */
	default_read(t.vcpu, t.vcpu, LOGICAL, t.size, 9);
	default_read(t.vcpu, t.vm, ABSOLUTE, t.size, 9);

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
	CHECK_N_DO(ERR_PROT_MOP, t.vcpu, LOGICAL, READ, mem2, t.size, GADDR_V(mem1), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, WRITE, mem1, t.size, GADDR_V(mem1), KEY(2));
	CHECK_N_DO(ERR_PROT_MOP, t.vm, ABSOLUTE, READ, mem2, t.size, GADDR_V(mem1), KEY(2));

	kvm_vm_free(t.kvm_vm);
}

static void test_errors_cmpxchg_key(void)
{
	struct test_default t = test_default_init(guest_copy_key_fetch_prot);
	int i;

	HOST_SYNC(t.vcpu, STAGE_INITED);
	HOST_SYNC(t.vcpu, STAGE_SKEYS_SET);

	for (i = 1; i <= 16; i *= 2) {
		__uint128_t old = 0;

		ERR_PROT_MOP(t.vm, ABSOLUTE, CMPXCHG, mem2, i, GADDR_V(mem2),
			     CMPXCHG_OLD(&old), KEY(2));
	}

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
	TEST_ASSERT_EQ(teid & teid_mask, 0);

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
	asm volatile ("sske %[key],%[addr]\n" :: [addr] "r"(0L), [key] "r"(0x18) : "cc");
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
	 * fetch protection override does not apply because memory range exceeded
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
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory address with CHECK_ONLY");
	rv = ERR_MOP(info, target, WRITE, mem1, size, GADDR((void *)~0xfffUL));
	TEST_ASSERT(rv > 0, "ioctl does not report bad guest memory address on write");

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

static void test_errors_cmpxchg(void)
{
	struct test_default t = test_default_init(guest_idle);
	__uint128_t old;
	int rv, i, power = 1;

	HOST_SYNC(t.vcpu, STAGE_INITED);

	for (i = 0; i < 32; i++) {
		if (i == power) {
			power *= 2;
			continue;
		}
		rv = ERR_MOP(t.vm, ABSOLUTE, CMPXCHG, mem1, i, GADDR_V(mem1),
			     CMPXCHG_OLD(&old));
		TEST_ASSERT(rv == -1 && errno == EINVAL,
			    "ioctl allows bad size for cmpxchg");
	}
	for (i = 1; i <= 16; i *= 2) {
		rv = ERR_MOP(t.vm, ABSOLUTE, CMPXCHG, mem1, i, GADDR((void *)~0xfffUL),
			     CMPXCHG_OLD(&old));
		TEST_ASSERT(rv > 0, "ioctl allows bad guest address for cmpxchg");
	}
	for (i = 2; i <= 16; i *= 2) {
		rv = ERR_MOP(t.vm, ABSOLUTE, CMPXCHG, mem1, i, GADDR_V(mem1 + 1),
			     CMPXCHG_OLD(&old));
		TEST_ASSERT(rv == -1 && errno == EINVAL,
			    "ioctl allows bad alignment for cmpxchg");
	}

	kvm_vm_free(t.kvm_vm);
}

int main(int argc, char *argv[])
{
	int extension_cap, idx;

	TEST_REQUIRE(kvm_has_cap(KVM_CAP_S390_MEM_OP));
	extension_cap = kvm_check_cap(KVM_CAP_S390_MEM_OP_EXTENSION);

	struct testdef {
		const char *name;
		void (*test)(void);
		bool requirements_met;
	} testlist[] = {
		{
			.name = "simple copy",
			.test = test_copy,
			.requirements_met = true,
		},
		{
			.name = "generic error checks",
			.test = test_errors,
			.requirements_met = true,
		},
		{
			.name = "copy with storage keys",
			.test = test_copy_key,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "cmpxchg with storage keys",
			.test = test_cmpxchg_key,
			.requirements_met = extension_cap & 0x2,
		},
		{
			.name = "concurrently cmpxchg with storage keys",
			.test = test_cmpxchg_key_concurrent,
			.requirements_met = extension_cap & 0x2,
		},
		{
			.name = "copy with key storage protection override",
			.test = test_copy_key_storage_prot_override,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "copy with key fetch protection",
			.test = test_copy_key_fetch_prot,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "copy with key fetch protection override",
			.test = test_copy_key_fetch_prot_override,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "copy with access register mode",
			.test = test_copy_access_register,
			.requirements_met = true,
		},
		{
			.name = "error checks with key",
			.test = test_errors_key,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "error checks for cmpxchg with key",
			.test = test_errors_cmpxchg_key,
			.requirements_met = extension_cap & 0x2,
		},
		{
			.name = "error checks for cmpxchg",
			.test = test_errors_cmpxchg,
			.requirements_met = extension_cap & 0x2,
		},
		{
			.name = "termination",
			.test = test_termination,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "error checks with key storage protection override",
			.test = test_errors_key_storage_prot_override,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "error checks without key fetch prot override",
			.test = test_errors_key_fetch_prot_override_not_enabled,
			.requirements_met = extension_cap > 0,
		},
		{
			.name = "error checks with key fetch prot override",
			.test = test_errors_key_fetch_prot_override_enabled,
			.requirements_met = extension_cap > 0,
		},
	};

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(testlist));

	for (idx = 0; idx < ARRAY_SIZE(testlist); idx++) {
		if (testlist[idx].requirements_met) {
			testlist[idx].test();
			ksft_test_result_pass("%s\n", testlist[idx].name);
		} else {
			ksft_test_result_skip("%s - requirements not met (kernel has extension cap %#x)\n",
					      testlist[idx].name, extension_cap);
		}
	}

	ksft_finished();	/* Print results and exit() accordingly */
}
