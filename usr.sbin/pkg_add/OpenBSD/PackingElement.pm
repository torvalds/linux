# ex:ts=8 sw=4:
# $OpenBSD: PackingElement.pm,v 1.292 2025/07/13 08:16:30 bentley Exp $
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

use OpenBSD::PackageInfo;
use OpenBSD::Paths;

# This is the basic class, which is mostly abstract, except for
# create and register_with_factory.
# It does provide base methods for stuff under it, though.

# XXX PackingElement uses (very seldom) multiple inheritance:
#	the subclasses ::DirBase and ::Unique are used as mix-ins
# 	and thus contain very limited functionality !

package OpenBSD::PackingElement;
our %keyword;

sub create($class, $line, $plist)
{
	if ($line =~ m/^\@(\S+)\s*(.*)$/o) {
		if (defined $keyword{$1}) {
			$keyword{$1}->add($plist, $2);
		} else {
			die "Unknown element: $line";
		}
	} else {
		chomp $line;
		OpenBSD::PackingElement::File->add($plist, $line);
	}
}

sub register_with_factory($class, $k = $class->keyword, $o = $class)
{
	$keyword{$k} = $o;
}

sub category($) { 'items' }

sub new($class, $args)
{
	bless { name => $args }, $class;
}

sub remove($self, $plist)
{
	$self->{deleted} = 1;
}

sub clone($object)
{
	# shallow copy
	my %h = %$object;
	bless \%h, ref($object);
}


# $self->register_manpage($plstate, $key)
sub register_manpage($, $, $)
{
}

# plist keeps a "state" while reading a plist
# $self->destate($plstate)
sub destate($, $)
{
}

sub add_object($self, $plist)
{
	$self->destate($plist->{state});
	$plist->add2list($self);
	return $self;
}

# $class->add($plist, @args):
# 	create an object with the correct arguments
#	returns the actual object created, IF ANY (XXX see subclasses
#	for instances of annotations like @symlink that DON'T create
#	an actual object)
#
#	most add methods have ONE single argument, except for
#	subclasses generated from comments !
sub add($class, $plist, @args)
{
	my $self = $class->new(@args);
	return $self->add_object($plist);
}

sub needs_keyword($) { 1 }

sub write($self, $fh)
{
	my $s = $self->stringize;
	if ($self->needs_keyword) {
		$s = " $s" unless $s eq '';
		say $fh "\@", $self->keyword, "$s";
	} else {
		say $fh $s;
	}
}

# specialized version to avoid copying digital signatures over
sub write_no_sig($self, $fh)
{
	$self->write($fh);
}

sub write_without_variation($self, $fh)
{
	$self->write_no_sig($fh);
}

# needed for comment checking
sub fullstring($self)
{
	my $s = $self->stringize;
	if ($self->needs_keyword) {
		$s = " $s" unless $s eq '';
		return "\@".$self->keyword.$s;
	} else {
		return $s;
	}
}

sub name($self)
{
	return $self->{name};
}

sub set_name($self, $v)
{
	$self->{name} = $v;
}

sub stringize($self)
{
	return $self->name;
}

sub IsFile($) { 0 }

sub is_a_library($) { 0 }
sub NoDuplicateNames($) { 0 }


sub copy_shallow_if($self, $copy, $h)
{
	$self->add_object($copy) if defined $h->{$self};
}

sub copy_deep_if($self, $copy, $h)
{
	$self->clone->add_object($copy) if defined $h->{$self};
}

sub finish($class, $state)
{
	OpenBSD::PackingElement::Fontdir->finish($state);
	OpenBSD::PackingElement::RcScript->report($state);
	if (defined $state->{readmes}) {
		$state->say("New and changed readme(s):");
		for my $file (sort @{$state->{readmes}}) {
			$state->say("\t#1", $file);
		}
	}
}

# Basic class hierarchy

# various stuff that's only linked to objects before/after them
# this class doesn't have real objects: no valid new nor clone...
package OpenBSD::PackingElement::Annotation;
our @ISA=qw(OpenBSD::PackingElement);
sub new($) { die "Can't create annotation objects" }

# concrete objects
package OpenBSD::PackingElement::Object;
our @ISA=qw(OpenBSD::PackingElement);

sub cwd($self)
{
	return ${$self->{cwd}};
}

# most objects should be fs relative, but there are
# exceptions, such as sample files that will get installed
# under /etc, or rc files !
sub absolute_okay($) { 0 }

sub compute_fullname($self, $state)
{
	$self->{cwd} = $state->{cwd};
	$self->set_name(File::Spec->canonpath($self->name));
	if ($self->name =~ m|^/|) {
		unless ($self->absolute_okay) {
			die "Absolute name forbidden: ", $self->name;
		}
	}
}

sub make_full($self, $path)
{
	if ($path !~ m|^/|o && $self->cwd ne '.') {
		$path = $self->cwd."/".$path;
		$path =~ s,^//,/,;
	}
	return $path;
}

sub fullname($self)
{
	return $self->make_full($self->name);
}

sub compute_modes($self, $state)
{
	if (defined $state->{mode}) {
		$self->{mode} = $state->{mode};
	}
	if (defined $state->{owner}) {
		$self->{owner} = $state->{owner};
		if (defined $state->{uid}) {
			$self->{uid} = $state->{uid};
		}
	}
	if (defined $state->{group}) {
		$self->{group} = $state->{group};
		if (defined $state->{gid}) {
			$self->{gid} = $state->{gid};
		}
	}
}

# concrete objects with file-like behavior
package OpenBSD::PackingElement::FileObject;
our @ISA=qw(OpenBSD::PackingElement::Object);

sub NoDuplicateNames($) { 1 }

sub dirclass($) { undef }

sub new($class, $args)
{
	if ($args =~ m/^(.*?)\/+$/o and defined $class->dirclass) {
		bless { name => $1 }, $class->dirclass;
	} else {
		bless { name => $args }, $class;
	}
}

sub destate($self, $state)
{
	$state->{lastfileobject} = $self;
	$self->compute_fullname($state);
}

sub set_tempname($self, $tempname)
{
	$self->{tempname} = $tempname;
}

sub realname($self, $state)
{
	my $name = $self->fullname;
	if (defined $self->{tempname}) {
		$name = $self->{tempname};
	}
	return $state->{destdir}.$name;
}

sub compute_digest($self, $filename, $class = 'OpenBSD::sha')
{
	require OpenBSD::md5;
	return $class->new($filename);
}

# exec/unexec and friends
package OpenBSD::PackingElement::Action;
our @ISA=qw(OpenBSD::PackingElement::Object);

# persistent state for following objects
package OpenBSD::PackingElement::State;
our @ISA=qw(OpenBSD::PackingElement::Object);

# meta information, stored elsewhere
package OpenBSD::PackingElement::Meta;
our @ISA=qw(OpenBSD::PackingElement);

# XXX mix-in class, see comment at top of file
package OpenBSD::PackingElement::Unique;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub add_object($self, $plist)
{
	$self->destate($plist->{state});
	$plist->addunique($self);
	return $self;
}

