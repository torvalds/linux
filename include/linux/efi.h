/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_EFI_H
#define _LINUX_EFI_H

/*
 * Extensible Firmware Interface
 * Based on 'Extensible Firmware Interface Specification' version 0.9, April 30, 1999
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999, 2002-2003 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 */
#include <linux/init.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/rtc.h>
#include <linux/ioport.h>
#include <linux/pfn.h>
#include <linux/pstore.h>
#include <linux/range.h>
#include <linux/reboot.h>
#include <linux/uuid.h>
#include <linux/screen_info.h>

#include <asm/page.h>

#define EFI_SUCCESS		0
#define EFI_LOAD_ERROR          ( 1 | (1UL << (BITS_PER_LONG-1)))
#define EFI_INVALID_PARAMETER	( 2 | (1UL << (BITS_PER_LONG-1)))
#define EFI_UNSUPPORTED		( 3 | (1UL << (BITS_PER_LONG-1)))
#define EFI_BAD_BUFFER_SIZE     ( 4 | (1UL << (BITS_PER_LONG-1)))
#define EFI_BUFFER_TOO_SMALL	( 5 | (1UL << (BITS_PER_LONG-1)))
#define EFI_NOT_READY		( 6 | (1UL << (BITS_PER_LONG-1)))
#define EFI_DEVICE_ERROR	( 7 | (1UL << (BITS_PER_LONG-1)))
#define EFI_WRITE_PROTECTED	( 8 | (1UL << (BITS_PER_LONG-1)))
#define EFI_OUT_OF_RESOURCES	( 9 | (1UL << (BITS_PER_LONG-1)))
#define EFI_NOT_FOUND		(14 | (1UL << (BITS_PER_LONG-1)))
#define EFI_ABORTED		(21 | (1UL << (BITS_PER_LONG-1)))
#define EFI_SECURITY_VIOLATION	(26 | (1UL << (BITS_PER_LONG-1)))

typedef unsigned long efi_status_t;
typedef u8 efi_bool_t;
typedef u16 efi_char16_t;		/* UNICODE character */
typedef u64 efi_physical_addr_t;
typedef void *efi_handle_t;

typedef guid_t efi_guid_t;

#define EFI_GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
	GUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7)

/*
 * Generic EFI table header
 */
typedef	struct {
	u64 signature;
	u32 revision;
	u32 headersize;
	u32 crc32;
	u32 reserved;
} efi_table_hdr_t;

/*
 * Memory map descriptor:
 */

/* Memory types: */
#define EFI_RESERVED_TYPE		 0
#define EFI_LOADER_CODE			 1
#define EFI_LOADER_DATA			 2
#define EFI_BOOT_SERVICES_CODE		 3
#define EFI_BOOT_SERVICES_DATA		 4
#define EFI_RUNTIME_SERVICES_CODE	 5
#define EFI_RUNTIME_SERVICES_DATA	 6
#define EFI_CONVENTIONAL_MEMORY		 7
#define EFI_UNUSABLE_MEMORY		 8
#define EFI_ACPI_RECLAIM_MEMORY		 9
#define EFI_ACPI_MEMORY_NVS		10
#define EFI_MEMORY_MAPPED_IO		11
#define EFI_MEMORY_MAPPED_IO_PORT_SPACE	12
#define EFI_PAL_CODE			13
#define EFI_PERSISTENT_MEMORY		14
#define EFI_MAX_MEMORY_TYPE		15

/* Attribute values: */
#define EFI_MEMORY_UC		((u64)0x0000000000000001ULL)	/* uncached */
#define EFI_MEMORY_WC		((u64)0x0000000000000002ULL)	/* write-coalescing */
#define EFI_MEMORY_WT		((u64)0x0000000000000004ULL)	/* write-through */
#define EFI_MEMORY_WB		((u64)0x0000000000000008ULL)	/* write-back */
#define EFI_MEMORY_UCE		((u64)0x0000000000000010ULL)	/* uncached, exported */
#define EFI_MEMORY_WP		((u64)0x0000000000001000ULL)	/* write-protect */
#define EFI_MEMORY_RP		((u64)0x0000000000002000ULL)	/* read-protect */
#define EFI_MEMORY_XP		((u64)0x0000000000004000ULL)	/* execute-protect */
#define EFI_MEMORY_NV		((u64)0x0000000000008000ULL)	/* non-volatile */
#define EFI_MEMORY_MORE_RELIABLE \
				((u64)0x0000000000010000ULL)	/* higher reliability */
#define EFI_MEMORY_RO		((u64)0x0000000000020000ULL)	/* read-only */
#define EFI_MEMORY_RUNTIME	((u64)0x8000000000000000ULL)	/* range requires runtime mapping */
#define EFI_MEMORY_DESCRIPTOR_VERSION	1

#define EFI_PAGE_SHIFT		12
#define EFI_PAGE_SIZE		(1UL << EFI_PAGE_SHIFT)
#define EFI_PAGES_MAX		(U64_MAX >> EFI_PAGE_SHIFT)

typedef struct {
	u32 type;
	u32 pad;
	u64 phys_addr;
	u64 virt_addr;
	u64 num_pages;
	u64 attribute;
} efi_memory_desc_t;

typedef struct {
	efi_guid_t guid;
	u32 headersize;
	u32 flags;
	u32 imagesize;
} efi_capsule_header_t;

struct efi_boot_memmap {
	efi_memory_desc_t	**map;
	unsigned long		*map_size;
	unsigned long		*desc_size;
	u32			*desc_ver;
	unsigned long		*key_ptr;
	unsigned long		*buff_size;
};

/*
 * EFI capsule flags
 */
#define EFI_CAPSULE_PERSIST_ACROSS_RESET	0x00010000
#define EFI_CAPSULE_POPULATE_SYSTEM_TABLE	0x00020000
#define EFI_CAPSULE_INITIATE_RESET		0x00040000

struct capsule_info {
	efi_capsule_header_t	header;
	efi_capsule_header_t	*capsule;
	int			reset_type;
	long			index;
	size_t			count;
	size_t			total_size;
	struct page		**pages;
	phys_addr_t		*phys;
	size_t			page_bytes_remain;
};

int __efi_capsule_setup_info(struct capsule_info *cap_info);

/*
 * Allocation types for calls to boottime->allocate_pages.
 */
#define EFI_ALLOCATE_ANY_PAGES		0
#define EFI_ALLOCATE_MAX_ADDRESS	1
#define EFI_ALLOCATE_ADDRESS		2
#define EFI_MAX_ALLOCATE_TYPE		3

typedef int (*efi_freemem_callback_t) (u64 start, u64 end, void *arg);

/*
 * Types and defines for Time Services
 */
#define EFI_TIME_ADJUST_DAYLIGHT 0x1
#define EFI_TIME_IN_DAYLIGHT     0x2
#define EFI_UNSPECIFIED_TIMEZONE 0x07ff

typedef struct {
	u16 year;
	u8 month;
	u8 day;
	u8 hour;
	u8 minute;
	u8 second;
	u8 pad1;
	u32 nanosecond;
	s16 timezone;
	u8 daylight;
	u8 pad2;
} efi_time_t;

