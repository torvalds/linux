/* $FreeBSD$ */

#include <machine/asm.h>

#define hc .long 0x44000022

ASENTRY(lv1_allocate_memory)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r7,48(%r1)
	std	%r8,56(%r1)
	li	%r11,0
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_write_htab_entry)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,1
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_construct_virtual_address_space)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r6,48(%r1)
	std	%r7,56(%r1)
	li	%r11,2
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_virtual_address_space_id_of_ppe)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r4,48(%r1)
	li	%r11,4
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_query_logical_partition_address_region_info)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-88(%r1)
	std	%r4,48(%r1)
	std	%r5,56(%r1)
	std	%r6,64(%r1)
	std	%r7,72(%r1)
	std	%r8,80(%r1)
	li	%r11,6
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r11,72(%r1)
	std	%r7,0(%r11)
	ld	%r11,80(%r1)
	std	%r8,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_select_virtual_address_space)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,7
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_pause)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,9
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_destruct_virtual_address_space)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,10
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_configure_irq_state_bitmap)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,11
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_connect_irq_plug_ext)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,12
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_release_memory)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,13
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_put_iopte)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,15
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_disconnect_irq_plug_ext)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,17
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_construct_event_receive_port)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r3,48(%r1)
	li	%r11,18
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_destruct_event_receive_port)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,19
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_send_event_locally)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,24
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_end_of_interrupt)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,27
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_connect_irq_plug)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,28
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_disconnect_irq_plus)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,29
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_end_of_interrupt_ext)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,30
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_did_update_interrupt_mask)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,31
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_shutdown_logical_partition)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,44
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_destruct_logical_spe)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,54
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_construct_logical_spe)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-96(%r1)
	std	%r10,48(%r1)
	ld	%r11,208(%r1)
	std	%r11,56(%r1)
	ld	%r11,216(%r1)
	std	%r11,64(%r1)
	ld	%r11,224(%r1)
	std	%r11,72(%r1)
	ld	%r11,232(%r1)
	std	%r11,80(%r1)
	ld	%r11,240(%r1)
	std	%r11,88(%r1)
	li	%r11,57
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r11,72(%r1)
	std	%r7,0(%r11)
	ld	%r11,80(%r1)
	std	%r8,0(%r11)
	ld	%r11,88(%r1)
	std	%r9,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_set_spe_interrupt_mask)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,61
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_disable_logical_spe)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,65
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_clear_spe_interrupt_status)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,66
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_spe_interrupt_status)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r5,48(%r1)
	li	%r11,67
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_logical_ppe_id)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r3,48(%r1)
	li	%r11,69
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_logical_partition_id)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r3,48(%r1)
	li	%r11,74
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_spe_irq_outlet)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r5,48(%r1)
	li	%r11,78
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_set_spe_privilege_state_area_1_register)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,79
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_repository_node_value)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r8,48(%r1)
	std	%r9,56(%r1)
	li	%r11,91
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_read_htab_entries)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-88(%r1)
	std	%r5,48(%r1)
	std	%r6,56(%r1)
	std	%r7,64(%r1)
	std	%r8,72(%r1)
	std	%r9,80(%r1)
	li	%r11,95
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r11,72(%r1)
	std	%r7,0(%r11)
	ld	%r11,80(%r1)
	std	%r8,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_set_dabr)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,96
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_allocate_io_segment)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r6,48(%r1)
	li	%r11,116
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_release_io_segment)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,117
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_construct_io_irq_outlet)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r4,48(%r1)
	li	%r11,120
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_destruct_io_irq_outlet)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,121
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_map_htab)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r4,48(%r1)
	li	%r11,122
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_unmap_htab)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,123
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_version_info)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r3,48(%r1)
	li	%r11,127
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_insert_htab_entry)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-72(%r1)
	std	%r9,48(%r1)
	std	%r10,56(%r1)
	ld	%r11,184(%r1)
	std	%r11,64(%r1)
	li	%r11,158
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_read_virtual_uart)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r6,48(%r1)
	li	%r11,162
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_write_virtual_uart)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r6,48(%r1)
	li	%r11,163
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_set_virtual_uart_param)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,164
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_virtual_uart_param)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r5,48(%r1)
	li	%r11,165
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_configure_virtual_uart)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r4,48(%r1)
	li	%r11,166
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_open_device)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,170
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_close_device)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,171
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_map_device_mmio_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r8,48(%r1)
	li	%r11,172
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_unmap_device_mmio_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,173
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_allocate_device_dma_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r8,48(%r1)
	li	%r11,174
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_free_device_dma_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,175
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_map_device_dma_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,176
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_unmap_device_dma_region)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,177
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_read_pci_config)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r9,48(%r1)
	li	%r11,178
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_write_pci_config)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,179
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_add_multicast_address)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,185
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_remove_multicast_address)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,186
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_start_tx_dma)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,187
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_stop_tx_dma)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,188
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_start_rx_dma)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,189
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_stop_rx_dma)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,190
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_set_interrupt_status_indicator)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,191
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_set_interrupt_mask)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,193
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_net_control)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r9,48(%r1)
	std	%r10,56(%r1)
	li	%r11,194
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_connect_interrupt_event_receive_port)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,197
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_disconnect_interrupt_event_receive_port)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,198
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_deconfigure_virtual_uart_irq)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,202
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_enable_logical_spe)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,207
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_open)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,210
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_close)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,211
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_device_map)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r4,48(%r1)
	std	%r5,56(%r1)
	li	%r11,212
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_device_unmap)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,213
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_memory_allocate)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r8,48(%r1)
	std	%r9,56(%r1)
	li	%r11,214
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_memory_free)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,216
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_context_allocate)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-88(%r1)
	std	%r5,48(%r1)
	std	%r6,56(%r1)
	std	%r7,64(%r1)
	std	%r8,72(%r1)
	std	%r9,80(%r1)
	li	%r11,217
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r11,64(%r1)
	std	%r6,0(%r11)
	ld	%r11,72(%r1)
	std	%r7,0(%r11)
	ld	%r11,80(%r1)
	std	%r8,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_context_free)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,218
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_context_iomap)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,221
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_context_attribute)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,225
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_context_intr)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r4,48(%r1)
	li	%r11,227
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_gpu_attribute)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,228
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_get_rtc)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r3,48(%r1)
	std	%r4,56(%r1)
	li	%r11,232
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_storage_read)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r9,48(%r1)
	li	%r11,245
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_storage_write)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r9,48(%r1)
	li	%r11,246
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_storage_send_device_command)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r9,48(%r1)
	li	%r11,248
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_storage_get_async_status)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-64(%r1)
	std	%r4,48(%r1)
	std	%r5,56(%r1)
	li	%r11,249
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r11,56(%r1)
	std	%r5,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_storage_check_async_status)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-56(%r1)
	std	%r5,48(%r1)
	li	%r11,254
	hc
	extsw	%r3,%r3
	ld	%r11,48(%r1)
	std	%r4,0(%r11)
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

ASENTRY(lv1_panic)
	mflr	%r0
	std	%r0,16(%r1)
	stdu	%r1,-48(%r1)
	li	%r11,255
	hc
	extsw	%r3,%r3
	ld	%r1,0(%r1)
	ld	%r0,16(%r1)
	mtlr	%r0
	blr

