/* $FreeBSD: head/sys/boot/efi/include/efiapi.h 278234 2015-02-05 07:19:30Z rpaulo $ */
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
// EFI platform variables
//

#define EFI_GLOBAL_VARIABLE     \
    { 0x8BE4DF61, 0x93CA, 0x11d2, { 0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C } }

// Variable attributes
#define EFI_VARIABLE_NON_VOLATILE           0x00000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS     0x00000002
#define EFI_VARIABLE_RUNTIME_ACCESS         0x00000004

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
    IN CHAR16                       *VariableName,
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
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IMAGE_UNLOAD) (
    IN EFI_HANDLE                   ImageHandle
    );


// Image handle
#define LOADED_IMAGE_PROTOCOL      \
    { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } }

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
// EFI Runtime Services Table
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

#define MPS_TABLE_GUID    \
  { 0xeb9d2d2f, 0x2d88, 0x11d3,	\
    { 0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define ACPI_TABLE_GUID    \
  { 0xeb9d2d30, 0x2d88, 0x11d3,	\
    { 0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define ACPI_20_TABLE_GUID  \
  { 0x8868e871, 0xe4f1, 0x11d3,	\
    { 0xbc, 0x22, 0x0, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define SMBIOS_TABLE_GUID    \
  { 0xeb9d2d31, 0x2d88, 0x11d3,	\
    { 0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define SMBIOS3_TABLE_GUID	\
  { 0xf2fd1544, 0x9794, 0x4a2c,	\
    { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

#define SAL_SYSTEM_TABLE_GUID    \
  { 0xeb9d2d32, 0x2d88, 0x11d3,	\
    { 0x9a, 0x16, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define FDT_TABLE_GUID    \
  { 0xb1b621d5, 0xf19c, 0x41a5,	\
    { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } }

#define DXE_SERVICES_TABLE_GUID	\
  { 0x5ad34ba, 0x6f02, 0x4214,	\
    { 0x95, 0x2e, 0x4d, 0xa0, 0x39, 0x8e, 0x2b, 0xb9 } }

#define	HOB_LIST_TABLE_GUID	\
  { 0x7739f24c, 0x93d7, 0x11d4,	\
    { 0x9a, 0x3a, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define MEMORY_TYPE_INFORMATION_TABLE_GUID \
  { 0x4c19049f, 0x4137, 0x4dd3,	\
    { 0x9c, 0x10, 0x8b, 0x97, 0xa8, 0x3f, 0xfd, 0xfa } }

#define DEBUG_IMAGE_INFO_TABLE_GUID \
  { 0x49152e77, 0x1ada, 0x4764,	\
    { 0xb7, 0xa2, 0x7a, 0xfe, 0xfe, 0xd9, 0x5e, 0x8b } }

#define EFI_SYSTEM_RESOURCE_TABLE_GUID \
  { 0xb122a263, 0x3661, 0x4f68,	\
    { 0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80 } }

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

//
// EFI System Resource Table
//

typedef struct _EFI_SYSTEM_RESOURCE_TABLE {
  UINT32	FwResourceCount;
  UINT32	FwResourceCountMax;
  UINT64	FwResourceVersion;
  //EFI_SYSTEM_RESOURCE_ENTRY Entries[];
} EFI_SYSTEM_RESOURCE_TABLE;

#define EFI_SYSTEM_RESOURCE_TABLE_FIRMWARE_RESOURCE_VERSION 1

typedef struct _EFI_SYSTEM_RESOURCE_ENTRY {
  EFI_GUID      FwClass;
  UINT32        FwType;
  UINT32        FwVersion;
  UINT32        LowestSupportedFwVersion;
  UINT32        CapsuleFlags;
  UINT32        LastAttemptVersion;
  UINT32        LastAttemptStatus;
} EFI_SYSTEM_RESOURCE_ENTRY;

#endif
