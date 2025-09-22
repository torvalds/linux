# ex:ts=8 sw=4:
# $OpenBSD: Add.pm,v 1.196 2023/10/11 13:54:43 espie Exp $
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

package OpenBSD::Add;
use OpenBSD::Error;
use OpenBSD::PackageInfo;
use OpenBSD::ArcCheck;
use OpenBSD::Paths;
use File::Copy;

sub manpages_index($state)
{
	return unless defined $state->{addman};
	my $destdir = $state->{destdir};

	# fudge verbose for API differences
	while (my ($k, $v) = each %{$state->{addman}}) {
		my @l = map { "$destdir$k/$_" } @$v;
		if ($state->{not}) {
			$state->say("Merging manpages in #1: #2",
			    $destdir.$k, join(' ', @l)) if $state->verbose;
		} else {
			$state->run_makewhatis(['-d', $destdir.$k], \@l);
		}
	}
	delete $state->{addman};
}

sub register_installation($plist, $state)
{
	if ($state->{not}) {
		$plist->to_cache;
	} else {
		my $dest = installed_info($plist->pkgname);
		mkdir($dest);
		$plist->copy_info($dest, $state);
		$plist->set_infodir($dest);
		$plist->to_installation;
	}
}

sub validate_plist($plist, $state, $set)
{
	$plist->prepare_for_addition($state, $plist->pkgname, $set);
}

sub record_partial_installation($plist, $state, $h)
{
	use OpenBSD::PackingElement;

	my $n = $plist->make_shallow_copy($h);
	my $borked = borked_package($plist->pkgname);
	$n->set_pkgname($borked);

	# last file may have not copied correctly
	my $last = $n->{state}{lastfile};
	if (defined $last && defined($last->{d})) {

		my $old = $last->{d};
		my $lastname = $last->realname($state);
		if (-f $lastname) {
			$last->{d} = $last->compute_digest($lastname, $old);
			if (!$old->equals($last->{d})) {
				$state->say("Adjusting #1 for #2 from #3 to #4",
				    $old->keyword, $lastname, $old->stringize,
				    $last->{d}->stringize);
			}
		} else {
			delete $last->{d};
		}
	}
	register_installation($n, $state);
	return $borked;
}

sub perform_installation($handle, $state)
{
	return if $state->{regression}{stub} && $handle->pkgname !~ /^quirks\-/;

	$state->{partial} = $handle->{partial};
	$state->progress->visit_with_size($handle->{plist}, 'install');
	if ($handle->{location}{early_close}) {
		$handle->{location}->close_now;
	} else {
		$handle->{location}->finish_and_close;
	}
}

sub skip_to_the_end($handle, $state, $tied, $p)
{
	$state->tweak_header("skipping");
	for my $e (values %$tied) {
		$e->tie($state);
		$p->advance($e);
	}
	if (keys %$tied > 0) {
		# skipped entries should still be read in CACHE mode
		if (defined $state->cache_directory) {
			while (my $e = $state->{archive}->next) {
			}
		} else {
			$handle->{location}{early_close} = 1;
		}
	}
}

sub perform_extraction($handle, $state)
{
	return if $state->{regression}{stub} && $handle->pkgname !~ /^quirks\-/;

	$handle->{partial} = {};
	$state->{partial} = $handle->{partial};
	$state->{archive} = $handle->{location};
	$state->{check_digest} = $handle->{plist}{check_digest};

	# archives are actually stored out of order, find_extractible 
	# will dispatch the packing-list  entries into hashes keyed by names.
	# For "tied" entries, also see tie_files in OpenBSD::PkgAdd.
	my ($wanted, $tied) = ({}, {});
	$handle->{plist}->find_extractible($state, $wanted, $tied);
	my $p = $state->progress->new_sizer($handle->{plist});

	# so iterate over the archive, and "consume" hashes entry as we go
	# it's necessary to delete them so that skip_to_the_end will work
	# correctly (relies on wanted being empty to trigger, and requires
	# tied to be correct for the progress meter).
	if (keys %$wanted == 0) {
		skip_to_the_end($handle, $state, $tied, $p);
		return;
	}
	while (my $file = $state->{archive}->next) {
		my $e = $tied->{$file->name};
		if (defined $e) {
			delete $tied->{$file->name};
			$e->prepare_to_extract($state, $file);
			$e->tie($state);
			$state->{archive}->skip;
			$p->advance($e);
			# skip to next;
			next;
		}
		$e = $wanted->{$file->name};
		if (!defined $e) {
			$state->fatal("archive member not found #1",
			    $file->name);
		}
		delete $wanted->{$file->name};
		# note that readmes are only recorded when !tied, since
		# we only care if they changed
		my $fullname = $e->fullname;
		if ($fullname =~ m,^$state->{localbase}/share/doc/pkg-readmes/,) {
			push(@{$state->{readmes}}, $fullname);
		}

		$e->prepare_to_extract($state, $file);
		$e->extract($state, $file);
		$p->advance($e);
		if (keys %$wanted == 0) {
			skip_to_the_end($handle, $state, $tied, $p);
			last;
		}
	}
	if (keys %$wanted > 0) {
		$state->fatal("Truncated archive");
	}
}