typedef struct {
	u32 resolution;
	u32 accuracy;
	u8 sets_to_zero;
} efi_time_cap_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 raise_tpl;
	u32 restore_tpl;
	u32 allocate_pages;
	u32 free_pages;
	u32 get_memory_map;
	u32 allocate_pool;
	u32 free_pool;
	u32 create_event;
	u32 set_timer;
	u32 wait_for_event;
	u32 signal_event;
	u32 close_event;
	u32 check_event;
	u32 install_protocol_interface;
	u32 reinstall_protocol_interface;
	u32 uninstall_protocol_interface;
	u32 handle_protocol;
	u32 __reserved;
	u32 register_protocol_notify;
	u32 locate_handle;
	u32 locate_device_path;
	u32 install_configuration_table;
	u32 load_image;
	u32 start_image;
	u32 exit;
	u32 unload_image;
	u32 exit_boot_services;
	u32 get_next_monotonic_count;
	u32 stall;
	u32 set_watchdog_timer;
	u32 connect_controller;
	u32 disconnect_controller;
	u32 open_protocol;
	u32 close_protocol;
	u32 open_protocol_information;
	u32 protocols_per_handle;
	u32 locate_handle_buffer;
	u32 locate_protocol;
	u32 install_multiple_protocol_interfaces;
	u32 uninstall_multiple_protocol_interfaces;
	u32 calculate_crc32;
	u32 copy_mem;
	u32 set_mem;
	u32 create_event_ex;
} __packed efi_boot_services_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	u64 raise_tpl;
	u64 restore_tpl;
	u64 allocate_pages;
	u64 free_pages;
	u64 get_memory_map;
	u64 allocate_pool;
	u64 free_pool;
	u64 create_event;
	u64 set_timer;
	u64 wait_for_event;
	u64 signal_event;
	u64 close_event;
	u64 check_event;
	u64 install_protocol_interface;
	u64 reinstall_protocol_interface;
	u64 uninstall_protocol_interface;
	u64 handle_protocol;
	u64 __reserved;
	u64 register_protocol_notify;
	u64 locate_handle;
	u64 locate_device_path;
	u64 install_configuration_table;
	u64 load_image;
	u64 start_image;
	u64 exit;
	u64 unload_image;
	u64 exit_boot_services;
	u64 get_next_monotonic_count;
	u64 stall;
	u64 set_watchdog_timer;
	u64 connect_controller;
	u64 disconnect_controller;
	u64 open_protocol;
	u64 close_protocol;
	u64 open_protocol_information;
	u64 protocols_per_handle;
	u64 locate_handle_buffer;
	u64 locate_protocol;
	u64 install_multiple_protocol_interfaces;
	u64 uninstall_multiple_protocol_interfaces;
	u64 calculate_crc32;
	u64 copy_mem;
	u64 set_mem;
	u64 create_event_ex;
} __packed efi_boot_services_64_t;

/*
 * EFI Boot Services table
 */
typedef struct {
	efi_table_hdr_t hdr;
	void *raise_tpl;
	void *restore_tpl;
	efi_status_t (*allocate_pages)(int, int, unsigned long,
				       efi_physical_addr_t *);
	efi_status_t (*free_pages)(efi_physical_addr_t, unsigned long);
	efi_status_t (*get_memory_map)(unsigned long *, void *, unsigned long *,
				       unsigned long *, u32 *);
	efi_status_t (*allocate_pool)(int, unsigned long, void **);
	efi_status_t (*free_pool)(void *);
	void *create_event;
	void *set_timer;
	void *wait_for_event;
	void *signal_event;
	void *close_event;
	void *check_event;
	void *install_protocol_interface;
	void *reinstall_protocol_interface;
	void *uninstall_protocol_interface;
	efi_status_t (*handle_protocol)(efi_handle_t, efi_guid_t *, void **);
	void *__reserved;
	void *register_protocol_notify;
	efi_status_t (*locate_handle)(int, efi_guid_t *, void *,
				      unsigned long *, efi_handle_t *);
	void *locate_device_path;
	efi_status_t (*install_configuration_table)(efi_guid_t *, void *);
	void *load_image;
	void *start_image;
	void *exit;
	void *unload_image;
	efi_status_t (*exit_boot_services)(efi_handle_t, unsigned long);
	void *get_next_monotonic_count;
	void *stall;
	void *set_watchdog_timer;
	void *connect_controller;
	void *disconnect_controller;
	void *open_protocol;
	void *close_protocol;
	void *open_protocol_information;
	void *protocols_per_handle;
	void *locate_handle_buffer;
	efi_status_t (*locate_protocol)(efi_guid_t *, void *, void **);
	void *install_multiple_protocol_interfaces;
	void *uninstall_multiple_protocol_interfaces;
	void *calculate_crc32;
	void *copy_mem;
	void *set_mem;
	void *create_event_ex;
} efi_boot_services_t;

