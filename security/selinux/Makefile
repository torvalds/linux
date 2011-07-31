#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o ss/

selinux-y := avc.o \
	     hooks.o \
	     selinuxfs.o \
	     netlink.o \
	     nlmsgtab.o \
	     netif.o \
	     netnode.o \
	     netport.o \
	     exports.o

selinux-$(CONFIG_SECURITY_NETWORK_XFRM) += xfrm.o

selinux-$(CONFIG_NETLABEL) += netlabel.o

EXTRA_CFLAGS += -Isecurity/selinux -Isecurity/selinux/include

$(obj)/avc.o: $(obj)/flask.h

quiet_cmd_flask = GEN     $(obj)/flask.h $(obj)/av_permissions.h
      cmd_flask = scripts/selinux/genheaders/genheaders $(obj)/flask.h $(obj)/av_permissions.h

targets += flask.h av_permissions.h
$(obj)/flask.h: $(src)/include/classmap.h FORCE
	$(call if_changed,flask)
