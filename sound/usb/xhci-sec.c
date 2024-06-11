// SPDX-License-Identifier: GPL-2.0
/*
 * xHCI secondary ring APIs
 *
 * Copyright (c) 2019,2021 The Linux Foundation. All rights reserved.
 * Copyright (C) 2008 Intel Corp.
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>

#include "xhci.h"

struct xhci_sec {
	struct xhci_ring	*event_ring;
	struct xhci_erst	erst;
	/* secondary interrupter */
	struct xhci_intr_reg __iomem *ir_set;
	struct xhci_hcd		*xhci;
	int			intr_num;

	struct list_head	list;
};

static LIST_HEAD(xhci_sec);

/* simplified redefinition from XHCI */
#define hcd_to_xhci(h) \
	((struct xhci_hcd *)(((h)->primary_hcd ?: (h))->hcd_priv))

static int xhci_event_ring_setup(struct xhci_hcd *xhci, struct xhci_ring **er,
	struct xhci_intr_reg __iomem *ir_set, struct xhci_erst *erst,
	unsigned int intr_num, gfp_t flags)
{
	dma_addr_t deq;
	u64 val_64;
	unsigned int val;
	int ret;

	*er = xhci_ring_alloc(xhci, ERST_NUM_SEGS, 1, TYPE_EVENT, 0, flags);
	if (!*er)
		return -ENOMEM;

	ret = xhci_alloc_erst(xhci, *er, erst, flags);
	if (ret)
		return ret;

	xhci_dbg(xhci, "intr# %d: num segs = %i, virt addr = %pK, dma addr = 0x%llx",
			intr_num,
			erst->num_entries,
			erst->entries,
			(unsigned long long)erst->erst_dma_addr);

	/* set ERST count with the number of entries in the segment table */
	val = readl_relaxed(&ir_set->erst_size);
	val &= ERST_SIZE_MASK;
	val |= ERST_NUM_SEGS;
	xhci_dbg(xhci, "Write ERST size = %i to ir_set %d (some bits preserved)",
			val, intr_num);
	writel_relaxed(val, &ir_set->erst_size);

	xhci_dbg(xhci, "intr# %d: Set ERST entries to point to event ring.",
			intr_num);
	/* set the segment table base address */
	xhci_dbg(xhci, "Set ERST base address for ir_set %d = 0x%llx",
			intr_num,
			(unsigned long long)erst->erst_dma_addr);
	val_64 = xhci_read_64(xhci, &ir_set->erst_base);
	val_64 &= ERST_PTR_MASK;
	val_64 |= (erst->erst_dma_addr & (u64) ~ERST_PTR_MASK);
	xhci_write_64(xhci, val_64, &ir_set->erst_base);

	/* Set the event ring dequeue address */
	deq = xhci_trb_virt_to_dma((*er)->deq_seg, (*er)->dequeue);
	if (deq == 0 && !in_interrupt())
		xhci_warn(xhci,
		"intr# %d:WARN something wrong with SW event ring deq ptr.\n",
		intr_num);
	/* Update HC event ring dequeue pointer */
	val_64 = xhci_read_64(xhci, &ir_set->erst_dequeue);
	val_64 &= ERST_PTR_MASK;
	/* Don't clear the EHB bit (which is RW1C) because
	 * there might be more events to service.
	 */
	val_64 &= ~ERST_EHB;
	xhci_dbg(xhci, "intr# %d:Write event ring dequeue pointer, preserving EHB bit",
		intr_num);
	xhci_write_64(xhci, ((u64) deq & (u64) ~ERST_PTR_MASK) | val_64,
			&ir_set->erst_dequeue);
	xhci_dbg(xhci, "Wrote ERST address to ir_set %d.", intr_num);

	return 0;
}

struct xhci_ring *xhci_sec_event_ring_setup(struct usb_device *udev, unsigned int intr_num)
{
	int ret;
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_sec *sec;

	if (udev->state == USB_STATE_NOTATTACHED || !HCD_RH_RUNNING(hcd))
		return ERR_PTR(-ENODEV);

	if (!xhci->max_interrupters)
		xhci->max_interrupters = HCS_MAX_INTRS(xhci->hcs_params1);

	if ((xhci->xhc_state & XHCI_STATE_HALTED) ||
			intr_num >= xhci->max_interrupters) {
		xhci_err(xhci, "%s:state %x intr# %d\n", __func__,
				xhci->xhc_state, intr_num);
		return ERR_PTR(-EINVAL);
	}

	list_for_each_entry(sec, &xhci_sec, list) {
		if (sec->xhci == xhci && sec->intr_num == intr_num)
			goto done;
	}

	sec = kzalloc(sizeof(*sec), GFP_KERNEL);
	if (!sec)
		return ERR_PTR(-ENOMEM);

	sec->intr_num = intr_num;
	sec->xhci = xhci;
	sec->ir_set = &xhci->run_regs->ir_set[intr_num];
	ret = xhci_event_ring_setup(xhci, &sec->event_ring, sec->ir_set,
				&sec->erst, intr_num, GFP_KERNEL);
	if (ret) {
		xhci_err(xhci, "sec event ring setup failed inter#%d\n",
			intr_num);
		kfree(sec);
		return ERR_PTR(ret);
	}
	list_add_tail(&sec->list, &xhci_sec);
done:
	return sec->event_ring;
}

