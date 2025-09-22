#	$OpenBSD: funcs.pl,v 1.10 2024/06/14 15:12:57 bluhm Exp $

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
use Digest::MD5;
use POSIX;
use Socket;
use Socket6;
use IO::Socket;

sub find_ports {
	my %args = @_;
	my $num    = delete $args{num}    // 1;
	my $domain = delete $args{domain} // AF_INET;
	my $addr   = delete $args{addr}   // "127.0.0.1";

	my @sockets = (1..$num);
	foreach my $s (@sockets) {
		$s = IO::Socket::IP->new(
		    Proto  => "tcp",
		    Domain => $domain,
		    $addr ? (LocalAddr => $addr) : (),
		) or die "find_ports: create and bind socket failed: $!";
	}
	my @ports = map { $_->sockport() } @sockets;

	return @ports;
}

sub path_md5 {
	my $name = shift;
	my $val = `cat md5-$name`;
}

########################################################################
# Client funcs
########################################################################

sub write_char {
	my $self = shift;
	my $len = shift // $self->{len} // 512;
	my $sleep = $self->{sleep};

	my $ctx = Digest::MD5->new();
	my $char = '0';
	for (my $i = 1; $i < $len; $i++) {
		$ctx->add($char);
		print $char
		    or die ref($self), " print failed: $!";
		if    ($char =~ /9/)  { $char = 'A' }
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
		$char = "\n";
		$ctx->add($char);
		print $char
		    or die ref($self), " print failed: $!";
		print STDERR ".\n";
	}
	IO::Handle::flush(\*STDOUT);

	print STDERR "LEN: ", $len, "\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub http_client {
	my $self = shift;

	unless ($self->{lengths}) {
		# only a single http request
		my $len = shift // $self->{len} // 512;
		my $cookie = $self->{cookie};
		http_request($self, $len, "1.0", $cookie);
		http_response($self, $len);
		return;
	}

	$self->{http_vers} ||= ["1.1", "1.0"];
	my $vers = $self->{http_vers}[0];
	my @lengths = @{$self->{redo}{lengths} || $self->{lengths}};
	my @cookies = @{$self->{redo}{cookies} || $self->{cookies} || []};
	while (defined (my $len = shift @lengths)) {
		my $cookie = shift @cookies || $self->{cookie};
		eval {
			http_request($self, $len, $vers, $cookie);
			http_response($self, $len);
		};
		warn $@ if $@;
		if (@lengths && ($@ || $vers eq "1.0")) {
			# reconnect and redo the outstanding requests
			$self->{redo} = {
			    lengths => \@lengths,
			    cookies => \@cookies,
			};
			return;
		}
	}
	delete $self->{redo};
	shift @{$self->{http_vers}};
	if (@{$self->{http_vers}}) {
		# run the tests again with other persistence
		$self->{redo} = {
		    lengths => [@{$self->{lengths}}],
		    cookies => [@{$self->{cookies} || []}],
		};
	}
}

sub http_request {
	my ($self, $len, $vers, $cookie) = @_;
	my $method = $self->{method} || "GET";
	my %header = %{$self->{header} || {}};

	# encode the requested length or chunks into the url
	my $path = ref($len) eq 'ARRAY' ? join("/", @$len) : $len;
	# overwrite path with custom path
	if (defined($self->{path})) {
		$path = $self->{path};
	}
	my @request = ("$method /$path HTTP/$vers");
	push @request, "Host: foo.bar" unless defined $header{Host};
	if ($vers eq "1.1" && $method eq "PUT") {
		if (ref($len) eq 'ARRAY') {
			push @request, "Transfer-Encoding: chunked"
			    if !defined $header{'Transfer-Encoding'};
		} else {
			push @request, "Content-Length: $len"
			    if !defined $header{'Content-Length'};
		}
	}
	foreach my $key (sort keys %header) {
		my $val = $header{$key};
		if (ref($val) eq 'ARRAY') {
			push @request, "$key: $_"
			    foreach @{$val};
		} else {
			push @request, "$key: $val";
		}
	}
	push @request, "Cookie: $cookie" if $cookie;
	push @request, "";
	print STDERR map { ">>> $_\n" } @request;
	print map { "$_\r\n" } @request;
	if ($method eq "PUT") {
		if (ref($len) eq 'ARRAY') {
			if ($vers eq "1.1") {
				write_chunked($self, @$len);
			} else {
				write_char($self, $_) foreach (@$len);
			}
		} else {
			write_char($self, $len);
		}
	}
	IO::Handle::flush(\*STDOUT);
	# XXX client shutdown seems to be broken in httpd
	#shutdown(\*STDOUT, SHUT_WR)
	#    or die ref($self), " shutdown write failed: $!"
	#    if $vers ne "1.1";
}

