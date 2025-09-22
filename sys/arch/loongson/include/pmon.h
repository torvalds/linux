/*	$OpenBSD: pmon.h,v 1.5 2017/05/21 13:00:53 visa Exp $	*/

/*
 * Copyright (c) 2009, 2012 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_MACHINE_PMON_H_
#define	_MACHINE_PMON_H_

#if defined(_KERNEL) || defined(_STANDALONE)

/*
 * PMON2000 callvec definitions
 */

/* 32-bit compatible types */
typedef	uint32_t	pmon_size_t;
typedef	int32_t		pmon_ssize_t;
typedef int64_t		pmon_off_t;

int		pmon_open(const char *, int, ...);
int		pmon_close(int);
int		pmon_read(int, void *, pmon_size_t);
pmon_ssize_t	pmon_write(int, const void *, pmon_size_t);
pmon_off_t	pmon_lseek(int, pmon_off_t, int);
int		pmon_printf(const char *, ...);
void		pmon_cacheflush(void);
char *		pmon_gets(char *);

#define	PMON_MAXLN	256	/* internal gets() size limit */

extern int32_t pmon_callvec;

const char	*pmon_getarg(const int);
void		 pmon_init(int32_t, int32_t, int32_t, int32_t, uint32_t);

#define	PMON_ENVTYPE_ENVP	0
#define	PMON_ENVTYPE_EFI	1
int		 pmon_getenvtype(void);

/*
 * The new environment interface is a /salmigondis/ of badly thought-out
 * structs put together, pretending to be inspired by EFI but conveniently
 * omitting key EFI structs because they are deemed non-applicable to
 * MIPS systems.
 * Of course, some fields are absolute addresses, while others are relative
 * pointers, to add to the confusion.
 */

struct pmon_env_reset {
	void			(*cold_boot)(void);	/* not filled */
	void			(*warm_boot)(void);
	void			(*boot)(unsigned int);	/* not filled */
	void			(*poweroff)(void);
};

/* all values are offsets relative to the beginning of the struct */
struct pmon_env_ptr {
	uint64_t		offs_mem;
	uint64_t		offs_cpu;
	uint64_t		offs_sys;
	uint64_t		offs_irq;
	uint64_t		offs_iface;
	uint64_t		offs_special;
	uint64_t		offs_device;
};

struct pmon_env_smbios {
	uint16_t		version;
	uint64_t		vga_bios;
	struct pmon_env_ptr	ptrs;
};

struct pmon_env_efi {
	uint64_t		mps;		/* not filled */
	uint64_t		acpi;		/* not filled */
	uint64_t		acpi20;		/* not filled */
	struct pmon_env_smbios	bios;
	uint64_t		sal_systab;	/* not filled */
	uint64_t		bootinfo;	/* not filled */
};

struct pmon_env {
	struct pmon_env_efi	efi;
	struct pmon_env_reset	reset;
};

#define	PMON_MEM_MAX	128
struct pmon_env_mem_entry {
	uint32_t		node;
	uint32_t		type;
#define	PMON_MEM_SYSTEM_LOW	1	/* physical memory <= 256 MB */
#define	PMON_MEM_SYSTEM_HIGH	2	/* physical memory > 256 MB */
#define	PMON_MEM_RESERVED	3
#define	PMON_MEM_PCI_IO		4
#define	PMON_MEM_PCI_MEM	5
#define	PMON_MEM_CPU_REGISTERS	6
#define	PMON_MEM_VIDEO_ROM	7
#define	PMON_MEM_OTHER_ROM	8
#define	PMON_MEM_ACPI_TABLE	9
	uint64_t		address;
	uint32_t		size;
};
struct pmon_env_mem {
	uint16_t		version;	/* not filled */
	uint32_t		nentries;
	uint32_t		mem_freq;
	struct pmon_env_mem_entry mem_map[PMON_MEM_MAX];
} __packed;

struct pmon_env_cpu {
	uint16_t		version;	/* not filled */
	uint32_t		prid;		/* cop0 PrID */
	uint32_t		cputype;
/* 0 and 1 are supposedly intended for 2E and 2F, which do NOT provide this
   interface; moreover Linux and PMON disagree on the values and have 2E and
   2F swapped. */
#define	PMON_CPUTYPE_LS3A	2
#define	PMON_CPUTYPE_LS3B	3
#define	PMON_CPUTYPE_LS1A	4
#define	PMON_CPUTYPE_LS1B	5
	uint32_t		node;		/* total number of NUMA nodes */
	uint16_t		coreid;		/* boot CPU core id */
	uint16_t		reserved_cores;	/* mask of reserved cores */
	uint32_t		speed;
	uint32_t		ncpus;
} __packed;

struct pmon_env_sys {
	uint16_t		version;	/* not filled */
	uint32_t		ccnuma_smp;
	uint32_t		double_channel;
} __packed;

struct pmon_env_irq {
	uint16_t		version;	/* not filled */
	uint16_t		size;		/* not filled */
	uint16_t		rtr_bus;	/* not filled */
	uint16_t		rtr_devfn;	/* not filled */
	uint32_t		vendor;		/* not filled */
	uint32_t		device;		/* not filled */
	uint32_t		pic_type;
#define	PMON_IRQ_PIC_HT		0
#define	PMON_IRQ_PIC_I8259	1
	uint64_t		ht_interrupt_bit;
	uint64_t		ht_enable;
	uint32_t		node;
	uint64_t		pci_memory_space_start;
	uint64_t		pci_memory_space_end;
	uint64_t		pci_io_space_start;	/* not filled */
	uint64_t		pci_io_space_end;	/* not filled */
	uint64_t		pci_cfg_space;	/* not filled */
} __packed;

struct pmon_env_iface {
	uint16_t		version;
#define	PMON_IFACE_VERSION	0x0001
	uint16_t		size;
	uint8_t			flag;
	char			description[64];	/* firmware version */
} __packed;

#define PMON_RESOURCE_MAX 128
struct pmon_env_resource {
	uint64_t		start;
	uint64_t		end;
	char			name[64];
	uint32_t		flags;
};
struct pmon_env_special {
	uint16_t		version;
	char			name[64];
	uint32_t		type;
	struct pmon_env_resource resource[PMON_RESOURCE_MAX];
};

struct pmon_env_device {
	char			name[64];	/* system description */
	uint32_t		nentries;
	struct pmon_env_resource resource[PMON_RESOURCE_MAX];
};

const char	*pmon_getenv(const char *);

const struct pmon_env_reset *pmon_get_env_reset(void);
const struct pmon_env_smbios *pmon_get_env_smbios(void);
const struct pmon_env_mem *pmon_get_env_mem(void);
const struct pmon_env_cpu *pmon_get_env_cpu(void);
const struct pmon_env_sys *pmon_get_env_sys(void);
const struct pmon_env_irq *pmon_get_env_irq(void);
const struct pmon_env_iface *pmon_get_env_iface(void);
const struct pmon_env_special *pmon_get_env_special(void);
const struct pmon_env_device *pmon_get_env_device(void);

#endif	/* _KERNEL || _STANDALONE */

#endif	/* _MACHINE_PMON_H_ */
