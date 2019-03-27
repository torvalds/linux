/* $FreeBSD$ */
#ifndef _EFI_PROT_H
#define _EFI_PROT_H

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

    efiprot.h

Abstract:

    EFI Protocols



Revision History

--*/

#include <efidef.h>

//
// Device Path protocol
//

#define DEVICE_PATH_PROTOCOL \
    { 0x9576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }


//
// Block IO protocol
//

#define BLOCK_IO_PROTOCOL \
    { 0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }
#define EFI_BLOCK_IO_INTERFACE_REVISION   0x00010000

INTERFACE_DECL(_EFI_BLOCK_IO);

typedef
EFI_STATUS
(EFIAPI *EFI_BLOCK_RESET) (
    IN struct _EFI_BLOCK_IO     *This,
    IN BOOLEAN                  ExtendedVerification
    );

typedef
EFI_STATUS
(EFIAPI *EFI_BLOCK_READ) (
    IN struct _EFI_BLOCK_IO     *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  LBA,
    IN UINTN                    BufferSize,
    OUT VOID                    *Buffer
    );


typedef
EFI_STATUS
(EFIAPI *EFI_BLOCK_WRITE) (
    IN struct _EFI_BLOCK_IO     *This,
    IN UINT32                   MediaId,
    IN EFI_LBA                  LBA,
    IN UINTN                    BufferSize,
    IN VOID                     *Buffer
    );


typedef
EFI_STATUS
(EFIAPI *EFI_BLOCK_FLUSH) (
    IN struct _EFI_BLOCK_IO     *This
    );



typedef struct {
    UINT32              MediaId;
    BOOLEAN             RemovableMedia;
    BOOLEAN             MediaPresent;

    BOOLEAN             LogicalPartition;
    BOOLEAN             ReadOnly;
    BOOLEAN             WriteCaching;

    UINT32              BlockSize;
    UINT32              IoAlign;

    EFI_LBA             LastBlock;
} EFI_BLOCK_IO_MEDIA;

typedef struct _EFI_BLOCK_IO {
    UINT64                  Revision;

    EFI_BLOCK_IO_MEDIA      *Media;

    EFI_BLOCK_RESET         Reset;
    EFI_BLOCK_READ          ReadBlocks;
    EFI_BLOCK_WRITE         WriteBlocks;
    EFI_BLOCK_FLUSH         FlushBlocks;

} EFI_BLOCK_IO;



//
// Disk Block IO protocol
//

