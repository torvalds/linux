/* $OpenBSD: efi.h,v 1.4 2023/01/14 12:11:11 kettenis Exp $ */

/* Public Domain */

#ifndef _MACHINE_EFI_H_
#define _MACHINE_EFI_H_

#ifdef __amd64__
#define EFIAPI		__attribute__((ms_abi))
#else
#define EFIAPI
#endif

#ifdef __LP64__
#define EFIERR(x)	(0x8000000000000000 | (x))
#else
#define EFIERR(x)	(0x80000000 | (x))
#endif

typedef uint8_t		UINT8;
typedef int16_t		INT16;
typedef uint16_t	UINT16;
typedef uint32_t	UINT32;
typedef uint64_t	UINT64;
typedef u_long		UINTN;
typedef uint16_t	CHAR16;
typedef void		VOID;
typedef uint64_t	EFI_PHYSICAL_ADDRESS;
typedef uint64_t	EFI_VIRTUAL_ADDRESS;
typedef UINTN		EFI_STATUS;
typedef VOID		*EFI_HANDLE;

typedef VOID		*EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef VOID		*EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef VOID		*EFI_BOOT_SERVICES;

typedef struct {
	UINT32	Data1;
	UINT16	Data2;
	UINT16	Data3;
	UINT8	Data4[8];
} EFI_GUID;

#define EFI_ACPI_20_TABLE_GUID \
  { 0x8868e871, 0xe4f1, 0x11d3, \
    { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 } }

