# ex:ts=8 sw=4:
# $OpenBSD: PackingList.pm,v 1.153 2023/11/23 09:44:08 espie Exp $
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

package OpenBSD::PackingList::State;
my $dot = '.';

sub new($class)
{
	bless { default_owner=>'root',
	     default_group=>'bin',
	     default_mode=> 0444,
	     owners => {},
	     groups => {},
	     cwd=>\$dot}, $class;
}

sub cwd($self)
{
	return ${$self->{cwd}};
}

sub set_cwd($self, $p)
{
	require File::Spec;

	$p = File::Spec->canonpath($p);
	$self->{cwd} = \$p;
}

package OpenBSD::PackingList::hashpath;
sub match($h, $plist)
{
	my $f = $plist->fullpkgpath2;
	if (!defined $f) {
		return 0;
	}
	for my $i (@{$h->{$f->{dir}}}) {
		if ($i->match($f)) {
			return 1;
		}
	}
	return 0;
}

sub partial_match($h, $subdir)
{
	for my $dir (keys %$h) {
		return 1 if $dir =~ m/\b\Q$subdir\E\b/;
	}
	return 0;
}

package OpenBSD::Composite;

# convert call to $self->sub(@args) into $self->visit(sub, @args)
sub AUTOLOAD
{
	our $AUTOLOAD;
	my $fullsub = $AUTOLOAD;
	(my $sub = $fullsub) =~ s/.*:://o;
	return if $sub eq 'DESTROY'; # special case
	my $self = $_[0];
	# verify it makes sense
	if ($self->element_class->can($sub)) {
		no strict "refs";
		# create the sub to avoid regenerating further calls
		*$fullsub = sub {
			my $self = shift;
			$self->visit($sub, @_);
		};
		# and jump to it
		goto &$fullsub;
	} else {
		die "Can't call $sub on ".ref($self);
	}
}

package OpenBSD::PackingList;
our @ISA = qw(OpenBSD::Composite);

use OpenBSD::PackingElement;
use OpenBSD::PackageInfo;

sub element_class($) { "OpenBSD::PackingElement" }

sub new($class)
{
	my $plist = bless {state => OpenBSD::PackingList::State->new,
		infodir => \(my $d)}, $class;
	OpenBSD::PackingElement::File->add($plist, CONTENTS);
	return $plist;
}

sub set_infodir($self, $dir)
{
	$dir .= '/' unless $dir =~ m/\/$/o;
	${$self->{infodir}} = $dir;
}

sub make_shallow_copy($plist, $h)
{
	my $copy = ref($plist)->new;
	$copy->set_infodir($plist->infodir);
	$plist->copy_shallow_if($copy, $h);
	return $copy;
}

sub make_deep_copy($plist, $h)
{
	my $copy = ref($plist)->new;
	$copy->set_infodir($plist->infodir);
	$plist->copy_deep_if($copy, $h);
	return $copy;
}

sub infodir($self)
{
	return ${$self->{infodir}};
}

sub zap_wrong_annotations($self)
{
	my $pkgname = $self->pkgname;
	if (defined $pkgname && $pkgname =~ m/^(?:\.libs\d*|partial)\-/) {
		delete $self->{'manual-installation'};
		delete $self->{'firmware'};
		delete $self->{'digital-signature'};
		delete $self->{'signer'};
	}
}

sub conflict_list($self)
{
	require OpenBSD::PkgCfl;

	return OpenBSD::PkgCfl->make_conflict_list($self);
}

sub read($a, $u, $code = \&defaultCode)
{
	$code //= \&defaultCode; # XXX callers may pass undef for now
	my $plist;
	if (ref $a) {
		$plist = $a;
	} else {
		$plist = $a->new;
	}
	&$code($u,
		sub($line) {
			return if $line =~ m/^\s*$/o;
			OpenBSD::PackingElement->create($line, $plist);
		});
	$plist->zap_wrong_annotations;
	return $plist;
}

sub defaultCode($fh, $cont)
{
	while (<$fh>) {
		&$cont($_);
	}
}

sub SharedItemsOnly($fh, $cont)
{
	while (<$fh>) {
		next unless m/^\@(?:cwd|dir|fontdir|ghost|mandir|newuser|newgroup|name)\b/o || m/^\@(?:sample|extra)\b.*\/$/o || m/^[^\@].*\/$/o;
		&$cont($_);
	}
}

