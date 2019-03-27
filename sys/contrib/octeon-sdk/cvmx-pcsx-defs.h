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
 * cvmx-pcsx-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pcsx.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PCSX_DEFS_H__
#define __CVMX_PCSX_DEFS_H__

static inline uint64_t CVMX_PCSX_ANX_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_ANX_ADV_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001010ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_ANX_EXT_ST_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_ANX_EXT_ST_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001028ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_ANX_LP_ABIL_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_ANX_LP_ABIL_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001018ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_ANX_RESULTS_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_ANX_RESULTS_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001020ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_INTX_EN_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_INTX_EN_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001088ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_INTX_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_INTX_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001080ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_LINKX_TIMER_COUNT_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_LINKX_TIMER_COUNT_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001040ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_LOG_ANLX_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_LOG_ANLX_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001090ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_MISCX_CTL_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_MISCX_CTL_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001078ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_MRX_CONTROL_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_MRX_CONTROL_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001000ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_MRX_STATUS_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_MRX_STATUS_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001008ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_RXX_STATES_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_RXX_STATES_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001058ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_RXX_SYNC_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_RXX_SYNC_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001050ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_SGMX_AN_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_SGMX_AN_ADV_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001068ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_SGMX_LP_ADV_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_SGMX_LP_ADV_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001070ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_TXX_STATES_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_TXX_STATES_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001060ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}
static inline uint64_t CVMX_PCSX_TX_RXX_POLARITY_REG(unsigned long offset, unsigned long block_id)
{
	switch(cvmx_get_octeon_family()) {
		case OCTEON_CNF71XX & OCTEON_FAMILY_MASK:
			if (((offset <= 1)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN63XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN52XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id == 0)))
				return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + (((offset) & 3) + ((block_id) & 0) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN56XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN66XX & OCTEON_FAMILY_MASK:
		case OCTEON_CN61XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 1)))
				return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + (((offset) & 3) + ((block_id) & 1) * 0x20000ull) * 1024;
			break;
		case OCTEON_CN68XX & OCTEON_FAMILY_MASK:
			if (((offset <= 3)) && ((block_id <= 4)))
				return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + (((offset) & 3) + ((block_id) & 7) * 0x4000ull) * 1024;
			break;
	}
	cvmx_warn("CVMX_PCSX_TX_RXX_POLARITY_REG (%lu, %lu) not supported on this chip\n", offset, block_id);
	return CVMX_ADD_IO_SEG(0x00011800B0001048ull) + (((offset) & 1) + ((block_id) & 0) * 0x20000ull) * 1024;
}

/**
 * cvmx_pcs#_an#_adv_reg
 *
 * Bits [15:9] in the Status Register indicate ability to operate as per those signalling specification,
 * when misc ctl reg MAC_PHY bit is set to MAC mode. Bits [15:9] will all, always read 1'b0, indicating
 * that the chip cannot operate in the corresponding modes.
 *
 * Bit [4] RM_FLT is a don't care when the selected mode is SGMII.
 *
 *
 *
 * PCS_AN_ADV_REG = AN Advertisement Register4
 */
union cvmx_pcsx_anx_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t np                           : 1;  /**< Always 0, no next page capability supported */
	uint64_t reserved_14_14               : 1;
	uint64_t rem_flt                      : 2;  /**< [<13>,<12>]
                                                         0    0  Link OK  XMIT=DATA
                                                         0    1  Link failure (loss of sync, XMIT!= DATA)
                                                         1    0  local device Offline
                                                         1    1  AN Error failure to complete AN
                                                                 AN Error is set if resolution function
                                                                 precludes operation with link partner */
	uint64_t reserved_9_11                : 3;
	uint64_t pause                        : 2;  /**< [<8>, <7>] Pause frame flow capability across link
                                                                  Exchanged during Auto Negotiation
                                                         0    0  No Pause
                                                         0    1  Symmetric pause
                                                         1    0  Asymmetric Pause
                                                         1    1  Both symm and asymm pause to local device */
	uint64_t hfd                          : 1;  /**< 1 means local device Half Duplex capable */
	uint64_t fd                           : 1;  /**< 1 means local device Full Duplex capable */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t fd                           : 1;
	uint64_t hfd                          : 1;
	uint64_t pause                        : 2;
	uint64_t reserved_9_11                : 3;
	uint64_t rem_flt                      : 2;
	uint64_t reserved_14_14               : 1;
	uint64_t np                           : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_anx_adv_reg_s        cn52xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn52xxp1;
	struct cvmx_pcsx_anx_adv_reg_s        cn56xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn56xxp1;
	struct cvmx_pcsx_anx_adv_reg_s        cn61xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn63xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn63xxp1;
	struct cvmx_pcsx_anx_adv_reg_s        cn66xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn68xx;
	struct cvmx_pcsx_anx_adv_reg_s        cn68xxp1;
	struct cvmx_pcsx_anx_adv_reg_s        cnf71xx;
};
typedef union cvmx_pcsx_anx_adv_reg cvmx_pcsx_anx_adv_reg_t;

/**
 * cvmx_pcs#_an#_ext_st_reg
 *
 * NOTE:
 * an_results_reg is don't care when AN_OVRD is set to 1. If AN_OVRD=0 and AN_CPT=1
 * the an_results_reg is valid.
 *
 *
 * PCS_AN_EXT_ST_REG = AN Extended Status Register15
 * as per IEEE802.3 Clause 22
 */
