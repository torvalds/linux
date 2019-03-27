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
 * cvmx-pip-defs.h
 *
 * Configuration and status register (CSR) type definitions for
 * Octeon pip.
 *
 * This file is auto generated. Do not edit.
 *
 * <hr>$Revision$<hr>
 *
 */
#ifndef __CVMX_PIP_DEFS_H__
#define __CVMX_PIP_DEFS_H__

#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_ALT_SKIP_CFGX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PIP_ALT_SKIP_CFGX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002A00ull) + ((offset) & 3) * 8;
}
#else
#define CVMX_PIP_ALT_SKIP_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002A00ull) + ((offset) & 3) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PIP_BCK_PRS CVMX_PIP_BCK_PRS_FUNC()
static inline uint64_t CVMX_PIP_BCK_PRS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN38XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN58XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PIP_BCK_PRS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800A0000038ull);
}
#else
#define CVMX_PIP_BCK_PRS (CVMX_ADD_IO_SEG(0x00011800A0000038ull))
#endif
#define CVMX_PIP_BIST_STATUS (CVMX_ADD_IO_SEG(0x00011800A0000000ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_BSEL_EXT_CFGX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PIP_BSEL_EXT_CFGX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002800ull) + ((offset) & 3) * 16;
}
#else
#define CVMX_PIP_BSEL_EXT_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002800ull) + ((offset) & 3) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_BSEL_EXT_POSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PIP_BSEL_EXT_POSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002808ull) + ((offset) & 3) * 16;
}
#else
#define CVMX_PIP_BSEL_EXT_POSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002808ull) + ((offset) & 3) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_BSEL_TBL_ENTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 511))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 511))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 511)))))
		cvmx_warn("CVMX_PIP_BSEL_TBL_ENTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0003000ull) + ((offset) & 511) * 8;
}
#else
#define CVMX_PIP_BSEL_TBL_ENTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0003000ull) + ((offset) & 511) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PIP_CLKEN CVMX_PIP_CLKEN_FUNC()
static inline uint64_t CVMX_PIP_CLKEN_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PIP_CLKEN not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800A0000040ull);
}
#else
#define CVMX_PIP_CLKEN (CVMX_ADD_IO_SEG(0x00011800A0000040ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_CRC_CTLX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PIP_CRC_CTLX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000040ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PIP_CRC_CTLX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000040ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_CRC_IVX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PIP_CRC_IVX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000050ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PIP_CRC_IVX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000050ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_DEC_IPSECX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3)))))
		cvmx_warn("CVMX_PIP_DEC_IPSECX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000080ull) + ((offset) & 3) * 8;
}
#else
#define CVMX_PIP_DEC_IPSECX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000080ull) + ((offset) & 3) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PIP_DSA_SRC_GRP CVMX_PIP_DSA_SRC_GRP_FUNC()
static inline uint64_t CVMX_PIP_DSA_SRC_GRP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PIP_DSA_SRC_GRP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800A0000190ull);
}
#else
#define CVMX_PIP_DSA_SRC_GRP (CVMX_ADD_IO_SEG(0x00011800A0000190ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PIP_DSA_VID_GRP CVMX_PIP_DSA_VID_GRP_FUNC()
static inline uint64_t CVMX_PIP_DSA_VID_GRP_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CN68XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PIP_DSA_VID_GRP not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800A0000198ull);
}
#else
#define CVMX_PIP_DSA_VID_GRP (CVMX_ADD_IO_SEG(0x00011800A0000198ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_FRM_LEN_CHKX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset == 0))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset == 0)))))
		cvmx_warn("CVMX_PIP_FRM_LEN_CHKX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000180ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PIP_FRM_LEN_CHKX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000180ull) + ((offset) & 1) * 8)
#endif
#define CVMX_PIP_GBL_CFG (CVMX_ADD_IO_SEG(0x00011800A0000028ull))
#define CVMX_PIP_GBL_CTL (CVMX_ADD_IO_SEG(0x00011800A0000020ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
#define CVMX_PIP_HG_PRI_QOS CVMX_PIP_HG_PRI_QOS_FUNC()
static inline uint64_t CVMX_PIP_HG_PRI_QOS_FUNC(void)
{
	if (!(OCTEON_IS_MODEL(OCTEON_CN52XX) || OCTEON_IS_MODEL(OCTEON_CN56XX) || OCTEON_IS_MODEL(OCTEON_CN61XX) || OCTEON_IS_MODEL(OCTEON_CN63XX) || OCTEON_IS_MODEL(OCTEON_CN66XX) || OCTEON_IS_MODEL(OCTEON_CNF71XX)))
		cvmx_warn("CVMX_PIP_HG_PRI_QOS not supported on this chip\n");
	return CVMX_ADD_IO_SEG(0x00011800A00001A0ull);
}
#else
#define CVMX_PIP_HG_PRI_QOS (CVMX_ADD_IO_SEG(0x00011800A00001A0ull))
#endif
#define CVMX_PIP_INT_EN (CVMX_ADD_IO_SEG(0x00011800A0000010ull))
#define CVMX_PIP_INT_REG (CVMX_ADD_IO_SEG(0x00011800A0000008ull))
#define CVMX_PIP_IP_OFFSET (CVMX_ADD_IO_SEG(0x00011800A0000060ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_PRI_TBLX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 255)))))
		cvmx_warn("CVMX_PIP_PRI_TBLX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0004000ull) + ((offset) & 255) * 8;
}
#else
#define CVMX_PIP_PRI_TBLX(offset) (CVMX_ADD_IO_SEG(0x00011800A0004000ull) + ((offset) & 255) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_PRT_CFGBX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_PRT_CFGBX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0008000ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_PIP_PRT_CFGBX(offset) (CVMX_ADD_IO_SEG(0x00011800A0008000ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_PRT_CFGX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_PRT_CFGX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000200ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_PIP_PRT_CFGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000200ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_PRT_TAGX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_PRT_TAGX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000400ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_PIP_PRT_TAGX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000400ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_QOS_DIFFX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_QOS_DIFFX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000600ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_PIP_QOS_DIFFX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000600ull) + ((offset) & 63) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_QOS_VLANX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_PIP_QOS_VLANX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A00000C0ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_PIP_QOS_VLANX(offset) (CVMX_ADD_IO_SEG(0x00011800A00000C0ull) + ((offset) & 7) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_QOS_WATCHX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 3))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 7))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 7)))))
		cvmx_warn("CVMX_PIP_QOS_WATCHX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000100ull) + ((offset) & 7) * 8;
}
#else
#define CVMX_PIP_QOS_WATCHX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000100ull) + ((offset) & 7) * 8)
#endif
#define CVMX_PIP_RAW_WORD (CVMX_ADD_IO_SEG(0x00011800A00000B0ull))
#define CVMX_PIP_SFT_RST (CVMX_ADD_IO_SEG(0x00011800A0000030ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT0_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT0_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000800ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT0_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000800ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT0_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT0_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040000ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT0_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040000ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT10_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT10_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001480ull) + ((offset) & 63) * 16;
}
#else
#define CVMX_PIP_STAT10_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001480ull) + ((offset) & 63) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT10_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT10_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040050ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT10_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040050ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT11_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT11_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001488ull) + ((offset) & 63) * 16;
}
#else
#define CVMX_PIP_STAT11_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001488ull) + ((offset) & 63) * 16)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT11_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT11_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040058ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT11_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040058ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT1_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT1_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000808ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT1_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000808ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT1_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT1_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040008ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT1_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040008ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT2_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT2_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000810ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT2_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000810ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT2_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT2_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040010ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT2_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040010ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT3_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT3_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000818ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT3_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000818ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT3_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT3_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040018ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT3_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040018ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT4_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT4_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000820ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT4_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000820ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT4_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT4_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040020ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT4_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040020ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT5_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT5_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000828ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT5_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000828ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT5_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT5_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040028ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT5_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040028ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT6_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT6_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000830ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT6_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000830ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT6_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT6_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040030ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT6_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040030ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT7_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT7_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000838ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT7_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000838ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT7_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT7_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040038ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT7_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040038ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT8_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT8_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000840ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT8_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000840ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT8_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT8_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040040ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT8_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040040ull) + ((offset) & 63) * 128)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT9_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT9_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0000848ull) + ((offset) & 63) * 80;
}
#else
#define CVMX_PIP_STAT9_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0000848ull) + ((offset) & 63) * 80)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT9_X(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT9_X(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0040048ull) + ((offset) & 63) * 128;
}
#else
#define CVMX_PIP_STAT9_X(offset) (CVMX_ADD_IO_SEG(0x00011800A0040048ull) + ((offset) & 63) * 128)
#endif
#define CVMX_PIP_STAT_CTL (CVMX_ADD_IO_SEG(0x00011800A0000018ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_ERRSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT_INB_ERRSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001A10ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_ERRSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A10ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_ERRS_PKNDX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT_INB_ERRS_PKNDX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0020010ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_ERRS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020010ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_OCTSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT_INB_OCTSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001A08ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_OCTSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A08ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_OCTS_PKNDX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT_INB_OCTS_PKNDX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0020008ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_OCTS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020008ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_PKTSX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 2) || ((offset >= 32) && (offset <= 33)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 35))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 3) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 3) || ((offset >= 16) && (offset <= 19)) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39)) || ((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1) || ((offset >= 32) && (offset <= 35)) || ((offset >= 36) && (offset <= 39))))))
		cvmx_warn("CVMX_PIP_STAT_INB_PKTSX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001A00ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_PKTSX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001A00ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_STAT_INB_PKTS_PKNDX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_STAT_INB_PKTS_PKNDX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0020000ull) + ((offset) & 63) * 32;
}
#else
#define CVMX_PIP_STAT_INB_PKTS_PKNDX(offset) (CVMX_ADD_IO_SEG(0x00011800A0020000ull) + ((offset) & 63) * 32)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_SUB_PKIND_FCSX(unsigned long block_id)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((block_id == 0)))))
		cvmx_warn("CVMX_PIP_SUB_PKIND_FCSX(%lu) is invalid on this chip\n", block_id);
	return CVMX_ADD_IO_SEG(0x00011800A0080000ull);
}
#else
#define CVMX_PIP_SUB_PKIND_FCSX(block_id) (CVMX_ADD_IO_SEG(0x00011800A0080000ull))
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_TAG_INCX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN30XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN31XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN38XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN50XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN52XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN56XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN58XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 63))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 63)))))
		cvmx_warn("CVMX_PIP_TAG_INCX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001800ull) + ((offset) & 63) * 8;
}
#else
#define CVMX_PIP_TAG_INCX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001800ull) + ((offset) & 63) * 8)
#endif
#define CVMX_PIP_TAG_MASK (CVMX_ADD_IO_SEG(0x00011800A0000070ull))
#define CVMX_PIP_TAG_SECRET (CVMX_ADD_IO_SEG(0x00011800A0000068ull))
#define CVMX_PIP_TODO_ENTRY (CVMX_ADD_IO_SEG(0x00011800A0000078ull))
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_VLAN_ETYPESX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN61XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN68XX) && ((offset <= 1))) ||
	      (OCTEON_IS_MODEL(OCTEON_CNF71XX) && ((offset <= 1)))))
		cvmx_warn("CVMX_PIP_VLAN_ETYPESX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A00001C0ull) + ((offset) & 1) * 8;
}
#else
#define CVMX_PIP_VLAN_ETYPESX(offset) (CVMX_ADD_IO_SEG(0x00011800A00001C0ull) + ((offset) & 1) * 8)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT0_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT0_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002000ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT0_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002000ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT10_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT10_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001700ull) + ((offset) & 63) * 16 - 16*40;
}
#else
#define CVMX_PIP_XSTAT10_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001700ull) + ((offset) & 63) * 16 - 16*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT11_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT11_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0001708ull) + ((offset) & 63) * 16 - 16*40;
}
#else
#define CVMX_PIP_XSTAT11_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0001708ull) + ((offset) & 63) * 16 - 16*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT1_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT1_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002008ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT1_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002008ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT2_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT2_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002010ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT2_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002010ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT3_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT3_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002018ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT3_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002018ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT4_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT4_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002020ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT4_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002020ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT5_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT5_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002028ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT5_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002028ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT6_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT6_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002030ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT6_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002030ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT7_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT7_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002038ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT7_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002038ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT8_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT8_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002040ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT8_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002040ull) + ((offset) & 63) * 80 - 80*40)
#endif
#if CVMX_ENABLE_CSR_ADDRESS_CHECKING
static inline uint64_t CVMX_PIP_XSTAT9_PRTX(unsigned long offset)
{
	if (!(
	      (OCTEON_IS_MODEL(OCTEON_CN63XX) && (((offset >= 40) && (offset <= 43)))) ||
	      (OCTEON_IS_MODEL(OCTEON_CN66XX) && (((offset >= 40) && (offset <= 41)) || ((offset >= 44) && (offset <= 47))))))
		cvmx_warn("CVMX_PIP_XSTAT9_PRTX(%lu) is invalid on this chip\n", offset);
	return CVMX_ADD_IO_SEG(0x00011800A0002048ull) + ((offset) & 63) * 80 - 80*40;
}
#else
#define CVMX_PIP_XSTAT9_PRTX(offset) (CVMX_ADD_IO_SEG(0x00011800A0002048ull) + ((offset) & 63) * 80 - 80*40)
#endif

/**
 * cvmx_pip_alt_skip_cfg#
 *
 * Notes:
 * The actual SKIP I determined by HW is based on the packet contents.  BIT0 and
 * BIT1 make up a two value value that the selects the skip value as follows.
 *
 *    lookup_value = LEN ? ( packet_in_bits[BIT1], packet_in_bits[BIT0] ) : ( 0, packet_in_bits[BIT0] );
 *    SKIP I       = lookup_value == 3 ? SKIP3 :
 *                   lookup_value == 2 ? SKIP2 :
 *                   lookup_value == 1 ? SKIP1 :
 *                   PIP_PRT_CFG<pknd>[SKIP];
 */
union cvmx_pip_alt_skip_cfgx {
	uint64_t u64;
	struct cvmx_pip_alt_skip_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_57_63               : 7;
	uint64_t len                          : 1;  /**< Indicates the length of the selection field
                                                         0 = 0,    BIT0
                                                         1 = BIT1, BIT0 */
	uint64_t reserved_46_55               : 10;
	uint64_t bit1                         : 6;  /**< Indicates the bit location in the first word of
                                                         the packet to use to select the skip amount.
                                                         BIT1 must be present in the packet. */
	uint64_t reserved_38_39               : 2;
	uint64_t bit0                         : 6;  /**< Indicates the bit location in the first word of
                                                         the packet to use to select the skip amount.
                                                         BIT0 must be present in the packet. */
	uint64_t reserved_23_31               : 9;
	uint64_t skip3                        : 7;  /**< Indicates number of bytes to skip from start of
                                                         packet 0-64 */
	uint64_t reserved_15_15               : 1;
	uint64_t skip2                        : 7;  /**< Indicates number of bytes to skip from start of
                                                         packet 0-64 */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip1                        : 7;  /**< Indicates number of bytes to skip from start of
                                                         packet 0-64 */
#else
	uint64_t skip1                        : 7;
	uint64_t reserved_7_7                 : 1;
	uint64_t skip2                        : 7;
	uint64_t reserved_15_15               : 1;
	uint64_t skip3                        : 7;
	uint64_t reserved_23_31               : 9;
	uint64_t bit0                         : 6;
	uint64_t reserved_38_39               : 2;
	uint64_t bit1                         : 6;
	uint64_t reserved_46_55               : 10;
	uint64_t len                          : 1;
	uint64_t reserved_57_63               : 7;
#endif
	} s;
	struct cvmx_pip_alt_skip_cfgx_s       cn61xx;
	struct cvmx_pip_alt_skip_cfgx_s       cn66xx;
	struct cvmx_pip_alt_skip_cfgx_s       cn68xx;
	struct cvmx_pip_alt_skip_cfgx_s       cnf71xx;
};
typedef union cvmx_pip_alt_skip_cfgx cvmx_pip_alt_skip_cfgx_t;

/**
 * cvmx_pip_bck_prs
 *
 * PIP_BCK_PRS = PIP's Back Pressure Register
 *
 * When to assert backpressure based on the todo list filling up
 */
union cvmx_pip_bck_prs {
	uint64_t u64;
	struct cvmx_pip_bck_prs_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bckprs                       : 1;  /**< PIP is currently asserting backpressure to IOB
                                                         Backpressure from PIP will assert when the
                                                         entries to the todo list exceed HIWATER.
                                                         Backpressure will be held until the todo entries
                                                         is less than or equal to LOWATER. */
	uint64_t reserved_13_62               : 50;
	uint64_t hiwater                      : 5;  /**< Water mark in the todo list to assert backpressure
                                                         Legal values are 1-26.  A 0 value will deadlock
                                                         the machine.  A value > 26, will trash memory */
	uint64_t reserved_5_7                 : 3;
	uint64_t lowater                      : 5;  /**< Water mark in the todo list to release backpressure
                                                         The LOWATER value should be < HIWATER. */
#else
	uint64_t lowater                      : 5;
	uint64_t reserved_5_7                 : 3;
	uint64_t hiwater                      : 5;
	uint64_t reserved_13_62               : 50;
	uint64_t bckprs                       : 1;
#endif
	} s;
	struct cvmx_pip_bck_prs_s             cn38xx;
	struct cvmx_pip_bck_prs_s             cn38xxp2;
	struct cvmx_pip_bck_prs_s             cn56xx;
	struct cvmx_pip_bck_prs_s             cn56xxp1;
	struct cvmx_pip_bck_prs_s             cn58xx;
	struct cvmx_pip_bck_prs_s             cn58xxp1;
	struct cvmx_pip_bck_prs_s             cn61xx;
	struct cvmx_pip_bck_prs_s             cn63xx;
	struct cvmx_pip_bck_prs_s             cn63xxp1;
	struct cvmx_pip_bck_prs_s             cn66xx;
	struct cvmx_pip_bck_prs_s             cn68xx;
	struct cvmx_pip_bck_prs_s             cn68xxp1;
	struct cvmx_pip_bck_prs_s             cnf71xx;
};
typedef union cvmx_pip_bck_prs cvmx_pip_bck_prs_t;

/**
 * cvmx_pip_bist_status
 *
 * PIP_BIST_STATUS = PIP's BIST Results
 *
 */
union cvmx_pip_bist_status {
	uint64_t u64;
	struct cvmx_pip_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_22_63               : 42;
	uint64_t bist                         : 22; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         BIST. */
#else
	uint64_t bist                         : 22;
	uint64_t reserved_22_63               : 42;
#endif
	} s;
	struct cvmx_pip_bist_status_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t bist                         : 18; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         BIST. */
#else
	uint64_t bist                         : 18;
	uint64_t reserved_18_63               : 46;
#endif
	} cn30xx;
	struct cvmx_pip_bist_status_cn30xx    cn31xx;
	struct cvmx_pip_bist_status_cn30xx    cn38xx;
	struct cvmx_pip_bist_status_cn30xx    cn38xxp2;
	struct cvmx_pip_bist_status_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t bist                         : 17; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         BIST. */
#else
	uint64_t bist                         : 17;
	uint64_t reserved_17_63               : 47;
#endif
	} cn50xx;
	struct cvmx_pip_bist_status_cn30xx    cn52xx;
	struct cvmx_pip_bist_status_cn30xx    cn52xxp1;
	struct cvmx_pip_bist_status_cn30xx    cn56xx;
	struct cvmx_pip_bist_status_cn30xx    cn56xxp1;
	struct cvmx_pip_bist_status_cn30xx    cn58xx;
	struct cvmx_pip_bist_status_cn30xx    cn58xxp1;
	struct cvmx_pip_bist_status_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_20_63               : 44;
	uint64_t bist                         : 20; /**< BIST Results.
                                                         HW sets a bit in BIST for for memory that fails
                                                         BIST. */
#else
	uint64_t bist                         : 20;
	uint64_t reserved_20_63               : 44;
#endif
	} cn61xx;
	struct cvmx_pip_bist_status_cn30xx    cn63xx;
	struct cvmx_pip_bist_status_cn30xx    cn63xxp1;
	struct cvmx_pip_bist_status_cn61xx    cn66xx;
	struct cvmx_pip_bist_status_s         cn68xx;
	struct cvmx_pip_bist_status_cn61xx    cn68xxp1;
	struct cvmx_pip_bist_status_cn61xx    cnf71xx;
};
typedef union cvmx_pip_bist_status cvmx_pip_bist_status_t;

/**
 * cvmx_pip_bsel_ext_cfg#
 *
 * PIP_BSEL_EXT_CFGX = Bit Select Extractor config register containing the
 * tag, offset, and skip values to be used when using the corresponding extractor.
 */
union cvmx_pip_bsel_ext_cfgx {
	uint64_t u64;
	struct cvmx_pip_bsel_ext_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t upper_tag                    : 16; /**< Extra Tag bits to be added to tag field from table
                                                         Only included when PIP_PRT_TAG[INC_PRT]=0
                                                         WORD2[TAG<31:16>] */
	uint64_t tag                          : 8;  /**< Extra Tag bits to be added to tag field from table
                                                         WORD2[TAG<15:8>] */
	uint64_t reserved_25_31               : 7;
	uint64_t offset                       : 9;  /**< Indicates offset to add to extractor mem adr
                                                         to get final address to the lookup table */
	uint64_t reserved_7_15                : 9;
	uint64_t skip                         : 7;  /**< Indicates number of bytes to skip from start of
                                                         packet 0-64 */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_15                : 9;
	uint64_t offset                       : 9;
	uint64_t reserved_25_31               : 7;
	uint64_t tag                          : 8;
	uint64_t upper_tag                    : 16;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_pip_bsel_ext_cfgx_s       cn61xx;
	struct cvmx_pip_bsel_ext_cfgx_s       cn68xx;
	struct cvmx_pip_bsel_ext_cfgx_s       cnf71xx;
};
typedef union cvmx_pip_bsel_ext_cfgx cvmx_pip_bsel_ext_cfgx_t;

/**
 * cvmx_pip_bsel_ext_pos#
 *
 * PIP_BSEL_EXT_POSX = Bit Select Extractor config register containing the 8
 * bit positions and valids to be used when using the corresponding extractor.
 *
 * Notes:
 * Examples on bit positioning:
 *   the most-significant-bit of the 3rd byte         ... PIP_BSEL_EXT_CFG*[SKIP]=1 POSn=15 (decimal) or
 *                                                        PIP_BSEL_EXT_CFG*[SKIP]=0 POSn=23 (decimal)
 *   the least-significant-bit of the 5th byte        ... PIP_BSEL_EXT_CFG*[SKIP]=4 POSn=0
 *   the second-least-significant bit of the 1st byte ... PIP_BSEL_EXT_CFG*[SKIP]=0 POSn=1
 *
 * POSn_VAL and POSn correspond to <n> in the resultant index into
 * PIP_BSEL_TBL_ENT. When only x bits (0 < x < 7) are to be extracted,
 * POS[7:x] should normally be clear.
 */
union cvmx_pip_bsel_ext_posx {
	uint64_t u64;
	struct cvmx_pip_bsel_ext_posx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pos7_val                     : 1;  /**< Valid bit for bit position 7 */
	uint64_t pos7                         : 7;  /**< Bit position for the 8th bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos6_val                     : 1;  /**< Valid bit for bit position 6 */
	uint64_t pos6                         : 7;  /**< Bit position for the 7th bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos5_val                     : 1;  /**< Valid bit for bit position 5 */
	uint64_t pos5                         : 7;  /**< Bit position for the 6th bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos4_val                     : 1;  /**< Valid bit for bit position 4 */
	uint64_t pos4                         : 7;  /**< Bit position for the 5th bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos3_val                     : 1;  /**< Valid bit for bit position 3 */
	uint64_t pos3                         : 7;  /**< Bit position for the 4th bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos2_val                     : 1;  /**< Valid bit for bit position 2 */
	uint64_t pos2                         : 7;  /**< Bit position for the 3rd bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos1_val                     : 1;  /**< Valid bit for bit position 1 */
	uint64_t pos1                         : 7;  /**< Bit position for the 2nd bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
	uint64_t pos0_val                     : 1;  /**< Valid bit for bit position 0 */
	uint64_t pos0                         : 7;  /**< Bit position for the 1st bit  from 128 bit segment
                                                         of pkt that is defined by the SKIP field of
                                                         PIP_BSEL_EXT_CFG register. */
#else
	uint64_t pos0                         : 7;
	uint64_t pos0_val                     : 1;
	uint64_t pos1                         : 7;
	uint64_t pos1_val                     : 1;
	uint64_t pos2                         : 7;
	uint64_t pos2_val                     : 1;
	uint64_t pos3                         : 7;
	uint64_t pos3_val                     : 1;
	uint64_t pos4                         : 7;
	uint64_t pos4_val                     : 1;
	uint64_t pos5                         : 7;
	uint64_t pos5_val                     : 1;
	uint64_t pos6                         : 7;
	uint64_t pos6_val                     : 1;
	uint64_t pos7                         : 7;
	uint64_t pos7_val                     : 1;
#endif
	} s;
	struct cvmx_pip_bsel_ext_posx_s       cn61xx;
	struct cvmx_pip_bsel_ext_posx_s       cn68xx;
	struct cvmx_pip_bsel_ext_posx_s       cnf71xx;
};
typedef union cvmx_pip_bsel_ext_posx cvmx_pip_bsel_ext_posx_t;

