/** @file
  The device path protocol as defined in UEFI 2.0.

  The device path represents a programmatic path to a device,
  from a software point of view. The path must persist from boot to boot, so 
  it can not contain things like PCI bus numbers that change from boot to boot.

Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
This program and the accompanying materials are licensed and made available under 
the terms and conditions of the BSD License that accompanies this distribution.  
The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php.                                          
    
THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.              

**/

#ifndef __EFI_DEVICE_PATH_PROTOCOL_H__
#define __EFI_DEVICE_PATH_PROTOCOL_H__

#include <Guid/PcAnsi.h>
#include <IndustryStandard/Bluetooth.h>
#include <IndustryStandard/Acpi60.h>

///
/// Device Path protocol.
///
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
  { \
    0x9576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

///
/// Device Path guid definition for backward-compatible with EFI1.1.
///
#define DEVICE_PATH_PROTOCOL  EFI_DEVICE_PATH_PROTOCOL_GUID

#pragma pack(1)

/**
  This protocol can be used on any device handle to obtain generic path/location 
  information concerning the physical device or logical device. If the handle does 
  not logically map to a physical device, the handle may not necessarily support 
  the device path protocol. The device path describes the location of the device 
  the handle is for. The size of the Device Path can be determined from the structures 
  that make up the Device Path.
**/
typedef struct {
  UINT8 Type;       ///< 0x01 Hardware Device Path.
                    ///< 0x02 ACPI Device Path.
                    ///< 0x03 Messaging Device Path.
                    ///< 0x04 Media Device Path.
                    ///< 0x05 BIOS Boot Specification Device Path.
                    ///< 0x7F End of Hardware Device Path.
                    
  UINT8 SubType;    ///< Varies by Type
                    ///< 0xFF End Entire Device Path, or
                    ///< 0x01 End This Instance of a Device Path and start a new
                    ///< Device Path.
                    
  UINT8 Length[2];  ///< Specific Device Path data. Type and Sub-Type define
                    ///< type of data. Size of data is included in Length.
                    
} EFI_DEVICE_PATH_PROTOCOL;

///
/// Device Path protocol definition for backward-compatible with EFI1.1.
/// 
typedef EFI_DEVICE_PATH_PROTOCOL  EFI_DEVICE_PATH;

///
/// Hardware Device Paths.
///
#define HARDWARE_DEVICE_PATH      0x01

///
/// PCI Device Path SubType.
///
#define HW_PCI_DP                 0x01

///
/// PCI Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// PCI Function Number.
  ///
  UINT8                           Function;
  ///
  /// PCI Device Number.
  ///
  UINT8                           Device;
} PCI_DEVICE_PATH;

///
/// PCCARD Device Path SubType.
///
#define HW_PCCARD_DP              0x02

///
/// PCCARD Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Function Number (0 = First Function).
  ///
  UINT8                           FunctionNumber;
} PCCARD_DEVICE_PATH;

///
/// Memory Mapped Device Path SubType.
///
#define HW_MEMMAP_DP              0x03

///
/// Memory Mapped Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// EFI_MEMORY_TYPE
  ///
  UINT32                          MemoryType;
  ///
  /// Starting Memory Address.
  ///
  EFI_PHYSICAL_ADDRESS            StartingAddress;
  ///
  /// Ending Memory Address.
  ///
  EFI_PHYSICAL_ADDRESS            EndingAddress;
} MEMMAP_DEVICE_PATH;

///
/// Hardware Vendor Device Path SubType.
///
#define HW_VENDOR_DP              0x04

///
/// The Vendor Device Path allows the creation of vendor-defined Device Paths. A vendor must
/// allocate a Vendor GUID for a Device Path. The Vendor GUID can then be used to define the
/// contents on the n bytes that follow in the Vendor Device Path node.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Vendor-assigned GUID that defines the data that follows.
  ///
  EFI_GUID                        Guid;
  ///
  /// Vendor-defined variable size data.
  ///
} VENDOR_DEVICE_PATH;

///
/// Controller Device Path SubType.
///
#define HW_CONTROLLER_DP          0x05

///
/// Controller Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Controller number.
  ///
  UINT32                          ControllerNumber;
} CONTROLLER_DEVICE_PATH;

///
/// BMC Device Path SubType.
///
#define HW_BMC_DP                 0x06

///
/// BMC Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Interface Type.
  ///
  UINT8                           InterfaceType;
  ///
  /// Base Address.
  ///
  UINT8                           BaseAddress[8];
} BMC_DEVICE_PATH;

///
/// ACPI Device Paths.
///
#define ACPI_DEVICE_PATH          0x02

///
/// ACPI Device Path SubType.
///
#define ACPI_DP                   0x01
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Device's PnP hardware ID stored in a numeric 32-bit
  /// compressed EISA-type ID. This value must match the
  /// corresponding _HID in the ACPI name space.
  ///
  UINT32                          HID;
  ///
  /// Unique ID that is required by ACPI if two devices have the
  /// same _HID. This value must also match the corresponding
  /// _UID/_HID pair in the ACPI name space. Only the 32-bit
  /// numeric value type of _UID is supported. Thus, strings must
  /// not be used for the _UID in the ACPI name space.
  ///
  UINT32                          UID;
} ACPI_HID_DEVICE_PATH;

