#!/usr/bin/perl
# $OpenBSD: run.pl,v 1.1 2005/04/08 17:12:49 cloder Exp $
# $EOM: run.pl,v 1.2 1999/08/05 22:42:42 niklas Exp $

#
# Copyright (c) 2004 Niklas Hallqvist.  All rights reserved.
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

use strict;
require 5.002;
require 'sys/syscall.ph';
use Socket;
use Sys::Hostname;

my ($rfd, $tickfac, $myaddr, $myport, $hisaddr, $hisport, $proto, $bindaddr,
    $conaddr, $sec, $tick, $action, $template, $data, $next,
    $nfd, $pkt, $verbose);

$| = 1;

$verbose = 1;
$tickfac = 0.001;
$myaddr = gethostbyname ('127.0.0.1');
$myport = 1501;
    $hisaddr = inet_aton ('127.0.0.1');
$hisport = 1500;

$proto = getprotobyname ('udp');
$bindaddr = sockaddr_in ($myport, $myaddr);
socket (SOCKET, PF_INET, SOCK_DGRAM, $proto) || die "socket: $!";
bind (SOCKET, $bindaddr);
vec ($rfd, fileno SOCKET, 1) = 1;

$conaddr = sockaddr_in ($hisport, $hisaddr);

sub getsec
{
    my ($tv) = pack ("ll", 0, 0);
    my ($tz) = pack ("ii", 0, 0);
    syscall (&SYS_gettimeofday, $tv, $tz) && return undef;
    my ($sec, $usec) = unpack ("ll", $tv);
    $sec % 86400 + $usec / 1000000;
}

$sec = &getsec;
while (<>) {
    next if /^\s*#/o || /^\s*$/o;
    chop;
    ($tick, $action, $template, $data) = split ' ', $_, 4;
    while ($data =~ /\\$/o) {
	chop $data;
	$_ = <>;
	next if /^\s*#/o;
	chop;
	$data .= $_;
    }
    $data =~ s/\s//go;
    $data = pack $template, $data;
    $next = $sec + $tick * $tickfac;
    if ($action eq "send") {
	# Wait for the moment to come.
	print STDERR "waiting ", $next - $sec, " secs\n";
	select undef, undef, undef, $next - $sec
	    while ($sec = &getsec) < $next;
#	print $data;
	send SOCKET, $data, 0, $conaddr;
	print STDERR "sent ", unpack ("H*", $data), "\n" if $verbose;
    } elsif ($action eq "recv") {
	$sec = &getsec;
	printf (STDERR "waiting for data or the %.3f sec timeout\n",
		$next - $sec);
	$nfd = select $rfd, undef, undef, $next - $sec;
	if ($nfd) {
	    printf STDERR "got back after %.3f secs\n", &getsec - $sec
		if $verbose;
#	    sysread (STDIN, $pkt, 65536) if $nfd;
	    sysread (SOCKET, $pkt, 65536) if $nfd;
	    print STDERR "read ", unpack ("H*", $pkt), "\n" if $verbose;
	    print STDERR "cmp  ", unpack ("H*", $data), "\n" if $verbose;
	} else {
	    print STDERR "timed out\n" if $verbose;
	}
	die "mismatch\n" if $pkt ne $data;
    }
}