/**
 * cvmx_pip_bsel_tbl_ent#
 *
 * PIP_BSEL_TBL_ENTX = Entry for the extractor table
 *
 */
union cvmx_pip_bsel_tbl_entx {
	uint64_t u64;
	struct cvmx_pip_bsel_tbl_entx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t tag_en                       : 1;  /**< Enables the use of the TAG field */
	uint64_t grp_en                       : 1;  /**< Enables the use of the GRP field */
	uint64_t tt_en                        : 1;  /**< Enables the use of the TT field */
	uint64_t qos_en                       : 1;  /**< Enables the use of the QOS field */
	uint64_t reserved_40_59               : 20;
	uint64_t tag                          : 8;  /**< TAG bits to be used if TAG_EN is set */
	uint64_t reserved_22_31               : 10;
	uint64_t grp                          : 6;  /**< GRP field to be used if GRP_EN is set */
	uint64_t reserved_10_15               : 6;
	uint64_t tt                           : 2;  /**< TT field to be used if TT_EN is set */
	uint64_t reserved_3_7                 : 5;
	uint64_t qos                          : 3;  /**< QOS field to be used if QOS_EN is set */
#else
	uint64_t qos                          : 3;
	uint64_t reserved_3_7                 : 5;
	uint64_t tt                           : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t grp                          : 6;
	uint64_t reserved_22_31               : 10;
	uint64_t tag                          : 8;
	uint64_t reserved_40_59               : 20;
	uint64_t qos_en                       : 1;
	uint64_t tt_en                        : 1;
	uint64_t grp_en                       : 1;
	uint64_t tag_en                       : 1;
#endif
	} s;
	struct cvmx_pip_bsel_tbl_entx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t tag_en                       : 1;  /**< Enables the use of the TAG field */
	uint64_t grp_en                       : 1;  /**< Enables the use of the GRP field */
	uint64_t tt_en                        : 1;  /**< Enables the use of the TT field */
	uint64_t qos_en                       : 1;  /**< Enables the use of the QOS field */
	uint64_t reserved_40_59               : 20;
	uint64_t tag                          : 8;  /**< TAG bits to be used if TAG_EN is set */
	uint64_t reserved_20_31               : 12;
	uint64_t grp                          : 4;  /**< GRP field to be used if GRP_EN is set */
	uint64_t reserved_10_15               : 6;
	uint64_t tt                           : 2;  /**< TT field to be used if TT_EN is set */
	uint64_t reserved_3_7                 : 5;
	uint64_t qos                          : 3;  /**< QOS field to be used if QOS_EN is set */
#else
	uint64_t qos                          : 3;
	uint64_t reserved_3_7                 : 5;
	uint64_t tt                           : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t grp                          : 4;
	uint64_t reserved_20_31               : 12;
	uint64_t tag                          : 8;
	uint64_t reserved_40_59               : 20;
	uint64_t qos_en                       : 1;
	uint64_t tt_en                        : 1;
	uint64_t grp_en                       : 1;
	uint64_t tag_en                       : 1;
#endif
	} cn61xx;
	struct cvmx_pip_bsel_tbl_entx_s       cn68xx;
	struct cvmx_pip_bsel_tbl_entx_cn61xx  cnf71xx;
};
typedef union cvmx_pip_bsel_tbl_entx cvmx_pip_bsel_tbl_entx_t;

/**
 * cvmx_pip_clken
 */
union cvmx_pip_clken {
	uint64_t u64;
	struct cvmx_pip_clken_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t clken                        : 1;  /**< Controls the conditional clocking within PIP
                                                         0=Allow HW to control the clocks
                                                         1=Force the clocks to be always on */
#else
	uint64_t clken                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_pip_clken_s               cn61xx;
	struct cvmx_pip_clken_s               cn63xx;
	struct cvmx_pip_clken_s               cn63xxp1;
	struct cvmx_pip_clken_s               cn66xx;
	struct cvmx_pip_clken_s               cn68xx;
	struct cvmx_pip_clken_s               cn68xxp1;
	struct cvmx_pip_clken_s               cnf71xx;
};
typedef union cvmx_pip_clken cvmx_pip_clken_t;

/**
 * cvmx_pip_crc_ctl#
 *
 * PIP_CRC_CTL = PIP CRC Control Register
 *
 * Controls datapath reflection when calculating CRC
 */
union cvmx_pip_crc_ctlx {
	uint64_t u64;
	struct cvmx_pip_crc_ctlx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_2_63                : 62;
	uint64_t invres                       : 1;  /**< Invert the result */
	uint64_t reflect                      : 1;  /**< Reflect the bits in each byte.
                                                          Byte order does not change.
                                                         - 0: CRC is calculated MSB to LSB
                                                         - 1: CRC is calculated LSB to MSB */
#else
	uint64_t reflect                      : 1;
	uint64_t invres                       : 1;
	uint64_t reserved_2_63                : 62;
#endif
	} s;
	struct cvmx_pip_crc_ctlx_s            cn38xx;
	struct cvmx_pip_crc_ctlx_s            cn38xxp2;
	struct cvmx_pip_crc_ctlx_s            cn58xx;
	struct cvmx_pip_crc_ctlx_s            cn58xxp1;
};
typedef union cvmx_pip_crc_ctlx cvmx_pip_crc_ctlx_t;

/**
 * cvmx_pip_crc_iv#
 *
 * PIP_CRC_IV = PIP CRC IV Register
 *
 * Determines the IV used by the CRC algorithm
 *
 * Notes:
 * * PIP_CRC_IV
 * PIP_CRC_IV controls the initial state of the CRC algorithm.  Octane can
 * support a wide range of CRC algorithms and as such, the IV must be
 * carefully constructed to meet the specific algorithm.  The code below
 * determines the value to program into Octane based on the algorthim's IV
 * and width.  In the case of Octane, the width should always be 32.
 *
 * PIP_CRC_IV0 sets the IV for ports 0-15 while PIP_CRC_IV1 sets the IV for
 * ports 16-31.
 *
 *  unsigned octane_crc_iv(unsigned algorithm_iv, unsigned poly, unsigned w)
 *  [
 *    int i;
 *    int doit;
 *    unsigned int current_val = algorithm_iv;
 *
 *    for(i = 0; i < w; i++) [
 *      doit = current_val & 0x1;
 *
 *      if(doit) current_val ^= poly;
 *      assert(!(current_val & 0x1));
 *
 *      current_val = (current_val >> 1) | (doit << (w-1));
 *    ]
 *
 *    return current_val;
 *  ]
 */
union cvmx_pip_crc_ivx {
	uint64_t u64;
	struct cvmx_pip_crc_ivx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t iv                           : 32; /**< IV used by the CRC algorithm.  Default is FCS32. */
#else
	uint64_t iv                           : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pip_crc_ivx_s             cn38xx;
	struct cvmx_pip_crc_ivx_s             cn38xxp2;
	struct cvmx_pip_crc_ivx_s             cn58xx;
	struct cvmx_pip_crc_ivx_s             cn58xxp1;
};
typedef union cvmx_pip_crc_ivx cvmx_pip_crc_ivx_t;

/**
 * cvmx_pip_dec_ipsec#
 *
 * PIP_DEC_IPSEC = UDP or TCP ports to watch for DEC IPSEC
 *
 * PIP sets the dec_ipsec based on TCP or UDP destination port.
 */
union cvmx_pip_dec_ipsecx {
	uint64_t u64;
	struct cvmx_pip_dec_ipsecx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_18_63               : 46;
	uint64_t tcp                          : 1;  /**< This DPRT should be used for TCP packets */
	uint64_t udp                          : 1;  /**< This DPRT should be used for UDP packets */
	uint64_t dprt                         : 16; /**< UDP or TCP destination port to match on */
#else
	uint64_t dprt                         : 16;
	uint64_t udp                          : 1;
	uint64_t tcp                          : 1;
	uint64_t reserved_18_63               : 46;
#endif
	} s;
	struct cvmx_pip_dec_ipsecx_s          cn30xx;
	struct cvmx_pip_dec_ipsecx_s          cn31xx;
	struct cvmx_pip_dec_ipsecx_s          cn38xx;
	struct cvmx_pip_dec_ipsecx_s          cn38xxp2;
	struct cvmx_pip_dec_ipsecx_s          cn50xx;
	struct cvmx_pip_dec_ipsecx_s          cn52xx;
	struct cvmx_pip_dec_ipsecx_s          cn52xxp1;
	struct cvmx_pip_dec_ipsecx_s          cn56xx;
	struct cvmx_pip_dec_ipsecx_s          cn56xxp1;
	struct cvmx_pip_dec_ipsecx_s          cn58xx;
	struct cvmx_pip_dec_ipsecx_s          cn58xxp1;
	struct cvmx_pip_dec_ipsecx_s          cn61xx;
	struct cvmx_pip_dec_ipsecx_s          cn63xx;
	struct cvmx_pip_dec_ipsecx_s          cn63xxp1;
	struct cvmx_pip_dec_ipsecx_s          cn66xx;
	struct cvmx_pip_dec_ipsecx_s          cn68xx;
	struct cvmx_pip_dec_ipsecx_s          cn68xxp1;
	struct cvmx_pip_dec_ipsecx_s          cnf71xx;
};
typedef union cvmx_pip_dec_ipsecx cvmx_pip_dec_ipsecx_t;

/**
 * cvmx_pip_dsa_src_grp
 */
union cvmx_pip_dsa_src_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_src_grp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t map15                        : 4;  /**< DSA Group Algorithm */
	uint64_t map14                        : 4;  /**< DSA Group Algorithm */
	uint64_t map13                        : 4;  /**< DSA Group Algorithm */
	uint64_t map12                        : 4;  /**< DSA Group Algorithm */
	uint64_t map11                        : 4;  /**< DSA Group Algorithm */
	uint64_t map10                        : 4;  /**< DSA Group Algorithm */
	uint64_t map9                         : 4;  /**< DSA Group Algorithm */
	uint64_t map8                         : 4;  /**< DSA Group Algorithm */
	uint64_t map7                         : 4;  /**< DSA Group Algorithm */
	uint64_t map6                         : 4;  /**< DSA Group Algorithm */
	uint64_t map5                         : 4;  /**< DSA Group Algorithm */
	uint64_t map4                         : 4;  /**< DSA Group Algorithm */
	uint64_t map3                         : 4;  /**< DSA Group Algorithm */
	uint64_t map2                         : 4;  /**< DSA Group Algorithm */
	uint64_t map1                         : 4;  /**< DSA Group Algorithm */
	uint64_t map0                         : 4;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
#else
	uint64_t map0                         : 4;
	uint64_t map1                         : 4;
	uint64_t map2                         : 4;
	uint64_t map3                         : 4;
	uint64_t map4                         : 4;
	uint64_t map5                         : 4;
	uint64_t map6                         : 4;
	uint64_t map7                         : 4;
	uint64_t map8                         : 4;
	uint64_t map9                         : 4;
	uint64_t map10                        : 4;
	uint64_t map11                        : 4;
	uint64_t map12                        : 4;
	uint64_t map13                        : 4;
	uint64_t map14                        : 4;
	uint64_t map15                        : 4;
#endif
	} s;
	struct cvmx_pip_dsa_src_grp_s         cn52xx;
	struct cvmx_pip_dsa_src_grp_s         cn52xxp1;
	struct cvmx_pip_dsa_src_grp_s         cn56xx;
	struct cvmx_pip_dsa_src_grp_s         cn61xx;
	struct cvmx_pip_dsa_src_grp_s         cn63xx;
	struct cvmx_pip_dsa_src_grp_s         cn63xxp1;
	struct cvmx_pip_dsa_src_grp_s         cn66xx;
	struct cvmx_pip_dsa_src_grp_s         cn68xx;
	struct cvmx_pip_dsa_src_grp_s         cn68xxp1;
	struct cvmx_pip_dsa_src_grp_s         cnf71xx;
};
typedef union cvmx_pip_dsa_src_grp cvmx_pip_dsa_src_grp_t;

/**
 * cvmx_pip_dsa_vid_grp
 */
union cvmx_pip_dsa_vid_grp {
	uint64_t u64;
	struct cvmx_pip_dsa_vid_grp_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t map15                        : 4;  /**< DSA Group Algorithm */
	uint64_t map14                        : 4;  /**< DSA Group Algorithm */
	uint64_t map13                        : 4;  /**< DSA Group Algorithm */
	uint64_t map12                        : 4;  /**< DSA Group Algorithm */
	uint64_t map11                        : 4;  /**< DSA Group Algorithm */
	uint64_t map10                        : 4;  /**< DSA Group Algorithm */
	uint64_t map9                         : 4;  /**< DSA Group Algorithm */
	uint64_t map8                         : 4;  /**< DSA Group Algorithm */
	uint64_t map7                         : 4;  /**< DSA Group Algorithm */
	uint64_t map6                         : 4;  /**< DSA Group Algorithm */
	uint64_t map5                         : 4;  /**< DSA Group Algorithm */
	uint64_t map4                         : 4;  /**< DSA Group Algorithm */
	uint64_t map3                         : 4;  /**< DSA Group Algorithm */
	uint64_t map2                         : 4;  /**< DSA Group Algorithm */
	uint64_t map1                         : 4;  /**< DSA Group Algorithm */
	uint64_t map0                         : 4;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
#else
	uint64_t map0                         : 4;
	uint64_t map1                         : 4;
	uint64_t map2                         : 4;
	uint64_t map3                         : 4;
	uint64_t map4                         : 4;
	uint64_t map5                         : 4;
	uint64_t map6                         : 4;
	uint64_t map7                         : 4;
	uint64_t map8                         : 4;
	uint64_t map9                         : 4;
	uint64_t map10                        : 4;
	uint64_t map11                        : 4;
	uint64_t map12                        : 4;
	uint64_t map13                        : 4;
	uint64_t map14                        : 4;
	uint64_t map15                        : 4;
#endif
	} s;
	struct cvmx_pip_dsa_vid_grp_s         cn52xx;
	struct cvmx_pip_dsa_vid_grp_s         cn52xxp1;
	struct cvmx_pip_dsa_vid_grp_s         cn56xx;
	struct cvmx_pip_dsa_vid_grp_s         cn61xx;
	struct cvmx_pip_dsa_vid_grp_s         cn63xx;
	struct cvmx_pip_dsa_vid_grp_s         cn63xxp1;
	struct cvmx_pip_dsa_vid_grp_s         cn66xx;
	struct cvmx_pip_dsa_vid_grp_s         cn68xx;
	struct cvmx_pip_dsa_vid_grp_s         cn68xxp1;
	struct cvmx_pip_dsa_vid_grp_s         cnf71xx;
};
typedef union cvmx_pip_dsa_vid_grp cvmx_pip_dsa_vid_grp_t;

/**
 * cvmx_pip_frm_len_chk#
 *
 * Notes:
 * PIP_FRM_LEN_CHK0 is used for packets on packet interface0, PCI, PCI RAW, and PKO loopback ports.
 * PIP_FRM_LEN_CHK1 is unused.
 */
union cvmx_pip_frm_len_chkx {
	uint64_t u64;
	struct cvmx_pip_frm_len_chkx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t maxlen                       : 16; /**< Byte count for Max-sized frame check
                                                         PIP_PRT_CFGn[MAXERR_EN] enables the check for
                                                         port n.
                                                         If enabled, failing packets set the MAXERR
                                                         interrupt and work-queue entry WORD2[opcode] is
                                                         set to OVER_FCS (0x3, if packet has bad FCS) or
                                                         OVER_ERR (0x4, if packet has good FCS).
                                                         The effective MAXLEN used by HW is
                                                         PIP_PRT_CFG[DSA_EN] == 0,
                                                          PIP_FRM_LEN_CHK[MAXLEN] + 4*VV + 4*VS
                                                         PIP_PRT_CFG[DSA_EN] == 1,
                                                          PIP_FRM_LEN_CHK[MAXLEN] + PIP_PRT_CFG[SKIP]+4*VS
                                                         If PTP_MODE, the 8B timestamp is prepended to the
                                                          packet.  MAXLEN should be increased by 8 to
                                                          compensate for the additional timestamp field. */
	uint64_t minlen                       : 16; /**< Byte count for Min-sized frame check
                                                         PIP_PRT_CFGn[MINERR_EN] enables the check for
                                                         port n.
                                                         If enabled, failing packets set the MINERR
                                                         interrupt and work-queue entry WORD2[opcode] is
                                                         set to UNDER_FCS (0x6, if packet has bad FCS) or
                                                         UNDER_ERR (0x8, if packet has good FCS).
                                                         If PTP_MODE, the 8B timestamp is prepended to the
                                                          packet.  MINLEN should be increased by 8 to
                                                          compensate for the additional timestamp field. */
#else
	uint64_t minlen                       : 16;
	uint64_t maxlen                       : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pip_frm_len_chkx_s        cn50xx;
	struct cvmx_pip_frm_len_chkx_s        cn52xx;
	struct cvmx_pip_frm_len_chkx_s        cn52xxp1;
	struct cvmx_pip_frm_len_chkx_s        cn56xx;
	struct cvmx_pip_frm_len_chkx_s        cn56xxp1;
	struct cvmx_pip_frm_len_chkx_s        cn61xx;
	struct cvmx_pip_frm_len_chkx_s        cn63xx;
	struct cvmx_pip_frm_len_chkx_s        cn63xxp1;
	struct cvmx_pip_frm_len_chkx_s        cn66xx;
	struct cvmx_pip_frm_len_chkx_s        cn68xx;
	struct cvmx_pip_frm_len_chkx_s        cn68xxp1;
	struct cvmx_pip_frm_len_chkx_s        cnf71xx;
};
typedef union cvmx_pip_frm_len_chkx cvmx_pip_frm_len_chkx_t;

/**
 * cvmx_pip_gbl_cfg
 *
 * PIP_GBL_CFG = PIP's Global Config Register
 *
 * Global config information that applies to all ports.
 *
 * Notes:
 * * IP6_UDP
 * IPv4 allows optional UDP checksum by sending the all 0's patterns.  IPv6
 * outlaws this and the spec says to always check UDP checksum.  This mode
 * bit allows the user to treat IPv6 as IPv4, meaning that the all 0's
 * pattern will cause a UDP checksum pass.
 */
union cvmx_pip_gbl_cfg {
	uint64_t u64;
	struct cvmx_pip_gbl_cfg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_19_63               : 45;
	uint64_t tag_syn                      : 1;  /**< Do not include src_crc for TCP/SYN&!ACK packets
                                                         0 = include src_crc
                                                         1 = tag hash is dst_crc for TCP/SYN&!ACK packets */
	uint64_t ip6_udp                      : 1;  /**< IPv6/UDP checksum is not optional
                                                         0 = Allow optional checksum code
                                                         1 = Do not allow optional checksum code */
	uint64_t max_l2                       : 1;  /**< Config bit to choose the largest L2 frame size
                                                         Chooses the value of the L2 Type/Length field
                                                         to classify the frame as length.
                                                         0 = 1500 / 0x5dc
                                                         1 = 1535 / 0x5ff */
	uint64_t reserved_11_15               : 5;
	uint64_t raw_shf                      : 3;  /**< RAW Packet shift amount
                                                         Number of bytes to pad a RAW packet. */
	uint64_t reserved_3_7                 : 5;
	uint64_t nip_shf                      : 3;  /**< Non-IP shift amount
                                                         Number of bytes to pad a packet that has been
                                                         classified as not IP. */
#else
	uint64_t nip_shf                      : 3;
	uint64_t reserved_3_7                 : 5;
	uint64_t raw_shf                      : 3;
	uint64_t reserved_11_15               : 5;
	uint64_t max_l2                       : 1;
	uint64_t ip6_udp                      : 1;
	uint64_t tag_syn                      : 1;
	uint64_t reserved_19_63               : 45;
#endif
	} s;
	struct cvmx_pip_gbl_cfg_s             cn30xx;
	struct cvmx_pip_gbl_cfg_s             cn31xx;
	struct cvmx_pip_gbl_cfg_s             cn38xx;
	struct cvmx_pip_gbl_cfg_s             cn38xxp2;
	struct cvmx_pip_gbl_cfg_s             cn50xx;
	struct cvmx_pip_gbl_cfg_s             cn52xx;
	struct cvmx_pip_gbl_cfg_s             cn52xxp1;
	struct cvmx_pip_gbl_cfg_s             cn56xx;
	struct cvmx_pip_gbl_cfg_s             cn56xxp1;
	struct cvmx_pip_gbl_cfg_s             cn58xx;
	struct cvmx_pip_gbl_cfg_s             cn58xxp1;
	struct cvmx_pip_gbl_cfg_s             cn61xx;
	struct cvmx_pip_gbl_cfg_s             cn63xx;
	struct cvmx_pip_gbl_cfg_s             cn63xxp1;
	struct cvmx_pip_gbl_cfg_s             cn66xx;
	struct cvmx_pip_gbl_cfg_s             cn68xx;
	struct cvmx_pip_gbl_cfg_s             cn68xxp1;
	struct cvmx_pip_gbl_cfg_s             cnf71xx;
};
typedef union cvmx_pip_gbl_cfg cvmx_pip_gbl_cfg_t;

/**
 * cvmx_pip_gbl_ctl
 *
 * PIP_GBL_CTL = PIP's Global Control Register
 *
 * Global control information.  These are the global checker enables for
 * IPv4/IPv6 and TCP/UDP parsing.  The enables effect all ports.
 *
 * Notes:
 * The following text describes the conditions in which each checker will
 * assert and flag an exception.  By disabling the checker, the exception will
 * not be flagged and the packet will be parsed as best it can.  Note, by
 * disabling conditions, packets can be parsed incorrectly (.i.e. IP_MAL and
 * L4_MAL could cause bits to be seen in the wrong place.  IP_CHK and L4_CHK
 * means that the packet was corrupted).
 *
 * * IP_CHK
 *   Indicates that an IPv4 packet contained an IPv4 header checksum
 *   violations.  Only applies to packets classified as IPv4.
 *
 * * IP_MAL
 *   Indicates that the packet was malformed.  Malformed packets are defined as
 *   packets that are not long enough to cover the IP header or not long enough
 *   to cover the length in the IP header.
 *
 * * IP_HOP
 *   Indicates that the IPv4 TTL field or IPv6 HOP field is zero.
 *
 * * IP4_OPTS
 *   Indicates the presence of IPv4 options.  It is set when the length != 5.
 *   This only applies to packets classified as IPv4.
 *
 * * IP6_EEXT
 *   Indicate the presence of IPv6 early extension headers.  These bits only
 *   apply to packets classified as IPv6.  Bit 0 will flag early extensions
 *   when next_header is any one of the following...
 *
 *         - hop-by-hop (0)
 *         - destination (60)
 *         - routing (43)
 *
 *   Bit 1 will flag early extentions when next_header is NOT any of the
 *   following...
 *
 *         - TCP (6)
 *         - UDP (17)
 *         - fragmentation (44)
 *         - ICMP (58)
 *         - IPSEC ESP (50)
 *         - IPSEC AH (51)
 *         - IPCOMP
 *
 * * L4_MAL
 *   Indicates that a TCP or UDP packet is not long enough to cover the TCP or
 *   UDP header.
 *
 * * L4_PRT
 *   Indicates that a TCP or UDP packet has an illegal port number - either the
 *   source or destination port is zero.
 *
 * * L4_CHK
 *   Indicates that a packet classified as either TCP or UDP contains an L4
 *   checksum failure
 *
 * * L4_LEN
 *   Indicates that the TCP or UDP length does not match the the IP length.
 *
 * * TCP_FLAG
 *   Indicates any of the following conditions...
 *
 *         [URG, ACK, PSH, RST, SYN, FIN] : tcp_flag
 *         6'b000001: (FIN only)
 *         6'b000000: (0)
 *         6'bxxx1x1: (RST+FIN+*)
 *         6'b1xxx1x: (URG+SYN+*)
 *         6'bxxx11x: (RST+SYN+*)
 *         6'bxxxx11: (SYN+FIN+*)
 */