union cvmx_pcsx_anx_ext_st_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_ext_st_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t thou_xfd                     : 1;  /**< 1 means PHY is 1000BASE-X Full Dup capable */
	uint64_t thou_xhd                     : 1;  /**< 1 means PHY is 1000BASE-X Half Dup capable */
	uint64_t thou_tfd                     : 1;  /**< 1 means PHY is 1000BASE-T Full Dup capable */
	uint64_t thou_thd                     : 1;  /**< 1 means PHY is 1000BASE-T Half Dup capable */
	uint64_t reserved_0_11                : 12;
#else
	uint64_t reserved_0_11                : 12;
	uint64_t thou_thd                     : 1;
	uint64_t thou_tfd                     : 1;
	uint64_t thou_xhd                     : 1;
	uint64_t thou_xfd                     : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn52xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn52xxp1;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn56xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn56xxp1;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn61xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn63xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn63xxp1;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn66xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn68xx;
	struct cvmx_pcsx_anx_ext_st_reg_s     cn68xxp1;
	struct cvmx_pcsx_anx_ext_st_reg_s     cnf71xx;
};
typedef union cvmx_pcsx_anx_ext_st_reg cvmx_pcsx_anx_ext_st_reg_t;

/**
 * cvmx_pcs#_an#_lp_abil_reg
 *
 * PCS_AN_LP_ABIL_REG = AN link Partner Ability Register5
 * as per IEEE802.3 Clause 37
 */
union cvmx_pcsx_anx_lp_abil_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_lp_abil_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t np                           : 1;  /**< 1=lp next page capable, 0=lp not next page capable */
	uint64_t ack                          : 1;  /**< 1=Acknowledgement received */
	uint64_t rem_flt                      : 2;  /**< [<13>,<12>] Link Partner's link status
                                                         0    0  Link OK
                                                         0    1  Offline
                                                         1    0  Link failure
                                                         1    1  AN Error */
	uint64_t reserved_9_11                : 3;
	uint64_t pause                        : 2;  /**< [<8>, <7>] Link Partner Pause setting
                                                         0    0  No Pause
                                                         0    1  Symmetric pause
                                                         1    0  Asymmetric Pause
                                                         1    1  Both symm and asymm pause to local device */
	uint64_t hfd                          : 1;  /**< 1 means link partner Half Duplex capable */
	uint64_t fd                           : 1;  /**< 1 means link partner Full Duplex capable */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t fd                           : 1;
	uint64_t hfd                          : 1;
	uint64_t pause                        : 2;
	uint64_t reserved_9_11                : 3;
	uint64_t rem_flt                      : 2;
	uint64_t ack                          : 1;
	uint64_t np                           : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn52xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn52xxp1;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn56xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn56xxp1;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn61xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn63xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn63xxp1;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn66xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn68xx;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cn68xxp1;
	struct cvmx_pcsx_anx_lp_abil_reg_s    cnf71xx;
};
typedef union cvmx_pcsx_anx_lp_abil_reg cvmx_pcsx_anx_lp_abil_reg_t;

/**
 * cvmx_pcs#_an#_results_reg
 *
 * PCS_AN_RESULTS_REG = AN Results Register
 *
 */
union cvmx_pcsx_anx_results_reg {
	uint64_t u64;
	struct cvmx_pcsx_anx_results_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t pause                        : 2;  /**< [<6>, <5>] PAUSE Selection (Don't care for SGMII)
                                                         0    0  Disable Pause, TX and RX
                                                         0    1  Enable pause frames RX only
                                                         1    0  Enable Pause frames TX only
                                                         1    1  Enable pause frames TX and RX */
	uint64_t spd                          : 2;  /**< [<4>, <3>] Link Speed Selection
                                                         0    0  10Mb/s
                                                         0    1  100Mb/s
                                                         1    0  1000Mb/s
                                                         1    1  NS */
	uint64_t an_cpt                       : 1;  /**< 1=AN Completed, 0=AN not completed or failed */
	uint64_t dup                          : 1;  /**< 1=Full Duplex, 0=Half Duplex */
	uint64_t link_ok                      : 1;  /**< 1=Link up(OK), 0=Link down */
#else
	uint64_t link_ok                      : 1;
	uint64_t dup                          : 1;
	uint64_t an_cpt                       : 1;
	uint64_t spd                          : 2;
	uint64_t pause                        : 2;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pcsx_anx_results_reg_s    cn52xx;
	struct cvmx_pcsx_anx_results_reg_s    cn52xxp1;
	struct cvmx_pcsx_anx_results_reg_s    cn56xx;
	struct cvmx_pcsx_anx_results_reg_s    cn56xxp1;
	struct cvmx_pcsx_anx_results_reg_s    cn61xx;
	struct cvmx_pcsx_anx_results_reg_s    cn63xx;
	struct cvmx_pcsx_anx_results_reg_s    cn63xxp1;
	struct cvmx_pcsx_anx_results_reg_s    cn66xx;
	struct cvmx_pcsx_anx_results_reg_s    cn68xx;
	struct cvmx_pcsx_anx_results_reg_s    cn68xxp1;
	struct cvmx_pcsx_anx_results_reg_s    cnf71xx;
};
typedef union cvmx_pcsx_anx_results_reg cvmx_pcsx_anx_results_reg_t;

