# SPDX-License-Identifier: GPL-2.0
#
# Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.

# Enclave lifetime management support for Nitro Enclaves (NE) - ioctl sample
# usage.

.PHONY: all clean

CFLAGS += -Wall

all:
	$(CC) $(CFLAGS) -o ne_ioctl_sample ne_ioctl_sample.c -lpthread

clean:
	rm -f ne_ioctl_sample
