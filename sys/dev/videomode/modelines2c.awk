#! /usr/bin/awk -f
#	$NetBSD: modelines2c.awk,v 1.4 2006/10/26 23:19:50 bjh21 Exp $
#	$FreeBSD$
#
# Copyright (c) 2006 Itronix Inc.
# All rights reserved.
#
# Written by Garrett D'Amore for Itronix Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of Itronix Inc. may not be used to endorse
#    or promote products derived from this software without specific
#    prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY ITRONIX INC. ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ITRONIX INC. BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#

BEGIN {
	nmodes = 0;
}

NR == 1 {
	split($0,v,"$");

	VERSION=v[2];

	printf("/*\t$NetBSD" "$\t*/\n\n");
	printf("/*\n") ;
	printf(" * THIS FILE AUTOMATICALLY GENERATED.  DO NOT EDIT.\n");
	printf(" *\n");
	printf(" * generated from:\n");
	printf(" *\t%s\n", VERSION);
	printf(" */\n\n");

	printf("#include <sys/cdefs.h>\n");
	printf("__KERNEL_RCSID(0, \"$NetBSD" "$\");\n\n");

	printf("#include <dev/videomode/videomode.h>\n\n");

	printf("/*\n");
	printf(" * These macros help the modelines below fit on one line.\n");
	printf(" */\n");
	printf("#define HP VID_PHSYNC\n");
	printf("#define HN VID_NHSYNC\n");
	printf("#define VP VID_PVSYNC\n");
	printf("#define VN VID_NVSYNC\n");
	printf("#define I VID_INTERLACE\n");
	printf("#define DS VID_DBLSCAN\n");
	printf("\n");

	printf("#define M(nm,hr,vr,clk,hs,he,ht,vs,ve,vt,f) \\\n");
	printf("\t{ clk, hr, hs, he, ht, vr, vs, ve, vt, f, nm } \n\n");

	printf("const struct videomode videomode_list[] = {\n");

	next
}

(/^ModeLine/) {
	dotclock =   $3;

	hdisplay =   $4;
	hsyncstart = $5;
	hsyncend =   $6;
	htotal =     $7;
	
	vdisplay =   $8;
	vsyncstart = $9;
	vsyncend =   $10;
	vtotal =     $11;

	macro =      "MODE";
	iflag =      "";
	iflags =     "";
	hflags =     "HP";
	vflags =     "VP";

	if ($12 ~ "^-")
		hflags = "HN";

	if ($13 ~ "^-")
		vflags = "VN";

	ifactor=1.0;
	if ($14 ~ "[Ii][Nn][Tt][Ee][Rr][Ll][Aa][Cc][Ee]") {
		iflag = "i";
		iflags = "|I";
		ifactor = 2.0;
	}

	# We truncate the vrefresh figure, but some mode descriptions rely
	# on rounding, so we can't win here.  Adding an additional .1
	# compensates to some extent.

	hrefresh= (dotclock * 1000000) / htotal;
	vrefresh= int(((hrefresh * ifactor) / vtotal) + .1);

	modestr = sprintf("%dx%dx%d%s", hdisplay, vdisplay, vrefresh, iflag);

#	printf("/* %dx%d%s refresh %d Hz, hsync %d kHz */\n",
#	    hdisplay, vdisplay, iflag, vrefresh, hrefresh/1000);
	printf("M(\"%s\",%d,%d,%d,%d,%d,%d,%d,%d,%d,%s),\n",
	    modestr,
	    hdisplay, vdisplay, dotclock * 1000,
	    hsyncstart, hsyncend, htotal,
	    vsyncstart, vsyncend, vtotal, hflags "|" vflags iflags);

	modestr = sprintf("%dx%dx%d%s",
	    hdisplay/2 , vdisplay/2, vrefresh, iflag);

	dmodes[nmodes]=sprintf("M(\"%s\",%d,%d,%d,%d,%d,%d,%d,%d,%d,%s),",
	    modestr,
	    hdisplay/2, vdisplay/2, dotclock * 1000 / 2,
	    hsyncstart/2, hsyncend/2, htotal/2,
	    vsyncstart/2, vsyncend/2, vtotal/2,
	    hflags "|" vflags "|DS" iflags);

	nmodes = nmodes + 1

}

END {

	printf("\n/* Derived Double Scan Modes */\n\n");

	for ( i = 0; i < nmodes; i++ )
	{
		print dmodes[i]; 
	}

	printf("};\n\n");
	printf("const int videomode_count = %d;\n", nmodes);
}