/**
 * cvmx_pcs#_int#_en_reg
 *
 * NOTE: RXERR and TXERR conditions to be discussed with Dan before finalising
 *      DBG_SYNC interrupt fires when code group synchronization state machine makes a transition from
 *      SYNC_ACQUIRED_1 state to SYNC_ACQUIRED_2 state(See IEEE 802.3-2005 figure 37-9). It is an indication that a bad code group
 *      was received after code group synchronizaton was achieved. This interrupt should be disabled during normal link operation.
 *      Use it as a debug help feature only.
 *
 *
 * PCS Interrupt Enable Register
 */
union cvmx_pcsx_intx_en_reg {
	uint64_t u64;
	struct cvmx_pcsx_intx_en_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t dbg_sync_en                  : 1;  /**< Code Group sync failure debug help */
	uint64_t dup                          : 1;  /**< Enable duplex mode changed interrupt */
	uint64_t sync_bad_en                  : 1;  /**< Enable rx sync st machine in bad state interrupt */
	uint64_t an_bad_en                    : 1;  /**< Enable AN state machine bad state interrupt */
	uint64_t rxlock_en                    : 1;  /**< Enable rx code group sync/bit lock failure interrupt */
	uint64_t rxbad_en                     : 1;  /**< Enable rx state machine in bad state interrupt */
	uint64_t rxerr_en                     : 1;  /**< Enable RX error condition interrupt */
	uint64_t txbad_en                     : 1;  /**< Enable tx state machine in bad state interrupt */
	uint64_t txfifo_en                    : 1;  /**< Enable tx fifo overflow condition interrupt */
	uint64_t txfifu_en                    : 1;  /**< Enable tx fifo underflow condition intrrupt */
	uint64_t an_err_en                    : 1;  /**< Enable AN Error condition interrupt */
	uint64_t xmit_en                      : 1;  /**< Enable XMIT variable state change interrupt */
	uint64_t lnkspd_en                    : 1;  /**< Enable Link Speed has changed interrupt */
#else
	uint64_t lnkspd_en                    : 1;
	uint64_t xmit_en                      : 1;
	uint64_t an_err_en                    : 1;
	uint64_t txfifu_en                    : 1;
	uint64_t txfifo_en                    : 1;
	uint64_t txbad_en                     : 1;
	uint64_t rxerr_en                     : 1;
	uint64_t rxbad_en                     : 1;
	uint64_t rxlock_en                    : 1;
	uint64_t an_bad_en                    : 1;
	uint64_t sync_bad_en                  : 1;
	uint64_t dup                          : 1;
	uint64_t dbg_sync_en                  : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pcsx_intx_en_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t dup                          : 1;  /**< Enable duplex mode changed interrupt */
	uint64_t sync_bad_en                  : 1;  /**< Enable rx sync st machine in bad state interrupt */
	uint64_t an_bad_en                    : 1;  /**< Enable AN state machine bad state interrupt */
	uint64_t rxlock_en                    : 1;  /**< Enable rx code group sync/bit lock failure interrupt */
	uint64_t rxbad_en                     : 1;  /**< Enable rx state machine in bad state interrupt */
	uint64_t rxerr_en                     : 1;  /**< Enable RX error condition interrupt */
	uint64_t txbad_en                     : 1;  /**< Enable tx state machine in bad state interrupt */
	uint64_t txfifo_en                    : 1;  /**< Enable tx fifo overflow condition interrupt */
	uint64_t txfifu_en                    : 1;  /**< Enable tx fifo underflow condition intrrupt */
	uint64_t an_err_en                    : 1;  /**< Enable AN Error condition interrupt */
	uint64_t xmit_en                      : 1;  /**< Enable XMIT variable state change interrupt */
	uint64_t lnkspd_en                    : 1;  /**< Enable Link Speed has changed interrupt */
#else
	uint64_t lnkspd_en                    : 1;
	uint64_t xmit_en                      : 1;
	uint64_t an_err_en                    : 1;
	uint64_t txfifu_en                    : 1;
	uint64_t txfifo_en                    : 1;
	uint64_t txbad_en                     : 1;
	uint64_t rxerr_en                     : 1;
	uint64_t rxbad_en                     : 1;
	uint64_t rxlock_en                    : 1;
	uint64_t an_bad_en                    : 1;
	uint64_t sync_bad_en                  : 1;
	uint64_t dup                          : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn52xx;
	struct cvmx_pcsx_intx_en_reg_cn52xx   cn52xxp1;
	struct cvmx_pcsx_intx_en_reg_cn52xx   cn56xx;
	struct cvmx_pcsx_intx_en_reg_cn52xx   cn56xxp1;
	struct cvmx_pcsx_intx_en_reg_s        cn61xx;
	struct cvmx_pcsx_intx_en_reg_s        cn63xx;
	struct cvmx_pcsx_intx_en_reg_s        cn63xxp1;
	struct cvmx_pcsx_intx_en_reg_s        cn66xx;
	struct cvmx_pcsx_intx_en_reg_s        cn68xx;
	struct cvmx_pcsx_intx_en_reg_s        cn68xxp1;
	struct cvmx_pcsx_intx_en_reg_s        cnf71xx;
};
typedef union cvmx_pcsx_intx_en_reg cvmx_pcsx_intx_en_reg_t;

