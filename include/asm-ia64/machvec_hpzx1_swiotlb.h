#ifndef _ASM_IA64_MACHVEC_HPZX1_SWIOTLB_h
#define _ASM_IA64_MACHVEC_HPZX1_SWIOTLB_h

extern ia64_mv_setup_t				dig_setup;
extern ia64_mv_dma_alloc_coherent		hwsw_alloc_coherent;
extern ia64_mv_dma_free_coherent		hwsw_free_coherent;
extern ia64_mv_dma_map_single			hwsw_map_single;
extern ia64_mv_dma_unmap_single			hwsw_unmap_single;
extern ia64_mv_dma_map_sg			hwsw_map_sg;
extern ia64_mv_dma_unmap_sg			hwsw_unmap_sg;
extern ia64_mv_dma_supported			hwsw_dma_supported;
extern ia64_mv_dma_mapping_error		hwsw_dma_mapping_error;
extern ia64_mv_dma_sync_single_for_cpu		hwsw_sync_single_for_cpu;
extern ia64_mv_dma_sync_sg_for_cpu		hwsw_sync_sg_for_cpu;
extern ia64_mv_dma_sync_single_for_device	hwsw_sync_single_for_device;
extern ia64_mv_dma_sync_sg_for_device		hwsw_sync_sg_for_device;

/*
 * This stuff has dual use!
 *
 * For a generic kernel, the macros are used to initialize the
 * platform's machvec structure.  When compiling a non-generic kernel,
 * the macros are used directly.
 */
#define platform_name				"hpzx1_swiotlb"

#define platform_setup				dig_setup
#define platform_dma_init			machvec_noop
#define platform_dma_alloc_coherent		hwsw_alloc_coherent
#define platform_dma_free_coherent		hwsw_free_coherent
#define platform_dma_map_single			hwsw_map_single
#define platform_dma_unmap_single		hwsw_unmap_single
#define platform_dma_map_sg			hwsw_map_sg
#define platform_dma_unmap_sg			hwsw_unmap_sg
#define platform_dma_supported			hwsw_dma_supported
#define platform_dma_mapping_error		hwsw_dma_mapping_error
#define platform_dma_sync_single_for_cpu	hwsw_sync_single_for_cpu
#define platform_dma_sync_sg_for_cpu		hwsw_sync_sg_for_cpu
#define platform_dma_sync_single_for_device	hwsw_sync_single_for_device
#define platform_dma_sync_sg_for_device		hwsw_sync_sg_for_device

#endif /* _ASM_IA64_MACHVEC_HPZX1_SWIOTLB_h */
