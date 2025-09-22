# ex:ts=8 sw=4:
# $OpenBSD: PkgSpec.pm,v 1.51 2023/06/13 09:07:17 espie Exp $
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

package OpenBSD::PkgSpec::flavorspec;
sub new($class, $spec)
{
	bless \$spec, $class;
}

sub check_1flavor($f, $spec)
{
	for my $flavor (split /\-/o, $spec) {
		# must not be here
		if ($flavor =~ m/^\!(.*)$/o) {
			return 0 if $f->{$1};
		# must be here
		} else {
			return 0 unless $f->{$flavor};
		}
	}
	return 1;
}

sub match($self, $h)
{
	# check each flavor constraint
	for my $c (split /\,/o, $$self) {
		if (check_1flavor($h->{flavors}, $c)) {
			return 1;
		}
	}
	return 0;
}

package OpenBSD::PkgSpec::exactflavor;
our @ISA = qw(OpenBSD::PkgSpec::flavorspec);
sub new($class, $value)
{
	bless {map{($_, 1)} split(/\-/, $value)}, $class;
}

sub flavor_string($self)
{
	return join('-', sort keys %$self);
}

sub match($self, $h)
{
	if ($self->flavor_string eq $h->flavor_string) {
		return 1;
	} else {
		return 0;
	}
}

package OpenBSD::PkgSpec::versionspec;
our @ISA = qw(OpenBSD::PackageName::version);
my $ops = {
	'<' => 'lt',
	'<=' => 'le',
	'>' => 'gt',
	'>=' => 'ge',
	'=' => 'eq'
};

sub new($class, $s)
{
	my ($op, $version) = ('=', $s);
	if ($s =~ m/^(\>\=|\>|\<\=|\<|\=)(.*)$/) {
		($op, $version) = ($1, $2);
	}
	return "OpenBSD::PkgSpec::version::$ops->{$op}"->from_string($version);
}

sub pnum_compare($self, $b)
{
	if (!defined $self->{p}) {
		return 0;
	} else {
		return $self->SUPER::pnum_compare($b);
	}
}

sub is_exact($)
{
	return 0;
}

package OpenBSD::PkgSpec::version::lt;
our @ISA = qw(OpenBSD::PkgSpec::versionspec);
sub match($self, $b)
{
	-$self->compare($b->{version}) < 0 ? 1 : 0;
}

package OpenBSD::PkgSpec::version::le;
our @ISA = qw(OpenBSD::PkgSpec::versionspec);
sub match($self, $b)
{
	-$self->compare($b->{version}) <= 0 ? 1 : 0;
}

package OpenBSD::PkgSpec::version::gt;
our @ISA = qw(OpenBSD::PkgSpec::versionspec);
sub match($self, $b)
{
	-$self->compare($b->{version}) > 0 ? 1 : 0;
}

package OpenBSD::PkgSpec::version::ge;
our @ISA = qw(OpenBSD::PkgSpec::versionspec);
sub match($self, $b)
{
	-$self->compare($b->{version}) >= 0 ? 1 : 0;
}

package OpenBSD::PkgSpec::version::eq;
our @ISA = qw(OpenBSD::PkgSpec::versionspec);
sub match($self, $b)
{
	-$self->compare($b->{version}) == 0 ? 1 : 0;
}

sub is_exact($)
{
	return 1;
}

package OpenBSD::PkgSpec::badspec;
sub new($class)
{
	bless {}, $class;
}

# $self->match*($list)
sub match_ref($, $)
{
	return ();
}

sub match_libs_ref($, $)
{
	return ();
}

sub match_locations($, $)
{
	return [];
}

sub is_valid($)
{
	return 0;
}

package OpenBSD::PkgSpec::SubPattern;
use OpenBSD::PackageName;

sub parse($class, $p)
{
	my $r = {};

	# let's try really hard to find the stem and the flavors
	unless ($p =~ m/^
	    	([^%]+?) # stem part
		\-
		(
		    (?:\>|\>\=|\<\=|\<|\=)?\d[^-%]*  # optional op + version
		    |\* # or any version
		)
		(?:\-([^%]*))? # optional flavor part
	    $/x) {
		return undef;
	}
	($r->{stemspec}, $r->{vspec}, $r->{flavorspec}) = ($1, $2, $3);

	$r->{flavorspec} //= '';
	$r->{stemspec} =~ s/\./\\\./go;
	$r->{stemspec} =~ s/\+/\\\+/go;
	$r->{stemspec} =~ s/\*/\.\*/go;
	$r->{stemspec} =~ s/\?/\./go;
	$r->{stemspec} =~ s/^(\\\.libs)\-/$1\\d*\-/go;
	return $r;
}

sub add_version_constraints($class, $constraints, $vspec)
{
	# turn the vspec into a list of constraints.
	if ($vspec eq '*') {
		# non constraint
	} else {
		for my $c (split /\,/, $vspec) {
			push(@$constraints,
			    OpenBSD::PkgSpec::versionspec->new($c));
		}
	}
}

