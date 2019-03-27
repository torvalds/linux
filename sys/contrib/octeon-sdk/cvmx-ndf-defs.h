/***********************license start***************
 * Copyright (c) 2003-2012  Cavium Inc. (support@cavium.com). All rights
 * reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.

 *   * Neither the name of Cavium Inc. nor the names of
 *     its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written
 *     permission.

 * This Software, including technical data, may be subject to U.S. export  control
 * laws, including the U.S. Export Administration Act and its  associated
 * regulations, and may be subject to export or import  regulations in other
 * countries.

 * TO THE MAXIMUM EXTENT PERMITTED BY LAW, THE SOFTWARE IS PROVIDED "AS IS"
 * AND WITH ALL FAULTS AND CAVIUM INC. MAKES NO PROMISES, REPRESENTATIONS OR
 * WARRANTIES, EITHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, WITH RESPECT TO
 * THE SOFTWARE, INCLUDING ITS CONDITION, ITS CONFORMITY TO ANY REPRESENTATION OR
 * DESCRIPTION, OR THE EXISTENCE OF ANY LATENT OR PATENT DEFECTS, AND CAVIUM
 * SPECIFICALLY DISCLAIMS ALL IMPLIED (IF ANY) WARRANTIES OF TITLE,
 * MERCHANTABILITY, NONINFRINGEMENT, FITNESS FOR A PARTICULAR PURPOSE, LACK OF
 * VIRUSES, ACCURACY OR COMPLETENESS, QUIET ENJOYMENT, QUIET POSSESSION OR
 * CORRESPONDENCE TO DESCRIPTION. THE ENTIRE  RISK ARISING OUT OF USE OR
 * PERFORMANCE OF THE SOFTWARE LIES WITH YOU.
 ***********************license end**************************************/


/**
 * cvmx-ndf-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon ndf.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_NDF_DEFS_H__
#define __CVMX_NDF_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_BT_PG_INFO CVMX_NDF_BT_PG_INFO_FUNC()
static inline uint64_t CVMX_NDF_BT_PG_INFO_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_BT_PG_INFO not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000018ull);
}
#else
#define CVMX_NDF_BT_PG_INFO (CVMX_ADD_IO_SEG(0x0001070001000018ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_CMD CVMX_NDF_CMD_FUNC()
static inline uint64_t CVMX_NDF_CMD_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_CMD not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000000ull);
}
#else
#define CVMX_NDF_CMD (CVMX_ADD_IO_SEG(0x0001070001000000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_DRBELL CVMX_NDF_DRBELL_FUNC()
static inline uint64_t CVMX_NDF_DRBELL_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_DRBELL not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000030ull);
}
#else
#define CVMX_NDF_DRBELL (CVMX_ADD_IO_SEG(0x0001070001000030ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_ECC_CNT CVMX_NDF_ECC_CNT_FUNC()
static inline uint64_t CVMX_NDF_ECC_CNT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_ECC_CNT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000010ull);
}
#else
#define CVMX_NDF_ECC_CNT (CVMX_ADD_IO_SEG(0x0001070001000010ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_INT CVMX_NDF_INT_FUNC()
static inline uint64_t CVMX_NDF_INT_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_INT not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000020ull);
}
#else
#define CVMX_NDF_INT (CVMX_ADD_IO_SEG(0x0001070001000020ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_INT_EN CVMX_NDF_INT_EN_FUNC()
static inline uint64_t CVMX_NDF_INT_EN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_INT_EN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000028ull);
}
#else
#define CVMX_NDF_INT_EN (CVMX_ADD_IO_SEG(0x0001070001000028ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_MISC CVMX_NDF_MISC_FUNC()
static inline uint64_t CVMX_NDF_MISC_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_MISC not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000008ull);
}
#else
#define CVMX_NDF_MISC (CVMX_ADD_IO_SEG(0x0001070001000008ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_NDF_ST_REG CVMX_NDF_ST_REG_FUNC()
static inline uint64_t CVMX_NDF_ST_REG_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX)))
		cvmx_warn("CVMX_NDF_ST_REG not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x0001070001000038ull);
}
#else
#define CVMX_NDF_ST_REG (CVMX_ADD_IO_SEG(0x0001070001000038ull))
#endif

/**
 * cvmx_ndf_bt_pg_info
 *
 * Notes:
 * NDF_BT_PG_INFO provides page size and number of column plus row address cycles information. SW writes to this CSR
 * during boot from Nand Flash. Additionally SW also writes the multiplier value for timing parameters. This value is
 * used during boot, in the SET_TM_PARAM command. This information is used only by the boot load state machine and is
 * otherwise a don't care, once boot is disabled. Also, boot dma's do not use this value.
 *
 * Bytes per Nand Flash page = 2 ** (SIZE + 1) times 256 bytes.
 * 512, 1k, 2k, 4k, 8k, 16k, 32k and 64k are legal bytes per page values
 *
 * Legal values for ADR_CYC field are 3 through 8. SW CSR writes with a value less than 3 will write a 3 to this
 * field, and a SW CSR write with a value greater than 8, will write an 8 to this field.
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_bt_pg_info {
	uint64_t u64;
	struct cvmx_ndf_bt_pg_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t t_mult                       : 4;  /**< Boot time TIM_MULT[3:0] field of SET__TM_PAR[63:0]
                                                         command */
	uint64_t adr_cyc                      : 4;  /**< # of column address cycles */
	uint64_t size                         : 3;  /**< bytes per page in the nand device */
