/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 2003 by Ralf Baechle
 * Copyright (C) 1995, 1996 Andreas Busse
 * Copyright (C) 1995, 1996 Stoned Elipot
 * Copyright (C) 1995, 1996 Paul M. Antoine.
 */
#ifndef _ASM_BOOTINFO_H
#define _ASM_BOOTINFO_H

#include <linux/types.h>
#include <asm/setup.h>

/*
 * The MACH_ IDs are sort of equivalent to PCI product IDs.  As such the
 * numbers do not necessarily reflect technical relations or similarities
 * between systems.
 */

/*
 * Valid machtype values for group unknown
 */
#define  MACH_UNKNOWN		0	/* whatever...			*/

/*
 * Valid machtype values for group JAZZ
 */
#define  MACH_ACER_PICA_61	0	/* Acer PICA-61 (PICA1)		*/
#define  MACH_MIPS_MAGNUM_4000	1	/* Mips Magnum 4000 "RC4030"	*/
#define  MACH_OLIVETTI_M700	2	/* Olivetti M700-10 (-15 ??)    */

/*
 * Valid machtype for group DEC
 */
#define  MACH_DSUNKNOWN		0
#define  MACH_DS23100		1	/* DECstation 2100 or 3100	*/
#define  MACH_DS5100		2	/* DECsystem 5100		*/
#define  MACH_DS5000_200	3	/* DECstation 5000/200		*/
#define  MACH_DS5000_1XX	4	/* DECstation 5000/120, 125, 133, 150 */
#define  MACH_DS5000_XX		5	/* DECstation 5000/20, 25, 33, 50 */
#define  MACH_DS5000_2X0	6	/* DECstation 5000/240, 260	*/
#define  MACH_DS5400		7	/* DECsystem 5400		*/
#define  MACH_DS5500		8	/* DECsystem 5500		*/
#define  MACH_DS5800		9	/* DECsystem 5800		*/
#define  MACH_DS5900		10	/* DECsystem 5900		*/

/*
 * Valid machtype for group SNI_RM
 */
#define  MACH_SNI_RM200_PCI	0	/* RM200/RM300/RM400 PCI series */

/*
 * Valid machtype for group SGI
 */
#define  MACH_SGI_IP22		0	/* Indy, Indigo2, Challenge S	*/
#define  MACH_SGI_IP27		1	/* Origin 200, Origin 2000, Onyx 2 */
#define  MACH_SGI_IP28		2	/* Indigo2 Impact		*/
#define  MACH_SGI_IP32		3	/* O2				*/
#define  MACH_SGI_IP30		4	/* Octane, Octane2              */

/*
 * Valid machtypes for group Toshiba
 */
#define  MACH_PALLAS		0
#define  MACH_TOPAS		1
#define  MACH_JMR		2
#define  MACH_TOSHIBA_JMR3927	3	/* JMR-TX3927 CPU/IO board */
#define  MACH_TOSHIBA_RBTX4927	4
#define  MACH_TOSHIBA_RBTX4937	5
#define  MACH_TOSHIBA_RBTX4938	6

/*
 * Valid machtype for group LASAT
 */
#define  MACH_LASAT_100		0	/* Masquerade II/SP100/SP50/SP25 */
#define  MACH_LASAT_200		1	/* Masquerade PRO/SP200 */

/*
 * Valid machtype for group NEC EMMA2RH
 */
#define  MACH_NEC_MARKEINS	0	/* NEC EMMA2RH Mark-eins	*/

/*
 * Valid machtype for group PMC-MSP
 */
#define MACH_MSP4200_EVAL       0	/* PMC-Sierra MSP4200 Evaluation */
#define MACH_MSP4200_GW         1	/* PMC-Sierra MSP4200 Gateway demo */
#define MACH_MSP4200_FPGA       2	/* PMC-Sierra MSP4200 Emulation */
#define MACH_MSP7120_EVAL       3	/* PMC-Sierra MSP7120 Evaluation */
#define MACH_MSP7120_GW         4	/* PMC-Sierra MSP7120 Residential GW */
#define MACH_MSP7120_FPGA       5	/* PMC-Sierra MSP7120 Emulation */
#define MACH_MSP_OTHER        255	/* PMC-Sierra unknown board type */

#define CL_SIZE			COMMAND_LINE_SIZE

extern char *system_type;
const char *get_system_type(void);

extern unsigned long mips_machtype;

#define BOOT_MEM_MAP_MAX	32
#define BOOT_MEM_RAM		1
#define BOOT_MEM_ROM_DATA	2
#define BOOT_MEM_RESERVED	3

/*
 * A memory map that's built upon what was determined
 * or specified on the command line.
 */
struct boot_mem_map {
	int nr_map;
	struct boot_mem_map_entry {
		phys_t addr;	/* start of memory segment */
		phys_t size;	/* size of memory segment */
		long type;		/* type of memory segment */
	} map[BOOT_MEM_MAP_MAX];
};

extern struct boot_mem_map boot_mem_map;

extern void add_memory_region(phys_t start, phys_t size, long type);

extern void prom_init(void);
extern void prom_free_prom_memory(void);

extern void free_init_pages(const char *what,
			    unsigned long begin, unsigned long end);

/*
 * Initial kernel command line, usually setup by prom_init()
 */
extern char arcs_cmdline[CL_SIZE];

/*
 * Registers a0, a1, a3 and a4 as passed to the kernel entry by firmware
 */
extern unsigned long fw_arg0, fw_arg1, fw_arg2, fw_arg3;

/*
 * Platform memory detection hook called by setup_arch
 */
extern void plat_mem_setup(void);

#endif /* _ASM_BOOTINFO_H */
