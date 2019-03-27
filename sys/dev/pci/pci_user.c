/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_bus.h"	/* XXX trim includes */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/linker.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/rwlock.h>
#include <sys/sglist.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include "pcib_if.h"
#include "pci_if.h"

#ifdef COMPAT_FREEBSD32
struct pci_conf32 {
	struct pcisel	pc_sel;		/* domain+bus+slot+function */
	u_int8_t	pc_hdr;		/* PCI header type */
	u_int16_t	pc_subvendor;	/* card vendor ID */
	u_int16_t	pc_subdevice;	/* card device ID, assigned by
					   card vendor */
	u_int16_t	pc_vendor;	/* chip vendor ID */
	u_int16_t	pc_device;	/* chip device ID, assigned by
					   chip vendor */
	u_int8_t	pc_class;	/* chip PCI class */
	u_int8_t	pc_subclass;	/* chip PCI subclass */
	u_int8_t	pc_progif;	/* chip PCI programming interface */
	u_int8_t	pc_revid;	/* chip revision ID */
	char		pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_int32_t	pd_unit;	/* device unit number */
};

struct pci_match_conf32 {
	struct pcisel		pc_sel;		/* domain+bus+slot+function */
	char			pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_int32_t		pd_unit;	/* Unit number */
	u_int16_t		pc_vendor;	/* PCI Vendor ID */
	u_int16_t		pc_device;	/* PCI Device ID */
	u_int8_t		pc_class;	/* PCI class */
	u_int32_t		flags;		/* Matching expression */
};

struct pci_conf_io32 {
	u_int32_t		pat_buf_len;	/* pattern buffer length */
	u_int32_t		num_patterns;	/* number of patterns */
	u_int32_t		patterns;	/* struct pci_match_conf ptr */
	u_int32_t		match_buf_len;	/* match buffer length */
	u_int32_t		num_matches;	/* number of matches returned */
	u_int32_t		matches;	/* struct pci_conf ptr */
	u_int32_t		offset;		/* offset into device list */
	u_int32_t		generation;	/* device list generation */
	u_int32_t		status;		/* request status */
};

#define	PCIOCGETCONF32	_IOC_NEWTYPE(PCIOCGETCONF, struct pci_conf_io32)
#endif

/*
 * This is the user interface to PCI configuration space.
 */

static d_open_t 	pci_open;
static d_close_t	pci_close;
static d_ioctl_t	pci_ioctl;

struct cdevsw pcicdev = {
	.d_version =	D_VERSION,
	.d_flags =	D_NEEDGIANT,
	.d_open =	pci_open,
	.d_close =	pci_close,
	.d_ioctl =	pci_ioctl,
	.d_name =	"pci",
};
  
static int
pci_open(struct cdev *dev, int oflags, int devtype, struct thread *td)
{
	int error;

	if (oflags & FWRITE) {
		error = securelevel_gt(td->td_ucred, 0);
		if (error)
			return (error);
	}

	return (0);
}

static int
pci_close(struct cdev *dev, int flag, int devtype, struct thread *td)
{
	return 0;
}

/*
 * Match a single pci_conf structure against an array of pci_match_conf
 * structures.  The first argument, 'matches', is an array of num_matches
 * pci_match_conf structures.  match_buf is a pointer to the pci_conf
 * structure that will be compared to every entry in the matches array.
 * This function returns 1 on failure, 0 on success.
 */
static int
pci_conf_match_native(struct pci_match_conf *matches, int num_matches,
	       struct pci_conf *match_buf)
{
	int i;

	if ((matches == NULL) || (match_buf == NULL) || (num_matches <= 0))
		return(1);

	for (i = 0; i < num_matches; i++) {
		/*
		 * I'm not sure why someone would do this...but...
		 */
		if (matches[i].flags == PCI_GETCONF_NO_MATCH)
			continue;

		/*
		 * Look at each of the match flags.  If it's set, do the
		 * comparison.  If the comparison fails, we don't have a
		 * match, go on to the next item if there is one.
		 */
		if (((matches[i].flags & PCI_GETCONF_MATCH_DOMAIN) != 0)
		 && (match_buf->pc_sel.pc_domain !=
		 matches[i].pc_sel.pc_domain))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_BUS) != 0)
		 && (match_buf->pc_sel.pc_bus != matches[i].pc_sel.pc_bus))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEV) != 0)
		 && (match_buf->pc_sel.pc_dev != matches[i].pc_sel.pc_dev))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_FUNC) != 0)
		 && (match_buf->pc_sel.pc_func != matches[i].pc_sel.pc_func))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_VENDOR) != 0) 
		 && (match_buf->pc_vendor != matches[i].pc_vendor))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEVICE) != 0)
		 && (match_buf->pc_device != matches[i].pc_device))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_CLASS) != 0)
		 && (match_buf->pc_class != matches[i].pc_class))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_UNIT) != 0)
		 && (match_buf->pd_unit != matches[i].pd_unit))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_NAME) != 0)
		 && (strncmp(matches[i].pd_name, match_buf->pd_name,
			     sizeof(match_buf->pd_name)) != 0))
			continue;

		return(0);
	}

	return(1);
}

#ifdef COMPAT_FREEBSD32
static int
pci_conf_match32(struct pci_match_conf32 *matches, int num_matches,
	       struct pci_conf *match_buf)
{
	int i;