sub add_flavor_constraints($class, $constraints, $flavorspec)
{
	# and likewise for flavors
	if ($flavorspec eq '') {
		# non constraint
	} else {
		push(@$constraints,
		    OpenBSD::PkgSpec::flavorspec->new($flavorspec));
	}
}

sub new($class, $p, $with_partial)
{
	my $r = $class->parse($p);
	if (defined $r) {
		my $stemspec = $r->{stemspec};
		my $constraints = [];
		$class->add_version_constraints($constraints, $r->{vspec});
		$class->add_flavor_constraints($constraints, $r->{flavorspec});

		my $o = bless {
			libstem => qr{^\.libs\d*\-$stemspec\-\d.*$},
		    }, $class;

		if ($with_partial) {
			$o->{fuzzystem} = qr{^(?:partial\-)*$stemspec\-\d.*$};
		} else {
			$o->{fuzzystem} = qr{^$stemspec\-\d.*$};
		}
		if (@$constraints != 0) {
			$o->{constraints} = $constraints;
		}
		if (defined $r->{e}) {
			$o->{e} = 1;
		}
	   	return $o;
	} else {
		return OpenBSD::PkgSpec::badspec->new;
	}
}

sub match_ref($o, $list)
{
	my @result = ();
	# Now, have to extract the version number, and the flavor...
LOOP1:
	for my $s (grep(/$o->{fuzzystem}/, @$list)) {
		my $name = OpenBSD::PackageName->from_string($s);
		if (defined $o->{constraints}) {
			for my $c (@{$o->{constraints}}) {
				next LOOP1 unless $c->match($name);
			}
		}
		if (wantarray) {
			push(@result, $s);
		} else {
			return 1;
		}
	}

	if (wantarray) {
		return @result;
	} else {
		return 0;
	}
}

sub match_libs_ref($o, $list)
{
	return grep(/$o->{libstem}/, @$list);
}


sub match_locations($o, $list)
{
	my $result = [];
	# Now, have to extract the version number, and the flavor...
LOOP2:
	for my $s (grep { $_->name =~ m/$o->{fuzzystem}/} @$list) {
		my $name = $s->pkgname;
		if (defined $o->{constraints}) {
			for my $c (@{$o->{constraints}}) {
				next LOOP2 unless $c->match($name);
			}
		}
		push(@$result, $s);
	}

	return $result;
}

sub is_valid($)
{
	return 1;
}

package OpenBSD::PkgSpec;
sub subpattern_class($)
{ "OpenBSD::PkgSpec::SubPattern" }
sub new($class, $pattern, $with_partial = 0)
{
	my @l = map { $class->subpattern_class->new($_, $with_partial) }
		(split /\|/o, $pattern);
	if (@l == 1) {
		return $l[0];
	} else {
		return bless \@l, $class;
	}
}

sub match_ref($self, $r)
{
	if (wantarray) {
		my @l = ();
		for my $subpattern (@$self) {
			push(@l, $subpattern->match_ref($r));
		}
		return @l;
	} else {
		for my $subpattern (@$self) {
			if ($subpattern->match_ref($r)) {
				return 1;
			}
		}
		return 0;
	}
}

sub match_libs_ref($self, $r)
{
	if (wantarray) {
		my @l = ();
		for my $subpattern (@$self) {
			push(@l, $subpattern->match_libs_ref($r));
		}
		return @l;
	} else {
		for my $subpattern (@$self) {
			if ($subpattern->match_libs_ref($r)) {
				return 1;
			}
		}
		return 0;
	}
}

sub match_locations($self, $r)
{
	my $l = [];
	for my $subpattern (@$self) {
		push(@$l, @{$subpattern->match_locations($r)});
	}
	return $l;
}

sub is_valid($self)
{
	for my $subpattern (@$self) {
		return 0 unless $subpattern->is_valid;
	}
	return 1;
}

package OpenBSD::PkgSpec::SubPattern::Exact;
our @ISA = qw(OpenBSD::PkgSpec::SubPattern);

sub add_version_constraints($class, $constraints, $vspec)
{
	return if $vspec eq '*'; # XXX
	my $v = OpenBSD::PkgSpec::versionspec->new($vspec);
	die "not a good exact spec" if !$v->is_exact;
	delete $v->{p};
	push(@$constraints, $v);
}

sub add_flavor_constraints($class, $constraints, $flavorspec)
{
	push(@$constraints, OpenBSD::PkgSpec::exactflavor->new($flavorspec));
}

package OpenBSD::PkgSpec::Exact;
our @ISA = qw(OpenBSD::PkgSpec);

sub subpattern_class($)
{ "OpenBSD::PkgSpec::SubPattern::Exact" }

1;
