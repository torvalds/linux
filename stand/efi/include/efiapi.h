/* $FreeBSD$ */
#ifndef _EFI_API_H
#define _EFI_API_H

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

    efiapi.h

Abstract:

    Global EFI runtime & boot service interfaces




Revision History

--*/

//
// EFI Specification Revision
//

#define EFI_SPECIFICATION_MAJOR_REVISION 1
#define EFI_SPECIFICATION_MINOR_REVISION 10

//
// Declare forward referenced data structures
//

INTERFACE_DECL(_EFI_SYSTEM_TABLE);

//
// EFI Memory
//

typedef
EFI_STATUS
(EFIAPI *EFI_ALLOCATE_PAGES) (
    IN EFI_ALLOCATE_TYPE            Type,
    IN EFI_MEMORY_TYPE              MemoryType,
    IN UINTN                        NoPages,
    OUT EFI_PHYSICAL_ADDRESS        *Memory
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FREE_PAGES) (
    IN EFI_PHYSICAL_ADDRESS         Memory,
    IN UINTN                        NoPages
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GET_MEMORY_MAP) (
    IN OUT UINTN                    *MemoryMapSize,
    IN OUT EFI_MEMORY_DESCRIPTOR    *MemoryMap,
    OUT UINTN                       *MapKey,
    OUT UINTN                       *DescriptorSize,
    OUT UINT32                      *DescriptorVersion
    );

#define NextMemoryDescriptor(Ptr,Size)  ((EFI_MEMORY_DESCRIPTOR *) (((UINT8 *) Ptr) + Size))


typedef
EFI_STATUS
(EFIAPI *EFI_ALLOCATE_POOL) (
    IN EFI_MEMORY_TYPE              PoolType,
    IN UINTN                        Size,
    OUT VOID                        **Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FREE_POOL) (
    IN VOID                         *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_SET_VIRTUAL_ADDRESS_MAP) (
    IN UINTN                        MemoryMapSize,
    IN UINTN                        DescriptorSize,
    IN UINT32                       DescriptorVersion,
    IN EFI_MEMORY_DESCRIPTOR        *VirtualMap
    );


#define EFI_OPTIONAL_PTR            0x00000001
#define EFI_INTERNAL_FNC            0x00000002      // Pointer to internal runtime fnc
#define EFI_INTERNAL_PTR            0x00000004      // Pointer to internal runtime data


typedef
EFI_STATUS
(EFIAPI *EFI_CONVERT_POINTER) (
    IN UINTN                        DebugDisposition,
    IN OUT VOID                     **Address
    );


//
// EFI Events
//



#define EVT_TIMER                           0x80000000
#define EVT_RUNTIME                         0x40000000
#define EVT_RUNTIME_CONTEXT                 0x20000000

#define EVT_NOTIFY_WAIT                     0x00000100
#define EVT_NOTIFY_SIGNAL                   0x00000200

#define EVT_SIGNAL_EXIT_BOOT_SERVICES       0x00000201
#define EVT_SIGNAL_VIRTUAL_ADDRESS_CHANGE   0x60000202

#define EVT_EFI_SIGNAL_MASK                 0x000000FF
#define EVT_EFI_SIGNAL_MAX                  2

typedef
VOID
(EFIAPI *EFI_EVENT_NOTIFY) (
    IN EFI_EVENT                Event,
    IN VOID                     *Context
    );

typedef
EFI_STATUS
(EFIAPI *EFI_CREATE_EVENT) (
    IN UINT32                       Type,
    IN EFI_TPL                      NotifyTpl,
    IN EFI_EVENT_NOTIFY             NotifyFunction,
    IN VOID                         *NotifyContext,
    OUT EFI_EVENT                   *Event
    );

typedef enum {
    TimerCancel,
    TimerPeriodic,
    TimerRelative,
    TimerTypeMax
} EFI_TIMER_DELAY;

typedef
EFI_STATUS
(EFIAPI *EFI_SET_TIMER) (
    IN EFI_EVENT                Event,
    IN EFI_TIMER_DELAY          Type,
    IN UINT64                   TriggerTime
    );

typedef
EFI_STATUS
(EFIAPI *EFI_SIGNAL_EVENT) (
    IN EFI_EVENT                Event
    );

typedef
EFI_STATUS
(EFIAPI *EFI_WAIT_FOR_EVENT) (
    IN UINTN                    NumberOfEvents,
    IN EFI_EVENT                *Event,
    OUT UINTN                   *Index
    );

typedef
EFI_STATUS
(EFIAPI *EFI_CLOSE_EVENT) (
    IN EFI_EVENT                Event
    );

typedef
EFI_STATUS
(EFIAPI *EFI_CHECK_EVENT) (
    IN EFI_EVENT                Event
    );

//
// Task priority level
//

#define TPL_APPLICATION    4
#define TPL_CALLBACK       8
#define TPL_NOTIFY        16
#define TPL_HIGH_LEVEL    31

typedef
EFI_TPL
(EFIAPI *EFI_RAISE_TPL) (
    IN EFI_TPL      NewTpl
    );

typedef
VOID
(EFIAPI *EFI_RESTORE_TPL) (
    IN EFI_TPL      OldTpl
    );


//
// EFI platform varibles
//

#define EFI_GLOBAL_VARIABLE \
    { 0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C} }

// Variable attributes
#define EFI_VARIABLE_NON_VOLATILE		0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS		0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS		0x00000004
#define	EFI_VARIABLE_HARDWARE_ERROR_RECORD	0x00000008
#define	EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS	0x00000010
#define	EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS	0x00000020
#define	EFI_VARIABLE_APPEND_WRITE		0x00000040

// Variable size limitation
#define EFI_MAXIMUM_VARIABLE_SIZE           1024

