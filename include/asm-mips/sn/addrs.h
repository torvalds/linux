/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 1999, 2000 by Ralf Baechle
 */
#ifndef _ASM_SN_ADDRS_H
#define _ASM_SN_ADDRS_H

#include <linux/config.h>

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif /* !__ASSEMBLY__ */

#include <asm/addrspace.h>
#include <asm/sn/kldir.h>

#if defined(CONFIG_SGI_IP27)
#include <asm/sn/sn0/addrs.h>
#elif defined(CONFIG_SGI_IP35)
#include <asm/sn/sn1/addrs.h>
#endif


#ifndef __ASSEMBLY__

#define PS_UINT_CAST		(unsigned long)
#define UINT64_CAST		(unsigned long)

#define HUBREG_CAST		(volatile hubreg_t *)

#else /* __ASSEMBLY__ */

#define PS_UINT_CAST
#define UINT64_CAST
#define HUBREG_CAST

#endif /* __ASSEMBLY__ */


#define NASID_GET_META(_n)	((_n) >> NASID_LOCAL_BITS)
#ifdef CONFIG_SGI_IP27
#define NASID_GET_LOCAL(_n)	((_n) & 0xf)
#endif
#define NASID_MAKE(_m, _l)	(((_m) << NASID_LOCAL_BITS) | (_l))

#define NODE_ADDRSPACE_MASK	(NODE_ADDRSPACE_SIZE - 1)
#define TO_NODE_ADDRSPACE(_pa)	(UINT64_CAST (_pa) & NODE_ADDRSPACE_MASK)

#define CHANGE_ADDR_NASID(_pa, _nasid)	\
		((UINT64_CAST (_pa) & ~NASID_MASK) | \
		 (UINT64_CAST(_nasid) <<  NASID_SHFT))


/*
 * The following macros are used to index to the beginning of a specific
 * node's address space.
 */

#define NODE_OFFSET(_n)		(UINT64_CAST (_n) << NODE_SIZE_BITS)

#define NODE_CAC_BASE(_n)	(CAC_BASE   + NODE_OFFSET(_n))
#define NODE_HSPEC_BASE(_n)	(HSPEC_BASE + NODE_OFFSET(_n))
#define NODE_IO_BASE(_n)	(IO_BASE    + NODE_OFFSET(_n))
#define NODE_MSPEC_BASE(_n)	(MSPEC_BASE + NODE_OFFSET(_n))
#define NODE_UNCAC_BASE(_n)	(UNCAC_BASE + NODE_OFFSET(_n))

#define TO_NODE(_n, _x)		(NODE_OFFSET(_n)     | ((_x)		   ))
#define TO_NODE_CAC(_n, _x)	(NODE_CAC_BASE(_n)   | ((_x) & TO_PHYS_MASK))
#define TO_NODE_UNCAC(_n, _x)	(NODE_UNCAC_BASE(_n) | ((_x) & TO_PHYS_MASK))
#define TO_NODE_MSPEC(_n, _x)	(NODE_MSPEC_BASE(_n) | ((_x) & TO_PHYS_MASK))
#define TO_NODE_HSPEC(_n, _x)	(NODE_HSPEC_BASE(_n) | ((_x) & TO_PHYS_MASK))


#define RAW_NODE_SWIN_BASE(nasid, widget)				\
	(NODE_IO_BASE(nasid) + (UINT64_CAST (widget) << SWIN_SIZE_BITS))

#define WIDGETID_GET(addr)	((unsigned char)((addr >> SWIN_SIZE_BITS) & 0xff))

/*
 * The following definitions pertain to the IO special address
 * space.  They define the location of the big and little windows
 * of any given node.
 */

#define SWIN_SIZE_BITS		24
#define SWIN_SIZE		(UINT64_CAST 1 << 24)
#define	SWIN_SIZEMASK		(SWIN_SIZE - 1)
#define	SWIN_WIDGET_MASK	0xF

/*
 * Convert smallwindow address to xtalk address.
 *
 * 'addr' can be physical or virtual address, but will be converted
 * to Xtalk address in the range 0 -> SWINZ_SIZEMASK
 */
#define	SWIN_WIDGETADDR(addr)	((addr) & SWIN_SIZEMASK)
#define	SWIN_WIDGETNUM(addr)	(((addr)  >> SWIN_SIZE_BITS) & SWIN_WIDGET_MASK)
/*
 * Verify if addr belongs to small window address on node with "nasid"
 *
 *
 * NOTE: "addr" is expected to be XKPHYS address, and NOT physical
 * address
 *
 *
 */
