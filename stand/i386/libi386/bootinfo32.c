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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/linker.h>
#include <machine/bootinfo.h>
#include <machine/metadata.h>
#include "bootstrap.h"
#include "libi386.h"
#include "btxv86.h"

#ifdef LOADER_GELI_SUPPORT
#include "geliboot.h"
#endif

static struct bootinfo  bi;

/*
 * Copy module-related data into the load area, where it can be
 * used as a directory for loaded modules.
 *
 * Module data is presented in a self-describing format.  Each datum
 * is preceded by a 32-bit identifier and a 32-bit size field.
 *
 * Currently, the following data are saved:
 *
 * MOD_NAME	(variable)		module name (string)
 * MOD_TYPE	(variable)		module type (string)
 * MOD_ARGS	(variable)		module parameters (string)
 * MOD_ADDR	sizeof(vm_offset_t)	module load address
 * MOD_SIZE	sizeof(size_t)		module size
 * MOD_METADATA	(variable)		type-specific metadata
 */
#define COPY32(v, a, c) {			\
    uint32_t	x = (v);			\
    if (c)					\
	i386_copyin(&x, a, sizeof(x));		\
    a += sizeof(x);				\
}

#define MOD_STR(t, a, s, c) {			\
    COPY32(t, a, c);				\
    COPY32(strlen(s) + 1, a, c);		\
    if (c)					\
	i386_copyin(s, a, strlen(s) + 1);	\
    a += roundup(strlen(s) + 1, sizeof(u_long));\
}

#define MOD_NAME(a, s, c)	MOD_STR(MODINFO_NAME, a, s, c)
#define MOD_TYPE(a, s, c)	MOD_STR(MODINFO_TYPE, a, s, c)
#define MOD_ARGS(a, s, c)	MOD_STR(MODINFO_ARGS, a, s, c)

#define MOD_VAR(t, a, s, c) {			\
    COPY32(t, a, c);				\
    COPY32(sizeof(s), a, c);			\
    if (c)					\
	i386_copyin(&s, a, sizeof(s));		\
    a += roundup(sizeof(s), sizeof(u_long));	\
}

#define MOD_ADDR(a, s, c)	MOD_VAR(MODINFO_ADDR, a, s, c)
#define MOD_SIZE(a, s, c)	MOD_VAR(MODINFO_SIZE, a, s, c)

#define MOD_METADATA(a, mm, c) {		\
    COPY32(MODINFO_METADATA | mm->md_type, a, c); \
    COPY32(mm->md_size, a, c);			\
    if (c)					\
	i386_copyin(mm->md_data, a, mm->md_size); \
    a += roundup(mm->md_size, sizeof(u_long));\
}

#define MOD_END(a, c) {				\
    COPY32(MODINFO_END, a, c);			\
    COPY32(0, a, c);				\
}

static vm_offset_t
bi_copymodules32(vm_offset_t addr)
{
    struct preloaded_file	*fp;
    struct file_metadata	*md;
    int				c;

    c = addr != 0;
    /* start with the first module on the list, should be the kernel */
    for (fp = file_findfile(NULL, NULL); fp != NULL; fp = fp->f_next) {

	MOD_NAME(addr, fp->f_name, c);	/* this field must come first */
	MOD_TYPE(addr, fp->f_type, c);
	if (fp->f_args)
	    MOD_ARGS(addr, fp->f_args, c);
	MOD_ADDR(addr, fp->f_addr, c);
	MOD_SIZE(addr, fp->f_size, c);
	for (md = fp->f_metadata; md != NULL; md = md->md_next)
	    if (!(md->md_type & MODINFOMD_NOCOPY))
		MOD_METADATA(addr, md, c);
    }
    MOD_END(addr, c);
    return(addr);
}

/*
 * Load the information expected by an i386 kernel.
 *
 * - The 'boothowto' argument is constructed
 * - The 'bootdev' argument is constructed
 * - The 'bootinfo' struct is constructed, and copied into the kernel space.
 * - The kernel environment is copied into kernel space.
 * - Module metadata are formatted and placed in kernel space.
 */
