/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2019 Facebook */

#ifndef __RELO_CORE_H
#define __RELO_CORE_H

/* bpf_core_relo_kind encodes which aspect of captured field/type/enum value
 * has to be adjusted by relocations.
 */
enum bpf_core_relo_kind {
	BPF_FIELD_BYTE_OFFSET = 0,	/* field byte offset */
	BPF_FIELD_BYTE_SIZE = 1,	/* field size in bytes */
	BPF_FIELD_EXISTS = 2,		/* field existence in target kernel */
	BPF_FIELD_SIGNED = 3,		/* field signedness (0 - unsigned, 1 - signed) */
	BPF_FIELD_LSHIFT_U64 = 4,	/* bitfield-specific left bitshift */
	BPF_FIELD_RSHIFT_U64 = 5,	/* bitfield-specific right bitshift */
	BPF_TYPE_ID_LOCAL = 6,		/* type ID in local BPF object */
	BPF_TYPE_ID_TARGET = 7,		/* type ID in target kernel */
	BPF_TYPE_EXISTS = 8,		/* type existence in target kernel */
	BPF_TYPE_SIZE = 9,		/* type size in bytes */
	BPF_ENUMVAL_EXISTS = 10,	/* enum value existence in target kernel */
	BPF_ENUMVAL_VALUE = 11,		/* enum value integer value */
};

/* The minimum bpf_core_relo checked by the loader
 *
 * CO-RE relocation captures the following data:
 * - insn_off - instruction offset (in bytes) within a BPF program that needs
 *   its insn->imm field to be relocated with actual field info;
 * - type_id - BTF type ID of the "root" (containing) entity of a relocatable
 *   type or field;
 * - access_str_off - offset into corresponding .BTF string section. String
 *   interpretation depends on specific relocation kind:
 *     - for field-based relocations, string encodes an accessed field using
 *     a sequence of field and array indices, separated by colon (:). It's
 *     conceptually very close to LLVM's getelementptr ([0]) instruction's
 *     arguments for identifying offset to a field.
 *     - for type-based relocations, strings is expected to be just "0";
 *     - for enum value-based relocations, string contains an index of enum
 *     value within its enum type;
 *
 * Example to provide a better feel.
 *
 *   struct sample {
 *       int a;
 *       struct {
 *           int b[10];
 *       };
 *   };
 *
 *   struct sample *s = ...;
 *   int x = &s->a;     // encoded as "0:0" (a is field #0)
 *   int y = &s->b[5];  // encoded as "0:1:0:5" (anon struct is field #1,
 *                      // b is field #0 inside anon struct, accessing elem #5)
 *   int z = &s[10]->b; // encoded as "10:1" (ptr is used as an array)
 *
 * type_id for all relocs in this example  will capture BTF type id of
 * `struct sample`.
 *
 * Such relocation is emitted when using __builtin_preserve_access_index()
 * Clang built-in, passing expression that captures field address, e.g.:
 *
 * bpf_probe_read(&dst, sizeof(dst),
 *		  __builtin_preserve_access_index(&src->a.b.c));
 *
 * In this case Clang will emit field relocation recording necessary data to
 * be able to find offset of embedded `a.b.c` field within `src` struct.
 *
 *   [0] https://llvm.org/docs/LangRef.html#getelementptr-instruction
 */
struct bpf_core_relo {
	__u32   insn_off;
	__u32   type_id;
	__u32   access_str_off;
	enum bpf_core_relo_kind kind;
};

struct bpf_core_cand {
	const struct btf *btf;
	const struct btf_type *t;
	const char *name;
	__u32 id;
};

/* dynamically sized list of type IDs and its associated struct btf */
struct bpf_core_cand_list {
	struct bpf_core_cand *cands;
	int len;
};

int bpf_core_apply_relo_insn(const char *prog_name,
			     struct bpf_insn *insn, int insn_idx,
			     const struct bpf_core_relo *relo, int relo_idx,
			     const struct btf *local_btf,
			     struct bpf_core_cand_list *cands);
int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
			      const struct btf *targ_btf, __u32 targ_id);

size_t bpf_core_essential_name_len(const char *name);
#endif
