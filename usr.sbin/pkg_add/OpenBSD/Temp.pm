# ex:ts=8 sw=4:
# $OpenBSD: Temp.pm,v 1.39 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2003-2005 Marc Espie <espie@openbsd.org>
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

package OpenBSD::Temp;

use OpenBSD::MkTemp;
use OpenBSD::Paths;
use OpenBSD::Error;

our $tempbase = $ENV{'PKG_TMPDIR'} || OpenBSD::Paths->vartmp;

# stuff that should be cleaned up on exit, registered by pid,
# so that it gets cleaned on exit from the correct process

my $dirs = {};
my $files = {};

my ($lastname, $lasterror, $lasttype);

OpenBSD::Handler->atend(
    sub($) {
	while (my ($name, $pid) = each %$files) {
		unlink($name) if $pid == $$;
	}
	while (my ($dir, $pid) = each %$dirs) {
		OpenBSD::Error->rmtree([$dir]) if $pid == $$;
	}
    });


sub dir($)
{
	my $caught;
	my $h = sub($sig, @) { $caught = $sig; };
	my $dir;

	{
	    local $SIG{'INT'} = $h;
	    local $SIG{'QUIT'} = $h;
	    local $SIG{'HUP'} = $h;
	    local $SIG{'KILL'} = $h;
	    local $SIG{'TERM'} = $h;
	    $dir = permanent_dir($tempbase, "pkginfo");
	    if (defined $dir) {
		    $dirs->{$dir} = $$;
	    }
	}
	if (defined $caught) {
		kill $caught, $$;
	}
	if (defined $dir) {
		return "$dir/";
	} else {
		return undef;
	}
}

sub fh_file($stem, $cleanup)
{
	my $caught;
	my $h = sub($sig, @) { $caught = $sig; };
	my ($fh, $file);

	{
	    local $SIG{'INT'} = $h;
	    local $SIG{'QUIT'} = $h;
	    local $SIG{'HUP'} = $h;
	    local $SIG{'KILL'} = $h;
	    local $SIG{'TERM'} = $h;
	    ($fh, $file) = permanent_file($tempbase, $stem);
	    if (defined $file) {
		    &$cleanup($file);
	    }
	}
	if (defined $caught) {
		kill $caught, $$;
	}
	return ($fh, $file);
}

sub file($)
{
	return (fh_file("pkgout", 
	    sub($name) { $files->{$name} = $$; })) [1];
}

sub reclaim($class, $name)
{
	delete $files->{$name};
	delete $dirs->{$name};
}

sub permanent_file($dir, $stem)
{
	my $template = "$stem.XXXXXXXXXX";
	if (defined $dir) {
		$template = "$dir/$template";
	}
	if (my @l = OpenBSD::MkTemp::mkstemp($template)) {
		return @l;
	}
	($lastname, $lasttype, $lasterror) = ($template, 'file', $!);
	return ();
}

sub permanent_dir($dir, $stem)
{
	my $template = "$stem.XXXXXXXXXX";
	if (defined $dir) {
		$template = "$dir/$template";
	}
	if (my $d = OpenBSD::MkTemp::mkdtemp($template)) {
		return $d;
	}
	($lastname, $lasttype, $lasterror) = ($template, 'dir', $!);
	return undef;
}

sub last_error($class, $template = "User #1 couldn't create temp #2 as #3: #4")
{
	my ($user) = getpwuid($>);
	return ($template, $user, $lasttype, $lastname, $lasterror);
}
1;
