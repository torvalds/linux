/*-
 * Copyright (c) 2013-2015 Sandvine Inc.
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

#include "opt_bus.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/ioccom.h>
#include <sys/iov.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/pciio.h>
#include <sys/queue.h>
#include <sys/rman.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/stdarg.h>

#include <sys/nv.h>
#include <sys/iov_schema.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_iov.h>
#include <dev/pci/pci_private.h>
#include <dev/pci/pci_iov_private.h>
#include <dev/pci/schema_private.h>

#include "pcib_if.h"

static MALLOC_DEFINE(M_SRIOV, "sr_iov", "PCI SR-IOV allocations");

static d_ioctl_t pci_iov_ioctl;

static struct cdevsw iov_cdevsw = {
	.d_version = D_VERSION,
	.d_name = "iov",
	.d_ioctl = pci_iov_ioctl
};

SYSCTL_DECL(_hw_pci);

/*
 * The maximum amount of memory we will allocate for user configuration of an
 * SR-IOV device.  1MB ought to be enough for anyone, but leave this 
 * configurable just in case.
 */
static u_long pci_iov_max_config = 1024 * 1024;
SYSCTL_ULONG(_hw_pci, OID_AUTO, iov_max_config, CTLFLAG_RWTUN,
    &pci_iov_max_config, 0, "Maximum allowed size of SR-IOV configuration.");


#define IOV_READ(d, r, w) \
	pci_read_config((d)->cfg.dev, (d)->cfg.iov->iov_pos + r, w)

#define IOV_WRITE(d, r, v, w) \
	pci_write_config((d)->cfg.dev, (d)->cfg.iov->iov_pos + r, v, w)

static nvlist_t	*pci_iov_build_schema(nvlist_t **pf_schema,
		    nvlist_t **vf_schema);
static void	pci_iov_build_pf_schema(nvlist_t *schema,
		    nvlist_t **driver_schema);
static void	pci_iov_build_vf_schema(nvlist_t *schema,
		    nvlist_t **driver_schema);
static nvlist_t	*pci_iov_get_pf_subsystem_schema(void);
static nvlist_t	*pci_iov_get_vf_subsystem_schema(void);

int
pci_iov_attach_name(device_t dev, struct nvlist *pf_schema,
    struct nvlist *vf_schema, const char *fmt, ...)
{
	char buf[NAME_MAX + 1];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return (PCI_IOV_ATTACH(device_get_parent(dev), dev, pf_schema,
	    vf_schema, buf));
}

int
pci_iov_attach_method(device_t bus, device_t dev, nvlist_t *pf_schema,
    nvlist_t *vf_schema, const char *name)
{
	device_t pcib;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	nvlist_t *schema;
	uint32_t version;
	int error;
	int iov_pos;

	dinfo = device_get_ivars(dev);
	pcib = device_get_parent(bus);
	schema = NULL;
	
	error = pci_find_extcap(dev, PCIZ_SRIOV, &iov_pos);

	if (error != 0)
		return (error);

	version = pci_read_config(dev, iov_pos, 4); 
	if (PCI_EXTCAP_VER(version) != 1) {
		if (bootverbose)
			device_printf(dev, 
			    "Unsupported version of SR-IOV (%d) detected\n",
			    PCI_EXTCAP_VER(version));

		return (ENXIO);
	}

	iov = malloc(sizeof(*dinfo->cfg.iov), M_SRIOV, M_WAITOK | M_ZERO);

	mtx_lock(&Giant);
	if (dinfo->cfg.iov != NULL) {
		error = EBUSY;
		goto cleanup;
	}
	iov->iov_pos = iov_pos;

	schema = pci_iov_build_schema(&pf_schema, &vf_schema);
	if (schema == NULL) {
		error = ENOMEM;
		goto cleanup;
	}

	error = pci_iov_validate_schema(schema);
	if (error != 0)
		goto cleanup;
	iov->iov_schema = schema;

	iov->iov_cdev = make_dev(&iov_cdevsw, device_get_unit(dev),
	    UID_ROOT, GID_WHEEL, 0600, "iov/%s", name);

	if (iov->iov_cdev == NULL) {
		error = ENOMEM;
		goto cleanup;
	}
	
	dinfo->cfg.iov = iov;
	iov->iov_cdev->si_drv1 = dinfo;
	mtx_unlock(&Giant);

	return (0);

cleanup:
	nvlist_destroy(schema);
	nvlist_destroy(pf_schema);
	nvlist_destroy(vf_schema);
	free(iov, M_SRIOV);
	mtx_unlock(&Giant);
	return (error);
}

