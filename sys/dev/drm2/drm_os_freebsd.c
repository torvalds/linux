
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <dev/drm2/drmP.h>

#include <dev/agp/agpreg.h>
#include <dev/pci/pcireg.h>

devclass_t drm_devclass;

MALLOC_DEFINE(DRM_MEM_DMA, "drm_dma", "DRM DMA Data Structures");
MALLOC_DEFINE(DRM_MEM_SAREA, "drm_sarea", "DRM SAREA Data Structures");
MALLOC_DEFINE(DRM_MEM_DRIVER, "drm_driver", "DRM DRIVER Data Structures");
MALLOC_DEFINE(DRM_MEM_MAGIC, "drm_magic", "DRM MAGIC Data Structures");
MALLOC_DEFINE(DRM_MEM_MINOR, "drm_minor", "DRM MINOR Data Structures");
MALLOC_DEFINE(DRM_MEM_IOCTLS, "drm_ioctls", "DRM IOCTL Data Structures");
MALLOC_DEFINE(DRM_MEM_MAPS, "drm_maps", "DRM MAP Data Structures");
MALLOC_DEFINE(DRM_MEM_BUFS, "drm_bufs", "DRM BUFFER Data Structures");
MALLOC_DEFINE(DRM_MEM_SEGS, "drm_segs", "DRM SEGMENTS Data Structures");
MALLOC_DEFINE(DRM_MEM_PAGES, "drm_pages", "DRM PAGES Data Structures");
MALLOC_DEFINE(DRM_MEM_FILES, "drm_files", "DRM FILE Data Structures");
MALLOC_DEFINE(DRM_MEM_QUEUES, "drm_queues", "DRM QUEUE Data Structures");
MALLOC_DEFINE(DRM_MEM_CMDS, "drm_cmds", "DRM COMMAND Data Structures");
MALLOC_DEFINE(DRM_MEM_MAPPINGS, "drm_mapping", "DRM MAPPING Data Structures");
MALLOC_DEFINE(DRM_MEM_BUFLISTS, "drm_buflists", "DRM BUFLISTS Data Structures");
MALLOC_DEFINE(DRM_MEM_AGPLISTS, "drm_agplists", "DRM AGPLISTS Data Structures");
MALLOC_DEFINE(DRM_MEM_CTXBITMAP, "drm_ctxbitmap",
    "DRM CTXBITMAP Data Structures");
MALLOC_DEFINE(DRM_MEM_SGLISTS, "drm_sglists", "DRM SGLISTS Data Structures");
MALLOC_DEFINE(DRM_MEM_MM, "drm_sman", "DRM MEMORY MANAGER Data Structures");
MALLOC_DEFINE(DRM_MEM_HASHTAB, "drm_hashtab", "DRM HASHTABLE Data Structures");
MALLOC_DEFINE(DRM_MEM_KMS, "drm_kms", "DRM KMS Data Structures");
MALLOC_DEFINE(DRM_MEM_VBLANK, "drm_vblank", "DRM VBLANK Handling Data");

const char *fb_mode_option = NULL;

#define NSEC_PER_USEC	1000L
#define NSEC_PER_SEC	1000000000L

int64_t
timeval_to_ns(const struct timeval *tv)
{
	return ((int64_t)tv->tv_sec * NSEC_PER_SEC) +
		tv->tv_usec * NSEC_PER_USEC;
}

struct timeval
ns_to_timeval(const int64_t nsec)
{
        struct timeval tv;
	long rem;

	if (nsec == 0) {
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		return (tv);
	}

        tv.tv_sec = nsec / NSEC_PER_SEC;
	rem = nsec % NSEC_PER_SEC;
        if (rem < 0) {
                tv.tv_sec--;
                rem += NSEC_PER_SEC;
        }
	tv.tv_usec = rem / 1000;
        return (tv);
}

/* Copied from OFED. */
unsigned long drm_linux_timer_hz_mask;

