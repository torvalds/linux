obj-$(CONFIG_RDS) += rds.o
rds-y :=	af_rds.o bind.o cong.o connection.o info.o message.o   \
			recv.o send.o stats.o sysctl.o threads.o transport.o \
			loop.o page.o rdma.o \
			rdma_transport.o \
			ib.o ib_cm.o ib_recv.o ib_ring.o ib_send.o ib_stats.o \
			ib_sysctl.o ib_rdma.o \
			iw.o iw_cm.o iw_recv.o iw_ring.o iw_send.o iw_stats.o \
			iw_sysctl.o iw_rdma.o

ifeq ($(CONFIG_RDS_DEBUG), y)
EXTRA_CFLAGS += -DDEBUG
endif

