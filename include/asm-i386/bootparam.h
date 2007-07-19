#ifndef _ASM_BOOTPARAM_H
#define _ASM_BOOTPARAM_H

#include <linux/types.h>
#include <linux/screen_info.h>
#include <linux/apm_bios.h>
#include <asm/e820.h>
#include <linux/edd.h>
#include <video/edid.h>

struct setup_header {
	u8	setup_sects;
	u16	root_flags;
	u32	syssize;
	u16	ram_size;
	u16	vid_mode;
	u16	root_dev;
	u16	boot_flag;
	u16	jump;
	u32	header;
	u16	version;
	u32	realmode_swtch;
	u16	start_sys;
	u16	kernel_version;
	u8	type_of_loader;
	u8	loadflags;
#define LOADED_HIGH	0x01
#define CAN_USE_HEAP	0x80
	u16	setup_move_size;
	u32	code32_start;
	u32	ramdisk_image;
	u32	ramdisk_size;
	u32	bootsect_kludge;
	u16	heap_end_ptr;
	u16	_pad1;
	u32	cmd_line_ptr;
	u32	initrd_addr_max;
	u32	kernel_alignment;
	u8	relocatable_kernel;
} __attribute__((packed));

struct sys_desc_table {
	u16 length;
	u8  table[14];
};

struct efi_info {
	u32 _pad1;
	u32 efi_systab;
	u32 efi_memdesc_size;
	u32 efi_memdesc_version;
	u32 efi_memmap;
	u32 efi_memmap_size;
	u32 _pad2[2];
};

/* The so-called "zeropage" */
struct boot_params {
	struct screen_info screen_info;			/* 0x000 */
	struct apm_bios_info apm_bios_info;		/* 0x040 */
	u8  _pad2[12];					/* 0x054 */
	u32 speedstep_info[4];				/* 0x060 */
	u8  _pad3[16];					/* 0x070 */
	u8  hd0_info[16];	/* obsolete! */		/* 0x080 */
	u8  hd1_info[16];	/* obsolete! */		/* 0x090 */
	struct sys_desc_table sys_desc_table;		/* 0x0a0 */
	u8  _pad4[144];					/* 0x0b0 */
	struct edid_info edid_info;			/* 0x140 */
	struct efi_info efi_info;			/* 0x1c0 */
	u32 alt_mem_k;					/* 0x1e0 */
	u32 scratch;		/* Scratch field! */	/* 0x1e4 */
	u8  e820_entries;				/* 0x1e8 */
	u8  eddbuf_entries;				/* 0x1e9 */
	u8  edd_mbr_sig_buf_entries;			/* 0x1ea */
	u8  _pad6[6];					/* 0x1eb */
	struct setup_header hdr;    /* setup header */	/* 0x1f1 */
	u8  _pad7[0x290-0x1f1-sizeof(struct setup_header)];
	u32 edd_mbr_sig_buffer[EDD_MBR_SIG_MAX];	/* 0x290 */
	struct e820entry e820_map[E820MAX];		/* 0x2d0 */
	u8  _pad8[48];					/* 0xcd0 */
	struct edd_info eddbuf[EDDMAXNR];		/* 0xd00 */
	u8  _pad9[276];					/* 0xeec */
} __attribute__((packed));

#endif /* _ASM_BOOTPARAM_H */