union cvmx_pip_gbl_ctl {
	uint64_t u64;
	struct cvmx_pip_gbl_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t egrp_dis                     : 1;  /**< PKT_INST_HDR extended group field disable
                                                         When set, HW will ignore the EGRP field of the
                                                         PKT_INST_HDR - bits 47:46. */
	uint64_t ihmsk_dis                    : 1;  /**< Instruction Header Mask Disable
                                                         0=Allow NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header to control which fields from
                                                           the instruction header are used for WQE WORD2.
                                                         1=Ignore the NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header and act as if these fields
                                                           were zero.  Thus always use the TAG,TT,GRP,QOS
                                                           (depending on the instruction header length)
                                                           from the instruction header for the WQE WORD2. */
	uint64_t dsa_grp_tvid                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
	uint64_t dsa_grp_scmd                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP when the
                                                         DSA tag command to TO_CPU */
	uint64_t dsa_grp_sid                  : 1;  /**< DSA Group Algorithm
                                                         Use the DSA VLAN id to compute GRP */
	uint64_t reserved_21_23               : 3;
	uint64_t ring_en                      : 1;  /**< Enable DPI ring information in WQE */
	uint64_t reserved_17_19               : 3;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         Does not apply to DPI ports (32-35)
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which DSA/VLAN CFI/ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which DSA/VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t ring_en                      : 1;
	uint64_t reserved_21_23               : 3;
	uint64_t dsa_grp_sid                  : 1;
	uint64_t dsa_grp_scmd                 : 1;
	uint64_t dsa_grp_tvid                 : 1;
	uint64_t ihmsk_dis                    : 1;
	uint64_t egrp_dis                     : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} s;
	struct cvmx_pip_gbl_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_17_63               : 47;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         Only applies to the packet interface prts (0-31)
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which VLAN CFI and ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_63               : 47;
#endif
	} cn30xx;
	struct cvmx_pip_gbl_ctl_cn30xx        cn31xx;
	struct cvmx_pip_gbl_ctl_cn30xx        cn38xx;
	struct cvmx_pip_gbl_ctl_cn30xx        cn38xxp2;
	struct cvmx_pip_gbl_ctl_cn30xx        cn50xx;
	struct cvmx_pip_gbl_ctl_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_27_63               : 37;
	uint64_t dsa_grp_tvid                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
	uint64_t dsa_grp_scmd                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP when the
                                                         DSA tag command to TO_CPU */
	uint64_t dsa_grp_sid                  : 1;  /**< DSA Group Algorithm
                                                         Use the DSA VLAN id to compute GRP */
	uint64_t reserved_21_23               : 3;
	uint64_t ring_en                      : 1;  /**< Enable PCIe ring information in WQE */
	uint64_t reserved_17_19               : 3;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         Does not apply to PCI ports (32-35)
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which DSA/VLAN CFI/ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which DSA/VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t ring_en                      : 1;
	uint64_t reserved_21_23               : 3;
	uint64_t dsa_grp_sid                  : 1;
	uint64_t dsa_grp_scmd                 : 1;
	uint64_t dsa_grp_tvid                 : 1;
	uint64_t reserved_27_63               : 37;
#endif
	} cn52xx;
	struct cvmx_pip_gbl_ctl_cn52xx        cn52xxp1;
	struct cvmx_pip_gbl_ctl_cn52xx        cn56xx;
	struct cvmx_pip_gbl_ctl_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_21_63               : 43;
	uint64_t ring_en                      : 1;  /**< Enable PCIe ring information in WQE */
	uint64_t reserved_17_19               : 3;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         Does not apply to PCI ports (32-35)
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which VLAN CFI and ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t ring_en                      : 1;
	uint64_t reserved_21_63               : 43;
#endif
	} cn56xxp1;
	struct cvmx_pip_gbl_ctl_cn30xx        cn58xx;
	struct cvmx_pip_gbl_ctl_cn30xx        cn58xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t ihmsk_dis                    : 1;  /**< Instruction Header Mask Disable
                                                         0=Allow NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header to control which fields from
                                                           the instruction header are used for WQE WORD2.
                                                         1=Ignore the NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header and act as if these fields
                                                           were zero.  Thus always use the TAG,TT,GRP,QOS
                                                           (depending on the instruction header length)
                                                           from the instruction header for the WQE WORD2. */
	uint64_t dsa_grp_tvid                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
	uint64_t dsa_grp_scmd                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP when the
                                                         DSA tag command to TO_CPU */
	uint64_t dsa_grp_sid                  : 1;  /**< DSA Group Algorithm
                                                         Use the DSA VLAN id to compute GRP */
	uint64_t reserved_21_23               : 3;
	uint64_t ring_en                      : 1;  /**< Enable DPI ring information in WQE */
	uint64_t reserved_17_19               : 3;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         Does not apply to DPI ports (32-35)
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which DSA/VLAN CFI/ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which DSA/VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_19               : 3;
	uint64_t ring_en                      : 1;
	uint64_t reserved_21_23               : 3;
	uint64_t dsa_grp_sid                  : 1;
	uint64_t dsa_grp_scmd                 : 1;
	uint64_t dsa_grp_tvid                 : 1;
	uint64_t ihmsk_dis                    : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn61xx;
	struct cvmx_pip_gbl_ctl_cn61xx        cn63xx;
	struct cvmx_pip_gbl_ctl_cn61xx        cn63xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx        cn66xx;
	struct cvmx_pip_gbl_ctl_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_29_63               : 35;
	uint64_t egrp_dis                     : 1;  /**< PKT_INST_HDR extended group field disable
                                                         When set, HW will ignore the EGRP field of the
                                                         PKT_INST_HDR - bits 47:46. */
	uint64_t ihmsk_dis                    : 1;  /**< Instruction Header Mask Disable
                                                         0=Allow NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header to control which fields from
                                                           the instruction header are used for WQE WORD2.
                                                         1=Ignore the NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header and act as if these fields
                                                           were zero.  Thus always use the TAG,TT,GRP,QOS
                                                           (depending on the instruction header length)
                                                           from the instruction header for the WQE WORD2. */
	uint64_t dsa_grp_tvid                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
	uint64_t dsa_grp_scmd                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP when the
                                                         DSA tag command to TO_CPU */
	uint64_t dsa_grp_sid                  : 1;  /**< DSA Group Algorithm
                                                         Use the DSA VLAN id to compute GRP */
	uint64_t reserved_17_23               : 7;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which DSA/VLAN CFI/ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which DSA/VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t dsa_grp_sid                  : 1;
	uint64_t dsa_grp_scmd                 : 1;
	uint64_t dsa_grp_tvid                 : 1;
	uint64_t ihmsk_dis                    : 1;
	uint64_t egrp_dis                     : 1;
	uint64_t reserved_29_63               : 35;
#endif
	} cn68xx;
	struct cvmx_pip_gbl_ctl_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_28_63               : 36;
	uint64_t ihmsk_dis                    : 1;  /**< Instruction Header Mask Disable
                                                         0=Allow NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header to control which fields from
                                                           the instruction header are used for WQE WORD2.
                                                         1=Ignore the NTAG,NTT,NGRP,NQOS bits in the
                                                           instruction header and act as if these fields
                                                           were zero.  Thus always use the TAG,TT,GRP,QOS
                                                           (depending on the instruction header length)
                                                           from the instruction header for the WQE WORD2. */
	uint64_t dsa_grp_tvid                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP */
	uint64_t dsa_grp_scmd                 : 1;  /**< DSA Group Algorithm
                                                         Use the DSA source id to compute GRP when the
                                                         DSA tag command to TO_CPU */
	uint64_t dsa_grp_sid                  : 1;  /**< DSA Group Algorithm
                                                         Use the DSA VLAN id to compute GRP */
	uint64_t reserved_17_23               : 7;
	uint64_t ignrs                        : 1;  /**< Ignore the PKT_INST_HDR[RS] bit when set
                                                         When using 2-byte instruction header words,
                                                         either PIP_PRT_CFG[DYN_RS] or IGNRS should be set */
	uint64_t vs_wqe                       : 1;  /**< Which DSA/VLAN CFI/ID to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t vs_qos                       : 1;  /**< Which DSA/VLAN priority to use when VLAN Stacking
                                                         0=use the 1st (network order) VLAN
                                                         1=use the 2nd (network order) VLAN */
	uint64_t l2_mal                       : 1;  /**< Enable L2 malformed packet check */
	uint64_t tcp_flag                     : 1;  /**< Enable TCP flags checks */
	uint64_t l4_len                       : 1;  /**< Enable TCP/UDP length check */
	uint64_t l4_chk                       : 1;  /**< Enable TCP/UDP checksum check */
	uint64_t l4_prt                       : 1;  /**< Enable TCP/UDP illegal port check */
	uint64_t l4_mal                       : 1;  /**< Enable TCP/UDP malformed packet check */
	uint64_t reserved_6_7                 : 2;
	uint64_t ip6_eext                     : 2;  /**< Enable IPv6 early extension headers */
	uint64_t ip4_opts                     : 1;  /**< Enable IPv4 options check */
	uint64_t ip_hop                       : 1;  /**< Enable TTL (IPv4) / hop (IPv6) check */
	uint64_t ip_mal                       : 1;  /**< Enable malformed check */
	uint64_t ip_chk                       : 1;  /**< Enable IPv4 header checksum check */
#else
	uint64_t ip_chk                       : 1;
	uint64_t ip_mal                       : 1;
	uint64_t ip_hop                       : 1;
	uint64_t ip4_opts                     : 1;
	uint64_t ip6_eext                     : 2;
	uint64_t reserved_6_7                 : 2;
	uint64_t l4_mal                       : 1;
	uint64_t l4_prt                       : 1;
	uint64_t l4_chk                       : 1;
	uint64_t l4_len                       : 1;
	uint64_t tcp_flag                     : 1;
	uint64_t l2_mal                       : 1;
	uint64_t vs_qos                       : 1;
	uint64_t vs_wqe                       : 1;
	uint64_t ignrs                        : 1;
	uint64_t reserved_17_23               : 7;
	uint64_t dsa_grp_sid                  : 1;
	uint64_t dsa_grp_scmd                 : 1;
	uint64_t dsa_grp_tvid                 : 1;
	uint64_t ihmsk_dis                    : 1;
	uint64_t reserved_28_63               : 36;
#endif
	} cn68xxp1;
	struct cvmx_pip_gbl_ctl_cn61xx        cnf71xx;
};
typedef union cvmx_pip_gbl_ctl cvmx_pip_gbl_ctl_t;

/**
 * cvmx_pip_hg_pri_qos
 *
 * Notes:
 * This register controls accesses to the HG_QOS_TABLE.  To write an entry of
 * the table, write PIP_HG_PRI_QOS with PRI=table address, QOS=priority level,
 * UP_QOS=1.  To read an entry of the table, write PIP_HG_PRI_QOS with
 * PRI=table address, QOS=dont_carepriority level, UP_QOS=0 and then read
 * PIP_HG_PRI_QOS.  The table data will be in PIP_HG_PRI_QOS[QOS].
 */
union cvmx_pip_hg_pri_qos {
	uint64_t u64;
	struct cvmx_pip_hg_pri_qos_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t up_qos                       : 1;  /**< When written to '1', updates the entry in the
                                                         HG_QOS_TABLE as specified by PRI to a value of
                                                         QOS as follows
                                                         HG_QOS_TABLE[PRI] = QOS */
	uint64_t reserved_11_11               : 1;
	uint64_t qos                          : 3;  /**< QOS Map level to priority */
	uint64_t reserved_6_7                 : 2;
	uint64_t pri                          : 6;  /**< The priority level from HiGig header
                                                         HiGig/HiGig+ PRI = [1'b0, CNG[1:0], COS[2:0]]
                                                         HiGig2       PRI = [DP[1:0], TC[3:0]] */
#else
	uint64_t pri                          : 6;
	uint64_t reserved_6_7                 : 2;
	uint64_t qos                          : 3;
	uint64_t reserved_11_11               : 1;
	uint64_t up_qos                       : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pip_hg_pri_qos_s          cn52xx;
	struct cvmx_pip_hg_pri_qos_s          cn52xxp1;
	struct cvmx_pip_hg_pri_qos_s          cn56xx;
	struct cvmx_pip_hg_pri_qos_s          cn61xx;
	struct cvmx_pip_hg_pri_qos_s          cn63xx;
	struct cvmx_pip_hg_pri_qos_s          cn63xxp1;
	struct cvmx_pip_hg_pri_qos_s          cn66xx;
	struct cvmx_pip_hg_pri_qos_s          cnf71xx;
};
typedef union cvmx_pip_hg_pri_qos cvmx_pip_hg_pri_qos_t;

/**
 * cvmx_pip_int_en
 *
 * PIP_INT_EN = PIP's Interrupt Enable Register
 *
 * Determines if hardward should raise an interrupt to software
 * when an exception event occurs.
 */
union cvmx_pip_int_en {
	uint64_t u64;
	struct cvmx_pip_int_en_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pip_int_en_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow
                                                         (not used in O2P) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure
                                                         (not used in O2P) */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC
                                                         (not used in O2P) */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn30xx;
	struct cvmx_pip_int_en_cn30xx         cn31xx;
	struct cvmx_pip_int_en_cn30xx         cn38xx;
	struct cvmx_pip_int_en_cn30xx         cn38xxp2;
	struct cvmx_pip_int_en_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t reserved_1_1                 : 1;
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn50xx;
	struct cvmx_pip_int_en_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t reserved_1_1                 : 1;
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn52xx;
	struct cvmx_pip_int_en_cn52xx         cn52xxp1;
	struct cvmx_pip_int_en_s              cn56xx;
	struct cvmx_pip_int_en_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC
                                                         (Disabled in 56xx) */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn56xxp1;
	struct cvmx_pip_int_en_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t reserved_9_11                : 3;
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn58xx;
	struct cvmx_pip_int_en_cn30xx         cn58xxp1;
	struct cvmx_pip_int_en_s              cn61xx;
	struct cvmx_pip_int_en_s              cn63xx;
	struct cvmx_pip_int_en_s              cn63xxp1;
	struct cvmx_pip_int_en_s              cn66xx;
	struct cvmx_pip_int_en_s              cn68xx;
	struct cvmx_pip_int_en_s              cn68xxp1;
	struct cvmx_pip_int_en_s              cnf71xx;
};
typedef union cvmx_pip_int_en cvmx_pip_int_en_t;

/**
 * cvmx_pip_int_reg
 *
 * PIP_INT_REG = PIP's Interrupt Register
 *
 * Any exception event that occurs is captured in the PIP_INT_REG.
 * PIP_INT_REG will set the exception bit regardless of the value
 * of PIP_INT_EN.  PIP_INT_EN only controls if an interrupt is
 * raised to software.
 *
 * Notes:
 * * TODOOVR
 *   The PIP Todo list stores packets that have been received and require work
 *   queue entry generation.  PIP will normally assert backpressure when the
 *   list fills up such that any error is normally is result of a programming
 *   the PIP_BCK_PRS[HIWATER] incorrectly.  PIP itself can handle 29M
 *   packets/sec X500MHz or 15Gbs X 64B packets.
 *
 * * SKPRUNT
 *   If a packet size is less then the amount programmed in the per port
 *   skippers, then there will be nothing to parse and the entire packet will
 *   basically be skipped over.  This is probably not what the user desired, so
 *   there is an indication to software.
 *
 * * BADTAG
 *   A tag is considered bad when it is resued by a new packet before it was
 *   released by PIP.  PIP considers a tag released by one of two methods.
 *   . QOS dropped so that it is released over the pip__ipd_release bus.
 *   . WorkQ entry is validated by the pip__ipd_done signal
 *
 * * PRTNXA
 *   If PIP receives a packet that is not in the valid port range, the port
 *   processed will be mapped into the valid port space (the mapping is
 *   currently unpredictable) and the PRTNXA bit will be set.  PRTNXA will be
 *   set for packets received under the following conditions:
 *
 *   * packet ports (ports 0-31)
 *     - GMX_INF_MODE[TYPE]==0 (SGMII), received port is 4-15 or 20-31
 *     - GMX_INF_MODE[TYPE]==1 (XAUI),  received port is 1-15 or 17-31
 *   * upper ports (pci and loopback ports 32-63)
 *     - received port is 40-47 or 52-63
 *
 * * BCKPRS
 *   PIP can assert backpressure to the receive logic when the todo list
 *   exceeds a high-water mark (see PIP_BCK_PRS for more details).  When this
 *   occurs, PIP can raise an interrupt to software.
 *
 * * CRCERR
 *   Octane can compute CRC in two places.  Each RGMII port will compute its
 *   own CRC, but PIP can provide an additional check or check loopback or
 *   PCI ports. If PIP computes a bad CRC, then PIP will raise an interrupt.
 *
 * * PKTDRP
 *   PIP can drop packets based on QOS results received from IPD.  If the QOS
 *   algorithm decides to drop a packet, PIP will assert an interrupt.
 */
union cvmx_pip_int_reg {
	uint64_t u64;
	struct cvmx_pip_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} s;
	struct cvmx_pip_int_reg_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow
                                                         (not used in O2P) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure
                                                         (not used in O2P) */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC
                                                         (not used in O2P) */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} cn30xx;
	struct cvmx_pip_int_reg_cn30xx        cn31xx;
	struct cvmx_pip_int_reg_cn30xx        cn38xx;
	struct cvmx_pip_int_reg_cn30xx        cn38xxp2;
	struct cvmx_pip_int_reg_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t reserved_1_1                 : 1;
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn50xx;
	struct cvmx_pip_int_reg_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t reserved_1_1                 : 1;
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t reserved_1_1                 : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn52xx;
	struct cvmx_pip_int_reg_cn52xx        cn52xxp1;
	struct cvmx_pip_int_reg_s             cn56xx;
	struct cvmx_pip_int_reg_cn56xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_12_63               : 52;
	uint64_t lenerr                       : 1;  /**< Frame was received with length error */
	uint64_t maxerr                       : 1;  /**< Frame was received with length > max_length */
	uint64_t minerr                       : 1;  /**< Frame was received with length < min_length */
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC
                                                         (Disabled in 56xx) */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t minerr                       : 1;
	uint64_t maxerr                       : 1;
	uint64_t lenerr                       : 1;
	uint64_t reserved_12_63               : 52;
#endif
	} cn56xxp1;
	struct cvmx_pip_int_reg_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_13_63               : 51;
	uint64_t punyerr                      : 1;  /**< Frame was received with length <=4B when CRC
                                                         stripping in IPD is enable */
	uint64_t reserved_9_11                : 3;
	uint64_t beperr                       : 1;  /**< Parity Error in back end memory */
	uint64_t feperr                       : 1;  /**< Parity Error in front end memory */
	uint64_t todoovr                      : 1;  /**< Todo list overflow (see PIP_BCK_PRS[HIWATER]) */
	uint64_t skprunt                      : 1;  /**< Packet was engulfed by skipper
                                                         This interrupt can occur with received PARTIAL
                                                         packets that are truncated to SKIP bytes or
                                                         smaller. */
	uint64_t badtag                       : 1;  /**< A bad tag was sent from IPD */
	uint64_t prtnxa                       : 1;  /**< Non-existent port */
	uint64_t bckprs                       : 1;  /**< PIP asserted backpressure */
	uint64_t crcerr                       : 1;  /**< PIP calculated bad CRC */
	uint64_t pktdrp                       : 1;  /**< Packet Dropped due to QOS */
#else
	uint64_t pktdrp                       : 1;
	uint64_t crcerr                       : 1;
	uint64_t bckprs                       : 1;
	uint64_t prtnxa                       : 1;
	uint64_t badtag                       : 1;
	uint64_t skprunt                      : 1;
	uint64_t todoovr                      : 1;
	uint64_t feperr                       : 1;
	uint64_t beperr                       : 1;
	uint64_t reserved_9_11                : 3;
	uint64_t punyerr                      : 1;
	uint64_t reserved_13_63               : 51;
#endif
	} cn58xx;
	struct cvmx_pip_int_reg_cn30xx        cn58xxp1;
	struct cvmx_pip_int_reg_s             cn61xx;
	struct cvmx_pip_int_reg_s             cn63xx;
	struct cvmx_pip_int_reg_s             cn63xxp1;
	struct cvmx_pip_int_reg_s             cn66xx;
	struct cvmx_pip_int_reg_s             cn68xx;
	struct cvmx_pip_int_reg_s             cn68xxp1;
	struct cvmx_pip_int_reg_s             cnf71xx;
};
typedef union cvmx_pip_int_reg cvmx_pip_int_reg_t;

/**
 * cvmx_pip_ip_offset
 *
 * PIP_IP_OFFSET = Location of the IP in the workQ entry
 *
 * An 8-byte offset to find the start of the IP header in the data portion of IP workQ entires
 *
 * Notes:
 * In normal configurations, OFFSET must be set in the 0..4 range to allow the
 * entire IP and TCP/UDP headers to be buffered in HW and calculate the L4
 * checksum for TCP/UDP packets.
 *
 * The MAX value of OFFSET is determined by the the types of packets that can
 * be sent to PIP as follows...
 *
 * Packet Type              MAX OFFSET
 * IPv4/TCP/UDP             7
 * IPv6/TCP/UDP             5
 * IPv6/without L4 parsing  6
 *
 * If the L4 can be ignored, then the MAX OFFSET for IPv6 packets can increase
 * to 6.  Here are the following programming restrictions for IPv6 packets and
 * OFFSET==6:
 *
 *  . PIP_GBL_CTL[TCP_FLAG] == 0
 *  . PIP_GBL_CTL[L4_LEN]   == 0
 *  . PIP_GBL_CTL[L4_CHK]   == 0
 *  . PIP_GBL_CTL[L4_PRT]   == 0
 *  . PIP_GBL_CTL[L4_MAL]   == 0
 *  . PIP_DEC_IPSEC[TCP]    == 0
 *  . PIP_DEC_IPSEC[UDP]    == 0
 *  . PIP_PRT_TAG[IP6_DPRT] == 0
 *  . PIP_PRT_TAG[IP6_SPRT] == 0
 *  . PIP_PRT_TAG[TCP6_TAG] == 0
 *  . PIP_GBL_CFG[TAG_SYN]  == 0
 */
