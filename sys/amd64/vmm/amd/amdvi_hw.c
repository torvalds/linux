/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016, Anish Gupta (anish@freebsd.org)
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/resource.h>
#include <machine/vmm.h>
#include <machine/pmap.h>
#include <machine/vmparam.h>
#include <machine/pci_cfgreg.h>

#include "pcib_if.h"

#include "io/iommu.h"
#include "amdvi_priv.h"

SYSCTL_DECL(_hw_vmm);
SYSCTL_NODE(_hw_vmm, OID_AUTO, amdvi, CTLFLAG_RW, NULL, NULL);

#define MOD_INC(a, s, m) (((a) + (s)) % ((m) * (s)))
#define MOD_DEC(a, s, m) (((a) - (s)) % ((m) * (s)))

/* Print RID or device ID in PCI string format. */
#define RID2PCI_STR(d) PCI_RID2BUS(d), PCI_RID2SLOT(d), PCI_RID2FUNC(d)

static void amdvi_dump_cmds(struct amdvi_softc *softc);
static void amdvi_print_dev_cap(struct amdvi_softc *softc);

MALLOC_DEFINE(M_AMDVI, "amdvi", "amdvi");

extern device_t *ivhd_devs;

extern int ivhd_count;
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, count, CTLFLAG_RDTUN, &ivhd_count,
    0, NULL);

static int amdvi_enable_user = 0;
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, enable, CTLFLAG_RDTUN,
    &amdvi_enable_user, 0, NULL);
TUNABLE_INT("hw.vmm.amdvi_enable", &amdvi_enable_user);

#ifdef AMDVI_ATS_ENABLE
/* XXX: ATS is not tested. */
static int amdvi_enable_iotlb = 1;
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, iotlb_enabled, CTLFLAG_RDTUN,
    &amdvi_enable_iotlb, 0, NULL);
TUNABLE_INT("hw.vmm.enable_iotlb", &amdvi_enable_iotlb);
#endif

static int amdvi_host_ptp = 1;	/* Use page tables for host. */
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, host_ptp, CTLFLAG_RDTUN,
    &amdvi_host_ptp, 0, NULL);
TUNABLE_INT("hw.vmm.amdvi.host_ptp", &amdvi_host_ptp);

/* Page table level used <= supported by h/w[v1=7]. */
static int amdvi_ptp_level = 4;
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, ptp_level, CTLFLAG_RDTUN,
    &amdvi_ptp_level, 0, NULL);
TUNABLE_INT("hw.vmm.amdvi.ptp_level", &amdvi_ptp_level);

/* Disable fault event reporting. */
static int amdvi_disable_io_fault = 0;
SYSCTL_INT(_hw_vmm_amdvi, OID_AUTO, disable_io_fault, CTLFLAG_RDTUN,
    &amdvi_disable_io_fault, 0, NULL);
TUNABLE_INT("hw.vmm.amdvi.disable_io_fault", &amdvi_disable_io_fault);

static uint32_t amdvi_dom_id = 0;	/* 0 is reserved for host. */
SYSCTL_UINT(_hw_vmm_amdvi, OID_AUTO, domain_id, CTLFLAG_RD,
    &amdvi_dom_id, 0, NULL);
/*
 * Device table entry.
 * Bus(256) x Dev(32) x Fun(8) x DTE(256 bits or 32 bytes).
 *	= 256 * 2 * PAGE_SIZE.
 */
static struct amdvi_dte amdvi_dte[PCI_NUM_DEV_MAX] __aligned(PAGE_SIZE);
CTASSERT(PCI_NUM_DEV_MAX == 0x10000);
CTASSERT(sizeof(amdvi_dte) == 0x200000);

static SLIST_HEAD (, amdvi_domain) dom_head;

static inline uint32_t
amdvi_pci_read(struct amdvi_softc *softc, int off)
{

	return (pci_cfgregread(PCI_RID2BUS(softc->pci_rid),
	    PCI_RID2SLOT(softc->pci_rid), PCI_RID2FUNC(softc->pci_rid),
	    off, 4));
}

#ifdef AMDVI_ATS_ENABLE
/* XXX: Should be in pci.c */
/*
 * Check if device has ATS capability and its enabled.
 * If ATS is absent or disabled, return (-1), otherwise ATS
 * queue length.
 */
static int
amdvi_find_ats_qlen(uint16_t devid)
{
	device_t dev;
	uint32_t off, cap;
	int qlen = -1;

	dev = pci_find_bsf(PCI_RID2BUS(devid), PCI_RID2SLOT(devid),
			   PCI_RID2FUNC(devid));

	if (!dev) {
		return (-1);
	}
#define PCIM_ATS_EN	BIT(31)

	if (pci_find_extcap(dev, PCIZ_ATS, &off) == 0) {
		cap = pci_read_config(dev, off + 4, 4);
		qlen = (cap & 0x1F);
		qlen = qlen ? qlen : 32;
		printf("AMD-Vi: PCI device %d.%d.%d ATS %s qlen=%d\n",
		       RID2PCI_STR(devid),
		       (cap & PCIM_ATS_EN) ? "enabled" : "Disabled",
		       qlen);
		qlen = (cap & PCIM_ATS_EN) ? qlen : -1;
	}

	return (qlen);
}

/*
 * Check if an endpoint device support device IOTLB or ATS.
 */
