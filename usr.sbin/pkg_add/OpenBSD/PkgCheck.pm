#! /usr/bin/perl

# ex:ts=8 sw=4:
# $OpenBSD: PkgCheck.pm,v 1.81 2023/06/16 10:38:29 espie Exp $
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

use OpenBSD::AddCreateDelete;

package Installer::State;
our @ISA = qw(OpenBSD::PkgAdd::State);
sub new($class, $cmd)
{
	my $state = $class->SUPER::new($cmd);
	$state->{localbase} = OpenBSD::Paths->localbase;
	return $state;
}

package Installer;
our @ISA = qw(OpenBSD::PkgAdd);

sub new($class, $mystate)
{
	my $state = Installer::State->new("pkg_check");
	$state->{v} = $mystate->{v};
	$state->{subst} = $mystate->{subst};
	$state->{interactive} = $mystate->{interactive};
	$state->{destdir} = $mystate->{destdir};
	$state->{signature_style} = $mystate->{signature_style};
	$state->progress->setup($state->opt('x'), $state->opt('m'), $state);
	bless { state => $state}, $class;
}

sub install($self, $pkg)
{
	my $state = $self->{state};
	push(@{$state->{setlist}}, 
	    $state->updateset->add_hints2($pkg));
	$self->try_and_run_command($state);
	return $state->{bad} != 0;
}

package OpenBSD::PackingElement;
sub thorough_check($self, $state)
{
	$self->basic_check($state);
}

sub basic_check($, $)
{
	1
}

# $self->find_dpendencies($state, $l, $checker, $pkgname)
sub find_dependencies($, $, $, $, $)
{
}

# XXX this is a snag for ShareLibs OO-ness
# $self->mark_indirect_depends($pkgname, $state)
sub mark_indirect_depends($self, $pkgname, $state)
{
	$self->mark_available_lib($pkgname, $state->shlibs);
}

# $self->cache_depends($copy)
sub cache_depends($, $)
{
}

package OpenBSD::PackingElement::DefineTag;

sub mark_indirect_depends($self, $pkgname, $state)
{
	$state->{tagdefinition}{$self->name} = $pkgname;
}

package OpenBSD::PackingElement::FileBase;
use File::Basename;

sub basic_check($self, $state)
{
	my $name = $state->destdir($self->fullname);
	$state->{known}{dirname($name)}{basename($name)} = 1;
	if ($self->{symlink}) {
		if (!-l $name) {
			if (!-e $name) {
				$state->log("#1 should be a symlink but does not exist", $name);
			} else {
				$state->log("#1 is not a symlink", $name);
			}
			return 0;
		} else {
			if (readlink($name) ne $self->{symlink}) {
				$state->log("#1 should point to #2 but points to #3 instead",
				    $name, $self->{symlink}, readlink($name));
				return 0;
			}
		}
		return 1;
	}
	if (!-e $name) {
		if (-l $name) {
			$state->log("#1 points to non-existent #2",
			    $name, readlink($name));
		} else {
			$state->log("#1 should exist", $name);
		}
	    	return 0;
	} elsif (!-f _) {
		$state->log("#1 is not a file", $name);
		return 0;
	}
	if ($self->{link}) {
		my ($a, $b) = (stat _)[0, 1];
		if (!-f $state->destdir($self->{link})) {
			$state->log("#1 should link to non-existent #2",
			    $name, $self->{link});
			return 0;
		} else {
			my ($c, $d) = (stat _)[0, 1];
			if (defined $a && defined $c) {
				if ($a != $c || $b != $d) {
					$state->log("#1 doesn't link to #2",
					    $name, $self->{link});
					return 0;
				}
			}

		}
	}
	return 1;
}

sub thorough_check($self, $state)
{
	my $name = $state->destdir($self->fullname);
	if (!$self->basic_check($state)) {
		return;
	}
	return if $self->{link} or $self->{symlink} or $self->{nochecksum};
	if (!-r $name) {
		$state->log("can't read #1", $name);
		return;
	}
	if (!defined $self->{d}) {
		$state->log("no checksum for #1", $name);
		return;
	}
	my $d = $self->compute_digest($name, $self->{d});
	if (!$d->equals($self->{d})) {
		$state->log("checksum for #1 does not match", $name);
	}
}

