#
# Makefile for building Trusted Computing Group's(TCG) runtime Integrity
# Measurement Architecture(IMA).
#

obj-$(CONFIG_IMA) += ima.o

ima-y := ima_fs.o ima_queue.o ima_init.o ima_main.o ima_crypto.o ima_api.o \
	 ima_policy.o ima_template.o ima_template_lib.o
ima-$(CONFIG_IMA_APPRAISE) += ima_appraise.o
obj-$(CONFIG_IMA_MOK_KEYRING) += ima_mok.o
