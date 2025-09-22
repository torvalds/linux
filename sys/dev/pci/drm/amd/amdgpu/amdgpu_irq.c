/*
 * Copyright 2008 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 * Copyright 2009 Jerome Glisse.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 *          Jerome Glisse
 */

/**
 * DOC: Interrupt Handling
 *
 * Interrupts generated within GPU hardware raise interrupt requests that are
 * passed to amdgpu IRQ handler which is responsible for detecting source and
 * type of the interrupt and dispatching matching handlers. If handling an
 * interrupt requires calling kernel functions that may sleep processing is
 * dispatched to work handlers.
 *
 * If MSI functionality is not disabled by module parameter then MSI
 * support will be enabled.
 *
 * For GPU interrupt sources that may be driven by another driver, IRQ domain
 * support is used (with mapping between virtual and hardware IRQs).
 */

#include <linux/irq.h>
#include <linux/pci.h>

#include <drm/drm_vblank.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_drv.h>
#include "amdgpu.h"
#include "amdgpu_ih.h"
#include "atom.h"
#include "amdgpu_connectors.h"
#include "amdgpu_trace.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_ras.h"

#include <linux/pm_runtime.h>

#ifdef CONFIG_DRM_AMD_DC
#include "amdgpu_dm_irq.h"
#endif

#define AMDGPU_WAIT_IDLE_TIMEOUT 200

const char *soc15_ih_clientid_name[] = {
	"IH",
	"SDMA2 or ACP",
	"ATHUB",
	"BIF",
	"SDMA3 or DCE",
	"SDMA4 or ISP",
	"VMC1 or PCIE0",
	"RLC",
	"SDMA0",
	"SDMA1",
	"SE0SH",
	"SE1SH",
	"SE2SH",
	"SE3SH",
	"VCN1 or UVD1",
	"THM",
	"VCN or UVD",
	"SDMA5 or VCE0",
	"VMC",
	"SDMA6 or XDMA",
	"GRBM_CP",
	"ATS",
	"ROM_SMUIO",
	"DF",
	"SDMA7 or VCE1",
	"PWR",
	"reserved",
	"UTCL2",
	"EA",
	"UTCL2LOG",
	"MP0",
	"MP1"
};

const int node_id_to_phys_map[NODEID_MAX] = {
	[AID0_NODEID] = 0,
	[XCD0_NODEID] = 0,
	[XCD1_NODEID] = 1,
	[AID1_NODEID] = 1,
	[XCD2_NODEID] = 2,
	[XCD3_NODEID] = 3,
	[AID2_NODEID] = 2,
	[XCD4_NODEID] = 4,
	[XCD5_NODEID] = 5,
	[AID3_NODEID] = 3,
	[XCD6_NODEID] = 6,
	[XCD7_NODEID] = 7,
};

/**
 * amdgpu_irq_disable_all - disable *all* interrupts
 *
 * @adev: amdgpu device pointer
 *
 * Disable all types of interrupts from all sources.
 */
void amdgpu_irq_disable_all(struct amdgpu_device *adev)
{
	unsigned long irqflags;
	unsigned int i, j, k;
	int r;

	spin_lock_irqsave(&adev->irq.lock, irqflags);
	for (i = 0; i < AMDGPU_IRQ_CLIENTID_MAX; ++i) {
		if (!adev->irq.client[i].sources)
			continue;

		for (j = 0; j < AMDGPU_MAX_IRQ_SRC_ID; ++j) {
			struct amdgpu_irq_src *src = adev->irq.client[i].sources[j];

			if (!src || !src->funcs->set || !src->num_types)
				continue;

			for (k = 0; k < src->num_types; ++k) {
				r = src->funcs->set(adev, src, k,
						    AMDGPU_IRQ_STATE_DISABLE);
				if (r)
					DRM_ERROR("error disabling interrupt (%d)\n",
						  r);
			}
		}
	}
	spin_unlock_irqrestore(&adev->irq.lock, irqflags);
}

