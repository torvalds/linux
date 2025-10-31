/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common helpers for drivers (e.g. low-level PCI/TSM drivers) implementing the
 * IDE key management protocol (IDE_KM) as defined by:
 * PCIe r7.0 section 6.33 Integrity & Data Encryption (IDE)
 *
 * Copyright(c) 2024-2025 Intel Corporation. All rights reserved.
 */

#ifndef __PCI_IDE_H__
#define __PCI_IDE_H__

enum pci_ide_partner_select {
	PCI_IDE_EP,
	PCI_IDE_RP,
	PCI_IDE_PARTNER_MAX,
	/*
	 * In addition to the resources in each partner port the
	 * platform / host-bridge additionally has a Stream ID pool that
	 * it shares across root ports. Let pci_ide_stream_alloc() use
	 * the alloc_stream_index() helper as endpoints and root ports.
	 */
	PCI_IDE_HB = PCI_IDE_PARTNER_MAX,
};

/**
 * struct pci_ide_partner - Per port pair Selective IDE Stream settings
 * @rid_start: Partner Port Requester ID range start
 * @rid_end: Partner Port Requester ID range end
 * @stream_index: Selective IDE Stream Register Block selection
 * @default_stream: Endpoint uses this stream for all upstream TLPs regardless of
 *		    address and RID association registers
 * @setup: flag to track whether to run pci_ide_stream_teardown() for this
 *	   partner slot
 * @enable: flag whether to run pci_ide_stream_disable() for this partner slot
 */
struct pci_ide_partner {
	u16 rid_start;
	u16 rid_end;
	u8 stream_index;
	unsigned int default_stream:1;
	unsigned int setup:1;
	unsigned int enable:1;
};

/**
 * struct pci_ide - PCIe Selective IDE Stream descriptor
 * @pdev: PCIe Endpoint in the pci_ide_partner pair
 * @partner: per-partner settings
 * @host_bridge_stream: allocated from host bridge @ide_stream_ida pool
 * @stream_id: unique Stream ID (within Partner Port pairing)
 * @name: name of the established Selective IDE Stream in sysfs
 * @tsm_dev: For TSM established IDE, the TSM device context
 *
 * Negative @stream_id values indicate "uninitialized" on the
 * expectation that with TSM established IDE the TSM owns the stream_id
 * allocation.
 */
struct pci_ide {
	struct pci_dev *pdev;
	struct pci_ide_partner partner[PCI_IDE_PARTNER_MAX];
	u8 host_bridge_stream;
	int stream_id;
	const char *name;
	struct tsm_dev *tsm_dev;
};

void pci_ide_set_nr_streams(struct pci_host_bridge *hb, u16 nr);
struct pci_ide_partner *pci_ide_to_settings(struct pci_dev *pdev,
					    struct pci_ide *ide);
struct pci_ide *pci_ide_stream_alloc(struct pci_dev *pdev);
void pci_ide_stream_free(struct pci_ide *ide);
int  pci_ide_stream_register(struct pci_ide *ide);
void pci_ide_stream_unregister(struct pci_ide *ide);
void pci_ide_stream_setup(struct pci_dev *pdev, struct pci_ide *ide);
void pci_ide_stream_teardown(struct pci_dev *pdev, struct pci_ide *ide);
int pci_ide_stream_enable(struct pci_dev *pdev, struct pci_ide *ide);
void pci_ide_stream_disable(struct pci_dev *pdev, struct pci_ide *ide);
void pci_ide_stream_release(struct pci_ide *ide);
DEFINE_FREE(pci_ide_stream_release, struct pci_ide *, if (_T) pci_ide_stream_release(_T))
#endif /* __PCI_IDE_H__ */
