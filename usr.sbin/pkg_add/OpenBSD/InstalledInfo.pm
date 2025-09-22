# ex:ts=8 sw=4:
# $OpenBSD: InstalledInfo.pm,v 1.2 2023/06/13 09:07:17 espie Exp $
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

package OpenBSD::InstalledInfo;
require Exporter;
our @ISA=qw(Exporter);
our @EXPORT=qw(CONTENTS DESC REQUIRED_BY REQUIRING DISPLAY UNDISPLAY);

use Fcntl qw/:flock/;
use OpenBSD::PackageName;
use OpenBSD::Paths;

use constant {
	CONTENTS => '+CONTENTS',
	DESC => '+DESC',
	REQUIRED_BY => '+REQUIRED_BY',
	REQUIRING => '+REQUIRING',
	DISPLAY => '+DISPLAY',
	UNDISPLAY => '+UNDISPLAY'
};

sub new($class, $state, $dir = $ENV{"PKG_DBDIR"} || OpenBSD::Paths->pkgdb)
{
	return bless {state => $state, pkgdb => $dir}, $class;
}

sub list($self)
{
	if (!defined $self->{list}) {
		$self->_init;
	}
	return $self->{list};
}

sub stems($self)
{
	if (!defined $self->{stemlist}) {
		$self->_init;
	}
	return $self->{stemlist};
}

sub _init($self)
{
	opendir(my $dir, $self->{pkgdb}) or 
		$self->{state}->fatal("Bad pkg_db #1: #2", $self->{pgkdb}, $!);

	$self->{stemlist} = OpenBSD::PackageName::compile_stemlist();
	while (my $e = readdir($dir)) {
		next if $e eq '.' or $e eq '..';
		$self->add($e);
	}
	closedir($dir);
	return $self;

}

my @info = (CONTENTS, DESC, REQUIRED_BY, REQUIRING, DISPLAY, UNDISPLAY);

my %info = ();
for my $i (@info) {
	my $j = $i;
	$j =~ s/\+/F/o;
	$info{$i} = $j;
}

sub add($self, @p)
{
	for my $p (@p) {
		$self->{list}{$p} = 1;
		$self->{stemlist}->add($p);
	}
	return $self;
}

sub delete($self, @p)
{
	for my $p (@p) {
		delete $self->{list}{$p};
		$self->{stemlist}->delete($p);

	}
	return $self;
}

sub packages($self, $all = 0)
{
	if ($all) {
		return grep { !/^\./o } keys %{$self->list};
	} else {
		return keys %{$self->list};
	}
}

sub fullname($self, $name)
{
	if ($name =~ m|^\Q$self->{pkgdb}\E/?|) {
		return "$name/";
	} else {
		return "$self->{pkgdb}/$name/";
	}
}

sub contents($self, $name)
{
	return $self->info($name).CONTENTS;
}

sub borked_package($self, $pkgname)
{
	$pkgname = "partial-$pkgname" unless $pkgname =~ m/^partial\-/;
	unless (-e "$self->{pkgdb}/$pkgname") {
		return $pkgname;
	}
	my $i = 1;

	while (-e "$self->{pkgdb}/$pkgname.$i") {
		$i++;
	}
	return "$pkgname.$i";
}

sub libs_package($self, $pkgname)
{
	$pkgname =~ s/^\.libs\d*\-//;
	unless (-e "$self->{pkgdb}/.libs-$pkgname") {
		return ".libs-$pkgname";
	}
	my $i = 1;

	while (-e "$self->{pkgdb}/.libs$i-$pkgname") {
		$i++;
	}
	return ".libs$i-$pkgname";
}

sub installed_name($self, $path)
{
	require File::Spec;
	my $name = File::Spec->canonpath($path);
	$name =~ s|/$||o;
	$name =~ s|^\Q$self->{pkgdb}\E/?||;
	$name =~ s|/\+CONTENTS$||o;
	return $name;
}

sub is_installed($self, $path)
{
	my $name = $self->installed_name($path);
	return defined $self->list->{$self->installed_name($path)};
}

sub info_names($class)
{
	return @info;
}

sub is_info_name($class, $name)
{
	return $info{$name};
}

sub lock($self, $shared = 0, $quiet = 0)
{
	return if defined $self->{dlock};
	my $mode = $shared ? LOCK_SH : LOCK_EX;
	open($self->{dlock}, '<', $self->{pkg_db}) or return;
	if (flock($self->{dlock}, $mode | LOCK_NB)) {
		return $self;
	}
	$self->{state}->errprint("Package database already locked... awaiting release... ") unless $quiet;
	flock($self->{dlock}, $mode);
	$self->{state}->errsay("done!") unless $quiet;
	return $self;
}

sub unlock($self)
{
	my $self = shift;
	if (defined $self->{dlock}) {
		flock($self->{dlock}, LOCK_UN);
		close($self->{dlock});
		delete $self->{dlock};
	}
}

1;
