#-
# Copyright (c) 2016 Jared D. McNeill <jmcneill@invisible.ca>
# All rights reserved.
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

#include <sys/bus.h>

INTERFACE sunxi_dma;

HEADER {
	#include <machine/bus.h>

	struct sunxi_dma_config {
		unsigned int dst_width;
		unsigned int dst_burst_len;
		unsigned int dst_drqtype;
		bool dst_noincr;
		unsigned int dst_blksize;	/* DDMA-only */
		unsigned int dst_wait_cyc;	/* DDMA-only */
		unsigned int src_width;
		unsigned int src_burst_len;
		unsigned int src_drqtype;
		bool src_noincr;
		unsigned int src_blksize;	/* DDMA-only */
		unsigned int src_wait_cyc;	/* DDMA-only */
	};

	typedef void (*sunxi_dma_callback)(void *);
}

#
# Allocate DMA channel
#
METHOD void * alloc {
	device_t dev;
	bool dedicated;
	sunxi_dma_callback callback;
	void *callback_arg;
};

#
# Free DMA channel
#
METHOD void free {
	device_t dev;
	void *dmachan;
};

#
# Set DMA channel configuration
#
METHOD int set_config {
	device_t dev;
	void *dmachan;
	const struct sunxi_dma_config *cfg;
};

#
# Start DMA channel transfer
#
METHOD int transfer {
	device_t dev;
	void *dmachan;
	bus_addr_t src;
	bus_addr_t dst;
	size_t nbytes;
};

#
# Halt DMA channel transfer
#
METHOD void halt {
	device_t dev;
	void *dmachan;
};