#else
	uint64_t size                         : 3;
	uint64_t adr_cyc                      : 4;
	uint64_t t_mult                       : 4;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_ndf_bt_pg_info_s          cn52xx;
	struct cvmx_ndf_bt_pg_info_s          cn63xx;
	struct cvmx_ndf_bt_pg_info_s          cn63xxp1;
	struct cvmx_ndf_bt_pg_info_s          cn66xx;
	struct cvmx_ndf_bt_pg_info_s          cn68xx;
	struct cvmx_ndf_bt_pg_info_s          cn68xxp1;
};
typedef union cvmx_ndf_bt_pg_info cvmx_ndf_bt_pg_info_t;

/**
 * cvmx_ndf_cmd
 *
 * Notes:
 * When SW reads this csr, RD_VAL bit in NDF_MISC csr is cleared to 0. SW must always write all 8 bytes whenever it writes
 * this csr. If there are fewer than 8 bytes left in the command sequence that SW wants the NAND flash controller to execute, it
 * must insert Idle (WAIT) commands to make up 8 bytes. SW also must ensure there is enough vacancy in the command fifo to accept these
 * 8 bytes, by first reading the FR_BYT field in the NDF_MISC csr.
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_cmd {
	uint64_t u64;
	struct cvmx_ndf_cmd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t nf_cmd                       : 64; /**< 8 Command Bytes */
#else
	uint64_t nf_cmd                       : 64;
#endif
	} s;
	struct cvmx_ndf_cmd_s                 cn52xx;
	struct cvmx_ndf_cmd_s                 cn63xx;
	struct cvmx_ndf_cmd_s                 cn63xxp1;
	struct cvmx_ndf_cmd_s                 cn66xx;
	struct cvmx_ndf_cmd_s                 cn68xx;
	struct cvmx_ndf_cmd_s                 cn68xxp1;
};
typedef union cvmx_ndf_cmd cvmx_ndf_cmd_t;