/**
 * cvmx_pcs#_int#_reg
 *
 * SGMII bit [12] is really a misnomer, it is a decode  of pi_qlm_cfg pins to indicate SGMII or 1000Base-X modes.
 *
 * Note: MODE bit
 * When MODE=1,  1000Base-X mode is selected. Auto negotiation will follow IEEE 802.3 clause 37.
 * When MODE=0,  SGMII mode is selected and the following note will apply.
 * Repeat note from SGM_AN_ADV register
 * NOTE: The SGMII AN Advertisement Register above will be sent during Auto Negotiation if the MAC_PHY mode bit in misc_ctl_reg
 * is set (1=PHY mode). If the bit is not set (0=MAC mode), the tx_config_reg[14] becomes ACK bit and [0] is always 1.
 * All other bits in tx_config_reg sent will be 0. The PHY dictates the Auto Negotiation results.
 *
 * PCS Interrupt Register
 */
union cvmx_pcsx_intx_reg {
	uint64_t u64;
	struct cvmx_pcsx_intx_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t dbg_sync                     : 1;  /**< Code Group sync failure debug help */
	uint64_t dup                          : 1;  /**< Set whenever Duplex mode changes on the link */
	uint64_t sync_bad                     : 1;  /**< Set by HW whenever rx sync st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t an_bad                       : 1;  /**< Set by HW whenever AN st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t rxlock                       : 1;  /**< Set by HW whenever code group Sync or bit lock
                                                         failure occurs
                                                         Cannot fire in loopback1 mode */
	uint64_t rxbad                        : 1;  /**< Set by HW whenever rx st machine reaches a  bad
                                                         state. Should never be set during normal operation */
	uint64_t rxerr                        : 1;  /**< Set whenever RX receives a code group error in
                                                         10 bit to 8 bit decode logic
                                                         Cannot fire in loopback1 mode */
	uint64_t txbad                        : 1;  /**< Set by HW whenever tx st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t txfifo                       : 1;  /**< Set whenever HW detects a TX fifo overflow
                                                         condition */
	uint64_t txfifu                       : 1;  /**< Set whenever HW detects a TX fifo underflowflow
                                                         condition */
	uint64_t an_err                       : 1;  /**< AN Error, AN resolution function failed */
	uint64_t xmit                         : 1;  /**< Set whenever HW detects a change in the XMIT
                                                         variable. XMIT variable states are IDLE, CONFIG and
                                                         DATA */
	uint64_t lnkspd                       : 1;  /**< Set by HW whenever Link Speed has changed */
#else
	uint64_t lnkspd                       : 1;
	uint64_t xmit                         : 1;
	uint64_t an_err                       : 1;
	uint64_t txfifu                       : 1;
	uint64_t txfifo                       : 1;
	uint64_t txbad                        : 1;
	uint64_t rxerr                        : 1;
	uint64_t rxbad                        : 1;
	uint64_t rxlock                       : 1;
	uint64_t an_bad                       : 1;
	uint64_t sync_bad                     : 1;
	uint64_t dup                          : 1;
	uint64_t dbg_sync                     : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pcsx_intx_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t dup                          : 1;  /**< Set whenever Duplex mode changes on the link */
	uint64_t sync_bad                     : 1;  /**< Set by HW whenever rx sync st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t an_bad                       : 1;  /**< Set by HW whenever AN st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t rxlock                       : 1;  /**< Set by HW whenever code group Sync or bit lock
                                                         failure occurs
                                                         Cannot fire in loopback1 mode */
	uint64_t rxbad                        : 1;  /**< Set by HW whenever rx st machine reaches a  bad
                                                         state. Should never be set during normal operation */
	uint64_t rxerr                        : 1;  /**< Set whenever RX receives a code group error in
                                                         10 bit to 8 bit decode logic
                                                         Cannot fire in loopback1 mode */
	uint64_t txbad                        : 1;  /**< Set by HW whenever tx st machine reaches a bad
                                                         state. Should never be set during normal operation */
	uint64_t txfifo                       : 1;  /**< Set whenever HW detects a TX fifo overflow
                                                         condition */
	uint64_t txfifu                       : 1;  /**< Set whenever HW detects a TX fifo underflowflow
                                                         condition */
	uint64_t an_err                       : 1;  /**< AN Error, AN resolution function failed */
	uint64_t xmit                         : 1;  /**< Set whenever HW detects a change in the XMIT
                                                         variable. XMIT variable states are IDLE, CONFIG and
                                                         DATA */
	uint64_t lnkspd                       : 1;  /**< Set by HW whenever Link Speed has changed */
#else
	uint64_t lnkspd                       : 1;
	uint64_t xmit                         : 1;
	uint64_t an_err                       : 1;
	uint64_t txfifu                       : 1;
	uint64_t txfifo                       : 1;
	uint64_t txbad                        : 1;
	uint64_t rxerr                        : 1;
	uint64_t rxbad                        : 1;
	uint64_t rxlock                       : 1;
	uint64_t an_bad                       : 1;
	uint64_t sync_bad                     : 1;
	uint64_t dup                          : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn52xx;
	struct cvmx_pcsx_intx_reg_cn52xx      cn52xxp1;
	struct cvmx_pcsx_intx_reg_cn52xx      cn56xx;
	struct cvmx_pcsx_intx_reg_cn52xx      cn56xxp1;
	struct cvmx_pcsx_intx_reg_s           cn61xx;
	struct cvmx_pcsx_intx_reg_s           cn63xx;
	struct cvmx_pcsx_intx_reg_s           cn63xxp1;
	struct cvmx_pcsx_intx_reg_s           cn66xx;
	struct cvmx_pcsx_intx_reg_s           cn68xx;
	struct cvmx_pcsx_intx_reg_s           cn68xxp1;
	struct cvmx_pcsx_intx_reg_s           cnf71xx;
};
typedef union cvmx_pcsx_intx_reg cvmx_pcsx_intx_reg_t;

