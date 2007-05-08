#ifndef _ASM_POWERPC_MMU_44X_H_
#define _ASM_POWERPC_MMU_44X_H_
/*
 * PPC440 support
 */

#define PPC44x_MMUCR_TID	0x000000ff
#define PPC44x_MMUCR_STS	0x00010000

#define	PPC44x_TLB_PAGEID	0
#define	PPC44x_TLB_XLAT		1
#define	PPC44x_TLB_ATTRIB	2

/* Page identification fields */
#define PPC44x_TLB_EPN_MASK	0xfffffc00      /* Effective Page Number */
#define	PPC44x_TLB_VALID	0x00000200      /* Valid flag */
#define PPC44x_TLB_TS		0x00000100	/* Translation address space */
#define PPC44x_TLB_1K		0x00000000	/* Page sizes */
#define PPC44x_TLB_4K		0x00000010
#define PPC44x_TLB_16K		0x00000020
#define PPC44x_TLB_64K		0x00000030
#define PPC44x_TLB_256K		0x00000040
#define PPC44x_TLB_1M		0x00000050
#define PPC44x_TLB_16M		0x00000070
#define	PPC44x_TLB_256M		0x00000090

/* Translation fields */
#define PPC44x_TLB_RPN_MASK	0xfffffc00      /* Real Page Number */
#define	PPC44x_TLB_ERPN_MASK	0x0000000f

/* Storage attribute and access control fields */
#define PPC44x_TLB_ATTR_MASK	0x0000ff80
#define PPC44x_TLB_U0		0x00008000      /* User 0 */
#define PPC44x_TLB_U1		0x00004000      /* User 1 */
#define PPC44x_TLB_U2		0x00002000      /* User 2 */
#define PPC44x_TLB_U3		0x00001000      /* User 3 */
#define PPC44x_TLB_W		0x00000800      /* Caching is write-through */
#define PPC44x_TLB_I		0x00000400      /* Caching is inhibited */
#define PPC44x_TLB_M		0x00000200      /* Memory is coherent */
#define PPC44x_TLB_G		0x00000100      /* Memory is guarded */
#define PPC44x_TLB_E		0x00000080      /* Memory is guarded */

#define PPC44x_TLB_PERM_MASK	0x0000003f
#define PPC44x_TLB_UX		0x00000020      /* User execution */
#define PPC44x_TLB_UW		0x00000010      /* User write */
#define PPC44x_TLB_UR		0x00000008      /* User read */
#define PPC44x_TLB_SX		0x00000004      /* Super execution */
#define PPC44x_TLB_SW		0x00000002      /* Super write */
#define PPC44x_TLB_SR		0x00000001      /* Super read */

/* Number of TLB entries */
#define PPC44x_TLB_SIZE		64

#ifndef __ASSEMBLY__

typedef unsigned long long phys_addr_t;

extern phys_addr_t fixup_bigphys_addr(phys_addr_t, phys_addr_t);

typedef struct {
	unsigned long id;
	unsigned long vdso_base;
} mm_context_t;

#endif /* !__ASSEMBLY__ */

#ifndef CONFIG_PPC_EARLY_DEBUG_44x
#define PPC44x_EARLY_TLBS	1
#else
#define PPC44x_EARLY_TLBS	2
#define PPC44x_EARLY_DEBUG_VIRTADDR	(ASM_CONST(0xf0000000) \
	| (ASM_CONST(CONFIG_PPC_EARLY_DEBUG_44x_PHYSLOW) & 0xffff))
#endif

/* Size of the TLBs used for pinning in lowmem */
#define PPC_PIN_SIZE	(1 << 28)	/* 256M */

#endif /* _ASM_POWERPC_MMU_44X_H_ */