int
pci_iov_detach_method(device_t bus, device_t dev)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;

	mtx_lock(&Giant);
	dinfo = device_get_ivars(dev);
	iov = dinfo->cfg.iov;

	if (iov == NULL) {
		mtx_unlock(&Giant);
		return (0);
	}

	if (iov->iov_num_vfs != 0 || iov->iov_flags & IOV_BUSY) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}

	dinfo->cfg.iov = NULL;

	if (iov->iov_cdev) {
		destroy_dev(iov->iov_cdev);
		iov->iov_cdev = NULL;
	}
	nvlist_destroy(iov->iov_schema);

	free(iov, M_SRIOV);
	mtx_unlock(&Giant);

	return (0);
}

static nvlist_t *
pci_iov_build_schema(nvlist_t **pf, nvlist_t **vf)
{
	nvlist_t *schema, *pf_driver, *vf_driver;

	/* We always take ownership of the schemas. */
	pf_driver = *pf;
	*pf = NULL;
	vf_driver = *vf;
	*vf = NULL;

	schema = pci_iov_schema_alloc_node();
	if (schema == NULL)
		goto cleanup;

	pci_iov_build_pf_schema(schema, &pf_driver);
	pci_iov_build_vf_schema(schema, &vf_driver);

	if (nvlist_error(schema) != 0)
		goto cleanup;

	return (schema);

cleanup:
	nvlist_destroy(schema);
	nvlist_destroy(pf_driver);
	nvlist_destroy(vf_driver);
	return (NULL);
}