/**
 * amdgpu_irq_handler - IRQ handler
 *
 * @irq: IRQ number (unused)
 * @arg: pointer to DRM device
 *
 * IRQ handler for amdgpu driver (all ASICs).
 *
 * Returns:
 * result of handling the IRQ, as defined by &irqreturn_t
 */
irqreturn_t amdgpu_irq_handler(void *arg)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct amdgpu_device *adev = drm_to_adev(dev);
	irqreturn_t ret;

	if (!adev->irq.installed)
		return 0;

	ret = amdgpu_ih_process(adev, &adev->irq.ih);
	if (ret == IRQ_HANDLED)
		pm_runtime_mark_last_busy(dev->dev);

	amdgpu_ras_interrupt_fatal_error_handler(adev);

	return ret;
}

/**
 * amdgpu_irq_handle_ih1 - kick of processing for IH1
 *
 * @work: work structure in struct amdgpu_irq
 *
 * Kick of processing IH ring 1.
 */
static void amdgpu_irq_handle_ih1(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  irq.ih1_work);

	amdgpu_ih_process(adev, &adev->irq.ih1);
}

/**
 * amdgpu_irq_handle_ih2 - kick of processing for IH2
 *
 * @work: work structure in struct amdgpu_irq
 *
 * Kick of processing IH ring 2.
 */
static void amdgpu_irq_handle_ih2(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  irq.ih2_work);

	amdgpu_ih_process(adev, &adev->irq.ih2);
}

/**
 * amdgpu_irq_handle_ih_soft - kick of processing for ih_soft
 *
 * @work: work structure in struct amdgpu_irq
 *
 * Kick of processing IH soft ring.
 */
static void amdgpu_irq_handle_ih_soft(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  irq.ih_soft_work);

	amdgpu_ih_process(adev, &adev->irq.ih_soft);
}

/**
 * amdgpu_msi_ok - check whether MSI functionality is enabled
 *
 * @adev: amdgpu device pointer (unused)
 *
 * Checks whether MSI functionality has been disabled via module parameter
 * (all ASICs).
 *
 * Returns:
 * *true* if MSIs are allowed to be enabled or *false* otherwise
 */
bool amdgpu_msi_ok(struct amdgpu_device *adev)
{
	if (amdgpu_msi == 1)
		return true;
	else if (amdgpu_msi == 0)
		return false;

	return true;
}

static void amdgpu_restore_msix(struct amdgpu_device *adev)
{
	STUB();
#ifdef notyet
	u16 ctrl;

	pci_read_config_word(adev->pdev, adev->pdev->msix_cap + PCI_MSIX_FLAGS, &ctrl);
	if (!(ctrl & PCI_MSIX_FLAGS_ENABLE))
		return;

	/* VF FLR */
	ctrl &= ~PCI_MSIX_FLAGS_ENABLE;
	pci_write_config_word(adev->pdev, adev->pdev->msix_cap + PCI_MSIX_FLAGS, ctrl);
	ctrl |= PCI_MSIX_FLAGS_ENABLE;
	pci_write_config_word(adev->pdev, adev->pdev->msix_cap + PCI_MSIX_FLAGS, ctrl);
#endif
}

/**
 * amdgpu_irq_init - initialize interrupt handling
 *
 * @adev: amdgpu device pointer
 *
 * Sets up work functions for hotplug and reset interrupts, enables MSI
 * functionality, initializes vblank, hotplug and reset interrupt handling.
 *
 * Returns:
 * 0 on success or error code on failure
 */
