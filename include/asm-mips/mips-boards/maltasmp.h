/*
 * There are several SMP models supported
 * SMTC is mutually exclusive to other options (atm)
 */
#if defined(CONFIG_MIPS_MT_SMTC)
#define malta_smtc	1
#define malta_cmp	0
#define malta_smvp	0
#else
#define malta_smtc	0
#if defined(CONFIG_MIPS_CMP)
extern int gcmp_present;
#define malta_cmp	gcmp_present
#else
#define malta_cmp	0
#endif
/* FIXME: should become COMFIG_MIPS_MT_SMVP */
#if defined(CONFIG_MIPS_MT_SMP)
#define malta_smvp	1
#else
#define malta_smvp	0
#endif
#endif

#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>

/* malta_smtc */
#include <asm/smtc.h>
#include <asm/smtc_ipi.h>

/* malta_cmp */
#include <asm/cmp.h>

/* malta_smvp */
#include <asm/smvp.h>