///
/// Expanded ACPI Device Path SubType.
///
#define ACPI_EXTENDED_DP          0x02
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Device's PnP hardware ID stored in a numeric 32-bit
  /// compressed EISA-type ID. This value must match the
  /// corresponding _HID in the ACPI name space.
  ///
  UINT32                          HID;
  ///
  /// Unique ID that is required by ACPI if two devices have the
  /// same _HID. This value must also match the corresponding
  /// _UID/_HID pair in the ACPI name space.
  ///
  UINT32                          UID;
  ///
  /// Device's compatible PnP hardware ID stored in a numeric
  /// 32-bit compressed EISA-type ID. This value must match at
  /// least one of the compatible device IDs returned by the
  /// corresponding _CID in the ACPI name space.
  ///
  UINT32                          CID;
  ///
  /// Optional variable length _HIDSTR.
  /// Optional variable length _UIDSTR.
  /// Optional variable length _CIDSTR.
  ///
} ACPI_EXTENDED_HID_DEVICE_PATH;

//
//  EISA ID Macro
//  EISA ID Definition 32-bits
//   bits[15:0] - three character compressed ASCII EISA ID.
//   bits[31:16] - binary number
//    Compressed ASCII is 5 bits per character 0b00001 = 'A' 0b11010 = 'Z'
//
#define PNP_EISA_ID_CONST         0x41d0
#define EISA_ID(_Name, _Num)      ((UINT32)((_Name) | (_Num) << 16))
#define EISA_PNP_ID(_PNPId)       (EISA_ID(PNP_EISA_ID_CONST, (_PNPId)))
#define EFI_PNP_ID(_PNPId)        (EISA_ID(PNP_EISA_ID_CONST, (_PNPId)))

#define PNP_EISA_ID_MASK          0xffff
#define EISA_ID_TO_NUM(_Id)       ((_Id) >> 16)

///
/// ACPI _ADR Device Path SubType.
///
#define ACPI_ADR_DP               0x03

///
/// The _ADR device path is used to contain video output device attributes to support the Graphics
/// Output Protocol. The device path can contain multiple _ADR entries if multiple video output
/// devices are displaying the same output.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// _ADR value. For video output devices the value of this
  /// field comes from Table B-2 of the ACPI 3.0 specification. At
  /// least one _ADR value is required.
  ///
  UINT32                          ADR;
  //
  // This device path may optionally contain more than one _ADR entry.
  //
} ACPI_ADR_DEVICE_PATH;

#define ACPI_ADR_DISPLAY_TYPE_OTHER             0
#define ACPI_ADR_DISPLAY_TYPE_VGA               1
#define ACPI_ADR_DISPLAY_TYPE_TV                2
#define ACPI_ADR_DISPLAY_TYPE_EXTERNAL_DIGITAL  3
#define ACPI_ADR_DISPLAY_TYPE_INTERNAL_DIGITAL  4

#define ACPI_DISPLAY_ADR(_DeviceIdScheme, _HeadId, _NonVgaOutput, _BiosCanDetect, _VendorInfo, _Type, _Port, _Index) \
          ((UINT32)(  ((UINT32)((_DeviceIdScheme) & 0x1) << 31) |  \
                      (((_HeadId)                 & 0x7) << 18) |  \
                      (((_NonVgaOutput)           & 0x1) << 17) |  \
                      (((_BiosCanDetect)          & 0x1) << 16) |  \
                      (((_VendorInfo)             & 0xf) << 12) |  \
                      (((_Type)                   & 0xf) << 8)  |  \
                      (((_Port)                   & 0xf) << 4)  |  \
                       ((_Index)                  & 0xf) ))

///
/// Messaging Device Paths.
/// This Device Path is used to describe the connection of devices outside the resource domain of the
/// system. This Device Path can describe physical messaging information like SCSI ID, or abstract
/// information like networking protocol IP addresses.
///
#define MESSAGING_DEVICE_PATH     0x03

///
/// ATAPI Device Path SubType
///
#define MSG_ATAPI_DP              0x01
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Set to zero for primary, or one for secondary.
  ///
  UINT8                           PrimarySecondary;
  ///
  /// Set to zero for master, or one for slave mode.
  ///
  UINT8                           SlaveMaster;
  ///
  /// Logical Unit Number.
  ///
  UINT16                          Lun;
} ATAPI_DEVICE_PATH;

///
/// SCSI Device Path SubType.
///
#define MSG_SCSI_DP               0x02
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Target ID on the SCSI bus (PUN).
  ///
  UINT16                          Pun;
  ///
  /// Logical Unit Number (LUN).
  ///
  UINT16                          Lun;
} SCSI_DEVICE_PATH;

///
/// Fibre Channel SubType.
///
#define MSG_FIBRECHANNEL_DP       0x03
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Reserved for the future.
  ///
  UINT32                          Reserved;
  ///
  /// Fibre Channel World Wide Number.
  ///
  UINT64                          WWN;
  ///
  /// Fibre Channel Logical Unit Number.
  ///
  UINT64                          Lun;
} FIBRECHANNEL_DEVICE_PATH;

