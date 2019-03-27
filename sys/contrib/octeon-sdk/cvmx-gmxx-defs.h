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
 * cvmx-gmxx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon gmxx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_GMXX_DEFS_H__
#define __CVMX_GMXX_DEFS_H__

static inline uint64_t CVMX_GMXX_BAD_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000518ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000518ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000518ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_BAD_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000518ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_BIST(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000400ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000400ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000400ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_BIST (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000400ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_BPID_MAPX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && (((offset <= 15)) && ((block_id <= 4))))))
		cvmx_warn("CVMX_GMXX_BPID_MAPX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 15) + ((block_id) & 7) * 0x200000ull) * 8;
}
#else
#define CVMX_GMXX_BPID_MAPX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 15) + ((block_id) & 7) * 0x200000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_BPID_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 4)))))
		cvmx_warn("CVMX_GMXX_BPID_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000700ull) + ((block_id) & 7) * 0x1000000ull;
}
#else
#define CVMX_GMXX_BPID_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180008000700ull) + ((block_id) & 7) * 0x1000000ull)
#endif
static inline uint64_t CVMX_GMXX_CLK_EN(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_CLK_EN (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080007F0ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_EBP_DIS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 4)))))
		cvmx_warn("CVMX_GMXX_EBP_DIS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000608ull) + ((block_id) & 7) * 0x1000000ull;
}
#else
#define CVMX_GMXX_EBP_DIS(block_id) (CVMX_ADD_IO_SEG(0x0001180008000608ull) + ((block_id) & 7) * 0x1000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_EBP_MSK(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 4)))))
		cvmx_warn("CVMX_GMXX_EBP_MSK(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000600ull) + ((block_id) & 7) * 0x1000000ull;
}
#else
#define CVMX_GMXX_EBP_MSK(block_id) (CVMX_ADD_IO_SEG(0x0001180008000600ull) + ((block_id) & 7) * 0x1000000ull)
#endif
static inline uint64_t CVMX_GMXX_HG2_CONTROL(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000550ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000550ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000550ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_HG2_CONTROL (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000550ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_INF_MODE(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_INF_MODE (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080007F8ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_NXA_ADR(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000510ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000510ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000510ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_NXA_ADR (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000510ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_PIPE_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 4)))))
		cvmx_warn("CVMX_GMXX_PIPE_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000760ull) + ((block_id) & 7) * 0x1000000ull;
}
#else
#define CVMX_GMXX_PIPE_STATUS(block_id) (CVMX_ADD_IO_SEG(0x0001180008000760ull) + ((block_id) & 7) * 0x1000000ull)
#endif
static inline uint64_t CVMX_GMXX_PRTX_CBFC_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000580ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000580ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000580ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_PRTX_CBFC_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000580ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_PRTX_CFG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_PRTX_CFG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000010ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RXAUI_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id <= 4)))))
		cvmx_warn("CVMX_GMXX_RXAUI_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000740ull) + ((block_id) & 7) * 0x1000000ull;
}
#else
#define CVMX_GMXX_RXAUI_CTL(block_id) (CVMX_ADD_IO_SEG(0x0001180008000740ull) + ((block_id) & 7) * 0x1000000ull)
#endif
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM0(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM0 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000180ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM1(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM1 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000188ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM2(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM2 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000190ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM3(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM3 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000198ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM4(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM4 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080001A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM5(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM5 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080001A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM_ALL_EN(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000110ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000110ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000110ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM_ALL_EN (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000110ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CAM_EN(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CAM_EN (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000108ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_ADR_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_ADR_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000100ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_DECISION(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_DECISION (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000040ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_FRM_CHK(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_FRM_CHK (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000020ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_FRM_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_FRM_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000018ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RXX_FRM_MAX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_RXX_FRM_MAX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000030ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
}
#else
#define CVMX_GMXX_RXX_FRM_MAX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000030ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RXX_FRM_MIN(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_RXX_FRM_MIN(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000028ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
}
#else
#define CVMX_GMXX_RXX_FRM_MIN(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000028ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#endif
static inline uint64_t CVMX_GMXX_RXX_IFG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_IFG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000058ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_INT_EN(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_INT_EN (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000008ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_INT_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_INT_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000000ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_JABBER(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_JABBER (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000038ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_PAUSE_DROP_TIME(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_PAUSE_DROP_TIME (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000068ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RXX_RX_INBND(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_RXX_RX_INBND(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000060ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
}
#else
#define CVMX_GMXX_RXX_RX_INBND(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000060ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#endif
static inline uint64_t CVMX_GMXX_RXX_STATS_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000050ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_OCTS (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000088ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_OCTS_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000098ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_DMAC(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_OCTS_DMAC (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080000A8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_OCTS_DRP(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_OCTS_DRP (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080000B8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_PKTS (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000080ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_BAD(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_PKTS_BAD (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080000C0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_PKTS_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000090ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_DMAC(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_PKTS_DMAC (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080000A0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_STATS_PKTS_DRP(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_STATS_PKTS_DRP (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080000B0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RXX_UDD_SKP(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_RXX_UDD_SKP (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000048ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_RX_BP_DROPX(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 8;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 3) + ((block_id) & 7) * 0x200000ull) * 8;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_BP_DROPX (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000420ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
}
static inline uint64_t CVMX_GMXX_RX_BP_OFFX(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 8;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 3) + ((block_id) & 7) * 0x200000ull) * 8;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_BP_OFFX (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000460ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
}
static inline uint64_t CVMX_GMXX_RX_BP_ONX(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 3) + ((block_id) & 0) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 3) + ((block_id) & 1) * 0x1000000ull) * 8;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 8;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 3) + ((block_id) & 7) * 0x200000ull) * 8;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_BP_ONX (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000440ull) + (((offset) & 1) + ((block_id) & 0) * 0x1000000ull) * 8;
}
static inline uint64_t CVMX_GMXX_RX_HG2_STATUS(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000548ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000548ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000548ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_HG2_STATUS (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000548ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RX_PASS_EN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_GMXX_RX_PASS_EN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080005F8ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_GMXX_RX_PASS_EN(block_id) (CVMX_ADD_IO_SEG(0x00011800080005F8ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RX_PASS_MAPX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 15)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 15)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_RX_PASS_MAPX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000600ull) + (((offset) & 15) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_GMXX_RX_PASS_MAPX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000600ull) + (((offset) & 15) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
static inline uint64_t CVMX_GMXX_RX_PRTS(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000410ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000410ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000410ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_PRTS (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000410ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_RX_PRT_INFO(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_PRT_INFO (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004E8ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_RX_TX_STATUS(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((block_id == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_GMXX_RX_TX_STATUS(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080007E8ull);
}
#else
#define CVMX_GMXX_RX_TX_STATUS(block_id) (CVMX_ADD_IO_SEG(0x00011800080007E8ull))
#endif
static inline uint64_t CVMX_GMXX_RX_XAUI_BAD_COL(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000538ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000538ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000538ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_XAUI_BAD_COL (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000538ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_RX_XAUI_CTL(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000530ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000530ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000530ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_RX_XAUI_CTL (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000530ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_SMACX(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_SMACX (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000230ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_SOFT_BIST(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_SOFT_BIST (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080007E8ull) + ((block_id) & 7) * 0x1000000ull;
}
static inline uint64_t CVMX_GMXX_STAT_BP(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000520ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000520ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000520ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_STAT_BP (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000520ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TB_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TB_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080007E0ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TXX_APPEND(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_APPEND (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000218ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_BURST(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_BURST (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000228ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_CBFC_XOFF(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_CBFC_XOFF (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080005A0ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TXX_CBFC_XON(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset == 0)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_CBFC_XON (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080005C0ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TXX_CLK(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && (((offset <= 3)) && ((block_id <= 1)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 2)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 3)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_TXX_CLK(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000208ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
}
#else
#define CVMX_GMXX_TXX_CLK(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000208ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048)
#endif
static inline uint64_t CVMX_GMXX_TXX_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000270ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_MIN_PKT(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_MIN_PKT (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000240ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_PAUSE_PKT_INTERVAL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000248ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_PAUSE_PKT_TIME(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_PAUSE_PKT_TIME (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000238ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_PAUSE_TOGO(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_PAUSE_TOGO (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000258ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_PAUSE_ZERO(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_PAUSE_ZERO (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000260ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TXX_PIPE(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && (((offset <= 3)) && ((block_id <= 4))))))
		cvmx_warn("CVMX_GMXX_TXX_PIPE(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000310ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
}
#else
#define CVMX_GMXX_TXX_PIPE(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000310ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048)
#endif
static inline uint64_t CVMX_GMXX_TXX_SGMII_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000300ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000300ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000300ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000300ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_SGMII_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000300ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_SLOT(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_SLOT (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000220ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_SOFT_PAUSE(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_SOFT_PAUSE (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000250ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT0(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT0 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000280ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT1(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT1 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000288ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT2(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT2 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000290ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT3(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT3 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000298ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT4(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT4 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002A0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT5(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT5 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002A8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT6(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT6 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002B0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT7(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT7 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002B8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT8(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT8 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002C0ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STAT9(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STAT9 (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800080002C8ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_STATS_CTL(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_STATS_CTL (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000268ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TXX_THRESH(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 3) + ((block_id) & 0) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 3) + ((block_id) & 1) * 0x10000ull) * 2048;
			break;
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
			if (((offset <= 2)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 3) + ((block_id) & 0) * 0x0ull) * 2048;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 3) + ((block_id) & 7) * 0x2000ull) * 2048;
			break;
	}
	cvmx_warn("CVMX_GMXX_TXX_THRESH (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000210ull) + (((offset) & 1) + ((block_id) & 0) * 0x10000ull) * 2048;
}
static inline uint64_t CVMX_GMXX_TX_BP(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_BP (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004D0ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_CLK_MSKX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && (((offset <= 1)) && ((block_id == 0)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && (((offset <= 1)) && ((block_id == 0))))))
		cvmx_warn("CVMX_GMXX_TX_CLK_MSKX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000780ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8;
}
#else
#define CVMX_GMXX_TX_CLK_MSKX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000780ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8)
#endif
static inline uint64_t CVMX_GMXX_TX_COL_ATTEMPT(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000498ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000498ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000498ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_COL_ATTEMPT (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000498ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_CORRUPT(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_CORRUPT (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004D8ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_HG2_REG1(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000558ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000558ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000558ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_HG2_REG1 (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000558ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_HG2_REG2(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000560ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000560ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000560ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_HG2_REG2 (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000560ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_IFG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000488ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000488ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000488ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_IFG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000488ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_INT_EN(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000508ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000508ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000508ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_INT_EN (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000508ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_INT_REG(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000500ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000500ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000500ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_INT_REG (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000500ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_JAM(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000490ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000490ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000490ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_JAM (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000490ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_LFSR(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_LFSR (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004F8ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_OVR_BP(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_OVR_BP (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004C8ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_PAUSE_PKT_DMAC(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_PAUSE_PKT_DMAC (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004A0ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_PAUSE_PKT_TYPE(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_PAUSE_PKT_TYPE (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004A8ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_TX_PRTS(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CN30XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN50XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN31XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000480ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN38XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN58XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000480ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000480ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_PRTS (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000480ull) + ((block_id) & 0) * 0x8000000ull;
}
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_SPI_CTL(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_GMXX_TX_SPI_CTL(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004C0ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_GMXX_TX_SPI_CTL(block_id) (CVMX_ADD_IO_SEG(0x00011800080004C0ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_SPI_DRAIN(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_GMXX_TX_SPI_DRAIN(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004E0ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_GMXX_TX_SPI_DRAIN(block_id) (CVMX_ADD_IO_SEG(0x00011800080004E0ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_SPI_MAX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_GMXX_TX_SPI_MAX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004B0ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_GMXX_TX_SPI_MAX(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B0ull) + ((block_id) & 1) * 0x8000000ull)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_SPI_ROUNDX(unsigned long offset, unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && (((offset <= 31)) && ((block_id <= 1))))))
		cvmx_warn("CVMX_GMXX_TX_SPI_ROUNDX(%lu,%lu) is invalid on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8;
}
#else
#define CVMX_GMXX_TX_SPI_ROUNDX(offset, block_id) (CVMX_ADD_IO_SEG(0x0001180008000680ull) + (((offset) & 31) + ((block_id) & 1) * 0x1000000ull) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_GMXX_TX_SPI_THRESH(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((block_id <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((block_id <= 1)))))
		cvmx_warn("CVMX_GMXX_TX_SPI_THRESH(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800080004B8ull) + ((block_id) & 1) * 0x8000000ull;
}
#else
#define CVMX_GMXX_TX_SPI_THRESH(block_id) (CVMX_ADD_IO_SEG(0x00011800080004B8ull) + ((block_id) & 1) * 0x8000000ull)
#endif
static inline uint64_t CVMX_GMXX_TX_XAUI_CTL(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000528ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000528ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000528ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_TX_XAUI_CTL (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000528ull) + ((block_id) & 0) * 0x8000000ull;
}
static inline uint64_t CVMX_GMXX_XAUI_EXT_LOOPBACK(unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
			if ((block_id == 0))
				return CVMX_ADD_IO_SEG(0x0001180008000540ull) + ((block_id) & 0) * 0x8000000ull;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 1))
				return CVMX_ADD_IO_SEG(0x0001180008000540ull) + ((block_id) & 1) * 0x8000000ull;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if ((block_id <= 4))
				return CVMX_ADD_IO_SEG(0x0001180008000540ull) + ((block_id) & 7) * 0x1000000ull;
			break;
	}
	cvmx_warn("CVMX_GMXX_XAUI_EXT_LOOPBACK (block_id = %lu) not supported on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x0001180008000540ull) + ((block_id) & 0) * 0x8000000ull;
}

/**
 * cvmx_gmx#_bad_reg
 *
 * GMX_BAD_REG = A collection of things that have gone very, very wrong
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of INB_NXA, LOSTSTAT, OUT_OVR, are used.
 *
 */
union cvmx_gmxx_bad_reg {
	uint64_t u64;
	struct cvmx_gmxx_bad_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t inb_nxa                      : 4;  /**< Inbound port > GMX_RX_PRTS */
	uint64_t statovr                      : 1;  /**< TX Statistics overflow
                                                         The common FIFO to SGMII and XAUI had an overflow
                                                         TX Stats are corrupted */
	uint64_t loststat                     : 4;  /**< TX Statistics data was over-written
                                                         In SGMII, one bit per port
                                                         In XAUI, only port0 is used
                                                         TX Stats are corrupted */
	uint64_t reserved_18_21               : 4;
	uint64_t out_ovr                      : 16; /**< Outbound data FIFO overflow (per port) */
	uint64_t ncb_ovr                      : 1;  /**< Outbound NCB FIFO Overflow */
	uint64_t out_col                      : 1;  /**< Outbound collision occured between PKO and NCB */
#else
	uint64_t out_col                      : 1;
	uint64_t ncb_ovr                      : 1;
	uint64_t out_ovr                      : 16;
	uint64_t reserved_18_21               : 4;
	uint64_t loststat                     : 4;
	uint64_t statovr                      : 1;
	uint64_t inb_nxa                      : 4;
	uint64_t reserved_31_63               : 33;
#endif
	} s;
	struct cvmx_gmxx_bad_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t inb_nxa                      : 4;  /**< Inbound port > GMX_RX_PRTS */
	uint64_t statovr                      : 1;  /**< TX Statistics overflow */
	uint64_t reserved_25_25               : 1;
	uint64_t loststat                     : 3;  /**< TX Statistics data was over-written (per RGM port)
                                                         TX Stats are corrupted */
	uint64_t reserved_5_21                : 17;
	uint64_t out_ovr                      : 3;  /**< Outbound data FIFO overflow (per port) */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t out_ovr                      : 3;
	uint64_t reserved_5_21                : 17;
	uint64_t loststat                     : 3;
	uint64_t reserved_25_25               : 1;
	uint64_t statovr                      : 1;
	uint64_t inb_nxa                      : 4;
	uint64_t reserved_31_63               : 33;
#endif
	} cn30xx;
	struct cvmx_gmxx_bad_reg_cn30xx       cn31xx;
	struct cvmx_gmxx_bad_reg_s            cn38xx;
	struct cvmx_gmxx_bad_reg_s            cn38xxp2;
	struct cvmx_gmxx_bad_reg_cn30xx       cn50xx;
	struct cvmx_gmxx_bad_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_31_63               : 33;
	uint64_t inb_nxa                      : 4;  /**< Inbound port > GMX_RX_PRTS */
	uint64_t statovr                      : 1;  /**< TX Statistics overflow
                                                         The common FIFO to SGMII and XAUI had an overflow
                                                         TX Stats are corrupted */
	uint64_t loststat                     : 4;  /**< TX Statistics data was over-written
                                                         In SGMII, one bit per port
                                                         In XAUI, only port0 is used
                                                         TX Stats are corrupted */
	uint64_t reserved_6_21                : 16;
	uint64_t out_ovr                      : 4;  /**< Outbound data FIFO overflow (per port) */
	uint64_t reserved_0_1                 : 2;
#else
	uint64_t reserved_0_1                 : 2;
	uint64_t out_ovr                      : 4;
	uint64_t reserved_6_21                : 16;
	uint64_t loststat                     : 4;
	uint64_t statovr                      : 1;
	uint64_t inb_nxa                      : 4;
	uint64_t reserved_31_63               : 33;
#endif
	} cn52xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn52xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx       cn56xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn56xxp1;
	struct cvmx_gmxx_bad_reg_s            cn58xx;
	struct cvmx_gmxx_bad_reg_s            cn58xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx       cn61xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn63xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn63xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx       cn66xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn68xx;
	struct cvmx_gmxx_bad_reg_cn52xx       cn68xxp1;
	struct cvmx_gmxx_bad_reg_cn52xx       cnf71xx;
};
typedef union cvmx_gmxx_bad_reg cvmx_gmxx_bad_reg_t;

/**
 * cvmx_gmx#_bist
 *
 * GMX_BIST = GMX BIST Results
 *
 */
union cvmx_gmxx_bist {
	uint64_t u64;
	struct cvmx_gmxx_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t status                       : 25; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.fif_bnk0
                                                         - 1: gmx#.inb.fif_bnk1
                                                         - 2: gmx#.inb.fif_bnk2
                                                         - 3: gmx#.inb.fif_bnk3
                                                         - 4: gmx#.inb.fif_bnk_ext0
                                                         - 5: gmx#.inb.fif_bnk_ext1
                                                         - 6: gmx#.inb.fif_bnk_ext2
                                                         - 7: gmx#.inb.fif_bnk_ext3
                                                         - 8: gmx#.outb.fif.fif_bnk0
                                                         - 9: gmx#.outb.fif.fif_bnk1
                                                         - 10: gmx#.outb.fif.fif_bnk2
                                                         - 11: gmx#.outb.fif.fif_bnk3
                                                         - 12: gmx#.outb.fif.fif_bnk_ext0
                                                         - 13: gmx#.outb.fif.fif_bnk_ext1
                                                         - 14: gmx#.outb.fif.fif_bnk_ext2
                                                         - 15: gmx#.outb.fif.fif_bnk_ext3
                                                         - 16: gmx#.csr.gmi0.srf8x64m1_bist
                                                         - 17: gmx#.csr.gmi1.srf8x64m1_bist
                                                         - 18: gmx#.csr.gmi2.srf8x64m1_bist
                                                         - 19: gmx#.csr.gmi3.srf8x64m1_bist
                                                         - 20: gmx#.csr.drf20x32m2_bist
                                                         - 21: gmx#.csr.drf20x48m2_bist
                                                         - 22: gmx#.outb.stat.drf16x27m1_bist
                                                         - 23: gmx#.outb.stat.drf40x64m1_bist
                                                         - 24: xgmii.tx.drf16x38m1_async_bist */
#else
	uint64_t status                       : 25;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_gmxx_bist_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t status                       : 10; /**< BIST Results.
                                                          HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.dpr512x78m4_bist
                                                         - 1: gmx#.outb.fif.dpr512x71m4_bist
                                                         - 2: gmx#.csr.gmi0.srf8x64m1_bist
                                                         - 3: gmx#.csr.gmi1.srf8x64m1_bist
                                                         - 4: gmx#.csr.gmi2.srf8x64m1_bist
                                                         - 5: 0
                                                         - 6: gmx#.csr.drf20x80m1_bist
                                                         - 7: gmx#.outb.stat.drf16x27m1_bist
                                                         - 8: gmx#.outb.stat.drf40x64m1_bist
                                                         - 9: 0 */
#else
	uint64_t status                       : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} cn30xx;
	struct cvmx_gmxx_bist_cn30xx          cn31xx;
	struct cvmx_gmxx_bist_cn30xx          cn38xx;
	struct cvmx_gmxx_bist_cn30xx          cn38xxp2;
	struct cvmx_gmxx_bist_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t status                       : 12; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails */
#else
	uint64_t status                       : 12;
	uint64_t reserved_12_63               : 52;
#endif
	} cn50xx;
	struct cvmx_gmxx_bist_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t status                       : 16; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.fif_bnk0
                                                         - 1: gmx#.inb.fif_bnk1
                                                         - 2: gmx#.inb.fif_bnk2
                                                         - 3: gmx#.inb.fif_bnk3
                                                         - 4: gmx#.outb.fif.fif_bnk0
                                                         - 5: gmx#.outb.fif.fif_bnk1
                                                         - 6: gmx#.outb.fif.fif_bnk2
                                                         - 7: gmx#.outb.fif.fif_bnk3
                                                         - 8: gmx#.csr.gmi0.srf8x64m1_bist
                                                         - 9: gmx#.csr.gmi1.srf8x64m1_bist
                                                         - 10: gmx#.csr.gmi2.srf8x64m1_bist
                                                         - 11: gmx#.csr.gmi3.srf8x64m1_bist
                                                         - 12: gmx#.csr.drf20x80m1_bist
                                                         - 13: gmx#.outb.stat.drf16x27m1_bist
                                                         - 14: gmx#.outb.stat.drf40x64m1_bist
                                                         - 15: xgmii.tx.drf16x38m1_async_bist */
#else
	uint64_t status                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} cn52xx;
	struct cvmx_gmxx_bist_cn52xx          cn52xxp1;
	struct cvmx_gmxx_bist_cn52xx          cn56xx;
	struct cvmx_gmxx_bist_cn52xx          cn56xxp1;
	struct cvmx_gmxx_bist_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t status                       : 17; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         - 0: gmx#.inb.fif_bnk0
                                                         - 1: gmx#.inb.fif_bnk1
                                                         - 2: gmx#.inb.fif_bnk2
                                                         - 3: gmx#.inb.fif_bnk3
                                                         - 4: gmx#.outb.fif.fif_bnk0
                                                         - 5: gmx#.outb.fif.fif_bnk1
                                                         - 6: gmx#.outb.fif.fif_bnk2
                                                         - 7: gmx#.outb.fif.fif_bnk3
                                                         - 8: gmx#.csr.gmi0.srf8x64m1_bist
                                                         - 9: gmx#.csr.gmi1.srf8x64m1_bist
                                                         - 10: gmx#.csr.gmi2.srf8x64m1_bist
                                                         - 11: gmx#.csr.gmi3.srf8x64m1_bist
                                                         - 12: gmx#.csr.drf20x80m1_bist
                                                         - 13: gmx#.outb.stat.drf16x27m1_bist
                                                         - 14: gmx#.outb.stat.drf40x64m1_bist
                                                         - 15: gmx#.outb.ncb.drf16x76m1_bist
                                                         - 16: gmx#.outb.fif.srf32x16m2_bist */
#else
	uint64_t status                       : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} cn58xx;
	struct cvmx_gmxx_bist_cn58xx          cn58xxp1;
	struct cvmx_gmxx_bist_s               cn61xx;
	struct cvmx_gmxx_bist_s               cn63xx;
	struct cvmx_gmxx_bist_s               cn63xxp1;
	struct cvmx_gmxx_bist_s               cn66xx;
	struct cvmx_gmxx_bist_s               cn68xx;
	struct cvmx_gmxx_bist_s               cn68xxp1;
	struct cvmx_gmxx_bist_s               cnf71xx;
};
typedef union cvmx_gmxx_bist cvmx_gmxx_bist_t;

/**
 * cvmx_gmx#_bpid_map#
 *
 * Notes:
 * GMX will build BPID_VECTOR<15:0> using the 16 GMX_BPID_MAP entries and the BPID
 * state from IPD.  In XAUI/RXAUI mode when PFC/CBFC/HiGig2 is used, the
 * BPID_VECTOR becomes the logical backpressure.  In XAUI/RXAUI mode when
 * PFC/CBFC/HiGig2 is not used or when in 4xSGMII mode, the BPID_VECTOR can be used
 * with the GMX_BPID_MSK register to determine the physical backpressure.
 *
 * In XAUI/RXAUI mode, the entire BPID_VECTOR<15:0> is available determining physical
 * backpressure for the single XAUI/RXAUI interface.
 *
 * In SGMII mode, BPID_VECTOR is broken up as follows:
 *    SGMII interface0 uses BPID_VECTOR<3:0>
 *    SGMII interface1 uses BPID_VECTOR<7:4>
 *    SGMII interface2 uses BPID_VECTOR<11:8>
 *    SGMII interface3 uses BPID_VECTOR<15:12>
 *
 * In all SGMII configurations, and in some XAUI/RXAUI configurations, the
 * interface protocols only support physical backpressure. In these cases, a single
 * BPID will commonly drive the physical backpressure for the physical
 * interface. We provide example programmings for these simple cases.
 *
 * In XAUI/RXAUI mode where PFC/CBFC/HiGig2 is not used, an example programming
 * would be as follows:
 *
 *    @verbatim
 *    GMX_BPID_MAP0[VAL]    = 1;
 *    GMX_BPID_MAP0[BPID]   = xaui_bpid;
 *    GMX_BPID_MSK[MSK_OR]  = 1;
 *    GMX_BPID_MSK[MSK_AND] = 0;
 *    @endverbatim
 *
 * In SGMII mode, an example programming would be as follows:
 *
 *    @verbatim
 *    for (i=0; i<4; i++) [
 *       if (GMX_PRTi_CFG[EN]) [
 *          GMX_BPID_MAP(i*4)[VAL]    = 1;
 *          GMX_BPID_MAP(i*4)[BPID]   = sgmii_bpid(i);
 *          GMX_BPID_MSK[MSK_OR]      = (1 << (i*4)) | GMX_BPID_MSK[MSK_OR];
 *       ]
 *    ]
 *    GMX_BPID_MSK[MSK_AND] = 0;
 *    @endverbatim
 */
union cvmx_gmxx_bpid_mapx {
	uint64_t u64;
	struct cvmx_gmxx_bpid_mapx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t status                       : 1;  /**< Current received BP from IPD */
	uint64_t reserved_9_15                : 7;
	uint64_t val                          : 1;  /**< Table entry is valid */
	uint64_t reserved_6_7                 : 2;
	uint64_t bpid                         : 6;  /**< Backpressure ID the entry maps to */
#else
	uint64_t bpid                         : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t val                          : 1;
	uint64_t reserved_9_15                : 7;
	uint64_t status                       : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_gmxx_bpid_mapx_s          cn68xx;
	struct cvmx_gmxx_bpid_mapx_s          cn68xxp1;
};
typedef union cvmx_gmxx_bpid_mapx cvmx_gmxx_bpid_mapx_t;

/**
 * cvmx_gmx#_bpid_msk
 */
union cvmx_gmxx_bpid_msk {
	uint64_t u64;
	struct cvmx_gmxx_bpid_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t msk_or                       : 16; /**< Assert physical BP when the backpressure ID vector
                                                         combined with MSK_OR indicates BP as follows.
                                                         phys_bp_msk_or =
                                                          (BPID_VECTOR<x:y> & MSK_OR<x:y>) != 0
                                                         phys_bp = phys_bp_msk_or || phys_bp_msk_and
                                                         In XAUI/RXAUI mode, x=15, y=0
                                                         In SGMII mode, x/y are set depending on the SGMII
                                                         interface.
                                                         SGMII interface0, x=3,  y=0
                                                         SGMII interface1, x=7,  y=4
                                                         SGMII interface2, x=11, y=8
                                                         SGMII interface3, x=15, y=12 */
	uint64_t reserved_16_31               : 16;
	uint64_t msk_and                      : 16; /**< Assert physical BP when the backpressure ID vector
                                                         combined with MSK_AND indicates BP as follows.
                                                         phys_bp_msk_and =
                                                          (BPID_VECTOR<x:y> & MSK_AND<x:y>) == MSK_AND<x:y>
                                                         phys_bp = phys_bp_msk_or || phys_bp_msk_and
                                                         In XAUI/RXAUI mode, x=15, y=0
                                                         In SGMII mode, x/y are set depending on the SGMII
                                                         interface.
                                                         SGMII interface0, x=3,  y=0
                                                         SGMII interface1, x=7,  y=4
                                                         SGMII interface2, x=11, y=8
                                                         SGMII interface3, x=15, y=12 */
#else
	uint64_t msk_and                      : 16;
	uint64_t reserved_16_31               : 16;
	uint64_t msk_or                       : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_bpid_msk_s           cn68xx;
	struct cvmx_gmxx_bpid_msk_s           cn68xxp1;
};
typedef union cvmx_gmxx_bpid_msk cvmx_gmxx_bpid_msk_t;

/**
 * cvmx_gmx#_clk_en
 *
 * DON'T PUT IN HRM*
 *
 */
union cvmx_gmxx_clk_en {
	uint64_t u64;
	struct cvmx_gmxx_clk_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t clk_en                       : 1;  /**< Force the clock enables on */
#else
	uint64_t clk_en                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_clk_en_s             cn52xx;
	struct cvmx_gmxx_clk_en_s             cn52xxp1;
	struct cvmx_gmxx_clk_en_s             cn56xx;
	struct cvmx_gmxx_clk_en_s             cn56xxp1;
	struct cvmx_gmxx_clk_en_s             cn61xx;
	struct cvmx_gmxx_clk_en_s             cn63xx;
	struct cvmx_gmxx_clk_en_s             cn63xxp1;
	struct cvmx_gmxx_clk_en_s             cn66xx;
	struct cvmx_gmxx_clk_en_s             cn68xx;
	struct cvmx_gmxx_clk_en_s             cn68xxp1;
	struct cvmx_gmxx_clk_en_s             cnf71xx;
};
typedef union cvmx_gmxx_clk_en cvmx_gmxx_clk_en_t;

/**
 * cvmx_gmx#_ebp_dis
 */
union cvmx_gmxx_ebp_dis {
	uint64_t u64;
	struct cvmx_gmxx_ebp_dis_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t dis                          : 16; /**< BP channel disable
                                                         GMX has the ability to remap unused channels
                                                         in order to get down to GMX_TX_PIPE[NUMP]
                                                         channels. */
#else
	uint64_t dis                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_ebp_dis_s            cn68xx;
	struct cvmx_gmxx_ebp_dis_s            cn68xxp1;
};
typedef union cvmx_gmxx_ebp_dis cvmx_gmxx_ebp_dis_t;

/**
 * cvmx_gmx#_ebp_msk
 */
union cvmx_gmxx_ebp_msk {
	uint64_t u64;
	struct cvmx_gmxx_ebp_msk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t msk                          : 16; /**< BP channel mask
                                                         GMX can completely ignore the channel BP for
                                                         channels specified by the MSK field.  Any channel
                                                         in which MSK == 1, will never send BP information
                                                         to PKO. */
#else
	uint64_t msk                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_ebp_msk_s            cn68xx;
	struct cvmx_gmxx_ebp_msk_s            cn68xxp1;
};
typedef union cvmx_gmxx_ebp_msk cvmx_gmxx_ebp_msk_t;

/**
 * cvmx_gmx#_hg2_control
 *
 * Notes:
 * The HiGig2 TX and RX enable would normally be both set together for HiGig2 messaging. However
 * setting just the TX or RX bit will result in only the HG2 message transmit or the receive
 * capability.
 * PHYS_EN and LOGL_EN bits when 1, allow link pause or back pressure to PKO as per received
 * HiGig2 message. When 0, link pause and back pressure to PKO in response to received messages
 * are disabled.
 *
 * GMX*_TX_XAUI_CTL[HG_EN] must be set to one(to enable HiGig) whenever either HG2TX_EN or HG2RX_EN
 * are set.
 *
 * GMX*_RX0_UDD_SKP[LEN] must be set to 16 (to select HiGig2) whenever either HG2TX_EN or HG2RX_EN
 * are set.
 *
 * GMX*_TX_OVR_BP[EN<0>] must be set to one and GMX*_TX_OVR_BP[BP<0>] must be cleared to zero
 * (to forcibly disable HW-automatic 802.3 pause packet generation) with the HiGig2 Protocol when
 * GMX*_HG2_CONTROL[HG2TX_EN]=0. (The HiGig2 protocol is indicated by GMX*_TX_XAUI_CTL[HG_EN]=1
 * and GMX*_RX0_UDD_SKP[LEN]=16.) The HW can only auto-generate backpressure via HiGig2 messages
 * (optionally, when HG2TX_EN=1) with the HiGig2 protocol.
 */
union cvmx_gmxx_hg2_control {
	uint64_t u64;
	struct cvmx_gmxx_hg2_control_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t hg2tx_en                     : 1;  /**< Enable Transmission of HG2 phys and logl messages
                                                         When set, also disables HW auto-generated (802.3
                                                         and CBFC) pause frames. (OCTEON cannot generate
                                                         proper 802.3 or CBFC pause frames in HiGig2 mode.) */
	uint64_t hg2rx_en                     : 1;  /**< Enable extraction and processing of HG2 message
                                                         packet from RX flow. Physical logical pause info
                                                         is used to pause physical link, back pressure PKO
                                                         HG2RX_EN must be set when HiGig2 messages are
                                                         present in the receive stream. */
	uint64_t phys_en                      : 1;  /**< 1 bit physical link pause enable for recevied
                                                         HiGig2 physical pause message */
	uint64_t logl_en                      : 16; /**< 16 bit xof enables for recevied HiGig2 messages
                                                         or CBFC packets */
#else
	uint64_t logl_en                      : 16;
	uint64_t phys_en                      : 1;
	uint64_t hg2rx_en                     : 1;
	uint64_t hg2tx_en                     : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_gmxx_hg2_control_s        cn52xx;
	struct cvmx_gmxx_hg2_control_s        cn52xxp1;
	struct cvmx_gmxx_hg2_control_s        cn56xx;
	struct cvmx_gmxx_hg2_control_s        cn61xx;
	struct cvmx_gmxx_hg2_control_s        cn63xx;
	struct cvmx_gmxx_hg2_control_s        cn63xxp1;
	struct cvmx_gmxx_hg2_control_s        cn66xx;
	struct cvmx_gmxx_hg2_control_s        cn68xx;
	struct cvmx_gmxx_hg2_control_s        cn68xxp1;
	struct cvmx_gmxx_hg2_control_s        cnf71xx;
};
typedef union cvmx_gmxx_hg2_control cvmx_gmxx_hg2_control_t;

/**
 * cvmx_gmx#_inf_mode
 *
 * GMX_INF_MODE = Interface Mode
 *
 */
union cvmx_gmxx_inf_mode {
	uint64_t u64;
	struct cvmx_gmxx_inf_mode_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t rate                         : 4;  /**< SERDES speed rate
                                                         reset value is based on the QLM speed select
                                                         0 = 1.25  Gbaud
                                                         1 = 3.125 Gbaud
                                                         (only valid for GMX0 instance)
                                                         Software must not change RATE from its reset value */
	uint64_t reserved_12_15               : 4;
	uint64_t speed                        : 4;  /**< Interface Speed
                                                         QLM speed pins  which select reference clock
                                                         period and interface data rate.  If the QLM PLL
                                                         inputs are correct, the speed setting correspond
                                                         to the following data rates (in Gbaud).
                                                         0  = 5
                                                         1  = 2.5
                                                         2  = 2.5
                                                         3  = 1.25
                                                         4  = 1.25
                                                         5  = 6.25
                                                         6  = 5
                                                         7  = 2.5
                                                         8  = 3.125
                                                         9  = 2.5
                                                         10 = 1.25
                                                         11 = 5
                                                         12 = 6.25
                                                         13 = 3.75
                                                         14 = 3.125
                                                         15 = QLM disabled */
	uint64_t reserved_7_7                 : 1;
	uint64_t mode                         : 3;  /**< Interface Electrical Operating Mode
                                                         - 0: SGMII (v1.8)
                                                         - 1: XAUI (IEEE 802.3-2005) */
	uint64_t reserved_3_3                 : 1;
	uint64_t p0mii                        : 1;  /**< Port 0 Interface Mode
                                                         - 0: Port 0 is RGMII
                                                         - 1: Port 0 is MII */
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Protocol Type
                                                         - 0: SGMII/1000Base-X
                                                         - 1: XAUI */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t p0mii                        : 1;
	uint64_t reserved_3_3                 : 1;
	uint64_t mode                         : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t speed                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t rate                         : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_gmxx_inf_mode_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t p0mii                        : 1;  /**< Port 0 Interface Mode
                                                         - 0: Port 0 is RGMII
                                                         - 1: Port 0 is MII */
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Port 1/2 Interface Mode
                                                         - 0: Ports 1 and 2 are RGMII
                                                         - 1: Port  1 is GMII/MII, Port 2 is unused
                                                             GMII/MII is selected by GMX_PRT1_CFG[SPEED] */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t p0mii                        : 1;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_gmxx_inf_mode_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Mode
                                                         - 0: All three ports are RGMII ports
                                                         - 1: prt0 is RGMII, prt1 is GMII, and prt2 is unused */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} cn31xx;
	struct cvmx_gmxx_inf_mode_cn31xx      cn38xx;
	struct cvmx_gmxx_inf_mode_cn31xx      cn38xxp2;
	struct cvmx_gmxx_inf_mode_cn30xx      cn50xx;
	struct cvmx_gmxx_inf_mode_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t speed                        : 2;  /**< Interface Speed
                                                         - 0: 1.250GHz
                                                         - 1: 2.500GHz
                                                         - 2: 3.125GHz
                                                         - 3: 3.750GHz */
	uint64_t reserved_6_7                 : 2;
	uint64_t mode                         : 2;  /**< Interface Electrical Operating Mode
                                                         - 0: Disabled (PCIe)
                                                         - 1: XAUI (IEEE 802.3-2005)
                                                         - 2: SGMII (v1.8)
                                                         - 3: PICMG3.1 */
	uint64_t reserved_2_3                 : 2;
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Protocol Type
                                                         - 0: SGMII/1000Base-X
                                                         - 1: XAUI */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t mode                         : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t speed                        : 2;
	uint64_t reserved_10_63               : 54;
#endif
	} cn52xx;
	struct cvmx_gmxx_inf_mode_cn52xx      cn52xxp1;
	struct cvmx_gmxx_inf_mode_cn52xx      cn56xx;
	struct cvmx_gmxx_inf_mode_cn52xx      cn56xxp1;
	struct cvmx_gmxx_inf_mode_cn31xx      cn58xx;
	struct cvmx_gmxx_inf_mode_cn31xx      cn58xxp1;
	struct cvmx_gmxx_inf_mode_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t speed                        : 4;  /**< Interface Speed
                                                         QLM speed pins  which select reference clock
                                                         period and interface data rate.  If the QLM PLL
                                                         inputs are correct, the speed setting correspond
                                                         to the following data rates (in Gbaud).
                                                         0  = 5
                                                         1  = 2.5
                                                         2  = 2.5
                                                         3  = 1.25
                                                         4  = 1.25
                                                         5  = 6.25
                                                         6  = 5
                                                         7  = 2.5
                                                         8  = 3.125
                                                         9  = 2.5
                                                         10 = 1.25
                                                         11 = 5
                                                         12 = 6.25
                                                         13 = 3.75
                                                         14 = 3.125
                                                         15 = QLM disabled */
	uint64_t reserved_5_7                 : 3;
	uint64_t mode                         : 1;  /**< Interface Electrical Operating Mode
                                                         - 0: SGMII (v1.8)
                                                         - 1: XAUI (IEEE 802.3-2005) */
	uint64_t reserved_2_3                 : 2;
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Protocol Type
                                                         - 0: SGMII/1000Base-X
                                                         - 1: XAUI */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t mode                         : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t speed                        : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} cn61xx;
	struct cvmx_gmxx_inf_mode_cn61xx      cn63xx;
	struct cvmx_gmxx_inf_mode_cn61xx      cn63xxp1;
	struct cvmx_gmxx_inf_mode_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t rate                         : 4;  /**< SERDES speed rate
                                                         reset value is based on the QLM speed select
                                                         0 = 1.25  Gbaud
                                                         1 = 3.125 Gbaud
                                                         (only valid for GMX0 instance)
                                                         Software must not change RATE from its reset value */
	uint64_t reserved_12_15               : 4;
	uint64_t speed                        : 4;  /**< Interface Speed
                                                         QLM speed pins  which select reference clock
                                                         period and interface data rate.  If the QLM PLL
                                                         inputs are correct, the speed setting correspond
                                                         to the following data rates (in Gbaud).
                                                         0  = 5
                                                         1  = 2.5
                                                         2  = 2.5
                                                         3  = 1.25
                                                         4  = 1.25
                                                         5  = 6.25
                                                         6  = 5
                                                         7  = 2.5
                                                         8  = 3.125
                                                         9  = 2.5
                                                         10 = 1.25
                                                         11 = 5
                                                         12 = 6.25
                                                         13 = 3.75
                                                         14 = 3.125
                                                         15 = QLM disabled */
	uint64_t reserved_5_7                 : 3;
	uint64_t mode                         : 1;  /**< Interface Electrical Operating Mode
                                                         - 0: SGMII (v1.8)
                                                         - 1: XAUI (IEEE 802.3-2005) */
	uint64_t reserved_2_3                 : 2;
	uint64_t en                           : 1;  /**< Interface Enable
                                                         Must be set to enable the packet interface.
                                                         Should be enabled before any other requests to
                                                         GMX including enabling port back pressure with
                                                         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Protocol Type
                                                         - 0: SGMII/1000Base-X
                                                         - 1: XAUI */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t mode                         : 1;
	uint64_t reserved_5_7                 : 3;
	uint64_t speed                        : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t rate                         : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn66xx;
	struct cvmx_gmxx_inf_mode_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t speed                        : 4;  /**< Interface Speed
                                                         QLM speed pins  which select reference clock
                                                         period and interface data rate.  If the QLM PLL
                                                         inputs are correct, the speed setting correspond
                                                         to the following data rates (in Gbaud).
                                                         0  = 5
                                                         1  = 2.5
                                                         2  = 2.5
                                                         3  = 1.25
                                                         4  = 1.25
                                                         5  = 6.25
                                                         6  = 5
                                                         7  = 2.5
                                                         8  = 3.125
                                                         9  = 2.5
                                                         10 = 1.25
                                                         11 = 5
                                                         12 = 6.25
                                                         13 = 3.75
                                                         14 = 3.125
                                                         15 = QLM disabled */
	uint64_t reserved_7_7                 : 1;
	uint64_t mode                         : 3;  /**< Interface Electrical Operating Mode
                                                         - 0: Reserved
                                                         - 1: Reserved
                                                         - 2: SGMII (v1.8)
                                                         - 3: XAUI (IEEE 802.3-2005)
                                                         - 4: Reserved
                                                         - 5: Reserved
                                                         - 6: Reserved
                                                         - 7: RXAUI */
	uint64_t reserved_2_3                 : 2;
	uint64_t en                           : 1;  /**< Interface Enable
                                                                   Must be set to enable the packet interface.
                                                                   Should be enabled before any other requests to
                                                                   GMX including enabling port back pressure with
                                                         b         IPD_CTL_STATUS[PBP_EN] */
	uint64_t type                         : 1;  /**< Interface Protocol Type
                                                         - 0: SGMII/1000Base-X
                                                         - 1: XAUI/RXAUI */
#else
	uint64_t type                         : 1;
	uint64_t en                           : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t mode                         : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t speed                        : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} cn68xx;
	struct cvmx_gmxx_inf_mode_cn68xx      cn68xxp1;
	struct cvmx_gmxx_inf_mode_cn61xx      cnf71xx;
};
typedef union cvmx_gmxx_inf_mode cvmx_gmxx_inf_mode_t;

/**
 * cvmx_gmx#_nxa_adr
 *
 * GMX_NXA_ADR = NXA Port Address
 *
 */
union cvmx_gmxx_nxa_adr {
	uint64_t u64;
	struct cvmx_gmxx_nxa_adr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t pipe                         : 7;  /**< Logged pipe for NXP exceptions */
	uint64_t reserved_6_15                : 10;
	uint64_t prt                          : 6;  /**< Logged address for NXA exceptions
                                                         The logged address will be from the first
                                                         exception that caused the problem.  NCB has
                                                         higher priority than PKO and will win.
                                                         (only PRT[3:0]) */
#else
	uint64_t prt                          : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t pipe                         : 7;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_gmxx_nxa_adr_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t prt                          : 6;  /**< Logged address for NXA exceptions
                                                         The logged address will be from the first
                                                         exception that caused the problem.  NCB has
                                                         higher priority than PKO and will win. */
#else
	uint64_t prt                          : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} cn30xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn31xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn38xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn38xxp2;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn50xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn52xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn52xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn56xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn56xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn58xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn58xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn61xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn63xx;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn63xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx       cn66xx;
	struct cvmx_gmxx_nxa_adr_s            cn68xx;
	struct cvmx_gmxx_nxa_adr_s            cn68xxp1;
	struct cvmx_gmxx_nxa_adr_cn30xx       cnf71xx;
};
typedef union cvmx_gmxx_nxa_adr cvmx_gmxx_nxa_adr_t;

/**
 * cvmx_gmx#_pipe_status
 *
 * DON'T PUT IN HRM*
 *
 */
union cvmx_gmxx_pipe_status {
	uint64_t u64;
	struct cvmx_gmxx_pipe_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t ovr                          : 4;  /**< Pipe credit return FIFO has overflowed. */
	uint64_t reserved_12_15               : 4;
	uint64_t bp                           : 4;  /**< Pipe credit return FIFO has filled up and asserted
                                                         backpressure to the datapath. */
	uint64_t reserved_4_7                 : 4;
	uint64_t stop                         : 4;  /**< PKO has asserted backpressure on the pipe credit
                                                         return interface. */
#else
	uint64_t stop                         : 4;
	uint64_t reserved_4_7                 : 4;
	uint64_t bp                           : 4;
	uint64_t reserved_12_15               : 4;
	uint64_t ovr                          : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} s;
	struct cvmx_gmxx_pipe_status_s        cn68xx;
	struct cvmx_gmxx_pipe_status_s        cn68xxp1;
};
typedef union cvmx_gmxx_pipe_status cvmx_gmxx_pipe_status_t;

/**
 * cvmx_gmx#_prt#_cbfc_ctl
 *
 * ** HG2 message CSRs end
 *
 *
 * Notes:
 * XOFF for a specific port is XOFF<prt> = (PHYS_EN<prt> & PHYS_BP) | (LOGL_EN<prt> & LOGL_BP<prt>)
 *
 */
union cvmx_gmxx_prtx_cbfc_ctl {
	uint64_t u64;
	struct cvmx_gmxx_prtx_cbfc_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t phys_en                      : 16; /**< Determines which ports will have physical
                                                         backpressure pause packets.
                                                         The value pplaced in the Class Enable Vector
                                                         field of the CBFC pause packet will be
                                                         PHYS_EN | LOGL_EN */
	uint64_t logl_en                      : 16; /**< Determines which ports will have logical
                                                         backpressure pause packets.
                                                         The value pplaced in the Class Enable Vector
                                                         field of the CBFC pause packet will be
                                                         PHYS_EN | LOGL_EN */
	uint64_t phys_bp                      : 16; /**< When RX_EN is set and the HW is backpressuring any
                                                         ports (from either CBFC pause packets or the
                                                         GMX_TX_OVR_BP[TX_PRT_BP] register) and all ports
                                                         indiciated by PHYS_BP are backpressured, simulate
                                                         physical backpressure by defering all packets on
                                                         the transmitter. */
	uint64_t reserved_4_15                : 12;
	uint64_t bck_en                       : 1;  /**< Forward CBFC Pause information to BP block */
	uint64_t drp_en                       : 1;  /**< Drop Control CBFC Pause Frames */
	uint64_t tx_en                        : 1;  /**< When set, allow for CBFC Pause Packets
                                                         Must be clear in HiGig2 mode i.e. when
                                                         GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                         GMX_RX_UDD_SKP[SKIP]=16. */
	uint64_t rx_en                        : 1;  /**< When set, allow for CBFC Pause Packets
                                                         Must be clear in HiGig2 mode i.e. when
                                                         GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                         GMX_RX_UDD_SKP[SKIP]=16. */
#else
	uint64_t rx_en                        : 1;
	uint64_t tx_en                        : 1;
	uint64_t drp_en                       : 1;
	uint64_t bck_en                       : 1;
	uint64_t reserved_4_15                : 12;
	uint64_t phys_bp                      : 16;
	uint64_t logl_en                      : 16;
	uint64_t phys_en                      : 16;
#endif
	} s;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn52xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn56xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn61xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn63xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn63xxp1;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn66xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn68xx;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cn68xxp1;
	struct cvmx_gmxx_prtx_cbfc_ctl_s      cnf71xx;
};
typedef union cvmx_gmxx_prtx_cbfc_ctl cvmx_gmxx_prtx_cbfc_ctl_t;

/**
 * cvmx_gmx#_prt#_cfg
 *
 * GMX_PRT_CFG = Port description
 *
 */
union cvmx_gmxx_prtx_cfg {
	uint64_t u64;
	struct cvmx_gmxx_prtx_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t pknd                         : 6;  /**< Port Kind used for processing the packet by PKI */
	uint64_t reserved_14_15               : 2;
	uint64_t tx_idle                      : 1;  /**< TX Machine is idle */
	uint64_t rx_idle                      : 1;  /**< RX Machine is idle */
	uint64_t reserved_9_11                : 3;
	uint64_t speed_msb                    : 1;  /**< Link Speed MSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t slottime                     : 1;  /**< Slot Time for Half-Duplex operation
                                                         0 = 512 bitimes (10/100Mbs operation)
                                                         1 = 4096 bitimes (1000Mbs operation)
                                                         (SGMII/1000Base-X only) */
	uint64_t duplex                       : 1;  /**< Duplex
                                                         0 = Half Duplex (collisions/extentions/bursts)
                                                         1 = Full Duplex
                                                         (SGMII/1000Base-X only) */
	uint64_t speed                        : 1;  /**< Link Speed LSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved
                                                         (SGMII/1000Base-X only) */
	uint64_t en                           : 1;  /**< Link Enable
                                                         When EN is clear, packets will not be received
                                                         or transmitted (including PAUSE and JAM packets).
                                                         If EN is cleared while a packet is currently
                                                         being received or transmitted, the packet will
                                                         be allowed to complete before the bus is idled.
                                                         On the RX side, subsequent packets in a burst
                                                         will be ignored. */
#else
	uint64_t en                           : 1;
	uint64_t speed                        : 1;
	uint64_t duplex                       : 1;
	uint64_t slottime                     : 1;
	uint64_t reserved_4_7                 : 4;
	uint64_t speed_msb                    : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t rx_idle                      : 1;
	uint64_t tx_idle                      : 1;
	uint64_t reserved_14_15               : 2;
	uint64_t pknd                         : 6;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_gmxx_prtx_cfg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t slottime                     : 1;  /**< Slot Time for Half-Duplex operation
                                                         0 = 512 bitimes (10/100Mbs operation)
                                                         1 = 4096 bitimes (1000Mbs operation) */
	uint64_t duplex                       : 1;  /**< Duplex
                                                         0 = Half Duplex (collisions/extentions/bursts)
                                                         1 = Full Duplex */
	uint64_t speed                        : 1;  /**< Link Speed
                                                         0 = 10/100Mbs operation
                                                             (in RGMII mode, GMX_TX_CLK[CLK_CNT] >  1)
                                                             (in MII   mode, GMX_TX_CLK[CLK_CNT] == 1)
                                                         1 = 1000Mbs operation */
	uint64_t en                           : 1;  /**< Link Enable
                                                         When EN is clear, packets will not be received
                                                         or transmitted (including PAUSE and JAM packets).
                                                         If EN is cleared while a packet is currently
                                                         being received or transmitted, the packet will
                                                         be allowed to complete before the bus is idled.
                                                         On the RX side, subsequent packets in a burst
                                                         will be ignored. */
#else
	uint64_t en                           : 1;
	uint64_t speed                        : 1;
	uint64_t duplex                       : 1;
	uint64_t slottime                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} cn30xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn31xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn38xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn38xxp2;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn50xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_14_63               : 50;
	uint64_t tx_idle                      : 1;  /**< TX Machine is idle */
	uint64_t rx_idle                      : 1;  /**< RX Machine is idle */
	uint64_t reserved_9_11                : 3;
	uint64_t speed_msb                    : 1;  /**< Link Speed MSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t slottime                     : 1;  /**< Slot Time for Half-Duplex operation
                                                         0 = 512 bitimes (10/100Mbs operation)
                                                         1 = 4096 bitimes (1000Mbs operation)
                                                         (SGMII/1000Base-X only) */
	uint64_t duplex                       : 1;  /**< Duplex
                                                         0 = Half Duplex (collisions/extentions/bursts)
                                                         1 = Full Duplex
                                                         (SGMII/1000Base-X only) */
	uint64_t speed                        : 1;  /**< Link Speed LSB [SPEED_MSB:SPEED]
                                                         10 = 10Mbs operation
                                                         00 = 100Mbs operation
                                                         01 = 1000Mbs operation
                                                         11 = Reserved
                                                         (SGMII/1000Base-X only) */
	uint64_t en                           : 1;  /**< Link Enable
                                                         When EN is clear, packets will not be received
                                                         or transmitted (including PAUSE and JAM packets).
                                                         If EN is cleared while a packet is currently
                                                         being received or transmitted, the packet will
                                                         be allowed to complete before the bus is idled.
                                                         On the RX side, subsequent packets in a burst
                                                         will be ignored. */
#else
	uint64_t en                           : 1;
	uint64_t speed                        : 1;
	uint64_t duplex                       : 1;
	uint64_t slottime                     : 1;
	uint64_t reserved_4_7                 : 4;
	uint64_t speed_msb                    : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t rx_idle                      : 1;
	uint64_t tx_idle                      : 1;
	uint64_t reserved_14_63               : 50;
#endif
	} cn52xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn52xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn56xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn56xxp1;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn58xx;
	struct cvmx_gmxx_prtx_cfg_cn30xx      cn58xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn61xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn63xx;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn63xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cn66xx;
	struct cvmx_gmxx_prtx_cfg_s           cn68xx;
	struct cvmx_gmxx_prtx_cfg_s           cn68xxp1;
	struct cvmx_gmxx_prtx_cfg_cn52xx      cnf71xx;
};
typedef union cvmx_gmxx_prtx_cfg cvmx_gmxx_prtx_cfg_t;

/**
 * cvmx_gmx#_rx#_adr_cam0
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam0 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam0_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam0_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam0 cvmx_gmxx_rxx_adr_cam0_t;

/**
 * cvmx_gmx#_rx#_adr_cam1
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam1 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam1_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam1_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam1 cvmx_gmxx_rxx_adr_cam1_t;

/**
 * cvmx_gmx#_rx#_adr_cam2
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam2 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam2_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam2_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam2 cvmx_gmxx_rxx_adr_cam2_t;

/**
 * cvmx_gmx#_rx#_adr_cam3
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam3 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam3_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam3_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam3 cvmx_gmxx_rxx_adr_cam3_t;

/**
 * cvmx_gmx#_rx#_adr_cam4
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam4 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam4_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam4_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam4 cvmx_gmxx_rxx_adr_cam4_t;

/**
 * cvmx_gmx#_rx#_adr_cam5
 *
 * GMX_RX_ADR_CAM = Address Filtering Control
 *
 */
union cvmx_gmxx_rxx_adr_cam5 {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t adr                          : 64; /**< The DMAC address to match on

                                                         Each entry contributes 8bits to one of 8 matchers.
                                                         The CAM matches against unicst or multicst DMAC
                                                         addresses.

                                                         ALL GMX_RX[0..3]_ADR_CAM[0..5] CSRs may be used
                                                         in either SGMII or XAUI mode such that any GMX
                                                         MAC can use any of the 32 common DMAC entries.

                                                         GMX_RX[1..3]_ADR_CAM[0..5] are the only non-port0
                                                         registers used in XAUI mode. */
#else
	uint64_t adr                          : 64;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn30xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn31xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn38xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn50xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn52xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn56xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn58xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn61xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn63xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn66xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn68xx;
	struct cvmx_gmxx_rxx_adr_cam5_s       cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam5_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam5 cvmx_gmxx_rxx_adr_cam5_t;

/**
 * cvmx_gmx#_rx#_adr_cam_all_en
 *
 * GMX_RX_ADR_CAM_ALL_EN = Address Filtering Control Enable
 *
 */
union cvmx_gmxx_rxx_adr_cam_all_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t en                           : 32; /**< CAM Entry Enables

                                                         GMX has 32 DMAC entries that can be accessed with
                                                         the GMX_RX[0..3]_ADR_CAM[0..5] CSRs.
                                                         These 32 DMAC entries can be used by any of the
                                                         four SGMII MACs or the XAUI MAC.

                                                         Each port interface has independent control of
                                                         which of the 32 DMAC entries to include in the
                                                         CAM lookup.

                                                         GMX_RXx_ADR_CAM_ALL_EN was not present in legacy
                                                         GMX implemenations which had only eight DMAC CAM
                                                         entries. New applications may choose to ignore
                                                         GMX_RXx_ADR_CAM_EN using GMX_RX_ADR_CAM_ALL_EN
                                                         instead.

                                                         EN represents the full 32 indepedent per MAC
                                                         enables.

                                                         Writes to EN will be reflected in
                                                         GMX_RXx_ADR_CAM_EN[EN] and writes to
                                                         GMX_RXx_ADR_CAM_EN[EN] will be reflected in EN.
                                                         Refer to GMX_RXx_ADR_CAM_EN for the CSR mapping.

                                                         In XAUI mode, only GMX_RX0_ADR_CAM_ALL_EN is used
                                                         and GMX_RX[1,2,3]_ADR_CAM_ALL_EN should not be
                                                         used. */
#else
	uint64_t en                           : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn61xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn66xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cn68xx;
	struct cvmx_gmxx_rxx_adr_cam_all_en_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam_all_en cvmx_gmxx_rxx_adr_cam_all_en_t;

/**
 * cvmx_gmx#_rx#_adr_cam_en
 *
 * GMX_RX_ADR_CAM_EN = Address Filtering Control Enable
 *
 */
union cvmx_gmxx_rxx_adr_cam_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_cam_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t en                           : 8;  /**< CAM Entry Enables

                                                         GMX has 32 DMAC entries that can be accessed with
                                                         the GMX_RX[0..3]_ADR_CAM[0..5] CSRs.
                                                         These 32 DMAC entries can be used by any of the
                                                         four SGMII MACs or the XAUI MAC.

                                                         Each port interface has independent control of
                                                         which of the 32 DMAC entries to include in the
                                                         CAM lookup.

                                                         Legacy GMX implementations were able to CAM
                                                         against eight DMAC entries while current
                                                         implementations use 32 common entries.
                                                         This register is intended for legacy applications
                                                         that only require eight DMAC CAM entries per MAC.
                                                         New applications may choose to ignore
                                                         GMX_RXx_ADR_CAM_EN using GMX_RXx_ADR_CAM_ALL_EN
                                                         instead.

                                                         EN controls the enables for the eight legacy CAM
                                                         entries as follows:
                                                          port0, EN = GMX_RX0_ADR_CAM_ALL_EN[EN<7:0>]
                                                          port1, EN = GMX_RX1_ADR_CAM_ALL_EN[EN<15:8>]
                                                          port2, EN = GMX_RX2_ADR_CAM_ALL_EN[EN<23:16>]
                                                          port3, EN = GMX_RX3_ADR_CAM_ALL_EN[EN<31:24>]

                                                         The full 32 indepedent per MAC enables are in
                                                         GMX_RX_ADR_CAM_ALL_EN.

                                                         Therefore, writes to GMX_RXX_ADR_CAM_ALL_EN[EN]
                                                         will be reflected in EN and writes to EN will be
                                                         reflected in GMX_RXX_ADR_CAM_ALL_EN[EN].

                                                         In XAUI mode, only GMX_RX0_ADR_CAM_EN is used and
                                                         GMX_RX[1,2,3]_ADR_CAM_EN should not be used. */
#else
	uint64_t en                           : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn30xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn31xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn38xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn38xxp2;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn50xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn52xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn52xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn56xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn56xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn58xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn58xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn61xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn63xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn63xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn66xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn68xx;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cn68xxp1;
	struct cvmx_gmxx_rxx_adr_cam_en_s     cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_cam_en cvmx_gmxx_rxx_adr_cam_en_t;

/**
 * cvmx_gmx#_rx#_adr_ctl
 *
 * GMX_RX_ADR_CTL = Address Filtering Control
 *
 *
 * Notes:
 * * ALGORITHM
 *   Here is some pseudo code that represents the address filter behavior.
 *
 *      @verbatim
 *      bool dmac_addr_filter(uint8 prt, uint48 dmac) [
 *        ASSERT(prt >= 0 && prt <= 3);
 *        if (is_bcst(dmac))                               // broadcast accept
 *          return (GMX_RX[prt]_ADR_CTL[BCST] ? ACCEPT : REJECT);
 *        if (is_mcst(dmac) & GMX_RX[prt]_ADR_CTL[MCST] == 1)   // multicast reject
 *          return REJECT;
 *        if (is_mcst(dmac) & GMX_RX[prt]_ADR_CTL[MCST] == 2)   // multicast accept
 *          return ACCEPT;
 *
 *        cam_hit = 0;
 *
 *        for (i=0; i<32; i++) [
 *          if (GMX_RX[prt]_ADR_CAM_ALL_EN[EN<i>] == 0)
 *            continue;
 *          uint48 unswizzled_mac_adr = 0x0;
 *          for (j=5; j>=0; j--) [
 *             unswizzled_mac_adr = (unswizzled_mac_adr << 8) | GMX_RX[i>>3]_ADR_CAM[j][ADR<(i&7)*8+7:(i&7)*8>];
 *          ]
 *          if (unswizzled_mac_adr == dmac) [
 *            cam_hit = 1;
 *            break;
 *          ]
 *        ]
 *
 *        if (cam_hit)
 *          return (GMX_RX[prt]_ADR_CTL[CAM_MODE] ? ACCEPT : REJECT);
 *        else
 *          return (GMX_RX[prt]_ADR_CTL[CAM_MODE] ? REJECT : ACCEPT);
 *      ]
 *      @endverbatim
 *
 * * XAUI Mode
 *
 *   In XAUI mode, only GMX_RX0_ADR_CTL is used.  GMX_RX[1,2,3]_ADR_CTL should not be used.
 */
union cvmx_gmxx_rxx_adr_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_adr_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t cam_mode                     : 1;  /**< Allow or deny DMAC address filter
                                                         0 = reject the packet on DMAC address match
                                                         1 = accept the packet on DMAC address match */
	uint64_t mcst                         : 2;  /**< Multicast Mode
                                                         0 = Use the Address Filter CAM
                                                         1 = Force reject all multicast packets
                                                         2 = Force accept all multicast packets
                                                         3 = Reserved */
	uint64_t bcst                         : 1;  /**< Accept All Broadcast Packets */
#else
	uint64_t bcst                         : 1;
	uint64_t mcst                         : 2;
	uint64_t cam_mode                     : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn30xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn31xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn38xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn38xxp2;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn50xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn52xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn52xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn56xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn56xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn58xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn58xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn61xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn63xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn63xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn66xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn68xx;
	struct cvmx_gmxx_rxx_adr_ctl_s        cn68xxp1;
	struct cvmx_gmxx_rxx_adr_ctl_s        cnf71xx;
};
typedef union cvmx_gmxx_rxx_adr_ctl cvmx_gmxx_rxx_adr_ctl_t;

/**
 * cvmx_gmx#_rx#_decision
 *
 * GMX_RX_DECISION = The byte count to decide when to accept or filter a packet
 *
 *
 * Notes:
 * As each byte in a packet is received by GMX, the L2 byte count is compared
 * against the GMX_RX_DECISION[CNT].  The L2 byte count is the number of bytes
 * from the beginning of the L2 header (DMAC).  In normal operation, the L2
 * header begins after the PREAMBLE+SFD (GMX_RX_FRM_CTL[PRE_CHK]=1) and any
 * optional UDD skip data (GMX_RX_UDD_SKP[LEN]).
 *
 * When GMX_RX_FRM_CTL[PRE_CHK] is clear, PREAMBLE+SFD are prepended to the
 * packet and would require UDD skip length to account for them.
 *
 *                                                 L2 Size
 * Port Mode             <GMX_RX_DECISION bytes (default=24)       >=GMX_RX_DECISION bytes (default=24)
 *
 * Full Duplex           accept packet                             apply filters
 *                       no filtering is applied                   accept packet based on DMAC and PAUSE packet filters
 *
 * Half Duplex           drop packet                               apply filters
 *                       packet is unconditionally dropped         accept packet based on DMAC
 *
 * where l2_size = MAX(0, total_packet_size - GMX_RX_UDD_SKP[LEN] - ((GMX_RX_FRM_CTL[PRE_CHK]==1)*8)
 */
union cvmx_gmxx_rxx_decision {
	uint64_t u64;
	struct cvmx_gmxx_rxx_decision_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t cnt                          : 5;  /**< The byte count to decide when to accept or filter
                                                         a packet. */
#else
	uint64_t cnt                          : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_gmxx_rxx_decision_s       cn30xx;
	struct cvmx_gmxx_rxx_decision_s       cn31xx;
	struct cvmx_gmxx_rxx_decision_s       cn38xx;
	struct cvmx_gmxx_rxx_decision_s       cn38xxp2;
	struct cvmx_gmxx_rxx_decision_s       cn50xx;
	struct cvmx_gmxx_rxx_decision_s       cn52xx;
	struct cvmx_gmxx_rxx_decision_s       cn52xxp1;
	struct cvmx_gmxx_rxx_decision_s       cn56xx;
	struct cvmx_gmxx_rxx_decision_s       cn56xxp1;
	struct cvmx_gmxx_rxx_decision_s       cn58xx;
	struct cvmx_gmxx_rxx_decision_s       cn58xxp1;
	struct cvmx_gmxx_rxx_decision_s       cn61xx;
	struct cvmx_gmxx_rxx_decision_s       cn63xx;
	struct cvmx_gmxx_rxx_decision_s       cn63xxp1;
	struct cvmx_gmxx_rxx_decision_s       cn66xx;
	struct cvmx_gmxx_rxx_decision_s       cn68xx;
	struct cvmx_gmxx_rxx_decision_s       cn68xxp1;
	struct cvmx_gmxx_rxx_decision_s       cnf71xx;
};
typedef union cvmx_gmxx_rxx_decision cvmx_gmxx_rxx_decision_t;

/**
 * cvmx_gmx#_rx#_frm_chk
 *
 * GMX_RX_FRM_CHK = Which frame errors will set the ERR bit of the frame
 *
 *
 * Notes:
 * If GMX_RX_UDD_SKP[LEN] != 0, then LENERR will be forced to zero in HW.
 *
 * In XAUI mode prt0 is used for checking.
 */
union cvmx_gmxx_rxx_frm_chk {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_chk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_chk_s        cn30xx;
	struct cvmx_gmxx_rxx_frm_chk_s        cn31xx;
	struct cvmx_gmxx_rxx_frm_chk_s        cn38xx;
	struct cvmx_gmxx_rxx_frm_chk_s        cn38xxp2;
	struct cvmx_gmxx_rxx_frm_chk_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t reserved_6_6                 : 1;
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t reserved_6_6                 : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx   cn52xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx   cn56xx;
	struct cvmx_gmxx_rxx_frm_chk_cn52xx   cn56xxp1;
	struct cvmx_gmxx_rxx_frm_chk_s        cn58xx;
	struct cvmx_gmxx_rxx_frm_chk_s        cn58xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn61xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cn63xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cn63xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cn66xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cn68xx;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cn68xxp1;
	struct cvmx_gmxx_rxx_frm_chk_cn61xx   cnf71xx;
};
typedef union cvmx_gmxx_rxx_frm_chk cvmx_gmxx_rxx_frm_chk_t;

/**
 * cvmx_gmx#_rx#_frm_ctl
 *
 * GMX_RX_FRM_CTL = Frame Control
 *
 *
 * Notes:
 * * PRE_STRP
 *   When PRE_CHK is set (indicating that the PREAMBLE will be sent), PRE_STRP
 *   determines if the PREAMBLE+SFD bytes are thrown away or sent to the Octane
 *   core as part of the packet.
 *
 *   In either mode, the PREAMBLE+SFD bytes are not counted toward the packet
 *   size when checking against the MIN and MAX bounds.  Furthermore, the bytes
 *   are skipped when locating the start of the L2 header for DMAC and Control
 *   frame recognition.
 *
 * * CTL_BCK/CTL_DRP
 *   These bits control how the HW handles incoming PAUSE packets.  Here are
 *   the most common modes of operation:
 *     CTL_BCK=1,CTL_DRP=1   - HW does it all
 *     CTL_BCK=0,CTL_DRP=0   - SW sees all pause frames
 *     CTL_BCK=0,CTL_DRP=1   - all pause frames are completely ignored
 *
 *   These control bits should be set to CTL_BCK=0,CTL_DRP=0 in halfdup mode.
 *   Since PAUSE packets only apply to fulldup operation, any PAUSE packet
 *   would constitute an exception which should be handled by the processing
 *   cores.  PAUSE packets should not be forwarded.
 */
union cvmx_gmxx_rxx_frm_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t ptp_mode                     : 1;  /**< Timestamp mode
                                                         When PTP_MODE is set, a 64-bit timestamp will be
                                                         prepended to every incoming packet. The timestamp
                                                         bytes are added to the packet in such a way as to
                                                         not modify the packet's receive byte count.  This
                                                         implies that the GMX_RX_JABBER, MINERR,
                                                         GMX_RX_DECISION, GMX_RX_UDD_SKP, and the
                                                         GMX_RX_STATS_* do not require any adjustment as
                                                         they operate on the received packet size.
                                                         When the packet reaches PKI, its size will
                                                         reflect the additional bytes and is subject to
                                                         the restrictions below.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1.
                                                         If PTP_MODE=1,
                                                          PIP_PRT_CFGx[SKIP] should be increased by 8.
                                                          PIP_PRT_CFGx[HIGIG_EN] should be 0.
                                                          PIP_FRM_CHKx[MAXLEN] should be increased by 8.
                                                          PIP_FRM_CHKx[MINLEN] should be increased by 8.
                                                          PIP_TAG_INCx[EN] should be adjusted.
                                                          PIP_PRT_CFGBx[ALT_SKP_EN] should be 0. */
	uint64_t reserved_11_11               : 1;
	uint64_t null_dis                     : 1;  /**< When set, do not modify the MOD bits on NULL ticks
                                                         due to PARITAL packets */
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PRE_STRP should be set to
                                                         account for the variable nature of the PREAMBLE.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII at 10/100Mbs only) */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for non-min
                                                         sized pkts with padding in the client data
                                                         (PASS3 Only) */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is  less strict.
                                                         GMX will begin the frame at the first SFD.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII/1000Base-X only) */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1. */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send a valid 802.3
                                                         PREAMBLE to begin every frame. GMX checks that a
                                                         valid PREAMBLE is received (based on PRE_FREE).
                                                         When a problem does occur within the PREAMBLE
                                                         seqeunce, the frame is marked as bad and not sent
                                                         into the core.  The GMX_GMX_RX_INT_REG[PCTERR]
                                                         interrupt is also raised.
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, PRE_CHK
                                                         must be zero.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1. */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t pre_align                    : 1;
	uint64_t null_dis                     : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t ptp_mode                     : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for non-min
                                                         sized pkts with padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< Allows for less strict PREAMBLE checking.
                                                         0-7 cycles of PREAMBLE followed by SFD (pass 1.0)
                                                         0-254 cycles of PREAMBLE followed by SFD (else) */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< Allows for less strict PREAMBLE checking.
                                                         0 - 7 cycles of PREAMBLE followed by SFD (pass1.0)
                                                         0 - 254 cycles of PREAMBLE followed by SFD (else) */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t reserved_8_63                : 56;
#endif
	} cn31xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx   cn38xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn31xx   cn38xxp2;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t null_dis                     : 1;  /**< When set, do not modify the MOD bits on NULL ticks
                                                         due to PARITAL packets */
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PREAMBLE can be consumed
                                                         by the HW so when PRE_ALIGN is set, PRE_FREE,
                                                         PRE_STRP must be set for correct operation.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_free                     : 1;  /**< Allows for less strict PREAMBLE checking.
                                                         0-254 cycles of PREAMBLE followed by SFD */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_align                    : 1;
	uint64_t null_dis                     : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx   cn52xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx   cn52xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn50xx   cn56xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PRE_STRP should be set to
                                                         account for the variable nature of the PREAMBLE.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII at 10/100Mbs only) */
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is  less strict.
                                                         0 - 254 cycles of PREAMBLE followed by SFD
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII/1000Base-X only) */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly.
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, PRE_CHK
                                                         must be zero. */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_align                    : 1;
	uint64_t reserved_10_63               : 54;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t null_dis                     : 1;  /**< When set, do not modify the MOD bits on NULL ticks
                                                         due to PARITAL packets
                                                         In spi4 mode, all ports use prt0 for checking. */
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PREAMBLE can be consumed
                                                         by the HW so when PRE_ALIGN is set, PRE_FREE,
                                                         PRE_STRP must be set for correct operation.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features. */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for non-min
                                                         sized pkts with padding in the client data
                                                         (PASS3 Only) */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is  less strict.
                                                         0 - 254 cycles of PREAMBLE followed by SFD */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send PREAMBLE+SFD
                                                         to begin every frame.  GMX checks that the
                                                         PREAMBLE is sent correctly */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t pre_align                    : 1;
	uint64_t null_dis                     : 1;
	uint64_t reserved_11_63               : 53;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn30xx   cn58xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t ptp_mode                     : 1;  /**< Timestamp mode
                                                         When PTP_MODE is set, a 64-bit timestamp will be
                                                         prepended to every incoming packet. The timestamp
                                                         bytes are added to the packet in such a way as to
                                                         not modify the packet's receive byte count.  This
                                                         implies that the GMX_RX_JABBER, MINERR,
                                                         GMX_RX_DECISION, GMX_RX_UDD_SKP, and the
                                                         GMX_RX_STATS_* do not require any adjustment as
                                                         they operate on the received packet size.
                                                         When the packet reaches PKI, its size will
                                                         reflect the additional bytes and is subject to
                                                         the restrictions below.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1.
                                                         If PTP_MODE=1,
                                                          PIP_PRT_CFGx[SKIP] should be increased by 8.
                                                          PIP_PRT_CFGx[HIGIG_EN] should be 0.
                                                          PIP_FRM_CHKx[MAXLEN] should be increased by 8.
                                                          PIP_FRM_CHKx[MINLEN] should be increased by 8.
                                                          PIP_TAG_INCx[EN] should be adjusted.
                                                          PIP_PRT_CFGBx[ALT_SKP_EN] should be 0. */
	uint64_t reserved_11_11               : 1;
	uint64_t null_dis                     : 1;  /**< When set, do not modify the MOD bits on NULL ticks
                                                         due to PARITAL packets */
	uint64_t pre_align                    : 1;  /**< When set, PREAMBLE parser aligns the the SFD byte
                                                         regardless of the number of previous PREAMBLE
                                                         nibbles.  In this mode, PRE_STRP should be set to
                                                         account for the variable nature of the PREAMBLE.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII at 10/100Mbs only) */
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_free                     : 1;  /**< When set, PREAMBLE checking is  less strict.
                                                         GMX will begin the frame at the first SFD.
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         (SGMII/1000Base-X only) */
	uint64_t ctl_smac                     : 1;  /**< Control Pause Frames can match station SMAC */
	uint64_t ctl_mcst                     : 1;  /**< Control Pause Frames can match globally assign
                                                         Multicast address */
	uint64_t ctl_bck                      : 1;  /**< Forward pause information to TX block */
	uint64_t ctl_drp                      : 1;  /**< Drop Control Pause Frames */
	uint64_t pre_strp                     : 1;  /**< Strip off the preamble (when present)
                                                         0=PREAMBLE+SFD is sent to core as part of frame
                                                         1=PREAMBLE+SFD is dropped
                                                         PRE_CHK must be set to enable this and all
                                                         PREAMBLE features.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1. */
	uint64_t pre_chk                      : 1;  /**< This port is configured to send a valid 802.3
                                                         PREAMBLE to begin every frame. GMX checks that a
                                                         valid PREAMBLE is received (based on PRE_FREE).
                                                         When a problem does occur within the PREAMBLE
                                                         seqeunce, the frame is marked as bad and not sent
                                                         into the core.  The GMX_GMX_RX_INT_REG[PCTERR]
                                                         interrupt is also raised.
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, PRE_CHK
                                                         must be zero.
                                                         If PTP_MODE=1 and PRE_CHK=1, PRE_STRP must be 1. */
#else
	uint64_t pre_chk                      : 1;
	uint64_t pre_strp                     : 1;
	uint64_t ctl_drp                      : 1;
	uint64_t ctl_bck                      : 1;
	uint64_t ctl_mcst                     : 1;
	uint64_t ctl_smac                     : 1;
	uint64_t pre_free                     : 1;
	uint64_t reserved_7_8                 : 2;
	uint64_t pre_align                    : 1;
	uint64_t null_dis                     : 1;
	uint64_t reserved_11_11               : 1;
	uint64_t ptp_mode                     : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn61xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cn63xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cn63xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cn66xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cn68xx;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cn68xxp1;
	struct cvmx_gmxx_rxx_frm_ctl_cn61xx   cnf71xx;
};
typedef union cvmx_gmxx_rxx_frm_ctl cvmx_gmxx_rxx_frm_ctl_t;

/**
 * cvmx_gmx#_rx#_frm_max
 *
 * GMX_RX_FRM_MAX = Frame Max length
 *
 *
 * Notes:
 * In spi4 mode, all spi4 ports use prt0 for checking.
 *
 * When changing the LEN field, be sure that LEN does not exceed
 * GMX_RX_JABBER[CNT]. Failure to meet this constraint will cause packets that
 * are within the maximum length parameter to be rejected because they exceed
 * the GMX_RX_JABBER[CNT] limit.
 */
union cvmx_gmxx_rxx_frm_max {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t len                          : 16; /**< Byte count for Max-sized frame check
                                                         GMX_RXn_FRM_CHK[MAXERR] enables the check for
                                                         port n.
                                                         If enabled, failing packets set the MAXERR
                                                         interrupt and work-queue entry WORD2[opcode] is
                                                         set to OVER_FCS (0x3, if packet has bad FCS) or
                                                         OVER_ERR (0x4, if packet has good FCS).
                                                         LEN =< GMX_RX_JABBER[CNT] */
#else
	uint64_t len                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_max_s        cn30xx;
	struct cvmx_gmxx_rxx_frm_max_s        cn31xx;
	struct cvmx_gmxx_rxx_frm_max_s        cn38xx;
	struct cvmx_gmxx_rxx_frm_max_s        cn38xxp2;
	struct cvmx_gmxx_rxx_frm_max_s        cn58xx;
	struct cvmx_gmxx_rxx_frm_max_s        cn58xxp1;
};
typedef union cvmx_gmxx_rxx_frm_max cvmx_gmxx_rxx_frm_max_t;

/**
 * cvmx_gmx#_rx#_frm_min
 *
 * GMX_RX_FRM_MIN = Frame Min length
 *
 *
 * Notes:
 * In spi4 mode, all spi4 ports use prt0 for checking.
 *
 */
union cvmx_gmxx_rxx_frm_min {
	uint64_t u64;
	struct cvmx_gmxx_rxx_frm_min_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t len                          : 16; /**< Byte count for Min-sized frame check
                                                         GMX_RXn_FRM_CHK[MINERR] enables the check for
                                                         port n.
                                                         If enabled, failing packets set the MINERR
                                                         interrupt and work-queue entry WORD2[opcode] is
                                                         set to UNDER_FCS (0x6, if packet has bad FCS) or
                                                         UNDER_ERR (0x8, if packet has good FCS). */
#else
	uint64_t len                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_rxx_frm_min_s        cn30xx;
	struct cvmx_gmxx_rxx_frm_min_s        cn31xx;
	struct cvmx_gmxx_rxx_frm_min_s        cn38xx;
	struct cvmx_gmxx_rxx_frm_min_s        cn38xxp2;
	struct cvmx_gmxx_rxx_frm_min_s        cn58xx;
	struct cvmx_gmxx_rxx_frm_min_s        cn58xxp1;
};
typedef union cvmx_gmxx_rxx_frm_min cvmx_gmxx_rxx_frm_min_t;

/**
 * cvmx_gmx#_rx#_ifg
 *
 * GMX_RX_IFG = RX Min IFG
 *
 */
union cvmx_gmxx_rxx_ifg {
	uint64_t u64;
	struct cvmx_gmxx_rxx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t ifg                          : 4;  /**< Min IFG (in IFG*8 bits) between packets used to
                                                         determine IFGERR. Normally IFG is 96 bits.
                                                         Note in some operating modes, IFG cycles can be
                                                         inserted or removed in order to achieve clock rate
                                                         adaptation. For these reasons, the default value
                                                         is slightly conservative and does not check upto
                                                         the full 96 bits of IFG.
                                                         (SGMII/1000Base-X only) */
#else
	uint64_t ifg                          : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_rxx_ifg_s            cn30xx;
	struct cvmx_gmxx_rxx_ifg_s            cn31xx;
	struct cvmx_gmxx_rxx_ifg_s            cn38xx;
	struct cvmx_gmxx_rxx_ifg_s            cn38xxp2;
	struct cvmx_gmxx_rxx_ifg_s            cn50xx;
	struct cvmx_gmxx_rxx_ifg_s            cn52xx;
	struct cvmx_gmxx_rxx_ifg_s            cn52xxp1;
	struct cvmx_gmxx_rxx_ifg_s            cn56xx;
	struct cvmx_gmxx_rxx_ifg_s            cn56xxp1;
	struct cvmx_gmxx_rxx_ifg_s            cn58xx;
	struct cvmx_gmxx_rxx_ifg_s            cn58xxp1;
	struct cvmx_gmxx_rxx_ifg_s            cn61xx;
	struct cvmx_gmxx_rxx_ifg_s            cn63xx;
	struct cvmx_gmxx_rxx_ifg_s            cn63xxp1;
	struct cvmx_gmxx_rxx_ifg_s            cn66xx;
	struct cvmx_gmxx_rxx_ifg_s            cn68xx;
	struct cvmx_gmxx_rxx_ifg_s            cn68xxp1;
	struct cvmx_gmxx_rxx_ifg_s            cnf71xx;
};
typedef union cvmx_gmxx_rxx_ifg cvmx_gmxx_rxx_ifg_t;

/**
 * cvmx_gmx#_rx#_int_en
 *
 * GMX_RX_INT_EN = Interrupt Enable
 *
 *
 * Notes:
 * In XAUI mode prt0 is used for checking.
 *
 */
union cvmx_gmxx_rxx_int_en {
	uint64_t u64;
	struct cvmx_gmxx_rxx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 CRC8 or Control char error interrupt enable */
	uint64_t hg2fld                       : 1;  /**< HiGig2 Bad field error interrupt enable */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         (SGMII/1000Base-X only) */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_gmxx_rxx_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_int_en_cn30xx    cn31xx;
	struct cvmx_gmxx_rxx_int_en_cn30xx    cn38xx;
	struct cvmx_gmxx_rxx_int_en_cn30xx    cn38xxp2;
	struct cvmx_gmxx_rxx_int_en_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t reserved_6_6                 : 1;
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t reserved_6_6                 : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 CRC8 or Control char error interrupt enable */
	uint64_t hg2fld                       : 1;  /**< HiGig2 Bad field error interrupt enable */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_int_en_cn52xx    cn52xxp1;
	struct cvmx_gmxx_rxx_int_en_cn52xx    cn56xx;
	struct cvmx_gmxx_rxx_int_en_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t reserved_27_63               : 37;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_en_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_int_en_cn58xx    cn58xxp1;
	struct cvmx_gmxx_rxx_int_en_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 CRC8 or Control char error interrupt enable */
	uint64_t hg2fld                       : 1;  /**< HiGig2 Bad field error interrupt enable */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn61xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cn63xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cn63xxp1;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cn66xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cn68xx;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cn68xxp1;
	struct cvmx_gmxx_rxx_int_en_cn61xx    cnf71xx;
};
typedef union cvmx_gmxx_rxx_int_en cvmx_gmxx_rxx_int_en_t;

/**
 * cvmx_gmx#_rx#_int_reg
 *
 * GMX_RX_INT_REG = Interrupt Register
 *
 *
 * Notes:
 * (1) exceptions will only be raised to the control processor if the
 *     corresponding bit in the GMX_RX_INT_EN register is set.
 *
 * (2) exception conditions 10:0 can also set the rcv/opcode in the received
 *     packet's workQ entry.  The GMX_RX_FRM_CHK register provides a bit mask
 *     for configuring which conditions set the error.
 *
 * (3) in half duplex operation, the expectation is that collisions will appear
 *     as either MINERR o r CAREXT errors.
 *
 * (4) JABBER - An RX Jabber error indicates that a packet was received which
 *              is longer than the maximum allowed packet as defined by the
 *              system.  GMX will truncate the packet at the JABBER count.
 *              Failure to do so could lead to system instabilty.
 *
 * (5) NIBERR - This error is illegal at 1000Mbs speeds
 *              (GMX_RX_PRT_CFG[SPEED]==0) and will never assert.
 *
 * (6) MAXERR - for untagged frames, the total frame DA+SA+TL+DATA+PAD+FCS >
 *              GMX_RX_FRM_MAX.  For tagged frames, DA+SA+VLAN+TL+DATA+PAD+FCS
 *              > GMX_RX_FRM_MAX + 4*VLAN_VAL + 4*VLAN_STACKED.
 *
 * (7) MINERR - total frame DA+SA+TL+DATA+PAD+FCS < 64
 *
 * (8) ALNERR - Indicates that the packet received was not an integer number of
 *              bytes.  If FCS checking is enabled, ALNERR will only assert if
 *              the FCS is bad.  If FCS checking is disabled, ALNERR will
 *              assert in all non-integer frame cases.
 *
 * (9) Collisions - Collisions can only occur in half-duplex mode.  A collision
 *                  is assumed by the receiver when the slottime
 *                  (GMX_PRT_CFG[SLOTTIME]) is not satisfied.  In 10/100 mode,
 *                  this will result in a frame < SLOTTIME.  In 1000 mode, it
 *                  could result either in frame < SLOTTIME or a carrier extend
 *                  error with the SLOTTIME.  These conditions are visible by...
 *
 *                  . transfer ended before slottime - COLDET
 *                  . carrier extend error           - CAREXT
 *
 * (A) LENERR - Length errors occur when the received packet does not match the
 *              length field.  LENERR is only checked for packets between 64
 *              and 1500 bytes.  For untagged frames, the length must exact
 *              match.  For tagged frames the length or length+4 must match.
 *
 * (B) PCTERR - checks that the frame begins with a valid PREAMBLE sequence.
 *              Does not check the number of PREAMBLE cycles.
 *
 * (C) OVRERR -
 *
 *              OVRERR is an architectural assertion check internal to GMX to
 *              make sure no assumption was violated.  In a correctly operating
 *              system, this interrupt can never fire.
 *
 *              GMX has an internal arbiter which selects which of 4 ports to
 *              buffer in the main RX FIFO.  If we normally buffer 8 bytes,
 *              then each port will typically push a tick every 8 cycles - if
 *              the packet interface is going as fast as possible.  If there
 *              are four ports, they push every two cycles.  So that's the
 *              assumption.  That the inbound module will always be able to
 *              consume the tick before another is produced.  If that doesn't
 *              happen - that's when OVRERR will assert.
 *
 * (D) In XAUI mode prt0 is used for interrupt logging.
 */
union cvmx_gmxx_rxx_int_reg {
	uint64_t u64;
	struct cvmx_gmxx_rxx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 received message CRC or Control char  error
                                                         Set when either CRC8 error detected or when
                                                         a Control Character is found in the message
                                                         bytes after the K.SOM
                                                         NOTE: HG2CC has higher priority than HG2FLD
                                                               i.e. a HiGig2 message that results in HG2CC
                                                               getting set, will never set HG2FLD. */
	uint64_t hg2fld                       : 1;  /**< HiGig2 received message field error, as below
                                                         1) MSG_TYPE field not 6'b00_0000
                                                            i.e. it is not a FLOW CONTROL message, which
                                                            is the only defined type for HiGig2
                                                         2) FWD_TYPE field not 2'b00 i.e. Link Level msg
                                                            which is the only defined type for HiGig2
                                                         3) FC_OBJECT field is neither 4'b0000 for
                                                            Physical Link nor 4'b0010 for Logical Link.
                                                            Those are the only two defined types in HiGig2 */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol
                                                         In XAUI mode, the column of data that was bad
                                                         will be logged in GMX_RX_XAUI_BAD_COL */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert
                                                         (SGMII/1000Base-X only) */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize
                                                         Frame length checks are typically handled in PIP
                                                         (PIP_INT_REG[MINERR]), but pause frames are
                                                         normally discarded before being inspected by PIP. */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_gmxx_rxx_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rxx_int_reg_cn30xx   cn31xx;
	struct cvmx_gmxx_rxx_int_reg_cn30xx   cn38xx;
	struct cvmx_gmxx_rxx_int_reg_cn30xx   cn38xxp2;
	struct cvmx_gmxx_rxx_int_reg_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t reserved_6_6                 : 1;
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t reserved_6_6                 : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn50xx;
	struct cvmx_gmxx_rxx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 received message CRC or Control char  error
                                                         Set when either CRC8 error detected or when
                                                         a Control Character is found in the message
                                                         bytes after the K.SOM
                                                         NOTE: HG2CC has higher priority than HG2FLD
                                                               i.e. a HiGig2 message that results in HG2CC
                                                               getting set, will never set HG2FLD. */
	uint64_t hg2fld                       : 1;  /**< HiGig2 received message field error, as below
                                                         1) MSG_TYPE field not 6'b00_0000
                                                            i.e. it is not a FLOW CONTROL message, which
                                                            is the only defined type for HiGig2
                                                         2) FWD_TYPE field not 2'b00 i.e. Link Level msg
                                                            which is the only defined type for HiGig2
                                                         3) FC_OBJECT field is neither 4'b0000 for
                                                            Physical Link nor 4'b0010 for Logical Link.
                                                            Those are the only two defined types in HiGig2 */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol
                                                         In XAUI mode, the column of data that was bad
                                                         will be logged in GMX_RX_XAUI_BAD_COL */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn52xx;
	struct cvmx_gmxx_rxx_int_reg_cn52xx   cn52xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn52xx   cn56xx;
	struct cvmx_gmxx_rxx_int_reg_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol
                                                         In XAUI mode, the column of data that was bad
                                                         will be logged in GMX_RX_XAUI_BAD_COL */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_0_0                 : 1;
#else
	uint64_t reserved_0_0                 : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t reserved_27_63               : 37;
#endif
	} cn56xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t phy_dupx                     : 1;  /**< Change in the RMGII inbound LinkDuplex */
	uint64_t phy_spd                      : 1;  /**< Change in the RMGII inbound LinkSpeed */
	uint64_t phy_link                     : 1;  /**< Change in the RMGII inbound LinkStatus */
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure */
	uint64_t coldet                       : 1;  /**< Collision Detection */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime */
	uint64_t rsverr                       : 1;  /**< RGMII reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert */
	uint64_t niberr                       : 1;  /**< Nibble error (hi_nibble != lo_nibble) */
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with RMGII Data reception error */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t alnerr                       : 1;  /**< Frame was received with an alignment error */
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t carext                       : 1;  /**< RGMII carrier extend error */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t alnerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t niberr                       : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t phy_link                     : 1;
	uint64_t phy_spd                      : 1;
	uint64_t phy_dupx                     : 1;
	uint64_t pause_drp                    : 1;
	uint64_t reserved_20_63               : 44;
#endif
	} cn58xx;
	struct cvmx_gmxx_rxx_int_reg_cn58xx   cn58xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t hg2cc                        : 1;  /**< HiGig2 received message CRC or Control char  error
                                                         Set when either CRC8 error detected or when
                                                         a Control Character is found in the message
                                                         bytes after the K.SOM
                                                         NOTE: HG2CC has higher priority than HG2FLD
                                                               i.e. a HiGig2 message that results in HG2CC
                                                               getting set, will never set HG2FLD. */
	uint64_t hg2fld                       : 1;  /**< HiGig2 received message field error, as below
                                                         1) MSG_TYPE field not 6'b00_0000
                                                            i.e. it is not a FLOW CONTROL message, which
                                                            is the only defined type for HiGig2
                                                         2) FWD_TYPE field not 2'b00 i.e. Link Level msg
                                                            which is the only defined type for HiGig2
                                                         3) FC_OBJECT field is neither 4'b0000 for
                                                            Physical Link nor 4'b0010 for Logical Link.
                                                            Those are the only two defined types in HiGig2 */
	uint64_t undat                        : 1;  /**< Unexpected Data
                                                         (XAUI Mode only) */
	uint64_t uneop                        : 1;  /**< Unexpected EOP
                                                         (XAUI Mode only) */
	uint64_t unsop                        : 1;  /**< Unexpected SOP
                                                         (XAUI Mode only) */
	uint64_t bad_term                     : 1;  /**< Frame is terminated by control character other
                                                         than /T/.  The error propagation control
                                                         character /E/ will be included as part of the
                                                         frame and does not cause a frame termination.
                                                         (XAUI Mode only) */
	uint64_t bad_seq                      : 1;  /**< Reserved Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t rem_fault                    : 1;  /**< Remote Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t loc_fault                    : 1;  /**< Local Fault Sequence Deteted
                                                         (XAUI Mode only) */
	uint64_t pause_drp                    : 1;  /**< Pause packet was dropped due to full GMX RX FIFO */
	uint64_t reserved_16_18               : 3;
	uint64_t ifgerr                       : 1;  /**< Interframe Gap Violation
                                                         Does not necessarily indicate a failure
                                                         (SGMII/1000Base-X only) */
	uint64_t coldet                       : 1;  /**< Collision Detection
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t falerr                       : 1;  /**< False carrier error or extend error after slottime
                                                         (SGMII/1000Base-X only) */
	uint64_t rsverr                       : 1;  /**< Reserved opcodes */
	uint64_t pcterr                       : 1;  /**< Bad Preamble / Protocol
                                                         In XAUI mode, the column of data that was bad
                                                         will be logged in GMX_RX_XAUI_BAD_COL */
	uint64_t ovrerr                       : 1;  /**< Internal Data Aggregation Overflow
                                                         This interrupt should never assert
                                                         (SGMII/1000Base-X only) */
	uint64_t reserved_9_9                 : 1;
	uint64_t skperr                       : 1;  /**< Skipper error */
	uint64_t rcverr                       : 1;  /**< Frame was received with Data reception error */
	uint64_t reserved_5_6                 : 2;
	uint64_t fcserr                       : 1;  /**< Frame was received with FCS/CRC error */
	uint64_t jabber                       : 1;  /**< Frame was received with length > sys_length */
	uint64_t reserved_2_2                 : 1;
	uint64_t carext                       : 1;  /**< Carrier extend error
                                                         (SGMII/1000Base-X only) */
	uint64_t minerr                       : 1;  /**< Pause Frame was received with length<minFrameSize
                                                         Frame length checks are typically handled in PIP
                                                         (PIP_INT_REG[MINERR]), but pause frames are
                                                         normally discarded before being inspected by PIP. */
#else
	uint64_t minerr                       : 1;
	uint64_t carext                       : 1;
	uint64_t reserved_2_2                 : 1;
	uint64_t jabber                       : 1;
	uint64_t fcserr                       : 1;
	uint64_t reserved_5_6                 : 2;
	uint64_t rcverr                       : 1;
	uint64_t skperr                       : 1;
	uint64_t reserved_9_9                 : 1;
	uint64_t ovrerr                       : 1;
	uint64_t pcterr                       : 1;
	uint64_t rsverr                       : 1;
	uint64_t falerr                       : 1;
	uint64_t coldet                       : 1;
	uint64_t ifgerr                       : 1;
	uint64_t reserved_16_18               : 3;
	uint64_t pause_drp                    : 1;
	uint64_t loc_fault                    : 1;
	uint64_t rem_fault                    : 1;
	uint64_t bad_seq                      : 1;
	uint64_t bad_term                     : 1;
	uint64_t unsop                        : 1;
	uint64_t uneop                        : 1;
	uint64_t undat                        : 1;
	uint64_t hg2fld                       : 1;
	uint64_t hg2cc                        : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn61xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cn63xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cn63xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cn66xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cn68xx;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cn68xxp1;
	struct cvmx_gmxx_rxx_int_reg_cn61xx   cnf71xx;
};
typedef union cvmx_gmxx_rxx_int_reg cvmx_gmxx_rxx_int_reg_t;

/**
 * cvmx_gmx#_rx#_jabber
 *
 * GMX_RX_JABBER = The max size packet after which GMX will truncate
 *
 *
 * Notes:
 * CNT must be 8-byte aligned such that CNT[2:0] == 0
 *
 * The packet that will be sent to the packet input logic will have an
 * additionl 8 bytes if GMX_RX_FRM_CTL[PRE_CHK] is set and
 * GMX_RX_FRM_CTL[PRE_STRP] is clear.  The max packet that will be sent is
 * defined as...
 *
 *      max_sized_packet = GMX_RX_JABBER[CNT]+((GMX_RX_FRM_CTL[PRE_CHK] & !GMX_RX_FRM_CTL[PRE_STRP])*8)
 *
 * In XAUI mode prt0 is used for checking.
 */
union cvmx_gmxx_rxx_jabber {
	uint64_t u64;
	struct cvmx_gmxx_rxx_jabber_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t cnt                          : 16; /**< Byte count for jabber check
                                                         Failing packets set the JABBER interrupt and are
                                                         optionally sent with opcode==JABBER
                                                         GMX will truncate the packet to CNT bytes */
#else
	uint64_t cnt                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_rxx_jabber_s         cn30xx;
	struct cvmx_gmxx_rxx_jabber_s         cn31xx;
	struct cvmx_gmxx_rxx_jabber_s         cn38xx;
	struct cvmx_gmxx_rxx_jabber_s         cn38xxp2;
	struct cvmx_gmxx_rxx_jabber_s         cn50xx;
	struct cvmx_gmxx_rxx_jabber_s         cn52xx;
	struct cvmx_gmxx_rxx_jabber_s         cn52xxp1;
	struct cvmx_gmxx_rxx_jabber_s         cn56xx;
	struct cvmx_gmxx_rxx_jabber_s         cn56xxp1;
	struct cvmx_gmxx_rxx_jabber_s         cn58xx;
	struct cvmx_gmxx_rxx_jabber_s         cn58xxp1;
	struct cvmx_gmxx_rxx_jabber_s         cn61xx;
	struct cvmx_gmxx_rxx_jabber_s         cn63xx;
	struct cvmx_gmxx_rxx_jabber_s         cn63xxp1;
	struct cvmx_gmxx_rxx_jabber_s         cn66xx;
	struct cvmx_gmxx_rxx_jabber_s         cn68xx;
	struct cvmx_gmxx_rxx_jabber_s         cn68xxp1;
	struct cvmx_gmxx_rxx_jabber_s         cnf71xx;
};
typedef union cvmx_gmxx_rxx_jabber cvmx_gmxx_rxx_jabber_t;

/**
 * cvmx_gmx#_rx#_pause_drop_time
 *
 * GMX_RX_PAUSE_DROP_TIME = The TIME field in a PAUSE Packet which was dropped due to GMX RX FIFO full condition
 *
 */
union cvmx_gmxx_rxx_pause_drop_time {
	uint64_t u64;
	struct cvmx_gmxx_rxx_pause_drop_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t status                       : 16; /**< Time extracted from the dropped PAUSE packet */
#else
	uint64_t status                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn50xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn52xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn52xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn56xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn56xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn58xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn58xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn61xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn63xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn63xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn66xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn68xx;
	struct cvmx_gmxx_rxx_pause_drop_time_s cn68xxp1;
	struct cvmx_gmxx_rxx_pause_drop_time_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_pause_drop_time cvmx_gmxx_rxx_pause_drop_time_t;

/**
 * cvmx_gmx#_rx#_rx_inbnd
 *
 * GMX_RX_INBND = RGMII InBand Link Status
 *
 *
 * Notes:
 * These fields are only valid if the attached PHY is operating in RGMII mode
 * and supports the optional in-band status (see section 3.4.1 of the RGMII
 * specification, version 1.3 for more information).
 */
union cvmx_gmxx_rxx_rx_inbnd {
	uint64_t u64;
	struct cvmx_gmxx_rxx_rx_inbnd_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t duplex                       : 1;  /**< RGMII Inbound LinkDuplex
                                                         0=half-duplex
                                                         1=full-duplex */
	uint64_t speed                        : 2;  /**< RGMII Inbound LinkSpeed
                                                         00=2.5MHz
                                                         01=25MHz
                                                         10=125MHz
                                                         11=Reserved */
	uint64_t status                       : 1;  /**< RGMII Inbound LinkStatus
                                                         0=down
                                                         1=up */
#else
	uint64_t status                       : 1;
	uint64_t speed                        : 2;
	uint64_t duplex                       : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn30xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn31xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn38xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn38xxp2;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn50xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn58xx;
	struct cvmx_gmxx_rxx_rx_inbnd_s       cn58xxp1;
};
typedef union cvmx_gmxx_rxx_rx_inbnd cvmx_gmxx_rxx_rx_inbnd_t;

/**
 * cvmx_gmx#_rx#_stats_ctl
 *
 * GMX_RX_STATS_CTL = RX Stats Control register
 *
 */
union cvmx_gmxx_rxx_stats_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rd_clr                       : 1;  /**< RX Stats registers will clear on reads */
#else
	uint64_t rd_clr                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn30xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn31xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn38xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn38xxp2;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn50xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn52xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn52xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn56xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn56xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn58xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn58xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn61xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn63xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn63xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn66xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn68xx;
	struct cvmx_gmxx_rxx_stats_ctl_s      cn68xxp1;
	struct cvmx_gmxx_rxx_stats_ctl_s      cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_ctl cvmx_gmxx_rxx_stats_ctl_t;

/**
 * cvmx_gmx#_rx#_stats_octs
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_octs {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of received good packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_s     cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_s     cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s     cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s     cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s     cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s     cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_s     cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_s     cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_octs cvmx_gmxx_rxx_stats_octs_t;

/**
 * cvmx_gmx#_rx#_stats_octs_ctl
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_octs_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of received pause packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_ctl_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_octs_ctl cvmx_gmxx_rxx_stats_octs_ctl_t;

/**
 * cvmx_gmx#_rx#_stats_octs_dmac
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_octs_dmac {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of filtered dmac packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_dmac_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_octs_dmac cvmx_gmxx_rxx_stats_octs_dmac_t;

/**
 * cvmx_gmx#_rx#_stats_octs_drp
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_octs_drp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_octs_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t cnt                          : 48; /**< Octet count of dropped packets */
#else
	uint64_t cnt                          : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn30xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn31xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn38xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn50xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn52xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn56xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn58xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn61xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn63xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn66xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn68xx;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_octs_drp_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_octs_drp cvmx_gmxx_rxx_stats_octs_drp_t;

/**
 * cvmx_gmx#_rx#_stats_pkts
 *
 * GMX_RX_STATS_PKTS
 *
 * Count of good received packets - packets that are not recognized as PAUSE
 * packets, dropped due the DMAC filter, dropped due FIFO full status, or
 * have any other OPCODE (FCS, Length, etc).
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_pkts {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of received good packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_s     cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_s     cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_pkts cvmx_gmxx_rxx_stats_pkts_t;

/**
 * cvmx_gmx#_rx#_stats_pkts_bad
 *
 * GMX_RX_STATS_PKTS_BAD
 *
 * Count of all packets received with some error that were not dropped
 * either due to the dmac filter or lack of room in the receive FIFO.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_pkts_bad {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of bad packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_bad_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_pkts_bad cvmx_gmxx_rxx_stats_pkts_bad_t;

/**
 * cvmx_gmx#_rx#_stats_pkts_ctl
 *
 * GMX_RX_STATS_PKTS_CTL
 *
 * Count of all packets received that were recognized as Flow Control or
 * PAUSE packets.  PAUSE packets with any kind of error are counted in
 * GMX_RX_STATS_PKTS_BAD.  Pause packets can be optionally dropped or
 * forwarded based on the GMX_RX_FRM_CTL[CTL_DRP] bit.  This count
 * increments regardless of whether the packet is dropped.  Pause packets
 * will never be counted in GMX_RX_STATS_PKTS.  Packets dropped due the dmac
 * filter will be counted in GMX_RX_STATS_PKTS_DMAC and not here.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_pkts_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of received pause packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_ctl_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_pkts_ctl cvmx_gmxx_rxx_stats_pkts_ctl_t;

/**
 * cvmx_gmx#_rx#_stats_pkts_dmac
 *
 * GMX_RX_STATS_PKTS_DMAC
 *
 * Count of all packets received that were dropped by the dmac filter.
 * Packets that match the DMAC will be dropped and counted here regardless
 * of if they were bad packets.  These packets will never be counted in
 * GMX_RX_STATS_PKTS.
 *
 * Some packets that were not able to satisify the DECISION_CNT may not
 * actually be dropped by Octeon, but they will be counted here as if they
 * were dropped.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_pkts_dmac {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of filtered dmac packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_dmac_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_pkts_dmac cvmx_gmxx_rxx_stats_pkts_dmac_t;

/**
 * cvmx_gmx#_rx#_stats_pkts_drp
 *
 * GMX_RX_STATS_PKTS_DRP
 *
 * Count of all packets received that were dropped due to a full receive FIFO.
 * This counts both partial packets in which there was enough space in the RX
 * FIFO to begin to buffer and the packet and total drops in which no packet was
 * sent to PKI.  This counts good and bad packets received - all packets dropped
 * by the FIFO.  It does not count packets dropped by the dmac or pause packet
 * filters.
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_RX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_rxx_stats_pkts_drp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t cnt                          : 32; /**< Count of dropped packets */
#else
	uint64_t cnt                          : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn30xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn31xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn38xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn38xxp2;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn50xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn52xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn52xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn56xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn56xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn58xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn58xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn61xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn63xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn63xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn66xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn68xx;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cn68xxp1;
	struct cvmx_gmxx_rxx_stats_pkts_drp_s cnf71xx;
};
typedef union cvmx_gmxx_rxx_stats_pkts_drp cvmx_gmxx_rxx_stats_pkts_drp_t;

/**
 * cvmx_gmx#_rx#_udd_skp
 *
 * GMX_RX_UDD_SKP = Amount of User-defined data before the start of the L2 data
 *
 *
 * Notes:
 * (1) The skip bytes are part of the packet and will be sent down the NCB
 *     packet interface and will be handled by PKI.
 *
 * (2) The system can determine if the UDD bytes are included in the FCS check
 *     by using the FCSSEL field - if the FCS check is enabled.
 *
 * (3) Assume that the preamble/sfd is always at the start of the frame - even
 *     before UDD bytes.  In most cases, there will be no preamble in these
 *     cases since it will be packet interface in direct communication to
 *     another packet interface (MAC to MAC) without a PHY involved.
 *
 * (4) We can still do address filtering and control packet filtering is the
 *     user desires.
 *
 * (5) UDD_SKP must be 0 in half-duplex operation unless
 *     GMX_RX_FRM_CTL[PRE_CHK] is clear.  If GMX_RX_FRM_CTL[PRE_CHK] is clear,
 *     then UDD_SKP will normally be 8.
 *
 * (6) In all cases, the UDD bytes will be sent down the packet interface as
 *     part of the packet.  The UDD bytes are never stripped from the actual
 *     packet.
 *
 * (7) If LEN != 0, then GMX_RX_FRM_CHK[LENERR] will be disabled and GMX_RX_INT_REG[LENERR] will be zero
 */
union cvmx_gmxx_rxx_udd_skp {
	uint64_t u64;
	struct cvmx_gmxx_rxx_udd_skp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t fcssel                       : 1;  /**< Include the skip bytes in the FCS calculation
                                                         0 = all skip bytes are included in FCS
                                                         1 = the skip bytes are not included in FCS
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, FCSSEL must
                                                         be zero. */
	uint64_t reserved_7_7                 : 1;
	uint64_t len                          : 7;  /**< Amount of User-defined data before the start of
                                                         the L2 data.  Zero means L2 comes first.
                                                         Max value is 64.
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, LEN must be
                                                         set to 12 or 16 (depending on HiGig header size)
                                                         to account for the HiGig header. LEN=12 selects
                                                         HiGig/HiGig+, and LEN=16 selects HiGig2. */
#else
	uint64_t len                          : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t fcssel                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_gmxx_rxx_udd_skp_s        cn30xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn31xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn38xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn38xxp2;
	struct cvmx_gmxx_rxx_udd_skp_s        cn50xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn52xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn52xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s        cn56xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn56xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s        cn58xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn58xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s        cn61xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn63xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn63xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s        cn66xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn68xx;
	struct cvmx_gmxx_rxx_udd_skp_s        cn68xxp1;
	struct cvmx_gmxx_rxx_udd_skp_s        cnf71xx;
};
typedef union cvmx_gmxx_rxx_udd_skp cvmx_gmxx_rxx_udd_skp_t;

/**
 * cvmx_gmx#_rx_bp_drop#
 *
 * GMX_RX_BP_DROP = FIFO mark for packet drop
 *
 *
 * Notes:
 * The actual watermark is dynamic with respect to the GMX_RX_PRTS
 * register.  The GMX_RX_PRTS controls the depth of the port's
 * FIFO so as ports are added or removed, the drop point may change.
 *
 * In XAUI mode prt0 is used for checking.
 */
union cvmx_gmxx_rx_bp_dropx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_dropx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mark                         : 6;  /**< Number of 8B ticks to reserve in the RX FIFO.
                                                         When the FIFO exceeds this count, packets will
                                                         be dropped and not buffered.
                                                         MARK should typically be programmed to ports+1.
                                                         Failure to program correctly can lead to system
                                                         instability. */
#else
	uint64_t mark                         : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_dropx_s        cn30xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn31xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn38xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn38xxp2;
	struct cvmx_gmxx_rx_bp_dropx_s        cn50xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn52xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn52xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s        cn56xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn56xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s        cn58xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn58xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s        cn61xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn63xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn63xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s        cn66xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn68xx;
	struct cvmx_gmxx_rx_bp_dropx_s        cn68xxp1;
	struct cvmx_gmxx_rx_bp_dropx_s        cnf71xx;
};
typedef union cvmx_gmxx_rx_bp_dropx cvmx_gmxx_rx_bp_dropx_t;

/**
 * cvmx_gmx#_rx_bp_off#
 *
 * GMX_RX_BP_OFF = Lowater mark for packet drop
 *
 *
 * Notes:
 * In XAUI mode, prt0 is used for checking.
 *
 */
union cvmx_gmxx_rx_bp_offx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_offx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t mark                         : 6;  /**< Water mark (8B ticks) to deassert backpressure */
#else
	uint64_t mark                         : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_offx_s         cn30xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn31xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn38xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn38xxp2;
	struct cvmx_gmxx_rx_bp_offx_s         cn50xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn52xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn52xxp1;
	struct cvmx_gmxx_rx_bp_offx_s         cn56xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn56xxp1;
	struct cvmx_gmxx_rx_bp_offx_s         cn58xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn58xxp1;
	struct cvmx_gmxx_rx_bp_offx_s         cn61xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn63xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn63xxp1;
	struct cvmx_gmxx_rx_bp_offx_s         cn66xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn68xx;
	struct cvmx_gmxx_rx_bp_offx_s         cn68xxp1;
	struct cvmx_gmxx_rx_bp_offx_s         cnf71xx;
};
typedef union cvmx_gmxx_rx_bp_offx cvmx_gmxx_rx_bp_offx_t;

/**
 * cvmx_gmx#_rx_bp_on#
 *
 * GMX_RX_BP_ON = Hiwater mark for port/interface backpressure
 *
 *
 * Notes:
 * In XAUI mode, prt0 is used for checking.
 *
 */
union cvmx_gmxx_rx_bp_onx {
	uint64_t u64;
	struct cvmx_gmxx_rx_bp_onx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t mark                         : 11; /**< Hiwater mark (8B ticks) for backpressure.
                                                         Each register is for an individual port.  In XAUI
                                                         mode, prt0 is used for the unified RX FIFO
                                                         GMX_RX_BP_ON must satisfy
                                                         BP_OFF <= BP_ON < (FIFO_SIZE - BP_DROP)
                                                         A value of zero will immediately assert back
                                                         pressure. */
#else
	uint64_t mark                         : 11;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_gmxx_rx_bp_onx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t mark                         : 9;  /**< Hiwater mark (8B ticks) for backpressure.
                                                         In RGMII mode, the backpressure is given per
                                                         port.  In Spi4 mode, the backpressure is for the
                                                         entire interface.  GMX_RX_BP_ON must satisfy
                                                         BP_OFF <= BP_ON < (FIFO_SIZE - BP_DROP)
                                                         The reset value is half the FIFO.
                                                         Reset value RGMII mode = 0x40  (512bytes)
                                                         Reset value Spi4 mode  = 0x100 (2048bytes)
                                                         A value of zero will immediately assert back
                                                         pressure. */
#else
	uint64_t mark                         : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} cn30xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn31xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn38xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn38xxp2;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn50xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn52xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn52xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn56xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn56xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn58xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn58xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn61xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn63xx;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn63xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cn66xx;
	struct cvmx_gmxx_rx_bp_onx_s          cn68xx;
	struct cvmx_gmxx_rx_bp_onx_s          cn68xxp1;
	struct cvmx_gmxx_rx_bp_onx_cn30xx     cnf71xx;
};
typedef union cvmx_gmxx_rx_bp_onx cvmx_gmxx_rx_bp_onx_t;

/**
 * cvmx_gmx#_rx_hg2_status
 *
 * ** HG2 message CSRs
 *
 */
union cvmx_gmxx_rx_hg2_status {
	uint64_t u64;
	struct cvmx_gmxx_rx_hg2_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t phtim2go                     : 16; /**< Physical time to go for removal of physical link
                                                         pause. Initial value from received HiGig2 msg pkt
                                                         Non-zero only when physical back pressure active */
	uint64_t xof                          : 16; /**< 16 bit xof back pressure vector from HiGig2 msg pkt
                                                         or from CBFC packets.
                                                         Non-zero only when logical back pressure is active
                                                         All bits will be 0 when LGTIM2GO=0 */
	uint64_t lgtim2go                     : 16; /**< Logical packet flow back pressure time remaining
                                                         Initial value set from xof time field of HiGig2
                                                         message packet received or a function of the
                                                         enabled and current timers for CBFC packets.
                                                         Non-zero only when logical back pressure is active */
#else
	uint64_t lgtim2go                     : 16;
	uint64_t xof                          : 16;
	uint64_t phtim2go                     : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_rx_hg2_status_s      cn52xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn52xxp1;
	struct cvmx_gmxx_rx_hg2_status_s      cn56xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn61xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn63xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn63xxp1;
	struct cvmx_gmxx_rx_hg2_status_s      cn66xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn68xx;
	struct cvmx_gmxx_rx_hg2_status_s      cn68xxp1;
	struct cvmx_gmxx_rx_hg2_status_s      cnf71xx;
};
typedef union cvmx_gmxx_rx_hg2_status cvmx_gmxx_rx_hg2_status_t;

/**
 * cvmx_gmx#_rx_pass_en
 *
 * GMX_RX_PASS_EN = Packet pass through mode enable
 *
 * When both Octane ports are running in Spi4 mode, packets can be directly
 * passed from one SPX interface to the other without being processed by the
 * core or PP's.  The register has one bit for each port to enable the pass
 * through feature.
 *
 * Notes:
 * (1) Can only be used in dual Spi4 configs
 *
 * (2) The mapped pass through output port cannot be the destination port for
 *     any Octane core traffic.
 */
union cvmx_gmxx_rx_pass_en {
	uint64_t u64;
	struct cvmx_gmxx_rx_pass_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t en                           : 16; /**< Which ports to configure in pass through mode */
#else
	uint64_t en                           : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_rx_pass_en_s         cn38xx;
	struct cvmx_gmxx_rx_pass_en_s         cn38xxp2;
	struct cvmx_gmxx_rx_pass_en_s         cn58xx;
	struct cvmx_gmxx_rx_pass_en_s         cn58xxp1;
};
typedef union cvmx_gmxx_rx_pass_en cvmx_gmxx_rx_pass_en_t;

/**
 * cvmx_gmx#_rx_pass_map#
 *
 * GMX_RX_PASS_MAP = Packet pass through port map
 *
 */
union cvmx_gmxx_rx_pass_mapx {
	uint64_t u64;
	struct cvmx_gmxx_rx_pass_mapx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t dprt                         : 4;  /**< Destination port to map Spi pass through traffic */
#else
	uint64_t dprt                         : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_rx_pass_mapx_s       cn38xx;
	struct cvmx_gmxx_rx_pass_mapx_s       cn38xxp2;
	struct cvmx_gmxx_rx_pass_mapx_s       cn58xx;
	struct cvmx_gmxx_rx_pass_mapx_s       cn58xxp1;
};
typedef union cvmx_gmxx_rx_pass_mapx cvmx_gmxx_rx_pass_mapx_t;

/**
 * cvmx_gmx#_rx_prt_info
 *
 * GMX_RX_PRT_INFO = Report the RX status for port
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of DROP and COMMIT are used.
 *
 */
union cvmx_gmxx_rx_prt_info {
	uint64_t u64;
	struct cvmx_gmxx_rx_prt_info_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t drop                         : 16; /**< Per port indication that data was dropped */
	uint64_t commit                       : 16; /**< Per port indication that SOP was accepted */
#else
	uint64_t commit                       : 16;
	uint64_t drop                         : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_rx_prt_info_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t drop                         : 3;  /**< Per port indication that data was dropped */
	uint64_t reserved_3_15                : 13;
	uint64_t commit                       : 3;  /**< Per port indication that SOP was accepted */
#else
	uint64_t commit                       : 3;
	uint64_t reserved_3_15                : 13;
	uint64_t drop                         : 3;
	uint64_t reserved_19_63               : 45;
#endif
	} cn30xx;
	struct cvmx_gmxx_rx_prt_info_cn30xx   cn31xx;
	struct cvmx_gmxx_rx_prt_info_s        cn38xx;
	struct cvmx_gmxx_rx_prt_info_cn30xx   cn50xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t drop                         : 4;  /**< Per port indication that data was dropped */
	uint64_t reserved_4_15                : 12;
	uint64_t commit                       : 4;  /**< Per port indication that SOP was accepted */
#else
	uint64_t commit                       : 4;
	uint64_t reserved_4_15                : 12;
	uint64_t drop                         : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn52xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn56xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn56xxp1;
	struct cvmx_gmxx_rx_prt_info_s        cn58xx;
	struct cvmx_gmxx_rx_prt_info_s        cn58xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn61xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn63xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn63xxp1;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn66xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn68xx;
	struct cvmx_gmxx_rx_prt_info_cn52xx   cn68xxp1;
	struct cvmx_gmxx_rx_prt_info_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t drop                         : 2;  /**< Per port indication that data was dropped */
	uint64_t reserved_2_15                : 14;
	uint64_t commit                       : 2;  /**< Per port indication that SOP was accepted */
#else
	uint64_t commit                       : 2;
	uint64_t reserved_2_15                : 14;
	uint64_t drop                         : 2;
	uint64_t reserved_18_63               : 46;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_rx_prt_info cvmx_gmxx_rx_prt_info_t;

/**
 * cvmx_gmx#_rx_prts
 *
 * GMX_RX_PRTS = Number of FIFOs to carve the RX buffer into
 *
 *
 * Notes:
 * GMX_RX_PRTS[PRTS] must be set to '1' in XAUI mode.
 *
 */
union cvmx_gmxx_rx_prts {
	uint64_t u64;
	struct cvmx_gmxx_rx_prts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t prts                         : 3;  /**< In SGMII/1000Base-X mode, the RX buffer can be
                                                         carved into several logical buffers depending on
                                                         the number or implemented ports.
                                                         0 or 1 port  = 512ticks / 4096bytes
                                                         2 ports      = 256ticks / 2048bytes
                                                         3 or 4 ports = 128ticks / 1024bytes */
#else
	uint64_t prts                         : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_gmxx_rx_prts_s            cn30xx;
	struct cvmx_gmxx_rx_prts_s            cn31xx;
	struct cvmx_gmxx_rx_prts_s            cn38xx;
	struct cvmx_gmxx_rx_prts_s            cn38xxp2;
	struct cvmx_gmxx_rx_prts_s            cn50xx;
	struct cvmx_gmxx_rx_prts_s            cn52xx;
	struct cvmx_gmxx_rx_prts_s            cn52xxp1;
	struct cvmx_gmxx_rx_prts_s            cn56xx;
	struct cvmx_gmxx_rx_prts_s            cn56xxp1;
	struct cvmx_gmxx_rx_prts_s            cn58xx;
	struct cvmx_gmxx_rx_prts_s            cn58xxp1;
	struct cvmx_gmxx_rx_prts_s            cn61xx;
	struct cvmx_gmxx_rx_prts_s            cn63xx;
	struct cvmx_gmxx_rx_prts_s            cn63xxp1;
	struct cvmx_gmxx_rx_prts_s            cn66xx;
	struct cvmx_gmxx_rx_prts_s            cn68xx;
	struct cvmx_gmxx_rx_prts_s            cn68xxp1;
	struct cvmx_gmxx_rx_prts_s            cnf71xx;
};
typedef union cvmx_gmxx_rx_prts cvmx_gmxx_rx_prts_t;

/**
 * cvmx_gmx#_rx_tx_status
 *
 * GMX_RX_TX_STATUS = GMX RX/TX Status
 *
 */
union cvmx_gmxx_rx_tx_status {
	uint64_t u64;
	struct cvmx_gmxx_rx_tx_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t tx                           : 3;  /**< Transmit data since last read */
	uint64_t reserved_3_3                 : 1;
	uint64_t rx                           : 3;  /**< Receive data since last read */
#else
	uint64_t rx                           : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t tx                           : 3;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_gmxx_rx_tx_status_s       cn30xx;
	struct cvmx_gmxx_rx_tx_status_s       cn31xx;
	struct cvmx_gmxx_rx_tx_status_s       cn50xx;
};
typedef union cvmx_gmxx_rx_tx_status cvmx_gmxx_rx_tx_status_t;

/**
 * cvmx_gmx#_rx_xaui_bad_col
 */
union cvmx_gmxx_rx_xaui_bad_col {
	uint64_t u64;
	struct cvmx_gmxx_rx_xaui_bad_col_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t val                          : 1;  /**< Set when GMX_RX_INT_REG[PCTERR] is set.
                                                         (XAUI mode only) */
	uint64_t state                        : 3;  /**< When GMX_RX_INT_REG[PCTERR] is set, STATE will
                                                         conatin the receive state at the time of the
                                                         error.
                                                         (XAUI mode only) */
	uint64_t lane_rxc                     : 4;  /**< When GMX_RX_INT_REG[PCTERR] is set, LANE_RXC will
                                                         conatin the XAUI column at the time of the error.
                                                         (XAUI mode only) */
	uint64_t lane_rxd                     : 32; /**< When GMX_RX_INT_REG[PCTERR] is set, LANE_RXD will
                                                         conatin the XAUI column at the time of the error.
                                                         (XAUI mode only) */
#else
	uint64_t lane_rxd                     : 32;
	uint64_t lane_rxc                     : 4;
	uint64_t state                        : 3;
	uint64_t val                          : 1;
	uint64_t reserved_40_63               : 24;
#endif
	} s;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn52xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn52xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn56xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn56xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn61xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn63xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn63xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn66xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn68xx;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cn68xxp1;
	struct cvmx_gmxx_rx_xaui_bad_col_s    cnf71xx;
};
typedef union cvmx_gmxx_rx_xaui_bad_col cvmx_gmxx_rx_xaui_bad_col_t;

/**
 * cvmx_gmx#_rx_xaui_ctl
 */
union cvmx_gmxx_rx_xaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rx_xaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t status                       : 2;  /**< Link Status
                                                         0=Link OK
                                                         1=Local Fault
                                                         2=Remote Fault
                                                         3=Reserved
                                                         (XAUI mode only) */
#else
	uint64_t status                       : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn52xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn52xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn56xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn56xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn61xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn63xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn63xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn66xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn68xx;
	struct cvmx_gmxx_rx_xaui_ctl_s        cn68xxp1;
	struct cvmx_gmxx_rx_xaui_ctl_s        cnf71xx;
};
typedef union cvmx_gmxx_rx_xaui_ctl cvmx_gmxx_rx_xaui_ctl_t;

/**
 * cvmx_gmx#_rxaui_ctl
 */
union cvmx_gmxx_rxaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_rxaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t disparity                    : 1;  /**< Selects which disparity calculation to use when
                                                         combining or splitting the RXAUI lanes.
                                                         0=Interleave lanes before PCS layer
                                                           As described in the Dune Networks/Broadcom
                                                           RXAUI v2.1 specification.
                                                           (obeys 6.25GHz SERDES disparity)
                                                         1=Interleave lanes after PCS layer
                                                           As described in the Marvell RXAUI Interface
                                                           specification.
                                                           (does not obey 6.25GHz SERDES disparity)
                                                         (RXAUI mode only) */
#else
	uint64_t disparity                    : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_rxaui_ctl_s          cn68xx;
	struct cvmx_gmxx_rxaui_ctl_s          cn68xxp1;
};
typedef union cvmx_gmxx_rxaui_ctl cvmx_gmxx_rxaui_ctl_t;

/**
 * cvmx_gmx#_smac#
 *
 * GMX_SMAC = Packet SMAC
 *
 */
union cvmx_gmxx_smacx {
	uint64_t u64;
	struct cvmx_gmxx_smacx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t smac                         : 48; /**< The SMAC field is used for generating and
                                                         accepting Control Pause packets */
#else
	uint64_t smac                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_smacx_s              cn30xx;
	struct cvmx_gmxx_smacx_s              cn31xx;
	struct cvmx_gmxx_smacx_s              cn38xx;
	struct cvmx_gmxx_smacx_s              cn38xxp2;
	struct cvmx_gmxx_smacx_s              cn50xx;
	struct cvmx_gmxx_smacx_s              cn52xx;
	struct cvmx_gmxx_smacx_s              cn52xxp1;
	struct cvmx_gmxx_smacx_s              cn56xx;
	struct cvmx_gmxx_smacx_s              cn56xxp1;
	struct cvmx_gmxx_smacx_s              cn58xx;
	struct cvmx_gmxx_smacx_s              cn58xxp1;
	struct cvmx_gmxx_smacx_s              cn61xx;
	struct cvmx_gmxx_smacx_s              cn63xx;
	struct cvmx_gmxx_smacx_s              cn63xxp1;
	struct cvmx_gmxx_smacx_s              cn66xx;
	struct cvmx_gmxx_smacx_s              cn68xx;
	struct cvmx_gmxx_smacx_s              cn68xxp1;
	struct cvmx_gmxx_smacx_s              cnf71xx;
};
typedef union cvmx_gmxx_smacx cvmx_gmxx_smacx_t;

/**
 * cvmx_gmx#_soft_bist
 *
 * GMX_SOFT_BIST = Software BIST Control
 *
 */
union cvmx_gmxx_soft_bist {
	uint64_t u64;
	struct cvmx_gmxx_soft_bist_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t start_bist                   : 1;  /**< Run BIST on all memories in the XAUI/RXAUI
                                                         CLK domain */
	uint64_t clear_bist                   : 1;  /**< Choose between full BIST and CLEAR bist
                                                         0=Run full BIST
                                                         1=Only run clear BIST */
#else
	uint64_t clear_bist                   : 1;
	uint64_t start_bist                   : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_gmxx_soft_bist_s          cn63xx;
	struct cvmx_gmxx_soft_bist_s          cn63xxp1;
	struct cvmx_gmxx_soft_bist_s          cn66xx;
	struct cvmx_gmxx_soft_bist_s          cn68xx;
	struct cvmx_gmxx_soft_bist_s          cn68xxp1;
};
typedef union cvmx_gmxx_soft_bist cvmx_gmxx_soft_bist_t;

/**
 * cvmx_gmx#_stat_bp
 *
 * GMX_STAT_BP = Number of cycles that the TX/Stats block has help up operation
 *
 *
 * Notes:
 * It has no relationship with the TX FIFO per se.  The TX engine sends packets
 * from PKO and upon completion, sends a command to the TX stats block for an
 * update based on the packet size.  The stats operation can take a few cycles -
 * normally not enough to be visible considering the 64B min packet size that is
 * ethernet convention.
 *
 * In the rare case in which SW attempted to schedule really, really, small packets
 * or the sclk (6xxx) is running ass-slow, then the stats updates may not happen in
 * real time and can back up the TX engine.
 *
 * This counter is the number of cycles in which the TX engine was stalled.  In
 * normal operation, it should always be zeros.
 */
union cvmx_gmxx_stat_bp {
	uint64_t u64;
	struct cvmx_gmxx_stat_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t bp                           : 1;  /**< Current TX stats BP state
                                                         When the TX stats machine cannot update the stats
                                                         registers quickly enough, the machine has the
                                                         ability to BP TX datapath.  This is a rare event
                                                         and will not occur in normal operation.
                                                         0 = no backpressure is applied
                                                         1 = backpressure is applied to TX datapath to
                                                             allow stat update operations to complete */
	uint64_t cnt                          : 16; /**< Number of cycles that BP has been asserted
                                                         Saturating counter */
#else
	uint64_t cnt                          : 16;
	uint64_t bp                           : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} s;
	struct cvmx_gmxx_stat_bp_s            cn30xx;
	struct cvmx_gmxx_stat_bp_s            cn31xx;
	struct cvmx_gmxx_stat_bp_s            cn38xx;
	struct cvmx_gmxx_stat_bp_s            cn38xxp2;
	struct cvmx_gmxx_stat_bp_s            cn50xx;
	struct cvmx_gmxx_stat_bp_s            cn52xx;
	struct cvmx_gmxx_stat_bp_s            cn52xxp1;
	struct cvmx_gmxx_stat_bp_s            cn56xx;
	struct cvmx_gmxx_stat_bp_s            cn56xxp1;
	struct cvmx_gmxx_stat_bp_s            cn58xx;
	struct cvmx_gmxx_stat_bp_s            cn58xxp1;
	struct cvmx_gmxx_stat_bp_s            cn61xx;
	struct cvmx_gmxx_stat_bp_s            cn63xx;
	struct cvmx_gmxx_stat_bp_s            cn63xxp1;
	struct cvmx_gmxx_stat_bp_s            cn66xx;
	struct cvmx_gmxx_stat_bp_s            cn68xx;
	struct cvmx_gmxx_stat_bp_s            cn68xxp1;
	struct cvmx_gmxx_stat_bp_s            cnf71xx;
};
typedef union cvmx_gmxx_stat_bp cvmx_gmxx_stat_bp_t;

/**
 * cvmx_gmx#_tb_reg
 *
 * DON'T PUT IN HRM*
 *
 */
union cvmx_gmxx_tb_reg {
	uint64_t u64;
	struct cvmx_gmxx_tb_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t wr_magic                     : 1;  /**< Enter stats model magic mode */
#else
	uint64_t wr_magic                     : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_tb_reg_s             cn61xx;
	struct cvmx_gmxx_tb_reg_s             cn66xx;
	struct cvmx_gmxx_tb_reg_s             cn68xx;
	struct cvmx_gmxx_tb_reg_s             cnf71xx;
};
typedef union cvmx_gmxx_tb_reg cvmx_gmxx_tb_reg_t;

/**
 * cvmx_gmx#_tx#_append
 *
 * GMX_TX_APPEND = Packet TX Append Control
 *
 */
union cvmx_gmxx_txx_append {
	uint64_t u64;
	struct cvmx_gmxx_txx_append_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t force_fcs                    : 1;  /**< Append the Ethernet FCS on each pause packet
                                                         when FCS is clear.  Pause packets are normally
                                                         padded to 60 bytes.  If GMX_TX_MIN_PKT[MIN_SIZE]
                                                         exceeds 59, then FORCE_FCS will not be used. */
	uint64_t fcs                          : 1;  /**< Append the Ethernet FCS on each packet */
	uint64_t pad                          : 1;  /**< Append PAD bytes such that min sized */
	uint64_t preamble                     : 1;  /**< Prepend the Ethernet preamble on each transfer
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, PREAMBLE
                                                         must be zero. */
#else
	uint64_t preamble                     : 1;
	uint64_t pad                          : 1;
	uint64_t fcs                          : 1;
	uint64_t force_fcs                    : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_txx_append_s         cn30xx;
	struct cvmx_gmxx_txx_append_s         cn31xx;
	struct cvmx_gmxx_txx_append_s         cn38xx;
	struct cvmx_gmxx_txx_append_s         cn38xxp2;
	struct cvmx_gmxx_txx_append_s         cn50xx;
	struct cvmx_gmxx_txx_append_s         cn52xx;
	struct cvmx_gmxx_txx_append_s         cn52xxp1;
	struct cvmx_gmxx_txx_append_s         cn56xx;
	struct cvmx_gmxx_txx_append_s         cn56xxp1;
	struct cvmx_gmxx_txx_append_s         cn58xx;
	struct cvmx_gmxx_txx_append_s         cn58xxp1;
	struct cvmx_gmxx_txx_append_s         cn61xx;
	struct cvmx_gmxx_txx_append_s         cn63xx;
	struct cvmx_gmxx_txx_append_s         cn63xxp1;
	struct cvmx_gmxx_txx_append_s         cn66xx;
	struct cvmx_gmxx_txx_append_s         cn68xx;
	struct cvmx_gmxx_txx_append_s         cn68xxp1;
	struct cvmx_gmxx_txx_append_s         cnf71xx;
};
typedef union cvmx_gmxx_txx_append cvmx_gmxx_txx_append_t;

/**
 * cvmx_gmx#_tx#_burst
 *
 * GMX_TX_BURST = Packet TX Burst Counter
 *
 */
union cvmx_gmxx_txx_burst {
	uint64_t u64;
	struct cvmx_gmxx_txx_burst_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t burst                        : 16; /**< Burst (refer to 802.3 to set correctly)
                                                         Only valid for 1000Mbs half-duplex operation
                                                          halfdup / 1000Mbs: 0x2000
                                                          all other modes:   0x0
                                                         (SGMII/1000Base-X only) */
#else
	uint64_t burst                        : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_burst_s          cn30xx;
	struct cvmx_gmxx_txx_burst_s          cn31xx;
	struct cvmx_gmxx_txx_burst_s          cn38xx;
	struct cvmx_gmxx_txx_burst_s          cn38xxp2;
	struct cvmx_gmxx_txx_burst_s          cn50xx;
	struct cvmx_gmxx_txx_burst_s          cn52xx;
	struct cvmx_gmxx_txx_burst_s          cn52xxp1;
	struct cvmx_gmxx_txx_burst_s          cn56xx;
	struct cvmx_gmxx_txx_burst_s          cn56xxp1;
	struct cvmx_gmxx_txx_burst_s          cn58xx;
	struct cvmx_gmxx_txx_burst_s          cn58xxp1;
	struct cvmx_gmxx_txx_burst_s          cn61xx;
	struct cvmx_gmxx_txx_burst_s          cn63xx;
	struct cvmx_gmxx_txx_burst_s          cn63xxp1;
	struct cvmx_gmxx_txx_burst_s          cn66xx;
	struct cvmx_gmxx_txx_burst_s          cn68xx;
	struct cvmx_gmxx_txx_burst_s          cn68xxp1;
	struct cvmx_gmxx_txx_burst_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_burst cvmx_gmxx_txx_burst_t;

/**
 * cvmx_gmx#_tx#_cbfc_xoff
 */
union cvmx_gmxx_txx_cbfc_xoff {
	uint64_t u64;
	struct cvmx_gmxx_txx_cbfc_xoff_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t xoff                         : 16; /**< Which ports to backpressure
                                                         Do not write in HiGig2 mode i.e. when
                                                         GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                         GMX_RX_UDD_SKP[SKIP]=16. */
#else
	uint64_t xoff                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn52xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn56xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn61xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn63xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn63xxp1;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn66xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn68xx;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cn68xxp1;
	struct cvmx_gmxx_txx_cbfc_xoff_s      cnf71xx;
};
typedef union cvmx_gmxx_txx_cbfc_xoff cvmx_gmxx_txx_cbfc_xoff_t;

/**
 * cvmx_gmx#_tx#_cbfc_xon
 */
union cvmx_gmxx_txx_cbfc_xon {
	uint64_t u64;
	struct cvmx_gmxx_txx_cbfc_xon_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t xon                          : 16; /**< Which ports to stop backpressure
                                                         Do not write in HiGig2 mode i.e. when
                                                         GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                         GMX_RX_UDD_SKP[SKIP]=16. */
#else
	uint64_t xon                          : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn52xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn56xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn61xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn63xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn63xxp1;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn66xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn68xx;
	struct cvmx_gmxx_txx_cbfc_xon_s       cn68xxp1;
	struct cvmx_gmxx_txx_cbfc_xon_s       cnf71xx;
};
typedef union cvmx_gmxx_txx_cbfc_xon cvmx_gmxx_txx_cbfc_xon_t;

/**
 * cvmx_gmx#_tx#_clk
 *
 * Per Port
 *
 *
 * GMX_TX_CLK = RGMII TX Clock Generation Register
 *
 * Notes:
 * Programming Restrictions:
 *  (1) In RGMII mode, if GMX_PRT_CFG[SPEED]==0, then CLK_CNT must be > 1.
 *  (2) In MII mode, CLK_CNT == 1
 *  (3) In RGMII or GMII mode, if CLK_CNT==0, Octeon will not generate a tx clock.
 *
 * RGMII Example:
 *  Given a 125MHz PLL reference clock...
 *   CLK_CNT ==  1 ==> 125.0MHz TXC clock period (8ns* 1)
 *   CLK_CNT ==  5 ==>  25.0MHz TXC clock period (8ns* 5)
 *   CLK_CNT == 50 ==>   2.5MHz TXC clock period (8ns*50)
 */
union cvmx_gmxx_txx_clk {
	uint64_t u64;
	struct cvmx_gmxx_txx_clk_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t clk_cnt                      : 6;  /**< Controls the RGMII TXC frequency
                                                         When PLL is used, TXC(phase) =
                                                          spi4_tx_pll_ref_clk(period)/2*CLK_CNT
                                                         When PLL bypass is used, TXC(phase) =
                                                          spi4_tx_pll_ref_clk(period)*2*CLK_CNT
                                                         NOTE: CLK_CNT==0 will not generate any clock
                                                         if CLK_CNT > 1 if GMX_PRT_CFG[SPEED]==0 */
#else
	uint64_t clk_cnt                      : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_gmxx_txx_clk_s            cn30xx;
	struct cvmx_gmxx_txx_clk_s            cn31xx;
	struct cvmx_gmxx_txx_clk_s            cn38xx;
	struct cvmx_gmxx_txx_clk_s            cn38xxp2;
	struct cvmx_gmxx_txx_clk_s            cn50xx;
	struct cvmx_gmxx_txx_clk_s            cn58xx;
	struct cvmx_gmxx_txx_clk_s            cn58xxp1;
};
typedef union cvmx_gmxx_txx_clk cvmx_gmxx_txx_clk_t;

/**
 * cvmx_gmx#_tx#_ctl
 *
 * GMX_TX_CTL = TX Control register
 *
 */
union cvmx_gmxx_txx_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t xsdef_en                     : 1;  /**< Enables the excessive deferral check for stats
                                                         and interrupts
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol_en                     : 1;  /**< Enables the excessive collision check for stats
                                                         and interrupts
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t xscol_en                     : 1;
	uint64_t xsdef_en                     : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_gmxx_txx_ctl_s            cn30xx;
	struct cvmx_gmxx_txx_ctl_s            cn31xx;
	struct cvmx_gmxx_txx_ctl_s            cn38xx;
	struct cvmx_gmxx_txx_ctl_s            cn38xxp2;
	struct cvmx_gmxx_txx_ctl_s            cn50xx;
	struct cvmx_gmxx_txx_ctl_s            cn52xx;
	struct cvmx_gmxx_txx_ctl_s            cn52xxp1;
	struct cvmx_gmxx_txx_ctl_s            cn56xx;
	struct cvmx_gmxx_txx_ctl_s            cn56xxp1;
	struct cvmx_gmxx_txx_ctl_s            cn58xx;
	struct cvmx_gmxx_txx_ctl_s            cn58xxp1;
	struct cvmx_gmxx_txx_ctl_s            cn61xx;
	struct cvmx_gmxx_txx_ctl_s            cn63xx;
	struct cvmx_gmxx_txx_ctl_s            cn63xxp1;
	struct cvmx_gmxx_txx_ctl_s            cn66xx;
	struct cvmx_gmxx_txx_ctl_s            cn68xx;
	struct cvmx_gmxx_txx_ctl_s            cn68xxp1;
	struct cvmx_gmxx_txx_ctl_s            cnf71xx;
};
typedef union cvmx_gmxx_txx_ctl cvmx_gmxx_txx_ctl_t;

/**
 * cvmx_gmx#_tx#_min_pkt
 *
 * GMX_TX_MIN_PKT = Packet TX Min Size Packet (PAD upto min size)
 *
 */
union cvmx_gmxx_txx_min_pkt {
	uint64_t u64;
	struct cvmx_gmxx_txx_min_pkt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t min_size                     : 8;  /**< Min frame in bytes before the FCS is applied
                                                         Padding is only appened when GMX_TX_APPEND[PAD]
                                                         for the coresponding port is set.
                                                         In SGMII mode, packets will be padded to
                                                          MIN_SIZE+1. The reset value will pad to 60 bytes.
                                                         In XAUI mode, packets will be padded to
                                                          MIN(252,(MIN_SIZE+1 & ~0x3))
                                                         When GMX_TX_XAUI_CTL[HG_EN] is set, the HiGig
                                                          header (12B or 16B) is normally added to the
                                                          packet, so MIN_SIZE should be 59+12=71B for
                                                          HiGig or 59+16=75B for HiGig2. */
#else
	uint64_t min_size                     : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_gmxx_txx_min_pkt_s        cn30xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn31xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn38xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn38xxp2;
	struct cvmx_gmxx_txx_min_pkt_s        cn50xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn52xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn52xxp1;
	struct cvmx_gmxx_txx_min_pkt_s        cn56xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn56xxp1;
	struct cvmx_gmxx_txx_min_pkt_s        cn58xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn58xxp1;
	struct cvmx_gmxx_txx_min_pkt_s        cn61xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn63xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn63xxp1;
	struct cvmx_gmxx_txx_min_pkt_s        cn66xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn68xx;
	struct cvmx_gmxx_txx_min_pkt_s        cn68xxp1;
	struct cvmx_gmxx_txx_min_pkt_s        cnf71xx;
};
typedef union cvmx_gmxx_txx_min_pkt cvmx_gmxx_txx_min_pkt_t;

/**
 * cvmx_gmx#_tx#_pause_pkt_interval
 *
 * GMX_TX_PAUSE_PKT_INTERVAL = Packet TX Pause Packet transmission interval - how often PAUSE packets will be sent
 *
 *
 * Notes:
 * Choosing proper values of GMX_TX_PAUSE_PKT_TIME[TIME] and
 * GMX_TX_PAUSE_PKT_INTERVAL[INTERVAL] can be challenging to the system
 * designer.  It is suggested that TIME be much greater than INTERVAL and
 * GMX_TX_PAUSE_ZERO[SEND] be set.  This allows a periodic refresh of the PAUSE
 * count and then when the backpressure condition is lifted, a PAUSE packet
 * with TIME==0 will be sent indicating that Octane is ready for additional
 * data.
 *
 * If the system chooses to not set GMX_TX_PAUSE_ZERO[SEND], then it is
 * suggested that TIME and INTERVAL are programmed such that they satisify the
 * following rule...
 *
 *    INTERVAL <= TIME - (largest_pkt_size + IFG + pause_pkt_size)
 *
 * where largest_pkt_size is that largest packet that the system can send
 * (normally 1518B), IFG is the interframe gap and pause_pkt_size is the size
 * of the PAUSE packet (normally 64B).
 */
union cvmx_gmxx_txx_pause_pkt_interval {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_pkt_interval_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t interval                     : 16; /**< Arbitrate for a 802.3 pause packet, HiGig2 message,
                                                         or CBFC pause packet every (INTERVAL*512)
                                                         bit-times.
                                                         Normally, 0 < INTERVAL < GMX_TX_PAUSE_PKT_TIME
                                                         INTERVAL=0, will only send a single PAUSE packet
                                                         for each backpressure event */
#else
	uint64_t interval                     : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn30xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn31xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn38xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn38xxp2;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn50xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn52xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn56xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn56xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn58xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn58xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn61xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn63xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn66xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn68xx;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_pkt_interval_s cnf71xx;
};
typedef union cvmx_gmxx_txx_pause_pkt_interval cvmx_gmxx_txx_pause_pkt_interval_t;

/**
 * cvmx_gmx#_tx#_pause_pkt_time
 *
 * GMX_TX_PAUSE_PKT_TIME = Packet TX Pause Packet pause_time field
 *
 *
 * Notes:
 * Choosing proper values of GMX_TX_PAUSE_PKT_TIME[TIME] and
 * GMX_TX_PAUSE_PKT_INTERVAL[INTERVAL] can be challenging to the system
 * designer.  It is suggested that TIME be much greater than INTERVAL and
 * GMX_TX_PAUSE_ZERO[SEND] be set.  This allows a periodic refresh of the PAUSE
 * count and then when the backpressure condition is lifted, a PAUSE packet
 * with TIME==0 will be sent indicating that Octane is ready for additional
 * data.
 *
 * If the system chooses to not set GMX_TX_PAUSE_ZERO[SEND], then it is
 * suggested that TIME and INTERVAL are programmed such that they satisify the
 * following rule...
 *
 *    INTERVAL <= TIME - (largest_pkt_size + IFG + pause_pkt_size)
 *
 * where largest_pkt_size is that largest packet that the system can send
 * (normally 1518B), IFG is the interframe gap and pause_pkt_size is the size
 * of the PAUSE packet (normally 64B).
 */
union cvmx_gmxx_txx_pause_pkt_time {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_pkt_time_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< The pause_time field placed in outbnd 802.3 pause
                                                         packets, HiGig2 messages, or CBFC pause packets.
                                                         pause_time is in 512 bit-times
                                                         Normally, TIME > GMX_TX_PAUSE_PKT_INTERVAL */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn30xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn31xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn38xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn38xxp2;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn50xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn52xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn52xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn56xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn56xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn58xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn58xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn61xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn63xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn63xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn66xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn68xx;
	struct cvmx_gmxx_txx_pause_pkt_time_s cn68xxp1;
	struct cvmx_gmxx_txx_pause_pkt_time_s cnf71xx;
};
typedef union cvmx_gmxx_txx_pause_pkt_time cvmx_gmxx_txx_pause_pkt_time_t;

/**
 * cvmx_gmx#_tx#_pause_togo
 *
 * GMX_TX_PAUSE_TOGO = Packet TX Amount of time remaining to backpressure
 *
 */
union cvmx_gmxx_txx_pause_togo {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_togo_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t msg_time                     : 16; /**< Amount of time remaining to backpressure
                                                         From the higig2 physical message pause timer
                                                         (only valid on port0) */
	uint64_t time                         : 16; /**< Amount of time remaining to backpressure
                                                         From the standard 802.3 pause timer */
#else
	uint64_t time                         : 16;
	uint64_t msg_time                     : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_togo_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< Amount of time remaining to backpressure */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} cn30xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn31xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn38xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn38xxp2;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn50xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn52xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn52xxp1;
	struct cvmx_gmxx_txx_pause_togo_s     cn56xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn56xxp1;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn58xx;
	struct cvmx_gmxx_txx_pause_togo_cn30xx cn58xxp1;
	struct cvmx_gmxx_txx_pause_togo_s     cn61xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn63xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn63xxp1;
	struct cvmx_gmxx_txx_pause_togo_s     cn66xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn68xx;
	struct cvmx_gmxx_txx_pause_togo_s     cn68xxp1;
	struct cvmx_gmxx_txx_pause_togo_s     cnf71xx;
};
typedef union cvmx_gmxx_txx_pause_togo cvmx_gmxx_txx_pause_togo_t;

/**
 * cvmx_gmx#_tx#_pause_zero
 *
 * GMX_TX_PAUSE_ZERO = Packet TX Amount of time remaining to backpressure
 *
 */
union cvmx_gmxx_txx_pause_zero {
	uint64_t u64;
	struct cvmx_gmxx_txx_pause_zero_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t send                         : 1;  /**< When backpressure condition clear, send PAUSE
                                                         packet with pause_time of zero to enable the
                                                         channel */
#else
	uint64_t send                         : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_txx_pause_zero_s     cn30xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn31xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn38xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn38xxp2;
	struct cvmx_gmxx_txx_pause_zero_s     cn50xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn52xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn52xxp1;
	struct cvmx_gmxx_txx_pause_zero_s     cn56xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn56xxp1;
	struct cvmx_gmxx_txx_pause_zero_s     cn58xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn58xxp1;
	struct cvmx_gmxx_txx_pause_zero_s     cn61xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn63xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn63xxp1;
	struct cvmx_gmxx_txx_pause_zero_s     cn66xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn68xx;
	struct cvmx_gmxx_txx_pause_zero_s     cn68xxp1;
	struct cvmx_gmxx_txx_pause_zero_s     cnf71xx;
};
typedef union cvmx_gmxx_txx_pause_zero cvmx_gmxx_txx_pause_zero_t;

/**
 * cvmx_gmx#_tx#_pipe
 */
union cvmx_gmxx_txx_pipe {
	uint64_t u64;
	struct cvmx_gmxx_txx_pipe_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_33_63               : 31;
	uint64_t ign_bp                       : 1;  /**< When set, GMX will not throttle the TX machines
                                                         if the PIPE return FIFO fills up.
                                                         IGN_BP should be clear in normal operation. */
	uint64_t reserved_21_31               : 11;
	uint64_t nump                         : 5;  /**< Number of pipes this port|channel supports.
                                                         In SGMII mode, each port binds to one pipe.
                                                         In XAUI/RXAUI mode, the port can bind upto 16
                                                         consecutive pipes.
                                                         SGMII      mode, NUMP = 0 or 1.
                                                         XAUI/RXAUI mode, NUMP = 0 or 1-16.
                                                         0 = Disabled */
	uint64_t reserved_7_15                : 9;
	uint64_t base                         : 7;  /**< When NUMP is non-zero, indicates the base pipe
                                                         number this port|channel will accept.
                                                         This port will accept pko packets from pipes in
                                                         the range of:
                                                           BASE .. (BASE+(NUMP-1))
                                                         BASE and NUMP must be constrained such that
                                                           1) BASE+(NUMP-1) < 127
                                                           2) Each used PKO pipe must map to exactly
                                                              one port|channel
                                                           3) The pipe ranges must be consistent with
                                                              the PKO configuration. */
#else
	uint64_t base                         : 7;
	uint64_t reserved_7_15                : 9;
	uint64_t nump                         : 5;
	uint64_t reserved_21_31               : 11;
	uint64_t ign_bp                       : 1;
	uint64_t reserved_33_63               : 31;
#endif
	} s;
	struct cvmx_gmxx_txx_pipe_s           cn68xx;
	struct cvmx_gmxx_txx_pipe_s           cn68xxp1;
};
typedef union cvmx_gmxx_txx_pipe cvmx_gmxx_txx_pipe_t;

/**
 * cvmx_gmx#_tx#_sgmii_ctl
 */
union cvmx_gmxx_txx_sgmii_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_sgmii_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t align                        : 1;  /**< Align the transmission to even cycles

                                                         Recommended value is:
                                                            ALIGN = !GMX_TX_APPEND[PREAMBLE]

                                                         (See the Transmit Conversion to Code groups
                                                          section in the SGMII Interface chapter of the
                                                          HRM for a complete discussion)

                                                         0 = Data can be sent on any cycle
                                                             In this mode, the interface will function at
                                                             maximum bandwidth. It is possible to for the
                                                             TX PCS machine to drop first byte of the TX
                                                             frame.  When GMX_TX_APPEND[PREAMBLE] is set,
                                                             the first byte will be a preamble byte which
                                                             can be dropped to compensate for an extended
                                                             IPG.

                                                         1 = Data will only be sent on even cycles.
                                                             In this mode, there can be bandwidth
                                                             implications when sending odd-byte packets as
                                                             the IPG can extend an extra cycle.
                                                             There will be no loss of data.

                                                         (SGMII/1000Base-X only) */
#else
	uint64_t align                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn52xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn52xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn56xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn56xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn61xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn63xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn63xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn66xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn68xx;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cn68xxp1;
	struct cvmx_gmxx_txx_sgmii_ctl_s      cnf71xx;
};
typedef union cvmx_gmxx_txx_sgmii_ctl cvmx_gmxx_txx_sgmii_ctl_t;

/**
 * cvmx_gmx#_tx#_slot
 *
 * GMX_TX_SLOT = Packet TX Slottime Counter
 *
 */
union cvmx_gmxx_txx_slot {
	uint64_t u64;
	struct cvmx_gmxx_txx_slot_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t slot                         : 10; /**< Slottime (refer to 802.3 to set correctly)
                                                         10/100Mbs: 0x40
                                                         1000Mbs:   0x200
                                                         (SGMII/1000Base-X only) */
#else
	uint64_t slot                         : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_gmxx_txx_slot_s           cn30xx;
	struct cvmx_gmxx_txx_slot_s           cn31xx;
	struct cvmx_gmxx_txx_slot_s           cn38xx;
	struct cvmx_gmxx_txx_slot_s           cn38xxp2;
	struct cvmx_gmxx_txx_slot_s           cn50xx;
	struct cvmx_gmxx_txx_slot_s           cn52xx;
	struct cvmx_gmxx_txx_slot_s           cn52xxp1;
	struct cvmx_gmxx_txx_slot_s           cn56xx;
	struct cvmx_gmxx_txx_slot_s           cn56xxp1;
	struct cvmx_gmxx_txx_slot_s           cn58xx;
	struct cvmx_gmxx_txx_slot_s           cn58xxp1;
	struct cvmx_gmxx_txx_slot_s           cn61xx;
	struct cvmx_gmxx_txx_slot_s           cn63xx;
	struct cvmx_gmxx_txx_slot_s           cn63xxp1;
	struct cvmx_gmxx_txx_slot_s           cn66xx;
	struct cvmx_gmxx_txx_slot_s           cn68xx;
	struct cvmx_gmxx_txx_slot_s           cn68xxp1;
	struct cvmx_gmxx_txx_slot_s           cnf71xx;
};
typedef union cvmx_gmxx_txx_slot cvmx_gmxx_txx_slot_t;

/**
 * cvmx_gmx#_tx#_soft_pause
 *
 * GMX_TX_SOFT_PAUSE = Packet TX Software Pause
 *
 */
union cvmx_gmxx_txx_soft_pause {
	uint64_t u64;
	struct cvmx_gmxx_txx_soft_pause_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t time                         : 16; /**< Back off the TX bus for (TIME*512) bit-times */
#else
	uint64_t time                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_txx_soft_pause_s     cn30xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn31xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn38xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn38xxp2;
	struct cvmx_gmxx_txx_soft_pause_s     cn50xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn52xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn52xxp1;
	struct cvmx_gmxx_txx_soft_pause_s     cn56xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn56xxp1;
	struct cvmx_gmxx_txx_soft_pause_s     cn58xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn58xxp1;
	struct cvmx_gmxx_txx_soft_pause_s     cn61xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn63xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn63xxp1;
	struct cvmx_gmxx_txx_soft_pause_s     cn66xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn68xx;
	struct cvmx_gmxx_txx_soft_pause_s     cn68xxp1;
	struct cvmx_gmxx_txx_soft_pause_s     cnf71xx;
};
typedef union cvmx_gmxx_txx_soft_pause cvmx_gmxx_txx_soft_pause_t;

/**
 * cvmx_gmx#_tx#_stat0
 *
 * GMX_TX_STAT0 = GMX_TX_STATS_XSDEF / GMX_TX_STATS_XSCOL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat0 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat0_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t xsdef                        : 32; /**< Number of packets dropped (never successfully
                                                         sent) due to excessive deferal
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 32; /**< Number of packets dropped (never successfully
                                                         sent) due to excessive collision.  Defined by
                                                         GMX_TX_COL_ATTEMPT[LIMIT].
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t xscol                        : 32;
	uint64_t xsdef                        : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat0_s          cn30xx;
	struct cvmx_gmxx_txx_stat0_s          cn31xx;
	struct cvmx_gmxx_txx_stat0_s          cn38xx;
	struct cvmx_gmxx_txx_stat0_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat0_s          cn50xx;
	struct cvmx_gmxx_txx_stat0_s          cn52xx;
	struct cvmx_gmxx_txx_stat0_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat0_s          cn56xx;
	struct cvmx_gmxx_txx_stat0_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat0_s          cn58xx;
	struct cvmx_gmxx_txx_stat0_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat0_s          cn61xx;
	struct cvmx_gmxx_txx_stat0_s          cn63xx;
	struct cvmx_gmxx_txx_stat0_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat0_s          cn66xx;
	struct cvmx_gmxx_txx_stat0_s          cn68xx;
	struct cvmx_gmxx_txx_stat0_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat0_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat0 cvmx_gmxx_txx_stat0_t;

/**
 * cvmx_gmx#_tx#_stat1
 *
 * GMX_TX_STAT1 = GMX_TX_STATS_SCOL  / GMX_TX_STATS_MCOL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat1 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t scol                         : 32; /**< Number of packets sent with a single collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t mcol                         : 32; /**< Number of packets sent with multiple collisions
                                                         but < GMX_TX_COL_ATTEMPT[LIMIT].
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t mcol                         : 32;
	uint64_t scol                         : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat1_s          cn30xx;
	struct cvmx_gmxx_txx_stat1_s          cn31xx;
	struct cvmx_gmxx_txx_stat1_s          cn38xx;
	struct cvmx_gmxx_txx_stat1_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat1_s          cn50xx;
	struct cvmx_gmxx_txx_stat1_s          cn52xx;
	struct cvmx_gmxx_txx_stat1_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat1_s          cn56xx;
	struct cvmx_gmxx_txx_stat1_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat1_s          cn58xx;
	struct cvmx_gmxx_txx_stat1_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat1_s          cn61xx;
	struct cvmx_gmxx_txx_stat1_s          cn63xx;
	struct cvmx_gmxx_txx_stat1_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat1_s          cn66xx;
	struct cvmx_gmxx_txx_stat1_s          cn68xx;
	struct cvmx_gmxx_txx_stat1_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat1_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat1 cvmx_gmxx_txx_stat1_t;

/**
 * cvmx_gmx#_tx#_stat2
 *
 * GMX_TX_STAT2 = GMX_TX_STATS_OCTS
 *
 *
 * Notes:
 * - Octect counts are the sum of all data transmitted on the wire including
 *   packet data, pad bytes, fcs bytes, pause bytes, and jam bytes.  The octect
 *   counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat2 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Number of total octets sent on the interface.
                                                         Does not count octets from frames that were
                                                         truncated due to collisions in halfdup mode. */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_txx_stat2_s          cn30xx;
	struct cvmx_gmxx_txx_stat2_s          cn31xx;
	struct cvmx_gmxx_txx_stat2_s          cn38xx;
	struct cvmx_gmxx_txx_stat2_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat2_s          cn50xx;
	struct cvmx_gmxx_txx_stat2_s          cn52xx;
	struct cvmx_gmxx_txx_stat2_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat2_s          cn56xx;
	struct cvmx_gmxx_txx_stat2_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat2_s          cn58xx;
	struct cvmx_gmxx_txx_stat2_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat2_s          cn61xx;
	struct cvmx_gmxx_txx_stat2_s          cn63xx;
	struct cvmx_gmxx_txx_stat2_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat2_s          cn66xx;
	struct cvmx_gmxx_txx_stat2_s          cn68xx;
	struct cvmx_gmxx_txx_stat2_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat2_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat2 cvmx_gmxx_txx_stat2_t;

/**
 * cvmx_gmx#_tx#_stat3
 *
 * GMX_TX_STAT3 = GMX_TX_STATS_PKTS
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat3 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat3_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pkts                         : 32; /**< Number of total frames sent on the interface.
                                                         Does not count frames that were truncated due to
                                                          collisions in halfdup mode. */
#else
	uint64_t pkts                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat3_s          cn30xx;
	struct cvmx_gmxx_txx_stat3_s          cn31xx;
	struct cvmx_gmxx_txx_stat3_s          cn38xx;
	struct cvmx_gmxx_txx_stat3_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat3_s          cn50xx;
	struct cvmx_gmxx_txx_stat3_s          cn52xx;
	struct cvmx_gmxx_txx_stat3_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat3_s          cn56xx;
	struct cvmx_gmxx_txx_stat3_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat3_s          cn58xx;
	struct cvmx_gmxx_txx_stat3_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat3_s          cn61xx;
	struct cvmx_gmxx_txx_stat3_s          cn63xx;
	struct cvmx_gmxx_txx_stat3_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat3_s          cn66xx;
	struct cvmx_gmxx_txx_stat3_s          cn68xx;
	struct cvmx_gmxx_txx_stat3_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat3_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat3 cvmx_gmxx_txx_stat3_t;

/**
 * cvmx_gmx#_tx#_stat4
 *
 * GMX_TX_STAT4 = GMX_TX_STATS_HIST1 (64) / GMX_TX_STATS_HIST0 (<64)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat4 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat4_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist1                        : 32; /**< Number of packets sent with an octet count of 64. */
	uint64_t hist0                        : 32; /**< Number of packets sent with an octet count
                                                         of < 64. */
#else
	uint64_t hist0                        : 32;
	uint64_t hist1                        : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat4_s          cn30xx;
	struct cvmx_gmxx_txx_stat4_s          cn31xx;
	struct cvmx_gmxx_txx_stat4_s          cn38xx;
	struct cvmx_gmxx_txx_stat4_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat4_s          cn50xx;
	struct cvmx_gmxx_txx_stat4_s          cn52xx;
	struct cvmx_gmxx_txx_stat4_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat4_s          cn56xx;
	struct cvmx_gmxx_txx_stat4_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat4_s          cn58xx;
	struct cvmx_gmxx_txx_stat4_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat4_s          cn61xx;
	struct cvmx_gmxx_txx_stat4_s          cn63xx;
	struct cvmx_gmxx_txx_stat4_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat4_s          cn66xx;
	struct cvmx_gmxx_txx_stat4_s          cn68xx;
	struct cvmx_gmxx_txx_stat4_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat4_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat4 cvmx_gmxx_txx_stat4_t;

/**
 * cvmx_gmx#_tx#_stat5
 *
 * GMX_TX_STAT5 = GMX_TX_STATS_HIST3 (128- 255) / GMX_TX_STATS_HIST2 (65- 127)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat5 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat5_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist3                        : 32; /**< Number of packets sent with an octet count of
                                                         128 - 255. */
	uint64_t hist2                        : 32; /**< Number of packets sent with an octet count of
                                                         65 - 127. */
#else
	uint64_t hist2                        : 32;
	uint64_t hist3                        : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat5_s          cn30xx;
	struct cvmx_gmxx_txx_stat5_s          cn31xx;
	struct cvmx_gmxx_txx_stat5_s          cn38xx;
	struct cvmx_gmxx_txx_stat5_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat5_s          cn50xx;
	struct cvmx_gmxx_txx_stat5_s          cn52xx;
	struct cvmx_gmxx_txx_stat5_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat5_s          cn56xx;
	struct cvmx_gmxx_txx_stat5_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat5_s          cn58xx;
	struct cvmx_gmxx_txx_stat5_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat5_s          cn61xx;
	struct cvmx_gmxx_txx_stat5_s          cn63xx;
	struct cvmx_gmxx_txx_stat5_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat5_s          cn66xx;
	struct cvmx_gmxx_txx_stat5_s          cn68xx;
	struct cvmx_gmxx_txx_stat5_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat5_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat5 cvmx_gmxx_txx_stat5_t;

/**
 * cvmx_gmx#_tx#_stat6
 *
 * GMX_TX_STAT6 = GMX_TX_STATS_HIST5 (512-1023) / GMX_TX_STATS_HIST4 (256-511)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat6 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat6_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist5                        : 32; /**< Number of packets sent with an octet count of
                                                         512 - 1023. */
	uint64_t hist4                        : 32; /**< Number of packets sent with an octet count of
                                                         256 - 511. */
#else
	uint64_t hist4                        : 32;
	uint64_t hist5                        : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat6_s          cn30xx;
	struct cvmx_gmxx_txx_stat6_s          cn31xx;
	struct cvmx_gmxx_txx_stat6_s          cn38xx;
	struct cvmx_gmxx_txx_stat6_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat6_s          cn50xx;
	struct cvmx_gmxx_txx_stat6_s          cn52xx;
	struct cvmx_gmxx_txx_stat6_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat6_s          cn56xx;
	struct cvmx_gmxx_txx_stat6_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat6_s          cn58xx;
	struct cvmx_gmxx_txx_stat6_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat6_s          cn61xx;
	struct cvmx_gmxx_txx_stat6_s          cn63xx;
	struct cvmx_gmxx_txx_stat6_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat6_s          cn66xx;
	struct cvmx_gmxx_txx_stat6_s          cn68xx;
	struct cvmx_gmxx_txx_stat6_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat6_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat6 cvmx_gmxx_txx_stat6_t;

/**
 * cvmx_gmx#_tx#_stat7
 *
 * GMX_TX_STAT7 = GMX_TX_STATS_HIST7 (1024-1518) / GMX_TX_STATS_HIST6 (>1518)
 *
 *
 * Notes:
 * - Packet length is the sum of all data transmitted on the wire for the given
 *   packet including packet data, pad bytes, fcs bytes, pause bytes, and jam
 *   bytes.  The octect counts do not include PREAMBLE byte or EXTEND cycles.
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat7 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat7_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t hist7                        : 32; /**< Number of packets sent with an octet count
                                                         of > 1518. */
	uint64_t hist6                        : 32; /**< Number of packets sent with an octet count of
                                                         1024 - 1518. */
#else
	uint64_t hist6                        : 32;
	uint64_t hist7                        : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat7_s          cn30xx;
	struct cvmx_gmxx_txx_stat7_s          cn31xx;
	struct cvmx_gmxx_txx_stat7_s          cn38xx;
	struct cvmx_gmxx_txx_stat7_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat7_s          cn50xx;
	struct cvmx_gmxx_txx_stat7_s          cn52xx;
	struct cvmx_gmxx_txx_stat7_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat7_s          cn56xx;
	struct cvmx_gmxx_txx_stat7_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat7_s          cn58xx;
	struct cvmx_gmxx_txx_stat7_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat7_s          cn61xx;
	struct cvmx_gmxx_txx_stat7_s          cn63xx;
	struct cvmx_gmxx_txx_stat7_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat7_s          cn66xx;
	struct cvmx_gmxx_txx_stat7_s          cn68xx;
	struct cvmx_gmxx_txx_stat7_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat7_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat7 cvmx_gmxx_txx_stat7_t;

/**
 * cvmx_gmx#_tx#_stat8
 *
 * GMX_TX_STAT8 = GMX_TX_STATS_MCST  / GMX_TX_STATS_BCST
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 * - Note, GMX determines if the packet is MCST or BCST from the DMAC of the
 *   packet.  GMX assumes that the DMAC lies in the first 6 bytes of the packet
 *   as per the 802.3 frame definition.  If the system requires additional data
 *   before the L2 header, then the MCST and BCST counters may not reflect
 *   reality and should be ignored by software.
 */
union cvmx_gmxx_txx_stat8 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat8_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t mcst                         : 32; /**< Number of packets sent to multicast DMAC.
                                                         Does not include BCST packets. */
	uint64_t bcst                         : 32; /**< Number of packets sent to broadcast DMAC.
                                                         Does not include MCST packets. */
#else
	uint64_t bcst                         : 32;
	uint64_t mcst                         : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat8_s          cn30xx;
	struct cvmx_gmxx_txx_stat8_s          cn31xx;
	struct cvmx_gmxx_txx_stat8_s          cn38xx;
	struct cvmx_gmxx_txx_stat8_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat8_s          cn50xx;
	struct cvmx_gmxx_txx_stat8_s          cn52xx;
	struct cvmx_gmxx_txx_stat8_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat8_s          cn56xx;
	struct cvmx_gmxx_txx_stat8_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat8_s          cn58xx;
	struct cvmx_gmxx_txx_stat8_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat8_s          cn61xx;
	struct cvmx_gmxx_txx_stat8_s          cn63xx;
	struct cvmx_gmxx_txx_stat8_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat8_s          cn66xx;
	struct cvmx_gmxx_txx_stat8_s          cn68xx;
	struct cvmx_gmxx_txx_stat8_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat8_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat8 cvmx_gmxx_txx_stat8_t;

/**
 * cvmx_gmx#_tx#_stat9
 *
 * GMX_TX_STAT9 = GMX_TX_STATS_UNDFLW / GMX_TX_STATS_CTL
 *
 *
 * Notes:
 * - Cleared either by a write (of any value) or a read when GMX_TX_STATS_CTL[RD_CLR] is set
 * - Counters will wrap
 */
union cvmx_gmxx_txx_stat9 {
	uint64_t u64;
	struct cvmx_gmxx_txx_stat9_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t undflw                       : 32; /**< Number of underflow packets */
	uint64_t ctl                          : 32; /**< Number of Control packets (PAUSE flow control)
                                                         generated by GMX.  It does not include control
                                                         packets forwarded or generated by the PP's.
                                                         CTL will count the number of generated PFC frames.
                                                         CTL will not track the number of generated HG2
                                                         messages. */
#else
	uint64_t ctl                          : 32;
	uint64_t undflw                       : 32;
#endif
	} s;
	struct cvmx_gmxx_txx_stat9_s          cn30xx;
	struct cvmx_gmxx_txx_stat9_s          cn31xx;
	struct cvmx_gmxx_txx_stat9_s          cn38xx;
	struct cvmx_gmxx_txx_stat9_s          cn38xxp2;
	struct cvmx_gmxx_txx_stat9_s          cn50xx;
	struct cvmx_gmxx_txx_stat9_s          cn52xx;
	struct cvmx_gmxx_txx_stat9_s          cn52xxp1;
	struct cvmx_gmxx_txx_stat9_s          cn56xx;
	struct cvmx_gmxx_txx_stat9_s          cn56xxp1;
	struct cvmx_gmxx_txx_stat9_s          cn58xx;
	struct cvmx_gmxx_txx_stat9_s          cn58xxp1;
	struct cvmx_gmxx_txx_stat9_s          cn61xx;
	struct cvmx_gmxx_txx_stat9_s          cn63xx;
	struct cvmx_gmxx_txx_stat9_s          cn63xxp1;
	struct cvmx_gmxx_txx_stat9_s          cn66xx;
	struct cvmx_gmxx_txx_stat9_s          cn68xx;
	struct cvmx_gmxx_txx_stat9_s          cn68xxp1;
	struct cvmx_gmxx_txx_stat9_s          cnf71xx;
};
typedef union cvmx_gmxx_txx_stat9 cvmx_gmxx_txx_stat9_t;

/**
 * cvmx_gmx#_tx#_stats_ctl
 *
 * GMX_TX_STATS_CTL = TX Stats Control register
 *
 */
union cvmx_gmxx_txx_stats_ctl {
	uint64_t u64;
	struct cvmx_gmxx_txx_stats_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rd_clr                       : 1;  /**< Stats registers will clear on reads */
#else
	uint64_t rd_clr                       : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_txx_stats_ctl_s      cn30xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn31xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn38xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn38xxp2;
	struct cvmx_gmxx_txx_stats_ctl_s      cn50xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn52xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn52xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s      cn56xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn56xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s      cn58xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn58xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s      cn61xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn63xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn63xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s      cn66xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn68xx;
	struct cvmx_gmxx_txx_stats_ctl_s      cn68xxp1;
	struct cvmx_gmxx_txx_stats_ctl_s      cnf71xx;
};
typedef union cvmx_gmxx_txx_stats_ctl cvmx_gmxx_txx_stats_ctl_t;

/**
 * cvmx_gmx#_tx#_thresh
 *
 * Per Port
 *
 *
 * GMX_TX_THRESH = Packet TX Threshold
 *
 * Notes:
 * In XAUI mode, prt0 is used for checking.  Since XAUI mode uses a single TX FIFO and is higher data rate, recommended value is 0x100.
 *
 */
union cvmx_gmxx_txx_thresh {
	uint64_t u64;
	struct cvmx_gmxx_txx_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_10_63               : 54;
	uint64_t cnt                          : 10; /**< Number of 16B ticks to accumulate in the TX FIFO
                                                         before sending on the packet interface
                                                         This register should be large enough to prevent
                                                         underflow on the packet interface and must never
                                                         be set to zero.  This register cannot exceed the
                                                         the TX FIFO depth which is...
                                                          GMX_TX_PRTS==0,1:  CNT MAX = 0x100
                                                          GMX_TX_PRTS==2  :  CNT MAX = 0x080
                                                          GMX_TX_PRTS==3,4:  CNT MAX = 0x040 */
#else
	uint64_t cnt                          : 10;
	uint64_t reserved_10_63               : 54;
#endif
	} s;
	struct cvmx_gmxx_txx_thresh_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t cnt                          : 7;  /**< Number of 16B ticks to accumulate in the TX FIFO
                                                         before sending on the RGMII interface
                                                         This register should be large enough to prevent
                                                         underflow on the RGMII interface and must never
                                                         be set below 4.  This register cannot exceed the
                                                         the TX FIFO depth which is 64 16B entries. */
#else
	uint64_t cnt                          : 7;
	uint64_t reserved_7_63                : 57;
#endif
	} cn30xx;
	struct cvmx_gmxx_txx_thresh_cn30xx    cn31xx;
	struct cvmx_gmxx_txx_thresh_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t cnt                          : 9;  /**< Number of 16B ticks to accumulate in the TX FIFO
                                                          before sending on the RGMII interface
                                                          This register should be large enough to prevent
                                                          underflow on the RGMII interface and must never
                                                          be set to zero.  This register cannot exceed the
                                                          the TX FIFO depth which is...
                                                           GMX_TX_PRTS==0,1:  CNT MAX = 0x100
                                                           GMX_TX_PRTS==2  :  CNT MAX = 0x080
                                                           GMX_TX_PRTS==3,4:  CNT MAX = 0x040
                                                         (PASS2 expands from 6 to 9 bits) */
#else
	uint64_t cnt                          : 9;
	uint64_t reserved_9_63                : 55;
#endif
	} cn38xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn38xxp2;
	struct cvmx_gmxx_txx_thresh_cn30xx    cn50xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn52xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn52xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn56xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn56xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn58xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn58xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn61xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn63xx;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn63xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx    cn66xx;
	struct cvmx_gmxx_txx_thresh_s         cn68xx;
	struct cvmx_gmxx_txx_thresh_s         cn68xxp1;
	struct cvmx_gmxx_txx_thresh_cn38xx    cnf71xx;
};
typedef union cvmx_gmxx_txx_thresh cvmx_gmxx_txx_thresh_t;

/**
 * cvmx_gmx#_tx_bp
 *
 * GMX_TX_BP = Packet Interface TX BackPressure Register
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of BP is used.
 *
 */
union cvmx_gmxx_tx_bp {
	uint64_t u64;
	struct cvmx_gmxx_tx_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t bp                           : 4;  /**< Per port BackPressure status
                                                         0=Port is available
                                                         1=Port should be back pressured */
#else
	uint64_t bp                           : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_tx_bp_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t bp                           : 3;  /**< Per port BackPressure status
                                                         0=Port is available
                                                         1=Port should be back pressured */
#else
	uint64_t bp                           : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_bp_cn30xx         cn31xx;
	struct cvmx_gmxx_tx_bp_s              cn38xx;
	struct cvmx_gmxx_tx_bp_s              cn38xxp2;
	struct cvmx_gmxx_tx_bp_cn30xx         cn50xx;
	struct cvmx_gmxx_tx_bp_s              cn52xx;
	struct cvmx_gmxx_tx_bp_s              cn52xxp1;
	struct cvmx_gmxx_tx_bp_s              cn56xx;
	struct cvmx_gmxx_tx_bp_s              cn56xxp1;
	struct cvmx_gmxx_tx_bp_s              cn58xx;
	struct cvmx_gmxx_tx_bp_s              cn58xxp1;
	struct cvmx_gmxx_tx_bp_s              cn61xx;
	struct cvmx_gmxx_tx_bp_s              cn63xx;
	struct cvmx_gmxx_tx_bp_s              cn63xxp1;
	struct cvmx_gmxx_tx_bp_s              cn66xx;
	struct cvmx_gmxx_tx_bp_s              cn68xx;
	struct cvmx_gmxx_tx_bp_s              cn68xxp1;
	struct cvmx_gmxx_tx_bp_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t bp                           : 2;  /**< Per port BackPressure status
                                                         0=Port is available
                                                         1=Port should be back pressured */
#else
	uint64_t bp                           : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_tx_bp cvmx_gmxx_tx_bp_t;

/**
 * cvmx_gmx#_tx_clk_msk#
 *
 * GMX_TX_CLK_MSK = GMX Clock Select
 *
 */
union cvmx_gmxx_tx_clk_mskx {
	uint64_t u64;
	struct cvmx_gmxx_tx_clk_mskx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t msk                          : 1;  /**< Write this bit to a 1 when switching clks */
#else
	uint64_t msk                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_gmxx_tx_clk_mskx_s        cn30xx;
	struct cvmx_gmxx_tx_clk_mskx_s        cn50xx;
};
typedef union cvmx_gmxx_tx_clk_mskx cvmx_gmxx_tx_clk_mskx_t;

/**
 * cvmx_gmx#_tx_col_attempt
 *
 * GMX_TX_COL_ATTEMPT = Packet TX collision attempts before dropping frame
 *
 */
union cvmx_gmxx_tx_col_attempt {
	uint64_t u64;
	struct cvmx_gmxx_tx_col_attempt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t limit                        : 5;  /**< Collision Attempts
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t limit                        : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_gmxx_tx_col_attempt_s     cn30xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn31xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn38xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn38xxp2;
	struct cvmx_gmxx_tx_col_attempt_s     cn50xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn52xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn52xxp1;
	struct cvmx_gmxx_tx_col_attempt_s     cn56xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn56xxp1;
	struct cvmx_gmxx_tx_col_attempt_s     cn58xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn58xxp1;
	struct cvmx_gmxx_tx_col_attempt_s     cn61xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn63xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn63xxp1;
	struct cvmx_gmxx_tx_col_attempt_s     cn66xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn68xx;
	struct cvmx_gmxx_tx_col_attempt_s     cn68xxp1;
	struct cvmx_gmxx_tx_col_attempt_s     cnf71xx;
};
typedef union cvmx_gmxx_tx_col_attempt cvmx_gmxx_tx_col_attempt_t;

/**
 * cvmx_gmx#_tx_corrupt
 *
 * GMX_TX_CORRUPT = TX - Corrupt TX packets with the ERR bit set
 *
 *
 * Notes:
 * Packets sent from PKO with the ERR wire asserted will be corrupted by
 * the transmitter if CORRUPT[prt] is set (XAUI uses prt==0).
 *
 * Corruption means that GMX will send a bad FCS value.  If GMX_TX_APPEND[FCS]
 * is clear then no FCS is sent and the GMX cannot corrupt it.  The corrupt FCS
 * value is 0xeeeeeeee for SGMII/1000Base-X and 4 bytes of the error
 * propagation code in XAUI mode.
 */
union cvmx_gmxx_tx_corrupt {
	uint64_t u64;
	struct cvmx_gmxx_tx_corrupt_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t corrupt                      : 4;  /**< Per port error propagation
                                                         0=Never corrupt packets
                                                         1=Corrupt packets with ERR */
#else
	uint64_t corrupt                      : 4;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_gmxx_tx_corrupt_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t corrupt                      : 3;  /**< Per port error propagation
                                                         0=Never corrupt packets
                                                         1=Corrupt packets with ERR */
#else
	uint64_t corrupt                      : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_corrupt_cn30xx    cn31xx;
	struct cvmx_gmxx_tx_corrupt_s         cn38xx;
	struct cvmx_gmxx_tx_corrupt_s         cn38xxp2;
	struct cvmx_gmxx_tx_corrupt_cn30xx    cn50xx;
	struct cvmx_gmxx_tx_corrupt_s         cn52xx;
	struct cvmx_gmxx_tx_corrupt_s         cn52xxp1;
	struct cvmx_gmxx_tx_corrupt_s         cn56xx;
	struct cvmx_gmxx_tx_corrupt_s         cn56xxp1;
	struct cvmx_gmxx_tx_corrupt_s         cn58xx;
	struct cvmx_gmxx_tx_corrupt_s         cn58xxp1;
	struct cvmx_gmxx_tx_corrupt_s         cn61xx;
	struct cvmx_gmxx_tx_corrupt_s         cn63xx;
	struct cvmx_gmxx_tx_corrupt_s         cn63xxp1;
	struct cvmx_gmxx_tx_corrupt_s         cn66xx;
	struct cvmx_gmxx_tx_corrupt_s         cn68xx;
	struct cvmx_gmxx_tx_corrupt_s         cn68xxp1;
	struct cvmx_gmxx_tx_corrupt_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t corrupt                      : 2;  /**< Per port error propagation
                                                         0=Never corrupt packets
                                                         1=Corrupt packets with ERR */
#else
	uint64_t corrupt                      : 2;
	uint64_t reserved_2_63                : 62;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_tx_corrupt cvmx_gmxx_tx_corrupt_t;

/**
 * cvmx_gmx#_tx_hg2_reg1
 *
 * Notes:
 * The TX_XOF[15:0] field in GMX(0)_TX_HG2_REG1 and the TX_XON[15:0] field in
 * GMX(0)_TX_HG2_REG2 register map to the same 16 physical flops. When written with address of
 * GMX(0)_TX_HG2_REG1, it will exhibit write 1 to set behavior and when written with address of
 * GMX(0)_TX_HG2_REG2, it will exhibit write 1 to clear behavior.
 * For reads, either address will return the $GMX(0)_TX_HG2_REG1 values.
 */
union cvmx_gmxx_tx_hg2_reg1 {
	uint64_t u64;
	struct cvmx_gmxx_tx_hg2_reg1_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t tx_xof                       : 16; /**< TX HiGig2 message for logical link pause when any
                                                         bit value changes
                                                          Only write in HiGig2 mode i.e. when
                                                          GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                          GMX_RX_UDD_SKP[SKIP]=16. */
#else
	uint64_t tx_xof                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn52xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn52xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn56xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn61xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn63xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn63xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn66xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn68xx;
	struct cvmx_gmxx_tx_hg2_reg1_s        cn68xxp1;
	struct cvmx_gmxx_tx_hg2_reg1_s        cnf71xx;
};
typedef union cvmx_gmxx_tx_hg2_reg1 cvmx_gmxx_tx_hg2_reg1_t;

/**
 * cvmx_gmx#_tx_hg2_reg2
 *
 * Notes:
 * The TX_XOF[15:0] field in GMX(0)_TX_HG2_REG1 and the TX_XON[15:0] field in
 * GMX(0)_TX_HG2_REG2 register map to the same 16 physical flops. When written with address  of
 * GMX(0)_TX_HG2_REG1, it will exhibit write 1 to set behavior and when written with address of
 * GMX(0)_TX_HG2_REG2, it will exhibit write 1 to clear behavior.
 * For reads, either address will return the $GMX(0)_TX_HG2_REG1 values.
 */
union cvmx_gmxx_tx_hg2_reg2 {
	uint64_t u64;
	struct cvmx_gmxx_tx_hg2_reg2_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t tx_xon                       : 16; /**< TX HiGig2 message for logical link pause when any
                                                         bit value changes
                                                          Only write in HiGig2 mode i.e. when
                                                          GMX_TX_XAUI_CTL[HG_EN]=1 and
                                                          GMX_RX_UDD_SKP[SKIP]=16. */
#else
	uint64_t tx_xon                       : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn52xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn52xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn56xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn61xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn63xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn63xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn66xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn68xx;
	struct cvmx_gmxx_tx_hg2_reg2_s        cn68xxp1;
	struct cvmx_gmxx_tx_hg2_reg2_s        cnf71xx;
};
typedef union cvmx_gmxx_tx_hg2_reg2 cvmx_gmxx_tx_hg2_reg2_t;

/**
 * cvmx_gmx#_tx_ifg
 *
 * GMX_TX_IFG = Packet TX Interframe Gap
 *
 *
 * Notes:
 * * Programming IFG1 and IFG2.
 *
 * For 10/100/1000Mbs half-duplex systems that require IEEE 802.3
 * compatibility, IFG1 must be in the range of 1-8, IFG2 must be in the range
 * of 4-12, and the IFG1+IFG2 sum must be 12.
 *
 * For 10/100/1000Mbs full-duplex systems that require IEEE 802.3
 * compatibility, IFG1 must be in the range of 1-11, IFG2 must be in the range
 * of 1-11, and the IFG1+IFG2 sum must be 12.
 *
 * For XAUI/10Gbs systems that require IEEE 802.3 compatibility, the
 * IFG1+IFG2 sum must be 12.  IFG1[1:0] and IFG2[1:0] must be zero.
 *
 * For all other systems, IFG1 and IFG2 can be any value in the range of
 * 1-15.  Allowing for a total possible IFG sum of 2-30.
 */
union cvmx_gmxx_tx_ifg {
	uint64_t u64;
	struct cvmx_gmxx_tx_ifg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t ifg2                         : 4;  /**< 1/3 of the interframe gap timing (in IFG2*8 bits)
                                                         If CRS is detected during IFG2, then the
                                                         interFrameSpacing timer is not reset and a frame
                                                         is transmited once the timer expires. */
	uint64_t ifg1                         : 4;  /**< 2/3 of the interframe gap timing (in IFG1*8 bits)
                                                         If CRS is detected during IFG1, then the
                                                         interFrameSpacing timer is reset and a frame is
                                                         not transmited. */
#else
	uint64_t ifg1                         : 4;
	uint64_t ifg2                         : 4;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_gmxx_tx_ifg_s             cn30xx;
	struct cvmx_gmxx_tx_ifg_s             cn31xx;
	struct cvmx_gmxx_tx_ifg_s             cn38xx;
	struct cvmx_gmxx_tx_ifg_s             cn38xxp2;
	struct cvmx_gmxx_tx_ifg_s             cn50xx;
	struct cvmx_gmxx_tx_ifg_s             cn52xx;
	struct cvmx_gmxx_tx_ifg_s             cn52xxp1;
	struct cvmx_gmxx_tx_ifg_s             cn56xx;
	struct cvmx_gmxx_tx_ifg_s             cn56xxp1;
	struct cvmx_gmxx_tx_ifg_s             cn58xx;
	struct cvmx_gmxx_tx_ifg_s             cn58xxp1;
	struct cvmx_gmxx_tx_ifg_s             cn61xx;
	struct cvmx_gmxx_tx_ifg_s             cn63xx;
	struct cvmx_gmxx_tx_ifg_s             cn63xxp1;
	struct cvmx_gmxx_tx_ifg_s             cn66xx;
	struct cvmx_gmxx_tx_ifg_s             cn68xx;
	struct cvmx_gmxx_tx_ifg_s             cn68xxp1;
	struct cvmx_gmxx_tx_ifg_s             cnf71xx;
};
typedef union cvmx_gmxx_tx_ifg cvmx_gmxx_tx_ifg_t;

/**
 * cvmx_gmx#_tx_int_en
 *
 * GMX_TX_INT_EN = Interrupt Enable
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of UNDFLW is used.
 *
 */
union cvmx_gmxx_tx_int_en {
	uint64_t u64;
	struct cvmx_gmxx_tx_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI link status changed - this denotes a change
                                                         to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI mode only) */
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_gmxx_tx_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t late_col                     : 3;  /**< TX Late Collision */
	uint64_t reserved_15_15               : 1;
	uint64_t xsdef                        : 3;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t reserved_11_11               : 1;
	uint64_t xscol                        : 3;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_5_7                 : 3;
	uint64_t undflw                       : 3;  /**< TX Underflow (RGMII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 3;
	uint64_t reserved_5_7                 : 3;
	uint64_t xscol                        : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t xsdef                        : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t late_col                     : 3;
	uint64_t reserved_19_63               : 45;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_int_en_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t xsdef                        : 3;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t reserved_11_11               : 1;
	uint64_t xscol                        : 3;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_5_7                 : 3;
	uint64_t undflw                       : 3;  /**< TX Underflow (RGMII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 3;
	uint64_t reserved_5_7                 : 3;
	uint64_t xscol                        : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t xsdef                        : 3;
	uint64_t reserved_15_63               : 49;
#endif
	} cn31xx;
	struct cvmx_gmxx_tx_int_en_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (PASS3 only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow (RGMII mode only) */
	uint64_t ncb_nxa                      : 1;  /**< Port address out-of-range from NCB Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t ncb_nxa                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_int_en_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow (RGMII mode only) */
	uint64_t ncb_nxa                      : 1;  /**< Port address out-of-range from NCB Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t ncb_nxa                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t reserved_16_63               : 48;
#endif
	} cn38xxp2;
	struct cvmx_gmxx_tx_int_en_cn30xx     cn50xx;
	struct cvmx_gmxx_tx_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_gmxx_tx_int_en_cn52xx     cn52xxp1;
	struct cvmx_gmxx_tx_int_en_cn52xx     cn56xx;
	struct cvmx_gmxx_tx_int_en_cn52xx     cn56xxp1;
	struct cvmx_gmxx_tx_int_en_cn38xx     cn58xx;
	struct cvmx_gmxx_tx_int_en_cn38xx     cn58xxp1;
	struct cvmx_gmxx_tx_int_en_s          cn61xx;
	struct cvmx_gmxx_tx_int_en_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t reserved_24_63               : 40;
#endif
	} cn63xx;
	struct cvmx_gmxx_tx_int_en_cn63xx     cn63xxp1;
	struct cvmx_gmxx_tx_int_en_s          cn66xx;
	struct cvmx_gmxx_tx_int_en_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI/RXAUI link status changed - this denotes a
                                                         change to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI/RXAUI mode only) */
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t pko_nxp                      : 1;  /**< Port pipe out-of-range from PKO Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t pko_nxp                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} cn68xx;
	struct cvmx_gmxx_tx_int_en_cn68xx     cn68xxp1;
	struct cvmx_gmxx_tx_int_en_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI link status changed - this denotes a change
                                                         to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI mode only) */
	uint64_t reserved_22_23               : 2;
	uint64_t ptp_lost                     : 2;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t reserved_18_19               : 2;
	uint64_t late_col                     : 2;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_19               : 2;
	uint64_t ptp_lost                     : 2;
	uint64_t reserved_22_23               : 2;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_tx_int_en cvmx_gmxx_tx_int_en_t;

/**
 * cvmx_gmx#_tx_int_reg
 *
 * GMX_TX_INT_REG = Interrupt Register
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of UNDFLW is used.
 *
 */
union cvmx_gmxx_tx_int_reg {
	uint64_t u64;
	struct cvmx_gmxx_tx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI link status changed - this denotes a change
                                                         to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI mode only) */
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} s;
	struct cvmx_gmxx_tx_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t late_col                     : 3;  /**< TX Late Collision */
	uint64_t reserved_15_15               : 1;
	uint64_t xsdef                        : 3;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t reserved_11_11               : 1;
	uint64_t xscol                        : 3;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_5_7                 : 3;
	uint64_t undflw                       : 3;  /**< TX Underflow (RGMII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 3;
	uint64_t reserved_5_7                 : 3;
	uint64_t xscol                        : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t xsdef                        : 3;
	uint64_t reserved_15_15               : 1;
	uint64_t late_col                     : 3;
	uint64_t reserved_19_63               : 45;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_int_reg_cn31xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_15_63               : 49;
	uint64_t xsdef                        : 3;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t reserved_11_11               : 1;
	uint64_t xscol                        : 3;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_5_7                 : 3;
	uint64_t undflw                       : 3;  /**< TX Underflow (RGMII mode only) */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 3;
	uint64_t reserved_5_7                 : 3;
	uint64_t xscol                        : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t xsdef                        : 3;
	uint64_t reserved_15_63               : 49;
#endif
	} cn31xx;
	struct cvmx_gmxx_tx_int_reg_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (PASS3 only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow (RGMII mode only) */
	uint64_t ncb_nxa                      : 1;  /**< Port address out-of-range from NCB Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t ncb_nxa                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_int_reg_cn38xxp2 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral (RGMII/halfdup mode only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions (RGMII/halfdup mode only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow (RGMII mode only) */
	uint64_t ncb_nxa                      : 1;  /**< Port address out-of-range from NCB Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t ncb_nxa                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t reserved_16_63               : 48;
#endif
	} cn38xxp2;
	struct cvmx_gmxx_tx_int_reg_cn30xx    cn50xx;
	struct cvmx_gmxx_tx_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t reserved_20_63               : 44;
#endif
	} cn52xx;
	struct cvmx_gmxx_tx_int_reg_cn52xx    cn52xxp1;
	struct cvmx_gmxx_tx_int_reg_cn52xx    cn56xx;
	struct cvmx_gmxx_tx_int_reg_cn52xx    cn56xxp1;
	struct cvmx_gmxx_tx_int_reg_cn38xx    cn58xx;
	struct cvmx_gmxx_tx_int_reg_cn38xx    cn58xxp1;
	struct cvmx_gmxx_tx_int_reg_s         cn61xx;
	struct cvmx_gmxx_tx_int_reg_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t reserved_24_63               : 40;
#endif
	} cn63xx;
	struct cvmx_gmxx_tx_int_reg_cn63xx    cn63xxp1;
	struct cvmx_gmxx_tx_int_reg_s         cn66xx;
	struct cvmx_gmxx_tx_int_reg_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI/RXAUI link status changed - this denotes ae
                                                         change to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI/RXAUI mode only) */
	uint64_t ptp_lost                     : 4;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t late_col                     : 4;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xsdef                        : 4;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t xscol                        : 4;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_6_7                 : 2;
	uint64_t undflw                       : 4;  /**< TX Underflow */
	uint64_t pko_nxp                      : 1;  /**< Port pipe out-of-range from PKO Interface */
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t pko_nxp                      : 1;
	uint64_t undflw                       : 4;
	uint64_t reserved_6_7                 : 2;
	uint64_t xscol                        : 4;
	uint64_t xsdef                        : 4;
	uint64_t late_col                     : 4;
	uint64_t ptp_lost                     : 4;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} cn68xx;
	struct cvmx_gmxx_tx_int_reg_cn68xx    cn68xxp1;
	struct cvmx_gmxx_tx_int_reg_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_25_63               : 39;
	uint64_t xchange                      : 1;  /**< XAUI link status changed - this denotes a change
                                                         to GMX_RX_XAUI_CTL[STATUS]
                                                         (XAUI mode only) */
	uint64_t reserved_22_23               : 2;
	uint64_t ptp_lost                     : 2;  /**< A packet with a PTP request was not able to be
                                                         sent due to XSCOL */
	uint64_t reserved_18_19               : 2;
	uint64_t late_col                     : 2;  /**< TX Late Collision
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_14_15               : 2;
	uint64_t xsdef                        : 2;  /**< TX Excessive deferral
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_10_11               : 2;
	uint64_t xscol                        : 2;  /**< TX Excessive collisions
                                                         (SGMII/1000Base-X half-duplex only) */
	uint64_t reserved_4_7                 : 4;
	uint64_t undflw                       : 2;  /**< TX Underflow */
	uint64_t reserved_1_1                 : 1;
	uint64_t pko_nxa                      : 1;  /**< Port address out-of-range from PKO Interface */
#else
	uint64_t pko_nxa                      : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t undflw                       : 2;
	uint64_t reserved_4_7                 : 4;
	uint64_t xscol                        : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t xsdef                        : 2;
	uint64_t reserved_14_15               : 2;
	uint64_t late_col                     : 2;
	uint64_t reserved_18_19               : 2;
	uint64_t ptp_lost                     : 2;
	uint64_t reserved_22_23               : 2;
	uint64_t xchange                      : 1;
	uint64_t reserved_25_63               : 39;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_tx_int_reg cvmx_gmxx_tx_int_reg_t;

/**
 * cvmx_gmx#_tx_jam
 *
 * GMX_TX_JAM = Packet TX Jam Pattern
 *
 */
union cvmx_gmxx_tx_jam {
	uint64_t u64;
	struct cvmx_gmxx_tx_jam_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t jam                          : 8;  /**< Jam pattern
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t jam                          : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_gmxx_tx_jam_s             cn30xx;
	struct cvmx_gmxx_tx_jam_s             cn31xx;
	struct cvmx_gmxx_tx_jam_s             cn38xx;
	struct cvmx_gmxx_tx_jam_s             cn38xxp2;
	struct cvmx_gmxx_tx_jam_s             cn50xx;
	struct cvmx_gmxx_tx_jam_s             cn52xx;
	struct cvmx_gmxx_tx_jam_s             cn52xxp1;
	struct cvmx_gmxx_tx_jam_s             cn56xx;
	struct cvmx_gmxx_tx_jam_s             cn56xxp1;
	struct cvmx_gmxx_tx_jam_s             cn58xx;
	struct cvmx_gmxx_tx_jam_s             cn58xxp1;
	struct cvmx_gmxx_tx_jam_s             cn61xx;
	struct cvmx_gmxx_tx_jam_s             cn63xx;
	struct cvmx_gmxx_tx_jam_s             cn63xxp1;
	struct cvmx_gmxx_tx_jam_s             cn66xx;
	struct cvmx_gmxx_tx_jam_s             cn68xx;
	struct cvmx_gmxx_tx_jam_s             cn68xxp1;
	struct cvmx_gmxx_tx_jam_s             cnf71xx;
};
typedef union cvmx_gmxx_tx_jam cvmx_gmxx_tx_jam_t;

/**
 * cvmx_gmx#_tx_lfsr
 *
 * GMX_TX_LFSR = LFSR used to implement truncated binary exponential backoff
 *
 */
union cvmx_gmxx_tx_lfsr {
	uint64_t u64;
	struct cvmx_gmxx_tx_lfsr_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t lfsr                         : 16; /**< The current state of the LFSR used to feed random
                                                         numbers to compute truncated binary exponential
                                                         backoff.
                                                         (SGMII/1000Base-X half-duplex only) */
#else
	uint64_t lfsr                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_lfsr_s            cn30xx;
	struct cvmx_gmxx_tx_lfsr_s            cn31xx;
	struct cvmx_gmxx_tx_lfsr_s            cn38xx;
	struct cvmx_gmxx_tx_lfsr_s            cn38xxp2;
	struct cvmx_gmxx_tx_lfsr_s            cn50xx;
	struct cvmx_gmxx_tx_lfsr_s            cn52xx;
	struct cvmx_gmxx_tx_lfsr_s            cn52xxp1;
	struct cvmx_gmxx_tx_lfsr_s            cn56xx;
	struct cvmx_gmxx_tx_lfsr_s            cn56xxp1;
	struct cvmx_gmxx_tx_lfsr_s            cn58xx;
	struct cvmx_gmxx_tx_lfsr_s            cn58xxp1;
	struct cvmx_gmxx_tx_lfsr_s            cn61xx;
	struct cvmx_gmxx_tx_lfsr_s            cn63xx;
	struct cvmx_gmxx_tx_lfsr_s            cn63xxp1;
	struct cvmx_gmxx_tx_lfsr_s            cn66xx;
	struct cvmx_gmxx_tx_lfsr_s            cn68xx;
	struct cvmx_gmxx_tx_lfsr_s            cn68xxp1;
	struct cvmx_gmxx_tx_lfsr_s            cnf71xx;
};
typedef union cvmx_gmxx_tx_lfsr cvmx_gmxx_tx_lfsr_t;

/**
 * cvmx_gmx#_tx_ovr_bp
 *
 * GMX_TX_OVR_BP = Packet Interface TX Override BackPressure
 *
 *
 * Notes:
 * In XAUI mode, only the lsb (corresponding to port0) of EN, BP, and IGN_FULL are used.
 *
 * GMX*_TX_OVR_BP[EN<0>] must be set to one and GMX*_TX_OVR_BP[BP<0>] must be cleared to zero
 * (to forcibly disable HW-automatic 802.3 pause packet generation) with the HiGig2 Protocol
 * when GMX*_HG2_CONTROL[HG2TX_EN]=0. (The HiGig2 protocol is indicated by
 * GMX*_TX_XAUI_CTL[HG_EN]=1 and GMX*_RX0_UDD_SKP[LEN]=16.) HW can only auto-generate backpressure
 * through HiGig2 messages (optionally, when GMX*_HG2_CONTROL[HG2TX_EN]=1) with the HiGig2
 * protocol.
 */
union cvmx_gmxx_tx_ovr_bp {
	uint64_t u64;
	struct cvmx_gmxx_tx_ovr_bp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t tx_prt_bp                    : 16; /**< Per port BP sent to PKO
                                                         0=Port is available
                                                         1=Port should be back pressured
                                                         TX_PRT_BP should not be set until
                                                         GMX_INF_MODE[EN] has been enabled */
	uint64_t reserved_12_31               : 20;
	uint64_t en                           : 4;  /**< Per port Enable back pressure override */
	uint64_t bp                           : 4;  /**< Per port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t ign_full                     : 4;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 4;
	uint64_t bp                           : 4;
	uint64_t en                           : 4;
	uint64_t reserved_12_31               : 20;
	uint64_t tx_prt_bp                    : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_tx_ovr_bp_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t en                           : 3;  /**< Per port Enable back pressure override */
	uint64_t reserved_7_7                 : 1;
	uint64_t bp                           : 3;  /**< Per port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t reserved_3_3                 : 1;
	uint64_t ign_full                     : 3;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t bp                           : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t en                           : 3;
	uint64_t reserved_11_63               : 53;
#endif
	} cn30xx;
	struct cvmx_gmxx_tx_ovr_bp_cn30xx     cn31xx;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t en                           : 4;  /**< Per port Enable back pressure override */
	uint64_t bp                           : 4;  /**< Per port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t ign_full                     : 4;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 4;
	uint64_t bp                           : 4;
	uint64_t en                           : 4;
	uint64_t reserved_12_63               : 52;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx     cn38xxp2;
	struct cvmx_gmxx_tx_ovr_bp_cn30xx     cn50xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn52xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn52xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s          cn56xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn56xxp1;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx     cn58xx;
	struct cvmx_gmxx_tx_ovr_bp_cn38xx     cn58xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s          cn61xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn63xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn63xxp1;
	struct cvmx_gmxx_tx_ovr_bp_s          cn66xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn68xx;
	struct cvmx_gmxx_tx_ovr_bp_s          cn68xxp1;
	struct cvmx_gmxx_tx_ovr_bp_cnf71xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t tx_prt_bp                    : 16; /**< Per port BP sent to PKO
                                                         0=Port is available
                                                         1=Port should be back pressured
                                                         TX_PRT_BP should not be set until
                                                         GMX_INF_MODE[EN] has been enabled */
	uint64_t reserved_10_31               : 22;
	uint64_t en                           : 2;  /**< Per port Enable back pressure override */
	uint64_t reserved_6_7                 : 2;
	uint64_t bp                           : 2;  /**< Per port BackPressure status to use
                                                         0=Port is available
                                                         1=Port should be back pressured */
	uint64_t reserved_2_3                 : 2;
	uint64_t ign_full                     : 2;  /**< Ignore the RX FIFO full when computing BP */
#else
	uint64_t ign_full                     : 2;
	uint64_t reserved_2_3                 : 2;
	uint64_t bp                           : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t en                           : 2;
	uint64_t reserved_10_31               : 22;
	uint64_t tx_prt_bp                    : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} cnf71xx;
};
typedef union cvmx_gmxx_tx_ovr_bp cvmx_gmxx_tx_ovr_bp_t;

/**
 * cvmx_gmx#_tx_pause_pkt_dmac
 *
 * GMX_TX_PAUSE_PKT_DMAC = Packet TX Pause Packet DMAC field
 *
 */
union cvmx_gmxx_tx_pause_pkt_dmac {
	uint64_t u64;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t dmac                         : 48; /**< The DMAC field placed is outbnd pause pkts */
#else
	uint64_t dmac                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn30xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn31xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn38xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn38xxp2;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn50xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn52xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn52xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn56xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn56xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn58xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn58xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn61xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn63xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn63xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn66xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn68xx;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cn68xxp1;
	struct cvmx_gmxx_tx_pause_pkt_dmac_s  cnf71xx;
};
typedef union cvmx_gmxx_tx_pause_pkt_dmac cvmx_gmxx_tx_pause_pkt_dmac_t;

/**
 * cvmx_gmx#_tx_pause_pkt_type
 *
 * GMX_TX_PAUSE_PKT_TYPE = Packet Interface TX Pause Packet TYPE field
 *
 */
union cvmx_gmxx_tx_pause_pkt_type {
	uint64_t u64;
	struct cvmx_gmxx_tx_pause_pkt_type_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t type                         : 16; /**< The TYPE field placed is outbnd pause pkts */
#else
	uint64_t type                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn30xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn31xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn38xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn38xxp2;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn50xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn52xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn52xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn56xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn56xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn58xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn58xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn61xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn63xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn63xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn66xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn68xx;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cn68xxp1;
	struct cvmx_gmxx_tx_pause_pkt_type_s  cnf71xx;
};
typedef union cvmx_gmxx_tx_pause_pkt_type cvmx_gmxx_tx_pause_pkt_type_t;

/**
 * cvmx_gmx#_tx_prts
 *
 * Common
 *
 *
 * GMX_TX_PRTS = TX Ports
 *
 * Notes:
 * * The value programmed for PRTS is the number of the highest architected
 * port number on the interface, plus 1.  For example, if port 2 is the
 * highest architected port, then the programmed value should be 3 since
 * there are 3 ports in the system - 0, 1, and 2.
 */
union cvmx_gmxx_tx_prts {
	uint64_t u64;
	struct cvmx_gmxx_tx_prts_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t prts                         : 5;  /**< Number of ports allowed on the interface
                                                         (SGMII/1000Base-X only) */
#else
	uint64_t prts                         : 5;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_gmxx_tx_prts_s            cn30xx;
	struct cvmx_gmxx_tx_prts_s            cn31xx;
	struct cvmx_gmxx_tx_prts_s            cn38xx;
	struct cvmx_gmxx_tx_prts_s            cn38xxp2;
	struct cvmx_gmxx_tx_prts_s            cn50xx;
	struct cvmx_gmxx_tx_prts_s            cn52xx;
	struct cvmx_gmxx_tx_prts_s            cn52xxp1;
	struct cvmx_gmxx_tx_prts_s            cn56xx;
	struct cvmx_gmxx_tx_prts_s            cn56xxp1;
	struct cvmx_gmxx_tx_prts_s            cn58xx;
	struct cvmx_gmxx_tx_prts_s            cn58xxp1;
	struct cvmx_gmxx_tx_prts_s            cn61xx;
	struct cvmx_gmxx_tx_prts_s            cn63xx;
	struct cvmx_gmxx_tx_prts_s            cn63xxp1;
	struct cvmx_gmxx_tx_prts_s            cn66xx;
	struct cvmx_gmxx_tx_prts_s            cn68xx;
	struct cvmx_gmxx_tx_prts_s            cn68xxp1;
	struct cvmx_gmxx_tx_prts_s            cnf71xx;
};
typedef union cvmx_gmxx_tx_prts cvmx_gmxx_tx_prts_t;

/**
 * cvmx_gmx#_tx_spi_ctl
 *
 * GMX_TX_SPI_CTL = Spi4 TX ModesSpi4
 *
 */
union cvmx_gmxx_tx_spi_ctl {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t tpa_clr                      : 1;  /**< TPA Clear Mode
                                                         Clear credit counter when satisifed status */
	uint64_t cont_pkt                     : 1;  /**< Contiguous Packet Mode
                                                         Finish one packet before switching to another
                                                         Cannot be set in Spi4 pass-through mode */
#else
	uint64_t cont_pkt                     : 1;
	uint64_t tpa_clr                      : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_ctl_s         cn38xx;
	struct cvmx_gmxx_tx_spi_ctl_s         cn38xxp2;
	struct cvmx_gmxx_tx_spi_ctl_s         cn58xx;
	struct cvmx_gmxx_tx_spi_ctl_s         cn58xxp1;
};
typedef union cvmx_gmxx_tx_spi_ctl cvmx_gmxx_tx_spi_ctl_t;

/**
 * cvmx_gmx#_tx_spi_drain
 *
 * GMX_TX_SPI_DRAIN = Drain out Spi TX FIFO
 *
 */
union cvmx_gmxx_tx_spi_drain {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_drain_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t drain                        : 16; /**< Per port drain control
                                                         0=Normal operation
                                                         1=GMX TX will be popped, but no valid data will
                                                           be sent to SPX.  Credits are correctly returned
                                                           to PKO.  STX_IGN_CAL should be set to ignore
                                                           TPA and not stall due to back-pressure.
                                                         (PASS3 only) */
#else
	uint64_t drain                        : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_drain_s       cn38xx;
	struct cvmx_gmxx_tx_spi_drain_s       cn58xx;
	struct cvmx_gmxx_tx_spi_drain_s       cn58xxp1;
};
typedef union cvmx_gmxx_tx_spi_drain cvmx_gmxx_tx_spi_drain_t;

/**
 * cvmx_gmx#_tx_spi_max
 *
 * GMX_TX_SPI_MAX = RGMII TX Spi4 MAX
 *
 */
union cvmx_gmxx_tx_spi_max {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_max_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_23_63               : 41;
	uint64_t slice                        : 7;  /**< Number of 16B blocks to transmit in a burst before
                                                         switching to the next port. SLICE does not always
                                                         limit the burst length transmitted by OCTEON.
                                                         Depending on the traffic pattern and
                                                         GMX_TX_SPI_ROUND programming, the next port could
                                                         be the same as the current port. In this case,
                                                         OCTEON may merge multiple sub-SLICE bursts into
                                                         one contiguous burst that is longer than SLICE
                                                         (as long as the burst does not cross a packet
                                                         boundary).
                                                         SLICE must be programmed to be >=
                                                           GMX_TX_SPI_THRESH[THRESH]
                                                         If SLICE==0, then the transmitter will tend to
                                                         send the complete packet. The port will only
                                                         switch if credits are exhausted or PKO cannot
                                                         keep up.
                                                         (90nm ONLY) */
	uint64_t max2                         : 8;  /**< MAX2 (per Spi4.2 spec) */
	uint64_t max1                         : 8;  /**< MAX1 (per Spi4.2 spec)
                                                         MAX1 >= GMX_TX_SPI_THRESH[THRESH] */
#else
	uint64_t max1                         : 8;
	uint64_t max2                         : 8;
	uint64_t slice                        : 7;
	uint64_t reserved_23_63               : 41;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_max_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t max2                         : 8;  /**< MAX2 (per Spi4.2 spec) */
	uint64_t max1                         : 8;  /**< MAX1 (per Spi4.2 spec)
                                                         MAX1 >= GMX_TX_SPI_THRESH[THRESH] */
#else
	uint64_t max1                         : 8;
	uint64_t max2                         : 8;
	uint64_t reserved_16_63               : 48;
#endif
	} cn38xx;
	struct cvmx_gmxx_tx_spi_max_cn38xx    cn38xxp2;
	struct cvmx_gmxx_tx_spi_max_s         cn58xx;
	struct cvmx_gmxx_tx_spi_max_s         cn58xxp1;
};
typedef union cvmx_gmxx_tx_spi_max cvmx_gmxx_tx_spi_max_t;

/**
 * cvmx_gmx#_tx_spi_round#
 *
 * GMX_TX_SPI_ROUND = Controls SPI4 TX Arbitration
 *
 */
union cvmx_gmxx_tx_spi_roundx {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_roundx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t round                        : 16; /**< Which Spi ports participate in each arbitration
                                                          round.  Each bit corresponds to a spi port
                                                         - 0: this port will arb in this round
                                                         - 1: this port will not arb in this round
                                                          (90nm ONLY) */
#else
	uint64_t round                        : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_roundx_s      cn58xx;
	struct cvmx_gmxx_tx_spi_roundx_s      cn58xxp1;
};
typedef union cvmx_gmxx_tx_spi_roundx cvmx_gmxx_tx_spi_roundx_t;

/**
 * cvmx_gmx#_tx_spi_thresh
 *
 * GMX_TX_SPI_THRESH = RGMII TX Spi4 Transmit Threshold
 *
 *
 * Notes:
 * Note: zero will map to 0x20
 *
 * This will normally creates Spi4 traffic bursts at least THRESH in length.
 * If dclk > eclk, then this rule may not always hold and Octeon may split
 * transfers into smaller bursts - some of which could be as short as 16B.
 * Octeon will never violate the Spi4.2 spec and send a non-EOP burst that is
 * not a multiple of 16B.
 */
union cvmx_gmxx_tx_spi_thresh {
	uint64_t u64;
	struct cvmx_gmxx_tx_spi_thresh_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_6_63                : 58;
	uint64_t thresh                       : 6;  /**< Transmit threshold in 16B blocks - cannot be zero
                                                         THRESH <= TX_FIFO size   (in non-passthrough mode)
                                                         THRESH <= TX_FIFO size-2 (in passthrough mode)
                                                         THRESH <= GMX_TX_SPI_MAX[MAX1]
                                                         THRESH <= GMX_TX_SPI_MAX[MAX2], if not then is it
                                                          possible for Octeon to send a Spi4 data burst of
                                                          MAX2 <= burst <= THRESH 16B ticks
                                                         GMX_TX_SPI_MAX[SLICE] must be programmed to be >=
                                                           THRESH */
#else
	uint64_t thresh                       : 6;
	uint64_t reserved_6_63                : 58;
#endif
	} s;
	struct cvmx_gmxx_tx_spi_thresh_s      cn38xx;
	struct cvmx_gmxx_tx_spi_thresh_s      cn38xxp2;
	struct cvmx_gmxx_tx_spi_thresh_s      cn58xx;
	struct cvmx_gmxx_tx_spi_thresh_s      cn58xxp1;
};
typedef union cvmx_gmxx_tx_spi_thresh cvmx_gmxx_tx_spi_thresh_t;

/**
 * cvmx_gmx#_tx_xaui_ctl
 */
union cvmx_gmxx_tx_xaui_ctl {
	uint64_t u64;
	struct cvmx_gmxx_tx_xaui_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_11_63               : 53;
	uint64_t hg_pause_hgi                 : 2;  /**< HGI Field for HW generated HiGig pause packets
                                                         (XAUI mode only) */
	uint64_t hg_en                        : 1;  /**< Enable HiGig Mode
                                                         When HG_EN is set and GMX_RX_UDD_SKP[SKIP]=12
                                                          the interface is in HiGig/HiGig+ mode and the
                                                          following must be set:
                                                          GMX_RX_FRM_CTL[PRE_CHK] == 0
                                                          GMX_RX_UDD_SKP[FCSSEL] == 0
                                                          GMX_RX_UDD_SKP[SKIP] == 12
                                                          GMX_TX_APPEND[PREAMBLE] == 0
                                                         When HG_EN is set and GMX_RX_UDD_SKP[SKIP]=16
                                                          the interface is in HiGig2 mode and the
                                                          following must be set:
                                                          GMX_RX_FRM_CTL[PRE_CHK] == 0
                                                          GMX_RX_UDD_SKP[FCSSEL] == 0
                                                          GMX_RX_UDD_SKP[SKIP] == 16
                                                          GMX_TX_APPEND[PREAMBLE] == 0
                                                          GMX_PRT0_CBFC_CTL[RX_EN] == 0
                                                          GMX_PRT0_CBFC_CTL[TX_EN] == 0
                                                         (XAUI mode only) */
	uint64_t reserved_7_7                 : 1;
	uint64_t ls_byp                       : 1;  /**< Bypass the link status as determined by the XGMII
                                                         receiver and set the link status of the
                                                         transmitter to LS.
                                                         (XAUI mode only) */
	uint64_t ls                           : 2;  /**< Link Status
                                                         0 = Link Ok
                                                             Link runs normally. RS passes MAC data to PCS
                                                         1 = Local Fault
                                                             RS layer sends continuous remote fault
                                                              sequences.
                                                         2 = Remote Fault
                                                             RS layer sends continuous idles sequences
                                                         3 = Link Drain
                                                             RS layer drops full packets to allow GMX and
                                                              PKO to drain their FIFOs
                                                         (XAUI mode only) */
	uint64_t reserved_2_3                 : 2;
	uint64_t uni_en                       : 1;  /**< Enable Unidirectional Mode (IEEE Clause 66)
                                                         (XAUI mode only) */
	uint64_t dic_en                       : 1;  /**< Enable the deficit idle counter for IFG averaging
                                                         (XAUI mode only) */
#else
	uint64_t dic_en                       : 1;
	uint64_t uni_en                       : 1;
	uint64_t reserved_2_3                 : 2;
	uint64_t ls                           : 2;
	uint64_t ls_byp                       : 1;
	uint64_t reserved_7_7                 : 1;
	uint64_t hg_en                        : 1;
	uint64_t hg_pause_hgi                 : 2;
	uint64_t reserved_11_63               : 53;
#endif
	} s;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn52xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn52xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn56xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn56xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn61xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn63xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn63xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn66xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn68xx;
	struct cvmx_gmxx_tx_xaui_ctl_s        cn68xxp1;
	struct cvmx_gmxx_tx_xaui_ctl_s        cnf71xx;
};
typedef union cvmx_gmxx_tx_xaui_ctl cvmx_gmxx_tx_xaui_ctl_t;

/**
 * cvmx_gmx#_xaui_ext_loopback
 */
union cvmx_gmxx_xaui_ext_loopback {
	uint64_t u64;
	struct cvmx_gmxx_xaui_ext_loopback_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_5_63                : 59;
	uint64_t en                           : 1;  /**< Loopback enable
                                                         Puts the packet interface in external loopback
                                                         mode on the XAUI bus in which the RX lines are
                                                         reflected on the TX lines.
                                                         (XAUI mode only) */
	uint64_t thresh                       : 4;  /**< Threshhold on the TX FIFO
                                                         SW must only write the typical value.  Any other
                                                         value will cause loopback mode not to function
                                                         correctly.
                                                         (XAUI mode only) */
#else
	uint64_t thresh                       : 4;
	uint64_t en                           : 1;
	uint64_t reserved_5_63                : 59;
#endif
	} s;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn52xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn52xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn56xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn56xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn61xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn63xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn63xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn66xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn68xx;
	struct cvmx_gmxx_xaui_ext_loopback_s  cn68xxp1;
	struct cvmx_gmxx_xaui_ext_loopback_s  cnf71xx;
};
typedef union cvmx_gmxx_xaui_ext_loopback cvmx_gmxx_xaui_ext_loopback_t;

#endif
