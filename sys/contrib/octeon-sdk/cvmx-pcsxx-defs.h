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
 * cvmx-pcsxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pcsxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCSXX_DEFS_H__
#define __CVMX_PCSXX_DEFS_H__

static inline uint64_t CVMX_PCSXX_10GBX_STATUS_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_10GBX_STATUS_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000828ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_BIST_STATUS_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_BIST_STATUS_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000870ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_BIT_LOCK_STATUS_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_BIT_LOCK_STATUS_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000850ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_CONTROL1_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_CONTROL1_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000800ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_CONTROL2_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_CONTROL2_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000818ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_INT_EN_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_INT_EN_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000860ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_INT_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_INT_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000858ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_LOG_ANL_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_LOG_ANL_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000868ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_MISC_CTL_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_MISC_CTL_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000848ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_RX_SYNC_STATES_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_RX_SYNC_STATES_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000838ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_SPD_ABIL_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_SPD_ABIL_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000810ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_STATUS1_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_STATUS1_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000808ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_STATUS2_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_STATUS2_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000820ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_TX_RX_POLARITY_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_TX_RX_POLARITY_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000840ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_PCSXX_TX_RX_STATES_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_PCSXX_TX_RX_STATES_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0000830ull) + ((block_id) & 7) * 0x1000000ull;
}

/**
 * cvmx_pcsx#_10gbx_status_reg
 *
 * PCSX_10GBX_STATUS_REG = 10gbx_status_reg
 *
 */
union cvmx_pcsxx_10gbx_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_10gbx_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t alignd                       : 1;  /**< 1=Lane alignment achieved, 0=Lanes not aligned */
	uint64_t pattst                       : 1;  /**< Always at 0, no pattern testing capability */
	uint64_t reserved_4_10                : 7;
	uint64_t l3sync                       : 1;  /**< 1=Rcv lane 3 code grp synchronized, 0=not sync'ed */
	uint64_t l2sync                       : 1;  /**< 1=Rcv lane 2 code grp synchronized, 0=not sync'ed */
	uint64_t l1sync                       : 1;  /**< 1=Rcv lane 1 code grp synchronized, 0=not sync'ed */
	uint64_t l0sync                       : 1;  /**< 1=Rcv lane 0 code grp synchronized, 0=not sync'ed */
#else
	uint64_t l0sync                       : 1;
	uint64_t l1sync                       : 1;
	uint64_t l2sync                       : 1;
	uint64_t l3sync                       : 1;
	uint64_t reserved_4_10                : 7;
	uint64_t pattst                       : 1;
	uint64_t alignd                       : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn52xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn52xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn56xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn56xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn61xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn63xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn63xxp1;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn66xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn68xx;
	struct cvmx_pcsxx_10gbx_status_reg_s  cn68xxp1;
};
typedef union cvmx_pcsxx_10gbx_status_reg cvmx_pcsxx_10gbx_status_reg_t;

/**
 * cvmx_pcsx#_bist_status_reg
 *
 * NOTE: Logic Analyzer is enabled with LA_EN for xaui only. PKT_SZ is effective only when LA_EN=1
 * For normal operation(xaui), this bit must be 0. The dropped lane is used to send rxc[3:0].
 * See pcs.csr  for sgmii/1000Base-X logic analyzer mode.
 * For full description see document at .../rtl/pcs/readme_logic_analyzer.txt
 *
 *
 *  PCSX Bist Status Register
 */
union cvmx_pcsxx_bist_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_bist_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t bist_status                  : 1;  /**< 1=bist failure, 0=bisted memory ok or bist in progress
                                                         pcsx.tx_sm.drf8x36m1_async_bist */
#else
	uint64_t bist_status                  : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_pcsxx_bist_status_reg_s   cn52xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn52xxp1;
	struct cvmx_pcsxx_bist_status_reg_s   cn56xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn56xxp1;
	struct cvmx_pcsxx_bist_status_reg_s   cn61xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn63xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn63xxp1;
	struct cvmx_pcsxx_bist_status_reg_s   cn66xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn68xx;
	struct cvmx_pcsxx_bist_status_reg_s   cn68xxp1;
};
typedef union cvmx_pcsxx_bist_status_reg cvmx_pcsxx_bist_status_reg_t;

/**
 * cvmx_pcsx#_bit_lock_status_reg
 *
 * LN_SWAP for XAUI is to simplify interconnection layout between devices
 *
 *
 * PCSX Bit Lock Status Register
 */
