/*-
 * Copyright (c) 2017, 2018 The FreeBSD Foundation
 * All rights reserved.
 * Copyright (c) 2018, 2019 Intel Corporation
 *
 * This software was developed by Konstantin Belousov <kib@FreeBSD.org>
 * under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include "opt_acpi.h"
#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/devicestat.h>
#include <sys/disk.h>
#include <sys/efi.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/limits.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rwlock.h>
#include <sys/sglist.h>
#include <sys/uio.h>
#include <sys/uuid.h>
#include <geom/geom.h>
#include <geom/geom_int.h>
#include <machine/vmparam.h>
#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <contrib/dev/acpica/include/acpi.h>
#include <contrib/dev/acpica/include/accommon.h>
#include <contrib/dev/acpica/include/acuuid.h>
#include <dev/acpica/acpivar.h>
#include <dev/nvdimm/nvdimm_var.h>

#define UUID_INITIALIZER_VOLATILE_MEMORY \
    {0x7305944f,0xfdda,0x44e3,0xb1,0x6c,{0x3f,0x22,0xd2,0x52,0xe5,0xd0}}
#define UUID_INITIALIZER_PERSISTENT_MEMORY \
    {0x66f0d379,0xb4f3,0x4074,0xac,0x43,{0x0d,0x33,0x18,0xb7,0x8c,0xdb}}
#define UUID_INITIALIZER_CONTROL_REGION \
    {0x92f701f6,0x13b4,0x405d,0x91,0x0b,{0x29,0x93,0x67,0xe8,0x23,0x4c}}
#define UUID_INITIALIZER_DATA_REGION \
    {0x91af0530,0x5d86,0x470e,0xa6,0xb0,{0x0a,0x2d,0xb9,0x40,0x82,0x49}}
#define UUID_INITIALIZER_VOLATILE_VIRTUAL_DISK \
    {0x77ab535a,0x45fc,0x624b,0x55,0x60,{0xf7,0xb2,0x81,0xd1,0xf9,0x6e}}
#define UUID_INITIALIZER_VOLATILE_VIRTUAL_CD \
    {0x3d5abd30,0x4175,0x87ce,0x6d,0x64,{0xd2,0xad,0xe5,0x23,0xc4,0xbb}}
#define UUID_INITIALIZER_PERSISTENT_VIRTUAL_DISK \
    {0x5cea02c9,0x4d07,0x69d3,0x26,0x9f,{0x44,0x96,0xfb,0xe0,0x96,0xf9}}
#define UUID_INITIALIZER_PERSISTENT_VIRTUAL_CD \
    {0x08018188,0x42cd,0xbb48,0x10,0x0f,{0x53,0x87,0xd5,0x3d,0xed,0x3d}}

static struct nvdimm_SPA_uuid_list_elm {
	const char		*u_name;
	struct uuid		u_id;
	const bool		u_usr_acc;
} nvdimm_SPA_uuid_list[] = {
	[SPA_TYPE_VOLATILE_MEMORY] = {
		.u_name =	"VOLA MEM ",
		.u_id =		UUID_INITIALIZER_VOLATILE_MEMORY,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_PERSISTENT_MEMORY] = {
		.u_name =	"PERS MEM",
		.u_id =		UUID_INITIALIZER_PERSISTENT_MEMORY,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_CONTROL_REGION] = {
		.u_name =	"CTRL RG ",
		.u_id =		UUID_INITIALIZER_CONTROL_REGION,
		.u_usr_acc =	false,
	},
	[SPA_TYPE_DATA_REGION] = {
		.u_name =	"DATA RG ",
		.u_id =		UUID_INITIALIZER_DATA_REGION,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_VOLATILE_VIRTUAL_DISK] = {
		.u_name =	"VIRT DSK",
		.u_id =		UUID_INITIALIZER_VOLATILE_VIRTUAL_DISK,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_VOLATILE_VIRTUAL_CD] = {
		.u_name =	"VIRT CD ",
		.u_id =		UUID_INITIALIZER_VOLATILE_VIRTUAL_CD,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_PERSISTENT_VIRTUAL_DISK] = {
		.u_name =	"PV DSK  ",
		.u_id =		UUID_INITIALIZER_PERSISTENT_VIRTUAL_DISK,
		.u_usr_acc =	true,
	},
	[SPA_TYPE_PERSISTENT_VIRTUAL_CD] = {
		.u_name =	"PV CD   ",
		.u_id =		UUID_INITIALIZER_PERSISTENT_VIRTUAL_CD,
		.u_usr_acc =	true,
	},
};

enum SPA_mapping_type
nvdimm_spa_type_from_uuid(struct uuid *uuid)
{
	int j;

	for (j = 0; j < nitems(nvdimm_SPA_uuid_list); j++) {
		if (uuidcmp(uuid, &nvdimm_SPA_uuid_list[j].u_id) != 0)
			continue;
		return (j);
	}
	return (SPA_TYPE_UNKNOWN);
}

static vm_memattr_t
nvdimm_spa_memattr(struct nvdimm_spa_dev *dev)
{
	vm_memattr_t mode;

	if ((dev->spa_efi_mem_flags & EFI_MD_ATTR_WB) != 0)
		mode = VM_MEMATTR_WRITE_BACK;
	else if ((dev->spa_efi_mem_flags & EFI_MD_ATTR_WT) != 0)
		mode = VM_MEMATTR_WRITE_THROUGH;
	else if ((dev->spa_efi_mem_flags & EFI_MD_ATTR_WC) != 0)
		mode = VM_MEMATTR_WRITE_COMBINING;
	else if ((dev->spa_efi_mem_flags & EFI_MD_ATTR_WP) != 0)
		mode = VM_MEMATTR_WRITE_PROTECTED;
	else if ((dev->spa_efi_mem_flags & EFI_MD_ATTR_UC) != 0)
		mode = VM_MEMATTR_UNCACHEABLE;
	else {
		if (bootverbose)
			printf("SPA mapping attr %#lx unsupported\n",
			    dev->spa_efi_mem_flags);
		mode = VM_MEMATTR_UNCACHEABLE;
	}
	return (mode);
}

static int
nvdimm_spa_uio(struct nvdimm_spa_dev *dev, struct uio *uio)
{
	struct vm_page m, *ma;
	off_t off;
	vm_memattr_t mattr;
	int error, n;

	error = 0;
	if (dev->spa_kva == NULL) {
		mattr = nvdimm_spa_memattr(dev);
		vm_page_initfake(&m, 0, mattr);
		ma = &m;
		while (uio->uio_resid > 0) {
			if (uio->uio_offset >= dev->spa_len)
				break;
			off = dev->spa_phys_base + uio->uio_offset;
			vm_page_updatefake(&m, trunc_page(off), mattr);
			n = PAGE_SIZE;
			if (n > uio->uio_resid)
				n = uio->uio_resid;
			error = uiomove_fromphys(&ma, off & PAGE_MASK, n, uio);
			if (error != 0)
				break;
		}
	} else {
		while (uio->uio_resid > 0) {
			if (uio->uio_offset >= dev->spa_len)
				break;
			n = INT_MAX;
			if (n > uio->uio_resid)
				n = uio->uio_resid;
			if (uio->uio_offset + n > dev->spa_len)
				n = dev->spa_len - uio->uio_offset;
			error = uiomove((char *)dev->spa_kva + uio->uio_offset,
			    n, uio);
			if (error != 0)
				break;
		}
	}
	return (error);
}

static int
nvdimm_spa_rw(struct cdev *dev, struct uio *uio, int ioflag)
{

	return (nvdimm_spa_uio(dev->si_drv1, uio));
}

static int
nvdimm_spa_ioctl(struct cdev *cdev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{
	struct nvdimm_spa_dev *dev;
	int error;

	dev = cdev->si_drv1;
	error = 0;
	switch (cmd) {
	case DIOCGSECTORSIZE:
		*(u_int *)data = DEV_BSIZE;
		break;
	case DIOCGMEDIASIZE:
		*(off_t *)data = dev->spa_len;
		break;
	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

static int
nvdimm_spa_mmap_single(struct cdev *cdev, vm_ooffset_t *offset, vm_size_t size,
    vm_object_t *objp, int nprot)
{
	struct nvdimm_spa_dev *dev;

	dev = cdev->si_drv1;
	if (dev->spa_obj == NULL)
		return (ENXIO);
	if (*offset >= dev->spa_len || *offset + size < *offset ||
	    *offset + size > dev->spa_len)
		return (EINVAL);
	vm_object_reference(dev->spa_obj);
	*objp = dev->spa_obj;
	return (0);
}

static struct cdevsw spa_cdevsw = {
	.d_version =	D_VERSION,
	.d_flags =	D_DISK,
	.d_name =	"nvdimm_spa",
	.d_read =	nvdimm_spa_rw,
	.d_write =	nvdimm_spa_rw,
	.d_ioctl =	nvdimm_spa_ioctl,
	.d_mmap_single = nvdimm_spa_mmap_single,
};

static void
nvdimm_spa_g_all_unmapped(struct nvdimm_spa_dev *dev, struct bio *bp, int rw)
{
	struct vm_page maa[bp->bio_ma_n];
	vm_page_t ma[bp->bio_ma_n];
	vm_memattr_t mattr;
	int i;

	mattr = nvdimm_spa_memattr(dev);
	for (i = 0; i < nitems(ma); i++) {
		maa[i].flags = 0;
		vm_page_initfake(&maa[i], dev->spa_phys_base +
		    trunc_page(bp->bio_offset) + PAGE_SIZE * i, mattr);
		ma[i] = &maa[i];
	}
	if (rw == BIO_READ)
		pmap_copy_pages(ma, bp->bio_offset & PAGE_MASK, bp->bio_ma,
		    bp->bio_ma_offset, bp->bio_length);
	else
		pmap_copy_pages(bp->bio_ma, bp->bio_ma_offset, ma,
		    bp->bio_offset & PAGE_MASK, bp->bio_length);
}

static void
nvdimm_spa_g_thread(void *arg)
{
	struct g_spa *sc;
	struct bio *bp;
	struct uio auio;
	struct iovec aiovec;
	int error;

	sc = arg;
	for (;;) {
		mtx_lock(&sc->spa_g_mtx);
		for (;;) {
			bp = bioq_takefirst(&sc->spa_g_queue);
			if (bp != NULL)
				break;
			msleep(&sc->spa_g_queue, &sc->spa_g_mtx, PRIBIO,
			    "spa_g", 0);
			if (!sc->spa_g_proc_run) {
				sc->spa_g_proc_exiting = true;
				wakeup(&sc->spa_g_queue);
				mtx_unlock(&sc->spa_g_mtx);
				kproc_exit(0);
			}
			continue;
		}
		mtx_unlock(&sc->spa_g_mtx);
		if (bp->bio_cmd != BIO_READ && bp->bio_cmd != BIO_WRITE &&
		    bp->bio_cmd != BIO_FLUSH) {
			error = EOPNOTSUPP;
			goto completed;
		}

		error = 0;
		if (bp->bio_cmd == BIO_FLUSH) {
			if (sc->dev->spa_kva != NULL) {
				pmap_large_map_wb(sc->dev->spa_kva,
				    sc->dev->spa_len);
			} else {
				pmap_flush_cache_phys_range(
				    (vm_paddr_t)sc->dev->spa_phys_base,
				    (vm_paddr_t)sc->dev->spa_phys_base +
				    sc->dev->spa_len,
				    nvdimm_spa_memattr(sc->dev));
			}
			/*
			 * XXX flush IMC
			 */
			goto completed;
		}
		
		if ((bp->bio_flags & BIO_UNMAPPED) != 0) {
			if (sc->dev->spa_kva != NULL) {
				aiovec.iov_base = (char *)sc->dev->spa_kva +
				    bp->bio_offset;
				aiovec.iov_len = bp->bio_length;
				auio.uio_iov = &aiovec;
				auio.uio_iovcnt = 1;
				auio.uio_resid = bp->bio_length;
				auio.uio_offset = bp->bio_offset;
				auio.uio_segflg = UIO_SYSSPACE;
				auio.uio_rw = bp->bio_cmd == BIO_READ ?
				    UIO_WRITE : UIO_READ;
				auio.uio_td = curthread;
				error = uiomove_fromphys(bp->bio_ma,
				    bp->bio_ma_offset, bp->bio_length, &auio);
				bp->bio_resid = auio.uio_resid;
			} else {
				nvdimm_spa_g_all_unmapped(sc->dev, bp,
				    bp->bio_cmd);
				bp->bio_resid = bp->bio_length;
				error = 0;
			}
		} else {
			aiovec.iov_base = bp->bio_data;
			aiovec.iov_len = bp->bio_length;
			auio.uio_iov = &aiovec;
			auio.uio_iovcnt = 1;
			auio.uio_resid = bp->bio_length;
			auio.uio_offset = bp->bio_offset;
			auio.uio_segflg = UIO_SYSSPACE;
			auio.uio_rw = bp->bio_cmd == BIO_READ ? UIO_READ :
			    UIO_WRITE;
			auio.uio_td = curthread;
			error = nvdimm_spa_uio(sc->dev, &auio);
			bp->bio_resid = auio.uio_resid;
		}
		bp->bio_bcount = bp->bio_length;
		devstat_end_transaction_bio(sc->spa_g_devstat, bp);
