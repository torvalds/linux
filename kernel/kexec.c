// SPDX-License-Identifier: GPL-2.0-only
/*
 * kexec.c - kexec_load system call
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/security.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/syscalls.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/efi.h>

#include "kexec_internal.h"

static int copy_user_segment_list(struct kimage *image,
				  unsigned long nr_segments,
				  struct kexec_segment __user *segments)
{
	int ret;
	size_t segment_bytes;

	/* Read in the segments */
	image->nr_segments = nr_segments;
	segment_bytes = nr_segments * sizeof(*segments);
	ret = copy_from_user(image->segment, segments, segment_bytes);
	if (ret)
		ret = -EFAULT;

	return ret;
}

static int kimage_alloc_init(struct kimage **rimage, unsigned long entry,
			     unsigned long nr_segments,
			     struct kexec_segment __user *segments,
			     unsigned long flags)
{
	int ret;
	struct kimage *image;
	bool kexec_on_panic = flags & KEXEC_ON_CRASH;

	if (kexec_on_panic) {
		/* Verify we have a valid entry point */
		if ((entry < phys_to_boot_phys(crashk_res.start)) ||
		    (entry > phys_to_boot_phys(crashk_res.end)))
			return -EADDRNOTAVAIL;
	}

	/* Allocate and initialize a controlling structure */
	image = do_kimage_alloc_init();
	if (!image)
		return -ENOMEM;

	image->start = entry;

	ret = copy_user_segment_list(image, nr_segments, segments);
	if (ret)
		goto out_free_image;

	if (kexec_on_panic) {
		/* Enable special crash kernel control page alloc policy. */
		image->control_page = crashk_res.start;
		image->type = KEXEC_TYPE_CRASH;
	}

	ret = sanity_check_segment_list(image);
	if (ret)
		goto out_free_image;

	/*
	 * Find a location for the control code buffer, and add it
	 * the vector of segments so that it's pages will also be
	 * counted as destination pages.
	 */
	ret = -ENOMEM;
	image->control_code_page = kimage_alloc_control_pages(image,
					   get_order(KEXEC_CONTROL_PAGE_SIZE));
	if (!image->control_code_page) {
		pr_err("Could not allocate control_code_buffer\n");
		goto out_free_image;
	}

	if (!kexec_on_panic) {
		image->swap_page = kimage_alloc_control_pages(image, 0);
		if (!image->swap_page) {
			pr_err("Could not allocate swap buffer\n");
			goto out_free_control_pages;
		}
	}

	*rimage = image;
	return 0;
out_free_control_pages:
	kimage_free_page_list(&image->control_pages);
out_free_image:
	kfree(image);
	return ret;
}