#define	NODE_SWIN_ADDR(nasid, addr)	\
		(((addr) >= NODE_SWIN_BASE(nasid, 0))  && \
		 ((addr) <  (NODE_SWIN_BASE(nasid, HUB_NUM_WIDGET) + SWIN_SIZE)\
		 ))

/*
 * The following define the major position-independent aliases used
 * in SN.
 *	UALIAS -- 256MB in size, reads in the UALIAS result in
 *			uncached references to the memory of the reader's node.
 *	CPU_UALIAS -- 128kb in size, the bottom part of UALIAS is flipped
 *			depending on which CPU does the access to provide
 *			all CPUs with unique uncached memory at low addresses.
 *	LBOOT  -- 256MB in size, reads in the LBOOT area result in
 *			uncached references to the local hub's boot prom and
 *			other directory-bus connected devices.
 *	IALIAS -- 8MB in size, reads in the IALIAS result in uncached
 *			references to the local hub's registers.
 */

#define UALIAS_BASE		HSPEC_BASE
#define UALIAS_SIZE		0x10000000	/* 256 Megabytes */
#define UALIAS_LIMIT		(UALIAS_BASE + UALIAS_SIZE)

/*
 * The bottom of ualias space is flipped depending on whether you're
 * processor 0 or 1 within a node.
 */
#ifdef CONFIG_SGI_IP27
#define UALIAS_FLIP_BASE	UALIAS_BASE
#define UALIAS_FLIP_SIZE	0x20000
#define UALIAS_FLIP_BIT		0x10000
#define UALIAS_FLIP_ADDR(_x)	(cputoslice(smp_processor_id()) ? \
				 (_x) ^ UALIAS_FLIP_BIT : (_x))

#define LBOOT_BASE		(HSPEC_BASE + 0x10000000)
#define LBOOT_SIZE		0x10000000
#define LBOOT_LIMIT		(LBOOT_BASE + LBOOT_SIZE)
#define LBOOT_STRIDE		0		/* IP27 has only one CPU PROM */

#endif

#define	HUB_REGISTER_WIDGET	1
#define IALIAS_BASE		NODE_SWIN_BASE(0, HUB_REGISTER_WIDGET)
#define IALIAS_SIZE		0x800000	/* 8 Megabytes */
#define IS_IALIAS(_a)		(((_a) >= IALIAS_BASE) &&		\
				 ((_a) < (IALIAS_BASE + IALIAS_SIZE)))

/*
 * Macro for referring to Hub's RBOOT space
 */

#ifdef CONFIG_SGI_IP27
#define RBOOT_SIZE		0x10000000	/* 256 Megabytes */
#define NODE_RBOOT_BASE(_n)	(NODE_HSPEC_BASE(_n) + 0x30000000)
#define NODE_RBOOT_LIMIT(_n)	(NODE_RBOOT_BASE(_n) + RBOOT_SIZE)

#endif

/*
 * Macros for referring the Hub's back door space
 *
 *   These macros correctly process addresses in any node's space.
 *   WARNING: They won't work in assembler.
 *
 *   BDDIR_ENTRY_LO returns the address of the low double-word of the dir
 *                  entry corresponding to a physical (Cac or Uncac) address.
 *   BDDIR_ENTRY_HI returns the address of the high double-word of the entry.
 *   BDPRT_ENTRY    returns the address of the double-word protection entry
 *                  corresponding to the page containing the physical address.
 *   BDPRT_ENTRY_S  Stores the value into the protection entry.
 *   BDPRT_ENTRY_L  Load the value from the protection entry.
 *   BDECC_ENTRY    returns the address of the ECC byte corresponding to a
 *                  double-word at a specified physical address.
 *   BDECC_ENTRY_H  returns the address of the two ECC bytes corresponding to a
 *                  quad-word at a specified physical address.
 */
#define NODE_BDOOR_BASE(_n)	(NODE_HSPEC_BASE(_n) + (NODE_ADDRSPACE_SIZE/2))

#define NODE_BDECC_BASE(_n)	(NODE_BDOOR_BASE(_n))
#define NODE_BDDIR_BASE(_n)	(NODE_BDOOR_BASE(_n) + (NODE_ADDRSPACE_SIZE/4))
#ifdef CONFIG_SGI_IP27
#define BDDIR_ENTRY_LO(_pa)	((HSPEC_BASE +				      \
				  NODE_ADDRSPACE_SIZE * 3 / 4 +		      \
				  0x200)				    | \
				 UINT64_CAST (_pa)	 & NASID_MASK	    | \
				 UINT64_CAST (_pa) >> 2 & BDDIR_UPPER_MASK  | \
				 UINT64_CAST (_pa) >> 3 & 0x1f << 4)

