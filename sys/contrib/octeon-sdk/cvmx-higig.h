/***********************license start***************
 * Copyright (c) 2003-2010  Cavium Inc. (support@cavium.com). All rights
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
 * @file
 *
 * Functions and typedefs for using Octeon in HiGig/HiGig+/HiGig2 mode over
 * XAUI.
 *
 * <hr>$Revision: 70030 $<hr>
 */

#ifndef __CVMX_HIGIG_H__
#define __CVMX_HIGIG_H__
#include "cvmx-wqe.h"
#include "cvmx-helper.h"
#include "cvmx-helper-util.h"

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct
{
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t start          : 8; /**< 8-bits of Preamble indicating start of frame */
            uint32_t hgi            : 2; /**< HiGig interface format indicator
                                            00 = Reserved
                                            01 = Pure preamble - IEEE standard framing of 10GE
                                            10 = XGS header - framing based on XGS family definition In this
                                                format, the default length of the header is 12 bytes and additional
                                                bytes are indicated by the HDR_EXT_LEN field
                                            11 = Reserved */
            uint32_t cng_high       : 1; /**< Congestion Bit High flag */
            uint32_t hdr_ext_len    : 3; /**< This field is valid only if the HGI field is a b'10' and it indicates the extension
                                            to the standard 12-bytes of XGS HiGig header. Each unit represents 4
                                            bytes, giving a total of 16 additional extension bytes. Value of b'101', b'110'
                                            and b'111' are reserved. For HGI field value of b'01' this field should be
                                            b'01'. For all other values of HGI it is don't care. */
            uint32_t src_modid_6    : 1; /**< This field is valid only if the HGI field is a b'10' and it represents Bit 6 of
                                            SRC_MODID (bits 4:0 are in Byte 4 and bit 5 is in Byte 9). For HGI field
                                            value of b'01' this field should be b'0'. For all other values of HGI it is don't
                                            care. */
            uint32_t dst_modid_6    : 1; /**< This field is valid only if the HGI field is a b'10' and it represents Bit 6 of
                                            DST_MODID (bits 4:0 are in Byte 7 and bit 5 is in Byte 9). ). For HGI field
                                            value of b'01' this field should be b'1'. For all other values of HGI it is don't
                                            care. */
            uint32_t vid_high       : 8; /**< 8-bits of the VLAN tag information */
            uint32_t vid_low        : 8; /**< 8 bits LSB of the VLAN tag information */
        } s;
    } dw0;
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t src_modid_low  : 5; /**< Bits 4:0 of Module ID of the source module on which the packet ingress (bit
                                            5 is in Byte 9 and bit 6 Is in Byte 1) */
            uint32_t opcode         : 3; /**< XGS HiGig op-code, indicating the type of packet
                                            000 =     Control frames used for CPU to CPU communications
                                            001 =     Unicast packet with destination resolved; The packet can be
                                                      either Layer 2 unicast packet or L3 unicast packet that was
                                                      routed in the ingress chip.
                                            010 =     Broadcast or unknown Unicast packet or unknown multicast,
                                                      destined to all members of the VLAN
                                            011 =     L2 Multicast packet, destined to all ports of the group indicated
                                                      in the L2MC_INDEX which is overlayed on DST_PORT/DST_MODID fields
                                            100 =     IP Multicast packet, destined to all ports of the group indicated
                                                      in the IPMC_INDEX which is overlayed on DST_PORT/DST_MODID fields
                                            101 =     Reserved
                                            110 =     Reserved
                                            111 =     Reserved */
            uint32_t pfm            : 2; /**< Three Port Filtering Modes (0, 1, 2) used in handling registed/unregistered
                                            multicast (unknown L2 multicast and IPMC) packets. This field is used
                                            when OPCODE is 011 or 100 Semantics of PFM bits are as follows;
                                            For registered L2 multicast packets:
                                                PFM= 0 - Flood to VLAN
                                                PFM= 1 or 2 - Send to group members in the L2MC table
                                            For unregistered L2 multicast packets:
                                                PFM= 0 or 1 - Flood to VLAN
                                                PFM= 2 - Drop the packet */
            uint32_t src_port_tgid  : 6; /**< If the MSB of this field is set, then it indicates the LAG the packet ingressed
                                            on, else it represents the physical port the packet ingressed on. */
            uint32_t dst_port       : 5; /**< Port number of destination port on which the packet needs to egress. */
            uint32_t priority       : 3; /**< This is the internal priority of the packet. This internal priority will go through
                                            COS_SEL mapping registers to map to the actual MMU queues. */
            uint32_t header_type    : 2; /**< Indicates the format of the next 4 bytes of the XGS HiGig header
                                            00 = Overlay 1 (default)
                                            01 = Overlay 2 (Classification Tag)
                                            10 = Reserved
                                            11 = Reserved */
            uint32_t cng_low        : 1; /**< Semantics of CNG_HIGH and CNG_LOW are as follows: The following
                                            encodings are to make it backward compatible:
                                            [CNG_HIGH, CNG_LOW] - COLOR
                                            [0, 0] - Packet is green
                                            [0, 1] - Packet is red
                                            [1, 1] - Packet is yellow
                                            [1, 0] - Undefined */
            uint32_t dst_modid_low  : 5; /**< Bits [4-: 0] of Module ID of the destination port on which the packet needs to egress. */
        } s;
    } dw1;
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t dst_t          : 1; /**< Destination Trunk: Indicates that the destination port is a member of a trunk
                                            group. */
            uint32_t dst_tgid       : 3; /**< Destination Trunk Group ID: Trunk group ID of the destination port. The
                                            DO_NOT_LEARN bit is overlaid on the second bit of this field. */
            uint32_t ingress_tagged : 1; /**< Ingress Tagged: Indicates whether the packet was tagged when it originally
                                            ingressed the system. */
            uint32_t mirror_only    : 1; /**< Mirror Only: XGS 1/2 mode: Indicates that the packet was switched and only
                                            needs to be mirrored. */
            uint32_t mirror_done    : 1; /**< Mirroring Done: XGS1/2 mode: Indicates that the packet was mirrored and
                                            may still need to be switched. */
            uint32_t mirror         : 1; /**< Mirror: XGS3 mode: a mirror copy packet. XGS1/2 mode: Indicates that the
                                            packet was switched and only needs to be mirrored. */

            uint32_t src_modid_5    : 1; /**< Source Module ID: Bit 5 of Src_ModID (bits 4:0 are in byte 4 and bit 6 is in
                                            byte 1) */
            uint32_t dst_modid_5    : 1; /**< Destination Module ID: Bit 5 of Dst_ModID (bits 4:0 are in byte 7 and bit 6
                                            is in byte 1) */
            uint32_t l3             : 1; /**< L3: Indicates that the packet is L3 switched */
            uint32_t label_present  : 1; /**< Label Present: Indicates that header contains a 20-bit VC label: HiGig+
                                            added field. */
            uint32_t vc_label_16_19 : 4; /**< VC Label: Bits 19:16 of VC label: HiGig+ added field */
            uint32_t vc_label_0_15  : 16;/**< VC Label: Bits 15:0 of VC label: HiGig+ added field */
        } o1;
        struct
        {
            uint32_t classification : 16; /**< Classification tag information from the HiGig device FFP */
            uint32_t reserved_0_15  : 16;

        } o2;
    } dw2;
} cvmx_higig_header_t;