static inline bool
amdvi_dev_support_iotlb(struct amdvi_softc *softc, uint16_t devid)
{
	struct ivhd_dev_cfg *cfg;
	int qlen, i;
	bool pci_ats, ivhd_ats;

	qlen = amdvi_find_ats_qlen(devid);
	if (qlen < 0)
		return (false);

	KASSERT(softc, ("softc is NULL"));
	cfg = softc->dev_cfg;

	ivhd_ats = false;
	for (i = 0; i < softc->dev_cfg_cnt; i++) {
		if ((cfg->start_id <= devid) && (cfg->end_id >= devid)) {
			ivhd_ats = cfg->enable_ats;
			break;
		}
		cfg++;
	}

	pci_ats = (qlen < 0) ? false : true;
	if (pci_ats != ivhd_ats)
		device_printf(softc->dev,
		    "BIOS bug: mismatch in ATS setting for %d.%d.%d,"
		    "ATS inv qlen = %d\n", RID2PCI_STR(devid), qlen);

	/* Ignore IVRS setting and respect PCI setting. */
	return (pci_ats);
}
#endif

/* Enable IOTLB support for IOMMU if its supported. */
static inline void
amdvi_hw_enable_iotlb(struct amdvi_softc *softc)
{
#ifndef AMDVI_ATS_ENABLE
	softc->iotlb = false;
#else
	bool supported;

	supported = (softc->ivhd_flag & IVHD_FLAG_IOTLB) ? true : false;

	if (softc->pci_cap & AMDVI_PCI_CAP_IOTLB) {
		if (!supported)
			device_printf(softc->dev, "IOTLB disabled by BIOS.\n");

		if (supported && !amdvi_enable_iotlb) {
			device_printf(softc->dev, "IOTLB disabled by user.\n");
			supported = false;
		}
	} else
		supported = false;

	softc->iotlb = supported;

#endif
}

static int
amdvi_init_cmd(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl = softc->ctrl;

	ctrl->cmd.len = 8;	/* Use 256 command buffer entries. */
	softc->cmd_max = 1 << ctrl->cmd.len;

	softc->cmd = malloc(sizeof(struct amdvi_cmd) *
	    softc->cmd_max, M_AMDVI, M_WAITOK | M_ZERO);

	if ((uintptr_t)softc->cmd & PAGE_MASK)
		panic("AMDVi: Command buffer not aligned on page boundary.");

	ctrl->cmd.base = vtophys(softc->cmd) / PAGE_SIZE;
	/*
	 * XXX: Reset the h/w pointers in case IOMMU is restarting,
	 * h/w doesn't clear these pointers based on empirical data.
	 */
	ctrl->cmd_tail = 0;
	ctrl->cmd_head = 0;

	return (0);
}

/*
 * Note: Update tail pointer after we have written the command since tail
 * pointer update cause h/w to execute new commands, see section 3.3
 * of AMD IOMMU spec ver 2.0.
 */
/* Get the command tail pointer w/o updating it. */
static struct amdvi_cmd *
amdvi_get_cmd_tail(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	struct amdvi_cmd *tail;

	KASSERT(softc, ("softc is NULL"));
	KASSERT(softc->cmd != NULL, ("cmd is NULL"));

	ctrl = softc->ctrl;
	KASSERT(ctrl != NULL, ("ctrl is NULL"));

	tail = (struct amdvi_cmd *)((uint8_t *)softc->cmd +
	    ctrl->cmd_tail);

	return (tail);
}

/*
 * Update the command tail pointer which will start command execution.
 */
static void
amdvi_update_cmd_tail(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	int size;

	size = sizeof(struct amdvi_cmd);
	KASSERT(softc->cmd != NULL, ("cmd is NULL"));

	ctrl = softc->ctrl;
	KASSERT(ctrl != NULL, ("ctrl is NULL"));

	ctrl->cmd_tail = MOD_INC(ctrl->cmd_tail, size, softc->cmd_max);
	softc->total_cmd++;

#ifdef AMDVI_DEBUG_CMD
	device_printf(softc->dev, "cmd_tail: %s Tail:0x%x, Head:0x%x.\n",
	    ctrl->cmd_tail,
	    ctrl->cmd_head);
#endif

}

/*
 * Various commands supported by IOMMU.
 */

/* Completion wait command. */
static void
amdvi_cmd_cmp(struct amdvi_softc *softc, const uint64_t data)
{
	struct amdvi_cmd *cmd;
	uint64_t pa;

	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));

	pa = vtophys(&softc->cmp_data);
	cmd->opcode = AMDVI_CMP_WAIT_OPCODE;
	cmd->word0 = (pa & 0xFFFFFFF8) |
	    (AMDVI_CMP_WAIT_STORE);
	//(AMDVI_CMP_WAIT_FLUSH | AMDVI_CMP_WAIT_STORE);
	cmd->word1 = (pa >> 32) & 0xFFFFF;
	cmd->addr = data;

	amdvi_update_cmd_tail(softc);
}

/* Invalidate device table entry. */
static void
amdvi_cmd_inv_dte(struct amdvi_softc *softc, uint16_t devid)
{
	struct amdvi_cmd *cmd;

	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));
	cmd->opcode = AMDVI_INVD_DTE_OPCODE;
	cmd->word0 = devid;
	amdvi_update_cmd_tail(softc);
#ifdef AMDVI_DEBUG_CMD
	device_printf(softc->dev, "Invalidated DTE:0x%x\n", devid);
#endif
}

/* Invalidate IOMMU page, use for invalidation of domain. */
static void
amdvi_cmd_inv_iommu_pages(struct amdvi_softc *softc, uint16_t domain_id,
			  uint64_t addr, bool guest_nested,
			  bool pde, bool page)
{
	struct amdvi_cmd *cmd;

	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));


	cmd->opcode = AMDVI_INVD_PAGE_OPCODE;
	cmd->word1 = domain_id;
	/*
	 * Invalidate all addresses for this domain.
	 */
	cmd->addr = addr;
	cmd->addr |= pde ? AMDVI_INVD_PAGE_PDE : 0;
	cmd->addr |= page ? AMDVI_INVD_PAGE_S : 0;

	amdvi_update_cmd_tail(softc);
}

