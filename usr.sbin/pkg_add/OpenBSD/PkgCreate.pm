#! /usr/bin/perl
# ex:ts=8 sw=4:
# $OpenBSD: PkgCreate.pm,v 1.200 2025/09/15 01:59:37 afresh1 Exp $
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
use OpenBSD::Dependencies::SolverBase;
use OpenBSD::Signer;

package OpenBSD::PkgCreate::State;
our @ISA = qw(OpenBSD::CreateSign::State);

sub init($self, @p)
{
	$self->{stash} = {};
	$self->SUPER::init(@p);
	$self->{simple_status} = 0;
}

sub stash($self, $key)
{
	return $self->{stash}{$key};
}

sub error($self, $msg, @p)
{
	$self->{bad}++;
	$self->progress->disable;
	# XXX the actual format is $msg.
	$self->errsay("Error: $msg", @p);
}

sub set_status($self, $status)
{
	if ($self->{simple_status}) {
		print "\n$status";
	} else {
		if ($self->progress->set_header($status)) {
			$self->progress->message('');
		} else {
			$| = 1;
			print "$status...";
			$self->{simple_status} = 1;
		}
	}
}

sub end_status($self)
{
	if ($self->{simple_status}) {
		print "\n";
	} else {
		$self->progress->clear;
	}
}

sub handle_options($state)
{
	$state->{system_version} = 0;
	$state->{opt} = {
	    'f' =>
		    sub($opt) {
			    push(@{$state->{contents}}, $opt);
		    },
	    'p' => 
		    sub($opt) {
			    $state->{prefix} = $opt;
		    },
	    'P' => sub($opt) {
			    $state->{dependencies}{$opt} = 1;
		    },
	    'V' => sub($opt) {
			    if ($opt !~ m/^\d+$/) {
			    	$state->usage("-V option requires a number");
			    }
			    $state->{system_version} += $opt;
		    },
	    'w' => sub($opt) {
			    $state->{libset}{$opt} = 1;
		    },
	    'W' => sub($opt) {
			    $state->{wantlib}{$opt} = 1;
		    },
	};
	$state->{no_exports} = 1;
	$state->SUPER::handle_options('p:f:d:M:U:u:A:B:P:V:w:W:qQS',
	    '[-nQqvSx] [-A arches] [-B pkg-destdir] [-D name[=value]]',
	    '[-L localbase] [-M displayfile] [-P pkg-dependency]',
	    '[-U undisplayfile] [-u userlist] [-V n] [-W wantedlib]',
	    '[-w libset] [-d desc -D COMMENT=value -f packinglist -p prefix]',
	    'pkg-name');

	my $base = '/';
	if (defined $state->opt('B')) {
		$base = $state->opt('B');
	} 

	$state->{base} = $base;
	# switch to silent mode for *any* introspection option
	$state->{silent} = defined $state->opt('n') || defined $state->opt('q')
	    || defined $state->opt('Q') || defined $state->opt('S');
	if (defined $state->opt('u')) {
		$state->{userlist} = $state->parse_userdb($state->opt('u'));
	}
	$state->{wrkobjdir} = $state->defines('WRKOBJDIR');
	$state->{fullpkgpath} = $state->{subst}->value('FULLPKGPATH') // '';
	$state->{no_ts_in_plist} = $state->defines('NO_TS_IN_PLIST');
}

sub parse_userdb($self, $fname)
{
	my $result = {};
	my $bad = 0;
	open(my $fh, '<', $fname) or $bad = 1;
	if ($bad) {
		$self->error("Can't open #1: #2", $fname, $!);
		return;
	}
	# skip header
	my $separator_found = 0;
	while (<$fh>) {
		if (m/^\-\-\-\-\-\-\-/) {
			$separator_found = 1;
			last;
		}
	}
	if (!$separator_found) {
		$self->error("File #1 does not appear to be a user.db", $fname);
		return;
	}
	# record ids and error out on duplicates
	my $known = {};
	while (<$fh>) {
		next if m/^\#/;
		chomp;
		my @l = split(/\s+/, $_);
		if (@l < 3 || $l[0] !~ m/^\d+$/ || $l[1] !~ m/^_/) {
			$self->error("Bad line: #1 at #2 of #3",
			    $_, $., $fname);
			next;
		}
		if (defined $known->{$l[0]}) {
			$self->error("Duplicate id: #1 in #2",
			    $l[0], $fname);
			next;
		}
		$known->{$l[0]} = 1;
		$result->{$l[1]} = $l[0];
	}
	return $result;
}

package OpenBSD::PkgCreate;

use OpenBSD::PackingList;
use OpenBSD::PackageInfo;
use OpenBSD::Getopt;
use OpenBSD::Temp;
use OpenBSD::Error;
use OpenBSD::Ustar;
use OpenBSD::ArcCheck;
use OpenBSD::Paths;
use File::Basename;

# Extra stuff needed to archive files
package OpenBSD::PackingElement;
sub create_package($self, $state)
{
	$self->archive($state);
	if ($state->verbose) {
		$self->comment_create_package($state);
	}
}

sub pretend_to_archive($self,$state)
{
	$self->comment_create_package($state);
}

# $self->record_digest($original, $entries, $new, $tail)
sub record_digest($, $, $, $, $) {}
# $self->stub_digest($ordered)
sub stub_digest($, $) {}
# $self->archive($state)
sub archive($, $) {}
# $self->comment_create_package($state)
sub comment_create_package($, $) {}
# $self->grab_manpages($state)
sub grab_manpages($, $) {}
# $self->register_for_archival($state)
sub register_for_archival($, $) {}

# $self->print_file
sub print_file($) {}

sub avert_duplicates_and_other_checks($self, $state)
{
	return unless $self->NoDuplicateNames;
	my $n = $self->fullname;
	if (defined $state->stash($n)) {
		$state->error("duplicate item in packing-list #1", $n);
	}
	$state->{stash}{$n} = 1;
}

sub makesum_plist($self, $state, $plist)
{
	$self->add_object($plist);
}

# $self->verify_checksum($state)
sub verify_checksum($, $)
{
}