///
/// Fibre Channel Ex SubType.
///
#define MSG_FIBRECHANNELEX_DP     0x15
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Reserved for the future.
  ///
  UINT32                          Reserved;
  ///
  /// 8 byte array containing Fibre Channel End Device Port Name.
  ///
  UINT8                           WWN[8];
  ///
  /// 8 byte array containing Fibre Channel Logical Unit Number.
  ///
  UINT8                           Lun[8];
} FIBRECHANNELEX_DEVICE_PATH;

///
/// 1394 Device Path SubType
///
#define MSG_1394_DP               0x04
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Reserved for the future.
  ///
  UINT32                          Reserved;
  ///
  /// 1394 Global Unique ID (GUID).
  ///
  UINT64                          Guid;
} F1394_DEVICE_PATH;

///
/// USB Device Path SubType.
///
#define MSG_USB_DP                0x05
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL      Header;
  ///
  /// USB Parent Port Number.
  ///
  UINT8                         ParentPortNumber;
  ///
  /// USB Interface Number.
  ///
  UINT8                         InterfaceNumber;
} USB_DEVICE_PATH;

///
/// USB Class Device Path SubType.
///
#define MSG_USB_CLASS_DP          0x0f
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL      Header;
  ///
  /// Vendor ID assigned by USB-IF. A value of 0xFFFF will
  /// match any Vendor ID.
  ///
  UINT16                        VendorId;
  ///
  /// Product ID assigned by USB-IF. A value of 0xFFFF will
  /// match any Product ID.
  ///
  UINT16                        ProductId;
  ///
  /// The class code assigned by the USB-IF. A value of 0xFF
  /// will match any class code.
  ///
  UINT8                         DeviceClass;
  ///
  /// The subclass code assigned by the USB-IF. A value of
  /// 0xFF will match any subclass code.
  ///
  UINT8                         DeviceSubClass;
  ///
  /// The protocol code assigned by the USB-IF. A value of
  /// 0xFF will match any protocol code.
  ///
  UINT8                         DeviceProtocol;
} USB_CLASS_DEVICE_PATH;

///
/// USB WWID Device Path SubType.
///
#define MSG_USB_WWID_DP           0x10

///
/// This device path describes a USB device using its serial number.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL      Header;
  ///
  /// USB interface number.
  ///
  UINT16                        InterfaceNumber;
  ///
  /// USB vendor id of the device.
  ///
  UINT16                        VendorId;
  ///
  /// USB product id of the device.
  ///
  UINT16                        ProductId;
  ///
  /// Last 64-or-fewer UTF-16 characters of the USB
  /// serial number. The length of the string is
  /// determined by the Length field less the offset of the
  /// Serial Number field (10)
  ///
  /// CHAR16                     SerialNumber[...];
} USB_WWID_DEVICE_PATH;

///
/// Device Logical Unit SubType.
///
#define MSG_DEVICE_LOGICAL_UNIT_DP  0x11
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL      Header;
  ///
  /// Logical Unit Number for the interface.
  ///
  UINT8                         Lun;
} DEVICE_LOGICAL_UNIT_DEVICE_PATH;

///
/// SATA Device Path SubType.
///
#define MSG_SATA_DP               0x12
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// The HBA port number that facilitates the connection to the
  /// device or a port multiplier. The value 0xFFFF is reserved.
  ///
  UINT16                          HBAPortNumber;
  ///
  /// The Port multiplier port number that facilitates the connection
  /// to the device. Must be set to 0xFFFF if the device is directly
  /// connected to the HBA.
  ///
  UINT16                          PortMultiplierPortNumber;
  ///
  /// Logical Unit Number.
  ///
  UINT16                          Lun;
} SATA_DEVICE_PATH;

///
/// Flag for if the device is directly connected to the HBA.
///
#define SATA_HBA_DIRECT_CONNECT_FLAG 0x8000

///
/// I2O Device Path SubType.
///
#define MSG_I2O_DP                0x06
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Target ID (TID) for a device.
  ///
  UINT32                          Tid;
} I2O_DEVICE_PATH;

///
/// MAC Address Device Path SubType.
///
#define MSG_MAC_ADDR_DP           0x0b
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// The MAC address for a network interface padded with 0s.
  ///
  EFI_MAC_ADDRESS                 MacAddress;
  ///
  /// Network interface type(i.e. 802.3, FDDI).
  ///
  UINT8                           IfType;
} MAC_ADDR_DEVICE_PATH;