#ifdef AMDVI_ATS_ENABLE
/* Invalidate device IOTLB. */
static void
amdvi_cmd_inv_iotlb(struct amdvi_softc *softc, uint16_t devid)
{
	struct amdvi_cmd *cmd;
	int qlen;

	if (!softc->iotlb)
		return;

	qlen = amdvi_find_ats_qlen(devid);
	if (qlen < 0) {
		panic("AMDVI: Invalid ATS qlen(%d) for device %d.%d.%d\n",
		      qlen, RID2PCI_STR(devid));
	}
	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));

#ifdef AMDVI_DEBUG_CMD
	device_printf(softc->dev, "Invalidate IOTLB devID 0x%x"
		      " Qlen:%d\n", devid, qlen);
#endif
	cmd->opcode = AMDVI_INVD_IOTLB_OPCODE;
	cmd->word0 = devid;
	cmd->word1 = qlen;
	cmd->addr = AMDVI_INVD_IOTLB_ALL_ADDR |
		AMDVI_INVD_IOTLB_S;
	amdvi_update_cmd_tail(softc);
}
#endif

#ifdef notyet				/* For Interrupt Remap. */
static void
amdvi_cmd_inv_intr_map(struct amdvi_softc *softc,
		       uint16_t devid)
{
	struct amdvi_cmd *cmd;

	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));
	cmd->opcode = AMDVI_INVD_INTR_OPCODE;
	cmd->word0 = devid;
	amdvi_update_cmd_tail(softc);
#ifdef AMDVI_DEBUG_CMD
	device_printf(softc->dev, "Invalidate INTR map of devID 0x%x\n", devid);
#endif
}
#endif

/* Invalidate domain using INVALIDATE_IOMMU_PAGES command. */
static void
amdvi_inv_domain(struct amdvi_softc *softc, uint16_t domain_id)
{
	struct amdvi_cmd *cmd;

	cmd = amdvi_get_cmd_tail(softc);
	KASSERT(cmd != NULL, ("Cmd is NULL"));

	/*
	 * See section 3.3.3 of IOMMU spec rev 2.0, software note
	 * for invalidating domain.
	 */
	amdvi_cmd_inv_iommu_pages(softc, domain_id, AMDVI_INVD_PAGE_ALL_ADDR,
				false, true, true);

#ifdef AMDVI_DEBUG_CMD
	device_printf(softc->dev, "Invalidate domain:0x%x\n", domain_id);

#endif
}

static	bool
amdvi_cmp_wait(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	const uint64_t VERIFY = 0xA5A5;
	volatile uint64_t *read;
	int i;
	bool status;

	ctrl = softc->ctrl;
	read = &softc->cmp_data;
	*read = 0;
	amdvi_cmd_cmp(softc, VERIFY);
	/* Wait for h/w to update completion data. */
	for (i = 0; i < 100 && (*read != VERIFY); i++) {
		DELAY(1000);		/* 1 ms */
	}
	status = (VERIFY == softc->cmp_data) ? true : false;

#ifdef AMDVI_DEBUG_CMD
	if (status)
		device_printf(softc->dev, "CMD completion DONE Tail:0x%x, "
			      "Head:0x%x, loop:%d.\n", ctrl->cmd_tail,
			      ctrl->cmd_head, loop);
#endif
	return (status);
}

static void
amdvi_wait(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	int i;

	KASSERT(softc, ("softc is NULL"));

	ctrl = softc->ctrl;
	KASSERT(ctrl != NULL, ("ctrl is NULL"));
	/* Don't wait if h/w is not enabled. */
	if ((ctrl->control & AMDVI_CTRL_EN) == 0)
		return;

	for (i = 0; i < 10; i++) {
		if (amdvi_cmp_wait(softc))
			return;
	}

	device_printf(softc->dev, "Error: completion failed"
		      " tail:0x%x, head:0x%x.\n",
		      ctrl->cmd_tail, ctrl->cmd_head);
	amdvi_dump_cmds(softc);
}

static void
amdvi_dump_cmds(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	struct amdvi_cmd *cmd;
	int off, i;

	ctrl = softc->ctrl;
	device_printf(softc->dev, "Dump all the commands:\n");
	/*
	 * If h/w is stuck in completion, it is the previous command,
	 * start dumping from previous command onward.
	 */
	off = MOD_DEC(ctrl->cmd_head, sizeof(struct amdvi_cmd),
	    softc->cmd_max);
	for (i = 0; off != ctrl->cmd_tail &&
	    i < softc->cmd_max; i++) {
		cmd = (struct amdvi_cmd *)((uint8_t *)softc->cmd + off);
		printf("  [CMD%d, off:0x%x] opcode= 0x%x 0x%x"
		    " 0x%x 0x%lx\n", i, off, cmd->opcode,
		    cmd->word0, cmd->word1, cmd->addr);
		off = (off + sizeof(struct amdvi_cmd)) %
		    (softc->cmd_max * sizeof(struct amdvi_cmd));
	}
}

static int
amdvi_init_event(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;

	ctrl = softc->ctrl;
	ctrl->event.len = 8;
	softc->event_max = 1 << ctrl->event.len;
	softc->event = malloc(sizeof(struct amdvi_event) *
	    softc->event_max, M_AMDVI, M_WAITOK | M_ZERO);
	if ((uintptr_t)softc->event & PAGE_MASK) {
		device_printf(softc->dev, "Event buffer not aligned on page.");
		return (false);
	}
	ctrl->event.base = vtophys(softc->event) / PAGE_SIZE;

	/* Reset the pointers. */
	ctrl->evt_head = 0;
	ctrl->evt_tail = 0;

	return (0);
}

