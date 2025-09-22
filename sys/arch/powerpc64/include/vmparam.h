/*
 * Machine dependent constants for powerpc64.
 */

#define	USRSTACK	VM_MAXUSER_ADDRESS

/*
 * Virtual memory related constants, all in bytes
 */
#define	MAXTSIZ		((paddr_t)256*1024*1024)	/* max text size */
#ifndef DFLDSIZ
#define	DFLDSIZ		((paddr_t)512*1024*1024)	/* initial data size limit */
#endif
#ifndef MAXDSIZ
#define	MAXDSIZ		((paddr_t)32*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		((paddr_t)16*1024*1024*1024)	/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		((paddr_t)2*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		((paddr_t)32*1024*1024)		/* max stack size */
#endif

#define	STACKGAP_RANDOM	256*1024

/*
 * Size of shared memory map
 */
#ifndef	SHMMAXPGS
#define	SHMMAXPGS	1024
#endif

/*
 * Size of User Raw I/O map
 */
#define	USRIOSIZE 	300

#define	VM_PHYS_SIZE		(USRIOSIZE * PAGE_SIZE)

#define VM_PHYSSEG_MAX		32
#define VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH

#define	VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define VM_MAXUSER_ADDRESS	0xbffffffffffff000UL
#define VM_MAX_ADDRESS		0xffffffffffffffffUL
#ifdef _KERNEL
#define VM_MIN_STACK_ADDRESS	0x9000000000000000UL
#endif
#define VM_MIN_KERNEL_ADDRESS	0xc000000000000000UL
#define VM_MAX_KERNEL_ADDRESS	0xc0000007ffffffffUL