int amdgpu_irq_init(struct amdgpu_device *adev)
{
	unsigned int irq, flags;
	int r;

	mtx_init(&adev->irq.lock, IPL_TTY);

#ifdef notyet
	/* Enable MSI if not disabled by module parameter */
	adev->irq.msi_enabled = false;

	if (!amdgpu_msi_ok(adev))
		flags = PCI_IRQ_INTX;
	else
		flags = PCI_IRQ_ALL_TYPES;

	/* we only need one vector */
	r = pci_alloc_irq_vectors(adev->pdev, 1, 1, flags);
	if (r < 0) {
		dev_err(adev->dev, "Failed to alloc msi vectors\n");
		return r;
	}

	if (amdgpu_msi_ok(adev)) {
		adev->irq.msi_enabled = true;
		dev_dbg(adev->dev, "using MSI/MSI-X.\n");
	}
#endif

	INIT_WORK(&adev->irq.ih1_work, amdgpu_irq_handle_ih1);
	INIT_WORK(&adev->irq.ih2_work, amdgpu_irq_handle_ih2);
	INIT_WORK(&adev->irq.ih_soft_work, amdgpu_irq_handle_ih_soft);

	/* Use vector 0 for MSI-X. */
	r = pci_irq_vector(adev->pdev, 0);
	if (r < 0)
		goto free_vectors;
	irq = r;

	/* PCI devices require shared interrupts. */
	r = request_irq(irq, amdgpu_irq_handler, IRQF_SHARED, adev_to_drm(adev)->driver->name,
			adev_to_drm(adev));
	if (r)
		goto free_vectors;

	adev->irq.installed = true;
	adev->irq.irq = irq;
	adev_to_drm(adev)->max_vblank_count = 0x00ffffff;

	DRM_DEBUG("amdgpu: irq initialized.\n");
	return 0;

free_vectors:
	if (adev->irq.msi_enabled)
		pci_free_irq_vectors(adev->pdev);

	adev->irq.msi_enabled = false;
	return r;
}

void amdgpu_irq_fini_hw(struct amdgpu_device *adev)
{
	if (adev->irq.installed) {
		free_irq(adev->irq.irq, adev_to_drm(adev));
		adev->irq.installed = false;
		if (adev->irq.msi_enabled)
			pci_free_irq_vectors(adev->pdev);
	}

	amdgpu_ih_ring_fini(adev, &adev->irq.ih_soft);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih1);
	amdgpu_ih_ring_fini(adev, &adev->irq.ih2);
}

/**
 * amdgpu_irq_fini_sw - shut down interrupt handling
 *
 * @adev: amdgpu device pointer
 *
 * Tears down work functions for hotplug and reset interrupts, disables MSI
 * functionality, shuts down vblank, hotplug and reset interrupt handling,
 * turns off interrupts from all sources (all ASICs).
 */
void amdgpu_irq_fini_sw(struct amdgpu_device *adev)
{
	unsigned int i, j;

	for (i = 0; i < AMDGPU_IRQ_CLIENTID_MAX; ++i) {
		if (!adev->irq.client[i].sources)
			continue;

		for (j = 0; j < AMDGPU_MAX_IRQ_SRC_ID; ++j) {
			struct amdgpu_irq_src *src = adev->irq.client[i].sources[j];

			if (!src)
				continue;

			kfree(src->enabled_types);
			src->enabled_types = NULL;
		}
		kfree(adev->irq.client[i].sources);
		adev->irq.client[i].sources = NULL;
	}
}

/**
 * amdgpu_irq_add_id - register IRQ source
 *
 * @adev: amdgpu device pointer
 * @client_id: client id
 * @src_id: source id
 * @source: IRQ source pointer
 *
 * Registers IRQ source on a client.
 *
 * Returns:
 * 0 on success or error code otherwise
 */
int amdgpu_irq_add_id(struct amdgpu_device *adev,
		      unsigned int client_id, unsigned int src_id,
		      struct amdgpu_irq_src *source)
{
	if (client_id >= AMDGPU_IRQ_CLIENTID_MAX)
		return -EINVAL;

	if (src_id >= AMDGPU_MAX_IRQ_SRC_ID)
		return -EINVAL;

	if (!source->funcs)
		return -EINVAL;