completed:
		bp->bio_completed = bp->bio_length;
		g_io_deliver(bp, error);
	}
}

static void
nvdimm_spa_g_start(struct bio *bp)
{
	struct g_spa *sc;

	sc = bp->bio_to->geom->softc;
	if (bp->bio_cmd == BIO_READ || bp->bio_cmd == BIO_WRITE) {
		mtx_lock(&sc->spa_g_stat_mtx);
		devstat_start_transaction_bio(sc->spa_g_devstat, bp);
		mtx_unlock(&sc->spa_g_stat_mtx);
	}
	mtx_lock(&sc->spa_g_mtx);
	bioq_disksort(&sc->spa_g_queue, bp);
	wakeup(&sc->spa_g_queue);
	mtx_unlock(&sc->spa_g_mtx);
}

static int
nvdimm_spa_g_access(struct g_provider *pp, int r, int w, int e)
{

	return (0);
}

static struct g_geom * nvdimm_spa_g_create(struct nvdimm_spa_dev *dev,
    const char *name);
static g_ctl_destroy_geom_t nvdimm_spa_g_destroy_geom;

struct g_class nvdimm_spa_g_class = {
	.name =		"SPA",
	.version =	G_VERSION,
	.start =	nvdimm_spa_g_start,
	.access =	nvdimm_spa_g_access,
	.destroy_geom =	nvdimm_spa_g_destroy_geom,
};
DECLARE_GEOM_CLASS(nvdimm_spa_g_class, g_spa);

