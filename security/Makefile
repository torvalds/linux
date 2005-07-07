#
# Makefile for the kernel security code
#

obj-$(CONFIG_KEYS)			+= keys/
subdir-$(CONFIG_SECURITY_SELINUX)	+= selinux

# if we don't select a security model, use the default capabilities
ifneq ($(CONFIG_SECURITY),y)
obj-y		+= commoncap.o
endif

# Object file lists
obj-$(CONFIG_SECURITY)			+= security.o dummy.o inode.o
# Must precede capability.o in order to stack properly.
obj-$(CONFIG_SECURITY_SELINUX)		+= selinux/built-in.o
obj-$(CONFIG_SECURITY_CAPABILITIES)	+= commoncap.o capability.o
obj-$(CONFIG_SECURITY_ROOTPLUG)		+= commoncap.o root_plug.o
obj-$(CONFIG_SECURITY_SECLVL)		+= seclvl.o
