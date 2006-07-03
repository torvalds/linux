#ifndef _DWARF2_H
#define _DWARF2_H

#include <linux/config.h>

#ifndef __ASSEMBLY__
#warning "asm/dwarf2.h should be only included in pure assembly files"
#endif

/*
   Macros for dwarf2 CFI unwind table entries.
   See "as.info" for details on these pseudo ops. Unfortunately
   they are only supported in very new binutils, so define them
   away for older version.
 */

#ifdef CONFIG_UNWIND_INFO

#define CFI_STARTPROC .cfi_startproc
#define CFI_ENDPROC .cfi_endproc
#define CFI_DEF_CFA .cfi_def_cfa
#define CFI_DEF_CFA_REGISTER .cfi_def_cfa_register
#define CFI_DEF_CFA_OFFSET .cfi_def_cfa_offset
#define CFI_ADJUST_CFA_OFFSET .cfi_adjust_cfa_offset
#define CFI_OFFSET .cfi_offset
#define CFI_REL_OFFSET .cfi_rel_offset
#define CFI_REGISTER .cfi_register
#define CFI_RESTORE .cfi_restore
#define CFI_REMEMBER_STATE .cfi_remember_state
#define CFI_RESTORE_STATE .cfi_restore_state

#else

/* Due to the structure of pre-exisiting code, don't use assembler line
   comment character # to ignore the arguments. Instead, use a dummy macro. */
.macro ignore a=0, b=0, c=0, d=0
.endm

#define CFI_STARTPROC	ignore
#define CFI_ENDPROC	ignore
#define CFI_DEF_CFA	ignore
#define CFI_DEF_CFA_REGISTER	ignore
#define CFI_DEF_CFA_OFFSET	ignore
#define CFI_ADJUST_CFA_OFFSET	ignore
#define CFI_OFFSET	ignore
#define CFI_REL_OFFSET	ignore
#define CFI_REGISTER	ignore
#define CFI_RESTORE	ignore
#define CFI_REMEMBER_STATE ignore
#define CFI_RESTORE_STATE ignore

#endif

#endif