static void
pci_iov_build_pf_schema(nvlist_t *schema, nvlist_t **driver_schema)
{
	nvlist_t *pf_schema, *iov_schema;

	pf_schema = pci_iov_schema_alloc_node();
	if (pf_schema == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	iov_schema = pci_iov_get_pf_subsystem_schema();

	/*
	 * Note that if either *driver_schema or iov_schema is NULL, then
	 * nvlist_move_nvlist will put the schema in the error state and
	 * SR-IOV will fail to initialize later, so we don't have to explicitly
	 * handle that case.
	 */
	nvlist_move_nvlist(pf_schema, DRIVER_CONFIG_NAME, *driver_schema);
	nvlist_move_nvlist(pf_schema, IOV_CONFIG_NAME, iov_schema);
	nvlist_move_nvlist(schema, PF_CONFIG_NAME, pf_schema);
	*driver_schema = NULL;
}

static void
pci_iov_build_vf_schema(nvlist_t *schema, nvlist_t **driver_schema)
{
	nvlist_t *vf_schema, *iov_schema;

	vf_schema = pci_iov_schema_alloc_node();
	if (vf_schema == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	iov_schema = pci_iov_get_vf_subsystem_schema();

	/*
	 * Note that if either *driver_schema or iov_schema is NULL, then
	 * nvlist_move_nvlist will put the schema in the error state and
	 * SR-IOV will fail to initialize later, so we don't have to explicitly
	 * handle that case.
	 */
	nvlist_move_nvlist(vf_schema, DRIVER_CONFIG_NAME, *driver_schema);
	nvlist_move_nvlist(vf_schema, IOV_CONFIG_NAME, iov_schema);
	nvlist_move_nvlist(schema, VF_SCHEMA_NAME, vf_schema);
	*driver_schema = NULL;
}

static nvlist_t *
pci_iov_get_pf_subsystem_schema(void)
{
	nvlist_t *pf;

	pf = pci_iov_schema_alloc_node();
	if (pf == NULL)
		return (NULL);

	pci_iov_schema_add_uint16(pf, "num_vfs", IOV_SCHEMA_REQUIRED, -1);
	pci_iov_schema_add_string(pf, "device", IOV_SCHEMA_REQUIRED, NULL);

	return (pf);
}

static nvlist_t *
pci_iov_get_vf_subsystem_schema(void)
{
	nvlist_t *vf;

	vf = pci_iov_schema_alloc_node();
	if (vf == NULL)
		return (NULL);

	pci_iov_schema_add_bool(vf, "passthrough", IOV_SCHEMA_HASDEFAULT, 0);

	return (vf);
}

static int
pci_iov_alloc_bar(struct pci_devinfo *dinfo, int bar, pci_addr_t bar_shift)
{
	struct resource *res;
	struct pcicfg_iov *iov;
	device_t dev, bus;
	rman_res_t start, end;
	pci_addr_t bar_size;
	int rid;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	rid = iov->iov_pos + PCIR_SRIOV_BAR(bar);
	bar_size = 1 << bar_shift;

	res = pci_alloc_multi_resource(bus, dev, SYS_RES_MEMORY, &rid, 0,
	    ~0, 1, iov->iov_num_vfs, RF_ACTIVE);

	if (res == NULL)
		return (ENXIO);

	iov->iov_bar[bar].res = res;
	iov->iov_bar[bar].bar_size = bar_size;
	iov->iov_bar[bar].bar_shift = bar_shift;

	start = rman_get_start(res);
	end = rman_get_end(res);
	return (rman_manage_region(&iov->rman, start, end));
}

static void
pci_iov_add_bars(struct pcicfg_iov *iov, struct pci_devinfo *dinfo)
{
	struct pci_iov_bar *bar;
	uint64_t bar_start;
	int i;

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		bar = &iov->iov_bar[i];
		if (bar->res != NULL) {
			bar_start = rman_get_start(bar->res) +
			    dinfo->cfg.vf.index * bar->bar_size;

			pci_add_bar(dinfo->cfg.dev, PCIR_BAR(i), bar_start,
			    bar->bar_shift);
		}
	}
}

static int
pci_iov_parse_config(struct pcicfg_iov *iov, struct pci_iov_arg *arg,
    nvlist_t **ret)
{
	void *packed_config;
	nvlist_t *config;
	int error;

	config = NULL;
	packed_config = NULL;

	if (arg->len > pci_iov_max_config) {
		error = EMSGSIZE;
		goto out;
	}

	packed_config = malloc(arg->len, M_SRIOV, M_WAITOK);

	error = copyin(arg->config, packed_config, arg->len);
	if (error != 0)
		goto out;

	config = nvlist_unpack(packed_config, arg->len, NV_FLAG_IGNORE_CASE);
	if (config == NULL) {
		error = EINVAL;
		goto out;
	}

	error = pci_iov_schema_validate_config(iov->iov_schema, config);
	if (error != 0)
		goto out;

	error = nvlist_error(config);
	if (error != 0)
		goto out;

	*ret = config;
	config = NULL;

out:
	nvlist_destroy(config);
	free(packed_config, M_SRIOV);
	return (error);
}

/*
 * Set the ARI_EN bit in the lowest-numbered PCI function with the SR-IOV
 * capability.  This bit is only writeable on the lowest-numbered PF but
 * affects all PFs on the device.
 */
static int
pci_iov_set_ari(device_t bus)
{
	device_t lowest;
	device_t *devlist;
	int i, error, devcount, lowest_func, lowest_pos, iov_pos, dev_func;
	uint16_t iov_ctl;

	/* If ARI is disabled on the downstream port there is nothing to do. */
	if (!PCIB_ARI_ENABLED(device_get_parent(bus)))
		return (0);

	error = device_get_children(bus, &devlist, &devcount);

	if (error != 0)
		return (error);

	lowest = NULL;
	for (i = 0; i < devcount; i++) {
		if (pci_find_extcap(devlist[i], PCIZ_SRIOV, &iov_pos) == 0) {
			dev_func = pci_get_function(devlist[i]);
			if (lowest == NULL || dev_func < lowest_func) {
				lowest = devlist[i];
				lowest_func = dev_func;
				lowest_pos = iov_pos;
			}
		}
	}
	free(devlist, M_TEMP);

	/*
	 * If we called this function some device must have the SR-IOV
	 * capability.
	 */
	KASSERT(lowest != NULL,
	    ("Could not find child of %s with SR-IOV capability",
	    device_get_nameunit(bus)));

	iov_ctl = pci_read_config(lowest, lowest_pos + PCIR_SRIOV_CTL, 2);
	iov_ctl |= PCIM_SRIOV_ARI_EN;
	pci_write_config(lowest, lowest_pos + PCIR_SRIOV_CTL, iov_ctl, 2);
	if ((pci_read_config(lowest, lowest_pos + PCIR_SRIOV_CTL, 2) &
	    PCIM_SRIOV_ARI_EN) == 0) {
		device_printf(lowest, "failed to enable ARI\n");
		return (ENXIO);
	}
	return (0);
}

static int
pci_iov_config_page_size(struct pci_devinfo *dinfo)
{
	uint32_t page_cap, page_size;

	page_cap = IOV_READ(dinfo, PCIR_SRIOV_PAGE_CAP, 4);

	/*
	 * If the system page size is less than the smallest SR-IOV page size
	 * then round up to the smallest SR-IOV page size.
	 */
	if (PAGE_SHIFT < PCI_SRIOV_BASE_PAGE_SHIFT)
		page_size = (1 << 0);
	else
		page_size = (1 << (PAGE_SHIFT - PCI_SRIOV_BASE_PAGE_SHIFT));

	/* Check that the device supports the system page size. */
	if (!(page_size & page_cap))
		return (ENXIO);

	IOV_WRITE(dinfo, PCIR_SRIOV_PAGE_SIZE, page_size, 4);
	return (0);
}

static int
pci_iov_init(device_t dev, uint16_t num_vfs, const nvlist_t *config)
{
	const nvlist_t *device, *driver_config;

	device = nvlist_get_nvlist(config, PF_CONFIG_NAME);
	driver_config = nvlist_get_nvlist(device, DRIVER_CONFIG_NAME);
	return (PCI_IOV_INIT(dev, num_vfs, driver_config));
}

static int
pci_iov_init_rman(device_t pf, struct pcicfg_iov *iov)
{
	int error;

	iov->rman.rm_start = 0;
	iov->rman.rm_end = ~0;
	iov->rman.rm_type = RMAN_ARRAY;
	snprintf(iov->rman_name, sizeof(iov->rman_name), "%s VF I/O memory",
	    device_get_nameunit(pf));
	iov->rman.rm_descr = iov->rman_name;

	error = rman_init(&iov->rman);
	if (error != 0)
		return (error);

	iov->iov_flags |= IOV_RMAN_INITED;
	return (0);
}

static int
pci_iov_alloc_bar_ea(struct pci_devinfo *dinfo, int bar)
{
	struct pcicfg_iov *iov;
	rman_res_t start, end;
	struct resource *res;
	struct resource_list *rl;
	struct resource_list_entry *rle;

	rl = &dinfo->resources;
	iov = dinfo->cfg.iov;

	rle = resource_list_find(rl, SYS_RES_MEMORY,
	    iov->iov_pos + PCIR_SRIOV_BAR(bar));
	if (rle == NULL)
		rle = resource_list_find(rl, SYS_RES_IOPORT,
		    iov->iov_pos + PCIR_SRIOV_BAR(bar));
	if (rle == NULL)
		return (ENXIO);
	res = rle->res;

	iov->iov_bar[bar].res = res;
	iov->iov_bar[bar].bar_size = rman_get_size(res) / iov->iov_num_vfs;
	iov->iov_bar[bar].bar_shift = pci_mapsize(iov->iov_bar[bar].bar_size);

	start = rman_get_start(res);
	end = rman_get_end(res);

	return (rman_manage_region(&iov->rman, start, end));
}

static int
pci_iov_setup_bars(struct pci_devinfo *dinfo)
{
	device_t dev;
	struct pcicfg_iov *iov;
	pci_addr_t bar_value, testval;
	int i, last_64, error;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	last_64 = 0;

	pci_add_resources_ea(device_get_parent(dev), dev, 1);

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		/* First, try to use BARs allocated with EA */
		error = pci_iov_alloc_bar_ea(dinfo, i);
		if (error == 0)
			continue;

		/* Allocate legacy-BAR only if EA is not enabled */
		if (pci_ea_is_enabled(dev, iov->iov_pos + PCIR_SRIOV_BAR(i)))
			continue;

		/*
		 * If a PCI BAR is a 64-bit wide BAR, then it spans two
		 * consecutive registers.  Therefore if the last BAR that
		 * we looked at was a 64-bit BAR, we need to skip this
		 * register as it's the second half of the last BAR.
		 */
		if (!last_64) {
			pci_read_bar(dev,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    &bar_value, &testval, &last_64);

			if (testval != 0) {
				error = pci_iov_alloc_bar(dinfo, i,
				   pci_mapsize(testval));
				if (error != 0)
					return (error);
			}
		} else
			last_64 = 0;
	}

	return (0);
}