/**
 * cvmx_ndf_drbell
 *
 * Notes:
 * SW csr writes will increment CNT by the signed 8 bit value being written. SW csr reads return the current CNT value.
 * HW will also modify the value of the CNT field. Everytime HW executes a BUS_ACQ[15:0] command, to arbitrate and win the
 * flash bus, it decrements the CNT field by 1. If the CNT field is already 0 or negative, HW command execution unit will
 * stall when it fetches the new BUS_ACQ[15:0] command, from the command fifo. Only when the SW writes to this CSR with a
 * non-zero data value, can the execution unit come out of the stalled condition, and resume execution.
 *
 * The intended use of this doorbell CSR is to control execution of the Nand Flash commands. The NDF execution unit
 * has to arbitrate for the flash bus, before it can enable a Nand Flash device connected to the Octeon chip, by
 * asserting the device's chip enable. Therefore SW should first load the command fifo, with a full sequence of
 * commands to perform a Nand Flash device task. This command sequence will start with a bus acquire command and
 * the last command in the sequence will be a bus release command. The execution unit will start execution of
 * the sequence only if the [CNT] field is non-zero when it fetches the bus acquire command, which is the first
 * command in this sequence. SW can also, load multiple such sequences, each starting with a chip enable command
 * and ending with a chip disable command, and then write a non-zero data value to this csr to increment the
 * CNT field by the number of the command sequences, loaded to the command fifo.
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_drbell {
	uint64_t u64;
	struct cvmx_ndf_drbell_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t cnt                          : 8;  /**< Doorbell count register, 2's complement 8 bit value */
#else
	uint64_t cnt                          : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_ndf_drbell_s              cn52xx;
	struct cvmx_ndf_drbell_s              cn63xx;
	struct cvmx_ndf_drbell_s              cn63xxp1;
	struct cvmx_ndf_drbell_s              cn66xx;
	struct cvmx_ndf_drbell_s              cn68xx;
	struct cvmx_ndf_drbell_s              cn68xxp1;
};
typedef union cvmx_ndf_drbell cvmx_ndf_drbell_t;

/**
 * cvmx_ndf_ecc_cnt
 *
 * Notes:
 * XOR_ECC[31:8] = [ecc_gen_byt258, ecc_gen_byt257, ecc_gen_byt256] xor [ecc_258, ecc_257, ecc_256]
 *         ecc_258, ecc_257 and ecc_256 are bytes stored in Nand Flash and read out during boot
 *         ecc_gen_byt258, ecc_gen_byt257, ecc_gen_byt256 are generated from data read out from Nand Flash
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_ecc_cnt {
	uint64_t u64;
	struct cvmx_ndf_ecc_cnt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t xor_ecc                      : 24; /**< result of XOR of ecc read bytes and ecc genarated
                                                         bytes. The value pertains to the last 1 bit ecc err */
	uint64_t ecc_err                      : 8;  /**< Count = \# of 1 bit errors fixed during boot
                                                         This count saturates instead of wrapping around. */
#else
	uint64_t ecc_err                      : 8;
	uint64_t xor_ecc                      : 24;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_ndf_ecc_cnt_s             cn52xx;
	struct cvmx_ndf_ecc_cnt_s             cn63xx;
	struct cvmx_ndf_ecc_cnt_s             cn63xxp1;
	struct cvmx_ndf_ecc_cnt_s             cn66xx;
	struct cvmx_ndf_ecc_cnt_s             cn68xx;
	struct cvmx_ndf_ecc_cnt_s             cn68xxp1;
};
typedef union cvmx_ndf_ecc_cnt cvmx_ndf_ecc_cnt_t;

/**
 * cvmx_ndf_int
 *
 * Notes:
 * FULL status is updated when the command fifo becomes full as a result of SW writing a new command to it.
 *
 * EMPTY status is updated when the command fifo becomes empty as a result of command execution unit fetching the
 * last instruction out of the command fifo.
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_int {
	uint64_t u64;
	struct cvmx_ndf_int_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t ovrf                         : 1;  /**< NDF_CMD write when fifo is full. Generally a
                                                         fatal error. */
	uint64_t ecc_mult                     : 1;  /**< Multi bit ECC error detected during boot */
	uint64_t ecc_1bit                     : 1;  /**< Single bit ECC error detected and fixed during boot */
	uint64_t sm_bad                       : 1;  /**< One of the state machines in a bad state */
	uint64_t wdog                         : 1;  /**< Watch Dog timer expired during command execution */
	uint64_t full                         : 1;  /**< Command fifo is full */
	uint64_t empty                        : 1;  /**< Command fifo is empty */