sub http_response {
	my ($self, $len) = @_;
	my $method = $self->{method} || "GET";
	my $code = $self->{code} || "200 OK";

	my $vers;
	my $chunked = 0;
	my $multipart = 0;
	my $boundary;
	{
		local $/ = "\r\n";
		local $_ = <STDIN>;
		defined
		    or die ref($self), " missing http $len response";
		chomp;
		print STDERR "<<< $_\n";
		m{^HTTP/(\d\.\d) $code$}
		    or die ref($self), " http response not $code"
		    unless $self->{httpnok};
		$vers = $1;
		while (<STDIN>) {
			chomp;
			print STDERR "<<< $_\n";
			last if /^$/;
			if (/^Content-Length: (.*)/) {
				if ($self->{httpnok} or $self->{multipart}) {
					$len = $1;
				} else {
					$1 == $len or die ref($self),
					    " bad content length $1";
				}
			}
			if (/^Transfer-Encoding: chunked$/) {
				$chunked = 1;
			}
			if (/^Content-Type: multipart\/byteranges; boundary=(.*)$/) {
				$multipart = 1;
				$boundary = $1;
			}
		}
	}
	die ref($self), " no multipart response"
	    if ($self->{multipart} && $multipart == 0);

	if ($multipart) {
		read_multipart($self, $boundary);
	} elsif ($chunked) {
		read_chunked($self);
	} else {
		read_char($self, $len)
		    if $method eq "GET";
	}
}

sub read_chunked {
	my $self = shift;

	for (;;) {
		my $len;
		{
			local $/ = "\r\n";
			local $_ = <STDIN>;
			defined or die ref($self), " missing chunk size";
			chomp;
			print STDERR "<<< $_\n";
			/^[[:xdigit:]]+$/
			    or die ref($self), " chunk size not hex: $_";
			$len = hex;
		}
		last unless $len > 0;
		read_char($self, $len);
		{
			local $/ = "\r\n";
			local $_ = <STDIN>;
			defined or die ref($self), " missing chunk data end";
			chomp;
			print STDERR "<<< $_\n";
			/^$/ or die ref($self), " no chunk data end: $_";
		}
	}
	{
		local $/ = "\r\n";
		while (<STDIN>) {
			chomp;
			print STDERR "<<< $_\n";
			last if /^$/;
		}
		defined or die ref($self), " missing chunk trailer";
	}
}

sub read_multipart {
	my $self = shift;
	my $boundary = shift;
	my $ctx = Digest::MD5->new();
	my $len = 0;

	for (;;) {
		my $part = 0;
		{
			local $/ = "\r\n";
			local $_ = <STDIN>;
			local $_ = <STDIN>;
			defined or die ref($self), " missing boundary";
			chomp;
			print STDERR "<<< $_\n";
			/^--$boundary(--)?$/
			    or die ref($self), " boundary not found: $_";
			if (not $1) {
				while (<STDIN>) {
					chomp;
					if (/^Content-Length: (.*)/) {
						$part = $1;
					}
					if (/^Content-Range: bytes (\d+)-(\d+)\/(\d+)$/) {
						$part = $2 - $1 + 1;
					}
					print STDERR "<<< $_\n";
					last if /^$/;
				}
			}
		}
		last unless $part > 0;

		$len += read_part($self, $ctx, $part);
	}

	print STDERR "LEN: ", $len, "\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub errignore {
	$SIG{PIPE} = 'IGNORE';
	$SIG{__DIE__} = sub {
		die @_ if $^S;
		warn "Error ignored";
		warn @_;
		IO::Handle::flush(\*STDERR);
		POSIX::_exit(0);
	};
}

########################################################################
# Common funcs
########################################################################

