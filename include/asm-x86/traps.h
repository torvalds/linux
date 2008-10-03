#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

/* Common in X86_32 and X86_64 */
asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void simd_coprocessor_error(void);
asmlinkage void alignment_check(void);
asmlinkage void spurious_interrupt_bug(void);
#ifdef CONFIG_X86_MCE
asmlinkage void machine_check(void);
#endif /* CONFIG_X86_MCE */

void do_divide_error(struct pt_regs *, long);
void do_overflow(struct pt_regs *, long);
void do_bounds(struct pt_regs *, long);
void do_coprocessor_segment_overrun(struct pt_regs *, long);
void do_invalid_TSS(struct pt_regs *, long);
void do_segment_not_present(struct pt_regs *, long);
void do_stack_segment(struct pt_regs *, long);
void do_alignment_check(struct pt_regs *, long);
void do_invalid_op(struct pt_regs *, long);
void do_general_protection(struct pt_regs *, long);
void do_nmi(struct pt_regs *, long);

extern int panic_on_unrecovered_nmi;
extern int kstack_depth_to_print;

#ifdef CONFIG_X86_32

void do_iret_error(struct pt_regs *, long);
void do_int3(struct pt_regs *, long);
void do_debug(struct pt_regs *, long);
void math_error(void __user *);
void do_coprocessor_error(struct pt_regs *, long);
void do_simd_coprocessor_error(struct pt_regs *, long);
void do_spurious_interrupt_bug(struct pt_regs *, long);
unsigned long patch_espfix_desc(unsigned long, unsigned long);
asmlinkage void math_emulate(long);

#else /* CONFIG_X86_32 */

asmlinkage void double_fault(void);

asmlinkage void do_int3(struct pt_regs *, long);
asmlinkage void do_stack_segment(struct pt_regs *, long);
asmlinkage void do_debug(struct pt_regs *, unsigned long);
asmlinkage void do_coprocessor_error(struct pt_regs *);
asmlinkage void do_simd_coprocessor_error(struct pt_regs *);
asmlinkage void do_spurious_interrupt_bug(struct pt_regs *);

#endif /* CONFIG_X86_32 */
#endif /* _ASM_X86_TRAPS_H */
