// SPDX-License-Identifier: GPL-2.0
#include "libunwind-arch.h"
#include "../debug.h"
#include "../maps.h"
#include <elf.h>
#include <errno.h>

int get_perf_regnum_for_unw_regnum(unsigned int e_machine, int unw_regnum)
{
	switch (e_machine) {
	case EM_ARM:
		return __get_perf_regnum_for_unw_regnum_arm(unw_regnum);
	case EM_AARCH64:
		return __get_perf_regnum_for_unw_regnum_arm64(unw_regnum);
	case EM_LOONGARCH:
		return __get_perf_regnum_for_unw_regnum_loongarch(unw_regnum);
	case EM_MIPS:
		return __get_perf_regnum_for_unw_regnum_mips(unw_regnum);
	case EM_PPC:
		return __get_perf_regnum_for_unw_regnum_ppc32(unw_regnum);
	case EM_PPC64:
		return __get_perf_regnum_for_unw_regnum_ppc64(unw_regnum);
	case EM_RISCV:
		return __get_perf_regnum_for_unw_regnum_riscv(unw_regnum);
	case EM_S390:
		return __get_perf_regnum_for_unw_regnum_s390(unw_regnum);
	case EM_386:
		return __get_perf_regnum_for_unw_regnum_i386(unw_regnum);
	case EM_X86_64:
		return __get_perf_regnum_for_unw_regnum_x86_64(unw_regnum);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return -EINVAL;
	}
}


void libunwind_arch__flush_access(struct maps *maps)
{
	unsigned int e_machine = maps__e_machine(maps);

	switch (e_machine) {
	case EM_NONE:
		break;  // No libunwind info on the maps.
	case EM_ARM:
		__libunwind_arch__flush_access_arm(maps);
		break;
	case EM_AARCH64:
		__libunwind_arch__flush_access_arm64(maps);
		break;
	case EM_LOONGARCH:
		__libunwind_arch__flush_access_loongarch(maps);
		break;
	case EM_MIPS:
		__libunwind_arch__flush_access_mips(maps);
		break;
	case EM_PPC:
		__libunwind_arch__flush_access_ppc32(maps);
		break;
	case EM_PPC64:
		__libunwind_arch__flush_access_ppc64(maps);
		break;
	case EM_RISCV:
		__libunwind_arch__flush_access_riscv(maps);
		break;
	case EM_S390:
		__libunwind_arch__flush_access_s390(maps);
		break;
	case EM_386:
		__libunwind_arch__flush_access_i386(maps);
		break;
	case EM_X86_64:
		__libunwind_arch__flush_access_x86_64(maps);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		break;
	}
}

void libunwind_arch__finish_access(struct maps *maps)
{
	unsigned int e_machine = maps__e_machine(maps);

	switch (e_machine) {
	case EM_NONE:
		break;  // No libunwind info on the maps.
	case EM_ARM:
		__libunwind_arch__finish_access_arm(maps);
		break;
	case EM_AARCH64:
		__libunwind_arch__finish_access_arm64(maps);
		break;
	case EM_LOONGARCH:
		__libunwind_arch__finish_access_loongarch(maps);
		break;
	case EM_MIPS:
		__libunwind_arch__finish_access_mips(maps);
		break;
	case EM_PPC:
		__libunwind_arch__finish_access_ppc32(maps);
		break;
	case EM_PPC64:
		__libunwind_arch__finish_access_ppc64(maps);
		break;
	case EM_RISCV:
		__libunwind_arch__finish_access_riscv(maps);
		break;
	case EM_S390:
		__libunwind_arch__finish_access_s390(maps);
		break;
	case EM_386:
		__libunwind_arch__finish_access_i386(maps);
		break;
	case EM_X86_64:
		__libunwind_arch__finish_access_x86_64(maps);
		break;
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		break;
	}
}