typedef
EFI_STATUS
(EFIAPI *EFI_GET_VARIABLE) (
    IN CHAR16                       *VariableName,
    IN EFI_GUID                     *VendorGuid,
    OUT UINT32                      *Attributes OPTIONAL,
    IN OUT UINTN                    *DataSize,
    OUT VOID                        *Data
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_VARIABLE_NAME) (
    IN OUT UINTN                    *VariableNameSize,
    IN OUT CHAR16                   *VariableName,
    IN OUT EFI_GUID                 *VendorGuid
    );


typedef
EFI_STATUS
(EFIAPI *EFI_SET_VARIABLE) (
    IN const CHAR16                 *VariableName,
    IN EFI_GUID                     *VendorGuid,
    IN UINT32                       Attributes,
    IN UINTN                        DataSize,
    IN VOID                         *Data
    );


//
// EFI Time
//

typedef struct {
        UINT32                      Resolution;     // 1e-6 parts per million
        UINT32                      Accuracy;       // hertz
        BOOLEAN                     SetsToZero;     // Set clears sub-second time
} EFI_TIME_CAPABILITIES;


typedef
EFI_STATUS
(EFIAPI *EFI_GET_TIME) (
    OUT EFI_TIME                    *Time,
    OUT EFI_TIME_CAPABILITIES       *Capabilities OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_SET_TIME) (
    IN EFI_TIME                     *Time
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GET_WAKEUP_TIME) (
    OUT BOOLEAN                     *Enabled,
    OUT BOOLEAN                     *Pending,
    OUT EFI_TIME                    *Time
    );

typedef
EFI_STATUS
(EFIAPI *EFI_SET_WAKEUP_TIME) (
    IN BOOLEAN                      Enable,
    IN EFI_TIME                     *Time OPTIONAL
    );


//
// Image functions
//


// PE32+ Subsystem type for EFI images

#if !defined(IMAGE_SUBSYSTEM_EFI_APPLICATION)
#define IMAGE_SUBSYSTEM_EFI_APPLICATION             10
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER     11
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER          12
#endif

// PE32+ Machine type for EFI images

#if !defined(EFI_IMAGE_MACHINE_IA32)
#define EFI_IMAGE_MACHINE_IA32      0x014c
#endif

#if !defined(EFI_IMAGE_MACHINE_EBC)
#define EFI_IMAGE_MACHINE_EBC       0x0EBC
#endif

// Image Entry prototype

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_ENTRY_POINT) (
    IN EFI_HANDLE                   ImageHandle,
    IN struct _EFI_SYSTEM_TABLE     *SystemTable
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_LOAD) (
    IN BOOLEAN                      BootPolicy,
    IN EFI_HANDLE                   ParentImageHandle,
    IN EFI_DEVICE_PATH              *FilePath,
    IN VOID                         *SourceBuffer   OPTIONAL,
    IN UINTN                        SourceSize,
    OUT EFI_HANDLE                  *ImageHandle
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_START) (
    IN EFI_HANDLE                   ImageHandle,
    OUT UINTN                       *ExitDataSize,
    OUT CHAR16                      **ExitData  OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_EXIT) (
    IN EFI_HANDLE                   ImageHandle,
    IN EFI_STATUS                   ExitStatus,
    IN UINTN                        ExitDataSize,
    IN CHAR16                       *ExitData OPTIONAL
    ) __dead2;

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_UNLOAD) (
    IN EFI_HANDLE                   ImageHandle
    );


// Image handle
#define LOADED_IMAGE_PROTOCOL \
    { 0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B} }

#define EFI_LOADED_IMAGE_INFORMATION_REVISION      0x1000
typedef struct {
    UINT32                          Revision;
    EFI_HANDLE                      ParentHandle;
    struct _EFI_SYSTEM_TABLE        *SystemTable;

    // Source location of image
    EFI_HANDLE                      DeviceHandle;
    EFI_DEVICE_PATH                 *FilePath;
    VOID                            *Reserved;

    // Images load options
    UINT32                          LoadOptionsSize;
    VOID                            *LoadOptions;

    // Location of where image was loaded
    VOID                            *ImageBase;
    UINT64                          ImageSize;
    EFI_MEMORY_TYPE                 ImageCodeType;
    EFI_MEMORY_TYPE                 ImageDataType;

    // If the driver image supports a dynamic unload request
    EFI_IMAGE_UNLOAD                Unload;

} EFI_LOADED_IMAGE;


typedef
EFI_STATUS
(EFIAPI *EFI_EXIT_BOOT_SERVICES) (
    IN EFI_HANDLE                   ImageHandle,
    IN UINTN                        MapKey
    );

//
// Misc
//


typedef
EFI_STATUS
(EFIAPI *EFI_STALL) (
    IN UINTN                    Microseconds
    );

typedef
EFI_STATUS
(EFIAPI *EFI_SET_WATCHDOG_TIMER) (
    IN UINTN                    Timeout,
    IN UINT64                   WatchdogCode,
    IN UINTN                    DataSize,
    IN CHAR16                   *WatchdogData OPTIONAL
    );


typedef enum {
    EfiResetCold,
    EfiResetWarm,
    EfiResetShutdown
} EFI_RESET_TYPE;

typedef
VOID
(EFIAPI *EFI_RESET_SYSTEM) (
    IN EFI_RESET_TYPE           ResetType,
    IN EFI_STATUS               ResetStatus,
    IN UINTN                    DataSize,
    IN CHAR16                   *ResetData OPTIONAL
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_MONOTONIC_COUNT) (
    OUT UINT64                  *Count
    );

typedef
EFI_STATUS
(EFIAPI *EFI_GET_NEXT_HIGH_MONO_COUNT) (
    OUT UINT32                  *HighCount
    );

