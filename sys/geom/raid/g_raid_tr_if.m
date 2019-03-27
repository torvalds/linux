#-
# Copyright (c) 2010 Alexander Motin
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
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

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/sbuf.h>
#include <sys/bus.h>
#include <machine/bus.h>
#include <sys/systm.h>
#include <geom/geom.h>
#include <geom/raid/g_raid.h>

# The G_RAID transformation class interface.

INTERFACE g_raid_tr;

# Default implementations of methods.
CODE {
	static int
	g_raid_tr_locked_default(struct g_raid_tr_object *tr, void *argp)
	{

		return (0);
	}
};

HEADER {
#define G_RAID_TR_TASTE_FAIL		-1
#define G_RAID_TR_TASTE_SUCCEED		 0
};

# taste() - volume taste method.
METHOD int taste {
	struct g_raid_tr_object *tr;
	struct g_raid_volume *volume;
};

# event() - events handling method.
METHOD int event {
	struct g_raid_tr_object *tr;
	struct g_raid_subdisk *sd;
	u_int event;
};

# start() - begin operation.
METHOD int start {
	struct g_raid_tr_object *tr;
};

# stop() - stop operation.
METHOD int stop {
	struct g_raid_tr_object *tr;
};

# iorequest() - manage forward transformation and generates requests to disks.
METHOD void iostart {
	struct g_raid_tr_object *tr;
	struct bio *bp;
};

# iodone() - manages backward transformation and reports completion status.
METHOD void iodone {
	struct g_raid_tr_object *tr;
	struct g_raid_subdisk *sd;
	struct bio *bp;
};

# kerneldump() - optimized for rebustness (simplified) kernel dumping routine.
METHOD int kerneldump {
	struct g_raid_tr_object *tr;
	void *virtual;
	vm_offset_t physical;
	off_t offset;
	size_t length;
} DEFAULT g_raid_tr_kerneldump_common;

# locked() - callback method for lock().
METHOD int locked {
	struct g_raid_tr_object *tr;
	void *argp;
} DEFAULT g_raid_tr_locked_default;

# free() - destructor.
METHOD int free {
	struct g_raid_tr_object *tr;
};

# idle() - callback when the volume is idle for a while and the TR wants
# to schedule some work for that idle period.
METHOD int idle {
	struct g_raid_tr_object *tr;
};
