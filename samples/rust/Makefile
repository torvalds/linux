# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(src)				# needed for trace events

obj-$(CONFIG_SAMPLE_RUST_MINIMAL)		+= rust_minimal.o
obj-$(CONFIG_SAMPLE_RUST_MISC_DEVICE)		+= rust_misc_device.o
obj-$(CONFIG_SAMPLE_RUST_PRINT)			+= rust_print.o
obj-$(CONFIG_SAMPLE_RUST_DMA)			+= rust_dma.o
obj-$(CONFIG_SAMPLE_RUST_DRIVER_PCI)		+= rust_driver_pci.o
obj-$(CONFIG_SAMPLE_RUST_DRIVER_PLATFORM)	+= rust_driver_platform.o
obj-$(CONFIG_SAMPLE_RUST_DRIVER_FAUX)		+= rust_driver_faux.o

rust_print-y := rust_print_main.o rust_print_events.o

subdir-$(CONFIG_SAMPLE_RUST_HOSTPROGS)		+= hostprogs