#else
	uint64_t empty                        : 1;
	uint64_t full                         : 1;
	uint64_t wdog                         : 1;
	uint64_t sm_bad                       : 1;
	uint64_t ecc_1bit                     : 1;
	uint64_t ecc_mult                     : 1;
	uint64_t ovrf                         : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_ndf_int_s                 cn52xx;
	struct cvmx_ndf_int_s                 cn63xx;
	struct cvmx_ndf_int_s                 cn63xxp1;
	struct cvmx_ndf_int_s                 cn66xx;
	struct cvmx_ndf_int_s                 cn68xx;
	struct cvmx_ndf_int_s                 cn68xxp1;
};
typedef union cvmx_ndf_int cvmx_ndf_int_t;

/**
 * cvmx_ndf_int_en
 *
 * Notes:
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 *
 */
union cvmx_ndf_int_en {
	uint64_t u64;
	struct cvmx_ndf_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t ovrf                         : 1;  /**< Wrote to a full command fifo */
	uint64_t ecc_mult                     : 1;  /**< Multi bit ECC error detected during boot */
	uint64_t ecc_1bit                     : 1;  /**< Single bit ECC error detected and fixed during boot */
	uint64_t sm_bad                       : 1;  /**< One of the state machines in a bad state */
	uint64_t wdog                         : 1;  /**< Watch Dog timer expired during command execution */
	uint64_t full                         : 1;  /**< Command fifo is full */
	uint64_t empty                        : 1;  /**< Command fifo is empty */
#else
	uint64_t empty                        : 1;
	uint64_t full                         : 1;
	uint64_t wdog                         : 1;
	uint64_t sm_bad                       : 1;
	uint64_t ecc_1bit                     : 1;
	uint64_t ecc_mult                     : 1;
	uint64_t ovrf                         : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_ndf_int_en_s              cn52xx;
	struct cvmx_ndf_int_en_s              cn63xx;
	struct cvmx_ndf_int_en_s              cn63xxp1;
	struct cvmx_ndf_int_en_s              cn66xx;
	struct cvmx_ndf_int_en_s              cn68xx;
	struct cvmx_ndf_int_en_s              cn68xxp1;
};
typedef union cvmx_ndf_int_en cvmx_ndf_int_en_t;

