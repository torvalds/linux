/*	$OpenBSD: pvbus.c,v 1.30 2025/09/16 12:18:10 hshoexer Exp $	*/

/*
 * Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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

#if !defined(__i386__) && !defined(__amd64__)
#error pvbus(4) is currently only supported on i386 and amd64
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/signalvar.h>
#include <sys/syslog.h>
#include <sys/proc.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>

#include <machine/specialreg.h>
#include <machine/cpu.h>
#include <machine/conf.h>
#include <machine/bus.h>
#include <machine/vmmvar.h>

#include <dev/pv/pvvar.h>
#include <dev/pv/pvreg.h>
#include <dev/pv/hypervreg.h>

#include "hyperv.h"

int has_hv_cpuid = 0;

extern void rdrand(void *);

int	 pvbus_match(struct device *, void *, void *);
void	 pvbus_attach(struct device *, struct device *, void *);
int	 pvbus_print(void *, const char *);
int	 pvbus_search(struct device *, void *, void *);

void	 pvbus_kvm(struct pvbus_hv *);
void	 pvbus_hyperv(struct pvbus_hv *);
void	 pvbus_hyperv_print(struct pvbus_hv *);
void	 pvbus_xen(struct pvbus_hv *);
void	 pvbus_xen_print(struct pvbus_hv *);

int	 pvbus_minor(struct pvbus_softc *, dev_t);
int	 pvbusgetstr(size_t, const char *, char **);

const struct cfattach pvbus_ca = {
	sizeof(struct pvbus_softc),
	pvbus_match,
	pvbus_attach,
};

struct cfdriver pvbus_cd = {
	NULL,
	"pvbus",
	DV_DULL,
	CD_COCOVM
};

struct pvbus_type {
	const char	*signature;
	const char	*name;
	void		(*init)(struct pvbus_hv *);
	void		(*print)(struct pvbus_hv *);
} pvbus_types[PVBUS_MAX] = {
	{ "KVMKVMKVM\0\0\0",	"KVM",	pvbus_kvm },
	{ "Microsoft Hv",	"Hyper-V", pvbus_hyperv, pvbus_hyperv_print },
	{ "VMwareVMware",	"VMware" },
	{ "XenVMMXenVMM",	"Xen",	pvbus_xen, pvbus_xen_print },
	{ "bhyve bhyve ",	"bhyve" },
	{ VMM_HV_SIGNATURE,	"OpenBSD", pvbus_kvm },
};

struct bus_dma_tag pvbus_dma_tag = {
	NULL,
	_bus_dmamap_create,
	_bus_dmamap_destroy,
	_bus_dmamap_load,
	_bus_dmamap_load_mbuf,
	_bus_dmamap_load_uio,
	_bus_dmamap_load_raw,
	_bus_dmamap_unload,
	_bus_dmamap_sync,
	_bus_dmamem_alloc,
	_bus_dmamem_alloc_range,
	_bus_dmamem_free,
	_bus_dmamem_map,
	_bus_dmamem_unmap,
	_bus_dmamem_mmap,
};

struct pvbus_hv pvbus_hv[PVBUS_MAX];
struct pvbus_softc *pvbus_softc;

int
pvbus_probe(void)
{
	/* Must be set in identcpu */
	if (!has_hv_cpuid)
		return (0);
	return (1);
}

int
pvbus_match(struct device *parent, void *match, void *aux)
{
	const char **busname = (const char **)aux;
	return (strcmp(*busname, pvbus_cd.cd_name) == 0);
}

void
pvbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct pvbus_softc *sc = (struct pvbus_softc *)self;
	int i, cnt;

	sc->pvbus_hv = pvbus_hv;
	pvbus_softc = sc;

	printf(":");
	for (i = 0, cnt = 0; i < PVBUS_MAX; i++) {
		if (pvbus_hv[i].hv_base == 0)
			continue;
		if (cnt++)
			printf(",");
		printf(" %s", pvbus_types[i].name);
		if (pvbus_types[i].print != NULL)
			(pvbus_types[i].print)(&pvbus_hv[i]);
	}

	printf("\n");
	config_search(pvbus_search, self, sc);
}