/**
 * cvmx_pcs#_link#_timer_count_reg
 *
 * PCS_LINK_TIMER_COUNT_REG = 1.6ms nominal link timer register
 *
 */
union cvmx_pcsx_linkx_timer_count_reg {
	uint64_t u64;
	struct cvmx_pcsx_linkx_timer_count_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t count                        : 16; /**< (core clock period times 1024) times "COUNT" should
                                                         be 1.6ms(SGMII)/10ms(otherwise) which is the link
                                                         timer used in auto negotiation.
                                                         Reset assums a 700MHz eclk for 1.6ms link timer */
#else
	uint64_t count                        : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn52xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn52xxp1;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn56xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn56xxp1;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn61xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn63xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn63xxp1;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn66xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn68xx;
	struct cvmx_pcsx_linkx_timer_count_reg_s cn68xxp1;
	struct cvmx_pcsx_linkx_timer_count_reg_s cnf71xx;
};
typedef union cvmx_pcsx_linkx_timer_count_reg cvmx_pcsx_linkx_timer_count_reg_t;

/**
 * cvmx_pcs#_log_anl#_reg
 *
 * PCS Logic Analyzer Register
 *
 */
union cvmx_pcsx_log_anlx_reg {
	uint64_t u64;
	struct cvmx_pcsx_log_anlx_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t lafifovfl                    : 1;  /**< 1=logic analyser fif overflowed during packetization
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
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pcsx_log_anlx_reg_s       cn52xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn52xxp1;
	struct cvmx_pcsx_log_anlx_reg_s       cn56xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn56xxp1;
	struct cvmx_pcsx_log_anlx_reg_s       cn61xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn63xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn63xxp1;
	struct cvmx_pcsx_log_anlx_reg_s       cn66xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn68xx;
	struct cvmx_pcsx_log_anlx_reg_s       cn68xxp1;
	struct cvmx_pcsx_log_anlx_reg_s       cnf71xx;
};
typedef union cvmx_pcsx_log_anlx_reg cvmx_pcsx_log_anlx_reg_t;

/**
 * cvmx_pcs#_misc#_ctl_reg
 *
 * SGMII Misc Control Register
 *
 */
union cvmx_pcsx_miscx_ctl_reg {
	uint64_t u64;
	struct cvmx_pcsx_miscx_ctl_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t sgmii                        : 1;  /**< 1=SGMII or 1000Base-X mode selected,
                                                         0=XAUI or PCIE mode selected
                                                         This bit represents pi_qlm1/3_cfg[1:0] pin status */
	uint64_t gmxeno                       : 1;  /**< GMX Enable override. When set to 1, forces GMX to
                                                         appear disabled. The enable/disable status of GMX
                                                         is checked only at SOP of every packet. */
	uint64_t loopbck2                     : 1;  /**< Sets external loopback mode to return rx data back
                                                         out via tx data path. 0=no loopback, 1=loopback */
	uint64_t mac_phy                      : 1;  /**< 0=MAC, 1=PHY decides the tx_config_reg value to be
                                                         sent during auto negotiation.
                                                         See SGMII spec ENG-46158 from CISCO */
	uint64_t mode                         : 1;  /**< 0=SGMII or 1= 1000 Base X */
	uint64_t an_ovrd                      : 1;  /**< 0=disable, 1= enable over ride AN results
                                                         Auto negotiation is allowed to happen but the
                                                         results are ignored when set. Duplex and Link speed
                                                         values are set from the pcs_mr_ctrl reg */
	uint64_t samp_pt                      : 7;  /**< Byte# in elongated frames for 10/100Mb/s operation
                                                         for data sampling on RX side in PCS.
                                                         Recommended values are 0x5 for 100Mb/s operation
                                                         and 0x32 for 10Mb/s operation.
                                                         For 10Mb/s operaton this field should be set to a
                                                         value less than 99 and greater than 0. If set out
                                                         of this range a value of 50 will be used for actual
                                                         sampling internally without affecting the CSR field
                                                         For 100Mb/s operation this field should be set to a
                                                         value less than 9 and greater than 0. If set out of
                                                         this range a value of 5 will be used for actual
                                                         sampling internally without affecting the CSR field */
#else
	uint64_t samp_pt                      : 7;
	uint64_t an_ovrd                      : 1;
	uint64_t mode                         : 1;
	uint64_t mac_phy                      : 1;
	uint64_t loopbck2                     : 1;
	uint64_t gmxeno                       : 1;
	uint64_t sgmii                        : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn52xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn52xxp1;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn56xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn56xxp1;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn61xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn63xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn63xxp1;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn66xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn68xx;
	struct cvmx_pcsx_miscx_ctl_reg_s      cn68xxp1;
	struct cvmx_pcsx_miscx_ctl_reg_s      cnf71xx;
};
typedef union cvmx_pcsx_miscx_ctl_reg cvmx_pcsx_miscx_ctl_reg_t;

/**
 * cvmx_pcs#_mr#_control_reg
 *
 * PCS_MR_CONTROL_REG = Control Register0
 *
 */
union cvmx_pcsx_mrx_control_reg {
	uint64_t u64;
	struct cvmx_pcsx_mrx_control_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t reset                        : 1;  /**< 1=SW Reset, the bit will return to 0 after pcs has
                                                         been reset. Takes 32 eclk cycles to reset pcs */
	uint64_t loopbck1                     : 1;  /**< 0=normal operation, 1=loopback. The loopback mode
                                                         will return(loopback) tx data from GMII tx back to
                                                         GMII rx interface. The loopback happens in the pcs
                                                         module. Auto Negotiation will be disabled even if
                                                         the AN_EN bit is set, during loopback */
	uint64_t spdlsb                       : 1;  /**< See bit 6 description */
	uint64_t an_en                        : 1;  /**< 1=AN Enable, 0=AN Disable */
	uint64_t pwr_dn                       : 1;  /**< 1=Power Down(HW reset), 0=Normal operation */
	uint64_t reserved_10_10               : 1;
	uint64_t rst_an                       : 1;  /**< If bit 12 is set and bit 3 of status reg is 1
                                                         Auto Negotiation begins. Else,SW writes are ignored
                                                         and this bit remians at 0. This bit clears itself
                                                         to 0, when AN starts. */
	uint64_t dup                          : 1;  /**< 1=full duplex, 0=half duplex; effective only if AN
                                                         disabled. If status register bits [15:9] and and
                                                         extended status reg bits [15:12] allow only one
                                                         duplex mode|, this bit will correspond to that
                                                         value and any attempt to write will be ignored. */
	uint64_t coltst                       : 1;  /**< 1=enable COL signal test, 0=disable test
                                                         During COL test, the COL signal will reflect the
                                                         GMII TX_EN signal with less than 16BT delay */
	uint64_t spdmsb                       : 1;  /**< [<6>, <13>]Link Speed effective only if AN disabled
                                                         0    0  10Mb/s
                                                         0    1  100Mb/s
                                                         1    0  1000Mb/s
                                                         1    1  NS */
	uint64_t uni                          : 1;  /**< Unidirectional (Std 802.3-2005, Clause 66.2)
                                                         This bit will override the AN_EN bit and disable
                                                         auto-negotiation variable mr_an_enable, when set
                                                         Used in both 1000Base-X and SGMII modes */
	uint64_t reserved_0_4                 : 5;
#else
	uint64_t reserved_0_4                 : 5;
	uint64_t uni                          : 1;
	uint64_t spdmsb                       : 1;
	uint64_t coltst                       : 1;
	uint64_t dup                          : 1;
	uint64_t rst_an                       : 1;
	uint64_t reserved_10_10               : 1;
	uint64_t pwr_dn                       : 1;
	uint64_t an_en                        : 1;
	uint64_t spdlsb                       : 1;
	uint64_t loopbck1                     : 1;
	uint64_t reset                        : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_mrx_control_reg_s    cn52xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn52xxp1;
	struct cvmx_pcsx_mrx_control_reg_s    cn56xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn56xxp1;
	struct cvmx_pcsx_mrx_control_reg_s    cn61xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn63xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn63xxp1;
	struct cvmx_pcsx_mrx_control_reg_s    cn66xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn68xx;
	struct cvmx_pcsx_mrx_control_reg_s    cn68xxp1;
	struct cvmx_pcsx_mrx_control_reg_s    cnf71xx;
};
typedef union cvmx_pcsx_mrx_control_reg cvmx_pcsx_mrx_control_reg_t;

/**
 * cvmx_pcs#_mr#_status_reg
 *
 * NOTE:
 * Whenever AN_EN bit[12] is set, Auto negotiation is allowed to happen. The results
 * of the auto negotiation process set the fields in the AN_RESULTS reg. When AN_EN is not set,
 * AN_RESULTS reg is don't care. The effective SPD, DUP etc.. get their values
 * from the pcs_mr_ctrl reg.
 *
 *  PCS_MR_STATUS_REG = Status Register1
 */
union cvmx_pcsx_mrx_status_reg {
	uint64_t u64;
	struct cvmx_pcsx_mrx_status_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t hun_t4                       : 1;  /**< 1 means 100Base-T4 capable */
	uint64_t hun_xfd                      : 1;  /**< 1 means 100Base-X Full Duplex */
	uint64_t hun_xhd                      : 1;  /**< 1 means 100Base-X Half Duplex */
	uint64_t ten_fd                       : 1;  /**< 1 means 10Mb/s Full Duplex */
	uint64_t ten_hd                       : 1;  /**< 1 means 10Mb/s Half Duplex */
	uint64_t hun_t2fd                     : 1;  /**< 1 means 100Base-T2 Full Duplex */
	uint64_t hun_t2hd                     : 1;  /**< 1 means 100Base-T2 Half Duplex */
	uint64_t ext_st                       : 1;  /**< 1 means extended status info in reg15 */
	uint64_t reserved_7_7                 : 1;
	uint64_t prb_sup                      : 1;  /**< 1 means able to work without preamble bytes at the
                                                         beginning of frames. 0 means not able to accept
                                                         frames without preamble bytes preceding them. */
	uint64_t an_cpt                       : 1;  /**< 1 means Auto Negotiation is complete and the
                                                         contents of the an_results_reg are valid. */
	uint64_t rm_flt                       : 1;  /**< Set to 1 when remote flt condition occurs. This bit
                                                         implements a latching Hi behavior. It is cleared by
                                                         SW read of this reg or when reset bit [15] in
                                                         Control Reg is asserted.
                                                         See an adv reg[13:12] for flt conditions */
	uint64_t an_abil                      : 1;  /**< 1 means Auto Negotiation capable */
	uint64_t lnk_st                       : 1;  /**< 1=link up, 0=link down. Set during AN process
                                                         Set whenever XMIT=DATA. Latching Lo behavior when
                                                         link goes down. Link down value of the bit stays
                                                         low until SW reads the reg. */
	uint64_t reserved_1_1                 : 1;
	uint64_t extnd                        : 1;  /**< Always 0, no extended capability regs present */
#else
	uint64_t extnd                        : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t lnk_st                       : 1;
	uint64_t an_abil                      : 1;
	uint64_t rm_flt                       : 1;
	uint64_t an_cpt                       : 1;
	uint64_t prb_sup                      : 1;
	uint64_t reserved_7_7                 : 1;
	uint64_t ext_st                       : 1;
	uint64_t hun_t2hd                     : 1;
	uint64_t hun_t2fd                     : 1;
	uint64_t ten_hd                       : 1;
	uint64_t ten_fd                       : 1;
	uint64_t hun_xhd                      : 1;
	uint64_t hun_xfd                      : 1;
	uint64_t hun_t4                       : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_mrx_status_reg_s     cn52xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn52xxp1;
	struct cvmx_pcsx_mrx_status_reg_s     cn56xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn56xxp1;
	struct cvmx_pcsx_mrx_status_reg_s     cn61xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn63xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn63xxp1;
	struct cvmx_pcsx_mrx_status_reg_s     cn66xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn68xx;
	struct cvmx_pcsx_mrx_status_reg_s     cn68xxp1;
	struct cvmx_pcsx_mrx_status_reg_s     cnf71xx;
};
typedef union cvmx_pcsx_mrx_status_reg cvmx_pcsx_mrx_status_reg_t;

/**
 * cvmx_pcs#_rx#_states_reg
 *
 * PCS_RX_STATES_REG = RX State Machines states register
 *
 */
union cvmx_pcsx_rxx_states_reg {
	uint64_t u64;
	struct cvmx_pcsx_rxx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t rx_bad                       : 1;  /**< Receive state machine in an illegal state */
	uint64_t rx_st                        : 5;  /**< Receive state machine state */
	uint64_t sync_bad                     : 1;  /**< Receive synchronization SM in an illegal state */
	uint64_t sync                         : 4;  /**< Receive synchronization SM state */
	uint64_t an_bad                       : 1;  /**< Auto Negotiation state machine in an illegal state */
	uint64_t an_st                        : 4;  /**< Auto Negotiation state machine state */
#else
	uint64_t an_st                        : 4;
	uint64_t an_bad                       : 1;
	uint64_t sync                         : 4;
	uint64_t sync_bad                     : 1;
	uint64_t rx_st                        : 5;
	uint64_t rx_bad                       : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_rxx_states_reg_s     cn52xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn52xxp1;
	struct cvmx_pcsx_rxx_states_reg_s     cn56xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn56xxp1;
	struct cvmx_pcsx_rxx_states_reg_s     cn61xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn63xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn63xxp1;
	struct cvmx_pcsx_rxx_states_reg_s     cn66xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn68xx;
	struct cvmx_pcsx_rxx_states_reg_s     cn68xxp1;
	struct cvmx_pcsx_rxx_states_reg_s     cnf71xx;
};
typedef union cvmx_pcsx_rxx_states_reg cvmx_pcsx_rxx_states_reg_t;

/**
 * cvmx_pcs#_rx#_sync_reg
 *
 * Note:
 * r_tx_rx_polarity_reg bit [2] will show correct polarity needed on the link receive path after code grp synchronization is achieved.
 *
 *
 *  PCS_RX_SYNC_REG = Code Group synchronization reg
 */
union cvmx_pcsx_rxx_sync_reg {
	uint64_t u64;
	struct cvmx_pcsx_rxx_sync_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t sync                         : 1;  /**< 1 means code group synchronization achieved */
	uint64_t bit_lock                     : 1;  /**< 1 means bit lock achieved */
#else
	uint64_t bit_lock                     : 1;
	uint64_t sync                         : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pcsx_rxx_sync_reg_s       cn52xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn52xxp1;
	struct cvmx_pcsx_rxx_sync_reg_s       cn56xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn56xxp1;
	struct cvmx_pcsx_rxx_sync_reg_s       cn61xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn63xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn63xxp1;
	struct cvmx_pcsx_rxx_sync_reg_s       cn66xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn68xx;
	struct cvmx_pcsx_rxx_sync_reg_s       cn68xxp1;
	struct cvmx_pcsx_rxx_sync_reg_s       cnf71xx;
};
typedef union cvmx_pcsx_rxx_sync_reg cvmx_pcsx_rxx_sync_reg_t;

/**
 * cvmx_pcs#_sgm#_an_adv_reg
 *
 * SGMII AN Advertisement Register (sent out as tx_config_reg)
 *
 */
union cvmx_pcsx_sgmx_an_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_sgmx_an_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t link                         : 1;  /**< Link status 1 Link Up, 0 Link Down */
	uint64_t ack                          : 1;  /**< Auto negotiation ack */
	uint64_t reserved_13_13               : 1;
	uint64_t dup                          : 1;  /**< Duplex mode 1=full duplex, 0=half duplex */
	uint64_t speed                        : 2;  /**< Link Speed
                                                         0    0  10Mb/s
                                                         0    1  100Mb/s
                                                         1    0  1000Mb/s
                                                         1    1  NS */
	uint64_t reserved_1_9                 : 9;
	uint64_t one                          : 1;  /**< Always set to match tx_config_reg<0> */
#else
	uint64_t one                          : 1;
	uint64_t reserved_1_9                 : 9;
	uint64_t speed                        : 2;
	uint64_t dup                          : 1;
	uint64_t reserved_13_13               : 1;
	uint64_t ack                          : 1;
	uint64_t link                         : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn52xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn52xxp1;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn56xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn56xxp1;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn61xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn63xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn63xxp1;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn66xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn68xx;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cn68xxp1;
	struct cvmx_pcsx_sgmx_an_adv_reg_s    cnf71xx;
};
typedef union cvmx_pcsx_sgmx_an_adv_reg cvmx_pcsx_sgmx_an_adv_reg_t;

