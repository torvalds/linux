#ifndef _ASM_POWERPC_MMU_HASH32_H_
#define _ASM_POWERPC_MMU_HASH32_H_
/*
 * 32-bit hash table MMU support
 */

/*
 * BATs
 */

/* Block size masks */
#define BL_128K	0x000
#define BL_256K 0x001
#define BL_512K 0x003
#define BL_1M   0x007
#define BL_2M   0x00F
#define BL_4M   0x01F
#define BL_8M   0x03F
#define BL_16M  0x07F
#define BL_32M  0x0FF
#define BL_64M  0x1FF
#define BL_128M 0x3FF
#define BL_256M 0x7FF

/* BAT Access Protection */
#define BPP_XX	0x00		/* No access */
#define BPP_RX	0x01		/* Read only */
#define BPP_RW	0x02		/* Read/write */

#ifndef __ASSEMBLY__
struct ppc_bat {
	struct {
		unsigned long bepi:15;	/* Effective page index (virtual address) */
		unsigned long :4;	/* Unused */
		unsigned long bl:11;	/* Block size mask */
		unsigned long vs:1;	/* Supervisor valid */
		unsigned long vp:1;	/* User valid */
	} batu; 		/* Upper register */
	struct {
		unsigned long brpn:15;	/* Real page index (physical address) */
		unsigned long :10;	/* Unused */
		unsigned long w:1;	/* Write-thru cache */
		unsigned long i:1;	/* Cache inhibit */
		unsigned long m:1;	/* Memory coherence */
		unsigned long g:1;	/* Guarded (MBZ in IBAT) */
		unsigned long :1;	/* Unused */
		unsigned long pp:2;	/* Page access protections */
	} batl;			/* Lower register */
};
#endif /* !__ASSEMBLY__ */

/*
 * Hash table
 */

/* Values for PP (assumes Ks=0, Kp=1) */
#define PP_RWXX	0	/* Supervisor read/write, User none */
#define PP_RWRX 1	/* Supervisor read/write, User read */
#define PP_RWRW 2	/* Supervisor read/write, User read/write */
#define PP_RXRX 3	/* Supervisor read,       User read */

#ifndef __ASSEMBLY__

/* Hardware Page Table Entry */
struct hash_pte {
	unsigned long v:1;	/* Entry is valid */
	unsigned long vsid:24;	/* Virtual segment identifier */
	unsigned long h:1;	/* Hash algorithm indicator */
	unsigned long api:6;	/* Abbreviated page index */
	unsigned long rpn:20;	/* Real (physical) page number */
	unsigned long    :3;	/* Unused */
	unsigned long r:1;	/* Referenced */
	unsigned long c:1;	/* Changed */
	unsigned long w:1;	/* Write-thru cache mode */
	unsigned long i:1;	/* Cache inhibited */
	unsigned long m:1;	/* Memory coherence */
	unsigned long g:1;	/* Guarded */
	unsigned long  :1;	/* Unused */
	unsigned long pp:2;	/* Page protection */
};

typedef struct {
	unsigned long id;
	unsigned long vdso_base;
} mm_context_t;

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_MMU_HASH32_H_ */