static void
drm_linux_timer_init(void *arg)
{

        /*
         * Compute an internal HZ value which can divide 2**32 to
         * avoid timer rounding problems when the tick value wraps
         * around 2**32:
         */
        drm_linux_timer_hz_mask = 1;
        while (drm_linux_timer_hz_mask < (unsigned long)hz)
                drm_linux_timer_hz_mask *= 2;
        drm_linux_timer_hz_mask--;
}
SYSINIT(drm_linux_timer, SI_SUB_DRIVERS, SI_ORDER_FIRST, drm_linux_timer_init, NULL);

static const drm_pci_id_list_t *
drm_find_description(int vendor, int device, const drm_pci_id_list_t *idlist)
{
	int i = 0;

	for (i = 0; idlist[i].vendor != 0; i++) {
		if ((idlist[i].vendor == vendor) &&
		    ((idlist[i].device == device) ||
		    (idlist[i].device == 0))) {
			return (&idlist[i]);
		}
	}
	return (NULL);
}

/*
 * drm_probe_helper: called by a driver at the end of its probe
 * method.
 */
int
drm_probe_helper(device_t kdev, const drm_pci_id_list_t *idlist)
{
	const drm_pci_id_list_t *id_entry;
	int vendor, device;

	vendor = pci_get_vendor(kdev);
	device = pci_get_device(kdev);

	if (pci_get_class(kdev) != PCIC_DISPLAY ||
	    (pci_get_subclass(kdev) != PCIS_DISPLAY_VGA &&
	     pci_get_subclass(kdev) != PCIS_DISPLAY_OTHER))
		return (-ENXIO);

	id_entry = drm_find_description(vendor, device, idlist);
	if (id_entry != NULL) {
		if (device_get_desc(kdev) == NULL) {
			DRM_DEBUG("%s desc: %s\n",
			    device_get_nameunit(kdev), id_entry->name);
			device_set_desc(kdev, id_entry->name);
		}
		DRM_OBSOLETE(kdev);
		return (-BUS_PROBE_GENERIC);
	}

	return (-ENXIO);
}

/*
 * drm_attach_helper: called by a driver at the end of its attach
 * method.
 */
int
drm_attach_helper(device_t kdev, const drm_pci_id_list_t *idlist,
    struct drm_driver *driver)
{
	struct drm_device *dev;
	int vendor, device;
	int ret;

	dev = device_get_softc(kdev);

	vendor = pci_get_vendor(kdev);
	device = pci_get_device(kdev);
	dev->id_entry = drm_find_description(vendor, device, idlist);

	ret = drm_get_pci_dev(kdev, dev, driver);

	return (ret);
}

int
drm_generic_suspend(device_t kdev)
{
	struct drm_device *dev;
	int error;

	DRM_DEBUG_KMS("Starting suspend\n");

	dev = device_get_softc(kdev);
	if (dev->driver->suspend) {
		pm_message_t state;

		state.event = PM_EVENT_SUSPEND;
		error = -dev->driver->suspend(dev, state);
		if (error)
			goto out;
	}

	error = bus_generic_suspend(kdev);

out:
	DRM_DEBUG_KMS("Finished suspend: %d\n", error);

	return error;
}

int
drm_generic_resume(device_t kdev)
{
	struct drm_device *dev;
	int error;

	DRM_DEBUG_KMS("Starting resume\n");

	dev = device_get_softc(kdev);
	if (dev->driver->resume) {
		error = -dev->driver->resume(dev);
		if (error)
			goto out;
	}

	error = bus_generic_resume(kdev);

out:
	DRM_DEBUG_KMS("Finished resume: %d\n", error);

	return error;
}

int
drm_generic_detach(device_t kdev)
{
	struct drm_device *dev;
	int i;

	dev = device_get_softc(kdev);

	drm_put_dev(dev);

	/* Clean up PCI resources allocated by drm_bufs.c.  We're not really
	 * worried about resource consumption while the DRM is inactive (between
	 * lastclose and firstopen or unload) because these aren't actually
	 * taking up KVA, just keeping the PCI resource allocated.
	 */
	for (i = 0; i < DRM_MAX_PCI_RESOURCE; i++) {
		if (dev->pcir[i] == NULL)
			continue;
		bus_release_resource(dev->dev, SYS_RES_MEMORY,
		    dev->pcirid[i], dev->pcir[i]);
		dev->pcir[i] = NULL;
	}

	if (pci_disable_busmaster(dev->dev))
		DRM_ERROR("Request to disable bus-master failed.\n");

	return (0);
}