union cvmx_pcsxx_bit_lock_status_reg {
	uint64_t u64;
	struct cvmx_pcsxx_bit_lock_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t bitlck3                      : 1;  /**< Receive Lane 3 bit lock status */
	uint64_t bitlck2                      : 1;  /**< Receive Lane 2 bit lock status */
	uint64_t bitlck1                      : 1;  /**< Receive Lane 1 bit lock status */
	uint64_t bitlck0                      : 1;  /**< Receive Lane 0 bit lock status */
#else
	uint64_t bitlck0                      : 1;
	uint64_t bitlck1                      : 1;
	uint64_t bitlck2                      : 1;
	uint64_t bitlck3                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn52xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn52xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn56xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn56xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn61xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn63xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn63xxp1;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn66xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn68xx;
	struct cvmx_pcsxx_bit_lock_status_reg_s cn68xxp1;
};
typedef union cvmx_pcsxx_bit_lock_status_reg cvmx_pcsxx_bit_lock_status_reg_t;

/**
 * cvmx_pcsx#_control1_reg
 *
 * NOTE: Logic Analyzer is enabled with LA_EN for the specified PCS lane only. PKT_SZ is effective only when LA_EN=1
 * For normal operation(sgmii or 1000Base-X), this bit must be 0.
 * See pcsx.csr for xaui logic analyzer mode.
 * For full description see document at .../rtl/pcs/readme_logic_analyzer.txt
 *
 *
 *  PCSX regs follow IEEE Std 802.3-2005, Section: 45.2.3
 *
 *
 *  PCSX_CONTROL1_REG = Control Register1
 */
union cvmx_pcsxx_control1_reg {
	uint64_t u64;
	struct cvmx_pcsxx_control1_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t reset                        : 1;  /**< 1=SW PCSX Reset, the bit will return to 0 after pcs
                                                         has been reset. Takes 32 eclk cycles to reset pcs
                                                         0=Normal operation */
	uint64_t loopbck1                     : 1;  /**< 0=normal operation, 1=internal loopback mode
                                                         xgmii tx data received from gmx tx port is returned
                                                         back into gmx, xgmii rx port. */
	uint64_t spdsel1                      : 1;  /**< See bit 6 description */
	uint64_t reserved_12_12               : 1;
	uint64_t lo_pwr                       : 1;  /**< 1=Power Down(HW reset), 0=Normal operation */
	uint64_t reserved_7_10                : 4;
	uint64_t spdsel0                      : 1;  /**< SPDSEL1 and SPDSEL0 are always at 1'b1. Write has
                                                         no effect.
                                                         [<6>, <13>]Link Speed selection
                                                           1    1   Bits 5:2 select speed */
	uint64_t spd                          : 4;  /**< Always select 10Gb/s, writes have no effect */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t spd                          : 4;
	uint64_t spdsel0                      : 1;
	uint64_t reserved_7_10                : 4;
	uint64_t lo_pwr                       : 1;
	uint64_t reserved_12_12               : 1;
	uint64_t spdsel1                      : 1;
	uint64_t loopbck1                     : 1;
	uint64_t reset                        : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsxx_control1_reg_s      cn52xx;
	struct cvmx_pcsxx_control1_reg_s      cn52xxp1;
	struct cvmx_pcsxx_control1_reg_s      cn56xx;
	struct cvmx_pcsxx_control1_reg_s      cn56xxp1;
	struct cvmx_pcsxx_control1_reg_s      cn61xx;
	struct cvmx_pcsxx_control1_reg_s      cn63xx;
	struct cvmx_pcsxx_control1_reg_s      cn63xxp1;
	struct cvmx_pcsxx_control1_reg_s      cn66xx;
	struct cvmx_pcsxx_control1_reg_s      cn68xx;
	struct cvmx_pcsxx_control1_reg_s      cn68xxp1;
};
typedef union cvmx_pcsxx_control1_reg cvmx_pcsxx_control1_reg_t;

/**
 * cvmx_pcsx#_control2_reg
 *
 * PCSX_CONTROL2_REG = Control Register2
 *
 */
