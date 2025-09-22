/* $FreeBSD: head/sys/boot/efi/include/efinet.h 163898 2006-11-02 02:42:48Z marcel $ */
#ifndef _EFINET_H
#define _EFINET_H


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
    efinet.h

Abstract:
    EFI Simple Network protocol

Revision History
--*/


///////////////////////////////////////////////////////////////////////////////
//
//      Simple Network Protocol
//

#define EFI_SIMPLE_NETWORK_PROTOCOL \
    { 0xA19832B9, 0xAC25, 0x11D3, { 0x9A, 0x2D, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }


INTERFACE_DECL(_EFI_SIMPLE_NETWORK);

///////////////////////////////////////////////////////////////////////////////
//

typedef struct {
    //
    // Total number of frames received.  Includes frames with errors and
    // dropped frames.
    //
    UINT64  RxTotalFrames;

    //
    // Number of valid frames received and copied into receive buffers.
    //
    UINT64  RxGoodFrames;

    //
    // Number of frames below the minimum length for the media.
    // This would be <64 for ethernet.
    //
    UINT64  RxUndersizeFrames;

    //
    // Number of frames longer than the maxminum length for the
    // media.  This would be >1500 for ethernet.
    //
    UINT64  RxOversizeFrames;

    //
    // Valid frames that were dropped because receive buffers were full.
    //
    UINT64  RxDroppedFrames;

    //
    // Number of valid unicast frames received and not dropped.
    //
    UINT64  RxUnicastFrames;

    //
    // Number of valid broadcast frames received and not dropped.
    //
    UINT64  RxBroadcastFrames;

    //
    // Number of valid multicast frames received and not dropped.
    //
    UINT64  RxMulticastFrames;

    //
    // Number of frames w/ CRC or alignment errors.
    //
    UINT64  RxCrcErrorFrames;

    //
    // Total number of bytes received.  Includes frames with errors
    // and dropped frames.
    //
    UINT64  RxTotalBytes;

    //
    // Transmit statistics.
    //
    UINT64  TxTotalFrames;
    UINT64  TxGoodFrames;
    UINT64  TxUndersizeFrames;
    UINT64  TxOversizeFrames;
    UINT64  TxDroppedFrames;
    UINT64  TxUnicastFrames;
    UINT64  TxBroadcastFrames;
    UINT64  TxMulticastFrames;
    UINT64  TxCrcErrorFrames;
    UINT64  TxTotalBytes;

    //
    // Number of collisions detection on this subnet.
    //
    UINT64  Collisions;

    //
    // Number of frames destined for unsupported protocol.
    //
    UINT64  UnsupportedProtocol;

} EFI_NETWORK_STATISTICS;

///////////////////////////////////////////////////////////////////////////////
//

typedef enum {
    EfiSimpleNetworkStopped,
    EfiSimpleNetworkStarted,
    EfiSimpleNetworkInitialized,
    EfiSimpleNetworkMaxState
} EFI_SIMPLE_NETWORK_STATE;

///////////////////////////////////////////////////////////////////////////////
//

#define EFI_SIMPLE_NETWORK_RECEIVE_UNICAST               0x01
#define EFI_SIMPLE_NETWORK_RECEIVE_MULTICAST             0x02
#define EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST             0x04
#define EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS           0x08
#define EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS_MULTICAST 0x10

///////////////////////////////////////////////////////////////////////////////
//

#define EFI_SIMPLE_NETWORK_RECEIVE_INTERRUPT        0x01
#define EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT       0x02
#define EFI_SIMPLE_NETWORK_COMMAND_INTERRUPT        0x04
#define EFI_SIMPLE_NETWORK_SOFTWARE_INTERRUPT       0x08

///////////////////////////////////////////////////////////////////////////////
//
#define MAX_MCAST_FILTER_CNT    16
typedef struct {
    UINT32                      State;
    UINT32                      HwAddressSize;
    UINT32                      MediaHeaderSize;
    UINT32                      MaxPacketSize;
    UINT32                      NvRamSize;
    UINT32                      NvRamAccessSize;
    UINT32                      ReceiveFilterMask;
    UINT32                      ReceiveFilterSetting;
    UINT32                      MaxMCastFilterCount;
    UINT32                      MCastFilterCount;
    EFI_MAC_ADDRESS             MCastFilter[MAX_MCAST_FILTER_CNT];
    EFI_MAC_ADDRESS             CurrentAddress;
    EFI_MAC_ADDRESS             BroadcastAddress;
    EFI_MAC_ADDRESS             PermanentAddress;
    UINT8                       IfType;
    BOOLEAN                     MacAddressChangeable;
    BOOLEAN                     MultipleTxSupported;
    BOOLEAN                     MediaPresentSupported;
    BOOLEAN                     MediaPresent;
} EFI_SIMPLE_NETWORK_MODE;

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_START) (
    IN struct _EFI_SIMPLE_NETWORK  *This
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_STOP) (
    IN struct _EFI_SIMPLE_NETWORK  *This
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_INITIALIZE) (
    IN struct _EFI_SIMPLE_NETWORK  *This,
    IN UINTN                       ExtraRxBufferSize  OPTIONAL,
    IN UINTN                       ExtraTxBufferSize  OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_RESET) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN BOOLEAN                      ExtendedVerification
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_SHUTDOWN) (
    IN struct _EFI_SIMPLE_NETWORK  *This
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_RECEIVE_FILTERS) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN UINT32                       Enable,
    IN UINT32                       Disable,
    IN BOOLEAN                      ResetMCastFilter,
    IN UINTN                        MCastFilterCnt     OPTIONAL,
    IN EFI_MAC_ADDRESS              *MCastFilter       OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_STATION_ADDRESS) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN BOOLEAN                      Reset,
    IN EFI_MAC_ADDRESS              *New      OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_STATISTICS) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN BOOLEAN                      Reset,
    IN OUT UINTN                    *StatisticsSize   OPTIONAL,
    OUT EFI_NETWORK_STATISTICS      *StatisticsTable  OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_MCAST_IP_TO_MAC) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN BOOLEAN                      IPv6,
    IN EFI_IP_ADDRESS               *IP,
    OUT EFI_MAC_ADDRESS             *MAC
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_NVDATA) (
    IN struct _EFI_SIMPLE_NETWORK  *This,
    IN BOOLEAN                     ReadWrite,
    IN UINTN                       Offset,
    IN UINTN                       BufferSize,
    IN OUT VOID                    *Buffer
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_GET_STATUS) (
    IN struct _EFI_SIMPLE_NETWORK  *This,
    OUT UINT32                     *InterruptStatus  OPTIONAL,
    OUT VOID                       **TxBuf           OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_TRANSMIT) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    IN UINTN                        HeaderSize,
    IN UINTN                        BufferSize,
    IN VOID                         *Buffer,
    IN EFI_MAC_ADDRESS              *SrcAddr     OPTIONAL,
    IN EFI_MAC_ADDRESS              *DestAddr    OPTIONAL,
    IN UINT16                       *Protocol    OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

