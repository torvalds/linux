# ex:ts=8 sw=4:
# $OpenBSD: Subst.pm,v 1.27 2025/05/27 03:42:59 tb Exp $
#
# Copyright (c) 2008 Marc Espie <espie@openbsd.org>
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

# very simple package, just holds everything needed for substitution
# according to package rules.

package OpenBSD::Subst;

# XXX ReverseSubst takes a state as an extra parameter
sub new($class, @)
{
	bless {}, $class;
}

sub hash($self)
{
	return $self;
}

sub add($self, $k, $v)
{
	$k =~ s/^\^//;
	$self->{$k} = $v;
}

sub value($self, $k)
{
	return $self->{$k};
}

sub parse_option($self, $opt)
{
	if ($opt =~ m/^([^=]+)\=(.*)$/o) {
		my ($k, $v) = ($1, $2);
		$v =~ s/^\'(.*)\'$/$1/;
		$v =~ s/^\"(.*)\"$/$1/;
		# variable name can't end with a '+',
		# recognize this as '+=' instead
		if ($k =~ s/\+$//) {
			if (defined $self->{$k}) {
				return $self->{$k} .= " $v";
			}
		}
		$self->add($k, $v);
	} else {
		$self->add($opt, 1);
	}
}

sub do($self, $s)
{
	return $s unless $s =~ m/\$/o;	# no need to subst if no $
	while ( my $k = ($s =~ m/\$\{([A-Za-z_][^\}]*)\}/o)[0] ) {
		my $v = $self->{$k};
		unless ( defined $v ) { $v = "\$\\\{$k\}"; }
		$s =~ s/\$\{\Q$k\E\}/$v/g;
	}
	$s =~ s/\$\\\{([A-Za-z_])/\$\{$1/go;
	return $s;
}

sub copy_fh2($self, $src, $dest)
{
	my $contents = do { local $/; <$src> };
	while (my ($k, $v) = each %{$self}) {
		$contents =~ s/\$\{\Q$k\E\}/$v/g;
	}
	$contents =~ s/\$\\\{([A-Za-z_])/\$\{$1/go;
	print $dest $contents;
}

sub copy_fh($self, $srcname, $dest)
{
	open my $src, '<', $srcname or die "can't open $srcname: $!";
	$self->copy_fh2($src, $dest);
}

sub copy($self, $srcname, $destname)
{
	open my $dest, '>', $destname or die "can't open $destname: $!";
	$self->copy_fh($srcname, $dest);
	return $dest;
}

sub has_fragment($self, $state, $frag, $location)
{
	my $v = $self->value($frag);

	if (!defined $v) {
		$state->fatal("Unknown fragment #1 in #2", 
		    $frag, $location);
	} elsif ($v == 1) {
		return 1;
	} elsif ($v == 0) {
		return 0;
	} else {
		$state->fatal("Invalid fragment define #1=#2", $frag, $v);
	}
}

sub empty($self, $k)
{
	my $v = $self->value($k);
	if (defined $v && $v) {
		return 0;
	} else {
		return 1;
	}
}

1;
