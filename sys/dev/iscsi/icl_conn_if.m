#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2014 The FreeBSD Foundation
# All rights reserved.
#
# This software was developed by Edward Tomasz Napierala under sponsorship
# from the FreeBSD Foundation.
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

#include <sys/socket.h>
#include <dev/iscsi/icl.h>

INTERFACE icl_conn;

METHOD size_t pdu_data_segment_length {
	struct icl_conn *_ic;
	const struct icl_pdu *_ip;
};

METHOD int pdu_append_data {
	struct icl_conn *_ic;
	struct icl_pdu *_ip;
	const void *_addr;
	size_t _len;
	int _flags;
};

METHOD void pdu_get_data {
	struct icl_conn *_ic;
	struct icl_pdu *_ip;
	size_t _off;
	void *_addr;
	size_t _len;
};

METHOD void pdu_queue {
	struct icl_conn *_ic;
	struct icl_pdu *_ip;
};

METHOD void pdu_free {
	struct icl_conn *_ic;
	struct icl_pdu *_ip;
};

METHOD struct icl_pdu * new_pdu {
	struct icl_conn *_ic;
	int _flags;
};

METHOD void free {
	struct icl_conn *_ic;
};

METHOD int handoff {
	struct icl_conn *_ic;
	int _fd;
};

METHOD void close {
	struct icl_conn *_ic;
};

METHOD int task_setup {
	struct icl_conn *_ic;
	struct icl_pdu *_ip;
	struct ccb_scsiio *_csio;
	uint32_t *_task_tag;
	void **_prvp;
};

METHOD void task_done {
	struct icl_conn *_ic;
	void *_prv;
};

METHOD int transfer_setup {
	struct icl_conn *_ic;
	union ctl_io *_io;
	uint32_t *_transfer_tag;
	void **_prvp;
};

METHOD void transfer_done {
	struct icl_conn *_ic;
	void *_prv;
};

#
# The function below is only used with ICL_KERNEL_PROXY.
#
METHOD int connect {
	struct icl_conn *_ic;
	int _domain;
	int _socktype;
	int _protocol;
	struct sockaddr *_from_sa;
	struct sockaddr *_to_sa;
};