/**
 * cvmx_ndf_misc
 *
 * Notes:
 * NBR_HWM this field specifies the high water mark for the NCB outbound load/store commands receive fifo.
 *   the fifo size is 16 entries.
 *
 * WAIT_CNT this field allows glitch filtering of the WAIT_n input to octeon, from Flash Memory. The count
 *   represents number of eclk cycles.
 *
 * FR_BYT  this field specifies \# of unfilled bytes in the command fifo. Bytes become unfilled as commands
 *   complete execution and exit. (fifo is 256 bytes when BT_DIS=0,  and 1536 bytes when BT_DIS=1)
 *
 * RD_DONE this W1C bit is set to 1 by HW when it reads the last 8 bytes out of the command fifo,
 *   in response to RD_CMD bit being set to 1 by SW.
 *
 * RD_VAL  this read only bit is set to 1 by HW when it reads next 8 bytes from command fifo in response
 *   to RD_CMD bit being set to 1. A SW read of NDF_CMD csr clears this bit to 0.
 *
 * RD_CMD  this R/W bit starts read out from the command fifo, 8 bytes at a time. SW should first read the
 *   RD_VAL bit in  this csr to see if next 8 bytes from the command fifo are available in the
 *   NDF_CMD csr. All command fifo reads start and end on an 8 byte boundary. A RD_CMD in the
 *   middle of command execution will cause the execution to freeze until RD_DONE is set to 1. RD_CMD
 *   bit will be cleared on any NDF_CMD csr write by SW.
 *
 * BT_DMA  this indicates to the NAND flash boot control state machine that boot dma read can begin.
 *   SW should set this bit to 1 after SW has loaded the command fifo. HW sets the bit to 0
 *   when boot dma command execution is complete. If chip enable 0 is not nand flash, this bit is
 *   permanently 1'b0 with SW writes ignored. Whenever BT_DIS=1, this bit will be 0.
 *
 * BT_DIS  this R/W bit indicates to NAND flash boot control state machine that boot operation has ended.
 *   whenever this bit changes from 0 to a 1, the command fifo is emptied as a side effect. This bit must
 *   never be set when booting from nand flash and region zero is enabled.
 *
 * EX_DIS  When 1, command execution stops after completing execution of all commands currently in the command
 *   fifo. Once command execution has stopped, and then new commands are loaded into the command fifo, execution
 *   will not resume as long as this bit is 1. When this bit is 0, command execution will resume if command fifo
 *   is not empty. EX_DIS should be set to 1, during boot i.e. when BT_DIS = 0.
 *
 * RST_FF  reset command fifo to make it empty, any command inflight is not aborted before reseting
 *   the fifo. The fifo comes up empty at the end of power on reset.
 *
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_misc {
	uint64_t u64;
	struct cvmx_ndf_misc_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t mb_dis                       : 1;  /**< Disable multibit error hangs and allow boot loads
                                                         or boot dma's proceed as if no multi bit errors
                                                         occured. HW will fix single bit errors as usual */
	uint64_t nbr_hwm                      : 3;  /**< Hi Water mark for NBR fifo or load/stores */
	uint64_t wait_cnt                     : 6;  /**< WAIT input filter count */
	uint64_t fr_byt                       : 11; /**< Number of unfilled Command fifo bytes */
	uint64_t rd_done                      : 1;  /**< This W1C bit is set to 1 by HW when it completes
                                                         command fifo read out, in response to RD_CMD */
	uint64_t rd_val                       : 1;  /**< This RO bit is set to 1 by HW when it reads next 8
                                                         bytes from Command fifo into the NDF_CMD csr
                                                         SW reads NDF_CMD csr, HW clears this bit to 0 */
	uint64_t rd_cmd                       : 1;  /**< When 1, HW reads out contents of the Command fifo 8
                                                         bytes at a time into the NDF_CMD csr */
	uint64_t bt_dma                       : 1;  /**< When set to 1, boot time dma is enabled */
	uint64_t bt_dis                       : 1;  /**< When boot operation is over SW must set to 1
                                                         causes boot state mchines to sleep */
	uint64_t ex_dis                       : 1;  /**< When set to 1, suspends execution of commands at
                                                         next command in the fifo. */
	uint64_t rst_ff                       : 1;  /**< 1=reset command fifo to make it empty,
                                                         0=normal operation */
