# ex:ts=8 sw=4:
# $OpenBSD: Ustar.pm,v 1.96 2023/06/13 09:07:17 espie Exp $
#
# Copyright (c) 2002-2014 Marc Espie <espie@openbsd.org>
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

# Handle utar archives

use v5.36;

package OpenBSD::Ustar;

use constant {
	FILE => "\0",
	FILE1 => '0',
	HARDLINK => '1',
	SOFTLINK => '2',
	CHARDEVICE => '3',
	BLOCKDEVICE => '4',
	DIR => '5',
	FIFO => '6',
	CONTFILE => '7',
	USTAR_HEADER => 'a100a8a8a8a12a12a8aa100a6a2a32a32a8a8a155a12',
	MAXFILENAME => 100,
	MAXLINKNAME => 100,
	MAXPREFIX => 155,
	MAXUSERNAME => 32,
	MAXGROUPNAME => 32,
	XHDR => 'x',
	# XXX those are NOT supported, just recognized
	GHDR => 'g',
	LONGLINK => 'K',
	LONGNAME => 'L',
};

use File::Basename ();
use OpenBSD::IdCache;
use OpenBSD::Paths;

our $uidcache = OpenBSD::UidCache->new;
our $gidcache = OpenBSD::GidCache->new;
our $unamecache = OpenBSD::UnameCache->new;
our $gnamecache = OpenBSD::GnameCache->new;

# This is a multiple of st_blksize everywhere....
my $buffsize = 2 * 1024 * 1024;

sub new($class, $fh, $state, $destdir = '')
{
	return bless {
	    fh => $fh,
	    swallow => 0,
	    state => $state,
	    key => {},
	    destdir => $destdir} , $class;
}

# $self->set_description($description):
#	application-level description of the archive for error messages
sub set_description($self, $d)
{
	$self->{description} = $d;
}

# $self->set_callback(sub($size_done) {}):
#	for large file extraction, provide intermediate callbacks with the
#	size already done for progress meters and the likes
sub set_callback($self, $code)
{
	$self->{callback} = $code;
}

sub _fatal($self, $msg, @args)
{
	$self->{state}->fatal("Ustar [#1][#2]: #3",
	    $self->{description} // '?', $self->{lastname} // '?',
	    $self->{state}->f($msg, @args));
}

sub _new_object($self, $h, $class)
{
	$h->{archive} = $self;
	$h->{destdir} = $self->{destdir};
	bless $h, $class;
	return $h;
}

sub skip($self)
{
	my $temp;

	while ($self->{swallow} > 0) {
		my $toread = $self->{swallow};
		if ($toread >$buffsize) {
			$toread = $buffsize;
		}
		my $actual = read($self->{fh}, $temp, $toread);
		if (!defined $actual) {
			$self->_fatal("Error while skipping archive: #1", $!);
		}
		if ($actual == 0) {
			$self->_fatal("Premature end of archive in header");
		}
		$self->{swallow} -= $actual;
	}
}

my $types = {
	DIR , 'OpenBSD::Ustar::Dir',
	HARDLINK , 'OpenBSD::Ustar::HardLink',
	SOFTLINK , 'OpenBSD::Ustar::SoftLink',
	FILE , 'OpenBSD::Ustar::File',
	FILE1 , 'OpenBSD::Ustar::File',
	FIFO , 'OpenBSD::Ustar::Fifo',
	CHARDEVICE , 'OpenBSD::Ustar::CharDevice',
	BLOCKDEVICE , 'OpenBSD::Ustar::BlockDevice',
};

my $unsupported = {
	XHDR => 'Extended header',
	GHDR => 'GNU header',
	LONGLINK => 'Long symlink',
	LONGNAME => 'Long file',
};
	
# helpers for the XHDR type
sub _read_records($self, $size)
{
	my $toread = $self->{swallow};
	my $result = '';
	while ($toread > 0) {
		my $buffer;
		my $maxread = $buffsize;
		$maxread = $toread if $maxread > $toread;
		my $actual = read($self->{fh}, $buffer, $maxread);
		if (!defined $actual) {
			$self->_fatal("Error reading from archive: #1", $!);
		}
		if ($actual == 0) {
			$self->_fatal("Premature end of archive");
		}
		$self->{swallow} -= $actual;
		$toread -= $actual;
		$result .= $buffer;
	}
	return substr($result, 0, $size);
}

