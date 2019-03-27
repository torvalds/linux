/* $FreeBSD$ */
#ifndef _EFIPXEBC_H
#define _EFIPXEBC_H

/*++

Copyright (c)  1999 - 2002 Intel Corporation. All rights reserved
This software and associated documentation (if any) is furnished
under a license and may only be used or copied in accordance
with the terms of the license. Except as permitted by such
license, no part of this software or documentation may be
reproduced, stored in a retrieval system, or transmitted in any
form or by any means without the express written consent of
Intel Corporation.

Module Name:

    efipxebc.h

Abstract:

    EFI PXE Base Code Protocol



Revision History

--*/

//
// PXE Base Code protocol
//

#define EFI_PXE_BASE_CODE_PROTOCOL \
    { 0x03c4e603, 0xac28, 0x11d3, {0x9a, 0x2d, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

INTERFACE_DECL(_EFI_PXE_BASE_CODE);

#define DEFAULT_TTL 8
#define DEFAULT_ToS 0
//
// Address definitions
//

typedef union {
    UINT32      Addr[4];
    EFI_IPv4_ADDRESS    v4;
    EFI_IPv6_ADDRESS    v6;
} EFI_IP_ADDRESS;

typedef UINT16 EFI_PXE_BASE_CODE_UDP_PORT;

//
// Packet definitions
//

typedef struct {
    UINT8                           BootpOpcode;
    UINT8                           BootpHwType;
    UINT8                           BootpHwAddrLen;
    UINT8                           BootpGateHops;
    UINT32                          BootpIdent;
    UINT16                          BootpSeconds;
    UINT16                          BootpFlags;
    UINT8                           BootpCiAddr[4];
    UINT8                           BootpYiAddr[4];
    UINT8                           BootpSiAddr[4];
    UINT8                           BootpGiAddr[4];
    UINT8                           BootpHwAddr[16];
    UINT8                           BootpSrvName[64];
    UINT8                           BootpBootFile[128];
    UINT32                          DhcpMagik;
    UINT8                           DhcpOptions[56];
} EFI_PXE_BASE_CODE_DHCPV4_PACKET;

// TBD in EFI v1.1
//typedef struct {
//    UINT8                           reserved;
//} EFI_PXE_BASE_CODE_DHCPV6_PACKET;

typedef union {
    UINT8                               Raw[1472];
    EFI_PXE_BASE_CODE_DHCPV4_PACKET     Dhcpv4;
//    EFI_PXE_BASE_CODE_DHCPV6_PACKET     Dhcpv6;
} EFI_PXE_BASE_CODE_PACKET;

typedef struct {
    UINT8                   Type;
    UINT8                   Code;
    UINT16                  Checksum;
    union {
        UINT32              reserved;
        UINT32              Mtu;
        UINT32              Pointer;
        struct {
            UINT16          Identifier;
            UINT16          Sequence;
        } Echo;
    } u;
    UINT8                   Data[494];
} EFI_PXE_BASE_CODE_ICMP_ERROR;

typedef struct {
    UINT8                   ErrorCode;
    CHAR8                   ErrorString[127];
} EFI_PXE_BASE_CODE_TFTP_ERROR;

//
// IP Receive Filter definitions
//
#define EFI_PXE_BASE_CODE_MAX_IPCNT             8
typedef struct {
    UINT8                       Filters;
    UINT8                       IpCnt;
    UINT16                      reserved;
    EFI_IP_ADDRESS              IpList[EFI_PXE_BASE_CODE_MAX_IPCNT];
} EFI_PXE_BASE_CODE_IP_FILTER;

#define EFI_PXE_BASE_CODE_IP_FILTER_STATION_IP             0x0001
#define EFI_PXE_BASE_CODE_IP_FILTER_BROADCAST              0x0002
#define EFI_PXE_BASE_CODE_IP_FILTER_PROMISCUOUS            0x0004
#define EFI_PXE_BASE_CODE_IP_FILTER_PROMISCUOUS_MULTICAST  0x0008

//
// ARP Cache definitions
//

typedef struct {
    EFI_IP_ADDRESS       IpAddr;
    EFI_MAC_ADDRESS      MacAddr;
} EFI_PXE_BASE_CODE_ARP_ENTRY;

typedef struct {
    EFI_IP_ADDRESS       IpAddr;
    EFI_IP_ADDRESS       SubnetMask;
    EFI_IP_ADDRESS       GwAddr;
} EFI_PXE_BASE_CODE_ROUTE_ENTRY;

//
// UDP definitions
//

#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_SRC_IP    0x0001
#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_SRC_PORT  0x0002
#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_IP   0x0004
#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_ANY_DEST_PORT 0x0008
#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_USE_FILTER    0x0010
#define EFI_PXE_BASE_CODE_UDP_OPFLAGS_MAY_FRAGMENT  0x0020

//
// Discover() definitions
//

#define EFI_PXE_BASE_CODE_BOOT_TYPE_BOOTSTRAP           0   
#define EFI_PXE_BASE_CODE_BOOT_TYPE_MS_WINNT_RIS        1
#define EFI_PXE_BASE_CODE_BOOT_TYPE_INTEL_LCM           2
#define EFI_PXE_BASE_CODE_BOOT_TYPE_DOSUNDI             3
#define EFI_PXE_BASE_CODE_BOOT_TYPE_NEC_ESMPRO          4
#define EFI_PXE_BASE_CODE_BOOT_TYPE_IBM_WSoD            5
#define EFI_PXE_BASE_CODE_BOOT_TYPE_IBM_LCCM            6
#define EFI_PXE_BASE_CODE_BOOT_TYPE_CA_UNICENTER_TNG    7
#define EFI_PXE_BASE_CODE_BOOT_TYPE_HP_OPENVIEW         8
#define EFI_PXE_BASE_CODE_BOOT_TYPE_ALTIRIS_9           9
#define EFI_PXE_BASE_CODE_BOOT_TYPE_ALTIRIS_10          10
#define EFI_PXE_BASE_CODE_BOOT_TYPE_ALTIRIS_11          11
#define EFI_PXE_BASE_CODE_BOOT_TYPE_NOT_USED_12         12
#define EFI_PXE_BASE_CODE_BOOT_TYPE_REDHAT_INSTALL      13
#define EFI_PXE_BASE_CODE_BOOT_TYPE_REDHAT_BOOT         14
#define EFI_PXE_BASE_CODE_BOOT_TYPE_REMBO               15
#define EFI_PXE_BASE_CODE_BOOT_TYPE_BEOBOOT             16
//
// 17 through 32767 are reserved
// 32768 through 65279 are for vendor use
// 65280 through 65534 are reserved
//
#define EFI_PXE_BASE_CODE_BOOT_TYPE_PXETEST             65535

#define EFI_PXE_BASE_CODE_BOOT_LAYER_MASK               0x7FFF
#define EFI_PXE_BASE_CODE_BOOT_LAYER_INITIAL            0x0000
#define EFI_PXE_BASE_CODE_BOOT_LAYER_CREDENTIALS        0x8000


typedef struct {
    UINT16                      Type;
    BOOLEAN                     AcceptAnyResponse;
    UINT8                       Reserved;
    EFI_IP_ADDRESS              IpAddr;
} EFI_PXE_BASE_CODE_SRVLIST;

typedef struct {
    BOOLEAN                     UseMCast;
    BOOLEAN                     UseBCast;
    BOOLEAN                     UseUCast;
    BOOLEAN                     MustUseList;
    EFI_IP_ADDRESS              ServerMCastIp;
    UINT16                      IpCnt;
    EFI_PXE_BASE_CODE_SRVLIST   SrvList[1];
} EFI_PXE_BASE_CODE_DISCOVER_INFO;

//
// Mtftp() definitions
//

typedef enum {
    EFI_PXE_BASE_CODE_TFTP_FIRST,
    EFI_PXE_BASE_CODE_TFTP_GET_FILE_SIZE,
    EFI_PXE_BASE_CODE_TFTP_READ_FILE,
    EFI_PXE_BASE_CODE_TFTP_WRITE_FILE,
    EFI_PXE_BASE_CODE_TFTP_READ_DIRECTORY,
    EFI_PXE_BASE_CODE_MTFTP_GET_FILE_SIZE,
    EFI_PXE_BASE_CODE_MTFTP_READ_FILE,
    EFI_PXE_BASE_CODE_MTFTP_READ_DIRECTORY,
    EFI_PXE_BASE_CODE_MTFTP_LAST
} EFI_PXE_BASE_CODE_TFTP_OPCODE;

typedef struct {
    EFI_IP_ADDRESS   MCastIp;
    EFI_PXE_BASE_CODE_UDP_PORT  CPort;
    EFI_PXE_BASE_CODE_UDP_PORT  SPort;
    UINT16                      ListenTimeout;
    UINT16                      TransmitTimeout;
} EFI_PXE_BASE_CODE_MTFTP_INFO;

//
// PXE Base Code Mode structure
//

#define EFI_PXE_BASE_CODE_MAX_ARP_ENTRIES       8
#define EFI_PXE_BASE_CODE_MAX_ROUTE_ENTRIES     8

typedef struct {
    BOOLEAN                         Started;
    BOOLEAN                         Ipv6Available;
    BOOLEAN                         Ipv6Supported;
    BOOLEAN                         UsingIpv6;
    BOOLEAN                         BisSupported;
    BOOLEAN                         BisDetected;
    BOOLEAN                         AutoArp;
    BOOLEAN                         SendGUID;
    BOOLEAN                         DhcpDiscoverValid;
    BOOLEAN                         DhcpAckReceived;
    BOOLEAN                         ProxyOfferReceived;
    BOOLEAN                         PxeDiscoverValid;
    BOOLEAN                         PxeReplyReceived;
    BOOLEAN                         PxeBisReplyReceived;
    BOOLEAN                         IcmpErrorReceived;
    BOOLEAN                         TftpErrorReceived;
    BOOLEAN                         MakeCallbacks;
    UINT8                           TTL;
    UINT8                           ToS;
    EFI_IP_ADDRESS                  StationIp;
    EFI_IP_ADDRESS                  SubnetMask;
    EFI_PXE_BASE_CODE_PACKET        DhcpDiscover;
    EFI_PXE_BASE_CODE_PACKET        DhcpAck;
    EFI_PXE_BASE_CODE_PACKET        ProxyOffer;
    EFI_PXE_BASE_CODE_PACKET        PxeDiscover;
    EFI_PXE_BASE_CODE_PACKET        PxeReply;
    EFI_PXE_BASE_CODE_PACKET        PxeBisReply;
    EFI_PXE_BASE_CODE_IP_FILTER     IpFilter;
    UINT32                          ArpCacheEntries;
    EFI_PXE_BASE_CODE_ARP_ENTRY     ArpCache[EFI_PXE_BASE_CODE_MAX_ARP_ENTRIES];
    UINT32                          RouteTableEntries;
    EFI_PXE_BASE_CODE_ROUTE_ENTRY   RouteTable[EFI_PXE_BASE_CODE_MAX_ROUTE_ENTRIES];
    EFI_PXE_BASE_CODE_ICMP_ERROR    IcmpError;
    EFI_PXE_BASE_CODE_TFTP_ERROR    TftpError;
} EFI_PXE_BASE_CODE_MODE;

//
// PXE Base Code Interface Function definitions
//

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_START) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN BOOLEAN                      UseIpv6
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_STOP) (
    IN struct _EFI_PXE_BASE_CODE    *This
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_DHCP) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN BOOLEAN                      SortOffers
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_DISCOVER) (
    IN struct _EFI_PXE_BASE_CODE            *This,
    IN UINT16                               Type,
    IN UINT16                               *Layer,
    IN BOOLEAN                              UseBis,
    IN OUT EFI_PXE_BASE_CODE_DISCOVER_INFO  *Info   OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_MTFTP) (
    IN struct _EFI_PXE_BASE_CODE        *This,
    IN EFI_PXE_BASE_CODE_TFTP_OPCODE    Operation,
    IN OUT VOID                         *BufferPtr  OPTIONAL,
    IN BOOLEAN                          Overwrite,
    IN OUT UINT64                       *BufferSize,
    IN UINTN                            *BlockSize  OPTIONAL,
    IN EFI_IP_ADDRESS                   *ServerIp,
    IN UINT8                            *Filename,
    IN EFI_PXE_BASE_CODE_MTFTP_INFO     *Info       OPTIONAL,
    IN BOOLEAN                          DontUseBuffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_UDP_WRITE) (
    IN struct _EFI_PXE_BASE_CODE        *This,
    IN UINT16                           OpFlags,
    IN EFI_IP_ADDRESS                   *DestIp,
    IN EFI_PXE_BASE_CODE_UDP_PORT       *DestPort,
    IN EFI_IP_ADDRESS                   *GatewayIp,  OPTIONAL
    IN EFI_IP_ADDRESS                   *SrcIp,      OPTIONAL
    IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *SrcPort,    OPTIONAL
    IN UINTN                            *HeaderSize, OPTIONAL
    IN VOID                             *HeaderPtr,  OPTIONAL
    IN UINTN                            *BufferSize,
    IN VOID                             *BufferPtr
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_UDP_READ) (
    IN struct _EFI_PXE_BASE_CODE        *This,
    IN UINT16                           OpFlags,
    IN OUT EFI_IP_ADDRESS               *DestIp,      OPTIONAL
    IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *DestPort,    OPTIONAL
    IN OUT EFI_IP_ADDRESS               *SrcIp,       OPTIONAL
    IN OUT EFI_PXE_BASE_CODE_UDP_PORT   *SrcPort,     OPTIONAL
    IN UINTN                            *HeaderSize,  OPTIONAL
    IN VOID                             *HeaderPtr,   OPTIONAL
    IN OUT UINTN                        *BufferSize,
    IN VOID                             *BufferPtr
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_SET_IP_FILTER) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN EFI_PXE_BASE_CODE_IP_FILTER  *NewFilter
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_ARP) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN EFI_IP_ADDRESS               *IpAddr,      
    IN EFI_MAC_ADDRESS              *MacAddr      OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_SET_PARAMETERS) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN BOOLEAN                      *NewAutoArp,    OPTIONAL
    IN BOOLEAN                      *NewSendGUID,   OPTIONAL
    IN UINT8                        *NewTTL,        OPTIONAL
    IN UINT8                        *NewToS,        OPTIONAL
    IN BOOLEAN                      *NewMakeCallback    OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_SET_STATION_IP) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    IN EFI_IP_ADDRESS               *NewStationIp,  OPTIONAL
    IN EFI_IP_ADDRESS               *NewSubnetMask  OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_PXE_BASE_CODE_SET_PACKETS) (
    IN struct _EFI_PXE_BASE_CODE    *This,
    BOOLEAN                         *NewDhcpDiscoverValid,  OPTIONAL
    BOOLEAN                         *NewDhcpAckReceived,    OPTIONAL
    BOOLEAN                         *NewProxyOfferReceived, OPTIONAL
    BOOLEAN                         *NewPxeDiscoverValid,   OPTIONAL
    BOOLEAN                         *NewPxeReplyReceived,   OPTIONAL
    BOOLEAN                         *NewPxeBisReplyReceived,OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewDhcpDiscover, OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewDhcpAck,      OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewProxyOffer,   OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewPxeDiscover,  OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewPxeReply,     OPTIONAL
    IN EFI_PXE_BASE_CODE_PACKET     *NewPxeBisReply   OPTIONAL
    );

