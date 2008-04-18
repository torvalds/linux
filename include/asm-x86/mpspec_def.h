#ifndef __ASM_MPSPEC_DEF_H
#define __ASM_MPSPEC_DEF_H

/*
 * Structure definitions for SMP machines following the
 * Intel Multiprocessing Specification 1.1 and 1.4.
 */

/*
 * This tag identifies where the SMP configuration
 * information is.
 */

#define SMP_MAGIC_IDENT	(('_'<<24) | ('P'<<16) | ('M'<<8) | '_')

#ifdef CONFIG_X86_32
# define MAX_MPC_ENTRY 1024
# define MAX_APICS      256
#else
/*
 * A maximum of 255 APICs with the current APIC ID architecture.
 */
# define MAX_APICS 255
#endif

struct intel_mp_floating {
	char mpf_signature[4];		/* "_MP_"			*/
	unsigned int mpf_physptr;	/* Configuration table address	*/
	unsigned char mpf_length;	/* Our length (paragraphs)	*/
	unsigned char mpf_specification;/* Specification version	*/
	unsigned char mpf_checksum;	/* Checksum (makes sum 0)	*/
	unsigned char mpf_feature1;	/* Standard or configuration ?	*/
	unsigned char mpf_feature2;	/* Bit7 set for IMCR|PIC	*/
	unsigned char mpf_feature3;	/* Unused (0)			*/
	unsigned char mpf_feature4;	/* Unused (0)			*/
	unsigned char mpf_feature5;	/* Unused (0)			*/
};

#define MPC_SIGNATURE "PCMP"

struct mp_config_table {
	char mpc_signature[4];
	unsigned short mpc_length;	/* Size of table */
	char mpc_spec;			/* 0x01 */
	char mpc_checksum;
	char mpc_oem[8];
	char mpc_productid[12];
	unsigned int mpc_oemptr;	/* 0 if not present */
	unsigned short mpc_oemsize;	/* 0 if not present */
	unsigned short mpc_oemcount;
	unsigned int mpc_lapic;	/* APIC address */
	unsigned int reserved;
};

/* Followed by entries */

#define	MP_PROCESSOR		0
#define	MP_BUS			1
#define	MP_IOAPIC		2
#define	MP_INTSRC		3
#define	MP_LINTSRC		4
/* Used by IBM NUMA-Q to describe node locality */
#define	MP_TRANSLATION		192

#define CPU_ENABLED		1	/* Processor is available */
#define CPU_BOOTPROCESSOR	2	/* Processor is the BP */

#define CPU_STEPPING_MASK	0x000F
#define CPU_MODEL_MASK		0x00F0
#define CPU_FAMILY_MASK		0x0F00

struct mpc_config_processor {
	unsigned char mpc_type;
	unsigned char mpc_apicid;	/* Local APIC number */
	unsigned char mpc_apicver;	/* Its versions */
	unsigned char mpc_cpuflag;
	unsigned int mpc_cpufeature;
	unsigned int mpc_featureflag;	/* CPUID feature value */
	unsigned int mpc_reserved[2];
};

struct mpc_config_bus {
	unsigned char mpc_type;
	unsigned char mpc_busid;
	unsigned char mpc_bustype[6];
};

/* List of Bus Type string values, Intel MP Spec. */
#define BUSTYPE_EISA	"EISA"
#define BUSTYPE_ISA	"ISA"
#define BUSTYPE_INTERN	"INTERN"	/* Internal BUS */
#define BUSTYPE_MCA	"MCA"
#define BUSTYPE_VL	"VL"		/* Local bus */
#define BUSTYPE_PCI	"PCI"
#define BUSTYPE_PCMCIA	"PCMCIA"
#define BUSTYPE_CBUS	"CBUS"
#define BUSTYPE_CBUSII	"CBUSII"
#define BUSTYPE_FUTURE	"FUTURE"
#define BUSTYPE_MBI	"MBI"
#define BUSTYPE_MBII	"MBII"
#define BUSTYPE_MPI	"MPI"
#define BUSTYPE_MPSA	"MPSA"
#define BUSTYPE_NUBUS	"NUBUS"
#define BUSTYPE_TC	"TC"
#define BUSTYPE_VME	"VME"
#define BUSTYPE_XPRESS	"XPRESS"

#define MPC_APIC_USABLE		0x01

struct mpc_config_ioapic {
	unsigned char mpc_type;
	unsigned char mpc_apicid;
	unsigned char mpc_apicver;
	unsigned char mpc_flags;
	unsigned int mpc_apicaddr;
};

struct mpc_config_intsrc {
	unsigned char mpc_type;
	unsigned char mpc_irqtype;
	unsigned short mpc_irqflag;
	unsigned char mpc_srcbus;
	unsigned char mpc_srcbusirq;
	unsigned char mpc_dstapic;
	unsigned char mpc_dstirq;
};

enum mp_irq_source_types {
	mp_INT = 0,
	mp_NMI = 1,
	mp_SMI = 2,
	mp_ExtINT = 3
};

#define MP_IRQDIR_DEFAULT	0
#define MP_IRQDIR_HIGH		1
#define MP_IRQDIR_LOW		3

#define MP_APIC_ALL	0xFF

struct mpc_config_lintsrc {
	unsigned char mpc_type;
	unsigned char mpc_irqtype;
	unsigned short mpc_irqflag;
	unsigned char mpc_srcbusid;
	unsigned char mpc_srcbusirq;
	unsigned char mpc_destapic;
	unsigned char mpc_destapiclint;
};

#define MPC_OEM_SIGNATURE "_OEM"

struct mp_config_oemtable {
	char oem_signature[4];
	unsigned short oem_length;	/* Size of table */
	char  oem_rev;			/* 0x01 */
	char  oem_checksum;
	char  mpc_oem[8];
};

/*
 *	Default configurations
 *
 *	1	2 CPU ISA 82489DX
 *	2	2 CPU EISA 82489DX neither IRQ 0 timer nor IRQ 13 DMA chaining
 *	3	2 CPU EISA 82489DX
 *	4	2 CPU MCA 82489DX
 *	5	2 CPU ISA+PCI
 *	6	2 CPU EISA+PCI
 *	7	2 CPU MCA+PCI
 */

enum mp_bustype {
	MP_BUS_ISA = 1,
	MP_BUS_EISA,
	MP_BUS_PCI,
	MP_BUS_MCA,
};
#endif