my $user_tagged = {};

sub extract_pkgname($pkgname)
{
	$pkgname =~ s/^.*\///;
	$pkgname =~ s/\.tgz$//;
	return $pkgname;
}

sub tweak_package_status($pkgname, $state)
{
	$pkgname = extract_pkgname($pkgname);
	return 0 unless is_installed($pkgname);
	return 0 unless $user_tagged->{$pkgname};
	return 1 if $state->{not};
	my $plist = OpenBSD::PackingList->from_installation($pkgname);
	if ($plist->has('manual-installation') && $state->{automatic} > 1) {
		delete $plist->{'manual-installation'};
		$plist->to_installation;
		return 1;
	} elsif (!$plist->has('manual-installation') && !$state->{automatic}) {
		OpenBSD::PackingElement::ManualInstallation->add($plist);
		$plist->to_installation;
		return 1;
	}
	return 0;
}

sub tweak_plist_status($plist, $state)
{
	my $pkgname = $plist->pkgname;
	if ($state->defines('FW_UPDATE')) {
		$plist->has('firmware') or
			OpenBSD::PackingElement::Firmware->add($plist);
	}
	return 0 unless $user_tagged->{$pkgname};
	if (!$plist->has('manual-installation') && !$state->{automatic}) {
		OpenBSD::PackingElement::ManualInstallation->add($plist);
	}
}

sub tag_user_packages(@p)
{
	for my $set (@p) {
		for my $n ($set->newer_names) {
			$user_tagged->{OpenBSD::PackageName::url2pkgname($n)} = 1;
		}
	}
}

# The whole package addition/replacecement works like this:
# first we run tie_files in PkgAdd to figure out tieto
# then "find_extractible" figures out the element of the plist that
# belong in the archive (thus find_extractible is the hook that always
# gets run on every plist entry just prior to extraction/skipping)
#
# Then the actual extraction proceeds through "prepare_to_extract" and
# either "tie' OR "extract" depending on the element status.
# Then later on, we run "install".
#
# Actual file system entries may get a tempname, or avoid temp altogether
# 
# In case of replacement, tempname will get used if the name is the same
# but the file content is different.
#
# If pkg_add can figure out the name is the same, it will set avoidtemp
#
# Note that directories, hardlinks and symlinks are purely plist objects 
# with no archive existence:
# Links always get deleted/re-added even in replacement mode, while directory
# deletion is delayed into OpenBSD::SharedItems, since several packages 
# may mention the same directory.
#
package OpenBSD::PackingElement;
use OpenBSD::Error;

# used by newuser/newgroup to deal with options.
my ($uidcache, $gidcache);

# $self->prepare_for_addition($state, $pkgname, $set)
sub prepare_for_addition($, $, $, $)
{
}

# $self->find_extractible($state, $wanted, $tied):
#	sort item into wanted (needed from archive) / tied (already there)
sub find_extractible($, $, $, $)
{
}

sub extract($self, $state)
{
	$state->{partial}{$self} = 1;
	if ($state->{interrupted}) {
		die "Interrupted";
	}
}

sub install($self, $state)
{
	# XXX "normal" items are already in partial, but NOT stuff
	# that's install-only, like symlinks and dirs...
	$state->{partial}{$self} = 1;
	if ($state->{interrupted}) {
		die "Interrupted";
	}
}

# $self->copy_info($dest, $state)
sub copy_info($, $, $)
{
}

