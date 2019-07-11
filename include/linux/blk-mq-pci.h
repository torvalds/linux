/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BLK_MQ_PCI_H
#define _LINUX_BLK_MQ_PCI_H

struct blk_mq_queue_map;
struct pci_dev;

int blk_mq_pci_map_queues(struct blk_mq_queue_map *qmap, struct pci_dev *pdev,
			  int offset);

#endif /* _LINUX_BLK_MQ_PCI_H */