sub remove($self, $plist)
{
	delete $plist->{$self->category};
}

sub category($self)
{
	return ref($self);
}

# all the stuff that ends up in signatures
package OpenBSD::PackingElement::VersionElement;
our @ISA=qw(OpenBSD::PackingElement::Meta);

# all dependency information
package OpenBSD::PackingElement::Depend;
our @ISA=qw(OpenBSD::PackingElement::VersionElement);

# Abstract class for all file-like elements
package OpenBSD::PackingElement::FileBase;
our @ISA=qw(OpenBSD::PackingElement::FileObject);

use File::Basename;

sub write($self, $fh)
{
	print $fh "\@comment no checksum\n" if defined $self->{nochecksum};
	print $fh "\@comment no debug\n" if defined $self->{nodebug};
	$self->SUPER::write($fh);
	if (defined $self->{d}) {
		$self->{d}->write($fh);
	}
	if (defined $self->{size}) {
		say $fh "\@size ", $self->{size};
	}
	if (defined $self->{ts}) {
		say $fh "\@ts ", $self->{ts};
	}
	if (defined $self->{symlink}) {
		say $fh "\@symlink ", $self->{symlink};
	}
	if (defined $self->{link}) {
		say $fh "\@link ", $self->{link};
	}
	if (defined $self->{tempname}) {
		say $fh "\@temp ", $self->{tempname};
	}
}

sub destate($self, $state)
{
	$self->SUPER::destate($state);
	$state->{lastfile} = $self;
	$state->{lastchecksummable} = $self;
	$self->compute_modes($state);
	if (defined $state->{nochecksum}) {
		$self->{nochecksum} = 1;
		undef $state->{nochecksum};
	}
	if (defined $state->{nodebug}) {
		$self->{nodebug} = 1;
		undef $state->{nodebug};
	}
}

sub add_digest($self, $d)
{
	$self->{d} = $d;
}

sub add_size($self, $sz)
{
	$self->{size} = $sz;
}

sub add_timestamp($self, $ts)
{
	$self->{ts} = $ts;
}

# XXX symlink/hardlinks are properties of File,
# because we want to use inheritance for other stuff.

sub make_symlink($self, $linkname)
{
	$self->{symlink} = $linkname;
}

sub make_hardlink($self, $linkname)
{
	$self->{link} = $linkname;
}

sub may_check_digest($self, $path, $state)
{
	if ($state->{check_digest}) {
		$self->check_digest($path, $state);
	}
}

sub check_digest($self, $path, $state)
{
	return if $self->{link} or $self->{symlink};
	if (!defined $self->{d}) {
		$state->log->fatal($state->f("#1 does not have a signature",
		    $self->fullname));
	}
	my $d = $self->compute_digest($path);
	if (!$d->equals($self->{d})) {
		$state->log->fatal($state->f("checksum for #1 does not match",
		    $self->fullname));
	}
	if ($state->verbose >= 3) {
		$state->say("Checksum match for #1", $self->fullname);
	}
}

sub IsFile($) { 1 }

package OpenBSD::PackingElement::FileWithDebugInfo;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

package OpenBSD::PackingElement::File;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

use OpenBSD::PackageInfo qw(is_info_name);
sub keyword($) { "file" }
__PACKAGE__->register_with_factory;

sub dirclass($) { "OpenBSD::PackingElement::Dir" }

sub needs_keyword($self)
{
	# files/dirnames that starts  with an @ will require a keyword
	return $self->stringize =~ m/\^@/;
}

sub add_object($self, $plist)
{
	$self->destate($plist->{state});
	my $j = is_info_name($self->name);
	if ($j && $self->cwd eq '.') {
		bless $self, "OpenBSD::PackingElement::$j";
		$self->add_object($plist);
	} else {
		$plist->add2list($self);
	}
	return $self;
}

package OpenBSD::PackingElement::Sample;
our @ISA=qw(OpenBSD::PackingElement::FileObject);

sub keyword($) { "sample" }
sub absolute_okay($) { 1 }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	if ($state->{lastfile} isa OpenBSD::PackingElement::SpecialFile) {
		die "Can't \@sample a specialfile: ",
		    $state->{lastfile}->stringize;
	}
	$self->{copyfrom} = $state->{lastfile};
	$self->compute_fullname($state);
	$self->compute_modes($state);
}

sub dirclass($) { "OpenBSD::PackingElement::Sampledir" }

# TODO @ghost data is not yet used
# it's meant for files that used to be "registered" but are
# somewhat autogenerated or something, and should vanish in a transparent way.
#
# the keyword was introduced very early but is (still) not used

# TODO @ghost data is not yet used
# it's meant for files that used to be "registered" but are
# somewhat autogenerated or something, and should vanish in a transparent way.
#
# the keyword was introduced very early but is (still) not used

package OpenBSD::PackingElement::Ghost;
our @ISA = qw(OpenBSD::PackingElement::FileObject);

sub keyword($) { "ghost" }
sub absolute_okay($) { 1 }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$self->compute_fullname($state);
	$self->compute_modes($state);
}

package OpenBSD::PackingElement::Sampledir;
our @ISA=qw(OpenBSD::PackingElement::DirBase OpenBSD::PackingElement::Sample);

sub absolute_okay($) { 1 }

sub destate($self, $state)
{
	$self->compute_fullname($state);
	$self->compute_modes($state);
}

package OpenBSD::PackingElement::RcScript;
use File::Basename;
our @ISA = qw(OpenBSD::PackingElement::FileBase);

sub keyword($) { "rcscript" }
sub absolute_okay($) { 1 }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$self->compute_fullname($state);
	$state->{lastfile} = $self;
	$state->{lastchecksummable} = $self;
	$self->compute_modes($state);
}

sub report($class, $state)
{
	my @l;
	for my $script (sort keys %{$state->{add_rcscripts}}) {
		next if $state->{delete_rcscripts}{$script};
		push(@l, $script);
	}
	if (@l > 0) {
		$state->say("The following new rcscripts were installed: #1",
		    join(' ', @l));
		$state->say("See rcctl(8) for details.");
	}
}

package OpenBSD::PackingElement::InfoFile;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

sub keyword($) { "info" }
__PACKAGE__->register_with_factory;
sub dirclass($) { "OpenBSD::PackingElement::Infodir" }

package OpenBSD::PackingElement::Shell;
our @ISA=qw(OpenBSD::PackingElement::FileWithDebugInfo);

