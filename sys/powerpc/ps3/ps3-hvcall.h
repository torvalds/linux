/*
 * Playstation 3 LV1 hypercall interface
 *
 * $FreeBSD$
 */

#include <sys/types.h>

enum lpar_id {
	PS3_LPAR_ID_CURRENT	= 0x00,
	PS3_LPAR_ID_PME		= 0x01,
};

/* Return codes from hypercalls */
#define LV1_SUCCESS			0
#define LV1_RESOURCE_SHORTAGE		-2
#define LV1_NO_PRIVILEGE		-3
#define LV1_DENIED_BY_POLICY		-4
#define LV1_ACCESS_VIOLATION		-5
#define LV1_NO_ENTRY			-6
#define LV1_DUPLICATE_ENTRY		-7
#define LV1_TYPE_MISMATCH		-8
#define LV1_BUSY			-9
#define LV1_EMPTY			-10
#define LV1_WRONG_STATE			-11
#define LV1_NO_MATCH			-13
#define LV1_ALREADY_CONNECTED		-14
#define LV1_UNSUPPORTED_PARAMETER_VALUE	-15
#define LV1_CONDITION_NOT_SATISFIED	-16
#define LV1_ILLEGAL_PARAMETER_VALUE	-17
#define LV1_BAD_OPTION			-18
#define LV1_IMPLEMENTATION_LIMITATION	-19
#define LV1_NOT_IMPLEMENTED		-20
#define LV1_INVALID_CLASS_ID		-21
#define LV1_CONSTRAINT_NOT_SATISFIED	-22
#define LV1_ALIGNMENT_ERROR		-23
#define LV1_HARDWARE_ERROR		-24
#define LV1_INVALID_DATA_FORMAT		-25
#define LV1_INVALID_OPERATION		-26
#define LV1_INTERNAL_ERROR		-32768

static inline uint64_t
lv1_repository_string(const char *str)
{
	uint64_t ret = 0;
	strncpy((char *)&ret, str, sizeof(ret));
	return (ret);
}

