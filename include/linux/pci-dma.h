#ifndef _LINUX_PCI_DMA_H
#define _LINUX_PCI_DMA_H

#ifdef CONFIG_NEED_DMA_MAP_STATE
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)        dma_addr_t ADDR_NAME;
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)          __u32 LEN_NAME;
#define pci_unmap_addr(PTR, ADDR_NAME)           ((PTR)->ADDR_NAME)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)  (((PTR)->ADDR_NAME) = (VAL))
#define pci_unmap_len(PTR, LEN_NAME)             ((PTR)->LEN_NAME)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)    (((PTR)->LEN_NAME) = (VAL))
#else
#define DECLARE_PCI_UNMAP_ADDR(ADDR_NAME)
#define DECLARE_PCI_UNMAP_LEN(LEN_NAME)
#define pci_unmap_addr(PTR, ADDR_NAME)           (0)
#define pci_unmap_addr_set(PTR, ADDR_NAME, VAL)  do { } while (0)
#define pci_unmap_len(PTR, LEN_NAME)             (0)
#define pci_unmap_len_set(PTR, LEN_NAME, VAL)    do { } while (0)
#endif

#endif