union cvmx_pcsxx_control2_reg {
	uint64_t u64;
	struct cvmx_pcsxx_control2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t type                         : 2;  /**< Always 2'b01, 10GBASE-X only supported */
#else
	uint64_t type                         : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pcsxx_control2_reg_s      cn52xx;
	struct cvmx_pcsxx_control2_reg_s      cn52xxp1;
	struct cvmx_pcsxx_control2_reg_s      cn56xx;
	struct cvmx_pcsxx_control2_reg_s      cn56xxp1;
	struct cvmx_pcsxx_control2_reg_s      cn61xx;
	struct cvmx_pcsxx_control2_reg_s      cn63xx;
	struct cvmx_pcsxx_control2_reg_s      cn63xxp1;
	struct cvmx_pcsxx_control2_reg_s      cn66xx;
	struct cvmx_pcsxx_control2_reg_s      cn68xx;
	struct cvmx_pcsxx_control2_reg_s      cn68xxp1;
};
typedef union cvmx_pcsxx_control2_reg cvmx_pcsxx_control2_reg_t;

/**
 * cvmx_pcsx#_int_en_reg
 *
 * Note: DBG_SYNC is a edge triggered interrupt. When set it indicates PCS Synchronization state machine in
 *       Figure 48-7 state diagram in IEEE Std 802.3-2005 changes state SYNC_ACQUIRED_1 to SYNC_ACQUIRED_2
 *       indicating an invalid code group was received on one of the 4 receive lanes.
 *       This interrupt should be always disabled and used only for link problem debugging help.
 *
 *
 * PCSX Interrupt Enable Register
 */
union cvmx_pcsxx_int_en_reg {
	uint64_t u64;
	struct cvmx_pcsxx_int_en_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t dbg_sync_en                  : 1;  /**< Code Group sync failure debug help */
	uint64_t algnlos_en                   : 1;  /**< Enable ALGNLOS interrupt */
	uint64_t synlos_en                    : 1;  /**< Enable SYNLOS interrupt */
	uint64_t bitlckls_en                  : 1;  /**< Enable BITLCKLS interrupt */
	uint64_t rxsynbad_en                  : 1;  /**< Enable RXSYNBAD  interrupt */
	uint64_t rxbad_en                     : 1;  /**< Enable RXBAD  interrupt */
	uint64_t txflt_en                     : 1;  /**< Enable TXFLT   interrupt */
#else
	uint64_t txflt_en                     : 1;
	uint64_t rxbad_en                     : 1;
	uint64_t rxsynbad_en                  : 1;
	uint64_t bitlckls_en                  : 1;
	uint64_t synlos_en                    : 1;
	uint64_t algnlos_en                   : 1;
	uint64_t dbg_sync_en                  : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pcsxx_int_en_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t algnlos_en                   : 1;  /**< Enable ALGNLOS interrupt */
	uint64_t synlos_en                    : 1;  /**< Enable SYNLOS interrupt */
	uint64_t bitlckls_en                  : 1;  /**< Enable BITLCKLS interrupt */
	uint64_t rxsynbad_en                  : 1;  /**< Enable RXSYNBAD  interrupt */
	uint64_t rxbad_en                     : 1;  /**< Enable RXBAD  interrupt */
	uint64_t txflt_en                     : 1;  /**< Enable TXFLT   interrupt */
#else
	uint64_t txflt_en                     : 1;
	uint64_t rxbad_en                     : 1;
	uint64_t rxsynbad_en                  : 1;
	uint64_t bitlckls_en                  : 1;
	uint64_t synlos_en                    : 1;
	uint64_t algnlos_en                   : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} cn52xx;
	struct cvmx_pcsxx_int_en_reg_cn52xx   cn52xxp1;
	struct cvmx_pcsxx_int_en_reg_cn52xx   cn56xx;
	struct cvmx_pcsxx_int_en_reg_cn52xx   cn56xxp1;
	struct cvmx_pcsxx_int_en_reg_s        cn61xx;
	struct cvmx_pcsxx_int_en_reg_s        cn63xx;
	struct cvmx_pcsxx_int_en_reg_s        cn63xxp1;
	struct cvmx_pcsxx_int_en_reg_s        cn66xx;
	struct cvmx_pcsxx_int_en_reg_s        cn68xx;
	struct cvmx_pcsxx_int_en_reg_s        cn68xxp1;
};
typedef union cvmx_pcsxx_int_en_reg cvmx_pcsxx_int_en_reg_t;

/**
 * cvmx_pcsx#_int_reg
 *
 * PCSX Interrupt Register
 *
 */