typedef struct
{
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t k_sop          : 8;  /**< The delimiter indicating the start of a packet transmission */
            uint32_t reserved_21_23 : 3;
            uint32_t mcst           : 1;  /**< MCST indicates whether the packet should be unicast or
                                            multicast forwarded through the XGS switching fabric
                                            - 0: Unicast
                                            - 1: Mulitcast */
            uint32_t tc             : 4;  /**< Traffic Class [3:0] indicates the distinctive Quality of Service (QoS)
                                            the switching fabric will provide when forwarding the packet
                                            through the fabric */
            uint32_t dst_modid_mgid : 8;  /**< When MCST=0, this field indicates the destination XGS module to
                                            which the packet will be delivered. When MCST=1, this field indicates
                                            higher order bits of the Multicast Group ID. */
            uint32_t dst_pid_mgid   : 8;  /**< When MCST=0, this field indicates a port associated with the
                                            module indicated by the DST_MODID, through which the packet
                                            will exit the system. When MCST=1, this field indicates lower order
                                            bits of the Multicast Group ID */
        } s;
    } dw0;
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t src_modid      : 8;  /**< Source Module ID indicates the source XGS module from which
                                            the packet is originated. (It can also be used for the fabric multicast
                                            load balancing purpose.) */
            uint32_t src_pid        : 8;  /**< Source Port ID indicates a port associated with the module
                                            indicated by the SRC_MODID, through which the packet has
                                            entered the system */
            uint32_t lbid           : 8;  /**< Load Balancing ID indicates a packet flow hashing index
                                            computed by the ingress XGS module for statistical distribution of
                                            packet flows through a multipath fabric */
            uint32_t dp             : 2;  /**< Drop Precedence indicates the traffic rate violation status of the
                                            packet measured by the ingress module.
                                            - 00: GREEN
                                            - 01: RED
                                            - 10: Reserved
                                            - 11: Yellow */
            uint32_t reserved_3_5   : 3;
            uint32_t ppd_type       : 3;  /**< Packet Processing Descriptor Type
                                            - 000: PPD Overlay1
                                            - 001: PPD Overlay2
                                            - 010~111: Reserved */
        } s;
    } dw1;
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t dst_t          : 1;  /**< Destination Trunk: Indicates that the destination port is a member of a trunk
                                            group. */
            uint32_t dst_tgid       : 3;  /**< Destination Trunk Group ID: Trunk group ID of the destination port. The
                                            DO_NOT_LEARN bit is overlaid on the second bit of this field. */
            uint32_t ingress_tagged : 1;  /**< Ingress Tagged: Indicates whether the packet was tagged when it originally
                                            ingressed the system. */
            uint32_t mirror_only    : 1;  /**< Mirror Only: XGS 1/2 mode: Indicates that the packet was switched and only
                                            needs to be mirrored. */
            uint32_t mirror_done    : 1;  /**< Mirroring Done: XGS1/2 mode: Indicates that the packet was mirrored and
                                            may still need to be switched. */
            uint32_t mirror         : 1;  /**< Mirror: XGS3 mode: a mirror copy packet. XGS1/2 mode: Indicates that the
                                            packet was switched and only needs to be mirrored. */
            uint32_t reserved_22_23 : 2;
            uint32_t l3             : 1;  /**< L3: Indicates that the packet is L3 switched */
            uint32_t label_present  : 1;  /**< Label Present: Indicates that header contains a 20-bit VC label: HiGig+
                                            added field. */
            uint32_t vc_label       : 20; /**< Refer to the HiGig+ Architecture Specification */
        } o1;
        struct
        {
            uint32_t classification : 16; /**< Classification tag information from the HiGig device FFP */
            uint32_t reserved_0_15  : 16;
        } o2;
    } dw2;
    union
    {
        uint32_t u32;
        struct
        {
            uint32_t vid            : 16; /**< VLAN tag information */
            uint32_t pfm            : 2;  /**< Three Port Filtering Modes (0, 1, 2) used in handling registed/unregistered
                                            multicast (unknown L2 multicast and IPMC) packets. This field is used
                                            when OPCODE is 011 or 100 Semantics of PFM bits are as follows;
                                            For registered L2 multicast packets:
                                                PFM= 0 - Flood to VLAN
                                                PFM= 1 or 2 - Send to group members in the L2MC table
                                            For unregistered L2 multicast packets:
                                                PFM= 0 or 1 - Flood to VLAN
                                                PFM= 2 - Drop the packet */
            uint32_t src_t          : 1;  /**< If the MSB of this field is set, then it indicates the LAG the packet ingressed
                                            on, else it represents the physical port the packet ingressed on. */
            uint32_t reserved_11_12 : 2;
            uint32_t opcode         : 3;  /**< XGS HiGig op-code, indicating the type of packet
                                            000 =     Control frames used for CPU to CPU communications
                                            001 =     Unicast packet with destination resolved; The packet can be
                                                      either Layer 2 unicast packet or L3 unicast packet that was
                                                      routed in the ingress chip.
                                            010 =     Broadcast or unknown Unicast packet or unknown multicast,
                                                      destined to all members of the VLAN
                                            011 =     L2 Multicast packet, destined to all ports of the group indicated
                                                      in the L2MC_INDEX which is overlayed on DST_PORT/DST_MODID fields
                                            100 =     IP Multicast packet, destined to all ports of the group indicated
                                                      in the IPMC_INDEX which is overlayed on DST_PORT/DST_MODID fields
                                            101 =     Reserved
                                            110 =     Reserved
                                            111 =     Reserved */
            uint32_t hdr_ext_len    : 3;  /**< This field is valid only if the HGI field is a b'10' and it indicates the extension
                                            to the standard 12-bytes of XGS HiGig header. Each unit represents 4
                                            bytes, giving a total of 16 additional extension bytes. Value of b'101', b'110'
                                            and b'111' are reserved. For HGI field value of b'01' this field should be
                                            b'01'. For all other values of HGI it is don't care. */
            uint32_t reserved_0_4   : 5;
        } s;
    } dw3;
} cvmx_higig2_header_t;