sub keyword($) { "shell" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::Manpage;
use File::Basename;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

sub keyword($) { "man" }
__PACKAGE__->register_with_factory;

sub register_manpage($self, $state, $key)
{
	# optimization: don't bother registering stuff from partial packages
	# (makewhatis will complain that the names don't match anyway)
	return if defined $self->{tempname};
	my $fname = $self->fullname;
	if ($fname =~ m,^(.*/man(?:/\w+)?)/((?:man|cat)[1-9n]\w*/.*),) {
		push(@{$state->{$key}{$1}}, $2);
    	}
}

sub is_source($self)
{
	return $self->name =~ m/man\/man[^\/]+\/[^\/]+\.[\dln][^\/]?$/o;
}

sub source_to_dest($self)
{
	my $v = $self->name;
	$v =~ s/(man\/)man([^\/]+\/[^\/]+)\.[\dln][^\/]?$/$1cat$2.0/;
	return $v;
}

# assumes the source is nroff, launches nroff
sub format($self, $state, $dest, $destfh)
{
	my $base = $state->{base};
	my $fname = $base.$self->fullname;
	if (-z $fname) {
		$state->error("empty source manpage: #1", $fname);
		return;
	}
	open(my $fh, '<', $fname) or die "Can't read $fname: $!";
	my $line = <$fh>;
	close $fh;
	my @extra = ();
	# extra preprocessors as described in man.
	if ($line =~ m/^\'\\\"\s+(.*)$/o) {
		for my $letter (split '', $1) {
			if ($letter =~ m/[ept]/o) {
				push(@extra, "-$letter");
			} elsif ($letter eq 'r') {
				push(@extra, "-R");
			}
		}
	}
	my $d = dirname($dest);
	unless (-d $d) {
		mkdir($d);
	}
	if (my ($dir, $file) = $fname =~ m/^(.*)\/([^\/]+\/[^\/]+)$/) {
		my $r = $state->system(
		    sub() {
			open STDOUT, '>&', $destfh or
			    die "Can't write to $dest: $!";
			close $destfh;
			chdir($dir) or die "Can't chdir to $dir: $!";
		    },
		    $state->{groff} // OpenBSD::Paths->groff,
		    qw(-mandoc -mtty-char -E -Ww -Tascii -P -c),
		    @extra, '--', $file);
		if ($r != 0) {
			# system already displays an error message
			return;
		}
	} else {
		$state->error("Can't parse source name #1", $fname);
		return;
	}
	return 1;
}

package OpenBSD::PackingElement::Lib;
our @ISA=qw(OpenBSD::PackingElement::FileWithDebugInfo);

our $todo = 0;

sub keyword($) { "lib" }
__PACKAGE__->register_with_factory;

sub mark_ldconfig_directory($self, $state)
{
	$state->ldconfig->mark_directory($self->fullname);
}

sub parse($self, $filename)
{
	if ($filename =~ m/^(.*?)\/?lib([^\/]+)\.so\.(\d+)\.(\d+)$/o) {
		return ($2, $3, $4, $1);
	} else {
		return undef;
	}
}

sub is_a_library($) { 1 }

package OpenBSD::PackingElement::Binary;
our @ISA=qw(OpenBSD::PackingElement::FileWithDebugInfo);

sub keyword($) { "bin" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::StaticLib;
our @ISA=qw(OpenBSD::PackingElement::FileWithDebugInfo);

sub keyword($) { "static-lib" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::SharedObject;
our @ISA=qw(OpenBSD::PackingElement::FileWithDebugInfo);

sub keyword($) { "so" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::PkgConfig;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

sub keyword($) { "pkgconfig" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::LibtoolLib;
our @ISA=qw(OpenBSD::PackingElement::FileBase);

sub keyword($) { "ltlib" }
__PACKAGE__->register_with_factory;

# Comment is very special:
#	- some annotations are comments for historic reasons
#	- CVSTags need to be recognized for register-plist (obsolescent)
#	- tools like update-plist will recognize @comment'ed entries
#	and thus destate needs to run on normal comments
package OpenBSD::PackingElement::Comment;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub keyword($) { "comment" }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$self->{cwd} = $state->{cwd};
}

sub add($class, $plist, $args)
{
	if ($args =~ m/^\$OpenBSD.*\$\s*$/o) {
		return OpenBSD::PackingElement::CVSTag->add($plist, $args);
	} elsif ($args =~ m/^(?:subdir|pkgpath)\=(.*?)\s+cdrom\=(.*?)\s+ftp\=(.*?)\s*$/o) {
		return OpenBSD::PackingElement::ExtraInfo->add($plist, $1, $2, $3);
	} elsif ($args =~ m/^(?:subdir|pkgpath)\=(.*?)\s+ftp\=(.*?)\s*$/o) {
		return OpenBSD::PackingElement::ExtraInfo->add($plist, $1, undef, $2);
	} elsif ($args eq 'no checksum') {
		$plist->{state}{nochecksum} = 1;
		return;
	} elsif ($args eq 'no debug') {
		$plist->{state}{nodebug} = 1;
		return;
	} else {
		return $class->SUPER::add($plist, $args);
	}
}

package OpenBSD::PackingElement::CVSTag;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub keyword($) { 'comment' }

sub category($) { 'cvstags'}

# don't incorporate this into compared signatures
sub write_without_variation($, $)
{
}

package OpenBSD::PackingElement::sha;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('sha');

sub add($class, $plist, $args)
{
	require OpenBSD::md5;

	$plist->{state}->{lastchecksummable}->add_digest(OpenBSD::sha->fromstring($args));
	return;
}

package OpenBSD::PackingElement::symlink;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('symlink');

sub add($class, $plist, $args)
{
	$plist->{state}->{lastfile}->make_symlink($args);
	return;
}

package OpenBSD::PackingElement::hardlink;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('link');

sub add($class, $plist, $args)
{
	$plist->{state}->{lastfile}->make_hardlink($args);
	return;
}

package OpenBSD::PackingElement::temp;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('temp');

sub add($class, $plist, $args)
{
	$plist->{state}->{lastfile}->set_tempname($args);
	return;
}

package OpenBSD::PackingElement::size;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('size');

sub add($class, $plist, $args)
{
	$plist->{state}->{lastfile}->add_size($args);
	return;
}

package OpenBSD::PackingElement::ts;
our @ISA=qw(OpenBSD::PackingElement::Annotation);

__PACKAGE__->register_with_factory('ts');

sub add($class, $plist, $args)
{
	$plist->{state}->{lastfile}->add_timestamp($args);
	return;
}

package OpenBSD::PackingElement::Option;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub keyword($) { 'option' }
__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	if ($args eq 'no-default-conflict') {
		return OpenBSD::PackingElement::NoDefaultConflict->new;
	} elsif ($args eq 'manual-installation') {
		return OpenBSD::PackingElement::ManualInstallation->new;
	} elsif ($args eq 'firmware') {
		return OpenBSD::PackingElement::Firmware->new;
	} elsif ($args =~ m/always-update\s+(\S+)/) {
		return OpenBSD::PackingElement::AlwaysUpdate->new_with_hash($1);
	} elsif ($args eq 'always-update') {
		return OpenBSD::PackingElement::AlwaysUpdate->new;
	} elsif ($args eq 'updatedb') {
		return OpenBSD::PackingElement::UpdateDB->new;
	} elsif ($args eq 'is-branch') {
		return OpenBSD::PackingElement::IsBranch->new;
	} else {
		die "Unknown option: $args";
	}
}

package OpenBSD::PackingElement::UniqueOption;
our @ISA=qw(OpenBSD::PackingElement::Unique OpenBSD::PackingElement::Option);

sub stringize($self)
{
	return $self->category;
}

sub new($class, @)
{
	bless {}, $class;
}

package OpenBSD::PackingElement::NoDefaultConflict;
our @ISA=qw(OpenBSD::PackingElement::UniqueOption);

sub category($) { 'no-default-conflict' }

package OpenBSD::PackingElement::ManualInstallation;
our @ISA=qw(OpenBSD::PackingElement::UniqueOption);

sub category($) { 'manual-installation' }

# don't incorporate this in signatures for obvious reasons
sub write_no_sig($, $)
{
}

package OpenBSD::PackingElement::Firmware;
our @ISA=qw(OpenBSD::PackingElement::ManualInstallation);
sub category($) { 'firmware' }

package OpenBSD::PackingElement::UpdateDB;
our @ISA=qw(OpenBSD::PackingElement::UniqueOption);
sub category($)
{
	'updatedb';
}

package OpenBSD::PackingElement::AlwaysUpdate;
our @ISA=qw(OpenBSD::PackingElement::UniqueOption);

sub category($)
{
	'always-update';
}

sub stringize($self)
{
	my @l = ($self->category);
	if (defined $self->{hash}) {
		push(@l, $self->{hash});
	}
	return join(' ', @l);
}

sub hash_plist($self, $plist)
{
	delete $self->{hash};
	my $content;
	open my $fh, '>', \$content;
	$plist->write_without_variation($fh);
	close $fh;
	my $digest = Digest::SHA::sha256_base64($content);
	$self->{hash} = $digest;
}

sub new_with_hash($class, $hash)
{
	bless { hash => $hash}, $class;
}

package OpenBSD::PackingElement::IsBranch;
our @ISA=qw(OpenBSD::PackingElement::UniqueOption);

sub category($)
{
	'is-branch';
}
# The special elements that don't end in the right place
package OpenBSD::PackingElement::ExtraInfo;
our @ISA=qw(OpenBSD::PackingElement::Unique OpenBSD::PackingElement::Comment);

sub category($) { 'extrainfo' }

# TODO gc cdromn
sub new($class, $subdir, $cdrom, $ftp)
{
	$ftp =~ s/^\"(.*)\"$/$1/;
	$ftp =~ s/^\'(.*)\'$/$1/;
	my $o = bless { subdir => $subdir,
	    path => OpenBSD::PkgPath->new($subdir),
	    ftp => $ftp}, $class;
	if (defined $cdrom) {
		$cdrom =~ s/^\"(.*)\"$/$1/;
		$cdrom =~ s/^\'(.*)\'$/$1/;
		$o->{cdrom} = $cdrom;
	}
	return $o;
}


sub subdir($self)
{
	return $self->{subdir};
}

sub _may_quote($s)
{
	if ($s =~ m/\s/) {
		return '"'.$s.'"';
	} else {
		return $s;
	}
}

sub stringize($self)
{
	my @l = (
	    "pkgpath=".$self->{subdir});
	if (defined $self->{cdrom}) {
		push @l, "cdrom="._may_quote($self->{cdrom});
	}
	push(@l, "ftp="._may_quote($self->{ftp}));
	return join(' ', @l);
}

package OpenBSD::PackingElement::Name;
use File::Spec;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub keyword($) { "name" }
__PACKAGE__->register_with_factory;
sub category($) { "name" }

package OpenBSD::PackingElement::LocalBase;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub keyword($) { "localbase" }
__PACKAGE__->register_with_factory;
sub category($) { "localbase" }

# meta-info: where the package was downloaded/installed from
# (TODO not as useful as could be, because the workflow isn't effective!)
package OpenBSD::PackingElement::Url;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub keyword($) { "url" }
__PACKAGE__->register_with_factory;
sub category($) { "url" }

# don't incorporate this in signatures for obvious reasons
sub write_no_sig($, $)
{
}

package OpenBSD::PackingElement::Version;
our @ISA=qw(OpenBSD::PackingElement::Unique OpenBSD::PackingElement::VersionElement);

sub keyword($) { "version" }
__PACKAGE__->register_with_factory;
sub category($) { "version" }

package OpenBSD::PackingElement::Conflict;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub keyword($) { "conflict" }
__PACKAGE__->register_with_factory;
sub category($) { "conflict" }

sub spec($self)
{
	require OpenBSD::Search;
	return OpenBSD::Search::PkgSpec->new($self->name);
}

package OpenBSD::PackingElement::Dependency;
our @ISA=qw(OpenBSD::PackingElement::Depend);
use OpenBSD::Error;

sub keyword($) { "depend" }
__PACKAGE__->register_with_factory;
sub category($) { "depend" }

sub new($class, $args)
{
	my ($pkgpath, $pattern, $def) = split /\:/o, $args;
	bless { name => $def, pkgpath => $pkgpath, pattern => $pattern,
	    def => $def }, $class;
}

sub stringize($self)
{
	return join(':', map { $self->{$_}}
	    (qw(pkgpath pattern def)));
}

OpenBSD::Auto::cache(spec,
    sub($self) {
	require OpenBSD::Search;

	my $src;
	if ($self->{pattern} eq '=') {
		$src = $self->{def};
	} else {
		$src = $self->{pattern};
	}
	return OpenBSD::Search::PkgSpec->new($src)
	    ->add_pkgpath_hint($self->{pkgpath});
    });

package OpenBSD::PackingElement::Wantlib;
our @ISA=qw(OpenBSD::PackingElement::Depend);

sub category($) { "wantlib" }
sub keyword($) { "wantlib" }
__PACKAGE__->register_with_factory;

OpenBSD::Auto::cache(spec,
    sub($self) {
    	require OpenBSD::LibSpec;
	return OpenBSD::LibSpec->from_string($self->name);
    });

package OpenBSD::PackingElement::Libset;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub category($) { "libset" }
sub keyword($) { "libset" }
__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	if ($args =~ m/(.*)\:(.*)/) {
		return bless {name => $1, libs => [split(/\,/, $2)]}, $class;
	} else {
		die "Bad args for libset: $args";
	}
}

sub stringize($self)
{
	return $self->{name}.':'.join(',', @{$self->{libs}});
}

package OpenBSD::PackingElement::PkgPath;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub keyword($) { "pkgpath" }
__PACKAGE__->register_with_factory;
sub category($) { "pkgpath" }

sub new($class, $fullpkgpath)
{
	bless {name => $fullpkgpath,
	    path => OpenBSD::PkgPath::WithOpts->new($fullpkgpath)}, $class;
}

sub subdir($self)
{
	return $self->{name};
}

package OpenBSD::PackingElement::AskUpdate;
our @ISA=qw(OpenBSD::PackingElement::Meta);

sub new($class, $args)
{
	my ($pattern, $message) = split /\s+/o, $args, 2;
	bless { pattern => $pattern, message => $message}, $class;
}

sub stringize($self)
{
	return join(' ', map { $self->{$_}}
	    (qw(pattern message)));
}

sub keyword($) { "ask-update" }
__PACKAGE__->register_with_factory;
sub category($) { "ask-update" }

OpenBSD::Auto::cache(spec,
    sub($self) {
	require OpenBSD::PkgSpec;

	return OpenBSD::PkgSpec->new($self->{pattern})
    });

package OpenBSD::PackingElement::NewAuth;
our @ISA=qw(OpenBSD::PackingElement::Action);

package OpenBSD::PackingElement::NewUser;
our @ISA=qw(OpenBSD::PackingElement::NewAuth);

sub type($) { "user" }
sub category($) { "users" }
sub keyword($) { "newuser" }
__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	my ($name, $uid, $group, $loginclass, $comment, $home, $shell) =
	    split /\:/o, $args;
	bless { name => $name, uid => $uid, group => $group,
	    class => $loginclass,
	    comment => $comment, home => $home, shell => $shell }, $class;
}

sub destate($self, $state)
{
	my $uid = $self->{uid};
	$uid =~ s/^\!//;
	$state->{owners}{$self->{name}} = $uid;
}

# $self->check:
# 	3 possible results
#	- undef: nothing to check, user/group was not there
#	- 0: does not match
#	- 1: exists and matches
sub check($self)
{
	my ($name, $passwd, $uid, $gid, $quota, $class, $gcos, $dir, $shell,
	    $expire) = getpwnam($self->name);
	return undef unless defined $name;
	if ($self->{uid} =~ m/^\!(.*)$/o) {
		return 0 unless $uid == $1;
	}
	if ($self->{group} =~ m/^\!(.*)$/o) {
		my $g = $1;
		unless ($g =~ m/^\d+$/o) {
			$g = getgrnam($g);
			return 0 unless defined $g;
		}
		return 0 unless $gid eq $g;
	}
	if ($self->{class} =~ m/^\!(.*)$/o) {
		return 0 unless $class eq $1;
	}
	if ($self->{comment} =~ m/^\!(.*)$/o) {
		return 0 unless $gcos eq $1;
	}
	if ($self->{home} =~ m/^\!(.*)$/o) {
		return 0 unless $dir eq $1;
	}
	if ($self->{shell} =~ m/^\!(.*)$/o) {
		return 0 unless $shell eq $1;
	}
	return 1;
}

sub stringize($self)
{
	return join(':', map { $self->{$_}}
	    (qw(name uid group class comment home shell)));
}

package OpenBSD::PackingElement::NewGroup;
our @ISA=qw(OpenBSD::PackingElement::NewAuth);


sub type($) { "group" }
sub category($) { "groups" }
sub keyword($) { "newgroup" }
__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	my ($name, $gid) = split /\:/o, $args;
	bless { name => $name, gid => $gid }, $class;
}