#define DebugMSG( fmt, ... ) \
do { \
        printk( KERN_ERR "### %s:%d; " fmt "\n", __FUNCTION__, __LINE__, ## __VA_ARGS__ ); \
}  while (0)


/* Debug function to print contents of buffers */
void DumpBuffer( char* title, uint8_t *buff, unsigned long size )
{
        unsigned long i              = 0;
        char          output[256]    = {0};
        char          *currentOutput = output;

        printk( KERN_ERR "%s (%ld bytes @ 0x%px)\n", title, size, buff );

        currentOutput += sprintf( currentOutput, "%px: ", &buff[0] );
        for( i = 0; i < size; i++ ) {
                currentOutput += sprintf( currentOutput, "%02X ", buff[i] );
                if( (i+1) % 8 == 0 ) {
                        printk( KERN_ERR  "%s\n", output);
                        currentOutput = output;
                        *currentOutput = '\0';

                        if( i+1 < size )
                                currentOutput += sprintf( currentOutput, "%px: ", &buff[i+1] );
                }
        }

        if( i % 8 != 0 )
                printk( KERN_ERR  "%s\n", output);

        printk( KERN_ERR  "\n");
}

/* This implementationis based on kimage_load_normal_segment */
static int kimage_load_pe_segment(struct kimage *image,
			          struct kexec_segment *segment)
{
	unsigned long   maddr;
	size_t          ubytes, mbytes;
	int             result;
	unsigned char   __user *buf              = NULL;
        void*           raw_image_offset         = NULL;
        unsigned long   offset_relative_to_image = 0;

	result  = 0;
	buf     = segment->buf;
	ubytes  = segment->bufsz;
	mbytes  = segment->memsz;

        /* Address of segment in efi image (ass seen in objdump*/
	maddr   = segment->mem;

        offset_relative_to_image  = maddr - image->raw_image_mem_base;
        raw_image_offset          = ( void* )image->raw_image + offset_relative_to_image;
        DebugMSG( "ubytes = 0x%lx; mbytes = 0x%lx; maddr = 0x%lx; "
                  "offset_relative_to_image = 0x%lx; raw_image_offset = %px",
                  ubytes, mbytes, maddr, offset_relative_to_image, raw_image_offset );
        DumpBuffer( "Segment start", buf, 32 );

	while (mbytes) {
		size_t uchunk, mchunk;

		mchunk = min_t(size_t, mbytes,
				PAGE_SIZE - (maddr & ~PAGE_MASK));
		uchunk = min(ubytes, mchunk);

                result = copy_from_user(raw_image_offset, buf, uchunk);
                DebugMSG( "copied 0x%lx bytes into raw image at 0x%px)",
                          uchunk, raw_image_offset );
	        raw_image_offset += uchunk;

                if (result)
                        return -EFAULT;

		ubytes -= uchunk;
		maddr  += mchunk;
		buf    += mchunk;
		mbytes -= mchunk;
	}

	return result;
}


void kimage_load_pe(struct kimage *image, unsigned long nr_segments)
{
        unsigned long raw_image_relative_start;
        size_t        image_size = 0;
        int           i;

        /* Calculate total image size and allocate it: */
        for (i = 0; i < nr_segments; i++) {
                image_size += image->segment[i].memsz;
        }
        image->raw_image          = vmalloc_exec( image_size );

        /* ImageBase in objdump of efi image */
        image->raw_image_mem_base = image->segment[0].mem;

        raw_image_relative_start  = image->start - image->raw_image_mem_base;
        image->raw_image_start    = (void*)( image->raw_image + raw_image_relative_start );
        DebugMSG(  "image->raw_image = %px; "
                   "image->raw_image_mem_base = 0x%lx; "
                   "image_size = 0x%lx; "
                   "image->raw_image_start = %px\n",
                   image->raw_image,
                   image->raw_image_mem_base,
                   image_size,
                   image->raw_image_start );

        for (i = 0; i < nr_segments; i++) {
                kimage_load_pe_segment(image, &image->segment[i]);
        }
}

/*
 * EFI types definitions: */

typedef struct {
        void*  Reset;

        void*  OutputString;
        void*  TestString;

        void*  QueryMode;
        void*  SetMode;
        void*  SetAttribute;

        void*  ClearScreen;
        void*  SetCursorPosition;
        void*  EnableCursor;

        ///
        /// Pointer to SIMPLE_TEXT_OUTPUT_MODE data.
        ///
        void* Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL ;

typedef void* EFI_HANDLE;

/*
 * Enumeration of memory types introduced in UEFI. */

/* TODO: There are similar definitions in efi.h. This one is taken from EDK-II
 * */
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


/****************** End of EFI types ***********************/

/* Using *char[] is much more elegant, but it is prone to chnages of enum
 * values. Therefore we opted to use switch cases, automatically generated.
 * */
char* get_efi_mem_type_str( int mem_type )
{
        char *description = "<None>";

        switch(mem_type) {
        case EfiReservedMemoryType:
                description = "EfiReservedMemoryType";
                break;
        case EfiLoaderCode:
                description = "EfiLoaderCode";
                break;
        case EfiLoaderData:
                description = "EfiLoaderData";
                break;
        case EfiBootServicesCode:
                description = "EfiBootServicesCode";
                break;
        case EfiBootServicesData:
                description = "EfiBootServicesData";
                break;
        case EfiRuntimeServicesCode:
                description = "EfiRuntimeServicesCode";
                break;
        case EfiRuntimeServicesData:
                description = "EfiRuntimeServicesData";
                break;
        case EfiConventionalMemory:
                description = "EfiConventionalMemory";
                break;
        case EfiUnusableMemory:
                description = "EfiUnusableMemory";
                break;
        case EfiACPIReclaimMemory:
                description = "EfiACPIReclaimMemory";
                break;
        case EfiACPIMemoryNVS:
                description = "EfiACPIMemoryNVS";
                break;
        case EfiMemoryMappedIO:
                description = "EfiMemoryMappedIO";
                break;
        case EfiMemoryMappedIOPortSpace:
                description = "EfiMemoryMappedIOPortSpace";
                break;
        case EfiPalCode:
                description = "EfiPalCode";
                break;
        case EfiPersistentMemory:
                description = "EfiPersistentMemory";
                break;
        case EfiMaxMemoryType:
                description = "EfiMaxMemoryType";
                break;
        }

        return description;
}


__attribute__((ms_abi)) efi_status_t efi_hook_RaiseTPL(void)
{
         DebugMSG( "BOOT SERVICE #0 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_RestoreTPL(void)
{
         DebugMSG( "BOOT SERVICE #1 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_AllocatePages(void)
{
         DebugMSG( "BOOT SERVICE #2 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_FreePages(void)
{
         DebugMSG( "BOOT SERVICE #3 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_GetMemoryMap(void)
{
         DebugMSG( "BOOT SERVICE #4 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_AllocatePool(
                        EFI_MEMORY_TYPE pool_type,
                        unsigned long  size,
                        void           **buffer )
{
        void* allocation = NULL;

        DebugMSG( "pool_type = 0x%x (%s), size = 0x%lx",
                  pool_type, get_efi_mem_type_str( pool_type ), size );

        allocation = kmalloc( size, GFP_KERNEL | GFP_DMA );
        if (allocation == NULL)
                return EFI_OUT_OF_RESOURCES;

        DebugMSG( "Allocated at 0x%px (physical addr: 0x%llx)",
                  allocation, virt_to_phys( allocation ) );

        /* TODO: Create 1:1 virt-to-phys mapping */
        /* TODO: Register memory allocation in some "database". We will need
         * this to create MemoryMap later on */

        /* TODO: later on this will need to be physical addr of allocation */
        *buffer = allocation;

        return EFI_SUCCESS;
}

__attribute__((ms_abi)) efi_status_t efi_hook_FreePool(void* buff)
{
         DebugMSG( "BOOT SERVICE #6 called" );

         /* TODO: We need to do some book keeping for the sake of MemoryMap */
         kfree(buff);

         return EFI_SUCCESS;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CreateEvent(void)
{
         DebugMSG( "BOOT SERVICE #7 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_SetTimer(void)
{
         DebugMSG( "BOOT SERVICE #8 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_WaitForEvent(void)
{
         DebugMSG( "BOOT SERVICE #9 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_SignalEvent(void)
{
         DebugMSG( "BOOT SERVICE #10 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CloseEvent(void)
{
         DebugMSG( "BOOT SERVICE #11 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CheckEvent(void)
{
         DebugMSG( "BOOT SERVICE #12 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_InstallProtocolInterface(void)
{
         DebugMSG( "BOOT SERVICE #13 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_ReinstallProtocolInterface(void)
{
         DebugMSG( "BOOT SERVICE #14 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_UninstallProtocolInterface(void)
{
         DebugMSG( "BOOT SERVICE #15 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_HandleProtocol(void)
{
         DebugMSG( "BOOT SERVICE #16 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_Reserved(void)
{
         DebugMSG( "BOOT SERVICE #17 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_RegisterProtocolNotify(void)
{
         DebugMSG( "BOOT SERVICE #18 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_LocateHandle(void)
{
         DebugMSG( "BOOT SERVICE #19 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_LocateDevicePath(void)
{
         DebugMSG( "BOOT SERVICE #20 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_InstallConfigurationTable(void)
{
         DebugMSG( "BOOT SERVICE #21 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_LoadImage(void)
{
         DebugMSG( "BOOT SERVICE #22 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_StartImage(void)
{
         DebugMSG( "BOOT SERVICE #23 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_Exit(void)
{
         DebugMSG( "BOOT SERVICE #24 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_UnloadImage(void)
{
         DebugMSG( "BOOT SERVICE #25 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_ExitBootServices(void)
{
         DebugMSG( "BOOT SERVICE #26 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_GetNextMonotonicCount(void)
{
         DebugMSG( "BOOT SERVICE #27 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_Stall(void)
{
         DebugMSG( "BOOT SERVICE #28 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_SetWatchdogTimer(void)
{
         DebugMSG( "BOOT SERVICE #29 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_ConnectController(void)
{
         DebugMSG( "BOOT SERVICE #30 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_DisconnectController(void)
{
         DebugMSG( "BOOT SERVICE #31 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_OpenProtocol(void)
{
         DebugMSG( "BOOT SERVICE #32 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CloseProtocol(void)
{
         DebugMSG( "BOOT SERVICE #33 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_OpenProtocolInformation(void)
{
         DebugMSG( "BOOT SERVICE #34 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_ProtocolsPerHandle(void)
{
         DebugMSG( "BOOT SERVICE #35 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_LocateHandleBuffer(void)
{
         DebugMSG( "BOOT SERVICE #36 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_LocateProtocol(void)
{
         DebugMSG( "BOOT SERVICE #37 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_InstallMultipleProtocolInterfaces(void)
{
         DebugMSG( "BOOT SERVICE #38 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_UninstallMultipleProtocolInterfaces(void)
{
         DebugMSG( "BOOT SERVICE #39 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CalculateCrc32(void)
{
         DebugMSG( "BOOT SERVICE #40 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CopyMem(void)
{
         DebugMSG( "BOOT SERVICE #41 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_SetMem(void)
{
         DebugMSG( "BOOT SERVICE #42 called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_hook_CreateEventEx(void)
{
         DebugMSG( "BOOT SERVICE #43 called" );

         return EFI_UNSUPPORTED;
}

efi_system_table_t  fake_systab        = {0};
efi_boot_services_t linux_bootservices = {0};

__attribute__((ms_abi)) efi_status_t efi_conout_hook_Reset(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_OutputString(void* this, char* str)
{
        /* str is CHAR16. We convert it to char* by skipping every 2nd char */

        char str_as_ascii[1024] = {0};
        unsigned int currIdx = 0;
        char c;

        while (currIdx < sizeof( str_as_ascii ))
        {
                c = str[currIdx*2];
                if (c == 0)
                        break;

                str_as_ascii[currIdx++] = c;
        }

        DebugMSG( "output: %s", str_as_ascii );

        return EFI_SUCCESS;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_TestString(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_QueryMode(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_SetMode(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_SetAttribute(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_ClearScreen(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_SetCursorPosition(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

__attribute__((ms_abi)) efi_status_t efi_conout_hook_EnableCursor(void)
{
         DebugMSG( "ConOut was called" );

         return EFI_UNSUPPORTED;
}

EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL con_out = {
        .Reset             = efi_conout_hook_Reset,
        .OutputString      = efi_conout_hook_OutputString,
        .TestString        = efi_conout_hook_TestString,
        .QueryMode         = efi_conout_hook_QueryMode,
        .SetMode           = efi_conout_hook_SetMode,
        .SetAttribute      = efi_conout_hook_SetAttribute,
        .ClearScreen       = efi_conout_hook_ClearScreen,
        .SetCursorPosition = efi_conout_hook_SetCursorPosition,
        .EnableCursor      = efi_conout_hook_EnableCursor,

        .Mode = NULL
};

void* efi_boot_service_hooks[44] = {0};

void initialize_efi_boot_service_hooks(void)
{
        efi_boot_service_hooks[0] = efi_hook_RaiseTPL;
        efi_boot_service_hooks[1] = efi_hook_RestoreTPL;
        efi_boot_service_hooks[2] = efi_hook_AllocatePages;
        efi_boot_service_hooks[3] = efi_hook_FreePages;
        efi_boot_service_hooks[4] = efi_hook_GetMemoryMap;
        efi_boot_service_hooks[5] = efi_hook_AllocatePool;
        efi_boot_service_hooks[6] = efi_hook_FreePool;
        efi_boot_service_hooks[7] = efi_hook_CreateEvent;
        efi_boot_service_hooks[8] = efi_hook_SetTimer;
        efi_boot_service_hooks[9] = efi_hook_WaitForEvent;
        efi_boot_service_hooks[10] = efi_hook_SignalEvent;
        efi_boot_service_hooks[11] = efi_hook_CloseEvent;
        efi_boot_service_hooks[12] = efi_hook_CheckEvent;
        efi_boot_service_hooks[13] = efi_hook_InstallProtocolInterface;
        efi_boot_service_hooks[14] = efi_hook_ReinstallProtocolInterface;
        efi_boot_service_hooks[15] = efi_hook_UninstallProtocolInterface;
        efi_boot_service_hooks[16] = efi_hook_HandleProtocol;
        efi_boot_service_hooks[17] = efi_hook_Reserved;
        efi_boot_service_hooks[18] = efi_hook_RegisterProtocolNotify;
        efi_boot_service_hooks[19] = efi_hook_LocateHandle;
        efi_boot_service_hooks[20] = efi_hook_LocateDevicePath;
        efi_boot_service_hooks[21] = efi_hook_InstallConfigurationTable;
        efi_boot_service_hooks[22] = efi_hook_LoadImage;
        efi_boot_service_hooks[23] = efi_hook_StartImage;
        efi_boot_service_hooks[24] = efi_hook_Exit;
        efi_boot_service_hooks[25] = efi_hook_UnloadImage;
        efi_boot_service_hooks[26] = efi_hook_ExitBootServices;
        efi_boot_service_hooks[27] = efi_hook_GetNextMonotonicCount;
        efi_boot_service_hooks[28] = efi_hook_Stall;
        efi_boot_service_hooks[29] = efi_hook_SetWatchdogTimer;
        efi_boot_service_hooks[30] = efi_hook_ConnectController;
        efi_boot_service_hooks[31] = efi_hook_DisconnectController;
        efi_boot_service_hooks[32] = efi_hook_OpenProtocol;
        efi_boot_service_hooks[33] = efi_hook_CloseProtocol;
        efi_boot_service_hooks[34] = efi_hook_OpenProtocolInformation;
        efi_boot_service_hooks[35] = efi_hook_ProtocolsPerHandle;
        efi_boot_service_hooks[36] = efi_hook_LocateHandleBuffer;
        efi_boot_service_hooks[37] = efi_hook_LocateProtocol;
        efi_boot_service_hooks[38] = efi_hook_InstallMultipleProtocolInterfaces;
        efi_boot_service_hooks[39] = efi_hook_UninstallMultipleProtocolInterfaces;
        efi_boot_service_hooks[40] = efi_hook_CalculateCrc32;
        efi_boot_service_hooks[41] = efi_hook_CopyMem;
        efi_boot_service_hooks[42] = efi_hook_SetMem;
        efi_boot_service_hooks[43] = efi_hook_CreateEventEx;
}

static void hook_boot_services( efi_system_table_t *systab )

{
        efi_boot_services_t *boot_services       = &linux_bootservices;
        void                **bootServiceFuncPtr = NULL;
        int                 boot_service_idx     = 0;
        uint64_t            top_of_bootservices;

        uint64_t            *systab_blob         = (uint64_t *)systab;
        uint64_t            marker               = 0xdeadbeefcafeba00;

        /*
         * Fill boot services table with known incrementing  values
         * This will help debugging when we see RIP or other registers
         * containing theses fixed values */
        while ( (uint8_t*)systab_blob < (uint8_t*)systab + sizeof( *systab ) ) {
                *systab_blob = marker++;
                systab_blob += 1;
        }

        systab->con_in_handle                    = 0xdeadbeefcafebab1;
        systab->con_in                           = 0xdeadbeefcafe0001;
        systab->con_out_handle                   = 0xdeadbeefcafebabe;
        systab->con_out                          = (unsigned long) &con_out;
        systab->stderr_handle                    = 0xdeadbeefcafe0003;
        systab->stderr                           = 0xdeadbeefcafe0004;

        /*
         * We will fill boot_services with actual function pointer, but this is
         * a precaution in case we missed a function pointer in our setup. */
        memset(boot_services, 0x43, sizeof( *boot_services ) );

        initialize_efi_boot_service_hooks();
        bootServiceFuncPtr  = &boot_services->raise_tpl; /* This is the first service */
        top_of_bootservices =
                (uint64_t)boot_services + sizeof( efi_boot_services_t );

        /* Now assign the function poointers: */
        while( (uint64_t)bootServiceFuncPtr < top_of_bootservices ) {
                *bootServiceFuncPtr = efi_boot_service_hooks[boot_service_idx];
                bootServiceFuncPtr += 1;
                boot_service_idx   += 1;
        }

        systab->boottime = boot_services;
}

typedef uint64_t (*EFI_APP_ENTRY)( void* imageHandle, void* systemTable  )
        __attribute__((ms_abi));

void launch_efi_app(EFI_APP_ENTRY efiApp, efi_system_table_t *systab)
{
        EFI_HANDLE ImageHandle = (void*)0xDEADBEEF; /* Obviously fake */

        efiApp( ImageHandle, systab );
}

void kimage_run_pe(struct kimage *image)
{
        EFI_APP_ENTRY efiApp = (EFI_APP_ENTRY)image->raw_image_start;

        /* Print the beginning of the entry point. You can compare this to the
         * objdump output of the EFI app you're running. */
        DumpBuffer( "Entry point:", (uint8_t*) image->raw_image_start, 64 );

        hook_boot_services( &fake_systab );
        efiApp = (EFI_APP_ENTRY)image->raw_image_start;
        launch_efi_app( efiApp, &fake_systab );
}

static int do_kexec_load(unsigned long entry, unsigned long nr_segments,
		struct kexec_segment __user *segments, unsigned long flags)
{
	struct kimage **dest_image, *image;
	unsigned long i;
	int ret;

	if (flags & KEXEC_ON_CRASH) {
		dest_image = &kexec_crash_image;
		if (kexec_crash_image)
			arch_kexec_unprotect_crashkres();
	} else {
		dest_image = &kexec_image;
	}

	if (nr_segments == 0) {
		/* Uninstall image */
		kimage_free(xchg(dest_image, NULL));
		return 0;
	}
	if (flags & KEXEC_ON_CRASH) {
		/*
		 * Loading another kernel to switch to if this one
		 * crashes.  Free any current crash dump kernel before
		 * we corrupt it.
		 */
		kimage_free(xchg(&kexec_crash_image, NULL));
	}

	ret = kimage_alloc_init(&image, entry, nr_segments, segments, flags);
	if (ret)
		return ret;

        if (flags & KEXEC_RUN_PE) {
                kimage_load_pe(image, nr_segments);
                kimage_run_pe(image);

                goto out;
        }

	if (flags & KEXEC_PRESERVE_CONTEXT)
		image->preserve_context = 1;

	ret = machine_kexec_prepare(image);
	if (ret)
		goto out;

	/*
	 * Some architecture(like S390) may touch the crash memory before
	 * machine_kexec_prepare(), we must copy vmcoreinfo data after it.
	 */
	ret = kimage_crash_copy_vmcoreinfo(image);
	if (ret)
		goto out;

	for (i = 0; i < nr_segments; i++) {
		ret = kimage_load_segment(image, &image->segment[i]);
		if (ret)
			goto out;
	}

	kimage_terminate(image);

	/* Install the new kernel and uninstall the old */
	image = xchg(dest_image, image);

out:
	if ((flags & KEXEC_ON_CRASH) && kexec_crash_image)
		arch_kexec_protect_crashkres();

	kimage_free(image);
	return ret;
}

/*
 * Exec Kernel system call: for obvious reasons only root may call it.
 *
 * This call breaks up into three pieces.
 * - A generic part which loads the new kernel from the current
 *   address space, and very carefully places the data in the
 *   allocated pages.
 *
 * - A generic part that interacts with the kernel and tells all of
 *   the devices to shut down.  Preventing on-going dmas, and placing
 *   the devices in a consistent state so a later kernel can
 *   reinitialize them.
 *
 * - A machine specific part that includes the syscall number
 *   and then copies the image to it's final destination.  And
 *   jumps into the image at entry.
 *
 * kexec does not sync, or unmount filesystems so if you need
 * that to happen you need to do that yourself.
 */

static inline int kexec_load_check(unsigned long nr_segments,
				   unsigned long flags)
{
	int result;

	/* We only trust the superuser with rebooting the system. */
	if (!capable(CAP_SYS_BOOT) || kexec_load_disabled)
		return -EPERM;

	/* Permit LSMs and IMA to fail the kexec */
	result = security_kernel_load_data(LOADING_KEXEC_IMAGE);
	if (result < 0)
		return result;

	/*
	 * Verify we have a legal set of flags
	 * This leaves us room for future extensions.
	 */
	if ((flags & KEXEC_FLAGS) != (flags & ~KEXEC_ARCH_MASK))
		return -EINVAL;

	/* Put an artificial cap on the number
	 * of segments passed to kexec_load.
	 */
	if (nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;

	return 0;
}

SYSCALL_DEFINE4(kexec_load, unsigned long, entry, unsigned long, nr_segments,
		struct kexec_segment __user *, segments, unsigned long, flags)
{
	int result;

	result = kexec_load_check(nr_segments, flags);
	if (result)
		return result;

	/* Verify we are on the appropriate architecture */
	if (((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH) &&
		((flags & KEXEC_ARCH_MASK) != KEXEC_ARCH_DEFAULT))
		return -EINVAL;

	/* Because we write directly to the reserved memory
	 * region when loading crash kernels we need a mutex here to
	 * prevent multiple crash  kernels from attempting to load
	 * simultaneously, and to prevent a crash kernel from loading
	 * over the top of a in use crash kernel.
	 *
	 * KISS: always take the mutex.
	 */
	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;

	result = do_kexec_load(entry, nr_segments, segments, flags);

	mutex_unlock(&kexec_mutex);

	return result;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(kexec_load, compat_ulong_t, entry,
		       compat_ulong_t, nr_segments,
		       struct compat_kexec_segment __user *, segments,
		       compat_ulong_t, flags)
{
	struct compat_kexec_segment in;
	struct kexec_segment out, __user *ksegments;
	unsigned long i, result;

	result = kexec_load_check(nr_segments, flags);
	if (result)
		return result;

	/* Don't allow clients that don't understand the native
	 * architecture to do anything.
	 */
	if ((flags & KEXEC_ARCH_MASK) == KEXEC_ARCH_DEFAULT)
		return -EINVAL;

	ksegments = compat_alloc_user_space(nr_segments * sizeof(out));
	for (i = 0; i < nr_segments; i++) {
		result = copy_from_user(&in, &segments[i], sizeof(in));
		if (result)
			return -EFAULT;

		out.buf   = compat_ptr(in.buf);
		out.bufsz = in.bufsz;
		out.mem   = in.mem;
		out.memsz = in.memsz;

		result = copy_to_user(&ksegments[i], &out, sizeof(out));
		if (result)
			return -EFAULT;
	}

	/* Because we write directly to the reserved memory
	 * region when loading crash kernels we need a mutex here to
	 * prevent multiple crash  kernels from attempting to load
	 * simultaneously, and to prevent a crash kernel from loading
	 * over the top of a in use crash kernel.
	 *
	 * KISS: always take the mutex.
	 */
	if (!mutex_trylock(&kexec_mutex))
		return -EBUSY;

	result = do_kexec_load(entry, nr_segments, ksegments, flags);

	mutex_unlock(&kexec_mutex);

	return result;
}
#endif
