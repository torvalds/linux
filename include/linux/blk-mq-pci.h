#ifndef _LINUX_BLK_MQ_PCI_H
#define _LINUX_BLK_MQ_PCI_H

struct blk_mq_tag_set;
struct pci_dev;

int blk_mq_pci_map_queues(struct blk_mq_tag_set *set, struct pci_dev *pdev);

#endif /* _LINUX_BLK_MQ_PCI_H */