sub UpdatePlistOnly($fh, $cont)
{
	while (<$fh>) {
		next unless m/^\@(?:cwd|dir|fontdir|ghost|mandir|depend)\b/o || m/^\@(?:sample|extra)\b.*\/$/o || m/^[^\@].*\/$/o;
		&$cont($_);
	}
}

sub DirrmOnly	# forwarder
{
	&OpenBSD::PackingList::SharedItemsOnly;
}

sub LibraryOnly($fh, $cont)
{
	while (<$fh>) {
		next unless m/^\@(?:cwd|lib|name|comment\s+subdir\=)\b/o;
		&$cont($_);
	}
}

sub FilesOnly($fh, $cont)
{
	while (<$fh>) {
	    	next unless m/^\@(?:cwd|name|info|man|file|lib|shell|sample|bin|rcscript|so|static-lib)\b/o || !m/^\@/o;
		&$cont($_);
	}
}

sub PrelinkStuffOnly($fh, $cont)
{
	while (<$fh>) {
		next unless m/^\@(?:cwd|bin|lib|name|define-tag|libset|depend|wantlib|comment\s+ubdir\=)\b/o;
		&$cont($_);
	}
}

sub DependOnly($fh, $cont)
{
	while (<$fh>) {
		if (m/^\@(?:libset|depend|wantlib|define-tag)\b/o) {
			&$cont($_);
		# XXX optimization
		} elsif (m/^\@(?:newgroup|newuser|cwd)\b/o) {
			last;
		}
	}
}

sub ExtraInfoOnly($fh, $cont)
{
	while (<$fh>) {
		if (m/^\@(?:name|pkgpath|comment\s+(?:subdir|pkgpath)\=|option)\b/o) {
			&$cont($_);
		# XXX optimization
		} elsif (m/^\@(?:libset|depend|wantlib|newgroup|newuser|cwd)\b/o) {
			last;
		}
	}
}

sub UpdateInfoOnly($fh, $cont)
{
	while (<$fh>) {
		# if old alwaysupdate, all info is sig
		# if new, we don't need the rest
		if (m/^\@option\s+always-update$/o) {
		    &$cont($_);
		    while (<$fh>) {
			    &$cont($_);
		    }
		    return;
		}
		if (m/^\@(?:name|libset|depend|wantlib|conflict|option|pkgpath|url|version|arch|comment\s+(?:subdir|pkgpath)\=)\b/o) {
			&$cont($_);
		# XXX optimization
		} elsif (m/^\@(?:newgroup|newuser|cwd)\b/o) {
			last;
		}
	}
}

sub ConflictOnly($fh, $cont)
{
	while (<$fh>) {
		if (m/^\@(?:name|conflict|option)\b/o) {
			&$cont($_);
		# XXX optimization
		} elsif (m/^\@(?:libset|depend|wantlib|newgroup|newuser|cwd)\b/o) {
			last;
		}
	}
}

sub fromfile($a, $fname, $code = \&defaultCode)
{
	open(my $fh, '<', $fname) or return;
	my $plist;
	eval {
		$plist = $a->read($fh, $code);
	};
	if ($@) {
		chomp $@;
		$@ =~ s/\.$/,/o;
		die "$@ in $fname, ";
	}
	close($fh);
	return $plist;
}

sub tofile($self, $fname)
{
	open(my $fh, '>', $fname) or return;
	$self->zap_wrong_annotations;
	$self->write($fh);
	close($fh) or return;
	return 1;
}

sub save($self)
{
	$self->tofile($self->infodir.CONTENTS);
}

sub add2list($plist, $object)
{
	my $category = $object->category;
	push @{$plist->{$category}}, $object;
}

