#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o ss/

selinux-y := avc.o hooks.o selinuxfs.o netlink.o nlmsgtab.o

selinux-$(CONFIG_SECURITY_NETWORK) += netif.o

EXTRA_CFLAGS += -Isecurity/selinux/include