sub _parse_records($self, $result, $h)
{
	open(my $fh, '<', \$h);
	while (<$fh>) {
		chomp;
		if (m/^(\d+)\s+(\w+?)\=(.*)$/) {
			my ($k, $v) = ($2, $3);
			if ($k eq 'path') {
				$result->{name} = $v;
			} elsif ($k eq 'linkpath') {
				$result->{linkname} = $v;
			}
		}
	}
}

sub next($self)
{
	# get rid of the current object
	$self->skip;
	my $header;
	my $n = read($self->{fh}, $header, 512);
	return if (defined $n) and $n == 0;
	$self->_fatal("Error while reading header")
	    unless defined $n and $n == 512;
	if ($header eq "\0"x512) {
		return $self->next;
	}
	# decode header
	my ($name, $mode, $uid, $gid, $size, $mtime, $chksum, $type,
	    $linkname, $magic, $version, $uname, $gname, $major, $minor,
	    $prefix, $pad) = unpack(USTAR_HEADER, $header);
	if ($magic ne "ustar\0" || $version ne '00') {
		$self->_fatal("Not an ustar archive header");
	}
	# verify checksum
	my $value = $header;
	substr($value, 148, 8) = " "x8;
	my $ck2 = unpack("%C*", $value);
	if ($ck2 != oct($chksum)) {
		$self->_fatal("Bad archive checksum");
	}
	$name =~ s/\0*$//o;
	$mode = oct($mode) & 0xfff;
	$uname =~ s/\0*$//o;
	$gname =~ s/\0*$//o;
	$linkname =~ s/\0*$//o;
	$major = oct($major);
	$minor = oct($minor);
	$uid = oct($uid);
	$gid = oct($gid);
	$uid = $uidcache->lookup($uname, $uid);
	$gid = $gidcache->lookup($gname, $gid);
	{
		no warnings; # XXX perl warns if oct converts >= 2^32 values
		$mtime = oct($mtime);
	}
	unless ($prefix =~ m/^\0/o) {
		$prefix =~ s/\0*$//o;
		$name = "$prefix/$name";
	}

	$self->{lastname} = $name;
	$size = oct($size);
	my $result= {
	    name => $name,
	    mode => $mode,
	    atime => $mtime,
	    mtime => $mtime,
	    linkname=> $linkname,
	    uname => $uname,
	    uid => $uid,
	    gname => $gname,
	    gid => $gid,
	    size => $size,
	    major => $major,
	    minor => $minor,
	};
	# adjust swallow
	$self->{swallow} = $size;
	if ($size % 512) {
		$self->{swallow} += 512 - $size % 512;
	}
	if ($type eq XHDR) {
		my $h = $self->_read_records($size);
		$result = $self->next;
		$self->_parse_records($result, $h);
		return $result;
	}
	if (defined $types->{$type}) {
		$self->_new_object($result, $types->{$type});
	} else {
		$self->_fatal("Unsupported type #1 (#2)", $type,
		    $unsupported->{$type} // "unknown");
	}
	if (!$result->isFile && $result->{size} != 0) {
		$self->_fatal("Bad archive: non null size for #1 (#2)",
		    $types->{$type}, $result->{name});
	}

	$self->{cachename} = $name;
	return $result;
}

# helper for prepare: ustar has strong limitations wrt directory/filename
sub _split_name($name)
{
	my $prefix = '';

	my $l = length $name;
	if ($l > MAXFILENAME && $l <= MAXFILENAME+MAXPREFIX+1) {
		while (length($name) > MAXFILENAME &&
		    $name =~ m/^(.*?\/)(.*)$/o) {
			$prefix .= $1;
			$name = $2;
		}
		$prefix =~ s|/$||;
	}
	return ($prefix, $name);
}

# helper for prepare
sub _extended_record($k, $v)
{
	my $string = " $k=$v\n";
	my $len = length($string);
	if ($len < 995) {
		return sprintf("%3d", $len+3).$string;
	} elsif ($len < 9995) {
		return sprintf("%04d", $len+4).$string;
	} else {
		return sprintf("%05d", $len+5).$string;
	}
}

sub _pack_header($archive, $type, $size, $entry, $prefix, $name, $linkname, 
    $uname, $gname, $major, $minor)
{

	my $header;
	my $cksum = ' 'x8;
	for (1 .. 2) {
		$header = pack(USTAR_HEADER,
		    $name,
		    sprintf("%07o", $entry->{mode}),
		    sprintf("%07o", $entry->{uid} // 0),
		    sprintf("%07o", $entry->{gid} // 0),
		    sprintf("%011o", $size),
		    sprintf("%011o", $entry->{mtime} // 0),
		    $cksum,
		    $type,
		    $linkname,
		    'ustar', '00',
		    $uname,
		    $gname,
		    sprintf("%07o", $major),
		    sprintf("%07o", $minor),
		    $prefix, "\0");
		$cksum = sprintf("%07o", unpack("%C*", $header));
	}
	return $header;
}

my $whatever = "usualSuspect000";

sub _mkheader($archive, $entry, $type)
{
	my ($prefix, $name) = _split_name($entry->name);
	my ($extendedname, $extendedlink);
	my $linkname = $entry->{linkname};
	my $size = $entry->{size};
	my ($major, $minor);
	if ($entry->isDevice) {
		$major = $entry->{major};
		$minor = $entry->{minor};
	} else {
		$major = 0;
		$minor = 0;
	}
	my ($uname, $gname);
	if (defined $entry->{uname}) {
		$uname = $entry->{uname};
	} else {
		$uname = $entry->{uid};
	}
	if (defined $entry->{gname}) {
		$gname = $entry->{gname};
	} else {
		$gname = $entry->{gid};
	}

	if (defined $entry->{cwd}) {
		my $cwd = $entry->{cwd};
		$cwd.='/' unless $cwd =~ m/\/$/o;
		$linkname =~ s/^\Q$cwd\E//;
	}
	if (!defined $linkname) {
		$linkname = '';
	}
	if (length $prefix > MAXPREFIX) {
		$prefix = substr($prefix, 0, MAXPREFIX);
		$extendedname = 1;
	}
	if (length $name > MAXFILENAME) {
		$name = substr($name, 0, MAXPREFIX);
		$extendedname = 1;
	}
	if (length $linkname > MAXLINKNAME) {
		$linkname = substr($linkname, 0, MAXLINKNAME);
		$extendedlink = 1;
	}
	if (length $uname > MAXUSERNAME) {
		$archive->_fatal("Username too long #1", $uname);
	}
	if (length $gname > MAXGROUPNAME) {
		$archive->_fatal("Groupname too long #1", $gname);
	}
	my $header = $archive->_pack_header($type, $size, $entry, 
	    $prefix, $name, $linkname, $uname, $gname, $major, $minor);
	my $x;
	if ($extendedname) {
		$x .= _extended_record("path", $entry->name);
	}
	if ($extendedlink) {
		$x .= _extended_record("linkpath",$entry->{linkname});
	}
	if ($x) {
		my $extended = $archive->_pack_header(XHDR, length($x), $entry,
		    '', $whatever, '', $uname, $gname, $major, $minor);
		$whatever++;
		if ((length $x) % 512) {
			$x .= "\0" x (512 - ((length $x) % 512));
		}
		return $extended.$x.$header;
	}
	return $header;
}

sub prepare($self, $filename, $destdir = $self->{destdir})
{
	my $realname = "$destdir/$filename";

	my ($dev, $ino, $mode, $uid, $gid, $rdev, $size, $mtime) =
	    (lstat $realname)[0,1,2, 4,5,6,7, 9];

	my $entry = {
		key => "$dev/$ino",
		name => $filename,
		realname => $realname,
		mode => $mode,
		uid => $uid,
		gid => $gid,
		size => $size,
		mtime => $mtime,
		uname => $unamecache->lookup($uid),
		gname => $gnamecache->lookup($gid),
		major => $rdev/256,
		minor => $rdev%256,
	};
	my $k = $entry->{key};
	my $class = "OpenBSD::Ustar::File"; # default
	if (defined $self->{key}{$k}) {
		$entry->{linkname} = $self->{key}{$k};
		$class = "OpenBSD::Ustar::HardLink";
	} elsif (-l $realname) {
		$entry->{linkname} = readlink($realname);
		$class = "OpenBSD::Ustar::SoftLink";
	} elsif (-p _) {
		$class = "OpenBSD::Ustar::Fifo";
	} elsif (-c _) {
		$class = "OpenBSD::Ustar::CharDevice";
	} elsif (-b _) {
		$class ="OpenBSD::Ustar::BlockDevice";
	} elsif (-d _) {
		$class = "OpenBSD::Ustar::Dir";
	}
	$self->_new_object($entry, $class);
	if (!$entry->isFile) {
		$entry->{size} = 0;
	}
	return $entry;
}

sub _pad($self)
{
	my $fh = $self->{fh};
	print $fh "\0"x1024 or 
	    $self->_fatal("Error writing to archive: #1", $!);
}

sub close($self)
{
	if (defined $self->{padout}) {
		$self->_pad;
	}
	close($self->{fh});
}

sub destdir($self)
{
	return $self->{destdir};
}

sub set_destdir($self, $d)
{
	$self->{destdir} = $d;
}

sub fh($self)
{
	return $self->{fh};
}

package OpenBSD::Ustar::Object;

sub recheck_owner($entry)
{
	# XXX weird format to prevent cvs from expanding OpenBSD id
	$entry->{uid} //= $OpenBSD::Ustar::uidcache
	    ->lookup($entry->{uname});
	$entry->{gid} //= $OpenBSD::Ustar::gidcache
	    ->lookup($entry->{gname});
}

sub _fatal($self, @args)
{
	$self->{archive}->_fatal(@args);
}

sub _left_todo($self, $toread)
{
	return if $toread == 0;
	return unless defined $self->{archive}{callback};
	&{$self->{archive}{callback}}($self->{size} - $toread);
}

sub name($self)
{
	return $self->{name};
}

sub fullname($self)
{
	return $self->{destdir}.$self->{name};
}

sub set_name($self, $v)
{
	$self->{name} = $v;
}

sub _set_modes_on_object($self, $o)
{
	chown $self->{uid}, $self->{gid}, $o;
	chmod $self->{mode}, $o;
	if (defined $self->{mtime} || defined $self->{atime}) {
		utime $self->{atime} // time, $self->{mtime} // time, $o;
	}
}

sub _set_modes($self)
{
	$self->_set_modes_on_object($self->fullname);
}

sub _ensure_dir($self, $dir)
{
	return if -d $dir;
	$self->_ensure_dir(File::Basename::dirname($dir));
	if (mkdir($dir)) {
		return;
	}
	$self->_fatal("Error making directory #1: #2", $dir, $!);
}

sub _make_basedir($self)
{
	my $dir = $self->{destdir}.File::Basename::dirname($self->name);
	$self->_ensure_dir($dir);
}

sub write($self)
{
	my $arc = $self->{archive};
	my $out = $arc->{fh};

	$arc->{padout} = 1;
	my $header = $arc->_mkheader($self, $self->type);
	print $out $header or 
	    $self->_fatal("Error writing to archive: #1", $!);
	$self->write_contents($arc);
	my $k = $self->{key};
	if (!defined $arc->{key}{$k}) {
		$arc->{key}{$k} = $self->name;
	}
}

sub alias($self, $arc, $alias)
{
	my $k = $self->{archive}.":".$self->{archive}{cachename};
	if (!defined $arc->{key}{$k}) {
		$arc->{key}{$k} = $alias;
	}
}

# $self->write_contents($arc)
sub write_contents($, $)
{
	# only files have anything to write
}

# $self->resolve_links($arc)
sub _resolve_links($, $)
{
	# only hard links must cheat
}

# $self->copy_contents($arc)
sub copy_contents($, $)
{
	# only files need copying
}

sub copy($self, $wrarc)
{
	my $out = $wrarc->{fh};
	$self->_resolve_links($wrarc);
	$wrarc->{padout} = 1;
	my $header = $wrarc->_mkheader($self, $self->type);
	print $out $header or 
	    $self->_fatal("Error writing to archive: #1", $!);

	$self->copy_contents($wrarc);
}

sub isDir($) { 0 }
sub isFile($) { 0 }
sub isDevice($) { 0 }
sub isFifo($) { 0 }
sub isLink($) { 0 }
sub isSymLink($) { 0 }
sub isHardLink($) { 0 }

package OpenBSD::Ustar::Dir;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_ensure_dir($self->fullname);
	$self->_set_modes;
}

sub isDir($) { 1 }

sub type($) { OpenBSD::Ustar::DIR }

package OpenBSD::Ustar::HardLink;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_make_basedir;
	my $linkname = $self->{linkname};
	if (defined $self->{cwd}) {
		$linkname=$self->{cwd}.'/'.$linkname;
	}
	link $self->{destdir}.$linkname, $self->fullname or
	    $self->_fatal("Can't link #1#2 to #1#3: #4",
	    	$self->{destdir}, $linkname, $self->name, $!);
}

sub _resolve_links($self, $arc)
{
	my $k = $self->{archive}.":".$self->{linkname};
	if (defined $arc->{key}{$k}) {
		$self->{linkname} = $arc->{key}{$k};
	} else {
		print join("\n", keys(%{$arc->{key}})), "\n";
		$self->_fatal("Can't copy link over: original for #1 NOT available", $k);
	}
}

sub isLink($) { 1 }
sub isHardLink($) { 1 }

sub type($) { OpenBSD::Ustar::HARDLINK }

package OpenBSD::Ustar::SoftLink;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_make_basedir;
	symlink $self->{linkname}, $self->fullname or
	    $self->_fatal("Can't symlink #1 to #2: #3",
	    	$self->{linkname}, $self->fullname, $!);
	require POSIX;
	POSIX::lchown($self->{uid}, $self->{gid}, $self->fullname);
}

sub isLink($) { 1 }
sub isSymLink($) { 1 }

sub type($) { OpenBSD::Ustar::SOFTLINK }

package OpenBSD::Ustar::Fifo;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_make_basedir;
	require POSIX;
	POSIX::mkfifo($self->fullname, $self->{mode}) or
	    $self->_fatal("Can't create fifo #1: #2", $self->fullname, $!);
	$self->_set_modes;
}

