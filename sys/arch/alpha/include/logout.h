/* $OpenBSD: logout.h,v 1.4 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: logout.h,v 1.6 2005/12/11 12:16:16 christos Exp $ */

/*
 * Copyright (c) 2009 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 1998 by Matthew Jacob
 * NASA AMES Research Center.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Various Alpha OSF/1 PAL Logout error definitions.
 */

/*
 * Information gathered from: DEC documentation
 */

/*
 * Avanti (AlphaStation 200 and 400) Specific PALcode Exception Logout
 * Area Definitions
 */

/*
 * Avanti Specific common logout frame header.
 * *Almost* identical to the generic logout header listed in alpha_cpu.h.
 */

typedef struct {
	unsigned int	la_frame_size;		/* frame size */
	unsigned int	la_flags;		/* flags; see alpha_cpu.h */
	unsigned int	la_cpu_offset;		/* offset to CPU area */
	unsigned int	la_system_offset;	/* offset to system area */
	unsigned int	mcheck_code;		/* machine check code */
	unsigned int	:32;
} mc_hdr_avanti;

/* Machine Check Codes */

/* SCB 660 Fatal Machine Checks */
#define AVANTI_RETRY_TIMEOUT		0x201L
#define	AVANTI_DMA_DATA_PARITY		0x202L
#define AVANTI_IO_PARITY		0x203L
#define AVANTI_TARGET_ABORT		0x204L
#define AVANTI_NO_DEVICE		0x205L
#define AVANTI_CORRRECTABLE_MEMORY	0x206L	/* Should never occur */
#define AVANTI_UNCORRECTABLE_PCI_MEMORY	0x207L
#define AVANTI_INVALID_PT_LOOKUP	0x208L
#define AVANTI_MEMORY			0x209L
#define AVANTI_BCACHE_TAG_ADDR_PARITY	0x20AL
#define AVANTI_BCACHE_TAG_CTRL_PARITY	0x20BL
#define AVANTI_NONEXISTENT_MEMORY	0x20CL
#define AVANTI_IO_BUS			0x20DL
#define AVANTI_BCACHE_TAG_PARITY	 0x80L
#define AVANTI_BCACHE_TAG_CTRL_PARITY2   0x82L

/* SCB 670 Processor Fatal Machine Checks */
#define AVANTI_HARD_ERROR		 0x84L
#define AVANTI_CORRECTABLE_ECC		 0x86L
#define AVANTI_NONCORRECTABLE_ECC	 0x88L
#define AVANTI_UNKNOWN_ERROR		 0x8AL
#define AVANTI_SOFT_ERROR		 0x8CL
#define AVANTI_BUGCHECK		 	 0x8EL
#define AVANTI_OS_BUGCHECK		 0x90L
#define AVANTI_DCACHE_FILL_PARITY 	 0x92L
#define AVANTI_ICACHE_FILL_PARITY	 0x94L

