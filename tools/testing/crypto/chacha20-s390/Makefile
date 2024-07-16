# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2022 Red Hat, Inc.
# Author: Vladis Dronov <vdronoff@gmail.com>

obj-m += test_cipher.o
test_cipher-y := test-cipher.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