///
/// IPv4 Device Path SubType
///
#define MSG_IPv4_DP               0x0c
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// The local IPv4 address.
  ///
  EFI_IPv4_ADDRESS                LocalIpAddress;
  ///
  /// The remote IPv4 address.
  ///
  EFI_IPv4_ADDRESS                RemoteIpAddress;
  ///
  /// The local port number.
  ///
  UINT16                          LocalPort;
  ///
  /// The remote port number.
  ///
  UINT16                          RemotePort;
  ///
  /// The network protocol(i.e. UDP, TCP).
  ///
  UINT16                          Protocol;
  ///
  /// 0x00 - The Source IP Address was assigned though DHCP.
  /// 0x01 - The Source IP Address is statically bound.
  ///
  BOOLEAN                         StaticIpAddress;
  ///
  /// The gateway IP address
  ///
  EFI_IPv4_ADDRESS                GatewayIpAddress;
  ///
  /// The subnet mask
  ///
  EFI_IPv4_ADDRESS                SubnetMask;
} IPv4_DEVICE_PATH;

///
/// IPv6 Device Path SubType.
///
#define MSG_IPv6_DP               0x0d
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// The local IPv6 address.
  ///
  EFI_IPv6_ADDRESS                LocalIpAddress;
  ///
  /// The remote IPv6 address.
  ///
  EFI_IPv6_ADDRESS                RemoteIpAddress;
  ///
  /// The local port number.
  ///
  UINT16                          LocalPort;
  ///
  /// The remote port number.
  ///
  UINT16                          RemotePort;
  ///
  /// The network protocol(i.e. UDP, TCP).
  ///
  UINT16                          Protocol;
  ///
  /// 0x00 - The Local IP Address was manually configured.
  /// 0x01 - The Local IP Address is assigned through IPv6
  /// stateless auto-configuration.
  /// 0x02 - The Local IP Address is assigned through IPv6
  /// stateful configuration.
  ///
  UINT8                           IpAddressOrigin;
  ///
  /// The prefix length
  ///
  UINT8                           PrefixLength;
  ///
  /// The gateway IP address
  ///
  EFI_IPv6_ADDRESS                GatewayIpAddress;
} IPv6_DEVICE_PATH;

///
/// InfiniBand Device Path SubType.
///
#define MSG_INFINIBAND_DP         0x09
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Flags to help identify/manage InfiniBand device path elements:
  /// Bit 0 - IOC/Service (0b = IOC, 1b = Service).
  /// Bit 1 - Extend Boot Environment.
  /// Bit 2 - Console Protocol.
  /// Bit 3 - Storage Protocol.
  /// Bit 4 - Network Protocol.
  /// All other bits are reserved.
  ///
  UINT32                          ResourceFlags;
  ///
  /// 128-bit Global Identifier for remote fabric port.
  ///
  UINT8                           PortGid[16];
  ///
  /// 64-bit unique identifier to remote IOC or server process.
  /// Interpretation of field specified by Resource Flags (bit 0).
  ///
  UINT64                          ServiceId;
  ///
  /// 64-bit persistent ID of remote IOC port.
  ///
  UINT64                          TargetPortId;
  ///
  /// 64-bit persistent ID of remote device.
  ///
  UINT64                          DeviceId;
} INFINIBAND_DEVICE_PATH;

#define INFINIBAND_RESOURCE_FLAG_IOC_SERVICE                0x01
#define INFINIBAND_RESOURCE_FLAG_EXTENDED_BOOT_ENVIRONMENT  0x02
#define INFINIBAND_RESOURCE_FLAG_CONSOLE_PROTOCOL           0x04
#define INFINIBAND_RESOURCE_FLAG_STORAGE_PROTOCOL           0x08
#define INFINIBAND_RESOURCE_FLAG_NETWORK_PROTOCOL           0x10

///
/// UART Device Path SubType.
///
#define MSG_UART_DP               0x0e
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Reserved.
  ///
  UINT32                          Reserved;
  ///
  /// The baud rate setting for the UART style device. A value of 0
  /// means that the device's default baud rate will be used.
  ///
  UINT64                          BaudRate;
  ///
  /// The number of data bits for the UART style device. A value
  /// of 0 means that the device's default number of data bits will be used.
  ///
  UINT8                           DataBits;
  ///
  /// The parity setting for the UART style device.
  /// Parity 0x00 - Default Parity.
  /// Parity 0x01 - No Parity.
  /// Parity 0x02 - Even Parity.
  /// Parity 0x03 - Odd Parity.
  /// Parity 0x04 - Mark Parity.
  /// Parity 0x05 - Space Parity.
  ///
  UINT8                           Parity;
  ///
  /// The number of stop bits for the UART style device.
  /// Stop Bits 0x00 - Default Stop Bits.
  /// Stop Bits 0x01 - 1 Stop Bit.
  /// Stop Bits 0x02 - 1.5 Stop Bits.
  /// Stop Bits 0x03 - 2 Stop Bits.
  ///
  UINT8                           StopBits;
} UART_DEVICE_PATH;

//
// Use VENDOR_DEVICE_PATH struct
//
#define MSG_VENDOR_DP             0x0a
typedef VENDOR_DEVICE_PATH        VENDOR_DEFINED_DEVICE_PATH;

#define DEVICE_PATH_MESSAGING_PC_ANSI     EFI_PC_ANSI_GUID
#define DEVICE_PATH_MESSAGING_VT_100      EFI_VT_100_GUID
#define DEVICE_PATH_MESSAGING_VT_100_PLUS EFI_VT_100_PLUS_GUID
#define DEVICE_PATH_MESSAGING_VT_UTF8     EFI_VT_UTF8_GUID