union cvmx_pcsxx_int_reg {
	uint64_t u64;
	struct cvmx_pcsxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t dbg_sync                     : 1;  /**< Code Group sync failure debug help, see Note below */
	uint64_t algnlos                      : 1;  /**< Set when XAUI lanes lose alignment */
	uint64_t synlos                       : 1;  /**< Set when Code group sync lost on 1 or more  lanes */
	uint64_t bitlckls                     : 1;  /**< Set when Bit lock lost on 1 or more xaui lanes */
	uint64_t rxsynbad                     : 1;  /**< Set when RX code grp sync st machine in bad state
                                                         in one of the 4 xaui lanes */
	uint64_t rxbad                        : 1;  /**< Set when RX state machine in bad state */
	uint64_t txflt                        : 1;  /**< None defined at this time, always 0x0 */
#else
	uint64_t txflt                        : 1;
	uint64_t rxbad                        : 1;
	uint64_t rxsynbad                     : 1;
	uint64_t bitlckls                     : 1;
	uint64_t synlos                       : 1;
	uint64_t algnlos                      : 1;
	uint64_t dbg_sync                     : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pcsxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t algnlos                      : 1;  /**< Set when XAUI lanes lose alignment */
	uint64_t synlos                       : 1;  /**< Set when Code group sync lost on 1 or more  lanes */
	uint64_t bitlckls                     : 1;  /**< Set when Bit lock lost on 1 or more xaui lanes */
	uint64_t rxsynbad                     : 1;  /**< Set when RX code grp sync st machine in bad state
                                                         in one of the 4 xaui lanes */
	uint64_t rxbad                        : 1;  /**< Set when RX state machine in bad state */
	uint64_t txflt                        : 1;  /**< None defined at this time, always 0x0 */
#else
	uint64_t txflt                        : 1;
	uint64_t rxbad                        : 1;
	uint64_t rxsynbad                     : 1;
	uint64_t bitlckls                     : 1;
	uint64_t synlos                       : 1;
	uint64_t algnlos                      : 1;
	uint64_t reserved_6_63                : 58;
#endif
	} cn52xx;
	struct cvmx_pcsxx_int_reg_cn52xx      cn52xxp1;
	struct cvmx_pcsxx_int_reg_cn52xx      cn56xx;
	struct cvmx_pcsxx_int_reg_cn52xx      cn56xxp1;
	struct cvmx_pcsxx_int_reg_s           cn61xx;
	struct cvmx_pcsxx_int_reg_s           cn63xx;
	struct cvmx_pcsxx_int_reg_s           cn63xxp1;
	struct cvmx_pcsxx_int_reg_s           cn66xx;
	struct cvmx_pcsxx_int_reg_s           cn68xx;
	struct cvmx_pcsxx_int_reg_s           cn68xxp1;
};
typedef union cvmx_pcsxx_int_reg cvmx_pcsxx_int_reg_t;

/**
 * cvmx_pcsx#_log_anl_reg
 *
 * PCSX Logic Analyzer Register
 *
 */
union cvmx_pcsxx_log_anl_reg {
	uint64_t u64;
	struct cvmx_pcsxx_log_anl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t enc_mode                     : 1;  /**< 1=send xaui encoded data, 0=send xaui raw data to GMX
                                                         See .../rtl/pcs/readme_logic_analyzer.txt for details */
	uint64_t drop_ln                      : 2;  /**< xaui lane# to drop from logic analyzer packets
                                                         [<5>, <4>]  Drop lane \#
                                                          0    0   Drop lane 0 data
                                                          0    1   Drop lane 1 data
                                                          1    0   Drop lane 2 data
                                                          1    1   Drop lane 3 data */
	uint64_t lafifovfl                    : 1;  /**< 1=logic analyser fif overflowed one or more times
                                                         during packetization.
                                                         Write 1 to clear this bit */
	uint64_t la_en                        : 1;  /**< 1= Logic Analyzer enabled, 0=Logic Analyzer disabled */
	uint64_t pkt_sz                       : 2;  /**< [<1>, <0>]  Logic Analyzer Packet Size
                                                         0    0   Packet size 1k bytes
                                                         0    1   Packet size 4k bytes
                                                         1    0   Packet size 8k bytes
                                                         1    1   Packet size 16k bytes */
#else
	uint64_t pkt_sz                       : 2;
	uint64_t la_en                        : 1;
	uint64_t lafifovfl                    : 1;
	uint64_t drop_ln                      : 2;
	uint64_t enc_mode                     : 1;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pcsxx_log_anl_reg_s       cn52xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn52xxp1;
	struct cvmx_pcsxx_log_anl_reg_s       cn56xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn56xxp1;
	struct cvmx_pcsxx_log_anl_reg_s       cn61xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn63xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn63xxp1;
	struct cvmx_pcsxx_log_anl_reg_s       cn66xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn68xx;
	struct cvmx_pcsxx_log_anl_reg_s       cn68xxp1;
};
typedef union cvmx_pcsxx_log_anl_reg cvmx_pcsxx_log_anl_reg_t;