typedef enum {
	EfiPciIoWidthUint8,
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

typedef enum {
	EfiPciIoAttributeOperationGet,
	EfiPciIoAttributeOperationSet,
	EfiPciIoAttributeOperationEnable,
	EfiPciIoAttributeOperationDisable,
	EfiPciIoAttributeOperationSupported,
    EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef struct {
	u32 read;
	u32 write;
} efi_pci_io_protocol_access_32_t;

typedef struct {
	u64 read;
	u64 write;
} efi_pci_io_protocol_access_64_t;

typedef struct {
	void *read;
	void *write;
} efi_pci_io_protocol_access_t;

typedef struct {
	u32 poll_mem;
	u32 poll_io;
	efi_pci_io_protocol_access_32_t mem;
	efi_pci_io_protocol_access_32_t io;
	efi_pci_io_protocol_access_32_t pci;
	u32 copy_mem;
	u32 map;
	u32 unmap;
	u32 allocate_buffer;
	u32 free_buffer;
	u32 flush;
	u32 get_location;
	u32 attributes;
	u32 get_bar_attributes;
	u32 set_bar_attributes;
	u64 romsize;
	u32 romimage;
} efi_pci_io_protocol_32_t;

typedef struct {
	u64 poll_mem;
	u64 poll_io;
	efi_pci_io_protocol_access_64_t mem;
	efi_pci_io_protocol_access_64_t io;
	efi_pci_io_protocol_access_64_t pci;
	u64 copy_mem;
	u64 map;
	u64 unmap;
	u64 allocate_buffer;
	u64 free_buffer;
	u64 flush;
	u64 get_location;
	u64 attributes;
	u64 get_bar_attributes;
	u64 set_bar_attributes;
	u64 romsize;
	u64 romimage;
} efi_pci_io_protocol_64_t;

typedef struct {
	void *poll_mem;
	void *poll_io;
	efi_pci_io_protocol_access_t mem;
	efi_pci_io_protocol_access_t io;
	efi_pci_io_protocol_access_t pci;
	void *copy_mem;
	void *map;
	void *unmap;
	void *allocate_buffer;
	void *free_buffer;
	void *flush;
	void *get_location;
	void *attributes;
	void *get_bar_attributes;
	void *set_bar_attributes;
	uint64_t romsize;
	void *romimage;
} efi_pci_io_protocol_t;

#define EFI_PCI_IO_ATTRIBUTE_ISA_MOTHERBOARD_IO 0x0001
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO 0x0002
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO 0x0004
#define EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY 0x0008
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO 0x0010
#define EFI_PCI_IO_ATTRIBUTE_IDE_PRIMARY_IO 0x0020
#define EFI_PCI_IO_ATTRIBUTE_IDE_SECONDARY_IO 0x0040
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_WRITE_COMBINE 0x0080
#define EFI_PCI_IO_ATTRIBUTE_IO 0x0100
#define EFI_PCI_IO_ATTRIBUTE_MEMORY 0x0200
#define EFI_PCI_IO_ATTRIBUTE_BUS_MASTER 0x0400
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_CACHED 0x0800
#define EFI_PCI_IO_ATTRIBUTE_MEMORY_DISABLE 0x1000
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_DEVICE 0x2000
#define EFI_PCI_IO_ATTRIBUTE_EMBEDDED_ROM 0x4000
#define EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE 0x8000
#define EFI_PCI_IO_ATTRIBUTE_ISA_IO_16 0x10000
#define EFI_PCI_IO_ATTRIBUTE_VGA_PALETTE_IO_16 0x20000
#define EFI_PCI_IO_ATTRIBUTE_VGA_IO_16 0x40000

typedef struct {
	u32 version;
	u32 get;
	u32 set;
	u32 del;
	u32 get_all;
} apple_properties_protocol_32_t;

typedef struct {
	u64 version;
	u64 get;
	u64 set;
	u64 del;
	u64 get_all;
} apple_properties_protocol_64_t;

typedef struct {
	u32 get_capability;
	u32 get_event_log;
	u32 hash_log_extend_event;
	u32 submit_command;
	u32 get_active_pcr_banks;
	u32 set_active_pcr_banks;
	u32 get_result_of_set_active_pcr_banks;
} efi_tcg2_protocol_32_t;

typedef struct {
	u64 get_capability;
	u64 get_event_log;
	u64 hash_log_extend_event;
	u64 submit_command;
	u64 get_active_pcr_banks;
	u64 set_active_pcr_banks;
	u64 get_result_of_set_active_pcr_banks;
} efi_tcg2_protocol_64_t;

typedef u32 efi_tcg2_event_log_format;

typedef struct {
	void *get_capability;
	efi_status_t (*get_event_log)(efi_handle_t, efi_tcg2_event_log_format,
		efi_physical_addr_t *, efi_physical_addr_t *, efi_bool_t *);
	void *hash_log_extend_event;
	void *submit_command;
	void *get_active_pcr_banks;
	void *set_active_pcr_banks;
	void *get_result_of_set_active_pcr_banks;
} efi_tcg2_protocol_t;

/*
 * Types and defines for EFI ResetSystem
 */
#define EFI_RESET_COLD 0
#define EFI_RESET_WARM 1
#define EFI_RESET_SHUTDOWN 2

/*
 * EFI Runtime Services table
 */
#define EFI_RUNTIME_SERVICES_SIGNATURE ((u64)0x5652453544e5552ULL)
#define EFI_RUNTIME_SERVICES_REVISION  0x00010000

typedef struct {
	efi_table_hdr_t hdr;
	u32 get_time;
	u32 set_time;
	u32 get_wakeup_time;
	u32 set_wakeup_time;
	u32 set_virtual_address_map;
	u32 convert_pointer;
	u32 get_variable;
	u32 get_next_variable;
	u32 set_variable;
	u32 get_next_high_mono_count;
	u32 reset_system;
	u32 update_capsule;
	u32 query_capsule_caps;
	u32 query_variable_info;
} efi_runtime_services_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	u64 get_time;
	u64 set_time;
	u64 get_wakeup_time;
	u64 set_wakeup_time;
	u64 set_virtual_address_map;
	u64 convert_pointer;
	u64 get_variable;
	u64 get_next_variable;
	u64 set_variable;
	u64 get_next_high_mono_count;
	u64 reset_system;
	u64 update_capsule;
	u64 query_capsule_caps;
	u64 query_variable_info;
} efi_runtime_services_64_t;

typedef efi_status_t efi_get_time_t (efi_time_t *tm, efi_time_cap_t *tc);
typedef efi_status_t efi_set_time_t (efi_time_t *tm);
typedef efi_status_t efi_get_wakeup_time_t (efi_bool_t *enabled, efi_bool_t *pending,
					    efi_time_t *tm);
typedef efi_status_t efi_set_wakeup_time_t (efi_bool_t enabled, efi_time_t *tm);
typedef efi_status_t efi_get_variable_t (efi_char16_t *name, efi_guid_t *vendor, u32 *attr,
					 unsigned long *data_size, void *data);
typedef efi_status_t efi_get_next_variable_t (unsigned long *name_size, efi_char16_t *name,
					      efi_guid_t *vendor);
typedef efi_status_t efi_set_variable_t (efi_char16_t *name, efi_guid_t *vendor, 
					 u32 attr, unsigned long data_size,
					 void *data);
typedef efi_status_t efi_get_next_high_mono_count_t (u32 *count);
typedef void efi_reset_system_t (int reset_type, efi_status_t status,
				 unsigned long data_size, efi_char16_t *data);
typedef efi_status_t efi_set_virtual_address_map_t (unsigned long memory_map_size,
						unsigned long descriptor_size,
						u32 descriptor_version,
						efi_memory_desc_t *virtual_map);
typedef efi_status_t efi_query_variable_info_t(u32 attr,
					       u64 *storage_space,
					       u64 *remaining_space,
					       u64 *max_variable_size);
typedef efi_status_t efi_update_capsule_t(efi_capsule_header_t **capsules,
					  unsigned long count,
					  unsigned long sg_list);
typedef efi_status_t efi_query_capsule_caps_t(efi_capsule_header_t **capsules,
					      unsigned long count,
					      u64 *max_size,
					      int *reset_type);
typedef efi_status_t efi_query_variable_store_t(u32 attributes,
						unsigned long size,
						bool nonblocking);

typedef struct {
	efi_table_hdr_t			hdr;
	efi_get_time_t			*get_time;
	efi_set_time_t			*set_time;
	efi_get_wakeup_time_t		*get_wakeup_time;
	efi_set_wakeup_time_t		*set_wakeup_time;
	efi_set_virtual_address_map_t	*set_virtual_address_map;
	void				*convert_pointer;
	efi_get_variable_t		*get_variable;
	efi_get_next_variable_t		*get_next_variable;
	efi_set_variable_t		*set_variable;
	efi_get_next_high_mono_count_t	*get_next_high_mono_count;
	efi_reset_system_t		*reset_system;
	efi_update_capsule_t		*update_capsule;
	efi_query_capsule_caps_t	*query_capsule_caps;
	efi_query_variable_info_t	*query_variable_info;
} efi_runtime_services_t;

void efi_native_runtime_setup(void);

/*
 * EFI Configuration Table and GUID definitions
 *
 * These are all defined in a single line to make them easier to
 * grep for and to see them at a glance - while still having a
 * similar structure to the definitions in the spec.
 *
 * Here's how they are structured:
 *
 * GUID: 12345678-1234-1234-1234-123456789012
 * Spec:
 *      #define EFI_SOME_PROTOCOL_GUID \
 *        {0x12345678,0x1234,0x1234,\
 *          {0x12,0x34,0x12,0x34,0x56,0x78,0x90,0x12}}
 * Here:
 *	#define SOME_PROTOCOL_GUID		EFI_GUID(0x12345678, 0x1234, 0x1234,  0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x90, 0x12)
 *					^ tabs					    ^extra space
 *
 * Note that the 'extra space' separates the values at the same place
 * where the UEFI SPEC breaks the line.
 */
#define NULL_GUID				EFI_GUID(0x00000000, 0x0000, 0x0000,  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)
#define MPS_TABLE_GUID				EFI_GUID(0xeb9d2d2f, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define ACPI_TABLE_GUID				EFI_GUID(0xeb9d2d30, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define ACPI_20_TABLE_GUID			EFI_GUID(0x8868e871, 0xe4f1, 0x11d3,  0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81)
#define SMBIOS_TABLE_GUID			EFI_GUID(0xeb9d2d31, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define SMBIOS3_TABLE_GUID			EFI_GUID(0xf2fd1544, 0x9794, 0x4a2c,  0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94)
#define SAL_SYSTEM_TABLE_GUID			EFI_GUID(0xeb9d2d32, 0x2d88, 0x11d3,  0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define HCDP_TABLE_GUID				EFI_GUID(0xf951938d, 0x620b, 0x42ef,  0x82, 0x79, 0xa8, 0x4b, 0x79, 0x61, 0x78, 0x98)
#define UGA_IO_PROTOCOL_GUID			EFI_GUID(0x61a4d49e, 0x6f68, 0x4f1b,  0xb9, 0x22, 0xa8, 0x6e, 0xed, 0x0b, 0x07, 0xa2)
#define EFI_GLOBAL_VARIABLE_GUID		EFI_GUID(0x8be4df61, 0x93ca, 0x11d2,  0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c)
#define UV_SYSTEM_TABLE_GUID			EFI_GUID(0x3b13a7d4, 0x633e, 0x11dd,  0x93, 0xec, 0xda, 0x25, 0x56, 0xd8, 0x95, 0x93)
#define LINUX_EFI_CRASH_GUID			EFI_GUID(0xcfc8fc79, 0xbe2e, 0x4ddc,  0x97, 0xf0, 0x9f, 0x98, 0xbf, 0xe2, 0x98, 0xa0)
#define LOADED_IMAGE_PROTOCOL_GUID		EFI_GUID(0x5b1b31a1, 0x9562, 0x11d2,  0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID	EFI_GUID(0x9042a9de, 0x23dc, 0x4a38,  0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)
#define EFI_UGA_PROTOCOL_GUID			EFI_GUID(0x982c298b, 0xf4fa, 0x41cb,  0xb8, 0x38, 0x77, 0xaa, 0x68, 0x8f, 0xb8, 0x39)
#define EFI_PCI_IO_PROTOCOL_GUID		EFI_GUID(0x4cf5b200, 0x68b8, 0x4ca5,  0x9e, 0xec, 0xb2, 0x3e, 0x3f, 0x50, 0x02, 0x9a)
#define EFI_FILE_INFO_ID			EFI_GUID(0x09576e92, 0x6d3f, 0x11d2,  0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_SYSTEM_RESOURCE_TABLE_GUID		EFI_GUID(0xb122a263, 0x3661, 0x4f68,  0x99, 0x29, 0x78, 0xf8, 0xb0, 0xd6, 0x21, 0x80)
#define EFI_FILE_SYSTEM_GUID			EFI_GUID(0x964e5b22, 0x6459, 0x11d2,  0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define DEVICE_TREE_GUID			EFI_GUID(0xb1b621d5, 0xf19c, 0x41a5,  0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0)
#define EFI_PROPERTIES_TABLE_GUID		EFI_GUID(0x880aaca3, 0x4adc, 0x4a04,  0x90, 0x79, 0xb7, 0x47, 0x34, 0x08, 0x25, 0xe5)
#define EFI_RNG_PROTOCOL_GUID			EFI_GUID(0x3152bca5, 0xeade, 0x433d,  0x86, 0x2e, 0xc0, 0x1c, 0xdc, 0x29, 0x1f, 0x44)
#define EFI_RNG_ALGORITHM_RAW			EFI_GUID(0xe43176d7, 0xb6e8, 0x4827,  0xb7, 0x84, 0x7f, 0xfd, 0xc4, 0xb6, 0x85, 0x61)
#define EFI_MEMORY_ATTRIBUTES_TABLE_GUID	EFI_GUID(0xdcfa911d, 0x26eb, 0x469f,  0xa2, 0x20, 0x38, 0xb7, 0xdc, 0x46, 0x12, 0x20)
#define EFI_CONSOLE_OUT_DEVICE_GUID		EFI_GUID(0xd3b36f2c, 0xd551, 0x11d4,  0x9a, 0x46, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d)
#define APPLE_PROPERTIES_PROTOCOL_GUID		EFI_GUID(0x91bd12fe, 0xf6c3, 0x44fb,  0xa5, 0xb7, 0x51, 0x22, 0xab, 0x30, 0x3a, 0xe0)
#define EFI_TCG2_PROTOCOL_GUID			EFI_GUID(0x607f766c, 0x7455, 0x42be,  0x93, 0x0b, 0xe4, 0xd7, 0x6d, 0xb2, 0x72, 0x0f)

#define EFI_IMAGE_SECURITY_DATABASE_GUID	EFI_GUID(0xd719b2cb, 0x3d3a, 0x4596,  0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f)
#define EFI_SHIM_LOCK_GUID			EFI_GUID(0x605dab50, 0xe046, 0x4300,  0xab, 0xb6, 0x3d, 0xd8, 0x10, 0xdd, 0x8b, 0x23)

#define EFI_CERT_SHA256_GUID			EFI_GUID(0xc1c41626, 0x504c, 0x4092, 0xac, 0xa9, 0x41, 0xf9, 0x36, 0x93, 0x43, 0x28)
#define EFI_CERT_X509_GUID			EFI_GUID(0xa5c059a1, 0x94e4, 0x4aa7, 0x87, 0xb5, 0xab, 0x15, 0x5c, 0x2b, 0xf0, 0x72)
#define EFI_CERT_X509_SHA256_GUID		EFI_GUID(0x3bd2a492, 0x96c0, 0x4079, 0xb4, 0x20, 0xfc, 0xf9, 0x8e, 0xf1, 0x03, 0xed)

/*
 * This GUID is used to pass to the kernel proper the struct screen_info
 * structure that was populated by the stub based on the GOP protocol instance
 * associated with ConOut
 */
#define LINUX_EFI_ARM_SCREEN_INFO_TABLE_GUID	EFI_GUID(0xe03fc20a, 0x85dc, 0x406e,  0xb9, 0x0e, 0x4a, 0xb5, 0x02, 0x37, 0x1d, 0x95)
#define LINUX_EFI_LOADER_ENTRY_GUID		EFI_GUID(0x4a67b082, 0x0a4c, 0x41cf,  0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f)
#define LINUX_EFI_RANDOM_SEED_TABLE_GUID	EFI_GUID(0x1ce1e5bc, 0x7ceb, 0x42f2,  0x81, 0xe5, 0x8a, 0xad, 0xf1, 0x80, 0xf5, 0x7b)
#define LINUX_EFI_TPM_EVENT_LOG_GUID		EFI_GUID(0xb7799cb0, 0xeca2, 0x4943,  0x96, 0x67, 0x1f, 0xae, 0x07, 0xb7, 0x47, 0xfa)
#define LINUX_EFI_MEMRESERVE_TABLE_GUID		EFI_GUID(0x888eb0c6, 0x8ede, 0x4ff5,  0xa8, 0xf0, 0x9a, 0xee, 0x5c, 0xb9, 0x77, 0xc2)

typedef struct {
	efi_guid_t guid;
	u64 table;
} efi_config_table_64_t;

typedef struct {
	efi_guid_t guid;
	u32 table;
} efi_config_table_32_t;

typedef struct {
	efi_guid_t guid;
	unsigned long table;
} efi_config_table_t;

typedef struct {
	efi_guid_t guid;
	const char *name;
	unsigned long *ptr;
} efi_config_table_type_t;

#define EFI_SYSTEM_TABLE_SIGNATURE ((u64)0x5453595320494249ULL)

#define EFI_2_30_SYSTEM_TABLE_REVISION  ((2 << 16) | (30))
#define EFI_2_20_SYSTEM_TABLE_REVISION  ((2 << 16) | (20))
#define EFI_2_10_SYSTEM_TABLE_REVISION  ((2 << 16) | (10))
#define EFI_2_00_SYSTEM_TABLE_REVISION  ((2 << 16) | (00))
#define EFI_1_10_SYSTEM_TABLE_REVISION  ((1 << 16) | (10))
#define EFI_1_02_SYSTEM_TABLE_REVISION  ((1 << 16) | (02))

typedef struct {
	efi_table_hdr_t hdr;
	u64 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 __pad1;
	u64 con_in_handle;
	u64 con_in;
	u64 con_out_handle;
	u64 con_out;
	u64 stderr_handle;
	u64 stderr;
	u64 runtime;
	u64 boottime;
	u32 nr_tables;
	u32 __pad2;
	u64 tables;
} efi_system_table_64_t;

typedef struct {
	efi_table_hdr_t hdr;
	u32 fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	u32 con_in_handle;
	u32 con_in;
	u32 con_out_handle;
	u32 con_out;
	u32 stderr_handle;
	u32 stderr;
	u32 runtime;
	u32 boottime;
	u32 nr_tables;
	u32 tables;
} efi_system_table_32_t;

typedef struct {
	efi_table_hdr_t hdr;
	unsigned long fw_vendor;	/* physical addr of CHAR16 vendor string */
	u32 fw_revision;
	unsigned long con_in_handle;
	unsigned long con_in;
	unsigned long con_out_handle;
	unsigned long con_out;
	unsigned long stderr_handle;
	unsigned long stderr;
	efi_runtime_services_t *runtime;
	efi_boot_services_t *boottime;
	unsigned long nr_tables;
	unsigned long tables;
} efi_system_table_t;

/*
 * Architecture independent structure for describing a memory map for the
 * benefit of efi_memmap_init_early(), saving us the need to pass four
 * parameters.
 */
struct efi_memory_map_data {
	phys_addr_t phys_map;
	unsigned long size;
	unsigned long desc_version;
	unsigned long desc_size;
};

struct efi_memory_map {
	phys_addr_t phys_map;
	void *map;
	void *map_end;
	int nr_map;
	unsigned long desc_version;
	unsigned long desc_size;
	bool late;
};

struct efi_mem_range {
	struct range range;
	u64 attribute;
};

struct efi_fdt_params {
	u64 system_table;
	u64 mmap;
	u32 mmap_size;
	u32 desc_size;
	u32 desc_ver;
};

typedef struct {
	u32 revision;
	u32 parent_handle;
	u32 system_table;
	u32 device_handle;
	u32 file_path;
	u32 reserved;
	u32 load_options_size;
	u32 load_options;
	u32 image_base;
	__aligned_u64 image_size;
	unsigned int image_code_type;
	unsigned int image_data_type;
	unsigned long unload;
} efi_loaded_image_32_t;

typedef struct {
	u32 revision;
	u64 parent_handle;
	u64 system_table;
	u64 device_handle;
	u64 file_path;
	u64 reserved;
	u32 load_options_size;
	u64 load_options;
	u64 image_base;
	__aligned_u64 image_size;
	unsigned int image_code_type;
	unsigned int image_data_type;
	unsigned long unload;
} efi_loaded_image_64_t;

typedef struct {
	u32 revision;
	void *parent_handle;
	efi_system_table_t *system_table;
	void *device_handle;
	void *file_path;
	void *reserved;
	u32 load_options_size;
	void *load_options;
	void *image_base;
	__aligned_u64 image_size;
	unsigned int image_code_type;
	unsigned int image_data_type;
	unsigned long unload;
} efi_loaded_image_t;


typedef struct {
	u64 size;
	u64 file_size;
	u64 phys_size;
	efi_time_t create_time;
	efi_time_t last_access_time;
	efi_time_t modification_time;
	__aligned_u64 attribute;
	efi_char16_t filename[1];
} efi_file_info_t;

typedef struct {
	u64 revision;
	u32 open;
	u32 close;
	u32 delete;
	u32 read;
	u32 write;
	u32 get_position;
	u32 set_position;
	u32 get_info;
	u32 set_info;
	u32 flush;
} efi_file_handle_32_t;

typedef struct {
	u64 revision;
	u64 open;
	u64 close;
	u64 delete;
	u64 read;
	u64 write;
	u64 get_position;
	u64 set_position;
	u64 get_info;
	u64 set_info;
	u64 flush;
} efi_file_handle_64_t;

typedef struct _efi_file_handle {
	u64 revision;
	efi_status_t (*open)(struct _efi_file_handle *,
			     struct _efi_file_handle **,
			     efi_char16_t *, u64, u64);
	efi_status_t (*close)(struct _efi_file_handle *);
	void *delete;
	efi_status_t (*read)(struct _efi_file_handle *, unsigned long *,
			     void *);
	void *write;
	void *get_position;
	void *set_position;
	efi_status_t (*get_info)(struct _efi_file_handle *, efi_guid_t *,
			unsigned long *, void *);
	void *set_info;
	void *flush;
} efi_file_handle_t;

typedef struct {
	u64 revision;
	u32 open_volume;
} efi_file_io_interface_32_t;

typedef struct {
	u64 revision;
	u64 open_volume;
} efi_file_io_interface_64_t;

typedef struct _efi_file_io_interface {
	u64 revision;
	int (*open_volume)(struct _efi_file_io_interface *,
			   efi_file_handle_t **);
} efi_file_io_interface_t;

#define EFI_FILE_MODE_READ	0x0000000000000001
#define EFI_FILE_MODE_WRITE	0x0000000000000002
#define EFI_FILE_MODE_CREATE	0x8000000000000000

typedef struct {
	u32 version;
	u32 length;
	u64 memory_protection_attribute;
} efi_properties_table_t;

#define EFI_PROPERTIES_TABLE_VERSION	0x00010000
#define EFI_PROPERTIES_RUNTIME_MEMORY_PROTECTION_NON_EXECUTABLE_PE_DATA	0x1

#define EFI_INVALID_TABLE_ADDR		(~0UL)

typedef struct {
	u32 version;
	u32 num_entries;
	u32 desc_size;
	u32 reserved;
	efi_memory_desc_t entry[0];
} efi_memory_attributes_table_t;

typedef struct {
	efi_guid_t signature_owner;
	u8 signature_data[];
} efi_signature_data_t;

typedef struct {
	efi_guid_t signature_type;
	u32 signature_list_size;
	u32 signature_header_size;
	u32 signature_size;
	u8 signature_header[];
	/* efi_signature_data_t signatures[][] */
} efi_signature_list_t;

typedef u8 efi_sha256_hash_t[32];

typedef struct {
	efi_sha256_hash_t to_be_signed_hash;
	efi_time_t time_of_revocation;
} efi_cert_x509_sha256_t;

/*
 * All runtime access to EFI goes through this structure:
 */
extern struct efi {
	efi_system_table_t *systab;	/* EFI system table */
	unsigned int runtime_version;	/* Runtime services version */
	unsigned long mps;		/* MPS table */
	unsigned long acpi;		/* ACPI table  (IA64 ext 0.71) */
	unsigned long acpi20;		/* ACPI table  (ACPI 2.0) */
	unsigned long smbios;		/* SMBIOS table (32 bit entry point) */
	unsigned long smbios3;		/* SMBIOS table (64 bit entry point) */
	unsigned long sal_systab;	/* SAL system table */
	unsigned long boot_info;	/* boot info table */
	unsigned long hcdp;		/* HCDP table */
	unsigned long uga;		/* UGA table */
	unsigned long uv_systab;	/* UV system table */
	unsigned long fw_vendor;	/* fw_vendor */
	unsigned long runtime;		/* runtime table */
	unsigned long config_table;	/* config tables */
	unsigned long esrt;		/* ESRT table */
	unsigned long properties_table;	/* properties table */
	unsigned long mem_attr_table;	/* memory attributes table */
	unsigned long rng_seed;		/* UEFI firmware random seed */
	unsigned long tpm_log;		/* TPM2 Event Log table */
	unsigned long mem_reserve;	/* Linux EFI memreserve table */
	efi_get_time_t *get_time;
	efi_set_time_t *set_time;
	efi_get_wakeup_time_t *get_wakeup_time;
	efi_set_wakeup_time_t *set_wakeup_time;
	efi_get_variable_t *get_variable;
	efi_get_next_variable_t *get_next_variable;
	efi_set_variable_t *set_variable;
	efi_set_variable_t *set_variable_nonblocking;
	efi_query_variable_info_t *query_variable_info;
	efi_query_variable_info_t *query_variable_info_nonblocking;
	efi_update_capsule_t *update_capsule;
	efi_query_capsule_caps_t *query_capsule_caps;
	efi_get_next_high_mono_count_t *get_next_high_mono_count;
	efi_reset_system_t *reset_system;
	efi_set_virtual_address_map_t *set_virtual_address_map;
	struct efi_memory_map memmap;
	unsigned long flags;
} efi;

extern struct mm_struct efi_mm;

static inline int
efi_guidcmp (efi_guid_t left, efi_guid_t right)
{
	return memcmp(&left, &right, sizeof (efi_guid_t));
}

static inline char *
efi_guid_to_str(efi_guid_t *guid, char *out)
{
	sprintf(out, "%pUl", guid->b);
        return out;
}

extern void efi_init (void);
extern void *efi_get_pal_addr (void);
extern void efi_map_pal_code (void);
extern void efi_memmap_walk (efi_freemem_callback_t callback, void *arg);
extern void efi_gettimeofday (struct timespec64 *ts);
extern void efi_enter_virtual_mode (void);	/* switch EFI to virtual mode, if possible */
#ifdef CONFIG_X86
extern efi_status_t efi_query_variable_store(u32 attributes,
					     unsigned long size,
					     bool nonblocking);
extern void efi_find_mirror(void);
#else

static inline efi_status_t efi_query_variable_store(u32 attributes,
						    unsigned long size,
						    bool nonblocking)
{
	return EFI_SUCCESS;
}
#endif
extern void __iomem *efi_lookup_mapped_addr(u64 phys_addr);

extern phys_addr_t __init efi_memmap_alloc(unsigned int num_entries);
extern int __init efi_memmap_init_early(struct efi_memory_map_data *data);
extern int __init efi_memmap_init_late(phys_addr_t addr, unsigned long size);
extern void __init efi_memmap_unmap(void);
extern int __init efi_memmap_install(phys_addr_t addr, unsigned int nr_map);
extern int __init efi_memmap_split_count(efi_memory_desc_t *md,
					 struct range *range);
extern void __init efi_memmap_insert(struct efi_memory_map *old_memmap,
				     void *buf, struct efi_mem_range *mem);

extern int efi_config_init(efi_config_table_type_t *arch_tables);
#ifdef CONFIG_EFI_ESRT
extern void __init efi_esrt_init(void);
#else
static inline void efi_esrt_init(void) { }
#endif
extern int efi_config_parse_tables(void *config_tables, int count, int sz,
				   efi_config_table_type_t *arch_tables);
extern u64 efi_get_iobase (void);
extern int efi_mem_type(unsigned long phys_addr);
extern u64 efi_mem_attributes (unsigned long phys_addr);
extern u64 efi_mem_attribute (unsigned long phys_addr, unsigned long size);
extern int __init efi_uart_console_only (void);
extern u64 efi_mem_desc_end(efi_memory_desc_t *md);
extern int efi_mem_desc_lookup(u64 phys_addr, efi_memory_desc_t *out_md);
extern void efi_mem_reserve(phys_addr_t addr, u64 size);
extern int efi_mem_reserve_persistent(phys_addr_t addr, u64 size);
extern void efi_initialize_iomem_resources(struct resource *code_resource,
		struct resource *data_resource, struct resource *bss_resource);
extern int efi_get_fdt_params(struct efi_fdt_params *params);
extern struct kobject *efi_kobj;

extern int efi_reboot_quirk_mode;
extern bool efi_poweroff_required(void);

#ifdef CONFIG_EFI_FAKE_MEMMAP
extern void __init efi_fake_memmap(void);
#else
static inline void efi_fake_memmap(void) { }
#endif

/*
 * efi_memattr_perm_setter - arch specific callback function passed into
 *                           efi_memattr_apply_permissions() that updates the
 *                           mapping permissions described by the second
 *                           argument in the page tables referred to by the
 *                           first argument.
 */
typedef int (*efi_memattr_perm_setter)(struct mm_struct *, efi_memory_desc_t *);

extern int efi_memattr_init(void);
extern int efi_memattr_apply_permissions(struct mm_struct *mm,
					 efi_memattr_perm_setter fn);

/*
 * efi_early_memdesc_ptr - get the n-th EFI memmap descriptor
 * @map: the start of efi memmap
 * @desc_size: the size of space for each EFI memmap descriptor
 * @n: the index of efi memmap descriptor
 *
 * EFI boot service provides the GetMemoryMap() function to get a copy of the
 * current memory map which is an array of memory descriptors, each of
 * which describes a contiguous block of memory. It also gets the size of the
 * map, and the size of each descriptor, etc.
 *
 * Note that per section 6.2 of UEFI Spec 2.6 Errata A, the returned size of
 * each descriptor might not be equal to sizeof(efi_memory_memdesc_t),
 * since efi_memory_memdesc_t may be extended in the future. Thus the OS
 * MUST use the returned size of the descriptor to find the start of each
 * efi_memory_memdesc_t in the memory map array. This should only be used
 * during bootup since for_each_efi_memory_desc_xxx() is available after the
 * kernel initializes the EFI subsystem to set up struct efi_memory_map.
 */
#define efi_early_memdesc_ptr(map, desc_size, n)			\
	(efi_memory_desc_t *)((void *)(map) + ((n) * (desc_size)))

/* Iterate through an efi_memory_map */
#define for_each_efi_memory_desc_in_map(m, md)				   \
	for ((md) = (m)->map;						   \
	     (md) && ((void *)(md) + (m)->desc_size) <= (m)->map_end;	   \
	     (md) = (void *)(md) + (m)->desc_size)

/**
 * for_each_efi_memory_desc - iterate over descriptors in efi.memmap
 * @md: the efi_memory_desc_t * iterator
 *
 * Once the loop finishes @md must not be accessed.
 */
#define for_each_efi_memory_desc(md) \
	for_each_efi_memory_desc_in_map(&efi.memmap, md)

/*
 * Format an EFI memory descriptor's type and attributes to a user-provided
 * character buffer, as per snprintf(), and return the buffer.
 */
char * __init efi_md_typeattr_format(char *buf, size_t size,
				     const efi_memory_desc_t *md);


typedef void (*efi_element_handler_t)(const char *source,
				      const void *element_data,
				      size_t element_size);
extern int __init parse_efi_signature_list(
	const char *source,
	const void *data, size_t size,
	efi_element_handler_t (*get_handler_for_guid)(const efi_guid_t *));

/**
 * efi_range_is_wc - check the WC bit on an address range
 * @start: starting kvirt address
 * @len: length of range
 *
 * Consult the EFI memory map and make sure it's ok to set this range WC.
 * Returns true or false.
 */
static inline int efi_range_is_wc(unsigned long start, unsigned long len)
{
	unsigned long i;

	for (i = 0; i < len; i += (1UL << EFI_PAGE_SHIFT)) {
		unsigned long paddr = __pa(start + i);
		if (!(efi_mem_attributes(paddr) & EFI_MEMORY_WC))
			return 0;
	}
	/* The range checked out */
	return 1;
}

#ifdef CONFIG_EFI_PCDP
extern int __init efi_setup_pcdp_console(char *);
#endif

/*
 * We play games with efi_enabled so that the compiler will, if
 * possible, remove EFI-related code altogether.
 */
#define EFI_BOOT		0	/* Were we booted from EFI? */
#define EFI_CONFIG_TABLES	2	/* Can we use EFI config tables? */
#define EFI_RUNTIME_SERVICES	3	/* Can we use runtime services? */
#define EFI_MEMMAP		4	/* Can we use EFI memory map? */
#define EFI_64BIT		5	/* Is the firmware 64-bit? */
#define EFI_PARAVIRT		6	/* Access is via a paravirt interface */
#define EFI_ARCH_1		7	/* First arch-specific bit */
#define EFI_DBG			8	/* Print additional debug info at runtime */
#define EFI_NX_PE_DATA		9	/* Can runtime data regions be mapped non-executable? */
#define EFI_MEM_ATTR		10	/* Did firmware publish an EFI_MEMORY_ATTRIBUTES table? */

#ifdef CONFIG_EFI
/*
 * Test whether the above EFI_* bits are enabled.
 */
static inline bool efi_enabled(int feature)
{
	return test_bit(feature, &efi.flags) != 0;
}
extern void efi_reboot(enum reboot_mode reboot_mode, const char *__unused);

extern bool efi_is_table_address(unsigned long phys_addr);
#else
static inline bool efi_enabled(int feature)
{
	return false;
}
static inline void
efi_reboot(enum reboot_mode reboot_mode, const char *__unused) {}

static inline bool
efi_capsule_pending(int *reset_type)
{
	return false;
}

static inline bool efi_is_table_address(unsigned long phys_addr)
{
	return false;
}
#endif

extern int efi_status_to_err(efi_status_t status);

/*
 * Variable Attributes
 */
#define EFI_VARIABLE_NON_VOLATILE       0x0000000000000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x0000000000000002
#define EFI_VARIABLE_RUNTIME_ACCESS     0x0000000000000004
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD 0x0000000000000008
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS 0x0000000000000010
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS 0x0000000000000020
#define EFI_VARIABLE_APPEND_WRITE	0x0000000000000040

#define EFI_VARIABLE_MASK 	(EFI_VARIABLE_NON_VOLATILE | \
				EFI_VARIABLE_BOOTSERVICE_ACCESS | \
				EFI_VARIABLE_RUNTIME_ACCESS | \
				EFI_VARIABLE_HARDWARE_ERROR_RECORD | \
				EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS | \
				EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS | \
				EFI_VARIABLE_APPEND_WRITE)
