/** @file
  This header file contains all of the PXE type definitions,
  structure prototypes, global variables and constants that
  are needed for porting PXE to EFI.

Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

  @par Revision Reference:
  32/64-bit PXE specification:
  alpha-4, 99-Dec-17.

**/

#ifndef __EFI_PXE_H__
#define __EFI_PXE_H__

#pragma pack(1)

#define PXE_BUSTYPE(a, b, c, d) \
    ( \
      (((PXE_UINT32) (d) & 0xFF) << 24) | (((PXE_UINT32) (c) & 0xFF) << 16) | (((PXE_UINT32) (b) & 0xFF) << 8) | \
        ((PXE_UINT32) (a) & 0xFF) \
    )

///
/// UNDI ROM ID and devive ID signature.
///
#define PXE_BUSTYPE_PXE PXE_BUSTYPE ('!', 'P', 'X', 'E')

///
/// BUS ROM ID signatures.
///
#define PXE_BUSTYPE_PCI     PXE_BUSTYPE ('P', 'C', 'I', 'R')
#define PXE_BUSTYPE_PC_CARD PXE_BUSTYPE ('P', 'C', 'C', 'R')
#define PXE_BUSTYPE_USB     PXE_BUSTYPE ('U', 'S', 'B', 'R')
#define PXE_BUSTYPE_1394    PXE_BUSTYPE ('1', '3', '9', '4')

#define PXE_SWAP_UINT16(n)  ((((PXE_UINT16) (n) & 0x00FF) << 8) | (((PXE_UINT16) (n) & 0xFF00) >> 8))

#define PXE_SWAP_UINT32(n) \
  ((((PXE_UINT32)(n) & 0x000000FF) << 24) | \
   (((PXE_UINT32)(n) & 0x0000FF00) << 8)  | \
   (((PXE_UINT32)(n) & 0x00FF0000) >> 8)  | \
   (((PXE_UINT32)(n) & 0xFF000000) >> 24))

#define PXE_SWAP_UINT64(n) \
  ((((PXE_UINT64)(n) & 0x00000000000000FFULL) << 56) | \
   (((PXE_UINT64)(n) & 0x000000000000FF00ULL) << 40) | \
   (((PXE_UINT64)(n) & 0x0000000000FF0000ULL) << 24) | \
   (((PXE_UINT64)(n) & 0x00000000FF000000ULL) << 8)  | \
   (((PXE_UINT64)(n) & 0x000000FF00000000ULL) >> 8)  | \
   (((PXE_UINT64)(n) & 0x0000FF0000000000ULL) >> 24) | \
   (((PXE_UINT64)(n) & 0x00FF000000000000ULL) >> 40) | \
   (((PXE_UINT64)(n) & 0xFF00000000000000ULL) >> 56))


#define PXE_CPBSIZE_NOT_USED  0               ///< zero
#define PXE_DBSIZE_NOT_USED   0               ///< zero
#define PXE_CPBADDR_NOT_USED  (PXE_UINT64) 0  ///< zero
#define PXE_DBADDR_NOT_USED   (PXE_UINT64) 0  ///< zero
#define PXE_CONST             CONST

#define PXE_VOLATILE          volatile

typedef VOID           PXE_VOID;
typedef UINT8          PXE_UINT8;
typedef UINT16         PXE_UINT16;
typedef UINT32         PXE_UINT32;
typedef UINTN          PXE_UINTN;

///
/// Typedef unsigned long PXE_UINT64.
///
typedef UINT64      PXE_UINT64;

typedef PXE_UINT8 PXE_BOOL;
#define PXE_FALSE 0            ///< zero
#define PXE_TRUE  (!PXE_FALSE)

typedef PXE_UINT16      PXE_OPCODE;

///
/// Return UNDI operational state.
///
#define PXE_OPCODE_GET_STATE  0x0000

///
/// Change UNDI operational state from Stopped to Started.
///
#define PXE_OPCODE_START  0x0001

///
/// Change UNDI operational state from Started to Stopped.
///
#define PXE_OPCODE_STOP 0x0002

///
/// Get UNDI initialization information.
///
#define PXE_OPCODE_GET_INIT_INFO  0x0003

///
/// Get NIC configuration information.
///
#define PXE_OPCODE_GET_CONFIG_INFO  0x0004

///
/// Changed UNDI operational state from Started to Initialized.
///
#define PXE_OPCODE_INITIALIZE 0x0005

///
/// Re-initialize the NIC H/W.
///
#define PXE_OPCODE_RESET  0x0006

///
/// Change the UNDI operational state from Initialized to Started.
///
#define PXE_OPCODE_SHUTDOWN 0x0007

///
/// Read & change state of external interrupt enables.
///
#define PXE_OPCODE_INTERRUPT_ENABLES  0x0008

///
/// Read & change state of packet receive filters.
///
#define PXE_OPCODE_RECEIVE_FILTERS  0x0009

///
/// Read & change station MAC address.
///
#define PXE_OPCODE_STATION_ADDRESS  0x000A

///
/// Read traffic statistics.
///
#define PXE_OPCODE_STATISTICS 0x000B

///
/// Convert multicast IP address to multicast MAC address.
///
#define PXE_OPCODE_MCAST_IP_TO_MAC  0x000C

///
/// Read or change non-volatile storage on the NIC.
///
#define PXE_OPCODE_NVDATA 0x000D

///
/// Get & clear interrupt status.
///
#define PXE_OPCODE_GET_STATUS 0x000E

///
/// Fill media header in packet for transmit.
///
#define PXE_OPCODE_FILL_HEADER  0x000F

///
/// Transmit packet(s).
///
#define PXE_OPCODE_TRANSMIT 0x0010

///
/// Receive packet.
///
#define PXE_OPCODE_RECEIVE  0x0011

///
/// Last valid PXE UNDI OpCode number.
///
#define PXE_OPCODE_LAST_VALID 0x0011

typedef PXE_UINT16  PXE_OPFLAGS;

#define PXE_OPFLAGS_NOT_USED  0x0000

//
// //////////////////////////////////////
// UNDI Get State
//
// No OpFlags

////////////////////////////////////////
// UNDI Start
//
// No OpFlags

////////////////////////////////////////
// UNDI Stop
//
// No OpFlags

////////////////////////////////////////
// UNDI Get Init Info
//
// No Opflags

////////////////////////////////////////
// UNDI Get Config Info
//
// No Opflags

///
/// UNDI Initialize
///
#define PXE_OPFLAGS_INITIALIZE_CABLE_DETECT_MASK    0x0001
#define PXE_OPFLAGS_INITIALIZE_DETECT_CABLE         0x0000
#define PXE_OPFLAGS_INITIALIZE_DO_NOT_DETECT_CABLE  0x0001

///
///
/// UNDI Reset
///
#define PXE_OPFLAGS_RESET_DISABLE_INTERRUPTS  0x0001
#define PXE_OPFLAGS_RESET_DISABLE_FILTERS     0x0002

///
/// UNDI Shutdown.
///
/// No OpFlags.

///
/// UNDI Interrupt Enables.
///
///
/// Select whether to enable or disable external interrupt signals.
/// Setting both enable and disable will return PXE_STATCODE_INVALID_OPFLAGS.
///
#define PXE_OPFLAGS_INTERRUPT_OPMASK  0xC000
#define PXE_OPFLAGS_INTERRUPT_ENABLE  0x8000
#define PXE_OPFLAGS_INTERRUPT_DISABLE 0x4000
#define PXE_OPFLAGS_INTERRUPT_READ    0x0000

///
/// Enable receive interrupts.  An external interrupt will be generated
/// after a complete non-error packet has been received.
///
#define PXE_OPFLAGS_INTERRUPT_RECEIVE 0x0001