int
nvdimm_spa_init(struct SPA_mapping *spa, ACPI_NFIT_SYSTEM_ADDRESS *nfitaddr,
    enum SPA_mapping_type spa_type)
{
	char *name;
	int error;

	spa->spa_type = spa_type;
	spa->spa_nfit_idx = nfitaddr->RangeIndex;
	spa->dev.spa_domain =
	    ((nfitaddr->Flags & ACPI_NFIT_PROXIMITY_VALID) != 0) ?
	    nfitaddr->ProximityDomain : -1;
	spa->dev.spa_phys_base = nfitaddr->Address;
	spa->dev.spa_len = nfitaddr->Length;
	spa->dev.spa_efi_mem_flags = nfitaddr->MemoryMapping;
	if (bootverbose) {
		printf("NVDIMM SPA%d base %#016jx len %#016jx %s fl %#jx\n",
		    spa->spa_nfit_idx,
		    (uintmax_t)spa->dev.spa_phys_base,
		    (uintmax_t)spa->dev.spa_len,
		    nvdimm_SPA_uuid_list[spa_type].u_name,
		    spa->dev.spa_efi_mem_flags);
	}
	if (!nvdimm_SPA_uuid_list[spa_type].u_usr_acc)
		return (0);

	asprintf(&name, M_NVDIMM, "spa%d", spa->spa_nfit_idx);
	error = nvdimm_spa_dev_init(&spa->dev, name);
	free(name, M_NVDIMM);
	return (error);
}

