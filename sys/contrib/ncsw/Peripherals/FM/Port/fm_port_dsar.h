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
 @File          fm_port_dsar.h

 @Description   Deep Sleep Auto Response project - common module header file.               

                Author - Eyal Harari
                
 @Cautions      See the FMan Controller spec and design document for more information.
*//***************************************************************************/

#ifndef __FM_PORT_DSAR_H_
#define __FM_PORT_DSAR_H_

#define DSAR_GETSER_MASK 0xFF0000FF

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(push,1)
#endif /* defined(__MWERKS__) && ... */ 

/**************************************************************************//**
 @Description   Deep Sleep Auto Response VLAN-IPv4 Binding Table (for ARP/ICMPv4)
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
typedef _Packed struct
{
        uint32_t ipv4Addr; /*!< 32 bit IPv4 Address. */
        uint16_t vlanId;   /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
					   /*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
        uint16_t reserved;
} _PackedType t_DsarArpBindingEntry;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response Address Resolution Protocol Statistics Descriptor
                Refer to the FMan Controller spec for more details.
	            0x00 INVAL_CNT Invalid ARP IPv4-Ethernet counter
	            0x04 ECHO_CNT Echo counter
	            0x08 CD_CNT Conflict Detection counter
	            0x0C AR_CNT Auto-Response counter
	            0x10 RATM_CNT Replies Addressed To Me counter
	            0x14 UKOP_CNT Unknown Operation counter
	            0x18 NMTP_CNT Not my TPA counter
	            0x1C NMVLAN_CNT Not My VLAN counter
*//***************************************************************************/
typedef _Packed struct
{
    uint32_t invalCnt;	/**< Invalid ARP IPv4-Ethernet counter. */
    uint32_t echoCnt;	/**< Echo counter. 						*/
    uint32_t cdCnt;		/**< Conflict Detection counter.		*/
    uint32_t arCnt;		/**< Auto-Response counter.				*/
    uint32_t ratmCnt;	/**< Replies Addressed To Me counter.	*/
    uint32_t ukopCnt;	/**< Unknown Operation counter.			*/
    uint32_t nmtpCnt;	/**< Not my TPA counter.				*/
    uint32_t nmVlanCnt; /**< Not My VLAN counter				*/
} _PackedType t_DsarArpStatistics;


/**************************************************************************//**
 @Description   Deep Sleep Auto Response Address Resolution Protocol Descriptor
                0x0 0-15 Control bits [0-15]. Bit 15  = CDEN.
                0x2 0-15 NumOfBindings Number of entries in the binding list.
                0x4 0-15 BindingsPointer Bindings Pointer. This points to an IPv4-MAC Addresses Bindings list.
                0x6 0-15
                0x8 0-15 StatisticsPointer Statistics Pointer. This field points to the ARP Descriptors statistics data structure.
                0xA 0-15
                0xC 0-15 Reserved Reserved. Must be cleared.
                0xE 015

*//***************************************************************************/
typedef _Packed struct
{
    uint16_t control;                       /** Control bits [0-15]. Bit 15  = CDEN */
    uint16_t numOfBindings;                 /**< Number of VLAN-IPv4 */
    uint32_t p_Bindings;	/**< VLAN-IPv4 Bindings table pointer. */
    uint32_t p_Statistics;   /**< Statistics Data Structure pointer. */
    uint32_t reserved1;                     /**< Reserved. */
} _PackedType t_DsarArpDescriptor;


/**************************************************************************//**
 @Description   Deep Sleep Auto Response VLAN-IPv4 Binding Table (for ARP/ICMPv4)
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
typedef _Packed struct 
{
    uint32_t ipv4Addr; /*!< 32 bit IPv4 Address. */
	uint16_t vlanId;   /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
					   /*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
	uint16_t reserved;
} _PackedType t_DsarIcmpV4BindingEntry;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response ICMPv4 Statistics Descriptor
                Refer to the FMan Controller spec for more details.
                0x00 INVAL_CNT Invalid ICMPv4 header counter
                0x04 NMVLAN_CNT Not My VLAN counter
                0x08 NMIP_CNT Not My IP counter
                0x0C AR_CNT Auto-Response counter
                0x10 CSERR_CNT Checksum Error counter
                0x14 Reserved Reserved
                0x18 Reserved Reserved
                0x1C Reserved Reserved

*//***************************************************************************/
typedef _Packed struct
{
    uint32_t invalCnt;	/**< Invalid ICMPv4 Echo counter. */
    uint32_t nmVlanCnt;	/**< Not My VLAN counter          */
    uint32_t nmIpCnt;	/**< Not My IP counter		      */
    uint32_t arCnt;		/**< Auto-Response counter        */
    uint32_t cserrCnt;	/**< Checksum Error counter       */
    uint32_t reserved0;	/**< Reserved                     */
    uint32_t reserved1;	/**< Reserved                     */
    uint32_t reserved2; /**< Reserved                     */
} _PackedType t_DsarIcmpV4Statistics;



