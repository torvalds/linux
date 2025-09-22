# ex:ts=8 sw=4:
# $OpenBSD: LibSpec.pm,v 1.21 2023/10/08 12:45:31 espie Exp $
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
#
use v5.36;

package OpenBSD::LibObject;

sub systemlibraryclass($self)
{
	return ref($self);
}

sub key($self)
{
	if (defined $self->{dir}) {
		return "$self->{dir}/$self->{stem}";
	} else {
		return $self->{stem};
	}
}

sub major($self)
{
	return $self->{major};
}

sub minor($self)
{
	return $self->{minor};
}

sub version($self)
{
	return ".".$self->major.".".$self->minor;
}

sub is_static($) { 0 }

sub is_valid($) { 1 }

sub stem($self)
{
	return $self->{stem};
}

sub badclass($self)
{
	"OpenBSD::BadLib";
}

sub lookup($spec, $repo, $base)
{
	my $approx = $spec->lookup_stem($repo);
	if (!defined $approx) {
		return undef;
	}
	my $r = [];
	for my $c (@$approx) {
		if ($spec->match($c, $base)) {
			push(@$r, $c);
		}
	}
	return $r;
}

sub compare($a, $b)
{
	if ($a->key ne $b->key) {
		return $a->key cmp $b->key;
	}
	if ($a->major != $b->major) {
		return $a->major <=> $b->major;
	}
	return $a->minor <=> $b->minor;
}

package OpenBSD::BadLib;
our @ISA=qw(OpenBSD::LibObject);

sub to_string($self)
{
	return $$self;
}

sub new($class, $string)
{
	bless \$string, $class;
}

sub is_valid($)
{
	return 0;
}

sub lookup_stem($, $)
{
	return undef;
}

# $spec->match($library, $base)
sub match($, $, $)
{
	return 0;
}

package OpenBSD::LibRepo;

sub new($class)
{
	bless {}, $class;
}

sub register($repo, $lib, $origin)
{
	$lib->set_origin($origin);
	push @{$repo->{$lib->stem}}, $lib;
}

sub find_best($repo, $stem)
{
	my $best;

	if (exists $repo->{$stem}) {
		for my $lib (@{$repo->{$stem}}) {
			if (!defined $best || $lib->is_better($best)) {
				$best = $lib;
			}
		}
	} 
	return $best;
}

package OpenBSD::Library;
our @ISA = qw(OpenBSD::LibObject);

sub systemlibraryclass($)
{
	"OpenBSD::Library::System";
}

sub from_string($class, $filename)
{
	if (my ($dir, $stem, $major, $minor) = $filename =~ m/^(.*)\/lib([^\/]+)\.so\.(\d+)\.(\d+)$/o) {
		bless { dir => $dir, stem => $stem, major => $major,
		    minor => $minor }, $class;
	} else {
		return $class->badclass->new($filename);
	}
}

sub to_string($self)
{
	return "$self->{dir}/lib$self->{stem}.so.$self->{major}.$self->{minor}";
}

sub set_origin($self, $origin)
{
	$self->{origin} = $origin;
	if ($origin eq 'system') {
		bless $self, $self->systemlibraryclass;
	}
	return $self;
}

sub origin($self)
{
	return $self->{origin};
}

sub no_match_dispatch($library, $spec, $base)
{
	return $spec->no_match_shared($library, $base);
}

sub is_better($self, $other)
{
	if ($other->is_static) {
		return 1;
	}
	if ($self->major > $other->major) {
		return 1;
	}
	if ($self->major == $other->major && $self->minor > $other->minor) {
		return 1;
    	}
	return 0;
}

# could be used for better reporting
# is used for regression testing
package OpenBSD::Library::System;
our @ISA = qw(OpenBSD::Library);

package OpenBSD::LibSpec;
our @ISA = qw(OpenBSD::LibObject);

sub new($class, $dir, $stem, $major, $minor)
{
	bless {
		dir => $dir, stem => $stem,
		major => $major, minor => $minor
	    }, $class;
}

my $cached = {};

sub from_string($class, $s)
{
	return $cached->{$s} //= $class->new_from_string($s);
}

sub new_with_stem($class, $stem, $major, $minor)
{
	if ($stem =~ m/^(.*)\/([^\/]+)$/o) {
		return $class->new($1, $2, $major, $minor);
	} else {
		return $class->new(undef, $stem, $major, $minor);
	}
}

sub new_from_string($class, $string)
{
	if (my ($stem, $major, $minor) = $string =~ m/^(.*)\.(\d+)\.(\d+)$/o) {
		return $class->new_with_stem($stem, $major, $minor);
	} else {
		return $class->badclass->new($string);
	}
}

sub to_string($self)
{
	return join('.', $self->key, $self->major, $self->minor);

}

sub lookup_stem($spec, $repo)
{
	my $result = $repo->{$spec->stem};
	if (!defined $result) {
		return undef;
	} else {
		return $result;
	}
}

sub no_match_major($spec, $library)
{
	return $spec->major != $library->major;
}

sub no_match_name($spec, $library, $base)
{
	if (defined $spec->{dir}) {
		if ("$base/$spec->{dir}" eq $library->{dir}) {
			return undef;
		}
	} else {
		for my $d ($base, OpenBSD::Paths->library_dirs) {
			if ("$d/lib" eq $library->{dir}) {
				return undef;
			}
		}
	}
	return "bad directory";
}

sub no_match_shared($spec, $library, $base)
{
	if ($spec->no_match_major($library)) {
		return "bad major";
	}
	if ($spec->major == $library->major &&
	    $spec->minor > $library->minor) {
		return "minor is too small";
	}
	return $spec->no_match_name($library, $base);
}

# classic double dispatch pattern
sub no_match($spec, $library, $base)
{
	return $library->no_match_dispatch($spec, $base);
}

sub match($spec, $library, $base)
{
	return !$spec->no_match($library, $base);
}

1;
