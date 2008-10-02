#ifndef __ASM_KGDB_H_
#define __ASM_KGDB_H_

#ifdef __KERNEL__

#include <asm/sgidefs.h>

#if (_MIPS_ISA == _MIPS_ISA_MIPS1) || (_MIPS_ISA == _MIPS_ISA_MIPS2) || \
	(_MIPS_ISA == _MIPS_ISA_MIPS32)

#define KGDB_GDB_REG_SIZE 32

#elif (_MIPS_ISA == _MIPS_ISA_MIPS3) || (_MIPS_ISA == _MIPS_ISA_MIPS4) || \
	(_MIPS_ISA == _MIPS_ISA_MIPS64)

#ifdef CONFIG_32BIT
#define KGDB_GDB_REG_SIZE 32
#else /* CONFIG_CPU_32BIT */
#define KGDB_GDB_REG_SIZE 64
#endif
#else
#error "Need to set KGDB_GDB_REG_SIZE for MIPS ISA"
#endif /* _MIPS_ISA */

#define BUFMAX			2048
#if (KGDB_GDB_REG_SIZE == 32)
#define NUMREGBYTES		(90*sizeof(u32))
#define NUMCRITREGBYTES		(12*sizeof(u32))
#else
#define NUMREGBYTES		(90*sizeof(u64))
#define NUMCRITREGBYTES		(12*sizeof(u64))
#endif
#define BREAK_INSTR_SIZE	4
#define CACHE_FLUSH_IS_SAFE	0

extern void arch_kgdb_breakpoint(void);
extern int kgdb_early_setup;
extern void *saved_vectors[32];
extern void handle_exception(struct pt_regs *regs);
extern void breakinst(void);

#endif				/* __KERNEL__ */

#endif /* __ASM_KGDB_H_ */
