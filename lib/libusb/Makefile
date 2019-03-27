#
# $FreeBSD$
#
# Makefile for the FreeBSD specific LibUSB 2.0
#

PACKAGE=lib${LIB}
LIB=		usb
SHLIB_MAJOR=	3
SHLIB_MINOR=	0
SRCS=		libusb20.c
SRCS+=		libusb20_desc.c
SRCS+=		libusb20_ugen20.c
INCS+=		libusb20.h
INCS+=		libusb20_desc.h
MAN=		libusb.3 libusb20.3
MKLINT=		no
NOGCCERROR=
PTHREAD_LIBS?=	-lpthread

WARNS?=		2

LIBADD=		pthread

MLINKS+=	libusb.3 usb.3

# libusb 0.1 compat
INCS+=		usb.h
SRCS+=		libusb01.c

# libusb 1.0 compat
INCS+=		libusb.h
SRCS+=		libusb10.c
SRCS+=		libusb10_desc.c
SRCS+=		libusb10_hotplug.c
SRCS+=		libusb10_io.c

.if defined(COMPAT_32BIT)
CFLAGS+=	-DCOMPAT_32BIT
.endif

FILES=		libusb-0.1.pc libusb-1.0.pc libusb-2.0.pc
FILESDIR=	${LIBDATADIR}/pkgconfig

#
# Cross platform support
#
# Examples:
# make LIBUSB_GLOBAL_INCLUDE_FILE=libusb_global_linux.h
# make COMPAT_32BIT=YES \
#   LIBUSB_GLOBAL_INCLUDE_FILE=libusb_global_linux.h \
#   DEBUG_FLAGS="-g"
#
# From Ubuntu 10.04:
# freebsd-make LIBUSB_GLOBAL_INCLUDE_FILE=libusb_global_linux.h \
#    PTHREAD_LIBS="-lpthread -lrt"
# freebsd-make COMPAT32_BIT=YES \
#    LIBUSB_GLOBAL_INCLUDE_FILE=libusb_global_linux.h \
#    PTHREAD_LIBS="-lpthread -lrt"
#
#
.if defined(LIBUSB_GLOBAL_INCLUDE_FILE)
CFLAGS+=	-DLIBUSB_GLOBAL_INCLUDE_FILE=\"${LIBUSB_GLOBAL_INCLUDE_FILE}\"
CFLAGS+=	-DUSB_GLOBAL_INCLUDE_FILE=\"${LIBUSB_GLOBAL_INCLUDE_FILE}\"
CFLAGS+=	-I ../../sys
.endif