/*
 * Length of a GUID string (strlen("aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"))
 * not including trailing NUL
 */
#define EFI_VARIABLE_GUID_LEN	UUID_STRING_LEN

/*
 * The type of search to perform when calling boottime->locate_handle
 */
#define EFI_LOCATE_ALL_HANDLES			0
#define EFI_LOCATE_BY_REGISTER_NOTIFY		1
#define EFI_LOCATE_BY_PROTOCOL			2

/*
 * EFI Device Path information
 */
#define EFI_DEV_HW			0x01
#define  EFI_DEV_PCI				 1
#define  EFI_DEV_PCCARD				 2
#define  EFI_DEV_MEM_MAPPED			 3
#define  EFI_DEV_VENDOR				 4
#define  EFI_DEV_CONTROLLER			 5
#define EFI_DEV_ACPI			0x02
#define   EFI_DEV_BASIC_ACPI			 1
#define   EFI_DEV_EXPANDED_ACPI			 2
#define EFI_DEV_MSG			0x03
#define   EFI_DEV_MSG_ATAPI			 1
#define   EFI_DEV_MSG_SCSI			 2
#define   EFI_DEV_MSG_FC			 3
#define   EFI_DEV_MSG_1394			 4
#define   EFI_DEV_MSG_USB			 5
#define   EFI_DEV_MSG_USB_CLASS			15
#define   EFI_DEV_MSG_I20			 6
#define   EFI_DEV_MSG_MAC			11
#define   EFI_DEV_MSG_IPV4			12
#define   EFI_DEV_MSG_IPV6			13
#define   EFI_DEV_MSG_INFINIBAND		 9
#define   EFI_DEV_MSG_UART			14
#define   EFI_DEV_MSG_VENDOR			10
#define EFI_DEV_MEDIA			0x04
#define   EFI_DEV_MEDIA_HARD_DRIVE		 1
#define   EFI_DEV_MEDIA_CDROM			 2
#define   EFI_DEV_MEDIA_VENDOR			 3
#define   EFI_DEV_MEDIA_FILE			 4
#define   EFI_DEV_MEDIA_PROTOCOL		 5
#define EFI_DEV_BIOS_BOOT		0x05
#define EFI_DEV_END_PATH		0x7F
#define EFI_DEV_END_PATH2		0xFF
#define   EFI_DEV_END_INSTANCE			0x01
#define   EFI_DEV_END_ENTIRE			0xFF

