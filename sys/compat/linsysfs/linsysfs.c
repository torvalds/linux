/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/queue.h>
#include <sys/blist.h>
#include <sys/conf.h>
#include <sys/exec.h>
#include <sys/filedesc.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sbuf.h>
#include <sys/smp.h>
#include <sys/socket.h>
#include <sys/vnode.h>
#include <sys/bus.h>
#include <sys/pciio.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <net/if.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>
#include <vm/vm_object.h>
#include <vm/swap_pager.h>

#include <machine/bus.h>

#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_mib.h>
#include <compat/linux/linux_util.h>
#include <fs/pseudofs/pseudofs.h>

struct scsi_host_queue {
	TAILQ_ENTRY(scsi_host_queue) scsi_host_next;
	char *path;
	char *name;
};

TAILQ_HEAD(,scsi_host_queue) scsi_host_q;

static int host_number = 0;

static int
atoi(const char *str)
{
	return (int)strtol(str, (char **)NULL, 10);
}

/*
 * Filler function for proc_name
 */
static int
linsysfs_scsiname(PFS_FILL_ARGS)
{
	struct scsi_host_queue *scsi_host;
	int index;

	if (strncmp(pn->pn_parent->pn_name, "host", 4) == 0) {
		index = atoi(&pn->pn_parent->pn_name[4]);
	} else {
		sbuf_printf(sb, "unknown\n");
		return (0);
	}
	TAILQ_FOREACH(scsi_host, &scsi_host_q, scsi_host_next) {
		if (index-- == 0) {
			sbuf_printf(sb, "%s\n", scsi_host->name);
			return (0);
		}
	}
	sbuf_printf(sb, "unknown\n");
	return (0);
}

/*
 * Filler function for device sym-link
 */
static int
linsysfs_link_scsi_host(PFS_FILL_ARGS)
{
	struct scsi_host_queue *scsi_host;
	int index;

	if (strncmp(pn->pn_parent->pn_name, "host", 4) == 0) {
		index = atoi(&pn->pn_parent->pn_name[4]);
	} else {
		sbuf_printf(sb, "unknown\n");
		return (0);
	}
	TAILQ_FOREACH(scsi_host, &scsi_host_q, scsi_host_next) {
		if (index-- == 0) {
			sbuf_printf(sb, "../../../devices%s", scsi_host->path);
			return(0);
		}
	}
	sbuf_printf(sb, "unknown\n");
	return (0);
}

static int
linsysfs_fill_data(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "%s", (char *)pn->pn_data);
	return (0);
}

static int
linsysfs_fill_vendor(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "0x%04x\n", pci_get_vendor((device_t)pn->pn_data));
	return (0);
}

static int
linsysfs_fill_device(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "0x%04x\n", pci_get_device((device_t)pn->pn_data));
	return (0);
}

static int
linsysfs_fill_subvendor(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "0x%04x\n", pci_get_subvendor((device_t)pn->pn_data));
	return (0);
}

static int
linsysfs_fill_subdevice(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "0x%04x\n", pci_get_subdevice((device_t)pn->pn_data));
	return (0);
}

static int
linsysfs_fill_revid(PFS_FILL_ARGS)
{
	sbuf_printf(sb, "0x%x\n", pci_get_revid((device_t)pn->pn_data));
	return (0);
}

static int
linsysfs_fill_config(PFS_FILL_ARGS)
{
	uint8_t config[48];
	device_t dev;
	uint32_t reg;

	dev = (device_t)pn->pn_data;
	bzero(config, sizeof(config));
	reg = pci_get_vendor(dev);
	config[0] = reg;
	config[1] = reg >> 8;
	reg = pci_get_device(dev);
	config[2] = reg;
	config[3] = reg >> 8;
	reg = pci_get_revid(dev);
	config[8] = reg;
	reg = pci_get_subvendor(dev);
	config[44] = reg;
	config[45] = reg >> 8;
	reg = pci_get_subdevice(dev);
	config[46] = reg;
	config[47] = reg >> 8;
	sbuf_bcat(sb, config, sizeof(config));
	return (0);
}