	if (!adev->irq.client[client_id].sources) {
		adev->irq.client[client_id].sources =
			kcalloc(AMDGPU_MAX_IRQ_SRC_ID,
				sizeof(struct amdgpu_irq_src *),
				GFP_KERNEL);
		if (!adev->irq.client[client_id].sources)
			return -ENOMEM;
	}

	if (adev->irq.client[client_id].sources[src_id] != NULL)
		return -EINVAL;

	if (source->num_types && !source->enabled_types) {
		atomic_t *types;

		types = kcalloc(source->num_types, sizeof(atomic_t),
				GFP_KERNEL);
		if (!types)
			return -ENOMEM;

		source->enabled_types = types;
	}

	adev->irq.client[client_id].sources[src_id] = source;
	return 0;
}

/**
 * amdgpu_irq_dispatch - dispatch IRQ to IP blocks
 *
 * @adev: amdgpu device pointer
 * @ih: interrupt ring instance
 *
 * Dispatches IRQ to IP blocks.
 */
void amdgpu_irq_dispatch(struct amdgpu_device *adev,
			 struct amdgpu_ih_ring *ih)
{
	u32 ring_index = ih->rptr >> 2;
	struct amdgpu_iv_entry entry;
	unsigned int client_id, src_id;
	struct amdgpu_irq_src *src;
	bool handled = false;
	int r;

	entry.ih = ih;
	entry.iv_entry = (const uint32_t *)&ih->ring[ring_index];

	/*
	 * timestamp is not supported on some legacy SOCs (cik, cz, iceland,
	 * si and tonga), so initialize timestamp and timestamp_src to 0
	 */
	entry.timestamp = 0;
	entry.timestamp_src = 0;

	amdgpu_ih_decode_iv(adev, &entry);

	trace_amdgpu_iv(ih - &adev->irq.ih, &entry);

	client_id = entry.client_id;
	src_id = entry.src_id;

	if (client_id >= AMDGPU_IRQ_CLIENTID_MAX) {
		DRM_DEBUG("Invalid client_id in IV: %d\n", client_id);

	} else	if (src_id >= AMDGPU_MAX_IRQ_SRC_ID) {
		DRM_DEBUG("Invalid src_id in IV: %d\n", src_id);

	} else if (((client_id == AMDGPU_IRQ_CLIENTID_LEGACY) ||
		    (client_id == SOC15_IH_CLIENTID_ISP)) &&
		   adev->irq.virq[src_id]) {
		STUB();
#ifdef notyet
		generic_handle_domain_irq(adev->irq.domain, src_id);
#endif

	} else if (!adev->irq.client[client_id].sources) {
		DRM_DEBUG("Unregistered interrupt client_id: %d src_id: %d\n",
			  client_id, src_id);

	} else if ((src = adev->irq.client[client_id].sources[src_id])) {
		r = src->funcs->process(adev, src, &entry);
		if (r < 0)
			DRM_ERROR("error processing interrupt (%d)\n", r);
		else if (r)
			handled = true;

	} else {
		DRM_DEBUG("Unregistered interrupt src_id: %d of client_id:%d\n",
			src_id, client_id);
	}

	/* Send it to amdkfd as well if it isn't already handled */
	if (!handled)
		amdgpu_amdkfd_interrupt(adev, entry.iv_entry);

	if (amdgpu_ih_ts_after(ih->processed_timestamp, entry.timestamp))
		ih->processed_timestamp = entry.timestamp;
}

/**
 * amdgpu_irq_delegate - delegate IV to soft IH ring
 *
 * @adev: amdgpu device pointer
 * @entry: IV entry
 * @num_dw: size of IV
 *
 * Delegate the IV to the soft IH ring and schedule processing of it. Used
 * if the hardware delegation to IH1 or IH2 doesn't work for some reason.
 */
void amdgpu_irq_delegate(struct amdgpu_device *adev,
			 struct amdgpu_iv_entry *entry,
			 unsigned int num_dw)
{
	amdgpu_ih_ring_write(adev, &adev->irq.ih_soft, entry->iv_entry, num_dw);
	schedule_work(&adev->irq.ih_soft_work);
}

