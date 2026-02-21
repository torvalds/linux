/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_REGS_H
#define __PERF_REGS_H

#include <linux/types.h>
#include <linux/compiler.h>

struct regs_dump;

enum {
	SDT_ARG_VALID = 0,
	SDT_ARG_SKIP,
};

int perf_sdt_arg_parse_op(uint16_t e_machine, char *old_op, char **new_op);
uint64_t perf_intr_reg_mask(uint16_t e_machine);
uint64_t perf_user_reg_mask(uint16_t e_machine);

const char *perf_reg_name(int id, uint16_t e_machine, uint32_t e_flags);
int perf_reg_value(u64 *valp, struct regs_dump *regs, int id);
uint64_t perf_arch_reg_ip(uint16_t e_machine);
uint64_t perf_arch_reg_sp(uint16_t e_machine);

int __perf_sdt_arg_parse_op_arm64(char *old_op, char **new_op);
uint64_t __perf_reg_mask_arm64(bool intr);
const char *__perf_reg_name_arm64(int id);
uint64_t __perf_reg_ip_arm64(void);
uint64_t __perf_reg_sp_arm64(void);

uint64_t __perf_reg_mask_arm(bool intr);
const char *__perf_reg_name_arm(int id);
uint64_t __perf_reg_ip_arm(void);
uint64_t __perf_reg_sp_arm(void);

uint64_t __perf_reg_mask_csky(bool intr);
const char *__perf_reg_name_csky(int id, uint32_t e_flags);
uint64_t __perf_reg_ip_csky(void);
uint64_t __perf_reg_sp_csky(void);

uint64_t __perf_reg_mask_loongarch(bool intr);
const char *__perf_reg_name_loongarch(int id);
uint64_t __perf_reg_ip_loongarch(void);
uint64_t __perf_reg_sp_loongarch(void);

uint64_t __perf_reg_mask_mips(bool intr);
const char *__perf_reg_name_mips(int id);
uint64_t __perf_reg_ip_mips(void);
uint64_t __perf_reg_sp_mips(void);

int __perf_sdt_arg_parse_op_powerpc(char *old_op, char **new_op);
uint64_t __perf_reg_mask_powerpc(bool intr);
const char *__perf_reg_name_powerpc(int id);
uint64_t __perf_reg_ip_powerpc(void);
uint64_t __perf_reg_sp_powerpc(void);

uint64_t __perf_reg_mask_riscv(bool intr);
const char *__perf_reg_name_riscv(int id);
uint64_t __perf_reg_ip_riscv(void);
uint64_t __perf_reg_sp_riscv(void);

uint64_t __perf_reg_mask_s390(bool intr);
const char *__perf_reg_name_s390(int id);
uint64_t __perf_reg_ip_s390(void);
uint64_t __perf_reg_sp_s390(void);

int __perf_sdt_arg_parse_op_x86(char *old_op, char **new_op);
uint64_t __perf_reg_mask_x86(bool intr);
const char *__perf_reg_name_x86(int id);
uint64_t __perf_reg_ip_x86(void);
uint64_t __perf_reg_sp_x86(void);

static inline uint64_t DWARF_MINIMAL_REGS(uint16_t e_machine)
{
	return (1ULL << perf_arch_reg_ip(e_machine)) | (1ULL << perf_arch_reg_sp(e_machine));
}

#endif /* __PERF_REGS_H */