#define DISK_IO_PROTOCOL \
    { 0xce345171, 0xba0b, 0x11d2, {0x8e, 0x4f, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }
#define EFI_DISK_IO_INTERFACE_REVISION   0x00010000

INTERFACE_DECL(_EFI_DISK_IO);

typedef
EFI_STATUS
(EFIAPI *EFI_DISK_READ) (
    IN struct _EFI_DISK_IO      *This,
    IN UINT32                   MediaId,
    IN UINT64                   Offset,
    IN UINTN                    BufferSize,
    OUT VOID                    *Buffer
    );


typedef
EFI_STATUS
(EFIAPI *EFI_DISK_WRITE) (
    IN struct _EFI_DISK_IO      *This,
    IN UINT32                   MediaId,
    IN UINT64                   Offset,
    IN UINTN                    BufferSize,
    IN VOID                     *Buffer
    );


typedef struct _EFI_DISK_IO {
    UINT64              Revision;
    EFI_DISK_READ       ReadDisk;
    EFI_DISK_WRITE      WriteDisk;
} EFI_DISK_IO;


//
// Simple file system protocol
//

#define SIMPLE_FILE_SYSTEM_PROTOCOL \
    { 0x964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

INTERFACE_DECL(_EFI_FILE_IO_INTERFACE);
INTERFACE_DECL(_EFI_FILE_HANDLE);

typedef
EFI_STATUS
(EFIAPI *EFI_VOLUME_OPEN) (
    IN struct _EFI_FILE_IO_INTERFACE    *This,
    OUT struct _EFI_FILE_HANDLE         **Root
    );

#define EFI_FILE_IO_INTERFACE_REVISION   0x00010000

typedef struct _EFI_FILE_IO_INTERFACE {
    UINT64                  Revision;
    EFI_VOLUME_OPEN         OpenVolume;
} EFI_FILE_IO_INTERFACE;

//
//
//

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_OPEN) (
    IN struct _EFI_FILE_HANDLE  *File,
    OUT struct _EFI_FILE_HANDLE **NewHandle,
    IN CHAR16                   *FileName,
    IN UINT64                   OpenMode,
    IN UINT64                   Attributes
    );

// Open modes
#define EFI_FILE_MODE_READ      0x0000000000000001
#define EFI_FILE_MODE_WRITE     0x0000000000000002
#define EFI_FILE_MODE_CREATE    0x8000000000000000

// File attributes
#define EFI_FILE_READ_ONLY      0x0000000000000001
#define EFI_FILE_HIDDEN         0x0000000000000002
#define EFI_FILE_SYSTEM         0x0000000000000004
#define EFI_FILE_RESERVIED      0x0000000000000008
#define EFI_FILE_DIRECTORY      0x0000000000000010
#define EFI_FILE_ARCHIVE        0x0000000000000020
#define EFI_FILE_VALID_ATTR     0x0000000000000037

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_CLOSE) (
    IN struct _EFI_FILE_HANDLE  *File
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_DELETE) (
    IN struct _EFI_FILE_HANDLE  *File
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_READ) (
    IN struct _EFI_FILE_HANDLE  *File,
    IN OUT UINTN                *BufferSize,
    OUT VOID                    *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_WRITE) (
    IN struct _EFI_FILE_HANDLE  *File,
    IN OUT UINTN                *BufferSize,
    IN VOID                     *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_POSITION) (
    IN struct _EFI_FILE_HANDLE  *File,
    IN UINT64                   Position
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_POSITION) (
    IN struct _EFI_FILE_HANDLE  *File,
    OUT UINT64                  *Position
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_GET_INFO) (
    IN struct _EFI_FILE_HANDLE  *File,
    IN EFI_GUID                 *InformationType,
    IN OUT UINTN                *BufferSize,
    OUT VOID                    *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_SET_INFO) (
    IN struct _EFI_FILE_HANDLE  *File,
    IN EFI_GUID                 *InformationType,
    IN UINTN                    BufferSize,
    IN VOID                     *Buffer
    );

typedef
EFI_STATUS
(EFIAPI *EFI_FILE_FLUSH) (
    IN struct _EFI_FILE_HANDLE  *File
    );



#define EFI_FILE_HANDLE_REVISION         0x00010000
typedef struct _EFI_FILE_HANDLE {
    UINT64                  Revision;
    EFI_FILE_OPEN           Open;
    EFI_FILE_CLOSE          Close;
    EFI_FILE_DELETE         Delete;
    EFI_FILE_READ           Read;
    EFI_FILE_WRITE          Write;
    EFI_FILE_GET_POSITION   GetPosition;
    EFI_FILE_SET_POSITION   SetPosition;
    EFI_FILE_GET_INFO       GetInfo;
    EFI_FILE_SET_INFO       SetInfo;
    EFI_FILE_FLUSH          Flush;
} EFI_FILE, *EFI_FILE_HANDLE;


//
// File information types
//