//
// Protocol handler functions
//

typedef enum {
    EFI_NATIVE_INTERFACE
} EFI_INTERFACE_TYPE;

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE) (
    IN OUT EFI_HANDLE           *Handle,
    IN EFI_GUID                 *Protocol,
    IN EFI_INTERFACE_TYPE       InterfaceType,
    IN VOID                     *Interface
    );

typedef
EFI_STATUS
(EFIAPI *EFI_REINSTALL_PROTOCOL_INTERFACE) (
    IN EFI_HANDLE               Handle,
    IN EFI_GUID                 *Protocol,
    IN VOID                     *OldInterface,
    IN VOID                     *NewInterface
    );

typedef
EFI_STATUS
(EFIAPI *EFI_UNINSTALL_PROTOCOL_INTERFACE) (
    IN EFI_HANDLE               Handle,
    IN EFI_GUID                 *Protocol,
    IN VOID                     *Interface
    );

typedef
EFI_STATUS
(EFIAPI *EFI_HANDLE_PROTOCOL) (
    IN EFI_HANDLE               Handle,
    IN EFI_GUID                 *Protocol,
    OUT VOID                    **Interface
    );

typedef
EFI_STATUS
(EFIAPI *EFI_REGISTER_PROTOCOL_NOTIFY) (
    IN EFI_GUID                 *Protocol,
    IN EFI_EVENT                Event,
    OUT VOID                    **Registration
    );

typedef enum {
    AllHandles,
    ByRegisterNotify,
    ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_HANDLE) (
    IN EFI_LOCATE_SEARCH_TYPE   SearchType,
    IN EFI_GUID                 *Protocol OPTIONAL,
    IN VOID                     *SearchKey OPTIONAL,
    IN OUT UINTN                *BufferSize,
    OUT EFI_HANDLE              *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_DEVICE_PATH) (
    IN EFI_GUID                 *Protocol,
    IN OUT EFI_DEVICE_PATH      **DevicePath,
    OUT EFI_HANDLE              *Device
    );

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_CONFIGURATION_TABLE) (
    IN EFI_GUID                 *Guid,
    IN VOID                     *Table
    );

typedef
EFI_STATUS
(EFIAPI *EFI_RESERVED_SERVICE) (
    VOID
    );

typedef
EFI_STATUS
(EFIAPI *EFI_CONNECT_CONTROLLER) (
  IN  EFI_HANDLE                    ControllerHandle,
  IN  EFI_HANDLE                    *DriverImageHandle    OPTIONAL,
  IN  EFI_DEVICE_PATH               *RemainingDevicePath  OPTIONAL,
  IN  BOOLEAN                       Recursive
  );

typedef
EFI_STATUS
(EFIAPI *EFI_DISCONNECT_CONTROLLER)(
  IN EFI_HANDLE           ControllerHandle,
  IN EFI_HANDLE           DriverImageHandle, OPTIONAL
  IN EFI_HANDLE           ChildHandle        OPTIONAL
  );

#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL	 0x00000001
#define EFI_OPEN_PROTOCOL_GET_PROTOCOL	       0x00000002
#define EFI_OPEN_PROTOCOL_TEST_PROTOCOL        0x00000004
#define EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER  0x00000008
#define EFI_OPEN_PROTOCOL_BY_DRIVER            0x00000010
#define EFI_OPEN_PROTOCOL_EXCLUSIVE            0x00000020

typedef
EFI_STATUS
(EFIAPI *EFI_OPEN_PROTOCOL) (
  IN EFI_HANDLE                 Handle,
  IN EFI_GUID                   *Protocol,
  OUT VOID                      **Interface,
  IN  EFI_HANDLE                ImageHandle,
  IN  EFI_HANDLE                ControllerHandle, OPTIONAL
  IN  UINT32                    Attributes
  );

typedef
EFI_STATUS
(EFIAPI *EFI_CLOSE_PROTOCOL) (
  IN EFI_HANDLE               Handle,
  IN EFI_GUID                 *Protocol,
  IN EFI_HANDLE               ImageHandle,
  IN EFI_HANDLE               DeviceHandle
  );

typedef struct {
  EFI_HANDLE                  AgentHandle;
  EFI_HANDLE                  ControllerHandle;
  UINT32                      Attributes;
  UINT32                      OpenCount;
} EFI_OPEN_PROTOCOL_INFORMATION_ENTRY;

typedef
EFI_STATUS
(EFIAPI *EFI_OPEN_PROTOCOL_INFORMATION) (
  IN  EFI_HANDLE                          UserHandle,
  IN  EFI_GUID                            *Protocol,
  IN  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **EntryBuffer,
  OUT UINTN                               *EntryCount
  );

typedef
EFI_STATUS
(EFIAPI *EFI_PROTOCOLS_PER_HANDLE) (
  IN EFI_HANDLE       UserHandle,
  OUT EFI_GUID        ***ProtocolBuffer,
  OUT UINTN           *ProtocolBufferCount
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_HANDLE_BUFFER) (
  IN EFI_LOCATE_SEARCH_TYPE       SearchType,
  IN EFI_GUID                     *Protocol OPTIONAL,
  IN VOID                         *SearchKey OPTIONAL,
  IN OUT UINTN                    *NumberHandles,
  OUT EFI_HANDLE                  **Buffer
  );

typedef
EFI_STATUS
(EFIAPI *EFI_LOCATE_PROTOCOL) (
  EFI_GUID  *Protocol,
  VOID      *Registration, OPTIONAL
  VOID      **Interface
  );

typedef
EFI_STATUS
(EFIAPI *EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES) (
  IN OUT EFI_HANDLE           *Handle,
  ...
  );

