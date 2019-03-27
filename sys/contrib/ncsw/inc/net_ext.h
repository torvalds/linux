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


/**************************************************************************//**
 @File          net_ext.h

 @Description   This file contains common and general netcomm headers definitions.
*//***************************************************************************/
#ifndef __NET_EXT_H
#define __NET_EXT_H

#include "std_ext.h"


typedef uint8_t headerFieldPpp_t;

#define NET_HEADER_FIELD_PPP_PID                        (1)
#define NET_HEADER_FIELD_PPP_COMPRESSED                 (NET_HEADER_FIELD_PPP_PID << 1)
#define NET_HEADER_FIELD_PPP_ALL_FIELDS                 ((NET_HEADER_FIELD_PPP_PID << 2) - 1)


typedef uint8_t headerFieldPppoe_t;

#define NET_HEADER_FIELD_PPPoE_VER                      (1)
#define NET_HEADER_FIELD_PPPoE_TYPE                     (NET_HEADER_FIELD_PPPoE_VER << 1)
#define NET_HEADER_FIELD_PPPoE_CODE                     (NET_HEADER_FIELD_PPPoE_VER << 2)
#define NET_HEADER_FIELD_PPPoE_SID                      (NET_HEADER_FIELD_PPPoE_VER << 3)
#define NET_HEADER_FIELD_PPPoE_LEN                      (NET_HEADER_FIELD_PPPoE_VER << 4)
#define NET_HEADER_FIELD_PPPoE_SESSION                  (NET_HEADER_FIELD_PPPoE_VER << 5)
#define NET_HEADER_FIELD_PPPoE_PID                      (NET_HEADER_FIELD_PPPoE_VER << 6)
#define NET_HEADER_FIELD_PPPoE_ALL_FIELDS               ((NET_HEADER_FIELD_PPPoE_VER << 7) - 1)

#define NET_HEADER_FIELD_PPPMUX_PID                     (1)
#define NET_HEADER_FIELD_PPPMUX_CKSUM                   (NET_HEADER_FIELD_PPPMUX_PID << 1)
#define NET_HEADER_FIELD_PPPMUX_COMPRESSED              (NET_HEADER_FIELD_PPPMUX_PID << 2)
#define NET_HEADER_FIELD_PPPMUX_ALL_FIELDS              ((NET_HEADER_FIELD_PPPMUX_PID << 3) - 1)

#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF            (1)
#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_LXT            (NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF << 1)
#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_LEN            (NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF << 2)
#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_PID            (NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF << 3)
#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_USE_PID        (NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF << 4)
#define NET_HEADER_FIELD_PPPMUX_SUBFRAME_ALL_FIELDS     ((NET_HEADER_FIELD_PPPMUX_SUBFRAME_PFF << 5) - 1)


typedef uint8_t headerFieldEth_t;

#define NET_HEADER_FIELD_ETH_DA                         (1)
#define NET_HEADER_FIELD_ETH_SA                         (NET_HEADER_FIELD_ETH_DA << 1)
#define NET_HEADER_FIELD_ETH_LENGTH                     (NET_HEADER_FIELD_ETH_DA << 2)
#define NET_HEADER_FIELD_ETH_TYPE                       (NET_HEADER_FIELD_ETH_DA << 3)
#define NET_HEADER_FIELD_ETH_FINAL_CKSUM                (NET_HEADER_FIELD_ETH_DA << 4)
#define NET_HEADER_FIELD_ETH_PADDING                    (NET_HEADER_FIELD_ETH_DA << 5)
#define NET_HEADER_FIELD_ETH_ALL_FIELDS                 ((NET_HEADER_FIELD_ETH_DA << 6) - 1)

#define NET_HEADER_FIELD_ETH_ADDR_SIZE                 6

typedef uint16_t headerFieldIp_t;

#define NET_HEADER_FIELD_IP_VER                         (1)
#define NET_HEADER_FIELD_IP_DSCP                        (NET_HEADER_FIELD_IP_VER << 2)
#define NET_HEADER_FIELD_IP_ECN                         (NET_HEADER_FIELD_IP_VER << 3)
#define NET_HEADER_FIELD_IP_PROTO                       (NET_HEADER_FIELD_IP_VER << 4)

#define NET_HEADER_FIELD_IP_PROTO_SIZE                  1

