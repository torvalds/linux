#	$OpenBSD: funcs.pl,v 1.10 2024/06/14 15:12:57 bluhm Exp $

# Copyright (c) 2010-2017 Alexander Bluhm <bluhm@openbsd.org>
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
use Digest::MD5;
use IO::Socket qw(sockatmark);
use Socket;
use Time::HiRes qw(time alarm sleep);
use BSD::Socket::Splice qw(setsplice getsplice geterror);

########################################################################
# Client funcs
########################################################################

sub write_stream {
	my $self = shift;
	my $len = shift // $self->{len} // 251;
	my $sleep = $self->{sleep};

	my $ctx = Digest::MD5->new();
	my $char = '0';
	for (my $i = 1; $i < $len; $i++) {
		$ctx->add($char);
		print $char
		    or die ref($self), " print failed: $!";
		if ($char =~ /9/)     { $char = 'A' }
		elsif ($char =~ /Z/)  { $char = 'a' }
		elsif ($char =~ /z/)  { $char = "\n" }
		elsif ($char =~ /\n/) { print STDERR "."; $char = '0' }
		else                  { $char++ }
		if ($self->{sleep}) {
			IO::Handle::flush(\*STDOUT);
			sleep $self->{sleep};
		}
	}
	if ($len) {
		$ctx->add("\n");
		print "\n"
		    or die ref($self), " print failed: $!";
		print STDERR ".\n";
	}
	IO::Handle::flush(\*STDOUT);

	print STDERR "LEN: $len\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub write_oob {
	my $self = shift;
	my $len = shift // $self->{len} // 251;

	my $ctx = Digest::MD5->new();
	my $msg = "";
	my $char = '0';
	for (my $i = 1; $i < $len; $i++) {
		$msg .= $char;
		if ($char =~ /9/) {
			$ctx->add("[$char]");
			defined(send(STDOUT, $msg, MSG_OOB))
			    or die ref($self), " send OOB failed: $!";
			# If tcp urgent data is sent too fast,
			# it may get overwritten and lost.
			sleep .1;
			$msg = "";
			$char = 'A';
		} elsif ($char =~ /Z/) {
			$ctx->add($char);
			$char = 'a';
		} elsif ($char =~ /z/) {
			$ctx->add($char);
			$char = "\n";
		} elsif ($char =~ /\n/) {
			$ctx->add($char);
			defined(send(STDOUT, $msg, 0))
			    or die ref($self), " send failed: $!";
			print STDERR ".";
			$msg = "";
			$char = '0';
		} else {
			$ctx->add($char);
			$char++;
		}
	}
	if ($len) {
		$msg .= "\n";
		$ctx->add("\n");
		send(STDOUT, $msg, 0)
		    or die ref($self), " send failed: $!";
		print STDERR ".\n";
	}
	IO::Handle::flush(\*STDOUT);

	print STDERR "LEN: $len\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub write_datagram {
	my $self = shift;
	my @lengths = @{$self->{lengths} || [ shift // $self->{len} // 251 ]};
	my $sleep = $self->{sleep};

	my $len = 0;
	my $ctx = Digest::MD5->new();
	my $char = '0';
	my @md5s;
	for (my $num = 0; $num < @lengths; $num++) {
		my $l = $lengths[$num];
		my $string = "";
		for (my $i = 1; $i < $l; $i++) {
			$ctx->add($char);
			$string .= $char;
			if    ($char =~ /9/)  { $char = 'A' }
			elsif ($char =~ /Z/)  { $char = 'a' }
			elsif ($char =~ /z/)  { $char = "\n" }
			elsif ($char =~ /\n/) { $char = '0' }
			else                  { $char++ }
		}
		if ($l) {
			$ctx->add("\n");
			$string .= "\n";
		}
		defined(my $write = syswrite(STDOUT, $string))
		    or die ref($self), " syswrite number $num failed: $!";
		$write == $l
		    or die ref($self), " syswrite length $l did write $write";
		$len += $write;
		print STDERR ".";
		sleep $self->{sleep} if $self->{sleep};
	}
	print STDERR "\n";

	print STDERR "LEN: $len\n";
	print STDERR "LENGTHS: @lengths\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub solingerout {
	my $self = shift;

	setsockopt(STDOUT, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
	    or die ref($self), " set linger out failed: $!";
}

########################################################################
# Relay funcs
########################################################################

sub relay_copy_stream {
	my $self = shift;
	my $max = $self->{max};
	my $idle = $self->{idle};
	my $size = $self->{size} || 8093;

	my $len = 0;
	while (1) {
		my $rin = my $win = my $ein = '';
		vec($rin, fileno(STDIN), 1) = 1;
		vec($ein, fileno(STDIN), 1) = 1 unless $self->{oobinline};
		defined(my $n = select($rin, undef, $ein, $idle))
		    or die ref($self), " select failed: $!";
		if ($idle && $n == 0) {
			print STDERR "\n";
			print STDERR "Timeout\n";
			last;
		}
		my $buf;
		my $atmark = sockatmark(\*STDIN)
		    or die ref($self), " sockatmark failed: $!";
		if ($atmark == 1) {
			if ($self->{oobinline}) {
				defined(recv(STDIN, $buf, 1, 0))
				    or die ref($self), " recv OOB failed: $!";
				$len += length($buf);
				defined(send(STDOUT, $buf, MSG_OOB))
				    or die ref($self), " send OOB failed: $!";
			} else {
				defined(recv(STDIN, $buf, 1, MSG_OOB)) ||
				    $!{EINVAL}
				    or die ref($self), " recv OOB failed: $!";
				print STDERR "OOB: $buf\n" if length($buf);
			}
		}
		if ($self->{nonblocking}) {
			vec($rin, fileno(STDIN), 1) = 1;
			select($rin, undef, undef, undef)
			    or die ref($self), " select read failed: $!";
		}
		my $read = sysread(STDIN, $buf,
		    $max && $max < $size ? $max : $size);
		next if !defined($read) && $!{EAGAIN};
		defined($read)
		    or die ref($self), " sysread at $len failed: $!";
		if ($read == 0) {
			print STDERR "\n";
			print STDERR "End\n";
			last;
		}
		print STDERR ".";
		if ($max && $len + $read > $max) {
			$read = $max - $len;
		}
		my $off = 0;
		while ($off < $read) {
			if ($self->{nonblocking}) {
				vec($win, fileno(STDOUT), 1) = 1;
				select(undef, $win, undef, undef)
				    or die ref($self),
				    " select write failed: $!";
			}
			my $write;
			# Unfortunately Perl installs signal handlers without
			# SA_RESTART.  Work around by restarting manually.
			do {
				$write = syswrite(STDOUT, $buf, $read - $off,
				    $off);
			} while (!defined($write) && $!{EINTR});
			defined($write) || $!{ETIMEDOUT}
			    or die ref($self), " syswrite at $len failed: $!";
			defined($write) or next;
			$off += $write;
			$len += $write;
		}
		if ($max && $len == $max) {
			print STDERR "\n";
			print STDERR "Big\n";
			print STDERR "Max\n";
			last;
		}
	}

	print STDERR "LEN: $len\n";
}

sub relay_copy_datagram {
	my $self = shift;
	my $max = $self->{max};
	my $idle = $self->{idle};
	my $size = $self->{size} || 2**16;

	my $len = 0;
	for (my $num = 0;; $num++) {
		my $rin = my $win = '';
		if ($idle) {
			vec($rin, fileno(STDIN), 1) = 1;
			defined(my $n = select($rin, undef, undef, $idle))
			    or die ref($self), " select idle failed: $!";
			if ($n == 0) {
				print STDERR "\n";
				print STDERR "Timeout\n";
				last;
			}
		} elsif ($self->{nonblocking}) {
			vec($rin, fileno(STDIN), 1) = 1;
			select($rin, undef, undef, undef)
			    or die ref($self), " select read failed: $!";
		}
		defined(my $read = sysread(STDIN, my $buf, $size))
		    or die ref($self), " sysread number $num failed: $!";
		print STDERR ".";

		if ($max && $len + $read > $max) {
			print STDERR "\n";
			print STDERR "Max\n";
			last;
		}

		if ($self->{nonblocking}) {
			vec($win, fileno(STDOUT), 1) = 1;
			select(undef, $win, undef, undef)
			    or die ref($self), " select write failed: $!";
		}
		defined(my $write = syswrite(STDOUT, $buf))
		    or die ref($self), " syswrite number $num failed: $!";
		if (defined($write)) {
			$read == $write
			    or die ref($self), " syswrite read $read ".
			    "did write $write";
			$len += $write;
		}

		if ($max && $len == $max) {
			print STDERR "\n";
			print STDERR "Big\n";
			print STDERR "Max\n";
			last;
		}
	}

	print STDERR "LEN: $len\n";
}

sub relay_copy {
	my $self = shift;
	my $protocol = $self->{protocol} || "tcp";

	if ($protocol =~ /tcp/) {
		relay_copy_stream($self, @_);
	} elsif ($protocol =~ /udp/) {
		relay_copy_datagram($self, @_);
	} else {
		die ref($self), " unknown protocol name: $protocol";
	}
}

sub relay_splice_stream {
	my $self = shift;
	my $max = $self->{max};
	my $idle = $self->{idle};

	my $len = 0;
	my $splicelen;
	my $shortsplice = 0;
	my $error;
	do {
		my $splicemax = $max ? $max - $len : 0;
		setsplice(\*STDIN, \*STDOUT, $splicemax, $idle)
		    or die ref($self), " splice stdin to stdout failed: $!";
		print STDERR "Spliced\n";

		if ($self->{readblocking}) {
			my $read;
			# block by reading from the source socket
			do {
				# busy loop to test soreceive
				$read = sysread(STDIN, my $buf, 2**16);
			} while ($self->{nonblocking} && !defined($read) &&
			    $!{EAGAIN});
			defined($read)
			    or die ref($self), " read blocking failed: $!";
			$read > 0 and die ref($self),
			    " read blocking has data: $read";
			print STDERR "Read\n";
		} else {
			my $rin = '';
			vec($rin, fileno(STDIN), 1) = 1;
			select($rin, undef, undef, undef)
			    or die ref($self), " select failed: $!";
		}

		defined($error = geterror(\*STDIN))
		    or die ref($self), " get error from stdin failed: $!";
		($! = $error) && ! $!{ETIMEDOUT} && ! $!{EFBIG}
		    and die ref($self), " splice failed: $!";

		defined($splicelen = getsplice(\*STDIN))
		    or die ref($self), " get splice len from stdin failed: $!";
		print STDERR "SPLICELEN: $splicelen\n";
		!$max || $splicelen <= $splicemax
		    or die ref($self), " splice len $splicelen ".
		    "greater than max $splicemax";
		$len += $splicelen;
	} while ($max && $max > $len && !$shortsplice++);

	relay_splice_check($self, $idle, $max, $len, $error);
	print STDERR "LEN: $len\n";
}

sub relay_splice_datagram {
	my $self = shift;
	my $max = $self->{max};
	my $idle = $self->{idle};

	my $splicemax = $max || 0;
	setsplice(\*STDIN, \*STDOUT, $splicemax, $idle)
	    or die ref($self), " splice stdin to stdout failed: $!";
	print STDERR "Spliced\n";

	my $rin = '';
	vec($rin, fileno(STDIN), 1) = 1;
	select($rin, undef, undef, undef)
	    or die ref($self), " select failed: $!";

	defined(my $error = geterror(\*STDIN))
	    or die ref($self), " get error from stdin failed: $!";
	($! = $error) && ! $!{ETIMEDOUT} && ! $!{EFBIG}
	    and die ref($self), " splice failed: $!";

	defined(my $splicelen = getsplice(\*STDIN))
	    or die ref($self), " get splice len from stdin failed: $!";
	print STDERR "SPLICELEN: $splicelen\n";
	!$max || $splicelen <= $splicemax
	    or die ref($self), " splice len $splicelen ".
	    "greater than max $splicemax";
	my $len = $splicelen;

	if ($max && $max > $len) {
		defined(my $read = sysread(STDIN, my $buf, $max - $len))
		    or die ref($self), " sysread stdin max failed: $!";
		$len += $read;
	}
	relay_splice_check($self, $idle, $max, $len, $error);
	print STDERR "LEN: $splicelen\n";
}

sub relay_splice_check {
	my $self = shift;
	my ($idle, $max, $len, $error) = @_;

	if ($idle && $error == Errno::ETIMEDOUT) {
		print STDERR "Timeout\n";
	}
	if ($max && $error == Errno::EFBIG) {
		print STDERR "Big\n";
	}
	if ($max && $max == $len) {
		print STDERR "Max\n";
	} elsif ($max && $max < $len) {
		die ref($self), " max $max less than len $len";
	} elsif ($max && $max > $len && $error == Errno::EFBIG) {
		die ref($self), " max $max greater than len $len";
	} elsif (!$error) {
		defined(my $read = sysread(STDIN, my $buf, 2**16))
		    or die ref($self), " sysread stdin failed: $!";
		$read > 0
		    and die ref($self), " sysread stdin has data: $read";
		print STDERR "End\n";
	}
}

sub relay_splice {
	my $self = shift;
	my $protocol = $self->{protocol} || "tcp";

	if ($protocol =~ /tcp/) {
		relay_splice_stream($self, @_);
	} elsif ($protocol =~ /udp/) {
		relay_splice_datagram($self, @_);
	} else {
		die ref($self), " unknown protocol name: $protocol";
	}
}

sub relay {
	my $self = shift;
	my $forward = $self->{forward};

	if ($forward =~ /copy/) {
		relay_copy($self, @_);
	} elsif ($forward =~ /splice/) {
		relay_splice($self, @_);
	} else {
		die ref($self), " unknown forward name: $forward";
	}

	my $soerror;
	$soerror = getsockopt(STDIN, SOL_SOCKET, SO_ERROR)
	    or die ref($self), " get error from stdin failed: $!";
	print STDERR "ERROR IN: ", unpack('i', $soerror), "\n";
	$soerror = getsockopt(STDOUT, SOL_SOCKET, SO_ERROR)
	    or die ref($self), " get error from stdout failed: $!";
	print STDERR "ERROR OUT: ", unpack('i', $soerror), "\n";
}

sub ioflip {
	my $self = shift;

	open(my $fh, '<&', \*STDIN)
	    or die ref($self), " ioflip dup failed: $!";
	open(STDIN, '<&', \*STDOUT)
	    or die ref($self), " ioflip dup STDIN failed: $!";
	open(STDOUT, '>&', $fh)
	    or die ref($self), " ioflip dup STDOUT failed: $!";
	close($fh)
	    or die ref($self), " ioflip close failed: $!";
}

sub errignore {
	$SIG{PIPE} = 'IGNORE';
	$SIG{__DIE__} = sub {
		die @_ if $^S;
		warn "Error ignored";
		my $soerror;
		$soerror = getsockopt(STDIN, SOL_SOCKET, SO_ERROR);
		print STDERR "ERROR IN: ", unpack('i', $soerror), "\n";
		$soerror = getsockopt(STDOUT, SOL_SOCKET, SO_ERROR);
		print STDERR "ERROR OUT: ", unpack('i', $soerror), "\n";
		warn @_;
		IO::Handle::flush(\*STDERR);
		POSIX::_exit(0);
	};
}

sub shutin {
	my $self = shift;
	shutdown(\*STDIN, SHUT_RD)
	    or die ref($self), " shutdown read failed: $!";
}

sub shutout {
	my $self = shift;
	IO::Handle::flush(\*STDOUT)
	    or die ref($self), " flush stdout failed: $!";
	shutdown(\*STDOUT, SHUT_WR)
	    or die ref($self), " shutdown write failed: $!";
}

########################################################################
# Server funcs
########################################################################

sub read_stream {
	my $self = shift;
	my $max = $self->{max};

	my $ctx = Digest::MD5->new();
	my $len = 0;
	while (<STDIN>) {
		$len += length($_);
		$ctx->add($_);
		print STDERR ".";
		if ($max && $len >= $max) {
			print STDERR "\nMax";
			last;
		}
	}
	print STDERR "\n";

	print STDERR "LEN: $len\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub read_oob {
	my $self = shift;
	my $size = $self->{size} || 4091;

	my $ctx = Digest::MD5->new();
	my $len = 0;
	while (1) {
		my $rin = my $ein = '';
		vec($rin, fileno(STDIN), 1) = 1;
		vec($ein, fileno(STDIN), 1) = 1 unless $self->{oobinline};
		select($rin, undef, $ein, undef)
		    or die ref($self), " select failed: $!";
		my $buf;
		my $atmark = sockatmark(\*STDIN)
		    or die ref($self), " sockatmark failed: $!";
		if ($atmark == 1) {
			if ($self->{oobinline}) {
				defined(recv(STDIN, $buf, 1, 0))
				    or die ref($self), " recv OOB failed: $!";
				print STDERR "[$buf]";
				$ctx->add("[$buf]");
				$len += length($buf);
			} else {
				defined(recv(STDIN, $buf, 1, MSG_OOB)) ||
				    $!{EINVAL}
				    or die ref($self), " recv OOB failed: $!";
				print STDERR "OOB: $buf\n" if length($buf);
			}
		}
		defined(recv(STDIN, $buf, $size, 0))
		    or die ref($self), " recv failed: $!";
		last unless length($buf);
		print STDERR $buf;
		$ctx->add($buf);
		$len += length($buf);
		print STDERR ".";
	}
	print STDERR "\n";

	print STDERR "LEN: $len\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub read_datagram {
	my $self = shift;
	my $max = $self->{max};
	my $idle = $self->{idle};
	my $size = $self->{size} || 2**16;

	my $ctx = Digest::MD5->new();
	my $len = 0;
	my @lengths;
	for (my $num = 0;; $num++) {
		if ($idle) {
			my $rin = '';
			vec($rin, fileno(STDIN), 1) = 1;
			defined(my $n = select($rin, undef, undef, $idle))
			    or die ref($self), " select idle failed: $!";
			if ($n == 0) {
				print STDERR "\n";
				print STDERR "Timeout";
				last;
			}
		}
		defined(my $read = sysread(STDIN, my $buf, $size))
		    or die ref($self), " sysread number $num failed: $!";
		$len += $read;
		push @lengths, $read;
		$ctx->add($buf);
		print STDERR ".";
		if ($max && $len >= $max) {
			print STDERR "\nMax";
			last;
		}
	}
	print STDERR "\n";

	print STDERR "LEN: $len\n";
	print STDERR "LENGTHS: @lengths\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub solingerin {
	my $self = shift;

	setsockopt(STDIN, SOL_SOCKET, SO_LINGER, pack('ii', 1, 0))
	    or die ref($self), " set linger in failed: $!";
}

########################################################################
# Script funcs
########################################################################

sub check_logs {
	my ($c, $r, $s, %args) = @_;

	return if $args{nocheck};

	check_relay($c, $r, $s, %args);
	check_len($c, $r, $s, %args);
	check_lengths($c, $r, $s, %args);
	check_md5($c, $r, $s, %args);
	check_error($c, $r, $s, %args);
}

sub check_relay {
	my ($c, $r, $s, %args) = @_;

	return unless $r;

	if (defined $args{relay}{timeout}) {
		my $lg = $r->loggrep(qr/^Timeout$/);
		die "no relay timeout"  if !$lg && $args{relay}{timeout};
		die "relay has timeout" if $lg && !$args{relay}{timeout};
	}
	if (defined $args{relay}{big}) {
		my $lg = $r->loggrep(qr/^Big$/);
		die "no relay big"  if !$lg && $args{relay}{big};
		die "relay has big" if $lg && !$args{relay}{big};
	}
	$r->loggrep(qr/^Max$/) or die "no relay max"
	    if $args{relay}{max} && !$args{relay}{nomax};
	$r->loggrep(qr/^End$/) or die "no relay end"
	    if $args{relay}{end};
}

sub check_len {
	my ($c, $r, $s, %args) = @_;

	my ($clen, $rlen, $slen);
	$clen = $c->loggrep(qr/^LEN: /) // die "no client len"
	    unless $args{client}{nocheck};
	$rlen = $r->loggrep(qr/^LEN: /) // die "no relay len"
	    if $r && ! $args{relay}{nocheck};
	$slen = $s->loggrep(qr/^LEN: /) // die "no server len"
	    unless $args{server}{nocheck};
	!$clen || !$rlen || $clen eq $rlen
	    or die "client: $clen", "relay: $rlen", "len mismatch";
	!$rlen || !$slen || $rlen eq $slen
	    or die "relay: $rlen", "server: $slen", "len mismatch";
	!$clen || !$slen || $clen eq $slen
	    or die "client: $clen", "server: $slen", "len mismatch";
	!defined($args{len}) || !$clen || $clen eq "LEN: $args{len}\n"
	    or die "client: $clen", "len $args{len} expected";
	!defined($args{len}) || !$rlen || $rlen eq "LEN: $args{len}\n"
	    or die "relay: $rlen", "len $args{len} expected";
	!defined($args{len}) || !$slen || $slen eq "LEN: $args{len}\n"
	    or die "server: $slen", "len $args{len} expected";
}

sub check_lengths {
	my ($c, $r, $s, %args) = @_;

	my ($clengths, $slengths);
	$clengths = $c->loggrep(qr/^LENGTHS: /)
	    unless $args{client}{nocheck};
	$slengths = $s->loggrep(qr/^LENGTHS: /)
	    unless $args{server}{nocheck};
	!$clengths || !$slengths || $clengths eq $slengths
	    or die "client: $clengths", "server: $slengths", "lengths mismatch";
	!defined($args{lengths}) || !$clengths ||
	    $clengths eq "LENGTHS: $args{lengths}\n"
	    or die "client: $clengths", "lengths $args{lengths} expected";
	!defined($args{lengths}) || !$slengths ||
	    $slengths eq "LENGTHS: $args{lengths}\n"
	    or die "server: $slengths", "lengths $args{lengths} expected";
}

sub check_md5 {
	my ($c, $r, $s, %args) = @_;

	my ($cmd5, $smd5);
	$cmd5 = $c->loggrep(qr/^MD5: /) unless $args{client}{nocheck};
	$smd5 = $s->loggrep(qr/^MD5: /) unless $args{server}{nocheck};
	!$cmd5 || !$smd5 || ref($args{md5}) eq 'ARRAY' || $cmd5 eq $smd5
	    or die "client: $cmd5", "server: $smd5", "md5 mismatch";
	my $md5 = ref($args{md5}) eq 'ARRAY' ?
	    join('|', @{$args{md5}}) : $args{md5};
	!$md5 || !$cmd5 || $cmd5 =~ /^MD5: ($md5)$/
	    or die "client: $cmd5", "md5 $md5 expected";
	!$md5 || !$smd5 || $smd5 =~ /^MD5: ($md5)$/
	    or die "server: $smd5", "md5 $md5 expected";
}

sub check_error {
	my ($c, $r, $s, %args) = @_;

	$args{relay}{errorin} //= 0 unless $args{relay}{nocheck};
	$args{relay}{errorout} //= 0 unless $args{relay}{nocheck};
	my %name2proc = (client => $c, relay => $r, server => $s);
	foreach my $name (qw(client relay server)) {
		my $p = $name2proc{$name}
		    or next;
		$args{$name}{errorin} //= $args{$name}{error};
		if (defined($args{$name}{errorin})) {
			my $ein = $p->loggrep(qr/^ERROR IN: /);
			defined($ein) &&
			    $ein eq "ERROR IN: $args{$name}{errorin}\n"
			    or die "$name: $ein ",
			    "error in $args{$name}{errorin} expected";
		}
		if (defined($args{$name}{errorout})) {
			my $eout = $p->loggrep(qr/^ERROR OUT: /);
			defined($eout) &&
			    $eout eq "ERROR OUT: $args{$name}{errorout}\n"
			    or die "$name: $eout ",
			    "error out $args{$name}{errorout} expected";
		}
	}
}

1;