typedef
EFI_STATUS
(EFIAPI *EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES) (
  IN EFI_HANDLE           Handle,
  ...
  );

typedef
EFI_STATUS
(EFIAPI *EFI_CALCULATE_CRC32) (
  IN  VOID                              *Data,
  IN  UINTN                             DataSize,
  OUT UINT32                            *Crc32
  );

typedef
VOID
(EFIAPI *EFI_COPY_MEM) (
  IN VOID     *Destination,
  IN VOID     *Source,
  IN UINTN    Length
  );

typedef
VOID
(EFIAPI *EFI_SET_MEM) (
  IN VOID     *Buffer,
  IN UINTN    Size,
  IN UINT8    Value
  );

//
// Standard EFI table header
//

typedef struct _EFI_TABLE_HEARDER {
  UINT64                      Signature;
  UINT32                      Revision;
  UINT32                      HeaderSize;
  UINT32                      CRC32;
  UINT32                      Reserved;
} EFI_TABLE_HEADER;


//
// EFI Runtime Serivces Table
//

#define EFI_RUNTIME_SERVICES_SIGNATURE  0x56524553544e5552
#define EFI_RUNTIME_SERVICES_REVISION   ((EFI_SPECIFICATION_MAJOR_REVISION<<16) | (EFI_SPECIFICATION_MINOR_REVISION))

typedef struct  {
  EFI_TABLE_HEADER                Hdr;

  //
  // Time services
  //

  EFI_GET_TIME                    GetTime;
  EFI_SET_TIME                    SetTime;
  EFI_GET_WAKEUP_TIME             GetWakeupTime;
  EFI_SET_WAKEUP_TIME             SetWakeupTime;

  //
  // Virtual memory services
  //

  EFI_SET_VIRTUAL_ADDRESS_MAP     SetVirtualAddressMap;
  EFI_CONVERT_POINTER             ConvertPointer;

  //
  // Variable serviers
  //

  EFI_GET_VARIABLE                GetVariable;
  EFI_GET_NEXT_VARIABLE_NAME      GetNextVariableName;
  EFI_SET_VARIABLE                SetVariable;

  //
  // Misc
  //

  EFI_GET_NEXT_HIGH_MONO_COUNT    GetNextHighMonotonicCount;
  EFI_RESET_SYSTEM                ResetSystem;

} EFI_RUNTIME_SERVICES;


//
// EFI Boot Services Table
//

#define EFI_BOOT_SERVICES_SIGNATURE     0x56524553544f4f42
#define EFI_BOOT_SERVICES_REVISION      ((EFI_SPECIFICATION_MAJOR_REVISION<<16) | (EFI_SPECIFICATION_MINOR_REVISION))

typedef struct {

  EFI_TABLE_HEADER                Hdr;

  //
  // Task priority functions
  //

  EFI_RAISE_TPL                   RaiseTPL;
  EFI_RESTORE_TPL                 RestoreTPL;

  //
  // Memory functions
  //

  EFI_ALLOCATE_PAGES              AllocatePages;
  EFI_FREE_PAGES                  FreePages;
  EFI_GET_MEMORY_MAP              GetMemoryMap;
  EFI_ALLOCATE_POOL               AllocatePool;
  EFI_FREE_POOL                   FreePool;

  //
  // Event & timer functions
  //

  EFI_CREATE_EVENT                CreateEvent;
  EFI_SET_TIMER                   SetTimer;
  EFI_WAIT_FOR_EVENT              WaitForEvent;
  EFI_SIGNAL_EVENT                SignalEvent;
  EFI_CLOSE_EVENT                 CloseEvent;
  EFI_CHECK_EVENT                 CheckEvent;

  //
  // Protocol handler functions
  //

  EFI_INSTALL_PROTOCOL_INTERFACE  InstallProtocolInterface;
  EFI_REINSTALL_PROTOCOL_INTERFACE ReinstallProtocolInterface;
  EFI_UNINSTALL_PROTOCOL_INTERFACE UninstallProtocolInterface;
  EFI_HANDLE_PROTOCOL             HandleProtocol;
  VOID                            *Reserved;
  EFI_REGISTER_PROTOCOL_NOTIFY    RegisterProtocolNotify;
  EFI_LOCATE_HANDLE               LocateHandle;
  EFI_LOCATE_DEVICE_PATH          LocateDevicePath;
  EFI_INSTALL_CONFIGURATION_TABLE InstallConfigurationTable;

  //
  // Image functions
  //

  EFI_IMAGE_LOAD                  LoadImage;
  EFI_IMAGE_START                 StartImage;
  EFI_EXIT                        Exit;
  EFI_IMAGE_UNLOAD                UnloadImage;
  EFI_EXIT_BOOT_SERVICES          ExitBootServices;

  //
  // Misc functions
  //

  EFI_GET_NEXT_MONOTONIC_COUNT    GetNextMonotonicCount;
  EFI_STALL                       Stall;
  EFI_SET_WATCHDOG_TIMER          SetWatchdogTimer;

  //
  // DriverSupport Services
  //
  EFI_CONNECT_CONTROLLER	        ConnectController;
  EFI_DISCONNECT_CONTROLLER       DisconnectController;

  //
  // Open and Close Protocol Services
  //
  EFI_OPEN_PROTOCOL               OpenProtocol;
  EFI_CLOSE_PROTOCOL              CloseProtocol;
  EFI_OPEN_PROTOCOL_INFORMATION   OpenProtocolInformation;

  //
  // Library Services to reduce size of drivers
  //
  EFI_PROTOCOLS_PER_HANDLE        ProtocolsPerHandle;
  EFI_LOCATE_HANDLE_BUFFER        LocateHandleBuffer;
  EFI_LOCATE_PROTOCOL             LocateProtocol;

  EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES    InstallMultipleProtocolInterfaces;
  EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES  UninstallMultipleProtocolInterfaces;

  //
  // CRC32 services
  //
  EFI_CALCULATE_CRC32             CalculateCrc32;

  //
  // Memory Utility Services
  //
  EFI_COPY_MEM                    CopyMem;
  EFI_SET_MEM                     SetMem;

} EFI_BOOT_SERVICES;