# LibUSB v1.0
MLINKS += libusb.3 libusb_get_version.3
MLINKS += libusb.3 libusb_init.3
MLINKS += libusb.3 libusb_exit.3
MLINKS += libusb.3 libusb_strerror.3
MLINKS += libusb.3 libusb_error_name.3
MLINKS += libusb.3 libusb_set_debug.3
MLINKS += libusb.3 libusb_get_device_list.3
MLINKS += libusb.3 libusb_free_device_list.3
MLINKS += libusb.3 libusb_get_bus_number.3
MLINKS += libusb.3 libusb_get_port_number.3
MLINKS += libusb.3 libusb_get_device_address.3
MLINKS += libusb.3 libusb_get_device_speed.3
MLINKS += libusb.3 libusb_get_max_packet_size.3
MLINKS += libusb.3 libusb_get_max_iso_packet_size.3
MLINKS += libusb.3 libusb_ref_device.3
MLINKS += libusb.3 libusb_unref_device.3
MLINKS += libusb.3 libusb_open.3
MLINKS += libusb.3 libusb_open_device_with_vid_pid.3
MLINKS += libusb.3 libusb_close.3
MLINKS += libusb.3 libusb_get_device.3
MLINKS += libusb.3 libusb_get_configuration.3
MLINKS += libusb.3 libusb_set_configuration.3
MLINKS += libusb.3 libusb_claim_interface.3
MLINKS += libusb.3 libusb_release_interface.3
MLINKS += libusb.3 libusb_set_interface_alt_setting.3
MLINKS += libusb.3 libusb_clear_halt.3
MLINKS += libusb.3 libusb_reset_device.3
MLINKS += libusb.3 libusb_check_connected.3
MLINKS += libusb.3 libusb_kernel_driver_active.3
MLINKS += libusb.3 libusb_get_driver.3
MLINKS += libusb.3 libusb_get_driver_np.3
MLINKS += libusb.3 libusb_detach_kernel_driver.3
MLINKS += libusb.3 libusb_detach_kernel_driver_np.3
MLINKS += libusb.3 libusb_attach_kernel_driver.3
MLINKS += libusb.3 libusb_set_auto_detach_kernel_driver.3
MLINKS += libusb.3 libusb_get_device_descriptor.3
MLINKS += libusb.3 libusb_get_active_config_descriptor.3
MLINKS += libusb.3 libusb_get_config_descriptor.3
MLINKS += libusb.3 libusb_get_config_descriptor_by_value.3
MLINKS += libusb.3 libusb_free_config_descriptor.3
MLINKS += libusb.3 libusb_get_string_descriptor.3
MLINKS += libusb.3 libusb_get_string_descriptor_ascii.3
MLINKS += libusb.3 libusb_parse_ss_endpoint_comp.3
MLINKS += libusb.3 libusb_free_ss_endpoint_comp.3
MLINKS += libusb.3 libusb_get_ss_endpoint_companion_descriptor.3
MLINKS += libusb.3 libusb_free_ss_endpoint_companion_descriptor.3
MLINKS += libusb.3 libusb_parse_bos_descriptor.3
MLINKS += libusb.3 libusb_free_bos_descriptor.3
MLINKS += libusb.3 libusb_get_usb_2_0_extension_descriptor.3
MLINKS += libusb.3 libusb_free_usb_2_0_extension_descriptor.3
MLINKS += libusb.3 libusb_get_ss_usb_device_capability_descriptor.3
MLINKS += libusb.3 libusb_free_ss_usb_device_capability_descriptor.3
MLINKS += libusb.3 libusb_get_container_id_descriptor.3
MLINKS += libusb.3 libusb_free_container_id_descriptor.3
MLINKS += libusb.3 libusb_alloc_streams.3
MLINKS += libusb.3 libusb_free_streams.3
MLINKS += libusb.3 libusb_alloc_transfer.3
MLINKS += libusb.3 libusb_free_transfer.3
MLINKS += libusb.3 libusb_transfer_set_stream_id.3
MLINKS += libusb.3 libusb_transfer_get_stream_id.3
MLINKS += libusb.3 libusb_submit_transfer.3
MLINKS += libusb.3 libusb_cancel_transfer.3
MLINKS += libusb.3 libusb_control_transfer.3
MLINKS += libusb.3 libusb_bulk_transfer.3
MLINKS += libusb.3 libusb_interrupt_transfer.3
MLINKS += libusb.3 libusb_try_lock_events.3
MLINKS += libusb.3 libusb_lock_events.3
MLINKS += libusb.3 libusb_unlock_events.3
MLINKS += libusb.3 libusb_event_handling_ok.3
MLINKS += libusb.3 libusb_event_handler_active.3
MLINKS += libusb.3 libusb_lock_event_waiters.3
MLINKS += libusb.3 libusb_unlock_event_waiters.3
MLINKS += libusb.3 libusb_wait_for_event.3
MLINKS += libusb.3 libusb_handle_events_timeout_completed.3
MLINKS += libusb.3 libusb_handle_events_completed.3
MLINKS += libusb.3 libusb_handle_events_timeout.3
MLINKS += libusb.3 libusb_handle_events.3
MLINKS += libusb.3 libusb_handle_events_locked.3
MLINKS += libusb.3 libusb_get_next_timeout.3
MLINKS += libusb.3 libusb_set_pollfd_notifiers.3
MLINKS += libusb.3 libusb_get_pollfds.3
MLINKS += libusb.3 libusb_hotplug_register_callback.3
MLINKS += libusb.3 libusb_hotplug_deregister_callback.3

