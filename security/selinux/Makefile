# SPDX-License-Identifier: GPL-2.0
#
# Makefile for building the SELinux module as part of the kernel tree.
#

obj-$(CONFIG_SECURITY_SELINUX) := selinux.o

selinux-y := avc.o hooks.o selinuxfs.o netlink.o nlmsgtab.o netif.o \
	     netnode.o netport.o status.o \
	     ss/ebitmap.o ss/hashtab.o ss/symtab.o ss/sidtab.o ss/avtab.o \
	     ss/policydb.o ss/services.o ss/conditional.o ss/mls.o ss/context.o

selinux-$(CONFIG_SECURITY_NETWORK_XFRM) += xfrm.o

selinux-$(CONFIG_NETLABEL) += netlabel.o

selinux-$(CONFIG_SECURITY_INFINIBAND) += ibpkey.o

selinux-$(CONFIG_IMA) += ima.o

ccflags-y := -I$(srctree)/security/selinux -I$(srctree)/security/selinux/include

$(addprefix $(obj)/,$(selinux-y)): $(obj)/flask.h

quiet_cmd_flask = GEN     $(obj)/flask.h $(obj)/av_permissions.h
      cmd_flask = $< $(obj)/flask.h $(obj)/av_permissions.h

targets += flask.h av_permissions.h
# once make >= 4.3 is required, we can use grouped targets in the rule below,
# which basically involves adding both headers and a '&' before the colon, see
# the example below:
#   $(obj)/flask.h $(obj)/av_permissions.h &: scripts/selinux/...
$(obj)/flask.h: scripts/selinux/genheaders/genheaders FORCE
	$(call if_changed,flask)