///
/// A new device path node is defined to declare flow control characteristics.
/// UART Flow Control Messaging Device Path
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// DEVICE_PATH_MESSAGING_UART_FLOW_CONTROL GUID.
  ///
  EFI_GUID                        Guid;
  ///
  /// Bitmap of supported flow control types.
  /// Bit 0 set indicates hardware flow control.
  /// Bit 1 set indicates Xon/Xoff flow control.
  /// All other bits are reserved and are clear.
  ///
  UINT32                          FlowControlMap;
} UART_FLOW_CONTROL_DEVICE_PATH;

#define UART_FLOW_CONTROL_HARDWARE         0x00000001
#define UART_FLOW_CONTROL_XON_XOFF         0x00000010

#define DEVICE_PATH_MESSAGING_SAS          EFI_SAS_DEVICE_PATH_GUID
///
/// Serial Attached SCSI (SAS) Device Path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// DEVICE_PATH_MESSAGING_SAS GUID.
  ///
  EFI_GUID                        Guid;
  ///
  /// Reserved for future use.
  ///
  UINT32                          Reserved;
  ///
  /// SAS Address for Serial Attached SCSI Target.
  ///
  UINT64                          SasAddress;
  ///
  /// SAS Logical Unit Number.
  ///
  UINT64                          Lun;
  ///
  /// More Information about the device and its interconnect.
  ///
  UINT16                          DeviceTopology;
  ///
  /// Relative Target Port (RTP).
  ///
  UINT16                          RelativeTargetPort;
} SAS_DEVICE_PATH;

///
/// Serial Attached SCSI (SAS) Ex Device Path SubType
///
#define MSG_SASEX_DP              0x16
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// 8-byte array of the SAS Address for Serial Attached SCSI Target Port.
  ///
  UINT8                           SasAddress[8];
  ///
  /// 8-byte array of the SAS Logical Unit Number.
  ///
  UINT8                           Lun[8];
  ///
  /// More Information about the device and its interconnect.
  ///
  UINT16                          DeviceTopology;
  ///
  /// Relative Target Port (RTP).
  ///
  UINT16                          RelativeTargetPort;
} SASEX_DEVICE_PATH;

///
/// NvmExpress Namespace Device Path SubType.
///
#define MSG_NVME_NAMESPACE_DP     0x17
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  UINT32                          NamespaceId;
  UINT64                          NamespaceUuid;
} NVME_NAMESPACE_DEVICE_PATH;

///
/// Uniform Resource Identifiers (URI) Device Path SubType
///
#define MSG_URI_DP                0x18
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Instance of the URI pursuant to RFC 3986.
  ///
  CHAR8                           Uri[];
} URI_DEVICE_PATH;

///
/// Universal Flash Storage (UFS) Device Path SubType.
///
#define MSG_UFS_DP                0x19
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Target ID on the UFS bus (PUN).
  ///
  UINT8                           Pun;
  ///
  /// Logical Unit Number (LUN).
  ///
  UINT8                           Lun;
} UFS_DEVICE_PATH;

///
/// SD (Secure Digital) Device Path SubType.
///
#define MSG_SD_DP                 0x1A
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  UINT8                           SlotNumber;
} SD_DEVICE_PATH;

///
/// EMMC (Embedded MMC) Device Path SubType.
///
#define MSG_EMMC_DP                 0x1D
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  UINT8                           SlotNumber;
} EMMC_DEVICE_PATH;

///
/// iSCSI Device Path SubType
///
#define MSG_ISCSI_DP              0x13
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Network Protocol (0 = TCP, 1+ = reserved).
  ///
  UINT16                          NetworkProtocol;
  ///
  /// iSCSI Login Options.
  ///
  UINT16                          LoginOption;
  ///
  /// iSCSI Logical Unit Number.
  ///
  UINT64                          Lun;
  ///
  /// iSCSI Target Portal group tag the initiator intends
  /// to establish a session with.
  ///
  UINT16                          TargetPortalGroupTag;
  ///
  /// iSCSI NodeTarget Name. The length of the name
  /// is determined by subtracting the offset of this field from Length.
  ///
  /// CHAR8                        iSCSI Target Name.
} ISCSI_DEVICE_PATH;

#define ISCSI_LOGIN_OPTION_NO_HEADER_DIGEST             0x0000
#define ISCSI_LOGIN_OPTION_HEADER_DIGEST_USING_CRC32C   0x0002
#define ISCSI_LOGIN_OPTION_NO_DATA_DIGEST               0x0000
#define ISCSI_LOGIN_OPTION_DATA_DIGEST_USING_CRC32C     0x0008
#define ISCSI_LOGIN_OPTION_AUTHMETHOD_CHAP              0x0000
#define ISCSI_LOGIN_OPTION_AUTHMETHOD_NON               0x1000
#define ISCSI_LOGIN_OPTION_CHAP_BI                      0x0000
#define ISCSI_LOGIN_OPTION_CHAP_UNI                     0x2000

