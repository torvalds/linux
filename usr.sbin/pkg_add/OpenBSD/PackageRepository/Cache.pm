# ex:ts=8 sw=4:
# $OpenBSD: Cache.pm,v 1.13 2023/09/16 09:33:13 espie Exp $
#
# Copyright (c) 2022 Marc Espie <espie@openbsd.org>
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

# supplementary glue to add support for reading the update.db locate(1)
# database in quirks
package OpenBSD::PackageRepository::Cache;

sub new($class, $state, $setlist)
{
	return undef unless -f OpenBSD::Paths->updateinfodb;

	my $o = bless { 
	    raw_data => {}, 
	    stems => {},
	    state => $state }, $class;

	$o->prime_update_info_cache($state, $setlist);
	return $o;

}
sub pipe_locate($self, @params)
{
	unshift(@params, OpenBSD::Paths->locate, 
	    '-d', OpenBSD::Paths->updateinfodb, '--');
	my $state = $self->{state};
	$state->errsay("Running #1", join(' ', @params))
	    if $state->defines("CACHING_VERBOSE");
	return @params;
}

# this is a hack to talk to quirks: the interface expects a list of
# search objects such that the last one can do add_stem, so we oblige
# (probably TODO: add a secondary interface in quirks, but this can do
# in the meantime)
sub add_stem($self, $stem)
{
	$self->{stems}{$stem} = 1;
}

sub prime_update_info_cache($self, $state, $setlist)
{
	my $progress = $state->progress;
	my $found = {};

	my $pseudo_search = [$self];

	# figure out a list of names to precache

	# okay, so basically instead of hitting locate once for each
	# package on the distant repository, we precache all the stems
	# we are asking to update/install
	# this is based on the assumption that most names are "regular"
	# and we won't cache too little or too much
	for my $set (@{$setlist}) {
		for my $h ($set->older, $set->hints) {
			next if $h->{update_found};
			my $name = $h->pkgname;
			my $stem = OpenBSD::PackageName::splitstem($name);
			next if $stem =~ m/^\.libs\d*\-/;
			next if $stem =~ m/^partial\-/;
			$stem =~ s/\%.*//; # zap branch info
			$stem =~ s/\-\-.*//; # and set flavors
			$self->add_stem($stem);
			$state->run_quirks(
			    sub($quirks) {
				$quirks->tweak_search($pseudo_search, $h, 
				    $state);
			    });
		}
	}
	my @list = sort keys %{$self->{stems}};
	return if @list == 0;

	my $total = scalar @list;
	$progress->set_header(
	    $state->f("Reading update info for installed packages", 
		$total));
	my $done = 0;
	my $oldname = ""; 
	# This can't go much faster, I've tried splitting the params
	# and running several locate(1) in //, but this yields negligible
	# gains for a lot of added complexity (reduced from 18 to 14 seconds
	# on my usual package install).
	open my $fh, "-|", $self->pipe_locate(map { "$_-[0-9]*"} @list) 
	    or $state->fatal("Can't run locate: #1", $!);
	while (<$fh>) {
		if (m/^(.*?)\:(.*)/) {
			my ($pkgname, $value) = ($1, $2);
			$found->{OpenBSD::PackageName::splitstem($pkgname)} = 1;
			$self->{raw_data}{$pkgname} //= '';
			$self->{raw_data}{$pkgname} .= "$value\n";
			if ($pkgname ne $oldname) {
				$oldname = $pkgname;
				$done++;
			}
			$progress->show($done, $total);
		}
	}
	close($fh);
	return unless $state->defines("CACHING_VERBOSE");
	for my $k (@list) {
		if (!defined $found->{$k}) {
			$state->say("No cache entry for #1", $k);
		}
	}
}

sub get_cached_info($self, $name)
{
	my $state = $self->{state};
	my $content;
	if (exists $self->{raw_data}{$name}) {
		$content = $self->{raw_data}{$name};
	} else {
		my $stem = OpenBSD::PackageName::splitstem($name);
		if (exists $self->{stems}{$stem}) {
			$state->say("Negative caching for #1", $name)
			    if $state->defines("CACHING_VERBOSE");
			return undef;
		}
		$content = '';
		open my $fh, "-|", $self->pipe_locate($name.":*") or die $!;
		while (<$fh>) {
			if (m/^.*?\:(.*)/) {
				$content .= $1."\n";
			} else {
				return undef;
			}
		}
		close ($fh);
	}
	if ($content eq '') {
		$state->say("Cache miss for #1", $name)
		    if $state->defines("CACHING_VERBOSE");
		return undef;
	}
	open my $fh2, "<", \$content;
	return OpenBSD::PackingList->read($fh2);
}

1;