///
/// Enable transmit interrupts.  An external interrupt will be generated
/// after a complete non-error packet has been transmitted.
///
#define PXE_OPFLAGS_INTERRUPT_TRANSMIT  0x0002

///
/// Enable command interrupts.  An external interrupt will be generated
/// when command execution stops.
///
#define PXE_OPFLAGS_INTERRUPT_COMMAND 0x0004

///
/// Generate software interrupt.  Setting this bit generates an external
/// interrupt, if it is supported by the hardware.
///
#define PXE_OPFLAGS_INTERRUPT_SOFTWARE  0x0008

///
/// UNDI Receive Filters.
///
///
/// Select whether to enable or disable receive filters.
/// Setting both enable and disable will return PXE_STATCODE_INVALID_OPCODE.
///
#define PXE_OPFLAGS_RECEIVE_FILTER_OPMASK   0xC000
#define PXE_OPFLAGS_RECEIVE_FILTER_ENABLE   0x8000
#define PXE_OPFLAGS_RECEIVE_FILTER_DISABLE  0x4000
#define PXE_OPFLAGS_RECEIVE_FILTER_READ     0x0000

///
/// To reset the contents of the multicast MAC address filter list,
/// set this OpFlag:
///
#define PXE_OPFLAGS_RECEIVE_FILTER_RESET_MCAST_LIST 0x2000

///
/// Enable unicast packet receiving.  Packets sent to the current station
/// MAC address will be received.
///
#define PXE_OPFLAGS_RECEIVE_FILTER_UNICAST  0x0001

///
/// Enable broadcast packet receiving.  Packets sent to the broadcast
/// MAC address will be received.
///
#define PXE_OPFLAGS_RECEIVE_FILTER_BROADCAST  0x0002

///
/// Enable filtered multicast packet receiving.  Packets sent to any
/// of the multicast MAC addresses in the multicast MAC address filter
/// list will be received.  If the filter list is empty, no multicast
///
#define PXE_OPFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST 0x0004

///
/// Enable promiscuous packet receiving.  All packets will be received.
///
#define PXE_OPFLAGS_RECEIVE_FILTER_PROMISCUOUS  0x0008

///
/// Enable promiscuous multicast packet receiving.  All multicast
/// packets will be received.
///
#define PXE_OPFLAGS_RECEIVE_FILTER_ALL_MULTICAST  0x0010

///
/// UNDI Station Address.
///
#define PXE_OPFLAGS_STATION_ADDRESS_READ   0x0000
#define PXE_OPFLAGS_STATION_ADDRESS_WRITE  0x0000
#define PXE_OPFLAGS_STATION_ADDRESS_RESET  0x0001

///
/// UNDI Statistics.
///
#define PXE_OPFLAGS_STATISTICS_READ   0x0000
#define PXE_OPFLAGS_STATISTICS_RESET  0x0001

///
/// UNDI MCast IP to MAC.
///
///
/// Identify the type of IP address in the CPB.
///
#define PXE_OPFLAGS_MCAST_IP_TO_MAC_OPMASK  0x0003
#define PXE_OPFLAGS_MCAST_IPV4_TO_MAC       0x0000
#define PXE_OPFLAGS_MCAST_IPV6_TO_MAC       0x0001

///
/// UNDI NvData.
///
///
/// Select the type of non-volatile data operation.
///
#define PXE_OPFLAGS_NVDATA_OPMASK 0x0001
#define PXE_OPFLAGS_NVDATA_READ   0x0000
#define PXE_OPFLAGS_NVDATA_WRITE  0x0001

///
/// UNDI Get Status.
///
///
/// Return current interrupt status.  This will also clear any interrupts
/// that are currently set.  This can be used in a polling routine.  The
/// interrupt flags are still set and cleared even when the interrupts
/// are disabled.
///
#define PXE_OPFLAGS_GET_INTERRUPT_STATUS  0x0001

///
/// Return list of transmitted buffers for recycling.  Transmit buffers
/// must not be changed or unallocated until they have recycled.  After
/// issuing a transmit command, wait for a transmit complete interrupt.
/// When a transmit complete interrupt is received, read the transmitted
/// buffers.  Do not plan on getting one buffer per interrupt.  Some
/// NICs and UNDIs may transmit multiple buffers per interrupt.
///
#define PXE_OPFLAGS_GET_TRANSMITTED_BUFFERS 0x0002

///
/// Return current media status.
///
#define PXE_OPFLAGS_GET_MEDIA_STATUS    0x0004

///
/// UNDI Fill Header.
///
#define PXE_OPFLAGS_FILL_HEADER_OPMASK      0x0001
#define PXE_OPFLAGS_FILL_HEADER_FRAGMENTED  0x0001
#define PXE_OPFLAGS_FILL_HEADER_WHOLE       0x0000

///
/// UNDI Transmit.
///
///
/// S/W UNDI only.  Return after the packet has been transmitted.  A
/// transmit complete interrupt will still be generated and the transmit
/// buffer will have to be recycled.
///
#define PXE_OPFLAGS_SWUNDI_TRANSMIT_OPMASK  0x0001
#define PXE_OPFLAGS_TRANSMIT_BLOCK          0x0001
#define PXE_OPFLAGS_TRANSMIT_DONT_BLOCK     0x0000

#define PXE_OPFLAGS_TRANSMIT_OPMASK     0x0002
#define PXE_OPFLAGS_TRANSMIT_FRAGMENTED 0x0002
#define PXE_OPFLAGS_TRANSMIT_WHOLE      0x0000

///
/// UNDI Receive.
///
/// No OpFlags.
///

///
/// PXE STATFLAGS.
///
typedef PXE_UINT16  PXE_STATFLAGS;

#define PXE_STATFLAGS_INITIALIZE  0x0000

///
/// Common StatFlags that can be returned by all commands.
///
///
/// The COMMAND_COMPLETE and COMMAND_FAILED status flags must be
/// implemented by all UNDIs.  COMMAND_QUEUED is only needed by UNDIs
/// that support command queuing.
///
#define PXE_STATFLAGS_STATUS_MASK       0xC000
#define PXE_STATFLAGS_COMMAND_COMPLETE  0xC000
#define PXE_STATFLAGS_COMMAND_FAILED    0x8000
#define PXE_STATFLAGS_COMMAND_QUEUED    0x4000

///
/// UNDI Get State.
///
#define PXE_STATFLAGS_GET_STATE_MASK        0x0003
#define PXE_STATFLAGS_GET_STATE_INITIALIZED 0x0002
#define PXE_STATFLAGS_GET_STATE_STARTED     0x0001
#define PXE_STATFLAGS_GET_STATE_STOPPED     0x0000

///
/// UNDI Start.
///
/// No additional StatFlags.
///

///
/// UNDI Get Init Info.
///
#define PXE_STATFLAGS_CABLE_DETECT_MASK           0x0001
#define PXE_STATFLAGS_CABLE_DETECT_NOT_SUPPORTED  0x0000
#define PXE_STATFLAGS_CABLE_DETECT_SUPPORTED      0x0001

#define PXE_STATFLAGS_GET_STATUS_NO_MEDIA_MASK           0x0002
#define PXE_STATFLAGS_GET_STATUS_NO_MEDIA_NOT_SUPPORTED  0x0000
#define PXE_STATFLAGS_GET_STATUS_NO_MEDIA_SUPPORTED      0x0002

///
/// UNDI Initialize.
///
#define PXE_STATFLAGS_INITIALIZED_NO_MEDIA  0x0001

///
/// UNDI Reset.
///
#define PXE_STATFLAGS_RESET_NO_MEDIA  0x0001

///
/// UNDI Shutdown.
///
/// No additional StatFlags.

///
/// UNDI Interrupt Enables.
///
///
/// If set, receive interrupts are enabled.
///
#define PXE_STATFLAGS_INTERRUPT_RECEIVE 0x0001