void *libunwind_arch__create_addr_space(unsigned int e_machine)
{
	switch (e_machine) {
	case EM_ARM:
		return __libunwind_arch__create_addr_space_arm();
	case EM_AARCH64:
		return __libunwind_arch__create_addr_space_arm64();
	case EM_LOONGARCH:
		return __libunwind_arch__create_addr_space_loongarch();
	case EM_MIPS:
		return __libunwind_arch__create_addr_space_mips();
	case EM_PPC:
		return __libunwind_arch__create_addr_space_ppc32();
	case EM_PPC64:
		return __libunwind_arch__create_addr_space_ppc64();
	case EM_RISCV:
		return __libunwind_arch__create_addr_space_riscv();
	case EM_S390:
		return __libunwind_arch__create_addr_space_s390();
	case EM_386:
		return __libunwind_arch__create_addr_space_i386();
	case EM_X86_64:
		return __libunwind_arch__create_addr_space_x86_64();
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return NULL;
	}
}

int libunwind_arch__dwarf_search_unwind_table(unsigned int e_machine,
					      void *as,
					      uint64_t ip,
					      struct libarch_unwind__dyn_info *di,
					      void *pi,
					      int need_unwind_info,
					      void *arg)
{
	switch (e_machine) {
	case EM_ARM:
		return __libunwind_arch__dwarf_search_unwind_table_arm(as, ip, di, pi,
								       need_unwind_info, arg);
	case EM_AARCH64:
		return __libunwind_arch__dwarf_search_unwind_table_arm64(as, ip, di, pi,
									 need_unwind_info, arg);
	case EM_LOONGARCH:
		return __libunwind_arch__dwarf_search_unwind_table_loongarch(as, ip, di, pi,
									     need_unwind_info, arg);
	case EM_MIPS:
		return __libunwind_arch__dwarf_search_unwind_table_mips(as, ip, di, pi,
									need_unwind_info, arg);
	case EM_PPC:
		return __libunwind_arch__dwarf_search_unwind_table_ppc32(as, ip, di, pi,
									 need_unwind_info, arg);
	case EM_PPC64:
		return __libunwind_arch__dwarf_search_unwind_table_ppc64(as, ip, di, pi,
									 need_unwind_info, arg);
	case EM_RISCV:
		return __libunwind_arch__dwarf_search_unwind_table_riscv(as, ip, di, pi,
									need_unwind_info, arg);
	case EM_S390:
		return __libunwind_arch__dwarf_search_unwind_table_s390(as, ip, di, pi,
									need_unwind_info, arg);
	case EM_386:
		return __libunwind_arch__dwarf_search_unwind_table_i386(as, ip, di, pi,
									need_unwind_info, arg);
	case EM_X86_64:
		return __libunwind_arch__dwarf_search_unwind_table_x86_64(as, ip, di, pi,
									  need_unwind_info, arg);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return -EINVAL;
	}
}

int libunwind_arch__dwarf_find_debug_frame(unsigned int e_machine,
					   int found,
					   struct libarch_unwind__dyn_info *di_debug,
					   uint64_t ip,
					   uint64_t segbase,
					   const char *obj_name,
					   uint64_t start,
					   uint64_t end)
{
	switch (e_machine) {
	case EM_ARM:
		return __libunwind_arch__dwarf_find_debug_frame_arm(found, di_debug, ip, segbase,
								    obj_name, start, end);
	case EM_AARCH64:
		return __libunwind_arch__dwarf_find_debug_frame_arm64(found, di_debug, ip, segbase,
								      obj_name, start, end);
	case EM_LOONGARCH:
		return __libunwind_arch__dwarf_find_debug_frame_loongarch(found, di_debug, ip,
									  segbase, obj_name,
									  start, end);
	case EM_MIPS:
		return __libunwind_arch__dwarf_find_debug_frame_mips(found, di_debug, ip, segbase,
								     obj_name, start, end);
	case EM_PPC:
		return __libunwind_arch__dwarf_find_debug_frame_ppc32(found, di_debug, ip, segbase,
								      obj_name, start, end);
	case EM_PPC64:
		return __libunwind_arch__dwarf_find_debug_frame_ppc64(found, di_debug, ip, segbase,
								      obj_name, start, end);
	case EM_RISCV:
		return __libunwind_arch__dwarf_find_debug_frame_riscv(found, di_debug, ip, segbase,
								     obj_name, start, end);
	case EM_S390:
		return __libunwind_arch__dwarf_find_debug_frame_s390(found, di_debug, ip, segbase,
								     obj_name, start, end);
	case EM_386:
		return __libunwind_arch__dwarf_find_debug_frame_i386(found, di_debug, ip, segbase,
								     obj_name, start, end);
	case EM_X86_64:
		return __libunwind_arch__dwarf_find_debug_frame_x86_64(found, di_debug, ip, segbase,
								       obj_name, start, end);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return -EINVAL;
	}
}

