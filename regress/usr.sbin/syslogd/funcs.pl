#	$OpenBSD: funcs.pl,v 1.41 2024/06/14 15:12:57 bluhm Exp $

# Copyright (c) 2010-2021 Alexander Bluhm <bluhm@openbsd.org>
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
use Errno;
use List::Util qw(first);
use Socket;
use Socket6;
use Sys::Syslog qw(:standard :extended :macros);
use Time::HiRes 'sleep';
use IO::Socket;
use IO::Socket::SSL;

my $firstlog = "syslogd regress test first message";
my $secondlog = "syslogd regress test second message";
my $thirdlog = "syslogd regress test third message";
my $testlog = "syslogd regress test log message";
my $downlog = "syslogd regress client shutdown";
my $charlog = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

sub find_ports {
	my %args = @_;
	my $num    = delete $args{num}    // 1;
	my $domain = delete $args{domain} // AF_INET;
	my $addr   = delete $args{addr}   // "127.0.0.1";
	my $proto  = delete $args{proto}  // "udp";
	$proto = "tcp" if $proto eq "tls";

	my @sockets = (1..$num);
	foreach my $s (@sockets) {
		$s = IO::Socket::IP->new(
		    Domain    => $domain,
		    LocalAddr => $addr,
		    Proto     => $proto,
		) or die "find_ports: create and bind socket failed: $!";
	}
	my @ports = map { $_->sockport() } @sockets;

	return wantarray ? @ports : $ports[0];
}

########################################################################
# Client funcs
########################################################################

sub write_log {
	my $self = shift;

	write_message($self, $testlog);
	IO::Handle::flush(\*STDOUT);
	${$self->{syslogd}}->loggrep($testlog, 2);
	write_shutdown($self);
}

sub write_between2logs {
	my $self = shift;
	my $func = shift;

	write_message($self, $firstlog);
	$func->($self, @_);
	write_message($self, $testlog);
	IO::Handle::flush(\*STDOUT);
	${$self->{syslogd}}->loggrep($testlog, 2);
	write_shutdown($self);
}

sub write_message {
	my $self = shift;

	if (defined($self->{connectdomain})) {
		my $msg = join("", @_);
		if ($self->{connectdomain} eq "sendsyslog") {
			my $flags = $self->{connect}{flags} || 0;
			sendsyslog($msg, $flags)
			    or die ref($self), " sendsyslog failed: $!";
		} elsif ($self->{connectproto} eq "udp") {
			# writing UDP packets works only with syswrite()
			defined(my $n = syswrite(STDOUT, $msg))
			    or die ref($self), " write log line failed: $!";
			$n == length($msg)
			    or die ref($self), " short UDP write";
		} else {
			print $msg;
			print "\n" if $self->{connectproto} =~ /^(tcp|tls)$/;
		}
		print STDERR "<<< $msg\n";
	} else {
		syslog(LOG_INFO, @_);
	}
}

sub sendsyslog {
	my $msg = shift;
	my $flags = shift;
	require 'sys/syscall.ph';
	return syscall(&SYS_sendsyslog, $msg, length($msg), $flags) != -1;
}

sub write_shutdown {
	my $self = shift;

	setlogsock("native")
	    or die ref($self), " setlogsock native failed: $!";
	syslog(LOG_NOTICE, $downlog);

	if (defined($self->{connectdomain}) &&
	    $self->{connectproto} eq "tls" &&
	    $self->{exit}) {
		# Due to missing handshakes TLS 1.3 cannot detect all
		# connection errors while writing.  Try to read.
		defined(read(STDIN, my $buf, 1))
		    or die ref($self), " error after shutdown: $!,$SSL_ERROR";
	}
}

sub write_lines {
	my $self = shift;
	my ($lines, $length) = @_;

	foreach (1..$lines) {
		write_chars($self, $length, " $_");
	}
}

sub write_lengths {
	my $self = shift;
	my ($lengths, $tail) = ref $_[0] ? @_ : [@_];

	write_chars($self, $lengths, $tail);
}

sub generate_chars {
	my ($len) = @_;

	my $msg = "";
	my $char = '0';
	for (my $i = 0; $i < $len; $i++) {
		$msg .= $char;
		if    ($char =~ /9/) { $char = 'A' }
		elsif ($char =~ /Z/) { $char = 'a' }
		elsif ($char =~ /z/) { $char = '0' }
		else                 { $char++ }
	}
	return $msg;
}

sub write_chars {
	my $self = shift;
	my ($length, $tail) = @_;

	foreach my $len (ref $length ? @$length : $length) {
		my $t = $tail // "";
		substr($t, 0, length($t) - $len, "")
		    if length($t) && length($t) > $len;
		my $msg = generate_chars($len - length($t));
		$msg .= $t if length($t);
		write_message($self, $msg);
		# if client is sending too fast, syslogd will not see everything
		sleep .01;
	}
}