struct efi_generic_dev_path {
	u8 type;
	u8 sub_type;
	u16 length;
} __attribute ((packed));

struct efi_dev_path {
	u8 type;	/* can be replaced with unnamed */
	u8 sub_type;	/* struct efi_generic_dev_path; */
	u16 length;	/* once we've moved to -std=c11 */
	union {
		struct {
			u32 hid;
			u32 uid;
		} acpi;
		struct {
			u8 fn;
			u8 dev;
		} pci;
	};
} __attribute ((packed));

#if IS_ENABLED(CONFIG_EFI_DEV_PATH_PARSER)
struct device *efi_get_device_by_path(struct efi_dev_path **node, size_t *len);
#endif

static inline void memrange_efi_to_native(u64 *addr, u64 *npages)
{
	*npages = PFN_UP(*addr + (*npages<<EFI_PAGE_SHIFT)) - PFN_DOWN(*addr);
	*addr &= PAGE_MASK;
}

/*
 * EFI Variable support.
 *
 * Different firmware drivers can expose their EFI-like variables using
 * the following.
 */

struct efivar_operations {
	efi_get_variable_t *get_variable;
	efi_get_next_variable_t *get_next_variable;
	efi_set_variable_t *set_variable;
	efi_set_variable_t *set_variable_nonblocking;
	efi_query_variable_store_t *query_variable_store;
};