#define SMBIOS_TABLE_GUID \
  { 0xeb9d2d31, 0x2d88, 0x11d3, \
    { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define SMBIOS3_TABLE_GUID \
  { 0xf2fd1544, 0x9794, 0x4a2c, \
    { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

#define EFI_SYSTEM_RESOURCE_TABLE_GUID \
  { 0xb122a263, 0x3661, 0x4f68, \
    { 0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80 } }

#define EFI_GLOBAL_VARIABLE \
  { 0x8be4df61, 0x93ca, 0x11d2, \
    { 0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c } }

typedef enum {
	EfiReservedMemoryType,
	EfiLoaderCode,
	EfiLoaderData,
	EfiBootServicesCode,
	EfiBootServicesData,
	EfiRuntimeServicesCode,
	EfiRuntimeServicesData,
	EfiConventionalMemory,
	EfiUnusableMemory,
	EfiACPIReclaimMemory,
	EfiACPIMemoryNVS,
	EfiMemoryMappedIO,
	EfiMemoryMappedIOPortSpace,
	EfiPalCode,
	EfiPersistentMemory,
        EfiMaxMemoryType
} EFI_MEMORY_TYPE;

#define EFI_MEMORY_UC			0x0000000000000001
#define EFI_MEMORY_WC			0x0000000000000002
#define EFI_MEMORY_WT			0x0000000000000004
#define EFI_MEMORY_WB			0x0000000000000008
#define EFI_MEMORY_UCE			0x0000000000000010
#define EFI_MEMORY_WP			0x0000000000001000
#define EFI_MEMORY_RP			0x0000000000002000
#define EFI_MEMORY_XP			0x0000000000004000
#define EFI_MEMORY_NV			0x0000000000008000
#define EFI_MEMORY_MORE_RELIABLE	0x0000000000010000
#define EFI_MEMORY_RO			0x0000000000020000
#define EFI_MEMORY_RUNTIME		0x8000000000000000

#define EFI_MEMORY_DESCRIPTOR_VERSION  1

typedef struct {
	UINT32			Type;
	UINT32			Pad;
	EFI_PHYSICAL_ADDRESS	PhysicalStart;
	EFI_VIRTUAL_ADDRESS	VirtualStart;
	UINT64			NumberOfPages;
	UINT64			Attribute;
} EFI_MEMORY_DESCRIPTOR;

#define NextMemoryDescriptor(Ptr, Size) \
	((EFI_MEMORY_DESCRIPTOR *)(((UINT8 *)Ptr) + Size))

typedef enum {
	EfiResetCold,
	EfiResetWarm,
	EfiResetShutdown,
	EfiResetPlatformSpecific
} EFI_RESET_TYPE;

typedef struct {
	UINT64				Signature;
	UINT32				Revision;
	UINT32				HeaderSize;
	UINT32				CRC32;
	UINT32				Reserved;
} EFI_TABLE_HEADER;

typedef struct {
	UINT16				Year;
	UINT8				Month;
	UINT8				Day;
	UINT8				Hour;
	UINT8				Minute;
	UINT8				Second;
	UINT8				Pad1;
	UINT32				Nanosecond;
	INT16				TimeZone;
	UINT8				Daylight;
	UINT8				Pad2;
} EFI_TIME;

typedef VOID		*EFI_TIME_CAPABILITIES;

typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(EFI_TIME *, EFI_TIME_CAPABILITIES *);
typedef EFI_STATUS (EFIAPI *EFI_SET_TIME)(EFI_TIME *);
typedef EFI_STATUS (EFIAPI *EFI_SET_VIRTUAL_ADDRESS_MAP)(UINTN, UINTN, UINT32, EFI_MEMORY_DESCRIPTOR *);
typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(CHAR16 *, EFI_GUID *, UINT32 *, UINTN *, VOID *);
typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_VARIABLE_NAME)(UINTN *, CHAR16 *, EFI_GUID *);
typedef EFI_STATUS (EFIAPI *EFI_SET_VARIABLE)(CHAR16 *, EFI_GUID *, UINT32, UINTN, VOID *);
typedef VOID (EFIAPI *EFI_RESET_SYSTEM)(EFI_RESET_TYPE, EFI_STATUS, UINTN, VOID *);

typedef struct {
	EFI_TABLE_HEADER		Hdr;
	EFI_GET_TIME			GetTime;
	EFI_SET_TIME			SetTime;
	VOID				*GetWakeupTime;
	VOID				*SetWakeupTime;

	EFI_SET_VIRTUAL_ADDRESS_MAP	SetVirtualAddressMap;
	VOID				*ConvertPointer;

	EFI_GET_VARIABLE		GetVariable;
	EFI_GET_NEXT_VARIABLE_NAME	GetNextVariableName;
	EFI_SET_VARIABLE		SetVariable;

	VOID				*GetNextHighMonotonicCount;
	EFI_RESET_SYSTEM		ResetSystem;
} EFI_RUNTIME_SERVICES;

typedef struct {
	EFI_GUID			VendorGuid;
	VOID				*VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
	EFI_TABLE_HEADER		Hdr;
	CHAR16				*FirmwareVendor;
	UINT32				FirmwareRevision;
	EFI_HANDLE			ConsoleInHandle;
	EFI_SIMPLE_TEXT_INPUT_PROTOCOL	*ConIn;
	EFI_HANDLE			ConsoleOutHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL	*ConOut;
	EFI_HANDLE			StandardErrorHandle;
	EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL	*StdErr;
	EFI_RUNTIME_SERVICES		*RuntimeServices;
	EFI_BOOT_SERVICES		*BootServices;
	UINTN				NumberOfTableEntries;
	EFI_CONFIGURATION_TABLE		*ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
	EFI_GUID			FwClass;
	UINT32				FwType;
	UINT32				FwVersion;
	UINT32				LowestSupportedFwVersion;
	UINT32				CapsuleFlags;
	UINT32				LastAttemptVersion;
	UINT32				LastAttemptStatus;
} EFI_SYSTEM_RESOURCE_ENTRY;

typedef struct {
	UINT32				FwResourceCount;
	UINT32				FwResourceCountMax;
	UINT64				FwResourceVersion;
	EFI_SYSTEM_RESOURCE_ENTRY	Entries[];
} EFI_SYSTEM_RESOURCE_TABLE;

#define EFI_SUCCESS	0

#define EFI_INVALID_PARAMETER	EFIERR(2)
#define EFI_UNSUPPORTED		EFIERR(3)
#define EFI_BUFFER_TOO_SMALL	EFIERR(5)
#define EFI_DEVICE_ERROR	EFIERR(7)
#define EFI_WRITE_PROTECTED	EFIERR(8)
#define EFI_OUT_OF_RESOURCES	EFIERR(9)
#define EFI_NOT_FOUND		EFIERR(14)
#define EFI_SECURITY_VIOLATION	EFIERR(26)

#define	efi_guidcmp(_a, _b)	memcmp((_a), (_b), sizeof(EFI_GUID))

#endif /* _DEV_ACPI_EFI_H_ */