typedef uint16_t headerFieldIpv4_t;

#define NET_HEADER_FIELD_IPv4_VER                       (1)
#define NET_HEADER_FIELD_IPv4_HDR_LEN                   (NET_HEADER_FIELD_IPv4_VER << 1)
#define NET_HEADER_FIELD_IPv4_TOS                       (NET_HEADER_FIELD_IPv4_VER << 2)
#define NET_HEADER_FIELD_IPv4_TOTAL_LEN                 (NET_HEADER_FIELD_IPv4_VER << 3)
#define NET_HEADER_FIELD_IPv4_ID                        (NET_HEADER_FIELD_IPv4_VER << 4)
#define NET_HEADER_FIELD_IPv4_FLAG_D                    (NET_HEADER_FIELD_IPv4_VER << 5)
#define NET_HEADER_FIELD_IPv4_FLAG_M                    (NET_HEADER_FIELD_IPv4_VER << 6)
#define NET_HEADER_FIELD_IPv4_OFFSET                    (NET_HEADER_FIELD_IPv4_VER << 7)
#define NET_HEADER_FIELD_IPv4_TTL                       (NET_HEADER_FIELD_IPv4_VER << 8)
#define NET_HEADER_FIELD_IPv4_PROTO                     (NET_HEADER_FIELD_IPv4_VER << 9)
#define NET_HEADER_FIELD_IPv4_CKSUM                     (NET_HEADER_FIELD_IPv4_VER << 10)
#define NET_HEADER_FIELD_IPv4_SRC_IP                    (NET_HEADER_FIELD_IPv4_VER << 11)
#define NET_HEADER_FIELD_IPv4_DST_IP                    (NET_HEADER_FIELD_IPv4_VER << 12)
#define NET_HEADER_FIELD_IPv4_OPTS                      (NET_HEADER_FIELD_IPv4_VER << 13)
#define NET_HEADER_FIELD_IPv4_OPTS_COUNT                (NET_HEADER_FIELD_IPv4_VER << 14)
#define NET_HEADER_FIELD_IPv4_ALL_FIELDS                ((NET_HEADER_FIELD_IPv4_VER << 15) - 1)

#define NET_HEADER_FIELD_IPv4_ADDR_SIZE                 4
#define NET_HEADER_FIELD_IPv4_PROTO_SIZE                1


typedef uint8_t headerFieldIpv6_t;

#define NET_HEADER_FIELD_IPv6_VER                       (1)
#define NET_HEADER_FIELD_IPv6_TC                        (NET_HEADER_FIELD_IPv6_VER << 1)
#define NET_HEADER_FIELD_IPv6_SRC_IP                    (NET_HEADER_FIELD_IPv6_VER << 2)
#define NET_HEADER_FIELD_IPv6_DST_IP                    (NET_HEADER_FIELD_IPv6_VER << 3)
#define NET_HEADER_FIELD_IPv6_NEXT_HDR                  (NET_HEADER_FIELD_IPv6_VER << 4)
#define NET_HEADER_FIELD_IPv6_FL                        (NET_HEADER_FIELD_IPv6_VER << 5)
#define NET_HEADER_FIELD_IPv6_HOP_LIMIT                 (NET_HEADER_FIELD_IPv6_VER << 6)
#define NET_HEADER_FIELD_IPv6_ALL_FIELDS                ((NET_HEADER_FIELD_IPv6_VER << 7) - 1)

#define NET_HEADER_FIELD_IPv6_ADDR_SIZE                 16
#define NET_HEADER_FIELD_IPv6_NEXT_HDR_SIZE             1

#define NET_HEADER_FIELD_ICMP_TYPE                      (1)
#define NET_HEADER_FIELD_ICMP_CODE                      (NET_HEADER_FIELD_ICMP_TYPE << 1)
#define NET_HEADER_FIELD_ICMP_CKSUM                     (NET_HEADER_FIELD_ICMP_TYPE << 2)
#define NET_HEADER_FIELD_ICMP_ID                        (NET_HEADER_FIELD_ICMP_TYPE << 3)
#define NET_HEADER_FIELD_ICMP_SQ_NUM                    (NET_HEADER_FIELD_ICMP_TYPE << 4)
#define NET_HEADER_FIELD_ICMP_ALL_FIELDS                ((NET_HEADER_FIELD_ICMP_TYPE << 5) - 1)