package OpenBSD::PackingElement::SpecialFile;
sub basic_check	# forwarder
{
	&OpenBSD::PackingElement::FileBase::basic_check;
}

sub thorough_check	# forwarder
{
	&OpenBSD::PackingElement::FileBase::basic_check;
}

package OpenBSD::PackingElement::DirlikeObject;
sub basic_check($self, $state)
{
	my $name = $state->destdir($self->fullname);
	$state->{known}{$name} //= {};
	if (!-e $name) {
		$state->log("#1 should exist", $name);
	}
	if (!-d _) {
		$state->log("#1 is not a directory", $name);
	}
}

package OpenBSD::PackingElement::Sample;
use File::Basename;
sub basic_check($self, $state)
{
	my $name = $state->destdir($self->fullname);
	$state->{known}{dirname($name)}{basename($name)} = 1;
	return 1;
}

package OpenBSD::PackingElement::Sampledir;
sub basic_check($self, $state)
{
	my $name = $state->destdir($self->fullname);
	$state->{known}{$name} //= {};
	return 1;
}

package OpenBSD::PackingElement::Mandir;
sub basic_check($self, $state)
{
	$self->SUPER::basic_check($state);
	my $name = $state->destdir($self->fullname);
	for my $file (OpenBSD::Paths->man_cruft) {
		$state->{known}{$name}{$file} = 1;
	}
	return 1;
}

package OpenBSD::PackingElement::Fontdir;
sub basic_check($self, $state)
{
	$self->SUPER::basic_check($state);
	my $name = $state->destdir($self->fullname);
	for my $i (qw(fonts.alias fonts.scale fonts.dir)) {
		$state->{known}{$name}{$i} = 1;
	}
	return 1;
}

package OpenBSD::PackingElement::Infodir;
sub basic_check($self, $state)
{
	$self->SUPER::basic_check($state);
	my $name = $state->destdir($self->fullname);
	$state->{known}{$name}{'dir'} = 1;
	return 1;
}

package OpenBSD::PackingElement::Depend;
sub cache_depends($self, $copy)
{
	$self->add_object($copy);
}

package OpenBSD::PackingElement::Dependency;
sub find_dependencies($self, $state, $l, $checker, $pkgname)
{
	# several ways to failure
	if (!$self->spec->is_valid) {
		$state->log("invalid \@", $self->keyword, " ",
		    $self->stringize);
		return;
	}
	my @deps = $self->spec->filter(@$l);
	if (@deps == 0) {
		$state->log("dependency #1 in #2 does not match any installed package",
		    $self->stringize, $pkgname);
		return;
	}
	my $okay = 0;
	for my $i (@deps) {
		if ($checker->find($i)) {
			$okay = 1;
		}
	}
	if (!$okay) {
		$checker->not_found($deps[0]);
	}
}

package OpenBSD::PackingElement::Wantlib;
sub find_dependencies($self, $state, $l, $checker, $pkgname)
{
	my $r = $state->shlibs->lookup_libspec($state->{localbase},
	    $self->spec);
	if (defined $r && @$r != 0) {
		my $okay = 0;
		for my $lib (@$r) {
			my $i = $lib->origin;
			if ($i eq 'system') {
				$okay = 1;
				$state->{needed_libs}{$lib->to_string} = 1;
				next;
			}
			if ($checker->find($i)) {
				$okay = 1;
			}
		}
		if (!$okay) {
			$checker->not_found($r->[0]->origin);
		}
	} else {
		$state->log("#1 in #2 not found", $self->stringize, $pkgname);
	}
}

package OpenBSD::PackingElement::Tag;
sub find_dependencies($self, $state, $l, $checker, $pkgname)
{
	my $location = $state->{tagdefinition}{$self->name};
	if (defined $location) {
		if ($location eq $pkgname) {
			return;
		}
		if (!$checker->find($location)) {
			$checker->not_found($location);
		} 
	} else {
		$state->log("definition for #1 not found", $self->stringize);
	}
}

sub cache_depends	# forwarder
{
	&OpenBSD::PackingElement::Depend::cache_depends;
}

package OpenBSD::PkgCheck::State;
our @ISA = qw(OpenBSD::AddCreateDelete::State);

use File::Spec;
use OpenBSD::Log;
use File::Basename;