/**
 * cvmx_pcsx#_misc_ctl_reg
 *
 * RX lane polarity vector [3:0] = XOR_RXPLRT<9:6>  ^  [4[RXPLRT<1>]];
 *
 * TX lane polarity vector [3:0] = XOR_TXPLRT<5:2>  ^  [4[TXPLRT<0>]];
 *
 * In short keep <1:0> to 2'b00, and use <5:2> and <9:6> fields to define per lane polarities
 *
 *
 *
 * PCSX Misc Control Register
 */
union cvmx_pcsxx_misc_ctl_reg {
	uint64_t u64;
	struct cvmx_pcsxx_misc_ctl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t tx_swap                      : 1;  /**< 0=do not swap xaui lanes going out to qlm's
                                                         1=swap lanes 3 <-> 0   and   2 <-> 1 */
	uint64_t rx_swap                      : 1;  /**< 0=do not swap xaui lanes coming in from qlm's
                                                         1=swap lanes 3 <-> 0   and   2 <-> 1 */
	uint64_t xaui                         : 1;  /**< 1=XAUI mode selected, 0=not XAUI mode selected
                                                         This bit represents pi_qlm1/3_cfg[1:0] pin status */
	uint64_t gmxeno                       : 1;  /**< GMX port enable override, GMX en/dis status is held
                                                         during data packet reception. */
#else
	uint64_t gmxeno                       : 1;
	uint64_t xaui                         : 1;
	uint64_t rx_swap                      : 1;
	uint64_t tx_swap                      : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn52xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn52xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn56xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn56xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn61xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn63xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn63xxp1;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn66xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn68xx;
	struct cvmx_pcsxx_misc_ctl_reg_s      cn68xxp1;
};
typedef union cvmx_pcsxx_misc_ctl_reg cvmx_pcsxx_misc_ctl_reg_t;

/**
 * cvmx_pcsx#_rx_sync_states_reg
 *
 * PCSX_RX_SYNC_STATES_REG = Receive Sync States Register
 *
 */
union cvmx_pcsxx_rx_sync_states_reg {
	uint64_t u64;
	struct cvmx_pcsxx_rx_sync_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t sync3st                      : 4;  /**< Receive lane 3 code grp sync state machine state */
	uint64_t sync2st                      : 4;  /**< Receive lane 2 code grp sync state machine state */
	uint64_t sync1st                      : 4;  /**< Receive lane 1 code grp sync state machine state */
	uint64_t sync0st                      : 4;  /**< Receive lane 0 code grp sync state machine state */
#else
	uint64_t sync0st                      : 4;
	uint64_t sync1st                      : 4;
	uint64_t sync2st                      : 4;
	uint64_t sync3st                      : 4;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn52xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn52xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn56xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn56xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn61xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn63xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn63xxp1;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn66xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn68xx;
	struct cvmx_pcsxx_rx_sync_states_reg_s cn68xxp1;
};
typedef union cvmx_pcsxx_rx_sync_states_reg cvmx_pcsxx_rx_sync_states_reg_t;

/**
 * cvmx_pcsx#_spd_abil_reg
 *
 * PCSX_SPD_ABIL_REG = Speed ability register
 *
 */
union cvmx_pcsxx_spd_abil_reg {
	uint64_t u64;
	struct cvmx_pcsxx_spd_abil_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t tenpasst                     : 1;  /**< Always 0, no 10PASS-TS/2BASE-TL capability support */
	uint64_t tengb                        : 1;  /**< Always 1, 10Gb/s supported */
#else
	uint64_t tengb                        : 1;
	uint64_t tenpasst                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pcsxx_spd_abil_reg_s      cn52xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn52xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s      cn56xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn56xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s      cn61xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn63xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn63xxp1;
	struct cvmx_pcsxx_spd_abil_reg_s      cn66xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn68xx;
	struct cvmx_pcsxx_spd_abil_reg_s      cn68xxp1;
};
typedef union cvmx_pcsxx_spd_abil_reg cvmx_pcsxx_spd_abil_reg_t;