sub destate($self, $state)
{
	my $gid = $self->{gid};
	$gid =~ s/^\!//;
	$state->{groups}{$self->{name}} = $gid;
}

sub check($self)
{
	my ($name, $passwd, $gid, $members) = getgrnam($self->name);
	return undef unless defined $name;
	if ($self->{gid} =~ m/^\!(.*)$/o) {
		return 0 unless $gid == $1;
	}
	return 1;
}

sub stringize($self)
{
	return join(':', map { $self->{$_}}
	    (qw(name gid)));
}

package OpenBSD::PackingElement::Cwd;
use File::Spec;
our @ISA=qw(OpenBSD::PackingElement::State);


sub keyword($) { 'cwd' }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$state->set_cwd($self->name);
}

package OpenBSD::PackingElement::Owner;
our @ISA=qw(OpenBSD::PackingElement::State);

sub keyword($) { 'owner' }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	delete $state->{uid};
	if ($self->name eq '') {
		undef $state->{owner};
	} else {
		$state->{owner} = $self->name;
		if (defined $state->{owners}{$self->name}) {
			$state->{uid} = $state->{owners}{$self->name};
		}
	}
}

package OpenBSD::PackingElement::Group;
our @ISA=qw(OpenBSD::PackingElement::State);

sub keyword($) { 'group' }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	delete $state->{gid};
	if ($self->name eq '') {
		undef $state->{group};
	} else {
		$state->{group} = $self->name;
		if (defined $state->{groups}{$self->name}) {
			$state->{gid} = $state->{groups}{$self->name};
		}
	}
}