union cvmx_pip_ip_offset {
	uint64_t u64;
	struct cvmx_pip_ip_offset_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t offset                       : 3;  /**< Number of 8B ticks to include in workQ entry
                                                          prior to IP data
                                                         - 0:  0 Bytes / IP start at WORD4 of workQ entry
                                                         - 1:  8 Bytes / IP start at WORD5 of workQ entry
                                                         - 2: 16 Bytes / IP start at WORD6 of workQ entry
                                                         - 3: 24 Bytes / IP start at WORD7 of workQ entry
                                                         - 4: 32 Bytes / IP start at WORD8 of workQ entry
                                                         - 5: 40 Bytes / IP start at WORD9 of workQ entry
                                                         - 6: 48 Bytes / IP start at WORD10 of workQ entry
                                                         - 7: 56 Bytes / IP start at WORD11 of workQ entry */
#else
	uint64_t offset                       : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_pip_ip_offset_s           cn30xx;
	struct cvmx_pip_ip_offset_s           cn31xx;
	struct cvmx_pip_ip_offset_s           cn38xx;
	struct cvmx_pip_ip_offset_s           cn38xxp2;
	struct cvmx_pip_ip_offset_s           cn50xx;
	struct cvmx_pip_ip_offset_s           cn52xx;
	struct cvmx_pip_ip_offset_s           cn52xxp1;
	struct cvmx_pip_ip_offset_s           cn56xx;
	struct cvmx_pip_ip_offset_s           cn56xxp1;
	struct cvmx_pip_ip_offset_s           cn58xx;
	struct cvmx_pip_ip_offset_s           cn58xxp1;
	struct cvmx_pip_ip_offset_s           cn61xx;
	struct cvmx_pip_ip_offset_s           cn63xx;
	struct cvmx_pip_ip_offset_s           cn63xxp1;
	struct cvmx_pip_ip_offset_s           cn66xx;
	struct cvmx_pip_ip_offset_s           cn68xx;
	struct cvmx_pip_ip_offset_s           cn68xxp1;
	struct cvmx_pip_ip_offset_s           cnf71xx;
};
typedef union cvmx_pip_ip_offset cvmx_pip_ip_offset_t;

/**
 * cvmx_pip_pri_tbl#
 *
 * Notes:
 * The priority level from HiGig header is as follows
 *
 * HiGig/HiGig+ PRI = [1'b0, CNG[1:0], COS[2:0]]
 * HiGig2       PRI = [DP[1:0], TC[3:0]]
 *
 * DSA          PRI = WORD0[15:13]
 *
 * VLAN         PRI = VLAN[15:13]
 *
 * DIFFSERV         = IP.TOS/CLASS<7:2>
 */
union cvmx_pip_pri_tblx {
	uint64_t u64;
	struct cvmx_pip_pri_tblx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t diff2_padd                   : 8;  /**< Diffserv port-add */
	uint64_t hg2_padd                     : 8;  /**< HG_PRI port-add */
	uint64_t vlan2_padd                   : 8;  /**< VLAN port-add */
	uint64_t reserved_38_39               : 2;
	uint64_t diff2_bpid                   : 6;  /**< Diffserv backpressure ID */
	uint64_t reserved_30_31               : 2;
	uint64_t hg2_bpid                     : 6;  /**< HG_PRI backpressure ID */
	uint64_t reserved_22_23               : 2;
	uint64_t vlan2_bpid                   : 6;  /**< VLAN backpressure ID */
	uint64_t reserved_11_15               : 5;
	uint64_t diff2_qos                    : 3;  /**< Diffserv QOS level */
	uint64_t reserved_7_7                 : 1;
	uint64_t hg2_qos                      : 3;  /**< HG_PRI QOS level */
	uint64_t reserved_3_3                 : 1;
	uint64_t vlan2_qos                    : 3;  /**< VLAN QOS level */
#else
	uint64_t vlan2_qos                    : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t hg2_qos                      : 3;
	uint64_t reserved_7_7                 : 1;
	uint64_t diff2_qos                    : 3;
	uint64_t reserved_11_15               : 5;
	uint64_t vlan2_bpid                   : 6;
	uint64_t reserved_22_23               : 2;
	uint64_t hg2_bpid                     : 6;
	uint64_t reserved_30_31               : 2;
	uint64_t diff2_bpid                   : 6;
	uint64_t reserved_38_39               : 2;
	uint64_t vlan2_padd                   : 8;
	uint64_t hg2_padd                     : 8;
	uint64_t diff2_padd                   : 8;
#endif
	} s;
	struct cvmx_pip_pri_tblx_s            cn68xx;
	struct cvmx_pip_pri_tblx_s            cn68xxp1;
};
typedef union cvmx_pip_pri_tblx cvmx_pip_pri_tblx_t;

/**
 * cvmx_pip_prt_cfg#
 *
 * PIP_PRT_CFGX = Per port config information
 *
 */
union cvmx_pip_prt_cfgx {
	uint64_t u64;
	struct cvmx_pip_prt_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_55_63               : 9;
	uint64_t ih_pri                       : 1;  /**< Use the PRI/QOS field in the instruction header
                                                         as the PRIORITY in BPID calculations. */
	uint64_t len_chk_sel                  : 1;  /**< Selects which PIP_FRM_LEN_CHK register is used
                                                         for this port-kind for MINERR and MAXERR checks.
                                                         LEN_CHK_SEL=0, use PIP_FRM_LEN_CHK0
                                                         LEN_CHK_SEL=1, use PIP_FRM_LEN_CHK1 */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for pkts with
                                                         padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for DSA/VLAN
                                                         pkts */
	uint64_t lenerr_en                    : 1;  /**< L2 length error check enable
                                                         Frame was received with length error
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the DPI and sRIO ports
                                                          because the CRC bytes may not normally be
                                                          present. */
	uint64_t maxerr_en                    : 1;  /**< Max frame error check enable
                                                         Frame was received with length > max_length
                                                         max_length is defined by PIP_FRM_LEN_CHK[MAXLEN] */
	uint64_t minerr_en                    : 1;  /**< Min frame error check enable
                                                         Frame was received with length < min_length
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the DPI and sRIO ports
                                                          because the CRC bytes may not normally be
                                                          present.
                                                          min_length is defined by PIP_FRM_LEN_CHK[MINLEN] */
	uint64_t grp_wat_47                   : 4;  /**< GRP Watcher enable
                                                         (Watchers 4-7) */
	uint64_t qos_wat_47                   : 4;  /**< QOS Watcher enable
                                                         (Watchers 4-7) */
	uint64_t reserved_37_39               : 3;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         Internally set for RAWFULL/RAWSCHED packets
                                                         on the DPI ports (32-35).
                                                         Internally cleared for all other packets on the
                                                         DPI ports (32-35).
                                                         Must be zero in DSA mode */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t hg_qos                       : 1;  /**< When set, uses the HiGig priority bits as a
                                                         lookup into the HG_QOS_TABLE (PIP_HG_PRI_QOS)
                                                         to determine the QOS value
                                                         HG_QOS must not be set when HIGIG_EN=0 */
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable
                                                         (Watchers 0-3) */
	uint64_t qos_vsel                     : 1;  /**< Which QOS in PIP_QOS_VLAN to use
                                                         0 = PIP_QOS_VLAN[QOS]
                                                         1 = PIP_QOS_VLAN[QOS1] */
	uint64_t qos_vod                      : 1;  /**< QOS VLAN over Diffserv
                                                         if DSA/VLAN exists, it is used
                                                         else if IP exists, Diffserv is used
                                                         else the per port default is used
                                                         Watchers are still highest priority */
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled */
	uint64_t higig_en                     : 1;  /**< Enable HiGig parsing
                                                         Should not be set for DPI ports (ports 32-35)
                                                         Should not be set for sRIO ports (ports 40-47)
                                                         Should not be set for ports in which PTP_MODE=1
                                                         When HIGIG_EN=1:
                                                          DSA_EN field below must be zero
                                                          PIP_PRT_CFGB[ALT_SKP_EN] must be zero.
                                                          SKIP field below is both Skip I size and the
                                                            size of the HiGig* header (12 or 16 bytes) */
	uint64_t dsa_en                       : 1;  /**< Enable DSA tag parsing
                                                         Should not be set for sRIO (ports 40-47)
                                                         Should not be set for ports in which PTP_MODE=1
                                                         When DSA_EN=1:
                                                          HIGIG_EN field above must be zero
                                                          SKIP field below is size of DSA tag (4, 8, or
                                                            12 bytes) rather than the size of Skip I
                                                          total SKIP (Skip I + header + Skip II
                                                            must be zero
                                                          INST_HDR field above must be zero (non-DPI
                                                            ports)
                                                          PIP_PRT_CFGB[ALT_SKP_EN] must be zero.
                                                          For DPI ports, SLI_PKT*_INSTR_HEADER[USE_IHDR]
                                                            and DPI_INST_HDR[R] should be clear
                                                          MODE field below must be "skip to L2" */
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = (illegal)
                                                         Must be 2 ("skip to L2") when in DSA mode. */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.
                                                         HW forces the SKIP to zero for packets on DPI
                                                         ports (32-35) when a PKT_INST_HDR is present.
                                                         See PIP_PRT_CFGB[ALT_SKP*] and PIP_ALT_SKIP_CFG.
                                                         See HRM sections "Parse Mode and Skip Length
                                                         Selection" and "Legal Skip Values"
                                                         for further details.
                                                         In DSA mode, indicates the DSA header length, not
                                                           Skip I size. (Must be 4,8,or 12)
                                                         In HIGIG mode, indicates both the Skip I size and
                                                           the HiGig header size (Must be 12 or 16).
                                                         If PTP_MODE, the 8B timestamp is prepended to the
                                                          packet.  SKIP should be increased by 8 to
                                                          compensate for the additional timestamp field. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t dsa_en                       : 1;
	uint64_t higig_en                     : 1;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t qos_vod                      : 1;
	uint64_t qos_vsel                     : 1;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t hg_qos                       : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t qos_wat_47                   : 4;
	uint64_t grp_wat_47                   : 4;
	uint64_t minerr_en                    : 1;
	uint64_t maxerr_en                    : 1;
	uint64_t lenerr_en                    : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t len_chk_sel                  : 1;
	uint64_t ih_pri                       : 1;
	uint64_t reserved_55_63               : 9;
#endif
	} s;
	struct cvmx_pip_prt_cfgx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         (not for PCI prts, 32-35) */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t reserved_27_27               : 1;
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable */
	uint64_t reserved_18_19               : 2;
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_10_15               : 6;
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = PCI Raw (illegal for software to set) */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.  Does not
                                                         apply to packets on PCI ports when a PKT_INST_HDR
                                                         is present.  See section 7.2.7 - Legal Skip
                                                         Values for further details. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t reserved_10_15               : 6;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t reserved_18_19               : 2;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t reserved_27_27               : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} cn30xx;
	struct cvmx_pip_prt_cfgx_cn30xx       cn31xx;
	struct cvmx_pip_prt_cfgx_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         (not for PCI prts, 32-35) */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t reserved_27_27               : 1;
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable */
	uint64_t reserved_18_19               : 2;
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled (for ports 0-31 only) */
	uint64_t reserved_10_11               : 2;
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = PCI Raw (illegal for software to set) */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.  Does not
                                                         apply to packets on PCI ports when a PKT_INST_HDR
                                                         is present.  See section 7.2.7 - Legal Skip
                                                         Values for further details. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t reserved_18_19               : 2;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t reserved_27_27               : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} cn38xx;
	struct cvmx_pip_prt_cfgx_cn38xx       cn38xxp2;
	struct cvmx_pip_prt_cfgx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_53_63               : 11;
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for pkts with
                                                         padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for VLAN pkts */
	uint64_t lenerr_en                    : 1;  /**< L2 length error check enable
                                                         Frame was received with length error */
	uint64_t maxerr_en                    : 1;  /**< Max frame error check enable
                                                         Frame was received with length > max_length */
	uint64_t minerr_en                    : 1;  /**< Min frame error check enable
                                                         Frame was received with length < min_length */
	uint64_t grp_wat_47                   : 4;  /**< GRP Watcher enable
                                                         (Watchers 4-7) */
	uint64_t qos_wat_47                   : 4;  /**< QOS Watcher enable
                                                         (Watchers 4-7) */
	uint64_t reserved_37_39               : 3;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         (not for PCI prts, 32-35) */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t reserved_27_27               : 1;
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable
                                                         (Watchers 0-3) */
	uint64_t reserved_19_19               : 1;
	uint64_t qos_vod                      : 1;  /**< QOS VLAN over Diffserv
                                                         if VLAN exists, it is used
                                                         else if IP exists, Diffserv is used
                                                         else the per port default is used
                                                         Watchers are still highest priority */
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled
                                                         (Disabled in 5020) */
	uint64_t reserved_10_11               : 2;
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = PCI Raw (illegal for software to set) */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.  Does not
                                                         apply to packets on PCI ports when a PKT_INST_HDR
                                                         is present.  See section 7.2.7 - Legal Skip
                                                         Values for further details. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t qos_vod                      : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t reserved_27_27               : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t qos_wat_47                   : 4;
	uint64_t grp_wat_47                   : 4;
	uint64_t minerr_en                    : 1;
	uint64_t maxerr_en                    : 1;
	uint64_t lenerr_en                    : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t reserved_53_63               : 11;
#endif
	} cn50xx;
	struct cvmx_pip_prt_cfgx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_53_63               : 11;
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for pkts with
                                                         padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for DSA/VLAN
                                                         pkts */
	uint64_t lenerr_en                    : 1;  /**< L2 length error check enable
                                                         Frame was received with length error
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the PCIe ports. */
	uint64_t maxerr_en                    : 1;  /**< Max frame error check enable
                                                         Frame was received with length > max_length */
	uint64_t minerr_en                    : 1;  /**< Min frame error check enable
                                                         Frame was received with length < min_length
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the PCIe ports. */
	uint64_t grp_wat_47                   : 4;  /**< GRP Watcher enable
                                                         (Watchers 4-7) */
	uint64_t qos_wat_47                   : 4;  /**< QOS Watcher enable
                                                         (Watchers 4-7) */
	uint64_t reserved_37_39               : 3;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         (not for PCI ports, 32-35)
                                                         Must be zero in DSA mode */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t hg_qos                       : 1;  /**< When set, uses the HiGig priority bits as a
                                                         lookup into the HG_QOS_TABLE (PIP_HG_PRI_QOS)
                                                         to determine the QOS value
                                                         HG_QOS must not be set when HIGIG_EN=0 */
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable
                                                         (Watchers 0-3) */
	uint64_t qos_vsel                     : 1;  /**< Which QOS in PIP_QOS_VLAN to use
                                                         0 = PIP_QOS_VLAN[QOS]
                                                         1 = PIP_QOS_VLAN[QOS1] */
	uint64_t qos_vod                      : 1;  /**< QOS VLAN over Diffserv
                                                         if DSA/VLAN exists, it is used
                                                         else if IP exists, Diffserv is used
                                                         else the per port default is used
                                                         Watchers are still highest priority */
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled
                                                         (Disabled in 52xx) */
	uint64_t higig_en                     : 1;  /**< Enable HiGig parsing
                                                         When HIGIG_EN=1:
                                                          DSA_EN field below must be zero
                                                          SKIP field below is both Skip I size and the
                                                            size of the HiGig* header (12 or 16 bytes) */
	uint64_t dsa_en                       : 1;  /**< Enable DSA tag parsing
                                                         When DSA_EN=1:
                                                          HIGIG_EN field above must be zero
                                                          SKIP field below is size of DSA tag (4, 8, or
                                                            12 bytes) rather than the size of Skip I
                                                          total SKIP (Skip I + header + Skip II
                                                            must be zero
                                                          INST_HDR field above must be zero
                                                          MODE field below must be "skip to L2" */
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = (illegal)
                                                         Must be 2 ("skip to L2") when in DSA mode. */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.
                                                         See section 7.2.7 - Legal Skip
                                                         Values for further details.
                                                         In DSA mode, indicates the DSA header length, not
                                                           Skip I size. (Must be 4,8,or 12)
                                                         In HIGIG mode, indicates both the Skip I size and
                                                           the HiGig header size (Must be 12 or 16). */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t dsa_en                       : 1;
	uint64_t higig_en                     : 1;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t qos_vod                      : 1;
	uint64_t qos_vsel                     : 1;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t hg_qos                       : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t qos_wat_47                   : 4;
	uint64_t grp_wat_47                   : 4;
	uint64_t minerr_en                    : 1;
	uint64_t maxerr_en                    : 1;
	uint64_t lenerr_en                    : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t reserved_53_63               : 11;
#endif
	} cn52xx;
	struct cvmx_pip_prt_cfgx_cn52xx       cn52xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx       cn56xx;
	struct cvmx_pip_prt_cfgx_cn50xx       cn56xxp1;
	struct cvmx_pip_prt_cfgx_cn58xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_37_63               : 27;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet that PIP
                                                         indicates is RAW.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         (not for PCI prts, 32-35) */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t reserved_27_27               : 1;
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable */
	uint64_t reserved_19_19               : 1;
	uint64_t qos_vod                      : 1;  /**< QOS VLAN over Diffserv
                                                         if VLAN exists, it is used
                                                         else if IP exists, Diffserv is used
                                                         else the per port default is used
                                                         Watchers are still highest priority */
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled (for ports 0-31 only) */
	uint64_t reserved_10_11               : 2;
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = PCI Raw (illegal for software to set) */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.  Does not
                                                         apply to packets on PCI ports when a PKT_INST_HDR
                                                         is present.  See section 7.2.7 - Legal Skip
                                                         Values for further details. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t reserved_10_11               : 2;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t qos_vod                      : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t reserved_27_27               : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_63               : 27;
#endif
	} cn58xx;
	struct cvmx_pip_prt_cfgx_cn58xx       cn58xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx       cn61xx;
	struct cvmx_pip_prt_cfgx_cn52xx       cn63xx;
	struct cvmx_pip_prt_cfgx_cn52xx       cn63xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx       cn66xx;
	struct cvmx_pip_prt_cfgx_cn68xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_55_63               : 9;
	uint64_t ih_pri                       : 1;  /**< Use the PRI/QOS field in the instruction header
                                                         as the PRIORITY in BPID calculations. */
	uint64_t len_chk_sel                  : 1;  /**< Selects which PIP_FRM_LEN_CHK register is used
                                                         for this port-kind for MINERR and MAXERR checks.
                                                         LEN_CHK_SEL=0, use PIP_FRM_LEN_CHK0
                                                         LEN_CHK_SEL=1, use PIP_FRM_LEN_CHK1 */
	uint64_t pad_len                      : 1;  /**< When set, disables the length check for pkts with
                                                         padding in the client data */
	uint64_t vlan_len                     : 1;  /**< When set, disables the length check for DSA/VLAN
                                                         pkts */
	uint64_t lenerr_en                    : 1;  /**< L2 length error check enable
                                                         Frame was received with length error
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the DPI rings
                                                          because the CRC bytes may not normally be
                                                          present. */
	uint64_t maxerr_en                    : 1;  /**< Max frame error check enable
                                                         Frame was received with length > max_length
                                                         max_length is defined by PIP_FRM_LEN_CHK[MAXLEN] */
	uint64_t minerr_en                    : 1;  /**< Min frame error check enable
                                                         Frame was received with length < min_length
                                                          Typically, this check will not be enabled for
                                                          incoming packets on the DPI rings
                                                          because the CRC bytes may not normally be
                                                          present.
                                                          min_length is defined by PIP_FRM_LEN_CHK[MINLEN] */
	uint64_t grp_wat_47                   : 4;  /**< GRP Watcher enable
                                                         (Watchers 4-7) */
	uint64_t qos_wat_47                   : 4;  /**< QOS Watcher enable
                                                         (Watchers 4-7) */
	uint64_t reserved_37_39               : 3;
	uint64_t rawdrp                       : 1;  /**< Allow the IPD to RED drop a packet.
                                                         Normally, IPD will never drop a packet in which
                                                         PKT_INST_HDR[R] is set.
                                                         0=never drop RAW packets based on RED algorithm
                                                         1=allow RAW packet drops based on RED algorithm */
	uint64_t tag_inc                      : 2;  /**< Which of the 4 PIP_TAG_INC to use when
                                                         calculating mask tag hash */
	uint64_t dyn_rs                       : 1;  /**< Dynamically calculate RS based on pkt size and
                                                         configuration.  If DYN_RS is set then
                                                         PKT_INST_HDR[RS] is not used.  When using 2-byte
                                                         instruction header words, either DYN_RS or
                                                         PIP_GBL_CTL[IGNRS] should be set. */
	uint64_t inst_hdr                     : 1;  /**< 8-byte INST_HDR is present on all packets
                                                         Normally INST_HDR should be set for packets that
                                                         include a PKT_INST_HDR prepended by DPI hardware.
                                                         (If SLI_PORTx_PKIND[RPK_ENB]=0, for packets that
                                                         include a PKT_INST_HDR prepended by DPI,
                                                         PIP internally sets INST_HDR before using it.)
                                                         Must be zero in DSA mode */
	uint64_t grp_wat                      : 4;  /**< GRP Watcher enable */
	uint64_t hg_qos                       : 1;  /**< When set, uses the HiGig priority bits as a
                                                         lookup into the HG_QOS_TABLE (PIP_HG_PRI_QOS)
                                                         to determine the QOS value
                                                         HG_QOS must not be set when HIGIG_EN=0 */
	uint64_t qos                          : 3;  /**< Default QOS level of the port */
	uint64_t qos_wat                      : 4;  /**< QOS Watcher enable
                                                         (Watchers 0-3) */
	uint64_t reserved_19_19               : 1;
	uint64_t qos_vod                      : 1;  /**< QOS VLAN over Diffserv
                                                         if DSA/VLAN exists, it is used
                                                         else if IP exists, Diffserv is used
                                                         else the per port default is used
                                                         Watchers are still highest priority */
	uint64_t qos_diff                     : 1;  /**< QOS Diffserv */
	uint64_t qos_vlan                     : 1;  /**< QOS VLAN */
	uint64_t reserved_13_15               : 3;
	uint64_t crc_en                       : 1;  /**< CRC Checking enabled */
	uint64_t higig_en                     : 1;  /**< Enable HiGig parsing
                                                         Normally HIGIG_EN should be clear for packets that
                                                         include a PKT_INST_HDR prepended by DPI hardware.
                                                         (If SLI_PORTx_PKIND[RPK_ENB]=0, for packets that
                                                         include a PKT_INST_HDR prepended by DPI,
                                                         PIP internally clears HIGIG_EN before using it.)
                                                         Should not be set for ports in which PTP_MODE=1
                                                         When HIGIG_EN=1:
                                                          DSA_EN field below must be zero
                                                          PIP_PRT_CFGB[ALT_SKP_EN] must be zero.
                                                          SKIP field below is both Skip I size and the
                                                            size of the HiGig* header (12 or 16 bytes) */
	uint64_t dsa_en                       : 1;  /**< Enable DSA tag parsing
                                                         Should not be set for ports in which PTP_MODE=1
                                                         When DSA_EN=1:
                                                          HIGIG_EN field above must be zero
                                                          SKIP field below is size of DSA tag (4, 8, or
                                                            12 bytes) rather than the size of Skip I
                                                          total SKIP (Skip I + header + Skip II
                                                            must be zero
                                                          INST_HDR field above must be zero
                                                          PIP_PRT_CFGB[ALT_SKP_EN] must be zero.
                                                          For DPI rings, DPI hardware must not prepend
                                                          a PKT_INST_HDR when DSA_EN=1.
                                                          MODE field below must be "skip to L2" */
	cvmx_pip_port_parse_mode_t mode       : 2;  /**< Parse Mode
                                                         0 = no packet inspection (Uninterpreted)
                                                         1 = L2 parsing / skip to L2
                                                         2 = IP parsing / skip to L3
                                                         3 = (illegal)
                                                         Must be 2 ("skip to L2") when in DSA mode. */
	uint64_t reserved_7_7                 : 1;
	uint64_t skip                         : 7;  /**< Optional Skip I amount for packets.
                                                         Should normally be zero for  packets on
                                                         DPI rings when a PKT_INST_HDR is prepended by DPI
                                                         hardware.
                                                         See PIP_PRT_CFGB[ALT_SKP*] and PIP_ALT_SKIP_CFG.
                                                         See HRM sections "Parse Mode and Skip Length
                                                         Selection" and "Legal Skip Values"
                                                         for further details.
                                                         In DSA mode, indicates the DSA header length, not
                                                           Skip I size. (Must be 4,8,or 12)
                                                         In HIGIG mode, indicates both the Skip I size and
                                                           the HiGig header size (Must be 12 or 16).
                                                         If PTP_MODE, the 8B timestamp is prepended to the
                                                          packet.  SKIP should be increased by 8 to
                                                          compensate for the additional timestamp field. */