//
// PXE Base Code Protocol structure
//

#define EFI_PXE_BASE_CODE_INTERFACE_REVISION    0x00010000

typedef struct _EFI_PXE_BASE_CODE {
    UINT64                              Revision;
    EFI_PXE_BASE_CODE_START             Start;
    EFI_PXE_BASE_CODE_STOP              Stop;
    EFI_PXE_BASE_CODE_DHCP              Dhcp;
    EFI_PXE_BASE_CODE_DISCOVER          Discover;
    EFI_PXE_BASE_CODE_MTFTP             Mtftp;
    EFI_PXE_BASE_CODE_UDP_WRITE         UdpWrite;
    EFI_PXE_BASE_CODE_UDP_READ          UdpRead;
    EFI_PXE_BASE_CODE_SET_IP_FILTER     SetIpFilter;
    EFI_PXE_BASE_CODE_ARP               Arp;
    EFI_PXE_BASE_CODE_SET_PARAMETERS    SetParameters;
    EFI_PXE_BASE_CODE_SET_STATION_IP    SetStationIp;
    EFI_PXE_BASE_CODE_SET_PACKETS       SetPackets;
    EFI_PXE_BASE_CODE_MODE              *Mode;
} EFI_PXE_BASE_CODE;

//
// Call Back Definitions
//

