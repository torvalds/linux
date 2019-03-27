#-
# KOBJ
#
# Copyright (c) 2000 Cameron Grant <cg@freebsd.org>
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

#include <dev/sound/pcm/sound.h>

INTERFACE feeder;

CODE {

	static int
	feeder_noinit(struct pcm_feeder* feeder)
	{
		return 0;
	}

	static int
	feeder_nofree(struct pcm_feeder* feeder)
	{
		return 0;
	}

	static int
	feeder_noset(struct pcm_feeder* feeder, int what, int value)
	{
		return -1;
	}

	static int
	feeder_noget(struct pcm_feeder* feeder, int what)
	{
		return -1;
	}

};

METHOD int init {
	struct pcm_feeder* feeder;
} DEFAULT feeder_noinit;

METHOD int free {
	struct pcm_feeder* feeder;
} DEFAULT feeder_nofree;

METHOD int set {
	struct pcm_feeder* feeder;
	int what;
	int value;
} DEFAULT feeder_noset;

METHOD int get {
	struct pcm_feeder* feeder;
	int what;
} DEFAULT feeder_noget;

METHOD int feed {
	struct pcm_feeder* feeder;
	struct pcm_channel* c;
	u_int8_t* buffer;
	u_int32_t count;
	void* source;
};