void
pvbus_identify(void)
{
	struct pvbus_hv *hv;
	uint32_t reg0, base;
	union {
		uint32_t	regs[3];
		char		str[CPUID_HV_SIGNATURE_STRLEN];
	} r;
	int i, cnt;
	const char *pv_name;

	for (base = CPUID_HV_SIGNATURE_START, cnt = 0;
	    base < CPUID_HV_SIGNATURE_END;
	    base += CPUID_HV_SIGNATURE_STEP) {
		CPUID(base, reg0, r.regs[0], r.regs[1], r.regs[2]);
		for (i = 0; i < 4; i++) {
			/*
			 * Check if first 4 chars are printable ASCII as
			 * minimal validity check
			 */
			if (r.str[i] < 32 || r.str[i] > 126)
				goto out;
		}

		for (i = 0; i < PVBUS_MAX; i++) {
			if (pvbus_types[i].signature == NULL ||
			    memcmp(pvbus_types[i].signature, r.str,
			    CPUID_HV_SIGNATURE_STRLEN) != 0)
				continue;
			hv = &pvbus_hv[i];
			hv->hv_base = base;
			if (pvbus_types[i].init != NULL)
				(pvbus_types[i].init)(hv);
			if (hw_vendor == NULL) {
				pv_name = pvbus_types[i].name;

				/*
				 * Use the HV name as a fallback if we didn't
				 * get the vendor name from the firmware/BIOS.
				 */
				if ((hw_vendor = malloc(strlen(pv_name) + 1,
	                            M_DEVBUF, M_NOWAIT)) != NULL) {
					strlcpy(hw_vendor, pv_name,
					    strlen(pv_name) + 1);
				}
			}
			cnt++;
		}
	}

 out:
	if (cnt)
		has_hv_cpuid = 1;
}

void
pvbus_init_cpu(void)
{
	int i;

	for (i = 0; i < PVBUS_MAX; i++) {
		if (pvbus_hv[i].hv_base == 0)
			continue;
		if (pvbus_hv[i].hv_init_cpu != NULL)
			(pvbus_hv[i].hv_init_cpu)(&pvbus_hv[i]);
	}
}

int
pvbus_search(struct device *parent, void *arg, void *aux)
{
	struct pvbus_softc *sc = (struct pvbus_softc *)aux;
	struct cfdata		*cf = arg;
	struct pv_attach_args	 pva;

	pva.pva_busname = cf->cf_driver->cd_name;
	pva.pva_hv = sc->pvbus_hv;
	pva.pva_dmat = &pvbus_dma_tag;

	if (cf->cf_attach->ca_match(parent, cf, &pva) > 0)
		config_attach(parent, cf, &pva, pvbus_print);

	return (0);
}

int
pvbus_print(void *aux, const char *pnp)
{
	struct pv_attach_args	*pva = aux;
	if (pnp)
		printf("%s at %s", pva->pva_busname, pnp);
	return (UNCONF);
}

void
pvbus_shutdown(struct device *dev)
{
	suspend_randomness();

	log(LOG_KERN | LOG_NOTICE, "Shutting down in response to request"
	    " from %s host\n", dev->dv_xname);
	prsignal(initprocess, SIGUSR2);
}

void
pvbus_reboot(struct device *dev)
{
	suspend_randomness();

	log(LOG_KERN | LOG_NOTICE, "Rebooting in response to request"
	    " from %s host\n", dev->dv_xname);
	prsignal(initprocess, SIGINT);
}

void
pvbus_kvm(struct pvbus_hv *hv)
{
	uint32_t regs[4];

	CPUID(hv->hv_base + CPUID_OFFSET_KVM_FEATURES,
	    regs[0], regs[1], regs[2], regs[3]);
	hv->hv_features = regs[0];
}

extern void hv_delay(int usecs);

void
pvbus_hyperv(struct pvbus_hv *hv)
{
	uint32_t regs[4];

	CPUID(hv->hv_base + CPUID_OFFSET_HYPERV_FEATURES,
	    regs[0], regs[1], regs[2], regs[3]);
	hv->hv_features = regs[0];

	CPUID(hv->hv_base + CPUID_OFFSET_HYPERV_VERSION,
	    regs[0], regs[1], regs[2], regs[3]);
	hv->hv_major = (regs[1] & HYPERV_VERSION_EBX_MAJOR_M) >>
	    HYPERV_VERSION_EBX_MAJOR_S;
	hv->hv_minor = (regs[1] & HYPERV_VERSION_EBX_MINOR_M) >>
	    HYPERV_VERSION_EBX_MINOR_S;

#if NHYPERV > 0
	if (hv->hv_features & CPUID_HV_MSR_TIME_REFCNT)
		delay_init(hv_delay, 4000);
#endif
}

void
pvbus_hyperv_print(struct pvbus_hv *hv)
{
	printf(" %u.%u", hv->hv_major, hv->hv_minor);
}