//
// EFI Configuration Table and GUID definitions
//

#define MPS_TABLE_GUID \
    { 0xeb9d2d2f, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define ACPI_TABLE_GUID \
    { 0xeb9d2d30, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define ACPI_20_TABLE_GUID \
    { 0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81} }

#define SMBIOS_TABLE_GUID \
    { 0xeb9d2d31, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define SMBIOS3_TABLE_GUID \
	{ 0xf2fd1544, 0x9794, 0x4a2c, {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94} }

#define SAL_SYSTEM_TABLE_GUID  \
    { 0xeb9d2d32, 0x2d88, 0x11d3, {0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define FDT_TABLE_GUID \
    { 0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0} }

#define DXE_SERVICES_TABLE_GUID \
    { 0x5ad34ba, 0x6f02, 0x4214, {0x95, 0x2e, 0x4d, 0xa0, 0x39, 0x8e, 0x2b, 0xb9} }

#define HOB_LIST_TABLE_GUID \
    { 0x7739f24c, 0x93d7, 0x11d4, {0x9a, 0x3a, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define LZMA_DECOMPRESSION_GUID \
	{ 0xee4e5898, 0x3914, 0x4259, {0x9d, 0x6e, 0xdc, 0x7b, 0xd7, 0x94, 0x3, 0xcf} }

#define ARM_MP_CORE_INFO_TABLE_GUID \
	{ 0xa4ee0728, 0xe5d7, 0x4ac5, {0xb2, 0x1e, 0x65, 0x8e, 0xd8, 0x57, 0xe8, 0x34} }

#define ESRT_TABLE_GUID \
	{ 0xb122a263, 0x3661, 0x4f68, {0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80} }

#define MEMORY_TYPE_INFORMATION_TABLE_GUID \
    { 0x4c19049f, 0x4137, 0x4dd3, {0x9c, 0x10, 0x8b, 0x97, 0xa8, 0x3f, 0xfd, 0xfa} }

#define DEBUG_IMAGE_INFO_TABLE_GUID \
    { 0x49152e77, 0x1ada, 0x4764, {0xb7, 0xa2, 0x7a, 0xfe, 0xfe, 0xd9, 0x5e, 0x8b} }

typedef struct _EFI_CONFIGURATION_TABLE {
  EFI_GUID                VendorGuid;
  VOID                    *VendorTable;
} EFI_CONFIGURATION_TABLE;


//
// EFI System Table
//




#define EFI_SYSTEM_TABLE_SIGNATURE      0x5453595320494249
#define EFI_SYSTEM_TABLE_REVISION      ((EFI_SPECIFICATION_MAJOR_REVISION<<16) | (EFI_SPECIFICATION_MINOR_REVISION))
#define EFI_1_10_SYSTEM_TABLE_REVISION ((1<<16) | 10)
#define EFI_1_02_SYSTEM_TABLE_REVISION ((1<<16) | 02)

typedef struct _EFI_SYSTEM_TABLE {
  EFI_TABLE_HEADER                Hdr;

  CHAR16                          *FirmwareVendor;
  UINT32                          FirmwareRevision;

  EFI_HANDLE                      ConsoleInHandle;
  SIMPLE_INPUT_INTERFACE          *ConIn;

  EFI_HANDLE                      ConsoleOutHandle;
  SIMPLE_TEXT_OUTPUT_INTERFACE    *ConOut;

  EFI_HANDLE                      StandardErrorHandle;
  SIMPLE_TEXT_OUTPUT_INTERFACE    *StdErr;

  EFI_RUNTIME_SERVICES            *RuntimeServices;
  EFI_BOOT_SERVICES               *BootServices;

  UINTN                           NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE         *ConfigurationTable;

} EFI_SYSTEM_TABLE;

/*
 * unlisted GUID's..
 */
#define EFI_EBC_INTERPRETER_PROTOCOL_GUID \
{ 0x13AC6DD1, 0x73D0, 0x11D4, {0xB0, 0x6B, 0x00, 0xAA, 0x00, 0xBD, 0x6D, 0xE7} }

#define EFI_DRIVER_CONFIGURATION2_PROTOCOL_GUID \
{ 0xbfd7dc1d, 0x24f1, 0x40d9, {0x82, 0xe7, 0x2e, 0x09, 0xbb, 0x6b, 0x4e, 0xbe} }

#define EFI_DRIVER_CONFIGURATION_PROTOCOL_GUID \
{ 0x107a772b, 0xd5e1, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_DRIVER_BINDING_PROTOCOL_GUID \
  { 0x18A031AB, 0xB443, 0x4D1A, \
    { 0xA5, 0xC0, 0x0C, 0x09, 0x26, 0x1E, 0x9F, 0x71 } \
  }

#define EFI_TAPE_IO_PROTOCOL_GUID \
  { 0x1e93e633, 0xd65a, 0x459e, \
    { 0xab, 0x84, 0x93, 0xd9, 0xec, 0x26, 0x6d, 0x18 } \
  }

#define EFI_SCSI_IO_PROTOCOL_GUID \
  { 0x932f47e6, 0x2362, 0x4002, \
    { 0x80, 0x3e, 0x3c, 0xd5, 0x4b, 0x13, 0x8f, 0x85 } \
  }

#define EFI_USB2_HC_PROTOCOL_GUID \
  { 0x3e745226, 0x9818, 0x45b6, \
    { 0xa2, 0xac, 0xd7, 0xcd, 0x0e, 0x8b, 0xa2, 0xbc } \
  }

#define EFI_DEBUG_SUPPORT_PROTOCOL_GUID \
  { 0x2755590C, 0x6F3C, 0x42FA, \
    { 0x9E, 0xA4, 0xA3, 0xBA, 0x54, 0x3C, 0xDA, 0x25 } \
  }

#define EFI_DEBUGPORT_PROTOCOL_GUID \
  { 0xEBA4E8D2, 0x3858, 0x41EC, \
    { 0xA2, 0x81, 0x26, 0x47, 0xBA, 0x96, 0x60, 0xD0 } \
  }

#define EFI_DECOMPRESS_PROTOCOL_GUID \
  { 0xd8117cfe, 0x94a6, 0x11d4, \
    { 0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } \
  }

#define EFI_ACPI_TABLE_PROTOCOL_GUID \
  { 0xffe06bdd, 0x6107, 0x46a6, \
    { 0x7b, 0xb2, 0x5a, 0x9c, 0x7e, 0xc5, 0x27, 0x5c} \
  }

#define EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID \
  { 0x587e72d7, 0xcc50, 0x4f79, \
    { 0x82, 0x09, 0xca, 0x29, 0x1f, 0xc1, 0xa1, 0x0f } \
  }

#define EFI_HII_DATABASE_PROTOCOL_GUID \
  { 0xef9fc172, 0xa1b2, 0x4693, \
    { 0xb3, 0x27, 0x6d, 0x32, 0xfc, 0x41, 0x60, 0x42 } \
  }

#define EFI_HII_STRING_PROTOCOL_GUID \
  { 0xfd96974, 0x23aa, 0x4cdc, \
    { 0xb9, 0xcb, 0x98, 0xd1, 0x77, 0x50, 0x32, 0x2a } \
  }

#define EFI_HII_IMAGE_PROTOCOL_GUID \
  { 0x31a6406a, 0x6bdf, 0x4e46, \
    { 0xb2, 0xa2, 0xeb, 0xaa, 0x89, 0xc4, 0x9, 0x20 } \
  }

#define EFI_HII_FONT_PROTOCOL_GUID \
  { 0xe9ca4775, 0x8657, 0x47fc, \
    { 0x97, 0xe7, 0x7e, 0xd6, 0x5a, 0x8, 0x43, 0x24 } \
  }
#define EFI_HII_CONFIGURATION_ACCESS_PROTOCOL_GUID \
  { 0x330d4706, 0xf2a0, 0x4e4f, \
    { 0xa3, 0x69, 0xb6, 0x6f, 0xa8, 0xd5, 0x43, 0x85 } \
  }

#define EFI_COMPONENT_NAME_PROTOCOL_GUID \
{ 0x107a772c, 0xd5e1, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_COMPONENT_NAME2_PROTOCOL_GUID \
  { 0x6a7a5cff, 0xe8d9, 0x4f70, \
    { 0xba, 0xda, 0x75, 0xab, 0x30, 0x25, 0xce, 0x14} \
  }

#define EFI_USB_IO_PROTOCOL_GUID \
  { 0x2B2F68D6, 0x0CD2, 0x44cf, \
    { 0x8E, 0x8B, 0xBB, 0xA2, 0x0B, 0x1B, 0x5B, 0x75 } \
  }
#define EFI_HCDP_TABLE_GUID \
  { 0xf951938d, 0x620b, 0x42ef, \
      { 0x82, 0x79, 0xa8, 0x4b, 0x79, 0x61, 0x78, 0x98 } \
  }

#define EFI_DEVICE_TREE_GUID \
  { 0xb1b621d5, 0xf19c, 0x41a5, \
      { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } \
  }

#define EFI_VENDOR_APPLE_GUID \
  { 0x2B0585EB, 0xD8B8, 0x49A9, \
      { 0x8B, 0x8C, 0xE2, 0x1B, 0x01, 0xAE, 0xF2, 0xB7 } \
  }

#define EFI_CONSOLE_IN_DEVICE_GUID    \
{ 0xd3b36f2b, 0xd551, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_CONSOLE_OUT_DEVICE_GUID    \
{ 0xd3b36f2c, 0xd551, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_STANDARD_ERROR_DEVICE_GUID	\
{ 0xd3b36f2d, 0xd551, 0x11d4, {0x9a, 0x46, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_UNICODE_COLLATION2_PROTOCOL_GUID \
{ 0xa4c751fc, 0x23ae, 0x4c3e, {0x92, 0xe9, 0x49, 0x64, 0xcf, 0x63, 0xf3, 0x49} }

#define EFI_FORM_BROWSER2_PROTOCOL_GUID \
{ 0xb9d4c360, 0xbcfb, 0x4f9b, {0x92, 0x98, 0x53, 0xc1, 0x36, 0x98, 0x22, 0x58} }

#define EFI_ARP_SERVICE_BINDING_PROTOCOL_GUID \
{ 0xf44c00ee, 0x1f2c, 0x4a00, {0xaa, 0x9, 0x1c, 0x9f, 0x3e, 0x8, 0x0, 0xa3} }

#define EFI_ARP_PROTOCOL_GUID \
{ 0xf4b427bb, 0xba21, 0x4f16, {0xbc, 0x4e, 0x43, 0xe4, 0x16, 0xab, 0x61, 0x9c} }

#define EFI_IP4_CONFIG_PROTOCOL_GUID \
{ 0x3b95aa31, 0x3793, 0x434b, {0x86, 0x67, 0xc8, 0x07, 0x08, 0x92, 0xe0, 0x5e} }

#define EFI_IP6_CONFIG_PROTOCOL_GUID \
{ 0x937fe521, 0x95ae, 0x4d1a, {0x89, 0x29, 0x48, 0xbc, 0xd9, 0x0a, 0xd3, 0x1a} }

#define EFI_MANAGED_NETWORK_SERVICE_BINDING_PROTOCOL_GUID \
{ 0xf36ff770, 0xa7e1, 0x42cf, {0x9e, 0xd2, 0x56, 0xf0, 0xf2, 0x71, 0xf4, 0x4c} }

#define EFI_MANAGED_NETWORK_PROTOCOL_GUID \
{ 0x7ab33a91, 0xace5, 0x4326, {0xb5, 0x72, 0xe7, 0xee, 0x33, 0xd3, 0x9f, 0x16} }

#define EFI_MTFTP4_SERVICE_BINDING_PROTOCOL_GUID \
{ 0x2FE800BE, 0x8F01, 0x4aa6, {0x94, 0x6B, 0xD7, 0x13, 0x88, 0xE1, 0x83, 0x3F} }

#define EFI_MTFTP4_PROTOCOL_GUID \
{ 0x78247c57, 0x63db, 0x4708, {0x99, 0xc2, 0xa8, 0xb4, 0xa9, 0xa6, 0x1f, 0x6b} }

#define EFI_MTFTP6_SERVICE_BINDING_PROTOCOL_GUID \
{ 0xd9760ff3, 0x3cca, 0x4267, {0x80, 0xf9, 0x75, 0x27, 0xfa, 0xfa, 0x42, 0x23} }

#define EFI_MTFTP6_PROTOCOL_GUID \
{ 0xbf0a78ba, 0xec29, 0x49cf, {0xa1, 0xc9, 0x7a, 0xe5, 0x4e, 0xab, 0x6a, 0x51} }

#define EFI_DHCP4_PROTOCOL_GUID \
{ 0x8a219718, 0x4ef5, 0x4761, {0x91, 0xc8, 0xc0, 0xf0, 0x4b, 0xda, 0x9e, 0x56} }

#define EFI_DHCP4_SERVICE_BINDING_PROTOCOL_GUID \
{ 0x9d9a39d8, 0xbd42, 0x4a73, {0xa4, 0xd5, 0x8e, 0xe9, 0x4b, 0xe1, 0x13, 0x80} }

#define EFI_DHCP6_SERVICE_BINDING_PROTOCOL_GUID \
{ 0x9fb9a8a1, 0x2f4a, 0x43a6, {0x88, 0x9c, 0xd0, 0xf7, 0xb6, 0xc4, 0x7a, 0xd5} }

#define EFI_DHCP6_PROTOCOL_GUID \
{ 0x87c8bad7, 0x595, 0x4053, {0x82, 0x97, 0xde, 0xde, 0x39, 0x5f, 0x5d, 0x5b} }

#define EFI_SCSI_PASS_THRU_PROTOCOL_GUID \
{ 0xa59e8fcf, 0xbda0, 0x43bb, {0x90, 0xb1, 0xd3, 0x73, 0x2e, 0xca, 0xa8, 0x77} }

#define EFI_EXT_SCSI_PASS_THRU_PROTOCOL_GUID \
{ 0x143b7632, 0xb81b, 0x4cb7, {0xab, 0xd3, 0xb6, 0x25, 0xa5, 0xb9, 0xbf, 0xfe} }

#define EFI_DISK_INFO_PROTOCOL_GUID \
{ 0xd432a67f, 0x14dc, 0x484b, {0xb3, 0xbb, 0x3f, 0x2, 0x91, 0x84, 0x93, 0x27} }

#define EFI_ISA_IO_PROTOCOL_GUID \
{ 0x7ee2bd44, 0x3da0, 0x11d4, { 0x9a, 0x38, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_VLAN_CONFIG_PROTOCOL_GUID \
{ 0x9e23d768, 0xd2f3, 0x4366, {0x9f, 0xc3, 0x3a, 0x7a, 0xba, 0x86, 0x43, 0x74} }

#define EFI_IDE_CONTROLLER_INIT_PROTOCOL_GUID \
{ 0xa1e37052, 0x80d9, 0x4e65, {0xa3, 0x17, 0x3e, 0x9a, 0x55, 0xc4, 0x3e, 0xc9} }

#define EFI_ISA_ACPI_PROTOCOL_GUID \
{ 0x64a892dc, 0x5561, 0x4536, {0x92, 0xc7, 0x79, 0x9b, 0xfc, 0x18, 0x33, 0x55} }

#define EFI_PCI_ENUMERATION_COMPLETE_GUID \
{ 0x30cfe3e7, 0x3de1, 0x4586, {0xbe, 0x20, 0xde, 0xab, 0xa1, 0xb3, 0xb7, 0x93} }

#define EFI_DRIVER_DIAGNOSTICS_PROTOCOL_GUID \
{ 0x0784924f, 0xe296, 0x11d4, {0x9a, 0x49, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define EFI_DRIVER_DIAGNOSTICS2_PROTOCOL_GUID \
{ 0x4d330321, 0x025f, 0x4aac, {0x90, 0xd8, 0x5e, 0xd9, 0x00, 0x17, 0x3b, 0x63} }

#define EFI_CAPSULE_ARCH_PROTOCOL_GUID \
{ 0x5053697e, 0x2cbc, 0x4819, {0x90, 0xd9, 0x05, 0x80, 0xde, 0xee, 0x57, 0x54} }

#define EFI_MONOTONIC_COUNTER_ARCH_PROTOCOL_GUID \
{0x1da97072, 0xbddc, 0x4b30, {0x99, 0xf1, 0x72, 0xa0, 0xb5, 0x6f, 0xff, 0x2a} }

#define EFI_REALTIME_CLOCK_ARCH_PROTOCOL_GUID \
{0x27cfac87, 0x46cc, 0x11d4, {0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_MP_SERVICES_PROTOCOL_GUID \
{ 0x3fdda605, 0xa76e, 0x4f46, {0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08} }

#define EFI_VARIABLE_ARCH_PROTOCOL_GUID \
{ 0x1e5668e2, 0x8481, 0x11d4, {0xbc, 0xf1, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_VARIABLE_WRITE_ARCH_PROTOCOL_GUID \
{ 0x6441f818, 0x6362, 0x4e44, {0xb5, 0x70, 0x7d, 0xba, 0x31, 0xdd, 0x24, 0x53} }

#define EFI_WATCHDOG_TIMER_ARCH_PROTOCOL_GUID \
{ 0x6441f818, 0x6362, 0x4e44, {0xb5, 0x70, 0x7d, 0xba, 0x31, 0xdd, 0x24, 0x53} }

#define EFI_ACPI_SUPPORT_PROTOCOL_GUID \
{ 0x6441f818, 0x6362, 0x4e44, {0xb5, 0x70, 0x7d, 0xba, 0x31, 0xdd, 0x24, 0x53} }

#define EFI_BDS_ARCH_PROTOCOL_GUID \
{ 0x665e3ff6, 0x46cc, 0x11d4, {0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_METRONOME_ARCH_PROTOCOL_GUID \
{ 0x26baccb2, 0x6f42, 0x11d4, {0xbc, 0xe7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_TIMER_ARCH_PROTOCOL_GUID \
{ 0x26baccb3, 0x6f42, 0x11d4, {0xbc, 0xe7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_DPC_PROTOCOL_GUID \
{ 0x480f8ae9, 0xc46, 0x4aa9, { 0xbc, 0x89, 0xdb, 0x9f, 0xba, 0x61, 0x98, 0x6} }

#define EFI_PRINT2_PROTOCOL_GUID  \
{ 0xf05976ef, 0x83f1, 0x4f3d, {0x86, 0x19, 0xf7, 0x59, 0x5d, 0x41, 0xe5, 0x38} }

#define EFI_RESET_ARCH_PROTOCOL_GUID  \
{ 0x27cfac88, 0x46cc, 0x11d4, {0x9a, 0x38, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define EFI_CPU_ARCH_PROTOCOL_GUID \
{ 0x26baccb1, 0x6f42, 0x11d4, {0xbc, 0xe7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define EFI_CPU_IO2_PROTOCOL_GUID \
{ 0xad61f191, 0xae5f, 0x4c0e, {0xb9, 0xfa, 0xe8, 0x69, 0xd2, 0x88, 0xc6, 0x4f} }

#define EFI_LEGACY_8259_PROTOCOL_GUID \
{ 0x38321dba, 0x4fe0, 0x4e17, {0x8a, 0xec, 0x41, 0x30, 0x55, 0xea, 0xed, 0xc1} }

#define EFI_SECURITY_ARCH_PROTOCOL_GUID \
{ 0xa46423e3, 0x4617, 0x49f1, {0xb9, 0xff, 0xd1, 0xbf, 0xa9, 0x11, 0x58, 0x39} }

#define EFI_SECURITY2_ARCH_PROTOCOL_GUID \
{ 0x94ab2f58, 0x1438, 0x4ef1, {0x91, 0x52, 0x18, 0x94, 0x1a, 0x3a, 0x0e, 0x68} }

#define EFI_RUNTIME_ARCH_PROTOCOL_GUID \
{ 0xb7dfb4e1, 0x52f, 0x449f, {0x87, 0xbe, 0x98, 0x18, 0xfc, 0x91, 0xb7, 0x33} }

#define EFI_STATUS_CODE_RUNTIME_PROTOCOL_GUID  \
{ 0xd2b2b828, 0x826, 0x48a7, {0xb3, 0xdf, 0x98, 0x3c, 0x0, 0x60, 0x24, 0xf0} }

#define EFI_DATA_HUB_PROTOCOL_GUID \
{ 0xae80d021, 0x618e, 0x11d4, {0xbc, 0xd7, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81} }

#define PCD_PROTOCOL_GUID \
{ 0x11b34006, 0xd85b, 0x4d0a, { 0xa2, 0x90, 0xd5, 0xa5, 0x71, 0x31, 0xe, 0xf7} }

#define EFI_PCD_PROTOCOL_GUID \
{ 0x13a3f0f6, 0x264a, 0x3ef0, {0xf2, 0xe0, 0xde, 0xc5, 0x12, 0x34, 0x2f, 0x34} }

#define EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL_GUID \
{ 0x8f644fa9, 0xe850, 0x4db1, {0x9c, 0xe2, 0xb, 0x44, 0x69, 0x8e, 0x8d, 0xa4 } }

#define EFI_FIRMWARE_VOLUME2_PROTOCOL_GUID \
{ 0x220e73b6, 0x6bdb, 0x4413, { 0x84, 0x5, 0xb9, 0x74, 0xb1, 0x8, 0x61, 0x9a } }

#define EFI_FIRMWARE_VOLUME_DISPATCH_PROTOCOL_GUID \
{ 0x7aa35a69, 0x506c, 0x444f, {0xa7, 0xaf, 0x69, 0x4b, 0xf5, 0x6f, 0x71, 0xc8} }

#define LZMA_COMPRESS_GUID \
{ 0xee4e5898, 0x3914, 0x4259, {0x9d, 0x6e, 0xdc, 0x7b, 0xd7, 0x94, 0x03, 0xcf} }
#endif
