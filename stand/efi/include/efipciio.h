/* $FreeBSD$ */
/** @file
  EFI PCI I/O Protocol provides the basic Memory, I/O, PCI configuration, 
  and DMA interfaces that a driver uses to access its PCI controller.

  Copyright (c) 2006 - 2010, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials                          
  are licensed and made available under the terms and conditions of the BSD License         
  which accompanies this distribution.  The full text of the license may be found at        
  http://opensource.org/licenses/bsd-license.php                                            

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             

**/

#ifndef __PCI_IO_H__
#define __PCI_IO_H__

#define EFI_PCI_ROOT_IO_GUID \
  { 0x2F707EBB, 0x4A1A, 0x11d4, { 0x9A, 0x38, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D }}

///
/// Global ID for the PCI I/O Protocol
///
#define EFI_PCI_IO_PROTOCOL_GUID \
    { 0x4cf5b200, 0x68b8, 0x4ca5, {0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x2, 0x9a} }

typedef struct _EFI_PCI_IO_PROTOCOL  EFI_PCI_IO_PROTOCOL;

///
/// *******************************************************
/// EFI_PCI_IO_PROTOCOL_WIDTH
/// *******************************************************
///
typedef enum {
  EfiPciIoWidthUint8      = 0,
  EfiPciIoWidthUint16,
  EfiPciIoWidthUint32,
  EfiPciIoWidthUint64,
  EfiPciIoWidthFifoUint8,
  EfiPciIoWidthFifoUint16,
  EfiPciIoWidthFifoUint32,
  EfiPciIoWidthFifoUint64,
  EfiPciIoWidthFillUint8,
  EfiPciIoWidthFillUint16,
  EfiPciIoWidthFillUint32,
  EfiPciIoWidthFillUint64,
  EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

//
// Complete PCI address generater
//
#define EFI_PCI_IO_PASS_THROUGH_BAR               0xff    ///< Special BAR that passes a memory or I/O cycle through unchanged
#define EFI_PCI_IO_ATTRIBUTE_MASK                 0x077f  ///< All the following I/O and Memory cycles
#define EFI_PCI_IO_ATTRIBUTE_ISA_MOTHERBOARD_IO   0x0001  ///< I/O cycles 0x0000-0x00FF (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO               0x0002  ///< I/O cycles 0x0100-0x03FF or greater (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO       0x0004  ///< I/O cycles 0x3C6, 0x3C8, 0x3C9 (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY           0x0008  ///< MEM cycles 0xA0000-0xBFFFF (24 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO               0x0010  ///< I/O cycles 0x3B0-0x3BB and 0x3C0-0x3DF (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO       0x0020  ///< I/O cycles 0x1F0-0x1F7, 0x3F6, 0x3F7 (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO     0x0040  ///< I/O cycles 0x170-0x177, 0x376, 0x377 (10 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE 0x0080  ///< Map a memory range so writes are combined
#define EFI_PCI_IO_ATTRIBUTE_IO                   0x0100  ///< Enable the I/O decode bit in the PCI Config Header
#define EFI_PCI_IO_ATTRIBUTE_MEMORY               0x0200  ///< Enable the Memory decode bit in the PCI Config Header
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER           0x0400  ///< Enable the DMA bit in the PCI Config Header
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_CACHED        0x0800  ///< Map a memory range so all r/w accesses are cached
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_DISABLE       0x1000  ///< Disable a memory range
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_DEVICE      0x2000  ///< Clear for an add-in PCI Device
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_ROM         0x4000  ///< Clear for a physical PCI Option ROM accessed through ROM BAR
#define EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE   0x8000  ///< Clear for PCI controllers that can not genrate a DAC
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO_16            0x10000 ///< I/O cycles 0x0100-0x03FF or greater (16 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO_16    0x20000 ///< I/O cycles 0x3C6, 0x3C8, 0x3C9 (16 bit decode)
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO_16            0x40000 ///< I/O cycles 0x3B0-0x3BB and 0x3C0-0x3DF (16 bit decode)

#define EFI_PCI_DEVICE_ENABLE                     (EFI_PCI_IO_ATTRIBUTE_IO | EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER)
#define EFI_VGA_DEVICE_ENABLE                     (EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO | EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY | EFI_PCI_IO_ATTRIBUTE_VGA_IO | EFI_PCI_IO_ATTRIBUTE_IO)

///
/// *******************************************************
/// EFI_PCI_IO_PROTOCOL_OPERATION
/// *******************************************************
///
typedef enum {
  ///
  /// A read operation from system memory by a bus master.
  ///
  EfiPciIoOperationBusMasterRead,
  ///
  /// A write operation from system memory by a bus master.
  ///
  EfiPciIoOperationBusMasterWrite,
  ///
  /// Provides both read and write access to system memory by both the processor and a
  /// bus master. The buffer is coherent from both the processor's and the bus master's point of view.
  ///
  EfiPciIoOperationBusMasterCommonBuffer,
  EfiPciIoOperationMaximum
} EFI_PCI_IO_PROTOCOL_OPERATION;

///
/// *******************************************************
/// EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION
/// *******************************************************
///
typedef enum {
  ///
  /// Retrieve the PCI controller's current attributes, and return them in Result.
  ///
  EfiPciIoAttributeOperationGet,
  ///
  /// Set the PCI controller's current attributes to Attributes.
  ///
  EfiPciIoAttributeOperationSet,
  ///
  /// Enable the attributes specified by the bits that are set in Attributes for this PCI controller.
  ///
  EfiPciIoAttributeOperationEnable,
  ///
  /// Disable the attributes specified by the bits that are set in Attributes for this PCI controller.
  ///
  EfiPciIoAttributeOperationDisable,
  ///
  /// Retrieve the PCI controller's supported attributes, and return them in Result.
  ///
  EfiPciIoAttributeOperationSupported,
  EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

/**                                                                 
  Reads from the memory space of a PCI controller. Returns either when the polling exit criteria is
  satisfied or after a defined duration.                                                           
          
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.
  @param  Width                 Signifies the width of the memory or I/O operations.
  @param  BarIndex              The BAR index of the standard PCI Configuration header to use as the
                                base address for the memory operation to perform.                   
  @param  Offset                The offset within the selected BAR to start the memory operation.
  @param  Mask                  Mask used for the polling criteria.
  @param  Value                 The comparison value used for the polling exit criteria.
  @param  Delay                 The number of 100 ns units to poll.
  @param  Result                Pointer to the last value read from the memory location.
                                
  @retval EFI_SUCCESS           The last data returned from the access matched the poll exit criteria.
  @retval EFI_UNSUPPORTED       BarIndex not valid for this PCI controller.
  @retval EFI_UNSUPPORTED       Offset is not valid for the BarIndex of this PCI controller.
  @retval EFI_TIMEOUT           Delay expired before a match occurred.
  @retval EFI_OUT_OF_RESOURCES  The request could not be completed due to a lack of resources.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_POLL_IO_MEM)(
  IN EFI_PCI_IO_PROTOCOL           *This,
  IN  EFI_PCI_IO_PROTOCOL_WIDTH    Width,
  IN  UINT8                        BarIndex,
  IN  UINT64                       Offset,
  IN  UINT64                       Mask,
  IN  UINT64                       Value,
  IN  UINT64                       Delay,
  OUT UINT64                       *Result
  );

/**                                                                 
  Enable a PCI driver to access PCI controller registers in the PCI memory or I/O space.
          
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.
  @param  Width                 Signifies the width of the memory or I/O operations.
  @param  BarIndex              The BAR index of the standard PCI Configuration header to use as the
                                base address for the memory or I/O operation to perform.                    
  @param  Offset                The offset within the selected BAR to start the memory or I/O operation.                                
  @param  Count                 The number of memory or I/O operations to perform.
  @param  Buffer                For read operations, the destination buffer to store the results. For write
                                operations, the source buffer to write data from.                          
  
  @retval EFI_SUCCESS           The data was read from or written to the PCI controller.
  @retval EFI_UNSUPPORTED       BarIndex not valid for this PCI controller.
  @retval EFI_UNSUPPORTED       The address range specified by Offset, Width, and Count is not
                                valid for the PCI BAR specified by BarIndex.                  
  @retval EFI_OUT_OF_RESOURCES  The request could not be completed due to a lack of resources.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_IO_MEM)(
  IN EFI_PCI_IO_PROTOCOL              *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH    Width,
  IN     UINT8                        BarIndex,
  IN     UINT64                       Offset,
  IN     UINTN                        Count,
  IN OUT VOID                         *Buffer
  );

typedef struct {
  ///
  /// Read PCI controller registers in the PCI memory or I/O space.
  ///
  EFI_PCI_IO_PROTOCOL_IO_MEM  Read;
  ///
  /// Write PCI controller registers in the PCI memory or I/O space.
  ///
  EFI_PCI_IO_PROTOCOL_IO_MEM  Write;
} EFI_PCI_IO_PROTOCOL_ACCESS;

/**                                                                 
  Enable a PCI driver to access PCI controller registers in PCI configuration space.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  Width                 Signifies the width of the memory operations.
  @param  Offset                The offset within the PCI configuration space for the PCI controller.
  @param  Count                 The number of PCI configuration operations to perform.
  @param  Buffer                For read operations, the destination buffer to store the results. For write
                                operations, the source buffer to write data from.
  
                                  
  @retval EFI_SUCCESS           The data was read from or written to the PCI controller.
  @retval EFI_UNSUPPORTED       The address range specified by Offset, Width, and Count is not
                                valid for the PCI configuration header of the PCI controller.
  @retval EFI_OUT_OF_RESOURCES  The request could not be completed due to a lack of resources.                                 
  @retval EFI_INVALID_PARAMETER Buffer is NULL or Width is invalid.                                
                                     
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG)(
  IN EFI_PCI_IO_PROTOCOL              *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH    Width,
  IN     UINT32                       Offset,
  IN     UINTN                        Count,
  IN OUT VOID                         *Buffer
  );

typedef struct {
  ///
  /// Read PCI controller registers in PCI configuration space.
  ///
  EFI_PCI_IO_PROTOCOL_CONFIG  Read;
  ///
  /// Write PCI controller registers in PCI configuration space.
  ///
  EFI_PCI_IO_PROTOCOL_CONFIG  Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

/**                                                                 
  Enables a PCI driver to copy one region of PCI memory space to another region of PCI
  memory space.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.
  @param  Width                 Signifies the width of the memory operations.
  @param  DestBarIndex          The BAR index in the standard PCI Configuration header to use as the
                                base address for the memory operation to perform.                   
  @param  DestOffset            The destination offset within the BAR specified by DestBarIndex to
                                start the memory writes for the copy operation.                   
  @param  SrcBarIndex           The BAR index in the standard PCI Configuration header to use as the
                                base address for the memory operation to perform.                   
  @param  SrcOffset             The source offset within the BAR specified by SrcBarIndex to start
                                the memory reads for the copy operation.                          
  @param  Count                 The number of memory operations to perform. Bytes moved is Width
                                size * Count, starting at DestOffset and SrcOffset.             
                                
  @retval EFI_SUCCESS           The data was copied from one memory region to another memory region.
  @retval EFI_UNSUPPORTED       DestBarIndex not valid for this PCI controller.
  @retval EFI_UNSUPPORTED       SrcBarIndex not valid for this PCI controller.
  @retval EFI_UNSUPPORTED       The address range specified by DestOffset, Width, and Count
                                is not valid for the PCI BAR specified by DestBarIndex.    
  @retval EFI_UNSUPPORTED       The address range specified by SrcOffset, Width, and Count is
                                not valid for the PCI BAR specified by SrcBarIndex.          
  @retval EFI_INVALID_PARAMETER Width is invalid.
  @retval EFI_OUT_OF_RESOURCES  The request could not be completed due to a lack of resources.
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_COPY_MEM)(
  IN EFI_PCI_IO_PROTOCOL              *This,
  IN     EFI_PCI_IO_PROTOCOL_WIDTH    Width,
  IN     UINT8                        DestBarIndex,
  IN     UINT64                       DestOffset,
  IN     UINT8                        SrcBarIndex,
  IN     UINT64                       SrcOffset,
  IN     UINTN                        Count
  );

/**                                                                 
  Provides the PCI controller-specific addresses needed to access system memory.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.
  @param  Operation             Indicates if the bus master is going to read or write to system memory.
  @param  HostAddress           The system memory address to map to the PCI controller.
  @param  NumberOfBytes         On input the number of bytes to map. On output the number of bytes
                                that were mapped.                                                 
  @param  DeviceAddress         The resulting map address for the bus master PCI controller to use to
                                access the hosts HostAddress.                                        
  @param  Mapping               A resulting value to pass to Unmap().
                                  
  @retval EFI_SUCCESS           The range was mapped for the returned NumberOfBytes.
  @retval EFI_UNSUPPORTED       The HostAddress cannot be mapped as a common buffer.                                
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  The request could not be completed due to a lack of resources.
  @retval EFI_DEVICE_ERROR      The system hardware could not map the requested address.
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_MAP)(
  IN EFI_PCI_IO_PROTOCOL                *This,
  IN     EFI_PCI_IO_PROTOCOL_OPERATION  Operation,
  IN     VOID                           *HostAddress,
  IN OUT UINTN                          *NumberOfBytes,
  OUT    EFI_PHYSICAL_ADDRESS           *DeviceAddress,
  OUT    VOID                           **Mapping
  );

/**                                                                 
  Completes the Map() operation and releases any corresponding resources.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.                                      
  @param  Mapping               The mapping value returned from Map().
                                  
  @retval EFI_SUCCESS           The range was unmapped.
  @retval EFI_DEVICE_ERROR      The data was not committed to the target system memory.
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_UNMAP)(
  IN EFI_PCI_IO_PROTOCOL           *This,
  IN  VOID                         *Mapping
  );

/**                                                                 
  Allocates pages that are suitable for an EfiPciIoOperationBusMasterCommonBuffer
  mapping.                                                                       
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.
  @param  Type                  This parameter is not used and must be ignored.
  @param  MemoryType            The type of memory to allocate, EfiBootServicesData or
                                EfiRuntimeServicesData.                               
  @param  Pages                 The number of pages to allocate.                                
  @param  HostAddress           A pointer to store the base system memory address of the
                                allocated range.                                        
  @param  Attributes            The requested bit mask of attributes for the allocated range.
                                  
  @retval EFI_SUCCESS           The requested memory pages were allocated.
  @retval EFI_UNSUPPORTED       Attributes is unsupported. The only legal attribute bits are
                                MEMORY_WRITE_COMBINE and MEMORY_CACHED.                     
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.
  @retval EFI_OUT_OF_RESOURCES  The memory pages could not be allocated.  
                                   
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER)(
  IN EFI_PCI_IO_PROTOCOL           *This,
  IN  EFI_ALLOCATE_TYPE            Type,
  IN  EFI_MEMORY_TYPE              MemoryType,
  IN  UINTN                        Pages,
  OUT VOID                         **HostAddress,
  IN  UINT64                       Attributes
  );

/**                                                                 
  Frees memory that was allocated with AllocateBuffer().
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  Pages                 The number of pages to free.                                
  @param  HostAddress           The base system memory address of the allocated range.                                    
                                  
  @retval EFI_SUCCESS           The requested memory pages were freed.
  @retval EFI_INVALID_PARAMETER The memory range specified by HostAddress and Pages
                                was not allocated with AllocateBuffer().
                                     
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FREE_BUFFER)(
  IN EFI_PCI_IO_PROTOCOL           *This,
  IN  UINTN                        Pages,
  IN  VOID                         *HostAddress
  );

/**                                                                 
  Flushes all PCI posted write transactions from a PCI host bridge to system memory.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
                                  
  @retval EFI_SUCCESS           The PCI posted write transactions were flushed from the PCI host
                                bridge to system memory.                                        
  @retval EFI_DEVICE_ERROR      The PCI posted write transactions were not flushed from the PCI
                                host bridge due to a hardware error.                           
                                     
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_FLUSH)(
  IN EFI_PCI_IO_PROTOCOL  *This
  );

/**                                                                 
  Retrieves this PCI controller's current PCI bus number, device number, and function number.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  SegmentNumber         The PCI controller's current PCI segment number.
  @param  BusNumber             The PCI controller's current PCI bus number.
  @param  DeviceNumber          The PCI controller's current PCI device number.
  @param  FunctionNumber        The PCI controller's current PCI function number.
                                  
  @retval EFI_SUCCESS           The PCI controller location was returned.                                                       
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.                              
                                     
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_LOCATION)(
  IN EFI_PCI_IO_PROTOCOL          *This,
  OUT UINTN                       *SegmentNumber,
  OUT UINTN                       *BusNumber,
  OUT UINTN                       *DeviceNumber,
  OUT UINTN                       *FunctionNumber
  );

/**                                                                 
  Performs an operation on the attributes that this PCI controller supports. The operations include
  getting the set of supported attributes, retrieving the current attributes, setting the current  
  attributes, enabling attributes, and disabling attributes.                                       
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  Operation             The operation to perform on the attributes for this PCI controller.
  @param  Attributes            The mask of attributes that are used for Set, Enable, and Disable
                                operations.                                                      
  @param  Result                A pointer to the result mask of attributes that are returned for the Get
                                and Supported operations.                                               
                                  
  @retval EFI_SUCCESS           The operation on the PCI controller's attributes was completed.
  @retval EFI_INVALID_PARAMETER One or more parameters are invalid.                              
  @retval EFI_UNSUPPORTED       one or more of the bits set in                               
                                Attributes are not supported by this PCI controller or one of
                                its parent bridges when Operation is Set, Enable or Disable.
                                       
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_ATTRIBUTES)(
  IN EFI_PCI_IO_PROTOCOL                       *This,
  IN  EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION  Operation,
  IN  UINT64                                   Attributes,
  OUT UINT64                                   *Result OPTIONAL
  );

/**                                                                 
  Gets the attributes that this PCI controller supports setting on a BAR using
  SetBarAttributes(), and retrieves the list of resource descriptors for a BAR.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  BarIndex              The BAR index of the standard PCI Configuration header to use as the
                                base address for resource range. The legal range for this field is 0..5.
  @param  Supports              A pointer to the mask of attributes that this PCI controller supports
                                setting for this BAR with SetBarAttributes().                        
  @param  Resources             A pointer to the ACPI 2.0 resource descriptors that describe the current
                                configuration of this BAR of the PCI controller.                        
                                  
  @retval EFI_SUCCESS           If Supports is not NULL, then the attributes that the PCI       
                                controller supports are returned in Supports. If Resources      
                                is not NULL, then the ACPI 2.0 resource descriptors that the PCI
                                controller is currently using are returned in Resources.          
  @retval EFI_INVALID_PARAMETER Both Supports and Attributes are NULL.
  @retval EFI_UNSUPPORTED       BarIndex not valid for this PCI controller.
  @retval EFI_OUT_OF_RESOURCES  There are not enough resources available to allocate
                                Resources.                                                 
                                
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES)(
  IN EFI_PCI_IO_PROTOCOL             *This,
  IN  UINT8                          BarIndex,
  OUT UINT64                         *Supports, OPTIONAL
  OUT VOID                           **Resources OPTIONAL
  );

/**                                                                 
  Sets the attributes for a range of a BAR on a PCI controller.
            
  @param  This                  A pointer to the EFI_PCI_IO_PROTOCOL instance.  
  @param  Attributes            The mask of attributes to set for the resource range specified by
                                BarIndex, Offset, and Length.                                    
  @param  BarIndex              The BAR index of the standard PCI Configuration header to use as the
                                base address for resource range. The legal range for this field is 0..5.
  @param  Offset                A pointer to the BAR relative base address of the resource range to be
                                modified by the attributes specified by Attributes.                   
  @param  Length                A pointer to the length of the resource range to be modified by the
                                attributes specified by Attributes.                                
                                  
  @retval EFI_SUCCESS           The set of attributes specified by Attributes for the resource      
                                range specified by BarIndex, Offset, and Length were                
                                set on the PCI controller, and the actual resource range is returned
                                in Offset and Length.                                               
  @retval EFI_INVALID_PARAMETER Offset or Length is NULL.
  @retval EFI_UNSUPPORTED       BarIndex not valid for this PCI controller.
  @retval EFI_OUT_OF_RESOURCES  There are not enough resources to set the attributes on the
                                resource range specified by BarIndex, Offset, and          
                                Length.                                                    
                                
**/
typedef
EFI_STATUS
(EFIAPI *EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES)(
  IN EFI_PCI_IO_PROTOCOL              *This,
  IN     UINT64                       Attributes,
  IN     UINT8                        BarIndex,
  IN OUT UINT64                       *Offset,
  IN OUT UINT64                       *Length
  );

///
/// The EFI_PCI_IO_PROTOCOL provides the basic Memory, I/O, PCI configuration, 
/// and DMA interfaces used to abstract accesses to PCI controllers. 
/// There is one EFI_PCI_IO_PROTOCOL instance for each PCI controller on a PCI bus. 
/// A device driver that wishes to manage a PCI controller in a system will have to 
/// retrieve the EFI_PCI_IO_PROTOCOL instance that is associated with the PCI controller. 
///
struct _EFI_PCI_IO_PROTOCOL {
  EFI_PCI_IO_PROTOCOL_POLL_IO_MEM         PollMem;
  EFI_PCI_IO_PROTOCOL_POLL_IO_MEM         PollIo;
  EFI_PCI_IO_PROTOCOL_ACCESS              Mem;
  EFI_PCI_IO_PROTOCOL_ACCESS              Io;
  EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS       Pci;
  EFI_PCI_IO_PROTOCOL_COPY_MEM            CopyMem;
  EFI_PCI_IO_PROTOCOL_MAP                 Map;
  EFI_PCI_IO_PROTOCOL_UNMAP               Unmap;
  EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER     AllocateBuffer;
  EFI_PCI_IO_PROTOCOL_FREE_BUFFER         FreeBuffer;
  EFI_PCI_IO_PROTOCOL_FLUSH               Flush;
  EFI_PCI_IO_PROTOCOL_GET_LOCATION        GetLocation;
  EFI_PCI_IO_PROTOCOL_ATTRIBUTES          Attributes;
  EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES  GetBarAttributes;
  EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES  SetBarAttributes;
  
  ///
  /// The size, in bytes, of the ROM image.
  ///
  UINT64                                  RomSize;

  ///
  /// A pointer to the in memory copy of the ROM image. The PCI Bus Driver is responsible 
  /// for allocating memory for the ROM image, and copying the contents of the ROM to memory. 
  /// The contents of this buffer are either from the PCI option ROM that can be accessed 
  /// through the ROM BAR of the PCI controller, or it is from a platform-specific location. 
  /// The Attributes() function can be used to determine from which of these two sources 
  /// the RomImage buffer was initialized.
  /// 
  VOID                                    *RomImage;
};

extern EFI_GUID gEfiPciIoProtocolGuid;

#endif