sub read_char {
	my $self = shift;
	my $max = shift // $self->{max};

	my $ctx = Digest::MD5->new();
	my $len = read_part($self, $ctx, $max);

	print STDERR "LEN: ", $len, "\n";
	print STDERR "MD5: ", $ctx->hexdigest, "\n";
}

sub read_part {
	my $self = shift;
	my ($ctx, $max) = @_;

	my $opct = 0;
	my $len = 0;
	for (;;) {
		if (defined($max) && $len >= $max) {
			print STDERR "Max\n";
			last;
		}
		my $rlen = POSIX::BUFSIZ;
		if (defined($max) && $rlen > $max - $len) {
			$rlen = $max - $len;
		}
		defined(my $n = read(STDIN, my $buf, $rlen))
		    or die ref($self), " read failed: $!";
		$n or last;
		$len += $n;
		$ctx->add($buf);
		my $pct = ($len / $max) * 100.0;
		if ($pct >= $opct + 1) {
			printf(STDERR "%.2f%% $len/$max\n", $pct);
			$opct = $pct;
		}
	}
	return $len;
}

sub write_chunked {
	my $self = shift;
	my @chunks = @_;

	foreach my $len (@chunks) {
		printf STDERR ">>> %x\n", $len;
		printf "%x\r\n", $len;
		write_char($self, $len);
		printf STDERR ">>> \n";
		print "\r\n";
	}
	my @trailer = ("0", "X-Chunk-Trailer: @chunks", "");
	print STDERR map { ">>> $_\n" } @trailer;
	print map { "$_\r\n" } @trailer;
}

########################################################################
# Script funcs
########################################################################

sub check_logs {
	my ($c, $r, %args) = @_;

	return if $args{nocheck};

	check_len($c, $r, %args);
	check_md5($c, $r, %args);
	check_loggrep($c, $r, %args);
	$r->loggrep("lost child")
	    and die "httpd lost child";
}

sub check_len {
	my ($c, $r, %args) = @_;

	$args{len} ||= 512 unless $args{lengths};

	my @clen;
	@clen = $c->loggrep(qr/^LEN: /) or die "no client len"
	    unless $args{client}{nocheck};
#	!@clen
#	    or die "client: @clen", "len mismatch";
	!defined($args{len}) || !$clen[0] || $clen[0] eq "LEN: $args{len}\n"
	    or die "client: $clen[0]", "len $args{len} expected";
	my @lengths = map { ref eq 'ARRAY' ? @$_ : $_ }
	    @{$args{lengths} || []};
	foreach my $len (@lengths) {
		unless ($args{client}{nocheck}) {
			my $clen = shift @clen;
			$clen eq "LEN: $len\n"
			    or die "client: $clen", "len $len expected";
		}
	}
}

sub check_md5 {
	my ($c, $r, %args) = @_;

	my @cmd5;
	@cmd5 = $c->loggrep(qr/^MD5: /) unless $args{client}{nocheck};
	my @md5 = ref($args{md5}) eq 'ARRAY' ? @{$args{md5}} : $args{md5} || ()
	    or return;
	foreach my $md5 (@md5) {
		unless ($args{client}{nocheck}) {
			my $cmd5 = shift @cmd5
			    or die "too few md5 in client log";
			$cmd5 =~ /^MD5: ($md5)$/
			    or die "client: $cmd5", "md5 $md5 expected";
		}
	}
	@cmd5 && ref($args{md5}) eq 'ARRAY'
	    and die "too many md5 in client log";
}

sub check_loggrep {
	my ($c, $r, %args) = @_;

	my %name2proc = (client => $c, httpd => $r);
	foreach my $name (qw(client httpd)) {
		my $p = $name2proc{$name} or next;
		my $pattern = $args{$name}{loggrep} or next;
		$pattern = [ $pattern ] unless ref($pattern) eq 'ARRAY';
		foreach my $pat (@$pattern) {
			if (ref($pat) eq 'HASH') {
				while (my($re, $num) = each %$pat) {
					my @matches = $p->loggrep($re);
					@matches == $num
					    or die "$name matches '@matches': ",
					    "'$re' => $num";
				}
			} else {
				$p->loggrep($pat)
				    or die "$name log missing pattern: '$pat'";
			}
		}
	}
}

1;
