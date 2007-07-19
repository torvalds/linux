#ifndef _ASM_POWERPC_MMU_FSL_BOOKE_H_
#define _ASM_POWERPC_MMU_FSL_BOOKE_H_
/*
 * Freescale Book-E MMU support
 */

/* Book-E defined page sizes */
#define BOOKE_PAGESZ_1K		0
#define BOOKE_PAGESZ_4K		1
#define BOOKE_PAGESZ_16K	2
#define BOOKE_PAGESZ_64K	3
#define BOOKE_PAGESZ_256K	4
#define BOOKE_PAGESZ_1M		5
#define BOOKE_PAGESZ_4M		6
#define BOOKE_PAGESZ_16M	7
#define BOOKE_PAGESZ_64M	8
#define BOOKE_PAGESZ_256M	9
#define BOOKE_PAGESZ_1GB	10
#define BOOKE_PAGESZ_4GB	11
#define BOOKE_PAGESZ_16GB	12
#define BOOKE_PAGESZ_64GB	13
#define BOOKE_PAGESZ_256GB	14
#define BOOKE_PAGESZ_1TB	15

#define MAS0_TLBSEL(x)	((x << 28) & 0x30000000)
#define MAS0_ESEL(x)	((x << 16) & 0x0FFF0000)
#define MAS0_NV(x)	((x) & 0x00000FFF)

#define MAS1_VALID 	0x80000000
#define MAS1_IPROT	0x40000000
#define MAS1_TID(x)	((x << 16) & 0x3FFF0000)
#define MAS1_TS		0x00001000
#define MAS1_TSIZE(x)	((x << 8) & 0x00000F00)

#define MAS2_EPN	0xFFFFF000
#define MAS2_X0		0x00000040
#define MAS2_X1		0x00000020
#define MAS2_W		0x00000010
#define MAS2_I		0x00000008
#define MAS2_M		0x00000004
#define MAS2_G		0x00000002
#define MAS2_E		0x00000001

#define MAS3_RPN	0xFFFFF000
#define MAS3_U0		0x00000200
#define MAS3_U1		0x00000100
#define MAS3_U2		0x00000080
#define MAS3_U3		0x00000040
#define MAS3_UX		0x00000020
#define MAS3_SX		0x00000010
#define MAS3_UW		0x00000008
#define MAS3_SW		0x00000004
#define MAS3_UR		0x00000002
#define MAS3_SR		0x00000001

#define MAS4_TLBSELD(x) MAS0_TLBSEL(x)
#define MAS4_TIDDSEL	0x000F0000
#define MAS4_TSIZED(x)	MAS1_TSIZE(x)
#define MAS4_X0D	0x00000040
#define MAS4_X1D	0x00000020
#define MAS4_WD		0x00000010
#define MAS4_ID		0x00000008
#define MAS4_MD		0x00000004
#define MAS4_GD		0x00000002
#define MAS4_ED		0x00000001

#define MAS6_SPID0	0x3FFF0000
#define MAS6_SPID1	0x00007FFE
#define MAS6_SAS	0x00000001
#define MAS6_SPID	MAS6_SPID0

#define MAS7_RPN	0xFFFFFFFF

#ifndef __ASSEMBLY__

#ifndef CONFIG_PHYS_64BIT
typedef unsigned long phys_addr_t;
#else
typedef unsigned long long phys_addr_t;
#endif

typedef struct {
	unsigned long id;
	unsigned long vdso_base;
} mm_context_t;
#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_FSL_BOOKE_H_ */
