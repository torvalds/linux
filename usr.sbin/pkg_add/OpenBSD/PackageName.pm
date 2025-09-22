# ex:ts=8 sw=4:
# $OpenBSD: PackageName.pm,v 1.58 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2010 Marc Espie <espie@openbsd.org>
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

package OpenBSD::PackageName;

sub url2pkgname($name)
{
	$name =~ s|.*/||;
	$name =~ s|\.tgz$||;

	return $name;
}

# see packages-specs(7)
sub splitname($n)
{
	if ($n =~ /^(.*?)\-(\d.*)$/o) {
		my $stem = $1;
		my $rest = $2;
		my @all = split /\-/o, $rest;
		return ($stem, @all);
	} else {
		return ($n);
	}
}

my $cached = {};

sub from_string($class, $s)
{
	return $cached->{$s} //= $class->new_from_string($s);
}

sub new_from_string($class, $n)
{
	if ($n =~ /^(.*?)\-(\d.*)$/o) {
		my $stem = $1;
		my $rest = $2;
		my @all = split /\-/o, $rest;
		my $version = OpenBSD::PackageName::version->from_string(shift @all);
		return bless {
			stem => $stem,
			version => $version,
			flavors => { map {($_, 1)} @all },
		}, "OpenBSD::PackageName::Name";
	} else {
		return bless {
			stem => $n,
		}, "OpenBSD::PackageName::Stem";
	}
}

sub splitstem($s)
{
	if ($s =~ /^(.*?)\-\d/o) {
		return $1;
	} else {
		return $s;
	}
}

sub pkg2stem($pkg)
{
	my $s = splitstem($pkg);
	$s =~ tr/A-Z/a-z/;
	return $s;

}
sub is_stem($s)
{
	if ($s =~ m/\-\d/o || $s eq '-') {
		return 0;
	} else {
		return 1;
	}
}

sub compile_stemlist(@p)
{
	my $hash = {};
	for my $n (@p) {
		$hash->{pkg2stem($n)}{$n} = 1;
	}
	bless $hash, "OpenBSD::PackageLocator::_compiled_stemlist";
}

sub avail2stems(@p)
{
	return compile_stemlist(@p);
}

package OpenBSD::PackageLocator::_compiled_stemlist;

sub find($self, $stem)
{
	$stem =~ tr/A-Z/a-z/;
	return keys %{$self->{$stem}};
}

sub add($self, $pkgname)
{
	$self->{OpenBSD::PackageName::pkg2stem($pkgname)}{$pkgname} = 1;
}

sub delete($self, $pkgname)
{
	my $stem = OpenBSD::PackageName::pkg2stem($pkgname);
	delete $self->{$stem}{$pkgname};
	if(keys %{$self->{$stem}} == 0) {
		delete $self->{$stem};
	}
}

sub find_partial($self, $partial)
{
	my @result = ();
	while (my ($stem, $pkgs) = each %$self) {
		next unless $stem =~ /\Q$partial\E/i;
		push(@result, keys %$pkgs);
	}
	return @result;
}

package OpenBSD::PackageName::dewey;

my $cache = {};

sub from_string($class, $string)
{
	my $o = bless { deweys => [ split(/\./o, $string) ],
		suffix => '', suffix_value => 0}, $class;
	if ($o->{deweys}->[-1] =~ m/^(\d+)(rc|alpha|beta|pre|pl)(\d*)$/) {
		$o->{deweys}->[-1] = $1;
		$o->{suffix} = $2;
		$o->{suffix_value} = $3;
	}
	return $o;
}

sub make($class, $string)
{
	return $cache->{$string} //= $class->from_string($string);
}

sub to_string($self)
{
	my $r = join('.', @{$self->{deweys}});
	if ($self->{suffix}) {
		$r .= $self->{suffix} . $self->{suffix_value};
	}
	return $r;
}