#define NET_HEADER_FIELD_ICMP_CODE_SIZE                 1
#define NET_HEADER_FIELD_ICMP_TYPE_SIZE                 1

#define NET_HEADER_FIELD_IGMP_VERSION                   (1)
#define NET_HEADER_FIELD_IGMP_TYPE                      (NET_HEADER_FIELD_IGMP_VERSION << 1)
#define NET_HEADER_FIELD_IGMP_CKSUM                     (NET_HEADER_FIELD_IGMP_VERSION << 2)
#define NET_HEADER_FIELD_IGMP_DATA                      (NET_HEADER_FIELD_IGMP_VERSION << 3)
#define NET_HEADER_FIELD_IGMP_ALL_FIELDS                ((NET_HEADER_FIELD_IGMP_VERSION << 4) - 1)


typedef uint16_t headerFieldTcp_t;

#define NET_HEADER_FIELD_TCP_PORT_SRC                   (1)
#define NET_HEADER_FIELD_TCP_PORT_DST                   (NET_HEADER_FIELD_TCP_PORT_SRC << 1)
#define NET_HEADER_FIELD_TCP_SEQ                        (NET_HEADER_FIELD_TCP_PORT_SRC << 2)
#define NET_HEADER_FIELD_TCP_ACK                        (NET_HEADER_FIELD_TCP_PORT_SRC << 3)
#define NET_HEADER_FIELD_TCP_OFFSET                     (NET_HEADER_FIELD_TCP_PORT_SRC << 4)
#define NET_HEADER_FIELD_TCP_FLAGS                      (NET_HEADER_FIELD_TCP_PORT_SRC << 5)
#define NET_HEADER_FIELD_TCP_WINDOW                     (NET_HEADER_FIELD_TCP_PORT_SRC << 6)
#define NET_HEADER_FIELD_TCP_CKSUM                      (NET_HEADER_FIELD_TCP_PORT_SRC << 7)
#define NET_HEADER_FIELD_TCP_URGPTR                     (NET_HEADER_FIELD_TCP_PORT_SRC << 8)
#define NET_HEADER_FIELD_TCP_OPTS                       (NET_HEADER_FIELD_TCP_PORT_SRC << 9)
#define NET_HEADER_FIELD_TCP_OPTS_COUNT                 (NET_HEADER_FIELD_TCP_PORT_SRC << 10)
#define NET_HEADER_FIELD_TCP_ALL_FIELDS                 ((NET_HEADER_FIELD_TCP_PORT_SRC << 11) - 1)

#define NET_HEADER_FIELD_TCP_PORT_SIZE                  2


typedef uint8_t headerFieldSctp_t;

#define NET_HEADER_FIELD_SCTP_PORT_SRC                  (1)
#define NET_HEADER_FIELD_SCTP_PORT_DST                  (NET_HEADER_FIELD_SCTP_PORT_SRC << 1)
#define NET_HEADER_FIELD_SCTP_VER_TAG                   (NET_HEADER_FIELD_SCTP_PORT_SRC << 2)
#define NET_HEADER_FIELD_SCTP_CKSUM                     (NET_HEADER_FIELD_SCTP_PORT_SRC << 3)
#define NET_HEADER_FIELD_SCTP_ALL_FIELDS                ((NET_HEADER_FIELD_SCTP_PORT_SRC << 4) - 1)

#define NET_HEADER_FIELD_SCTP_PORT_SIZE                 2

typedef uint8_t headerFieldDccp_t;

#define NET_HEADER_FIELD_DCCP_PORT_SRC                  (1)
#define NET_HEADER_FIELD_DCCP_PORT_DST                  (NET_HEADER_FIELD_DCCP_PORT_SRC << 1)
#define NET_HEADER_FIELD_DCCP_ALL_FIELDS                ((NET_HEADER_FIELD_DCCP_PORT_SRC << 2) - 1)

#define NET_HEADER_FIELD_DCCP_PORT_SIZE                 2


typedef uint8_t headerFieldUdp_t;

