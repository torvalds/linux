#	$OpenBSD: Tap.pm,v 1.1 2016/09/28 12:40:35 bluhm Exp $

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

# Encapsulate tap interface handling into separate module.

use strict;
use warnings;

package Tap;
use parent 'Exporter';
our @EXPORT_OK = qw(opentap);

use Carp;
use Fcntl;
use File::Basename;
use POSIX qw(_exit);
use PassFd 'recvfd';
use Socket;

sub opentap {
    my ($tap_number) = @_;
    my $tap_device = "/dev/tap$tap_number";

    if ($> == 0) {
	sysopen(my $tap, $tap_device, O_RDWR)
	    or croak "Open $tap_device failed: $!";
	return $tap;
    }

    if (!$ENV{SUDO}) {
	die "To open the device $tap_device you must run as root or\n".
	    "set the SUDO environment variable and allow closefrom_override.\n";
    }

    my $opentap;
    my $curdir = dirname($0) || ".";
    if (-x "$curdir/opentap") {
	$opentap = "$curdir/opentap";
    } elsif (-x "./opentap") {
	$opentap = "./opentap";
    } else {
	die "To open the device $tap_device the tool opentap is needed.\n".
	    "Executable opentap not found in $curdir or current directory.\n";
    }

    socketpair(my $parent, my $child, AF_UNIX, SOCK_STREAM, PF_UNSPEC)
	or croak "Socketpair failed: $!";
    $child->fcntl(F_SETFD, 0)
	or croak "Fcntl setfd failed: $!";

    defined(my $pid = fork())
	or croak "Fork failed: $!";

    unless ($pid) {
	# child process
	close($parent) or do {
	    warn "Close parent socket failed: $!";
	    _exit(3);
	};
	my @cmd = ($ENV{SUDO}, '-C', $child->fileno()+1, $opentap,
	    $child->fileno(), $tap_number);
	exec(@cmd);
	warn "Exec '@cmd' failed: $!";
	_exit(3);
    }

    # parent process
    close($child)
	or croak "Close child socket failed: $!";
    my $tap = recvfd($parent)
	or croak "Recvfd failed: $!";
    wait()
	or croak "Wait failed: $!";
    $? == 0
	or croak "Child process failed: $?";

    return $tap;
}

1;
