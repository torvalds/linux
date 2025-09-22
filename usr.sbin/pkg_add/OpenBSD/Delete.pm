# ex:ts=8 sw=4:
# $OpenBSD: Delete.pm,v 1.170 2025/04/28 18:56:25 kn Exp $
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

package OpenBSD::Delete;
use OpenBSD::Error;
use OpenBSD::PackageInfo;
use OpenBSD::RequiredBy;
use OpenBSD::Paths;
use File::Basename;

sub keep_old_files($state, $plist)
{
	my $p = OpenBSD::PackingList->new;
	my $borked = borked_package($plist->pkgname);
	$p->set_infodir(installed_info($borked));
	mkdir($p->infodir);

	$plist->copy_old_stuff($p, $state);
	$p->set_pkgname($borked);
	$p->to_installation;
	return $borked;
}

sub manpages_unindex($state)
{
	return unless defined $state->{rmman};
	my $destdir = $state->{destdir};

	while (my ($k, $v) = each %{$state->{rmman}}) {
		my @l = map { "$destdir$k/$_" } @$v;
		if ($state->{not}) {
			$state->say("Removing manpages in #1: #2",
			    $destdir.$k, join(' ', @l)) if $state->verbose;
		} else {
			$state->run_makewhatis(['-u', $destdir.$k], \@l);
		}
	}
	delete $state->{rmman};
}

sub validate_plist($plist, $state)
{
	$plist->prepare_for_deletion($state, $plist->pkgname);
}

sub remove_packing_info($plist, $state)
{
	my $dir = $plist->infodir;

	for my $fname (info_names()) {
		unlink($dir.$fname);
	}
	OpenBSD::RequiredBy->forget($dir);
	OpenBSD::Requiring->forget($dir);
	rmdir($dir) or
	    $state->fatal("can't finish removing directory #1: #2", $dir, $!);
}

sub delete_handle($handle, $state)
{
	my $pkgname = $handle->pkgname;
	my $plist = $handle->plist;
	if ($plist->has('firmware') && !$state->defines('FW_UPDATE')) {
		if ($state->is_interactive) {
			if (!$state->confirm_defaults_to_no(
			    "\nDelete firmware #1", $pkgname)) {
				$state->errsay("NOT deleting #1", $pkgname);
				return;
			}
		} else {
			$state->errsay("NOT deleting #1: use fw_update -d", 
			    $pkgname);
			return;
		}
	}

	$state->{problems} = 0;
	validate_plist($plist, $state);
	$state->fatal("can't recover from deinstalling #1", $pkgname)
	    if $state->{problems};
	$state->vstat->synchronize;

	delete_plist($plist, $state);
}

sub unregister_dependencies($plist, $state)
{
	my $pkgname = $plist->pkgname;
	my $l = OpenBSD::Requiring->new($pkgname);

	for my $name ($l->list) {
		$state->say("remove dependency of #1 on #2", $pkgname, $name)
		    if $state->verbose >= 3;
		local $@;
		try {
			OpenBSD::RequiredBy->new($name)->delete($pkgname);
		} catch {
			$state->errsay($_);
		};
	}
	$l->erase;
}

sub delete_plist($plist, $state)
{
	my $pkgname = $plist->pkgname;
	$state->{pkgname} = $pkgname;
	if (!$state->{regression}{stub} || $pkgname =~ /^quirks\-/) {
		if (!$state->{size_only}) {
			$plist->register_manpage($state, 'rmman');
			manpages_unindex($state);
			$state->progress->visit_with_size($plist, 'delete');
		}
	}

	unregister_dependencies($plist, $state);
	return if $state->{not};
	if ($state->{baddelete}) {
	    my $borked = keep_old_files($state, $plist);
	    $state->log("Files kept as #1 package", $borked);
	    delete $state->{baddelete};
	}


	remove_packing_info($plist, $state);
	delete_installed($pkgname);
}

package OpenBSD::PackingElement;

sub rename_file_to_temp($self, $state)
{
	require OpenBSD::Temp;

	my $n = $self->realname($state);

	my (undef, $j) = OpenBSD::Temp::permanent_file(undef, $n);
	if (!defined $j) {
		$state->errsay(OpenBSD::Temp->last_error);
		return;
	}
	if (rename($n, $j)) {
		$state->say("Renaming old file #1 to #2", $n, $j);
		if ($self->name !~ m/^\//o && $self->cwd ne '.') {
			my $c = $self->cwd;
			$j =~ s|^\Q$c\E/||;
		}
		$self->set_name($j);
	} else {
		$state->errsay("Bad rename #1 to #2: #3", $n, $j, $!);
	}
}

