#-
# Copyright (c) 2016 Alexander Motin <mav@FreeBSD.org>
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
#include <machine/bus.h>

INTERFACE ntb;

HEADER {
	enum ntb_speed {
		NTB_SPEED_AUTO = -1,
		NTB_SPEED_NONE = 0,
		NTB_SPEED_GEN1 = 1,
		NTB_SPEED_GEN2 = 2,
		NTB_SPEED_GEN3 = 3,
		NTB_SPEED_GEN4 = 4,
	};

	enum ntb_width {
		NTB_WIDTH_AUTO = -1,
		NTB_WIDTH_NONE = 0,
		NTB_WIDTH_1 = 1,
		NTB_WIDTH_2 = 2,
		NTB_WIDTH_4 = 4,
		NTB_WIDTH_8 = 8,
		NTB_WIDTH_12 = 12,
		NTB_WIDTH_16 = 16,
		NTB_WIDTH_32 = 32,
	};

	typedef void (*ntb_db_callback)(void *data, uint32_t vector);
	typedef void (*ntb_event_callback)(void *data);
	struct ntb_ctx_ops {
		ntb_event_callback	link_event;
		ntb_db_callback		db_event;
	};
};

METHOD bool link_is_up {
	device_t	 ntb;
	enum ntb_speed	*speed;
	enum ntb_width	*width;
};

METHOD int link_enable {
	device_t	 ntb;
	enum ntb_speed	 speed;
	enum ntb_width	 width;
};

METHOD int link_disable {
	device_t	 ntb;
};

METHOD bool link_enabled {
	device_t	 ntb;
};

METHOD int set_ctx {
	device_t	 ntb;
	void		*ctx;
	const struct ntb_ctx_ops *ctx_ops;
};

METHOD void * get_ctx {
	device_t	 ntb;
	const struct ntb_ctx_ops **ctx_ops;
};

METHOD void clear_ctx {
	device_t	 ntb;
};

METHOD uint8_t mw_count {
	device_t	 ntb;
};

METHOD int mw_get_range {
	device_t	 ntb;
	unsigned	 mw_idx;
	vm_paddr_t	*base;
	caddr_t		*vbase;
	size_t		*size;
	size_t		*align;
	size_t		*align_size;
	bus_addr_t	*plimit;
};

METHOD int mw_set_trans {
	device_t	 ntb;
	unsigned	 mw_idx;
	bus_addr_t	 addr;
	size_t		 size;
};

METHOD int mw_clear_trans {
	device_t	 ntb;
	unsigned	 mw_idx;
};

METHOD int mw_get_wc {
	device_t	 ntb;
	unsigned	 mw_idx;
	vm_memattr_t	*mode;
};

METHOD int mw_set_wc {
	device_t	 ntb;
	unsigned	 mw_idx;
	vm_memattr_t	 mode;
};

METHOD uint8_t spad_count {
	device_t	 ntb;
};

METHOD void spad_clear {
	device_t	 ntb;
};

METHOD int spad_write {
	device_t	 ntb;
	unsigned int	 idx;
	uint32_t	 val;
};

METHOD int spad_read {
	device_t	 ntb;
	unsigned int	 idx;
	uint32_t	 *val;
};

METHOD int peer_spad_write {
	device_t	 ntb;
	unsigned int	 idx;
	uint32_t	 val;
};

METHOD int peer_spad_read {
	device_t	 ntb;
	unsigned int	 idx;
	uint32_t	*val;
};

METHOD uint64_t db_valid_mask {
	device_t	 ntb;
};

METHOD int db_vector_count {
	device_t	 ntb;
};

METHOD uint64_t db_vector_mask {
	device_t	 ntb;
	uint32_t	 vector;
};

METHOD int peer_db_addr {
	device_t	 ntb;
	bus_addr_t	*db_addr;
	vm_size_t	*db_size;
};

METHOD void db_clear {
	device_t	 ntb;
	uint64_t	 bits;
};

METHOD void db_clear_mask {
	device_t	 ntb;
	uint64_t	 bits;
};

METHOD uint64_t db_read {
	device_t	 ntb;
};

METHOD void db_set_mask {
	device_t	 ntb;
	uint64_t	 bits;
};

METHOD void peer_db_set {
	device_t	 ntb;
	uint64_t	 bits;
};
