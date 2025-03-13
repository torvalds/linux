// SPDX-License-Identifier: GPL-2.0-only
/*
 * Get values of vector registers as soon as the program starts to test if
 * is properly cleaning the values before starting a new program. Vector
 * registers are caller saved, so no function calls may happen before reading
 * the values. To further ensure consistency, this file is compiled without
 * libc and without auto-vectorization.
 *
 * To be "clean" all values must be either all ones or all zeroes.
 */

#define __stringify_1(x...)	#x
#define __stringify(x...)	__stringify_1(x)

int main(int argc, char **argv)
{
	char prev_value = 0, value;
	unsigned long vl;
	int first = 1;

	if (argc > 2 && strcmp(argv[2], "x"))
		asm volatile (
			// 0 | zimm[10:0] | rs1 | 1 1 1 | rd |1010111| vsetvli
			// vsetvli	t4, x0, e8, m1, d1
			".4byte		0b00000000000000000111111011010111\n\t"
			"mv		%[vl], t4\n\t"
			: [vl] "=r" (vl) : : "t4"
		);
	else
		asm volatile (
			".option push\n\t"
			".option arch, +v\n\t"
			"vsetvli	%[vl], x0, e8, m1, ta, ma\n\t"
			".option pop\n\t"
			: [vl] "=r" (vl)
		);

#define CHECK_VECTOR_REGISTER(register) ({					\
	for (int i = 0; i < vl; i++) {						\
		asm volatile (							\
			".option push\n\t"					\
			".option arch, +v\n\t"					\
			"vmv.x.s %0, " __stringify(register) "\n\t"		\
			"vsrl.vi " __stringify(register) ", " __stringify(register) ", 8\n\t" \
			".option pop\n\t"					\
			: "=r" (value));					\
		if (first) {							\
			first = 0;						\
		} else if (value != prev_value || !(value == 0x00 || value == 0xff)) { \
			printf("Register " __stringify(register)		\
				" values not clean! value: %u\n", value);	\
			exit(-1);						\
		}								\
		prev_value = value;						\
	}									\
})

	CHECK_VECTOR_REGISTER(v0);
	CHECK_VECTOR_REGISTER(v1);
	CHECK_VECTOR_REGISTER(v2);
	CHECK_VECTOR_REGISTER(v3);
	CHECK_VECTOR_REGISTER(v4);
	CHECK_VECTOR_REGISTER(v5);
	CHECK_VECTOR_REGISTER(v6);
	CHECK_VECTOR_REGISTER(v7);
	CHECK_VECTOR_REGISTER(v8);
	CHECK_VECTOR_REGISTER(v9);
	CHECK_VECTOR_REGISTER(v10);
	CHECK_VECTOR_REGISTER(v11);
	CHECK_VECTOR_REGISTER(v12);
	CHECK_VECTOR_REGISTER(v13);
	CHECK_VECTOR_REGISTER(v14);
	CHECK_VECTOR_REGISTER(v15);
	CHECK_VECTOR_REGISTER(v16);
	CHECK_VECTOR_REGISTER(v17);
	CHECK_VECTOR_REGISTER(v18);
	CHECK_VECTOR_REGISTER(v19);
	CHECK_VECTOR_REGISTER(v20);
	CHECK_VECTOR_REGISTER(v21);
	CHECK_VECTOR_REGISTER(v22);
	CHECK_VECTOR_REGISTER(v23);
	CHECK_VECTOR_REGISTER(v24);
	CHECK_VECTOR_REGISTER(v25);
	CHECK_VECTOR_REGISTER(v26);
	CHECK_VECTOR_REGISTER(v27);
	CHECK_VECTOR_REGISTER(v28);
	CHECK_VECTOR_REGISTER(v29);
	CHECK_VECTOR_REGISTER(v30);
	CHECK_VECTOR_REGISTER(v31);

#undef CHECK_VECTOR_REGISTER

	return 0;
}