# LibUSB v0.1
MLINKS += libusb.3 usb_open.3
MLINKS += libusb.3 usb_close.3
MLINKS += libusb.3 usb_get_string.3
MLINKS += libusb.3 usb_get_string_simple.3
MLINKS += libusb.3 usb_get_descriptor_by_endpoint.3
MLINKS += libusb.3 usb_get_descriptor.3
MLINKS += libusb.3 usb_parse_descriptor.3
MLINKS += libusb.3 usb_parse_configuration.3
MLINKS += libusb.3 usb_destroy_configuration.3
MLINKS += libusb.3 usb_fetch_and_parse_descriptors.3
MLINKS += libusb.3 usb_bulk_write.3
MLINKS += libusb.3 usb_bulk_read.3
MLINKS += libusb.3 usb_interrupt_write.3
MLINKS += libusb.3 usb_interrupt_read.3
MLINKS += libusb.3 usb_control_msg.3
MLINKS += libusb.3 usb_set_configuration.3
MLINKS += libusb.3 usb_claim_interface.3
MLINKS += libusb.3 usb_release_interface.3
MLINKS += libusb.3 usb_set_altinterface.3
MLINKS += libusb.3 usb_resetep.3
MLINKS += libusb.3 usb_clear_halt.3
MLINKS += libusb.3 usb_reset.3
MLINKS += libusb.3 usb_strerror.3
MLINKS += libusb.3 usb_init.3
MLINKS += libusb.3 usb_set_debug.3
MLINKS += libusb.3 usb_find_busses.3
MLINKS += libusb.3 usb_find_devices.3
MLINKS += libusb.3 usb_device.3
MLINKS += libusb.3 usb_get_busses.3
MLINKS += libusb.3 usb_check_connected.3