typedef struct {
	/* Registers from the CPU */
	u_int64_t	paltemp[32];	/* PAL TEMP REGS.		*/
	u_int64_t	exc_addr;	/* Address of excepting ins.	*/
	u_int64_t	exc_sum;	/* Summary of arithmetic traps.	*/
	u_int64_t	exc_mask;	/* Exception mask.		*/
	u_int64_t	iccsr;
	u_int64_t	pal_base;	/* Base address for PALcode.	*/
	u_int64_t	hier;
	u_int64_t	hirr;
	u_int64_t	mm_csr;
	u_int64_t	dc_stat;
	u_int64_t	dc_addr;
	u_int64_t	abox_ctl;
	u_int64_t	biu_stat;	/* Bus Interface Unit Status.	*/
	u_int64_t	biu_addr;
	u_int64_t	biu_ctl;
	u_int64_t	fill_syndrome;
	u_int64_t	fill_addr;
	u_int64_t	va;
	u_int64_t	bc_tag;

	/* Registers from the cache and memory controller (21071-CA) */
	u_int64_t	coma_gcr;	/* Error and Diag. Status.	*/
	u_int64_t	coma_edsr;
	u_int64_t	coma_ter;
	u_int64_t	coma_elar;
	u_int64_t	coma_ehar;
	u_int64_t	coma_ldlr;
	u_int64_t	coma_ldhr;
	u_int64_t	coma_base0;
	u_int64_t	coma_base1;
	u_int64_t	coma_base2;
	u_int64_t	coma_cnfg0;
	u_int64_t	coma_cnfg1;
	u_int64_t	coma_cnfg2;

	/* Registers from the PCI bridge (21071-DA) */
	u_int64_t	epic_dcsr;	 /* Diag. Control and Status.	*/
	u_int64_t	epic_pear;
	u_int64_t	epic_sear;
	u_int64_t	epic_tbr1;
	u_int64_t	epic_tbr2;
	u_int64_t	epic_pbr1;
	u_int64_t	epic_pbr2;
	u_int64_t	epic_pmr1;
	u_int64_t	epic_pmr2;
	u_int64_t	epic_harx1;
	u_int64_t	epic_harx2;
	u_int64_t	epic_pmlt;
	u_int64_t	epic_tag0;
	u_int64_t	epic_tag1;
	u_int64_t	epic_tag2;
	u_int64_t	epic_tag3;
	u_int64_t	epic_tag4;
	u_int64_t	epic_tag5;
	u_int64_t	epic_tag6;
	u_int64_t	epic_tag7;
	u_int64_t	epic_data0;
	u_int64_t	epic_data1;
	u_int64_t	epic_data2;
	u_int64_t	epic_data3;
	u_int64_t	epic_data4;
	u_int64_t	epic_data5;
	u_int64_t	epic_data6;
	u_int64_t	epic_data7;
} mc_uc_avanti;

/*
 * Information gathered from: OSF/1 header files.
 */


/*
 * EV5 Specific OSF/1 Pal Code Exception Logout Area Definitions
 * (inspired from OSF/1 Header files).
 */

/*
 * EV5 Specific common logout frame header.
 * *Almost* identical to the generic logout header listed in alpha_cpu.h.
 */

typedef struct {
	unsigned int	la_frame_size;		/* frame size */
	unsigned int	la_flags;		/* flags; see alpha_cpu.h */
	unsigned int	la_cpu_offset;		/* offset to CPU area */
	unsigned int	la_system_offset;	/* offset to system area */
	unsigned long	mcheck_code;		/* machine check code */
} mc_hdr_ev5;

/* Machine Check Codes */
#define	EV5_CORRECTED		0x86L
#define	SYSTEM_CORRECTED	0x201L

/*
 * EV5 Specific Machine Check logout frame for uncorrectable errors.
 * This is used to log uncorrectable errors such as double bit ECC errors.
 *
 * This typically resides in the CPU offset area of the logout frame.
 */

typedef struct {
	u_int64_t	shadow[8];	/* Shadow reg. 8-14, 25		*/
	u_int64_t	paltemp[24];	/* PAL TEMP REGS.		*/
	u_int64_t	exc_addr;	/* Address of excepting ins.	*/
	u_int64_t	exc_sum;	/* Summary of arithmetic traps.	*/
	u_int64_t	exc_mask;	/* Exception mask.		*/
	u_int64_t	pal_base;	/* Base address for PALcode.	*/
	u_int64_t	isr;		/* Interrupt Status Reg.	*/
	u_int64_t	icsr;		/* CURRENT SETUP OF EV5 IBOX	*/
	u_int64_t	ic_perr_stat;	/*
					 * I-CACHE Reg:
					 *	<13> IBOX Timeout
					 *	<12> TAG parity
					 *	<11> Data parity
					 */
	u_int64_t	dc_perr_stat;	/* D-CACHE error Reg:
					 * Bits set to 1:
					 *  <2> Data error in bank 0
					 *  <3> Data error in bank 1
					 *  <4> Tag error in bank 0
					 *  <5> Tag error in bank 1
					 */
	u_int64_t	va;		/* Effective VA of fault or miss. */
	u_int64_t	mm_stat;	/*
					 * Holds the reason for D-stream
					 * fault or D-cache parity errors
					 */
	u_int64_t	sc_addr;	/*
					 * Address that was being accessed
					 * when EV5 detected Secondary cache
					 * failure.
					 */
	u_int64_t	sc_stat;	/*
					 * Helps determine if the error was
					 * TAG/Data parity(Secondary Cache)
					 */
	u_int64_t	bc_tag_addr;	/* Contents of EV5 BC_TAG_ADDR	  */
	u_int64_t	ei_addr;	/*
					 * Physical address of any transfer
					 * that is logged in the EV5 EI_STAT
					 */
	u_int64_t	fill_syndrome;	/* For correcting ECC errors.	  */
	u_int64_t	ei_stat;	/*
					 * Helps identify reason of any
					 * processor uncorrectable error
					 * at its external interface.
					 */
	u_int64_t	ld_lock;	/* Contents of EV5 LD_LOCK register*/
} mc_uc_ev5;
#define	EV5_IC_PERR_IBOXTMO	0x2000