struct efivars {
	struct kset *kset;
	struct kobject *kobject;
	const struct efivar_operations *ops;
};

/*
 * The maximum size of VariableName + Data = 1024
 * Therefore, it's reasonable to save that much
 * space in each part of the structure,
 * and we use a page for reading/writing.
 */

#define EFI_VAR_NAME_LEN	1024

struct efi_variable {
	efi_char16_t  VariableName[EFI_VAR_NAME_LEN/sizeof(efi_char16_t)];
	efi_guid_t    VendorGuid;
	unsigned long DataSize;
	__u8          Data[1024];
	efi_status_t  Status;
	__u32         Attributes;
} __attribute__((packed));

struct efivar_entry {
	struct efi_variable var;
	struct list_head list;
	struct kobject kobj;
	bool scanning;
	bool deleting;
};

typedef struct {
	u32 reset;
	u32 output_string;
	u32 test_string;
} efi_simple_text_output_protocol_32_t;

typedef struct {
	u64 reset;
	u64 output_string;
	u64 test_string;
} efi_simple_text_output_protocol_64_t;

struct efi_simple_text_output_protocol {
	void *reset;
	efi_status_t (*output_string)(void *, void *);
	void *test_string;
};

#define PIXEL_RGB_RESERVED_8BIT_PER_COLOR		0
#define PIXEL_BGR_RESERVED_8BIT_PER_COLOR		1
#define PIXEL_BIT_MASK					2
#define PIXEL_BLT_ONLY					3
#define PIXEL_FORMAT_MAX				4