/**
 * amdgpu_irq_update - update hardware interrupt state
 *
 * @adev: amdgpu device pointer
 * @src: interrupt source pointer
 * @type: type of interrupt
 *
 * Updates interrupt state for the specific source (all ASICs).
 */
int amdgpu_irq_update(struct amdgpu_device *adev,
			     struct amdgpu_irq_src *src, unsigned int type)
{
	unsigned long irqflags;
	enum amdgpu_interrupt_state state;
	int r;

	spin_lock_irqsave(&adev->irq.lock, irqflags);

	/* We need to determine after taking the lock, otherwise
	 * we might disable just enabled interrupts again
	 */
	if (amdgpu_irq_enabled(adev, src, type))
		state = AMDGPU_IRQ_STATE_ENABLE;
	else
		state = AMDGPU_IRQ_STATE_DISABLE;

	r = src->funcs->set(adev, src, type, state);
	spin_unlock_irqrestore(&adev->irq.lock, irqflags);
	return r;
}

/**
 * amdgpu_irq_gpu_reset_resume_helper - update interrupt states on all sources
 *
 * @adev: amdgpu device pointer
 *
 * Updates state of all types of interrupts on all sources on resume after
 * reset.
 */
void amdgpu_irq_gpu_reset_resume_helper(struct amdgpu_device *adev)
{
	int i, j, k;

	if (amdgpu_sriov_vf(adev) || amdgpu_passthrough(adev))
		amdgpu_restore_msix(adev);

	for (i = 0; i < AMDGPU_IRQ_CLIENTID_MAX; ++i) {
		if (!adev->irq.client[i].sources)
			continue;

		for (j = 0; j < AMDGPU_MAX_IRQ_SRC_ID; ++j) {
			struct amdgpu_irq_src *src = adev->irq.client[i].sources[j];

			if (!src || !src->funcs || !src->funcs->set)
				continue;
			for (k = 0; k < src->num_types; k++)
				amdgpu_irq_update(adev, src, k);
		}
	}
}

/**
 * amdgpu_irq_get - enable interrupt
 *
 * @adev: amdgpu device pointer
 * @src: interrupt source pointer
 * @type: type of interrupt
 *
 * Enables specified type of interrupt on the specified source (all ASICs).
 *
 * Returns:
 * 0 on success or error code otherwise
 */
int amdgpu_irq_get(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned int type)
{
	if (!adev->irq.installed)
		return -ENOENT;

	if (type >= src->num_types)
		return -EINVAL;

	if (!src->enabled_types || !src->funcs->set)
		return -EINVAL;

	if (atomic_inc_return(&src->enabled_types[type]) == 1)
		return amdgpu_irq_update(adev, src, type);

	return 0;
}

/**
 * amdgpu_irq_put - disable interrupt
 *
 * @adev: amdgpu device pointer
 * @src: interrupt source pointer
 * @type: type of interrupt
 *
 * Enables specified type of interrupt on the specified source (all ASICs).
 *
 * Returns:
 * 0 on success or error code otherwise
 */
int amdgpu_irq_put(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned int type)
{
	if (!adev->irq.installed)
		return -ENOENT;

	if (type >= src->num_types)
		return -EINVAL;

	if (!src->enabled_types || !src->funcs->set)
		return -EINVAL;

	if (WARN_ON(!amdgpu_irq_enabled(adev, src, type)))
		return -EINVAL;

	if (atomic_dec_and_test(&src->enabled_types[type]))
		return amdgpu_irq_update(adev, src, type);

	return 0;
}

/**
 * amdgpu_irq_enabled - check whether interrupt is enabled or not
 *
 * @adev: amdgpu device pointer
 * @src: interrupt source pointer
 * @type: type of interrupt
 *
 * Checks whether the given type of interrupt is enabled on the given source.
 *
 * Returns:
 * *true* if interrupt is enabled, *false* if interrupt is disabled or on
 * invalid parameters
 */