int
drm_add_busid_modesetting(struct drm_device *dev, struct sysctl_ctx_list *ctx,
    struct sysctl_oid *top)
{
	struct sysctl_oid *oid;

	snprintf(dev->busid_str, sizeof(dev->busid_str),
	     "pci:%04x:%02x:%02x.%d", dev->pci_domain, dev->pci_bus,
	     dev->pci_slot, dev->pci_func);
	oid = SYSCTL_ADD_STRING(ctx, SYSCTL_CHILDREN(top), OID_AUTO, "busid",
	    CTLFLAG_RD, dev->busid_str, 0, NULL);
	if (oid == NULL)
		return (-ENOMEM);
	dev->modesetting = (dev->driver->driver_features & DRIVER_MODESET) != 0;
	oid = SYSCTL_ADD_INT(ctx, SYSCTL_CHILDREN(top), OID_AUTO,
	    "modesetting", CTLFLAG_RD, &dev->modesetting, 0, NULL);
	if (oid == NULL)
		return (-ENOMEM);

	return (0);
}

static int
drm_device_find_capability(struct drm_device *dev, int cap)
{

	return (pci_find_cap(dev->dev, cap, NULL) == 0);
}

int
drm_pci_device_is_agp(struct drm_device *dev)
{
	if (dev->driver->device_is_agp != NULL) {
		int ret;

		/* device_is_agp returns a tristate, 0 = not AGP, 1 = definitely
		 * AGP, 2 = fall back to PCI capability
		 */
		ret = (*dev->driver->device_is_agp)(dev);
		if (ret != DRM_MIGHT_BE_AGP)
			return ret;
	}

	return (drm_device_find_capability(dev, PCIY_AGP));
}

int
drm_pci_device_is_pcie(struct drm_device *dev)
{

	return (drm_device_find_capability(dev, PCIY_EXPRESS));
}

static bool
dmi_found(const struct dmi_system_id *dsi)
{
	char *hw_vendor, *hw_prod;
	int i, slot;
	bool res;

	hw_vendor = kern_getenv("smbios.planar.maker");
	hw_prod = kern_getenv("smbios.planar.product");
	res = true;
	for (i = 0; i < nitems(dsi->matches); i++) {
		slot = dsi->matches[i].slot;
		switch (slot) {
		case DMI_NONE:
			break;
		case DMI_SYS_VENDOR:
		case DMI_BOARD_VENDOR:
			if (hw_vendor != NULL &&
			    !strcmp(hw_vendor, dsi->matches[i].substr)) {
				break;
			} else {
				res = false;
				goto out;
			}
		case DMI_PRODUCT_NAME:
		case DMI_BOARD_NAME:
			if (hw_prod != NULL &&
			    !strcmp(hw_prod, dsi->matches[i].substr)) {
				break;
			} else {
				res = false;
				goto out;
			}
		default:
			res = false;
			goto out;
		}
	}
out:
	freeenv(hw_vendor);
	freeenv(hw_prod);

	return (res);
}

bool
dmi_check_system(const struct dmi_system_id *sysid)
{
	const struct dmi_system_id *dsi;
	bool res;

	for (res = false, dsi = sysid; dsi->matches[0].slot != 0 ; dsi++) {
		if (dmi_found(dsi)) {
			res = true;
			if (dsi->callback != NULL && dsi->callback(dsi))
				break;
		}
	}
	return (res);
}

#if __OS_HAS_MTRR
int
drm_mtrr_add(unsigned long offset, unsigned long size, unsigned int flags)
{
	int act;
	struct mem_range_desc mrdesc;

	mrdesc.mr_base = offset;
	mrdesc.mr_len = size;
	mrdesc.mr_flags = flags;
	act = MEMRANGE_SET_UPDATE;
	strlcpy(mrdesc.mr_owner, "drm", sizeof(mrdesc.mr_owner));
	return (-mem_range_attr_set(&mrdesc, &act));
}