typedef
EFI_STATUS
(EFIAPI *EFI_SIMPLE_NETWORK_RECEIVE) (
    IN struct _EFI_SIMPLE_NETWORK   *This,
    OUT UINTN                       *HeaderSize  OPTIONAL,
    IN OUT UINTN                    *BufferSize,
    OUT VOID                        *Buffer,
    OUT EFI_MAC_ADDRESS             *SrcAddr     OPTIONAL,
    OUT EFI_MAC_ADDRESS             *DestAddr    OPTIONAL,
    OUT UINT16                      *Protocol    OPTIONAL
);

///////////////////////////////////////////////////////////////////////////////
//

#define EFI_SIMPLE_NETWORK_INTERFACE_REVISION   0x00010000

typedef struct _EFI_SIMPLE_NETWORK {
    UINT64                              Revision;
    EFI_SIMPLE_NETWORK_START            Start;
    EFI_SIMPLE_NETWORK_STOP             Stop;
    EFI_SIMPLE_NETWORK_INITIALIZE       Initialize;
    EFI_SIMPLE_NETWORK_RESET            Reset;
    EFI_SIMPLE_NETWORK_SHUTDOWN         Shutdown;
    EFI_SIMPLE_NETWORK_RECEIVE_FILTERS  ReceiveFilters;
    EFI_SIMPLE_NETWORK_STATION_ADDRESS  StationAddress;
    EFI_SIMPLE_NETWORK_STATISTICS       Statistics;
    EFI_SIMPLE_NETWORK_MCAST_IP_TO_MAC  MCastIpToMac;
    EFI_SIMPLE_NETWORK_NVDATA           NvData;
    EFI_SIMPLE_NETWORK_GET_STATUS       GetStatus;
    EFI_SIMPLE_NETWORK_TRANSMIT         Transmit;
    EFI_SIMPLE_NETWORK_RECEIVE          Receive;
    EFI_EVENT                           WaitForPacket;
    EFI_SIMPLE_NETWORK_MODE             *Mode;
} EFI_SIMPLE_NETWORK;

#endif /* _EFINET_H */