# $self->prepare_for_deletion($state, $pkgname)
sub prepare_for_deletion($, $, $)
{
}

# $self->delete($state)
sub delete($, $)
{
}

# $self->record_shared($recorder, $pkgname)
sub record_shared($, $, $)
{
}

sub copy_old_stuff($self, $plist, $state)
{
}

package OpenBSD::PackingElement::Cwd;

sub copy_old_stuff($self, $plist, $state)
{
	$self->add_object($plist);
}

package OpenBSD::PackingElement::FileObject;
use File::Basename;

sub mark_directory($self, $state, $dir)
{
	$state->{dirs_okay}{$dir} = 1;
	my $d2 = dirname($dir);
	if ($d2 ne $dir) {
		$self->mark_directory($state, $d2);
	}
}

sub mark_dir($self, $state)
{
	$self->mark_directory($state, dirname($self->fullname));
}

sub do_not_delete($self, $state)
{
	my $realname = $self->realname($state);
	$state->{baddelete} = 1;
	$self->{stillaround} = 1;

	delete $self->{symlink};
	delete $self->{link};
	my $algo = $self->{d};
	delete $self->{d};

	if (-l $realname) {
		$self->{symlink} = readlink $realname;
	} elsif (-f _) {
		$self->{d} = $self->compute_digest($realname, $algo);
	} elsif (-d _) {
		# what should we do ?
	}
}


package OpenBSD::PackingElement::DirlikeObject;
sub mark_dir($self, $state)
{
	$self->mark_directory($state, $self->fullname);
}

package OpenBSD::PackingElement::RcScript;
# XXX we should check stuff more thoroughly

sub delete($self, $state)
{
	$state->{delete_rcscripts}{$self->fullname} = 1;
	$self->SUPER::delete($state);
}

package OpenBSD::PackingElement::NewUser;
sub delete($self, $state)
{
	if ($state->verbose >= 2) {
		$state->say("rmuser: #1", $self->name);
	}

	$self->record_shared($state->{recorder}, $state->{pkgname});
}

sub record_shared($self, $recorder, $pkgname)
{
	$recorder->{users}{$self->name} = $pkgname;
}

package OpenBSD::PackingElement::NewGroup;
sub delete($self, $state)
{
	if ($state->verbose >= 2) {
		$state->say("rmgroup: #1", $self->name);
	}

	$self->record_shared($state->{recorder}, $state->{pkgname});
}

sub record_shared($self, $recorder, $pkgname)
{
	$recorder->{groups}{$self->name} = $pkgname;
}

package OpenBSD::PackingElement::DirBase;
sub prepare_for_deletion($self, $state, $pkgname)
{
	$state->vstat->remove_directory(
	    $self->retrieve_fullname($state, $pkgname), $self);
}

sub delete($self, $state)
{
	if ($state->verbose >= 5) {
		$state->say("rmdir: #1", $self->fullname);
	}

	$self->record_shared($state->{recorder}, $state->{pkgname});
}

sub record_shared($self, $recorder, $pkgname)
{
	# enough for the entry to exist, we only record interesting
	# entries more thoroughly
	$recorder->{dirs}{$self->fullname} //= [];
}

package OpenBSD::PackingElement::Mandir;
sub record_shared($self, $recorder, $pkgname)
{
	$self->{pkgname} = $pkgname;
	push(@{$recorder->{dirs}{$self->fullname}} , $self);
}

package OpenBSD::PackingElement::Fontdir;
sub record_shared($self, $recorder, $pkgname)
{
	$self->{pkgname} = $pkgname;
	push(@{$recorder->{dirs}{$self->fullname}} , $self);
	$recorder->{fonts_todo}{$self->fullname} = 1;
}

package OpenBSD::PackingElement::Infodir;
sub record_shared	# forwarder
{
	&OpenBSD::PackingElement::Mandir::record_shared;
}

package OpenBSD::PackingElement::Unexec;
sub delete($self, $state)
{
	if ($self->should_run($state)) {
		$self->run($state);
	}
}

sub should_run($, $) { 1 }

package OpenBSD::PackingElement::UnexecDelete;
sub should_run($self, $state)
{
	return !$state->replacing;
}

package OpenBSD::PackingElement::UnexecUpdate;
sub should_run($self, $state)
{
	return $state->replacing;
}

package OpenBSD::PackingElement::DefineTag::Atend;
sub delete($self, $state)
{
	if (!$state->replacing) {
		$state->{tags}{deleted}{$self->name} = 1;
	}
}


package OpenBSD::PackingElement::Tag;
sub delete($self, $state)
{
	for my $d (@{$self->{definition_list}}) {
		$d->add_tag($self, "delete", $state);
	}
}