static inline void
amdvi_decode_evt_flag(uint16_t flag)
{

	flag &= AMDVI_EVENT_FLAG_MASK;
	printf(" 0x%b]\n", flag,
		"\020"
		"\001GN"
		"\002NX"
		"\003US"
		"\004I"
		"\005PR"
		"\006RW"
		"\007PE"
		"\010RZ"
		"\011TR"
		);
}

/* See section 2.5.4 of AMD IOMMU spec ver 2.62.*/
static inline void
amdvi_decode_evt_flag_type(uint8_t type)
{

	switch (AMDVI_EVENT_FLAG_TYPE(type)) {
	case 0:
		printf("RSVD\n");
		break;
	case 1:
		printf("Master Abort\n");
		break;
	case 2:
		printf("Target Abort\n");
		break;
	case 3:
		printf("Data Err\n");
		break;
	default:
		break;
	}
}

static void
amdvi_decode_inv_dte_evt(uint16_t devid, uint16_t domid, uint64_t addr,
    uint16_t flag)
{

	printf("\t[IO_PAGE_FAULT EVT: devId:0x%x DomId:0x%x"
	    " Addr:0x%lx",
	    devid, domid, addr);
	amdvi_decode_evt_flag(flag);
}

static void
amdvi_decode_pf_evt(uint16_t devid, uint16_t domid, uint64_t addr,
    uint16_t flag)
{

	printf("\t[IO_PAGE_FAULT EVT: devId:0x%x DomId:0x%x"
	    " Addr:0x%lx",
	    devid, domid, addr);
	amdvi_decode_evt_flag(flag);
}

static void
amdvi_decode_dte_hwerr_evt(uint16_t devid, uint16_t domid,
    uint64_t addr, uint16_t flag)
{

	printf("\t[DEV_TAB_HW_ERR EVT: devId:0x%x DomId:0x%x"
	    " Addr:0x%lx", devid, domid, addr);
	amdvi_decode_evt_flag(flag);
	amdvi_decode_evt_flag_type(flag);
}

static void
amdvi_decode_page_hwerr_evt(uint16_t devid, uint16_t domid, uint64_t addr,
    uint16_t flag)
{

	printf("\t[PAGE_TAB_HW_ERR EVT: devId:0x%x DomId:0x%x"
	    " Addr:0x%lx", devid, domid, addr);
	amdvi_decode_evt_flag(flag);
	amdvi_decode_evt_flag_type(AMDVI_EVENT_FLAG_TYPE(flag));
}

static void
amdvi_decode_evt(struct amdvi_event *evt)
{
	struct amdvi_cmd *cmd;

	switch (evt->opcode) {
	case AMDVI_EVENT_INVALID_DTE:
		amdvi_decode_inv_dte_evt(evt->devid, evt->pasid_domid,
		    evt->addr, evt->flag);
		break;

	case AMDVI_EVENT_PFAULT:
		amdvi_decode_pf_evt(evt->devid, evt->pasid_domid,
		    evt->addr, evt->flag);
		break;

	case AMDVI_EVENT_DTE_HW_ERROR:
		amdvi_decode_dte_hwerr_evt(evt->devid, evt->pasid_domid,
		    evt->addr, evt->flag);
		break;

	case AMDVI_EVENT_PAGE_HW_ERROR:
		amdvi_decode_page_hwerr_evt(evt->devid, evt->pasid_domid,
		    evt->addr, evt->flag);
		break;

	case AMDVI_EVENT_ILLEGAL_CMD:
		/* FALL THROUGH */
	case AMDVI_EVENT_CMD_HW_ERROR:
		printf("\t[%s EVT]\n", (evt->opcode == AMDVI_EVENT_ILLEGAL_CMD) ?
		    "ILLEGAL CMD" : "CMD HW ERR");
		cmd = (struct amdvi_cmd *)PHYS_TO_DMAP(evt->addr);
		printf("\tCMD opcode= 0x%x 0x%x 0x%x 0x%lx\n",
		    cmd->opcode, cmd->word0, cmd->word1, cmd->addr);
		break;

	case AMDVI_EVENT_IOTLB_TIMEOUT:
		printf("\t[IOTLB_INV_TIMEOUT devid:0x%x addr:0x%lx]\n",
		    evt->devid, evt->addr);
		break;

	case AMDVI_EVENT_INVALID_DTE_REQ:
		printf("\t[INV_DTE devid:0x%x addr:0x%lx type:0x%x tr:%d]\n",
		    evt->devid, evt->addr, evt->flag >> 9,
		    (evt->flag >> 8) & 1);
		break;

	case AMDVI_EVENT_INVALID_PPR_REQ:
	case AMDVI_EVENT_COUNTER_ZERO:
		printf("AMD-Vi: v2 events.\n");
		break;

	default:
		printf("Unsupported AMD-Vi event:%d\n", evt->opcode);
	}
}

static void
amdvi_print_events(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	struct amdvi_event *event;
	int i, size;

	ctrl = softc->ctrl;
	size = sizeof(struct amdvi_event);
	for (i = 0; i < softc->event_max; i++) {
		event = &softc->event[ctrl->evt_head / size];
		if (!event->opcode)
			break;
		device_printf(softc->dev, "\t[Event%d: Head:0x%x Tail:0x%x]\n",
		    i, ctrl->evt_head, ctrl->evt_tail);
		amdvi_decode_evt(event);
		ctrl->evt_head = MOD_INC(ctrl->evt_head, size,
		    softc->event_max);
	}
}

static int
amdvi_init_dte(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;

	ctrl = softc->ctrl;
	ctrl->dte.base = vtophys(amdvi_dte) / PAGE_SIZE;
	ctrl->dte.size = 0x1FF;		/* 2MB device table. */

	return (0);
}

/*
 * Not all capabilities of IOMMU are available in ACPI IVHD flag
 * or EFR entry, read directly from device.
 */