#define BDDIR_ENTRY_HI(_pa)	((HSPEC_BASE +				      \
				  NODE_ADDRSPACE_SIZE * 3 / 4 +		      \
				  0x208)				    | \
				 UINT64_CAST (_pa)	 & NASID_MASK	    | \
				 UINT64_CAST (_pa) >> 2 & BDDIR_UPPER_MASK  | \
				 UINT64_CAST (_pa) >> 3 & 0x1f << 4)

#define BDPRT_ENTRY(_pa, _rgn)	((HSPEC_BASE +				      \
				  NODE_ADDRSPACE_SIZE * 3 / 4)		    | \
				 UINT64_CAST (_pa)	 & NASID_MASK	    | \
				 UINT64_CAST (_pa) >> 2 & BDDIR_UPPER_MASK  | \
				 (_rgn) << 3)
#define BDPRT_ENTRY_ADDR(_pa,_rgn) (BDPRT_ENTRY((_pa),(_rgn)))
#define BDPRT_ENTRY_S(_pa,_rgn,_val) (*(__psunsigned_t *)BDPRT_ENTRY((_pa),(_rgn))=(_val))
#define BDPRT_ENTRY_L(_pa,_rgn)	(*(__psunsigned_t *)BDPRT_ENTRY((_pa),(_rgn)))

#define BDECC_ENTRY(_pa)	((HSPEC_BASE +				      \
				  NODE_ADDRSPACE_SIZE / 2)		    | \
				 UINT64_CAST (_pa)	 & NASID_MASK	    | \
				 UINT64_CAST (_pa) >> 2 & BDECC_UPPER_MASK  | \
				 UINT64_CAST (_pa) >> 3 & 3)

/*
 * Macro to convert a back door directory or protection address into the
 *   raw physical address of the associated cache line or protection page.
 */
#define BDADDR_IS_DIR(_ba)	((UINT64_CAST  (_ba) & 0x200) != 0)
#define BDADDR_IS_PRT(_ba)	((UINT64_CAST  (_ba) & 0x200) == 0)

#define BDDIR_TO_MEM(_ba)	(UINT64_CAST  (_ba) & NASID_MASK            | \
				 (UINT64_CAST (_ba) & BDDIR_UPPER_MASK)<<2  | \
				 (UINT64_CAST (_ba) & 0x1f << 4) << 3)

#define BDPRT_TO_MEM(_ba) 	(UINT64_CAST  (_ba) & NASID_MASK	    | \
				 (UINT64_CAST (_ba) & BDDIR_UPPER_MASK)<<2)

#define BDECC_TO_MEM(_ba)	(UINT64_CAST  (_ba) & NASID_MASK	    | \
				 (UINT64_CAST (_ba) & BDECC_UPPER_MASK)<<2  | \
				 (UINT64_CAST (_ba) & 3) << 3)
#endif /* CONFIG_SGI_IP27 */


/*
 * The following macros produce the correct base virtual address for
 * the hub registers.  The LOCAL_HUB_* macros produce the appropriate
 * address for the local registers.  The REMOTE_HUB_* macro produce
 * the address for the specified hub's registers.  The intent is
 * that the appropriate PI, MD, NI, or II register would be substituted
 * for _x.
 */

/*
 * WARNING:
 *	When certain Hub chip workaround are defined, it's not sufficient
 *	to dereference the *_HUB_ADDR() macros.  You should instead use
 *	HUB_L() and HUB_S() if you must deal with pointers to hub registers.
 *	Otherwise, the recommended approach is to use *_HUB_L() and *_HUB_S().
 *	They're always safe.
 */
#define LOCAL_HUB_ADDR(_x)	(HUBREG_CAST (IALIAS_BASE + (_x)))
#define REMOTE_HUB_ADDR(_n, _x)	(HUBREG_CAST (NODE_SWIN_BASE(_n, 1) +	\
					      0x800000 + (_x)))
#ifdef CONFIG_SGI_IP27
#define REMOTE_HUB_PI_ADDR(_n, _sn, _x)	(HUBREG_CAST (NODE_SWIN_BASE(_n, 1) +	\
					      0x800000 + (_x)))
#endif /* CONFIG_SGI_IP27 */

#ifndef __ASSEMBLY__

#define HUB_L(_a)			*(_a)
#define	HUB_S(_a, _d)			*(_a) = (_d)

