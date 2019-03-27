/*
 * Copyright 2008-2012 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


/******************************************************************************
 @File          fm_kg.h

 @Description   FM KG private header
*//***************************************************************************/
#ifndef __FM_KG_H
#define __FM_KG_H

#include "std_ext.h"

/***********************************************************************/
/*          Keygen defines                                             */
/***********************************************************************/
/* maskes */
#if (DPAA_VERSION >= 11)
#define KG_SCH_VSP_SHIFT_MASK                   0x0003f000
#define KG_SCH_OM_VSPE                          0x00000001
#define KG_SCH_VSP_NO_KSP_EN                    0x80000000

#define MAX_SP_SHIFT                            23
#define KG_SCH_VSP_MASK_SHIFT                   12
#define KG_SCH_VSP_SHIFT                        24
#endif /* (DPAA_VERSION >= 11) */

typedef uint32_t t_KnownFieldsMasks;
#define KG_SCH_KN_PORT_ID                   0x80000000
#define KG_SCH_KN_MACDST                    0x40000000
#define KG_SCH_KN_MACSRC                    0x20000000
#define KG_SCH_KN_TCI1                      0x10000000
#define KG_SCH_KN_TCI2                      0x08000000
#define KG_SCH_KN_ETYPE                     0x04000000
#define KG_SCH_KN_PPPSID                    0x02000000
#define KG_SCH_KN_PPPID                     0x01000000
#define KG_SCH_KN_MPLS1                     0x00800000
#define KG_SCH_KN_MPLS2                     0x00400000
#define KG_SCH_KN_MPLS_LAST                 0x00200000
#define KG_SCH_KN_IPSRC1                    0x00100000
#define KG_SCH_KN_IPDST1                    0x00080000
#define KG_SCH_KN_PTYPE1                    0x00040000
#define KG_SCH_KN_IPTOS_TC1                 0x00020000
#define KG_SCH_KN_IPV6FL1                   0x00010000
#define KG_SCH_KN_IPSRC2                    0x00008000
#define KG_SCH_KN_IPDST2                    0x00004000
#define KG_SCH_KN_PTYPE2                    0x00002000
#define KG_SCH_KN_IPTOS_TC2                 0x00001000
#define KG_SCH_KN_IPV6FL2                   0x00000800
#define KG_SCH_KN_GREPTYPE                  0x00000400
#define KG_SCH_KN_IPSEC_SPI                 0x00000200
#define KG_SCH_KN_IPSEC_NH                  0x00000100
#define KG_SCH_KN_IPPID                     0x00000080
#define KG_SCH_KN_L4PSRC                    0x00000004
#define KG_SCH_KN_L4PDST                    0x00000002
#define KG_SCH_KN_TFLG                      0x00000001

