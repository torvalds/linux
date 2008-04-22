/*
 * Derived from include/asm-x86/mach-summit/mach_mpparse.h
 *          and include/asm-x86/mach-default/bios_ebda.h
 *
 * Author: Laurent Vivier <Laurent.Vivier@bull.net>
 */

#ifndef __ASM_RIO_H
#define __ASM_RIO_H

#define RIO_TABLE_VERSION	3

struct rio_table_hdr {
	u8 version;		/* Version number of this data structure  */
	u8 num_scal_dev;	/* # of Scalability devices               */
	u8 num_rio_dev;		/* # of RIO I/O devices                   */
} __attribute__((packed));

struct scal_detail {
	u8 node_id;		/* Scalability Node ID                    */
	u32 CBAR;		/* Address of 1MB register space          */
	u8 port0node;		/* Node ID port connected to: 0xFF=None   */
	u8 port0port;		/* Port num port connected to: 0,1,2, or  */
				/* 0xFF=None                              */
	u8 port1node;		/* Node ID port connected to: 0xFF = None */
	u8 port1port;		/* Port num port connected to: 0,1,2, or  */
				/* 0xFF=None                              */
	u8 port2node;		/* Node ID port connected to: 0xFF = None */
	u8 port2port;		/* Port num port connected to: 0,1,2, or  */
				/* 0xFF=None                              */
	u8 chassis_num;		/* 1 based Chassis number (1 = boot node) */
} __attribute__((packed));

struct rio_detail {
	u8 node_id;		/* RIO Node ID                            */
	u32 BBAR;		/* Address of 1MB register space          */
	u8 type;		/* Type of device                         */
	u8 owner_id;		/* Node ID of Hurricane that owns this    */
				/* node                                   */
	u8 port0node;		/* Node ID port connected to: 0xFF=None   */
	u8 port0port;		/* Port num port connected to: 0,1,2, or  */
				/* 0xFF=None                              */
	u8 port1node;		/* Node ID port connected to: 0xFF=None   */
	u8 port1port;		/* Port num port connected to: 0,1,2, or  */
				/* 0xFF=None                              */
	u8 first_slot;		/* Lowest slot number below this Calgary  */
	u8 status;		/* Bit 0 = 1 : the XAPIC is used          */
				/*       = 0 : the XAPIC is not used, ie: */
				/*            ints fwded to another XAPIC */
				/*           Bits1:7 Reserved             */
	u8 WP_index;		/* instance index - lower ones have       */
				/*     lower slot numbers/PCI bus numbers */
	u8 chassis_num;		/* 1 based Chassis number                 */
} __attribute__((packed));

enum {
	HURR_SCALABILTY	= 0,	/* Hurricane Scalability info */
	HURR_RIOIB	= 2,	/* Hurricane RIOIB info       */
	COMPAT_CALGARY	= 4,	/* Compatibility Calgary      */
	ALT_CALGARY	= 5,	/* Second Planar Calgary      */
};

/*
 * there is a real-mode segmented pointer pointing to the
 * 4K EBDA area at 0x40E.
 */
static inline unsigned long get_bios_ebda(void)
{
	unsigned long address = *(unsigned short *)phys_to_virt(0x40EUL);
	address <<= 4;
	return address;
}

#endif /* __ASM_RIO_H */
