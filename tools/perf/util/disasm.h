// SPDX-License-Identifier: GPL-2.0
#ifndef __PERF_UTIL_DISASM_H
#define __PERF_UTIL_DISASM_H

#include "map_symbol.h"

#ifdef HAVE_DWARF_SUPPORT
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

struct arch {
	const char	*name;
	struct ins	*instructions;
	size_t		nr_instructions;
	size_t		nr_instructions_allocated;
	struct ins_ops  *(*associate_instruction_ops)(struct arch *arch, const char *name);
	bool		sorted_instructions;
	bool		initialized;
	const char	*insn_suffix;
	void		*priv;
	unsigned int	model;
	unsigned int	family;
	int		(*init)(struct arch *arch, char *cpuid);
	bool		(*ins_is_fused)(struct arch *arch, const char *ins1,
					const char *ins2);
	struct		{
		char comment_char;
		char skip_functions_char;
		char register_char;
		char memory_ref_char;
		char imm_char;
	} objdump;
#ifdef HAVE_DWARF_SUPPORT
	void		(*update_insn_state)(struct type_state *state,
				struct data_loc_info *dloc, Dwarf_Die *cu_die,
				struct disasm_line *dl);
#endif
};

struct ins {
	const char     *name;
	struct ins_ops *ops;
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
	int (*parse)(struct arch *arch, struct ins_operands *ops, struct map_symbol *ms,
			struct disasm_line *dl);
	int (*scnprintf)(struct ins *ins, char *bf, size_t size,
			 struct ins_operands *ops, int max_ins_name);
};

struct annotate_args {
	struct arch		  *arch;
	struct map_symbol	  ms;
	struct evsel		  *evsel;
	struct annotation_options *options;
	s64			  offset;
	char			  *line;
	int			  line_nr;
	char			  *fileloc;
};

struct arch *arch__find(const char *name);
bool arch__is(struct arch *arch, const char *name);

struct ins_ops *ins__find(struct arch *arch, const char *name, struct disasm_line *dl);
int ins__scnprintf(struct ins *ins, char *bf, size_t size,
		   struct ins_operands *ops, int max_ins_name);

bool ins__is_call(const struct ins *ins);
bool ins__is_jump(const struct ins *ins);
bool ins__is_fused(struct arch *arch, const char *ins1, const char *ins2);
bool ins__is_nop(const struct ins *ins);
bool ins__is_ret(const struct ins *ins);
bool ins__is_lock(const struct ins *ins);

struct disasm_line *disasm_line__new(struct annotate_args *args);
void disasm_line__free(struct disasm_line *dl);

int disasm_line__scnprintf(struct disasm_line *dl, char *bf, size_t size,
			   bool raw, int max_ins_name);

int symbol__disassemble(struct symbol *sym, struct annotate_args *args);

#endif /* __PERF_UTIL_DISASM_H */
