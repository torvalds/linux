/*-
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SYS_EFI_H_
#define _SYS_EFI_H_

#include <sys/uuid.h>
#include <machine/efi.h>

#define	EFI_PAGE_SHIFT		12
#define	EFI_PAGE_SIZE		(1 << EFI_PAGE_SHIFT)
#define	EFI_PAGE_MASK		(EFI_PAGE_SIZE - 1)

#define	EFI_TABLE_ACPI20			\
	{0x8868e871,0xe4f1,0x11d3,0xbc,0x22,{0x00,0x80,0xc7,0x3c,0x88,0x81}}
#define	EFI_TABLE_SAL				\
	{0xeb9d2d32,0x2d88,0x11d3,0x9a,0x16,{0x00,0x90,0x27,0x3f,0xc1,0x4d}}

enum efi_reset {
	EFI_RESET_COLD = 0,
	EFI_RESET_WARM = 1,
	EFI_RESET_SHUTDOWN = 2,
};

typedef uint16_t	efi_char;
typedef unsigned long efi_status;

struct efi_cfgtbl {
	struct uuid	ct_uuid;
	uint64_t	ct_data;
};

struct efi_md {
	uint32_t	md_type;
#define	EFI_MD_TYPE_NULL	0
#define	EFI_MD_TYPE_CODE	1	/* Loader text. */
#define	EFI_MD_TYPE_DATA	2	/* Loader data. */
#define	EFI_MD_TYPE_BS_CODE	3	/* Boot services text. */
#define	EFI_MD_TYPE_BS_DATA	4	/* Boot services data. */
#define	EFI_MD_TYPE_RT_CODE	5	/* Runtime services text. */
#define	EFI_MD_TYPE_RT_DATA	6	/* Runtime services data. */
#define	EFI_MD_TYPE_FREE	7	/* Unused/free memory. */
#define	EFI_MD_TYPE_BAD		8	/* Bad memory */
#define	EFI_MD_TYPE_RECLAIM	9	/* ACPI reclaimable memory. */
#define	EFI_MD_TYPE_FIRMWARE	10	/* ACPI NV memory */
#define	EFI_MD_TYPE_IOMEM	11	/* Memory-mapped I/O. */
#define	EFI_MD_TYPE_IOPORT	12	/* I/O port space. */
#define	EFI_MD_TYPE_PALCODE	13	/* PAL */
#define	EFI_MD_TYPE_PERSISTENT	14	/* Persistent memory. */
	uint32_t	__pad;
	uint64_t	md_phys;
	void		*md_virt;
	uint64_t	md_pages;
	uint64_t	md_attr;
#define	EFI_MD_ATTR_UC		0x0000000000000001UL
#define	EFI_MD_ATTR_WC		0x0000000000000002UL
#define	EFI_MD_ATTR_WT		0x0000000000000004UL
#define	EFI_MD_ATTR_WB		0x0000000000000008UL
#define	EFI_MD_ATTR_UCE		0x0000000000000010UL
#define	EFI_MD_ATTR_WP		0x0000000000001000UL
#define	EFI_MD_ATTR_RP		0x0000000000002000UL
#define	EFI_MD_ATTR_XP		0x0000000000004000UL
#define	EFI_MD_ATTR_NV		0x0000000000008000UL
#define	EFI_MD_ATTR_MORE_RELIABLE \
				0x0000000000010000UL
#define	EFI_MD_ATTR_RO		0x0000000000020000UL
#define	EFI_MD_ATTR_RT		0x8000000000000000UL
};

#define efi_next_descriptor(ptr, size) \
    ((struct efi_md *)(((uint8_t *)(ptr)) + (size)))

struct efi_tm {
	uint16_t	tm_year;		/* 1998 - 20XX */
	uint8_t		tm_mon;			/* 1 - 12 */
	uint8_t		tm_mday;		/* 1 - 31 */
	uint8_t		tm_hour;		/* 0 - 23 */
	uint8_t		tm_min;			/* 0 - 59 */
	uint8_t		tm_sec;			/* 0 - 59 */
	uint8_t		__pad1;
	uint32_t	tm_nsec;		/* 0 - 999,999,999 */
	int16_t		tm_tz;			/* -1440 to 1440 or 2047 */
	uint8_t		tm_dst;
	uint8_t		__pad2;
};