///
/// VLAN Device Path SubType.
///
#define MSG_VLAN_DP               0x14
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// VLAN identifier (0-4094).
  ///
  UINT16                          VlanId;
} VLAN_DEVICE_PATH;

///
/// Bluetooth Device Path SubType.
///
#define MSG_BLUETOOTH_DP     0x1b
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// 48bit Bluetooth device address.
  ///
  BLUETOOTH_ADDRESS               BD_ADDR;
} BLUETOOTH_DEVICE_PATH;

///
/// Wi-Fi Device Path SubType.
///
#define MSG_WIFI_DP               0x1C
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Service set identifier. A 32-byte octets string.
  ///
  UINT8                           SSId[32];
} WIFI_DEVICE_PATH;

//
// Media Device Path
//
#define MEDIA_DEVICE_PATH         0x04

///
/// Hard Drive Media Device Path SubType.
///
#define MEDIA_HARDDRIVE_DP        0x01

///
/// The Hard Drive Media Device Path is used to represent a partition on a hard drive.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Describes the entry in a partition table, starting with entry 1.
  /// Partition number zero represents the entire device. Valid
  /// partition numbers for a MBR partition are [1, 4]. Valid
  /// partition numbers for a GPT partition are [1, NumberOfPartitionEntries].
  ///
  UINT32                          PartitionNumber;
  ///
  /// Starting LBA of the partition on the hard drive.
  ///
  UINT64                          PartitionStart;
  ///
  /// Size of the partition in units of Logical Blocks.
  ///
  UINT64                          PartitionSize;
  ///
  /// Signature unique to this partition:
  /// If SignatureType is 0, this field has to be initialized with 16 zeros.
  /// If SignatureType is 1, the MBR signature is stored in the first 4 bytes of this field.
  /// The other 12 bytes are initialized with zeros.
  /// If SignatureType is 2, this field contains a 16 byte signature.
  ///
  UINT8                           Signature[16];
  ///
  /// Partition Format: (Unused values reserved).
  /// 0x01 - PC-AT compatible legacy MBR.
  /// 0x02 - GUID Partition Table.
  ///
  UINT8                           MBRType;
  ///
  /// Type of Disk Signature: (Unused values reserved).
  /// 0x00 - No Disk Signature.
  /// 0x01 - 32-bit signature from address 0x1b8 of the type 0x01 MBR.
  /// 0x02 - GUID signature.
  ///
  UINT8                           SignatureType;
} HARDDRIVE_DEVICE_PATH;

#define MBR_TYPE_PCAT             0x01
#define MBR_TYPE_EFI_PARTITION_TABLE_HEADER 0x02

#define NO_DISK_SIGNATURE         0x00
#define SIGNATURE_TYPE_MBR        0x01
#define SIGNATURE_TYPE_GUID       0x02

///
/// CD-ROM Media Device Path SubType.
///
#define MEDIA_CDROM_DP            0x02

///
/// The CD-ROM Media Device Path is used to define a system partition that exists on a CD-ROM.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Boot Entry number from the Boot Catalog. The Initial/Default entry is defined as zero.
  ///
  UINT32                          BootEntry;
  ///
  /// Starting RBA of the partition on the medium. CD-ROMs use Relative logical Block Addressing.
  ///
  UINT64                          PartitionStart;
  ///
  /// Size of the partition in units of Blocks, also called Sectors.
  ///
  UINT64                          PartitionSize;
} CDROM_DEVICE_PATH;

//
// Use VENDOR_DEVICE_PATH struct
//
#define MEDIA_VENDOR_DP           0x03  ///< Media vendor device path subtype.

///
/// File Path Media Device Path SubType
///
#define MEDIA_FILEPATH_DP         0x04
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// A NULL-terminated Path string including directory and file names.
  ///
  CHAR16                          PathName[1];
} FILEPATH_DEVICE_PATH;

#define SIZE_OF_FILEPATH_DEVICE_PATH  OFFSET_OF(FILEPATH_DEVICE_PATH,PathName)

///
/// Media Protocol Device Path SubType.
///
#define MEDIA_PROTOCOL_DP         0x05

///
/// The Media Protocol Device Path is used to denote the protocol that is being 
/// used in a device path at the location of the path specified. 
/// Many protocols are inherent to the style of device path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// The ID of the protocol.
  ///
  EFI_GUID                        Protocol;
} MEDIA_PROTOCOL_DEVICE_PATH;

///
/// PIWG Firmware File SubType.
///
#define MEDIA_PIWG_FW_FILE_DP     0x06

///
/// This device path is used by systems implementing the UEFI PI Specification 1.0 to describe a firmware file.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Firmware file name
  ///
  EFI_GUID                        FvFileName;
} MEDIA_FW_VOL_FILEPATH_DEVICE_PATH;

///
/// PIWG Firmware Volume Device Path SubType.
///
#define MEDIA_PIWG_FW_VOL_DP      0x07

///
/// This device path is used by systems implementing the UEFI PI Specification 1.0 to describe a firmware volume.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Firmware volume name.
  ///
  EFI_GUID                        FvName;
} MEDIA_FW_VOL_DEVICE_PATH;