package OpenBSD::PackingElement::Mode;
our @ISA=qw(OpenBSD::PackingElement::State);

sub keyword($) { 'mode' }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	if ($self->name eq '') {
		undef $state->{mode};
	} else {
		$state->{mode} = $self->name;
	}
}

package OpenBSD::PackingElement::ExeclikeAction;
use File::Basename;
use OpenBSD::Error;
our @ISA=qw(OpenBSD::PackingElement::Action);

sub command($self)
{
	return $self->name;
}

sub expand($self, $state)
{
	my $e = $self->command;
	if ($e =~ m/\%F/o) {
		die "Bad expand" unless defined $state->{lastfile};
		$e =~ s/\%F/$state->{lastfile}->{name}/g;
	}
	if ($e =~ m/\%D/o) {
		die "Bad expand" unless defined $state->{cwd};
		$e =~ s/\%D/$state->cwd/ge;
	}
	if ($e =~ m/\%B/o) {
		die "Bad expand" unless defined $state->{lastfile};
		$e =~ s/\%B/dirname($state->{lastfile}->fullname)/ge;
	}
	if ($e =~ m/\%f/o) {
		die "Bad expand" unless defined $state->{lastfile};
		$e =~ s/\%f/basename($state->{lastfile}->fullname)/ge;
	}
	return $e;
}

sub destate($self, $state)
{
	$self->{expanded} = $self->expand($state);
}

sub run($self, $state, $v = $self->{expanded})
{
	$state->ldconfig->ensure;
	$state->say("#1 #2", $self->keyword, $v) if $state->verbose >= 2;
	$state->log->system(OpenBSD::Paths->sh, '-c', $v) unless $state->{not};
}

# so tags are going to get triggered by packages we depend on.
# turns out it's simpler to have them as "actions" because that's basically
# what's going to happen, so destate is good for them, gives us access
# to things like %D
package OpenBSD::PackingElement::TagBase;
our @ISA=qw(OpenBSD::PackingElement::ExeclikeAction);

sub command($self)
{
	return $self->{params};
}

package OpenBSD::PackingElement::Tag;
our @ISA=qw(OpenBSD::PackingElement::TagBase);
sub keyword($) { 'tag' }

__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	my ($tag, $params) = split(/\s+/, $args, 2);
	bless {
		name => $tag,
		params => $params // '',
	    }, $class;
}

sub stringize($self)
{
	if ($self->{params} ne '') {
		return join(' ', $self->name, $self->{params});
	} else {
		return $self->name;
	}
}

# tags are a kind of dependency, we have a special list for them, BUT
# they're still part of the normal packing-list
sub add_object($self, $plist)
{
	push(@{$plist->{tags}}, $self);
	$self->SUPER::add_object($plist);
}