/*
 * EV5 Specific Machine Check logout frame for correctable errors.
 *
 * This is used to log correctable errors such as Single bit ECC errors.
 */
typedef struct {
	u_int64_t	ei_addr;	/*
					 * Physical address of any transfer
					 * that is logged in the EV5 EI_STAT
					 */
	u_int64_t	fill_syndrome;	/* For correcting ECC errors.	  */
	u_int64_t	ei_stat;	/*
					 * Helps identify reason of any
					 * processor uncorrectable error
					 * at its external interface.
					 */
	u_int64_t	isr;		/* Interrupt Status Reg. 	  */
} mc_cc_ev5;

/*
 * Information gathered from: AlphaServer ES40 Service Guide
 */

/*
 * EV6 Specific OSF/1 Pal Code Exception Logout Area Definitions
 */

/*
 * EV6 Specific common logout frame header.
 * *Almost* identical to the generic logout header listed in alpha_cpu.h.
 */

typedef struct {
	unsigned int	la_frame_size;		/* frame size */
	unsigned int	la_flags;		/* flags; see alpha_cpu.h */
	unsigned int	la_cpu_offset;		/* offset to CPU area */
	unsigned int	la_system_offset;	/* offset to system area */
	unsigned int	mcheck_code;		/* machine check code */
	unsigned int	mcheck_rev;		/* frame revision */
#define	MC_EV6_FRAME_REVISION		1
} mc_hdr_ev6;

/*
 * EV6 Specific Machine Check processor area.
 */

typedef struct {
	uint64_t	i_stat;
	uint64_t	dc_stat;
	uint64_t	c_addr;
	uint64_t	c_syndrome_0;
	uint64_t	c_syndrome_1;
	uint64_t	c_stat;
	uint64_t	c_sts;
	uint64_t	mm_stat;
	/* the following fields only exist for uncorrectable errors */
	uint64_t	exc_addr;
	uint64_t	ier_cm;
	uint64_t	isum;
	uint64_t	reserved0;
	uint64_t	pal_base;
	uint64_t	i_ctl;
	uint64_t	pctx;
	uint64_t	reserved1;
	uint64_t	reserved2;
} mc_cpu_ev6;

/* C_STAT bits */
#define	EV6_C_STAT_MASK					0x1f
#define	EV6_C_STAT_NO_ERROR				0x00
#define	EV6_C_STAT_SNGL_BC_TAG_PERR			0x01
#define	EV6_C_STAT_SNGL_DC_DUPLICATE_TAG_PERR		0x02
#define	EV6_C_STAT_SNGL_DSTREAM_MEM_ECC_ERR		0x03
#define	EV6_C_STAT_SNGL_DSTREAM_BC_ECC_ERR		0x04
#define	EV6_C_STAT_SNGL_DSTREAM_DC_ECC_ERR		0x05
#define	EV6_C_STAT_SNGL_BC_PROBE_HIT_ERR		0x06
#define	EV6_C_STAT_SNGL_BC_PROBE_HIT_ERR2		0x07
#define	EV6_C_STAT_SNGL_ISTREAM_MEM_ECC_ERR		0x0b
#define	EV6_C_STAT_SNGL_ISTREAM_BC_ECC_ERR		0x0c
#define	EV6_C_STAT_DBL_DSTREAM_MEM_ECC_ERR		0x13
#define	EV6_C_STAT_DBL_DSTREAM_BC_ECC_ERR		0x14
#define	EV6_C_STAT_DBL_ISTREAM_MEM_ECC_ERR		0x1b
#define	EV6_C_STAT_DBL_ISTREAM_BC_ECC_ERR		0x1c

