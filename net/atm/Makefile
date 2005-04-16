#
# Makefile for the ATM Protocol Families.
#

atm-y		:= addr.o pvc.o signaling.o svc.o ioctl.o common.o atm_misc.o raw.o resources.o
mpoa-objs	:= mpc.o mpoa_caches.o mpoa_proc.o

obj-$(CONFIG_ATM) += atm.o
obj-$(CONFIG_ATM_CLIP) += clip.o
atm-$(subst m,y,$(CONFIG_ATM_CLIP)) += ipcommon.o
obj-$(CONFIG_ATM_BR2684) += br2684.o
atm-$(subst m,y,$(CONFIG_ATM_BR2684)) += ipcommon.o
atm-$(subst m,y,$(CONFIG_NET_SCH_ATM)) += ipcommon.o
atm-$(CONFIG_PROC_FS) += proc.o

obj-$(CONFIG_ATM_LANE) += lec.o
obj-$(CONFIG_ATM_MPOA) += mpoa.o
obj-$(CONFIG_PPPOATM) += pppoatm.o
