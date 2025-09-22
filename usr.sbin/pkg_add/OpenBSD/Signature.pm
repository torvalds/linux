# ex:ts=8 sw=4:
# $OpenBSD: Signature.pm,v 1.29 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2010 Marc Espie <espie@openbsd.org>
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

# this is the code that handles "update signatures", which has nothing
# to do with cryptography

use v5.36;

package OpenBSD::PackingElement;
sub signature($, $) {}

package OpenBSD::PackingElement::VersionElement;
sub signature($self, $hash)
{
	$hash->{$self->signature_key} = $self;
}

sub always($)
{
	return 1;
}

package OpenBSD::PackingElement::Version;
sub signature($self, $hash)
{
	$hash->{VERSION}{name} += $self->name;
}

package OpenBSD::PackingElement::Dependency;
sub signature_key($self)
{
	return $self->{pkgpath};
}

sub sigspec($self)
{
	return OpenBSD::PackageName->from_string($self->{def});
}

sub long_string($self)
{
	return '@'.$self->sigspec->to_string;
}

sub compare($a, $b)
{
	return $a->sigspec->compare($b->sigspec);
}

sub always($)
{
	return 0;
}

package OpenBSD::PackingElement::Wantlib;
sub signature_key($self)
{
	my $spec = $self->spec;
	if ($spec->is_valid) {
		return $spec->key;
	} else {
		return "???";
	}
}

sub compare($a, $b)
{
	return $a->spec->compare($b->spec);
}

sub long_string($self)
{
	return $self->spec->to_string;
}

sub always($)
{
	return 1;
}

package OpenBSD::PackingElement::Version;
sub signature_key($)
{
	return 'VERSION';
}

sub long_string($self)
{
	return $self->{name};
}

sub compare($a, $b)
{
	return $a->{name} <=> $b->{name};
}

package OpenBSD::Signature;
sub from_plist($class, $plist)
{
	my $k = {};
	$k->{VERSION} = OpenBSD::PackingElement::Version->new(0);
	$plist->visit('signature', $k);

	if ($plist->has('always-update')) {
		return $class->full->new($plist->pkgname, $k, $plist);
	} else {
		return $class->new($plist->pkgname, $k);
	}
}

sub full($)
{
	return "OpenBSD::Signature::Full";
}

sub new($class, $pkgname, $extra)
{
	bless { name => $pkgname, extra => $extra }, $class;
}

sub string($self)
{
	return join(',', $self->{name}, sort map {$_->long_string} values %{$self->{extra}});
}

sub compare($a, $b, $state)
{
	return $b->revert_compare($a, $state);
}

sub revert_compare($b, $a, $state)
{
	if ($a->{name} eq $b->{name}) {
		# first check if system version changed
		# then we don't have to go any further
		my $d = $b->{extra}{VERSION}->compare($a->{extra}{VERSION});
		if ($d < 0) {
			return 1;
		} elsif ($d > 0) {
			return -1;
		}

		my $shortened = $state->defines("SHORTENED");
		my $awins = 0;
		my $bwins = 0;
		my $done = {};
		my $errors = 0;
		while (my ($k, $v) = each %{$a->{extra}}) {
			if (!defined $b->{extra}{$k}) {
				$state->errsay(
				    "Couldn't find #1 in second signature", $k);
				$errors++;
				next;
			}
			$done->{$k} = 1;
			next if $shortened && !$v->always;
			my $r = $v->compare($b->{extra}{$k});
			if ($r > 0) {
				$awins++;
			} elsif ($r < 0) {
				$bwins++;
			}
		}
		for my $k (keys %{$b->{extra}}) {
			if (!$done->{$k}) {
				$state->errsay(
				    "Couldn't find #1 in first signature", $k);
				$errors++;
			}
		}
		if ($errors) {
			$a->print_error($b, $state);
			return undef;
		}
		if ($awins == 0) {
			return -$bwins;
		} elsif ($bwins == 0) {
			return $awins;
		} else {
			return undef;
		}
	} else {
		return OpenBSD::PackageName->from_string($a->{name})->compare(OpenBSD::PackageName->from_string($b->{name}));
	}
}

sub print_error($a, $b, $state)
{
	$state->errsay("Error: #1 exists in two non-comparable versions",
	    $a->{name});
	$state->errsay("Someone forgot to bump a REVISION");
	$state->errsay("#1 vs. #2", $a->string, $b->string);
}

package OpenBSD::Signature::Full;
our @ISA=qw(OpenBSD::Signature);

sub new($class, $pkgname, $extra, $plist)
{
	my $o = $class->SUPER::new($pkgname, $extra);
	my $a = $plist->get('always-update');
	# TODO remove after 2025
	if (!defined $a->{hash}) {
		$a->hash_plist($plist);
	}
	$o->{hash} = $a->{hash};
	return $o;
}

sub string($self)
{
	return join(',', $self->SUPER::string, $self->{hash});
}

sub revert_compare($b, $a, $state)
{
	my $r = $b->SUPER::revert_compare($a, $state);
	if (defined $r && $r == 0) {
		if ($a->string ne $b->string) {
			return undef;
		}
	}
	return $r;
}

sub compare($a, $b, $state)
{
	my $r = $a->SUPER::compare($b, $state);
	if (defined $r && $r == 0) {
		if ($a->string ne $b->string) {
			return undef;
		}
	}
	return $r;
}

1;
