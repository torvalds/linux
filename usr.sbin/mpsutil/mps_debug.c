/*-
 * Copyright (c) 2018 Netflix, Inc.
 * Written by: Scott Long <scottl@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$FreeBSD$");

#include <sys/param.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <err.h>
#include <libutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mpsutil.h"

MPS_TABLE(top, debug);

struct mps_dumpreq_hdr {
	uint32_t	smid;
	uint32_t	state;
	uint32_t	numframes;
	uint32_t	deschi;
	uint32_t	desclo;
};

static int find_sgl(char *);
static void print_sgl(char *, int, int);

#define MPS_FRAME_LEN 128

static int
debug_dumpreqs(int ac, char **av)
{
	struct mps_dumpreq_hdr *hdr;
	char *buf, sysctlbuf[128];
	size_t len;
	int numframes, error, offset;

	len = 0;
	buf = NULL;
	snprintf(sysctlbuf, sizeof(sysctlbuf), "dev.%s.%d.dump_reqs",
	    is_mps ? "mps" : "mpr", mps_unit);

	error = sysctlbyname(sysctlbuf, NULL, &len, NULL, 0);
	if (error)
		return (error);

	if (len == 0)
		return (0);

	buf = malloc(len);
	if (buf == NULL)
		return (ENOMEM);

	error = sysctlbyname(sysctlbuf, buf, &len, NULL, 0);
	if (error) {
		printf("len= %zd, error= %d errno= %d\n", len, error, errno);
		return (error);
	}

	while (len >= MPS_FRAME_LEN) {
		hdr = (struct mps_dumpreq_hdr *)buf;
		numframes = hdr->numframes;

		printf("SMID= %d state= %#x numframes= %d desc.hi= %#08x "
		    "desc.lo= %#08x\n", hdr->smid, hdr->state,
		    hdr->numframes, hdr->deschi, hdr->desclo);

		buf += sizeof(struct mps_dumpreq_hdr);
		len -= sizeof(struct mps_dumpreq_hdr);

		if ((offset = find_sgl(buf)) != -1)
			print_sgl(buf, offset, numframes);

		buf += MPS_FRAME_LEN * numframes;
		len -= MPS_FRAME_LEN * numframes;
	}

	return (error);
}

static int
find_sgl(char *buf)
{
	MPI2_REQUEST_HEADER *req;
	MPI2_SCSI_IO_REQUEST *scsi;
	int offset = 0;

	req = (MPI2_REQUEST_HEADER *)buf;

	switch (req->Function) {
	case MPI2_FUNCTION_SCSI_IO_REQUEST:
		scsi = (MPI2_SCSI_IO_REQUEST *)buf;
		offset = scsi->SGLOffset0;
		break;
	default:
		offset = -1;
	}

	return (offset);
}

#define SGL_FLAGS "\10LastElement\7EndOfBuffer\4Local\3Host2IOC\2Addr64\1EndOfList"

static void
print_sgl(char *buf, int offset, int numframes)
{
	MPI2_SGE_SIMPLE64 *sge;
	MPI2_SGE_CHAIN_UNION *sgc;
	MPI2_REQUEST_HEADER *req;
	u_int i = 0, flags;
	char *frame, tmpbuf[128];

	req = (MPI2_REQUEST_HEADER *)buf;
	frame = (char *)buf;
	sge = (MPI2_SGE_SIMPLE64 *)&frame[offset * 4];
	printf("SGL for command\n");

	hexdump(frame, MPS_FRAME_LEN, NULL, 0);
	while (frame != NULL) {
		flags = sge->FlagsLength >> MPI2_SGE_FLAGS_SHIFT;
		bzero(tmpbuf, sizeof(tmpbuf));
		mps_parse_flags(flags, SGL_FLAGS, tmpbuf, sizeof(tmpbuf));
		printf("seg%d flags=%x %s len= 0x%06x addr=0x%016jx\n", i,
		    flags, tmpbuf, sge->FlagsLength & 0xffffff,
		    mps_to_u64(&sge->Address));
		if (flags & (MPI2_SGE_FLAGS_END_OF_LIST |
		    MPI2_SGE_FLAGS_END_OF_BUFFER))
			break;
		sge++;
		i++;
		if (flags & MPI2_SGE_FLAGS_LAST_ELEMENT) {
			sgc = (MPI2_SGE_CHAIN_UNION *)sge;
			if ((sgc->Flags & MPI2_SGE_FLAGS_CHAIN_ELEMENT) == 0) {
				printf("Invalid chain element\n");
				break;
			}
			bzero(tmpbuf, sizeof(tmpbuf));
			mps_parse_flags(sgc->Flags, SGL_FLAGS, tmpbuf,
			    sizeof(tmpbuf));
			if (sgc->Flags & MPI2_SGE_FLAGS_64_BIT_ADDRESSING)
				printf("chain64 flags=0x%x %s len=0x%x "
				    "Offset=0x%x addr=0x%016jx\n", sgc->Flags,
				    tmpbuf, sgc->Length, sgc->NextChainOffset,
				    mps_to_u64(&sgc->u.Address64));
			else
				printf("chain32 flags=0x%x %s len=0x%x "
				    "Offset=0x%x addr=0x%08x\n", sgc->Flags,
				    tmpbuf, sgc->Length, sgc->NextChainOffset,
				    sgc->u.Address32);
			if (--numframes <= 0)
				break;
			frame += MPS_FRAME_LEN;
			sge = (MPI2_SGE_SIMPLE64 *)frame;
			hexdump(frame, MPS_FRAME_LEN, NULL, 0);
		}
	}
}

MPS_COMMAND(debug, dumpreqs, debug_dumpreqs, "", "Dump the active request queue")