#define EFI_FILE_INFO_ID \
    { 0x9576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

typedef struct {
    UINT64                  Size;
    UINT64                  FileSize;
    UINT64                  PhysicalSize;
    EFI_TIME                CreateTime;
    EFI_TIME                LastAccessTime;
    EFI_TIME                ModificationTime;
    UINT64                  Attribute;
    CHAR16                  FileName[1];
} EFI_FILE_INFO;

//
// The FileName field of the EFI_FILE_INFO data structure is variable length.
// Whenever code needs to know the size of the EFI_FILE_INFO data structure, it needs to
// be the size of the data structure without the FileName field.  The following macro 
// computes this size correctly no matter how big the FileName array is declared.
// This is required to make the EFI_FILE_INFO data structure ANSI compilant. 
//

#define SIZE_OF_EFI_FILE_INFO EFI_FIELD_OFFSET(EFI_FILE_INFO,FileName)

#define EFI_FILE_SYSTEM_INFO_ID \
    { 0x9576e93, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

typedef struct {
    UINT64                  Size;
    BOOLEAN                 ReadOnly;
    UINT64                  VolumeSize;
    UINT64                  FreeSpace;
    UINT32                  BlockSize;
    CHAR16                  VolumeLabel[1];
} EFI_FILE_SYSTEM_INFO;

//
// The VolumeLabel field of the EFI_FILE_SYSTEM_INFO data structure is variable length.
// Whenever code needs to know the size of the EFI_FILE_SYSTEM_INFO data structure, it needs
// to be the size of the data structure without the VolumeLable field.  The following macro 
// computes this size correctly no matter how big the VolumeLable array is declared.
// This is required to make the EFI_FILE_SYSTEM_INFO data structure ANSI compilant. 
//

#define SIZE_OF_EFI_FILE_SYSTEM_INFO EFI_FIELD_OFFSET(EFI_FILE_SYSTEM_INFO,VolumeLabel)

#define EFI_FILE_SYSTEM_VOLUME_LABEL_INFO_ID \
    { 0xDB47D7D3,0xFE81, 0x11d3, {0x9A, 0x35, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D} }

typedef struct {
    CHAR16                  VolumeLabel[1];
} EFI_FILE_SYSTEM_VOLUME_LABEL_INFO;

#define SIZE_OF_EFI_FILE_SYSTEM_VOLUME_LABEL_INFO EFI_FIELD_OFFSET(EFI_FILE_SYSTEM_VOLUME_LABEL_INFO,VolumeLabel)

//
// Load file protocol
//


#define LOAD_FILE_PROTOCOL \
    { 0x56EC3091, 0x954C, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B} }

INTERFACE_DECL(_EFI_LOAD_FILE_INTERFACE);

typedef
EFI_STATUS
(EFIAPI *EFI_LOAD_FILE) (
    IN struct _EFI_LOAD_FILE_INTERFACE  *This,
    IN EFI_DEVICE_PATH                  *FilePath,
    IN BOOLEAN                          BootPolicy,
    IN OUT UINTN                        *BufferSize,
    IN VOID                             *Buffer OPTIONAL
    );

typedef struct _EFI_LOAD_FILE_INTERFACE {
    EFI_LOAD_FILE                       LoadFile;
} EFI_LOAD_FILE_INTERFACE;


//
// Device IO protocol
//

#define DEVICE_IO_PROTOCOL \
    { 0xaf6ac311, 0x84c3, 0x11d2, {0x8e, 0x3c, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b} }

INTERFACE_DECL(_EFI_DEVICE_IO_INTERFACE);

typedef enum {
    IO_UINT8,
    IO_UINT16,
    IO_UINT32,
    IO_UINT64,
//
// Specification Change: Copy from MMIO to MMIO vs. MMIO to buffer, buffer to MMIO
//
    MMIO_COPY_UINT8,
    MMIO_COPY_UINT16,
    MMIO_COPY_UINT32,
    MMIO_COPY_UINT64
} EFI_IO_WIDTH;

#define EFI_PCI_ADDRESS(bus,dev,func,reg) \
  ( (UINT64) ( (((UINTN)bus) << 24) + (((UINTN)dev) << 16) + (((UINTN)func) << 8) + ((UINTN)reg) ))

typedef
EFI_STATUS
(EFIAPI *EFI_DEVICE_IO) (
    IN struct _EFI_DEVICE_IO_INTERFACE *This,
    IN EFI_IO_WIDTH                 Width,
    IN UINT64                       Address,
    IN UINTN                        Count,
    IN OUT VOID                     *Buffer
    );

typedef struct {
    EFI_DEVICE_IO                   Read;
    EFI_DEVICE_IO                   Write;
} EFI_IO_ACCESS;

typedef 
EFI_STATUS
(EFIAPI *EFI_PCI_DEVICE_PATH) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This,
    IN UINT64                           Address,
    IN OUT EFI_DEVICE_PATH              **PciDevicePath
    );

