#
# Makefile for SCTP support code.
#

obj-$(CONFIG_IP_SCTP) += sctp.o

sctp-y := sm_statetable.o sm_statefuns.o sm_sideeffect.o \
	  protocol.o endpointola.o associola.o \
	  transport.o chunk.o sm_make_chunk.o ulpevent.o \
	  inqueue.o outqueue.o ulpqueue.o command.o \
	  tsnmap.o bind_addr.o socket.o primitive.o \
	  output.o input.o debug.o ssnmap.o proc.o crc32c.o \
	  auth.o

sctp-$(CONFIG_SCTP_DBG_OBJCNT) += objcnt.o
sctp-$(CONFIG_SYSCTL) += sysctl.o

sctp-$(subst m,y,$(CONFIG_IPV6))	+= ipv6.o