# and the define tag thingy is very similar... the main difference being
# how it's actually registered
package OpenBSD::PackingElement::DefineTag;
our @ISA=qw(OpenBSD::PackingElement::TagBase);

sub category($) {'define-tag'}
sub keyword($) { 'define-tag' }
__PACKAGE__->register_with_factory;

# define-tag may be parsed several times, but these objects must be
# unique for tag accumulation to work correctly
my $cache = {};

my $subclass = {
	'at-end' => 'Atend',
	'supersedes' => 'Supersedes',
	'cleanup' => 'Cleanup' };

sub new($class, $args)
{
	my ($tag, $mode, $params) = split(/\s+/, $args, 3);
	$cache->{$args} //= bless {
	    name => $tag,
	    mode => $mode,
	    params => $params,
	    }, $class;
}

sub stringize($self)
{
	return join(' ', $self->name, $self->{mode}, $self->{params});
}

sub add_object($self, $plist)
{
	my $sub = $subclass->{$self->{mode}};
	if (!defined $sub) {
		die "unknown mode for \@define-tag";
	}
	bless $self, "OpenBSD::PackingElement::DefineTag::$sub";
	push(@{$plist->{tags_definitions}{$self->name}}, $self);
	$self->SUPER::add_object($plist);
}

sub destate($, $)
{
}

package OpenBSD::PackingElement::DefineTag::Atend;
our @ISA = qw(OpenBSD::PackingElement::DefineTag);

sub add_tag($self, $tag, $mode, $state)
{
	# add the tag contents if they exist
	# they're stored in a hash because the order doesn't matter
	if ($tag->{params} ne '') {
		$self->{list}{$tag->{expanded}} = 1;
	}
	# special case: we have to run things *now* if deleting
	if ($mode eq 'delete' && $tag->{found_in_self} && !$state->replacing) {
		
		$self->run_tag($state) 
		    unless $state->{tags}{superseded}{$self->name};
		delete $state->{tags}{atend}{$self->name};
	} else {
		$state->{tags}{atend}{$self->name} = $self;
	}
}

sub run_tag($self, $state)
{
	my $command = $self->command;
	if ($command =~ m/\%D/) {
		$command =~ s/\%D/$state->{localbase}/g;
	}

	if ($command =~ m/\%l/) {
		my $l = join(' ', keys %{$self->{list}});
		$command =~ s/\%l/$l/g;
	}
	if ($command =~ m/\%u/) {
		for my $p (keys %{$self->{list}}) {
			my $v = $command;
			$v =~ s/\%u/$p/g;
			$self->run($state, $v);
			$state->say("Running #1", $v) 
			    if $state->defines("TRACE_TAGS");
		}
	} else {
		$self->run($state, $command);
		$state->say("Running #1", $command)
		    if $state->defines("TRACE_TAGS");
	}
}

sub need_params($self)
{
	return $self->{params} =~ m/\%[lu]/;
}

package OpenBSD::PackingElement::DefineTag::Cleanup;
our @ISA = qw(OpenBSD::PackingElement::DefineTag);

sub add_tag($self, $tag, $mode, $state)
{
	# okay, we don't need to look at directories if we're not deleting
	return unless $mode eq 'delete';
	# this does not work at all like 'at-end'
	# instead we record a hash of directories we may want to cleanup
	push(@{$state->{tag_cleanup}{$tag->{expanded}}}, $self);
}

sub need_params($)
{
	1
}

package OpenBSD::PackingElement::DefineTag::Supersedes;
our @ISA = qw(OpenBSD::PackingElement::DefineTag);

sub add_tag($self, $tag, $, $state)
{
	$state->{tags}{superseded}{$self->{params}} = 1;
}

sub need_params($)
{
	0
}

package OpenBSD::PackingElement::Exec;
our @ISA=qw(OpenBSD::PackingElement::ExeclikeAction);