///
/// If set, transmit interrupts are enabled.
///
#define PXE_STATFLAGS_INTERRUPT_TRANSMIT  0x0002

///
/// If set, command interrupts are enabled.
///
#define PXE_STATFLAGS_INTERRUPT_COMMAND 0x0004

///
/// UNDI Receive Filters.
///

///
/// If set, unicast packets will be received.
///
#define PXE_STATFLAGS_RECEIVE_FILTER_UNICAST  0x0001

///
/// If set, broadcast packets will be received.
///
#define PXE_STATFLAGS_RECEIVE_FILTER_BROADCAST  0x0002

///
/// If set, multicast packets that match up with the multicast address
/// filter list will be received.
///
#define PXE_STATFLAGS_RECEIVE_FILTER_FILTERED_MULTICAST 0x0004

///
/// If set, all packets will be received.
///
#define PXE_STATFLAGS_RECEIVE_FILTER_PROMISCUOUS  0x0008

///
/// If set, all multicast packets will be received.
///
#define PXE_STATFLAGS_RECEIVE_FILTER_ALL_MULTICAST  0x0010

///
/// UNDI Station Address.
///
/// No additional StatFlags.
///

///
/// UNDI Statistics.
///
/// No additional StatFlags.
///

///
//// UNDI MCast IP to MAC.
////
//// No additional StatFlags.

///
/// UNDI NvData.
///
/// No additional StatFlags.
///

///
/// UNDI Get Status.
///

///
/// Use to determine if an interrupt has occurred.
///
#define PXE_STATFLAGS_GET_STATUS_INTERRUPT_MASK 0x000F
#define PXE_STATFLAGS_GET_STATUS_NO_INTERRUPTS  0x0000

///
/// If set, at least one receive interrupt occurred.
///
#define PXE_STATFLAGS_GET_STATUS_RECEIVE  0x0001

///
/// If set, at least one transmit interrupt occurred.
///
#define PXE_STATFLAGS_GET_STATUS_TRANSMIT 0x0002

///
/// If set, at least one command interrupt occurred.
///
#define PXE_STATFLAGS_GET_STATUS_COMMAND  0x0004

///
/// If set, at least one software interrupt occurred.
///
#define PXE_STATFLAGS_GET_STATUS_SOFTWARE 0x0008

///
/// This flag is set if the transmitted buffer queue is empty.  This flag
/// will be set if all transmitted buffer addresses get written into the DB.
///
#define PXE_STATFLAGS_GET_STATUS_TXBUF_QUEUE_EMPTY  0x0010

///
/// This flag is set if no transmitted buffer addresses were written
/// into the DB.  (This could be because DBsize was too small.)
///
#define PXE_STATFLAGS_GET_STATUS_NO_TXBUFS_WRITTEN  0x0020

///
/// This flag is set if there is no media detected.
///
#define PXE_STATFLAGS_GET_STATUS_NO_MEDIA  0x0040

///
/// UNDI Fill Header.
///
/// No additional StatFlags.
///

///
/// UNDI Transmit.
///
/// No additional StatFlags.

///
/// UNDI Receive
///.

///
/// No additional StatFlags.
///
typedef PXE_UINT16  PXE_STATCODE;

#define PXE_STATCODE_INITIALIZE 0x0000

///
/// Common StatCodes returned by all UNDI commands, UNDI protocol functions
/// and BC protocol functions.
///
#define PXE_STATCODE_SUCCESS              0x0000

#define PXE_STATCODE_INVALID_CDB          0x0001
#define PXE_STATCODE_INVALID_CPB          0x0002
#define PXE_STATCODE_BUSY                 0x0003
#define PXE_STATCODE_QUEUE_FULL           0x0004
#define PXE_STATCODE_ALREADY_STARTED      0x0005
#define PXE_STATCODE_NOT_STARTED          0x0006
#define PXE_STATCODE_NOT_SHUTDOWN         0x0007
#define PXE_STATCODE_ALREADY_INITIALIZED  0x0008
#define PXE_STATCODE_NOT_INITIALIZED      0x0009
#define PXE_STATCODE_DEVICE_FAILURE       0x000A
#define PXE_STATCODE_NVDATA_FAILURE       0x000B
#define PXE_STATCODE_UNSUPPORTED          0x000C
#define PXE_STATCODE_BUFFER_FULL          0x000D
#define PXE_STATCODE_INVALID_PARAMETER    0x000E
#define PXE_STATCODE_INVALID_UNDI         0x000F
#define PXE_STATCODE_IPV4_NOT_SUPPORTED   0x0010
#define PXE_STATCODE_IPV6_NOT_SUPPORTED   0x0011
#define PXE_STATCODE_NOT_ENOUGH_MEMORY    0x0012
#define PXE_STATCODE_NO_DATA              0x0013

typedef PXE_UINT16  PXE_IFNUM;

///
/// This interface number must be passed to the S/W UNDI Start command.
///
#define PXE_IFNUM_START 0x0000

///
/// This interface number is returned by the S/W UNDI Get State and
/// Start commands if information in the CDB, CPB or DB is invalid.
///
#define PXE_IFNUM_INVALID 0x0000

typedef PXE_UINT16  PXE_CONTROL;

///
/// Setting this flag directs the UNDI to queue this command for later
/// execution if the UNDI is busy and it supports command queuing.
/// If queuing is not supported, a PXE_STATCODE_INVALID_CONTROL error
/// is returned.  If the queue is full, a PXE_STATCODE_CDB_QUEUE_FULL
/// error is returned.
///
#define PXE_CONTROL_QUEUE_IF_BUSY 0x0002

///
/// These two bit values are used to determine if there are more UNDI
/// CDB structures following this one.  If the link bit is set, there
/// must be a CDB structure following this one.  Execution will start
/// on the next CDB structure as soon as this one completes successfully.
/// If an error is generated by this command, execution will stop.
///
#define PXE_CONTROL_LINK              0x0001
#define PXE_CONTROL_LAST_CDB_IN_LIST  0x0000

typedef PXE_UINT8   PXE_FRAME_TYPE;

#define PXE_FRAME_TYPE_NONE                     0x00
#define PXE_FRAME_TYPE_UNICAST                  0x01
#define PXE_FRAME_TYPE_BROADCAST                0x02
#define PXE_FRAME_TYPE_FILTERED_MULTICAST       0x03
#define PXE_FRAME_TYPE_PROMISCUOUS              0x04
#define PXE_FRAME_TYPE_PROMISCUOUS_MULTICAST    0x05

#define PXE_FRAME_TYPE_MULTICAST                PXE_FRAME_TYPE_FILTERED_MULTICAST

typedef PXE_UINT32  PXE_IPV4;

typedef PXE_UINT32  PXE_IPV6[4];
#define PXE_MAC_LENGTH  32

typedef PXE_UINT8   PXE_MAC_ADDR[PXE_MAC_LENGTH];

typedef PXE_UINT8   PXE_IFTYPE;
typedef UINT16      PXE_MEDIA_PROTOCOL;