#define NET_HEADER_FIELD_UDP_PORT_SRC                   (1)
#define NET_HEADER_FIELD_UDP_PORT_DST                   (NET_HEADER_FIELD_UDP_PORT_SRC << 1)
#define NET_HEADER_FIELD_UDP_LEN                        (NET_HEADER_FIELD_UDP_PORT_SRC << 2)
#define NET_HEADER_FIELD_UDP_CKSUM                      (NET_HEADER_FIELD_UDP_PORT_SRC << 3)
#define NET_HEADER_FIELD_UDP_ALL_FIELDS                 ((NET_HEADER_FIELD_UDP_PORT_SRC << 4) - 1)

#define NET_HEADER_FIELD_UDP_PORT_SIZE                  2

typedef uint8_t headerFieldUdpLite_t;

#define NET_HEADER_FIELD_UDP_LITE_PORT_SRC              (1)
#define NET_HEADER_FIELD_UDP_LITE_PORT_DST              (NET_HEADER_FIELD_UDP_LITE_PORT_SRC << 1)
#define NET_HEADER_FIELD_UDP_LITE_ALL_FIELDS            ((NET_HEADER_FIELD_UDP_LITE_PORT_SRC << 2) - 1)

#define NET_HEADER_FIELD_UDP_LITE_PORT_SIZE             2

typedef uint8_t headerFieldUdpEncapEsp_t;

#define NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC         (1)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_DST         (NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 1)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_LEN              (NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 2)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_CKSUM            (NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 3)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_SPI              (NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 4)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_SEQUENCE_NUM     (NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 5)
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_ALL_FIELDS       ((NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SRC << 6) - 1)

#define NET_HEADER_FIELD_UDP_ENCAP_ESP_PORT_SIZE        2
#define NET_HEADER_FIELD_UDP_ENCAP_ESP_SPI_SIZE         4

#define NET_HEADER_FIELD_IPHC_CID                       (1)
#define NET_HEADER_FIELD_IPHC_CID_TYPE                  (NET_HEADER_FIELD_IPHC_CID << 1)
#define NET_HEADER_FIELD_IPHC_HCINDEX                   (NET_HEADER_FIELD_IPHC_CID << 2)
#define NET_HEADER_FIELD_IPHC_GEN                       (NET_HEADER_FIELD_IPHC_CID << 3)
#define NET_HEADER_FIELD_IPHC_D_BIT                     (NET_HEADER_FIELD_IPHC_CID << 4)
#define NET_HEADER_FIELD_IPHC_ALL_FIELDS                ((NET_HEADER_FIELD_IPHC_CID << 5) - 1)

#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE           (1)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_FLAGS          (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 1)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_LENGTH         (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 2)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_TSN            (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 3)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_STREAM_ID      (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 4)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_STREAM_SQN     (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 5)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_PAYLOAD_PID    (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 6)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_UNORDERED      (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 7)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_BEGGINING      (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 8)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_END            (NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 9)
#define NET_HEADER_FIELD_SCTP_CHUNK_DATA_ALL_FIELDS     ((NET_HEADER_FIELD_SCTP_CHUNK_DATA_TYPE << 10) - 1)

#define NET_HEADER_FIELD_L2TPv2_TYPE_BIT                (1)
#define NET_HEADER_FIELD_L2TPv2_LENGTH_BIT              (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 1)
#define NET_HEADER_FIELD_L2TPv2_SEQUENCE_BIT            (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 2)
#define NET_HEADER_FIELD_L2TPv2_OFFSET_BIT              (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 3)
#define NET_HEADER_FIELD_L2TPv2_PRIORITY_BIT            (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 4)
#define NET_HEADER_FIELD_L2TPv2_VERSION                 (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 5)
#define NET_HEADER_FIELD_L2TPv2_LEN                     (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 6)
#define NET_HEADER_FIELD_L2TPv2_TUNNEL_ID               (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 7)
#define NET_HEADER_FIELD_L2TPv2_SESSION_ID              (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 8)
#define NET_HEADER_FIELD_L2TPv2_NS                      (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 9)
#define NET_HEADER_FIELD_L2TPv2_NR                      (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 10)
#define NET_HEADER_FIELD_L2TPv2_OFFSET_SIZE             (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 11)
#define NET_HEADER_FIELD_L2TPv2_FIRST_BYTE              (NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 12)
#define NET_HEADER_FIELD_L2TPv2_ALL_FIELDS              ((NET_HEADER_FIELD_L2TPv2_TYPE_BIT << 13) - 1)