/* C_STS bits */
#define	EV6_C_STS_MASK					0x0f
#define	EV6_C_STS_PARITY				0x08
#define	EV6_C_STS_VALID					0x04
#define	EV6_C_STS_DIRTY					0x02
#define	EV6_C_STS_SHARED				0x01

/* DC_STAT */
#define	EV6_DC_STAT_MASK				0x1f
#define	EV6_DC_STAT_PIPELINE_0_ERROR			0x01
#define	EV6_DC_STAT_PIPELINE_1_ERROR			0x02
#define	EV6_DC_STAT_STORE_DATA_ECC_ERROR		0x04
#define	EV6_DC_STAT_LOAD_DATA_ECC_ERROR			0x08
#define	EV6_DC_STAT_STORE_DATA_ECC_ERROR_REPEATED	0x10

/* MM_STAT */
#define	EV6_MM_STAT_MASK				0x03ff
#define	EV6_MM_STAT_WRITE				0x0001
#define	EV6_MM_STAT_ACCESS_VIOLATION			0x0002
#define	EV6_MM_STAT_FOR_SET				0x0004
#define	EV6_MM_STAT_FOW_SET				0x0008
#define	EV6_MM_STAT_OPCODE_MASK				0x02f0
#define	EV6_MM_STAT_DCACHE_CORRECTABLE_ERROR		0x0300

/*
 * EV6 Specific Machine Check system area.
 */

typedef struct {
	uint64_t	flags;
	uint64_t	c_dir;
	uint64_t	c_misc;
	uint64_t	p0_perror;
	uint64_t	p1_perror;
} mc_sys_ev6;

/*
 * EV6 Environmental Error logout frame.
 */

typedef struct {
	uint64_t	flags;
	uint64_t	c_dir;
	uint64_t	smir;
	uint64_t	cpuir;
	uint64_t	psir;
	uint64_t	lm78_isr;
	uint64_t	doors;
	uint64_t	temp_warning;
	uint64_t	fan_control;
	uint64_t	fatal_power_down;
	uint64_t	reserved;
} mc_env_ev6;

/* SMIR */
#define	EV6_ENV_SMIR_RESET				0x80
#define	EV6_ENV_SMIR_PCI1_RESET				0x40
#define	EV6_ENV_SMIR_PCI0_RESET				0x20
#define	EV6_ENV_SMIR_OVERTEMP				0x10
#define	EV6_ENV_SMIR_DC_FAILURE				0x04
#define	EV6_ENV_SMIR_RMC_HALT				0x02
#define	EV6_ENV_SMIR_PSU_FAILURE			0x01

/* CPUIR */
#define	EV6_ENV_CPUIR_CPU_FAIL(cpuno)			((cpuno) << 4)
#define	EV6_ENV_CPUIR_CPU_ENABLE(cpuno)			((cpuno) << 0)

/* PSIR */
#define	EV6_ENV_PSIR_PSU_FAIL(psuno)			((psuno) << 4)
#define	EV6_ENV_PSIR_PSU_ENABLE(psuno)			((psuno) << 0)

