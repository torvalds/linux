#	$OpenBSD: RSyslogd.pm,v 1.7 2019/09/10 22:35:07 bluhm Exp $

# Copyright (c) 2010-2014 Alexander Bluhm <bluhm@openbsd.org>
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

package RSyslogd;
use parent 'Proc';
use Carp;
use Cwd;

sub new {
	my $class = shift;
	my %args = @_;
	$args{logfile} ||= "rsyslogd.log";
	$args{up} ||= "calling (select|poll)";
	$args{down} ||= "Clean shutdown completed";
	$args{func} = sub { Carp::confess "$class func may not be called" };
	$args{conffile} ||= "rsyslogd.conf";
	$args{pidfile} ||= "rsyslogd.pid";
	$args{outfile} ||= "rsyslogd.out";
	my $self = Proc::new($class, %args);

	_make_abspath(\$self->{$_}) foreach (qw(conffile pidfile outfile));

	my ($listendomain, $listenproto, $listenaddr, $listenport);
	if (defined($self->{listendomain})) {
		$listendomain = $self->{listendomain}
		    or croak "$class listen domain not given";
		$listenproto = $self->{listenproto}
		    or croak "$class listen protocol not given";
		$listenaddr = $self->{listenaddr}
		    or croak "$class listen address not given";
		$listenport = $self->{listenport}
		    or croak "$class listen port not given";
	}
	my ($connectdomain, $connectproto, $connectaddr, $connectport);
	if (defined($self->{connectdomain})) {
		$connectdomain = $self->{connectdomain}
		    or croak "$class connect domain not given";
		$connectproto = $self->{connectproto}
		    or croak "$class connect protocol not given";
		$connectaddr = $self->{connectaddr}
		    or croak "$class connect address not given";
		$connectport = $self->{connectport}
		    or croak "$class connect port not given";
	}

	open(my $fh, '>', $self->{conffile})
	    or die ref($self), " create conf file $self->{conffile} failed: $!";
	if ($listendomain && $listenproto eq "udp") {
		print $fh "\$ModLoad imudp\n";
		print $fh "\$UDPServerRun $listenport\n";
	}
	if ($listendomain && $listenproto eq "tcp") {
		print $fh "\$ModLoad imtcp\n";
		print $fh "\$InputTCPServerRun $listenport\n";
	}
	if ($listendomain && $listenproto eq "tls") {
		print $fh "\$DefaultNetstreamDriver gtls\n";
		my %cert = (
		    CA   => "ca.crt",
		    Cert => "server.crt",
		    Key  => "server.key",
		);
		while(my ($k, $v) = each %cert) {
			_make_abspath(\$v);
			print $fh "\$DefaultNetstreamDriver${k}File $v\n";
		}
		print $fh "\$ModLoad imtcp\n";
		print $fh "\$InputTCPServerStreamDriverMode 1\n";
		print $fh "\$InputTCPServerStreamDriverAuthMode anon\n";
		print $fh "\$InputTCPServerRun $listenport\n";
	}
	if ($connectdomain && $connectproto eq "udp") {
		print $fh "*.*\t\@$connectaddr:$connectport\n";
	}
	if ($connectdomain && $connectproto eq "tcp") {
		print $fh "*.*\t\@\@$connectaddr:$connectport\n";
	}
	if ($connectdomain && $connectproto eq "tls") {
		print $fh "\$DefaultNetstreamDriver gtls\n";
		my %cert = (
		    CA   => "127.0.0.1.crt",
		);
		while(my ($k, $v) = each %cert) {
			_make_abspath(\$v);
			print $fh "\$DefaultNetstreamDriver${k}File $v\n";
		}
		print $fh "\$ActionSendStreamDriverAuthMode x509/name\n";
		print $fh "\$ActionSendStreamDriverPermittedPeer 127.0.0.1\n";
		print $fh "\$ActionSendStreamDriverMode 1\n";
		print $fh "*.*\t\@\@$connectaddr:$connectport\n";
	}
	print $fh "*.*\t$self->{outfile}\n";
	print $fh $self->{conf} if $self->{conf};
	close $fh;

	unlink($self->{outfile});
	return $self;
}

sub child {
	my $self = shift;
	my @sudo = $ENV{SUDO} ? $ENV{SUDO} : "env";

	my @pkill = (@sudo, "pkill", "-KILL", "-x", "rsyslogd");
	my @pgrep = ("pgrep", "-x", "rsyslogd");
	system(@pkill) && $? != 256
	    and die ref($self), " system '@pkill' failed: $?";
	while ($? == 0) {
		print STDERR "rsyslogd still running\n";
		system(@pgrep) && $? != 256
		    and die ref($self), " system '@pgrep' failed: $?";
	}
	print STDERR "rsyslogd not running\n";

	my @cmd = ("rsyslogd", "-dn", "-f", $self->{conffile},
	    "-i", $self->{pidfile});
	print STDERR "execute: @cmd\n";
	exec @cmd;
	die ref($self), " exec '@cmd' failed: $!";
}

sub _make_abspath {
	my $file = ref($_[0]) ? ${$_[0]} : $_[0];
	if (substr($file, 0, 1) ne "/") {
		$file = getcwd(). "/". $file;
		${$_[0]} = $file if ref($_[0]);
	}
	return $file;
}

sub down {
	my $self = shift;

	$self->kill();
	return Proc::down($self);
}

1;