void
pvbus_xen(struct pvbus_hv *hv)
{
	uint32_t regs[4];

	CPUID(hv->hv_base + CPUID_OFFSET_XEN_VERSION,
	    regs[0], regs[1], regs[2], regs[3]);
	hv->hv_major = regs[0] >> XEN_VERSION_MAJOR_S;
	hv->hv_minor = regs[0] & XEN_VERSION_MINOR_M;

	/* x2apic is broken in Xen 4.2 or older */
	if ((hv->hv_major < 4) ||
	    (hv->hv_major == 4 && hv->hv_minor < 3)) {
		/* Remove CPU flag for x2apic */
		cpu_ecxfeature &= ~CPUIDECX_X2APIC;
	}
}

void
pvbus_xen_print(struct pvbus_hv *hv)
{
	printf(" %u.%u", hv->hv_major, hv->hv_minor);
}

int
pvbus_minor(struct pvbus_softc *sc, dev_t dev)
{
	int hvid, cnt;
	struct pvbus_hv *hv;

	for (hvid = 0, cnt = 0; hvid < PVBUS_MAX; hvid++) {
		hv = &sc->pvbus_hv[hvid];
		if (hv->hv_base == 0)
			continue;
		if (minor(dev) == cnt++)
			return (hvid);
	}

	return (-1);
}

int
pvbusopen(dev_t dev, int flags, int mode, struct proc *p)
{
	if (pvbus_softc == NULL)
		return (ENODEV);
	if (pvbus_minor(pvbus_softc, dev) == -1)
		return (ENXIO);
	return (0);
}

int
pvbusclose(dev_t dev, int flags, int mode, struct proc *p)
{
	if (pvbus_softc == NULL)
		return (ENODEV);
	if (pvbus_minor(pvbus_softc, dev) == -1)
		return (ENXIO);
	return (0);
}

int
pvbusgetstr(size_t srclen, const char *src, char **dstp)
{
	int error = 0;
	char *dst;

	/*
	 * Reject size that is too short or obviously too long:
	 * - Known pv backends other than vmware have a hard limit smaller than
	 *   PVBUS_KVOP_MAXSIZE in their messaging.  vmware has a software
	 *   limit at 1MB, but current open-vm-tools has a limit at 64KB
	 *   (=PVBUS_KVOP_MAXSIZE).
	 */
	if (srclen < 1)
		return (EINVAL);
	else if (srclen > PVBUS_KVOP_MAXSIZE)
		return (ENAMETOOLONG);

	*dstp = dst = malloc(srclen + 1, M_TEMP, M_WAITOK | M_ZERO);
	if (src != NULL) {
		error = copyin(src, dst, srclen);
		dst[srclen] = '\0';
	}

	return (error);
}

int
pvbusioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct pvbus_req *pvr = (struct pvbus_req *)data;
	struct pvbus_softc *sc = pvbus_softc;
	char *value = NULL, *key = NULL;
	const char *str = NULL;
	size_t valuelen = 0, keylen = 0, sz;
	int hvid, error = 0, op;
	struct pvbus_hv *hv;

	if (sc == NULL)
		return (ENODEV);
	if ((hvid = pvbus_minor(sc, dev)) == -1)
		return (ENXIO);

	switch (cmd) {
	case PVBUSIOC_KVWRITE:
		if ((flags & FWRITE) == 0)
			return (EPERM);
	case PVBUSIOC_KVREAD:
		hv = &sc->pvbus_hv[hvid];
		if (hv->hv_base == 0 || hv->hv_kvop == NULL)
			return (ENXIO);
		break;
	case PVBUSIOC_TYPE:
		str = pvbus_types[hvid].name;
		sz = strlen(str) + 1;
		if (sz > pvr->pvr_keylen)
			return (ENOMEM);
		error = copyout(str, pvr->pvr_key, sz);
		return (error);
	default:
		return (ENOTTY);
	}

	str = NULL;
	op = PVBUS_KVREAD;

	switch (cmd) {
	case PVBUSIOC_KVWRITE:
		str = pvr->pvr_value;
		op = PVBUS_KVWRITE;

		/* FALLTHROUGH */
	case PVBUSIOC_KVREAD:
		keylen = pvr->pvr_keylen;
		if ((error = pvbusgetstr(keylen, pvr->pvr_key, &key)) != 0)
			break;

		valuelen = pvr->pvr_valuelen;
		if ((error = pvbusgetstr(valuelen, str, &value)) != 0)
			break;

		/* Call driver-specific callback */
		if ((error = (hv->hv_kvop)(hv->hv_arg, op,
		    key, value, valuelen)) != 0)
			break;

		sz = strlen(value) + 1;
		if ((error = copyout(value, pvr->pvr_value, sz)) != 0)
			break;
		break;
	default:
		error = ENOTTY;
		break;
	}

	free(key, M_TEMP, keylen + 1);
	free(value, M_TEMP, valuelen + 1);

	return (error);
}
