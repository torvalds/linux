# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the TLS subsystem.
#

CFLAGS_trace.o := -I$(src)

obj-$(CONFIG_TLS) += tls.o

tls-y := tls_main.o tls_sw.o tls_proc.o trace.o tls_strp.o

tls-$(CONFIG_TLS_TOE) += tls_toe.o
tls-$(CONFIG_TLS_DEVICE) += tls_device.o tls_device_fallback.o
