# SPDX-License-Identifier: GPL-2.0
#
# Makefile for trusted keys
#

obj-$(CONFIG_TRUSTED_KEYS) += trusted.o
trusted-y += trusted_core.o
trusted-y += trusted_tpm1.o

$(obj)/trusted_tpm2.o: $(obj)/tpm2key.asn1.h
trusted-y += trusted_tpm2.o
trusted-y += tpm2key.asn1.o