#else
	uint64_t rst_ff                       : 1;
	uint64_t ex_dis                       : 1;
	uint64_t bt_dis                       : 1;
	uint64_t bt_dma                       : 1;
	uint64_t rd_cmd                       : 1;
	uint64_t rd_val                       : 1;
	uint64_t rd_done                      : 1;
	uint64_t fr_byt                       : 11;
	uint64_t wait_cnt                     : 6;
	uint64_t nbr_hwm                      : 3;
	uint64_t mb_dis                       : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} s;
	struct cvmx_ndf_misc_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t nbr_hwm                      : 3;  /**< Hi Water mark for NBR fifo or load/stores */
	uint64_t wait_cnt                     : 6;  /**< WAIT input filter count */
	uint64_t fr_byt                       : 11; /**< Number of unfilled Command fifo bytes */
	uint64_t rd_done                      : 1;  /**< This W1C bit is set to 1 by HW when it completes
                                                         command fifo read out, in response to RD_CMD */
	uint64_t rd_val                       : 1;  /**< This RO bit is set to 1 by HW when it reads next 8
                                                         bytes from Command fifo into the NDF_CMD csr
                                                         SW reads NDF_CMD csr, HW clears this bit to 0 */
	uint64_t rd_cmd                       : 1;  /**< When 1, HW reads out contents of the Command fifo 8
                                                         bytes at a time into the NDF_CMD csr */
	uint64_t bt_dma                       : 1;  /**< When set to 1, boot time dma is enabled */
	uint64_t bt_dis                       : 1;  /**< When boot operation is over SW must set to 1
                                                         causes boot state mchines to sleep */
	uint64_t ex_dis                       : 1;  /**< When set to 1, suspends execution of commands at
                                                         next command in the fifo. */
	uint64_t rst_ff                       : 1;  /**< 1=reset command fifo to make it empty,
                                                         0=normal operation */
#else
	uint64_t rst_ff                       : 1;
	uint64_t ex_dis                       : 1;
	uint64_t bt_dis                       : 1;
	uint64_t bt_dma                       : 1;
	uint64_t rd_cmd                       : 1;
	uint64_t rd_val                       : 1;
	uint64_t rd_done                      : 1;
	uint64_t fr_byt                       : 11;
	uint64_t wait_cnt                     : 6;
	uint64_t nbr_hwm                      : 3;
	uint64_t reserved_27_63               : 37;
#endif
	} cn52xx;
	struct cvmx_ndf_misc_s                cn63xx;
	struct cvmx_ndf_misc_s                cn63xxp1;
	struct cvmx_ndf_misc_s                cn66xx;
	struct cvmx_ndf_misc_s                cn68xx;
	struct cvmx_ndf_misc_s                cn68xxp1;
};
typedef union cvmx_ndf_misc cvmx_ndf_misc_t;

/**
 * cvmx_ndf_st_reg
 *
 * Notes:
 * This CSR aggregates all state machines used in nand flash controller for debug.
 * Like all NDF_... registers, 64-bit operations must be used to access this register
 */
union cvmx_ndf_st_reg {
	uint64_t u64;
	struct cvmx_ndf_st_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t exe_idle                     : 1;  /**< Command Execution status 1=IDLE, 0=Busy
                                                         1 means execution of command sequence is complete
                                                         and command fifo is empty */
	uint64_t exe_sm                       : 4;  /**< Command Execution State machine states */
	uint64_t bt_sm                        : 4;  /**< Boot load and Boot dma State machine states */
	uint64_t rd_ff_bad                    : 1;  /**< CMD fifo read back State machine in bad state */
	uint64_t rd_ff                        : 2;  /**< CMD fifo read back State machine states */
	uint64_t main_bad                     : 1;  /**< Main State machine in bad state */
	uint64_t main_sm                      : 3;  /**< Main State machine states */
#else
	uint64_t main_sm                      : 3;
	uint64_t main_bad                     : 1;
	uint64_t rd_ff                        : 2;
	uint64_t rd_ff_bad                    : 1;
	uint64_t bt_sm                        : 4;
	uint64_t exe_sm                       : 4;
	uint64_t exe_idle                     : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_ndf_st_reg_s              cn52xx;
	struct cvmx_ndf_st_reg_s              cn63xx;
	struct cvmx_ndf_st_reg_s              cn63xxp1;
	struct cvmx_ndf_st_reg_s              cn66xx;
	struct cvmx_ndf_st_reg_s              cn68xx;
	struct cvmx_ndf_st_reg_s              cn68xxp1;
};
typedef union cvmx_ndf_st_reg cvmx_ndf_st_reg_t;

#endif