package OpenBSD::PackingElement::FileBase;
use OpenBSD::Error;

sub prepare_for_deletion($self, $state, $pkgname)
{
	my $fname = $self->retrieve_fullname($state, $pkgname);
	my $s;
	my $size = $self->{tied} ? 0 : $self->retrieve_size;
	if ($state->{delete_first}) {
		$s = $state->vstat->remove_first($fname, $size);
	} else {
		$s = $state->vstat->remove($fname, $size);
	}
	return unless defined $s;
	if ($s->ro) {
		$s->report_ro($state, $fname);
	}
}

sub is_intact($self, $state, $realname)
{
	return 1 if defined($self->{link}) or $self->{nochecksum};
	if (!defined $self->{d}) {
		if ($self->fullname eq $realname) {
			$state->say("NOT deleting #1 (no checksum)", $realname);
		} else {
			$state->say("Not deleting #1 (no checksum for #2",
			    $realname, $self->fullname);
		}
		$state->log("Couldn't delete #1 (no checksum)", $realname);
		return 0;
	}
	return 1 unless $state->defines('checksum');
	my $d = $self->compute_digest($realname, $self->{d});
	return 1 if $d->equals($self->{d});
	if ($self->fullname eq $realname) {
		$state->say("NOT deleting #1 (bad checksum)", $realname);
	} else {
		$state->say("Not deleting #1 (bad checksum for #2)",
		    $realname, $self->fullname);
	}
	$state->log("Couldn't delete #1 (bad checksum)", $realname);
	return 0;
}

sub delete($self, $state)
{
	my $realname = $self->realname($state);
	return if defined $state->{current_set}{dont_delete}{$realname};

	if (defined $self->{symlink}) {
		if (-l $realname) {
			my $contents = readlink $realname;
			if ($contents ne $self->{symlink}) {
				$state->say("Symlink does not match: #1 (#2 vs. #3)",
				    $realname, $contents, $self->{symlink});
				$self->do_not_delete($state);
				return;
			}
		} else  {
			if (-e $realname) {
				$state->say("Bogus symlink: #1", $realname);
				$self->do_not_delete($state);
			} else {
				$state->say("Can't delete missing symlink: #1",
				    $realname);
			}
			return;
		}
	} else {
		if (-l $realname) {
				$state->say("Unexpected symlink: #1", $realname);
				$self->do_not_delete($state);
		} else {
			if (!-f $realname) {
				$state->say("File #1 does not exist", $realname);
				return;
			}
			if (!$self->is_intact($state, $realname)) {
				$self->do_not_delete($state);
				return;
			}
		}
	}
	if ($state->verbose >= 5) {
		$state->say("deleting: #1", $realname);
	}
	return if $state->{not};
	if ($state->{delete_first} && $self->{tied}) {
		push(@{$state->{delayed}}, $realname);
	} else {
		if (!unlink $realname) {
			$state->errsay("Problem deleting #1: #2", $realname, 
			    $!);
			$state->log("deleting #1 failed: #2", $realname, $!);
		}
	}
}

sub copy_old_stuff($self, $plist, $state)
{
	if (defined $self->{stillaround}) {
		delete $self->{stillaround};
		if ($state->replacing) {
			$self->rename_file_to_temp($state);
		}
		$self->add_object($plist);
	}
}

package OpenBSD::PackingElement::SpecialFile;
use OpenBSD::PackageInfo;

sub copy_old_stuff($, $, $)
{
}

package OpenBSD::PackingElement::Meta;
sub copy_old_stuff($self, $plist, $state)
{
	$self->add_object($plist);
}

package OpenBSD::PackingElement::DigitalSignature;
sub copy_old_stuff($, $, $)
{
}

package OpenBSD::PackingElement::FDESC;
sub copy_old_stuff($self, $plist, $state)
{
	require File::Copy;

	File::Copy::copy($self->fullname, $plist->infodir);
	$self->add_object($plist);
}

package OpenBSD::PackingElement::Sample;
use OpenBSD::Error;
use File::Basename;

