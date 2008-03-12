#ifndef _ASM_X8664_PROTO_H
#define _ASM_X8664_PROTO_H 1

#include <asm/ldt.h>

/* misc architecture specific prototypes */

extern void early_idt_handler(void);

extern void system_call(void);
extern void syscall_init(void);

extern void ia32_syscall(void);
extern void ia32_cstar_target(void);
extern void ia32_sysenter_target(void);

extern void reserve_bootmem_generic(unsigned long phys, unsigned len);

extern void syscall32_cpu_init(void);

extern void check_efer(void);

extern int reboot_force;

long do_arch_prctl(struct task_struct *task, int code, unsigned long addr);

#define round_up(x, y) (((x) + (y) - 1) & ~((y) - 1))
#define round_down(x, y) ((x) & ~((y) - 1))

#endif