///
/// Media relative offset range device path.
///
#define MEDIA_RELATIVE_OFFSET_RANGE_DP 0x08

///
/// Used to describe the offset range of media relative.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL  Header;
  UINT32                    Reserved;
  UINT64                    StartingOffset;
  UINT64                    EndingOffset;
} MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH;

///
/// This GUID defines a RAM Disk supporting a raw disk format in volatile memory.
///
#define EFI_VIRTUAL_DISK_GUID               EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_DISK_REGION_VOLATILE

extern  EFI_GUID                            gEfiVirtualDiskGuid;

///
/// This GUID defines a RAM Disk supporting an ISO image in volatile memory.
///
#define EFI_VIRTUAL_CD_GUID                 EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_CD_REGION_VOLATILE

extern  EFI_GUID                            gEfiVirtualCdGuid;

///
/// This GUID defines a RAM Disk supporting a raw disk format in persistent memory.
///
#define EFI_PERSISTENT_VIRTUAL_DISK_GUID    EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_DISK_REGION_PERSISTENT

extern  EFI_GUID                            gEfiPersistentVirtualDiskGuid;

///
/// This GUID defines a RAM Disk supporting an ISO image in persistent memory.
///
#define EFI_PERSISTENT_VIRTUAL_CD_GUID      EFI_ACPI_6_0_NFIT_GUID_RAM_DISK_SUPPORTING_VIRTUAL_CD_REGION_PERSISTENT

extern  EFI_GUID                            gEfiPersistentVirtualCdGuid;

///
/// Media ram disk device path.
///
#define MEDIA_RAM_DISK_DP         0x09

///
/// Used to describe the ram disk device path.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Starting Memory Address.
  ///
  UINT32                          StartingAddr[2];
  ///
  /// Ending Memory Address.
  ///
  UINT32                          EndingAddr[2];
  ///
  /// GUID that defines the type of the RAM Disk.
  ///
  EFI_GUID                        TypeGuid;
  ///
  /// RAM Diskinstance number, if supported. The default value is zero.
  ///
  UINT16                          Instance;
} MEDIA_RAM_DISK_DEVICE_PATH;

///
/// BIOS Boot Specification Device Path.
///
#define BBS_DEVICE_PATH           0x05

///
/// BIOS Boot Specification Device Path SubType.
///
#define BBS_BBS_DP                0x01

///
/// This Device Path is used to describe the booting of non-EFI-aware operating systems.
///
typedef struct {
  EFI_DEVICE_PATH_PROTOCOL        Header;
  ///
  /// Device Type as defined by the BIOS Boot Specification.
  ///
  UINT16                          DeviceType;
  ///
  /// Status Flags as defined by the BIOS Boot Specification.
  ///
  UINT16                          StatusFlag;
  ///
  /// Null-terminated ASCII string that describes the boot device to a user.
  ///
  CHAR8                           String[1];
} BBS_BBS_DEVICE_PATH;

//
// DeviceType definitions - from BBS specification
//
#define BBS_TYPE_FLOPPY           0x01
#define BBS_TYPE_HARDDRIVE        0x02
#define BBS_TYPE_CDROM            0x03
#define BBS_TYPE_PCMCIA           0x04
#define BBS_TYPE_USB              0x05
#define BBS_TYPE_EMBEDDED_NETWORK 0x06
#define BBS_TYPE_BEV              0x80
#define BBS_TYPE_UNKNOWN          0xFF


///
/// Union of all possible Device Paths and pointers to Device Paths.
///
typedef union {
  EFI_DEVICE_PATH_PROTOCOL                   DevPath;
  PCI_DEVICE_PATH                            Pci;
  PCCARD_DEVICE_PATH                         PcCard;
  MEMMAP_DEVICE_PATH                         MemMap;
  VENDOR_DEVICE_PATH                         Vendor;

  CONTROLLER_DEVICE_PATH                     Controller;
  BMC_DEVICE_PATH                            Bmc;
  ACPI_HID_DEVICE_PATH                       Acpi;
  ACPI_EXTENDED_HID_DEVICE_PATH              ExtendedAcpi;
  ACPI_ADR_DEVICE_PATH                       AcpiAdr;

  ATAPI_DEVICE_PATH                          Atapi;
  SCSI_DEVICE_PATH                           Scsi;
  ISCSI_DEVICE_PATH                          Iscsi;
  FIBRECHANNEL_DEVICE_PATH                   FibreChannel;
  FIBRECHANNELEX_DEVICE_PATH                 FibreChannelEx;

  F1394_DEVICE_PATH                          F1394;
  USB_DEVICE_PATH                            Usb;
  SATA_DEVICE_PATH                           Sata;
  USB_CLASS_DEVICE_PATH                      UsbClass;
  USB_WWID_DEVICE_PATH                       UsbWwid;
  DEVICE_LOGICAL_UNIT_DEVICE_PATH            LogicUnit;
  I2O_DEVICE_PATH                            I2O;
  MAC_ADDR_DEVICE_PATH                       MacAddr;
  IPv4_DEVICE_PATH                           Ipv4;
  IPv6_DEVICE_PATH                           Ipv6;
  VLAN_DEVICE_PATH                           Vlan;
  INFINIBAND_DEVICE_PATH                     InfiniBand;
  UART_DEVICE_PATH                           Uart;
  UART_FLOW_CONTROL_DEVICE_PATH              UartFlowControl;
  SAS_DEVICE_PATH                            Sas;
  SASEX_DEVICE_PATH                          SasEx;
  NVME_NAMESPACE_DEVICE_PATH                 NvmeNamespace;
  URI_DEVICE_PATH                            Uri;
  BLUETOOTH_DEVICE_PATH                      Bluetooth;
  WIFI_DEVICE_PATH                           WiFi;
  UFS_DEVICE_PATH                            Ufs;
  SD_DEVICE_PATH                             Sd;
  EMMC_DEVICE_PATH                           Emmc;
  HARDDRIVE_DEVICE_PATH                      HardDrive;
  CDROM_DEVICE_PATH                          CD;

  FILEPATH_DEVICE_PATH                       FilePath;
  MEDIA_PROTOCOL_DEVICE_PATH                 MediaProtocol;

  MEDIA_FW_VOL_DEVICE_PATH                   FirmwareVolume;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH          FirmwareFile;
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH    Offset;
  MEDIA_RAM_DISK_DEVICE_PATH                 RamDisk;
  BBS_BBS_DEVICE_PATH                        Bbs;
} EFI_DEV_PATH;



