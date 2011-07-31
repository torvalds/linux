ifeq ($(CONFIG_CAIF_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

caif-objs := caif_dev.o \
	cfcnfg.o cfmuxl.o cfctrl.o  \
	cffrml.o cfveil.o cfdbgl.o\
	cfserl.o cfdgml.o  \
	cfrfml.o cfvidl.o cfutill.o \
	cfsrvl.o cfpkt_skbuff.o caif_config_util.o

obj-$(CONFIG_CAIF) += caif.o
obj-$(CONFIG_CAIF_NETDEV) += chnl_net.o
obj-$(CONFIG_CAIF) += caif_socket.o

export-objs := caif.o