sub register_forbidden($self, $state)
{
	if ($self->is_forbidden) {
		push(@{$state->{forbidden}}, $self);
	}
}

sub is_forbidden($) { 0 }
sub resolve_link($filename, $base, $level = 0)
{
	if (-l $filename) {
		my $l = readlink($filename);
		if ($level++ > 14) {
			return undef;
		}
		if ($l =~ m|^/|) {
			return $base.resolve_link($l, $base, $level);
		} else {
			return resolve_link(File::Spec->catfile(File::Basename::dirname($filename),$l), $base, $level);
		}
	} else {
		return $filename;
	}
}

sub compute_checksum($self, $result, $state, $base)
{
	my $name = $self->fullname;
	my $fname = $name;
	my $okay = 1;
	if (defined $base) {
		$fname = $base.$fname;
	}
	for my $field (qw(symlink link size ts)) {  # md5
		if (defined $result->{$field}) {
			$state->error("User tried to define @#1 for #2",
			    $field, $fname);
			$okay = 0;
		}
	}
	if (defined $self->{wtempname}) {
		$fname = $self->{wtempname};
	}
	if (-l $fname) {
		if (!defined $base) {
			$state->error("special file #1 can't be a symlink",
			    $self->stringize);
			$okay = 0;
		}
		my $value = readlink $fname;
		my $chk = resolve_link($fname, $base);
		$fname =~ s|^//|/|; # cosmetic
		if (!defined $chk) {
			$state->error("bogus symlink: #1 (too deep)", $fname);
			$okay = 0;
		} elsif (!-e $chk) {
			push(@{$state->{bad_symlinks}{$chk}}, $fname);
		}
		if (defined $state->{wrkobjdir} && 
		    $value =~ m/^\Q$state->{wrkobjdir}\E\//) {
		    	$state->error(
			    "bad symlink: #1 (points into WRKOBJDIR)",
			    $fname);
			$okay = 0;
		}
		$result->make_symlink($value);
	} elsif (-f _) {
		my ($dev, $ino, $size, $mtime) = (stat _)[0,1,7, 9];
		# XXX when rebuilding packages, tied updates can produce
		# spurious hardlinks. We also refer to the installed plist 
		# we're rebuilding to know if we must checksum.
		if (defined $state->stash("$dev/$ino") && !defined $self->{d}) {
			$result->make_hardlink($state->stash("$dev/$ino"));
		} else {
			$state->{stash}{"$dev/$ino"} = $name;
			$result->add_digest($self->compute_digest($fname))
			    unless $state->{bad};
			$result->add_size($size);
			unless ($state->{no_ts_in_plist}) {
				$result->add_timestamp($mtime);
			}
		}
	} elsif (-d _) {
		$state->error("#1 should be a file and not a directory", $fname);
		$okay = 0;
	} else {
		$state->error("#1 does not exist", $fname);
		$okay = 0;
	}
	return $okay;
}

sub makesum_plist_with_base($self, $plist, $state, $base)
{
	if ($self->compute_checksum($self, $state, $base)) {
		$self->add_object($plist);
	}
}

sub verify_checksum_with_base($self, $state, $base)
{
	my $check = ref($self)->new($self->name);
	if (!$self->compute_checksum($check, $state, $base)) {
		return;
	}

	for my $field (qw(symlink link size)) {  # md5
		if ((defined $check->{$field} && defined $self->{$field} &&
		    $check->{$field} ne $self->{$field}) ||
		    (defined $check->{$field} xor defined $self->{$field})) {
		    	$state->error("#1 inconsistency for #2",
			    $field, $self->fullname);
		}
	}
	if ((defined $check->{d} && defined $self->{d} &&
	    !$check->{d}->equals($self->{d})) ||
	    (defined $check->{d} xor defined $self->{d})) {
	    	$state->error("checksum inconsistency for #1",
		    $self->fullname);
	}
}


sub prepare_for_archival($self, $state)
{
	my $o = $state->{archive}->prepare_long($self);
	if (!$o->verify_modes($self)) {
		$state->error("modes don't match for #1", $self->fullname);
	}
	if (!$o->is_allowed) {
		$state->error("can't package #1", $self->fullname);
	}
	return $o;
}

# $self->discover_directories($state)
sub discover_directories($, $)
{
}

# $self->check_version($state, $unsubst)
sub check_version($, $, $)
{
}


# Virtual PackingElements related to chunked gzips and LRU caching.
# see save_history
package OpenBSD::PackingElement::StreamMarker;
our @ISA = qw(OpenBSD::PackingElement::Meta);
sub new($class)
{
	bless {}, $class;
}

sub comment_create_package($self, $state)
{
	$self->SUPER::comment_create_package($state);
	$state->say("Gzip: next chunk");
}

sub archive($self, $state)
{
	$state->new_gstream;
}

package OpenBSD::PackingElement::LRUFrontier;
our @ISA = qw(OpenBSD::PackingElement::Meta);
sub new($class)
{
	bless {}, $class;
}

sub comment_create_package($self, $state)
{
	$self->SUPER::comment_create_package($state);
	$state->say("LRU: end of modified files");
}

package OpenBSD::PackingElement::RcScript;
sub set_destdir($self, $state)
{
	if ($self->name =~ m/^\//) {
		$state->{archive}->set_destdir($state->{base});
	} else {
		$self->SUPER::set_destdir($state);
	}
}

package OpenBSD::PackingElement::SpecialFile;
sub record_digest($self, $, $, $new, $)
{
	push(@$new, $self);
}

sub stub_digest($self, $ordered)
{
	push(@$ordered, $self);
}

sub archive	# forwarder
{
	&OpenBSD::PackingElement::FileBase::archive;
}

sub pretend_to_archive	# forwarder
{
	&OpenBSD::PackingElement::FileBase::pretend_to_archive;
}

sub set_destdir($, $)
{
}

sub may_add($class, $subst, $plist, $opt)
{
	if (defined $opt) {
		my $o = $class->add($plist);
		$subst->copy($opt, $o->fullname) if defined $o->fullname;
	}
}

sub comment_create_package($self, $state)
{
	$state->say("Adding #1", $self->name);
}

