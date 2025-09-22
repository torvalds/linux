# ex:ts=8 sw=4:
# $OpenBSD: ArcCheck.pm,v 1.42 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2005-2006 Marc Espie <espie@openbsd.org>
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

# Supplementary code to handle archives in the package context.
# Ustar allows about anything, but we want to forbid a lot of things.
# this code is used during creation and extraction, "bridging" the gap
# between tar balls OpenBSD::Ustar::Object and 
# packing list OpenBSD::PackingElement

# specifically, during create time:
# 	$o = $archive->prepare_long($item);
# 	if (!$o->verify_modes($self))
# 		error...
# 	if (!$o->is_allowed)
#		error...
# - prevent a lot of weird objects from entering the archives
# - make sure all relevant users/modes are recorded in the PLIST item

# during extraction:
#	$o->validate_meta($item) or
#		error...

use v5.36;

use OpenBSD::Ustar;

package OpenBSD::Ustar::Object;
use POSIX;

sub is_allowed($) { 0 }

# match archive header link name against actual link name
sub _check_linkname($self, $linkname)
{
	my $c = $self->{linkname};
	if ($self->isHardLink && defined $self->{cwd}) {
		$c = $self->{cwd}.'/'.$c;
	}
	return $c eq $linkname;
}

sub _errsay($o, @msg)
{
	$o->{archive}{state}->errsay(@msg);
}

sub validate_meta($o, $item)
{
	$o->{cwd} = $item->cwd;
	if (defined $item->{symlink} || $o->isSymLink) {
		if (!defined $item->{symlink}) {
			$o->_errsay("bogus symlink #1 -> #2", 
			    $item->name, $o->{linkname});
			$o->_errsay("\t(no \@symlink annotation in packing-list)");
			return 0;
		}
		if (!$o->isSymLink) {
			$o->_errsay("bogus symlink #1 -> #2", 
			    $item->name, $item->{symlink});
			$o->_errsay("\t(not a symlink in the tarball)");
			return 0;
		}
		if (!$o->_check_linkname($item->{symlink})) {
			$o->_errsay("archive symlink does not match #1 != #2",
			    $o->{linkname}, $item->{symlink});
			return 0;
		}
	} elsif (defined $item->{link} || $o->isHardLink) {
		if (!defined $item->{link}) {
			$o->_errsay("bogus hardlink #1 -> #2",
			    $item->name, $o->{linkname});
			$o->_errsay("\t(no \@link annotation in packing-list)");
			return 0;
		}
		if (!$o->isHardLink) {
			$o->_errsay("bogus hardlink #1 -> #2",
			    $item->name, $item->{link});
			$o->_errsay("\t(not a link in the tarball)");
			return 0;
		}
		if (!$o->_check_linkname($item->{link})) {
			$o->_errsay("archive hardlink does not match #1 != #2",
			    $o->{linkname}, $item->{link});
			return 0;
		}
	} elsif ($o->isFile) {
		if (!defined $item->{size}) {
			$o->_errsay("Error: file #1 does not have recorded size",
			    $item->fullname);
			return 0;
		} elsif ($item->{size} != $o->{size}) {
			$o->_errsay("Error: size does not match for #1",
			    $item->fullname);
			return 0;
		}
	} else {
		$o->_errsay("archive content for #1 should be file", 
		    $item->name);
		return 0;
	}
	return $o->verify_modes($item);
}

sub _strip_modes($o, $item)
{
	my $result = $o->{mode};

	# disallow writable files/dirs without explicit annotation
	if (!defined $item->{mode}) {
		# if there's an owner, we have to be explicit
		if (defined $item->{owner}) {
			$result &= ~(S_IWUSR|S_IWGRP|S_IWOTH);
		} else {
			$result &= ~(S_IWGRP|S_IWOTH);
		}
		# and make libraries non-executable
		if ($item->is_a_library) {
			$result &= ~(S_IXUSR|S_IXGRP|S_IXOTH);
		}
		$result |= S_IROTH | S_IRGRP;
	}
	# XXX newer tarballs never have suid/sgid, but some 7.0 packages
	# still have them... We'll strip them here unconditionally instead
	# of in XXX /1 and XXX /2
	if (defined $item->{group} || defined $item->{owner}) {
		$result &= ~(S_ISUID|S_ISGID);
	}
	return $result;
}

sub _printable_mode($o)
{
	return sprintf("%4o", 
	    $o->{mode} & (S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID));
}

sub verify_modes($o, $item)
{
	my $result = 1;

	if (!defined $item->{owner}) {
		if ($o->{uname} ne 'root') {
			$o->_errsay("Error: no \@owner for #1 (#2)",
			    $item->fullname, $o->{uname});
	    		$result = 0;
		}
	}
	if (!defined $item->{group}) {
		if ($o->{gname} ne 'bin' && $o->{gname} ne 'wheel') {
			$o->_errsay("Error: no \@group for #1 (#2)",
			    $item->fullname, $o->{gname});
			$result = 0;
		}
	}
	# XXX /1
	$o->{mode} &= ~(S_ISUID|S_ISGID);
	if ($o->{mode} != $o->_strip_modes($o)) {
		$o->_errsay("Error: weird mode for #1: #2", $item->fullname,
		    $o->_printable_mode);
		    $result = 0;
 	}
	return $result;
}

package OpenBSD::Ustar::HardLink;
sub is_allowed($) { 1 }

package OpenBSD::Ustar::SoftLink;
sub is_allowed($) { 1 }

package OpenBSD::Ustar::File;
sub is_allowed($) { 1 }

package OpenBSD::Ustar;
use POSIX;

# prepare item according to pkg_create's rules.
sub prepare_long($self, $item)
{
	my $entry;
	if (defined $item->{wtempname}) {
		$entry = $self->prepare($item->{wtempname}, '');
	} else {
		$entry = $self->prepare($item->name);
	}
	if (defined $item->{owner}) {
		$entry->{uname} = $item->{owner};
		if (defined $item->{uid}) {
			$entry->{uid} = $item->{uid};
		} else {
			delete $entry->{uid};
		}
	} else {
		$entry->{uname} = "root";
		delete $entry->{uid};
	}
	if (defined $item->{group}) {
		$entry->{gname} = $item->{group};
		if (defined $item->{gid}) {
			$entry->{gid} = $item->{gid};
		} else {
			delete $entry->{gid};
		}
	} else {
		$entry->{gname} = "bin";
		delete $entry->{gid};
	}
	# likewise, we skip links on extractions, so hey, don't even care
	# about modes and stuff.
	if ($entry->isSymLink) {
		$entry->{mode} = 0777;
		$entry->{uname} = 'root';
		$entry->{gname} = 'wheel';
		delete $entry->{uid};
		delete $entry->{gid};
	}
	$entry->recheck_owner;
	if (!defined $entry->{uname}) {
		$self->fatal("No user name for #1 (uid #2)",
		    $item->name, $entry->{uid});
	}
	if (!defined $entry->{gname}) {
		$self->fatal("No group name for #1 (gid #2)",
		    $item->name, $entry->{gid});
	}
	# XXX /2
	$entry->{mode} = $entry->_strip_modes($item) & ~(S_ISUID|S_ISGID);
	if (defined $item->{ts}) {
		delete $entry->{mtime};
	}

	$entry->set_name($item->name);
	return $entry;
}

1;