sub isFifo($) { 1 }
sub type($) { OpenBSD::Ustar::FIFO }

package OpenBSD::UStar::Device;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_make_basedir;
	$self->{archive}{state}->system(OpenBSD::Paths->mknod,
	    '-m', $self->{mode}, '--', $self->fullname,
	    $self->devicetype, $self->{major}, $self->{minor});
	$self->_set_modes;
}

sub isDevice($) { 1 }

package OpenBSD::Ustar::BlockDevice;
our @ISA=qw(OpenBSD::Ustar::Device);

sub type($) { OpenBSD::Ustar::BLOCKDEVICE }
sub devicetype($) { 'b' }

package OpenBSD::Ustar::CharDevice;
our @ISA=qw(OpenBSD::Ustar::Device);

sub type($) { OpenBSD::Ustar::BLOCKDEVICE }
sub devicetype($) { 'c' }


# This is very specific to classic Unix: files with series of 0s should
# have "gaps" created by using lseek while writing.
package OpenBSD::CompactWriter;

use constant {
	FH => 0,
	BS => 1,
	ZEROES => 2,
	UNFINISHED => 3,
};

sub new($class, $out)
{
	my $bs = (stat $out)[11];
	my $zeroes;
	if (defined $bs) {
		$zeroes = "\x00"x$bs;
	}
	bless [ $out, $bs, $zeroes, 0 ], $class;
}