	if ((matches == NULL) || (match_buf == NULL) || (num_matches <= 0))
		return(1);

	for (i = 0; i < num_matches; i++) {
		/*
		 * I'm not sure why someone would do this...but...
		 */
		if (matches[i].flags == PCI_GETCONF_NO_MATCH)
			continue;

		/*
		 * Look at each of the match flags.  If it's set, do the
		 * comparison.  If the comparison fails, we don't have a
		 * match, go on to the next item if there is one.
		 */
		if (((matches[i].flags & PCI_GETCONF_MATCH_DOMAIN) != 0)
		 && (match_buf->pc_sel.pc_domain !=
		 matches[i].pc_sel.pc_domain))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_BUS) != 0)
		 && (match_buf->pc_sel.pc_bus != matches[i].pc_sel.pc_bus))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEV) != 0)
		 && (match_buf->pc_sel.pc_dev != matches[i].pc_sel.pc_dev))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_FUNC) != 0)
		 && (match_buf->pc_sel.pc_func != matches[i].pc_sel.pc_func))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_VENDOR) != 0)
		 && (match_buf->pc_vendor != matches[i].pc_vendor))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEVICE) != 0)
		 && (match_buf->pc_device != matches[i].pc_device))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_CLASS) != 0)
		 && (match_buf->pc_class != matches[i].pc_class))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_UNIT) != 0)
		 && (match_buf->pd_unit != matches[i].pd_unit))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_NAME) != 0)
		 && (strncmp(matches[i].pd_name, match_buf->pd_name,
			     sizeof(match_buf->pd_name)) != 0))
			continue;

		return(0);
	}

	return(1);
}
#endif	/* COMPAT_FREEBSD32 */

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6)
#define PRE7_COMPAT

typedef enum {
	PCI_GETCONF_NO_MATCH_OLD	= 0x00,
	PCI_GETCONF_MATCH_BUS_OLD	= 0x01,
	PCI_GETCONF_MATCH_DEV_OLD	= 0x02,
	PCI_GETCONF_MATCH_FUNC_OLD	= 0x04,
	PCI_GETCONF_MATCH_NAME_OLD	= 0x08,
	PCI_GETCONF_MATCH_UNIT_OLD	= 0x10,
	PCI_GETCONF_MATCH_VENDOR_OLD	= 0x20,
	PCI_GETCONF_MATCH_DEVICE_OLD	= 0x40,
	PCI_GETCONF_MATCH_CLASS_OLD	= 0x80
} pci_getconf_flags_old;

struct pcisel_old {
	u_int8_t	pc_bus;		/* bus number */
	u_int8_t	pc_dev;		/* device on this bus */
	u_int8_t	pc_func;	/* function on this device */
};

struct pci_conf_old {
	struct pcisel_old pc_sel;	/* bus+slot+function */
	u_int8_t	pc_hdr;		/* PCI header type */
	u_int16_t	pc_subvendor;	/* card vendor ID */
	u_int16_t	pc_subdevice;	/* card device ID, assigned by
					   card vendor */
	u_int16_t	pc_vendor;	/* chip vendor ID */
	u_int16_t	pc_device;	/* chip device ID, assigned by
					   chip vendor */
	u_int8_t	pc_class;	/* chip PCI class */
	u_int8_t	pc_subclass;	/* chip PCI subclass */
	u_int8_t	pc_progif;	/* chip PCI programming interface */
	u_int8_t	pc_revid;	/* chip revision ID */
	char		pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_long		pd_unit;	/* device unit number */
};

struct pci_match_conf_old {
	struct pcisel_old	pc_sel;		/* bus+slot+function */
	char			pd_name[PCI_MAXNAMELEN + 1];  /* device name */
	u_long			pd_unit;	/* Unit number */
	u_int16_t		pc_vendor;	/* PCI Vendor ID */
	u_int16_t		pc_device;	/* PCI Device ID */
	u_int8_t		pc_class;	/* PCI class */
	pci_getconf_flags_old	flags;		/* Matching expression */
};

struct pci_io_old {
	struct pcisel_old pi_sel;	/* device to operate on */
	int		pi_reg;		/* configuration register to examine */
	int		pi_width;	/* width (in bytes) of read or write */
	u_int32_t	pi_data;	/* data to write or result of read */
};

#ifdef COMPAT_FREEBSD32
struct pci_conf_old32 {
	struct pcisel_old pc_sel;	/* bus+slot+function */
	uint8_t		pc_hdr;		/* PCI header type */
	uint16_t	pc_subvendor;	/* card vendor ID */
	uint16_t	pc_subdevice;	/* card device ID, assigned by
					   card vendor */
	uint16_t	pc_vendor;	/* chip vendor ID */
	uint16_t	pc_device;	/* chip device ID, assigned by
					   chip vendor */
	uint8_t		pc_class;	/* chip PCI class */
	uint8_t		pc_subclass;	/* chip PCI subclass */
	uint8_t		pc_progif;	/* chip PCI programming interface */
	uint8_t		pc_revid;	/* chip revision ID */
	char		pd_name[PCI_MAXNAMELEN + 1]; /* device name */
	uint32_t	pd_unit;	/* device unit number (u_long) */
};