#define NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT           (1)
#define NET_HEADER_FIELD_L2TPv3_CTRL_LENGTH_BIT         (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 1)
#define NET_HEADER_FIELD_L2TPv3_CTRL_SEQUENCE_BIT       (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 2)
#define NET_HEADER_FIELD_L2TPv3_CTRL_VERSION            (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 3)
#define NET_HEADER_FIELD_L2TPv3_CTRL_LENGTH             (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 4)
#define NET_HEADER_FIELD_L2TPv3_CTRL_CONTROL            (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 5)
#define NET_HEADER_FIELD_L2TPv3_CTRL_SENT               (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 6)
#define NET_HEADER_FIELD_L2TPv3_CTRL_RECV               (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 7)
#define NET_HEADER_FIELD_L2TPv3_CTRL_FIRST_BYTE         (NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 8)
#define NET_HEADER_FIELD_L2TPv3_CTRL_ALL_FIELDS         ((NET_HEADER_FIELD_L2TPv3_CTRL_TYPE_BIT << 9) - 1)

#define NET_HEADER_FIELD_L2TPv3_SESS_TYPE_BIT           (1)
#define NET_HEADER_FIELD_L2TPv3_SESS_VERSION            (NET_HEADER_FIELD_L2TPv3_SESS_TYPE_BIT << 1)
#define NET_HEADER_FIELD_L2TPv3_SESS_ID                 (NET_HEADER_FIELD_L2TPv3_SESS_TYPE_BIT << 2)
#define NET_HEADER_FIELD_L2TPv3_SESS_COOKIE             (NET_HEADER_FIELD_L2TPv3_SESS_TYPE_BIT << 3)
#define NET_HEADER_FIELD_L2TPv3_SESS_ALL_FIELDS         ((NET_HEADER_FIELD_L2TPv3_SESS_TYPE_BIT << 4) - 1)


typedef uint8_t headerFieldVlan_t;

#define NET_HEADER_FIELD_VLAN_VPRI                      (1)
#define NET_HEADER_FIELD_VLAN_CFI                       (NET_HEADER_FIELD_VLAN_VPRI << 1)
#define NET_HEADER_FIELD_VLAN_VID                       (NET_HEADER_FIELD_VLAN_VPRI << 2)
#define NET_HEADER_FIELD_VLAN_LENGTH                    (NET_HEADER_FIELD_VLAN_VPRI << 3)
#define NET_HEADER_FIELD_VLAN_TYPE                      (NET_HEADER_FIELD_VLAN_VPRI << 4)
#define NET_HEADER_FIELD_VLAN_ALL_FIELDS                ((NET_HEADER_FIELD_VLAN_VPRI << 5) - 1)

#define NET_HEADER_FIELD_VLAN_TCI                       (NET_HEADER_FIELD_VLAN_VPRI | \
                                                         NET_HEADER_FIELD_VLAN_CFI | \
                                                         NET_HEADER_FIELD_VLAN_VID)


typedef uint8_t headerFieldLlc_t;

#define NET_HEADER_FIELD_LLC_DSAP                       (1)
#define NET_HEADER_FIELD_LLC_SSAP                       (NET_HEADER_FIELD_LLC_DSAP << 1)
#define NET_HEADER_FIELD_LLC_CTRL                       (NET_HEADER_FIELD_LLC_DSAP << 2)
#define NET_HEADER_FIELD_LLC_ALL_FIELDS                 ((NET_HEADER_FIELD_LLC_DSAP << 3) - 1)

#define NET_HEADER_FIELD_NLPID_NLPID                    (1)
#define NET_HEADER_FIELD_NLPID_ALL_FIELDS               ((NET_HEADER_FIELD_NLPID_NLPID << 1) - 1)


typedef uint8_t headerFieldSnap_t;

#define NET_HEADER_FIELD_SNAP_OUI                       (1)
#define NET_HEADER_FIELD_SNAP_PID                       (NET_HEADER_FIELD_SNAP_OUI << 1)
#define NET_HEADER_FIELD_SNAP_ALL_FIELDS                ((NET_HEADER_FIELD_SNAP_OUI << 2) - 1)


typedef uint8_t headerFieldLlcSnap_t;

#define NET_HEADER_FIELD_LLC_SNAP_TYPE                  (1)
#define NET_HEADER_FIELD_LLC_SNAP_ALL_FIELDS            ((NET_HEADER_FIELD_LLC_SNAP_TYPE << 1) - 1)

