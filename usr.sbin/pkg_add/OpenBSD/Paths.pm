# ex:ts=8 sw=4:
# $OpenBSD: Paths.pm,v 1.40 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2007-2014 Marc Espie <espie@openbsd.org>
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

package OpenBSD::Paths;

# Commands
sub ldconfig($) { '/sbin/ldconfig' }
sub chroot($) { '/usr/sbin/chroot' }
sub mkfontscale($) { '/usr/X11R6/bin/mkfontscale' }
sub mkfontdir($) { '/usr/X11R6/bin/mkfontdir' }
sub fc_cache($) { '/usr/X11R6/bin/fc-cache' }
sub install_info($) { '/usr/bin/install-info' }
sub useradd($) { '/usr/sbin/useradd' }
sub groupadd($) { '/usr/sbin/groupadd' }
sub sysctl($) { '/sbin/sysctl' }
sub openssl($) { '/usr/bin/openssl' }
sub pkgca($) { '/etc/ssl/pkgca.pem' }
sub signify($) { '/usr/bin/signify' }
sub signifykey($,$k) { "/etc/signify/$k.pub" }
sub pkg_add($) { '/usr/sbin/pkg_add' }
sub chmod($) { '/bin/chmod' }	# external command is used for symbolic modes.
sub gzip($) { '/usr/bin/gzip' }
sub ftp($) { $ENV{'FETCH_CMD'} || '/usr/bin/ftp' }
sub groff($) { '/usr/local/bin/groff' }
sub sh($) { '/bin/sh' }
sub arch($) { '/usr/bin/arch' }
sub uname($) { '/usr/bin/uname' }
sub userdel($) { '/usr/sbin/userdel' }
sub groupdel($) { '/usr/sbin/groupdel' }
sub makewhatis($) { '/usr/sbin/makewhatis' }
sub mknod($) { '/sbin/mknod' }
sub mount($) { '/sbin/mount' }
sub df($) { '/bin/df' }
sub ssh($) { '/usr/bin/ssh' }
sub make($) { '/usr/bin/make' }
sub mklocatedb($) { '/usr/libexec/locate.mklocatedb' }
sub locate($) { '/usr/bin/locate' }
sub hostname($) { '/bin/hostname' }
sub doas($) { '/usr/bin/doas' }
sub env($) { '/usr/bin/env' }
sub du($) { '/usr/bin/du' }
sub diff($) { '/usr/bin/diff' }
sub sha256($) { '/bin/sha256' }

# Various paths
sub shells($) { '/etc/shells' }
sub pkgdb($) { '/var/db/pkg' }
sub localbase($) { '/usr/local' }
sub vartmp($) { '/tmp' }
sub portsdir($) { '/usr/ports' }

sub library_dirs($) { ("/usr", "/usr/X11R6") }
sub master_keys($) { ("/etc/master_key") }
sub installurl($) { "/etc/installurl" }
sub srclocatedb($) { "/usr/lib/locate/src.db" }
sub xlocatedb($) { "/usr/X11R6/lib/locate/xorg.db" }
sub updateinfodb($) { '/usr/local/share/update.db' }

sub font_cruft($) { ("fonts.alias", "fonts.dir", "fonts.cache-1", "fonts.scale") }
sub man_cruft($) { ("whatis.db", "mandoc.db", "mandoc.index") }
sub info_cruft($) { ("dir") }

# a bit of code, OS-dependent stuff that's run-time detected and has no
# home yet.

my ($machine_arch, $arch, $osversion, $osdirectory);

sub architecture($self)
{
	if (!defined $arch) {
		my $cmd = $self->uname." -m";
		chomp($arch = `$cmd`);
	}
	return $arch;
}

sub machine_architecture($self)
{
	if (!defined $machine_arch) {
		my $cmd = $self->arch." -s";
		chomp($machine_arch = `$cmd`);
	}
	return $machine_arch;
}

sub compute_osversion($self)
{
	open my $cmd, '-|', $self->sysctl, '-n', 'kern.version';
	my $line = <$cmd>;
	close($cmd);
	if ($line =~ m/^OpenBSD (\d\.\d)(\S*)\s/) {
		$osversion = $1;
		if ($2 eq '-current' or $2 eq '-beta') {
			$osdirectory = 'snapshots';
		} else {
			$osdirectory = $osversion;
		}
	}
}

sub os_version($self)
{
	if (!defined $osversion) {
		$self->compute_osversion;
	}
	return $osversion;
}

sub os_directory($self)
{
	if (!defined $osversion) {
		$self->compute_osversion;
	}
	return $osdirectory;
}

1;
