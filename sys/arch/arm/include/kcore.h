/*	$OpenBSD: kcore.h,v 1.1 2007/05/19 15:49:05 miod Exp $	*/
/* public domain */

/* Make sure this is larger than DRAM_BLOCKS on all arm-based platforms */
#define	NPHYS_RAM_SEGS	8

typedef struct cpu_kcore_hdr {
	u_int32_t	kernelbase;		/* value of KERNEL_BASE */
	u_int32_t	kerneloffs;		/* offset of kernel in RAM */
	u_int32_t	staticsize;		/* size of contiguous mapping */
	u_int32_t	pmap_kernel_l1;		/* pmap_kernel()->pm_l1 */
	u_int32_t	pmap_kernel_l2;		/* pmap_kernel()->pm_l2 */
	u_int32_t	reserved[11];
	phys_ram_seg_t	ram_segs[NPHYS_RAM_SEGS];
} cpu_kcore_hdr_t;