sub addunique($plist, $object)
{
	my $category = $object->category;
	if (defined $plist->{$category}) {
		die "Duplicate $category in plist ".($plist->pkgname // "?");
	}
	$plist->{$category} = $object;
}

sub has($plist, $name)
{
	return defined $plist->{$name};
}

sub get($plist, $name)
{
	return $plist->{$name};
}

sub set_pkgname($self, $name)
{
	if (defined $self->{name}) {
		$self->{name}->set_name($name);
	} else {
		OpenBSD::PackingElement::Name->add($self, $name);
	}
}

sub pkgname($self)
{
	if (defined $self->{name}) {
		return $self->{name}->name;
	} else {
		return undef;
	}
}

sub localbase($self)
{
	if (defined $self->{localbase}) {
		return $self->{localbase}->name;
	} else {
		return '/usr/local';
	}
}

sub is_signed($self)
{
	return defined $self->{'digital-signature'};
}

sub fullpkgpath($self)
{
	if (defined $self->{extrainfo} && $self->{extrainfo}{subdir} ne '') {
		return $self->{extrainfo}{subdir};
	} else {
		return undef;
	}
}

sub fullpkgpath2($self)
{
	if (defined $self->{extrainfo} && $self->{extrainfo}{subdir} ne '') {
		return $self->{extrainfo}{path};
	} else {
		return undef;
	}
}

sub pkgpath($self)
{
	if (!defined $self->{_hashpath}) {
		my $h = $self->{_hashpath} =
		    bless {}, "OpenBSD::PackingList::hashpath";
		my $f = $self->fullpkgpath2;
		if (defined $f) {
			push(@{$h->{$f->{dir}}}, $f);
		}
		if (defined $self->{pkgpath}) {
			for my $i (@{$self->{pkgpath}}) {
				push(@{$h->{$i->{path}{dir}}}, $i->{path});
			}
		}
	}
	return $self->{_hashpath};
}

sub match_pkgpath($self, $plist2)
{
	return $self->pkgpath->match($plist2) ||
	    $plist2->pkgpath->match($self);
}

our @unique_categories =
    (qw(name url version signer digital-signature no-default-conflict manual-installation firmware always-update updatedb is-branch extrainfo localbase arch));

our @list_categories =
    (qw(conflict pkgpath ask-update libset depend
    	wantlib define-tag groups users items));

our @cache_categories =
    (qw(libset depend wantlib));

sub visit($self, $method, @l)
{
	if (defined $self->{cvstags}) {
		for my $item (@{$self->{cvstags}}) {
			$item->$method(@l) unless $item->{deleted};
		}
	}

	# XXX unique and info files really get deleted, so there's no need
	# to remove them later.
	for my $unique_item (@unique_categories) {
		$self->{$unique_item}->$method(@l) 
		    if defined $self->{$unique_item};
	}

	for my $special (OpenBSD::PackageInfo::info_names()) {
		$self->{$special}->$method(@l) if defined $self->{$special};
	}

	for my $listname (@list_categories) {
		if (defined $self->{$listname}) {
			for my $item (@{$self->{$listname}}) {
				$item->$method(@l) if !$item->{deleted};
			}
		}
	}
}

my $plist_cache = {};

sub from_installation($o, $pkgname, $code = \&defaultCode)
{
	require OpenBSD::PackageInfo;

	$code //= \&defaultCode;
	if ($code == \&DependOnly && defined $plist_cache->{$pkgname}) {
	    return $plist_cache->{$pkgname};
	}
	my $filename = OpenBSD::PackageInfo::installed_contents($pkgname);
	my $plist = $o->fromfile($filename, $code);
	if (defined $plist && $code == \&DependOnly) {
		$plist_cache->{$pkgname} = $plist;
	}
	if (defined $plist) {
		$plist->set_infodir(OpenBSD::PackageInfo::installed_info($pkgname));
	}
	if (!defined $plist) {
		print STDERR "Warning: couldn't read packing-list from installed package $pkgname\n";
		unless (-e $filename) {
			print STDERR "File $filename does not exist\n";
		}
	}
	return $plist;
}

sub to_cache($self)
{
	return if defined $plist_cache->{$self->pkgname};
	my $plist = OpenBSD::PackingList->new;
	for my $c (@cache_categories) {
		if (defined $self->{$c}) {
			$plist->{$c} = $self->{$c};
		}
	}
	$plist_cache->{$self->pkgname} = $plist;
}

sub to_installation($self)
{
	require OpenBSD::PackageInfo;

	return if $main::not;

	$self->tofile(OpenBSD::PackageInfo::installed_contents($self->pkgname));
}

sub signature($self)
{
	require OpenBSD::Signature;
	return OpenBSD::Signature->from_plist($self);
}

1;