struct pci_match_conf_old32 {
	struct pcisel_old pc_sel;	/* bus+slot+function */
	char		pd_name[PCI_MAXNAMELEN + 1]; /* device name */
	uint32_t	pd_unit;	/* Unit number (u_long) */
	uint16_t	pc_vendor;	/* PCI Vendor ID */
	uint16_t	pc_device;	/* PCI Device ID */
	uint8_t		pc_class;	/* PCI class */
	pci_getconf_flags_old flags;	/* Matching expression */
};

#define	PCIOCGETCONF_OLD32	_IOWR('p', 1, struct pci_conf_io32)
#endif	/* COMPAT_FREEBSD32 */

#define	PCIOCGETCONF_OLD	_IOWR('p', 1, struct pci_conf_io)
#define	PCIOCREAD_OLD		_IOWR('p', 2, struct pci_io_old)
#define	PCIOCWRITE_OLD		_IOWR('p', 3, struct pci_io_old)

static int
pci_conf_match_old(struct pci_match_conf_old *matches, int num_matches,
    struct pci_conf *match_buf)
{
	int i;

	if ((matches == NULL) || (match_buf == NULL) || (num_matches <= 0))
		return(1);

	for (i = 0; i < num_matches; i++) {
		if (match_buf->pc_sel.pc_domain != 0)
			continue;

		/*
		 * I'm not sure why someone would do this...but...
		 */
		if (matches[i].flags == PCI_GETCONF_NO_MATCH_OLD)
			continue;

		/*
		 * Look at each of the match flags.  If it's set, do the
		 * comparison.  If the comparison fails, we don't have a
		 * match, go on to the next item if there is one.
		 */
		if (((matches[i].flags & PCI_GETCONF_MATCH_BUS_OLD) != 0)
		 && (match_buf->pc_sel.pc_bus != matches[i].pc_sel.pc_bus))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEV_OLD) != 0)
		 && (match_buf->pc_sel.pc_dev != matches[i].pc_sel.pc_dev))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_FUNC_OLD) != 0)
		 && (match_buf->pc_sel.pc_func != matches[i].pc_sel.pc_func))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_VENDOR_OLD) != 0)
		 && (match_buf->pc_vendor != matches[i].pc_vendor))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEVICE_OLD) != 0)
		 && (match_buf->pc_device != matches[i].pc_device))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_CLASS_OLD) != 0)
		 && (match_buf->pc_class != matches[i].pc_class))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_UNIT_OLD) != 0)
		 && (match_buf->pd_unit != matches[i].pd_unit))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_NAME_OLD) != 0)
		 && (strncmp(matches[i].pd_name, match_buf->pd_name,
			     sizeof(match_buf->pd_name)) != 0))
			continue;

		return(0);
	}

	return(1);
}

#ifdef COMPAT_FREEBSD32
static int
pci_conf_match_old32(struct pci_match_conf_old32 *matches, int num_matches,
    struct pci_conf *match_buf)
{
	int i;

	if ((matches == NULL) || (match_buf == NULL) || (num_matches <= 0))
		return(1);

	for (i = 0; i < num_matches; i++) {
		if (match_buf->pc_sel.pc_domain != 0)
			continue;

		/*
		 * I'm not sure why someone would do this...but...
		 */
		if (matches[i].flags == PCI_GETCONF_NO_MATCH_OLD)
			continue;

		/*
		 * Look at each of the match flags.  If it's set, do the
		 * comparison.  If the comparison fails, we don't have a
		 * match, go on to the next item if there is one.
		 */
		if (((matches[i].flags & PCI_GETCONF_MATCH_BUS_OLD) != 0) &&
		    (match_buf->pc_sel.pc_bus != matches[i].pc_sel.pc_bus))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEV_OLD) != 0) &&
		    (match_buf->pc_sel.pc_dev != matches[i].pc_sel.pc_dev))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_FUNC_OLD) != 0) &&
		    (match_buf->pc_sel.pc_func != matches[i].pc_sel.pc_func))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_VENDOR_OLD) != 0) &&
		    (match_buf->pc_vendor != matches[i].pc_vendor))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_DEVICE_OLD) != 0) &&
		    (match_buf->pc_device != matches[i].pc_device))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_CLASS_OLD) != 0) &&
		    (match_buf->pc_class != matches[i].pc_class))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_UNIT_OLD) != 0) &&
		    ((u_int32_t)match_buf->pd_unit != matches[i].pd_unit))
			continue;

		if (((matches[i].flags & PCI_GETCONF_MATCH_NAME_OLD) != 0) &&
		    (strncmp(matches[i].pd_name, match_buf->pd_name,
		    sizeof(match_buf->pd_name)) != 0))
			continue;

		return (0);
	}

	return (1);
}
#endif	/* COMPAT_FREEBSD32 */
#endif	/* !PRE7_COMPAT */

union pci_conf_union {
	struct pci_conf		pc;
#ifdef COMPAT_FREEBSD32
	struct pci_conf32	pc32;
#endif
#ifdef PRE7_COMPAT
	struct pci_conf_old	pco;
#ifdef COMPAT_FREEBSD32
	struct pci_conf_old32	pco32;
#endif
#endif
};

static int
pci_conf_match(u_long cmd, struct pci_match_conf *matches, int num_matches,
    struct pci_conf *match_buf)
{

