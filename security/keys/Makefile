#
# Makefile for key management
#

obj-y := \
	key.o \
	keyring.o \
	keyctl.o \
	process_keys.o \
	user_defined.o \
	request_key.o

obj-$(CONFIG_KEYS_COMPAT) += compat.o
obj-$(CONFIG_PROC_FS) += proc.o