#define EFI_PXE_BASE_CODE_CALLBACK_PROTOCOL \
    { 0x245dca21, 0xfb7b, 0x11d3, {0x8f, 0x01, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

//
// Revision Number
//

#define EFI_PXE_BASE_CODE_CALLBACK_INTERFACE_REVISION   0x00010000

INTERFACE_DECL(_EFI_PXE_BASE_CODE_CALLBACK);

typedef enum {
    EFI_PXE_BASE_CODE_FUNCTION_FIRST,
    EFI_PXE_BASE_CODE_FUNCTION_DHCP,
    EFI_PXE_BASE_CODE_FUNCTION_DISCOVER,
    EFI_PXE_BASE_CODE_FUNCTION_MTFTP,
    EFI_PXE_BASE_CODE_FUNCTION_UDP_WRITE,
    EFI_PXE_BASE_CODE_FUNCTION_UDP_READ,
    EFI_PXE_BASE_CODE_FUNCTION_ARP,
    EFI_PXE_BASE_CODE_FUNCTION_IGMP,
    EFI_PXE_BASE_CODE_PXE_FUNCTION_LAST
} EFI_PXE_BASE_CODE_FUNCTION;

typedef enum {
    EFI_PXE_BASE_CODE_CALLBACK_STATUS_FIRST,
    EFI_PXE_BASE_CODE_CALLBACK_STATUS_CONTINUE,
    EFI_PXE_BASE_CODE_CALLBACK_STATUS_ABORT,
    EFI_PXE_BASE_CODE_CALLBACK_STATUS_LAST
} EFI_PXE_BASE_CODE_CALLBACK_STATUS;

typedef
EFI_PXE_BASE_CODE_CALLBACK_STATUS 
(EFIAPI *EFI_PXE_CALLBACK) (
    IN struct _EFI_PXE_BASE_CODE_CALLBACK   *This,
    IN EFI_PXE_BASE_CODE_FUNCTION           Function,
    IN BOOLEAN                              Received,
    IN UINT32                               PacketLen,
    IN EFI_PXE_BASE_CODE_PACKET             *Packet     OPTIONAL
    );

typedef struct _EFI_PXE_BASE_CODE_CALLBACK {
    UINT64                      Revision;
    EFI_PXE_CALLBACK            Callback;
} EFI_PXE_BASE_CODE_CALLBACK;

#endif /* _EFIPXEBC_H */