#else
	uint64_t skip                         : 7;
	uint64_t reserved_7_7                 : 1;
	cvmx_pip_port_parse_mode_t mode       : 2;
	uint64_t dsa_en                       : 1;
	uint64_t higig_en                     : 1;
	uint64_t crc_en                       : 1;
	uint64_t reserved_13_15               : 3;
	uint64_t qos_vlan                     : 1;
	uint64_t qos_diff                     : 1;
	uint64_t qos_vod                      : 1;
	uint64_t reserved_19_19               : 1;
	uint64_t qos_wat                      : 4;
	uint64_t qos                          : 3;
	uint64_t hg_qos                       : 1;
	uint64_t grp_wat                      : 4;
	uint64_t inst_hdr                     : 1;
	uint64_t dyn_rs                       : 1;
	uint64_t tag_inc                      : 2;
	uint64_t rawdrp                       : 1;
	uint64_t reserved_37_39               : 3;
	uint64_t qos_wat_47                   : 4;
	uint64_t grp_wat_47                   : 4;
	uint64_t minerr_en                    : 1;
	uint64_t maxerr_en                    : 1;
	uint64_t lenerr_en                    : 1;
	uint64_t vlan_len                     : 1;
	uint64_t pad_len                      : 1;
	uint64_t len_chk_sel                  : 1;
	uint64_t ih_pri                       : 1;
	uint64_t reserved_55_63               : 9;
#endif
	} cn68xx;
	struct cvmx_pip_prt_cfgx_cn68xx       cn68xxp1;
	struct cvmx_pip_prt_cfgx_cn52xx       cnf71xx;
};
typedef union cvmx_pip_prt_cfgx cvmx_pip_prt_cfgx_t;

/**
 * cvmx_pip_prt_cfgb#
 *
 * Notes:
 * PIP_PRT_CFGB* does not exist prior to pass 1.2.
 *
 */
union cvmx_pip_prt_cfgbx {
	uint64_t u64;
	struct cvmx_pip_prt_cfgbx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t alt_skp_sel                  : 2;  /**< Alternate skip selector
                                                         When enabled (ALT_SKP_EN), selects which of the
                                                         four PIP_ALT_SKIP_CFGx to use with the packets
                                                         arriving on the port-kind. */
	uint64_t alt_skp_en                   : 1;  /**< Enable the alternate skip selector
                                                         When enabled, the HW is able to recompute the
                                                         SKIP I value based on the packet contents.
                                                         Up to two of the initial 64 bits of the header
                                                         are used along with four PIP_ALT_SKIP_CFGx to
                                                         determine the updated SKIP I value.
                                                         The bits of the packet used should be present in
                                                         all packets.
                                                         PIP_PRT_CFG[DSA_EN,HIGIG_EN] must be disabled
                                                         when ALT_SKP_EN is set.
                                                         ALT_SKP_EN must not be set for DPI ports (32-35)
                                                         when a PKT_INST_HDR is present.
                                                         ALT_SKP_EN should not be enabled for ports which
                                                         have GMX_RX_FRM_CTL[PTP_MODE] set as the timestamp
                                                         will be prepended onto the initial 64 bits of the
                                                         packet. */
	uint64_t reserved_35_35               : 1;
	uint64_t bsel_num                     : 2;  /**< Which of the 4 bit select extractors to use
                                                         (Alias to PIP_PRT_CFG) */
	uint64_t bsel_en                      : 1;  /**< Enable to turn on/off use of bit select extractor
                                                         (Alias to PIP_PRT_CFG) */
	uint64_t reserved_24_31               : 8;
	uint64_t base                         : 8;  /**< Base priority address into the table */
	uint64_t reserved_6_15                : 10;
	uint64_t bpid                         : 6;  /**< Default BPID to use for packets on this port-kind. */
#else
	uint64_t bpid                         : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t base                         : 8;
	uint64_t reserved_24_31               : 8;
	uint64_t bsel_en                      : 1;
	uint64_t bsel_num                     : 2;
	uint64_t reserved_35_35               : 1;
	uint64_t alt_skp_en                   : 1;
	uint64_t alt_skp_sel                  : 2;
	uint64_t reserved_39_63               : 25;
#endif
	} s;
	struct cvmx_pip_prt_cfgbx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t alt_skp_sel                  : 2;  /**< Alternate skip selector
                                                         When enabled (ALT_SKP_EN), selects which of the
                                                         four PIP_ALT_SKIP_CFGx to use with the packets
                                                         arriving on the port-kind. */
	uint64_t alt_skp_en                   : 1;  /**< Enable the alternate skip selector
                                                         When enabled, the HW is able to recompute the
                                                         SKIP I value based on the packet contents.
                                                         Up to two of the initial 64 bits of the header
                                                         are used along with four PIP_ALT_SKIP_CFGx to
                                                         determine the updated SKIP I value.
                                                         The bits of the packet used should be present in
                                                         all packets.
                                                         PIP_PRT_CFG[DSA_EN,HIGIG_EN] must be disabled
                                                         when ALT_SKP_EN is set.
                                                         ALT_SKP_EN must not be set for DPI ports (32-35)
                                                         when a PKT_INST_HDR is present.
                                                         ALT_SKP_EN should not be enabled for ports which
                                                         have GMX_RX_FRM_CTL[PTP_MODE] set as the timestamp
                                                         will be prepended onto the initial 64 bits of the
                                                         packet. */
	uint64_t reserved_35_35               : 1;
	uint64_t bsel_num                     : 2;  /**< Which of the 4 bit select extractors to use
                                                         (Alias to PIP_PRT_CFG) */
	uint64_t bsel_en                      : 1;  /**< Enable to turn on/off use of bit select extractor
                                                         (Alias to PIP_PRT_CFG) */
	uint64_t reserved_0_31                : 32;
#else
	uint64_t reserved_0_31                : 32;
	uint64_t bsel_en                      : 1;
	uint64_t bsel_num                     : 2;
	uint64_t reserved_35_35               : 1;
	uint64_t alt_skp_en                   : 1;
	uint64_t alt_skp_sel                  : 2;
	uint64_t reserved_39_63               : 25;
#endif
	} cn61xx;
	struct cvmx_pip_prt_cfgbx_cn66xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_39_63               : 25;
	uint64_t alt_skp_sel                  : 2;  /**< Alternate skip selector
                                                         When enabled (ALT_SKP_EN), selects which of the
                                                         four PIP_ALT_SKIP_CFGx to use with the packets
                                                         arriving on the port-kind. */
	uint64_t alt_skp_en                   : 1;  /**< Enable the alternate skip selector
                                                         When enabled, the HW is able to recompute the
                                                         SKIP I value based on the packet contents.
                                                         Up to two of the initial 64 bits of the header
                                                         are used along with four PIP_ALT_SKIP_CFGx to
                                                         determine the updated SKIP I value.
                                                         The bits of the packet used should be present in
                                                         all packets.
                                                         PIP_PRT_CFG[DSA_EN,HIGIG_EN] must be disabled
                                                         when ALT_SKP_EN is set.
                                                         ALT_SKP_EN must not be set for DPI ports (32-35)
                                                         when a PKT_INST_HDR is present. */
	uint64_t reserved_0_35                : 36;
#else
	uint64_t reserved_0_35                : 36;
	uint64_t alt_skp_en                   : 1;
	uint64_t alt_skp_sel                  : 2;
	uint64_t reserved_39_63               : 25;
#endif
	} cn66xx;
	struct cvmx_pip_prt_cfgbx_s           cn68xx;
	struct cvmx_pip_prt_cfgbx_cn68xxp1 {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_24_63               : 40;
	uint64_t base                         : 8;  /**< Base priority address into the table */
	uint64_t reserved_6_15                : 10;
	uint64_t bpid                         : 6;  /**< Default BPID to use for packets on this port-kind. */
#else
	uint64_t bpid                         : 6;
	uint64_t reserved_6_15                : 10;
	uint64_t base                         : 8;
	uint64_t reserved_24_63               : 40;
#endif
	} cn68xxp1;
	struct cvmx_pip_prt_cfgbx_cn61xx      cnf71xx;
};
typedef union cvmx_pip_prt_cfgbx cvmx_pip_prt_cfgbx_t;

/**
 * cvmx_pip_prt_tag#
 *
 * PIP_PRT_TAGX = Per port config information
 *
 */
union cvmx_pip_prt_tagx {
	uint64_t u64;
	struct cvmx_pip_prt_tagx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_54_63               : 10;
	uint64_t portadd_en                   : 1;  /**< Enables PIP to optionally increment the incoming
                                                         port from the MACs based on port-kind
                                                         configuration and packet contents. */
	uint64_t inc_hwchk                    : 1;  /**< Include the HW_checksum into WORD0 of the WQE
                                                         instead of the L4PTR.  This mode will be
                                                         deprecated in future products. */
	uint64_t reserved_50_51               : 2;
	uint64_t grptagbase_msb               : 2;  /**< Most significant 2 bits of the GRPTAGBASE value. */
	uint64_t reserved_46_47               : 2;
	uint64_t grptagmask_msb               : 2;  /**< Most significant 2 bits of the GRPTAGMASK value.
                                                         group when GRPTAG is set. */
	uint64_t reserved_42_43               : 2;
	uint64_t grp_msb                      : 2;  /**< Most significant 2 bits of the 6-bit value
                                                         indicating the group to schedule to. */
	uint64_t grptagbase                   : 4;  /**< Offset to use when computing group from tag bits
                                                         when GRPTAG is set. */
	uint64_t grptagmask                   : 4;  /**< Which bits of the tag to exclude when computing
                                                         group when GRPTAG is set. */
	uint64_t grptag                       : 1;  /**< When set, use the lower bit of the tag to compute
                                                         the group in the work queue entry
                                                         GRP = WQE[TAG[3:0]] & ~GRPTAGMASK + GRPTAGBASE */
	uint64_t grptag_mskip                 : 1;  /**< When set, GRPTAG will be used regardless if the
                                                         packet IS_IP. */
	uint64_t tag_mode                     : 2;  /**< Which tag algorithm to use
                                                         0 = always use tuple tag algorithm
                                                         1 = always use mask tag algorithm
                                                         2 = if packet is IP, use tuple else use mask
                                                         3 = tuple XOR mask */
	uint64_t inc_vs                       : 2;  /**< determines the DSA/VLAN ID (VID) to be included in
                                                         tuple tag when VLAN stacking is detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID (VLAN0) in hash
                                                         2 = include VID (VLAN1) in hash
                                                         3 = include VID ([VLAN0,VLAN1]) in hash */
	uint64_t inc_vlan                     : 1;  /**< when set, the DSA/VLAN ID is included in tuple tag
                                                         when VLAN stacking is not detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID in hash */
	uint64_t inc_prt_flag                 : 1;  /**< sets whether the port is included in tuple tag */
	uint64_t ip6_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_nxth_flag                : 1;  /**< sets whether ipv6 includes next header in tuple
                                                         tag hash */
	uint64_t ip4_pctl_flag                : 1;  /**< sets whether ipv4 includes protocol in tuple
                                                         tag hash */
	uint64_t ip6_dst_flag                 : 1;  /**< sets whether ipv6 includes dst address in tuple
                                                         tag hash */
	uint64_t ip4_dst_flag                 : 1;  /**< sets whether ipv4 includes dst address in tuple
                                                         tag hash */
	uint64_t ip6_src_flag                 : 1;  /**< sets whether ipv6 includes src address in tuple
                                                         tag hash */
	uint64_t ip4_src_flag                 : 1;  /**< sets whether ipv4 includes src address in tuple
                                                         tag hash */
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv6)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv4)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip6_tag_type      : 2;  /**< sets whether IPv6 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip4_tag_type      : 2;  /**< sets whether IPv4 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t non_tag_type      : 2;  /**< sets whether non-IP packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	uint64_t grp                          : 4;  /**< 4-bit value indicating the group to schedule to */
#else
	uint64_t grp                          : 4;
	cvmx_pow_tag_type_t non_tag_type      : 2;
	cvmx_pow_tag_type_t ip4_tag_type      : 2;
	cvmx_pow_tag_type_t ip6_tag_type      : 2;
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;
	uint64_t ip4_src_flag                 : 1;
	uint64_t ip6_src_flag                 : 1;
	uint64_t ip4_dst_flag                 : 1;
	uint64_t ip6_dst_flag                 : 1;
	uint64_t ip4_pctl_flag                : 1;
	uint64_t ip6_nxth_flag                : 1;
	uint64_t ip4_sprt_flag                : 1;
	uint64_t ip6_sprt_flag                : 1;
	uint64_t ip4_dprt_flag                : 1;
	uint64_t ip6_dprt_flag                : 1;
	uint64_t inc_prt_flag                 : 1;
	uint64_t inc_vlan                     : 1;
	uint64_t inc_vs                       : 2;
	uint64_t tag_mode                     : 2;
	uint64_t grptag_mskip                 : 1;
	uint64_t grptag                       : 1;
	uint64_t grptagmask                   : 4;
	uint64_t grptagbase                   : 4;
	uint64_t grp_msb                      : 2;
	uint64_t reserved_42_43               : 2;
	uint64_t grptagmask_msb               : 2;
	uint64_t reserved_46_47               : 2;
	uint64_t grptagbase_msb               : 2;
	uint64_t reserved_50_51               : 2;
	uint64_t inc_hwchk                    : 1;
	uint64_t portadd_en                   : 1;
	uint64_t reserved_54_63               : 10;
#endif
	} s;
	struct cvmx_pip_prt_tagx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t grptagbase                   : 4;  /**< Offset to use when computing group from tag bits
                                                         when GRPTAG is set. */
	uint64_t grptagmask                   : 4;  /**< Which bits of the tag to exclude when computing
                                                         group when GRPTAG is set. */
	uint64_t grptag                       : 1;  /**< When set, use the lower bit of the tag to compute
                                                         the group in the work queue entry
                                                         GRP = WQE[TAG[3:0]] & ~GRPTAGMASK + GRPTAGBASE */
	uint64_t reserved_30_30               : 1;
	uint64_t tag_mode                     : 2;  /**< Which tag algorithm to use
                                                         0 = always use tuple tag algorithm
                                                         1 = always use mask tag algorithm
                                                         2 = if packet is IP, use tuple else use mask
                                                         3 = tuple XOR mask */
	uint64_t inc_vs                       : 2;  /**< determines the VLAN ID (VID) to be included in
                                                         tuple tag when VLAN stacking is detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID (VLAN0) in hash
                                                         2 = include VID (VLAN1) in hash
                                                         3 = include VID ([VLAN0,VLAN1]) in hash */
	uint64_t inc_vlan                     : 1;  /**< when set, the VLAN ID is included in tuple tag
                                                         when VLAN stacking is not detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID in hash */
	uint64_t inc_prt_flag                 : 1;  /**< sets whether the port is included in tuple tag */
	uint64_t ip6_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_nxth_flag                : 1;  /**< sets whether ipv6 includes next header in tuple
                                                         tag hash */
	uint64_t ip4_pctl_flag                : 1;  /**< sets whether ipv4 includes protocol in tuple
                                                         tag hash */
	uint64_t ip6_dst_flag                 : 1;  /**< sets whether ipv6 includes dst address in tuple
                                                         tag hash */
	uint64_t ip4_dst_flag                 : 1;  /**< sets whether ipv4 includes dst address in tuple
                                                         tag hash */
	uint64_t ip6_src_flag                 : 1;  /**< sets whether ipv6 includes src address in tuple
                                                         tag hash */
	uint64_t ip4_src_flag                 : 1;  /**< sets whether ipv4 includes src address in tuple
                                                         tag hash */
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv6)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv4)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip6_tag_type      : 2;  /**< sets whether IPv6 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip4_tag_type      : 2;  /**< sets whether IPv4 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t non_tag_type      : 2;  /**< sets whether non-IP packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	uint64_t grp                          : 4;  /**< 4-bit value indicating the group to schedule to */
#else
	uint64_t grp                          : 4;
	cvmx_pow_tag_type_t non_tag_type      : 2;
	cvmx_pow_tag_type_t ip4_tag_type      : 2;
	cvmx_pow_tag_type_t ip6_tag_type      : 2;
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;
	uint64_t ip4_src_flag                 : 1;
	uint64_t ip6_src_flag                 : 1;
	uint64_t ip4_dst_flag                 : 1;
	uint64_t ip6_dst_flag                 : 1;
	uint64_t ip4_pctl_flag                : 1;
	uint64_t ip6_nxth_flag                : 1;
	uint64_t ip4_sprt_flag                : 1;
	uint64_t ip6_sprt_flag                : 1;
	uint64_t ip4_dprt_flag                : 1;
	uint64_t ip6_dprt_flag                : 1;
	uint64_t inc_prt_flag                 : 1;
	uint64_t inc_vlan                     : 1;
	uint64_t inc_vs                       : 2;
	uint64_t tag_mode                     : 2;
	uint64_t reserved_30_30               : 1;
	uint64_t grptag                       : 1;
	uint64_t grptagmask                   : 4;
	uint64_t grptagbase                   : 4;
	uint64_t reserved_40_63               : 24;
#endif
	} cn30xx;
	struct cvmx_pip_prt_tagx_cn30xx       cn31xx;
	struct cvmx_pip_prt_tagx_cn30xx       cn38xx;
	struct cvmx_pip_prt_tagx_cn30xx       cn38xxp2;
	struct cvmx_pip_prt_tagx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_40_63               : 24;
	uint64_t grptagbase                   : 4;  /**< Offset to use when computing group from tag bits
                                                         when GRPTAG is set. */
	uint64_t grptagmask                   : 4;  /**< Which bits of the tag to exclude when computing
                                                         group when GRPTAG is set. */
	uint64_t grptag                       : 1;  /**< When set, use the lower bit of the tag to compute
                                                         the group in the work queue entry
                                                         GRP = WQE[TAG[3:0]] & ~GRPTAGMASK + GRPTAGBASE */
	uint64_t grptag_mskip                 : 1;  /**< When set, GRPTAG will be used regardless if the
                                                         packet IS_IP. */
	uint64_t tag_mode                     : 2;  /**< Which tag algorithm to use
                                                         0 = always use tuple tag algorithm
                                                         1 = always use mask tag algorithm
                                                         2 = if packet is IP, use tuple else use mask
                                                         3 = tuple XOR mask */
	uint64_t inc_vs                       : 2;  /**< determines the VLAN ID (VID) to be included in
                                                         tuple tag when VLAN stacking is detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID (VLAN0) in hash
                                                         2 = include VID (VLAN1) in hash
                                                         3 = include VID ([VLAN0,VLAN1]) in hash */
	uint64_t inc_vlan                     : 1;  /**< when set, the VLAN ID is included in tuple tag
                                                         when VLAN stacking is not detected
                                                         0 = do not include VID in tuple tag generation
                                                         1 = include VID in hash */
	uint64_t inc_prt_flag                 : 1;  /**< sets whether the port is included in tuple tag */
	uint64_t ip6_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_dprt_flag                : 1;  /**< sets whether the TCP/UDP dst port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv6 packets */
	uint64_t ip4_sprt_flag                : 1;  /**< sets whether the TCP/UDP src port is
                                                         included in tuple tag for IPv4 */
	uint64_t ip6_nxth_flag                : 1;  /**< sets whether ipv6 includes next header in tuple
                                                         tag hash */
	uint64_t ip4_pctl_flag                : 1;  /**< sets whether ipv4 includes protocol in tuple
                                                         tag hash */
	uint64_t ip6_dst_flag                 : 1;  /**< sets whether ipv6 includes dst address in tuple
                                                         tag hash */
	uint64_t ip4_dst_flag                 : 1;  /**< sets whether ipv4 includes dst address in tuple
                                                         tag hash */
	uint64_t ip6_src_flag                 : 1;  /**< sets whether ipv6 includes src address in tuple
                                                         tag hash */
	uint64_t ip4_src_flag                 : 1;  /**< sets whether ipv4 includes src address in tuple
                                                         tag hash */
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv6)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;  /**< sets the tag_type of a TCP packet (IPv4)
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip6_tag_type      : 2;  /**< sets whether IPv6 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t ip4_tag_type      : 2;  /**< sets whether IPv4 packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	cvmx_pow_tag_type_t non_tag_type      : 2;  /**< sets whether non-IP packet tag type
                                                         0 = ordered tags
                                                         1 = atomic tags
                                                         2 = Null tags */
	uint64_t grp                          : 4;  /**< 4-bit value indicating the group to schedule to */
#else
	uint64_t grp                          : 4;
	cvmx_pow_tag_type_t non_tag_type      : 2;
	cvmx_pow_tag_type_t ip4_tag_type      : 2;
	cvmx_pow_tag_type_t ip6_tag_type      : 2;
	cvmx_pow_tag_type_t tcp4_tag_type     : 2;
	cvmx_pow_tag_type_t tcp6_tag_type     : 2;
	uint64_t ip4_src_flag                 : 1;
	uint64_t ip6_src_flag                 : 1;
	uint64_t ip4_dst_flag                 : 1;
	uint64_t ip6_dst_flag                 : 1;
	uint64_t ip4_pctl_flag                : 1;
	uint64_t ip6_nxth_flag                : 1;
	uint64_t ip4_sprt_flag                : 1;
	uint64_t ip6_sprt_flag                : 1;
	uint64_t ip4_dprt_flag                : 1;
	uint64_t ip6_dprt_flag                : 1;
	uint64_t inc_prt_flag                 : 1;
	uint64_t inc_vlan                     : 1;
	uint64_t inc_vs                       : 2;
	uint64_t tag_mode                     : 2;
	uint64_t grptag_mskip                 : 1;
	uint64_t grptag                       : 1;
	uint64_t grptagmask                   : 4;
	uint64_t grptagbase                   : 4;
	uint64_t reserved_40_63               : 24;
#endif
	} cn50xx;
	struct cvmx_pip_prt_tagx_cn50xx       cn52xx;
	struct cvmx_pip_prt_tagx_cn50xx       cn52xxp1;
	struct cvmx_pip_prt_tagx_cn50xx       cn56xx;
	struct cvmx_pip_prt_tagx_cn50xx       cn56xxp1;
	struct cvmx_pip_prt_tagx_cn30xx       cn58xx;
	struct cvmx_pip_prt_tagx_cn30xx       cn58xxp1;
	struct cvmx_pip_prt_tagx_cn50xx       cn61xx;
	struct cvmx_pip_prt_tagx_cn50xx       cn63xx;
	struct cvmx_pip_prt_tagx_cn50xx       cn63xxp1;
	struct cvmx_pip_prt_tagx_cn50xx       cn66xx;
	struct cvmx_pip_prt_tagx_s            cn68xx;
	struct cvmx_pip_prt_tagx_s            cn68xxp1;
	struct cvmx_pip_prt_tagx_cn50xx       cnf71xx;
};
typedef union cvmx_pip_prt_tagx cvmx_pip_prt_tagx_t;

/**
 * cvmx_pip_qos_diff#
 *
 * PIP_QOS_DIFFX = QOS Diffserv Tables
 *
 */
union cvmx_pip_qos_diffx {
	uint64_t u64;
	struct cvmx_pip_qos_diffx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t qos                          : 3;  /**< Diffserv QOS level */
#else
	uint64_t qos                          : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} s;
	struct cvmx_pip_qos_diffx_s           cn30xx;
	struct cvmx_pip_qos_diffx_s           cn31xx;
	struct cvmx_pip_qos_diffx_s           cn38xx;
	struct cvmx_pip_qos_diffx_s           cn38xxp2;
	struct cvmx_pip_qos_diffx_s           cn50xx;
	struct cvmx_pip_qos_diffx_s           cn52xx;
	struct cvmx_pip_qos_diffx_s           cn52xxp1;
	struct cvmx_pip_qos_diffx_s           cn56xx;
	struct cvmx_pip_qos_diffx_s           cn56xxp1;
	struct cvmx_pip_qos_diffx_s           cn58xx;
	struct cvmx_pip_qos_diffx_s           cn58xxp1;
	struct cvmx_pip_qos_diffx_s           cn61xx;
	struct cvmx_pip_qos_diffx_s           cn63xx;
	struct cvmx_pip_qos_diffx_s           cn63xxp1;
	struct cvmx_pip_qos_diffx_s           cn66xx;
	struct cvmx_pip_qos_diffx_s           cnf71xx;
};
typedef union cvmx_pip_qos_diffx cvmx_pip_qos_diffx_t;