static void
pci_iov_enumerate_vfs(struct pci_devinfo *dinfo, const nvlist_t *config,
    uint16_t first_rid, uint16_t rid_stride)
{
	char device_name[VF_MAX_NAME];
	const nvlist_t *device, *driver_config, *iov_config;
	device_t bus, dev, vf;
	struct pcicfg_iov *iov;
	struct pci_devinfo *vfinfo;
	int i, error;
	uint16_t vid, did, next_rid;

	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	next_rid = first_rid;
	vid = pci_get_vendor(dev);
	did = IOV_READ(dinfo, PCIR_SRIOV_VF_DID, 2);

	for (i = 0; i < iov->iov_num_vfs; i++, next_rid += rid_stride) {
		snprintf(device_name, sizeof(device_name), VF_PREFIX"%d", i);
		device = nvlist_get_nvlist(config, device_name);
		iov_config = nvlist_get_nvlist(device, IOV_CONFIG_NAME);
		driver_config = nvlist_get_nvlist(device, DRIVER_CONFIG_NAME);

		vf = PCI_CREATE_IOV_CHILD(bus, dev, next_rid, vid, did);
		if (vf == NULL)
			break;

		/*
		 * If we are creating passthrough devices then force the ppt
		 * driver to attach to prevent a VF driver from claiming the
		 * VFs.
		 */
		if (nvlist_get_bool(iov_config, "passthrough"))
			device_set_devclass_fixed(vf, "ppt");

		vfinfo = device_get_ivars(vf);

		vfinfo->cfg.iov = iov;
		vfinfo->cfg.vf.index = i;

		pci_iov_add_bars(iov, vfinfo);

		error = PCI_IOV_ADD_VF(dev, i, driver_config);
		if (error != 0) {
			device_printf(dev, "Failed to add VF %d\n", i);
			device_delete_child(bus, vf);
		}
	}

	bus_generic_attach(bus);
}

