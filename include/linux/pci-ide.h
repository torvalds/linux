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
 * @rid_end: Partner Port Requester ID range end (inclusive)
 * @stream_index: Selective IDE Stream Register Block selection
 * @mem_assoc: PCI bus memory address association for targeting peer partner
 * @pref_assoc: PCI bus prefetchable memory address association for
 *		targeting peer partner
 * @default_stream: Endpoint uses this stream for all upstream TLPs regardless of
 *		    address and RID association registers
 * @setup: flag to track whether to run pci_ide_stream_teardown() for this
 *	   partner slot
 * @enable: flag whether to run pci_ide_stream_disable() for this partner slot
 *
 * By default, pci_ide_stream_alloc() initializes @mem_assoc and @pref_assoc
 * with the immediate ancestor downstream port memory ranges (i.e. Type 1
 * Configuration Space Header values). Caller may zero size ({0, -1}) the range
 * to drop it from consideration at pci_ide_stream_setup() time.
 */
struct pci_ide_partner {
	u16 rid_start;
	u16 rid_end;
	u8 stream_index;
	struct pci_bus_region mem_assoc;
	struct pci_bus_region pref_assoc;
	unsigned int default_stream:1;
	unsigned int setup:1;
	unsigned int enable:1;
};

/**
 * struct pci_ide_regs - Hardware register association settings for Selective
 *			 IDE Streams
 * @rid1: IDE RID Association Register 1
 * @rid2: IDE RID Association Register 2
 * @addr: Up to two address association blocks (IDE Address Association Register
 *	  1 through 3) for MMIO and prefetchable MMIO
 * @nr_addr: Number of address association blocks initialized
 *
 * See pci_ide_stream_to_regs()
 */
struct pci_ide_regs {
	u32 rid1;
	u32 rid2;
	struct {
		u32 assoc1;
		u32 assoc2;
		u32 assoc3;
	} addr[2];
	int nr_addr;
};

/**
 * struct pci_ide - PCIe Selective IDE Stream descriptor
 * @pdev: PCIe Endpoint in the pci_ide_partner pair
 * @partner: per-partner settings
 * @host_bridge_stream: allocated from host bridge @ide_stream_ida pool
 * @stream_id: unique Stream ID (within Partner Port pairing)
 * @name: name of the established Selective IDE Stream in sysfs
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
};

/*
 * Some devices need help with aliased stream-ids even for idle streams. Use
 * this id as the "never enabled" place holder.
 */
#define PCI_IDE_RESERVED_STREAM_ID 255

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