bool amdgpu_irq_enabled(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
			unsigned int type)
{
	if (!adev->irq.installed)
		return false;

	if (type >= src->num_types)
		return false;

	if (!src->enabled_types || !src->funcs->set)
		return false;

	return !!atomic_read(&src->enabled_types[type]);
}

#ifdef __linux__
/* XXX: Generic IRQ handling */
static void amdgpu_irq_mask(struct irq_data *irqd)
{
	/* XXX */
}

static void amdgpu_irq_unmask(struct irq_data *irqd)
{
	/* XXX */
}

/* amdgpu hardware interrupt chip descriptor */
static struct irq_chip amdgpu_irq_chip = {
	.name = "amdgpu-ih",
	.irq_mask = amdgpu_irq_mask,
	.irq_unmask = amdgpu_irq_unmask,
};
#endif

#ifdef __linux__
/**
 * amdgpu_irqdomain_map - create mapping between virtual and hardware IRQ numbers
 *
 * @d: amdgpu IRQ domain pointer (unused)
 * @irq: virtual IRQ number
 * @hwirq: hardware irq number
 *
 * Current implementation assigns simple interrupt handler to the given virtual
 * IRQ.
 *
 * Returns:
 * 0 on success or error code otherwise
 */
static int amdgpu_irqdomain_map(struct irq_domain *d,
				unsigned int irq, irq_hw_number_t hwirq)
{
	if (hwirq >= AMDGPU_MAX_IRQ_SRC_ID)
		return -EPERM;

	irq_set_chip_and_handler(irq,
				 &amdgpu_irq_chip, handle_simple_irq);
	return 0;
}

/* Implementation of methods for amdgpu IRQ domain */
static const struct irq_domain_ops amdgpu_hw_irqdomain_ops = {
	.map = amdgpu_irqdomain_map,
};
#endif

/**
 * amdgpu_irq_add_domain - create a linear IRQ domain
 *
 * @adev: amdgpu device pointer
 *
 * Creates an IRQ domain for GPU interrupt sources
 * that may be driven by another driver (e.g., ACP).
 *
 * Returns:
 * 0 on success or error code otherwise
 */
int amdgpu_irq_add_domain(struct amdgpu_device *adev)
{
#ifdef __linux__
	adev->irq.domain = irq_domain_add_linear(NULL, AMDGPU_MAX_IRQ_SRC_ID,
						 &amdgpu_hw_irqdomain_ops, adev);
	if (!adev->irq.domain) {
		DRM_ERROR("GPU irq add domain failed\n");
		return -ENODEV;
	}
#endif

	return 0;
}

/**
 * amdgpu_irq_remove_domain - remove the IRQ domain
 *
 * @adev: amdgpu device pointer
 *
 * Removes the IRQ domain for GPU interrupt sources
 * that may be driven by another driver (e.g., ACP).
 */
void amdgpu_irq_remove_domain(struct amdgpu_device *adev)
{
	STUB();
#if 0
	if (adev->irq.domain) {
		irq_domain_remove(adev->irq.domain);
		adev->irq.domain = NULL;
	}
#endif
}

/**
 * amdgpu_irq_create_mapping - create mapping between domain Linux IRQs
 *
 * @adev: amdgpu device pointer
 * @src_id: IH source id
 *
 * Creates mapping between a domain IRQ (GPU IH src id) and a Linux IRQ
 * Use this for components that generate a GPU interrupt, but are driven
 * by a different driver (e.g., ACP).
 *
 * Returns:
 * Linux IRQ
 */
unsigned int amdgpu_irq_create_mapping(struct amdgpu_device *adev, unsigned int src_id)
{
	STUB();
	return 0;
#if 0
	adev->irq.virq[src_id] = irq_create_mapping(adev->irq.domain, src_id);

	return adev->irq.virq[src_id];
#endif
}
