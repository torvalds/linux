ifeq ($(CONFIG_CAIF_DEBUG),1)
CAIF_DBG_FLAGS := -DDEBUG
endif

ccflags-y := $(CAIF_FLAGS) $(CAIF_DBG_FLAGS)

caif-objs := caif_dev.o \
	cfcnfg.o cfmuxl.o cfctrl.o  \
	cffrml.o cfveil.o cfdbgl.o\
	cfserl.o cfdgml.o  \
	cfrfml.o cfvidl.o cfutill.o \
	cfsrvl.o cfpkt_skbuff.o caif_config_util.o
clean-dirs:= .tmp_versions

clean-files:= \
	Module.symvers \
	modules.order \
	*.cmd \
	*.o \
	*~

obj-$(CONFIG_CAIF) += caif.o
obj-$(CONFIG_CAIF_NETDEV) += chnl_net.o
obj-$(CONFIG_CAIF) += caif_socket.o

export-objs := caif.o