/* LM78_ISR */
#define	EV6_ENV_LM78_PSU_AC_HIGH_LIMIT			0x0000800000000000
#define	EV6_ENV_LM78_PSU_AC_LOW_LIMIT			0x0000400000000000
#define	EV6_ENV_LM78_PSU_OVERTEMP			0x0000200000000000
#define	EV6_ENV_LM78_PSU_12V_OVERAMP			0x0000100000000000
#define	EV6_ENV_LM78_PSU_5V_OVERAMP			0x0000080000000000
#define	EV6_ENV_LM78_PSU_3_3V_OVERAMP			0x0000040000000000
#define	EV6_ENV_LM78_PSU_NUMBER_MASK			0x0000030000000000
#define	EV6_ENV_LM78_PSU_NUMBER_SHIFT			40
#define	EV6_ENV_LM78_FAN6_FAILURE			0x0000008000000000
#define	EV6_ENV_LM78_FAN3_FAILURE			0x0000004000000000
#define	EV6_ENV_LM78_ZONE2_OVERTEMP			0x0000001000000000
#define	EV6_ENV_LM78_CPU3_VIO_OOT			0x0000000800000000
#define	EV6_ENV_LM78_CPU3_VCORE_OOT			0x0000000400000000
#define	EV6_ENV_LM78_CPU2_VIO_OOT			0x0000000200000000
#define	EV6_ENV_LM78_CPU2_VCORE_OOT			0x0000000100000000
#define	EV6_ENV_LM78_FAN5_FAILURE			0x0000000000800000
#define	EV6_ENV_LM78_FAN4_FAILURE			0x0000000000400000
#define	EV6_ENV_LM78_ZONE1_OVERTEMP			0x0000000000100000
#define	EV6_ENV_LM78_CPU1_VIO_OOT			0x0000000000080000
#define	EV6_ENV_LM78_CPU1_VCORE_OOT			0x0000000000040000
#define	EV6_ENV_LM78_CPU0_VIO_OOT			0x0000000000020000
#define	EV6_ENV_LM78_CPU0_VCORE_OOT			0x0000000000010000
#define	EV6_ENV_LM78_PSU_MINUS12V_OOT			0x0000000000000400
#define	EV6_ENV_LM78_CTERM_OOT				0x0000000000000100
#define	EV6_ENV_LM78_FAN2_FAILURE			0x0000000000000080
#define	EV6_ENV_LM78_FAN1_FAILURE			0x0000000000000040
#define	EV6_ENV_LM78_CPU_OVERTEMP			0x0000000000000020
#define	EV6_ENV_LM78_ZONA0_OVERTEMP			0x0000000000000010
#define	EV6_ENV_LM78_VTERM_OOT				0x0000000000000008
#define	EV6_ENV_LM78_PSU_12V_OOT			0x0000000000000004
#define	EV6_ENV_LM78_PSU_5V_OOT				0x0000000000000002
#define	EV6_ENV_LM78_PSU_3_3V_OOT			0x0000000000000001

/* Doors */
#define	EV6_ENV_DOORS_PCI_CLOSED			0x80
#define	EV6_ENV_DOORS_FAN_CLOSED			0x40
#define	EV6_ENV_DOORS_CPU_CLOSED			0x20
#define	EV6_ENV_DOORS_PCI_OPEN				0x08
#define	EV6_ENV_DOORS_FAN_OPEN				0x04
#define	EV6_ENV_DOORS_CPU_OPEN				0x02

/* System Temperature Warning (sticky?) */
#define	EV6_ENV_STW_ZONE2				0x40
#define	EV6_ENV_STW_ZONE1				0x20
#define	EV6_ENV_STW_ZONE0				0x10
#define	EV6_ENV_STW_CPU3				0x08
#define	EV6_ENV_STW_CPU2				0x04
#define	EV6_ENV_STW_CPU1				0x02
#define	EV6_ENV_STW_CPU0				0x01

/* System Fan Control Fault */
#define	EV6_ENV_SFCF_FAN1234_LOW_SPEED			0x0800
#define	EV6_ENV_SFCF_FAN1234_HIGH_SPEED			0x0400
#define	EV6_ENV_SFCF_FAN56_LOW_SPEED			0x0200
#define	EV6_ENV_SFCF_FAN56_HIGH_SPEED			0x0100
#define	EV6_ENV_SFCF_FAN6_NONRESPONSIVE			0x0020
#define	EV6_ENV_SFCF_FAN5_NONRESPONSIVE			0x0010
#define	EV6_ENV_SFCF_FAN4_NONRESPONSIVE			0x0008
#define	EV6_ENV_SFCF_FAN3_NONRESPONSIVE			0x0004
#define	EV6_ENV_SFCF_FAN2_NONRESPONSIVE			0x0002
#define	EV6_ENV_SFCF_FAN1_NONRESPONSIVE			0x0001
