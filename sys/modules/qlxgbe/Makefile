#/*
# * Copyright (c) 2011-2016 Qlogic Corporation
# * All rights reserved.
# *
# *  Redistribution and use in source and binary forms, with or without
# *  modification, are permitted provided that the following conditions
# *  are met:
# *
# *  1. Redistributions of source code must retain the above copyright
# *     notice, this list of conditions and the following disclaimer.
# *  2. Redistributions in binary form must reproduce the above copyright
# *     notice, this list of conditions and the following disclaimer in the
# *     documentation and/or other materials provided with the distribution.
# *
# *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# *  POSSIBILITY OF SUCH DAMAGE.
# */
#/*
# * File : Makefile
# * Author : David C Somayajulu, Qlogic Corporation, Aliso Viejo, CA 92656.
# */
#
# $FreeBSD$
#

.PATH: ${SRCTOP}/sys/dev/qlxgbe
#.PATH: ${.CURDIR}

KMOD=if_qlxgbe
SRCS=ql_os.c ql_dbg.c ql_hw.c ql_misc.c ql_isr.c ql_ioctl.c
SRCS+= ql_reset.c
SRCS+= ql_fw.c
SRCS+= ql_boot.c
SRCS+= ql_minidump.c

SRCS+= device_if.h bus_if.h pci_if.h

CFLAGS += -DQLA_LOCK_NO_SLEEP=1
#CFLAGS += -DQL_DBG -g
# Please turn off QL_LDFLASH_FW when you enable QL_ENABLE_ISCSI_TLV
# You may comment out both QL_LDFLASH_FW and QL_ENABLE_ISCSI_TLV if you like
# the firmware and boot loader compiled into the driver
#CFLAGS += -DQL_ENABLE_ISCSI_TLV
#CFLAGS += -DQL_LDFLASH_FW


.include <bsd.kmod.mk>