/**
 * cvmx_pcs#_sgm#_lp_adv_reg
 *
 * NOTE: The SGMII AN Advertisement Register above will be sent during Auto Negotiation if the MAC_PHY mode bit in misc_ctl_reg
 * is set (1=PHY mode). If the bit is not set (0=MAC mode), the tx_config_reg[14] becomes ACK bit and [0] is always 1.
 * All other bits in tx_config_reg sent will be 0. The PHY dictates the Auto Negotiation results.
 *
 * SGMII LP Advertisement Register (received as rx_config_reg)
 */
union cvmx_pcsx_sgmx_lp_adv_reg {
	uint64_t u64;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t link                         : 1;  /**< Link status 1 Link Up, 0 Link Down */
	uint64_t reserved_13_14               : 2;
	uint64_t dup                          : 1;  /**< Duplex mode 1=full duplex, 0=half duplex */
	uint64_t speed                        : 2;  /**< Link Speed
                                                         0    0  10Mb/s
                                                         0    1  100Mb/s
                                                         1    0  1000Mb/s
                                                         1    1  NS */
	uint64_t reserved_1_9                 : 9;
	uint64_t one                          : 1;  /**< Always set to match tx_config_reg<0> */
#else
	uint64_t one                          : 1;
	uint64_t reserved_1_9                 : 9;
	uint64_t speed                        : 2;
	uint64_t dup                          : 1;
	uint64_t reserved_13_14               : 2;
	uint64_t link                         : 1;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn52xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn52xxp1;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn56xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn56xxp1;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn61xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn63xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn63xxp1;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn66xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn68xx;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cn68xxp1;
	struct cvmx_pcsx_sgmx_lp_adv_reg_s    cnf71xx;
};
typedef union cvmx_pcsx_sgmx_lp_adv_reg cvmx_pcsx_sgmx_lp_adv_reg_t;