///
/// This information is from the ARP section of RFC 1700.
///
///     1 Ethernet (10Mb)                                    [JBP]
///     2 Experimental Ethernet (3Mb)                        [JBP]
///     3 Amateur Radio AX.25                                [PXK]
///     4 Proteon ProNET Token Ring                          [JBP]
///     5 Chaos                                              [GXP]
///     6 IEEE 802 Networks                                  [JBP]
///     7 ARCNET                                             [JBP]
///     8 Hyperchannel                                       [JBP]
///     9 Lanstar                                             [TU]
///    10 Autonet Short Address                             [MXB1]
///    11 LocalTalk                                         [JKR1]
///    12 LocalNet (IBM* PCNet or SYTEK* LocalNET)           [JXM]
///    13 Ultra link                                        [RXD2]
///    14 SMDS                                              [GXC1]
///    15 Frame Relay                                        [AGM]
///    16 Asynchronous Transmission Mode (ATM)              [JXB2]
///    17 HDLC                                               [JBP]
///    18 Fibre Channel                            [Yakov Rekhter]
///    19 Asynchronous Transmission Mode (ATM)      [Mark Laubach]
///    20 Serial Line                                        [JBP]
///    21 Asynchronous Transmission Mode (ATM)              [MXB1]
///
/// * Other names and brands may be claimed as the property of others.
///
#define PXE_IFTYPE_ETHERNET       0x01
#define PXE_IFTYPE_TOKENRING      0x04
#define PXE_IFTYPE_FIBRE_CHANNEL  0x12

typedef struct s_pxe_hw_undi {
  PXE_UINT32  Signature;      ///< PXE_ROMID_SIGNATURE.
  PXE_UINT8   Len;            ///< sizeof(PXE_HW_UNDI).
  PXE_UINT8   Fudge;          ///< makes 8-bit cksum equal zero.
  PXE_UINT8   Rev;            ///< PXE_ROMID_REV.
  PXE_UINT8   IFcnt;          ///< physical connector count lower byte.
  PXE_UINT8   MajorVer;       ///< PXE_ROMID_MAJORVER.
  PXE_UINT8   MinorVer;       ///< PXE_ROMID_MINORVER.
  PXE_UINT8   IFcntExt;       ///< physical connector count upper byte.
  PXE_UINT8   reserved;       ///< zero, not used.
  PXE_UINT32  Implementation; ///< implementation flags.
  ///< reserved             ///< vendor use.
  ///< UINT32 Status;       ///< status port.
  ///< UINT32 Command;      ///< command port.
  ///< UINT64 CDBaddr;      ///< CDB address port.
  ///<
} PXE_HW_UNDI;

///
/// Status port bit definitions.
///

///
/// UNDI operation state.
///
#define PXE_HWSTAT_STATE_MASK   0xC0000000
#define PXE_HWSTAT_BUSY         0xC0000000
#define PXE_HWSTAT_INITIALIZED  0x80000000
#define PXE_HWSTAT_STARTED      0x40000000
#define PXE_HWSTAT_STOPPED      0x00000000

///
/// If set, last command failed.
///
#define PXE_HWSTAT_COMMAND_FAILED 0x20000000

///
/// If set, identifies enabled receive filters.
///
#define PXE_HWSTAT_PROMISCUOUS_MULTICAST_RX_ENABLED 0x00001000
#define PXE_HWSTAT_PROMISCUOUS_RX_ENABLED           0x00000800
#define PXE_HWSTAT_BROADCAST_RX_ENABLED             0x00000400
#define PXE_HWSTAT_MULTICAST_RX_ENABLED             0x00000200
#define PXE_HWSTAT_UNICAST_RX_ENABLED               0x00000100

///
/// If set, identifies enabled external interrupts.
///
#define PXE_HWSTAT_SOFTWARE_INT_ENABLED     0x00000080
#define PXE_HWSTAT_TX_COMPLETE_INT_ENABLED  0x00000040
#define PXE_HWSTAT_PACKET_RX_INT_ENABLED    0x00000020
#define PXE_HWSTAT_CMD_COMPLETE_INT_ENABLED 0x00000010

///
/// If set, identifies pending interrupts.
///
#define PXE_HWSTAT_SOFTWARE_INT_PENDING     0x00000008
#define PXE_HWSTAT_TX_COMPLETE_INT_PENDING  0x00000004
#define PXE_HWSTAT_PACKET_RX_INT_PENDING    0x00000002
#define PXE_HWSTAT_CMD_COMPLETE_INT_PENDING 0x00000001

///
/// Command port definitions.
///

///
/// If set, CDB identified in CDBaddr port is given to UNDI.
/// If not set, other bits in this word will be processed.
///
#define PXE_HWCMD_ISSUE_COMMAND   0x80000000
#define PXE_HWCMD_INTS_AND_FILTS  0x00000000

///
/// Use these to enable/disable receive filters.
///
#define PXE_HWCMD_PROMISCUOUS_MULTICAST_RX_ENABLE 0x00001000
#define PXE_HWCMD_PROMISCUOUS_RX_ENABLE           0x00000800
#define PXE_HWCMD_BROADCAST_RX_ENABLE             0x00000400
#define PXE_HWCMD_MULTICAST_RX_ENABLE             0x00000200
#define PXE_HWCMD_UNICAST_RX_ENABLE               0x00000100

///
/// Use these to enable/disable external interrupts.
///
#define PXE_HWCMD_SOFTWARE_INT_ENABLE     0x00000080
#define PXE_HWCMD_TX_COMPLETE_INT_ENABLE  0x00000040
#define PXE_HWCMD_PACKET_RX_INT_ENABLE    0x00000020
#define PXE_HWCMD_CMD_COMPLETE_INT_ENABLE 0x00000010

///
/// Use these to clear pending external interrupts.
///
#define PXE_HWCMD_CLEAR_SOFTWARE_INT      0x00000008
#define PXE_HWCMD_CLEAR_TX_COMPLETE_INT   0x00000004
#define PXE_HWCMD_CLEAR_PACKET_RX_INT     0x00000002
#define PXE_HWCMD_CLEAR_CMD_COMPLETE_INT  0x00000001

typedef struct s_pxe_sw_undi {
  PXE_UINT32  Signature;      ///< PXE_ROMID_SIGNATURE.
  PXE_UINT8   Len;            ///< sizeof(PXE_SW_UNDI).
  PXE_UINT8   Fudge;          ///< makes 8-bit cksum zero.
  PXE_UINT8   Rev;            ///< PXE_ROMID_REV.
  PXE_UINT8   IFcnt;          ///< physical connector count lower byte.
  PXE_UINT8   MajorVer;       ///< PXE_ROMID_MAJORVER.
  PXE_UINT8   MinorVer;       ///< PXE_ROMID_MINORVER.
  PXE_UINT8   IFcntExt;       ///< physical connector count upper byte.
  PXE_UINT8   reserved1;      ///< zero, not used.
  PXE_UINT32  Implementation; ///< Implementation flags.
  PXE_UINT64  EntryPoint;     ///< API entry point.
  PXE_UINT8   reserved2[3];   ///< zero, not used.
  PXE_UINT8   BusCnt;         ///< number of bustypes supported.
  PXE_UINT32  BusType[1];     ///< list of supported bustypes.
} PXE_SW_UNDI;

typedef union u_pxe_undi {
  PXE_HW_UNDI hw;
  PXE_SW_UNDI sw;
} PXE_UNDI;

///
/// Signature of !PXE structure.
///
#define PXE_ROMID_SIGNATURE PXE_BUSTYPE ('!', 'P', 'X', 'E')

///
/// !PXE structure format revision
///.
#define PXE_ROMID_REV 0x02

///
/// UNDI command interface revision.  These are the values that get sent
/// in option 94 (Client Network Interface Identifier) in the DHCP Discover
/// and PXE Boot Server Request packets.
///
#define PXE_ROMID_MAJORVER    0x03
#define PXE_ROMID_MINORVER    0x01