#define NET_HEADER_FIELD_ARP_HTYPE                      (1)
#define NET_HEADER_FIELD_ARP_PTYPE                      (NET_HEADER_FIELD_ARP_HTYPE << 1)
#define NET_HEADER_FIELD_ARP_HLEN                       (NET_HEADER_FIELD_ARP_HTYPE << 2)
#define NET_HEADER_FIELD_ARP_PLEN                       (NET_HEADER_FIELD_ARP_HTYPE << 3)
#define NET_HEADER_FIELD_ARP_OPER                       (NET_HEADER_FIELD_ARP_HTYPE << 4)
#define NET_HEADER_FIELD_ARP_SHA                        (NET_HEADER_FIELD_ARP_HTYPE << 5)
#define NET_HEADER_FIELD_ARP_SPA                        (NET_HEADER_FIELD_ARP_HTYPE << 6)
#define NET_HEADER_FIELD_ARP_THA                        (NET_HEADER_FIELD_ARP_HTYPE << 7)
#define NET_HEADER_FIELD_ARP_TPA                        (NET_HEADER_FIELD_ARP_HTYPE << 8)
#define NET_HEADER_FIELD_ARP_ALL_FIELDS                 ((NET_HEADER_FIELD_ARP_HTYPE << 9) - 1)

#define NET_HEADER_FIELD_RFC2684_LLC                    (1)
#define NET_HEADER_FIELD_RFC2684_NLPID                  (NET_HEADER_FIELD_RFC2684_LLC << 1)
#define NET_HEADER_FIELD_RFC2684_OUI                    (NET_HEADER_FIELD_RFC2684_LLC << 2)
#define NET_HEADER_FIELD_RFC2684_PID                    (NET_HEADER_FIELD_RFC2684_LLC << 3)
#define NET_HEADER_FIELD_RFC2684_VPN_OUI                (NET_HEADER_FIELD_RFC2684_LLC << 4)
#define NET_HEADER_FIELD_RFC2684_VPN_IDX                (NET_HEADER_FIELD_RFC2684_LLC << 5)
#define NET_HEADER_FIELD_RFC2684_ALL_FIELDS             ((NET_HEADER_FIELD_RFC2684_LLC << 6) - 1)

#define NET_HEADER_FIELD_USER_DEFINED_SRCPORT           (1)
#define NET_HEADER_FIELD_USER_DEFINED_PCDID             (NET_HEADER_FIELD_USER_DEFINED_SRCPORT << 1)
#define NET_HEADER_FIELD_USER_DEFINED_ALL_FIELDS        ((NET_HEADER_FIELD_USER_DEFINED_SRCPORT << 2) - 1)

#define NET_HEADER_FIELD_PAYLOAD_BUFFER                 (1)
#define NET_HEADER_FIELD_PAYLOAD_SIZE                   (NET_HEADER_FIELD_PAYLOAD_BUFFER << 1)
#define NET_HEADER_FIELD_MAX_FRM_SIZE                   (NET_HEADER_FIELD_PAYLOAD_BUFFER << 2)
#define NET_HEADER_FIELD_MIN_FRM_SIZE                   (NET_HEADER_FIELD_PAYLOAD_BUFFER << 3)
#define NET_HEADER_FIELD_PAYLOAD_TYPE                   (NET_HEADER_FIELD_PAYLOAD_BUFFER << 4)
#define NET_HEADER_FIELD_FRAME_SIZE                     (NET_HEADER_FIELD_PAYLOAD_BUFFER << 5)
#define NET_HEADER_FIELD_PAYLOAD_ALL_FIELDS             ((NET_HEADER_FIELD_PAYLOAD_BUFFER << 6) - 1)


typedef uint8_t headerFieldGre_t;

#define NET_HEADER_FIELD_GRE_TYPE                       (1)
#define NET_HEADER_FIELD_GRE_ALL_FIELDS                 ((NET_HEADER_FIELD_GRE_TYPE << 1) - 1)


typedef uint8_t headerFieldMinencap_t;