	switch (cmd) {
	case PCIOCGETCONF:
		return (pci_conf_match_native(
		    (struct pci_match_conf *)matches, num_matches, match_buf));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
		return (pci_conf_match32((struct pci_match_conf32 *)matches,
		    num_matches, match_buf));
#endif
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
		return (pci_conf_match_old(
		    (struct pci_match_conf_old *)matches, num_matches,
		    match_buf));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF_OLD32:
		return (pci_conf_match_old32(
		    (struct pci_match_conf_old32 *)matches, num_matches,
		    match_buf));
#endif
#endif
	default:
		/* programmer error */
		return (0);
	}
}

/*
 * Like PVE_NEXT but takes an explicit length since 'pve' is a user
 * pointer that cannot be dereferenced.
 */
#define	PVE_NEXT_LEN(pve, datalen)					\
	((struct pci_vpd_element *)((char *)(pve) +			\
	    sizeof(struct pci_vpd_element) + (datalen)))

static int
pci_list_vpd(device_t dev, struct pci_list_vpd_io *lvio)
{
	struct pci_vpd_element vpd_element, *vpd_user;
	struct pcicfg_vpd *vpd;
	size_t len;
	int error, i;

	vpd = pci_fetch_vpd_list(dev);
	if (vpd->vpd_reg == 0 || vpd->vpd_ident == NULL)
		return (ENXIO);

	/*
	 * Calculate the amount of space needed in the data buffer.  An
	 * identifier element is always present followed by the read-only
	 * and read-write keywords.
	 */
	len = sizeof(struct pci_vpd_element) + strlen(vpd->vpd_ident);
	for (i = 0; i < vpd->vpd_rocnt; i++)
		len += sizeof(struct pci_vpd_element) + vpd->vpd_ros[i].len;
	for (i = 0; i < vpd->vpd_wcnt; i++)
		len += sizeof(struct pci_vpd_element) + vpd->vpd_w[i].len;

	if (lvio->plvi_len == 0) {
		lvio->plvi_len = len;
		return (0);
	}
	if (lvio->plvi_len < len) {
		lvio->plvi_len = len;
		return (ENOMEM);
	}

	/*
	 * Copyout the identifier string followed by each keyword and
	 * value.
	 */
	vpd_user = lvio->plvi_data;
	vpd_element.pve_keyword[0] = '\0';
	vpd_element.pve_keyword[1] = '\0';
	vpd_element.pve_flags = PVE_FLAG_IDENT;
	vpd_element.pve_datalen = strlen(vpd->vpd_ident);
	error = copyout(&vpd_element, vpd_user, sizeof(vpd_element));
	if (error)
		return (error);
	error = copyout(vpd->vpd_ident, vpd_user->pve_data,
	    strlen(vpd->vpd_ident));
	if (error)
		return (error);
	vpd_user = PVE_NEXT_LEN(vpd_user, vpd_element.pve_datalen);
	vpd_element.pve_flags = 0;
	for (i = 0; i < vpd->vpd_rocnt; i++) {
		vpd_element.pve_keyword[0] = vpd->vpd_ros[i].keyword[0];
		vpd_element.pve_keyword[1] = vpd->vpd_ros[i].keyword[1];
		vpd_element.pve_datalen = vpd->vpd_ros[i].len;
		error = copyout(&vpd_element, vpd_user, sizeof(vpd_element));
		if (error)
			return (error);
		error = copyout(vpd->vpd_ros[i].value, vpd_user->pve_data,
		    vpd->vpd_ros[i].len);
		if (error)
			return (error);
		vpd_user = PVE_NEXT_LEN(vpd_user, vpd_element.pve_datalen);
	}
	vpd_element.pve_flags = PVE_FLAG_RW;
	for (i = 0; i < vpd->vpd_wcnt; i++) {
		vpd_element.pve_keyword[0] = vpd->vpd_w[i].keyword[0];
		vpd_element.pve_keyword[1] = vpd->vpd_w[i].keyword[1];
		vpd_element.pve_datalen = vpd->vpd_w[i].len;
		error = copyout(&vpd_element, vpd_user, sizeof(vpd_element));
		if (error)
			return (error);
		error = copyout(vpd->vpd_w[i].value, vpd_user->pve_data,
		    vpd->vpd_w[i].len);
		if (error)
			return (error);
		vpd_user = PVE_NEXT_LEN(vpd_user, vpd_element.pve_datalen);
	}
	KASSERT((char *)vpd_user - (char *)lvio->plvi_data == len,
	    ("length mismatch"));
	lvio->plvi_len = len;
	return (0);
}

static size_t
pci_match_conf_size(u_long cmd)
{

	switch (cmd) {
	case PCIOCGETCONF:
		return (sizeof(struct pci_match_conf));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
		return (sizeof(struct pci_match_conf32));
#endif
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
		return (sizeof(struct pci_match_conf_old));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF_OLD32:
		return (sizeof(struct pci_match_conf_old32));
#endif
#endif
	default:
		/* programmer error */
		return (0);
	}
}