sub write($self, $buffer)
{
	my ($fh, $bs, $zeroes, $e) = @$self;
START:
	if (defined $bs) {
		for (my $i = 0; $i + $bs <= length($buffer); $i+= $bs) {
			if (substr($buffer, $i, $bs) eq $zeroes) {
				my $r = syswrite($fh, $buffer, $i);
				unless (defined $r && $r == $i) {
					return 0;
				}
				$i+=$bs;
				my $seek_forward = $bs;
				while (substr($buffer, $i, $bs) eq $zeroes) {
					$i += $bs;
					$seek_forward += $bs;
				}
				defined(sysseek($fh, $seek_forward, 1))
				    or return 0;
				$buffer = substr($buffer, $i);
				if (length $buffer == 0) {
					$self->[UNFINISHED] = 1;
					return 1;
				}
				goto START;
			}
		}
	}
	$self->[UNFINISHED] = 0;
	my $r = syswrite($fh, $buffer);
	if (defined $r && $r == length $buffer) {
		return 1;
	} else {
		return 0;
	}
}

sub close($self)
{
	if ($self->[UNFINISHED]) {
		defined(sysseek($self->[FH], -1, 1)) or return 0;
		defined(syswrite($self->[FH], "\0")) or return 0;
	}
	return 1;
}

