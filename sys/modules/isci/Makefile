# isci driver Makefile
#
# $FreeBSD$

ISCI_SRC_PATH = ${SRCTOP}/sys

.PATH:  ${ISCI_SRC_PATH}/dev/isci ${ISCI_SRC_PATH}/dev/isci/scil

KMOD    = isci
SRCS    = isci.c isci_oem_parameters.c \
		isci_controller.c isci_domain.c isci_io_request.c \
		isci_timer.c isci_remote_device.c isci_logger.c \
		isci_task_request.c isci_sysctl.c isci_interrupt.c

SRCS += \
		sci_base_controller.c \
		sci_base_domain.c \
		sci_base_iterator.c \
		sci_base_library.c \
		sci_base_logger.c \
		sci_base_memory_descriptor_list.c \
		sci_base_memory_descriptor_list_decorator.c \
		sci_base_object.c \
		sci_base_observer.c \
		sci_base_phy.c \
		sci_base_port.c \
		sci_base_remote_device.c \
	 	sci_base_request.c \
		sci_base_state_machine.c \
		sci_base_state_machine_logger.c \
		sci_base_state_machine_observer.c \
		sci_base_subject.c

SRCS += \
		sci_abstract_list.c \
		sci_util.c

SRCS += \
		scic_sds_controller.c \
		scic_sds_library.c scic_sds_pci.c \
		scic_sds_phy.c scic_sds_port.c \
		scic_sds_port_configuration_agent.c \
		scic_sds_remote_device.c scic_sds_remote_node_context.c \
		scic_sds_remote_node_table.c scic_sds_request.c \
		scic_sds_sgpio.c scic_sds_smp_remote_device.c \
		scic_sds_smp_request.c scic_sds_ssp_request.c \
		scic_sds_stp_packet_request.c scic_sds_stp_remote_device.c \
		scic_sds_stp_request.c scic_sds_unsolicited_frame_control.c

SRCS += \
		scif_sas_controller.c \
		scif_sas_controller_state_handlers.c \
		scif_sas_controller_states.c scif_sas_domain.c \
		scif_sas_domain_state_handlers.c scif_sas_domain_states.c \
		scif_sas_high_priority_request_queue.c \
		scif_sas_internal_io_request.c scif_sas_io_request.c \
		scif_sas_io_request_state_handlers.c \
		scif_sas_io_request_states.c scif_sas_library.c \
		scif_sas_remote_device.c \
		scif_sas_remote_device_ready_substate_handlers.c \
		scif_sas_remote_device_ready_substates.c \
		scif_sas_remote_device_starting_substate_handlers.c \
		scif_sas_remote_device_starting_substates.c \
		scif_sas_remote_device_state_handlers.c \
		scif_sas_remote_device_states.c scif_sas_request.c \
		scif_sas_smp_activity_clear_affiliation.c \
		scif_sas_smp_io_request.c scif_sas_smp_phy.c \
		scif_sas_smp_remote_device.c scif_sas_stp_io_request.c \
		scif_sas_stp_remote_device.c scif_sas_stp_task_request.c \
		scif_sas_task_request.c scif_sas_task_request_state_handlers.c \
		scif_sas_task_request_states.c scif_sas_timer.c

SRCS += \
		sati.c \
		sati_abort_task_set.c sati_atapi.c \
		sati_device.c sati_inquiry.c sati_log_sense.c \
		sati_lun_reset.c sati_mode_pages.c sati_mode_select.c \
		sati_mode_sense.c sati_mode_sense_6.c sati_mode_sense_10.c \
		sati_move.c sati_passthrough.c sati_read.c sati_read_buffer.c \
		sati_read_capacity.c  \
		sati_report_luns.c sati_request_sense.c sati_reassign_blocks.c \
		sati_start_stop_unit.c sati_synchronize_cache.c \
		sati_test_unit_ready.c sati_unmap.c sati_util.c \
		sati_verify.c sati_write.c  \
		sati_write_buffer.c sati_write_long.c sati_write_and_verify.c

SRCS += opt_scsi.h opt_cam.h opt_isci.h
SRCS += device_if.h bus_if.h pci_if.h

.include <bsd.kmod.mk>
