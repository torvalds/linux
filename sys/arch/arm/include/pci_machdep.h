/*	$OpenBSD: pci_machdep.h,v 1.18 2022/01/06 08:46:50 kettenis Exp $ */

/*
 * Copyright (c) 2003-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

typedef struct arm32_pci_chipset *pci_chipset_tag_t;
typedef uint64_t pcitag_t;

#define PCITAG_NODE(x)		((x) >> 32)
#define PCITAG_OFFSET(x)	((x) & 0xffffffff)

typedef u_long pci_intr_handle_t;

struct pci_attach_args;

/*
 * arm32-specific PCI structure and type definitions.
 * NOT TO BE USED DIRECTLY BY MACHINE INDEPENDENT CODE.
 */
struct arm32_pci_chipset {
	void		*pc_conf_v;
	void		(*pc_attach_hook)(struct device *,
			    struct device *, struct pcibus_attach_args *);
	int		(*pc_bus_maxdevs)(void *, int);
	pcitag_t	(*pc_make_tag)(void *, int, int, int);
	void		(*pc_decompose_tag)(void *, pcitag_t, int *,
			    int *, int *);
	int		(*pc_conf_size)(void *, pcitag_t);
	pcireg_t	(*pc_conf_read)(void *, pcitag_t, int);
	void		(*pc_conf_write)(void *, pcitag_t, int, pcireg_t);
	int		(*pc_probe_device_hook)(void *, struct pci_attach_args *);

	void		*pc_intr_v;
	int		(*pc_intr_map)(struct pci_attach_args *,
			    pci_intr_handle_t *);
	int		(*pc_intr_map_msi)(struct pci_attach_args *,
			    pci_intr_handle_t *);
	int		(*pc_intr_map_msix)(struct pci_attach_args *,
			    int, pci_intr_handle_t *);
	const char	*(*pc_intr_string)(void *, pci_intr_handle_t);
	void		*(*pc_intr_establish)(void *, pci_intr_handle_t,
			    int, struct cpu_info *, int (*)(void *), void *,
			    char *);
	void		(*pc_intr_disestablish)(void *, void *);
};

/*
 * Functions provided to machine-independent PCI code.
 */
#define	pci_attach_hook(p, s, pba)					\
    (*(pba)->pba_pc->pc_attach_hook)((p), (s), (pba))
#define	pci_bus_maxdevs(c, b)						\
    (*(c)->pc_bus_maxdevs)((c)->pc_conf_v, (b))
#define	pci_make_tag(c, b, d, f)					\
    (*(c)->pc_make_tag)((c)->pc_conf_v, (b), (d), (f))
#define	pci_decompose_tag(c, t, bp, dp, fp)				\
    (*(c)->pc_decompose_tag)((c)->pc_conf_v, (t), (bp), (dp), (fp))
#define	pci_conf_size(c, t)						\
    (*(c)->pc_conf_size)((c)->pc_conf_v, (t))
#define	pci_conf_read(c, t, r)						\
    (*(c)->pc_conf_read)((c)->pc_conf_v, (t), (r))
#define	pci_conf_write(c, t, r, v)					\
    (*(c)->pc_conf_write)((c)->pc_conf_v, (t), (r), (v))
#define	pci_probe_device_hook(c, a)					\
    (*(c)->pc_probe_device_hook)((c)->pc_conf_v, (a))
#define	pci_intr_map(c, ihp)						\
    (*(c)->pa_pc->pc_intr_map)((c), (ihp))
#define	pci_intr_map_msi(c, ihp)					\
    (*(c)->pa_pc->pc_intr_map_msi)((c), (ihp))
#define	pci_intr_map_msix(c, vec, ihp)					\
    (*(c)->pa_pc->pc_intr_map_msix)((c), (vec), (ihp))
#define	pci_intr_string(c, ih)						\
    (*(c)->pc_intr_string)((c)->pc_intr_v, (ih))
#define	pci_intr_establish(c, ih, l, h, a, nm)				\
    (*(c)->pc_intr_establish)((c)->pc_intr_v, (ih), (l), NULL, (h), (a),\
	(nm))
#define	pci_intr_establish_cpu(c, ih, l, ci, h, a, nm)			\
    (*(c)->pc_intr_establish)((c)->pc_intr_v, (ih), (l), (ci), (h), (a),\
	(nm))
#define	pci_intr_disestablish(c, iv)					\
    (*(c)->pc_intr_disestablish)((c)->pc_intr_v, (iv))

#define	pci_min_powerstate(c, t)	(PCI_PMCSR_STATE_D3)
#define	pci_set_powerstate_md(c, t, s, p)

#define	pci_dev_postattach(a, b)
