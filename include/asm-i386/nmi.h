/*
 *  linux/include/asm-i386/nmi.h
 */
#ifndef ASM_NMI_H
#define ASM_NMI_H

#include <linux/pm.h>
 
struct pt_regs;
 
typedef int (*nmi_callback_t)(struct pt_regs * regs, int cpu);
 
/** 
 * set_nmi_callback
 *
 * Set a handler for an NMI. Only one handler may be
 * set. Return 1 if the NMI was handled.
 */
void set_nmi_callback(nmi_callback_t callback);
 
/** 
 * unset_nmi_callback
 *
 * Remove the handler previously set.
 */
void unset_nmi_callback(void);
 
#endif /* ASM_NMI_H */