int
bi_load32(char *args, int *howtop, int *bootdevp, vm_offset_t *bip, vm_offset_t *modulep, vm_offset_t *kernendp)
{
    struct preloaded_file	*xp, *kfp;
    struct i386_devdesc		*rootdev;
    struct file_metadata	*md;
    vm_offset_t			addr;
    vm_offset_t			kernend;
    vm_offset_t			envp;
    vm_offset_t			size;
    vm_offset_t			ssym, esym;
    char			*rootdevname;
    int				bootdevnr, i, howto;
    char			*kernelname;
    const char			*kernelpath;

    howto = bi_getboothowto(args);

    /*
     * Allow the environment variable 'rootdev' to override the supplied device
     * This should perhaps go to MI code and/or have $rootdev tested/set by
     * MI code before launching the kernel.
     */
    rootdevname = getenv("rootdev");
    i386_getdev((void **)(&rootdev), rootdevname, NULL);
    if (rootdev == NULL) {		/* bad $rootdev/$currdev */
	printf("can't determine root device\n");
	return(EINVAL);
    }

    /* Try reading the /etc/fstab file to select the root device */
    getrootmount(i386_fmtdev((void *)rootdev));

    /* Do legacy rootdev guessing */

    /* XXX - use a default bootdev of 0.  Is this ok??? */
    bootdevnr = 0;

    switch(rootdev->dd.d_dev->dv_type) {
    case DEVT_CD:
    case DEVT_DISK:
	/* pass in the BIOS device number of the current disk */
	bi.bi_bios_dev = bd_unit2bios(rootdev);
	bootdevnr = bd_getdev(rootdev);
	break;

    case DEVT_NET:
    case DEVT_ZFS:
	    break;

    default:
	printf("WARNING - don't know how to boot from device type %d\n",
	    rootdev->dd.d_dev->dv_type);
    }
    if (bootdevnr == -1) {
	printf("root device %s invalid\n", i386_fmtdev(rootdev));
	return (EINVAL);
    }
    free(rootdev);

    /* find the last module in the chain */
    addr = 0;
    for (xp = file_findfile(NULL, NULL); xp != NULL; xp = xp->f_next) {
	if (addr < (xp->f_addr + xp->f_size))
	    addr = xp->f_addr + xp->f_size;
    }
    /* pad to a page boundary */
    addr = roundup(addr, PAGE_SIZE);

    /* copy our environment */
    envp = addr;
    addr = bi_copyenv(addr);

    /* pad to a page boundary */
    addr = roundup(addr, PAGE_SIZE);

    kfp = file_findfile(NULL, "elf kernel");
    if (kfp == NULL)
      kfp = file_findfile(NULL, "elf32 kernel");
    if (kfp == NULL)
	panic("can't find kernel file");
    kernend = 0;	/* fill it in later */
    file_addmetadata(kfp, MODINFOMD_HOWTO, sizeof howto, &howto);
    file_addmetadata(kfp, MODINFOMD_ENVP, sizeof envp, &envp);
    file_addmetadata(kfp, MODINFOMD_KERNEND, sizeof kernend, &kernend);
    bios_addsmapdata(kfp);
#ifdef LOADER_GELI_SUPPORT
    geli_export_key_metadata(kfp);
#endif

    /* Figure out the size and location of the metadata */
    *modulep = addr;
    size = bi_copymodules32(0);
    kernend = roundup(addr + size, PAGE_SIZE);
    *kernendp = kernend;

    /* patch MODINFOMD_KERNEND */
    md = file_findmetadata(kfp, MODINFOMD_KERNEND);
    bcopy(&kernend, md->md_data, sizeof kernend);

    /* copy module list and metadata */
    (void)bi_copymodules32(addr);

    ssym = esym = 0;
    md = file_findmetadata(kfp, MODINFOMD_SSYM);
    if (md != NULL)
	ssym = *((vm_offset_t *)&(md->md_data));
    md = file_findmetadata(kfp, MODINFOMD_ESYM);
    if (md != NULL)
	esym = *((vm_offset_t *)&(md->md_data));
    if (ssym == 0 || esym == 0)
	ssym = esym = 0;		/* sanity */

    /* legacy bootinfo structure */
    kernelname = getenv("kernelname");
    i386_getdev(NULL, kernelname, &kernelpath);
    bi.bi_version = BOOTINFO_VERSION;
    bi.bi_kernelname = 0;		/* XXX char * -> kernel name */
    bi.bi_nfs_diskless = 0;		/* struct nfs_diskless * */
    bi.bi_n_bios_used = 0;		/* XXX would have to hook biosdisk driver for these */
    for (i = 0; i < N_BIOS_GEOM; i++)
        bi.bi_bios_geom[i] = bd_getbigeom(i);
    bi.bi_size = sizeof(bi);
    bi.bi_memsizes_valid = 1;
    bi.bi_basemem = bios_basemem / 1024;
    bi.bi_extmem = bios_extmem / 1024;
    bi.bi_envp = envp;
    bi.bi_modulep = *modulep;
    bi.bi_kernend = kernend;
    bi.bi_kernelname = VTOP(kernelpath);
    bi.bi_symtab = ssym;       /* XXX this is only the primary kernel symtab */
    bi.bi_esymtab = esym;

    /* legacy boot arguments */
    *howtop = howto | RB_BOOTINFO;
    *bootdevp = bootdevnr;
    *bip = VTOP(&bi);

    return(0);
}