sub keyword($) { "exec" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::ExecAlways;
our @ISA=qw(OpenBSD::PackingElement::Exec);

sub keyword($) { "exec-always" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::ExecAdd;
our @ISA=qw(OpenBSD::PackingElement::Exec);

sub keyword($) { "exec-add" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::ExecUpdate;
our @ISA=qw(OpenBSD::PackingElement::Exec);

sub keyword($) { "exec-update" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::Unexec;
our @ISA=qw(OpenBSD::PackingElement::ExeclikeAction);

sub keyword($) { "unexec" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::UnexecAlways;
our @ISA=qw(OpenBSD::PackingElement::Unexec);

sub keyword($) { "unexec-always" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::UnexecUpdate;
our @ISA=qw(OpenBSD::PackingElement::Unexec);

sub keyword($) { "unexec-update" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::UnexecDelete;
our @ISA=qw(OpenBSD::PackingElement::Unexec);

sub keyword($) { "unexec-delete" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::ExtraUnexec;
our @ISA=qw(OpenBSD::PackingElement::ExeclikeAction);

sub keyword($) { "extraunexec" }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::DirlikeObject;
our @ISA=qw(OpenBSD::PackingElement::FileObject);

# XXX mix-in class, see comment at top of file
package OpenBSD::PackingElement::DirBase;
our @ISA=qw(OpenBSD::PackingElement::DirlikeObject);

sub destate($self, $state)
{
	$state->{lastdir} = $self;
	$self->SUPER::destate($state);
}


sub stringize($self)
{
	return $self->name."/";
}

sub write($self, $fh)
{
	$self->SUPER::write($fh);
}

package OpenBSD::PackingElement::Dir;
our @ISA=qw(OpenBSD::PackingElement::DirBase);

sub keyword($) { "dir" }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$self->SUPER::destate($state);
	$self->compute_modes($state);
}

sub needs_keyword($self)
{
	return $self->stringize =~ m/\^@/o;
}

package OpenBSD::PackingElement::Infodir;
our @ISA=qw(OpenBSD::PackingElement::Dir);
sub keyword($) { "info" }
sub needs_keyword($) { 1 }

package OpenBSD::PackingElement::Fontdir;
our @ISA=qw(OpenBSD::PackingElement::Dir);
sub keyword($) { "fontdir" }
__PACKAGE__->register_with_factory;
sub needs_keyword($) { 1 }
sub dirclass($) { "OpenBSD::PackingElement::Fontdir" }

sub install($self, $state)
{
	$self->SUPER::install($state);
	$state->log("You may wish to update your font path for #1", $self->fullname)
		unless $self->fullname =~ /^\/usr\/local\/share\/fonts/;
	$state->{recorder}{fonts_todo}{$state->{destdir}.$self->fullname} = 1;
}

sub reload($self, $state)
{
	$state->{recorder}{fonts_todo}{$state->{destdir}.$self->fullname} = 1;
}

sub _update_fontalias($state, $dirname)
{
	my $alias_name = "$dirname/fonts.alias";
	if ($state->verbose > 1) {
		$state->say("Assembling #1 from #2", 
		    $alias_name, "$alias_name-*");
	}

	if (open my $out, '>', $alias_name) {
		for my $alias (glob "$alias_name-*") {
			if (open my $f ,'<', $alias) {
				print {$out} <$f>;
				close $f;
			} else {
				$state->errsay("Couldn't read #1: #2", 
				    $alias, $!);
			}
		}
		close $out;
	} else {
		$state->errsay("Couldn't write #1: #2", $alias_name, $!);
	}
}

sub _restore_fontdir($state, $dirname)
{
	if (-f "$dirname/fonts.dir.dist") {

		unlink("$dirname/fonts.dir");
		$state->copy_file("$dirname/fonts.dir.dist",
		    "$dirname/fonts.dir");
	}
}

sub _run_if_exists($state, $user, $cmd, @l)
{
	unshift(@l, $cmd);
	if (-x $cmd) {
		if (defined $user) {
			my (undef, undef, $uid, $gid) = getpwnam($user);
			if (!defined $uid) {
				$state->log->fatal(
				    $state->f("Couldn't change identity: no #1 user",
				        $user));
			}
			unshift(@l, sub() { $state->change_user($uid, $gid); });
		}
		$state->vsystem(@l);
	} else {
		$state->errsay("#1 not found", $cmd);
	}
}

sub finish($class, $state)
{
	return if $state->{not};

	my @l = keys %{$state->{recorder}->{fonts_todo}};
	@l = grep {-d $_} @l;

	if (@l != 0) {
		$state->print("Updating font cache: ") if $state->verbose < 2;
		require OpenBSD::Error;

		map { _update_fontalias($state, $_) } @l;
		_run_if_exists($state, undef,
		    OpenBSD::Paths->mkfontscale, '--', @l);
		_run_if_exists($state, undef,
		    OpenBSD::Paths->mkfontdir, '--', @l);
		map { _restore_fontdir($state, $_) } @l;

		_run_if_exists($state, '_fc-cache',
		    OpenBSD::Paths->fc_cache, '--', @l);
		$state->say("ok") if $state->verbose < 2;
	}
}


package OpenBSD::PackingElement::Mandir;
our @ISA=qw(OpenBSD::PackingElement::Dir);

sub keyword($) { "mandir" }
__PACKAGE__->register_with_factory;
sub needs_keyword($) { 1 }
sub dirclass($) { "OpenBSD::PackingElement::Mandir" }

package OpenBSD::PackingElement::Extra;
our @ISA=qw(OpenBSD::PackingElement::FileObject);

sub keyword($) { 'extra' }
sub absolute_okay($) { 1 }
__PACKAGE__->register_with_factory;

sub destate($self, $state)
{
	$self->compute_fullname($state);
}

sub dirclass($) { "OpenBSD::PackingElement::Extradir" }

package OpenBSD::PackingElement::Extradir;
our @ISA=qw(OpenBSD::PackingElement::DirBase OpenBSD::PackingElement::Extra);
sub absolute_okay($) { 1 }

sub destate	# forwarder
{
	&OpenBSD::PackingElement::Extra::destate;
}

package OpenBSD::PackingElement::ExtraGlob;
our @ISA=qw(OpenBSD::PackingElement::FileObject);

sub keyword($) { 'extraglob' }
sub absolute_okay($) { 1 }
__PACKAGE__->register_with_factory;

package OpenBSD::PackingElement::SpecialFile;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub add_digest	# forwarder
{
	&OpenBSD::PackingElement::FileBase::add_digest;
}

sub add_size	# forwarder
{
	&OpenBSD::PackingElement::FileBase::add_size;
}

sub add_timestamp($, $)
{
	# just don't
}

sub compute_digest	# forwarder
{
	&OpenBSD::PackingElement::FileObject::compute_digest;
}

sub write	# forwarder
{
	&OpenBSD::PackingElement::FileBase::write;
}

sub needs_keyword($) { 0 }

sub add_object($self, $plist)
{
	$self->{infodir} = $plist->{infodir};
	$self->SUPER::add_object($plist);
}

sub infodir($self)
{
	return ${$self->{infodir}};
}

sub stringize($self)
{
	return $self->category;
}

sub fullname($self)
{
	my $d = $self->infodir;
	if (defined $d) {
		return $d.$self->name;
	} else {
		return undef;
	}
}

sub category($self)
{
	return $self->name;
}

sub new	# forwarder
{
	&OpenBSD::PackingElement::UniqueOption::new;
}

sub may_verify_digest($self, $state)
{
	if (!$state->{check_digest}) {
		return;
	}
	if (!defined $self->{d}) {
		$state->log->fatal($state->f("#1 does not have a signature",
		    $self->fullname));
	}
	my $d = $self->compute_digest($self->fullname);
	if (!$d->equals($self->{d})) {
		$state->log->fatal($state->f("checksum for #1 does not match",
		    $self->fullname));
	}
	if ($state->verbose >= 3) {
		$state->say("Checksum match for #1", $self->fullname);
	}
}

package OpenBSD::PackingElement::FCONTENTS;
our @ISA=qw(OpenBSD::PackingElement::SpecialFile);
sub name($) { OpenBSD::PackageInfo::CONTENTS }
# XXX we don't write `self'
sub write($, $)
{}

sub copy_shallow_if($, $, $)
{
}

sub copy_deep_if($, $, $)
{
}

# CONTENTS doesn't have a checksum
sub may_verify_digest($, $)
{
}

package OpenBSD::PackingElement::FDESC;
our @ISA=qw(OpenBSD::PackingElement::SpecialFile);
sub name($) { OpenBSD::PackageInfo::DESC }

package OpenBSD::PackingElement::DisplayFile;
our @ISA=qw(OpenBSD::PackingElement::SpecialFile);
use OpenBSD::Error;

sub prepare($self, $state)
{
	my $fname = $self->fullname;
	if (open(my $src, '<', $fname)) {
		while (<$src>) {
			chomp;
			next if m/^\+\-+\s*$/o;
			s/^[+-] //o;
			$state->log("#1", $_);
		}
	} else {
		$state->errsay("Can't open #1: #2", $fname, $!);
    	}
}

package OpenBSD::PackingElement::FDISPLAY;
our @ISA=qw(OpenBSD::PackingElement::DisplayFile);
sub name($) { OpenBSD::PackageInfo::DISPLAY }

package OpenBSD::PackingElement::FUNDISPLAY;
our @ISA=qw(OpenBSD::PackingElement::DisplayFile);
sub name($) { OpenBSD::PackageInfo::UNDISPLAY }

package OpenBSD::PackingElement::Arch;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub category($) { 'arch' }
sub keyword($) { 'arch' }
__PACKAGE__->register_with_factory;

sub new($class, $args)
{
	my @arches= split(/\,/o, $args);
	bless { arches => \@arches }, $class;
}

sub stringize($self)
{
	return join(',', @{$self->{arches}});
}

sub check($self, $forced_arch = undef)
{
	for my $ok (@{$self->{arches}}) {
		return 1 if $ok eq '*';
		if (defined $forced_arch) {
			if ($ok eq $forced_arch) {
				return 1;
			} else {
				next;
			}
		}
		return 1 if $ok eq OpenBSD::Paths->machine_architecture;
		return 1 if $ok eq OpenBSD::Paths->architecture;
	}
	return 0;
}

package OpenBSD::PackingElement::Signer;
our @ISA=qw(OpenBSD::PackingElement::Unique);
sub keyword($) { 'signer' }
__PACKAGE__->register_with_factory;
sub category($) { "signer" }
sub new($class, $args)
{
	unless ($args =~ m/^[\w\d\.\-\+\@]+$/) {
		die "Invalid characters in signer $args";
	}
	$class->SUPER::new($args);
}

# don't incorporate this into compared signatures
sub write_without_variation($, $)
{
}

# XXX digital-signatures have to be unique, since they are a part
# of the unsigned packing-list, with only the b64sig part removed
# (likewise for signer)
package OpenBSD::PackingElement::DigitalSignature;
our @ISA=qw(OpenBSD::PackingElement::Unique);

sub keyword($) { 'digital-signature' }
__PACKAGE__->register_with_factory;
sub category($) { "digital-signature" }

# parse to and from a subset of iso8601
#
# allows us to represent timestamps in a human readable format without
# any ambiguity
sub _time_to_iso8601($time)
{
	my ($sec, $min, $hour, $day, $month, $year, @rest) = gmtime($time);
	return sprintf("%04d-%02d-%02dT%02d:%02d:%02dZ",
	    $year+1900, $month+1, $day, $hour, $min, $sec);
}

sub iso8601($self)
{
	return _time_to_iso8601($self->{timestamp});
}

sub _iso8601_to_time($s)
{
	# XXX RFC 9557 explicitly wants +00:00 instead of Z for UTC,
	# so recognize both
	if ($s =~ m/^(\d{4})\-(\d{2})\-(\d{2})T(\d{2})\:(\d{2})\:(\d{2})(?:Z|\+00\:00)$/) {
		my ($year, $month, $day, $hour, $min, $sec) =
		    ($1 - 1900, $2-1, $3, $4, $5, $6);
		require POSIX;
		local $ENV{TZ} = 'UTC';
		my $t = POSIX::mktime($sec, $min, $hour, $day, $month, $year);
		return $t;
	} else {
		die "Incorrect ISO8601 timestamp: $s";
	}
}

sub new($class, $args)
{
	my ($key, $tsbase, $tsmin, $tssec, $signature) = split(/\:/, $args);
	my $timestamp = _iso8601_to_time("$tsbase:$tsmin:$tssec");
	bless { key => $key, timestamp => $timestamp, b64sig => $signature },
		$class;
}

sub blank($class, $type)
{
	bless { key => $type, timestamp => time, b64sig => '' }, $class;
}

sub stringize($self)
{
	return join(':', $self->{key}, _time_to_iso8601($self->{timestamp}),
	    $self->{b64sig});
}

sub write_no_sig($self, $fh)
{
	say $fh "\@", $self->keyword, " ", $self->{key}, ":",
	    _time_to_iso8601($self->{timestamp});
}

# don't incorporate this into compared signatures
sub write_without_variation($, $)
{
}

package OpenBSD::PackingElement::Old;
our @ISA=qw(OpenBSD::PackingElement);

my $warned;

sub new($class, $k, $args)
{
	bless { keyword => $k, name => $args }, $class;
}

sub add($o, $plist, $args)
{
	my $keyword = $$o;
	if (!$warned->{$keyword}) {
		say STDERR "Warning: obsolete construct: \@$keyword $args";
		$warned->{$keyword} = 1;
	}
	my $o2 = OpenBSD::PackingElement::Old->new($keyword, $args);
	$o2->add_object($plist);
	$plist->{deprecated} = 1;
	return undef;
}

sub keyword($self)
{
	return $self->{keyword};
}

sub register_old_keyword($class, $k)
{
	$class->register_with_factory($k, bless \$k, $class);
}

for my $k (qw(src display mtree ignore_inst dirrm pkgcfl pkgdep newdepend
    libdepend endfake ignore vendor incompatibility md5 sysctl)) {
	__PACKAGE__->register_old_keyword($k);
}

# pkgpath objects are parsed in extrainfo and pkgpath objects
# so that erroneous pkgpaths will be flagged early
package OpenBSD::PkgPath;
sub new($class, $fullpkgpath)
{
	my ($dir, @mandatory) = split(/\,/, $fullpkgpath);
	my $o = 
	    bless {dir => $dir,
		mandatory => {map {($_, 1)} @mandatory},
	    }, $class;
	my @sub = grep {/^\-/} @mandatory;
	if (@sub > 1) {
		say STDERR "Invalid $fullpkgpath (multiple subpackages)";
		exit 1;
	}
	if (@sub == 1) {
		$o->{subpackage} = shift @sub;
	}
	return $o;
}

sub fullpkgpath($self)
{
	if(%{$self->{mandatory}}) {
		my $m = join(",", keys %{$self->{mandatory}});
		return "$self->{dir},$m";
	} else {
		return $self->{dir};
	}
}

# a pkgpath has a dir, and some flavors/multi parts. To match, we must
# remove them all. So, keep a full hash of everything we have (has), and
# when stuff $to_rm matches, remove them from $from.
# We match when we're left with nothing.
sub trim($self, $has, $from, $to_rm)
{
	for my $f (keys %$to_rm) {
		if ($has->{$f}) {
			delete $from->{$f};
		} else {
			return 0;
		}
	}
	return 1;
}

# basic match: after mandatory, nothing left
sub match2($self, $has, $h)
{
	if (keys %$h) {
		return 0;
	} else {
		return 1;
	}
}

# zap mandatory, check that what's left is okay.
sub match($self, $other)
{
	# make a copy of options
	my %h = %{$other->{mandatory}};
	if (!$self->trim($other->{mandatory}, \%h, $self->{mandatory})) {
		return 0;
	}
	if ($self->match2($other->{mandatory}, \%h)) {
		return 1;
	} else {
		return 0;
	}
}

package OpenBSD::PkgPath::WithOpts;
our @ISA = qw(OpenBSD::PkgPath);

sub new($class, $fullpkgpath)
{
	my @opts = ();
	while ($fullpkgpath =~ s/\[\,(.*?)\]//) {
		push(@opts, {map {($_, 1)} split(/\,/, $1) });
	};
	my $o = $class->SUPER::new($fullpkgpath);
	if (@opts == 0) {
		bless $o, "OpenBSD::PkgPath";
	} else {
		$o->{opts} = \@opts;
	}
	return $o;
}

# match with options: systematically trim any optional part that  fully
# matches, until we're left with nothing, or some options keep happening.
sub match2($self, $has, $h)
{
	if (!keys %$h) {
		return 1;
	}
	for my $opts (@{$self->{opts}}) {
		my %h2 = %$h;
		if ($self->trim($has, \%h2, $opts)) {
			$h = \%h2;
			if (!keys %$h) {
				return 1;
			}
		}
	}
	return 0;
}

1;
