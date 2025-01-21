# SPDX-License-Identifier: GPL-2.0
ccflags-y += -I$(src)				# needed for trace events

obj-$(CONFIG_SAMPLE_RUST_MINIMAL)		+= rust_minimal.o
obj-$(CONFIG_SAMPLE_RUST_PRINT)			+= rust_print.o

rust_print-y := rust_print_main.o rust_print_events.o

subdir-$(CONFIG_SAMPLE_RUST_HOSTPROGS)		+= hostprogs
