# ex:ts=8 sw=4:
# $OpenBSD: PackageInfo.pm,v 1.65 2023/06/13 09:07:17 espie Exp $
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

package OpenBSD::PackageInfo;
require Exporter;
our @ISA=qw(Exporter);
our @EXPORT=qw(installed_packages installed_info installed_name info_names is_info_name installed_stems
    lock_db unlock_db
    add_installed delete_installed is_installed borked_package 
    CONTENTS DESC REQUIRED_BY REQUIRING DISPLAY UNDISPLAY);

use OpenBSD::PackageName;
use OpenBSD::Paths;
use constant {
	CONTENTS => '+CONTENTS',
	DESC => '+DESC',
	REQUIRED_BY => '+REQUIRED_BY',
	REQUIRING => '+REQUIRING',
	DISPLAY => '+DISPLAY',
	UNDISPLAY => '+UNDISPLAY'};

use Fcntl qw/:flock/;
my $pkg_db = $ENV{"PKG_DBDIR"} || OpenBSD::Paths->pkgdb;

my ($list, $stemlist);

our @info = (CONTENTS, DESC, REQUIRED_BY, REQUIRING, DISPLAY, UNDISPLAY);

our %info = ();
for my $i (@info) {
	my $j = $i;
	$j =~ s/\+/F/o;
	$info{$i} = $j;
}

sub _init_list()
{
	$list = {};
	$stemlist = OpenBSD::PackageName::compile_stemlist();

	opendir(my $dir, $pkg_db) or die "Bad pkg_db: $!";
	while (my $e = readdir($dir)) {
		next if $e eq '.' or $e eq '..';
		add_installed($e);
	}
	closedir($dir);
}

sub add_installed(@p)
{
	if (!defined $list) {
		_init_list();
	}
	for my $p (@p) {
		$list->{$p} = 1;
		$stemlist->add($p);
	}
}

sub delete_installed(@p)
{
	if (!defined $list) {
		_init_list();
	}
	for my $p (@p) {
		delete $list->{$p};
		$stemlist->delete($p);

	}
}

sub installed_stems()
{
	if (!defined $list) {
		_init_list();
	}
	return $stemlist;
}

sub installed_packages($all = 0)
{
	if (!defined $list) {
		_init_list();
	}
	if ($all) {
		return grep { !/^\./o } keys %$list;
	} else {
		return keys %$list;
	}
}

sub installed_info($name)
{
	# XXX remove the o if we allow pkg_db to change dynamically
	if ($name =~ m|^\Q$pkg_db\E/?|o) {
		return "$name/";
	} else {
		return "$pkg_db/$name/";
	}
}

sub installed_contents($name)
{
	return installed_info($name).CONTENTS;
}

sub borked_package($pkgname)
{
	$pkgname = "partial-$pkgname" unless $pkgname =~ m/^partial\-/;
	unless (-e "$pkg_db/$pkgname") {
		return $pkgname;
	}
	my $i = 1;

	while (-e "$pkg_db/$pkgname.$i") {
		$i++;
	}
	return "$pkgname.$i";
}

sub libs_package($pkgname)
{
	$pkgname =~ s/^\.libs\d*\-//;
	unless (-e "$pkg_db/.libs-$pkgname") {
		return ".libs-$pkgname";
	}
	my $i = 1;

	while (-e "$pkg_db/.libs$i-$pkgname") {
		$i++;
	}
	return ".libs$i-$pkgname";
}

sub is_installed($p)
{
	my $name = installed_name($p);
	if (!defined $list) {
		installed_packages();
	}
	return defined $list->{$name};
}

sub installed_name($p)
{
	require File::Spec;
	my $name = File::Spec->canonpath($p);
	$name =~ s|/$||o;
	# XXX remove the o if we allow pkg_db to change dynamically
	$name =~ s|^\Q$pkg_db\E/?||o;
	$name =~ s|/\+CONTENTS$||o;
	return $name;
}

sub info_names()
{
	return @info;
}

sub is_info_name($name)
{
	return $info{$name};
}

my $dlock;

sub lock_db($shared = 0, $state = undef)
{
	my $mode = $shared ? LOCK_SH : LOCK_EX;
	open($dlock, '<', $pkg_db) or return;
	if (flock($dlock, $mode | LOCK_NB)) {
		return;
	}
	if (!defined $state) {
		require OpenBSD::BaseState;
		$state = 'OpenBSD::BaseState';
	}
	$state->errprint("Package database already locked... awaiting release... ");
	while (!flock($dlock, $mode)) {
	}
	$state->errsay("done!");
	return;
}

sub unlock_db()
{
	if (defined $dlock) {
		flock($dlock, LOCK_UN);
		close($dlock);
	}
}

1;