sub makesum_plist($self, $state, $plist)
{
	$self->makesum_plist_with_base($plist, $state, undef);
}

sub verify_checksum($self, $state)
{
	$self->verify_checksum_with_base($state, undef);
}

sub prepare_for_archival($self, $state)
{
	my $o = $state->{archive}->prepare_long($self);
	$o->{uname} = 'root';
	$o->{gname} = 'wheel';
	$o->{uid} = 0;
	$o->{gid} = 0;
	$o->{mode} &= 0555; # zap all write and suid modes
	return $o;
}

sub forbidden($) { 1 }

sub register_for_archival($self, $ordered)
{
	push(@$ordered, $self);
}

# override for CONTENTS: we cannot checksum this.
package OpenBSD::PackingElement::FCONTENTS;
sub makesum_plist($, $, $)
{
}

sub verify_checksum($, $)
{
}

sub archive($self, $state)
{
	$self->SUPER::archive($state);
}

sub comment_create_package($self, $state)
{
	$self->SUPER::comment_create_package($state);
}

sub stub_digest($self, $ordered)
{
	push(@$ordered, $self);
}

package OpenBSD::PackingElement::Cwd;
sub archive($, $)
{
}

sub pretend_to_archive($self, $state)
{
	$self->comment_create_package($state);
}

sub comment_create_package($self, $state)
{
	$state->say("Cwd: #1", $self->name);
}

package OpenBSD::PackingElement::FileBase;

sub record_digest($self, $original, $entries, $new, $tail)
{
	if (defined $self->{d}) {
		my $k = $self->{d}->stringize;
		push(@{$entries->{$k}}, $self);
		push(@$original, $k);
	} else {
		push(@$tail, $self);
	}
}

sub register_for_archival($self, $ordered)
{
	push(@$ordered, $self);
}

sub set_destdir($self, $state)
{
	$state->{archive}->set_destdir($state->{base}."/".$self->cwd);
}

sub archive($self, $state)
{
	$self->set_destdir($state);
	my $o = $self->prepare_for_archival($state);

	$o->write unless $state->{bad};
}

sub pretend_to_archive($self, $state)
{
	$self->set_destdir($state);
	$self->prepare_for_archival($state);
	$self->comment_create_package($state);
}

sub comment_create_package($self, $state)
{
	$state->say("Adding #1", $self->name);
}

sub print_file($item)
{
	say '@', $item->keyword, " ", $item->fullname;
}

sub makesum_plist($self, $state, $plist)
{
	$self->makesum_plist_with_base($plist, $state, $state->{base});
}

sub verify_checksum($self, $state)
{
	$self->verify_checksum_with_base($state, $state->{base});
}

package OpenBSD::PackingElement::Dir;
sub discover_directories($self, $state)
{
	$state->{known_dirs}->{$self->fullname} = 1;
}

package OpenBSD::PackingElement::InfoFile;
sub makesum_plist($self, $state, $plist)
{
	$self->SUPER::makesum_plist($state, $plist);
	my $fname = $self->fullname;
	for (my $i = 1; ; $i++) {
		if (-e "$state->{base}/$fname-$i") {
			my $e = OpenBSD::PackingElement::File->add($plist, 
			    $self->name."-".$i);
			$e->compute_checksum($e, $state, $state->{base});
		} else {
			last;
		}
	}
}

package OpenBSD::PackingElement::Manpage;
use File::Basename;

sub grab_manpages($self, $state)
{
	my $filename;
	if ($self->{wtempname}) {
		$filename = $self->{wtempname};
	} else {
		$filename = $state->{base}.$self->fullname;
	}
	push(@{$state->{manpages}}, $filename);
}

sub format_source_page($self, $state, $plist)
{
	if ($state->{subst}->empty("USE_GROFF") || !$self->is_source) {
		return 0;
	}
	my $dest = $self->source_to_dest;
	my $fullname = $self->cwd."/".$dest;
	my $d = dirname($fullname);
	$state->{mandir} //= OpenBSD::Temp::permanent_dir(
	    $ENV{TMPDIR} // '/tmp', "manpage") or
	    	$state->error(OpenBSD::Temp->last_error) and
		return 0;
	my $tempname = $state->{mandir}.$fullname;
	require File::Path;
	File::Path::make_path($state->{mandir}.$d);
	open my $fh, ">", $tempname;
	if (!defined $fh) {
	    $state->error("can't create #1: #2", $tempname, $!);
	    return 0;
    	}
	chmod 0444, $fh;
	if (-d $state->{base}.$d) {
		undef $d;
	}
	if (!$self->format($state, $tempname, $fh)) {
		return 0;
	}
	if (-z $tempname) {
		$state->errsay("groff produced empty result for #1", $dest);
		$state->errsay("\tkeeping source manpage");
		return 0;
	}
	if (defined $d && !$state->{known_dirs}->{$d}) {
		$state->{known_dirs}->{$d} = 1;
		OpenBSD::PackingElement::Dir->add($plist, dirname($dest));
	}
	my $e = OpenBSD::PackingElement::Manpage->add($plist, $dest);
	$e->{wtempname} = $tempname;
	$e->compute_checksum($e, $state, $state->{base});
	return 1;
}

sub makesum_plist($self, $state, $plist)
{
	if (!$self->format_source_page($state, $plist)) {
		$self->SUPER::makesum_plist($state, $plist);
	}
}


package OpenBSD::PackingElement::Depend;
sub avert_duplicates_and_other_checks($self, $state)
{
	if (!$self->spec->is_valid) {
		$state->error("invalid \@#1 #2 in packing-list",
		    $self->keyword, $self->stringize);
	}
	$self->SUPER::avert_duplicates_and_other_checks($state);
}

sub forbidden($) { 1 }

package OpenBSD::PackingElement::Conflict;
sub avert_duplicates_and_other_checks($self, $state)
{
	$state->{has_conflict}++;
	OpenBSD::PackingElement::Depend::avert_duplicates_and_other_checks($self, $state);
}

package OpenBSD::PackingElement::AskUpdate;
sub avert_duplicates_and_other_checks	# forwarder
{
	&OpenBSD::PackingElement::Depend::avert_duplicates_and_other_checks;
}

