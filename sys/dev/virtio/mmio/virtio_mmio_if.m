#-
# Copyright (c) 2014 Ruslan Bukin <br@bsdpad.com>
# All rights reserved.
#
# This software was developed by SRI International and the University of
# Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
# ("CTSRD"), as part of the DARPA CRASH research programme.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#include <sys/types.h>

#
# This is optional interface to virtio mmio backend.
# Useful when backend is implemented not by the hardware but software, e.g.
# by using another cpu core.
#

INTERFACE virtio_mmio;

CODE {
	static int
	virtio_mmio_prewrite(device_t dev, size_t offset, int val)
	{

		return (1);
	}

	static int
	virtio_mmio_note(device_t dev, size_t offset, int val)
	{

		return (1);
	}

	static int
	virtio_mmio_setup_intr(device_t dev, device_t mmio_dev,
					void *handler, void *ih_user)
	{

		return (1);
	}
};

#
# Inform backend we are going to write data at offset.
#
METHOD int prewrite {
	device_t	dev;
	size_t		offset;
	int		val;
} DEFAULT virtio_mmio_prewrite;

#
# Inform backend we have data wrotten to offset.
#
METHOD int note {
	device_t	dev;
	size_t		offset;
	int		val;
} DEFAULT virtio_mmio_note;

#
# Inform backend we are going to poll virtqueue.
#
METHOD int poll {
	device_t	dev;
};

#
# Setup backend-specific interrupts.
#
METHOD int setup_intr {
	device_t	dev;
	device_t	mmio_dev;
	void		*handler;
	void		*ih_user;
} DEFAULT virtio_mmio_setup_intr;