sub suffix_compare($a, $b)
{
	if ($a->{suffix} eq $b->{suffix}) {
		return $a->{suffix_value} <=> $b->{suffix_value};
	}
	if ($a->{suffix} eq 'pl') {
		return 1;
	}
	if ($b->{suffix} eq 'pl') {
		return -1;
	}

	if ($a->{suffix} gt $b->{suffix}) {
		return -suffix_compare($b, $a);
	}
	# order is '', alpha, beta, pre, rc
	# we know that a < b,
	if ($a->{suffix} eq '') {
		return 1;
	}
	if ($a->{suffix} eq 'alpha') {
		return -1;
	}
	if ($a->{suffix} eq 'beta') {
		return -1;
	}
	# refuse to compare pre vs. rc
	return 0;
}

sub compare($a, $b)
{
	# Try a diff in dewey numbers first
	for (my $i = 0; ; $i++) {
		if (!defined $a->{deweys}->[$i]) {
			if (!defined $b->{deweys}->[$i]) {
				last;
			} else {
				return -1;
			}
		}
		if (!defined $b->{deweys}->[$i]) {
			return 1;
		}
		my $r = dewey_compare($a->{deweys}->[$i],
			$b->{deweys}->[$i]);
		return $r if $r != 0;
	}
	return suffix_compare($a, $b);
}

sub dewey_compare($a, $b)
{
	# numerical comparison
	if ($a =~ m/^\d+$/o and $b =~ m/^\d+$/o) {
		return $a <=> $b;
	}
	# added lowercase letter
	if ("$a.$b" =~ m/^(\d+)([a-z]?)\.(\d+)([a-z]?)$/o) {
		my ($an, $al, $bn, $bl) = ($1, $2, $3, $4);
		if ($an != $bn) {
			return $an <=> $bn;
		} else {
			return $al cmp $bl;
		}
	}
	return $a cmp $b;
}

package OpenBSD::PackageName::version;

sub p($self)
{
	return defined $self->{p} ? $self->{p} : -1;
}

sub v($self)
{
	return defined $self->{v} ? $self->{v} : -1;
}

sub from_string($class, $string)
{
	my $o = bless {}, $class;
	if ($string =~ m/^(.*)v(\d+)$/o) {
		$o->{v} = $2;
		$string = $1;
	}
	if ($string =~ m/^(.*)p(\d+)$/o) {
		$o->{p} = $2;
		$string = $1;
	}
	$o->{dewey} = OpenBSD::PackageName::dewey->make($string);

	return $o;
}

sub to_string($o)
{
	my $string = $o->{dewey}->to_string;
	if (defined $o->{p}) {
		$string .= 'p'.$o->{p};
	}
	if (defined $o->{v}) {
		$string .= 'v'.$o->{v};
	}
	return $string;
}

sub pnum_compare($a, $b)
{
	return $a->p <=> $b->p;
}

sub compare($a, $b)
{
	# Simple case: epoch number
	if ($a->v != $b->v) {
		return $a->v <=> $b->v;
	}
	# Simple case: only p number differs
	if ($a->{dewey} eq $b->{dewey}) {
		return $a->pnum_compare($b);
	}

	return $a->{dewey}->compare($b->{dewey});
}

sub has_issues($self)
{
	if ($self->{dewey}{deweys}[-1] =~ m/v\d+$/ && defined $self->{p}) {
		return ("correct order is pNvM");
	} else {
		return ();
	}
}

package OpenBSD::PackageName::Stem;
sub to_string($o)
{
	return $o->{stem};
}

sub to_pattern($o)
{
	return $o->{stem}.'-*';
}

sub has_issues($self)
{
	return ("is a stem");
}

package OpenBSD::PackageName::Name;
sub flavor_string($o)
{
	return join('-', sort keys %{$o->{flavors}});
}

sub to_string($o)
{
	return join('-', $o->{stem}, $o->{version}->to_string,
	    sort keys %{$o->{flavors}});
}

sub to_pattern($o)
{
	return join('-', $o->{stem}, '*', $o->flavor_string);
}

sub compare($a, $b)
{
	if ($a->{stem} ne $b->{stem} || $a->flavor_string ne $b->flavor_string) {
		return undef;
	}
	return $a->{version}->compare($b->{version});
}

sub has_issues($self)
{
	return ((map {"flavor $_ can't start with digit"}
	    	grep { /^\d/ } keys %{$self->{flavors}}),
		$self->{version}->has_issues);
}

1;