#define NET_HEADER_FIELD_MINENCAP_SRC_IP                (1)
#define NET_HEADER_FIELD_MINENCAP_DST_IP                (NET_HEADER_FIELD_MINENCAP_SRC_IP << 1)
#define NET_HEADER_FIELD_MINENCAP_TYPE                  (NET_HEADER_FIELD_MINENCAP_SRC_IP << 2)
#define NET_HEADER_FIELD_MINENCAP_ALL_FIELDS            ((NET_HEADER_FIELD_MINENCAP_SRC_IP << 3) - 1)


typedef uint8_t headerFieldIpsecAh_t;

#define NET_HEADER_FIELD_IPSEC_AH_SPI                   (1)
#define NET_HEADER_FIELD_IPSEC_AH_NH                    (NET_HEADER_FIELD_IPSEC_AH_SPI << 1)
#define NET_HEADER_FIELD_IPSEC_AH_ALL_FIELDS            ((NET_HEADER_FIELD_IPSEC_AH_SPI << 2) - 1)


typedef uint8_t headerFieldIpsecEsp_t;

#define NET_HEADER_FIELD_IPSEC_ESP_SPI                  (1)
#define NET_HEADER_FIELD_IPSEC_ESP_SEQUENCE_NUM         (NET_HEADER_FIELD_IPSEC_ESP_SPI << 1)
#define NET_HEADER_FIELD_IPSEC_ESP_ALL_FIELDS           ((NET_HEADER_FIELD_IPSEC_ESP_SPI << 2) - 1)

#define NET_HEADER_FIELD_IPSEC_ESP_SPI_SIZE             4


typedef uint8_t headerFieldMpls_t;

#define NET_HEADER_FIELD_MPLS_LABEL_STACK               (1)
#define NET_HEADER_FIELD_MPLS_LABEL_STACK_ALL_FIELDS    ((NET_HEADER_FIELD_MPLS_LABEL_STACK << 1) - 1)


typedef uint8_t headerFieldMacsec_t;

#define NET_HEADER_FIELD_MACSEC_SECTAG                  (1)
#define NET_HEADER_FIELD_MACSEC_ALL_FIELDS              ((NET_HEADER_FIELD_MACSEC_SECTAG << 1) - 1)


typedef enum {
    HEADER_TYPE_NONE = 0,
    HEADER_TYPE_PAYLOAD,
    HEADER_TYPE_ETH,
    HEADER_TYPE_VLAN,
    HEADER_TYPE_IPv4,
    HEADER_TYPE_IPv6,
    HEADER_TYPE_IP,
    HEADER_TYPE_TCP,
    HEADER_TYPE_UDP,
    HEADER_TYPE_UDP_LITE,
    HEADER_TYPE_IPHC,
    HEADER_TYPE_SCTP,
    HEADER_TYPE_SCTP_CHUNK_DATA,
    HEADER_TYPE_PPPoE,
    HEADER_TYPE_PPP,
    HEADER_TYPE_PPPMUX,
    HEADER_TYPE_PPPMUX_SUBFRAME,
    HEADER_TYPE_L2TPv2,
    HEADER_TYPE_L2TPv3_CTRL,
    HEADER_TYPE_L2TPv3_SESS,
    HEADER_TYPE_LLC,
    HEADER_TYPE_LLC_SNAP,
    HEADER_TYPE_NLPID,
    HEADER_TYPE_SNAP,
    HEADER_TYPE_MPLS,
    HEADER_TYPE_IPSEC_AH,
    HEADER_TYPE_IPSEC_ESP,
    HEADER_TYPE_UDP_ENCAP_ESP, /* RFC 3948 */
    HEADER_TYPE_MACSEC,
    HEADER_TYPE_GRE,
    HEADER_TYPE_MINENCAP,
    HEADER_TYPE_DCCP,
    HEADER_TYPE_ICMP,
    HEADER_TYPE_IGMP,
    HEADER_TYPE_ARP,
    HEADER_TYPE_CAPWAP,
    HEADER_TYPE_CAPWAP_DTLS,
    HEADER_TYPE_RFC2684,
    HEADER_TYPE_USER_DEFINED_L2,
    HEADER_TYPE_USER_DEFINED_L3,
    HEADER_TYPE_USER_DEFINED_L4,
    HEADER_TYPE_USER_DEFINED_SHIM1,
    HEADER_TYPE_USER_DEFINED_SHIM2,
    MAX_HEADER_TYPE_COUNT
} e_NetHeaderType;


#endif /* __NET_EXT_H */
