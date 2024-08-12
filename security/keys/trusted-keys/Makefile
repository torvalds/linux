# SPDX-License-Identifier: GPL-2.0
#
# Makefile for trusted keys
#

obj-$(CONFIG_TRUSTED_KEYS) += trusted.o
trusted-y += trusted_core.o
trusted-$(CONFIG_TRUSTED_KEYS_TPM) += trusted_tpm1.o

$(obj)/trusted_tpm2.o: $(obj)/tpm2key.asn1.h
trusted-$(CONFIG_TRUSTED_KEYS_TPM) += trusted_tpm2.o
trusted-$(CONFIG_TRUSTED_KEYS_TPM) += tpm2key.asn1.o

trusted-$(CONFIG_TRUSTED_KEYS_TEE) += trusted_tee.o

trusted-$(CONFIG_TRUSTED_KEYS_CAAM) += trusted_caam.o

trusted-$(CONFIG_TRUSTED_KEYS_DCP) += trusted_dcp.o