sub set_modes($self, $state, $name)
{
	if (defined $self->{owner} || defined $self->{group}) {
		require OpenBSD::IdCache;

		if (!defined $uidcache) {
			$uidcache = OpenBSD::UidCache->new;
			$gidcache = OpenBSD::GidCache->new;
		}
		my ($uid, $gid) = (-1, -1);
		if (defined $self->{owner}) {
			$uid = $uidcache->lookup($self->{owner}, $uid);
		}
		if (defined $self->{group}) {
			$gid = $gidcache->lookup($self->{group}, $gid);
		}
		chown $uid, $gid, $name;
	}
	if (defined $self->{mode}) {
		my $v = $self->{mode};
		if ($v =~ m/^\d+$/o) {
			chmod oct($v), $name;
		} else {
			$state->system(OpenBSD::Paths->chmod, 
			    $self->{mode}, $name);
		}
	}
	if (defined $self->{ts}) {
		utime $self->{ts}, $self->{ts}, $name;
	}
}

package OpenBSD::PackingElement::Meta;

# XXX stuff that's invisible to find_extractible should be considered extracted
# for the most part, otherwise we create broken partial packages
sub find_extractible($self, $state, $wanted, $tied)
{
	$state->{partial}{$self} = 1;
}

package OpenBSD::PackingElement::Cwd;
sub find_extractible	# forwarder
{
	&OpenBSD::PackingElement::Meta::find_extractible;
}

package OpenBSD::PackingElement::ExtraInfo;
use OpenBSD::Error;

sub prepare_for_addition($self, $state, $pkgname, $)
{
	if ($state->{ftp_only} && $self->{ftp} ne 'yes') {
	    $state->errsay("Package #1 is not for ftp", $pkgname);
	    $state->{problems}++;
	}
}

package OpenBSD::PackingElement::NewAuth;
use OpenBSD::Error;

sub add_entry($, $l, @p)
{
	while (@p >= 2) {
		my $f = shift @p;
		my $v = shift @p;
		next if !defined $v or $v eq '';
		if ($v =~ m/^\!(.*)$/o) {
			push(@$l, $f, $1);
		} else {
			push(@$l, $f, $v);
		}
	}
}

sub prepare_for_addition($self, $state, $pkgname, $)
{
	my $ok = $self->check;
	if (defined $ok) {
		if ($ok == 0) {
			$state->errsay("#1 #2 does not match",
			    $self->type, $self->name);
			$state->{problems}++;
		}
	}
	$self->{okay} = $ok;
}

sub install($self, $state)
{
	$self->SUPER::install($state);
	my $auth = $self->name;
	$state->say("adding #1 #2", $self->type, $auth) if $state->verbose >= 2;
	return if $state->{not};
	return if defined $self->{okay};
	my $l=[];
	push(@$l, "-v") if $state->verbose >= 2;
	$self->build_args($l);
	$state->vsystem($self->command,, @$l, '--', $auth);
}

package OpenBSD::PackingElement::NewUser;

sub command($) { OpenBSD::Paths->useradd }

sub build_args($self, $l)
{
	$self->add_entry($l,
	    '-u', $self->{uid},
	    '-g', $self->{group},
	    '-L', $self->{class},
	    '-c', $self->{comment},
	    '-d', $self->{home},
	    '-s', $self->{shell});
}

package OpenBSD::PackingElement::NewGroup;

sub command($) { OpenBSD::Paths->groupadd }

sub build_args($self, $l)
{
	$self->add_entry($l, '-g', $self->{gid});
}

package OpenBSD::PackingElement::FileBase;
use OpenBSD::Error;
use File::Basename;
use File::Path;
use OpenBSD::Temp;

sub find_extractible($self, $state, $wanted, $tied)
{
	if ($self->{tieto} || $self->{link} || $self->{symlink}) {
		$tied->{$self->name} = $self;
	} else {
		$wanted->{$self->name} = $self;
	}
}

sub prepare_for_addition($self, $state, $pkgname, $)
{
	my $fname = $self->retrieve_fullname($state, $pkgname);
	# check for collisions with existing stuff
	if ($state->vstat->exists($fname)) {
		push(@{$state->{colliding}}, $self);
		$self->{newly_found} = $pkgname;
		$state->{problems}++;
		return;
	}
	return if $state->{regression}{stub} && $pkgname !~ /^quirks\-/;
	my $s = $state->vstat->add($fname, 
	    $self->{tieto} ? 0 : $self->retrieve_size, $pkgname);
	return unless defined $s;
	if ($s->ro) {
		$s->report_ro($state, $fname);
	}
	if ($s->avail < 0) {
		$s->report_overflow($state, $fname);
	}
}