static size_t
pci_conf_size(u_long cmd)
{

	switch (cmd) {
	case PCIOCGETCONF:
		return (sizeof(struct pci_conf));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
		return (sizeof(struct pci_conf32));
#endif
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
		return (sizeof(struct pci_conf_old));
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF_OLD32:
		return (sizeof(struct pci_conf_old32));
#endif
#endif
	default:
		/* programmer error */
		return (0);
	}
}

static void
pci_conf_io_init(struct pci_conf_io *cio, caddr_t data, u_long cmd)
{
#if defined(COMPAT_FREEBSD32)
	struct pci_conf_io32 *cio32;
#endif

	switch (cmd) {
	case PCIOCGETCONF:
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
#endif
		*cio = *(struct pci_conf_io *)data;
		return;

#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD32:
#endif
               cio32 = (struct pci_conf_io32 *)data;
               cio->pat_buf_len = cio32->pat_buf_len;
               cio->num_patterns = cio32->num_patterns;
               cio->patterns = (void *)(uintptr_t)cio32->patterns;
               cio->match_buf_len = cio32->match_buf_len;
               cio->num_matches = cio32->num_matches;
               cio->matches = (void *)(uintptr_t)cio32->matches;
               cio->offset = cio32->offset;
               cio->generation = cio32->generation;
               cio->status = cio32->status;
               return;
#endif

	default:
		/* programmer error */
		return;
	}
}

static void
pci_conf_io_update_data(const struct pci_conf_io *cio, caddr_t data,
    u_long cmd)
{
	struct pci_conf_io *d_cio;
#if defined(COMPAT_FREEBSD32)
	struct pci_conf_io32 *cio32;
#endif

	switch (cmd) {
	case PCIOCGETCONF:
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
#endif
		d_cio = (struct pci_conf_io *)data;
		d_cio->status = cio->status;
		d_cio->generation = cio->generation;
		d_cio->offset = cio->offset;
		d_cio->num_matches = cio->num_matches;
		return;

#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD32:
#endif
		cio32 = (struct pci_conf_io32 *)data;

		cio32->status = cio->status;
		cio32->generation = cio->generation;
		cio32->offset = cio->offset;
		cio32->num_matches = cio->num_matches;
		return;
#endif

	default:
		/* programmer error */
		return;
	}
}

static void
pci_conf_for_copyout(const struct pci_conf *pcp, union pci_conf_union *pcup,
    u_long cmd)
{

	memset(pcup, 0, sizeof(*pcup));

	switch (cmd) {
	case PCIOCGETCONF:
		pcup->pc = *pcp;
		return;

#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
		pcup->pc32.pc_sel = pcp->pc_sel;
		pcup->pc32.pc_hdr = pcp->pc_hdr;
		pcup->pc32.pc_subvendor = pcp->pc_subvendor;
		pcup->pc32.pc_subdevice = pcp->pc_subdevice;
		pcup->pc32.pc_vendor = pcp->pc_vendor;
		pcup->pc32.pc_device = pcp->pc_device;
		pcup->pc32.pc_class = pcp->pc_class;
		pcup->pc32.pc_subclass = pcp->pc_subclass;
		pcup->pc32.pc_progif = pcp->pc_progif;
		pcup->pc32.pc_revid = pcp->pc_revid;
		strlcpy(pcup->pc32.pd_name, pcp->pd_name,
		    sizeof(pcup->pc32.pd_name));
		pcup->pc32.pd_unit = (uint32_t)pcp->pd_unit;
		return;
#endif

#ifdef PRE7_COMPAT
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF_OLD32:
		pcup->pco32.pc_sel.pc_bus = pcp->pc_sel.pc_bus;
		pcup->pco32.pc_sel.pc_dev = pcp->pc_sel.pc_dev;
		pcup->pco32.pc_sel.pc_func = pcp->pc_sel.pc_func;
		pcup->pco32.pc_hdr = pcp->pc_hdr;
		pcup->pco32.pc_subvendor = pcp->pc_subvendor;
		pcup->pco32.pc_subdevice = pcp->pc_subdevice;
		pcup->pco32.pc_vendor = pcp->pc_vendor;
		pcup->pco32.pc_device = pcp->pc_device;
		pcup->pco32.pc_class = pcp->pc_class;
		pcup->pco32.pc_subclass = pcp->pc_subclass;
		pcup->pco32.pc_progif = pcp->pc_progif;
		pcup->pco32.pc_revid = pcp->pc_revid;
		strlcpy(pcup->pco32.pd_name, pcp->pd_name,
		    sizeof(pcup->pco32.pd_name));
		pcup->pco32.pd_unit = (uint32_t)pcp->pd_unit;
		return;

#endif /* COMPAT_FREEBSD32 */
	case PCIOCGETCONF_OLD:
		pcup->pco.pc_sel.pc_bus = pcp->pc_sel.pc_bus;
		pcup->pco.pc_sel.pc_dev = pcp->pc_sel.pc_dev;
		pcup->pco.pc_sel.pc_func = pcp->pc_sel.pc_func;
		pcup->pco.pc_hdr = pcp->pc_hdr;
		pcup->pco.pc_subvendor = pcp->pc_subvendor;
		pcup->pco.pc_subdevice = pcp->pc_subdevice;
		pcup->pco.pc_vendor = pcp->pc_vendor;
		pcup->pco.pc_device = pcp->pc_device;
		pcup->pco.pc_class = pcp->pc_class;
		pcup->pco.pc_subclass = pcp->pc_subclass;
		pcup->pco.pc_progif = pcp->pc_progif;
		pcup->pco.pc_revid = pcp->pc_revid;
		strlcpy(pcup->pco.pd_name, pcp->pd_name,
		    sizeof(pcup->pco.pd_name));
		pcup->pco.pd_unit = pcp->pd_unit;
		return;
#endif /* PRE7_COMPAT */

	default:
		/* programmer error */
		return;
	}
}