///
/// Implementation flags.
///
#define PXE_ROMID_IMP_HW_UNDI                             0x80000000
#define PXE_ROMID_IMP_SW_VIRT_ADDR                        0x40000000
#define PXE_ROMID_IMP_64BIT_DEVICE                        0x00010000
#define PXE_ROMID_IMP_FRAG_SUPPORTED                      0x00008000
#define PXE_ROMID_IMP_CMD_LINK_SUPPORTED                  0x00004000
#define PXE_ROMID_IMP_CMD_QUEUE_SUPPORTED                 0x00002000
#define PXE_ROMID_IMP_MULTI_FRAME_SUPPORTED               0x00001000
#define PXE_ROMID_IMP_NVDATA_SUPPORT_MASK                 0x00000C00
#define PXE_ROMID_IMP_NVDATA_BULK_WRITABLE                0x00000C00
#define PXE_ROMID_IMP_NVDATA_SPARSE_WRITABLE              0x00000800
#define PXE_ROMID_IMP_NVDATA_READ_ONLY                    0x00000400
#define PXE_ROMID_IMP_NVDATA_NOT_AVAILABLE                0x00000000
#define PXE_ROMID_IMP_STATISTICS_SUPPORTED                0x00000200
#define PXE_ROMID_IMP_STATION_ADDR_SETTABLE               0x00000100
#define PXE_ROMID_IMP_PROMISCUOUS_MULTICAST_RX_SUPPORTED  0x00000080
#define PXE_ROMID_IMP_PROMISCUOUS_RX_SUPPORTED            0x00000040
#define PXE_ROMID_IMP_BROADCAST_RX_SUPPORTED              0x00000020
#define PXE_ROMID_IMP_FILTERED_MULTICAST_RX_SUPPORTED     0x00000010
#define PXE_ROMID_IMP_SOFTWARE_INT_SUPPORTED              0x00000008
#define PXE_ROMID_IMP_TX_COMPLETE_INT_SUPPORTED           0x00000004
#define PXE_ROMID_IMP_PACKET_RX_INT_SUPPORTED             0x00000002
#define PXE_ROMID_IMP_CMD_COMPLETE_INT_SUPPORTED          0x00000001

typedef struct s_pxe_cdb {
  PXE_OPCODE    OpCode;
  PXE_OPFLAGS   OpFlags;
  PXE_UINT16    CPBsize;
  PXE_UINT16    DBsize;
  PXE_UINT64    CPBaddr;
  PXE_UINT64    DBaddr;
  PXE_STATCODE  StatCode;
  PXE_STATFLAGS StatFlags;
  PXE_UINT16    IFnum;
  PXE_CONTROL   Control;
} PXE_CDB;

typedef union u_pxe_ip_addr {
  PXE_IPV6  IPv6;
  PXE_IPV4  IPv4;
} PXE_IP_ADDR;

typedef union pxe_device {
  ///
  /// PCI and PC Card NICs are both identified using bus, device
  /// and function numbers.  For PC Card, this may require PC
  /// Card services to be loaded in the BIOS or preboot
  /// environment.
  ///
  struct {
    ///
    /// See S/W UNDI ROMID structure definition for PCI and
    /// PCC BusType definitions.
    ///
    PXE_UINT32  BusType;

    ///
    /// Bus, device & function numbers that locate this device.
    ///
    PXE_UINT16  Bus;
    PXE_UINT8   Device;
    PXE_UINT8   Function;
  }
  PCI, PCC;

} PXE_DEVICE;

///
/// cpb and db definitions
///
#define MAX_PCI_CONFIG_LEN    64  ///< # of dwords.
#define MAX_EEPROM_LEN        128 ///< # of dwords.
#define MAX_XMIT_BUFFERS      32  ///< recycling Q length for xmit_done.
#define MAX_MCAST_ADDRESS_CNT 8

typedef struct s_pxe_cpb_start_30 {
  ///
  /// PXE_VOID Delay(UINTN microseconds);
  ///
  /// UNDI will never request a delay smaller than 10 microseconds
  /// and will always request delays in increments of 10 microseconds.
  /// The Delay() CallBack routine must delay between n and n + 10
  /// microseconds before returning control to the UNDI.
  ///
  /// This field cannot be set to zero.
  ///
  UINT64  Delay;

  ///
  /// PXE_VOID Block(UINT32 enable);
  ///
  /// UNDI may need to block multi-threaded/multi-processor access to
  /// critical code sections when programming or accessing the network
  /// device.  To this end, a blocking service is needed by the UNDI.
  /// When UNDI needs a block, it will call Block() passing a non-zero
  /// value.  When UNDI no longer needs a block, it will call Block()
  /// with a zero value.  When called, if the Block() is already enabled,
  /// do not return control to the UNDI until the previous Block() is
  /// disabled.
  ///
  /// This field cannot be set to zero.
  ///
  UINT64  Block;

  ///
  /// PXE_VOID Virt2Phys(UINT64 virtual, UINT64 physical_ptr);
  ///
  /// UNDI will pass the virtual address of a buffer and the virtual
  /// address of a 64-bit physical buffer.  Convert the virtual address
  /// to a physical address and write the result to the physical address
  /// buffer.  If virtual and physical addresses are the same, just
  /// copy the virtual address to the physical address buffer.
  ///
  /// This field can be set to zero if virtual and physical addresses
  /// are equal.
  ///
  UINT64  Virt2Phys;
  ///
  /// PXE_VOID Mem_IO(UINT8 read_write, UINT8 len, UINT64 port,
  ///              UINT64 buf_addr);
  ///
  /// UNDI will read or write the device io space using this call back
  /// function. It passes the number of bytes as the len parameter and it
  /// will be either 1,2,4 or 8.
  ///
  /// This field can not be set to zero.
  ///
  UINT64  Mem_IO;
} PXE_CPB_START_30;

typedef struct s_pxe_cpb_start_31 {
  ///
  /// PXE_VOID Delay(UINT64 UnqId, UINTN microseconds);
  ///
  /// UNDI will never request a delay smaller than 10 microseconds
  /// and will always request delays in increments of 10 microseconds.
  /// The Delay() CallBack routine must delay between n and n + 10
  /// microseconds before returning control to the UNDI.
  ///
  /// This field cannot be set to zero.
  ///
  UINT64  Delay;

  ///
  /// PXE_VOID Block(UINT64 unq_id, UINT32 enable);
  ///
  /// UNDI may need to block multi-threaded/multi-processor access to
  /// critical code sections when programming or accessing the network
  /// device.  To this end, a blocking service is needed by the UNDI.
  /// When UNDI needs a block, it will call Block() passing a non-zero
  /// value.  When UNDI no longer needs a block, it will call Block()
  /// with a zero value.  When called, if the Block() is already enabled,
  /// do not return control to the UNDI until the previous Block() is
  /// disabled.
  ///
  /// This field cannot be set to zero.
  ///
  UINT64  Block;

  ///
  /// PXE_VOID Virt2Phys(UINT64 UnqId, UINT64 virtual, UINT64 physical_ptr);
  ///
  /// UNDI will pass the virtual address of a buffer and the virtual
  /// address of a 64-bit physical buffer.  Convert the virtual address
  /// to a physical address and write the result to the physical address
  /// buffer.  If virtual and physical addresses are the same, just
  /// copy the virtual address to the physical address buffer.
  ///
  /// This field can be set to zero if virtual and physical addresses
  /// are equal.
  ///
  UINT64  Virt2Phys;
  ///
  /// PXE_VOID Mem_IO(UINT64 UnqId, UINT8 read_write, UINT8 len, UINT64 port,
  ///              UINT64 buf_addr);
  ///
  /// UNDI will read or write the device io space using this call back
  /// function. It passes the number of bytes as the len parameter and it
  /// will be either 1,2,4 or 8.
  ///
  /// This field can not be set to zero.
  ///
  UINT64  Mem_IO;
  ///
  /// PXE_VOID Map_Mem(UINT64 unq_id, UINT64 virtual_addr, UINT32 size,
  ///                 UINT32 Direction, UINT64 mapped_addr);
  ///
  /// UNDI will pass the virtual address of a buffer, direction of the data
  /// flow from/to the mapped buffer (the constants are defined below)
  /// and a place holder (pointer) for the mapped address.
  /// This call will Map the given address to a physical DMA address and write
  /// the result to the mapped_addr pointer.  If there is no need to
  /// map the given address to a lower address (i.e. the given address is
  /// associated with a physical address that is already compatible to be
  /// used with the DMA, it converts the given virtual address to it's
  /// physical address and write that in the mapped address pointer.
  ///
  /// This field can be set to zero if there is no mapping service available.
  ///
  UINT64  Map_Mem;

  ///
  /// PXE_VOID UnMap_Mem(UINT64 unq_id, UINT64 virtual_addr, UINT32 size,
  ///            UINT32 Direction, UINT64 mapped_addr);
  ///
  /// UNDI will pass the virtual and mapped addresses of a buffer.
  /// This call will un map the given address.
  ///
  /// This field can be set to zero if there is no unmapping service available.
  ///
  UINT64  UnMap_Mem;

  ///
  /// PXE_VOID Sync_Mem(UINT64 unq_id, UINT64 virtual,
  ///            UINT32 size, UINT32 Direction, UINT64 mapped_addr);
  ///
  /// UNDI will pass the virtual and mapped addresses of a buffer.
  /// This call will synchronize the contents of both the virtual and mapped.
  /// buffers for the given Direction.
  ///
  /// This field can be set to zero if there is no service available.
  ///
  UINT64  Sync_Mem;

  ///
  /// protocol driver can provide anything for this Unique_ID, UNDI remembers
  /// that as just a 64bit value associated to the interface specified by
  /// the ifnum and gives it back as a parameter to all the call-back routines
  /// when calling for that interface!
  ///
  UINT64  Unique_ID;
} PXE_CPB_START_31;