static int
pci_iov_config(struct cdev *cdev, struct pci_iov_arg *arg)
{
	device_t bus, dev;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	nvlist_t *config;
	int i, error;
	uint16_t rid_off, rid_stride;
	uint16_t first_rid, last_rid;
	uint16_t iov_ctl;
	uint16_t num_vfs, total_vfs;
	int iov_inited;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	iov_inited = 0;
	config = NULL;

	if ((iov->iov_flags & IOV_BUSY) || iov->iov_num_vfs != 0) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}
	iov->iov_flags |= IOV_BUSY;

	error = pci_iov_parse_config(iov, arg, &config);
	if (error != 0)
		goto out;

	num_vfs = pci_iov_config_get_num_vfs(config);
	total_vfs = IOV_READ(dinfo, PCIR_SRIOV_TOTAL_VFS, 2);
	if (num_vfs > total_vfs) {
		error = EINVAL;
		goto out;
	}

	error = pci_iov_config_page_size(dinfo);
	if (error != 0)
		goto out;

	error = pci_iov_set_ari(bus);
	if (error != 0)
		goto out;

	error = pci_iov_init(dev, num_vfs, config);
	if (error != 0)
		goto out;
	iov_inited = 1;

	IOV_WRITE(dinfo, PCIR_SRIOV_NUM_VFS, num_vfs, 2);

	rid_off = IOV_READ(dinfo, PCIR_SRIOV_VF_OFF, 2);
	rid_stride = IOV_READ(dinfo, PCIR_SRIOV_VF_STRIDE, 2);

	first_rid = pci_get_rid(dev) + rid_off;
	last_rid = first_rid + (num_vfs - 1) * rid_stride;

	/* We don't yet support allocating extra bus numbers for VFs. */
	if (pci_get_bus(dev) != PCI_RID2BUS(last_rid)) {
		error = ENOSPC;
		goto out;
	}

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl &= ~(PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE);
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);

	error = pci_iov_init_rman(dev, iov);
	if (error != 0)
		goto out;

	iov->iov_num_vfs = num_vfs;

	error = pci_iov_setup_bars(dinfo);
	if (error != 0)
		goto out;

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl |= PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE;
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);

	/* Per specification, we must wait 100ms before accessing VFs. */
	pause("iov", roundup(hz, 10));
	pci_iov_enumerate_vfs(dinfo, config, first_rid, rid_stride);

	nvlist_destroy(config);
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);

	return (0);