/**************************************************************************//**
 @Description   Deep Sleep Auto Response ICMPv4 Descriptor
                0x0 0-15 Control bits [0-15]
                0x2 0-15 NumOfBindings Number of entries in the binding list.
                0x4 0-15 BindingsPointer Bindings Pointer. This points to an VLAN-IPv4 Addresses Bindings list.
                0x6 0-15
                0x8 0-15 StatisticsPointer Statistics Pointer. This field points to the ICMPv4 statistics data structure.
                0xA 0-15
                0xC 0-15 Reserved Reserved. Must be cleared.
                0xE 015

*//***************************************************************************/
typedef _Packed struct
{
    uint16_t control;                       /** Control bits [0-15].                */
    uint16_t numOfBindings;                 /**< Number of VLAN-IPv4                */
    uint32_t p_Bindings;	/**< VLAN-IPv4 Bindings table pointer.  */
    uint32_t p_Statistics;   /**< Statistics Data Structure pointer. */
    uint32_t reserved1;                     /**< Reserved.                          */
} _PackedType t_DsarIcmpV4Descriptor;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response VLAN-IPv4 Binding Table (for ARP/ICMPv4)
                The 4 left-most bits (15:12) of the VlanId parameter are control flags.
                Flags[3:1] (VlanId[15:13]): Reserved, should be cleared.
                Flags[0] (VlanId[12]): Temporary address.
                • 0 - Assigned IP address.
                • 1- Temporary (tentative) IP address.
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
typedef _Packed struct 
{
    uint32_t ipv6Addr[4];  /*!< 3 * 32 bit IPv4 Address.                                                    */
	uint16_t resFlags:4;   /*!< reserved flags. should be cleared                                           */
	uint16_t vlanId:12;    /*!< 12 bits VLAN ID.                                                            */
					       /*!< This field should be 0x000 for an entry with no VLAN tag or a null VLAN ID. */
	uint16_t reserved;
} _PackedType t_DsarIcmpV6BindingEntry;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response ICMPv4 Statistics Descriptor
                Refer to the FMan Controller spec for more details.
                0x00 INVAL_CNT Invalid ICMPv4 header counter
                0x04 NMVLAN_CNT Not My VLAN counter
                0x08 NMIP_CNT Not My IP counter
                0x0C AR_CNT Auto-Response counter
                0x10 CSERR_CNT Checksum Error counter
                0x14 MCAST_CNT Multicast counter
                0x18 Reserved Reserved
                0x1C Reserved Reserved

*//***************************************************************************/
typedef _Packed struct
{
    uint32_t invalCnt;	/**< Invalid ICMPv4 Echo counter. */
    uint32_t nmVlanCnt;	/**< Not My VLAN counter          */
    uint32_t nmIpCnt;	/**< Not My IP counter		      */
    uint32_t arCnt;		/**< Auto-Response counter        */
    uint32_t reserved1;	/**< Reserved                     */
    uint32_t reserved2; /**< Reserved                     */
    uint32_t reserved3;	/**< Reserved                     */
    uint32_t reserved4; /**< Reserved                     */
} _PackedType t_DsarIcmpV6Statistics;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response Neighbor Discovery Statistics Descriptor
                0x00 INVAL_CNT Invalid Neighbor Discovery message counter
                0x04 NMVLAN_CNT Not My VLAN counter
                0x08 NMIP_CNT Not My IP counter
                0x0C AR_CNT Auto-Response counter
                0x10 CSERR_CNT Checksum Error counter
                0x14 USADVERT_CNT Unsolicited Neighbor Advertisements counter
                0x18 NMMCAST_CNT Not My Multicast group counter
                0x1C NSLLA_CNT No Source Link-Layer Address counter. Indicates that there was a match on a Target
                     Address of a packet that its source IP address is a unicast address, but the ICMPv6
                     Source Link-layer Address option is omitted