static int
amdvi_print_pci_cap(device_t dev)
{
	struct amdvi_softc *softc;
	uint32_t off, cap;


	softc = device_get_softc(dev);
	off = softc->cap_off;

	/*
	 * Section 3.7.1 of IOMMU sepc rev 2.0.
	 * Read capability from device.
	 */
	cap = amdvi_pci_read(softc, off);

	/* Make sure capability type[18:16] is 3. */
	KASSERT((((cap >> 16) & 0x7) == 0x3),
	    ("Not a IOMMU capability 0x%x@0x%x", cap, off));

	softc->pci_cap = cap >> 24;
	device_printf(softc->dev, "PCI cap 0x%x@0x%x feature:%b\n",
	    cap, off, softc->pci_cap,
	    "\20\1IOTLB\2HT\3NPCache\4EFR\5CapExt");

	return (0);
}

static void
amdvi_event_intr(void *arg)
{
	struct amdvi_softc *softc;
	struct amdvi_ctrl *ctrl;

	softc = (struct amdvi_softc *)arg;
	ctrl = softc->ctrl;
	device_printf(softc->dev, "EVT INTR %ld Status:0x%x"
	    " EVT Head:0x%x Tail:0x%x]\n", softc->event_intr_cnt++,
	    ctrl->status, ctrl->evt_head, ctrl->evt_tail);
	printf("  [CMD Total 0x%lx] Tail:0x%x, Head:0x%x.\n",
	    softc->total_cmd, ctrl->cmd_tail, ctrl->cmd_head);

	amdvi_print_events(softc);
	ctrl->status &= AMDVI_STATUS_EV_OF | AMDVI_STATUS_EV_INTR;
}

static void
amdvi_free_evt_intr_res(device_t dev)
{

	struct amdvi_softc *softc;

	softc = device_get_softc(dev);
	if (softc->event_tag != NULL) {
		bus_teardown_intr(dev, softc->event_res, softc->event_tag);
	}
	if (softc->event_res != NULL) {
		bus_release_resource(dev, SYS_RES_IRQ, softc->event_rid,
		    softc->event_res);
	}
	bus_delete_resource(dev, SYS_RES_IRQ, softc->event_rid);
	PCIB_RELEASE_MSI(device_get_parent(device_get_parent(dev)),
	    dev, 1, &softc->event_irq);
}

static bool
amdvi_alloc_intr_resources(struct amdvi_softc *softc)
{
	struct amdvi_ctrl *ctrl;
	device_t dev, pcib;
	device_t mmio_dev;
	uint64_t msi_addr;
	uint32_t msi_data;
	int err;

	dev = softc->dev;
	pcib = device_get_parent(device_get_parent(dev));
	mmio_dev = pci_find_bsf(PCI_RID2BUS(softc->pci_rid),
            PCI_RID2SLOT(softc->pci_rid), PCI_RID2FUNC(softc->pci_rid));
	if (device_is_attached(mmio_dev)) {
		device_printf(dev,
		    "warning: IOMMU device is claimed by another driver %s\n",
		    device_get_driver(mmio_dev)->name);
	}

	softc->event_irq = -1;
	softc->event_rid = 0;

	/*
	 * Section 3.7.1 of IOMMU rev 2.0. With MSI, there is only one
	 * interrupt. XXX: Enable MSI/X support.
	 */
	err = PCIB_ALLOC_MSI(pcib, dev, 1, 1, &softc->event_irq);
	if (err) {
		device_printf(dev,
		    "Couldn't find event MSI IRQ resource.\n");
		return (ENOENT);
	}

	err = bus_set_resource(dev, SYS_RES_IRQ, softc->event_rid,
	    softc->event_irq, 1);
	if (err) {
		device_printf(dev, "Couldn't set event MSI resource.\n");
		return (ENXIO);
	}

	softc->event_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &softc->event_rid, RF_ACTIVE);
	if (!softc->event_res) {
		device_printf(dev,
		    "Unable to allocate event INTR resource.\n");
		return (ENOMEM);
	}

	if (bus_setup_intr(dev, softc->event_res,
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, amdvi_event_intr,
	    softc, &softc->event_tag)) {
		device_printf(dev, "Fail to setup event intr\n");
		bus_release_resource(softc->dev, SYS_RES_IRQ,
		    softc->event_rid, softc->event_res);
		softc->event_res = NULL;
		return (ENXIO);
	}

	bus_describe_intr(dev, softc->event_res, softc->event_tag,
	    "fault");

	err = PCIB_MAP_MSI(pcib, dev, softc->event_irq, &msi_addr,
	    &msi_data);
	if (err) {
		device_printf(dev,
		    "Event interrupt config failed, err=%d.\n",
		    err);
		amdvi_free_evt_intr_res(softc->dev);
		return (err);
	}

	/* Clear interrupt status bits. */
	ctrl = softc->ctrl;
	ctrl->status &= AMDVI_STATUS_EV_OF | AMDVI_STATUS_EV_INTR;

	/* Now enable MSI interrupt. */
	pci_enable_msi(mmio_dev, msi_addr, msi_data);
	return (0);
}


static void
amdvi_print_dev_cap(struct amdvi_softc *softc)
{
	struct ivhd_dev_cfg *cfg;
	int i;

	cfg = softc->dev_cfg;
	for (i = 0; i < softc->dev_cfg_cnt; i++) {
		device_printf(softc->dev, "device [0x%x - 0x%x]"
		    "config:%b%s\n", cfg->start_id, cfg->end_id,
		    cfg->data,
		    "\020\001INIT\002ExtInt\003NMI"
		    "\007LINT0\008LINT1",
		    cfg->enable_ats ? "ATS enabled" : "");
		cfg++;
	}
}