/**
 * cvmx_pcs#_tx#_states_reg
 *
 * PCS_TX_STATES_REG = TX State Machines states register
 *
 */
union cvmx_pcsx_txx_states_reg {
	uint64_t u64;
	struct cvmx_pcsx_txx_states_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t xmit                         : 2;  /**< 0=undefined, 1=config, 2=idle, 3=data */
	uint64_t tx_bad                       : 1;  /**< Xmit state machine in a bad state */
	uint64_t ord_st                       : 4;  /**< Xmit ordered set state machine state */
#else
	uint64_t ord_st                       : 4;
	uint64_t tx_bad                       : 1;
	uint64_t xmit                         : 2;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pcsx_txx_states_reg_s     cn52xx;
	struct cvmx_pcsx_txx_states_reg_s     cn52xxp1;
	struct cvmx_pcsx_txx_states_reg_s     cn56xx;
	struct cvmx_pcsx_txx_states_reg_s     cn56xxp1;
	struct cvmx_pcsx_txx_states_reg_s     cn61xx;
	struct cvmx_pcsx_txx_states_reg_s     cn63xx;
	struct cvmx_pcsx_txx_states_reg_s     cn63xxp1;
	struct cvmx_pcsx_txx_states_reg_s     cn66xx;
	struct cvmx_pcsx_txx_states_reg_s     cn68xx;
	struct cvmx_pcsx_txx_states_reg_s     cn68xxp1;
	struct cvmx_pcsx_txx_states_reg_s     cnf71xx;
};
typedef union cvmx_pcsx_txx_states_reg cvmx_pcsx_txx_states_reg_t;

