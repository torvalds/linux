#!/bin/awk
# $FreeBSD$

#-
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2012 M. Warner Losh.
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

#
# Generate FreeBSD's board ID's defines from Linux's
# arm board list.
#
# You can grab a new copy any time with:
# fetch -o sys/arm/conf/mach-types http://www.arm.linux.org.uk/developer/machines/download.php
#
BEGIN	{ nr = 0; boardid[nr] = "ARM_BOARD_ID_NONE"; num[nr++] = 0; }
/^#/	{ next; }
/^[     ]*$/ { next; }

NF == 4 {
    boardid[nr] = "ARM_BOARD_ID_"$3;
    num[nr] = $4;
    nr++
}

END	{
    printf("/* Arm board ID file generated automatically from Linux's mach-types file. */\n\n");
    printf("#ifndef _SYS_ARM_ARM_BOARDID_H\n");
    printf("#define _SYS_ARM_ARM_BOARDID_H\n\n");
    for (i = 0; i < nr; i++) {
        printf("#define %-30s %d\n", boardid[i], num[i]);
    }
    printf("\n#endif /* _SYS_ARM_ARM_BOARDID_H */\n");
}