#define TO_AND_FROM_DEVICE    0
#define FROM_DEVICE           1
#define TO_DEVICE             2

#define PXE_DELAY_MILLISECOND 1000
#define PXE_DELAY_SECOND      1000000
#define PXE_IO_READ           0
#define PXE_IO_WRITE          1
#define PXE_MEM_READ          2
#define PXE_MEM_WRITE         4

typedef struct s_pxe_db_get_init_info {
  ///
  /// Minimum length of locked memory buffer that must be given to
  /// the Initialize command. Giving UNDI more memory will generally
  /// give better performance.
  ///
  /// If MemoryRequired is zero, the UNDI does not need and will not
  /// use system memory to receive and transmit packets.
  ///
  PXE_UINT32  MemoryRequired;

  ///
  /// Maximum frame data length for Tx/Rx excluding the media header.
  ///
  PXE_UINT32  FrameDataLen;

  ///
  /// Supported link speeds are in units of mega bits.  Common ethernet
  /// values are 10, 100 and 1000.  Unused LinkSpeeds[] entries are zero
  /// filled.
  ///
  PXE_UINT32  LinkSpeeds[4];

  ///
  /// Number of non-volatile storage items.
  ///
  PXE_UINT32  NvCount;

  ///
  /// Width of non-volatile storage item in bytes.  0, 1, 2 or 4
  ///
  PXE_UINT16  NvWidth;

  ///
  /// Media header length.  This is the typical media header length for
  /// this UNDI.  This information is needed when allocating receive
  /// and transmit buffers.
  ///
  PXE_UINT16  MediaHeaderLen;

  ///
  /// Number of bytes in the NIC hardware (MAC) address.
  ///
  PXE_UINT16  HWaddrLen;

  ///
  /// Maximum number of multicast MAC addresses in the multicast
  /// MAC address filter list.
  ///
  PXE_UINT16  MCastFilterCnt;

  ///
  /// Default number and size of transmit and receive buffers that will
  /// be allocated by the UNDI.  If MemoryRequired is non-zero, this
  /// allocation will come out of the memory buffer given to the Initialize
  /// command.  If MemoryRequired is zero, this allocation will come out of
  /// memory on the NIC.
  ///
  PXE_UINT16  TxBufCnt;
  PXE_UINT16  TxBufSize;
  PXE_UINT16  RxBufCnt;
  PXE_UINT16  RxBufSize;

  ///
  /// Hardware interface types defined in the Assigned Numbers RFC
  /// and used in DHCP and ARP packets.
  /// See the PXE_IFTYPE typedef and PXE_IFTYPE_xxx macros.
  ///
  PXE_UINT8   IFtype;

  ///
  /// Supported duplex.  See PXE_DUPLEX_xxxxx #defines below.
  ///
  PXE_UINT8   SupportedDuplexModes;

  ///
  /// Supported loopback options.  See PXE_LOOPBACK_xxxxx #defines below.
  ///
  PXE_UINT8   SupportedLoopBackModes;
} PXE_DB_GET_INIT_INFO;

#define PXE_MAX_TXRX_UNIT_ETHER           1500

#define PXE_HWADDR_LEN_ETHER              0x0006
#define PXE_MAC_HEADER_LEN_ETHER          0x000E

#define PXE_DUPLEX_ENABLE_FULL_SUPPORTED  1
#define PXE_DUPLEX_FORCE_FULL_SUPPORTED   2

#define PXE_LOOPBACK_INTERNAL_SUPPORTED   1
#define PXE_LOOPBACK_EXTERNAL_SUPPORTED   2

typedef struct s_pxe_pci_config_info {
  ///
  /// This is the flag field for the PXE_DB_GET_CONFIG_INFO union.
  /// For PCI bus devices, this field is set to PXE_BUSTYPE_PCI.
  ///
  UINT32  BusType;

  ///
  /// This identifies the PCI network device that this UNDI interface.
  /// is bound to.
  ///
  UINT16  Bus;
  UINT8   Device;
  UINT8   Function;

  ///
  /// This is a copy of the PCI configuration space for this
  /// network device.
  ///
  union {
    UINT8   Byte[256];
    UINT16  Word[128];
    UINT32  Dword[64];
  } Config;
} PXE_PCI_CONFIG_INFO;

typedef struct s_pxe_pcc_config_info {
  ///
  /// This is the flag field for the PXE_DB_GET_CONFIG_INFO union.
  /// For PCC bus devices, this field is set to PXE_BUSTYPE_PCC.
  ///
  PXE_UINT32  BusType;

  ///
  /// This identifies the PCC network device that this UNDI interface
  /// is bound to.
  ///
  PXE_UINT16  Bus;
  PXE_UINT8   Device;
  PXE_UINT8   Function;

  ///
  /// This is a copy of the PCC configuration space for this
  /// network device.
  ///
  union {
    PXE_UINT8   Byte[256];
    PXE_UINT16  Word[128];
    PXE_UINT32  Dword[64];
  } Config;
} PXE_PCC_CONFIG_INFO;

typedef union u_pxe_db_get_config_info {
  PXE_PCI_CONFIG_INFO   pci;
  PXE_PCC_CONFIG_INFO   pcc;
} PXE_DB_GET_CONFIG_INFO;

typedef struct s_pxe_cpb_initialize {
  ///
  /// Address of first (lowest) byte of the memory buffer.  This buffer must
  /// be in contiguous physical memory and cannot be swapped out.  The UNDI
  /// will be using this for transmit and receive buffering.
  ///
  PXE_UINT64  MemoryAddr;

  ///
  /// MemoryLength must be greater than or equal to MemoryRequired
  /// returned by the Get Init Info command.
  ///
  PXE_UINT32  MemoryLength;

  ///
  /// Desired link speed in Mbit/sec.  Common ethernet values are 10, 100
  /// and 1000.  Setting a value of zero will auto-detect and/or use the
  /// default link speed (operation depends on UNDI/NIC functionality).
  ///
  PXE_UINT32  LinkSpeed;

  ///
  /// Suggested number and size of receive and transmit buffers to
  /// allocate.  If MemoryAddr and MemoryLength are non-zero, this
  /// allocation comes out of the supplied memory buffer.  If MemoryAddr
  /// and MemoryLength are zero, this allocation comes out of memory
  /// on the NIC.
  ///
  /// If these fields are set to zero, the UNDI will allocate buffer
  /// counts and sizes as it sees fit.
  ///
  PXE_UINT16  TxBufCnt;
  PXE_UINT16  TxBufSize;
  PXE_UINT16  RxBufCnt;
  PXE_UINT16  RxBufSize;

  ///
  /// The following configuration parameters are optional and must be zero
  /// to use the default values.
  ///
  PXE_UINT8   DuplexMode;

  PXE_UINT8   LoopBackMode;
} PXE_CPB_INITIALIZE;

