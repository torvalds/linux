#ifndef _DWARF2_H
#define _DWARF2_H 1


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
#define CFI_UNDEFINED .cfi_undefined

#else

/* use assembler line comment character # to ignore the arguments. */
#define CFI_STARTPROC	#
#define CFI_ENDPROC	#
#define CFI_DEF_CFA	#
#define CFI_DEF_CFA_REGISTER	#
#define CFI_DEF_CFA_OFFSET	#
#define CFI_ADJUST_CFA_OFFSET	#
#define CFI_OFFSET	#
#define CFI_REL_OFFSET	#
#define CFI_REGISTER	#
#define CFI_RESTORE	#
#define CFI_REMEMBER_STATE	#
#define CFI_RESTORE_STATE	#
#define CFI_UNDEFINED	#

#endif

#endif