package OpenBSD::PackingElement::Dependency;
sub avert_duplicates_and_other_checks($self, $state)
{
	$self->SUPER::avert_duplicates_and_other_checks($state);

	my @issues = OpenBSD::PackageName->from_string($self->{def})->has_issues;
	if (@issues > 0) {
		$state->error("\@#1 #2\n  #3, #4",
		    $self->keyword, $self->stringize,
		    $self->{def}, join(' ', @issues));
	} elsif ($self->spec->is_valid) {
		my @m = $self->spec->filter($self->{def});
		if (@m == 0) {
			$state->error(
			    "\@#1 #2\n".
			    "  pattern #3 doesn't match default #4\n",
			    $self->keyword, $self->stringize,
			    $self->{pattern}, $self->{def});
		}
	}
}

package OpenBSD::PackingElement::Name;
sub avert_duplicates_and_other_checks($self, $state)
{
	my @issues = OpenBSD::PackageName->from_string($self->name)->has_issues;
	if (@issues > 0) {
		$state->error("bad package name #1: ", $self->name,
		    join(' ', @issues));
	}
	$self->SUPER::avert_duplicates_and_other_checks($state);
}

sub forbidden($) { 1 }

package OpenBSD::PackingElement::NoDefaultConflict;
sub avert_duplicates_and_other_checks($self, $state)
{
	$state->{has_no_default_conflict}++;
}

package OpenBSD::PackingElement::NewAuth;
sub avert_duplicates_and_other_checks($self, $state)
{
	my $userlist = $state->{userlist};
	if (defined $userlist) {
		my $entry = $userlist->{$self->{name}};
		my $id = $self->id;
		$id =~ s/^!//;
		if (!defined $entry) {
			$state->error("#1 #2: not registered in #3",
			    $self->keyword, $self->{name}, $state->opt('u'));
		} elsif ($entry != $id) {
			$state->error(
			    "#1 #2: id mismatch in #3 (#4 vs #5)",
			    $self->keyword, $self->{name}, $state->opt('u'),
			    $entry, $id);
		}
	}
	$self->SUPER::avert_duplicates_and_other_checks($state);
}

package OpenBSD::PackingElement::NewUser;
sub id($self)
{
	return $self->{uid};
}

package OpenBSD::PackingElement::NewGroup;
sub id($self)
{
	return $self->{gid};
}

package OpenBSD::PackingElement::Lib;
sub check_version($self, $state, $unsubst)
{
	my @l  = $self->parse($self->name);
	if (defined $l[0]) {
		if (!$unsubst =~ m/\$\{LIB$l[0]_VERSION\}/) {
			$state->error(
			    "Incorrectly versioned shared library: #1", 
			    $unsubst);
		}
	} else {
		$state->error("Invalid shared library #1", $unsubst);
	}
	$state->{has_libraries} = 1;
}

package OpenBSD::PackingElement::DigitalSignature;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::Signer;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::ExtraInfo;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::ManualInstallation;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::Firmware;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::Url;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::Arch;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::LocalBase;
sub is_forbidden($) { 1 }

package OpenBSD::PackingElement::Version;
sub is_forbidden($) { 1 }

# put together file and filename, in order to handle fragments simply
package MyFile;
sub new($class, $filename)
{
	open(my $fh, '<', $filename) or return undef;

	bless { fh => $fh, name => $filename }, (ref($class) || $class);
}

sub readline($self)
{
	return readline $self->{fh};
}

sub name($self)
{
	return $self->{name};
}

sub close($self)
{
	close($self->{fh});
}

sub deduce_name($self, $frag, $not, $p, $state)
{
	my $o = $self->name;
	my $noto = $o;
	my $nofrag = "no-$frag";

	$o =~ s/PFRAG\./PFRAG.$frag-/o or
	    $o =~ s/PLIST/PFRAG.$frag/o;

	$noto =~ s/PFRAG\./PFRAG.no-$frag-/o or
	    $noto =~ s/PLIST/PFRAG.no-$frag/o;
	unless (-e $o or -e $noto) {
		$p->missing_fragments($state, $frag, $o, $noto);
		return;
	}
	if ($not) {
		return $noto if -e $noto;
    	} else {
		return $o if -e $o;
	}
	return;
}

# special solver class for PkgCreate
package OpenBSD::Dependencies::CreateSolver;
our @ISA = qw(OpenBSD::Dependencies::SolverBase);

# we need to "hack" a special set
sub new($class, $plist)
{
	bless { set => OpenBSD::PseudoSet->new($plist), 
	    old_dependencies => {}, bad => [] }, $class;
}

sub solve_all_depends($solver, $state)
{
	$solver->{tag_finder} = OpenBSD::lookup::tag->new($solver, $state);
	while (1) {
		my @todo = $solver->solve_depends($state);
		if (@todo == 0) {
			return;
		}
		if ($solver->solve_wantlibs($state, 0)) {
			return;
		}
		$solver->{set}->add_new(@todo);
	}
}

sub solve_wantlibs($solver, $state, $final)
{
	my $okay = 1;
	my $lib_finder = OpenBSD::lookup::library->new($solver);
	my $h = $solver->{set}{new}[0];
	for my $lib (@{$h->{plist}{wantlib}}) {
		$solver->{localbase} = $h->{plist}->localbase;
		next if $lib_finder->lookup($solver,
		    $solver->{to_register}{$h}, $state,
		    $lib->spec);
		$okay = 0;
		$state->shlibs->report_problem($lib->spec) if $final;
	}
	if (!$okay && $final) {
		$solver->dump($state);
		$lib_finder->dump($state);
	}
	return $okay;
}

sub really_solve_dependency($self, $state, $dep, $package)
{
	$state->progress->message($dep->{pkgpath});

	my $v;

	# look in installed packages, but only for different paths
	my $p1 = $dep->{pkgpath};
	my $p2 = $state->{fullpkgpath};
	$p1 =~ s/\,.*//;
	$p2 =~ s/\,.*//;
	$p2 =~ s,^debug/,,;
	if ($p1 ne $p2) {
		# look in installed packages
		$v = $self->find_dep_in_installed($state, $dep);
	}
	if (!defined $v) {
		$v = $self->find_dep_in_self($state, $dep);
	}

	# and in portstree otherwise
	if (!defined $v) {
		$v = $self->solve_from_ports($state, $dep, $package);
	}
	return $v;
}