#define PXE_DUPLEX_DEFAULT      0x00
#define PXE_FORCE_FULL_DUPLEX   0x01
#define PXE_ENABLE_FULL_DUPLEX  0x02
#define PXE_FORCE_HALF_DUPLEX   0x04
#define PXE_DISABLE_FULL_DUPLEX 0x08

#define LOOPBACK_NORMAL         0
#define LOOPBACK_INTERNAL       1
#define LOOPBACK_EXTERNAL       2

typedef struct s_pxe_db_initialize {
  ///
  /// Actual amount of memory used from the supplied memory buffer.  This
  /// may be less that the amount of memory suppllied and may be zero if
  /// the UNDI and network device do not use external memory buffers.
  ///
  /// Memory used by the UNDI and network device is allocated from the
  /// lowest memory buffer address.
  ///
  PXE_UINT32  MemoryUsed;

  ///
  /// Actual number and size of receive and transmit buffers that were
  /// allocated.
  ///
  PXE_UINT16  TxBufCnt;
  PXE_UINT16  TxBufSize;
  PXE_UINT16  RxBufCnt;
  PXE_UINT16  RxBufSize;
} PXE_DB_INITIALIZE;

typedef struct s_pxe_cpb_receive_filters {
  ///
  /// List of multicast MAC addresses.  This list, if present, will
  /// replace the existing multicast MAC address filter list.
  ///
  PXE_MAC_ADDR  MCastList[MAX_MCAST_ADDRESS_CNT];
} PXE_CPB_RECEIVE_FILTERS;

typedef struct s_pxe_db_receive_filters {
  ///
  /// Filtered multicast MAC address list.
  ///
  PXE_MAC_ADDR  MCastList[MAX_MCAST_ADDRESS_CNT];
} PXE_DB_RECEIVE_FILTERS;

typedef struct s_pxe_cpb_station_address {
  ///
  /// If supplied and supported, the current station MAC address
  /// will be changed.
  ///
  PXE_MAC_ADDR  StationAddr;
} PXE_CPB_STATION_ADDRESS;

typedef struct s_pxe_dpb_station_address {
  ///
  /// Current station MAC address.
  ///
  PXE_MAC_ADDR  StationAddr;

  ///
  /// Station broadcast MAC address.
  ///
  PXE_MAC_ADDR  BroadcastAddr;

  ///
  /// Permanent station MAC address.
  ///
  PXE_MAC_ADDR  PermanentAddr;
} PXE_DB_STATION_ADDRESS;

typedef struct s_pxe_db_statistics {
  ///
  /// Bit field identifying what statistic data is collected by the
  /// UNDI/NIC.
  /// If bit 0x00 is set, Data[0x00] is collected.
  /// If bit 0x01 is set, Data[0x01] is collected.
  /// If bit 0x20 is set, Data[0x20] is collected.
  /// If bit 0x21 is set, Data[0x21] is collected.
  /// Etc.
  ///
  PXE_UINT64  Supported;

  ///
  /// Statistic data.
  ///
  PXE_UINT64  Data[64];
} PXE_DB_STATISTICS;

///
/// Total number of frames received.  Includes frames with errors and
/// dropped frames.
///
#define PXE_STATISTICS_RX_TOTAL_FRAMES  0x00

///
/// Number of valid frames received and copied into receive buffers.
///
#define PXE_STATISTICS_RX_GOOD_FRAMES 0x01

///
/// Number of frames below the minimum length for the media.
/// This would be <64 for ethernet.
///
#define PXE_STATISTICS_RX_UNDERSIZE_FRAMES  0x02

///
/// Number of frames longer than the maxminum length for the
/// media.  This would be >1500 for ethernet.
///
#define PXE_STATISTICS_RX_OVERSIZE_FRAMES 0x03

///
/// Valid frames that were dropped because receive buffers were full.
///
#define PXE_STATISTICS_RX_DROPPED_FRAMES  0x04

///
/// Number of valid unicast frames received and not dropped.
///
#define PXE_STATISTICS_RX_UNICAST_FRAMES  0x05

///
/// Number of valid broadcast frames received and not dropped.
///
#define PXE_STATISTICS_RX_BROADCAST_FRAMES  0x06

///
/// Number of valid mutlicast frames received and not dropped.
///
#define PXE_STATISTICS_RX_MULTICAST_FRAMES  0x07

///
/// Number of frames w/ CRC or alignment errors.
///
#define PXE_STATISTICS_RX_CRC_ERROR_FRAMES  0x08

///
/// Total number of bytes received.  Includes frames with errors
/// and dropped frames.
///
#define PXE_STATISTICS_RX_TOTAL_BYTES 0x09

///
/// Transmit statistics.
///
#define PXE_STATISTICS_TX_TOTAL_FRAMES      0x0A
#define PXE_STATISTICS_TX_GOOD_FRAMES       0x0B
#define PXE_STATISTICS_TX_UNDERSIZE_FRAMES  0x0C
#define PXE_STATISTICS_TX_OVERSIZE_FRAMES   0x0D
#define PXE_STATISTICS_TX_DROPPED_FRAMES    0x0E
#define PXE_STATISTICS_TX_UNICAST_FRAMES    0x0F
#define PXE_STATISTICS_TX_BROADCAST_FRAMES  0x10
#define PXE_STATISTICS_TX_MULTICAST_FRAMES  0x11
#define PXE_STATISTICS_TX_CRC_ERROR_FRAMES  0x12
#define PXE_STATISTICS_TX_TOTAL_BYTES       0x13

///
/// Number of collisions detection on this subnet.
///
#define PXE_STATISTICS_COLLISIONS 0x14

///
/// Number of frames destined for unsupported protocol.
///
#define PXE_STATISTICS_UNSUPPORTED_PROTOCOL 0x15

///
/// Number of valid frames received that were duplicated.
///
#define PXE_STATISTICS_RX_DUPLICATED_FRAMES 0x16

///
/// Number of encrypted frames received that failed to decrypt.
///
#define PXE_STATISTICS_RX_DECRYPT_ERROR_FRAMES 0x17

///
/// Number of frames that failed to transmit after exceeding the retry limit.
///
#define PXE_STATISTICS_TX_ERROR_FRAMES 0x18

///
/// Number of frames transmitted successfully after more than one attempt.
///
#define PXE_STATISTICS_TX_RETRY_FRAMES 0x19

typedef struct s_pxe_cpb_mcast_ip_to_mac {
  ///
  /// Multicast IP address to be converted to multicast MAC address.
  ///
  PXE_IP_ADDR IP;
} PXE_CPB_MCAST_IP_TO_MAC;

typedef struct s_pxe_db_mcast_ip_to_mac {
  ///
  /// Multicast MAC address.
  ///
  PXE_MAC_ADDR  MAC;
} PXE_DB_MCAST_IP_TO_MAC;