static int
pci_bar_mmap(device_t pcidev, struct pci_bar_mmap *pbm)
{
	vm_map_t map;
	vm_object_t obj;
	struct thread *td;
	struct sglist *sg;
	struct pci_map *pm;
	vm_paddr_t pbase;
	vm_size_t plen;
	vm_offset_t addr;
	vm_prot_t prot;
	int error, flags;

	td = curthread;
	map = &td->td_proc->p_vmspace->vm_map;
	if ((pbm->pbm_flags & ~(PCIIO_BAR_MMAP_FIXED | PCIIO_BAR_MMAP_EXCL |
	    PCIIO_BAR_MMAP_RW | PCIIO_BAR_MMAP_ACTIVATE)) != 0 ||
	    pbm->pbm_memattr != (vm_memattr_t)pbm->pbm_memattr ||
	    !pmap_is_valid_memattr(map->pmap, pbm->pbm_memattr))
		return (EINVAL);

	/* Fetch the BAR physical base and length. */
	pm = pci_find_bar(pcidev, pbm->pbm_reg);
	if (pm == NULL)
		return (EINVAL);
	if (!pci_bar_enabled(pcidev, pm))
		return (EBUSY); /* XXXKIB enable if _ACTIVATE */
	if (!PCI_BAR_MEM(pm->pm_value))
		return (EIO);
	pbase = trunc_page(pm->pm_value);
	plen = round_page(pm->pm_value + ((pci_addr_t)1 << pm->pm_size)) -
	    pbase;
	prot = VM_PROT_READ | (((pbm->pbm_flags & PCIIO_BAR_MMAP_RW) != 0) ?
	    VM_PROT_WRITE : 0);

	/* Create vm structures and mmap. */
	sg = sglist_alloc(1, M_WAITOK);
	error = sglist_append_phys(sg, pbase, plen);
	if (error != 0)
		goto out;
	obj = vm_pager_allocate(OBJT_SG, sg, plen, prot, 0, td->td_ucred);
	if (obj == NULL) {
		error = EIO;
		goto out;
	}
	obj->memattr = pbm->pbm_memattr;
	flags = MAP_SHARED;
	addr = 0;
	if ((pbm->pbm_flags & PCIIO_BAR_MMAP_FIXED) != 0) {
		addr = (uintptr_t)pbm->pbm_map_base;
		flags |= MAP_FIXED;
	}
	if ((pbm->pbm_flags & PCIIO_BAR_MMAP_EXCL) != 0)
		flags |= MAP_CHECK_EXCL;
	error = vm_mmap_object(map, &addr, plen, prot, prot, flags, obj, 0,
	    FALSE, td);
	if (error != 0) {
		vm_object_deallocate(obj);
		goto out;
	}
	pbm->pbm_map_base = (void *)addr;
	pbm->pbm_map_length = plen;
	pbm->pbm_bar_off = pm->pm_value - pbase;
	pbm->pbm_bar_length = (pci_addr_t)1 << pm->pm_size;

out:
	sglist_free(sg);
	return (error);
}

