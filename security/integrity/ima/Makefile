# SPDX-License-Identifier: GPL-2.0
#
# Makefile for building Trusted Computing Group's(TCG) runtime Integrity
# Measurement Architecture(IMA).
#

obj-$(CONFIG_IMA) += ima.o ima_iint.o

ima-y := ima_fs.o ima_queue.o ima_init.o ima_main.o ima_crypto.o ima_api.o \
	 ima_policy.o ima_template.o ima_template_lib.o
ima-$(CONFIG_IMA_APPRAISE) += ima_appraise.o
ima-$(CONFIG_IMA_APPRAISE_MODSIG) += ima_modsig.o
ima-$(CONFIG_HAVE_IMA_KEXEC) += ima_kexec.o
ima-$(CONFIG_IMA_BLACKLIST_KEYRING) += ima_mok.o
ima-$(CONFIG_IMA_MEASURE_ASYMMETRIC_KEYS) += ima_asymmetric_keys.o
ima-$(CONFIG_IMA_QUEUE_EARLY_BOOT_KEYS) += ima_queue_keys.o

ifeq ($(CONFIG_EFI),y)
ima-$(CONFIG_IMA_SECURE_AND_OR_TRUSTED_BOOT) += ima_efi.o
endif