typedef uint8_t t_GenericCodes;
#define KG_SCH_GEN_SHIM1                       0x70
#define KG_SCH_GEN_DEFAULT                     0x10
#define KG_SCH_GEN_PARSE_RESULT_N_FQID         0x20
#define KG_SCH_GEN_START_OF_FRM                0x40
#define KG_SCH_GEN_SHIM2                       0x71
#define KG_SCH_GEN_IP_PID_NO_V                 0x72
#define KG_SCH_GEN_ETH                         0x03
#define KG_SCH_GEN_ETH_NO_V                    0x73
#define KG_SCH_GEN_SNAP                        0x04
#define KG_SCH_GEN_SNAP_NO_V                   0x74
#define KG_SCH_GEN_VLAN1                       0x05
#define KG_SCH_GEN_VLAN1_NO_V                  0x75
#define KG_SCH_GEN_VLAN2                       0x06
#define KG_SCH_GEN_VLAN2_NO_V                  0x76
#define KG_SCH_GEN_ETH_TYPE                    0x07
#define KG_SCH_GEN_ETH_TYPE_NO_V               0x77
#define KG_SCH_GEN_PPP                         0x08
#define KG_SCH_GEN_PPP_NO_V                    0x78
#define KG_SCH_GEN_MPLS1                       0x09
#define KG_SCH_GEN_MPLS2                       0x19
#define KG_SCH_GEN_MPLS3                       0x29
#define KG_SCH_GEN_MPLS1_NO_V                  0x79
#define KG_SCH_GEN_MPLS_LAST                   0x0a
#define KG_SCH_GEN_MPLS_LAST_NO_V              0x7a
#define KG_SCH_GEN_IPV4                        0x0b
#define KG_SCH_GEN_IPV6                        0x1b
#define KG_SCH_GEN_L3_NO_V                     0x7b
#define KG_SCH_GEN_IPV4_TUNNELED               0x0c
#define KG_SCH_GEN_IPV6_TUNNELED               0x1c
#define KG_SCH_GEN_MIN_ENCAP                   0x2c
#define KG_SCH_GEN_IP2_NO_V                    0x7c
#define KG_SCH_GEN_GRE                         0x0d
#define KG_SCH_GEN_GRE_NO_V                    0x7d
#define KG_SCH_GEN_TCP                         0x0e
#define KG_SCH_GEN_UDP                         0x1e
#define KG_SCH_GEN_IPSEC_AH                    0x2e
#define KG_SCH_GEN_SCTP                        0x3e
#define KG_SCH_GEN_DCCP                        0x4e
#define KG_SCH_GEN_IPSEC_ESP                   0x6e
#define KG_SCH_GEN_L4_NO_V                     0x7e
#define KG_SCH_GEN_NEXTHDR                     0x7f
/* shifts */
#define KG_SCH_PP_SHIFT_HIGH_SHIFT          27
#define KG_SCH_PP_SHIFT_LOW_SHIFT           12
#define KG_SCH_PP_MASK_SHIFT                16
#define KG_SCH_MODE_CCOBASE_SHIFT           24
#define KG_SCH_DEF_MAC_ADDR_SHIFT           30
#define KG_SCH_DEF_TCI_SHIFT                28
#define KG_SCH_DEF_ENET_TYPE_SHIFT          26
#define KG_SCH_DEF_PPP_SESSION_ID_SHIFT     24
#define KG_SCH_DEF_PPP_PROTOCOL_ID_SHIFT    22
#define KG_SCH_DEF_MPLS_LABEL_SHIFT         20
#define KG_SCH_DEF_IP_ADDR_SHIFT            18
#define KG_SCH_DEF_PROTOCOL_TYPE_SHIFT      16
#define KG_SCH_DEF_IP_TOS_TC_SHIFT          14
#define KG_SCH_DEF_IPV6_FLOW_LABEL_SHIFT    12
#define KG_SCH_DEF_IPSEC_SPI_SHIFT          10
#define KG_SCH_DEF_L4_PORT_SHIFT            8
#define KG_SCH_DEF_TCP_FLAG_SHIFT           6
#define KG_SCH_HASH_CONFIG_SHIFT_SHIFT      24
#define KG_SCH_GEN_MASK_SHIFT               16
#define KG_SCH_GEN_HT_SHIFT                 8
#define KG_SCH_GEN_SIZE_SHIFT               24
#define KG_SCH_GEN_DEF_SHIFT                29
#define FM_PCD_KG_KGAR_NUM_SHIFT            16

/* others */
#define NUM_OF_SW_DEFAULTS                  3
#define MAX_PP_SHIFT                        23
#define MAX_KG_SCH_SIZE                     16
#define MASK_FOR_GENERIC_BASE_ID            0x20
#define MAX_HASH_SHIFT                      40
#define MAX_KG_SCH_FQID_BIT_OFFSET          31
#define MAX_KG_SCH_PP_BIT_OFFSET            15
#define MAX_DIST_FQID_SHIFT                 23

#define GET_MASK_SEL_SHIFT(shift,i)                 \
switch (i) {                                        \
    case (0):shift = 26;break;                      \
    case (1):shift = 20;break;                      \
    case (2):shift = 10;break;                      \
    case (3):shift = 4;break;                       \
    default:                                        \
    RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);   \
}

#define GET_MASK_OFFSET_SHIFT(shift,i)              \
switch (i) {                                        \
    case (0):shift = 16;break;                      \
    case (1):shift = 0;break;                       \
    case (2):shift = 28;break;                      \
    case (3):shift = 24;break;                      \
    default:                                        \
    RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);   \
}

#define GET_MASK_SHIFT(shift,i)                     \
switch (i) {                                        \
    case (0):shift = 24;break;                      \
    case (1):shift = 16;break;                      \
    case (2):shift = 8;break;                       \
    case (3):shift = 0;break;                       \
    default:                                        \
    RETURN_ERROR(MAJOR, E_INVALID_VALUE, NO_MSG);   \
}

/***********************************************************************/
/*          Keygen defines                                             */
/***********************************************************************/

#define KG_DOUBLE_MEANING_REGS_OFFSET           0x100
#define NO_VALIDATION                           0x70
#define KG_ACTION_REG_TO                        1024
#define KG_MAX_PROFILE                          255
#define SCHEME_ALWAYS_DIRECT                    0xFFFFFFFF


#endif /* __FM_KG_H */