/*
 * Filler function for PCI uevent file
 */
static int
linsysfs_fill_uevent_pci(PFS_FILL_ARGS)
{
	device_t dev;

	dev = (device_t)pn->pn_data;
	sbuf_printf(sb, "DRIVER=%s\nPCI_CLASS=%X\nPCI_ID=%04X:%04X\n"
	    "PCI_SUBSYS_ID=%04X:%04X\nPCI_SLOT_NAME=%04d:%02x:%02x.%x\n",
	    linux_driver_get_name_dev(dev), pci_get_class(dev),
	    pci_get_vendor(dev), pci_get_device(dev), pci_get_subvendor(dev),
	    pci_get_subdevice(dev), pci_get_domain(dev), pci_get_bus(dev),
	    pci_get_slot(dev), pci_get_function(dev));
	return (0);
}

/*
 * Filler function for drm uevent file
 */
static int
linsysfs_fill_uevent_drm(PFS_FILL_ARGS)
{
	device_t dev;
	int unit;

	dev = (device_t)pn->pn_data;
	unit = device_get_unit(dev);
	sbuf_printf(sb,
	    "MAJOR=226\nMINOR=%d\nDEVNAME=dri/card%d\nDEVTYPE=dri_minor\n",
	    unit, unit);
	return (0);
}

static char *
get_full_pfs_path(struct pfs_node *cur)
{
	char *temp, *path;

	temp = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	path = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
	path[0] = '\0';

	do {
		snprintf(temp, MAXPATHLEN, "%s/%s", cur->pn_name, path);
		strlcpy(path, temp, MAXPATHLEN);
		cur = cur->pn_parent;
	} while (cur->pn_parent != NULL);

	path[strlen(path) - 1] = '\0'; /* remove extra slash */
	free(temp, M_TEMP);
	return (path);
}

/*
 * Filler function for symlink from drm char device to PCI device
 */
static int
linsysfs_fill_vgapci(PFS_FILL_ARGS)
{
	char *path;

	path = get_full_pfs_path((struct pfs_node*)pn->pn_data);
	sbuf_printf(sb, "../../../%s", path);
	free(path, M_TEMP);
	return (0);
}

#undef PCI_DEV
#define PCI_DEV "pci"
#define DRMN_DEV "drmn"
static int
linsysfs_run_bus(device_t dev, struct pfs_node *dir, struct pfs_node *scsi,
    struct pfs_node *chardev, struct pfs_node *drm, char *path, char *prefix)
{
	struct scsi_host_queue *scsi_host;
	struct pfs_node *sub_dir, *cur_file;
	int i, nchildren, error;
	device_t *children, parent;
	devclass_t devclass;
	const char *name = NULL;
	struct pci_devinfo *dinfo;
	char *device, *host, *new_path, *devname;

	new_path = path;
	devname = malloc(16, M_TEMP, M_WAITOK);