static void xhci_handle_sec_intr_events(struct xhci_hcd *xhci,
		struct xhci_ring *ring, struct xhci_intr_reg __iomem *ir_set)
{
	union xhci_trb *erdp_trb, *current_trb;
	struct xhci_segment	*seg;
	u64 erdp_reg;
	u32 iman_reg;
	dma_addr_t deq;
	unsigned long segment_offset;

	/* disable irq, ack pending interrupt and ack all pending events */

	iman_reg = readl_relaxed(&ir_set->irq_pending);
	iman_reg &= ~IMAN_IE;
	writel_relaxed(iman_reg, &ir_set->irq_pending);
	iman_reg = readl_relaxed(&ir_set->irq_pending);
	if (iman_reg & IMAN_IP)
		writel_relaxed(iman_reg, &ir_set->irq_pending);

	/* last acked event trb is in erdp reg  */
	erdp_reg = xhci_read_64(xhci, &ir_set->erst_dequeue);
	deq = (dma_addr_t)(erdp_reg & ~ERST_PTR_MASK);
	if (!deq) {
		pr_debug("%s: event ring handling not required\n", __func__);
		return;
	}

	seg = ring->first_seg;
	segment_offset = deq - seg->dma;

	/* find out virtual address of the last acked event trb */
	erdp_trb = current_trb = &seg->trbs[0] +
				(segment_offset/sizeof(*current_trb));

	/* read cycle state of the last acked trb to find out CCS */
	ring->cycle_state = le32_to_cpu(current_trb->event_cmd.flags) & TRB_CYCLE;

	while (1) {
		/* last trb of the event ring: toggle cycle state */
		if (current_trb == &seg->trbs[TRBS_PER_SEGMENT - 1]) {
			ring->cycle_state ^= 1;
			current_trb = &seg->trbs[0];
		} else {
			current_trb++;
		}

		/* cycle state transition */
		if ((le32_to_cpu(current_trb->event_cmd.flags) & TRB_CYCLE) !=
		    ring->cycle_state)
			break;
	}

	if (erdp_trb != current_trb) {
		deq = xhci_trb_virt_to_dma(ring->deq_seg, current_trb);
		if (deq == 0)
			xhci_warn(xhci,
				"WARN invalid SW event ring dequeue ptr.\n");
		/* Update HC event ring dequeue pointer */
		erdp_reg &= ERST_PTR_MASK;
		erdp_reg |= ((u64) deq & (u64) ~ERST_PTR_MASK);
	}

	/* Clear the event handler busy flag (RW1C); event ring is empty. */
	erdp_reg |= ERST_EHB;
	xhci_write_64(xhci, erdp_reg, &ir_set->erst_dequeue);
}

static int sec_event_ring_cleanup(struct xhci_hcd *xhci, struct xhci_ring *ring,
		struct xhci_intr_reg __iomem *ir_set, struct xhci_erst *erst)
{
	int size;
	struct device *dev = xhci_to_hcd(xhci)->self.sysdev;

	if (!HCD_RH_RUNNING(xhci_to_hcd(xhci)))
		return 0;

	size = sizeof(struct xhci_erst_entry)*(erst->num_entries);
	if (erst->entries) {
		xhci_handle_sec_intr_events(xhci, ring, ir_set);
		dma_free_coherent(dev, size, erst->entries,
				erst->erst_dma_addr);
		erst->entries = NULL;
	}

	xhci_ring_free(xhci, ring);
	xhci_dbg(xhci, "Freed sec event ring");

	return 0;
}

int xhci_sec_event_ring_cleanup(struct usb_device *udev, struct xhci_ring *ring)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct xhci_sec *sec;
	unsigned long flags;

	spin_lock_irqsave(&xhci->lock, flags);
	list_for_each_entry(sec, &xhci_sec, list) {
		if (sec->event_ring == ring) {
			list_del(&sec->list);
			spin_unlock_irqrestore(&xhci->lock, flags);
			sec_event_ring_cleanup(xhci, ring, sec->ir_set,
					&sec->erst);
			kfree(sec);
			return 0;
		}
	}
	spin_unlock_irqrestore(&xhci->lock, flags);
	return 0;
}

phys_addr_t xhci_get_sec_event_ring_phys_addr(struct usb_device *udev,
	struct xhci_ring *ring, dma_addr_t *dma)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct device *dev = hcd->self.sysdev;
	struct sg_table sgt;
	phys_addr_t pa;
	struct xhci_sec *sec;

	if (udev->state == USB_STATE_NOTATTACHED || !HCD_RH_RUNNING(hcd) ||
			(xhci->xhc_state & XHCI_STATE_HALTED))
		return 0;

	list_for_each_entry(sec, &xhci_sec, list) {
		if (sec->event_ring == ring) {
			dma_get_sgtable(dev, &sgt, ring->first_seg->trbs,
				ring->first_seg->dma, TRB_SEGMENT_SIZE);

			*dma = ring->first_seg->dma;

			pa = page_to_phys(sg_page(sgt.sgl));
			sg_free_table(&sgt);

			return pa;
		}
	}

	return 0;
}

