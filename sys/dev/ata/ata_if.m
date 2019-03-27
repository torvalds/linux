# Copyright (c) 2004 - 2008 Søren Schmidt <sos@FreeBSD.org>
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

#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/callout.h>
#include <sys/sema.h>
#include <sys/taskqueue.h>
#include <vm/uma.h>
#include <machine/bus.h>
#include <sys/ata.h>
#include <dev/ata/ata-all.h>

INTERFACE ata;

CODE {
	static int ata_null_setmode(device_t dev, int target, int mode)
	{

		if (mode > ATA_PIO_MAX)
			return (ATA_PIO_MAX);
		return (mode);
	}
};
METHOD int setmode {
    device_t    dev;
    int		target;
    int		mode;
}  DEFAULT ata_null_setmode;

CODE {
	static int ata_null_getrev(device_t dev, int target)
	{
		return (0);
	}
};

METHOD int getrev {
    device_t    dev;
    int		target;
} DEFAULT ata_null_getrev;

METHOD void reset {
    device_t    channel;
} DEFAULT ata_generic_reset;

METHOD int reinit {
    device_t    dev;
};