package OpenBSD::Ustar::File;
our @ISA=qw(OpenBSD::Ustar::Object);

sub create($self)
{
	$self->_make_basedir;
	open(my $fh, '>', $self->fullname) or
	    $self->_fatal("Can't write to #1: #2", $self->fullname, $!);
	$self->extract_to_fh($fh);
}

sub extract_to_fh($self, $fh)
{
	my $buffer;
	my $out = OpenBSD::CompactWriter->new($fh);
	my $toread = $self->{size};
	if ($self->{partial}) {
		$toread -= length($self->{partial});
		unless ($out->write($self->{partial})) {
			$self->_fatal("Error writing to #1: #2",
			    $self->fullname, $!);
		}
	}
	while ($toread > 0) {
		my $maxread = $buffsize;
		$maxread = $toread if $maxread > $toread;
		my $actual = read($self->{archive}{fh}, $buffer, $maxread);
		if (!defined $actual) {
			$self->_fatal("Error reading from archive: #1", $!);
		}
		if ($actual == 0) {
			$self->_fatal("Premature end of archive");
		}
		$self->{archive}{swallow} -= $actual;
		unless ($out->write($buffer)) {
			$self->_fatal("Error writing to #1: #2",
			    $self->fullname, $!);
		}

		$toread -= $actual;
		$self->_left_todo($toread);
	}
	$self->_set_modes_on_object($fh);
	$out->close or $self->_fatal("Error closing #1: #2",
	    $self->fullname, $!);
}