# LibUSB v2.0
MLINKS += libusb20.3 libusb20_tr_close.3
MLINKS += libusb20.3 libusb20_tr_open.3
MLINKS += libusb20.3 libusb20_tr_get_pointer.3
MLINKS += libusb20.3 libusb20_tr_get_time_complete.3
MLINKS += libusb20.3 libusb20_tr_get_actual_frames.3
MLINKS += libusb20.3 libusb20_tr_get_actual_length.3
MLINKS += libusb20.3 libusb20_tr_get_max_frames.3
MLINKS += libusb20.3 libusb20_tr_get_max_packet_length.3
MLINKS += libusb20.3 libusb20_tr_get_max_total_length.3
MLINKS += libusb20.3 libusb20_tr_get_status.3
MLINKS += libusb20.3 libusb20_tr_pending.3
MLINKS += libusb20.3 libusb20_tr_callback_wrapper.3
MLINKS += libusb20.3 libusb20_tr_clear_stall_sync.3
MLINKS += libusb20.3 libusb20_tr_drain.3
MLINKS += libusb20.3 libusb20_tr_set_buffer.3
MLINKS += libusb20.3 libusb20_tr_set_callback.3
MLINKS += libusb20.3 libusb20_tr_set_flags.3
MLINKS += libusb20.3 libusb20_tr_get_length.3
MLINKS += libusb20.3 libusb20_tr_set_length.3
MLINKS += libusb20.3 libusb20_tr_set_priv_sc0.3
MLINKS += libusb20.3 libusb20_tr_set_priv_sc1.3
MLINKS += libusb20.3 libusb20_tr_set_timeout.3
MLINKS += libusb20.3 libusb20_tr_set_total_frames.3
MLINKS += libusb20.3 libusb20_tr_setup_bulk.3
MLINKS += libusb20.3 libusb20_tr_setup_control.3
MLINKS += libusb20.3 libusb20_tr_setup_intr.3
MLINKS += libusb20.3 libusb20_tr_setup_isoc.3
MLINKS += libusb20.3 libusb20_tr_bulk_intr_sync.3
MLINKS += libusb20.3 libusb20_tr_start.3
MLINKS += libusb20.3 libusb20_tr_stop.3
MLINKS += libusb20.3 libusb20_tr_submit.3
MLINKS += libusb20.3 libusb20_tr_get_priv_sc0.3
MLINKS += libusb20.3 libusb20_tr_get_priv_sc1.3
MLINKS += libusb20.3 libusb20_dev_get_backend_name.3
MLINKS += libusb20.3 libusb20_dev_get_info.3
MLINKS += libusb20.3 libusb20_dev_get_iface_desc.3
MLINKS += libusb20.3 libusb20_dev_get_desc.3
MLINKS += libusb20.3 libusb20_dev_close.3
MLINKS += libusb20.3 libusb20_dev_detach_kernel_driver.3
MLINKS += libusb20.3 libusb20_dev_set_config_index.3
MLINKS += libusb20.3 libusb20_dev_get_debug.3
MLINKS += libusb20.3 libusb20_dev_get_fd.3
MLINKS += libusb20.3 libusb20_dev_kernel_driver_active.3
MLINKS += libusb20.3 libusb20_dev_open.3
MLINKS += libusb20.3 libusb20_dev_process.3
MLINKS += libusb20.3 libusb20_dev_request_sync.3
MLINKS += libusb20.3 libusb20_dev_req_string_sync.3
MLINKS += libusb20.3 libusb20_dev_req_string_simple_sync.3
MLINKS += libusb20.3 libusb20_dev_reset.3
MLINKS += libusb20.3 libusb20_dev_check_connected.3
MLINKS += libusb20.3 libusb20_dev_set_power_mode.3
MLINKS += libusb20.3 libusb20_dev_get_power_mode.3
MLINKS += libusb20.3 libusb20_dev_get_port_path.3
MLINKS += libusb20.3 libusb20_dev_get_power_usage.3
MLINKS += libusb20.3 libusb20_dev_set_alt_index.3
MLINKS += libusb20.3 libusb20_dev_get_device_desc.3
MLINKS += libusb20.3 libusb20_dev_alloc_config.3
MLINKS += libusb20.3 libusb20_dev_alloc.3
MLINKS += libusb20.3 libusb20_dev_get_address.3
MLINKS += libusb20.3 libusb20_dev_get_parent_address.3
MLINKS += libusb20.3 libusb20_dev_get_parent_port.3
MLINKS += libusb20.3 libusb20_dev_get_bus_number.3
MLINKS += libusb20.3 libusb20_dev_get_mode.3
MLINKS += libusb20.3 libusb20_dev_get_speed.3
MLINKS += libusb20.3 libusb20_dev_get_config_index.3
MLINKS += libusb20.3 libusb20_dev_free.3
MLINKS += libusb20.3 libusb20_dev_set_debug.3
MLINKS += libusb20.3 libusb20_dev_wait_process.3
MLINKS += libusb20.3 libusb20_be_get_template.3
MLINKS += libusb20.3 libusb20_be_set_template.3
MLINKS += libusb20.3 libusb20_be_get_dev_quirk.3
MLINKS += libusb20.3 libusb20_be_get_quirk_name.3
MLINKS += libusb20.3 libusb20_be_add_dev_quirk.3
MLINKS += libusb20.3 libusb20_be_remove_dev_quirk.3
MLINKS += libusb20.3 libusb20_be_alloc_default.3
MLINKS += libusb20.3 libusb20_be_device_foreach.3
MLINKS += libusb20.3 libusb20_be_dequeue_device.3
MLINKS += libusb20.3 libusb20_be_enqueue_device.3
MLINKS += libusb20.3 libusb20_be_free.3
MLINKS += libusb20.3 libusb20_me_get_1.3
MLINKS += libusb20.3 libusb20_me_get_2.3
MLINKS += libusb20.3 libusb20_me_encode.3
MLINKS += libusb20.3 libusb20_me_decode.3
MLINKS += libusb20.3 libusb20_desc_foreach.3
MLINKS += libusb20.3 libusb20_strerror.3
MLINKS += libusb20.3 libusb20_error_name.3

.include <bsd.lib.mk>