typedef enum {
    EfiBusMasterRead,
    EfiBusMasterWrite,
    EfiBusMasterCommonBuffer
} EFI_IO_OPERATION_TYPE;

typedef
EFI_STATUS
(EFIAPI *EFI_IO_MAP) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This,
    IN EFI_IO_OPERATION_TYPE            Operation,
    IN EFI_PHYSICAL_ADDRESS             *HostAddress,
    IN OUT UINTN                        *NumberOfBytes,
    OUT EFI_PHYSICAL_ADDRESS            *DeviceAddress,
    OUT VOID                            **Mapping
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IO_UNMAP) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This,
    IN VOID                             *Mapping
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IO_ALLOCATE_BUFFER) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This,
    IN EFI_ALLOCATE_TYPE                Type,
    IN EFI_MEMORY_TYPE                  MemoryType,
    IN UINTN                            Pages,
    IN OUT EFI_PHYSICAL_ADDRESS         *HostAddress
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IO_FLUSH) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This
    );

typedef
EFI_STATUS
(EFIAPI *EFI_IO_FREE_BUFFER) (
    IN struct _EFI_DEVICE_IO_INTERFACE  *This,
    IN UINTN                            Pages,
    IN EFI_PHYSICAL_ADDRESS             HostAddress
    );

typedef struct _EFI_DEVICE_IO_INTERFACE {
    EFI_IO_ACCESS                       Mem;
    EFI_IO_ACCESS                       Io;
    EFI_IO_ACCESS                       Pci;
    EFI_IO_MAP                          Map;
    EFI_PCI_DEVICE_PATH                 PciDevicePath;
    EFI_IO_UNMAP                        Unmap;
    EFI_IO_ALLOCATE_BUFFER              AllocateBuffer;
    EFI_IO_FLUSH                        Flush;
    EFI_IO_FREE_BUFFER                  FreeBuffer;
} EFI_DEVICE_IO_INTERFACE;


//
// Unicode Collation protocol
//

#define UNICODE_COLLATION_PROTOCOL \
    { 0x1d85cd7f, 0xf43d, 0x11d2, {0x9a, 0xc, 0x0, 0x90, 0x27, 0x3f, 0xc1, 0x4d} }

#define UNICODE_BYTE_ORDER_MARK       (CHAR16)(0xfeff)

INTERFACE_DECL(_EFI_UNICODE_COLLATION_INTERFACE);

typedef
INTN
(EFIAPI *EFI_UNICODE_COLLATION_STRICOLL) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN CHAR16                         *s1,
    IN CHAR16                         *s2
    );

typedef
BOOLEAN
(EFIAPI *EFI_UNICODE_COLLATION_METAIMATCH) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN CHAR16                         *String,
    IN CHAR16                         *Pattern
    );

typedef
VOID
(EFIAPI *EFI_UNICODE_COLLATION_STRLWR) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN OUT CHAR16                       *Str
    );

typedef
VOID
(EFIAPI *EFI_UNICODE_COLLATION_STRUPR) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN OUT CHAR16                       *Str
    );

typedef
VOID
(EFIAPI *EFI_UNICODE_COLLATION_FATTOSTR) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN UINTN                            FatSize,
    IN CHAR8                            *Fat,
    OUT CHAR16                          *String
    );

