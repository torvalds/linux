# ex:ts=8 sw=4:
# $OpenBSD: md5.pm,v 1.20 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2007 Marc Espie <espie@openbsd.org>
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

use v5.36;

# XXX even though there is ONE current implementation of OpenBSD::digest
# (SHA256) we keep the framework open in case we ever need to switch,
# as we did in the past with md5 -> sha256
package OpenBSD::digest;

sub new($class, $filename)
{
	$class = ref($class) || $class;
	my $digest = $class->digest_file($filename);
	bless \$digest, $class;
}

sub key($self)
{
	return $$self;
}

sub write($self, $fh)
{
	print $fh "\@", $self->keyword, " ", $self->stringize, "\n";
}

sub digest_file($self, $fname)
{
	my $d = $self->_algo;
	eval {
		$d->addfile($fname);
	};
	if ($@) {
		$@ =~ s/\sat.*//;
		die "can't compute ", $self->keyword, " on $fname: $@";
	}
	return $d->digest;
}

sub fromstring($class, $arg)
{
	$class = ref($class) || $class;
	my $d = $class->_unstringize($arg);
	bless \$d, $class;
}

sub equals($a, $b)
{
	return ref($a) eq ref($b) && $$a eq $$b;
}

package OpenBSD::sha;
our @ISA=(qw(OpenBSD::digest));

use Digest::SHA;
use MIME::Base64;

sub _algo($self)
{

	return Digest::SHA->new(256);
}

sub stringize($self)
{
	return encode_base64($$self, '');
}

sub _unstringize($class, $arg)
{
	if ($arg =~ /^[0-9a-f]{64}$/i) {
		return pack('H*', $arg);
	}
	return decode_base64($arg);
}

sub keyword($)
{
	return "sha";
}

1;
