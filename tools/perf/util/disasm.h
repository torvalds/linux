// SPDX-License-Identifier: GPL-2.0
#ifndef __PERF_UTIL_DISASM_H
#define __PERF_UTIL_DISASM_H

#include "map_symbol.h"

#ifdef HAVE_LIBDW_SUPPORT
#include "dwarf-aux.h"
#endif

struct annotation_options;
struct disasm_line;
struct ins;
struct evsel;
struct symbol;
struct data_loc_info;
struct type_state;
struct disasm_line;

struct e_machine_and_e_flags {
	uint32_t e_flags;
	uint16_t e_machine;
};

struct arch {
	/** @name: name such as "x86" or "powerpc". */
	const char		*name;
	const struct ins	*instructions;
	size_t			nr_instructions;
	size_t			nr_instructions_allocated;
	const char		*insn_suffix;
	unsigned int		model;
	unsigned int		family;
	/** @id: ELF machine and flags associated with arch. */
	struct e_machine_and_e_flags id;
	bool			sorted_instructions;
	struct		{
		char comment_char;
		char skip_functions_char;
		char register_char;
		char memory_ref_char;
		char imm_char;
	} objdump;
	bool		(*ins_is_fused)(const struct arch *arch, const char *ins1,
					const char *ins2);
	const struct ins_ops  *(*associate_instruction_ops)(struct arch *arch, const char *name);
#ifdef HAVE_LIBDW_SUPPORT
	void		(*update_insn_state)(struct type_state *state,
				struct data_loc_info *dloc, Dwarf_Die *cu_die,
				struct disasm_line *dl);
#endif
};

struct ins {
	const char     *name;
	const struct ins_ops *ops;
};

struct ins_operands {
	char	*raw;
	struct {
		char	*raw;
		char	*name;
		struct symbol *sym;
		u64	addr;
		s64	offset;
		bool	offset_avail;
		bool	outside;
		bool	multi_regs;
		bool	mem_ref;
	} target;
	union {
		struct {
			char	*raw;
			char	*name;
			u64	addr;
			bool	multi_regs;
			bool	mem_ref;
		} source;
		struct {
			struct ins	    ins;
			struct ins_operands *ops;
		} locked;
		struct {
			char	*raw_comment;
			char	*raw_func_start;
		} jump;
	};
};

struct ins_ops {
	void (*free)(struct ins_operands *ops);
	int (*parse)(const struct arch *arch, struct ins_operands *ops, struct map_symbol *ms,
			struct disasm_line *dl);
	int (*scnprintf)(const struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops, int max_ins_name);
	bool is_jump;
	bool is_call;
};

struct annotate_args {
	const struct arch	  *arch;
	struct map_symbol	  *ms;
	struct annotation_options *options;
	s64			  offset;
	char			  *line;
	int			  line_nr;
	char			  *fileloc;
};

const struct arch *arch__find(uint16_t e_machine, uint32_t e_flags, const char *cpuid);
bool arch__is_x86(const struct arch *arch);
bool arch__is_powerpc(const struct arch *arch);

extern const struct ins_ops call_ops;
extern const struct ins_ops dec_ops;
extern const struct ins_ops jump_ops;
extern const struct ins_ops mov_ops;
extern const struct ins_ops nop_ops;
extern const struct ins_ops lock_ops;
extern const struct ins_ops ret_ops;

int arch__associate_ins_ops(struct arch *arch, const char *name, const struct ins_ops *ops);

const struct arch *arch__new_arc(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_arm(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_arm64(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_csky(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_loongarch(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_mips(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_powerpc(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_riscv64(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_s390(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_sparc(const struct e_machine_and_e_flags *id, const char *cpuid);
const struct arch *arch__new_x86(const struct e_machine_and_e_flags *id, const char *cpuid);

const struct ins_ops *ins__find(const struct arch *arch, const char *name, struct disasm_line *dl);

bool ins__is_call(const struct ins *ins);
bool ins__is_jump(const struct ins *ins);
bool ins__is_fused(const struct arch *arch, const char *ins1, const char *ins2);
bool ins__is_ret(const struct ins *ins);
bool ins__is_lock(const struct ins *ins);

const struct ins_ops *check_ppc_insn(struct disasm_line *dl);

struct disasm_line *disasm_line__new(struct annotate_args *args);
void disasm_line__free(struct disasm_line *dl);

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size,
			   bool raw, int max_ins_name);

int ins__raw_scnprintf(const struct ins *ins, char *bf, size_t size,
			   struct ins_operands *ops, int max_ins_name);
int ins__scnprintf(const struct ins *ins, char *bf, size_t size,
		   struct ins_operands *ops, int max_ins_name);
int call__scnprintf(const struct ins *ins, char *bf, size_t size,
		    struct ins_operands *ops, int max_ins_name);
int jump__scnprintf(const struct ins *ins, char *bf, size_t size,
		    struct ins_operands *ops, int max_ins_name);
int mov__scnprintf(const struct ins *ins, char *bf, size_t size,
		   struct ins_operands *ops, int max_ins_name);

int symbol__disassemble(struct symbol *sym, struct annotate_args *args);

char *expand_tabs(char *line, char **storage, size_t *storage_len);

#endif /* __PERF_UTIL_DISASM_H */