out:
	if (iov_inited)
		PCI_IOV_UNINIT(dev);

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		if (iov->iov_bar[i].res != NULL) {
			pci_release_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    iov->iov_bar[i].res);
			pci_delete_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i));
			iov->iov_bar[i].res = NULL;
		}
	}

	if (iov->iov_flags & IOV_RMAN_INITED) {
		rman_fini(&iov->rman);
		iov->iov_flags &= ~IOV_RMAN_INITED;
	}

	nvlist_destroy(config);
	iov->iov_num_vfs = 0;
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);
	return (error);
}

void
pci_iov_cfg_restore(device_t dev, struct pci_devinfo *dinfo)
{
	struct pcicfg_iov *iov;

	iov = dinfo->cfg.iov;

	IOV_WRITE(dinfo, PCIR_SRIOV_PAGE_SIZE, iov->iov_page_size, 4);
	IOV_WRITE(dinfo, PCIR_SRIOV_NUM_VFS, iov->iov_num_vfs, 2);
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov->iov_ctl, 2);
}

void
pci_iov_cfg_save(device_t dev, struct pci_devinfo *dinfo)
{
	struct pcicfg_iov *iov;

	iov = dinfo->cfg.iov;

	iov->iov_page_size = IOV_READ(dinfo, PCIR_SRIOV_PAGE_SIZE, 4);
	iov->iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
}

/* Return true if child is a VF of the given PF. */
static int
pci_iov_is_child_vf(struct pcicfg_iov *pf, device_t child)
{
	struct pci_devinfo *vfinfo;

	vfinfo = device_get_ivars(child);

	if (!(vfinfo->cfg.flags & PCICFG_VF))
		return (0);

	return (pf == vfinfo->cfg.iov);
}

static int
pci_iov_delete(struct cdev *cdev)
{
	device_t bus, dev, vf, *devlist;
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	int i, error, devcount;
	uint32_t iov_ctl;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	iov = dinfo->cfg.iov;
	dev = dinfo->cfg.dev;
	bus = device_get_parent(dev);
	devlist = NULL;

	if (iov->iov_flags & IOV_BUSY) {
		mtx_unlock(&Giant);
		return (EBUSY);
	}

	if (iov->iov_num_vfs == 0) {
		mtx_unlock(&Giant);
		return (ECHILD);
	}

	iov->iov_flags |= IOV_BUSY;

	error = device_get_children(bus, &devlist, &devcount);

	if (error != 0)
		goto out;

	for (i = 0; i < devcount; i++) {
		vf = devlist[i];

		if (!pci_iov_is_child_vf(iov, vf))
			continue;

		error = device_detach(vf);
		if (error != 0) {
			device_printf(dev,
			   "Could not disable SR-IOV: failed to detach VF %s\n",
			    device_get_nameunit(vf));
			goto out;
		}
	}

	for (i = 0; i < devcount; i++) {
		vf = devlist[i];

		if (pci_iov_is_child_vf(iov, vf))
			device_delete_child(bus, vf);
	}
	PCI_IOV_UNINIT(dev);

	iov_ctl = IOV_READ(dinfo, PCIR_SRIOV_CTL, 2);
	iov_ctl &= ~(PCIM_SRIOV_VF_EN | PCIM_SRIOV_VF_MSE);
	IOV_WRITE(dinfo, PCIR_SRIOV_CTL, iov_ctl, 2);
	IOV_WRITE(dinfo, PCIR_SRIOV_NUM_VFS, 0, 2);

	iov->iov_num_vfs = 0;

	for (i = 0; i <= PCIR_MAX_BAR_0; i++) {
		if (iov->iov_bar[i].res != NULL) {
			pci_release_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i),
			    iov->iov_bar[i].res);
			pci_delete_resource(bus, dev, SYS_RES_MEMORY,
			    iov->iov_pos + PCIR_SRIOV_BAR(i));
			iov->iov_bar[i].res = NULL;
		}
	}

	if (iov->iov_flags & IOV_RMAN_INITED) {
		rman_fini(&iov->rman);
		iov->iov_flags &= ~IOV_RMAN_INITED;
	}

	error = 0;