sub delete($self, $state)
{
	my $realname = $self->realname($state);

	my $orig = $self->{copyfrom};
	if (!defined $orig) {
		$state->fatal("\@sample element does not reference a valid file");
	}
	my $action = $state->replacing ? "check" : "remove";
	my $origname = $orig->realname($state);
	if (! -e $realname) {
		$state->log("File #1 does not exist", $realname);
		return;
	}
	if (! -f $realname) {
		$state->log("File #1 is not a file", $realname);
		return;
	}

	if (!defined $orig->{d}) {
		$state->log("Couldn't delete #1 (no checksum)", $realname);
		return;
	}

	if ($state->{quick} && $state->{quick} >= 2) {
		unless ($state->{extra}) {
			$self->mark_dir($state);
			$state->log("You should also #1 #2", $action, $realname );
			return;
		}
	} else {
		my $d = $self->compute_digest($realname, $orig->{d});
		if ($d->equals($orig->{d})) {
			$state->say("File #1 identical to sample", $realname) if $state->verbose >= 2;
		} else {
			unless ($state->{extra}) {
				$self->mark_dir($state);
				$state->log("You should also #1 #2 (which was modified)", $action, $realname);
				return;
			}
		}
	}
	$state->say("deleting #1", $realname) if $state->verbose >= 2;
	return if $state->{not};
	if (!unlink $realname) {
		$state->errsay("Problem deleting #1: #2", $realname, $!);
		$state->log("deleting #1 failed: #2", $realname, $!);
	}
}


package OpenBSD::PackingElement::InfoFile;
use File::Basename;
use OpenBSD::Error;
sub delete($self, $state)
{
	unless ($state->{not}) {
	    my $fullname = $state->{destdir}.$self->fullname;
	    $state->vsystem(OpenBSD::Paths->install_info,
		"--delete", "--info-dir=".dirname($fullname), '--', $fullname);
	}
	$self->SUPER::delete($state);
}

package OpenBSD::PackingElement::Shell;
sub delete($self, $state)
{
	unless ($state->{not}) {
		my $destdir = $state->{destdir};
		my $fullname = $self->fullname;
		my @l=();
		if (open(my $shells, '<', $destdir.OpenBSD::Paths->shells)) {
			while(<$shells>) {
				push(@l, $_);
				s/^\#.*//o;
				if ($_ =~ m/^\Q$fullname\E\s*$/) {
					pop(@l);
				}
			}
			close($shells);
			open(my $shells2, '>', $destdir.OpenBSD::Paths->shells);
			print $shells2 @l;
			close $shells2;
			$state->say("Shell #1 removed from #2",
			    $fullname, $destdir.OpenBSD::Paths->shells)
			    	if $state->verbose;
		}
	}
	$self->SUPER::delete($state);
}

package OpenBSD::PackingElement::Extra;
use File::Basename;

sub delete($self, $state)
{
	return if defined $state->{current_set}{known_extra}{$self->fullname};
	my $realname = $self->realname($state);
	if ($state->verbose >= 2 && $state->{extra}) {
		$state->say("deleting extra file: #1", $realname);
	}
	return if $state->{not};
	return unless -e $realname or -l $realname;
	if ($state->{extra}) {
		unlink($realname) or
		    $state->say("problem deleting extra file #1: #2", $realname, $!);
	} elsif (!$state->{update}) {
		$state->log("You should also remove #1", $realname);
		$self->mark_dir($state);
	}
}


package OpenBSD::PackingElement::Extradir;
sub delete($self, $state)
{
	return unless $state->{extra};
	return if defined $state->{current_set}{known_extra}{$self->fullname};
	my $realname = $self->realname($state);
	if ($state->{extra}) {
		$self->SUPER::delete($state);
	} elsif (!$state->{update}) {
		$state->log("You should also remove the directory #1", $realname);
		$self->mark_dir($state);
	}
}

package OpenBSD::PackingElement::ExtraUnexec;

sub delete($self, $state)
{
	if ($state->{extra}) {
		$self->run($state);
	} elsif (!$state->{update}) {
		$state->log("You should also run #1", $self->{expanded});
	}
}

package OpenBSD::PackingElement::Lib;
sub delete($self, $state)
{
	$self->SUPER::delete($state);
	$self->mark_ldconfig_directory($state);
}

package OpenBSD::PackingElement::Depend;
sub copy_old_stuff($self, $plist, $state)
{
	OpenBSD::PackingElement::Comment->add($plist, 
	    "\@".$self->keyword." ".$self->stringize);
}

package OpenBSD::PackingElement::FDISPLAY;
sub delete($self, $state)
{
	$state->{current_set}{known_displays}{$self->{d}->key} = 1;
	$self->SUPER::delete($state);
}

package OpenBSD::PackingElement::FUNDISPLAY;
sub delete($self, $state)
{
	my $d = $self->{d};
	if (!$state->{current_set}{known_displays}{$self->{d}->key}) {
		$self->prepare($state);
	}
	$self->SUPER::delete($state);
}

package OpenBSD::PackingElement::Mandir;
sub delete($self, $state)
{
	$state->{current_set}{known_mandirs}{$self->fullname} = 1;
	$self->SUPER::delete($state);
}

1;
