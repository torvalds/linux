/*-
 * Copyright (c) 2001 Benno Rice
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/types.h>

#include <stand.h>

#include "libofw.h"
#include "openfirm.h"

struct ofw_mapping {
        vm_offset_t     va;
        int             len;
        vm_offset_t     pa;
        int             mode;
};

struct ofw_mapping2 {
        vm_offset_t     va;
        int             len;
        vm_offset_t     pa_hi;
        vm_offset_t     pa_lo;
        int             mode;
};

void
ofw_memmap(int acells)
{
	struct		ofw_mapping *mapptr;
	struct		ofw_mapping2 *mapptr2;
        phandle_t	mmup;
        int		nmapping, i;
        u_char		mappings[256 * sizeof(struct ofw_mapping2)];
        char		lbuf[80];

	mmup = OF_instance_to_package(mmu);

	bzero(mappings, sizeof(mappings));

	nmapping = OF_getprop(mmup, "translations", mappings, sizeof(mappings));
	if (nmapping == -1) {
		printf("Could not get memory map (%d)\n",
		    nmapping);
		return;
	}

	pager_open();
	if (acells == 1) {
		nmapping /= sizeof(struct ofw_mapping);
		mapptr = (struct ofw_mapping *) mappings;

		printf("%17s\t%17s\t%8s\t%6s\n", "Virtual Range",
		    "Physical Range", "#Pages", "Mode");

		for (i = 0; i < nmapping; i++) {
			sprintf(lbuf, "%08x-%08x\t%08x-%08x\t%8d\t%6x\n",
				mapptr[i].va,
				mapptr[i].va + mapptr[i].len,
				mapptr[i].pa,
				mapptr[i].pa + mapptr[i].len,
				mapptr[i].len / 0x1000,
				mapptr[i].mode);
			if (pager_output(lbuf))
				break;
		}
	} else {
		nmapping /= sizeof(struct ofw_mapping2);
		mapptr2 = (struct ofw_mapping2 *) mappings;

		printf("%17s\t%17s\t%8s\t%6s\n", "Virtual Range",
		       "Physical Range", "#Pages", "Mode");

		for (i = 0; i < nmapping; i++) {
			sprintf(lbuf, "%08x-%08x\t%08x-%08x\t%8d\t%6x\n",
				mapptr2[i].va,
				mapptr2[i].va + mapptr2[i].len,
				mapptr2[i].pa_lo,
				mapptr2[i].pa_lo + mapptr2[i].len,
				mapptr2[i].len / 0x1000,
				mapptr2[i].mode);
			if (pager_output(lbuf))
				break;
		}
	}
	pager_close();
}