int
nvdimm_spa_dev_init(struct nvdimm_spa_dev *dev, const char *name)
{
	struct make_dev_args mda;
	struct sglist *spa_sg;
	char *devname;
	int error, error1;

	error1 = pmap_large_map(dev->spa_phys_base, dev->spa_len,
	    &dev->spa_kva, nvdimm_spa_memattr(dev));
	if (error1 != 0) {
		printf("NVDIMM %s cannot map into KVA, error %d\n", name,
		    error1);
		dev->spa_kva = NULL;
	}

	spa_sg = sglist_alloc(1, M_WAITOK);
	error = sglist_append_phys(spa_sg, dev->spa_phys_base,
	    dev->spa_len);
	if (error == 0) {
		dev->spa_obj = vm_pager_allocate(OBJT_SG, spa_sg, dev->spa_len,
		    VM_PROT_ALL, 0, NULL);
		if (dev->spa_obj == NULL) {
			printf("NVDIMM %s failed to alloc vm object", name);
			sglist_free(spa_sg);
		}
	} else {
		printf("NVDIMM %s failed to init sglist, error %d", name,
		    error);
		sglist_free(spa_sg);
	}

	make_dev_args_init(&mda);
	mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
	mda.mda_devsw = &spa_cdevsw;
	mda.mda_cr = NULL;
	mda.mda_uid = UID_ROOT;
	mda.mda_gid = GID_OPERATOR;
	mda.mda_mode = 0660;
	mda.mda_si_drv1 = dev;
	asprintf(&devname, M_NVDIMM, "nvdimm_%s", name);
	error = make_dev_s(&mda, &dev->spa_dev, "%s", devname);
	free(devname, M_NVDIMM);
	if (error != 0) {
		printf("NVDIMM %s cannot create devfs node, error %d\n", name,
		    error);
		if (error1 == 0)
			error1 = error;
	}
	dev->spa_g = nvdimm_spa_g_create(dev, name);
	if (dev->spa_g == NULL && error1 == 0)
		error1 = ENXIO;
	return (error1);
}