/* Returns 1 if the arguments are OK;
 * returns 0 this is a root hub; returns -EINVAL for NULL pointers.
 */
static int xhci_check_args(struct usb_hcd *hcd, struct usb_device *udev,
		struct usb_host_endpoint *ep, int check_ep, bool check_virt_dev,
		const char *func)
{
	struct xhci_hcd	*xhci;
	struct xhci_virt_device	*virt_dev;

	if (!hcd || (check_ep && !ep) || !udev) {
		pr_debug("xHCI %s called with invalid args\n", func);
		return -EINVAL;
	}
	if (!udev->parent) {
		pr_debug("xHCI %s called for root hub\n", func);
		return 0;
	}

	xhci = hcd_to_xhci(hcd);
	if (check_virt_dev) {
		if (!udev->slot_id || !xhci->devs[udev->slot_id]) {
			xhci_dbg(xhci, "xHCI %s called with unaddressed device\n",
					func);
			return -EINVAL;
		}

		virt_dev = xhci->devs[udev->slot_id];
		if (virt_dev->udev != udev) {
			xhci_dbg(xhci, "xHCI %s called with udev and virt_dev does not match\n",
					func);
			return -EINVAL;
		}
	}

	if (xhci->xhc_state & XHCI_STATE_HALTED)
		return -ENODEV;

	return 1;
}

phys_addr_t xhci_get_xfer_ring_phys_addr(struct usb_device *udev,
		struct usb_host_endpoint *ep, dma_addr_t *dma)
{
	int ret;
	unsigned int ep_index;
	struct xhci_virt_device *virt_dev;
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct device *dev = hcd->self.sysdev;
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	struct sg_table sgt;
	phys_addr_t pa;

	if (udev->state == USB_STATE_NOTATTACHED || !HCD_RH_RUNNING(hcd))
		return 0;

	ret = xhci_check_args(hcd, udev, ep, 1, true, __func__);
	if (ret <= 0) {
		xhci_err(xhci, "%s: invalid args\n", __func__);
		return 0;
	}

	virt_dev = xhci->devs[udev->slot_id];
	ep_index = xhci_get_endpoint_index(&ep->desc);

	if (virt_dev->eps[ep_index].ring &&
		virt_dev->eps[ep_index].ring->first_seg) {

		dma_get_sgtable(dev, &sgt,
			virt_dev->eps[ep_index].ring->first_seg->trbs,
			virt_dev->eps[ep_index].ring->first_seg->dma,
			TRB_SEGMENT_SIZE);

		*dma = virt_dev->eps[ep_index].ring->first_seg->dma;

		pa = page_to_phys(sg_page(sgt.sgl));
		sg_free_table(&sgt);

		return pa;
	}

	return 0;
}

/* Ring the host controller doorbell after placing a command on the ring */
int xhci_stop_endpoint(struct usb_device *udev, struct usb_host_endpoint *ep)
{
	struct usb_hcd *hcd = bus_to_hcd(udev->bus);
	struct xhci_hcd *xhci = hcd_to_xhci(hcd);
	unsigned int ep_index;
	struct xhci_virt_device *virt_dev;
	struct xhci_command *cmd;
	unsigned long flags;
	int ret = 0;

	ret = xhci_check_args(hcd, udev, ep, 1, true, __func__);
	if (ret <= 0)
		return ret;

	cmd = xhci_alloc_command(xhci, true, GFP_NOIO);
	if (!cmd)
		return -ENOMEM;

	spin_lock_irqsave(&xhci->lock, flags);
	virt_dev = xhci->devs[udev->slot_id];
	if (!virt_dev) {
		ret = -ENODEV;
		goto err;
	}

	ep_index = xhci_get_endpoint_index(&ep->desc);
	if (virt_dev->eps[ep_index].ring &&
			virt_dev->eps[ep_index].ring->dequeue) {
		ret = xhci_queue_stop_endpoint(xhci, cmd, udev->slot_id,
				ep_index, 0);
		if (ret)
			goto err;

		xhci_ring_cmd_db(xhci);
		spin_unlock_irqrestore(&xhci->lock, flags);

		/* Wait for stop endpoint command to finish */
		wait_for_completion(cmd->completion);

		if (cmd->status == COMP_COMMAND_ABORTED ||
				cmd->status == COMP_STOPPED) {
			xhci_warn(xhci,
				"stop endpoint command timeout for ep%d%s\n",
				usb_endpoint_num(&ep->desc),
				usb_endpoint_dir_in(&ep->desc) ? "in" : "out");
			ret = -ETIME;
		}
		goto free_cmd;
	}

err:
	spin_unlock_irqrestore(&xhci->lock, flags);
free_cmd:
	xhci_free_command(xhci, cmd);
	return ret;
}