static int
pci_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int flag, struct thread *td)
{
	device_t pcidev;
	const char *name;
	struct devlist *devlist_head;
	struct pci_conf_io *cio = NULL;
	struct pci_devinfo *dinfo;
	struct pci_io *io;
	struct pci_bar_io *bio;
	struct pci_list_vpd_io *lvio;
	struct pci_match_conf *pattern_buf;
	struct pci_map *pm;
	struct pci_bar_mmap *pbm;
	size_t confsz, iolen;
	int error, ionum, i, num_patterns;
	union pci_conf_union pcu;
#ifdef PRE7_COMPAT
	struct pci_io iodata;
	struct pci_io_old *io_old;

	io_old = NULL;
#endif

	if (!(flag & FWRITE)) {
		switch (cmd) {
		case PCIOCGETCONF:
#ifdef COMPAT_FREEBSD32
		case PCIOCGETCONF32:
#endif
#ifdef PRE7_COMPAT
		case PCIOCGETCONF_OLD:
#ifdef COMPAT_FREEBSD32
		case PCIOCGETCONF_OLD32:
#endif
#endif
		case PCIOCGETBAR:
		case PCIOCLISTVPD:
			break;
		default:
			return (EPERM);
		}
	}


	switch (cmd) {
	case PCIOCGETCONF:
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF32:
#endif
#ifdef PRE7_COMPAT
	case PCIOCGETCONF_OLD:
#ifdef COMPAT_FREEBSD32
	case PCIOCGETCONF_OLD32:
#endif
#endif
		cio = malloc(sizeof(struct pci_conf_io), M_TEMP,
		    M_WAITOK | M_ZERO);
		pci_conf_io_init(cio, data, cmd);
		pattern_buf = NULL;
		num_patterns = 0;
		dinfo = NULL;

		cio->num_matches = 0;

		/*
		 * If the user specified an offset into the device list,
		 * but the list has changed since they last called this
		 * ioctl, tell them that the list has changed.  They will
		 * have to get the list from the beginning.
		 */
		if ((cio->offset != 0)
		 && (cio->generation != pci_generation)){
			cio->status = PCI_GETCONF_LIST_CHANGED;
			error = 0;
			goto getconfexit;
		}

		/*
		 * Check to see whether the user has asked for an offset
		 * past the end of our list.
		 */
		if (cio->offset >= pci_numdevs) {
			cio->status = PCI_GETCONF_LAST_DEVICE;
			error = 0;
			goto getconfexit;
		}

		/* get the head of the device queue */
		devlist_head = &pci_devq;

		/*
		 * Determine how much room we have for pci_conf structures.
		 * Round the user's buffer size down to the nearest
		 * multiple of sizeof(struct pci_conf) in case the user
		 * didn't specify a multiple of that size.
		 */
		confsz = pci_conf_size(cmd);
		iolen = min(cio->match_buf_len - (cio->match_buf_len % confsz),
		    pci_numdevs * confsz);

		/*
		 * Since we know that iolen is a multiple of the size of
		 * the pciconf union, it's okay to do this.
		 */
		ionum = iolen / confsz;

		/*
		 * If this test is true, the user wants the pci_conf
		 * structures returned to match the supplied entries.
		 */
		if ((cio->num_patterns > 0) && (cio->num_patterns < pci_numdevs)
		 && (cio->pat_buf_len > 0)) {
			/*
			 * pat_buf_len needs to be:
			 * num_patterns * sizeof(struct pci_match_conf)
			 * While it is certainly possible the user just
			 * allocated a large buffer, but set the number of
			 * matches correctly, it is far more likely that
			 * their kernel doesn't match the userland utility
			 * they're using.  It's also possible that the user
			 * forgot to initialize some variables.  Yes, this
			 * may be overly picky, but I hazard to guess that
			 * it's far more likely to just catch folks that
			 * updated their kernel but not their userland.
			 */
			if (cio->num_patterns * pci_match_conf_size(cmd) !=
			    cio->pat_buf_len) {
				/* The user made a mistake, return an error. */
				cio->status = PCI_GETCONF_ERROR;
				error = EINVAL;
				goto getconfexit;
			}

			/*
			 * Allocate a buffer to hold the patterns.
			 */
			pattern_buf = malloc(cio->pat_buf_len, M_TEMP,
			    M_WAITOK);
			error = copyin(cio->patterns, pattern_buf,
			    cio->pat_buf_len);
			if (error != 0) {
				error = EINVAL;
				goto getconfexit;
			}
			num_patterns = cio->num_patterns;
		} else if ((cio->num_patterns > 0)
			|| (cio->pat_buf_len > 0)) {
			/*
			 * The user made a mistake, spit out an error.
			 */
			cio->status = PCI_GETCONF_ERROR;
			error = EINVAL;
                       goto getconfexit;
		}

		/*
		 * Go through the list of devices and copy out the devices
		 * that match the user's criteria.
		 */
		for (cio->num_matches = 0, i = 0,
				 dinfo = STAILQ_FIRST(devlist_head);
		     dinfo != NULL;
		     dinfo = STAILQ_NEXT(dinfo, pci_links), i++) {

			if (i < cio->offset)
				continue;

			/* Populate pd_name and pd_unit */
			name = NULL;
			if (dinfo->cfg.dev)
				name = device_get_name(dinfo->cfg.dev);
			if (name) {
				strncpy(dinfo->conf.pd_name, name,
					sizeof(dinfo->conf.pd_name));
				dinfo->conf.pd_name[PCI_MAXNAMELEN] = 0;
				dinfo->conf.pd_unit =
					device_get_unit(dinfo->cfg.dev);
			} else {
				dinfo->conf.pd_name[0] = '\0';
				dinfo->conf.pd_unit = 0;
			}

			if (pattern_buf == NULL ||
			    pci_conf_match(cmd, pattern_buf, num_patterns,
			    &dinfo->conf) == 0) {
				/*
				 * If we've filled up the user's buffer,
				 * break out at this point.  Since we've
				 * got a match here, we'll pick right back
				 * up at the matching entry.  We can also
				 * tell the user that there are more matches
				 * left.
				 */
				if (cio->num_matches >= ionum) {
					error = 0;
					break;
				}

				pci_conf_for_copyout(&dinfo->conf, &pcu, cmd);
				error = copyout(&pcu,
				    (caddr_t)cio->matches +
				    confsz * cio->num_matches, confsz);
				if (error)
					break;
				cio->num_matches++;
			}
		}

		/*
		 * Set the pointer into the list, so if the user is getting
		 * n records at a time, where n < pci_numdevs,
		 */
		cio->offset = i;

		/*
		 * Set the generation, the user will need this if they make
		 * another ioctl call with offset != 0.
		 */
		cio->generation = pci_generation;

		/*
		 * If this is the last device, inform the user so he won't
		 * bother asking for more devices.  If dinfo isn't NULL, we
		 * know that there are more matches in the list because of
		 * the way the traversal is done.
		 */
		if (dinfo == NULL)
			cio->status = PCI_GETCONF_LAST_DEVICE;
		else
			cio->status = PCI_GETCONF_MORE_DEVS;

getconfexit:
		pci_conf_io_update_data(cio, data, cmd);
		free(cio, M_TEMP);
		free(pattern_buf, M_TEMP);

		break;

#ifdef PRE7_COMPAT
	case PCIOCREAD_OLD:
	case PCIOCWRITE_OLD:
		io_old = (struct pci_io_old *)data;
		iodata.pi_sel.pc_domain = 0;
		iodata.pi_sel.pc_bus = io_old->pi_sel.pc_bus;
		iodata.pi_sel.pc_dev = io_old->pi_sel.pc_dev;
		iodata.pi_sel.pc_func = io_old->pi_sel.pc_func;
		iodata.pi_reg = io_old->pi_reg;
		iodata.pi_width = io_old->pi_width;
		iodata.pi_data = io_old->pi_data;
		data = (caddr_t)&iodata;
		/* FALLTHROUGH */
#endif
	case PCIOCREAD:
	case PCIOCWRITE:
		io = (struct pci_io *)data;
		switch(io->pi_width) {
		case 4:
		case 2:
		case 1:
			/* Make sure register is not negative and aligned. */
			if (io->pi_reg < 0 ||
			    io->pi_reg & (io->pi_width - 1)) {
				error = EINVAL;
				break;
			}
			/*
			 * Assume that the user-level bus number is
			 * in fact the physical PCI bus number.
			 * Look up the grandparent, i.e. the bridge device,
			 * so that we can issue configuration space cycles.
			 */
			pcidev = pci_find_dbsf(io->pi_sel.pc_domain,
			    io->pi_sel.pc_bus, io->pi_sel.pc_dev,
			    io->pi_sel.pc_func);
			if (pcidev) {
#ifdef PRE7_COMPAT
				if (cmd == PCIOCWRITE || cmd == PCIOCWRITE_OLD)
#else
				if (cmd == PCIOCWRITE)
#endif
					pci_write_config(pcidev,
							  io->pi_reg,
							  io->pi_data,
							  io->pi_width);
#ifdef PRE7_COMPAT
				else if (cmd == PCIOCREAD_OLD)
					io_old->pi_data =
						pci_read_config(pcidev,
							  io->pi_reg,
							  io->pi_width);
#endif
				else
					io->pi_data =
						pci_read_config(pcidev,
							  io->pi_reg,
							  io->pi_width);
				error = 0;
			} else {
#ifdef COMPAT_FREEBSD4
				if (cmd == PCIOCREAD_OLD) {
					io_old->pi_data = -1;
					error = 0;
				} else
#endif
					error = ENODEV;
			}
			break;
		default:
			error = EINVAL;
			break;
		}
		break;

	case PCIOCGETBAR:
		bio = (struct pci_bar_io *)data;

		/*
		 * Assume that the user-level bus number is
		 * in fact the physical PCI bus number.
		 */
		pcidev = pci_find_dbsf(bio->pbi_sel.pc_domain,
		    bio->pbi_sel.pc_bus, bio->pbi_sel.pc_dev,
		    bio->pbi_sel.pc_func);
		if (pcidev == NULL) {
			error = ENODEV;
			break;
		}
		pm = pci_find_bar(pcidev, bio->pbi_reg);
		if (pm == NULL) {
			error = EINVAL;
			break;
		}
		bio->pbi_base = pm->pm_value;
		bio->pbi_length = (pci_addr_t)1 << pm->pm_size;
		bio->pbi_enabled = pci_bar_enabled(pcidev, pm);
		error = 0;
		break;
	case PCIOCATTACHED:
		error = 0;
		io = (struct pci_io *)data;
		pcidev = pci_find_dbsf(io->pi_sel.pc_domain, io->pi_sel.pc_bus,
				       io->pi_sel.pc_dev, io->pi_sel.pc_func);
		if (pcidev != NULL)
			io->pi_data = device_is_attached(pcidev);
		else
			error = ENODEV;
		break;
	case PCIOCLISTVPD:
		lvio = (struct pci_list_vpd_io *)data;

		/*
		 * Assume that the user-level bus number is
		 * in fact the physical PCI bus number.
		 */
		pcidev = pci_find_dbsf(lvio->plvi_sel.pc_domain,
		    lvio->plvi_sel.pc_bus, lvio->plvi_sel.pc_dev,
		    lvio->plvi_sel.pc_func);
		if (pcidev == NULL) {
			error = ENODEV;
			break;
		}
		error = pci_list_vpd(pcidev, lvio);
		break;

	case PCIOCBARMMAP:
		pbm = (struct pci_bar_mmap *)data;
		if ((flag & FWRITE) == 0 &&
		    (pbm->pbm_flags & PCIIO_BAR_MMAP_RW) != 0)
			return (EPERM);
		pcidev = pci_find_dbsf(pbm->pbm_sel.pc_domain,
		    pbm->pbm_sel.pc_bus, pbm->pbm_sel.pc_dev,
		    pbm->pbm_sel.pc_func);
		error = pcidev == NULL ? ENODEV : pci_bar_mmap(pcidev, pbm);
		break;

	default:
		error = ENOTTY;
		break;
	}

	return (error);
}
