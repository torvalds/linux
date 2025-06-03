// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/*
 * Execute a fastop() instruction, with or without forced emulation.  BT bit 0
 * to set RFLAGS.CF based on whether or not the input is even or odd, so that
 * instructions like ADC and SBB are deterministic.
 */
#define guest_execute_fastop_1(FEP, insn, __val, __flags)				\
({											\
	__asm__ __volatile__("bt $0, %[val]\n\t"					\
			     FEP insn " %[val]\n\t"					\
			     "pushfq\n\t"						\
			     "pop %[flags]\n\t"						\
			     : [val]"+r"(__val), [flags]"=r"(__flags)			\
			     : : "cc", "memory");					\
})

#define guest_test_fastop_1(insn, type_t, __val)					\
({											\
	type_t val = __val, ex_val = __val, input = __val;				\
	uint64_t flags, ex_flags;							\
											\
	guest_execute_fastop_1("", insn, ex_val, ex_flags);				\
	guest_execute_fastop_1(KVM_FEP, insn, val, flags);				\
											\
	__GUEST_ASSERT(val == ex_val,							\
		       "Wanted 0x%lx for '%s 0x%lx', got 0x%lx",			\
		       (uint64_t)ex_val, insn, (uint64_t)input, (uint64_t)val);		\
	__GUEST_ASSERT(flags == ex_flags,						\
			"Wanted flags 0x%lx for '%s 0x%lx', got 0x%lx",			\
			ex_flags, insn, (uint64_t)input, flags);			\
})

#define guest_execute_fastop_2(FEP, insn, __input, __output, __flags)			\
({											\
	__asm__ __volatile__("bt $0, %[output]\n\t"					\
			     FEP insn " %[input], %[output]\n\t"			\
			     "pushfq\n\t"						\
			     "pop %[flags]\n\t"						\
			     : [output]"+r"(__output), [flags]"=r"(__flags)		\
			     : [input]"r"(__input) : "cc", "memory");			\
})

#define guest_test_fastop_2(insn, type_t, __val1, __val2)				\
({											\
	type_t input = __val1, input2 = __val2, output = __val2, ex_output = __val2;	\
	uint64_t flags, ex_flags;							\
											\
	guest_execute_fastop_2("", insn, input, ex_output, ex_flags);			\
	guest_execute_fastop_2(KVM_FEP, insn, input, output, flags);			\
											\
	__GUEST_ASSERT(output == ex_output,						\
		       "Wanted 0x%lx for '%s 0x%lx 0x%lx', got 0x%lx",			\
		       (uint64_t)ex_output, insn, (uint64_t)input,			\
		       (uint64_t)input2, (uint64_t)output);				\
	__GUEST_ASSERT(flags == ex_flags,						\
			"Wanted flags 0x%lx for '%s 0x%lx, 0x%lx', got 0x%lx",		\
			ex_flags, insn, (uint64_t)input, (uint64_t)input2, flags);	\
})

#define guest_execute_fastop_cl(FEP, insn, __shift, __output, __flags)			\
({											\
	__asm__ __volatile__("bt $0, %[output]\n\t"					\
			     FEP insn " %%cl, %[output]\n\t"				\
			     "pushfq\n\t"						\
			     "pop %[flags]\n\t"						\
			     : [output]"+r"(__output), [flags]"=r"(__flags)		\
			     : "c"(__shift) : "cc", "memory");				\
})

#define guest_test_fastop_cl(insn, type_t, __val1, __val2)				\
({											\
	type_t output = __val2, ex_output = __val2, input = __val2;			\
	uint8_t shift = __val1;								\
	uint64_t flags, ex_flags;							\
											\
	guest_execute_fastop_cl("", insn, shift, ex_output, ex_flags);			\
	guest_execute_fastop_cl(KVM_FEP, insn, shift, output, flags);			\
											\
	__GUEST_ASSERT(output == ex_output,						\
		       "Wanted 0x%lx for '%s 0x%x, 0x%lx', got 0x%lx",			\
		       (uint64_t)ex_output, insn, shift, (uint64_t)input,		\
		       (uint64_t)output);						\
	__GUEST_ASSERT(flags == ex_flags,						\
			"Wanted flags 0x%lx for '%s 0x%x, 0x%lx', got 0x%lx",		\
			ex_flags, insn, shift, (uint64_t)input, flags);			\
})

static const uint64_t vals[] = {
	0,
	1,
	2,
	4,
	7,
	0x5555555555555555,
	0xaaaaaaaaaaaaaaaa,
	0xfefefefefefefefe,
	0xffffffffffffffff,
};

#define guest_test_fastops(type_t, suffix)						\
do {											\
	int i, j;									\
											\
	for (i = 0; i < ARRAY_SIZE(vals); i++) {					\
		guest_test_fastop_1("dec" suffix, type_t, vals[i]);			\
		guest_test_fastop_1("inc" suffix, type_t, vals[i]);			\
		guest_test_fastop_1("neg" suffix, type_t, vals[i]);			\
		guest_test_fastop_1("not" suffix, type_t, vals[i]);			\
											\
		for (j = 0; j < ARRAY_SIZE(vals); j++) {				\
			guest_test_fastop_2("add" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("adc" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("and" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bsf" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bsr" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bt" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("btc" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("btr" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bts" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("cmp" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("imul" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("or" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("sbb" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("sub" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("test" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("xor" suffix, type_t, vals[i], vals[j]);	\
											\
			guest_test_fastop_cl("rol" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("ror" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("rcl" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("rcr" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("sar" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("shl" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_cl("shr" suffix, type_t, vals[i], vals[j]);	\
		}									\
	}										\
} while (0)

static void guest_code(void)
{
	guest_test_fastops(uint16_t, "w");
	guest_test_fastops(uint32_t, "l");
	guest_test_fastops(uint64_t, "q");

	GUEST_DONE();
}

int main(int argc, char *argv[])
{
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;

	TEST_REQUIRE(is_forced_emulation_enabled);

	vm = vm_create_with_one_vcpu(&vcpu, guest_code);

	vcpu_run(vcpu);
	TEST_ASSERT_EQ(get_ucall(vcpu, NULL), UCALL_DONE);

	kvm_vm_free(vm);
}