typedef struct s_pxe_cpb_nvdata_sparse {
  ///
  /// NvData item list.  Only items in this list will be updated.
  ///
  struct {
    ///
    ///  Non-volatile storage address to be changed.
    ///
    PXE_UINT32  Addr;

    ///
    /// Data item to write into above storage address.
    ///
    union {
      PXE_UINT8   Byte;
      PXE_UINT16  Word;
      PXE_UINT32  Dword;
    } Data;
  } Item[MAX_EEPROM_LEN];
} PXE_CPB_NVDATA_SPARSE;

///
/// When using bulk update, the size of the CPB structure must be
/// the same size as the non-volatile NIC storage.
///
typedef union u_pxe_cpb_nvdata_bulk {
  ///
  /// Array of byte-wide data items.
  ///
  PXE_UINT8   Byte[MAX_EEPROM_LEN << 2];

  ///
  /// Array of word-wide data items.
  ///
  PXE_UINT16  Word[MAX_EEPROM_LEN << 1];

  ///
  /// Array of dword-wide data items.
  ///
  PXE_UINT32  Dword[MAX_EEPROM_LEN];
} PXE_CPB_NVDATA_BULK;

typedef struct s_pxe_db_nvdata {
  ///
  /// Arrays of data items from non-volatile storage.
  ///
  union {
    ///
    /// Array of byte-wide data items.
    ///
    PXE_UINT8   Byte[MAX_EEPROM_LEN << 2];

    ///
    /// Array of word-wide data items.
    ///
    PXE_UINT16  Word[MAX_EEPROM_LEN << 1];

    ///
    /// Array of dword-wide data items.
    ///
    PXE_UINT32  Dword[MAX_EEPROM_LEN];
  } Data;
} PXE_DB_NVDATA;

typedef struct s_pxe_db_get_status {
  ///
  /// Length of next receive frame (header + data).  If this is zero,
  /// there is no next receive frame available.
  ///
  PXE_UINT32  RxFrameLen;

  ///
  /// Reserved, set to zero.
  ///
  PXE_UINT32  reserved;

  ///
  ///  Addresses of transmitted buffers that need to be recycled.
  ///
  PXE_UINT64  TxBuffer[MAX_XMIT_BUFFERS];
} PXE_DB_GET_STATUS;

typedef struct s_pxe_cpb_fill_header {
  ///
  /// Source and destination MAC addresses.  These will be copied into
  /// the media header without doing byte swapping.
  ///
  PXE_MAC_ADDR  SrcAddr;
  PXE_MAC_ADDR  DestAddr;

  ///
  /// Address of first byte of media header.  The first byte of packet data
  /// follows the last byte of the media header.
  ///
  PXE_UINT64        MediaHeader;

  ///
  /// Length of packet data in bytes (not including the media header).
  ///
  PXE_UINT32        PacketLen;

  ///
  /// Protocol type.  This will be copied into the media header without
  /// doing byte swapping.  Protocol type numbers can be obtained from
  /// the Assigned Numbers RFC 1700.
  ///
  PXE_UINT16        Protocol;

  ///
  /// Length of the media header in bytes.
  ///
  PXE_UINT16        MediaHeaderLen;
} PXE_CPB_FILL_HEADER;

#define PXE_PROTOCOL_ETHERNET_IP  0x0800
#define PXE_PROTOCOL_ETHERNET_ARP 0x0806
#define MAX_XMIT_FRAGMENTS        16

typedef struct s_pxe_cpb_fill_header_fragmented {
  ///
  /// Source and destination MAC addresses.  These will be copied into
  /// the media header without doing byte swapping.
  ///
  PXE_MAC_ADDR        SrcAddr;
  PXE_MAC_ADDR        DestAddr;

  ///
  /// Length of packet data in bytes (not including the media header).
  ///
  PXE_UINT32          PacketLen;

  ///
  /// Protocol type.  This will be copied into the media header without
  /// doing byte swapping.  Protocol type numbers can be obtained from
  /// the Assigned Numbers RFC 1700.
  ///
  PXE_MEDIA_PROTOCOL  Protocol;

  ///
  /// Length of the media header in bytes.
  ///
  PXE_UINT16          MediaHeaderLen;

  ///
  /// Number of packet fragment descriptors.
  ///
  PXE_UINT16          FragCnt;

  ///
  /// Reserved, must be set to zero.
  ///
  PXE_UINT16          reserved;

  ///
  /// Array of packet fragment descriptors.  The first byte of the media
  /// header is the first byte of the first fragment.
  ///
  struct {
    ///
    /// Address of this packet fragment.
    ///
    PXE_UINT64  FragAddr;

    ///
    /// Length of this packet fragment.
    ///
    PXE_UINT32  FragLen;

    ///
    /// Reserved, must be set to zero.
    ///
    PXE_UINT32  reserved;
  } FragDesc[MAX_XMIT_FRAGMENTS];
}
PXE_CPB_FILL_HEADER_FRAGMENTED;

typedef struct s_pxe_cpb_transmit {
  ///
  /// Address of first byte of frame buffer.  This is also the first byte
  /// of the media header.
  ///
  PXE_UINT64  FrameAddr;

  ///
  /// Length of the data portion of the frame buffer in bytes.  Do not
  /// include the length of the media header.
  ///
  PXE_UINT32  DataLen;

  ///
  /// Length of the media header in bytes.
  ///
  PXE_UINT16  MediaheaderLen;

  ///
  /// Reserved, must be zero.
  ///
  PXE_UINT16  reserved;
} PXE_CPB_TRANSMIT;

typedef struct s_pxe_cpb_transmit_fragments {
  ///
  /// Length of packet data in bytes (not including the media header).
  ///
  PXE_UINT32  FrameLen;

  ///
  /// Length of the media header in bytes.
  ///
  PXE_UINT16  MediaheaderLen;

  ///
  /// Number of packet fragment descriptors.
  ///
  PXE_UINT16  FragCnt;

  ///
  /// Array of frame fragment descriptors.  The first byte of the first
  /// fragment is also the first byte of the media header.
  ///
  struct {
    ///
    /// Address of this frame fragment.
    ///
    PXE_UINT64  FragAddr;

    ///
    /// Length of this frame fragment.
    ///
    PXE_UINT32  FragLen;

    ///
    /// Reserved, must be set to zero.
    ///
    PXE_UINT32  reserved;
  } FragDesc[MAX_XMIT_FRAGMENTS];
}
PXE_CPB_TRANSMIT_FRAGMENTS;

typedef struct s_pxe_cpb_receive {
  ///
  /// Address of first byte of receive buffer.  This is also the first byte
  /// of the frame header.
  ///
  PXE_UINT64  BufferAddr;

  ///
  /// Length of receive buffer.  This must be large enough to hold the
  /// received frame (media header + data).  If the length of smaller than
  /// the received frame, data will be lost.
  ///
  PXE_UINT32  BufferLen;

  ///
  /// Reserved, must be set to zero.
  ///
  PXE_UINT32  reserved;
} PXE_CPB_RECEIVE;

typedef struct s_pxe_db_receive {
  ///
  /// Source and destination MAC addresses from media header.
  ///
  PXE_MAC_ADDR        SrcAddr;
  PXE_MAC_ADDR        DestAddr;

  ///
  /// Length of received frame.  May be larger than receive buffer size.
  /// The receive buffer will not be overwritten.  This is how to tell
  /// if data was lost because the receive buffer was too small.
  ///
  PXE_UINT32          FrameLen;

  ///
  /// Protocol type from media header.
  ///
  PXE_MEDIA_PROTOCOL  Protocol;

  ///
  /// Length of media header in received frame.
  ///
  PXE_UINT16          MediaHeaderLen;

  ///
  /// Type of receive frame.
  ///
  PXE_FRAME_TYPE      Type;

  ///
  /// Reserved, must be zero.
  ///
  PXE_UINT8           reserved[7];

} PXE_DB_RECEIVE;

#pragma pack()

#endif