int lv1_allocate_memory(uint64_t size, uint64_t log_page_size, uint64_t zero, uint64_t flags, uint64_t *base_addr, uint64_t *muid);
int lv1_write_htab_entry(uint64_t vas_id, uint64_t slot, uint64_t pte_hi, uint64_t pte_lo);
int lv1_construct_virtual_address_space(uint64_t log_pteg_count, uint64_t n_sizes, uint64_t page_sizes, uint64_t *vas_id, uint64_t *hv_pteg_count);
int lv1_get_virtual_address_space_id_of_ppe(uint64_t ppe_id, uint64_t *vas_id);
int lv1_query_logical_partition_address_region_info(uint64_t lpar_id, uint64_t *base_addr, uint64_t *size, uint64_t *access_right, uint64_t *max_page_size, uint64_t *flags);
int lv1_select_virtual_address_space(uint64_t vas_id);
int lv1_pause(uint64_t mode);
int lv1_destruct_virtual_address_space(uint64_t vas_id);
int lv1_configure_irq_state_bitmap(uint64_t ppe_id, uint64_t cpu_id, uint64_t bitmap_addr);
int lv1_connect_irq_plug_ext(uint64_t ppe_id, uint64_t cpu_id, uint64_t virq, uint64_t outlet, uint64_t zero);
int lv1_release_memory(uint64_t base_addr);
int lv1_put_iopte(uint64_t ioas_id, uint64_t ioif_addr, uint64_t lpar_addr, uint64_t io_id, uint64_t flags);
int lv1_disconnect_irq_plug_ext(uint64_t ppe_id, uint64_t cpu_id, uint64_t virq);
int lv1_construct_event_receive_port(uint64_t *outlet);
int lv1_destruct_event_receive_port(uint64_t outlet);
int lv1_send_event_locally(uint64_t outlet);
int lv1_end_of_interrupt(uint64_t irq);
int lv1_connect_irq_plug(uint64_t virq, uint64_t irq);
int lv1_disconnect_irq_plus(uint64_t virq);
int lv1_end_of_interrupt_ext(uint64_t ppe_id, uint64_t cpu_id, uint64_t virq);
int lv1_did_update_interrupt_mask(uint64_t ppe_id, uint64_t cpu_id);
int lv1_shutdown_logical_partition(uint64_t cmd);
int lv1_destruct_logical_spe(uint64_t spe_id);
int lv1_construct_logical_spe(uint64_t pshift1, uint64_t pshift2, uint64_t pshift3, uint64_t pshift4, uint64_t pshift5, uint64_t vas_id, uint64_t spe_type, uint64_t *priv2_addr, uint64_t *problem_phys, uint64_t *local_store_phys, uint64_t *unused, uint64_t *shadow_addr, uint64_t *spe_id);
int lv1_set_spe_interrupt_mask(uint64_t spe_id, uint64_t class, uint64_t mask);
int lv1_disable_logical_spe(uint64_t spe_id, uint64_t zero);
int lv1_clear_spe_interrupt_status(uint64_t spe_id, uint64_t class, uint64_t stat, uint64_t zero);
int lv1_get_spe_interrupt_status(uint64_t spe_id, uint64_t class, uint64_t *stat);
int lv1_get_logical_ppe_id(uint64_t *ppe_id);
int lv1_get_logical_partition_id(uint64_t *lpar_id);
int lv1_get_spe_irq_outlet(uint64_t spe_id, uint64_t class, uint64_t *outlet);
int lv1_set_spe_privilege_state_area_1_register(uint64_t spe_id, uint64_t offset, uint64_t value);
int lv1_get_repository_node_value(uint64_t lpar_id, uint64_t n1, uint64_t n2, uint64_t n3, uint64_t n4, uint64_t *v1, uint64_t *v2);
int lv1_read_htab_entries(uint64_t vas_id, uint64_t slot, uint64_t *hi1, uint64_t *hi2, uint64_t *hi3, uint64_t *hi4, uint64_t *rcbits);
int lv1_set_dabr(uint64_t dabr, uint64_t flags);
int lv1_allocate_io_segment(uint64_t ioas_id, uint64_t seg_size, uint64_t io_pagesize, uint64_t *ioif_addr);
int lv1_release_io_segment(uint64_t ioas_id, uint64_t ioif_addr);
int lv1_construct_io_irq_outlet(uint64_t interrupt_id, uint64_t *outlet);
int lv1_destruct_io_irq_outlet(uint64_t outlet);
int lv1_map_htab(uint64_t lpar_id, uint64_t *htab_addr);
int lv1_unmap_htab(uint64_t htab_addr);
int lv1_get_version_info(uint64_t *firm_vers);
int lv1_insert_htab_entry(uint64_t vas_id, uint64_t pteg, uint64_t pte_hi, uint64_t pte_lo, uint64_t lockflags, uint64_t flags, uint64_t *index, uint64_t *evicted_hi, uint64_t *evicted_lo);
int lv1_read_virtual_uart(uint64_t port, uint64_t buffer, uint64_t bytes, uint64_t *bytes_read);
int lv1_write_virtual_uart(uint64_t port, uint64_t buffer, uint64_t bytes, uint64_t *bytes_written);
int lv1_set_virtual_uart_param(uint64_t port, uint64_t param, uint64_t value);
int lv1_get_virtual_uart_param(uint64_t port, uint64_t param, uint64_t *value);
int lv1_configure_virtual_uart(uint64_t lpar_addr, uint64_t *outlet);
int lv1_open_device(uint64_t bus, uint64_t dev, uint64_t zero);
int lv1_close_device(uint64_t bus, uint64_t dev);
int lv1_map_device_mmio_region(uint64_t bus, uint64_t dev, uint64_t bus_addr, uint64_t size, uint64_t page_size, uint64_t *lpar_addr);
int lv1_unmap_device_mmio_region(uint64_t bus, uint64_t dev, uint64_t lpar_addr);
int lv1_allocate_device_dma_region(uint64_t bus, uint64_t dev, uint64_t io_size, uint64_t io_pagesize, uint64_t flag, uint64_t *dma_region);
int lv1_free_device_dma_region(uint64_t bus, uint64_t dev, uint64_t dma_region);
int lv1_map_device_dma_region(uint64_t bus, uint64_t dev, uint64_t lpar_addr, uint64_t dma_region, uint64_t size, uint64_t flags);
int lv1_unmap_device_dma_region(uint64_t bus, uint64_t dev, uint64_t dma_region, uint64_t size);
int lv1_read_pci_config(uint64_t ps3bus, uint64_t bus, uint64_t dev, uint64_t func, uint64_t offset, uint64_t size, uint64_t *result);
int lv1_write_pci_config(uint64_t ps3bus, uint64_t bus, uint64_t dev, uint64_t func, uint64_t offset, uint64_t size, uint64_t data);
int lv1_net_add_multicast_address(uint64_t bus, uint64_t dev, uint64_t addr, uint64_t flags);
int lv1_net_remove_multicast_address(uint64_t bus, uint64_t dev, uint64_t zero, uint64_t one);
int lv1_net_start_tx_dma(uint64_t bus, uint64_t dev, uint64_t bus_addr, uint64_t zero);
int lv1_net_stop_tx_dma(uint64_t bus, uint64_t dev, uint64_t zero);
int lv1_net_start_rx_dma(uint64_t bus, uint64_t dev, uint64_t bus_addr, uint64_t zero);
int lv1_net_stop_rx_dma(uint64_t bus, uint64_t dev, uint64_t zero);
int lv1_net_set_interrupt_status_indicator(uint64_t bus, uint64_t dev, uint64_t irq_status_addr, uint64_t zero);
int lv1_net_set_interrupt_mask(uint64_t bus, uint64_t dev, uint64_t mask, uint64_t zero);
int lv1_net_control(uint64_t bus, uint64_t dev, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t *v1, uint64_t *v2);
int lv1_connect_interrupt_event_receive_port(uint64_t bus, uint64_t dev, uint64_t outlet, uint64_t irq);
int lv1_disconnect_interrupt_event_receive_port(uint64_t bus, uint64_t dev, uint64_t outlet, uint64_t irq);
int lv1_deconfigure_virtual_uart_irq(void);
int lv1_enable_logical_spe(uint64_t spe_id, uint64_t resource_id);
int lv1_gpu_open(uint64_t zero);
int lv1_gpu_close(void);
int lv1_gpu_device_map(uint64_t dev, uint64_t *lpar_addr, uint64_t *lpar_size);
int lv1_gpu_device_unmap(uint64_t dev);
int lv1_gpu_memory_allocate(uint64_t ddr_size, uint64_t zero1, uint64_t zero2, uint64_t zero3, uint64_t zero4, uint64_t *handle, uint64_t *ddr_lpar);
int lv1_gpu_memory_free(uint64_t handle);
int lv1_gpu_context_allocate(uint64_t handle, uint64_t flags, uint64_t *chandle, uint64_t *lpar_dma_control, uint64_t *lpar_driver_info, uint64_t *lpar_reports, uint64_t *lpar_reports_size);
int lv1_gpu_context_free(uint64_t chandle);
int lv1_gpu_context_iomap(uint64_t changle, uint64_t gpu_ioif, uint64_t xdr_lpar, uint64_t fbsize, uint64_t ioflags);
int lv1_gpu_context_attribute(uint64_t chandle, uint64_t op, uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4);
int lv1_gpu_context_intr(uint64_t chandle, uint64_t *v1);
int lv1_gpu_attribute(uint64_t p1, uint64_t p2, uint64_t p3, uint64_t p4, uint64_t p5);
int lv1_get_rtc(uint64_t *rtc_val, uint64_t *timebase);
int lv1_storage_read(uint64_t dev, uint64_t region, uint64_t sector, uint64_t nsectors, uint64_t flags, uint64_t buf, uint64_t *dma_tag);
int lv1_storage_write(uint64_t dev, uint64_t region, uint64_t sector, uint64_t nsectors, uint64_t flags, uint64_t buf, uint64_t *dma_tag);
int lv1_storage_send_device_command(uint64_t dev, uint64_t cmd_id, uint64_t cmd_block, uint64_t cmd_size, uint64_t data_buf, uint64_t blocks, uint64_t *dma_tag);
int lv1_storage_get_async_status(uint64_t dev, uint64_t *dma_tag, uint64_t *status);
int lv1_storage_check_async_status(uint64_t dev, uint64_t dma_tag, uint64_t *status);
int lv1_panic(uint64_t howto);
