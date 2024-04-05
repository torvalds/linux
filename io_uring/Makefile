# SPDX-License-Identifier: GPL-2.0
#
# Makefile for io_uring

obj-$(CONFIG_IO_URING)		+= io_uring.o xattr.o nop.o fs.o splice.o \
					sync.o advise.o filetable.o \
					openclose.o uring_cmd.o epoll.o \
					statx.o net.o msg_ring.o timeout.o \
					sqpoll.o fdinfo.o tctx.o poll.o \
					cancel.o kbuf.o rsrc.o rw.o opdef.o \
					notif.o waitid.o register.o truncate.o
obj-$(CONFIG_IO_WQ)		+= io-wq.o
obj-$(CONFIG_FUTEX)		+= futex.o
obj-$(CONFIG_NET_RX_BUSY_POLL) += napi.o