struct unwind_info *libunwind_arch_unwind_info__new(struct thread *thread,
						    struct perf_sample *sample, int max_stack,
						    bool best_effort, uint16_t e_machine,
						    uint64_t first_ip)
{
	switch (e_machine) {
	case EM_ARM:
		return __libunwind_arch_unwind_info__new_arm(thread, sample, max_stack,
							     best_effort, first_ip);
	case EM_AARCH64:
		return __libunwind_arch_unwind_info__new_arm64(thread, sample, max_stack,
							       best_effort, first_ip);
	case EM_LOONGARCH:
		return __libunwind_arch_unwind_info__new_loongarch(thread, sample, max_stack,
								   best_effort, first_ip);
	case EM_MIPS:
		return __libunwind_arch_unwind_info__new_mips(thread, sample, max_stack,
							      best_effort, first_ip);
	case EM_PPC:
		return __libunwind_arch_unwind_info__new_ppc32(thread, sample, max_stack,
							       best_effort, first_ip);
	case EM_PPC64:
		return __libunwind_arch_unwind_info__new_ppc64(thread, sample, max_stack,
							       best_effort, first_ip);
	case EM_RISCV:
		return __libunwind_arch_unwind_info__new_riscv(thread, sample, max_stack,
							      best_effort, first_ip);
	case EM_S390:
		return __libunwind_arch_unwind_info__new_s390(thread, sample, max_stack,
							      best_effort, first_ip);
	case EM_386:
		return __libunwind_arch_unwind_info__new_i386(thread, sample, max_stack,
							      best_effort, first_ip);
	case EM_X86_64:
		return __libunwind_arch_unwind_info__new_x86_64(thread, sample, max_stack,
								best_effort, first_ip);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", e_machine);
		return NULL;
	}
}

void libunwind_arch_unwind_info__delete(struct unwind_info *ui)
{
	free(ui);
}

int libunwind_arch__unwind_step(struct unwind_info *ui)
{
	switch (ui->e_machine) {
	case EM_ARM:
		return __libunwind_arch__unwind_step_arm(ui);
	case EM_AARCH64:
		return __libunwind_arch__unwind_step_arm64(ui);
	case EM_LOONGARCH:
		return __libunwind_arch__unwind_step_loongarch(ui);
	case EM_MIPS:
		return __libunwind_arch__unwind_step_mips(ui);
	case EM_PPC:
		return __libunwind_arch__unwind_step_ppc32(ui);
	case EM_PPC64:
		return __libunwind_arch__unwind_step_ppc64(ui);
	case EM_RISCV:
		return __libunwind_arch__unwind_step_riscv(ui);
	case EM_S390:
		return __libunwind_arch__unwind_step_s390(ui);
	case EM_386:
		return __libunwind_arch__unwind_step_i386(ui);
	case EM_X86_64:
		return __libunwind_arch__unwind_step_x86_64(ui);
	default:
		pr_err("ELF MACHINE %x is not supported.\n", ui->e_machine);
		return -EINVAL;
	}
}