static int
amdvi_handle_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct amdvi_softc *softc;
	int result, type, error = 0;

	softc = (struct amdvi_softc *)arg1;
	type = arg2;

	switch (type) {
	case 0:
		result = softc->ctrl->cmd_head;
		error = sysctl_handle_int(oidp, &result, 0,
		    req);
		break;
	case 1:
		result = softc->ctrl->cmd_tail;
		error = sysctl_handle_int(oidp, &result, 0,
		    req);
		break;
	case 2:
		result = softc->ctrl->evt_head;
		error = sysctl_handle_int(oidp, &result, 0,
		    req);
		break;
	case 3:
		result = softc->ctrl->evt_tail;
		error = sysctl_handle_int(oidp, &result, 0,
		    req);
		break;

	default:
		device_printf(softc->dev, "Unknown sysctl:%d\n", type);
	}

	return (error);
}

static void
amdvi_add_sysctl(struct amdvi_softc *softc)
{
	struct sysctl_oid_list *child;
	struct sysctl_ctx_list *ctx;
	device_t dev;

	dev = softc->dev;
	ctx = device_get_sysctl_ctx(dev);
	child = SYSCTL_CHILDREN(device_get_sysctl_tree(dev));

	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "event_intr_count", CTLFLAG_RD,
	    &softc->event_intr_cnt, "Event interrupt count");
	SYSCTL_ADD_ULONG(ctx, child, OID_AUTO, "command_count", CTLFLAG_RD,
	    &softc->total_cmd, "Command submitted count");
	SYSCTL_ADD_U16(ctx, child, OID_AUTO, "pci_rid", CTLFLAG_RD,
	    &softc->pci_rid, 0, "IOMMU RID");
	SYSCTL_ADD_U16(ctx, child, OID_AUTO, "start_dev_rid", CTLFLAG_RD,
	    &softc->start_dev_rid, 0, "Start of device under this IOMMU");
	SYSCTL_ADD_U16(ctx, child, OID_AUTO, "end_dev_rid", CTLFLAG_RD,
	    &softc->end_dev_rid, 0, "End of device under this IOMMU");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "command_head",
	    CTLTYPE_UINT | CTLFLAG_RD, softc, 0,
	    amdvi_handle_sysctl, "IU", "Command head");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "command_tail",
	    CTLTYPE_UINT | CTLFLAG_RD, softc, 1,
	    amdvi_handle_sysctl, "IU", "Command tail");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "event_head",
	    CTLTYPE_UINT | CTLFLAG_RD, softc, 2,
	    amdvi_handle_sysctl, "IU", "Command head");
	SYSCTL_ADD_PROC(ctx, child, OID_AUTO, "event_tail",
	    CTLTYPE_UINT | CTLFLAG_RD, softc, 3,
	    amdvi_handle_sysctl, "IU", "Command tail");
}

int
amdvi_setup_hw(struct amdvi_softc *softc)
{
	device_t dev;
	int status;

	dev = softc->dev;

	amdvi_hw_enable_iotlb(softc);

	amdvi_print_dev_cap(softc);

	if ((status = amdvi_print_pci_cap(dev)) != 0) {
		device_printf(dev, "PCI capability.\n");
		return (status);
	}
	if ((status = amdvi_init_cmd(softc)) != 0) {
		device_printf(dev, "Couldn't configure command buffer.\n");
		return (status);
	}
	if ((status = amdvi_init_event(softc)) != 0) {
		device_printf(dev, "Couldn't configure event buffer.\n");
		return (status);
	}
	if ((status = amdvi_init_dte(softc)) != 0) {
		device_printf(dev, "Couldn't configure device table.\n");
		return (status);
	}
	if ((status = amdvi_alloc_intr_resources(softc)) != 0) {
		return (status);
	}
	amdvi_add_sysctl(softc);
	return (0);
}

int
amdvi_teardown_hw(struct amdvi_softc *softc)
{
	device_t dev;

	dev = softc->dev;

	/* 
	 * Called after disable, h/w is stopped by now, free all the resources. 
	 */
	amdvi_free_evt_intr_res(dev);

	if (softc->cmd)
		free(softc->cmd, M_AMDVI);

	if (softc->event)
		free(softc->event, M_AMDVI);

	return (0);
}

/*********** bhyve interfaces *********************/
static int
amdvi_init(void)
{
	if (!ivhd_count) {
		return (EIO);
	}
	if (!amdvi_enable_user && ivhd_count) {
		printf("bhyve: Found %d AMD-Vi/IOMMU device(s), "
		    	"use hw.vmm.amdvi.enable=1 to enable pass-through.\n",
		    ivhd_count);
		return (EINVAL);
	}
	return (0);
}

static void
amdvi_cleanup(void)
{
	/* Nothing. */
}

static uint16_t
amdvi_domainId(void)
{

	/*
	 * If we hit maximum domain limit, rollover leaving host
	 * domain(0).
	 * XXX: make sure that this domain is not used.
	 */
	if (amdvi_dom_id == AMDVI_MAX_DOMAIN)
		amdvi_dom_id = 1;

	return ((uint16_t)amdvi_dom_id++);
}

static void
amdvi_do_inv_domain(uint16_t domain_id, bool create)
{
	struct amdvi_softc *softc;
	int i;

	for (i = 0; i < ivhd_count; i++) {
		softc = device_get_softc(ivhd_devs[i]);
		KASSERT(softc, ("softc is NULL"));
		/*
		 * If not present pages are cached, invalidate page after
		 * creating domain.
		 */
#if 0
		if (create && ((softc->pci_cap & AMDVI_PCI_CAP_NPCACHE) == 0))
			continue;
#endif
		amdvi_inv_domain(softc, domain_id);
		amdvi_wait(softc);
	}
}

