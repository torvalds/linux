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

# The G_RAID metadata class interface.

INTERFACE g_raid_md;

HEADER {
#define G_RAID_MD_TASTE_FAIL		-1
#define G_RAID_MD_TASTE_EXISTING	 0
#define G_RAID_MD_TASTE_NEW		 1
};

# Default implementations of methods.
CODE {
	static int
	g_raid_md_create_default(struct g_raid_md_object *md,
	    struct g_class *mp, struct g_geom **gp)
	{

		return (G_RAID_MD_TASTE_FAIL);
	}

	static int
	g_raid_md_create_req_default(struct g_raid_md_object *md,
	    struct g_class *mp, struct gctl_req *req, struct g_geom **gp)
	{

		return (G_RAID_MD_CREATE(md, mp, gp));
	}

	static int
	g_raid_md_ctl_default(struct g_raid_md_object *md,
	    struct gctl_req *req)
	{

		return (-1);
	}

	static int
	g_raid_md_volume_event_default(struct g_raid_md_object *md,
	    struct g_raid_volume *vol, u_int event)
	{

		return (-1);
	}

	static int
	g_raid_md_free_disk_default(struct g_raid_md_object *md,
	    struct g_raid_volume *vol)
	{

		return (0);
	}

	static int
	g_raid_md_free_volume_default(struct g_raid_md_object *md,
	    struct g_raid_volume *vol)
	{

		return (0);
	}
};

# create() - create new node from scratch.
METHOD int create {
	struct g_raid_md_object *md;
	struct g_class *mp;
	struct g_geom **gp;
} DEFAULT g_raid_md_create_default;

# create_req() - create new node from scratch, with request argument.
METHOD int create_req {
	struct g_raid_md_object *md;
	struct g_class *mp;
	struct gctl_req *req;
	struct g_geom **gp;
} DEFAULT g_raid_md_create_req_default;

# taste() - taste disk and, if needed, create new node.
METHOD int taste {
	struct g_raid_md_object *md;
	struct g_class *mp;
	struct g_consumer *cp;
	struct g_geom **gp;
};

# ctl() - user-level control commands handling method.
METHOD int ctl {
	struct g_raid_md_object *md;
	struct gctl_req *req;
} DEFAULT g_raid_md_ctl_default;

# event() - events handling method.
METHOD int event {
	struct g_raid_md_object *md;
	struct g_raid_disk *disk;
	u_int event;
};

# volume_event() - events handling method.
METHOD int volume_event {
	struct g_raid_md_object *md;
	struct g_raid_volume *vol;
	u_int event;
} DEFAULT g_raid_md_volume_event_default;

# write() - metadata write method.
METHOD int write {
	struct g_raid_md_object *md;
	struct g_raid_volume *vol;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
};

# fail_disk() - mark disk as failed and remove it from use.
METHOD int fail_disk {
	struct g_raid_md_object *md;
	struct g_raid_subdisk *sd;
	struct g_raid_disk *disk;
};

# free_disk() - disk destructor.
METHOD int free_disk {
	struct g_raid_md_object *md;
	struct g_raid_disk *disk;
} DEFAULT g_raid_md_free_disk_default;

# free_volume() - volume destructor.
METHOD int free_volume {
	struct g_raid_md_object *md;
	struct g_raid_volume *vol;
} DEFAULT g_raid_md_free_volume_default;

# free() - destructor.
METHOD int free {
	struct g_raid_md_object *md;
};