struct efi_pixel_bitmask {
	u32 red_mask;
	u32 green_mask;
	u32 blue_mask;
	u32 reserved_mask;
};

struct efi_graphics_output_mode_info {
	u32 version;
	u32 horizontal_resolution;
	u32 vertical_resolution;
	int pixel_format;
	struct efi_pixel_bitmask pixel_information;
	u32 pixels_per_scan_line;
} __packed;

struct efi_graphics_output_protocol_mode_32 {
	u32 max_mode;
	u32 mode;
	u32 info;
	u32 size_of_info;
	u64 frame_buffer_base;
	u32 frame_buffer_size;
} __packed;

struct efi_graphics_output_protocol_mode_64 {
	u32 max_mode;
	u32 mode;
	u64 info;
	u64 size_of_info;
	u64 frame_buffer_base;
	u64 frame_buffer_size;
} __packed;

struct efi_graphics_output_protocol_mode {
	u32 max_mode;
	u32 mode;
	unsigned long info;
	unsigned long size_of_info;
	u64 frame_buffer_base;
	unsigned long frame_buffer_size;
} __packed;

struct efi_graphics_output_protocol_32 {
	u32 query_mode;
	u32 set_mode;
	u32 blt;
	u32 mode;
};

struct efi_graphics_output_protocol_64 {
	u64 query_mode;
	u64 set_mode;
	u64 blt;
	u64 mode;
};

struct efi_graphics_output_protocol {
	unsigned long query_mode;
	unsigned long set_mode;
	unsigned long blt;
	struct efi_graphics_output_protocol_mode *mode;
};

typedef efi_status_t (*efi_graphics_output_protocol_query_mode)(
	struct efi_graphics_output_protocol *, u32, unsigned long *,
	struct efi_graphics_output_mode_info **);

extern struct list_head efivar_sysfs_list;

static inline void
efivar_unregister(struct efivar_entry *var)
{
	kobject_put(&var->kobj);
}

int efivars_register(struct efivars *efivars,
		     const struct efivar_operations *ops,
		     struct kobject *kobject);
int efivars_unregister(struct efivars *efivars);
struct kobject *efivars_kobject(void);

int efivar_init(int (*func)(efi_char16_t *, efi_guid_t, unsigned long, void *),
		void *data, bool duplicates, struct list_head *head);

int efivar_entry_add(struct efivar_entry *entry, struct list_head *head);
int efivar_entry_remove(struct efivar_entry *entry);

int __efivar_entry_delete(struct efivar_entry *entry);
int efivar_entry_delete(struct efivar_entry *entry);

int efivar_entry_size(struct efivar_entry *entry, unsigned long *size);
int __efivar_entry_get(struct efivar_entry *entry, u32 *attributes,
		       unsigned long *size, void *data);
int efivar_entry_get(struct efivar_entry *entry, u32 *attributes,
		     unsigned long *size, void *data);
int efivar_entry_set(struct efivar_entry *entry, u32 attributes,
		     unsigned long size, void *data, struct list_head *head);
int efivar_entry_set_get_size(struct efivar_entry *entry, u32 attributes,
			      unsigned long *size, void *data, bool *set);
int efivar_entry_set_safe(efi_char16_t *name, efi_guid_t vendor, u32 attributes,
			  bool block, unsigned long size, void *data);

int efivar_entry_iter_begin(void);
void efivar_entry_iter_end(void);

int __efivar_entry_iter(int (*func)(struct efivar_entry *, void *),
			struct list_head *head, void *data,
			struct efivar_entry **prev);
int efivar_entry_iter(int (*func)(struct efivar_entry *, void *),
		      struct list_head *head, void *data);

