#
# Makefile for the NetLabel subsystem.
#

# base objects
obj-y	:= netlabel_user.o netlabel_kapi.o
obj-y	+= netlabel_domainhash.o netlabel_addrlist.o

# management objects
obj-y	+= netlabel_mgmt.o

# protocol modules
obj-y	+= netlabel_unlabeled.o
obj-y	+= netlabel_cipso_v4.o
obj-$(subst m,y,$(CONFIG_IPV6)) += netlabel_calipso.o
