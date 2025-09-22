#!/usr/bin/perl
#	$OpenBSD: testfd.pl,v 1.2 2014/07/11 22:28:51 bluhm Exp $

# Copyright (c) 2014 Alexander Bluhm <bluhm@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# Test file descriptor passing with the Perl xs module PassFd.

use strict;
use warnings;
use Socket;
use Fcntl qw(F_SETFD FD_CLOEXEC);
use PassFd qw(sendfd recvfd);
use POSIX qw(_exit);

socketpair(my $parent, my $child, AF_UNIX, SOCK_STREAM, PF_UNSPEC)
    or die "socketpair failed: $!";
$child->fcntl(F_SETFD, 0)
    or die "fcntl setfd failed: $!";

defined(my $pid = fork())
    or die "fork failed: $!";
unless ($pid) {
    # child process
    close($parent)
	or do { warn "Close parent socket failed: $!"; _exit(1); };
    open(my $fd, '<', $0)
	or do { warn "Open $0 failed: $!"; _exit(1); };
    sendfd($child, $fd)
	or do { warn "Sendfd failed: $!"; _exit(1); };
    _exit(0);
}
# parent process
close($child)
    or die "Close child socket failed: $!";
my $fd = recvfd($parent)
    or die "Recvfd failed: $!";
wait()
    or die "Wait failed: $!";
$? == 0
    or die "Child process failed: $?";

defined (my $line = <$fd>)
    or die "Read from fd failed: $!";
$line =~ /perl/
    or die "Could not read perl from fd: $line\n";

warn "Passing fd successful\n";
exit 0;