sub init($self)
{
	$self->{l} = OpenBSD::Log->new($self);
	$self->SUPER::init;
}

sub log($self, @p)
{
	if (@p == 0) {
		return $self->{l};
	} else {
		$self->{l}->say(@p);
	}
}

sub handle_options($self)
{
	$self->{no_exports} = 1;

	$self->add_interactive_options;
	$self->SUPER::handle_options('fFB:q',
		'[-FfIimnqvx] [-B pkg-destdir] [-D value]');
	$self->{force} = $self->opt('f');
	$self->{quick} = $self->opt('q') // 0;
	$self->{filesystem} = $self->opt('F');
	if (defined $self->opt('B')) {
		$self->{destdir} = $self->opt('B');
	} 
	if (defined $self->{destdir}) {
		$self->{destdir} .= '/';
	} else {
		$self->{destdir} = '';
	}
}

sub destdir($self, $path)
{
	return File::Spec->canonpath($self->{destdir}.$path);
}

sub process_entry($self, $entry)
{
	my $name = $self->destdir($entry);
	$self->{known}{dirname($name)}{basename($name)} = 1;
}

package OpenBSD::DependencyCheck;

sub new($class, $state, $name, $req)
{
	my $o = bless {
		not_yet => {},
		possible => {},
		others => {},
		name => $name,
		req => $req
	    }, $class;
	for my $pkg ($req->list) {
		$o->{not_yet}{$pkg} = 1;
		if ($state->{exists}{$pkg}) {
			$o->{possible}{$pkg} = 1;
		} else {
			$state->errsay("#1: bogus #2", $name, $o->string($pkg));
		}
	}
	return $o;
}

sub find($self, $name)
{
	if ($self->{possible}{$name}) {
		delete $self->{not_yet}{$name};
		return 1;
	} else {
		return 0;
	}
}

sub not_found($self, $name)
{
	$self->{others}{$name} = 1;
}

sub ask_delete_deps($self, $state, $l)
{
	if ($state->{force}) {
		$self->{req}->delete(@$l);
	} elsif ($state->confirm_defaults_to_no(
	    "Remove extra #1", $self->string(@$l))) {
			$self->{req}->delete(@$l);
	}
}

sub ask_add_deps($self, $state, $l)
{
	if ($state->{force}) {
		$self->{req}->add(@$l);
	} elsif ($state->confirm_defaults_to_no(
	    "Add missing #1", $self->string(@$l))) {
			$self->{req}->add(@$l);
	}
}

sub adjust($self, $state)
{
	if (keys %{$self->{not_yet}} > 0) {
		my @todo = sort keys %{$self->{not_yet}};
		unless ($state->{subst}->value("weed_libs")) {
			@todo = grep {!/^\.libs/} @todo;
		}
		if (@todo != 0) {
			$state->errsay("#1 has too many #2",
			    $self->{name}, $self->string(@todo));
			$self->ask_delete_deps($state, \@todo);
		}
	}
	if (keys %{$self->{others}} > 0) {
		my @todo = sort keys %{$self->{others}};
		$state->errsay("#1 is missing #2",
		    $self->{name}, $self->string(@todo));
		    if ($self->{name} =~ m/^partial/) {
			    $state->errsay("not a problem, since this is a partial- package");
		    } else {
			    $self->ask_add_deps($state, \@todo);
		    }
	}
}

package OpenBSD::DirectDependencyCheck;
our @ISA = qw(OpenBSD::DependencyCheck);
use OpenBSD::RequiredBy;
sub string($self, @p)
{
	return "dependencies: ". join(' ', @p);
}

sub new($class, $state, $name)
{
	return $class->SUPER::new($state, $name,
	    OpenBSD::Requiring->new($name));
}

package OpenBSD::ReverseDependencyCheck;
our @ISA = qw(OpenBSD::DependencyCheck);
use OpenBSD::RequiredBy;
sub string($self, @p)
{
	return "reverse dependencies: ". join(' ', @p);
}

sub new($class, $state, $name)
{
	return $class->SUPER::new($state, $name,
	    OpenBSD::RequiredBy->new($name));
}

package OpenBSD::Pkglocate;
sub new($class, $state)
{
	bless {state => $state, result => {unknown => []}, 
	    params => []}, $class;
}