sub diskcachename($self, $dep)
{
	if ($ENV{_DEPENDS_CACHE}) {
		my $diskcache = $dep->{pkgpath};
		$diskcache =~ s/\//--/g;
		return $ENV{_DEPENDS_CACHE}."/pkgcreate-".$diskcache;
	} else {
		return undef;
	}
}

sub to_cache($self, $plist, $final)
{
	# try to cache atomically. 
	# no error if it doesn't work
	require OpenBSD::MkTemp;
	my ($fh, $tmp) = OpenBSD::MkTemp::mkstemp(
	    "$ENV{_DEPENDS_CACHE}/my.XXXXXXXXXXX") or return;
	chmod 0644, $fh;
	$plist->write($fh);
	close($fh);
	rename($tmp, $final);
	unlink($tmp);
}

sub ask_tree($self, $state, $pkgpath, $portsdir, $data, @action)
{
	my $make = OpenBSD::Paths->make;
	my $errors = OpenBSD::Temp->file;
	if (!defined $errors) {
		$state->fatal(OpenBSD::Temp->last_error);
	}
	my $pid = open(my $fh, "-|");
	if (!defined $pid) {
		$state->fatal("cannot fork: #1", $!);
	}
	if ($pid == 0) {
		$ENV{FULLPATH} = 'Yes';
		delete $ENV{FLAVOR};
		delete $ENV{SUBPACKAGE};
		$ENV{SUBDIR} = $pkgpath;
		$ENV{ECHO_MSG} = ':';

		if (!chdir $portsdir) {
			$state->errsay("Can't chdir #1: #2", $portsdir, $!);
			exit(2);
		}
		open STDERR, ">>", $errors;
		# make sure the child starts with a single identity
		$( = $); $< = $>;
		# XXX we're already running as ${BUILD_USER}
		# so we can't do this again
		push(@action, 'PORTS_PRIVSEP=No');
		$DB::inhibit_exit = 0;
		exec $make ('make', @action);
	}
	my $plist = OpenBSD::PackingList->read($fh, $data);
	while(<$fh>) {	# XXX avoid spurious errors from child
	}
	close($fh);
	if ($? != 0) {
		$state->errsay("child running '#2' failed: #1", 
		    $state->child_error,
		    join(' ', 'make', @action));
		if (open my $fh, '<', $errors) {
			while(<$fh>) {
				$state->errprint("#1", $_);
			}
			close($fh);
		}
	}
	unlink($errors);
	return $plist;
}

sub really_solve_from_ports($self, $state, $dep, $portsdir)
{
	my $diskcache = $self->diskcachename($dep);
	my $plist;

	if (defined $diskcache && -f $diskcache) {
		$plist = OpenBSD::PackingList->fromfile($diskcache);
	} else {
		$plist = $self->ask_tree($state, $dep->{pkgpath}, $portsdir,
		    \&OpenBSD::PackingList::PrelinkStuffOnly,
		    'print-plist-libs-with-depends',
		    'wantlib_args=no-wantlib-args');
		if ($? != 0 || !defined $plist->pkgname) {
			return undef;
		}
		if (defined $diskcache) {
			$self->to_cache($plist, $diskcache);
		}
	}
	$state->shlibs->add_libs_from_plist($plist);
	$self->{tag_finder}->find_in_plist($plist, $dep->{pkgpath});
	$self->add_dep($plist);
	return $plist->pkgname;
}

my $cache = {};

sub solve_from_ports($self, $state, $dep, $package)
{
	my $portsdir = $state->defines('PORTSDIR');
	return undef unless defined $portsdir;
	my $pkgname;
	if (defined $cache->{$dep->{pkgpath}}) {
		$pkgname = $cache->{$dep->{pkgpath}};
	} else {
		$pkgname = $self->really_solve_from_ports($state, $dep, 
		    $portsdir);
		$cache->{$dep->{pkgpath}} = $pkgname;
	}
	if (!defined $pkgname) {
		$state->error("Can't obtain dependency #1 from ports tree",
		    $dep->{pattern});
		return undef;
	}
	if ($dep->spec->filter($pkgname) == 0) {
		$state->error("Dependency #1 doesn't match FULLPKGNAME: #2",
		    $dep->{pattern}, $pkgname);
		return undef;
	}

	return $pkgname;
}

# we don't want old libs
sub find_old_lib($, $, $, $, $)
{
	return undef;
}

package OpenBSD::PseudoHandle;
sub new($class, $plist)
{
	bless { plist => $plist}, $class;
}

sub pkgname($self)
{
	return $self->{plist}->pkgname;
}

sub dependency_info($self)
{
	return $self->{plist};
}

package OpenBSD::PseudoSet;
sub new($class, @elements)
{
	my $o = bless {}, $class;
	$o->add_new(@elements);
}

sub add_new($self, @elements)
{
	for my $i (@elements) {
		push(@{$self->{new}}, OpenBSD::PseudoHandle->new($i));
	}
	return $self;
}

sub newer($self)
{
	return @{$self->{new}};
}


sub newer_names($self)
{
	return map {$_->pkgname} @{$self->{new}};
}

sub older($)
{
	return ();
}

sub older_names($)
{
	return ();
}

sub kept($)
{
	return ();
}

sub kept_names($)
{
	return ();
}

sub print($self)
{
	return $self->{new}[0]->pkgname;
}

package OpenBSD::PkgCreate;
our @ISA = qw(OpenBSD::AddCreateDelete);

sub handle_fragment($self, $state, $old, $not, $frag, $location)
{
	if ($state->{subst}->has_fragment($state, $frag, $location)) {
		return undef if defined $not;
	} else {
		return undef unless defined $not;
	}
	my $newname = $old->deduce_name($frag, $not, $self, $state);
	if (defined $newname) {
		$state->set_status("switching to $newname")
		    unless $state->{silent};
		my $f = $old->new($newname);
		if (!defined $f) {
			$self->cant_read_fragment($state, $newname);
		} else {
			return $f;
		}
	}
	return undef;
}