/**
 * cvmx_pip_qos_vlan#
 *
 * PIP_QOS_VLANX = QOS VLAN Tables
 *
 * If the PIP indentifies a packet is DSA/VLAN tagged, then the QOS
 * can be set based on the DSA/VLAN user priority.  These eight register
 * comprise the QOS values for all DSA/VLAN user priority values.
 */
union cvmx_pip_qos_vlanx {
	uint64_t u64;
	struct cvmx_pip_qos_vlanx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_7_63                : 57;
	uint64_t qos1                         : 3;  /**< DSA/VLAN QOS level
                                                         Selected when PIP_PRT_CFGx[QOS_VSEL] = 1 */
	uint64_t reserved_3_3                 : 1;
	uint64_t qos                          : 3;  /**< DSA/VLAN QOS level
                                                         Selected when PIP_PRT_CFGx[QOS_VSEL] = 0 */
#else
	uint64_t qos                          : 3;
	uint64_t reserved_3_3                 : 1;
	uint64_t qos1                         : 3;
	uint64_t reserved_7_63                : 57;
#endif
	} s;
	struct cvmx_pip_qos_vlanx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_3_63                : 61;
	uint64_t qos                          : 3;  /**< VLAN QOS level */
#else
	uint64_t qos                          : 3;
	uint64_t reserved_3_63                : 61;
#endif
	} cn30xx;
	struct cvmx_pip_qos_vlanx_cn30xx      cn31xx;
	struct cvmx_pip_qos_vlanx_cn30xx      cn38xx;
	struct cvmx_pip_qos_vlanx_cn30xx      cn38xxp2;
	struct cvmx_pip_qos_vlanx_cn30xx      cn50xx;
	struct cvmx_pip_qos_vlanx_s           cn52xx;
	struct cvmx_pip_qos_vlanx_s           cn52xxp1;
	struct cvmx_pip_qos_vlanx_s           cn56xx;
	struct cvmx_pip_qos_vlanx_cn30xx      cn56xxp1;
	struct cvmx_pip_qos_vlanx_cn30xx      cn58xx;
	struct cvmx_pip_qos_vlanx_cn30xx      cn58xxp1;
	struct cvmx_pip_qos_vlanx_s           cn61xx;
	struct cvmx_pip_qos_vlanx_s           cn63xx;
	struct cvmx_pip_qos_vlanx_s           cn63xxp1;
	struct cvmx_pip_qos_vlanx_s           cn66xx;
	struct cvmx_pip_qos_vlanx_s           cnf71xx;
};
typedef union cvmx_pip_qos_vlanx cvmx_pip_qos_vlanx_t;

/**
 * cvmx_pip_qos_watch#
 *
 * PIP_QOS_WATCHX = QOS Watcher Tables
 *
 * Sets up the Configuration CSRs for the four QOS Watchers.
 * Each Watcher can be set to look for a specific protocol,
 * TCP/UDP destination port, or Ethertype to override the
 * default QOS value.
 */
union cvmx_pip_qos_watchx {
	uint64_t u64;
	struct cvmx_pip_qos_watchx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t mask                         : 16; /**< Mask off a range of values */
	uint64_t reserved_30_31               : 2;
	uint64_t grp                          : 6;  /**< The GRP number of the watcher */
	uint64_t reserved_23_23               : 1;
	uint64_t qos                          : 3;  /**< The QOS level of the watcher */
	uint64_t reserved_19_19               : 1;
	cvmx_pip_qos_watch_types match_type   : 3;  /**< The field for the watcher match against
                                                         0   = disable across all ports
                                                         1   = protocol (ipv4)
                                                             = next_header (ipv6)
                                                         2   = TCP destination port
                                                         3   = UDP destination port
                                                         4   = Ether type
                                                         5-7 = Reserved */
	uint64_t match_value                  : 16; /**< The value to watch for */
#else
	uint64_t match_value                  : 16;
	cvmx_pip_qos_watch_types match_type   : 3;
	uint64_t reserved_19_19               : 1;
	uint64_t qos                          : 3;
	uint64_t reserved_23_23               : 1;
	uint64_t grp                          : 6;
	uint64_t reserved_30_31               : 2;
	uint64_t mask                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_qos_watchx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t mask                         : 16; /**< Mask off a range of values */
	uint64_t reserved_28_31               : 4;
	uint64_t grp                          : 4;  /**< The GRP number of the watcher */
	uint64_t reserved_23_23               : 1;
	uint64_t qos                          : 3;  /**< The QOS level of the watcher */
	uint64_t reserved_18_19               : 2;
	cvmx_pip_qos_watch_types match_type   : 2;  /**< The field for the watcher match against
                                                         0 = disable across all ports
                                                         1 = protocol (ipv4)
                                                           = next_header (ipv6)
                                                         2 = TCP destination port
                                                         3 = UDP destination port */
	uint64_t match_value                  : 16; /**< The value to watch for */
#else
	uint64_t match_value                  : 16;
	cvmx_pip_qos_watch_types match_type   : 2;
	uint64_t reserved_18_19               : 2;
	uint64_t qos                          : 3;
	uint64_t reserved_23_23               : 1;
	uint64_t grp                          : 4;
	uint64_t reserved_28_31               : 4;
	uint64_t mask                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} cn30xx;
	struct cvmx_pip_qos_watchx_cn30xx     cn31xx;
	struct cvmx_pip_qos_watchx_cn30xx     cn38xx;
	struct cvmx_pip_qos_watchx_cn30xx     cn38xxp2;
	struct cvmx_pip_qos_watchx_cn50xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t mask                         : 16; /**< Mask off a range of values */
	uint64_t reserved_28_31               : 4;
	uint64_t grp                          : 4;  /**< The GRP number of the watcher */
	uint64_t reserved_23_23               : 1;
	uint64_t qos                          : 3;  /**< The QOS level of the watcher */
	uint64_t reserved_19_19               : 1;
	cvmx_pip_qos_watch_types match_type   : 3;  /**< The field for the watcher match against
                                                         0   = disable across all ports
                                                         1   = protocol (ipv4)
                                                             = next_header (ipv6)
                                                         2   = TCP destination port
                                                         3   = UDP destination port
                                                         4   = Ether type
                                                         5-7 = Reserved */
	uint64_t match_value                  : 16; /**< The value to watch for */
#else
	uint64_t match_value                  : 16;
	cvmx_pip_qos_watch_types match_type   : 3;
	uint64_t reserved_19_19               : 1;
	uint64_t qos                          : 3;
	uint64_t reserved_23_23               : 1;
	uint64_t grp                          : 4;
	uint64_t reserved_28_31               : 4;
	uint64_t mask                         : 16;
	uint64_t reserved_48_63               : 16;
#endif
	} cn50xx;
	struct cvmx_pip_qos_watchx_cn50xx     cn52xx;
	struct cvmx_pip_qos_watchx_cn50xx     cn52xxp1;
	struct cvmx_pip_qos_watchx_cn50xx     cn56xx;
	struct cvmx_pip_qos_watchx_cn50xx     cn56xxp1;
	struct cvmx_pip_qos_watchx_cn30xx     cn58xx;
	struct cvmx_pip_qos_watchx_cn30xx     cn58xxp1;
	struct cvmx_pip_qos_watchx_cn50xx     cn61xx;
	struct cvmx_pip_qos_watchx_cn50xx     cn63xx;
	struct cvmx_pip_qos_watchx_cn50xx     cn63xxp1;
	struct cvmx_pip_qos_watchx_cn50xx     cn66xx;
	struct cvmx_pip_qos_watchx_s          cn68xx;
	struct cvmx_pip_qos_watchx_s          cn68xxp1;
	struct cvmx_pip_qos_watchx_cn50xx     cnf71xx;
};
typedef union cvmx_pip_qos_watchx cvmx_pip_qos_watchx_t;

/**
 * cvmx_pip_raw_word
 *
 * PIP_RAW_WORD = The RAW Word2 of the workQ entry.
 *
 * The RAW Word2 to be inserted into the workQ entry of RAWFULL packets.
 */
union cvmx_pip_raw_word {
	uint64_t u64;
	struct cvmx_pip_raw_word_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_56_63               : 8;
	uint64_t word                         : 56; /**< Word2 of the workQ entry
                                                         The 8-bit bufs field is still set by HW (IPD) */
#else
	uint64_t word                         : 56;
	uint64_t reserved_56_63               : 8;
#endif
	} s;
	struct cvmx_pip_raw_word_s            cn30xx;
	struct cvmx_pip_raw_word_s            cn31xx;
	struct cvmx_pip_raw_word_s            cn38xx;
	struct cvmx_pip_raw_word_s            cn38xxp2;
	struct cvmx_pip_raw_word_s            cn50xx;
	struct cvmx_pip_raw_word_s            cn52xx;
	struct cvmx_pip_raw_word_s            cn52xxp1;
	struct cvmx_pip_raw_word_s            cn56xx;
	struct cvmx_pip_raw_word_s            cn56xxp1;
	struct cvmx_pip_raw_word_s            cn58xx;
	struct cvmx_pip_raw_word_s            cn58xxp1;
	struct cvmx_pip_raw_word_s            cn61xx;
	struct cvmx_pip_raw_word_s            cn63xx;
	struct cvmx_pip_raw_word_s            cn63xxp1;
	struct cvmx_pip_raw_word_s            cn66xx;
	struct cvmx_pip_raw_word_s            cn68xx;
	struct cvmx_pip_raw_word_s            cn68xxp1;
	struct cvmx_pip_raw_word_s            cnf71xx;
};
typedef union cvmx_pip_raw_word cvmx_pip_raw_word_t;

/**
 * cvmx_pip_sft_rst
 *
 * PIP_SFT_RST = PIP Soft Reset
 *
 * When written to a '1', resets the pip block
 *
 * Notes:
 * When RST is set to a '1' by SW, PIP will get a short reset pulse (3 cycles
 * in duration).  Although this will reset much of PIP's internal state, some
 * CSRs will not reset.
 *
 * . PIP_BIST_STATUS
 * . PIP_STAT0_PRT*
 * . PIP_STAT1_PRT*
 * . PIP_STAT2_PRT*
 * . PIP_STAT3_PRT*
 * . PIP_STAT4_PRT*
 * . PIP_STAT5_PRT*
 * . PIP_STAT6_PRT*
 * . PIP_STAT7_PRT*
 * . PIP_STAT8_PRT*
 * . PIP_STAT9_PRT*
 * . PIP_XSTAT0_PRT*
 * . PIP_XSTAT1_PRT*
 * . PIP_XSTAT2_PRT*
 * . PIP_XSTAT3_PRT*
 * . PIP_XSTAT4_PRT*
 * . PIP_XSTAT5_PRT*
 * . PIP_XSTAT6_PRT*
 * . PIP_XSTAT7_PRT*
 * . PIP_XSTAT8_PRT*
 * . PIP_XSTAT9_PRT*
 * . PIP_STAT_INB_PKTS*
 * . PIP_STAT_INB_OCTS*
 * . PIP_STAT_INB_ERRS*
 * . PIP_TAG_INC*
 */
union cvmx_pip_sft_rst {
	uint64_t u64;
	struct cvmx_pip_sft_rst_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rst                          : 1;  /**< Soft Reset */
#else
	uint64_t rst                          : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} s;
	struct cvmx_pip_sft_rst_s             cn30xx;
	struct cvmx_pip_sft_rst_s             cn31xx;
	struct cvmx_pip_sft_rst_s             cn38xx;
	struct cvmx_pip_sft_rst_s             cn50xx;
	struct cvmx_pip_sft_rst_s             cn52xx;
	struct cvmx_pip_sft_rst_s             cn52xxp1;
	struct cvmx_pip_sft_rst_s             cn56xx;
	struct cvmx_pip_sft_rst_s             cn56xxp1;
	struct cvmx_pip_sft_rst_s             cn58xx;
	struct cvmx_pip_sft_rst_s             cn58xxp1;
	struct cvmx_pip_sft_rst_s             cn61xx;
	struct cvmx_pip_sft_rst_s             cn63xx;
	struct cvmx_pip_sft_rst_s             cn63xxp1;
	struct cvmx_pip_sft_rst_s             cn66xx;
	struct cvmx_pip_sft_rst_s             cn68xx;
	struct cvmx_pip_sft_rst_s             cn68xxp1;
	struct cvmx_pip_sft_rst_s             cnf71xx;
};
typedef union cvmx_pip_sft_rst cvmx_pip_sft_rst_t;

/**
 * cvmx_pip_stat0_#
 *
 * PIP Statistics Counters
 *
 * Note: special stat counter behavior
 *
 * 1) Read and write operations must arbitrate for the statistics resources
 *     along with the packet engines which are incrementing the counters.
 *     In order to not drop packet information, the packet HW is always a
 *     higher priority and the CSR requests will only be satisified when
 *     there are idle cycles.  This can potentially cause long delays if the
 *     system becomes full.
 *
 * 2) stat counters can be cleared in two ways.  If PIP_STAT_CTL[RDCLR] is
 *     set, then all read accesses will clear the register.  In addition,
 *     any write to a stats register will also reset the register to zero.
 *     Please note that the clearing operations must obey rule \#1 above.
 *
 * 3) all counters are wrapping - software must ensure they are read periodically
 *
 * 4) The counters accumulate statistics for packets that are sent to PKI.  If
 *    PTP_MODE is enabled, the 8B timestamp is prepended to the packet.  This
 *    additional 8B of data is captured in the octet counts.
 *
 * 5) X represents either the packet's port-kind or backpressure ID as
 *    determined by PIP_STAT_CTL[MODE]
 * PIP_STAT0_X = PIP_STAT_DRP_PKTS / PIP_STAT_DRP_OCTS
 */
union cvmx_pip_stat0_x {
	uint64_t u64;
	struct cvmx_pip_stat0_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drp_pkts                     : 32; /**< Inbound packets marked to be dropped by the IPD
                                                         QOS widget per port */
	uint64_t drp_octs                     : 32; /**< Inbound octets marked to be dropped by the IPD
                                                         QOS widget per port */
#else
	uint64_t drp_octs                     : 32;
	uint64_t drp_pkts                     : 32;
#endif
	} s;
	struct cvmx_pip_stat0_x_s             cn68xx;
	struct cvmx_pip_stat0_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat0_x cvmx_pip_stat0_x_t;

/**
 * cvmx_pip_stat0_prt#
 *
 * PIP Statistics Counters
 *
 * Note: special stat counter behavior
 *
 * 1) Read and write operations must arbitrate for the statistics resources
 *     along with the packet engines which are incrementing the counters.
 *     In order to not drop packet information, the packet HW is always a
 *     higher priority and the CSR requests will only be satisified when
 *     there are idle cycles.  This can potentially cause long delays if the
 *     system becomes full.
 *
 * 2) stat counters can be cleared in two ways.  If PIP_STAT_CTL[RDCLR] is
 *     set, then all read accesses will clear the register.  In addition,
 *     any write to a stats register will also reset the register to zero.
 *     Please note that the clearing operations must obey rule \#1 above.
 *
 * 3) all counters are wrapping - software must ensure they are read periodically
 *
 * 4) The counters accumulate statistics for packets that are sent to PKI.  If
 *    PTP_MODE is enabled, the 8B timestamp is prepended to the packet.  This
 *    additional 8B of data is captured in the octet counts.
 * PIP_STAT0_PRT = PIP_STAT_DRP_PKTS / PIP_STAT_DRP_OCTS
 */
union cvmx_pip_stat0_prtx {
	uint64_t u64;
	struct cvmx_pip_stat0_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drp_pkts                     : 32; /**< Inbound packets marked to be dropped by the IPD
                                                         QOS widget per port */
	uint64_t drp_octs                     : 32; /**< Inbound octets marked to be dropped by the IPD
                                                         QOS widget per port */
#else
	uint64_t drp_octs                     : 32;
	uint64_t drp_pkts                     : 32;
#endif
	} s;
	struct cvmx_pip_stat0_prtx_s          cn30xx;
	struct cvmx_pip_stat0_prtx_s          cn31xx;
	struct cvmx_pip_stat0_prtx_s          cn38xx;
	struct cvmx_pip_stat0_prtx_s          cn38xxp2;
	struct cvmx_pip_stat0_prtx_s          cn50xx;
	struct cvmx_pip_stat0_prtx_s          cn52xx;
	struct cvmx_pip_stat0_prtx_s          cn52xxp1;
	struct cvmx_pip_stat0_prtx_s          cn56xx;
	struct cvmx_pip_stat0_prtx_s          cn56xxp1;
	struct cvmx_pip_stat0_prtx_s          cn58xx;
	struct cvmx_pip_stat0_prtx_s          cn58xxp1;
	struct cvmx_pip_stat0_prtx_s          cn61xx;
	struct cvmx_pip_stat0_prtx_s          cn63xx;
	struct cvmx_pip_stat0_prtx_s          cn63xxp1;
	struct cvmx_pip_stat0_prtx_s          cn66xx;
	struct cvmx_pip_stat0_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat0_prtx cvmx_pip_stat0_prtx_t;

/**
 * cvmx_pip_stat10_#
 *
 * PIP_STAT10_X = PIP_STAT_L2_MCAST / PIP_STAT_L2_BCAST
 *
 */