/**
 * cvmx_pcsx#_status1_reg
 *
 * PCSX_STATUS1_REG = Status Register1
 *
 */
union cvmx_pcsxx_status1_reg {
	uint64_t u64;
	struct cvmx_pcsxx_status1_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t flt                          : 1;  /**< 1=Fault condition detected, 0=No fault condition
                                                         This bit is a logical OR of Status2 reg bits 11,10 */
	uint64_t reserved_3_6                 : 4;
	uint64_t rcv_lnk                      : 1;  /**< 1=Receive Link up, 0=Receive Link down
                                                         Latching Low version of r_10gbx_status_reg[12],
                                                         Link down status continues until SW read. */
	uint64_t lpable                       : 1;  /**< Always set to 1 for Low Power ablility indication */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t lpable                       : 1;
	uint64_t rcv_lnk                      : 1;
	uint64_t reserved_3_6                 : 4;
	uint64_t flt                          : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pcsxx_status1_reg_s       cn52xx;
	struct cvmx_pcsxx_status1_reg_s       cn52xxp1;
	struct cvmx_pcsxx_status1_reg_s       cn56xx;
	struct cvmx_pcsxx_status1_reg_s       cn56xxp1;
	struct cvmx_pcsxx_status1_reg_s       cn61xx;
	struct cvmx_pcsxx_status1_reg_s       cn63xx;
	struct cvmx_pcsxx_status1_reg_s       cn63xxp1;
	struct cvmx_pcsxx_status1_reg_s       cn66xx;
	struct cvmx_pcsxx_status1_reg_s       cn68xx;
	struct cvmx_pcsxx_status1_reg_s       cn68xxp1;
};
typedef union cvmx_pcsxx_status1_reg cvmx_pcsxx_status1_reg_t;

/**
 * cvmx_pcsx#_status2_reg
 *
 * PCSX_STATUS2_REG = Status Register2
 *
 */
union cvmx_pcsxx_status2_reg {
	uint64_t u64;
	struct cvmx_pcsxx_status2_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t dev                          : 2;  /**< Always at 2'b10, means a Device present at the addr */
	uint64_t reserved_12_13               : 2;
	uint64_t xmtflt                       : 1;  /**< 0=No xmit fault, 1=xmit fault. Implements latching
                                                         High function until SW read. */
	uint64_t rcvflt                       : 1;  /**< 0=No rcv fault, 1=rcv fault. Implements latching
                                                         High function until SW read */
	uint64_t reserved_3_9                 : 7;
	uint64_t tengb_w                      : 1;  /**< Always 0, no 10GBASE-W capability */
	uint64_t tengb_x                      : 1;  /**< Always 1, 10GBASE-X capable */
	uint64_t tengb_r                      : 1;  /**< Always 0, no 10GBASE-R capability */
#else
	uint64_t tengb_r                      : 1;
	uint64_t tengb_x                      : 1;
	uint64_t tengb_w                      : 1;
	uint64_t reserved_3_9                 : 7;
	uint64_t rcvflt                       : 1;
	uint64_t xmtflt                       : 1;
	uint64_t reserved_12_13               : 2;
	uint64_t dev                          : 2;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsxx_status2_reg_s       cn52xx;
	struct cvmx_pcsxx_status2_reg_s       cn52xxp1;
	struct cvmx_pcsxx_status2_reg_s       cn56xx;
	struct cvmx_pcsxx_status2_reg_s       cn56xxp1;
	struct cvmx_pcsxx_status2_reg_s       cn61xx;
	struct cvmx_pcsxx_status2_reg_s       cn63xx;
	struct cvmx_pcsxx_status2_reg_s       cn63xxp1;
	struct cvmx_pcsxx_status2_reg_s       cn66xx;
	struct cvmx_pcsxx_status2_reg_s       cn68xx;
	struct cvmx_pcsxx_status2_reg_s       cn68xxp1;
};
typedef union cvmx_pcsxx_status2_reg cvmx_pcsxx_status2_reg_t;