typedef union {
  EFI_DEVICE_PATH_PROTOCOL                   *DevPath;
  PCI_DEVICE_PATH                            *Pci;
  PCCARD_DEVICE_PATH                         *PcCard;
  MEMMAP_DEVICE_PATH                         *MemMap;
  VENDOR_DEVICE_PATH                         *Vendor;

  CONTROLLER_DEVICE_PATH                     *Controller;
  BMC_DEVICE_PATH                            *Bmc;
  ACPI_HID_DEVICE_PATH                       *Acpi;
  ACPI_EXTENDED_HID_DEVICE_PATH              *ExtendedAcpi;
  ACPI_ADR_DEVICE_PATH                       *AcpiAdr;

  ATAPI_DEVICE_PATH                          *Atapi;
  SCSI_DEVICE_PATH                           *Scsi;
  ISCSI_DEVICE_PATH                          *Iscsi;
  FIBRECHANNEL_DEVICE_PATH                   *FibreChannel;
  FIBRECHANNELEX_DEVICE_PATH                 *FibreChannelEx;

  F1394_DEVICE_PATH                          *F1394;
  USB_DEVICE_PATH                            *Usb;
  SATA_DEVICE_PATH                           *Sata;
  USB_CLASS_DEVICE_PATH                      *UsbClass;
  USB_WWID_DEVICE_PATH                       *UsbWwid;
  DEVICE_LOGICAL_UNIT_DEVICE_PATH            *LogicUnit;
  I2O_DEVICE_PATH                            *I2O;
  MAC_ADDR_DEVICE_PATH                       *MacAddr;
  IPv4_DEVICE_PATH                           *Ipv4;
  IPv6_DEVICE_PATH                           *Ipv6;
  VLAN_DEVICE_PATH                           *Vlan;
  INFINIBAND_DEVICE_PATH                     *InfiniBand;
  UART_DEVICE_PATH                           *Uart;
  UART_FLOW_CONTROL_DEVICE_PATH              *UartFlowControl;
  SAS_DEVICE_PATH                            *Sas;
  SASEX_DEVICE_PATH                          *SasEx;
  NVME_NAMESPACE_DEVICE_PATH                 *NvmeNamespace;
  URI_DEVICE_PATH                            *Uri;
  BLUETOOTH_DEVICE_PATH                      *Bluetooth;
  WIFI_DEVICE_PATH                           *WiFi;
  UFS_DEVICE_PATH                            *Ufs;
  SD_DEVICE_PATH                             *Sd;
  EMMC_DEVICE_PATH                           *Emmc;
  HARDDRIVE_DEVICE_PATH                      *HardDrive;
  CDROM_DEVICE_PATH                          *CD;

  FILEPATH_DEVICE_PATH                       *FilePath;
  MEDIA_PROTOCOL_DEVICE_PATH                 *MediaProtocol;

  MEDIA_FW_VOL_DEVICE_PATH                   *FirmwareVolume;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH          *FirmwareFile;
  MEDIA_RELATIVE_OFFSET_RANGE_DEVICE_PATH    *Offset;
  MEDIA_RAM_DISK_DEVICE_PATH                 *RamDisk;
  BBS_BBS_DEVICE_PATH                        *Bbs;
  UINT8                                      *Raw;
} EFI_DEV_PATH_PTR;

#pragma pack()
                                             
#define END_DEVICE_PATH_TYPE                 0x7f
#define END_ENTIRE_DEVICE_PATH_SUBTYPE       0xFF
#define END_INSTANCE_DEVICE_PATH_SUBTYPE     0x01

extern EFI_GUID gEfiDevicePathProtocolGuid;

#endif