sub add_param($self, @p)
{
	push(@{$self->{params}}, @p);
	while (@{$self->{params}} > 200) {
		$self->run_command;
	}
}

sub run_command($self)
{
	if (@{$self->{params}} == 0) {
		return;
	}
	my %h = map {($_, 1)} @{$self->{params}};
	open(my $cmd, '-|', 'pkg_locate', map {"*:$_"} @{$self->{params}});
	while (<$cmd>) {
		chomp;
		my ($pkgname, $pkgpath, $path) = split(':', $_, 3);

		# pkglocate will return false positives, so trim them
		if ($h{$path}) {
			push(@{$self->{result}{"$pkgname:$pkgpath"} }, $path);
			delete $h{$path};
		}
	}
	close($cmd);
	for my $k (keys %h) {
		push(@{$self->{result}{unknown}}, $k);
	}

	$self->{params} = [];
}

sub show_results($self)
{
	while (@{$self->{params}} > 0) {
		$self->run_command;
	}
	my $state = $self->{state};
	my $r = $self->{result};
	my $u = $r->{unknown};
	delete $r->{unknown};

	$state->say("Not found:");
	for my $e (sort @$u) {
		$state->say("\t#1", $e);
	}

	for my $k (sort keys %{$r}) {
		$state->say("In #1:", $k);
		for my $e (sort @{$r->{$k}}) {
			$state->say("\t#1", $e);
		}
	}
}

package OpenBSD::PkgCheck;
our @ISA = qw(OpenBSD::AddCreateDelete);

use OpenBSD::PackageInfo;
use OpenBSD::PackingList;
use File::Find;
use OpenBSD::Paths;
use OpenBSD::Mtree;

sub fill_base_system($self, $state)
{
	open(my $cmd, '-|', 'locate', 
	    '-d', OpenBSD::Paths->srclocatedb,
	    '-d', OpenBSD::Paths->xlocatedb, ':');
	while (<$cmd>) {
		chomp;
		my ($set, $path) = split(':', $_, 2);
		$state->{basesystem}{$path} = 1;
	}
	close($cmd);
}

sub remove($self, $state, $name)
{
	$state->{removed}{$name} = 1;
	my $dir = installed_info($name);
	for my $i (@OpenBSD::PackageInfo::info) {
		if (-e $dir.$i) {
			if ($state->verbose) {
				$state->say("unlink(#1)", $dir.$i);
			}
			unless ($state->{not}) {
				unlink($dir.$i) or
				    $state->errsay("#1: Couldn't delete #2: #3",
				    	$name, $dir.$i, $!);
			}
		}
	}
	if (-f $dir) {
		if ($state->verbose) {
			$state->say("unlink(#1)", $dir);
		}
		unless ($state->{not}) {
			unlink($dir) or
			    $state->errsay("#1: Couldn't delete #2: #3",
				$name, $dir, $!);
		}
	} elsif (-d $dir) {
		if ($state->verbose) {
			$state->say("rmdir(#1)", $dir);
		}
		unless ($state->{not}) {
			rmdir($dir) or
			    $state->errsay("#1: Couldn't delete #2: #3",
			    	$name, $dir, $!);
		}
	}
}

sub may_remove($self, $state, $name)
{
	if ($state->{force}) {
		$self->remove($state, $name);
	} elsif ($state->confirm_defaults_to_no(
	    "Remove wrong package #1", $name)) {
			$self->remove($state, $name);
	}
	$state->{bogus}{$name} = 1;
}

sub may_unlink($self, $state, $path)
{
	if (!$state->{force} && 
	    !$state->confirm_defaults_to_no("Remove #1", $path)) {
		return;
	}
	if ($state->verbose) {
		$state->say("remove #1", $path);
	}
	return if $state->{not};
	unlink($path) or rmdir($path) or
	    $state->errsay("Couldn't delete #1: #2", $path, $!);
}

sub may_fix_ownership($self, $state, $path)
{
	if (!$state->{force} && 
	    !$state->confirm_defaults_to_no("Give #1 to root:wheel", $path)) {
		return;
	}
	if ($state->verbose) {
		$state->say("chown root:wheel #1", $path);
	}
	return if $state->{not};
	chown 0, 0, $path or
	    $state->errsay("Couldn't fix ownership for #1: #2", $path, $!);
}