sub FileClass($)
{
	return "MyFile";
}

# hook for update-plist, which wants to record fragment positions
sub record_fragment($, $, $, $, $)
{
}

# hook for update-plist, which wants to record original file info
sub annotate($, $, $, $)
{
}

sub read_fragments($self, $state, $plist, $filename)
{
	my $stack = [];
	my $subst = $state->{subst};
	my $main = $self->FileClass->new($filename);
	return undef if !defined $main;
	push(@$stack, $main);
	my $fast = $subst->value("LIBS_ONLY");

	return $plist->read($stack,
	    sub($stack, $cont) {
		while(my $file = pop @$stack) {
			while (my $l = $file->readline) {
				$state->progress->working(2048) 
				    unless $state->{silent};
				# add a file name to uncommitted cvs tags so
				# that the plist is always the same
				if ($l =~m/^(\@comment\s+\$(?:Open)BSD\$)$/o) {
					$l = '@comment $'.'OpenBSD: '.basename($file->name).',v$';
				}
				if ($l =~ m/^(\!)?\%\%(.*)\%\%$/) {
					$self->record_fragment($plist, $1, $2, 
					    $file);
					if (my $f2 = $self->handle_fragment($state, $file, $1, $2, $filename)) {
						push(@$stack, $file);
						$file = $f2;
					}
					next;
				}
				my $s = $subst->do($l);
				if ($fast) {
					next unless $s =~ m/^\@(?:cwd|lib|libset|define-tag|depend|wantlib)\b/o || $s =~ m/lib.*\.a$/o;
				}
	# XXX some things, like @comment no checksum, don't produce an object
				my $o = &$cont($s);
				if (defined $o) {
					$o->check_version($state, $s);
					$self->annotate($o, $l, $file);
				}
			}
		}
	    });
}

sub add_description($state, $plist, $name, $opt_d)
{
	my $o = OpenBSD::PackingElement::FDESC->add($plist, $name);
	my $subst = $state->{subst};
	my $comment = $subst->value('COMMENT');
	if (defined $comment) {
		if (length $comment > 60) {
			$state->fatal("comment is too long\n#1\n#2\n",
			    $comment, ' 'x60 . "^" x (length($comment)-60));
		}
	} else {
		$state->usage("Comment required");
	}
	if (!defined $opt_d) {
		$state->usage("Description required");
	}
	return if defined $state->opt('q');

	open(my $fh, '+>', $o->fullname) or die "Can't write to DESCR: $!";
	if (defined $comment) {
		print $fh $subst->do($comment), "\n";
	}
	if ($opt_d =~ /^\-(.*)$/o) {
		print $fh $1, "\n";
	} else {
		$subst->copy_fh($opt_d, $fh);
	}
	if (defined $comment) {
		if ($subst->empty('MAINTAINER')) {
			$state->errsay("no MAINTAINER");
		} else {
			print $fh "\n", 
			    $subst->do('Maintainer: ${MAINTAINER}'), "\n";
		}
		if (!$subst->empty('HOMEPAGE')) {
			print $fh "\n", $subst->do('WWW: ${HOMEPAGE}'), "\n";
		}
	}
	seek($fh, 0, 0) or die "Can't rewind DESCR: $!";
    	my $errors = 0;
	while (<$fh>) {
		chomp;
		if ($state->safe($_) ne $_) {
			$state->errsay(
			    "DESCR contains weird characters: #1 on line #2", 
			    $_, $.);
		$errors++;
		}
	}
	if ($errors) {
		$state->fatal("Can't continue");
	}
	close($fh);
}

sub add_extra_info($self, $plist, $state)
{
	my $subst = $state->{subst};
	my $fullpkgpath = $state->{fullpkgpath};
	my $cdrom = $subst->value('PERMIT_PACKAGE_CDROM') ||
	    $subst->value('CDROM');;
	my $ftp = $subst->value('PERMIT_PACKAGE_FTP') ||
	    $subst->value('FTP');
	$ftp //= 'no';
	$ftp = 'yes' if $ftp =~ m/^yes$/io;
	$cdrom = 'yes' if defined $cdrom && $cdrom =~ m/^yes$/io;

	OpenBSD::PackingElement::ExtraInfo->add($plist,
	    $fullpkgpath, $cdrom, $ftp);
}

sub add_elements($self, $plist, $state)
{
	my $subst = $state->{subst};
	add_description($state, $plist, DESC, $state->opt('d'));
	OpenBSD::PackingElement::FDISPLAY->may_add($subst, $plist,
	    $state->opt('M'));
	OpenBSD::PackingElement::FUNDISPLAY->may_add($subst, $plist,
	    $state->opt('U'));
	for my $d (sort keys %{$state->{dependencies}}) {
		OpenBSD::PackingElement::Dependency->add($plist, $d);
	}

	for my $w (sort keys %{$state->{wantlib}}) {
		OpenBSD::PackingElement::Wantlib->add($plist, $w);
	}
	for my $w (sort keys %{$state->{libset}}) {
		OpenBSD::PackingElement::Libset->add($plist, $w);
	}

	if (defined $state->opt('A')) {
		OpenBSD::PackingElement::Arch->add($plist, $state->opt('A'));
	}

	if (defined $state->opt('L')) {
		OpenBSD::PackingElement::LocalBase->add($plist, $state->opt('L'));
		$state->{groff} = $state->opt('L'). '/bin/groff';
	}
	$self->add_extra_info($plist, $state);
	if ($state->{system_version}) {
		OpenBSD::PackingElement::Version->add($plist, 
		    $state->{system_version});
    	}
}

sub cant_read_fragment($self, $state, $frag)
{
	$state->fatal("can't read packing-list #1", $frag);
}

sub missing_fragments($self, $state, $frag, $o, $noto)
{
	$state->fatal("Missing fragments for #1: #2 and #3 don't exist",
		$frag, $o, $noto);
}

