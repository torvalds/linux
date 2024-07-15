// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <stdint.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

#include "bpf_misc.h"

struct core_reloc_bitfields {
	/* unsigned bitfields */
	uint8_t		ub1: 1;
	uint8_t		ub2: 2;
	uint32_t	ub7: 7;
	/* signed bitfields */
	int8_t		sb4: 4;
	int32_t		sb20: 20;
	/* non-bitfields */
	uint32_t	u32;
	int32_t		s32;
} __attribute__((preserve_access_index));

SEC("tc")
__description("single CO-RE bitfield roundtrip")
__btf_path("btf__core_reloc_bitfields.bpf.o")
__success
__retval(3)
int single_field_roundtrip(struct __sk_buff *ctx)
{
	struct core_reloc_bitfields bitfields;

	__builtin_memset(&bitfields, 0, sizeof(bitfields));
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub2, 3);
	return BPF_CORE_READ_BITFIELD(&bitfields, ub2);
}

SEC("tc")
__description("multiple CO-RE bitfield roundtrip")
__btf_path("btf__core_reloc_bitfields.bpf.o")
__success
__retval(0x3FD)
int multiple_field_roundtrip(struct __sk_buff *ctx)
{
	struct core_reloc_bitfields bitfields;
	uint8_t ub2;
	int8_t sb4;

	__builtin_memset(&bitfields, 0, sizeof(bitfields));
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub2, 1);
	BPF_CORE_WRITE_BITFIELD(&bitfields, sb4, -1);

	ub2 = BPF_CORE_READ_BITFIELD(&bitfields, ub2);
	sb4 = BPF_CORE_READ_BITFIELD(&bitfields, sb4);

	return (((uint8_t)sb4) << 2) | ub2;
}

SEC("tc")
__description("adjacent CO-RE bitfield roundtrip")
__btf_path("btf__core_reloc_bitfields.bpf.o")
__success
__retval(7)
int adjacent_field_roundtrip(struct __sk_buff *ctx)
{
	struct core_reloc_bitfields bitfields;
	uint8_t ub1, ub2;

	__builtin_memset(&bitfields, 0, sizeof(bitfields));
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub1, 1);
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub2, 3);

	ub1 = BPF_CORE_READ_BITFIELD(&bitfields, ub1);
	ub2 = BPF_CORE_READ_BITFIELD(&bitfields, ub2);

	return (ub2 << 1) | ub1;
}

SEC("tc")
__description("multibyte CO-RE bitfield roundtrip")
__btf_path("btf__core_reloc_bitfields.bpf.o")
__success
__retval(0x21)
int multibyte_field_roundtrip(struct __sk_buff *ctx)
{
	struct core_reloc_bitfields bitfields;
	uint32_t ub7;
	uint8_t ub1;

	__builtin_memset(&bitfields, 0, sizeof(bitfields));
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub1, 1);
	BPF_CORE_WRITE_BITFIELD(&bitfields, ub7, 16);

	ub1 = BPF_CORE_READ_BITFIELD(&bitfields, ub1);
	ub7 = BPF_CORE_READ_BITFIELD(&bitfields, ub7);

	return (ub7 << 1) | ub1;
}

char _license[] SEC("license") = "GPL";