	parent = device_get_parent(dev);
	if (parent) {
		devclass = device_get_devclass(parent);
		if (devclass != NULL)
			name = devclass_get_name(devclass);
		if (name && strcmp(name, PCI_DEV) == 0) {
			dinfo = device_get_ivars(dev);
			if (dinfo) {
				device = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
				new_path = malloc(MAXPATHLEN, M_TEMP,
				    M_WAITOK);
				new_path[0] = '\000';
				strcpy(new_path, path);
				host = malloc(MAXPATHLEN, M_TEMP, M_WAITOK);
				device[0] = '\000';
				sprintf(device, "%s:%02x:%02x.%x",
				    prefix,
				    dinfo->cfg.bus,
				    dinfo->cfg.slot,
				    dinfo->cfg.func);
				strcat(new_path, "/");
				strcat(new_path, device);
				dir = pfs_create_dir(dir, device,
				    NULL, NULL, NULL, 0);
				cur_file = pfs_create_file(dir, "vendor",
				    &linsysfs_fill_vendor, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir, "device",
				    &linsysfs_fill_device, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir,
				    "subsystem_vendor",
				    &linsysfs_fill_subvendor, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir,
				    "subsystem_device",
				    &linsysfs_fill_subdevice, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir, "revision",
				    &linsysfs_fill_revid, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir, "config",
				    &linsysfs_fill_config, NULL, NULL, NULL,
				    PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_file(dir, "uevent",
				    &linsysfs_fill_uevent_pci, NULL, NULL,
				    NULL, PFS_RD);
				cur_file->pn_data = (void*)dev;
				cur_file = pfs_create_link(dir, "subsystem",
				    &linsysfs_fill_data, NULL, NULL, NULL, 0);
				/* libdrm just checks that the link ends in "/pci" */
				cur_file->pn_data = "/sys/bus/pci";

				if (dinfo->cfg.baseclass == PCIC_STORAGE) {
					/* DJA only make this if needed */
					sprintf(host, "host%d", host_number++);
					strcat(new_path, "/");
					strcat(new_path, host);
					pfs_create_dir(dir, host,
					    NULL, NULL, NULL, 0);
					scsi_host = malloc(sizeof(
					    struct scsi_host_queue),
					    M_DEVBUF, M_NOWAIT);
					scsi_host->path = malloc(
					    strlen(new_path) + 1,
					    M_DEVBUF, M_NOWAIT);
					scsi_host->path[0] = '\000';
					bcopy(new_path, scsi_host->path,
					    strlen(new_path) + 1);
					scsi_host->name = "unknown";

					sub_dir = pfs_create_dir(scsi, host,
					    NULL, NULL, NULL, 0);
					pfs_create_link(sub_dir, "device",
					    &linsysfs_link_scsi_host,
					    NULL, NULL, NULL, 0);
					pfs_create_file(sub_dir, "proc_name",
					    &linsysfs_scsiname,
					    NULL, NULL, NULL, PFS_RD);
					scsi_host->name
					    = linux_driver_get_name_dev(dev);
					TAILQ_INSERT_TAIL(&scsi_host_q,
					    scsi_host, scsi_host_next);
				}
				free(device, M_TEMP);
				free(host, M_TEMP);
			}
		}

		devclass = device_get_devclass(dev);
		if (devclass != NULL)
			name = devclass_get_name(devclass);
		else
			name = NULL;
		if (name != NULL && strcmp(name, DRMN_DEV) == 0 &&
		    device_get_unit(dev) >= 0) {
			dinfo = device_get_ivars(parent);
			if (dinfo != NULL && dinfo->cfg.baseclass == PCIC_DISPLAY) {
				sprintf(devname, "226:%d",
				    device_get_unit(dev));
				sub_dir = pfs_create_dir(chardev,
				    devname, NULL, NULL, NULL, 0);
				cur_file = pfs_create_link(sub_dir,
				    "device", &linsysfs_fill_vgapci, NULL,
				    NULL, NULL, PFS_RD);
				cur_file->pn_data = (void*)dir;
				cur_file = pfs_create_file(sub_dir,
				    "uevent", &linsysfs_fill_uevent_drm, NULL,
				    NULL, NULL, PFS_RD);
				cur_file->pn_data = (void*)dev;
				sprintf(devname, "card%d",
				    device_get_unit(dev));
				sub_dir = pfs_create_dir(drm,
				    devname, NULL, NULL, NULL, 0);
				cur_file = pfs_create_link(sub_dir,
				    "device", &linsysfs_fill_vgapci, NULL,
				    NULL, NULL, PFS_RD);
				cur_file->pn_data = (void*)dir;
			}
		}
	}

	error = device_get_children(dev, &children, &nchildren);
	if (error == 0) {
		for (i = 0; i < nchildren; i++)
			if (children[i])
				linsysfs_run_bus(children[i], dir, scsi,
				    chardev, drm, new_path, prefix);
		free(children, M_TEMP);
	}
	if (new_path != path)
		free(new_path, M_TEMP);
	free(devname, M_TEMP);

	return (1);
}

/*
 * Filler function for sys/devices/system/cpu/online
 */
static int
linsysfs_cpuonline(PFS_FILL_ARGS)
{

	sbuf_printf(sb, "%d-%d\n", CPU_FIRST(), mp_maxid);
	return (0);
}

/*
 * Filler function for sys/devices/system/cpu/cpuX/online
 */
static int
linsysfs_cpuxonline(PFS_FILL_ARGS)
{

	sbuf_printf(sb, "1\n");
	return (0);
}

static void
linsysfs_listcpus(struct pfs_node *dir)
{
	struct pfs_node *cpu;
	char *name;
	int i, count, len;

	len = 1;
	count = mp_maxcpus;
	while (count > 10) {
		count /= 10;
		len++;
	}
	len += sizeof("cpu");
	name = malloc(len, M_TEMP, M_WAITOK);

	for (i = 0; i < mp_ncpus; ++i) {
		/* /sys/devices/system/cpu/cpuX */
		sprintf(name, "cpu%d", i);
		cpu = pfs_create_dir(dir, name, NULL, NULL, NULL, 0);

		pfs_create_file(cpu, "online", &linsysfs_cpuxonline,
		    NULL, NULL, NULL, PFS_RD);
	}
	free(name, M_TEMP);
}

/*
 * Constructor
 */
static int
linsysfs_init(PFS_INIT_ARGS)
{
	struct pfs_node *root;
	struct pfs_node *class;
	struct pfs_node *dir, *sys, *cpu;
	struct pfs_node *drm;
	struct pfs_node *pci;
	struct pfs_node *scsi;
	struct pfs_node *devdir, *chardev;
	devclass_t devclass;
	device_t dev;

	TAILQ_INIT(&scsi_host_q);

	root = pi->pi_root;

	/* /sys/class/... */
	class = pfs_create_dir(root, "class", NULL, NULL, NULL, 0);
	scsi = pfs_create_dir(class, "scsi_host", NULL, NULL, NULL, 0);
	drm = pfs_create_dir(class, "drm", NULL, NULL, NULL, 0);

	/* /sys/dev/... */
	devdir = pfs_create_dir(root, "dev", NULL, NULL, NULL, 0);
	chardev = pfs_create_dir(devdir, "char", NULL, NULL, NULL, 0);

	/* /sys/devices/... */
	dir = pfs_create_dir(root, "devices", NULL, NULL, NULL, 0);
	pci = pfs_create_dir(dir, "pci0000:00", NULL, NULL, NULL, 0);

	devclass = devclass_find("root");
	if (devclass == NULL) {
		return (0);
	}

	dev = devclass_get_device(devclass, 0);
	linsysfs_run_bus(dev, pci, scsi, chardev, drm, "/pci0000:00", "0000");

	/* /sys/devices/system */
	sys = pfs_create_dir(dir, "system", NULL, NULL, NULL, 0);

	/* /sys/devices/system/cpu */
	cpu = pfs_create_dir(sys, "cpu", NULL, NULL, NULL, 0);

	pfs_create_file(cpu, "online", &linsysfs_cpuonline,
	    NULL, NULL, NULL, PFS_RD);

	linsysfs_listcpus(cpu);

	return (0);
}

/*
 * Destructor
 */
static int
linsysfs_uninit(PFS_INIT_ARGS)
{
	struct scsi_host_queue *scsi_host, *scsi_host_tmp;

	TAILQ_FOREACH_SAFE(scsi_host, &scsi_host_q, scsi_host_next,
	    scsi_host_tmp) {
		TAILQ_REMOVE(&scsi_host_q, scsi_host, scsi_host_next);
		free(scsi_host->path, M_TEMP);
		free(scsi_host, M_TEMP);
	}

	return (0);
}

PSEUDOFS(linsysfs, 1, VFCF_JAIL);
#if defined(__aarch64__) || defined(__amd64__)
MODULE_DEPEND(linsysfs, linux_common, 1, 1, 1);
#else
MODULE_DEPEND(linsysfs, linux, 1, 1, 1);
#endif
