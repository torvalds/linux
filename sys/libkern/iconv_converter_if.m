#-
# Copyright (c) 2000-2001 Boris Popov
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

#include <sys/iconv.h>

INTERFACE iconv_converter;

STATICMETHOD int open {
	struct iconv_converter_class *dcp;
	struct iconv_cspair *cspto;
	struct iconv_cspair *cspfrom;
	void **hpp;
};

METHOD int close {
	void *handle;
};

METHOD int conv {
	void *handle;
	const char **inbuf;
        size_t *inbytesleft;
	char **outbuf;
	size_t *outbytesleft;
	int convchar;
	int casetype;
};

STATICMETHOD int init {
	struct iconv_converter_class *dcp;
} DEFAULT iconv_converter_initstub;

STATICMETHOD int done {
	struct iconv_converter_class *dcp;
} DEFAULT iconv_converter_donestub;

STATICMETHOD const char * name {
	struct iconv_converter_class *dcp;
};

METHOD int tolower {
	void *handle;
	int c;
} DEFAULT iconv_converter_tolowerstub;

METHOD int toupper {
	void *handle;
	int c;
} DEFAULT iconv_converter_tolowerstub;