#define LOCAL_HUB_L(_r)			HUB_L(LOCAL_HUB_ADDR(_r))
#define LOCAL_HUB_S(_r, _d)		HUB_S(LOCAL_HUB_ADDR(_r), (_d))
#define REMOTE_HUB_L(_n, _r)		HUB_L(REMOTE_HUB_ADDR((_n), (_r)))
#define REMOTE_HUB_S(_n, _r, _d)	HUB_S(REMOTE_HUB_ADDR((_n), (_r)), (_d))
#define REMOTE_HUB_PI_L(_n, _sn, _r)	HUB_L(REMOTE_HUB_PI_ADDR((_n), (_sn), (_r)))
#define REMOTE_HUB_PI_S(_n, _sn, _r, _d) HUB_S(REMOTE_HUB_PI_ADDR((_n), (_sn), (_r)), (_d))

#endif /* !__ASSEMBLY__ */

/*
 * The following macros are used to get to a hub/bridge register, given
 * the base of the register space.
 */
#define HUB_REG_PTR(_base, _off)	\
	(HUBREG_CAST ((__psunsigned_t)(_base) + (__psunsigned_t)(_off)))

#define HUB_REG_PTR_L(_base, _off)	\
	HUB_L(HUB_REG_PTR((_base), (_off)))

#define HUB_REG_PTR_S(_base, _off, _data)	\
	HUB_S(HUB_REG_PTR((_base), (_off)), (_data))

/*
 * Software structure locations -- permanently fixed
 *    See diagram in kldir.h
 */

#define PHYS_RAMBASE		0x0
#define K0_RAMBASE		PHYS_TO_K0(PHYS_RAMBASE)

#define EX_HANDLER_OFFSET(slice) ((slice) << 16)
#define EX_HANDLER_ADDR(nasid, slice)					\
	PHYS_TO_K0(NODE_OFFSET(nasid) | EX_HANDLER_OFFSET(slice))
#define EX_HANDLER_SIZE		0x0400

#define EX_FRAME_OFFSET(slice)	((slice) << 16 | 0x400)
#define EX_FRAME_ADDR(nasid, slice)					\
	PHYS_TO_K0(NODE_OFFSET(nasid) | EX_FRAME_OFFSET(slice))
#define EX_FRAME_SIZE		0x0c00

#define ARCS_SPB_OFFSET		0x1000
#define ARCS_SPB_ADDR(nasid)						\
	PHYS_TO_K0(NODE_OFFSET(nasid) | ARCS_SPB_OFFSET)
#define ARCS_SPB_SIZE		0x0400

#define KLDIR_OFFSET		0x2000
#define KLDIR_ADDR(nasid)						\
	TO_NODE_UNCAC((nasid), KLDIR_OFFSET)
#define KLDIR_SIZE		0x0400


/*
 * Software structure locations -- indirected through KLDIR
 *    See diagram in kldir.h
 *
 * Important:	All low memory structures must only be accessed
 *		uncached, except for the symmon stacks.
 */

#define KLI_LAUNCH		0		/* Dir. entries */
#define KLI_KLCONFIG		1
#define	KLI_NMI			2
#define KLI_GDA			3
#define KLI_FREEMEM		4
#define	KLI_SYMMON_STK		5
#define KLI_PI_ERROR		6
#define KLI_KERN_VARS		7
#define	KLI_KERN_XP		8
#define	KLI_KERN_PARTID		9

#ifndef __ASSEMBLY__

#define KLD_BASE(nasid)		((kldir_ent_t *) KLDIR_ADDR(nasid))
#define KLD_LAUNCH(nasid)	(KLD_BASE(nasid) + KLI_LAUNCH)
#define KLD_NMI(nasid)		(KLD_BASE(nasid) + KLI_NMI)
#define KLD_KLCONFIG(nasid)	(KLD_BASE(nasid) + KLI_KLCONFIG)
#define KLD_PI_ERROR(nasid)	(KLD_BASE(nasid) + KLI_PI_ERROR)
#define KLD_GDA(nasid)		(KLD_BASE(nasid) + KLI_GDA)
#define KLD_SYMMON_STK(nasid)	(KLD_BASE(nasid) + KLI_SYMMON_STK)
#define KLD_FREEMEM(nasid)	(KLD_BASE(nasid) + KLI_FREEMEM)
#define KLD_KERN_VARS(nasid)	(KLD_BASE(nasid) + KLI_KERN_VARS)
#define	KLD_KERN_XP(nasid)	(KLD_BASE(nasid) + KLI_KERN_XP)
#define	KLD_KERN_PARTID(nasid)	(KLD_BASE(nasid) + KLI_KERN_PARTID)

