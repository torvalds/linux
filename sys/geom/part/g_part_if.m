#-
# Copyright (c) 2006-2009 Marcel Moolenaar
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
#include <geom/part/g_part.h>

# The G_PART scheme interface.

INTERFACE g_part;

# Default implementations of methods.
CODE {
	static void
	default_fullname(struct g_part_table *table,
	    struct g_part_entry *entry, struct sbuf *sb, const char *pfx)
	{
		char buf[32];

		sbuf_printf(sb, "%s%s", pfx,
		    G_PART_NAME(table, entry, buf, sizeof(buf)));
	}

	static int
	default_precheck(struct g_part_table *t __unused,
	    enum g_part_ctl r __unused, struct g_part_parms *p __unused)
	{
		return (0);
	}

	static int
	default_resize(struct g_part_table *t __unused,
	    struct g_part_entry *e __unused, struct g_part_parms *p __unused)
	{
		return (ENOSYS);
	}

	static int
	default_recover(struct g_part_table *t __unused)
	{
		return (ENOSYS);
	}

	static int
	default_ioctl(struct g_part_table *table __unused, struct g_provider *pp __unused,
	    u_long cmd __unused, void *data __unused, int fflag __unused,
	    struct thread *td __unused)
	{
		return (ENOIOCTL);
	}
};

# add() - scheme specific processing for the add verb.
METHOD int add {
	struct g_part_table *table;
	struct g_part_entry *entry;
	struct g_part_parms *gpp;
};

# bootcode() - scheme specific processing for the bootcode verb.
METHOD int bootcode {
	struct g_part_table *table;
	struct g_part_parms *gpp;
};

# create() - scheme specific processing for the create verb.
METHOD int create {
	struct g_part_table *table;
	struct g_part_parms *gpp;
};

# destroy() - scheme specific processing for the destroy verb.
METHOD int destroy {
	struct g_part_table *table;
	struct g_part_parms *gpp;
};

# dumpconf()
METHOD void dumpconf {
	struct g_part_table *table;
	struct g_part_entry *entry;
	struct sbuf *sb;
	const char *indent;
};

# dumpto() - return whether the partiton can be used for kernel dumps.
METHOD int dumpto {
	struct g_part_table *table;
	struct g_part_entry *entry;
};

# fullname() - write the name of the given partition entry to the sbuf.
METHOD void fullname {
	struct g_part_table *table;
	struct g_part_entry *entry;
	struct sbuf *sb;
	const char *pfx;
} DEFAULT default_fullname;

# ioctl() - implement historic ioctls, perhaps.
METHOD int ioctl {
	struct g_part_table *table;
	struct g_provider *pp;
	u_long cmd;
	void *data;
	int fflag;
	struct thread *td;
} DEFAULT default_ioctl;

# modify() - scheme specific processing for the modify verb.
METHOD int modify {
	struct g_part_table *table;
	struct g_part_entry *entry;
	struct g_part_parms *gpp;
};

# resize() - scheme specific processing for the resize verb.
METHOD int resize {
	struct g_part_table *table;
	struct g_part_entry *entry;
	struct g_part_parms *gpp;
} DEFAULT default_resize;

# name() - return the name of the given partition entry.
# Typical names are "p1", "s0" or "c".
METHOD const char * name {
	struct g_part_table *table;
	struct g_part_entry *entry;
	char *buf;
	size_t bufsz;
};

# precheck() - method to allow schemes to check the parameters given
# to the mentioned ctl request. This only applies to the requests that
# operate on a GEOM. In other words, it does not apply to the create
# request.
# It is allowed (intended actually) to change the parameters according
# to the schemes needs before they are used. Returning an error will
# terminate the request immediately.
METHOD int precheck {
	struct g_part_table *table;
	enum g_part_ctl req;
	struct g_part_parms *gpp;
} DEFAULT default_precheck;

# probe() - probe the provider attached to the given consumer for the
# existence of the scheme implemented by the G_PART interface handler.
METHOD int probe {
	struct g_part_table *table;
	struct g_consumer *cp;
};

# read() - read the on-disk partition table into memory.
METHOD int read {
	struct g_part_table *table;
	struct g_consumer *cp;
};

# recover() - scheme specific processing for the recover verb.
METHOD int recover {
	struct g_part_table *table;
} DEFAULT default_recover;

# setunset() - set or unset partition entry attributes.
METHOD int setunset {
	struct g_part_table *table;
	struct g_part_entry *entry;
	const char *attrib;
	unsigned int set;
};

# type() - return a string representation of the partition type.
# Preferably, the alias names.
METHOD const char * type {
        struct g_part_table *table;
        struct g_part_entry *entry;
        char *buf;
        size_t bufsz;
};

# write() - write the in-memory partition table to disk.
METHOD int write {
	struct g_part_table *table;
	struct g_consumer *cp;
};
