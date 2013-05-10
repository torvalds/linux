#
# Makefile for the Linux AX.25 layer.
#

obj-$(CONFIG_AX25) += ax25.o

ax25-y	 := ax25_addr.o ax25_dev.o ax25_iface.o ax25_in.o ax25_ip.o ax25_out.o \
	    ax25_route.o ax25_std_in.o ax25_std_subr.o ax25_std_timer.o \
	    ax25_subr.o ax25_timer.o ax25_uid.o af_ax25.o
ax25-$(CONFIG_AX25_DAMA_SLAVE) += ax25_ds_in.o ax25_ds_subr.o ax25_ds_timer.o
ax25-$(CONFIG_SYSCTL) += sysctl_net_ax25.o