#define LAUNCH_OFFSET(nasid, slice)					\
	(KLD_LAUNCH(nasid)->offset +					\
	 KLD_LAUNCH(nasid)->stride * (slice))
#define LAUNCH_ADDR(nasid, slice)					\
	TO_NODE_UNCAC((nasid), LAUNCH_OFFSET(nasid, slice))
#define LAUNCH_SIZE(nasid)	KLD_LAUNCH(nasid)->size

#define NMI_OFFSET(nasid, slice)					\
	(KLD_NMI(nasid)->offset +					\
	 KLD_NMI(nasid)->stride * (slice))
#define NMI_ADDR(nasid, slice)						\
	TO_NODE_UNCAC((nasid), NMI_OFFSET(nasid, slice))
#define NMI_SIZE(nasid)	KLD_NMI(nasid)->size

#define KLCONFIG_OFFSET(nasid)	KLD_KLCONFIG(nasid)->offset
#define KLCONFIG_ADDR(nasid)						\
	TO_NODE_UNCAC((nasid), KLCONFIG_OFFSET(nasid))
#define KLCONFIG_SIZE(nasid)	KLD_KLCONFIG(nasid)->size

#define GDA_ADDR(nasid)		KLD_GDA(nasid)->pointer
#define GDA_SIZE(nasid)		KLD_GDA(nasid)->size

#define SYMMON_STK_OFFSET(nasid, slice)					\
	(KLD_SYMMON_STK(nasid)->offset +				\
	 KLD_SYMMON_STK(nasid)->stride * (slice))
#define SYMMON_STK_STRIDE(nasid)	KLD_SYMMON_STK(nasid)->stride

#define SYMMON_STK_ADDR(nasid, slice)					\
	TO_NODE_CAC((nasid), SYMMON_STK_OFFSET(nasid, slice))

#define SYMMON_STK_SIZE(nasid)	KLD_SYMMON_STK(nasid)->stride

#define SYMMON_STK_END(nasid)	(SYMMON_STK_ADDR(nasid, 0) + KLD_SYMMON_STK(nasid)->size)

/* loading symmon 4k below UNIX. the arcs loader needs the topaddr for a
 * relocatable program
 */
#define	UNIX_DEBUG_LOADADDR	0x300000
#define	SYMMON_LOADADDR(nasid)						\
	TO_NODE(nasid, PHYS_TO_K0(UNIX_DEBUG_LOADADDR - 0x1000))

#define FREEMEM_OFFSET(nasid)	KLD_FREEMEM(nasid)->offset
#define FREEMEM_ADDR(nasid)	SYMMON_STK_END(nasid)
/*
 * XXX
 * Fix this. FREEMEM_ADDR should be aware of if symmon is loaded.
 * Also, it should take into account what prom thinks to be a safe
 * address
	PHYS_TO_K0(NODE_OFFSET(nasid) + FREEMEM_OFFSET(nasid))
 */
#define FREEMEM_SIZE(nasid)	KLD_FREEMEM(nasid)->size

#define PI_ERROR_OFFSET(nasid)	KLD_PI_ERROR(nasid)->offset
#define PI_ERROR_ADDR(nasid)						\
	TO_NODE_UNCAC((nasid), PI_ERROR_OFFSET(nasid))
#define PI_ERROR_SIZE(nasid)	KLD_PI_ERROR(nasid)->size

#define NODE_OFFSET_TO_K0(_nasid, _off)					\
	PHYS_TO_K0((NODE_OFFSET(_nasid) + (_off)) | CAC_BASE)
#define NODE_OFFSET_TO_K1(_nasid, _off)					\
	TO_UNCAC((NODE_OFFSET(_nasid) + (_off)) | UNCAC_BASE)
#define K0_TO_NODE_OFFSET(_k0addr)					\
	((__psunsigned_t)(_k0addr) & NODE_ADDRSPACE_MASK)

#define KERN_VARS_ADDR(nasid)	KLD_KERN_VARS(nasid)->pointer
#define KERN_VARS_SIZE(nasid)	KLD_KERN_VARS(nasid)->size

#define	KERN_XP_ADDR(nasid)	KLD_KERN_XP(nasid)->pointer
#define	KERN_XP_SIZE(nasid)	KLD_KERN_XP(nasid)->size

#define GPDA_ADDR(nasid)	TO_NODE_CAC(nasid, GPDA_OFFSET)

#endif /* !__ASSEMBLY__ */


#endif /* _ASM_SN_ADDRS_H */