union cvmx_pip_stat10_x {
	uint64_t u64;
	struct cvmx_pip_stat10_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L2 Broadcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         broadcast packet when the 48-bit DMAC is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
	uint64_t mcast                        : 32; /**< Number of packets with L2 Mulitcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         multicast packet when the least-significant bit
                                                         of the first byte of the DMAC is set and the
                                                         packet is not an L2 broadcast packet.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_stat10_x_s            cn68xx;
	struct cvmx_pip_stat10_x_s            cn68xxp1;
};
typedef union cvmx_pip_stat10_x cvmx_pip_stat10_x_t;

/**
 * cvmx_pip_stat10_prt#
 *
 * PIP_STAT10_PRTX = PIP_STAT_L2_MCAST / PIP_STAT_L2_BCAST
 *
 */
union cvmx_pip_stat10_prtx {
	uint64_t u64;
	struct cvmx_pip_stat10_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L2 Broadcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         broadcast packet when the 48-bit DMAC is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
	uint64_t mcast                        : 32; /**< Number of packets with L2 Mulitcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         multicast packet when the least-significant bit
                                                         of the first byte of the DMAC is set and the
                                                         packet is not an L2 broadcast packet.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_stat10_prtx_s         cn52xx;
	struct cvmx_pip_stat10_prtx_s         cn52xxp1;
	struct cvmx_pip_stat10_prtx_s         cn56xx;
	struct cvmx_pip_stat10_prtx_s         cn56xxp1;
	struct cvmx_pip_stat10_prtx_s         cn61xx;
	struct cvmx_pip_stat10_prtx_s         cn63xx;
	struct cvmx_pip_stat10_prtx_s         cn63xxp1;
	struct cvmx_pip_stat10_prtx_s         cn66xx;
	struct cvmx_pip_stat10_prtx_s         cnf71xx;
};
typedef union cvmx_pip_stat10_prtx cvmx_pip_stat10_prtx_t;

/**
 * cvmx_pip_stat11_#
 *
 * PIP_STAT11_X = PIP_STAT_L3_MCAST / PIP_STAT_L3_BCAST
 *
 */
union cvmx_pip_stat11_x {
	uint64_t u64;
	struct cvmx_pip_stat11_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L3 Broadcast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be broadcast
                                                         when all bits are set in the MSB of the
                                                         destination address. IPv6 does not have the
                                                         concept of a broadcast packets.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
	uint64_t mcast                        : 32; /**< Number of packets with L3 Multicast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be multicast
                                                         when the most-significant nibble of the 32-bit
                                                         destination address is 0xE (i.e. it is a class D
                                                         address). The HW considers an IPv6 packet to be
                                                         multicast when the most-significant byte of the
                                                         128-bit destination address is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_stat11_x_s            cn68xx;
	struct cvmx_pip_stat11_x_s            cn68xxp1;
};
typedef union cvmx_pip_stat11_x cvmx_pip_stat11_x_t;

/**
 * cvmx_pip_stat11_prt#
 *
 * PIP_STAT11_PRTX = PIP_STAT_L3_MCAST / PIP_STAT_L3_BCAST
 *
 */
union cvmx_pip_stat11_prtx {
	uint64_t u64;
	struct cvmx_pip_stat11_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L3 Broadcast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be broadcast
                                                         when all bits are set in the MSB of the
                                                         destination address. IPv6 does not have the
                                                         concept of a broadcast packets.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
	uint64_t mcast                        : 32; /**< Number of packets with L3 Multicast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be multicast
                                                         when the most-significant nibble of the 32-bit
                                                         destination address is 0xE (i.e. it is a class D
                                                         address). The HW considers an IPv6 packet to be
                                                         multicast when the most-significant byte of the
                                                         128-bit destination address is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_stat11_prtx_s         cn52xx;
	struct cvmx_pip_stat11_prtx_s         cn52xxp1;
	struct cvmx_pip_stat11_prtx_s         cn56xx;
	struct cvmx_pip_stat11_prtx_s         cn56xxp1;
	struct cvmx_pip_stat11_prtx_s         cn61xx;
	struct cvmx_pip_stat11_prtx_s         cn63xx;
	struct cvmx_pip_stat11_prtx_s         cn63xxp1;
	struct cvmx_pip_stat11_prtx_s         cn66xx;
	struct cvmx_pip_stat11_prtx_s         cnf71xx;
};
typedef union cvmx_pip_stat11_prtx cvmx_pip_stat11_prtx_t;

/**
 * cvmx_pip_stat1_#
 *
 * PIP_STAT1_X = PIP_STAT_OCTS
 *
 */
union cvmx_pip_stat1_x {
	uint64_t u64;
	struct cvmx_pip_stat1_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Number of octets received by PIP (good and bad) */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_stat1_x_s             cn68xx;
	struct cvmx_pip_stat1_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat1_x cvmx_pip_stat1_x_t;

/**
 * cvmx_pip_stat1_prt#
 *
 * PIP_STAT1_PRTX = PIP_STAT_OCTS
 *
 */
union cvmx_pip_stat1_prtx {
	uint64_t u64;
	struct cvmx_pip_stat1_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Number of octets received by PIP (good and bad) */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_stat1_prtx_s          cn30xx;
	struct cvmx_pip_stat1_prtx_s          cn31xx;
	struct cvmx_pip_stat1_prtx_s          cn38xx;
	struct cvmx_pip_stat1_prtx_s          cn38xxp2;
	struct cvmx_pip_stat1_prtx_s          cn50xx;
	struct cvmx_pip_stat1_prtx_s          cn52xx;
	struct cvmx_pip_stat1_prtx_s          cn52xxp1;
	struct cvmx_pip_stat1_prtx_s          cn56xx;
	struct cvmx_pip_stat1_prtx_s          cn56xxp1;
	struct cvmx_pip_stat1_prtx_s          cn58xx;
	struct cvmx_pip_stat1_prtx_s          cn58xxp1;
	struct cvmx_pip_stat1_prtx_s          cn61xx;
	struct cvmx_pip_stat1_prtx_s          cn63xx;
	struct cvmx_pip_stat1_prtx_s          cn63xxp1;
	struct cvmx_pip_stat1_prtx_s          cn66xx;
	struct cvmx_pip_stat1_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat1_prtx cvmx_pip_stat1_prtx_t;

/**
 * cvmx_pip_stat2_#
 *
 * PIP_STAT2_X = PIP_STAT_PKTS     / PIP_STAT_RAW
 *
 */
union cvmx_pip_stat2_x {
	uint64_t u64;
	struct cvmx_pip_stat2_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pkts                         : 32; /**< Number of packets processed by PIP */
	uint64_t raw                          : 32; /**< RAWFULL + RAWSCH Packets without an L1/L2 error
                                                         received by PIP per port */
#else
	uint64_t raw                          : 32;
	uint64_t pkts                         : 32;
#endif
	} s;
	struct cvmx_pip_stat2_x_s             cn68xx;
	struct cvmx_pip_stat2_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat2_x cvmx_pip_stat2_x_t;

/**
 * cvmx_pip_stat2_prt#
 *
 * PIP_STAT2_PRTX = PIP_STAT_PKTS     / PIP_STAT_RAW
 *
 */
union cvmx_pip_stat2_prtx {
	uint64_t u64;
	struct cvmx_pip_stat2_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pkts                         : 32; /**< Number of packets processed by PIP */
	uint64_t raw                          : 32; /**< RAWFULL + RAWSCH Packets without an L1/L2 error
                                                         received by PIP per port */
#else
	uint64_t raw                          : 32;
	uint64_t pkts                         : 32;
#endif
	} s;
	struct cvmx_pip_stat2_prtx_s          cn30xx;
	struct cvmx_pip_stat2_prtx_s          cn31xx;
	struct cvmx_pip_stat2_prtx_s          cn38xx;
	struct cvmx_pip_stat2_prtx_s          cn38xxp2;
	struct cvmx_pip_stat2_prtx_s          cn50xx;
	struct cvmx_pip_stat2_prtx_s          cn52xx;
	struct cvmx_pip_stat2_prtx_s          cn52xxp1;
	struct cvmx_pip_stat2_prtx_s          cn56xx;
	struct cvmx_pip_stat2_prtx_s          cn56xxp1;
	struct cvmx_pip_stat2_prtx_s          cn58xx;
	struct cvmx_pip_stat2_prtx_s          cn58xxp1;
	struct cvmx_pip_stat2_prtx_s          cn61xx;
	struct cvmx_pip_stat2_prtx_s          cn63xx;
	struct cvmx_pip_stat2_prtx_s          cn63xxp1;
	struct cvmx_pip_stat2_prtx_s          cn66xx;
	struct cvmx_pip_stat2_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat2_prtx cvmx_pip_stat2_prtx_t;

/**
 * cvmx_pip_stat3_#
 *
 * PIP_STAT3_X = PIP_STAT_BCST     / PIP_STAT_MCST
 *
 */
union cvmx_pip_stat3_x {
	uint64_t u64;
	struct cvmx_pip_stat3_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcst                         : 32; /**< Number of indentified L2 broadcast packets
                                                         Does not include multicast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
	uint64_t mcst                         : 32; /**< Number of indentified L2 multicast packets
                                                         Does not include broadcast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
#else
	uint64_t mcst                         : 32;
	uint64_t bcst                         : 32;
#endif
	} s;
	struct cvmx_pip_stat3_x_s             cn68xx;
	struct cvmx_pip_stat3_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat3_x cvmx_pip_stat3_x_t;

/**
 * cvmx_pip_stat3_prt#
 *
 * PIP_STAT3_PRTX = PIP_STAT_BCST     / PIP_STAT_MCST
 *
 */
union cvmx_pip_stat3_prtx {
	uint64_t u64;
	struct cvmx_pip_stat3_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcst                         : 32; /**< Number of indentified L2 broadcast packets
                                                         Does not include multicast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
	uint64_t mcst                         : 32; /**< Number of indentified L2 multicast packets
                                                         Does not include broadcast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
#else
	uint64_t mcst                         : 32;
	uint64_t bcst                         : 32;
#endif
	} s;
	struct cvmx_pip_stat3_prtx_s          cn30xx;
	struct cvmx_pip_stat3_prtx_s          cn31xx;
	struct cvmx_pip_stat3_prtx_s          cn38xx;
	struct cvmx_pip_stat3_prtx_s          cn38xxp2;
	struct cvmx_pip_stat3_prtx_s          cn50xx;
	struct cvmx_pip_stat3_prtx_s          cn52xx;
	struct cvmx_pip_stat3_prtx_s          cn52xxp1;
	struct cvmx_pip_stat3_prtx_s          cn56xx;
	struct cvmx_pip_stat3_prtx_s          cn56xxp1;
	struct cvmx_pip_stat3_prtx_s          cn58xx;
	struct cvmx_pip_stat3_prtx_s          cn58xxp1;
	struct cvmx_pip_stat3_prtx_s          cn61xx;
	struct cvmx_pip_stat3_prtx_s          cn63xx;
	struct cvmx_pip_stat3_prtx_s          cn63xxp1;
	struct cvmx_pip_stat3_prtx_s          cn66xx;
	struct cvmx_pip_stat3_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat3_prtx cvmx_pip_stat3_prtx_t;

/**
 * cvmx_pip_stat4_#
 *
 * PIP_STAT4_X = PIP_STAT_HIST1    / PIP_STAT_HIST0
 *
 */
union cvmx_pip_stat4_x {
	uint64_t u64;
	struct cvmx_pip_stat4_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h65to127                     : 32; /**< Number of 65-127B packets */
	uint64_t h64                          : 32; /**< Number of 1-64B packets */
#else
	uint64_t h64                          : 32;
	uint64_t h65to127                     : 32;
#endif
	} s;
	struct cvmx_pip_stat4_x_s             cn68xx;
	struct cvmx_pip_stat4_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat4_x cvmx_pip_stat4_x_t;

/**
 * cvmx_pip_stat4_prt#
 *
 * PIP_STAT4_PRTX = PIP_STAT_HIST1    / PIP_STAT_HIST0
 *
 */
union cvmx_pip_stat4_prtx {
	uint64_t u64;
	struct cvmx_pip_stat4_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h65to127                     : 32; /**< Number of 65-127B packets */
	uint64_t h64                          : 32; /**< Number of 1-64B packets */
#else
	uint64_t h64                          : 32;
	uint64_t h65to127                     : 32;
#endif
	} s;
	struct cvmx_pip_stat4_prtx_s          cn30xx;
	struct cvmx_pip_stat4_prtx_s          cn31xx;
	struct cvmx_pip_stat4_prtx_s          cn38xx;
	struct cvmx_pip_stat4_prtx_s          cn38xxp2;
	struct cvmx_pip_stat4_prtx_s          cn50xx;
	struct cvmx_pip_stat4_prtx_s          cn52xx;
	struct cvmx_pip_stat4_prtx_s          cn52xxp1;
	struct cvmx_pip_stat4_prtx_s          cn56xx;
	struct cvmx_pip_stat4_prtx_s          cn56xxp1;
	struct cvmx_pip_stat4_prtx_s          cn58xx;
	struct cvmx_pip_stat4_prtx_s          cn58xxp1;
	struct cvmx_pip_stat4_prtx_s          cn61xx;
	struct cvmx_pip_stat4_prtx_s          cn63xx;
	struct cvmx_pip_stat4_prtx_s          cn63xxp1;
	struct cvmx_pip_stat4_prtx_s          cn66xx;
	struct cvmx_pip_stat4_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat4_prtx cvmx_pip_stat4_prtx_t;

/**
 * cvmx_pip_stat5_#
 *
 * PIP_STAT5_X = PIP_STAT_HIST3    / PIP_STAT_HIST2
 *
 */
union cvmx_pip_stat5_x {
	uint64_t u64;
	struct cvmx_pip_stat5_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h256to511                    : 32; /**< Number of 256-511B packets */
	uint64_t h128to255                    : 32; /**< Number of 128-255B packets */
#else
	uint64_t h128to255                    : 32;
	uint64_t h256to511                    : 32;
#endif
	} s;
	struct cvmx_pip_stat5_x_s             cn68xx;
	struct cvmx_pip_stat5_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat5_x cvmx_pip_stat5_x_t;

/**
 * cvmx_pip_stat5_prt#
 *
 * PIP_STAT5_PRTX = PIP_STAT_HIST3    / PIP_STAT_HIST2
 *
 */
union cvmx_pip_stat5_prtx {
	uint64_t u64;
	struct cvmx_pip_stat5_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h256to511                    : 32; /**< Number of 256-511B packets */
	uint64_t h128to255                    : 32; /**< Number of 128-255B packets */
#else
	uint64_t h128to255                    : 32;
	uint64_t h256to511                    : 32;
#endif
	} s;
	struct cvmx_pip_stat5_prtx_s          cn30xx;
	struct cvmx_pip_stat5_prtx_s          cn31xx;
	struct cvmx_pip_stat5_prtx_s          cn38xx;
	struct cvmx_pip_stat5_prtx_s          cn38xxp2;
	struct cvmx_pip_stat5_prtx_s          cn50xx;
	struct cvmx_pip_stat5_prtx_s          cn52xx;
	struct cvmx_pip_stat5_prtx_s          cn52xxp1;
	struct cvmx_pip_stat5_prtx_s          cn56xx;
	struct cvmx_pip_stat5_prtx_s          cn56xxp1;
	struct cvmx_pip_stat5_prtx_s          cn58xx;
	struct cvmx_pip_stat5_prtx_s          cn58xxp1;
	struct cvmx_pip_stat5_prtx_s          cn61xx;
	struct cvmx_pip_stat5_prtx_s          cn63xx;
	struct cvmx_pip_stat5_prtx_s          cn63xxp1;
	struct cvmx_pip_stat5_prtx_s          cn66xx;
	struct cvmx_pip_stat5_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat5_prtx cvmx_pip_stat5_prtx_t;

/**
 * cvmx_pip_stat6_#
 *
 * PIP_STAT6_X = PIP_STAT_HIST5    / PIP_STAT_HIST4
 *
 */
union cvmx_pip_stat6_x {
	uint64_t u64;
	struct cvmx_pip_stat6_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h1024to1518                  : 32; /**< Number of 1024-1518B packets */
	uint64_t h512to1023                   : 32; /**< Number of 512-1023B packets */
#else
	uint64_t h512to1023                   : 32;
	uint64_t h1024to1518                  : 32;
#endif
	} s;
	struct cvmx_pip_stat6_x_s             cn68xx;
	struct cvmx_pip_stat6_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat6_x cvmx_pip_stat6_x_t;

/**
 * cvmx_pip_stat6_prt#
 *
 * PIP_STAT6_PRTX = PIP_STAT_HIST5    / PIP_STAT_HIST4
 *
 */
union cvmx_pip_stat6_prtx {
	uint64_t u64;
	struct cvmx_pip_stat6_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h1024to1518                  : 32; /**< Number of 1024-1518B packets */
	uint64_t h512to1023                   : 32; /**< Number of 512-1023B packets */
#else
	uint64_t h512to1023                   : 32;
	uint64_t h1024to1518                  : 32;
#endif
	} s;
	struct cvmx_pip_stat6_prtx_s          cn30xx;
	struct cvmx_pip_stat6_prtx_s          cn31xx;
	struct cvmx_pip_stat6_prtx_s          cn38xx;
	struct cvmx_pip_stat6_prtx_s          cn38xxp2;
	struct cvmx_pip_stat6_prtx_s          cn50xx;
	struct cvmx_pip_stat6_prtx_s          cn52xx;
	struct cvmx_pip_stat6_prtx_s          cn52xxp1;
	struct cvmx_pip_stat6_prtx_s          cn56xx;
	struct cvmx_pip_stat6_prtx_s          cn56xxp1;
	struct cvmx_pip_stat6_prtx_s          cn58xx;
	struct cvmx_pip_stat6_prtx_s          cn58xxp1;
	struct cvmx_pip_stat6_prtx_s          cn61xx;
	struct cvmx_pip_stat6_prtx_s          cn63xx;
	struct cvmx_pip_stat6_prtx_s          cn63xxp1;
	struct cvmx_pip_stat6_prtx_s          cn66xx;
	struct cvmx_pip_stat6_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat6_prtx cvmx_pip_stat6_prtx_t;

/**
 * cvmx_pip_stat7_#
 *
 * PIP_STAT7_X = PIP_STAT_FCS      / PIP_STAT_HIST6
 *
 */
union cvmx_pip_stat7_x {
	uint64_t u64;
	struct cvmx_pip_stat7_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fcs                          : 32; /**< Number of packets with FCS or Align opcode errors */
	uint64_t h1519                        : 32; /**< Number of 1519-max packets */
#else
	uint64_t h1519                        : 32;
	uint64_t fcs                          : 32;
#endif
	} s;
	struct cvmx_pip_stat7_x_s             cn68xx;
	struct cvmx_pip_stat7_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat7_x cvmx_pip_stat7_x_t;

/**
 * cvmx_pip_stat7_prt#
 *
 * PIP_STAT7_PRTX = PIP_STAT_FCS      / PIP_STAT_HIST6
 *
 *
 * Notes:
 * DPI does not check FCS, therefore FCS will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore FCS will never increment on sRIO ports 40-47
 */
union cvmx_pip_stat7_prtx {
	uint64_t u64;
	struct cvmx_pip_stat7_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fcs                          : 32; /**< Number of packets with FCS or Align opcode errors */
	uint64_t h1519                        : 32; /**< Number of 1519-max packets */
#else
	uint64_t h1519                        : 32;
	uint64_t fcs                          : 32;
#endif
	} s;
	struct cvmx_pip_stat7_prtx_s          cn30xx;
	struct cvmx_pip_stat7_prtx_s          cn31xx;
	struct cvmx_pip_stat7_prtx_s          cn38xx;
	struct cvmx_pip_stat7_prtx_s          cn38xxp2;
	struct cvmx_pip_stat7_prtx_s          cn50xx;
	struct cvmx_pip_stat7_prtx_s          cn52xx;
	struct cvmx_pip_stat7_prtx_s          cn52xxp1;
	struct cvmx_pip_stat7_prtx_s          cn56xx;
	struct cvmx_pip_stat7_prtx_s          cn56xxp1;
	struct cvmx_pip_stat7_prtx_s          cn58xx;
	struct cvmx_pip_stat7_prtx_s          cn58xxp1;
	struct cvmx_pip_stat7_prtx_s          cn61xx;
	struct cvmx_pip_stat7_prtx_s          cn63xx;
	struct cvmx_pip_stat7_prtx_s          cn63xxp1;
	struct cvmx_pip_stat7_prtx_s          cn66xx;
	struct cvmx_pip_stat7_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat7_prtx cvmx_pip_stat7_prtx_t;

/**
 * cvmx_pip_stat8_#
 *
 * PIP_STAT8_X = PIP_STAT_FRAG     / PIP_STAT_UNDER
 *
 */
union cvmx_pip_stat8_x {
	uint64_t u64;
	struct cvmx_pip_stat8_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t frag                         : 32; /**< Number of packets with length < min and FCS error */
	uint64_t undersz                      : 32; /**< Number of packets with length < min */
#else
	uint64_t undersz                      : 32;
	uint64_t frag                         : 32;
#endif
	} s;
	struct cvmx_pip_stat8_x_s             cn68xx;
	struct cvmx_pip_stat8_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat8_x cvmx_pip_stat8_x_t;

/**
 * cvmx_pip_stat8_prt#
 *
 * PIP_STAT8_PRTX = PIP_STAT_FRAG     / PIP_STAT_UNDER
 *
 *
 * Notes:
 * DPI does not check FCS, therefore FRAG will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore FRAG will never increment on sRIO ports 40-47
 */
union cvmx_pip_stat8_prtx {
	uint64_t u64;
	struct cvmx_pip_stat8_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t frag                         : 32; /**< Number of packets with length < min and FCS error */
	uint64_t undersz                      : 32; /**< Number of packets with length < min */
#else
	uint64_t undersz                      : 32;
	uint64_t frag                         : 32;
#endif
	} s;
	struct cvmx_pip_stat8_prtx_s          cn30xx;
	struct cvmx_pip_stat8_prtx_s          cn31xx;
	struct cvmx_pip_stat8_prtx_s          cn38xx;
	struct cvmx_pip_stat8_prtx_s          cn38xxp2;
	struct cvmx_pip_stat8_prtx_s          cn50xx;
	struct cvmx_pip_stat8_prtx_s          cn52xx;
	struct cvmx_pip_stat8_prtx_s          cn52xxp1;
	struct cvmx_pip_stat8_prtx_s          cn56xx;
	struct cvmx_pip_stat8_prtx_s          cn56xxp1;
	struct cvmx_pip_stat8_prtx_s          cn58xx;
	struct cvmx_pip_stat8_prtx_s          cn58xxp1;
	struct cvmx_pip_stat8_prtx_s          cn61xx;
	struct cvmx_pip_stat8_prtx_s          cn63xx;
	struct cvmx_pip_stat8_prtx_s          cn63xxp1;
	struct cvmx_pip_stat8_prtx_s          cn66xx;
	struct cvmx_pip_stat8_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat8_prtx cvmx_pip_stat8_prtx_t;

/**
 * cvmx_pip_stat9_#
 *
 * PIP_STAT9_X = PIP_STAT_JABBER   / PIP_STAT_OVER
 *
 */
union cvmx_pip_stat9_x {
	uint64_t u64;
	struct cvmx_pip_stat9_x_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t jabber                       : 32; /**< Number of packets with length > max and FCS error */
	uint64_t oversz                       : 32; /**< Number of packets with length > max */
#else
	uint64_t oversz                       : 32;
	uint64_t jabber                       : 32;
#endif
	} s;
	struct cvmx_pip_stat9_x_s             cn68xx;
	struct cvmx_pip_stat9_x_s             cn68xxp1;
};
typedef union cvmx_pip_stat9_x cvmx_pip_stat9_x_t;

/**
 * cvmx_pip_stat9_prt#
 *
 * PIP_STAT9_PRTX = PIP_STAT_JABBER   / PIP_STAT_OVER
 *
 *
 * Notes:
 * DPI does not check FCS, therefore JABBER will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore JABBER will never increment on sRIO ports 40-47 due to FCS errors
 * sRIO does use the JABBER opcode to communicate sRIO error, therefore JABBER can increment under the sRIO error conditions
 */
union cvmx_pip_stat9_prtx {
	uint64_t u64;
	struct cvmx_pip_stat9_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t jabber                       : 32; /**< Number of packets with length > max and FCS error */
	uint64_t oversz                       : 32; /**< Number of packets with length > max */
#else
	uint64_t oversz                       : 32;
	uint64_t jabber                       : 32;
#endif
	} s;
	struct cvmx_pip_stat9_prtx_s          cn30xx;
	struct cvmx_pip_stat9_prtx_s          cn31xx;
	struct cvmx_pip_stat9_prtx_s          cn38xx;
	struct cvmx_pip_stat9_prtx_s          cn38xxp2;
	struct cvmx_pip_stat9_prtx_s          cn50xx;
	struct cvmx_pip_stat9_prtx_s          cn52xx;
	struct cvmx_pip_stat9_prtx_s          cn52xxp1;
	struct cvmx_pip_stat9_prtx_s          cn56xx;
	struct cvmx_pip_stat9_prtx_s          cn56xxp1;
	struct cvmx_pip_stat9_prtx_s          cn58xx;
	struct cvmx_pip_stat9_prtx_s          cn58xxp1;
	struct cvmx_pip_stat9_prtx_s          cn61xx;
	struct cvmx_pip_stat9_prtx_s          cn63xx;
	struct cvmx_pip_stat9_prtx_s          cn63xxp1;
	struct cvmx_pip_stat9_prtx_s          cn66xx;
	struct cvmx_pip_stat9_prtx_s          cnf71xx;
};
typedef union cvmx_pip_stat9_prtx cvmx_pip_stat9_prtx_t;

/**
 * cvmx_pip_stat_ctl
 *
 * PIP_STAT_CTL = PIP's Stat Control Register
 *
 * Controls how the PIP statistics counters are handled.
 */
union cvmx_pip_stat_ctl {
	uint64_t u64;
	struct cvmx_pip_stat_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_9_63                : 55;
	uint64_t mode                         : 1;  /**< The PIP_STAT*_X registers can be indexed either by
                                                         port-kind or backpressure ID.
                                                         Does not apply to the PIP_STAT_INB* registers.
                                                         0 = X represents the packet's port-kind
                                                         1 = X represents the packet's backpressure ID */
	uint64_t reserved_1_7                 : 7;
	uint64_t rdclr                        : 1;  /**< Stat registers are read and clear
                                                         0 = stat registers hold value when read
                                                         1 = stat registers are cleared when read */
#else
	uint64_t rdclr                        : 1;
	uint64_t reserved_1_7                 : 7;
	uint64_t mode                         : 1;
	uint64_t reserved_9_63                : 55;
#endif
	} s;
	struct cvmx_pip_stat_ctl_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_1_63                : 63;
	uint64_t rdclr                        : 1;  /**< Stat registers are read and clear
                                                         0 = stat registers hold value when read
                                                         1 = stat registers are cleared when read */
#else
	uint64_t rdclr                        : 1;
	uint64_t reserved_1_63                : 63;
#endif
	} cn30xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn31xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn38xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn38xxp2;
	struct cvmx_pip_stat_ctl_cn30xx       cn50xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn52xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn52xxp1;
	struct cvmx_pip_stat_ctl_cn30xx       cn56xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn56xxp1;
	struct cvmx_pip_stat_ctl_cn30xx       cn58xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn58xxp1;
	struct cvmx_pip_stat_ctl_cn30xx       cn61xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn63xx;
	struct cvmx_pip_stat_ctl_cn30xx       cn63xxp1;
	struct cvmx_pip_stat_ctl_cn30xx       cn66xx;
	struct cvmx_pip_stat_ctl_s            cn68xx;
	struct cvmx_pip_stat_ctl_s            cn68xxp1;
	struct cvmx_pip_stat_ctl_cn30xx       cnf71xx;
};
typedef union cvmx_pip_stat_ctl cvmx_pip_stat_ctl_t;

/**
 * cvmx_pip_stat_inb_errs#
 *
 * PIP_STAT_INB_ERRSX = Inbound error packets received by PIP per port
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems.
 */
union cvmx_pip_stat_inb_errsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_errsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t errs                         : 16; /**< Number of packets with errors
                                                         received by PIP */
#else
	uint64_t errs                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pip_stat_inb_errsx_s      cn30xx;
	struct cvmx_pip_stat_inb_errsx_s      cn31xx;
	struct cvmx_pip_stat_inb_errsx_s      cn38xx;
	struct cvmx_pip_stat_inb_errsx_s      cn38xxp2;
	struct cvmx_pip_stat_inb_errsx_s      cn50xx;
	struct cvmx_pip_stat_inb_errsx_s      cn52xx;
	struct cvmx_pip_stat_inb_errsx_s      cn52xxp1;
	struct cvmx_pip_stat_inb_errsx_s      cn56xx;
	struct cvmx_pip_stat_inb_errsx_s      cn56xxp1;
	struct cvmx_pip_stat_inb_errsx_s      cn58xx;
	struct cvmx_pip_stat_inb_errsx_s      cn58xxp1;
	struct cvmx_pip_stat_inb_errsx_s      cn61xx;
	struct cvmx_pip_stat_inb_errsx_s      cn63xx;
	struct cvmx_pip_stat_inb_errsx_s      cn63xxp1;
	struct cvmx_pip_stat_inb_errsx_s      cn66xx;
	struct cvmx_pip_stat_inb_errsx_s      cnf71xx;
};
typedef union cvmx_pip_stat_inb_errsx cvmx_pip_stat_inb_errsx_t;

/**
 * cvmx_pip_stat_inb_errs_pknd#
 *
 * PIP_STAT_INB_ERRS_PKNDX = Inbound error packets received by PIP per pkind
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems.
 */
union cvmx_pip_stat_inb_errs_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_errs_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t errs                         : 16; /**< Number of packets with errors
                                                         received by PIP */
#else
	uint64_t errs                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pip_stat_inb_errs_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_errs_pkndx_s cn68xxp1;
};
typedef union cvmx_pip_stat_inb_errs_pkndx cvmx_pip_stat_inb_errs_pkndx_t;

/**
 * cvmx_pip_stat_inb_octs#
 *
 * PIP_STAT_INB_OCTSX = Inbound octets received by PIP per port
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems. The OCTS will include the bytes from
 * timestamp fields in PTP_MODE.
 */
union cvmx_pip_stat_inb_octsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_octsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Total number of octets from all packets received
                                                         by PIP */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_stat_inb_octsx_s      cn30xx;
	struct cvmx_pip_stat_inb_octsx_s      cn31xx;
	struct cvmx_pip_stat_inb_octsx_s      cn38xx;
	struct cvmx_pip_stat_inb_octsx_s      cn38xxp2;
	struct cvmx_pip_stat_inb_octsx_s      cn50xx;
	struct cvmx_pip_stat_inb_octsx_s      cn52xx;
	struct cvmx_pip_stat_inb_octsx_s      cn52xxp1;
	struct cvmx_pip_stat_inb_octsx_s      cn56xx;
	struct cvmx_pip_stat_inb_octsx_s      cn56xxp1;
	struct cvmx_pip_stat_inb_octsx_s      cn58xx;
	struct cvmx_pip_stat_inb_octsx_s      cn58xxp1;
	struct cvmx_pip_stat_inb_octsx_s      cn61xx;
	struct cvmx_pip_stat_inb_octsx_s      cn63xx;
	struct cvmx_pip_stat_inb_octsx_s      cn63xxp1;
	struct cvmx_pip_stat_inb_octsx_s      cn66xx;
	struct cvmx_pip_stat_inb_octsx_s      cnf71xx;
};
typedef union cvmx_pip_stat_inb_octsx cvmx_pip_stat_inb_octsx_t;

/**
 * cvmx_pip_stat_inb_octs_pknd#
 *
 * PIP_STAT_INB_OCTS_PKNDX = Inbound octets received by PIP per pkind
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems. The OCTS will include the bytes from
 * timestamp fields in PTP_MODE.
 */
union cvmx_pip_stat_inb_octs_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_octs_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Total number of octets from all packets received
                                                         by PIP */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_stat_inb_octs_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_octs_pkndx_s cn68xxp1;
};
typedef union cvmx_pip_stat_inb_octs_pkndx cvmx_pip_stat_inb_octs_pkndx_t;

