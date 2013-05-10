#ifdef CONFIG_PROVIDE_OHCI1394_DMA_INIT
extern int __initdata init_ohci1394_dma_early;
extern void __init init_ohci1394_dma_on_all_controllers(void);
#endif