/**
 * cvmx_pcs#_tx_rx#_polarity_reg
 *
 * PCS_POLARITY_REG = TX_RX polarity reg
 *
 */
union cvmx_pcsx_tx_rxx_polarity_reg {
	uint64_t u64;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_4_63                : 60;
	uint64_t rxovrd                       : 1;  /**< When 0, <2> determines polarity
                                                         when 1, <1> determines polarity */
	uint64_t autorxpl                     : 1;  /**< Auto RX polarity detected. 1=inverted, 0=normal
                                                         This bit always represents the correct rx polarity
                                                         setting needed for successful rx path operartion,
                                                         once a successful code group sync is obtained */
	uint64_t rxplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
	uint64_t txplrt                       : 1;  /**< 1 is inverted polarity, 0 is normal polarity */
#else
	uint64_t txplrt                       : 1;
	uint64_t rxplrt                       : 1;
	uint64_t autorxpl                     : 1;
	uint64_t rxovrd                       : 1;
	uint64_t reserved_4_63                : 60;
#endif
	} s;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn52xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn52xxp1;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn56xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn56xxp1;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn61xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn63xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn63xxp1;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn66xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn68xx;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cn68xxp1;
	struct cvmx_pcsx_tx_rxx_polarity_reg_s cnf71xx;
};
typedef union cvmx_pcsx_tx_rxx_polarity_reg cvmx_pcsx_tx_rxx_polarity_reg_t;

#endif