sub prepare_to_extract($self, $state, $file)
{
	my $fullname = $self->fullname;
	my $destdir = $state->{destdir};

	$file->{cwd} = $self->cwd;
	if (!$file->validate_meta($self)) {
		$state->fatal("can't continue");
	}

	$file->set_name($fullname);
	$file->{destdir} = $destdir;
}

sub find_safe_dir($self, $state)
{
	# figure out a safe directory where to put the temp file

	my $fullname = $self->fullname;
	my $filename = $state->{destdir}.$fullname;
	my $d = dirname($filename);
	my $orig = $d;

	# we go back up until we find an existing directory.
	# hopefully this will be on the same file system.
	my @candidates = ();
	while (!-d $d) {
		push(@candidates, $d);
		$d = dirname($d);
	}
	# and now we try to go back down, creating the best path we can
	while (@candidates > 0) {
		my $c = pop @candidates;
		last if -e $c; # okay this exists, but is not a directory
		$d = $c;
	}
	if (!-e _ && !$state->{not}) {
		$state->make_path($d, $fullname);
	}
	if ($state->{current_set}{simple_update} && 
	    $d eq $orig && 
	    !-e $filename) {
		$self->{avoid_temp} = $filename;
	}

	return $d;
}

sub create_temp($self, $d, $state)
{
	my $fullname = $self->fullname;
	my ($fh, $tempname) = OpenBSD::Temp::permanent_file($d, "pkg");
	$self->{tempname} = $tempname;
	if (!defined $tempname) {
		if ($state->allow_nonroot($fullname)) {
			$state->errsay("Can't create temp file outside localbase for #1", $fullname);
			$state->errsay(OpenBSD::Temp->last_error);
			return undef;
		}
		$state->fatal(OpenBSD::Temp->last_error);
	}
	return ($fh, $tempname);
}

sub may_create_temp($self, $d, $state)
{
	if ($self->{avoid_temp}) {
		if (open(my $fh, '>', $self->{avoid_temp})) {
			return ($fh, $self->{avoid_temp});
		}
	}
	delete $self->{avoid_temp};
	return $self->create_temp($d, $state);
}

sub tie($self, $state)
{
	if (defined $self->{link} || defined $self->{symlink}) {
		return;
	}

	$self->SUPER::extract($state);

	my $d = $self->find_safe_dir($state);
	my $src = $self->{tieto}->realname($state);
	my $dest = $self->realname($state);
	if ($state->{current_set}{simple_update} && $src eq $dest) {
		$state->say("No name change on tied file #1", $src)
		    if $state->verbose >= 3;
		$state->{current_set}{dont_delete}{$dest} = 1;
		$self->{avoid_temp} = 1;
		return;
	}
	if ($state->{not}) {
		$state->say("link #1 -> #2", 
		    $self->name, $d) if $state->verbose >= 3;
	} else {
		my ($fh, $tempname) = $self->may_create_temp($d, $state);

		return if !defined $tempname;
		unlink($tempname);
		$state->say("link #1 -> #2", $src, $tempname)
		    if $state->verbose >= 3;
		link($src, $tempname) || $state->copy_file($src, $tempname);
	}
}


sub extract($self, $state, $file)
{
	$self->SUPER::extract($state);

	my $d = $self->find_safe_dir($state);
	if ($state->{not}) {
		$state->say("extract #1 -> #2", 
		    $self->name, $d) if $state->verbose >= 3;
		$state->{archive}->skip;
	} else {
		my ($fh, $filename) = $self->may_create_temp($d, $state);
		if (!defined $filename) {
			$state->{archive}->skip;
			return;
		}

		if ($self->{avoid_temp}) {
			$state->{current_set}{dont_delete}{$filename} = 1;
		}
		$state->say("extract #1 -> #2", $self->name, $filename) 
		    if $state->verbose >= 3;


		if (!$file->isFile) {
			$state->fatal("can't extract #1, it's not a file", 
			    $self->stringize);
		}
		$file->extract_to_fh($fh);
		$self->may_check_digest($filename, $state);
	}
}