sub contents($self)
{
	my $toread = $self->{size};
	my $buffer;
	my $offset = 0;
	if ($self->{partial}) {
		$buffer = $self->{partial};
		$offset = length($self->{partial});
		$toread -= $offset;
	}

	while ($toread != 0) {
		my $sz = $toread;
		my $actual = read($self->{archive}{fh}, $buffer, $sz, $offset);
		if (!defined $actual) {
			$self->_fatal("Error reading from archive: #1", $!);
		}
		if ($actual != $sz) {
			$self->_fatal("Error: short read from archive");
		}
		$self->{archive}{swallow} -= $actual;
		$toread -= $actual;
		$offset += $actual;
	}

	$self->{partial} = $buffer;
	return $buffer;
}

sub write_contents($self, $arc)
{
	my $filename = $self->{realname};
	my $size = $self->{size};
	my $out = $arc->{fh};
	open my $fh, "<", $filename or 
	    $self->_fatal("Can't read file #1: #2", $filename, $!);

	my $buffer;
	my $toread = $size;
	while ($toread > 0) {
		my $maxread = $buffsize;
		$maxread = $toread if $maxread > $toread;
		my $actual = read($fh, $buffer, $maxread);
		if (!defined $actual) {
			$self->_fatal("Error reading from file: #1", $!);
		}
		if ($actual == 0) {
			$self->_fatal("Premature end of file");
		}
		unless (print $out $buffer) {
			$self->_fatal("Error writing to archive: #1", $!);
		}

		$toread -= $actual;
		$self->_left_todo($toread);
	}
	# explicitly pad archive to 512 bytes blocksize
	if ($size % 512) {
		print $out "\0" x (512 - $size % 512) or
		    $self->_fatal("Error writing to archive: #1", $!);
	}
}

sub copy_contents($self, $arc)
{
	my $out = $arc->{fh};
	my $buffer;
	my $size = $self->{size};
	my $toread = $size;
	while ($toread > 0) {
		my $maxread = $buffsize;
		$maxread = $toread if $maxread > $toread;
		my $actual = read($self->{archive}{fh}, $buffer, $maxread);
		if (!defined $actual) {
			$self->_fatal("Error reading from archive: #1", $!);
		}
		if ($actual == 0) {
			$self->_fatal("Premature end of archive");
		}
		$self->{archive}{swallow} -= $actual;
		print $out $buffer or
			$self->_fatal("Error writing to archive #1", $!);

		$toread -= $actual;
	}
	# explicitly pad archive to 512 bytes blocksize
	if ($size % 512) {
		print $out "\0" x (512 - $size % 512) or
		    $self->_fatal("Error writing to archive: #1", $!);
	}
	$self->alias($arc, $self->name);
}

sub isFile($) { 1 }

sub type($) { OpenBSD::Ustar::FILE1 }

1;
