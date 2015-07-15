obj-$(CONFIG_MAC802154)	+= mac802154.o
mac802154-objs		:= main.o rx.o tx.o mac_cmd.o mib.o \
			   iface.o llsec.o util.o cfg.o trace.o

CFLAGS_trace.o := -I$(src)

ccflags-y += -D__CHECK_ENDIAN__
