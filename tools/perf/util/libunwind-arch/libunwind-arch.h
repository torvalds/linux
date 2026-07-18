/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBUNWIND_ARCH_H
#define __LIBUNWIND_ARCH_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct machine;
struct maps;
struct perf_sample;
struct thread;

struct unwind_info {
	struct machine		*machine;
	struct thread		*thread;
	struct perf_sample	*sample;
	void			*cursor;
	uint64_t		*ips;
	int			 cur_ip;
	int			 max_ips;
	unsigned int		 unw_word_t_size;
	uint16_t		 e_machine;
	bool			 best_effort;
};

struct libarch_unwind__dyn_info {
	uint64_t start_ip;
	uint64_t end_ip;
	uint64_t segbase;
	uint64_t table_data;
	uint64_t table_len;
};
struct libarch_unwind__proc_info;

int __get_perf_regnum_for_unw_regnum_arm(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_arm64(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_loongarch(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_mips(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_ppc32(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_ppc64(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_riscv(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_s390(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_i386(int unw_regnum);
int __get_perf_regnum_for_unw_regnum_x86_64(int unw_regnum);
int get_perf_regnum_for_unw_regnum(unsigned int e_machine, int unw_regnum);

void __libunwind_arch__flush_access_arm(struct maps *maps);
void __libunwind_arch__flush_access_arm64(struct maps *maps);
void __libunwind_arch__flush_access_loongarch(struct maps *maps);
void __libunwind_arch__flush_access_mips(struct maps *maps);
void __libunwind_arch__flush_access_ppc32(struct maps *maps);
void __libunwind_arch__flush_access_ppc64(struct maps *maps);
void __libunwind_arch__flush_access_riscv(struct maps *maps);
void __libunwind_arch__flush_access_s390(struct maps *maps);
void __libunwind_arch__flush_access_i386(struct maps *maps);
void __libunwind_arch__flush_access_x86_64(struct maps *maps);
void libunwind_arch__flush_access(struct maps *maps);

void __libunwind_arch__finish_access_arm(struct maps *maps);
void __libunwind_arch__finish_access_arm64(struct maps *maps);
void __libunwind_arch__finish_access_loongarch(struct maps *maps);
void __libunwind_arch__finish_access_mips(struct maps *maps);
void __libunwind_arch__finish_access_ppc32(struct maps *maps);
void __libunwind_arch__finish_access_ppc64(struct maps *maps);
void __libunwind_arch__finish_access_riscv(struct maps *maps);
void __libunwind_arch__finish_access_s390(struct maps *maps);
void __libunwind_arch__finish_access_i386(struct maps *maps);
void __libunwind_arch__finish_access_x86_64(struct maps *maps);
void libunwind_arch__finish_access(struct maps *maps);

void *__libunwind_arch__create_addr_space_arm(void);
void *__libunwind_arch__create_addr_space_arm64(void);
void *__libunwind_arch__create_addr_space_loongarch(void);
void *__libunwind_arch__create_addr_space_mips(void);
void *__libunwind_arch__create_addr_space_ppc32(void);
void *__libunwind_arch__create_addr_space_ppc64(void);
void *__libunwind_arch__create_addr_space_riscv(void);
void *__libunwind_arch__create_addr_space_s390(void);
void *__libunwind_arch__create_addr_space_i386(void);
void *__libunwind_arch__create_addr_space_x86_64(void);
void *libunwind_arch__create_addr_space(unsigned int e_machine);

int __libunwind__find_proc_info(void *as, uint64_t ip, void *pi, int need_unwind_info, void *arg);
int __libunwind__access_mem(void *as, uint64_t addr, void *valp_word, int __write, void *arg);
int __libunwind__access_reg(void *as, int regnum, void *valp_word, int __write, void *arg);

int __libunwind_arch__dwarf_search_unwind_table_arm(void *as, uint64_t ip,
						    struct libarch_unwind__dyn_info *di,
						    void *pi,
						    int need_unwind_info,
						    void *arg);
int __libunwind_arch__dwarf_search_unwind_table_arm64(void *as, uint64_t ip,
						      struct libarch_unwind__dyn_info *di,
						      void *pi,
						      int need_unwind_info,
						      void *arg);
int __libunwind_arch__dwarf_search_unwind_table_loongarch(void *as, uint64_t ip,
							  struct libarch_unwind__dyn_info *di,
							  void *pi,
							  int need_unwind_info,
							  void *arg);
int __libunwind_arch__dwarf_search_unwind_table_mips(void *as, uint64_t ip,
						     struct libarch_unwind__dyn_info *di,
						     void *pi,
						     int need_unwind_info,
						     void *arg);
int __libunwind_arch__dwarf_search_unwind_table_ppc32(void *as, uint64_t ip,
						      struct libarch_unwind__dyn_info *di,
						      void *pi,
						      int need_unwind_info,
						      void *arg);
int __libunwind_arch__dwarf_search_unwind_table_ppc64(void *as, uint64_t ip,
						      struct libarch_unwind__dyn_info *di,
						      void *pi,
						      int need_unwind_info,
						      void *arg);
int __libunwind_arch__dwarf_search_unwind_table_riscv(void *as, uint64_t ip,
						     struct libarch_unwind__dyn_info *di,
						     void *pi,
						     int need_unwind_info,
						     void *arg);
int __libunwind_arch__dwarf_search_unwind_table_s390(void *as, uint64_t ip,
						     struct libarch_unwind__dyn_info *di,
						     void *pi,
						     int need_unwind_info,
						     void *arg);
int __libunwind_arch__dwarf_search_unwind_table_i386(void *as, uint64_t ip,
						     struct libarch_unwind__dyn_info *di,
						     void *pi,
						     int need_unwind_info,
						     void *arg);
int __libunwind_arch__dwarf_search_unwind_table_x86_64(void *as, uint64_t ip,
						       struct libarch_unwind__dyn_info *di,
						       void *pi,
						       int need_unwind_info,
						       void *arg);
int libunwind_arch__dwarf_search_unwind_table(unsigned int e_machine,
					      void *as,
					      uint64_t ip,
					      struct libarch_unwind__dyn_info *di,
					      void *pi,
					      int need_unwind_info,
					      void *arg);

int __libunwind_arch__dwarf_find_debug_frame_arm(int found,
						 struct libarch_unwind__dyn_info *di_debug,
						 uint64_t ip,
						 uint64_t segbase,
						 const char *obj_name,
						 uint64_t start,
						 uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_arm64(int found,
						   struct libarch_unwind__dyn_info *di_debug,
						   uint64_t ip,
						   uint64_t segbase,
						   const char *obj_name,
						   uint64_t start,
						   uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_loongarch(int found,
						       struct libarch_unwind__dyn_info *di_debug,
						       uint64_t ip,
						       uint64_t segbase,
						       const char *obj_name,
						       uint64_t start,
						       uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_mips(int found,
						  struct libarch_unwind__dyn_info *di_debug,
						  uint64_t ip,
						  uint64_t segbase,
						  const char *obj_name,
						  uint64_t start,
						  uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_ppc32(int found,
						   struct libarch_unwind__dyn_info *di_debug,
						   uint64_t ip,
						   uint64_t segbase,
						   const char *obj_name,
						   uint64_t start,
						   uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_ppc64(int found,
						   struct libarch_unwind__dyn_info *di_debug,
						   uint64_t ip,
						   uint64_t segbase,
						   const char *obj_name,
						   uint64_t start,
						   uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_riscv(int found,
						  struct libarch_unwind__dyn_info *di_debug,
						  uint64_t ip,
						  uint64_t segbase,
						  const char *obj_name,
						  uint64_t start,
						  uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_s390(int found,
						  struct libarch_unwind__dyn_info *di_debug,
						  uint64_t ip,
						  uint64_t segbase,
						  const char *obj_name,
						  uint64_t start,
						  uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_i386(int found,
						  struct libarch_unwind__dyn_info *di_debug,
						  uint64_t ip,
						  uint64_t segbase,
						  const char *obj_name,
						  uint64_t start,
						  uint64_t end);
int __libunwind_arch__dwarf_find_debug_frame_x86_64(int found,
						    struct libarch_unwind__dyn_info *di_debug,
						    uint64_t ip,
						    uint64_t segbase,
						    const char *obj_name,
						    uint64_t start,
						    uint64_t end);
int libunwind_arch__dwarf_find_debug_frame(unsigned int e_machine,
					   int found,
					   struct libarch_unwind__dyn_info *di_debug,
					   uint64_t ip,
					   uint64_t segbase,
					   const char *obj_name,
					   uint64_t start,
					   uint64_t end);

struct unwind_info *__libunwind_arch_unwind_info__new_arm(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_arm64(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_loongarch(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_mips(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_ppc32(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_ppc64(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_riscv(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_s390(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_i386(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *__libunwind_arch_unwind_info__new_x86_64(struct thread *thread,
							struct perf_sample *sample,
							   int max_stack,
							   bool best_effort,
							uint64_t first_ip);
struct unwind_info *libunwind_arch_unwind_info__new(struct thread *thread,
						struct perf_sample *sample,
						int max_stack,
						bool best_effort,
						uint16_t e_machine,
						uint64_t first_ip);

void libunwind_arch_unwind_info__delete(struct unwind_info *ui);

int __libunwind_arch__unwind_step_arm(struct unwind_info *ui);
int __libunwind_arch__unwind_step_arm64(struct unwind_info *ui);
int __libunwind_arch__unwind_step_loongarch(struct unwind_info *ui);
int __libunwind_arch__unwind_step_mips(struct unwind_info *ui);
int __libunwind_arch__unwind_step_ppc32(struct unwind_info *ui);
int __libunwind_arch__unwind_step_ppc64(struct unwind_info *ui);
int __libunwind_arch__unwind_step_riscv(struct unwind_info *ui);
int __libunwind_arch__unwind_step_s390(struct unwind_info *ui);
int __libunwind_arch__unwind_step_i386(struct unwind_info *ui);
int __libunwind_arch__unwind_step_x86_64(struct unwind_info *ui);
int libunwind_arch__unwind_step(struct unwind_info *ui);

#endif /* __LIBUNWIND_ARCH_H */
