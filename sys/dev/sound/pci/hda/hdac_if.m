# Copyright (c) 2012 Alexander Motin <mav@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer,
#    without modification, immediately at the beginning of the file.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# $FreeBSD$

#include <sys/rman.h>

INTERFACE hdac;

METHOD struct mtx * get_mtx {
    device_t    dev;
    device_t    child;
};

METHOD uint32_t codec_command {
    device_t    dev;
    device_t    child;
    uint32_t    verb;
};

METHOD int stream_alloc {
    device_t    dev;
    device_t    child;
    int         dir;
    int         format;
    int         stripe;
    uint32_t    **dmapos;
};

METHOD void stream_free {
    device_t    dev;
    device_t    child;
    int         dir;
    int         stream;
};

METHOD int stream_start {
    device_t    dev;
    device_t    child;
    int         dir;
    int         stream;
    bus_addr_t  buf;
    int         blksz;
    int         blkcnt;
};

METHOD void stream_stop {
    device_t    dev;
    device_t    child;
    int         dir;
    int         stream;
};

METHOD void stream_reset {
    device_t    dev;
    device_t    child;
    int         dir;
    int         stream;
};

METHOD uint32_t stream_getptr {
    device_t    dev;
    device_t    child;
    int         dir;
    int         stream;
};

METHOD void stream_intr {
    device_t    dev;
    int         dir;
    int         stream;
};

METHOD int unsol_alloc {
    device_t    dev;
    device_t    child;
    int         wanted;
};

METHOD void unsol_free {
    device_t    dev;
    device_t    child;
    int         tag;
};

METHOD void unsol_intr {
    device_t    dev;
    uint32_t    resp;
};

METHOD void pindump {
    device_t    dev;
};