sub may_fix_perms($self, $state, $path, $perm, $readable)
{
	if (!$state->{force} && 
	    !$state->confirm_defaults_to_no("Make #1 #2", $path,
	    ($readable ? "not world/group-writable" : "world readable"))) {
		return;
	}
	if ($state->verbose) {
		$state->say("chmod #1 #2", sprintf("%04o", $perm), $path);
	}
	return if $state->{not};
	chmod $perm, $path or
	    $state->errsay("Couldn't fix perms for #1: #2", $path, $!);
}

sub for_all_packages($self, $state, $l, $msg, $code)
{
	$state->progress->for_list($msg, $l,
	    sub($name) {
		return if $state->{removed}{$name};
		if ($state->{bogus}{$name}) {
			$state->errsay("skipping #1", $name);
			return;
		}
		&$code($name);
	    });
}

sub check_dir_permissions($self, $state, $dir)
{
	my ($perm, $uid, $gid) = (stat $dir)[2, 4, 5];
	$perm &= 0777;

	if (($perm & 0555) != 0555) {
		$state->errsay("Directory #1 is not world-readable", $dir);
		$perm |= 0555;
		$self->may_fix_perms($state, $dir, $perm, 0);
	}
	if ($uid != 0 || $gid != 0) {
		$state->errsay("Directory #1 does not belong to root:wheel",
		    $dir);
	    	$self->may_fix_ownership($state, $dir);
	}
	if (($perm & 0022) != 0) {
		$state->errsay("Directory #1 is world/group writable", $dir);
		$perm &= 0755;
		$self->may_fix_perms($state, $dir, $perm, 1);
	}
}

sub check_permissions($self, $state, $dir)
{
	$self->check_dir_permissions($state, $dir);
	opendir(my $d, $dir) or return;
	for my $name (readdir $d) {
		next if $name eq '.' or $name eq '..';
		my $file = $dir.$name;
		if (!grep {$_ eq $name} (@OpenBSD::PackageInfo::info)) {
			$state->errsay("Weird filename in pkg db: #1",
			    $file);
			$self->may_unlink($state, $file);
			next;
		}
		my ($perm, $uid, $gid) = (stat $file)[2, 4, 5];
		if (!-f $file) {
			$state->errsay("#1 should be a file", $file);
			$self->may_unlink($state, $file);
			next;
		}
		$perm &= 0777;
		if (($perm & 0444) != 0444) {
			$state->errsay("File #1 is not world-readable", $file);
			$perm |= 0444;
			$self->may_fix_perms($state, $file, $perm, 0);
		}
		if ($uid != 0 || $gid != 0) {
			$state->errsay("File #1 does not belong to root:wheel",
			    $file);
			$self->may_fix_ownership($state, $file);
		}
		if (($perm & 0022) != 0) {
			$state->errsay("File #1 is world/group writable",
			    $file);
			$perm &= 0755;
			$self->may_fix_perms($state, $file, $perm, 1);
		}
	}
	closedir($d);
}