*//***************************************************************************/
typedef _Packed struct
{
    uint32_t invalCnt;	  /**< Invalid ICMPv4 Echo counter.                */
    uint32_t nmVlanCnt;	  /**< Not My VLAN counter                         */
    uint32_t nmIpCnt;	  /**< Not My IP counter		                   */
    uint32_t arCnt;		  /**< Auto-Response counter                       */
    uint32_t reserved1;	  /**< Reserved                                    */
    uint32_t usadvertCnt; /**< Unsolicited Neighbor Advertisements counter */
    uint32_t nmmcastCnt;  /**< Not My Multicast group counter              */
    uint32_t nsllaCnt;    /**< No Source Link-Layer Address counter        */
} _PackedType t_NdStatistics;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response ICMPv6 Descriptor
                0x0 0-15 Control bits [0-15]
                0x2 0-15 NumOfBindings Number of entries in the binding list.
                0x4 0-15 BindingsPointer Bindings Pointer. This points to an VLAN-IPv4 Addresses Bindings list.
                0x6 0-15
                0x8 0-15 StatisticsPointer Statistics Pointer. This field points to the ICMPv4 statistics data structure.
                0xA 0-15
                0xC 0-15 Reserved Reserved. Must be cleared.
                0xE 015

*//***************************************************************************/
typedef _Packed struct
{
    uint16_t control;                       /** Control bits [0-15].                */
    uint16_t numOfBindings;                 /**< Number of VLAN-IPv6                */
    uint32_t p_Bindings;	/**< VLAN-IPv4 Bindings table pointer.  */
    uint32_t p_Statistics;   /**< Statistics Data Structure pointer. */
	uint32_t reserved1;                     /**< Reserved.                          */
} _PackedType t_DsarIcmpV6Descriptor;


/**************************************************************************//**
 @Description   Internet Control Message Protocol (ICMPv6) Echo message header
                The fields names are taken from RFC 4443.
*//***************************************************************************/
/* 0                   1                   2                   3     */
/* 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1   */
/* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
/* |     Type      |     Code      |          Checksum             | */
/* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
/* |           Identifier          |        Sequence Number        | */
/* +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+ */
/* |     Data ...                                                    */
/* +-+-+-+-+-                                                        */
typedef _Packed struct
{
	uint8_t  type;
	uint8_t  code;
	uint16_t checksum;
	uint16_t identifier;
	uint16_t sequenceNumber;
} _PackedType t_IcmpV6EchoHdr;

/**************************************************************************//**
 @Description   Internet Control Message Protocol (ICMPv6) 
                Neighbor Solicitation/Advertisement header
                The fields names are taken from RFC 4861.
                The R/S/O fields are valid for Neighbor Advertisement only
*//***************************************************************************/
/* 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Type      |     Code      |          Checksum             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |R|S|O|                     Reserved                            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * +                                                               +
 * |                                                               |
 * +                       Target Address                          +
 * |                                                               |
 * +                                                               +
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Options ...
 * +-+-+-+-+-+-+-+-+-+-+-+-
 *
 * Options Format:
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Type      |    Length     |   Link-Layer Address ...      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Link-Layer Address                           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
typedef _Packed struct
{
	uint8_t  type;
	uint8_t  code;
	uint16_t checksum;
	uint32_t router:1;
	uint32_t solicited:1;
	uint32_t override:1;
	uint32_t reserved:29;
	uint32_t targetAddr[4];
	uint8_t  optionType;
	uint8_t  optionLength;
	uint8_t  linkLayerAddr[6];
} _PackedType t_IcmpV6NdHdr;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response ICMPv6 Descriptor
                0x0 0-15 Control bits [0-15]
                0x2 0-15 NumOfBindings Number of entries in the binding list.
                0x4 0-15 BindingsPointer Bindings Pointer. This points to an VLAN-IPv4 Addresses Bindings list.
                0x6 0-15
                0x8 0-15 StatisticsPointer Statistics Pointer. This field points to the ICMPv4 statistics data structure.
                0xA 0-15
                0xC 0-15 Reserved Reserved. Must be cleared.
                0xE 015

*//***************************************************************************/
typedef _Packed struct
{
    uint16_t control;                       /** Control bits [0-15].                    */
    uint16_t numOfBindings;                 /**< Number of VLAN-IPv6                    */
    uint32_t p_Bindings;	/**< VLAN-IPv4 Bindings table pointer.      */
    uint32_t p_Statistics;   /**< Statistics Data Structure pointer.     */
	uint32_t solicitedAddr;                 /**< Solicited Node Multicast Group Address */
} _PackedType t_DsarNdDescriptor;

