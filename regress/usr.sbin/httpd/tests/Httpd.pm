#	$OpenBSD: Httpd.pm,v 1.4 2021/10/05 17:40:08 anton Exp $

# Copyright (c) 2010-2015 Alexander Bluhm <bluhm@openbsd.org>
# Copyright (c) 2015 Reyk Floeter <reyk@openbsd.org>
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

use strict;
use warnings;

package Httpd;
use parent 'Proc';
use Carp;
use File::Basename;

sub new {
	my $class = shift;
	my %args = @_;
	$args{chroot} ||= ".";
	$args{docroot} ||= "htdocs";
	$args{logfile} ||= $args{chroot}."/httpd.log";
	$args{up} ||= $args{dryrun} || "server_launch: ";
	$args{down} ||= $args{dryrun} ? "httpd.conf:" : "parent terminating";
	$args{func} = sub { Carp::confess "$class func may not be called" };
	$args{conffile} ||= "httpd.conf";
	my $self = Proc::new($class, %args);
	ref($self->{http}) eq 'ARRAY'
	    or $self->{http} = [ split("\n", $self->{http} || "") ];
	$self->{listenaddr}
	    or croak "$class listen addr not given";
	$self->{listenport}
	    or croak "$class listen port not given";

	my $test = basename($self->{testfile} || "");
	# tls does not allow a too long session id, so truncate it
	substr($test, 25, length($test) - 25, "") if length($test) > 25;
	open(my $fh, '>', $self->{conffile})
	    or die ref($self), " conf file $self->{conffile} create failed: $!";

	# substitute variables in config file
	my $curdir = dirname($0) || ".";
	my $connectport = $self->{connectport};
	my $connectaddr = $self->{connectaddr};
	my $listenaddr = $self->{listenaddr};
	my $listenport = $self->{listenport};

	print $fh "prefork 1\n";  # only crashes of first child are observed
	print $fh "chroot \"".$args{docroot}."\"\n";
	print $fh "logdir \"".$args{chroot}."\"\n";

	my @http = @{$self->{http}};
	print $fh  "server \"www.$test.local\" {";
	my $tls = $self->{listentls} ? "tls " : "";
	print $fh  "\n\tlisten on $self->{listenaddr} ".
	    "${tls}port $self->{listenport}" unless grep { /^listen / } @http;
	# substitute variables in config file
	foreach (@http) {
		s/(\$[a-z]+)/$1/eeg;
	}
	print $fh  map { "\n\t$_" } @http;
	if ($self->{listentls}) {
	    print $fh "\n";
	    print $fh "\ttls certificate \"".$args{chroot}."/server.crt\"\n";
	    print $fh "\ttls key \"".$args{chroot}."/server.key\"";
	    $self->{verifytls}
		and print $fh "\n\ttls client ca \"".$args{chroot}."/ca.crt\"";
	}
	print $fh "\n\troot \"/\"";
	print $fh "\n\tlog style combined";
	print $fh  "\n}\n";

	return $self;
}

sub child {
	my $self = shift;
	my @sudo = $ENV{SUDO} ? split(' ', $ENV{SUDO}) : ();
	my @ktrace = $ENV{KTRACE} ? ($ENV{KTRACE}, "-i") : ();
	my $httpd = $ENV{HTTPD} ? $ENV{HTTPD} : "httpd";
	my @cmd = (@sudo, @ktrace, $httpd, "-dvv", "-f", $self->{conffile});
	print STDERR "execute: @cmd\n";
	exec @cmd;
	die ref($self), " exec '@cmd' failed: $!";
}

1;
