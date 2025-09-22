# ex:ts=8 sw=4:
# $OpenBSD: SharedLibs.pm,v 1.62 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2010 Marc Espie <espie@openbsd.org>
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

use OpenBSD::Paths;
use OpenBSD::LibSpec;

package OpenBSD::PackingElement;

sub mark_available_lib($, $, $)
{
}

package OpenBSD::PackingElement::Lib;

sub mark_available_lib($self, $pkgname, $object)
{
	$object->register_libname($self->fullname, $pkgname);
}

package OpenBSD::SharedLibs;
use File::Basename;
use OpenBSD::Error;

sub _basestate($)
{
	require OpenBSD::Basestate;
	return 'OpenBSD::BaseState';
}

sub new($class, $state = $class->_basestate)
{
	bless {
	    state => $state,
	    repo => OpenBSD::LibRepo->new,
	    printed => {},
	    done_plist => {},
	    done_system => 0
	    }, $class;
}

sub register_library($self, $lib, $pkgname)
{
	$self->{repo}->register($lib, $pkgname);
}

sub register_libname($self, $name, $pkgname)
{
	my $lib = OpenBSD::Library->from_string($name);
	if ($lib->is_valid) {
		$self->register_library($lib, $pkgname);
	} else {
		$self->{state}->errsay("Bogus library in #1: #2", $pkgname, 
		    $name) unless $pkgname eq 'system';
	}

}

sub find_best($self, $stem)
{
	return $self->{repo}->find_best($stem);
}

sub system_dirs($)
{
	return OpenBSD::Paths->library_dirs;
}

sub add_libs_from_system($self, $destdir)
{
	return if $self->{done_system};
	$self->{done_system} = 1;
	for my $dirname ($self->system_dirs) {
		opendir(my $dir, $destdir.$dirname."/lib") or next;
		while (my $d = readdir($dir)) {
			next unless $d =~ m/\.so/;
			$self->register_libname("$dirname/lib/$d", 'system');
		}
		closedir($dir);
	}
}

sub add_libs_from_installed_package($self, $pkgname)
{
	return if $self->{done_plist}{$pkgname};
	$self->{done_plist}{$pkgname} = 1;
	my $plist = OpenBSD::PackingList->from_installation($pkgname,
	    \&OpenBSD::PackingList::LibraryOnly);
	return if !defined $plist;

	$plist->mark_available_lib($pkgname, $self);
}

sub add_libs_from_plist($self, $plist)
{
	my $pkgname = $plist->pkgname;
	return if $self->{done_plist}{$pkgname};
	$self->{done_plist}{$pkgname} = 1;
	$plist->mark_available_lib($pkgname, $self);
}

sub lookup_libspec($self, $base, $spec)
{
	return $spec->lookup($self->{repo}, $base);
}


sub report_problem($self, $spec)
{
	my $name = $spec->to_string;
	my $state = $self->{state};
	my $base = $state->{localbase};
	my $approx = $spec->lookup_stem($self->{repo});
	my $printed = $self->{printed};

	my $r = "";
	if (!$spec->is_valid) {
		$r = "| bad library specification\n";
	} elsif (!defined $approx) {
 		$r = "| not found anywhere\n";
	} else {
		for my $bad (sort {$a->compare($b)} @$approx) {
			my $ouch = $spec->no_match($bad, $base);
			$ouch //= "not reachable";
			$r .= "| ".$bad->to_string." (".$bad->origin."): ".
			    $ouch."\n";
		}
	}
	if (!defined $printed->{$name} || $printed->{$name} ne $r) {
		$printed->{$name} = $r;
		$state->errsay("|library #1 not found", $name);
		$state->print("#1", $r);
	}
}

1;