sub read_all_fragments($self, $state, $plist)
{
	if (defined $state->{prefix}) {
		OpenBSD::PackingElement::Cwd->add($plist, $state->{prefix});
	} else {
		$state->usage("Prefix required");
	}
	for my $contentsfile (@{$state->{contents}}) {
		$self->read_fragments($state, $plist, $contentsfile) or
		    $self->cant_read_fragment($state, $contentsfile);
	}

	$plist->register_forbidden($state);
	if (defined $state->{forbidden}) {
		for my $e (@{$state->{forbidden}}) {
			$state->errsay("Error: #1 can't be set explicitly", "\@".$e->keyword." ".$e->stringize);
		}
		$state->fatal("Can't continue");
	}
}

sub create_plist($self, $state, $pkgname)
{
	my $plist = OpenBSD::PackingList->new;

	if ($pkgname =~ m|([^/]+)$|o) {
		$pkgname = $1;
		$pkgname =~ s/\.tgz$//o;
	}
	$plist->set_pkgname($pkgname);
	unless ($state->{silent}) {
		$state->say("Creating package #1", $pkgname)
		    if defined $state->opt('v');
		$state->set_status("reading plist");
	}
	my $dir = OpenBSD::Temp->dir;
	if (!$dir) {
		$state->fatal(OpenBSD::Temp->last_error);
	}
	$plist->set_infodir($dir);
	# XXX optimization: we want -S to be fast even if we don't check
	# everything, e.g., we don't need the actual packing-list to
	# print a signature if that's all we do.
	if (!(defined $state->opt('S') && defined $state->opt('n'))) {
		$self->read_all_fragments($state, $plist);
	}
	$self->add_elements($plist, $state);

	return $plist;
}

sub make_plist_with_sum($self, $state, $plist)
{
	my $p2 = OpenBSD::PackingList->new;
	$state->progress->visit_with_count($plist, 'makesum_plist', $p2);
	$p2->set_infodir($plist->infodir);
	return $p2;
}

sub read_existing_plist($self, $state, $contents)
{
	my $plist = OpenBSD::PackingList->new;
	if (-d $contents && -f $contents.'/'.CONTENTS) {
		$plist->set_infodir($contents);
		$contents .= '/'.CONTENTS;
	} else {
		$plist->set_infodir(dirname($contents));
	}
	$plist->fromfile($contents) or
	    $state->fatal("can't read packing-list #1", $contents);
	return $plist;
}

sub create_package($self, $state, $plist, $ordered, $wname)
{
	$state->say("Creating gzip'd tar ball in '#1'", $wname)
	    if $state->opt('v');
	my $h = sub {	# SIGHANDLER
		unlink $wname;
		my $caught = shift;
		$SIG{$caught} = 'DEFAULT';
		kill $caught, $$;
	};

	local $SIG{'INT'} = $h;
	local $SIG{'QUIT'} = $h;
	local $SIG{'HUP'} = $h;
	local $SIG{'KILL'} = $h;
	local $SIG{'TERM'} = $h;
	$state->{archive} = $state->create_archive($wname, $plist->infodir);
	$state->set_status("archiving");
	my $p = $state->progress->new_sizer($plist);
	for my $e (@$ordered) {
		$e->create_package($state);
		$p->advance($e);
	}
	$state->end_status;
	$state->{archive}->close;
	if ($state->{bad}) {
		unlink($wname);
		exit(1);
	}
}

sub show_bad_symlinks($self, $state)
{
	for my $dest (sort keys %{$state->{bad_symlinks}}) {
		$state->errsay("Warning: symlink(s) point to non-existent #1",
		    $dest);
		for my $link (@{$state->{bad_symlinks}{$dest}}) {
			$state->errsay("\t#1", $link);
		}
	}
}

sub check_dependencies($self, $plist, $state)
{
	my $solver = OpenBSD::Dependencies::CreateSolver->new($plist);

	# look for libraries in the "real" tree
	$state->{destdir} = '/';

	$solver->solve_all_depends($state);
	if (!$solver->solve_wantlibs($state, 1)) {
		$state->{bad}++;
	}
}

sub finish_manpages($self, $state, $plist)
{
	$plist->grab_manpages($state);
	if (defined $state->{manpages}) {
		$state->run_makewhatis(['-t'], $state->{manpages});
	}

	if (defined $state->{mandir}) {
		require File::Path;
		File::Path::remove_tree($state->{mandir});
	}
}

# we maintain an LRU cache of files (by checksum) to speed-up
# pkg_add -u
sub save_history($self, $plist, $state, $dir)
{
	unless (-d $dir) {
		require File::Path;

		File::Path::make_path($dir);
	}

	my $name = $plist->fullpkgpath;
	$name =~ s,/,.,g;
	my $oldfname = "$dir/$name";
	my $fname = "$oldfname.lru";

	# if we have history, we record the order of checksums
	my $known = {};
	if (open(my $f, '<', $fname)) {
		while (<$f>) {
			chomp;
			$known->{$_} //= $.;
		}
		close($f);
	} elsif (open(my $f2, '<', $oldfname)) {
		while (<$f2>) {
			chomp;
			$known->{$_} //= $.;
		}
		close($f2);
	}

	my $todo = [];		
	my $entries = {};
	my $list = [];
	my $tail = [];
	# scan the plist: find data we need to sort, index them by hash,
	# directly put some stuff at start of list, and put non indexed stuff
	# at end (e.g., symlinks and hardlinks)
	$plist->record_digest($todo, $entries, $list, $tail);

	my $name2 = "$fname.new";
	open(my $f, ">", $name2) or 
	    $state->fatal("Can't create #1: #2", $name2, $!);
	
	my $found = {};
	# split the remaining list
	# - first, unknown stuff
	for my $h (@$todo) {
		if ($known->{$h}) {
			$found->{$h} = $known->{$h};
		} else {
			print $f "$h\n" if defined $f;
			push(@$list, (shift @{$entries->{$h}}));
		}
	}
	# dummy entry for verbose output
	push(@$list, OpenBSD::PackingElement::LRUFrontier->new);
	# - then known stuff, preserve the order
	for my $h (sort  {$found->{$a} <=> $found->{$b}} keys %$found) {
		print $f "$h\n" if defined $f;
		push(@$list, @{$entries->{$h}});
	}
	close($f);
	rename($name2, $fname) or 
	    $state->fatal("Can't rename #1->#2: #3", $name2, $fname, $!);
	unlink($oldfname);
	# even with no former history, it's a good idea to save chunks
	# for instance: packages like texlive will not change all that
	# fast, so there's a good chance the end chunks will be ordered
	# correctly
	my $l = [@$tail];
	my $i = 0;
	my $end_marker = OpenBSD::PackingElement::StreamMarker->new;
	while (@$list > 0) {
		my $e = pop @$list;
		if ($i++ % 16 == 0) {
			unshift @$l, $end_marker;
		}
		unshift @$l, $e;
	}
	# remove extraneous marker if @$tail is empty.
	if ($l->[-1] eq $end_marker) {
		pop @$l;
	}
	return $l;
}

