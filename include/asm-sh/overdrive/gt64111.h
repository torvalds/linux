#ifndef _GT64111_H_
#define _GT64111_H_

#define MASTER_INTERFACE         0x0
#define RAS10_LO_DEC_ADR         0x8        
#define RAS10_HI_DEC_ADR         0x10
#define RAS32_LO_DEC_ADR         0x18
#define RAS32_HI_DEC_ADR         0x20
#define CS20_LO_DEC_ADR          0x28
#define CS20_HI_DEC_ADR          0x30
#define CS3_LO_DEC_ADR           0x38
#define CS3_HI_DEC_ADR           0x40
#define PCI_IO_LO_DEC_ADR        0x48
#define PCI_IO_HI_DEC_ADR        0x50
#define PCI_MEM0_LO_DEC_ADR      0x58
#define PCI_MEM0_HI_DEC_ADR      0x60
#define INTERNAL_SPACE_DEC       0x68
#define BUS_ERR_ADR_LO_CPU       0x70
#define READONLY0                0x78
#define PCI_MEM1_LO_DEC_ADR      0x80
#define PCI_MEM1_HI_DEC_ADR      0x88
#define RAS0_LO_DEC_ADR          0x400   
#define RAS0_HI_DEC_ADR          0x404
#define RAS1_LO_DEC_ADR          0x408
#define RAS1_HI_DEC_ADR          0x40c
#define RAS2_LO_DEC_ADR          0x410
#define RAS2_HI_DEC_ADR          0x414
#define RAS3_LO_DEC_ADR          0x418
#define RAS3_HI_DEC_ADR          0x41c
#define DEV_CS0_LO_DEC_ADR       0x420
#define DEV_CS0_HI_DEC_ADR       0x424
#define DEV_CS1_LO_DEC_ADR       0x428
#define DEV_CS1_HI_DEC_ADR       0x42c
#define DEV_CS2_LO_DEC_ADR       0x430
#define DEV_CS2_HI_DEC_ADR       0x434
#define DEV_CS3_LO_DEC_ADR       0x438
#define DEV_CS3_HI_DEC_ADR       0x43c
#define DEV_BOOTCS_LO_DEC_ADR    0x440
#define DEV_BOOTCS_HI_DEC_ADR    0x444
#define DEV_ADR_DEC_ERR          0x470
#define DRAM_CFG                 0x448   
#define DRAM_BANK0_PARMS         0x44c   
#define DRAM_BANK1_PARMS         0x450
#define DRAM_BANK2_PARMS         0x454
#define DRAM_BANK3_PARMS         0x458
#define DEV_BANK0_PARMS          0x45c
#define DEV_BANK1_PARMS          0x460
#define DEV_BANK2_PARMS          0x464
#define DEV_BANK3_PARMS          0x468
#define DEV_BOOT_BANK_PARMS      0x46c
#define CH0_DMA_BYTECOUNT        0x800
#define CH1_DMA_BYTECOUNT        0x804
#define CH2_DMA_BYTECOUNT        0x808
#define CH3_DMA_BYTECOUNT        0x80c
#define CH0_DMA_SRC_ADR          0x810
#define CH1_DMA_SRC_ADR          0x814
#define CH2_DMA_SRC_ADR          0x818
#define CH3_DMA_SRC_ADR          0x81c
#define CH0_DMA_DST_ADR          0x820
#define CH1_DMA_DST_ADR          0x824
#define CH2_DMA_DST_ADR          0x828
#define CH3_DMA_DST_ADR          0x82c
#define CH0_NEXT_REC_PTR         0x830
#define CH1_NEXT_REC_PTR         0x834
#define CH2_NEXT_REC_PTR         0x838
#define CH3_NEXT_REC_PTR         0x83c
#define CH0_CTRL                 0x840
#define CH1_CTRL                 0x844
#define CH2_CTRL                 0x848
#define CH3_CTRL                 0x84c
#define DMA_ARBITER              0x860
#define TIMER0                   0x850
#define TIMER1                   0x854
#define TIMER2                   0x858
#define TIMER3                   0x85c
#define TIMER_CTRL               0x864
#define PCI_CMD                  0xc00
#define PCI_TIMEOUT              0xc04
#define PCI_RAS10_BANK_SIZE      0xc08
#define PCI_RAS32_BANK_SIZE      0xc0c
#define PCI_CS20_BANK_SIZE       0xc10
#define PCI_CS3_BANK_SIZE        0xc14
#define PCI_SERRMASK             0xc28
#define PCI_INTACK               0xc34
#define PCI_BAR_EN               0xc3c
#define PCI_CFG_ADR              0xcf8
#define PCI_CFG_DATA             0xcfc
#define PCI_INTCAUSE             0xc18
#define PCI_MAST_MASK            0xc1c
#define PCI_PCIMASK              0xc24
#define BAR_ENABLE_ADR           0xc3c

/* These are config registers, accessible via PCI space */
#define PCI_CONFIG_RAS10_BASE_ADR   0x010
#define PCI_CONFIG_RAS32_BASE_ADR   0x014
#define PCI_CONFIG_CS20_BASE_ADR    0x018
#define PCI_CONFIG_CS3_BASE_ADR     0x01c
#define PCI_CONFIG_INT_REG_MM_ADR   0x020
#define PCI_CONFIG_INT_REG_IO_ADR   0x024
#define PCI_CONFIG_BOARD_VENDOR     0x02c
#define PCI_CONFIG_ROM_ADR          0x030
#define PCI_CONFIG_INT_PIN_LINE     0x03c





#endif