/**
 * cvmx_pcsx#_tx_rx_polarity_reg
 *
 * PCSX_POLARITY_REG = TX_RX polarity reg
 *
 */
union cvmx_pcsxx_tx_rx_polarity_reg {
	uint64_t u64;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t xor_rxplrt                   : 4;  /**< Per lane RX polarity control */
	uint64_t xor_txplrt                   : 4;  /**< Per lane TX polarity control */
	uint64_t rxplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
	uint64_t txplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
#else
	uint64_t txplrt                       : 1;
	uint64_t rxplrt                       : 1;
	uint64_t xor_txplrt                   : 4;
	uint64_t xor_rxplrt                   : 4;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn52xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t rxplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
	uint64_t txplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
#else
	uint64_t txplrt                       : 1;
	uint64_t rxplrt                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn52xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn56xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_cn52xxp1 cn56xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn61xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn63xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn63xxp1;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn66xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn68xx;
	struct cvmx_pcsxx_tx_rx_polarity_reg_s cn68xxp1;
};
typedef union cvmx_pcsxx_tx_rx_polarity_reg cvmx_pcsxx_tx_rx_polarity_reg_t;

/**
 * cvmx_pcsx#_tx_rx_states_reg
 *
 * PCSX_TX_RX_STATES_REG = Transmit Receive States Register
 *
 */
union cvmx_pcsxx_tx_rx_states_reg {
	uint64_t u64;
	struct cvmx_pcsxx_tx_rx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t term_err                     : 1;  /**< 1=Check end function detected error in packet
                                                         terminate ||T|| column or the one after it */
	uint64_t syn3bad                      : 1;  /**< 1=lane 3 code grp sync state machine in bad state */
	uint64_t syn2bad                      : 1;  /**< 1=lane 2 code grp sync state machine in bad state */
	uint64_t syn1bad                      : 1;  /**< 1=lane 1 code grp sync state machine in bad state */
	uint64_t syn0bad                      : 1;  /**< 1=lane 0 code grp sync state machine in bad state */
	uint64_t rxbad                        : 1;  /**< 1=Rcv state machine in a bad state, HW malfunction */
	uint64_t algn_st                      : 3;  /**< Lane alignment state machine state state */
	uint64_t rx_st                        : 2;  /**< Receive state machine state state */
	uint64_t tx_st                        : 3;  /**< Transmit state machine state state */
#else
	uint64_t tx_st                        : 3;
	uint64_t rx_st                        : 2;
	uint64_t algn_st                      : 3;
	uint64_t rxbad                        : 1;
	uint64_t syn0bad                      : 1;
	uint64_t syn1bad                      : 1;
	uint64_t syn2bad                      : 1;
	uint64_t syn3bad                      : 1;
	uint64_t term_err                     : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} s;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn52xx;
	struct cvmx_pcsxx_tx_rx_states_reg_cn52xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t syn3bad                      : 1;  /**< 1=lane 3 code grp sync state machine in bad state */
	uint64_t syn2bad                      : 1;  /**< 1=lane 2 code grp sync state machine in bad state */
	uint64_t syn1bad                      : 1;  /**< 1=lane 1 code grp sync state machine in bad state */
	uint64_t syn0bad                      : 1;  /**< 1=lane 0 code grp sync state machine in bad state */
	uint64_t rxbad                        : 1;  /**< 1=Rcv state machine in a bad state, HW malfunction */
	uint64_t algn_st                      : 3;  /**< Lane alignment state machine state state */
	uint64_t rx_st                        : 2;  /**< Receive state machine state state */
	uint64_t tx_st                        : 3;  /**< Transmit state machine state state */
#else
	uint64_t tx_st                        : 3;
	uint64_t rx_st                        : 2;
	uint64_t algn_st                      : 3;
	uint64_t rxbad                        : 1;
	uint64_t syn0bad                      : 1;
	uint64_t syn1bad                      : 1;
	uint64_t syn2bad                      : 1;
	uint64_t syn3bad                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn52xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn56xx;
	struct cvmx_pcsxx_tx_rx_states_reg_cn52xxp1 cn56xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn61xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn63xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn63xxp1;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn66xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn68xx;
	struct cvmx_pcsxx_tx_rx_states_reg_s  cn68xxp1;
};
typedef union cvmx_pcsxx_tx_rx_states_reg cvmx_pcsxx_tx_rx_states_reg_t;

#endif