sub validate_pkgname($self, $state, $pkgname)
{
	my $revision = $state->defines('REVISION_CHECK');
	my $epoch = $state->defines('EPOCH_CHECK');
	my $flavor_list = $state->defines('FLAVOR_LIST_CHECK');
	if ($revision eq '') {
		$revision = -1;
	}
	if ($epoch eq '') {
		$epoch = -1;
	}
	my $okay_flavors = {map {($_, 1)} split(/\s+/, $flavor_list) };
	my $v = OpenBSD::PackageName->from_string($pkgname);

	# first check we got a non buggy pkgname, since otherwise
	# the parts we test won't even exist !
	if ($v->has_issues) {
		$state->errsay("Error FULLPKGNAME #1 #2", $pkgname,
		    $v->has_issues);
		$state->fatal("Can't continue");
	} 
	my $errors = 0;
	if ($v->{version}->p != $revision) {
		$state->errsay("REVISION mismatch (REVISION=#1)", $revision);
		$errors++;
	}
	if ($v->{version}->v != $epoch) {
		$state->errsay("EPOCH mismatch (EPOCH=#1)", $epoch);
		$errors++;
	}
	for my $f (keys %{$v->{flavors}}) {
		if (!exists $okay_flavors->{$f}) {
			$state->errsay("bad FLAVOR #1 (admissible flavors #2)",
			    $f, $flavor_list);
			$errors++;
		}
	}
	if ($errors) {
		$state->fatal("Can't continue");
	}
}

sub run_command($self, $state)
{
	if (defined $state->opt('Q')) {
		$state->{opt}{q} = 1;
	}

	if (!defined $state->{contents}) {
		$state->usage("Packing-list required");
	}

	my $plist;
	if ($state->{regen_package}) {
		if (!defined $state->{contents} || @{$state->{contents}} > 1) {
			$state->usage("Exactly one single packing-list is required");
		}
		$plist = $self->read_existing_plist($state, 
		    $state->{contents}[0]);
	} else {
		$plist = $self->create_plist($state, $ARGV[0]);
	}


	if (defined $state->opt('S')) {
		print $plist->signature->string, "\n";
		# no need to check anything else if we're running -n
		exit 0 if defined $state->opt('n');
	}
	$plist->discover_directories($state);
	my $ordered = [];
	unless (defined $state->opt('q') && defined $state->opt('n')) {
		$state->set_status("checking dependencies");
		$self->check_dependencies($plist, $state);
		if ($state->{regression}{stub}) {
			$plist->stub_digest($ordered);
		} else {
			$state->set_status("checksumming");
			if ($state->{regen_package}) {
				$state->progress->visit_with_count($plist, 
				    'verify_checksum');
			} else {
				$plist = $self->make_plist_with_sum($state, 
				    $plist);
				my $h = $plist->get('always-update');
				if (defined $h) {
					$h->hash_plist($plist);
				}
			}
			if (defined(my $dir = $state->defines('HISTORY_DIR'))) {
				$ordered = $self->save_history($plist, 
				    $state, $dir);
			} else {
				$plist->register_for_archival($ordered);
			}
			$self->show_bad_symlinks($state);
		}
		$state->end_status;
	}

	if (!defined $plist->pkgname) {
		$state->fatal("can't write unnamed packing-list");
	}
	if (defined $state->defines('REVISION_CHECK')) {
		$self->validate_pkgname($state, $plist->pkgname);
	}

	if (defined $state->opt('q')) {
		if (defined $state->opt('Q')) {
			$plist->print_file;
		} else {
			$plist->write(\*STDOUT);
		}
		return 0 if defined $state->opt('n');
	}

	if ($plist->{deprecated}) {
		$state->fatal("found obsolete constructs");
	}

	$plist->avert_duplicates_and_other_checks($state);
	if ($state->{has_no_default_conflict} && !$state->{has_conflict}) {
		$state->errsay("Warning: \@option no-default-conflict without \@conflict");
	}
	$state->{stash} = {};

	if ($state->{bad} && !$state->{regression}{plist_checks}) {
		$state->fatal("can't continue");
	}
	$state->{bad} = 0;

	my $wname;
	if ($state->{regen_package}) {
		$wname = $plist->pkgname.".tgz";
	} else {
		$plist->save or $state->fatal("can't write packing-list");
		$wname = $ARGV[0];
	}

	if ($state->opt('n')) {
		$state->{archive} = OpenBSD::Ustar->new(undef, $state,
		    $plist->infodir);
		$plist->pretend_to_archive($state);
	} else {
		$self->create_package($state, $plist, $ordered, $wname);
	}
	if (!$state->defines("stub")) {
		$self->finish_manpages($state, $plist);
	}
}

sub parse_and_run($self, $cmd)
{
	my $state = OpenBSD::PkgCreate::State->new($cmd);
	$state->handle_options;

	if (@ARGV == 0) {
		$state->{regen_package} = 1;
	} elsif (@ARGV != 1) {
		$state->usage("Exactly one single package name is required: #1",
		    join(' ', @ARGV));
	}

	$self->try_and_run_command($state);
	return $state->{bad} != 0;
}

1;
