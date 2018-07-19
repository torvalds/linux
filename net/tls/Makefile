#
# Makefile for the TLS subsystem.
#

obj-$(CONFIG_TLS) += tls.o

tls-y := tls_main.o tls_sw.o

tls-$(CONFIG_TLS_DEVICE) += tls_device.o tls_device_fallback.o