typedef
BOOLEAN
(EFIAPI *EFI_UNICODE_COLLATION_STRTOFAT) (
    IN struct _EFI_UNICODE_COLLATION_INTERFACE  *This,
    IN CHAR16                           *String,
    IN UINTN                            FatSize,
    OUT CHAR8                           *Fat
    );


typedef struct _EFI_UNICODE_COLLATION_INTERFACE {

    // general
    EFI_UNICODE_COLLATION_STRICOLL                StriColl;
    EFI_UNICODE_COLLATION_METAIMATCH              MetaiMatch;
    EFI_UNICODE_COLLATION_STRLWR                  StrLwr;
    EFI_UNICODE_COLLATION_STRUPR                  StrUpr;

    // for supporting fat volumes
    EFI_UNICODE_COLLATION_FATTOSTR                FatToStr;
    EFI_UNICODE_COLLATION_STRTOFAT                StrToFat;

    CHAR8                               *SupportedLanguages;
} EFI_UNICODE_COLLATION_INTERFACE;

//
// Driver Binding protocol
//

#define DRIVER_BINDING_PROTOCOL \
  { 0x18a031ab, 0xb443, 0x4d1a, {0xa5, 0xc0, 0x0c, 0x09, 0x26, 0x1e, 0x9f, 0x71} }

INTERFACE_DECL(_EFI_DRIVER_BINDING);

typedef
EFI_STATUS
(EFIAPI *EFI_DRIVER_BINDING_SUPPORTED) (
    IN struct _EFI_DRIVER_BINDING *This,
    IN EFI_HANDLE ControllerHandle,
    IN EFI_DEVICE_PATH *RemainingPath
    );

typedef
EFI_STATUS
(EFIAPI *EFI_DRIVER_BINDING_START) (
    IN struct _EFI_DRIVER_BINDING *This,
    IN EFI_HANDLE ControllerHandle,
    IN EFI_DEVICE_PATH *RemainingPath
    );

typedef
EFI_STATUS
(EFIAPI *EFI_DRIVER_BINDING_STOP) (
    IN struct _EFI_DRIVER_BINDING *This,
    IN EFI_HANDLE ControllerHandle,
    IN UINTN NumberOfChildren,
    IN EFI_HANDLE *ChildHandleBuffer
    );

typedef struct _EFI_DRIVER_BINDING {
    EFI_DRIVER_BINDING_SUPPORTED Supported;
    EFI_DRIVER_BINDING_START Start;
    EFI_DRIVER_BINDING_STOP Stop;
    UINT32 Version;
    EFI_HANDLE ImageHandle;
    EFI_HANDLE DriverBindingHandle;
} EFI_DRIVER_BINDING;

//
// Component Name Protocol 2
//

#define COMPONENT_NAME2_PROTOCOL \
  { 0x6a7a5cff, 0xe8d9, 0x4f70, {0xba, 0xda, 0x75, 0xab, 0x30, 0x25, 0xce, 0x14 } }

INTERFACE_DECL(_EFI_COMPONENT_NAME2);

typedef
EFI_STATUS
(EFIAPI *EFI_COMPONENT_NAME_GET_DRIVER_NAME) (
    IN struct _EFI_COMPONENT_NAME2 *This,
    IN CHAR8 * Language,
    OUT CHAR16 **DriverName
    );

typedef
EFI_STATUS
(EFIAPI *EFI_COMPONENT_NAME_GET_CONTROLLER_NAME) (
    IN struct _EFI_COMPONENT_NAME2 *This,
    IN EFI_HANDLE ControllerHandle,
    IN EFI_HANDLE ChildHandle OPTIONAL,
    IN CHAR8 *Language,
    OUT CHAR16 **ControllerName
    );

typedef struct _EFI_COMPONENT_NAME2 {
    EFI_COMPONENT_NAME_GET_DRIVER_NAME GetDriverName;
    EFI_COMPONENT_NAME_GET_CONTROLLER_NAME GetControllerName;
    CHAR8 **SupportedLanguages;
} EFI_COMPONENT_NAME2;

#endif
