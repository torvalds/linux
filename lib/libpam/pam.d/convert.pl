#!/usr/bin/perl -w
#-
# SPDX-License-Identifier: BSD-3-Clause
#
# Copyright (c) 2001,2002 Networks Associates Technologies, Inc.
# All rights reserved.
#
# This software was developed for the FreeBSD Project by ThinkSec AS and
# NAI Labs, the Security Research Division of Network Associates, Inc.
# under DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the
# DARPA CHATS research program.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
# 3. The name of the author may not be used to endorse or promote
#    products derived from this software without specific prior written
#    permission.
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

use strict;
use Fcntl;
use vars qw(%SERVICES);

MAIN:{
    my $line;
    my $service;
    my $version;
    my $type;
    local *FILE;

    while (<>) {
	chomp();
	s/\s*$//;
	next unless m/^(\#*)(\w+)\s+(auth|account|session|password)\s+(\S.*)$/;
	$line = $1.$3;
	$line .= "\t" x ((16 - length($line) + 7) / 8);
	$line .= $4;
	push(@{$SERVICES{$2}->{$3}}, $line);
    }

    foreach $service (keys(%SERVICES)) {
	$version = '$' . 'FreeBSD' . '$';
	if (sysopen(FILE, $service, O_RDONLY)) {
		while (<FILE>) {
			next unless (m/(\$[F]reeBSD.*?\$)/);
			$version = $1;
			last;
		}
		close(FILE);
	}
	sysopen(FILE, $service, O_RDWR|O_CREAT|O_TRUNC)
	    or die("$service: $!\n");
	print(FILE "#\n");
	print(FILE "# $version\n");
	print(FILE "#\n");
	print(FILE "# PAM configuration for the \"$service\" service\n");
	print(FILE "#\n");
	foreach $type (qw(auth account session password)) {
	    next unless exists($SERVICES{$service}->{$type});
	    print(FILE "\n");
	    print(FILE "# $type\n");
	    print(FILE join("\n", @{$SERVICES{$service}->{$type}}, ""));
	}
	close(FILE);
	warn("$service\n");
    }

    exit(0);
}