struct efivar_entry *efivar_entry_find(efi_char16_t *name, efi_guid_t guid,
				       struct list_head *head, bool remove);

bool efivar_validate(efi_guid_t vendor, efi_char16_t *var_name, u8 *data,
		     unsigned long data_size);
bool efivar_variable_is_removable(efi_guid_t vendor, const char *name,
				  size_t len);

extern struct work_struct efivar_work;
void efivar_run_worker(void);

#if defined(CONFIG_EFI_VARS) || defined(CONFIG_EFI_VARS_MODULE)
int efivars_sysfs_init(void);

#define EFIVARS_DATA_SIZE_MAX 1024

#endif /* CONFIG_EFI_VARS */
extern bool efi_capsule_pending(int *reset_type);

extern int efi_capsule_supported(efi_guid_t guid, u32 flags,
				 size_t size, int *reset);

extern int efi_capsule_update(efi_capsule_header_t *capsule,
			      phys_addr_t *pages);

#ifdef CONFIG_EFI_RUNTIME_MAP
int efi_runtime_map_init(struct kobject *);
int efi_get_runtime_map_size(void);
int efi_get_runtime_map_desc_size(void);
int efi_runtime_map_copy(void *buf, size_t bufsz);
#else
static inline int efi_runtime_map_init(struct kobject *kobj)
{
	return 0;
}

static inline int efi_get_runtime_map_size(void)
{
	return 0;
}

static inline int efi_get_runtime_map_desc_size(void)
{
	return 0;
}

static inline int efi_runtime_map_copy(void *buf, size_t bufsz)
{
	return 0;
}

#endif

/* prototypes shared between arch specific and generic stub code */

void efi_printk(efi_system_table_t *sys_table_arg, char *str);

void efi_free(efi_system_table_t *sys_table_arg, unsigned long size,
	      unsigned long addr);

char *efi_convert_cmdline(efi_system_table_t *sys_table_arg,
			  efi_loaded_image_t *image, int *cmd_line_len);

efi_status_t efi_get_memory_map(efi_system_table_t *sys_table_arg,
				struct efi_boot_memmap *map);

efi_status_t efi_low_alloc(efi_system_table_t *sys_table_arg,
			   unsigned long size, unsigned long align,
			   unsigned long *addr);

efi_status_t efi_high_alloc(efi_system_table_t *sys_table_arg,
			    unsigned long size, unsigned long align,
			    unsigned long *addr, unsigned long max);

efi_status_t efi_relocate_kernel(efi_system_table_t *sys_table_arg,
				 unsigned long *image_addr,
				 unsigned long image_size,
				 unsigned long alloc_size,
				 unsigned long preferred_addr,
				 unsigned long alignment);

efi_status_t handle_cmdline_files(efi_system_table_t *sys_table_arg,
				  efi_loaded_image_t *image,
				  char *cmd_line, char *option_string,
				  unsigned long max_addr,
				  unsigned long *load_addr,
				  unsigned long *load_size);

efi_status_t efi_parse_options(char const *cmdline);

efi_status_t efi_setup_gop(efi_system_table_t *sys_table_arg,
			   struct screen_info *si, efi_guid_t *proto,
			   unsigned long size);

bool efi_runtime_disabled(void);
extern void efi_call_virt_check_flags(unsigned long flags, const char *call);

enum efi_secureboot_mode {
	efi_secureboot_mode_unset,
	efi_secureboot_mode_unknown,
	efi_secureboot_mode_disabled,
	efi_secureboot_mode_enabled,
};
enum efi_secureboot_mode efi_get_secureboot(efi_system_table_t *sys_table);

#ifdef CONFIG_RESET_ATTACK_MITIGATION
void efi_enable_reset_attack_mitigation(efi_system_table_t *sys_table_arg);
#else
static inline void
efi_enable_reset_attack_mitigation(efi_system_table_t *sys_table_arg) { }
#endif

void efi_retrieve_tpm2_eventlog(efi_system_table_t *sys_table);

/*
 * Arch code can implement the following three template macros, avoiding
 * reptition for the void/non-void return cases of {__,}efi_call_virt():
 *
 *  * arch_efi_call_virt_setup()
 *
 *    Sets up the environment for the call (e.g. switching page tables,
 *    allowing kernel-mode use of floating point, if required).
 *
 *  * arch_efi_call_virt()
 *
 *    Performs the call. The last expression in the macro must be the call
 *    itself, allowing the logic to be shared by the void and non-void
 *    cases.
 *
 *  * arch_efi_call_virt_teardown()
 *
 *    Restores the usual kernel environment once the call has returned.
 */

#define efi_call_virt_pointer(p, f, args...)				\
({									\
	efi_status_t __s;						\
	unsigned long __flags;						\
									\
	arch_efi_call_virt_setup();					\
									\
	local_save_flags(__flags);					\
	__s = arch_efi_call_virt(p, f, args);				\
	efi_call_virt_check_flags(__flags, __stringify(f));		\
									\
	arch_efi_call_virt_teardown();					\
									\
	__s;								\
})

#define __efi_call_virt_pointer(p, f, args...)				\
({									\
	unsigned long __flags;						\
									\
	arch_efi_call_virt_setup();					\
									\
	local_save_flags(__flags);					\
	arch_efi_call_virt(p, f, args);					\
	efi_call_virt_check_flags(__flags, __stringify(f));		\
									\
	arch_efi_call_virt_teardown();					\
})

typedef efi_status_t (*efi_exit_boot_map_processing)(
	efi_system_table_t *sys_table_arg,
	struct efi_boot_memmap *map,
	void *priv);

efi_status_t efi_exit_boot_services(efi_system_table_t *sys_table,
				    void *handle,
				    struct efi_boot_memmap *map,
				    void *priv,
				    efi_exit_boot_map_processing priv_func);

#define EFI_RANDOM_SEED_SIZE		64U

struct linux_efi_random_seed {
	u32	size;
	u8	bits[];
};

struct linux_efi_tpm_eventlog {
	u32	size;
	u8	version;
	u8	log[];
};

extern int efi_tpm_eventlog_init(void);

/*
 * efi_runtime_service() function identifiers.
 * "NONE" is used by efi_recover_from_page_fault() to check if the page
 * fault happened while executing an efi runtime service.
 */
enum efi_rts_ids {
	NONE,
	GET_TIME,
	SET_TIME,
	GET_WAKEUP_TIME,
	SET_WAKEUP_TIME,
	GET_VARIABLE,
	GET_NEXT_VARIABLE,
	SET_VARIABLE,
	QUERY_VARIABLE_INFO,
	GET_NEXT_HIGH_MONO_COUNT,
	RESET_SYSTEM,
	UPDATE_CAPSULE,
	QUERY_CAPSULE_CAPS,
};

/*
 * efi_runtime_work:	Details of EFI Runtime Service work
 * @arg<1-5>:		EFI Runtime Service function arguments
 * @status:		Status of executing EFI Runtime Service
 * @efi_rts_id:		EFI Runtime Service function identifier
 * @efi_rts_comp:	Struct used for handling completions
 */
struct efi_runtime_work {
	void *arg1;
	void *arg2;
	void *arg3;
	void *arg4;
	void *arg5;
	efi_status_t status;
	struct work_struct work;
	enum efi_rts_ids efi_rts_id;
	struct completion efi_rts_comp;
};

extern struct efi_runtime_work efi_rts_work;

/* Workqueue to queue EFI Runtime Services */
extern struct workqueue_struct *efi_rts_wq;

struct linux_efi_memreserve {
	int		size;			// allocated size of the array
	atomic_t	count;			// number of entries used
	phys_addr_t	next;			// pa of next struct instance
	struct {
		phys_addr_t	base;
		phys_addr_t	size;
	} entry[0];
};

#define EFI_MEMRESERVE_SIZE(count) (sizeof(struct linux_efi_memreserve) + \
	(count) * sizeof(((struct linux_efi_memreserve *)0)->entry[0]))

#define EFI_MEMRESERVE_COUNT(size) (((size) - sizeof(struct linux_efi_memreserve)) \
	/ sizeof(((struct linux_efi_memreserve *)0)->entry[0]))

#endif /* _LINUX_EFI_H */