sub write_unix {
	my $self = shift;
	my $path = shift || "/dev/log";
	my $id = shift // $path;

	my $u = IO::Socket::UNIX->new(
	    Type  => SOCK_DGRAM,
	    Peer => $path,
	) or die ref($self), " connect to $path unix socket failed: $!";
	my $msg = "id $id unix socket: $testlog";
	print $u $msg;
	print STDERR "<<< $msg\n";
}

sub write_tcp {
	my $self = shift;
	my $fh = shift || \*STDOUT;
	my $id = shift // $fh;

	my $msg = "id $id tcp socket: $testlog";
	print $fh "$msg\n";
	print STDERR "<<< $msg\n";
}

sub redo_connect {
	my $self = shift;
	my $func = shift;

	$func->($self, @_);
	if ($self->{cs}) {
		# wait for possible icmp errors, port is open
		sleep .1;
		close(delete $self->{cs})
		    or die ref($self), " close failed: $!";
		delete $self->{ts};
	}
	if (my $redo = shift @{$self->{redo}}) {
		if (my $connect = $redo->{connect}) {
			delete $self->{logsock};
			$self->{connectdomain} = $connect->{domain};
			$self->{connectaddr}   = $connect->{addr};
			$self->{connectproto}  = $connect->{proto};
			$self->{connectport}   = $connect->{port};
		} elsif (my $logsock = $redo->{logsock}) {
			delete $self->{connectdomain};
			delete $self->{connectaddr};
			delete $self->{connectproto};
			delete $self->{connectport};
			$self->{logsock} = $logsock;
		} else {
			die ref($self), " no connect or logsock in redo";
		}
	} else {
		delete $self->{connectdomain};
		delete $self->{connectaddr};
		delete $self->{connectproto};
		delete $self->{connectport};
		$self->{logsock} = { type => "native" };
		setlogsock($self->{logsock})
		    or die ref($self), " setlogsock failed: $!";
		sleep .1;
		write_log($self);
		undef $self->{redo};
	}
}

########################################################################
# Server funcs
########################################################################

sub read_log {
	my $self = shift;

	read_message($self, $downlog);
}

sub read_between2logs {
	my $self = shift;
	my $func = shift;

	read_message($self, $firstlog);
	$func->($self, @_);
	read_message($self, $testlog);
	read_message($self, $downlog);
}

sub accept_between2logs {
	my $self = shift;
	my $func = shift;

	unless ($self->{redo}) {
		read_message($self, $firstlog);
		$func->($self, @_);
		$self->{redo} = 1;
	} else {
		$self->{redo} = 0;
		read_message($self, $testlog);
		read_message($self, $downlog);
	}
}

sub read_message {
	my $self = shift;
	my $regex = shift;

	local $_;
	for (;;) {
		if ($self->{listenproto} eq "udp") {
			# reading UDP packets works only with sysread()
			defined(my $n = sysread(STDIN, $_, 8194))
			    or die ref($self), " read log line failed: $!";
			last if $n == 0;
		} else {
			defined($_ = <STDIN>)
			    or last;
		}
		chomp;
		print STDERR ">>> $_\n";
		last if /$regex/;
	}
}

########################################################################
# Script funcs
########################################################################

sub get_testlog {
	return $testlog;
}

sub get_testgrep {
	return qr/$testlog\r*$/;
}

sub get_firstlog {
	return $firstlog;
}

sub get_secondlog {
	return $secondlog;
}

sub get_thirdlog {
	return $thirdlog;
}

sub get_charlog {
	# add a space so that we match at the beginning of the message
	return " $charlog";
}

sub get_between2loggrep {
	return (
	    qr/$firstlog/ => 1,
	    qr/$testlog/ => 1,
	);
}

sub get_downlog {
	return $downlog;
}

sub selector2config {
    my %s2m = @_;
    my $conf = "";
    my $i = 0;
    foreach my $sel (sort keys %s2m) {
	$conf .= "$sel\t\$objdir/file-$i.log\n";
	$i++;
    }
    return $conf;
}

sub selector2loggrep {
    my %s2m = @_;
    my %allmsg;
    @allmsg{map { @$_} values %s2m} = ();
    my @loggrep;
    foreach my $sel (sort keys %s2m) {
	my @m = @{$s2m{$sel}};
	my %msg;
	$msg{$_}++ foreach (@m);
	my %nomsg = %allmsg;
	delete @nomsg{@m};
	push @loggrep, {
	    (map { qr/: $_$/ => $msg{$_} } sort keys %msg),
	    (map { qr/: $_$/ => 0 } sort keys %nomsg),
	};
    }
    return @loggrep;
}