int
drm_mtrr_del(int handle __unused, unsigned long offset, unsigned long size,
    unsigned int flags)
{
	int act;
	struct mem_range_desc mrdesc;

	mrdesc.mr_base = offset;
	mrdesc.mr_len = size;
	mrdesc.mr_flags = flags;
	act = MEMRANGE_SET_REMOVE;
	strlcpy(mrdesc.mr_owner, "drm", sizeof(mrdesc.mr_owner));
	return (-mem_range_attr_set(&mrdesc, &act));
}
#endif

void
drm_clflush_pages(vm_page_t *pages, unsigned long num_pages)
{

#if defined(__i386__) || defined(__amd64__)
	pmap_invalidate_cache_pages(pages, num_pages);
#else
	DRM_ERROR("drm_clflush_pages not implemented on this architecture");
#endif
}

void
drm_clflush_virt_range(char *addr, unsigned long length)
{

#if defined(__i386__) || defined(__amd64__)
	pmap_force_invalidate_cache_range((vm_offset_t)addr,
	    (vm_offset_t)addr + length);
#else
	DRM_ERROR("drm_clflush_virt_range not implemented on this architecture");
#endif
}

void
hex_dump_to_buffer(const void *buf, size_t len, int rowsize, int groupsize,
    char *linebuf, size_t linebuflen, bool ascii __unused)
{
	int i, j, c;

	i = j = 0;

	while (i < len && j <= linebuflen) {
		c = ((const char *)buf)[i];

		if (i != 0) {
			if (i % rowsize == 0) {
				/* Newline required. */
				sprintf(linebuf + j, "\n");
				++j;
			} else if (i % groupsize == 0) {
				/* Space required. */
				sprintf(linebuf + j, " ");
				++j;
			}
		}

		if (j > linebuflen - 4)
			break;

		sprintf(linebuf + j, "%02X", c);
		j += 2;

		++i;
	}

	if (j <= linebuflen)
		sprintf(linebuf + j, "\n");
}

#if DRM_LINUX

#include <sys/sysproto.h>

MODULE_DEPEND(DRIVER_NAME, linux, 1, 1, 1);

#define LINUX_IOCTL_DRM_MIN		0x6400
#define LINUX_IOCTL_DRM_MAX		0x64ff

static linux_ioctl_function_t drm_linux_ioctl;
static struct linux_ioctl_handler drm_handler = {drm_linux_ioctl,
    LINUX_IOCTL_DRM_MIN, LINUX_IOCTL_DRM_MAX};

/* The bits for in/out are switched on Linux */
#define LINUX_IOC_IN	IOC_OUT
#define LINUX_IOC_OUT	IOC_IN

static int
drm_linux_ioctl(DRM_STRUCTPROC *p, struct linux_ioctl_args* args)
{
	int error;
	int cmd = args->cmd;

	args->cmd &= ~(LINUX_IOC_IN | LINUX_IOC_OUT);
	if (cmd & LINUX_IOC_IN)
		args->cmd |= IOC_IN;
	if (cmd & LINUX_IOC_OUT)
		args->cmd |= IOC_OUT;

	error = ioctl(p, (struct ioctl_args *)args);

	return error;
}
#endif /* DRM_LINUX */

static int
drm_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		TUNABLE_INT_FETCH("drm.debug", &drm_debug);
		TUNABLE_INT_FETCH("drm.notyet", &drm_notyet);
		break;
	}
	return (0);
}

static moduledata_t drm_mod = {
	"drmn",
	drm_modevent,
	0
};

DECLARE_MODULE(drmn, drm_mod, SI_SUB_DRIVERS, SI_ORDER_FIRST);
MODULE_VERSION(drmn, 1);
MODULE_DEPEND(drmn, agp, 1, 1, 1);
MODULE_DEPEND(drmn, pci, 1, 1, 1);
MODULE_DEPEND(drmn, mem, 1, 1, 1);
MODULE_DEPEND(drmn, iicbus, 1, 1, 1);
