/*-
 * Copyright (c) 1998 Michael Smith <msmith@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */


/*
 * i386 fully-qualified device descriptor.
 */
struct i386_devdesc {
    struct devdesc	dd;		/* Must be first. */
    union 
    {
	struct 
	{
	    int		slice;
	    int		partition;
	    off_t	offset;
	} biosdisk;
	struct
	{
	    uint64_t	pool_guid;
	    uint64_t	root_guid;
	} zfs;
    } d_kind;
};

/*
 * relocater trampoline support.
 */
struct relocate_data {
	uint32_t	src;
	uint32_t	dest;
	uint32_t	size;
};

extern void relocater(void);

/*
 * The relocater_data[] is fixed size array allocated in relocater_tramp.S
 */
extern struct relocate_data relocater_data[];
extern uint32_t relocater_size;

extern uint16_t relocator_ip;
extern uint16_t relocator_cs;
extern uint16_t relocator_ds;
extern uint16_t relocator_es;
extern uint16_t relocator_fs;
extern uint16_t relocator_gs;
extern uint16_t relocator_ss;
extern uint16_t relocator_sp;
extern uint32_t relocator_esi;
extern uint32_t relocator_eax;
extern uint32_t relocator_ebx;
extern uint32_t relocator_edx;
extern uint32_t relocator_ebp;
extern uint16_t relocator_a20_enabled;

int	i386_getdev(void **vdev, const char *devspec, const char **path);
char	*i386_fmtdev(void *vdev);
int	i386_setcurrdev(struct env_var *ev, int flags, const void *value);

extern struct devdesc	currdev;	/* our current device */

#define MAXDEV		31		/* maximum number of distinct devices */
#define MAXBDDEV	MAXDEV

/* exported devices XXX rename? */
extern struct devsw bioscd;
extern struct devsw biosfd;
extern struct devsw bioshd;
extern struct devsw pxedisk;
extern struct fs_ops pxe_fsops;

int	bc_add(int biosdev);		/* Register CD booted from. */
uint32_t bd_getbigeom(int bunit);	/* return geometry in bootinfo format */
int	bd_bios2unit(int biosdev);	/* xlate BIOS device -> biosdisk unit */
int	bd_unit2bios(struct i386_devdesc *); /* xlate biosdisk -> BIOS device */
int	bd_getdev(struct i386_devdesc *dev);	/* return dev_t for (dev) */

ssize_t	i386_copyin(const void *src, vm_offset_t dest, const size_t len);
ssize_t	i386_copyout(const vm_offset_t src, void *dest, const size_t len);
ssize_t	i386_readin(const int fd, vm_offset_t dest, const size_t len);

struct preloaded_file;
void	bios_addsmapdata(struct preloaded_file *);
void	bios_getsmap(void);

void	bios_getmem(void);
extern uint32_t		bios_basemem;	/* base memory in bytes */
extern uint32_t		bios_extmem;	/* extended memory in bytes */
extern vm_offset_t	memtop;		/* last address of physical memory + 1 */
extern vm_offset_t	memtop_copyin;	/* memtop less heap size for the cases */
					/*  when heap is at the top of         */
					/*  extended memory; for other cases   */
					/*  just the same as memtop            */
extern uint32_t		high_heap_size;	/* extended memory region available */
extern vm_offset_t	high_heap_base;	/* for use as the heap */

/* 16KB buffer space for real mode data transfers. */
#define	BIO_BUFFER_SIZE 0x4000
void *bio_alloc(size_t size);
void bio_free(void *ptr, size_t size);

/*
 * Values for width parameter to biospci_{read,write}_config
 */
#define BIOSPCI_8BITS	0
#define BIOSPCI_16BITS	1
#define BIOSPCI_32BITS	2

void	biospci_detect(void);
int	biospci_find_devclass(uint32_t class, int index, uint32_t *locator);
int	biospci_read_config(uint32_t locator, int offset, int width, uint32_t *val);
uint32_t biospci_locator(int8_t bus, uint8_t device, uint8_t function);
int	biospci_write_config(uint32_t locator, int offset, int width, uint32_t val);

void	biosacpi_detect(void);

int	i386_autoload(void);

int	bi_getboothowto(char *kargs);
void	bi_setboothowto(int howto);
vm_offset_t	bi_copyenv(vm_offset_t addr);
int	bi_load32(char *args, int *howtop, int *bootdevp, vm_offset_t *bip,
	    vm_offset_t *modulep, vm_offset_t *kernend);
int	bi_load64(char *args, vm_offset_t addr, vm_offset_t *modulep,
	    vm_offset_t *kernend, int add_smap);

void	pxe_enable(void *pxeinfo);