sub install($self, $state)
{
	$self->SUPER::install($state);
	my $fullname = $self->fullname;
	my $destdir = $state->{destdir};
	if ($state->{not}) {
		$state->say("moving tempfile -> #1",
		    $destdir.$fullname) if $state->verbose >= 5;
		return;
	}
	$state->make_path(dirname($destdir.$fullname), $fullname);
	if (defined $self->{link}) {
		link($destdir.$self->{link}, $destdir.$fullname);
		$state->say("link #1 -> #2", $destdir.$self->{link}, 
		    $destdir.$fullname) if $state->verbose >= 5;
	} elsif (defined $self->{symlink}) {
		symlink($self->{symlink}, $destdir.$fullname);
		$state->say("symlink #1 -> #2", $self->{symlink}, 
		    $destdir.$fullname) if $state->verbose >= 5;
	} else {
		if (defined $self->{avoid_temp}) {
			delete $self->{avoid_temp};
		} else {
			if (!defined $self->{tempname}) {
				return if $state->allow_nonroot($fullname);
				$state->fatal("No tempname for #1", $fullname);
			}
			rename($self->{tempname}, $destdir.$fullname) or
			    $state->fatal("can't move #1 to #2: #3",
				$self->{tempname}, $fullname, $!);
			$state->say("moving #1 -> #2",
			    $self->{tempname}, $destdir.$fullname)
				if $state->verbose >= 5;
			delete $self->{tempname};
		}
	}
	$self->set_modes($state, $destdir.$fullname);
}

package OpenBSD::PackingElement::Extra;
sub find_extractible($self, $state, $wanted, $tied)
{
	$state->{current_set}{known_extra}{$self->fullname} = 1;
}

package OpenBSD::PackingElement::RcScript;
sub install($self, $state)
{
	$state->{add_rcscripts}{$self->fullname} = 1;
	$self->SUPER::install($state);
}

package OpenBSD::PackingElement::Sample;
use OpenBSD::Error;
use File::Copy;

sub prepare_for_addition($self, $state, $pkgname, $)
{
	if (!defined $self->{copyfrom}) {
		$state->errsay("\@sample element #1 does not reference a valid file",
		    $self->fullname);
		$state->{problems}++;
	}
	my $fname = $state->{destdir}.$self->fullname;
	# If file already exists, we won't change it
	if ($state->vstat->exists($fname)) {
		return;
	}
	my $size = $self->{copyfrom}->{size};
	my $s = $state->vstat->add($fname, $size, $pkgname);
	return unless defined $s;
	if ($s->ro) {
		$s->report_ro($state, $fname);
	}
	if ($s->avail < 0) {
		$s->report_overflow($state, $fname);
	}
}

sub find_extractible($self, $state, $wanted, $tied)
{
	$state->{current_set}{known_sample}{$self->fullname} = 1;
}

# $self->extract($state)
sub extract($, $)
{
}

sub install($self, $state)
{
	$self->SUPER::install($state);
	my $destdir = $state->{destdir};
	my $filename = $destdir.$self->fullname;
	my $orig = $self->{copyfrom};
	my $origname = $destdir.$orig->fullname;
	if (-e $filename) {
		if ($state->verbose) {
		    $state->say("The existing file #1 has NOT been changed",
		    	$filename);
		    if (defined $orig->{d}) {

			# XXX assume this would be the same type of file
			my $d = $self->compute_digest($filename, $orig->{d});
			if ($d->equals($orig->{d})) {
			    $state->say("(but it seems to match the sample file #1)", $origname);
			} else {
			    $state->say("It does NOT match the sample file #1",
				$origname);
			    $state->say("You may wish to update it manually");
			}
		    }
		}
	} else {
		if ($state->{not}) {
			$state->say("The file #1 would be installed from #2",
			    $filename, $origname) if $state->verbose >= 2;
		} else {
			if (!copy($origname, $filename)) {
				$state->errsay("File #1 could not be installed:\n\t#2", $filename, $!);
			}
			$self->set_modes($state, $filename);
			if ($state->verbose >= 2) {
			    $state->say("installed #1 from #2",
				$filename, $origname);
			}
		}
	}
}

package OpenBSD::PackingElement::Sampledir;
sub extract($, $)
{
}

sub install	# forwarder
{
	&OpenBSD::PackingElement::Dir::install;
}

package OpenBSD::PackingElement::Mandir;

sub install($self, $state)
{
	$self->SUPER::install($state);
	if (!$state->{current_set}{known_mandirs}{$self->fullname}) {
		$state->log("You may wish to add #1 to /etc/man.conf", 
		    $self->fullname);
	}
}

package OpenBSD::PackingElement::Manpage;

sub install($self, $state)
{
	$self->SUPER::install($state);
	$self->register_manpage($state, 'addman');
}

package OpenBSD::PackingElement::InfoFile;
use File::Basename;
use OpenBSD::Error;