static void *
amdvi_create_domain(vm_paddr_t maxaddr)
{
	struct amdvi_domain *dom;

	dom = malloc(sizeof(struct amdvi_domain), M_AMDVI, M_ZERO | M_WAITOK);
	dom->id = amdvi_domainId();
	//dom->maxaddr = maxaddr;
#ifdef AMDVI_DEBUG_CMD
	printf("Created domain #%d\n", dom->id);
#endif
	/*
	 * Host domain(#0) don't create translation table.
	 */
	if (dom->id || amdvi_host_ptp)
		dom->ptp = malloc(PAGE_SIZE, M_AMDVI, M_WAITOK | M_ZERO);

	dom->ptp_level = amdvi_ptp_level;

	amdvi_do_inv_domain(dom->id, true);
	SLIST_INSERT_HEAD(&dom_head, dom, next);

	return (dom);
}

static void
amdvi_free_ptp(uint64_t *ptp, int level)
{
	int i;

	if (level < 1)
		return;

	for (i = 0; i < NPTEPG ; i++) {
		if ((ptp[i] & AMDVI_PT_PRESENT) == 0)
			continue;
		/* XXX: Add super-page or PTE mapping > 4KB. */
#ifdef notyet
		/* Super-page mapping. */
		if (AMDVI_PD_SUPER(ptp[i]))
			continue;
#endif

		amdvi_free_ptp((uint64_t *)PHYS_TO_DMAP(ptp[i]
		    & AMDVI_PT_MASK), level - 1);

	}

	free(ptp, M_AMDVI);
}

static void
amdvi_destroy_domain(void *arg)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;
	KASSERT(domain, ("domain is NULL"));
#ifdef AMDVI_DEBUG_CMD
	printf("Destroying domain %d\n", domain->id);
#endif
	if (domain->ptp)
		amdvi_free_ptp(domain->ptp, domain->ptp_level);

	amdvi_do_inv_domain(domain->id, false);
	SLIST_REMOVE(&dom_head, domain, amdvi_domain, next);
	free(domain, M_AMDVI);
}

static uint64_t
amdvi_set_pt(uint64_t *pt, int level, vm_paddr_t gpa,
    vm_paddr_t hpa, uint64_t pg_size, bool create)
{
	uint64_t *page, pa;
	int shift, index;
	const int PT_SHIFT = 9;
	const int PT_INDEX_MASK = (1 << PT_SHIFT) - 1;	/* Based on PT_SHIFT */

	if (!pg_size)
		return (0);

	if (hpa & (pg_size - 1)) {
		printf("HPA is not size aligned.\n");
		return (0);
	}
	if (gpa & (pg_size - 1)) {
		printf("HPA is not size aligned.\n");
		return (0);
	}
	shift = PML4SHIFT;
	while ((shift > PAGE_SHIFT) && (pg_size < (1UL << shift))) {
		index = (gpa >> shift) & PT_INDEX_MASK;

		if ((pt[index] == 0) && create) {
			page = malloc(PAGE_SIZE, M_AMDVI, M_WAITOK | M_ZERO);
			pa = vtophys(page);
			pt[index] = pa | AMDVI_PT_PRESENT | AMDVI_PT_RW |
			    ((level - 1) << AMDVI_PD_LEVEL_SHIFT);
		}
#ifdef AMDVI_DEBUG_PTE
		if ((gpa % 0x1000000) == 0)
			printf("[level%d, shift = %d]PTE:0x%lx\n",
			    level, shift, pt[index]);
#endif
#define PTE2PA(x)	((uint64_t)(x) & AMDVI_PT_MASK)
		pa = PTE2PA(pt[index]);
		pt = (uint64_t *)PHYS_TO_DMAP(pa);
		shift -= PT_SHIFT;
		level--;
	}

	/* Leaf entry. */
	index = (gpa >> shift) & PT_INDEX_MASK;

	if (create) {
		pt[index] = hpa | AMDVI_PT_RW | AMDVI_PT_PRESENT;
	} else
		pt[index] = 0;

#ifdef AMDVI_DEBUG_PTE
	if ((gpa % 0x1000000) == 0)
		printf("[Last level%d, shift = %d]PTE:0x%lx\n",
		    level, shift, pt[index]);
#endif
	return (1ULL << shift);
}

static uint64_t
amdvi_update_mapping(struct amdvi_domain *domain, vm_paddr_t gpa,
    vm_paddr_t hpa, uint64_t size, bool create)
{
	uint64_t mapped, *ptp, len;
	int level;

	KASSERT(domain, ("domain is NULL"));
	level = domain->ptp_level;
	KASSERT(level, ("Page table level is 0"));

	ptp = domain->ptp;
	KASSERT(ptp, ("PTP is NULL"));
	mapped = 0;
	while (mapped < size) {
		len = amdvi_set_pt(ptp, level, gpa + mapped, hpa + mapped,
		    PAGE_SIZE, create);
		if (!len) {
			printf("Error: Couldn't map HPA:0x%lx GPA:0x%lx\n",
			    hpa, gpa);
			return (0);
		}
		mapped += len;
	}

	return (mapped);
}

static uint64_t
amdvi_create_mapping(void *arg, vm_paddr_t gpa, vm_paddr_t hpa,
    uint64_t len)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;

	if (domain->id && !domain->ptp) {
		printf("ptp is NULL");
		return (-1);
	}

	/*
	 * If host domain is created w/o page table, skip IOMMU page
	 * table set-up.
	 */
	if (domain->ptp)
		return (amdvi_update_mapping(domain, gpa, hpa, len, true));
	else
		return (len);
}

static uint64_t
amdvi_destroy_mapping(void *arg, vm_paddr_t gpa, uint64_t len)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;
	/*
	 * If host domain is created w/o page table, skip IOMMU page
	 * table set-up.
	 */
	if (domain->ptp)
		return (amdvi_update_mapping(domain, gpa, 0, len, false));
	return
	    (len);
}

