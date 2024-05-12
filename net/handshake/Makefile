# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Generic HANDSHAKE service
#
# Author: Chuck Lever <chuck.lever@oracle.com>
#
# Copyright (c) 2023, Oracle and/or its affiliates.
#

obj-y += handshake.o
handshake-y := genl.o netlink.o request.o tlshd.o trace.o

obj-$(CONFIG_NET_HANDSHAKE_KUNIT_TEST) += handshake-test.o