out:
	free(devlist, M_TEMP);
	iov->iov_flags &= ~IOV_BUSY;
	mtx_unlock(&Giant);
	return (error);
}

static int
pci_iov_get_schema_ioctl(struct cdev *cdev, struct pci_iov_schema *output)
{
	struct pci_devinfo *dinfo;
	void *packed;
	size_t output_len, size;
	int error;

	packed = NULL;

	mtx_lock(&Giant);
	dinfo = cdev->si_drv1;
	packed = nvlist_pack(dinfo->cfg.iov->iov_schema, &size);
	mtx_unlock(&Giant);

	if (packed == NULL) {
		error = ENOMEM;
		goto fail;
	}

	output_len = output->len;
	output->len = size;
	if (size <= output_len) {
		error = copyout(packed, output->schema, size);

		if (error != 0)
			goto fail;

		output->error = 0;
	} else
		/*
		 * If we return an error then the ioctl code won't copyout
		 * output back to userland, so we flag the error in the struct
		 * instead.
		 */
		output->error = EMSGSIZE;

	error = 0;

fail:
	free(packed, M_NVLIST);

	return (error);
}

static int
pci_iov_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
{

	switch (cmd) {
	case IOV_CONFIG:
		return (pci_iov_config(dev, (struct pci_iov_arg *)data));
	case IOV_DELETE:
		return (pci_iov_delete(dev));
	case IOV_GET_SCHEMA:
		return (pci_iov_get_schema_ioctl(dev,
		    (struct pci_iov_schema *)data));
	default:
		return (EINVAL);
	}
}

struct resource *
pci_vf_alloc_mem_resource(device_t dev, device_t child, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct pci_devinfo *dinfo;
	struct pcicfg_iov *iov;
	struct pci_map *map;
	struct resource *res;
	struct resource_list_entry *rle;
	rman_res_t bar_start, bar_end;
	pci_addr_t bar_length;
	int error;

	dinfo = device_get_ivars(child);
	iov = dinfo->cfg.iov;

	map = pci_find_bar(child, *rid);
	if (map == NULL)
		return (NULL);

	bar_length = 1 << map->pm_size;
	bar_start = map->pm_value;
	bar_end = bar_start + bar_length - 1;

	/* Make sure that the resource fits the constraints. */
	if (bar_start >= end || bar_end <= bar_start || count != 1)
		return (NULL);

	/* Clamp the resource to the constraints if necessary. */
	if (bar_start < start)
		bar_start = start;
	if (bar_end > end)
		bar_end = end;
	bar_length = bar_end - bar_start + 1;

	res = rman_reserve_resource(&iov->rman, bar_start, bar_end,
	    bar_length, flags, child);
	if (res == NULL)
		return (NULL);

	rle = resource_list_add(&dinfo->resources, SYS_RES_MEMORY, *rid,
	    bar_start, bar_end, 1);
	if (rle == NULL) {
		rman_release_resource(res);
		return (NULL);
	}

	rman_set_rid(res, *rid);

	if (flags & RF_ACTIVE) {
		error = bus_activate_resource(child, SYS_RES_MEMORY, *rid, res);
		if (error != 0) {
			resource_list_delete(&dinfo->resources, SYS_RES_MEMORY,
			    *rid);
			rman_release_resource(res);
			return (NULL);
		}
	}
	rle->res = res;

	return (res);
}

int
pci_vf_release_mem_resource(device_t dev, device_t child, int rid,
    struct resource *r)
{
	struct pci_devinfo *dinfo;
	struct resource_list_entry *rle;
	int error;

	dinfo = device_get_ivars(child);

	if (rman_get_flags(r) & RF_ACTIVE) {
		error = bus_deactivate_resource(child, SYS_RES_MEMORY, rid, r);
		if (error != 0)
			return (error);
	}

	rle = resource_list_find(&dinfo->resources, SYS_RES_MEMORY, rid);
	if (rle != NULL) {
		rle->res = NULL;
		resource_list_delete(&dinfo->resources, SYS_RES_MEMORY,
		    rid);
	}

	return (rman_release_resource(r));
}