sub sanity_check($self, $state, $l)
{
	# let's find /var/db/pkg or its equivalent
	my $base = installed_info("");
	$base =~ s,/*$,,;
	$self->check_dir_permissions($state, $base);

	$self->for_all_packages($state, $l, "Packing-list sanity", sub($name) {
		if ($name ne $state->safe($name)) {
			$state->errsay("#1: bogus pkgname", $name);
			$self->may_remove($state, $name);
			return;
		}
		my $info = installed_info($name);
		if (-f $info) {
			$state->errsay("#1: #2 should be a directory",
			    $name, $info);
			if ($info =~ m/\.core$/) {
				$state->errsay("looks like a core dump, ".
					"removing");
				$self->remove($state, $name);
			} else {
				$self->may_remove($state, $name);
			}
			return;
		}
		$self->check_permissions($state, $info);
		my $contents = $info.OpenBSD::PackageInfo::CONTENTS;
		unless (-f $contents) {
			$state->errsay("#1: missing #2", $name, $contents);
			$self->may_remove($state, $name);
			return;
		}
		my $plist;
		eval {
			$plist = OpenBSD::PackingList->fromfile($contents);
		};
		if ($@ || !defined $plist) {
			$state->errsay("#1: bad packing-list", $name);
			if ($@) {
				$state->errsay("#1", $@);
			}
			$self->may_remove($state, $name);
			return;
		}
		if (!defined $plist->pkgname) {
			$state->errsay("#1: no pkgname in plist", $name);
			$self->may_remove($state, $name);
			return;
		}
		if ($plist->pkgname ne $name) {
			$state->errsay("#1: pkgname does not match", $name);
			$self->may_remove($state, $name);
		}
		$plist->mark_indirect_depends($plist->pkgname, $state);
		my $p = OpenBSD::PackingList->new;
		$plist->cache_depends($p);
		$state->{plist_cache}{$plist->pkgname} = $p;
		$state->{exists}{$plist->pkgname} = 1;
	});
}

sub dependencies_check($self, $state, $l)
{
	$state->shlibs->add_libs_from_system($state->{destdir});
	$self->for_all_packages($state, $l, "Direct dependencies", sub($name) {
		$state->log->set_context($name);
		my $plist = $state->{plist_cache}{$name};
		my $checker = OpenBSD::DirectDependencyCheck->new($state,
		    $name);
		$state->{localbase} = $plist->localbase;
		$plist->find_dependencies($state, $l, $checker, $name);
		$checker->adjust($state);
		for my $dep ($checker->{req}->list) {
			push(@{$state->{reverse}{$dep}}, $name);
		}
	});
	delete $state->{plist_cache};
}

sub reverse_dependencies_check($self, $state, $l)
{
	$self->for_all_packages($state, $l, "Reverse dependencies", sub($name) {
		my $checker = OpenBSD::ReverseDependencyCheck->new($state,
		    $name);
		for my $i (@{$state->{reverse}{$name}}) {
			$checker->find($i) or $checker->not_found($i);
		}
		$checker->adjust($state);
	});
}

sub package_files_check($self, $state, $l)
{
	$self->for_all_packages($state, $l, "Files from packages", sub($name) {
		my $plist = OpenBSD::PackingList->from_installation($name);
		$state->log->set_context($name);
		if ($state->{quick}) {
			$plist->basic_check($state);
		} else {
			$plist->thorough_check($state);
		}
		$plist->mark_available_lib($plist->pkgname, $state->shlibs);
	});
}

sub install_pkglocate($self, $state)
{
	my $spec = 'pkglocatedb->=1.1';

	my @l = installed_stems()->find('pkglocatedb');
	require OpenBSD::PkgSpec;
	if (OpenBSD::PkgSpec->new($spec)->match_ref(\@l)) {
		return 1;
	}
	unless ($state->confirm_defaults_to_no("Unknown file system entries.\n".
	    "Do you want to install $spec to look them up")) {
	    	return 0;
	}

	require OpenBSD::PkgAdd;

	$state->{installer} //= Installer->new($state);
	if ($state->{installer}->install('pkglocatedb--')) {
		return 1;
	} else {
		$state->errsay("Couldn't install #1", $spec);
		return 0;
	}
}

# non fancy display of unknown objects
sub display_unknown($self, $state)
{
	if (defined $state->{unknown}{file}) {
		$state->say("Unknown files:");
		for my $e (sort @{$state->{unknown}{file}}) {
			$state->say("\t#1", $e);
		}
	}
	if (defined $state->{unknown}{dir}) {
		$state->say("Unknown directories:");
		for my $e (sort {$b cmp $a } @{$state->{unknown}{dir}}) {
			$state->say("\t#1", $e);
		}
	}
}

sub display_tmps($self, $state)
{
	$state->say("Unregistered temporary files:");
	for my $e (sort @{$state->{tmps}}) {
		$state->say("\t#1", $e);
	}
	if ($state->{force}) {
		unlink(@{$state->{tmps}});
	} elsif ($state->confirm_defaults_to_no("Remove")) {
			unlink(@{$state->{tmps}});
	}
}

sub display_unregs($self, $state)
{
	$state->say("System libs NOT in locate dbs:");
	for my $e (sort @{$state->{unreg_libs}}) {
		$state->say("\t#1", $e);
	}
}

sub locate_unknown($self, $state)
{
	my $locator = OpenBSD::Pkglocate->new($state);
	if (defined $state->{unknown}{file}) {
		$state->progress->for_list("Locating unknown files", 
		    $state->{unknown}{file},
			sub($p) {
				$locator->add_param($p);
			});
	}
	if (defined $state->{unknown}{dir}) {
		$state->progress->for_list("Locating unknown directories", 
		    $state->{unknown}{dir},
			sub($p) {
				$locator->add_param($p);
			});
	}
	$locator->show_results;
}

sub fill_localbase($self, $state, $base)
{
	for my $file (OpenBSD::Paths->man_cruft) {
		$state->{known}{$base."/man"}{$file} = 1;
	}
	$state->{known}{$base."/info"}{'dir'} = 1;
	$state->{known}{$base."/lib/X11"}{'app-defaults'} = 1;
	$state->{known}{$base."/libdata"} = {};
	$state->{known}{$base."/libdata/perl5"} = {};
}

sub fill_root($self, $state, $root)
{
	OpenBSD::Mtree::parse($state->{known}, $root, 
	    '/etc/mtree/4.4BSD.dist', 1);
	OpenBSD::Mtree::parse($state->{known}, $root,
	    '/etc/mtree/BSD.x11.dist', 1);
}

sub filesystem_check($self, $state)
{
	$state->{known} //= {};
	$self->fill_localbase($state, 
	    $state->destdir(OpenBSD::Paths->localbase));
	my $root = $state->{destdir} || '/';
	$self->fill_root($state, $root);
	$self->fill_base_system($state);

	$state->progress->set_header("Checking file system");
	find(sub() {
		$state->progress->working(1024);
		if (-d $_) {
			for my $i ('/dev', '/home', OpenBSD::Paths->pkgdb, '/var/log', '/var/backups', '/var/cron', '/var/run', '/tmp', '/var/tmp') {
				if ($File::Find::name eq $state->destdir($i)) {
					$File::Find::prune = 1;
				}
			}
		}
		if (defined $state->{basesystem}{$File::Find::name}) {
			delete $state->{basesystem}{$File::Find::name};
			return;
		}
		if (defined $state->{needed_libs}{$File::Find::name}) {
			push(@{$state->{unreg_libs}}, $File::Find::name);
			return;
		}
		if (-d $_) {
			if ($_ eq "lost+found") {
				$state->say("fsck(8) info found: #1",
				    $File::Find::name);
				$File::Find::prune = 1;
				return;
			}
			# some directories we've got to ignore
			if (! -r -x _) {
				$File::Find::prune = 1;
				$state->errsay("can't enter #1", 
				    $File::Find::name);
			}
			return if defined $state->{known}{$File::Find::name};
			if (-l $_) {
				return if $state->{known}{$File::Find::dir}{$_};
			}
			push(@{$state->{unknown}{dir}}, $File::Find::name);
			$File::Find::prune = 1;
		} else {
			return if $state->{known}{$File::Find::dir}{$_};
			if (m/^pkg\..{10}$/) {
				push(@{$state->{tmps}}, $File::Find::name);
			} else {
				push(@{$state->{unknown}{file}}, 
				    $File::Find::name);
			}
		}
	}, $root);
	if (defined $state->{tmps}) {
		$self->display_tmps($state);
	}
	if (defined $state->{unreg_libs}) {
		$self->display_unregs($state);
	}
	if (defined $state->{unknown}) {
		if ($self->install_pkglocate($state)) {
			$self->locate_unknown($state);
		} else {
			$self->display_unknown($state);
		}
	}
}

sub run($self, $state)
{
	my $list = [installed_packages()];

	my $list2;
	if (@ARGV != 0) {
		$list2 = \@ARGV;
	} else {
		$list2 = $list;
	}
	$self->sanity_check($state, $list);
	$self->dependencies_check($state, $list);
	$state->log->dump;
	$self->reverse_dependencies_check($state, $list);
	$state->log->dump;
	if ($state->{quick} < 2) {
		$self->package_files_check($state, $list2);
		$state->log->dump;
	}
	if ($state->{filesystem}) {
		$self->filesystem_check($state);
		$state->progress->next;
	}
}

sub parse_and_run($self, $cmd)
{
	my $state = OpenBSD::PkgCheck::State->new($cmd);
	$state->handle_options;
	lock_db(0, $state) unless $state->{subst}->value('nolock');
	$self->run($state);
	return 0;
}

1;
