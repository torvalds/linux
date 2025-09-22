# ex:ts=8 sw=4:
# $OpenBSD: Handle.pm,v 1.44 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2007-2009 Marc Espie <espie@openbsd.org>
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

# fairly non-descriptive name. Used to store various package information
# during installs and updates.

use v5.36;

package OpenBSD::Handle;

use OpenBSD::PackageInfo;
use OpenBSD::Error;

use constant {
	BAD_PACKAGE => 1,
	CANT_INSTALL => 2,
	ALREADY_INSTALLED => 3,
	NOT_FOUND => 4,
	CANT_DELETE => 5,
};

sub is_real($) { return 1; }

sub cleanup($self, $error = undef, $errorinfo = undef)
{
	if (defined $error) {
		$self->{error} //= $error;
		$self->{errorinfo} //= $errorinfo;
	}
	if (defined $self->location) {
		if (defined $self->{error} && $self->{error} == BAD_PACKAGE) {
			$self->location->close_with_client_error;
		} else {
			$self->location->close_now;
		}
		$self->location->wipe_info;
	}
	delete $self->{plist};
	delete $self->{db};
	delete $self->{conflict_list};
}

sub new($class)
{
	return bless {}, $class;
}

sub system($class)
{
	return OpenBSD::Handle::BaseSystem->new;
}

sub pkgname($self)
{
	if (!defined $self->{pkgname}) {
		if (defined $self->{plist}) {
			$self->{pkgname} = $self->{plist}->pkgname;
		} elsif (defined $self->{location}) {
			$self->{pkgname} = $self->{location}->name;
		} elsif (defined $self->{name}) {
			require OpenBSD::PackageName;

			$self->{pkgname} =
			    OpenBSD::PackageName::url2pkgname($self->{name});
		}
	}

	return $self->{pkgname};
}

sub location($self)
{
	return $self->{location};
}

sub plist($self)
{
	return $self->{plist};
}

sub dependency_info($self)
{
	if (defined $self->{plist}) {
		return $self->{plist};
	} elsif (defined $self->{location} && 
	    defined $self->{location}{update_info}) {
		return $self->{location}{update_info};
	} else {
		return undef;
	}
}

OpenBSD::Auto::cache(conflict_list,
    sub($self) {
    	require OpenBSD::PkgCfl;
	return OpenBSD::PkgCfl->make_conflict_list($self->dependency_info);
    });

sub set_error($self, $error)
{
	$self->{error} = $error;
}

sub has_error($self, $error = undef)
{
	if (!defined $self->{error}) {
		return undef;
	}
	if (defined $error) {
		return $self->{error} eq $error;
	}
	return $self->{error};
}

sub has_reported_error($self)
{
	return $self->{error_reported};
}

sub error_message($self)
{
	my $error = $self->{error};
	if ($error == BAD_PACKAGE) {
		return "bad package";
	} elsif ($error == CANT_INSTALL) {
		if ($self->{errorinfo}) {
			return "$self->{errorinfo}";
		} else {
			return "can't install";
		}
	} elsif ($error == NOT_FOUND) {
		return "not found";
	} elsif ($error == ALREADY_INSTALLED) {
		return "already installed";
	} else {
		return "no error";
	}
}

sub complete_old($self)
{
	my $location = $self->{location};

	if (!defined $location) {
		$self->set_error(NOT_FOUND);
    	} else {
		my $plist = $location->plist;
		if (!defined $plist) {
			$self->set_error(BAD_PACKAGE);
		} else {
			$self->{plist} = $plist;
			delete $location->{contents};
			delete $location->{update_info};
		}
	}
}

sub complete_dependency_info($self)
{
	my $location = $self->{location};

	if (!defined $location) {
		$self->set_error(NOT_FOUND);
	} else {
		if (!defined $self->{plist}) {
			# trigger build
			$location->update_info;
		}
	}
}

sub create_old($class, $pkgname, $state)
{
	my $self= $class->new;
	$self->{name} = $pkgname;

	my $location = $state->repo->installed->find($pkgname);
	if (defined $location) {
		$self->{location} = $location;
	}
	$self->complete_dependency_info;

	return $self;
}

sub create_new($class, $pkg)
{
	my $handle = $class->new;
	$handle->{name} = $pkg;
	$handle->{tweaked} = 0;
	return $handle;
}

sub from_location($class, $location)
{
	my $handle = $class->new;
	$handle->{location} = $location;
	$handle->{tweaked} = 0;
	return $handle;
}

sub get_plist($handle, $state)
{
	my $location = $handle->{location};
	my $pkg = $handle->pkgname;

	if ($state->verbose >= 2) {
		$state->say("#1parsing #2", $state->deptree_header($pkg), $pkg);
	}
	my $plist = $location->plist;
	unless (defined $plist) {
		$state->say("Can't find CONTENTS from #1", $location->url)
		    unless $location->{error_reported};
		$location->close_with_client_error;
		$location->wipe_info;
		$handle->set_error(BAD_PACKAGE);
		$handle->{error_reported} = 1;
		return;
	}
	delete $location->{update_info};
	$location->decorate($plist);
	if ($plist->localbase ne $state->{localbase}) {
		$state->say("Localbase mismatch: package has: #1, user wants: #2",
		    $plist->localbase, $state->{localbase});
		$location->close_with_client_error;
		$location->wipe_info;
		$handle->set_error(BAD_PACKAGE);
		return;
	}
	my $pkgname = $handle->{pkgname} = $plist->pkgname;

	if ($pkg ne '-') {
		if (!defined $pkgname or $pkg ne $pkgname) {
			$state->say("Package name is not consistent ???");
			$location->close_with_client_error;
			$location->wipe_info;
			$handle->set_error(BAD_PACKAGE);
			return;
		}
	}
	$handle->{plist} = $plist;
}

sub get_location($handle, $state)
{
	my $name = $handle->{name};

	my $location = $state->repo->find($name);
	if (!$location) {
		$state->print("#1", $state->deptree_header($name));
		$handle->set_error(NOT_FOUND);
		$handle->{tweaked} =
		    OpenBSD::Add::tweak_package_status($handle->pkgname,
			$state);
		if (!$handle->{tweaked}) {
			$state->say("Can't find #1", $name);
			$handle->{error_reported} = 1;
			eval {
				my $r = [$name];
				$state->quirks->filter_obsolete($r, $state);
			}
		}
		return;
	}
	$handle->{location} = $location;
	$handle->{pkgname} = $location->name;
}

sub complete($handle, $state)
{
	return if $handle->has_error;

	if (!defined $handle->{location}) {
		$handle->get_location($state);
	}
	return if $handle->has_error;
	if (!defined $handle->{plist}) {
		$handle->get_plist($state);
	}
}

package OpenBSD::Handle::BaseSystem;
our @ISA = qw(OpenBSD::Handle);
sub pkgname($) { return "BaseSystem"; }

sub is_real($) { return 0; }

1;