static struct g_geom *
nvdimm_spa_g_create(struct nvdimm_spa_dev *dev, const char *name)
{
	struct g_geom *gp;
	struct g_spa *sc;
	int error;

	gp = NULL;
	sc = malloc(sizeof(struct g_spa), M_NVDIMM, M_WAITOK | M_ZERO);
	sc->dev = dev;
	bioq_init(&sc->spa_g_queue);
	mtx_init(&sc->spa_g_mtx, "spag", NULL, MTX_DEF);
	mtx_init(&sc->spa_g_stat_mtx, "spagst", NULL, MTX_DEF);
	sc->spa_g_proc_run = true;
	sc->spa_g_proc_exiting = false;
	error = kproc_create(nvdimm_spa_g_thread, sc, &sc->spa_g_proc, 0, 0,
	    "g_spa");
	if (error != 0) {
		mtx_destroy(&sc->spa_g_mtx);
		mtx_destroy(&sc->spa_g_stat_mtx);
		free(sc, M_NVDIMM);
		printf("NVDIMM %s cannot create geom worker, error %d\n", name,
		    error);
	} else {
		g_topology_lock();
		gp = g_new_geomf(&nvdimm_spa_g_class, "%s", name);
		gp->softc = sc;
		sc->spa_p = g_new_providerf(gp, "%s", name);
		sc->spa_p->mediasize = dev->spa_len;
		sc->spa_p->sectorsize = DEV_BSIZE;
		sc->spa_p->flags |= G_PF_DIRECT_SEND | G_PF_DIRECT_RECEIVE |
		    G_PF_ACCEPT_UNMAPPED;
		g_error_provider(sc->spa_p, 0);
		sc->spa_g_devstat = devstat_new_entry("spa", -1, DEV_BSIZE,
		    DEVSTAT_ALL_SUPPORTED, DEVSTAT_TYPE_DIRECT,
		    DEVSTAT_PRIORITY_MAX);
		g_topology_unlock();
	}
	return (gp);
}

void
nvdimm_spa_fini(struct SPA_mapping *spa)
{

	nvdimm_spa_dev_fini(&spa->dev);
}

void
nvdimm_spa_dev_fini(struct nvdimm_spa_dev *dev)
{

	if (dev->spa_g != NULL) {
		g_topology_lock();
		nvdimm_spa_g_destroy_geom(NULL, dev->spa_g->class, dev->spa_g);
		g_topology_unlock();
	}
	if (dev->spa_dev != NULL) {
		destroy_dev(dev->spa_dev);
		dev->spa_dev = NULL;
	}
	vm_object_deallocate(dev->spa_obj);
	if (dev->spa_kva != NULL) {
		pmap_large_unmap(dev->spa_kva, dev->spa_len);
		dev->spa_kva = NULL;
	}
}

static int
nvdimm_spa_g_destroy_geom(struct gctl_req *req, struct g_class *cp,
    struct g_geom *gp)
{
	struct g_spa *sc;

	sc = gp->softc;
	mtx_lock(&sc->spa_g_mtx);
	sc->spa_g_proc_run = false;
	wakeup(&sc->spa_g_queue);
	while (!sc->spa_g_proc_exiting)
		msleep(&sc->spa_g_queue, &sc->spa_g_mtx, PRIBIO, "spa_e", 0);
	mtx_unlock(&sc->spa_g_mtx);
	g_topology_assert();
	g_wither_geom(gp, ENXIO);
	sc->spa_p = NULL;
	if (sc->spa_g_devstat != NULL) {
		devstat_remove_entry(sc->spa_g_devstat);
		sc->spa_g_devstat = NULL;
	}
	mtx_destroy(&sc->spa_g_mtx);
	mtx_destroy(&sc->spa_g_stat_mtx);
	free(sc, M_NVDIMM);
	return (0);
}