struct efi_tmcap {
	uint32_t	tc_res;		/* 1e-6 parts per million */
	uint32_t	tc_prec;	/* hertz */
	uint8_t		tc_stz;		/* Set clears sub-second time */
};

struct efi_tblhdr {
	uint64_t	th_sig;
	uint32_t	th_rev;
	uint32_t	th_hdrsz;
	uint32_t	th_crc32;
	uint32_t	__res;
};

#ifdef _KERNEL

#ifdef EFIABI_ATTR
struct efi_rt {
	struct efi_tblhdr rt_hdr;
	efi_status	(*rt_gettime)(struct efi_tm *, struct efi_tmcap *)
	    EFIABI_ATTR;
	efi_status	(*rt_settime)(struct efi_tm *) EFIABI_ATTR;
	efi_status	(*rt_getwaketime)(uint8_t *, uint8_t *,
	    struct efi_tm *) EFIABI_ATTR;
	efi_status	(*rt_setwaketime)(uint8_t, struct efi_tm *)
	    EFIABI_ATTR;
	efi_status	(*rt_setvirtual)(u_long, u_long, uint32_t,
	    struct efi_md *) EFIABI_ATTR;
	efi_status	(*rt_cvtptr)(u_long, void **) EFIABI_ATTR;
	efi_status	(*rt_getvar)(efi_char *, struct uuid *, uint32_t *,
	    u_long *, void *) EFIABI_ATTR;
	efi_status	(*rt_scanvar)(u_long *, efi_char *, struct uuid *)
	    EFIABI_ATTR;
	efi_status	(*rt_setvar)(efi_char *, struct uuid *, uint32_t,
	    u_long, void *) EFIABI_ATTR;
	efi_status	(*rt_gethicnt)(uint32_t *) EFIABI_ATTR;
	efi_status	(*rt_reset)(enum efi_reset, efi_status, u_long,
	    efi_char *) EFIABI_ATTR;
};
#endif

struct efi_systbl {
	struct efi_tblhdr st_hdr;
#define	EFI_SYSTBL_SIG	0x5453595320494249UL
	efi_char	*st_fwvendor;
	uint32_t	st_fwrev;
	uint32_t	__pad;
	void		*st_cin;
	void		*st_cinif;
	void		*st_cout;
	void		*st_coutif;
	void		*st_cerr;
	void		*st_cerrif;
	uint64_t	st_rt;
	void		*st_bs;
	u_long		st_entries;
	uint64_t	st_cfgtbl;
};

extern vm_paddr_t efi_systbl_phys;

struct efirt_callinfo;

/* Internal MD EFI functions */
int efi_arch_enter(void);
void efi_arch_leave(void);
vm_offset_t efi_phys_to_kva(vm_paddr_t);
int efi_rt_arch_call(struct efirt_callinfo *);
bool efi_create_1t1_map(struct efi_md *, int, int);
void efi_destroy_1t1_map(void);

/* Public MI EFI functions */
int efi_rt_ok(void);
int efi_get_table(struct uuid *uuid, void **ptr);
int efi_get_time(struct efi_tm *tm);
int efi_get_time_capabilities(struct efi_tmcap *tmcap);
int efi_reset_system(enum efi_reset type);
int efi_set_time(struct efi_tm *tm);
int efi_var_get(uint16_t *name, struct uuid *vendor, uint32_t *attrib,
    size_t *datasize, void *data);
int efi_var_nextname(size_t *namesize, uint16_t *name, struct uuid *vendor);
int efi_var_set(uint16_t *name, struct uuid *vendor, uint32_t attrib,
    size_t datasize, void *data);

#endif	/* _KERNEL */

#endif /* _SYS_EFI_H_ */
