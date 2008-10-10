#ifndef ASM_X86__MACH_SUMMIT__MACH_MPPARSE_H
#define ASM_X86__MACH_SUMMIT__MACH_MPPARSE_H

#include <mach_apic.h>
#include <asm/tsc.h>

extern int use_cyclone;

#ifdef CONFIG_X86_SUMMIT_NUMA
extern void setup_summit(void);
#else
#define setup_summit()	{}
#endif

static inline int mps_oem_check(struct mp_config_table *mpc, char *oem, 
		char *productid)
{
	if (!strncmp(oem, "IBM ENSW", 8) && 
			(!strncmp(productid, "VIGIL SMP", 9) 
			 || !strncmp(productid, "EXA", 3)
			 || !strncmp(productid, "RUTHLESS SMP", 12))){
		mark_tsc_unstable("Summit based system");
		use_cyclone = 1; /*enable cyclone-timer*/
		setup_summit();
		return 1;
	}
	return 0;
}

/* Hook from generic ACPI tables.c */
static inline int acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strncmp(oem_id, "IBM", 3) &&
	    (!strncmp(oem_table_id, "SERVIGIL", 8)
	     || !strncmp(oem_table_id, "EXA", 3))){
		mark_tsc_unstable("Summit based system");
		use_cyclone = 1; /*enable cyclone-timer*/
		setup_summit();
		return 1;
	}
	return 0;
}

struct rio_table_hdr {
	unsigned char version;      /* Version number of this data structure           */
	                            /* Version 3 adds chassis_num & WP_index           */
	unsigned char num_scal_dev; /* # of Scalability devices (Twisters for Vigil)   */
	unsigned char num_rio_dev;  /* # of RIO I/O devices (Cyclones and Winnipegs)   */
} __attribute__((packed));

struct scal_detail {
	unsigned char node_id;      /* Scalability Node ID                             */
	unsigned long CBAR;         /* Address of 1MB register space                   */
	unsigned char port0node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port0port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port1node;    /* Node ID port connected to: 0xFF = None          */
	unsigned char port1port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port2node;    /* Node ID port connected to: 0xFF = None          */
	unsigned char port2port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char chassis_num;  /* 1 based Chassis number (1 = boot node)          */
} __attribute__((packed));

struct rio_detail {
	unsigned char node_id;      /* RIO Node ID                                     */
	unsigned long BBAR;         /* Address of 1MB register space                   */
	unsigned char type;         /* Type of device                                  */
	unsigned char owner_id;     /* For WPEG: Node ID of Cyclone that owns this WPEG*/
	                            /* For CYC:  Node ID of Twister that owns this CYC */
	unsigned char port0node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port0port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char port1node;    /* Node ID port connected to: 0xFF=None            */
	unsigned char port1port;    /* Port num port connected to: 0,1,2, or 0xFF=None */
	unsigned char first_slot;   /* For WPEG: Lowest slot number below this WPEG    */
	                            /* For CYC:  0                                     */
	unsigned char status;       /* For WPEG: Bit 0 = 1 : the XAPIC is used         */
	                            /*                 = 0 : the XAPIC is not used, ie:*/
	                            /*                     ints fwded to another XAPIC */
	                            /*           Bits1:7 Reserved                      */
	                            /* For CYC:  Bits0:7 Reserved                      */
	unsigned char WP_index;     /* For WPEG: WPEG instance index - lower ones have */
	                            /*           lower slot numbers/PCI bus numbers    */
	                            /* For CYC:  No meaning                            */
	unsigned char chassis_num;  /* 1 based Chassis number                          */
	                            /* For LookOut WPEGs this field indicates the      */
	                            /* Expansion Chassis #, enumerated from Boot       */
	                            /* Node WPEG external port, then Boot Node CYC     */
	                            /* external port, then Next Vigil chassis WPEG     */
	                            /* external port, etc.                             */
	                            /* Shared Lookouts have only 1 chassis number (the */
	                            /* first one assigned)                             */
} __attribute__((packed));


typedef enum {
	CompatTwister = 0,  /* Compatibility Twister               */
	AltTwister    = 1,  /* Alternate Twister of internal 8-way */
	CompatCyclone = 2,  /* Compatibility Cyclone               */
	AltCyclone    = 3,  /* Alternate Cyclone of internal 8-way */
	CompatWPEG    = 4,  /* Compatibility WPEG                  */
	AltWPEG       = 5,  /* Second Planar WPEG                  */
	LookOutAWPEG  = 6,  /* LookOut WPEG                        */
	LookOutBWPEG  = 7,  /* LookOut WPEG                        */
} node_type;

static inline int is_WPEG(struct rio_detail *rio){
	return (rio->type == CompatWPEG || rio->type == AltWPEG ||
		rio->type == LookOutAWPEG || rio->type == LookOutBWPEG);
}

#endif /* ASM_X86__MACH_SUMMIT__MACH_MPPARSE_H */