/**
 * Initialize the HiGig aspects of a XAUI interface. This function
 * should be called before the cvmx-helper generic init.
 *
 * @param interface Interface to initialize HiGig on (0-1)
 * @param enable_higig2
 *                  Non zero to enable HiGig2 support. Zero to support HiGig
 *                  and HiGig+.
 *
 * @return Zero on success, negative on failure
 */
static inline int cvmx_higig_initialize(int interface, int enable_higig2)
{
    cvmx_pip_prt_cfgx_t pip_prt_cfg;
    cvmx_gmxx_rxx_udd_skp_t gmx_rx_udd_skp;
    cvmx_gmxx_txx_min_pkt_t gmx_tx_min_pkt;
    cvmx_gmxx_txx_append_t gmx_tx_append;
    cvmx_gmxx_tx_ifg_t gmx_tx_ifg;
    cvmx_gmxx_tx_ovr_bp_t gmx_tx_ovr_bp;
    cvmx_gmxx_rxx_frm_ctl_t gmx_rx_frm_ctl;
    cvmx_gmxx_tx_xaui_ctl_t gmx_tx_xaui_ctl;
    int i, pknd;
    int header_size = (enable_higig2) ? 16 : 12;

    /* Setup PIP to handle HiGig */
    if (octeon_has_feature(OCTEON_FEATURE_PKND))
        pknd = cvmx_helper_get_pknd(interface, 0);
    else
        pknd = interface*16;
    pip_prt_cfg.u64 = cvmx_read_csr(CVMX_PIP_PRT_CFGX(pknd));
    pip_prt_cfg.s.dsa_en = 0;
    pip_prt_cfg.s.higig_en = 1;
    pip_prt_cfg.s.hg_qos = 1;
    pip_prt_cfg.s.skip = header_size;
    cvmx_write_csr(CVMX_PIP_PRT_CFGX(pknd), pip_prt_cfg.u64);

    /* Setup some sample QoS defaults. These can be changed later */
    if (!OCTEON_IS_MODEL(OCTEON_CN68XX))
    {
        for (i=0; i<64; i++)
        {
            cvmx_pip_hg_pri_qos_t pip_hg_pri_qos;
            pip_hg_pri_qos.u64 = 0;
            pip_hg_pri_qos.s.up_qos = 1;
            pip_hg_pri_qos.s.pri = i;
            pip_hg_pri_qos.s.qos = i&7;
            cvmx_write_csr(CVMX_PIP_HG_PRI_QOS, pip_hg_pri_qos.u64);
        }
    }

    /* Setup GMX RX to treat the HiGig header as user data to ignore */
    gmx_rx_udd_skp.u64 = cvmx_read_csr(CVMX_GMXX_RXX_UDD_SKP(0, interface));
    gmx_rx_udd_skp.s.len = header_size;
    gmx_rx_udd_skp.s.fcssel = 0;
    cvmx_write_csr(CVMX_GMXX_RXX_UDD_SKP(0, interface), gmx_rx_udd_skp.u64);

    /* Disable GMX preamble checking */
    gmx_rx_frm_ctl.u64 = cvmx_read_csr(CVMX_GMXX_RXX_FRM_CTL(0, interface));
    gmx_rx_frm_ctl.s.pre_chk = 0;
    cvmx_write_csr(CVMX_GMXX_RXX_FRM_CTL(0, interface), gmx_rx_frm_ctl.u64);

    /* Setup GMX TX to pad properly min sized packets */
    gmx_tx_min_pkt.u64 = cvmx_read_csr(CVMX_GMXX_TXX_MIN_PKT(0, interface));
    gmx_tx_min_pkt.s.min_size = 59 + header_size;
    cvmx_write_csr(CVMX_GMXX_TXX_MIN_PKT(0, interface), gmx_tx_min_pkt.u64);

    /* Setup GMX TX to not add a preamble */
    gmx_tx_append.u64 = cvmx_read_csr(CVMX_GMXX_TXX_APPEND(0, interface));
    gmx_tx_append.s.preamble = 0;
    cvmx_write_csr(CVMX_GMXX_TXX_APPEND(0, interface), gmx_tx_append.u64);

    /* Reduce the inter frame gap to 8 bytes */
    gmx_tx_ifg.u64 = cvmx_read_csr(CVMX_GMXX_TX_IFG(interface));
    gmx_tx_ifg.s.ifg1 = 4;
    gmx_tx_ifg.s.ifg2 = 4;
    cvmx_write_csr(CVMX_GMXX_TX_IFG(interface), gmx_tx_ifg.u64);

    /* Disable GMX backpressure */
    gmx_tx_ovr_bp.u64 = cvmx_read_csr(CVMX_GMXX_TX_OVR_BP(interface));
    gmx_tx_ovr_bp.s.bp = 0;
    gmx_tx_ovr_bp.s.en = 0xf;
    gmx_tx_ovr_bp.s.ign_full = 0xf;
    cvmx_write_csr(CVMX_GMXX_TX_OVR_BP(interface), gmx_tx_ovr_bp.u64);

    if (enable_higig2)
    {
        /* Enable HiGig2 support and forwarding of virtual port backpressure
            to PKO */
        cvmx_gmxx_hg2_control_t gmx_hg2_control;
        gmx_hg2_control.u64 = cvmx_read_csr(CVMX_GMXX_HG2_CONTROL(interface));
        gmx_hg2_control.s.hg2rx_en = 1;
        gmx_hg2_control.s.hg2tx_en = 1;
        gmx_hg2_control.s.logl_en = 0xffff;
        gmx_hg2_control.s.phys_en = 1;
        cvmx_write_csr(CVMX_GMXX_HG2_CONTROL(interface), gmx_hg2_control.u64);
    }

    /* Enable HiGig */
    gmx_tx_xaui_ctl.u64 = cvmx_read_csr(CVMX_GMXX_TX_XAUI_CTL(interface));
    gmx_tx_xaui_ctl.s.hg_en = 1;
    cvmx_write_csr(CVMX_GMXX_TX_XAUI_CTL(interface), gmx_tx_xaui_ctl.u64);

    return 0;
}

#ifdef	__cplusplus
}
#endif

#endif //  __CVMX_HIGIG_H__
