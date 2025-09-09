// SPDX-License-Identifier: GPL-2.0-only
#include "test_util.h"
#include "kvm_util.h"
#include "processor.h"

/*
 * Execute a fastop() instruction, with or without forced emulation.  BT bit 0
 * to set RFLAGS.CF based on whether or not the input is even or odd, so that
 * instructions like ADC and SBB are deterministic.
 */
#define fastop(__insn)									\
	"bt $0, %[bt_val]\n\t"								\
	__insn "\n\t"									\
	"pushfq\n\t"									\
	"pop %[flags]\n\t"

#define flags_constraint(flags_val) [flags]"=r"(flags_val)
#define bt_constraint(__bt_val) [bt_val]"rm"((uint32_t)__bt_val)

#define guest_execute_fastop_1(FEP, insn, __val, __flags)				\
({											\
	__asm__ __volatile__(fastop(FEP insn " %[val]")					\
			     : [val]"+r"(__val), flags_constraint(__flags)		\
			     : bt_constraint(__val)					\
			     : "cc", "memory");						\
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
	__asm__ __volatile__(fastop(FEP insn " %[input], %[output]")			\
			     : [output]"+r"(__output), flags_constraint(__flags)	\
			     : [input]"r"(__input), bt_constraint(__output)		\
			     : "cc", "memory");						\
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
	__asm__ __volatile__(fastop(FEP insn " %%cl, %[output]")			\
			     : [output]"+r"(__output), flags_constraint(__flags)	\
			     : "c"(__shift), bt_constraint(__output)			\
			     : "cc", "memory");						\
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

#define guest_execute_fastop_div(__KVM_ASM_SAFE, insn, __a, __d, __rm, __flags)		\
({											\
	uint64_t ign_error_code;							\
	uint8_t vector;									\
											\
	__asm__ __volatile__(fastop(__KVM_ASM_SAFE(insn " %[denom]"))			\
			     : "+a"(__a), "+d"(__d), flags_constraint(__flags),		\
			       KVM_ASM_SAFE_OUTPUTS(vector, ign_error_code)		\
			     : [denom]"rm"(__rm), bt_constraint(__rm)			\
			     : "cc", "memory", KVM_ASM_SAFE_CLOBBERS);			\
	vector;										\
})

#define guest_test_fastop_div(insn, type_t, __val1, __val2)				\
({											\
	type_t _a = __val1, _d = __val1, rm = __val2;					\
	type_t a = _a, d = _d, ex_a = _a, ex_d = _d;					\
	uint64_t flags, ex_flags;							\
	uint8_t v, ex_v;								\
											\
	ex_v = guest_execute_fastop_div(KVM_ASM_SAFE, insn, ex_a, ex_d, rm, ex_flags);	\
	v = guest_execute_fastop_div(KVM_ASM_SAFE_FEP, insn, a, d, rm, flags);		\
											\
	GUEST_ASSERT_EQ(v, ex_v);							\
	__GUEST_ASSERT(v == ex_v,							\
		       "Wanted vector 0x%x for '%s 0x%lx:0x%lx/0x%lx', got 0x%x",	\
		       ex_v, insn, (uint64_t)_a, (uint64_t)_d, (uint64_t)rm, v);	\
	__GUEST_ASSERT(a == ex_a && d == ex_d,						\
		       "Wanted 0x%lx:0x%lx for '%s 0x%lx:0x%lx/0x%lx', got 0x%lx:0x%lx",\
		       (uint64_t)ex_a, (uint64_t)ex_d, insn, (uint64_t)_a,		\
		       (uint64_t)_d, (uint64_t)rm, (uint64_t)a, (uint64_t)d);		\
	__GUEST_ASSERT(v || ex_v || (flags == ex_flags),				\
			"Wanted flags 0x%lx for '%s  0x%lx:0x%lx/0x%lx', got 0x%lx",	\
			ex_flags, insn, (uint64_t)_a, (uint64_t)_d, (uint64_t)rm, flags);\
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
if (sizeof(type_t) != 1) {							\
			guest_test_fastop_2("bsf" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bsr" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bt" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("btc" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("btr" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("bts" suffix, type_t, vals[i], vals[j]);	\
			guest_test_fastop_2("imul" suffix, type_t, vals[i], vals[j]);	\
}											\
			guest_test_fastop_2("cmp" suffix, type_t, vals[i], vals[j]);	\
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
											\
			guest_test_fastop_div("div" suffix, type_t, vals[i], vals[j]);	\
		}									\
	}										\
} while (0)

static void guest_code(void)
{
	guest_test_fastops(uint8_t, "b");
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