/**
 * cvmx_pip_stat_inb_pkts#
 *
 * PIP_STAT_INB_PKTSX = Inbound packets received by PIP per port
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems.
 */
union cvmx_pip_stat_inb_pktsx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_pktsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pkts                         : 32; /**< Number of packets without errors
                                                         received by PIP */
#else
	uint64_t pkts                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pip_stat_inb_pktsx_s      cn30xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn31xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn38xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn38xxp2;
	struct cvmx_pip_stat_inb_pktsx_s      cn50xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn52xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn52xxp1;
	struct cvmx_pip_stat_inb_pktsx_s      cn56xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn56xxp1;
	struct cvmx_pip_stat_inb_pktsx_s      cn58xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn58xxp1;
	struct cvmx_pip_stat_inb_pktsx_s      cn61xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn63xx;
	struct cvmx_pip_stat_inb_pktsx_s      cn63xxp1;
	struct cvmx_pip_stat_inb_pktsx_s      cn66xx;
	struct cvmx_pip_stat_inb_pktsx_s      cnf71xx;
};
typedef union cvmx_pip_stat_inb_pktsx cvmx_pip_stat_inb_pktsx_t;

/**
 * cvmx_pip_stat_inb_pkts_pknd#
 *
 * PIP_STAT_INB_PKTS_PKNDX = Inbound packets received by PIP per pkind
 *
 * Inbound stats collect all data sent to PIP from all packet interfaces.
 * Its the raw counts of everything that comes into the block.  The counts
 * will reflect all error packets and packets dropped by the PKI RED engine.
 * These counts are intended for system debug, but could convey useful
 * information in production systems.
 */
union cvmx_pip_stat_inb_pkts_pkndx {
	uint64_t u64;
	struct cvmx_pip_stat_inb_pkts_pkndx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t pkts                         : 32; /**< Number of packets without errors
                                                         received by PIP */
#else
	uint64_t pkts                         : 32;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pip_stat_inb_pkts_pkndx_s cn68xx;
	struct cvmx_pip_stat_inb_pkts_pkndx_s cn68xxp1;
};
typedef union cvmx_pip_stat_inb_pkts_pkndx cvmx_pip_stat_inb_pkts_pkndx_t;

/**
 * cvmx_pip_sub_pkind_fcs#
 */
union cvmx_pip_sub_pkind_fcsx {
	uint64_t u64;
	struct cvmx_pip_sub_pkind_fcsx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t port_bit                     : 64; /**< When set '1', the pkind corresponding to the bit
                                                         position set, will subtract the FCS for packets
                                                         on that pkind. */
#else
	uint64_t port_bit                     : 64;
#endif
	} s;
	struct cvmx_pip_sub_pkind_fcsx_s      cn68xx;
	struct cvmx_pip_sub_pkind_fcsx_s      cn68xxp1;
};
typedef union cvmx_pip_sub_pkind_fcsx cvmx_pip_sub_pkind_fcsx_t;

/**
 * cvmx_pip_tag_inc#
 *
 * PIP_TAG_INC = Which bytes to include in the new tag hash algorithm
 *
 * # $PIP_TAG_INCX = 0x300+X X=(0..63) RegType=(RSL) RtlReg=(pip_tag_inc_csr_direct_TestbuilderTask)
 */
union cvmx_pip_tag_incx {
	uint64_t u64;
	struct cvmx_pip_tag_incx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_8_63                : 56;
	uint64_t en                           : 8;  /**< Which bytes to include in mask tag algorithm
                                                         Broken into 4, 16-entry masks to cover 128B
                                                         PIP_PRT_CFG[TAG_INC] selects 1 of 4 to use
                                                         registers  0-15 map to PIP_PRT_CFG[TAG_INC] == 0
                                                         registers 16-31 map to PIP_PRT_CFG[TAG_INC] == 1
                                                         registers 32-47 map to PIP_PRT_CFG[TAG_INC] == 2
                                                         registers 48-63 map to PIP_PRT_CFG[TAG_INC] == 3
                                                         [7] coresponds to the MSB of the 8B word
                                                         [0] coresponds to the LSB of the 8B word
                                                         If PTP_MODE, the 8B timestamp is prepended to the
                                                          packet.  The EN byte masks should be adjusted to
                                                          compensate for the additional timestamp field. */
#else
	uint64_t en                           : 8;
	uint64_t reserved_8_63                : 56;
#endif
	} s;
	struct cvmx_pip_tag_incx_s            cn30xx;
	struct cvmx_pip_tag_incx_s            cn31xx;
	struct cvmx_pip_tag_incx_s            cn38xx;
	struct cvmx_pip_tag_incx_s            cn38xxp2;
	struct cvmx_pip_tag_incx_s            cn50xx;
	struct cvmx_pip_tag_incx_s            cn52xx;
	struct cvmx_pip_tag_incx_s            cn52xxp1;
	struct cvmx_pip_tag_incx_s            cn56xx;
	struct cvmx_pip_tag_incx_s            cn56xxp1;
	struct cvmx_pip_tag_incx_s            cn58xx;
	struct cvmx_pip_tag_incx_s            cn58xxp1;
	struct cvmx_pip_tag_incx_s            cn61xx;
	struct cvmx_pip_tag_incx_s            cn63xx;
	struct cvmx_pip_tag_incx_s            cn63xxp1;
	struct cvmx_pip_tag_incx_s            cn66xx;
	struct cvmx_pip_tag_incx_s            cn68xx;
	struct cvmx_pip_tag_incx_s            cn68xxp1;
	struct cvmx_pip_tag_incx_s            cnf71xx;
};
typedef union cvmx_pip_tag_incx cvmx_pip_tag_incx_t;

/**
 * cvmx_pip_tag_mask
 *
 * PIP_TAG_MASK = Mask bit in the tag generation
 *
 */
union cvmx_pip_tag_mask {
	uint64_t u64;
	struct cvmx_pip_tag_mask_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_16_63               : 48;
	uint64_t mask                         : 16; /**< When set, MASK clears individual bits of lower 16
                                                         bits of the computed tag.  Does not effect RAW
                                                         or INSTR HDR packets. */
#else
	uint64_t mask                         : 16;
	uint64_t reserved_16_63               : 48;
#endif
	} s;
	struct cvmx_pip_tag_mask_s            cn30xx;
	struct cvmx_pip_tag_mask_s            cn31xx;
	struct cvmx_pip_tag_mask_s            cn38xx;
	struct cvmx_pip_tag_mask_s            cn38xxp2;
	struct cvmx_pip_tag_mask_s            cn50xx;
	struct cvmx_pip_tag_mask_s            cn52xx;
	struct cvmx_pip_tag_mask_s            cn52xxp1;
	struct cvmx_pip_tag_mask_s            cn56xx;
	struct cvmx_pip_tag_mask_s            cn56xxp1;
	struct cvmx_pip_tag_mask_s            cn58xx;
	struct cvmx_pip_tag_mask_s            cn58xxp1;
	struct cvmx_pip_tag_mask_s            cn61xx;
	struct cvmx_pip_tag_mask_s            cn63xx;
	struct cvmx_pip_tag_mask_s            cn63xxp1;
	struct cvmx_pip_tag_mask_s            cn66xx;
	struct cvmx_pip_tag_mask_s            cn68xx;
	struct cvmx_pip_tag_mask_s            cn68xxp1;
	struct cvmx_pip_tag_mask_s            cnf71xx;
};
typedef union cvmx_pip_tag_mask cvmx_pip_tag_mask_t;

/**
 * cvmx_pip_tag_secret
 *
 * PIP_TAG_SECRET = Initial value in tag generation
 *
 * The source and destination IV's provide a mechanism for each Octeon to be unique.
 */
union cvmx_pip_tag_secret {
	uint64_t u64;
	struct cvmx_pip_tag_secret_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_32_63               : 32;
	uint64_t dst                          : 16; /**< Secret for the destination tuple tag CRC calc */
	uint64_t src                          : 16; /**< Secret for the source tuple tag CRC calc */
#else
	uint64_t src                          : 16;
	uint64_t dst                          : 16;
	uint64_t reserved_32_63               : 32;
#endif
	} s;
	struct cvmx_pip_tag_secret_s          cn30xx;
	struct cvmx_pip_tag_secret_s          cn31xx;
	struct cvmx_pip_tag_secret_s          cn38xx;
	struct cvmx_pip_tag_secret_s          cn38xxp2;
	struct cvmx_pip_tag_secret_s          cn50xx;
	struct cvmx_pip_tag_secret_s          cn52xx;
	struct cvmx_pip_tag_secret_s          cn52xxp1;
	struct cvmx_pip_tag_secret_s          cn56xx;
	struct cvmx_pip_tag_secret_s          cn56xxp1;
	struct cvmx_pip_tag_secret_s          cn58xx;
	struct cvmx_pip_tag_secret_s          cn58xxp1;
	struct cvmx_pip_tag_secret_s          cn61xx;
	struct cvmx_pip_tag_secret_s          cn63xx;
	struct cvmx_pip_tag_secret_s          cn63xxp1;
	struct cvmx_pip_tag_secret_s          cn66xx;
	struct cvmx_pip_tag_secret_s          cn68xx;
	struct cvmx_pip_tag_secret_s          cn68xxp1;
	struct cvmx_pip_tag_secret_s          cnf71xx;
};
typedef union cvmx_pip_tag_secret cvmx_pip_tag_secret_t;

/**
 * cvmx_pip_todo_entry
 *
 * PIP_TODO_ENTRY = Head entry of the Todo list (debug only)
 *
 * Summary of the current packet that has completed and waiting to be processed
 */
union cvmx_pip_todo_entry {
	uint64_t u64;
	struct cvmx_pip_todo_entry_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t val                          : 1;  /**< Entry is valid */
	uint64_t reserved_62_62               : 1;
	uint64_t entry                        : 62; /**< Todo list entry summary */
#else
	uint64_t entry                        : 62;
	uint64_t reserved_62_62               : 1;
	uint64_t val                          : 1;
#endif
	} s;
	struct cvmx_pip_todo_entry_s          cn30xx;
	struct cvmx_pip_todo_entry_s          cn31xx;
	struct cvmx_pip_todo_entry_s          cn38xx;
	struct cvmx_pip_todo_entry_s          cn38xxp2;
	struct cvmx_pip_todo_entry_s          cn50xx;
	struct cvmx_pip_todo_entry_s          cn52xx;
	struct cvmx_pip_todo_entry_s          cn52xxp1;
	struct cvmx_pip_todo_entry_s          cn56xx;
	struct cvmx_pip_todo_entry_s          cn56xxp1;
	struct cvmx_pip_todo_entry_s          cn58xx;
	struct cvmx_pip_todo_entry_s          cn58xxp1;
	struct cvmx_pip_todo_entry_s          cn61xx;
	struct cvmx_pip_todo_entry_s          cn63xx;
	struct cvmx_pip_todo_entry_s          cn63xxp1;
	struct cvmx_pip_todo_entry_s          cn66xx;
	struct cvmx_pip_todo_entry_s          cn68xx;
	struct cvmx_pip_todo_entry_s          cn68xxp1;
	struct cvmx_pip_todo_entry_s          cnf71xx;
};
typedef union cvmx_pip_todo_entry cvmx_pip_todo_entry_t;

/**
 * cvmx_pip_vlan_etypes#
 */
union cvmx_pip_vlan_etypesx {
	uint64_t u64;
	struct cvmx_pip_vlan_etypesx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t type3                        : 16; /**< VLAN Ethertype */
	uint64_t type2                        : 16; /**< VLAN Ethertype */
	uint64_t type1                        : 16; /**< VLAN Ethertype */
	uint64_t type0                        : 16; /**< VLAN Ethertype
                                                         Specifies ethertypes that will be parsed as
                                                         containing VLAN information. Each TYPE is
                                                         orthagonal; if all eight are not required, set
                                                         multiple TYPEs to the same value (as in the
                                                         0x8100 default value). */
#else
	uint64_t type0                        : 16;
	uint64_t type1                        : 16;
	uint64_t type2                        : 16;
	uint64_t type3                        : 16;
#endif
	} s;
	struct cvmx_pip_vlan_etypesx_s        cn61xx;
	struct cvmx_pip_vlan_etypesx_s        cn66xx;
	struct cvmx_pip_vlan_etypesx_s        cn68xx;
	struct cvmx_pip_vlan_etypesx_s        cnf71xx;
};
typedef union cvmx_pip_vlan_etypesx cvmx_pip_vlan_etypesx_t;

/**
 * cvmx_pip_xstat0_prt#
 *
 * PIP_XSTAT0_PRT = PIP_XSTAT_DRP_PKTS / PIP_XSTAT_DRP_OCTS
 *
 */
union cvmx_pip_xstat0_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat0_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t drp_pkts                     : 32; /**< Inbound packets marked to be dropped by the IPD
                                                         QOS widget per port */
	uint64_t drp_octs                     : 32; /**< Inbound octets marked to be dropped by the IPD
                                                         QOS widget per port */
#else
	uint64_t drp_octs                     : 32;
	uint64_t drp_pkts                     : 32;
#endif
	} s;
	struct cvmx_pip_xstat0_prtx_s         cn63xx;
	struct cvmx_pip_xstat0_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat0_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat0_prtx cvmx_pip_xstat0_prtx_t;

/**
 * cvmx_pip_xstat10_prt#
 *
 * PIP_XSTAT10_PRTX = PIP_XSTAT_L2_MCAST / PIP_XSTAT_L2_BCAST
 *
 */
union cvmx_pip_xstat10_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat10_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L2 Broadcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         broadcast packet when the 48-bit DMAC is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
	uint64_t mcast                        : 32; /**< Number of packets with L2 Mulitcast DMAC
                                                         that were dropped due to RED.
                                                         The HW will consider a packet to be an L2
                                                         multicast packet when the least-significant bit
                                                         of the first byte of the DMAC is set and the
                                                         packet is not an L2 broadcast packet.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_xstat10_prtx_s        cn63xx;
	struct cvmx_pip_xstat10_prtx_s        cn63xxp1;
	struct cvmx_pip_xstat10_prtx_s        cn66xx;
};
typedef union cvmx_pip_xstat10_prtx cvmx_pip_xstat10_prtx_t;

/**
 * cvmx_pip_xstat11_prt#
 *
 * PIP_XSTAT11_PRTX = PIP_XSTAT_L3_MCAST / PIP_XSTAT_L3_BCAST
 *
 */
union cvmx_pip_xstat11_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat11_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcast                        : 32; /**< Number of packets with L3 Broadcast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be broadcast
                                                         when all bits are set in the MSB of the
                                                         destination address. IPv6 does not have the
                                                         concept of a broadcast packets.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
	uint64_t mcast                        : 32; /**< Number of packets with L3 Multicast Dest Address
                                                         that were dropped due to RED.
                                                         The HW considers an IPv4 packet to be multicast
                                                         when the most-significant nibble of the 32-bit
                                                         destination address is 0xE (i.e. it is a class D
                                                         address). The HW considers an IPv6 packet to be
                                                         multicast when the most-significant byte of the
                                                         128-bit destination address is all 1's.
                                                         Only applies when the parse mode for the packet
                                                         is SKIP-TO-L2 and the packet is IP or the parse
                                                         mode for the packet is SKIP-TO-IP. */
#else
	uint64_t mcast                        : 32;
	uint64_t bcast                        : 32;
#endif
	} s;
	struct cvmx_pip_xstat11_prtx_s        cn63xx;
	struct cvmx_pip_xstat11_prtx_s        cn63xxp1;
	struct cvmx_pip_xstat11_prtx_s        cn66xx;
};
typedef union cvmx_pip_xstat11_prtx cvmx_pip_xstat11_prtx_t;

/**
 * cvmx_pip_xstat1_prt#
 *
 * PIP_XSTAT1_PRTX = PIP_XSTAT_OCTS
 *
 */
union cvmx_pip_xstat1_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat1_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t reserved_48_63               : 16;
	uint64_t octs                         : 48; /**< Number of octets received by PIP (good and bad) */
#else
	uint64_t octs                         : 48;
	uint64_t reserved_48_63               : 16;
#endif
	} s;
	struct cvmx_pip_xstat1_prtx_s         cn63xx;
	struct cvmx_pip_xstat1_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat1_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat1_prtx cvmx_pip_xstat1_prtx_t;

/**
 * cvmx_pip_xstat2_prt#
 *
 * PIP_XSTAT2_PRTX = PIP_XSTAT_PKTS     / PIP_XSTAT_RAW
 *
 */
union cvmx_pip_xstat2_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat2_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t pkts                         : 32; /**< Number of packets processed by PIP */
	uint64_t raw                          : 32; /**< RAWFULL + RAWSCH Packets without an L1/L2 error
                                                         received by PIP per port */
#else
	uint64_t raw                          : 32;
	uint64_t pkts                         : 32;
#endif
	} s;
	struct cvmx_pip_xstat2_prtx_s         cn63xx;
	struct cvmx_pip_xstat2_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat2_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat2_prtx cvmx_pip_xstat2_prtx_t;

/**
 * cvmx_pip_xstat3_prt#
 *
 * PIP_XSTAT3_PRTX = PIP_XSTAT_BCST     / PIP_XSTAT_MCST
 *
 */
union cvmx_pip_xstat3_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat3_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t bcst                         : 32; /**< Number of indentified L2 broadcast packets
                                                         Does not include multicast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
	uint64_t mcst                         : 32; /**< Number of indentified L2 multicast packets
                                                         Does not include broadcast packets
                                                         Only includes packets whose parse mode is
                                                         SKIP_TO_L2. */
#else
	uint64_t mcst                         : 32;
	uint64_t bcst                         : 32;
#endif
	} s;
	struct cvmx_pip_xstat3_prtx_s         cn63xx;
	struct cvmx_pip_xstat3_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat3_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat3_prtx cvmx_pip_xstat3_prtx_t;

/**
 * cvmx_pip_xstat4_prt#
 *
 * PIP_XSTAT4_PRTX = PIP_XSTAT_HIST1    / PIP_XSTAT_HIST0
 *
 */
union cvmx_pip_xstat4_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat4_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h65to127                     : 32; /**< Number of 65-127B packets */
	uint64_t h64                          : 32; /**< Number of 1-64B packets */
#else
	uint64_t h64                          : 32;
	uint64_t h65to127                     : 32;
#endif
	} s;
	struct cvmx_pip_xstat4_prtx_s         cn63xx;
	struct cvmx_pip_xstat4_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat4_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat4_prtx cvmx_pip_xstat4_prtx_t;

/**
 * cvmx_pip_xstat5_prt#
 *
 * PIP_XSTAT5_PRTX = PIP_XSTAT_HIST3    / PIP_XSTAT_HIST2
 *
 */
union cvmx_pip_xstat5_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat5_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h256to511                    : 32; /**< Number of 256-511B packets */
	uint64_t h128to255                    : 32; /**< Number of 128-255B packets */
#else
	uint64_t h128to255                    : 32;
	uint64_t h256to511                    : 32;
#endif
	} s;
	struct cvmx_pip_xstat5_prtx_s         cn63xx;
	struct cvmx_pip_xstat5_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat5_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat5_prtx cvmx_pip_xstat5_prtx_t;

/**
 * cvmx_pip_xstat6_prt#
 *
 * PIP_XSTAT6_PRTX = PIP_XSTAT_HIST5    / PIP_XSTAT_HIST4
 *
 */
union cvmx_pip_xstat6_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat6_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t h1024to1518                  : 32; /**< Number of 1024-1518B packets */
	uint64_t h512to1023                   : 32; /**< Number of 512-1023B packets */
#else
	uint64_t h512to1023                   : 32;
	uint64_t h1024to1518                  : 32;
#endif
	} s;
	struct cvmx_pip_xstat6_prtx_s         cn63xx;
	struct cvmx_pip_xstat6_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat6_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat6_prtx cvmx_pip_xstat6_prtx_t;

/**
 * cvmx_pip_xstat7_prt#
 *
 * PIP_XSTAT7_PRTX = PIP_XSTAT_FCS      / PIP_XSTAT_HIST6
 *
 *
 * Notes:
 * DPI does not check FCS, therefore FCS will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore FCS will never increment on sRIO ports 40-47
 */
union cvmx_pip_xstat7_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat7_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t fcs                          : 32; /**< Number of packets with FCS or Align opcode errors */
	uint64_t h1519                        : 32; /**< Number of 1519-max packets */
#else
	uint64_t h1519                        : 32;
	uint64_t fcs                          : 32;
#endif
	} s;
	struct cvmx_pip_xstat7_prtx_s         cn63xx;
	struct cvmx_pip_xstat7_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat7_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat7_prtx cvmx_pip_xstat7_prtx_t;

/**
 * cvmx_pip_xstat8_prt#
 *
 * PIP_XSTAT8_PRTX = PIP_XSTAT_FRAG     / PIP_XSTAT_UNDER
 *
 *
 * Notes:
 * DPI does not check FCS, therefore FRAG will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore FRAG will never increment on sRIO ports 40-47
 */
union cvmx_pip_xstat8_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat8_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t frag                         : 32; /**< Number of packets with length < min and FCS error */
	uint64_t undersz                      : 32; /**< Number of packets with length < min */
#else
	uint64_t undersz                      : 32;
	uint64_t frag                         : 32;
#endif
	} s;
	struct cvmx_pip_xstat8_prtx_s         cn63xx;
	struct cvmx_pip_xstat8_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat8_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat8_prtx cvmx_pip_xstat8_prtx_t;

/**
 * cvmx_pip_xstat9_prt#
 *
 * PIP_XSTAT9_PRTX = PIP_XSTAT_JABBER   / PIP_XSTAT_OVER
 *
 *
 * Notes:
 * DPI does not check FCS, therefore JABBER will never increment on DPI ports 32-35
 * sRIO does not check FCS, therefore JABBER will never increment on sRIO ports 40-47 due to FCS errors
 * sRIO does use the JABBER opcode to communicate sRIO error, therefore JABBER can increment under the sRIO error conditions
 */
union cvmx_pip_xstat9_prtx {
	uint64_t u64;
	struct cvmx_pip_xstat9_prtx_s {
#ifdef __BIG_ENDIAN_BITFIELD
	uint64_t jabber                       : 32; /**< Number of packets with length > max and FCS error */
	uint64_t oversz                       : 32; /**< Number of packets with length > max */
#else
	uint64_t oversz                       : 32;
	uint64_t jabber                       : 32;
#endif
	} s;
	struct cvmx_pip_xstat9_prtx_s         cn63xx;
	struct cvmx_pip_xstat9_prtx_s         cn63xxp1;
	struct cvmx_pip_xstat9_prtx_s         cn66xx;
};
typedef union cvmx_pip_xstat9_prtx cvmx_pip_xstat9_prtx_t;

#endif