/**************************************************************************//**
@Description    Deep Sleep Auto Response SNMP OIDs table entry
                 
*//***************************************************************************/
typedef struct {
    uint16_t oidSize;     /**< Size in octets of the OID. */
    uint16_t resSize;     /**< Size in octets of the value that is attached to the OID. */
    uint32_t p_Oid;       /**< Pointer to the OID. OID is encoded in BER but type and length are excluded. */
    uint32_t resValOrPtr; /**< Value (for up to 4 octets) or pointer to the Value. Encoded in BER. */
    uint32_t reserved;
} t_OidsTblEntry;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response SNMP IPv4 Addresses Table Entry
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
typedef struct
{
    uint32_t ipv4Addr; /*!< 32 bit IPv4 Address. */
    uint16_t vlanId;   /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
                       /*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
    uint16_t reserved;
} t_DsarSnmpIpv4AddrTblEntry;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response SNMP IPv6 Addresses Table Entry
                Refer to the FMan Controller spec for more details.
*//***************************************************************************/
#pragma pack(push,1)
typedef struct
{
    uint32_t ipv6Addr[4];  /*!< 4 * 32 bit IPv6 Address.                                                     */
    uint16_t vlanId;       /*!< 12 bits VLAN ID. The 4 left-most bits should be cleared                      */
                           /*!< This field should be 0x0000 for an entry with no VLAN tag or a null VLAN ID. */
    uint16_t reserved;
} t_DsarSnmpIpv6AddrTblEntry;
#pragma pack(pop)

/**************************************************************************//**
@Description    Deep Sleep Auto Response SNMP statistics table
                 
*//***************************************************************************/
typedef struct {
    uint32_t snmpErrCnt;  /**< Counts SNMP errors (wrong version, BER encoding, format). */
    uint32_t snmpCommunityErrCnt; /**< Counts messages that were dropped due to insufficient permission. */
    uint32_t snmpTotalDiscardCnt; /**< Counts any message that was dropped. */
    uint32_t snmpGetReqCnt; /**< Counts the number of get-request messages */
    uint32_t snmpGetNextReqCnt; /**< Counts the number of get-next-request messages */
} t_DsarSnmpStatistics;

/**************************************************************************//**
 @Description   Deep Sleep Auto Response SNMP Descriptor

*//***************************************************************************/
typedef struct
{
    uint16_t control;                          /**< Control bits [0-15]. */
    uint16_t maxSnmpMsgLength;                 /**< Maximal allowed SNMP message length. */
    uint16_t numOfIpv4Addresses;               /**< Number of entries in IPv4 addresses table. */
    uint16_t numOfIpv6Addresses;               /**< Number of entries in IPv6 addresses table. */
    uint32_t p_Ipv4AddrTbl; /**< Pointer to IPv4 addresses table. */
    uint32_t p_Ipv6AddrTbl; /**< Pointer to IPv6 addresses table. */
    uint32_t p_RdOnlyCommunityStr;             /**< Pointer to the Read Only Community String. */
    uint32_t p_RdWrCommunityStr;               /**< Pointer to the Read Write Community String. */
    uint32_t p_OidsTbl;                 /**< Pointer to OIDs table. */
    uint32_t oidsTblSize;                      /**< Number of entries in OIDs table. */
    uint32_t p_Statistics;                 /**< Pointer to SNMP statistics table. */
} t_DsarSnmpDescriptor;

/**************************************************************************//**
@Description    Deep Sleep Auto Response (Common) Statistics
                 
*//***************************************************************************/
typedef _Packed struct {
	uint32_t dsarDiscarded;
	uint32_t dsarErrDiscarded;
	uint32_t dsarFragDiscarded;
	uint32_t dsarTunnelDiscarded;
	uint32_t dsarArpDiscarded;
	uint32_t dsarIpDiscarded;
	uint32_t dsarTcpDiscarded;
	uint32_t dsarUdpDiscarded;
	uint32_t dsarIcmpV6ChecksumErr; /* ICMPv6 Checksum Error counter */
	uint32_t dsarIcmpV6OtherType;   /* ICMPv6 'Other' type (not Echo or Neighbor Solicitaion/Advertisement counter */
	uint32_t dsarIcmpV4OtherType;   /* ICMPv4 'Other' type (not Echo) counter */
} _PackedType t_ArStatistics;


/**************************************************************************//**
@Description    Deep Sleep Auto Response TCP/UDP port filter table entry
                 
*//***************************************************************************/
typedef _Packed struct {
	uint32_t	Ports;
	uint32_t	PortsMask;
} _PackedType t_PortTblEntry;


					
/**************************************************************************//**
@Description    Deep Sleep Auto Response Common Parameters Descriptor
                 
*//***************************************************************************/
typedef _Packed struct {
	uint8_t   arTxPort;            /* 0x00 0-7 Auto Response Transmit Port number            */
	uint8_t   controlBits;         /* 0x00 8-15 Auto Response control bits                   */
	uint16_t  res1;                /* 0x00 16-31 Reserved                                    */
	uint32_t  activeHPNIA;         /* 0x04 0-31 Active mode Hardware Parser NIA              */
	uint16_t  snmpPort;            /* 0x08 0-15 SNMP Port.                                   */
	uint8_t   macStationAddr[6];   /* 0x08 16-31 and 0x0C 0-31 MAC Station Address           */
	uint8_t   res2;				   /* 0x10 0-7 Reserved				    					 */
	uint8_t   filterControl;       /* 0x10 8-15 Filtering Control Bits.                      */
	uint16_t   tcpControlPass;	   /* 0x10 16-31 TCP control pass flags					     */
	uint8_t   ipProtocolTblSize;   /* 0x14 0-7 IP Protocol Table Size.                       */
	uint8_t   udpPortTblSize;      /* 0x14 8-15 UDP Port Table Size.                         */
	uint8_t   tcpPortTblSize;      /* 0x14 16-23 TCP Port Table Size.                        */
	uint8_t   res3;                /* 0x14 24-31 Reserved                                    */
	uint32_t  p_IpProtocolFiltTbl; /* 0x18 0-31 Pointer to IP Protocol Filter Table          */
	uint32_t p_UdpPortFiltTbl; /* 0x1C 0-31 Pointer to UDP Port Filter Table          */
	uint32_t p_TcpPortFiltTbl; /* 0x20 0-31 Pointer to TCP Port Filter Table          */
	uint32_t res4;                 /* 0x24 Reserved                                          */
	uint32_t p_ArpDescriptor;     /* 0x28 0-31 ARP Descriptor Pointer.                      */
	uint32_t p_NdDescriptor;      /* 0x2C 0-31 Neighbor Discovery Descriptor.               */
	uint32_t p_IcmpV4Descriptor;  /* 0x30 0-31 ICMPv4 Descriptor pointer.                   */
	uint32_t p_IcmpV6Descriptor;  /* 0x34 0-31 ICMPv6 Descriptor pointer.                   */
	uint32_t p_SnmpDescriptor;    /* 0x38 0-31 SNMP Descriptor pointer.                     */
	uint32_t p_ArStats;     /* 0x3C 0-31 Pointer to Auto Response Statistics          */
} _PackedType t_ArCommonDesc;

#if defined(__MWERKS__) && !defined(__GNUC__)
#pragma pack(pop)
#endif /* defined(__MWERKS__) && ... */ 

/* t_ArCommonDesc.filterControl bits */
#define	IP_PROT_TBL_PASS_MASK	0x08
#define UDP_PORT_TBL_PASS_MASK	0x04
#define TCP_PORT_TBL_PASS_MASK	0x02

/* Offset of TCF flags within TCP packet */
#define TCP_FLAGS_OFFSET 12


#endif /* __FM_PORT_DSAR_H_ */
