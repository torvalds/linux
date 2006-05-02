#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o ss/

selinux-y := avc.o hooks.o selinuxfs.o netlink.o nlmsgtab.o netif.o exports.o

selinux-$(CONFIG_SECURITY_NETWORK_XFRM) += xfrm.o

EXTRA_CFLAGS += -Isecurity/selinux/include

