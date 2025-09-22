# ex:ts=8 sw=4:
# $OpenBSD: RequiredBy.pm,v 1.30 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2005 Marc Espie <espie@openbsd.org>
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

package OpenBSD::RequirementList;
use OpenBSD::PackageInfo;
use Carp;

sub fatal_error($self, $msg)
{
	require OpenBSD::Tracker;
	OpenBSD::Tracker->dump;
	confess ref($self), ": $msg $self->{filename}: $!";
}

sub fill_entries($self)
{
	if (!exists $self->{entries}) {
		my $l = $self->{entries} = {};

		if (-f $self->{filename}) {
			open(my $fh, '<', $self->{filename}) or 
			    $self->fatal_error("reading");
			while(<$fh>) {
				s/\s+$//o;
				next if /^$/o;
				chomp;
				$l->{$_} = 1;
			}
			close($fh);
			$self->{nonempty} = 1;
		} else {
			$self->{nonempty} = 0;
		}
	}
}

sub synch($self)
{
	return $self if $main::not;

	if (!unlink $self->{filename}) {
		if ($self->{nonempty}) {
		    croak ref($self), ": erasing $self->{filename}: $!";
		}
	}
	if (%{$self->{entries}}) {
		open(my $fh, '>', $self->{filename}) or 
		    $self->fatal_error("writing");
		while (my ($k, $v) = each %{$self->{entries}}) {
			print $fh "$k\n";
		}
		close($fh) or
		    croak ref($self), ": closing $self->{filename}: $!";
		$self->{nonempty} = 1;
	} else {
		$self->{nonempty} = 0;
	}
	return $self;
}

sub list($self)
{
	if (wantarray) {
		$self->fill_entries;
		return keys %{$self->{entries}};
	} else {
		if (exists $self->{entries}) {
			return %{$self->{entries}} ? 1 : 0;
		} elsif (!exists $self->{nonempty}) {
			$self->{nonempty} = -f $self->{filename} ? 1 : 0;
		}
		return $self->{nonempty};
	}
}

sub erase($self)
{
	$self->{entries} = {};
	$self->synch;
}

sub delete($self, @pkgnames)
{
	$self->fill_entries;
	for my $pkg (@pkgnames) {
		delete $self->{entries}->{$pkg};
	}
	$self->synch;
}

sub add($self, @pkgnames)
{
	$self->fill_entries;
	for my $pkg (@pkgnames) {
		$self->{entries}->{$pkg} = 1;
	}
	$self->synch;
}

my $cache = {};

sub new($class, $pkgname)
{
	my $f = installed_info($pkgname).$class->filename;
	if (!exists $cache->{$f}) {
		return $cache->{$f} = bless { filename => $f }, $class;
	}
	return $cache->{$f};
}

sub forget($class, $dir)
{
	my $f = $dir.$class->filename;
	if (exists $cache->{$f}) {
		$cache->{$f}->{entries} = {};
		$cache->{$f}->{nonempty} = 0;
	}
}

sub compute_closure($class, @seed)
{
	my @todo = @seed;
	my %done = ();

	while (my $pkgname = pop @todo) {
		next if $done{$pkgname};
		$done{$pkgname} = 1;
		for my $dep ($class->new($pkgname)->list) {
			next if defined $done{$dep};
			push(@todo, $dep);
		}
	}
	return keys %done;
}

package OpenBSD::RequiredBy;
our @ISA=qw(OpenBSD::RequirementList);
use OpenBSD::PackageInfo;

sub filename($) { REQUIRED_BY };

package OpenBSD::Requiring;
our @ISA=qw(OpenBSD::RequirementList);
use OpenBSD::PackageInfo;

sub filename($) { REQUIRING };

1;
