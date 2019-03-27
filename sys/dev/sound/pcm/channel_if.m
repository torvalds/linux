#-
# KOBJ
#
# Copyright (c) 2005-2009 Ariff Abdullah <ariff@FreeBSD.org>
# Portions Copyright (c) Ryan Beasley <ryan.beasley@gmail.com> - GSoC 2006
# Copyright (c) 2000 Cameron Grant <cg@FreeBSD.org>
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

INTERFACE channel;

CODE {

	static int
	channel_noreset(kobj_t obj, void *data)
	{
		return 0;
	}

	static int
	channel_noresetdone(kobj_t obj, void *data)
	{
		return 0;
	}

	static int
	channel_nofree(kobj_t obj, void *data)
	{
		return 1;
	}

	static u_int32_t
	channel_nogetptr(kobj_t obj, void *data)
	{
		return 0;
	}

	static int
	channel_nonotify(kobj_t obj, void *data, u_int32_t changed)
	{
		return 0;
	}

	static int
	channel_nogetpeaks(kobj_t obj, void *data, int *lpeak, int *rpeak)
	{
		return -1;
	}

	static int
	channel_nogetrates(kobj_t obj, void *data, int **rates)
	{
		*rates = NULL;
		return 0;
	}

	static int
	channel_nosetfragments(kobj_t obj, void *data, u_int32_t blocksize, u_int32_t blockcount)
	{
		return ENOTSUP;
	}

	static struct pcmchan_matrix *
	channel_nogetmatrix(kobj_t obj, void *data, u_int32_t format)
	{
		format = feeder_matrix_default_format(format);
		return (feeder_matrix_format_map(format));
	}

	static int
	channel_nosetmatrix(kobj_t obj, void *data, struct pcmchan_matrix *m)
	{
		return ENOTSUP;
	}
};

METHOD void* init {
	kobj_t obj;
	void *devinfo;
	struct snd_dbuf *b;
	struct pcm_channel *c;
	int dir;
};

METHOD int free {
	kobj_t obj;
	void *data;
} DEFAULT channel_nofree;

METHOD int reset {
	kobj_t obj;
	void *data;
} DEFAULT channel_noreset;

METHOD int resetdone {
	kobj_t obj;
	void *data;
} DEFAULT channel_noresetdone;

METHOD int setformat {
	kobj_t obj;
	void *data;
	u_int32_t format;
};

METHOD u_int32_t setspeed {
	kobj_t obj;
	void *data;
	u_int32_t speed;
};

METHOD u_int32_t setblocksize {
	kobj_t obj;
	void *data;
	u_int32_t blocksize;
};

METHOD int setfragments {
	kobj_t obj;
	void *data;
	u_int32_t blocksize;
	u_int32_t blockcount;
} DEFAULT channel_nosetfragments;

METHOD int trigger {
	kobj_t obj;
	void *data;
	int go;
};

METHOD u_int32_t getptr {
	kobj_t obj;
	void *data;
} DEFAULT channel_nogetptr;

METHOD struct pcmchan_caps* getcaps {
	kobj_t obj;
	void *data;
};

METHOD int notify {
	kobj_t obj;
	void *data;
	u_int32_t changed;
} DEFAULT channel_nonotify;

/**
 * @brief Retrieve channel peak values
 *
 * This function is intended to obtain peak volume values for samples
 * played/recorded on a channel.  Values are on a linear scale from 0 to
 * 32767.  If the channel is monaural, a single value should be recorded
 * in @c lpeak.
 *
 * If hardware support isn't available, the SNDCTL_DSP_GET[IO]PEAKS
 * operation should return EINVAL.  However, we may opt to provide
 * software support that the user may toggle via sysctl/mixext.
 *
 * @param obj	standard kobj object (usually @c channel->methods)
 * @param data	driver-specific data (usually @c channel->devinfo)
 * @param lpeak	pointer to store left peak level
 * @param rpeak	pointer to store right peak level
 *
 * @retval -1	Error; usually operation isn't supported.
 * @retval 0	success
 */
METHOD int getpeaks {
	kobj_t obj;
	void *data;
	int *lpeak;
	int *rpeak;
} DEFAULT channel_nogetpeaks;

/**
 * @brief Retrieve discrete supported sample rates
 *
 * Some cards operate at fixed rates, and this call is intended to retrieve
 * those rates primarily for when in-kernel rate adjustment is undesirable
 * (e.g., application wants direct DMA access after setting a channel to run
 * "uncooked").
 *
 * The parameter @c rates is a double pointer which will be reset to
 * point to an array of supported sample rates.  The number of elements
 * in the array is returned to the caller.
 *
 * @param obj	standard kobj object (usually @c channel->methods)
 * @param data	driver-specific data (usually @c channel->devinfo)
 * @param rates	rate array pointer
 *
 * @return Number of rates in the array
 */
METHOD int getrates {
	kobj_t obj;
	void *data;
	int **rates;
} DEFAULT channel_nogetrates;

METHOD struct pcmchan_matrix * getmatrix {
	kobj_t obj;
	void *data;
	u_int32_t format;
} DEFAULT channel_nogetmatrix;

METHOD int setmatrix {
	kobj_t obj;
	void *data;
	struct pcmchan_matrix *m;
} DEFAULT channel_nosetmatrix;
