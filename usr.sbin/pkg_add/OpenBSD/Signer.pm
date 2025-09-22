#! /usr/bin/perl
# ex:ts=8 sw=4:
# $OpenBSD: Signer.pm,v 1.12 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2014 Marc Espie <espie@openbsd.org>
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

# code necessary to create signed packages

# the factory that chooses what method to use to sign things
# we keep that just in case we need to change scheme again
package OpenBSD::Signer;
use OpenBSD::PackageInfo;

my $h = {
	signify2 => 'OpenBSD::Signer::SIGNIFY2',
};

sub factory($class, $state)
{
	my @p = @{$state->{signature_params}};

	if (defined $h->{$p[0]}) {
		return $h->{$p[0]}->new($state, @p);
	} else {
		$state->usage("Unknown signature scheme $p[0]");
	}
}

package OpenBSD::Signer::SIGNIFY2;
our @ISA = qw(OpenBSD::Signer);
sub new($class, $state, @p)
{
	if (@p != 2 || !-f $p[1]) {
		$state->usage("$p[0] signature wants -s privkey");
	}
	my $o = bless {privkey => $p[1]}, $class;
	return $o;
}

sub sign($signer, $pkg, $state, $tmp)
{
	my $privkey = $signer->{privkey};
 	my $url = $pkg->url;
	if (!$pkg->{repository}->is_local_file) {
		$pkg->close(1);
		$state->fatal("Signing distant package #1 is not supported",
		    $url);
	}
	$url =~ s/^file://;
	$state->system(OpenBSD::Paths->signify, '-zS', '-s', $privkey, '-m', $url, '-x', $tmp);
}

sub want_local($)
{
	return 1;
}
# specific parameter handling plus element creation
package OpenBSD::CreateSign::State;
our @ISA = qw(OpenBSD::AddCreateDelete::State);

sub create_archive($state, $filename, $dir)
{
	require IO::Compress::Gzip;
	my $level = $state->{subst}->value('COMPRESSION_LEVEL') // 6;
	my $fh = IO::Compress::Gzip->new($filename, 
	    -Level => $level, -Time => 0) or
		$state->fatal("Can't create archive #1: #2", $filename, $!);
	$state->{archive_filename} = $filename;
	return OpenBSD::Ustar->new($fh, $state, $dir);
}

sub new_gstream($state)
{
	close($state->{archive}{fh});
	my $level = $state->{subst}->value('COMPRESSION_LEVEL') // 6;
	$state->{archive}{fh} =IO::Compress::Gzip->new(
	    $state->{archive_filename}, 
	    -Level => $level, -Time => 0, -Append => 1) or
		$state->fatal("Can't append to archive #1: #2", 
		    $state->{archive_filename}, $!);
}

sub ntodo($self, $offset = 0)
{
	return sprintf("%u/%u", $self->{done}-$offset, $self->{total});
}

1;