sub check_logs {
	my ($c, $r, $s, $m, %args) = @_;

	return if $args{nocheck};

	check_log($c, $r, $s, @$m);
	check_out($r, %args);
	check_fstat($c, $r, $s);
	check_ktrace($c, $r, $s);
	if (my $file = $s->{"outfile"}) {
		my $pattern = $s->{filegrep} || get_testgrep();
		check_pattern(ref $s, $file, $pattern, \&filegrep);
	}
	check_multifile(@{$args{multifile} || []});
}

sub compare($$) {
	local $_ = $_[1];
	if (/^\d+/) {
		return $_[0] == $_;
	} elsif (/^==(\d+)/) {
		return $_[0] == $1;
	} elsif (/^!=(\d+)/) {
		return $_[0] != $1;
	} elsif (/^>=(\d+)/) {
		return $_[0] >= $1;
	} elsif (/^<=(\d+)/) {
		return $_[0] <= $1;
	} elsif (/^~(\d+)/) {
		return $1 * 0.8 <= $_[0] && $_[0] <= $1 * 1.2;
	}
	die "bad compare operator: $_";
}

sub check_pattern {
	my ($name, $proc, $pattern, $func) = @_;

	$pattern = [ $pattern ] unless ref($pattern) eq 'ARRAY';
	foreach my $pat (@$pattern) {
		if (ref($pat) eq 'HASH') {
			foreach my $re (sort keys %$pat) {
				my $num = $pat->{$re};
				my @matches = $func->($proc, $re);
				compare(@matches, $num)
				    or die "$name matches '@matches': ",
				    "'$re' => $num";
			}
		} else {
			$func->($proc, $pat)
			    or die "$name log missing pattern: $pat";
		}
	}
}

sub check_log {
	foreach my $proc (@_) {
		next unless $proc && !$proc->{nocheck};
		my $pattern = $proc->{loggrep} || get_testgrep();
		check_pattern(ref $proc, $proc, $pattern, \&loggrep);
	}
}

sub loggrep {
	my ($proc, $pattern) = @_;

	return $proc->loggrep($pattern);
}

sub check_out {
	my ($r, %args) = @_;

	unless ($args{pipe}{nocheck}) {
		$r->loggrep("bytes transferred", 1) or sleep 1;
	}
	foreach my $dev (qw(console user)) {
		$args{$dev}{nocheck} ||= $args{tty}{nocheck};
		$args{$dev}{loggrep} ||= $args{tty}{loggrep};
		next if $args{$dev}{nocheck};
		my $ctl = $r->{"ctl$dev"};
		close($ctl);
		my $file = $r->{"out$dev"};
		open(my $fh, '<', $file)
		    or die "Open file $file for reading failed: $!";
		grep { /^logout/ or /^console .* off/ } <$fh> or sleep 1;
		close($fh);
	}

	foreach my $name (qw(file pipe console user)) {
		next if $args{$name}{nocheck};
		my $file = $r->{"out$name"} or die;
		my $pattern = $args{$name}{loggrep} || get_testgrep();
		check_pattern($name, $file, $pattern, \&filegrep);
	}
}

sub check_fstat {
	foreach my $proc (@_) {
		my $pattern = $proc && $proc->{fstat} or next;
		my $file = $proc->{fstatfile} or die;
		check_pattern("fstat", $file, $pattern, \&filegrep);
	}
}

sub filegrep {
	my ($file, $pattern) = @_;

	open(my $fh, '<', $file)
	    or die "Open file $file for reading failed: $!";
	return wantarray ?
	    grep { /$pattern/ } <$fh> : first { /$pattern/ } <$fh>;
}

sub check_ktrace {
	foreach my $proc (@_) {
		my $pattern = $proc && $proc->{ktrace} or next;
		my $file = $proc->{ktracefile} or die;
		check_pattern("ktrace", $file, $pattern, \&kdumpgrep);
	}
}

sub kdumpgrep {
	my ($file, $pattern) = @_;

	my @sudo = ! -r $file && $ENV{SUDO} ? $ENV{SUDO} : ();
	my @cmd = (@sudo, "kdump", "-f", $file);
	open(my $fh, '-|', @cmd)
	    or die "Open pipe from '@cmd' failed: $!";
	my @matches = grep { /$pattern/ } <$fh>;
	close($fh) or die $! ?
	    "Close pipe from '@cmd' failed: $!" :
	    "Command '@cmd' failed: $?";
	return wantarray ? @matches : $matches[0];
}

sub create_multifile {
	for (my $i = 0; $i < @_; $i++) {
		my $file = "file-$i.log";
		open(my $fh, '>', $file)
		    or die "Create $file failed: $!";
	}
}

sub check_multifile {
	for (my $i = 0; $i < @_; $i++) {
		my $file = "file-$i.log";
		my $pattern = $_[$i]{loggrep} or die;
		check_pattern("multifile $i", $file, $pattern, \&filegrep);
	}
}

1;