static struct amdvi_softc *
amdvi_find_iommu(uint16_t devid)
{
	struct amdvi_softc *softc;
	int i;

	for (i = 0; i < ivhd_count; i++) {
		softc = device_get_softc(ivhd_devs[i]);
		if ((devid >= softc->start_dev_rid) &&
		    (devid <= softc->end_dev_rid))
			return (softc);
	}

	/*
	 * XXX: BIOS bug, device not in IVRS table, assume its from first IOMMU.
	 */
	printf("BIOS bug device(%d.%d.%d) doesn't have IVHD entry.\n",
	    RID2PCI_STR(devid));

	return (device_get_softc(ivhd_devs[0]));
}

/*
 * Set-up device table entry.
 * IOMMU spec Rev 2.0, section 3.2.2.2, some of the fields must
 * be set concurrently, e.g. read and write bits.
 */
static void
amdvi_set_dte(struct amdvi_domain *domain, uint16_t devid, bool enable)
{
	struct amdvi_softc *softc;
	struct amdvi_dte* temp;

	KASSERT(domain, ("domain is NULL for pci_rid:0x%x\n", devid));
	
	softc = amdvi_find_iommu(devid);
	KASSERT(softc, ("softc is NULL for pci_rid:0x%x\n", devid));

	temp = &amdvi_dte[devid];

#ifdef AMDVI_ATS_ENABLE
	/* If IOMMU and device support IOTLB, enable it. */
	if (amdvi_dev_support_iotlb(softc, devid) && softc->iotlb)
		temp->iotlb_enable = 1;
#endif

	/* Avoid duplicate I/O faults. */
	temp->sup_second_io_fault = 1;
	temp->sup_all_io_fault = amdvi_disable_io_fault;

	temp->dt_valid = 1;
	temp->domain_id = domain->id;

	if (enable) {
		if (domain->ptp) {
			temp->pt_base = vtophys(domain->ptp) >> 12;
			temp->pt_level = amdvi_ptp_level;
		}
		/*
		 * XXX: Page table valid[TV] bit must be set even if host domain
		 * page tables are not enabled.
		 */
		temp->pt_valid = 1;
		temp->read_allow = 1;
		temp->write_allow = 1;
	}
}

static void
amdvi_inv_device(uint16_t devid)
{
	struct amdvi_softc *softc;

	softc = amdvi_find_iommu(devid);
	KASSERT(softc, ("softc is NULL"));

	amdvi_cmd_inv_dte(softc, devid);
#ifdef AMDVI_ATS_ENABLE
	if (amdvi_dev_support_iotlb(softc, devid))
		amdvi_cmd_inv_iotlb(softc, devid);
#endif
	amdvi_wait(softc);
}

static void
amdvi_add_device(void *arg, uint16_t devid)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;
	KASSERT(domain != NULL, ("domain is NULL"));
#ifdef AMDVI_DEBUG_CMD
	printf("Assigning device(%d.%d.%d) to domain:%d\n",
	    RID2PCI_STR(devid), domain->id);
#endif
	amdvi_set_dte(domain, devid, true);
	amdvi_inv_device(devid);
}

static void
amdvi_remove_device(void *arg, uint16_t devid)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;
#ifdef AMDVI_DEBUG_CMD
	printf("Remove device(0x%x) from domain:%d\n",
	       devid, domain->id);
#endif
	amdvi_set_dte(domain, devid, false);
	amdvi_inv_device(devid);
}

static void
amdvi_enable(void)
{
	struct amdvi_ctrl *ctrl;
	struct amdvi_softc *softc;
	uint64_t val;
	int i;

	for (i = 0; i < ivhd_count; i++) {
		softc = device_get_softc(ivhd_devs[i]);
		KASSERT(softc, ("softc is NULL\n"));
		ctrl = softc->ctrl;
		KASSERT(ctrl, ("ctrl is NULL\n"));

		val = (	AMDVI_CTRL_EN 		|
			AMDVI_CTRL_CMD 		|
		    	AMDVI_CTRL_ELOG 	|
		    	AMDVI_CTRL_ELOGINT 	|
		    	AMDVI_CTRL_INV_TO_1S);

		if (softc->ivhd_flag & IVHD_FLAG_COH)
			val |= AMDVI_CTRL_COH;
		if (softc->ivhd_flag & IVHD_FLAG_HTT)
			val |= AMDVI_CTRL_HTT;
		if (softc->ivhd_flag & IVHD_FLAG_RPPW)
			val |= AMDVI_CTRL_RPPW;
		if (softc->ivhd_flag & IVHD_FLAG_PPW)
			val |= AMDVI_CTRL_PPW;
		if (softc->ivhd_flag & IVHD_FLAG_ISOC)
			val |= AMDVI_CTRL_ISOC;

		ctrl->control = val;
	}
}

static void
amdvi_disable(void)
{
	struct amdvi_ctrl *ctrl;
	struct amdvi_softc *softc;
	int i;

	for (i = 0; i < ivhd_count; i++) {
		softc = device_get_softc(ivhd_devs[i]);
		KASSERT(softc, ("softc is NULL\n"));
		ctrl = softc->ctrl;
		KASSERT(ctrl, ("ctrl is NULL\n"));

		ctrl->control = 0;
	}
}

static void
amdvi_inv_tlb(void *arg)
{
	struct amdvi_domain *domain;

	domain = (struct amdvi_domain *)arg;
	KASSERT(domain, ("domain is NULL"));
	amdvi_do_inv_domain(domain->id, false);
}

struct iommu_ops iommu_ops_amd = {
	amdvi_init,
	amdvi_cleanup,
	amdvi_enable,
	amdvi_disable,
	amdvi_create_domain,
	amdvi_destroy_domain,
	amdvi_create_mapping,
	amdvi_destroy_mapping,
	amdvi_add_device,
	amdvi_remove_device,
	amdvi_inv_tlb
};