sub install($self, $state)
{
	$self->SUPER::install($state);
	return if $state->{not};
	my $fullname = $state->{destdir}.$self->fullname;
	$state->vsystem(OpenBSD::Paths->install_info,
	    "--info-dir=".dirname($fullname), '--', $fullname);
}

package OpenBSD::PackingElement::Shell;
sub install($self, $state)
{
	$self->SUPER::install($state);
	return if $state->{not};
	my $fullname = $self->fullname;
	my $destdir = $state->{destdir};
	# go append to /etc/shells if needed
	open(my $shells, '<', $destdir.OpenBSD::Paths->shells) or return;
	while(<$shells>) {
		s/^\#.*//o;
		return if m/^\Q$fullname\E\s*$/;
	}
	close($shells);
	open(my $shells2, '>>', $destdir.OpenBSD::Paths->shells) or return;
	print $shells2 $fullname, "\n";
	close $shells2;
	$state->say("Shell #1 appended to #2", $fullname,
	    $destdir.OpenBSD::Paths->shells) if $state->verbose;
}

package OpenBSD::PackingElement::Dir;
sub extract($self, $state)
{
	my $fullname = $self->fullname;
	my $destdir = $state->{destdir};

	return if -e $destdir.$fullname;
	$self->SUPER::extract($state);
	$state->say("new directory #1", $destdir.$fullname)
	    if $state->verbose >= 3;
	return if $state->{not};
	$state->make_path($destdir.$fullname, $fullname);
}

sub install($self, $state)
{
	$self->SUPER::install($state);
	my $fullname = $self->fullname;
	my $destdir = $state->{destdir};

	$state->say("new directory #1", $destdir.$fullname) 
	    if $state->verbose >= 5;
	return if $state->{not};
	$state->make_path($destdir.$fullname, $fullname);
	$self->set_modes($state, $destdir.$fullname);
}

package OpenBSD::PackingElement::Exec;
use OpenBSD::Error;

sub install($self, $state)
{
	$self->SUPER::install($state);
	if ($self->should_run($state)) {
		$self->run($state);
	}
}

sub should_run($, $) { 1 }

package OpenBSD::PackingElement::ExecAdd;
sub should_run($self, $state)
{
	return !$state->replacing;
}

package OpenBSD::PackingElement::ExecUpdate;
sub should_run($self, $state)
{
	return $state->replacing;
}

package OpenBSD::PackingElement::Tag;

sub install($self, $state)
{
	for my $d (@{$self->{definition_list}}) {
		$d->add_tag($self, "install", $state);
	}
}

package OpenBSD::PackingElement::Lib;

sub install($self, $state)
{
	$self->SUPER::install($state);
	$self->mark_ldconfig_directory($state);
}

package OpenBSD::PackingElement::SpecialFile;
use OpenBSD::PackageInfo;
use OpenBSD::Error;

sub copy_info($self, $dest, $state)
{
	require File::Copy;

	File::Copy::move($self->fullname, $dest) or
	    $state->errsay("Problem while moving #1 into #2: #3",
		$self->fullname, $dest, $!);
}

sub extract($self, $state)
{
	$self->may_verify_digest($state);
}

sub find_extractible($self, $state, $, $)
{
	$self->may_verify_digest($state);
}

package OpenBSD::PackingElement::FCONTENTS;
sub copy_info($, $, $)
{
}

package OpenBSD::PackingElement::AskUpdate;
sub prepare_for_addition($self, $state, $pkgname, $set)
{
	my @old = $set->older_names;
	if ($self->spec->match_ref(\@old) > 0) {
		my $key = "update_".OpenBSD::PackageName::splitstem($pkgname);
		return if $state->defines($key);
		if ($state->is_interactive) {
			if ($state->confirm_defaults_to_no(
			    "#1: #2.\nDo you want to update now",
			    $pkgname, $self->{message})) {
			    	return;
			}
		} else {
			$state->errsay("Can't update #1 now: #2",
			    $pkgname, $self->{message});
		}
		$state->{problems}++;
	}
}

package OpenBSD::PackingElement::FDISPLAY;
sub install($self, $state)
{
	my $d = $self->{d};
	if (!$state->{current_set}{known_displays}{$self->{d}->key}) {
		$self->prepare($state);
	}
	$self->SUPER::install($state);
}

package OpenBSD::PackingElement::FUNDISPLAY;
sub find_extractible($self, $state, $wanted, $tied)
{
	$state->{current_set}{known_displays}{$self->{d}->key} = 1;
	$self->SUPER::find_extractible($state, $wanted, $tied);
}

1;
