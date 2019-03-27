#	$FreeBSD$
#
# Copyright (c) 2018		Conrad Meyer <cem@FreeBSD.org>
# Copyright (c) 2002, 2003	Sam Leffler, Errno Consulting
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

PROGS=	cryptocheck cryptotest cryptokeytest cryptostats \
	ubsecstats hifnstats ipsecstats safestats
MAN=
BINDIR?=	/usr/local/bin

# cryptocheck: test symmetric crypto functions
LIBADD.cryptocheck+=	crypto util

# cryptokeytest: test asymmetric crypto functions
LIBADD.cryptokeytest+=	crypto

# cryptostats: dump statistics kept by the core crypto code
# ubsecstats: print statistics kept by the Broadcom driver
# hifnstats: print statistics kept by the HIFN driver
# safestats: statistics kept by the SafeNet driver
# ipsecstats: print statistics kept by fast ipsec

CLEANFILES+=	core a.out

.include <bsd.progs.mk>
