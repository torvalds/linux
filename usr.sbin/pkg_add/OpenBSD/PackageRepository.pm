# ex:ts=8 sw=4:
# $OpenBSD: PackageRepository.pm,v 1.178 2025/06/01 00:45:39 bentley Exp $
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

# XXX load extra class, grab match from Base class, and tweak inheritance
# to get all methods.

use OpenBSD::PackageRepository::Installed;
$OpenBSD::PackageRepository::Installed::ISA = qw(OpenBSD::PackageRepository);

package OpenBSD::PackageRepository;
our @ISA=(qw(OpenBSD::PackageRepositoryBase));

use OpenBSD::PackageLocation;
use OpenBSD::Paths;
use OpenBSD::Error;
use OpenBSD::Temp;

sub make_error_file($self, $object)
{
	$object->{errors} = OpenBSD::Temp->file;
	if (!defined $object->{errors}) {
		$self->{state}->fatal(OpenBSD::Temp->last_error);
	}
}

sub baseurl($self)
{
	return $self->{path};
}

sub new($class, $baseurl, $state)
{
	if (!defined $state) {
		require Carp;
		Carp::croak "fatal: old api call to $class: needs state";
	}
	my $o = $class->parse(\$baseurl, $state);
	if ($baseurl ne '') {
		return undef;
	}
	return $o;
}

sub can_be_empty($self)
{
	$self->{empty_okay} = 1;
	return $self;
}

my $cache = {};

sub unique($class, $o)
{
	return $o unless defined $o;
	if (defined $cache->{$o->url}) {
		return $cache->{$o->url};
	}
	$cache->{$o->url} = $o;
	return $o;
}

OpenBSD::Handler->atend(
    sub($) {
	for my $repo (values %$cache) {
		$repo->cleanup;
	}
    });

sub parse_fullurl($class, $r, $state)
{
	$class->strip_urlscheme($r) or return undef;
	return $class->unique($class->parse_url($r, $state));
}

sub dont_cleanup($)
{
}

sub ftp($) { 'OpenBSD::PackageRepository::FTP' }
sub http($) { 'OpenBSD::PackageRepository::HTTP' }
sub https($) { 'OpenBSD::PackageRepository::HTTPS' }
sub scp($) { 'OpenBSD::PackageRepository::SCP' }
sub file($) { 'OpenBSD::PackageRepository::Local' }
sub installed($) { 'OpenBSD::PackageRepository::Installed' }

sub parse($class, $r, $state)
{
	{
	no warnings qw(uninitialized);	# in case installpath is empty
	$$r =~ s/^installpath(\:|$)/$state->installpath.$1/e;
	}

	my $u = $$r;
	return undef if $u eq '';

		

	if ($u =~ m/^ftp\:/io) {
		return $class->ftp->parse_fullurl($r, $state);
	} elsif ($u =~ m/^http\:/io) {
#		require OpenBSD::PackageRepository::HTTP;

		return $class->http->parse_fullurl($r, $state);
	} elsif ($u =~ m/^https\:/io) {
		return $class->https->parse_fullurl($r, $state);
	} elsif ($u =~ m/^scp\:/io) {
		return undef if $state->defines("NO_SCP");

		require OpenBSD::PackageRepository::SCP;

		return $class->scp->parse_fullurl($r, $state);
	} elsif ($u =~ m/^file\:/io) {
		return $class->file->parse_fullurl($r, $state);
	} elsif ($u =~ m/^inst\:$/io) {
		return $class->installed->parse_fullurl($r, $state);
	} else {
		if ($$r =~ m/^([a-z0-9][a-z0-9.]+\.[a-z0-9.]+)(\:|$)/ 
		    && !-d $1) {
			$$r =~ s//http:\/\/$1\/%m$2/;
			return $class->http->parse_fullurl($r, $state);
		}
		return $class->file->parse_fullurl($r, $state);
	}
}

sub available($self)
{
	return @{$self->list};
}

sub stemlist($self)
{
	if (!defined $self->{stemlist}) {
		require OpenBSD::PackageName;
		my @l = $self->available;
		if (@l == 0 && !$self->{empty_okay}) {
			$self->{state}->errsay("#1: #2", $self->url,
				$self->{no_such_dir} ? "no such dir" : "empty");
		}
		$self->{stemlist} = OpenBSD::PackageName::avail2stems(@l);
	}
	return $self->{stemlist};
}

sub wipe_info($self, $pkg)
{
	require File::Path;

	my $dir = $pkg->{dir};
	if (defined $dir) {
		OpenBSD::Error->rmtree($dir);
		OpenBSD::Temp->reclaim($dir);
		delete $pkg->{dir};
	}
}

# by default, all objects may exist
# $repo->may_exist($name)
sub may_exist($, $)
{
	return 1;
}

# by default, we don't track opened files for this key

sub opened($)
{
	undef;
}

# hint: 0 premature close, 1 real error. undef, normal !

sub close($self, $object, $hint = undef)
{
	close($object->{fh}) if defined $object->{fh};
	if (defined $object->{pid2}) {
		local $SIG{ALRM} = sub {
			kill HUP => $object->{pid2};
		};
		alarm(30);
		waitpid($object->{pid2}, 0);
		alarm(0);
	}
	$self->parse_problems($object->{errors}, $hint, $object)
	    if defined $object->{errors};
	undef $object->{errors};
	$object->deref;
}

sub make_room($self)
{
	# kill old files if too many
	my $already = $self->opened;
	if (defined $already) {
		# gc old objects
		if (@$already >= $self->maxcount) {
			@$already = grep { defined $_->{fh} } @$already;
		}
		while (@$already >= $self->maxcount) {
			my $o = shift @$already;
			$self->close_now($o);
		}
	}
	return $already;
}

# open method that tracks opened files per-host.
sub open($self, $object)
{
	return unless $self->may_exist($object->{name});

	# kill old files if too many
	my $already = $self->make_room;
	local $SIG{'PIPE'} = 'DEFAULT';
	my $fh = $self->open_pipe($object);
	if (!defined $fh) {
		return;
	}
	$object->{fh} = $fh;
	if (defined $already) {
		push @$already, $object;
	}
	return $fh;
}

sub find($repository, $name)
{
	my $self = $repository->new_location($name);

	if ($self->contents) {
		return $self;
	}
	return undef;
}

sub grabPlist($repository, $name, @code)
{
	my $self = $repository->new_location($name);

	return $self->grabPlist(@code);
}

sub parse_problems($self, $filename, $hint = 0, $object = undef)
{
	CORE::open(my $fh, '<', $filename) or return;
	my $baseurl = $self->url;
	my $objecturl = $baseurl;
	if (defined $object) {
		$objecturl = $object->url;
		$object->{error_reported} = 1;
	}
	my $notyet = 1;
	my $broken = 0;
	my $signify_error = 0;
	$self->{last_error} = 0;
	$self->{count}++;
	while(<$fh>) {
		if (m/^Redirected to (https?)\:\/\/([^\/]*)/) {
			my ($scheme, $newhost) = ($1, $2);
			$self->{state}->print("#1", $_);
			next if $scheme ne $self->urlscheme;
			# XXX try logging but syslog doesn't exist for Info
			eval { 
			    $self->{state}->syslog("Redirected from #1 to #2",
				$self->{host}, $newhost);
			};
			$self->{host} = $newhost;
			$self->setup_session;
			$baseurl = $self->url;
			next;
		}
		next if m/^(?:200|220|221|226|229|230|227|250|331|500|150)[\s\-]/o;
		next if m/^EPSV command not understood/o;
		next if m/^Trying [\da-f\.\:]+\.\.\./o;
		# XXX make_room may call close_now on objects of the right
		# type, but from a different repository
		next if m/^Requesting (?:\Q$baseurl\E|\Q$objecturl\E)/;
		next if m/^Remote system type is\s+/o;
		next if m/^Connected to\s+/o;
		next if m/^remote\:\s+/o;
		next if m/^Using binary mode to transfer files/o;
		next if m/^Retrieving\s+/o;
		next if m/^Success?fully retrieved file/o;
		next if m/^\d+\s+bytes\s+received\s+in/o;
		next if m/^ftp: connect to address.*: No route to host/o;
		if (m/^ftp: Writing -: Broken pipe/o) {
			$broken = 1;
			next;
		}
		if (m/^tls session resumed\: (\w+)/) {
			next; # disable the detailed handling for now
			my $s = $1;
			if ($s eq 'yes') {
				# everything okay for now
				$self->{said_slow} = 0;
				next;
			}
			next if $self->{count} < 2 || $self->{said_slow};
			$self->{said_slow} = 1;
			$self->{state}->say("#1: no session resumption supported by ftp(1) on connection ##2", $self->{host}, $self->{count});
			$self->{state}->say("#1: https will be slow", $self->{host});
			next;
		}
		# http error
		if (m/^ftp: Error retrieving .*: 404/o) {
			$self->{lasterror} = 404;
			if (!defined $object) {
				$self->{no_such_dir} = 1;
				next;
			}
			# ignore errors for stable packages
			next if $self->can_be_empty;
		}

		if (defined $hint && $hint == 0) {
			next if m/^ftp: -: short write/o;
			next if m/^ftp: local: -: Broken pipe/o;
			next if m/^421\s+/o;
		}
		# not retrieving the file => always the same message
		# so it's superfluous
		next if m/^signify:/ && $self->{lasterror};
		if ($notyet) {
			$self->{state}->errprint("#1: ", $objecturl);
			$notyet = 0;
		}
		if (m/^signify:/) {
			$signify_error = 1;
			s/.*unsigned .*archive.*/unsigned package/;
		}
		if (m/^421\s+/o ||
		    m/^ftp: connect: Connection timed out/o ||
		    m/^ftp: Can't connect or login to host/o) {
			$self->{lasterror} = 421;
		}
		if (m/^550\s+/o) {
			$self->{lasterror} = 550;
		}
		$self->{state}->errprint("#1", $_);
	}
	if ($broken) {
		unless ($signify_error || defined $hint && $hint == 0) { 
			$self->{state}->errprint('#1', "ftp: Broken pipe");
		}
	}
	CORE::close($fh);
	OpenBSD::Temp->reclaim($filename);
	unlink $filename;
}

sub cleanup($)
{
	# nothing to do
}

sub relative_url($self, $name = undef)
{
	if (defined $name) {
		return $self->baseurl.$name.".tgz";
	} else {
		return $self->baseurl;
	}
}

sub add_to_list($self, $list, $filename)
{
	if ($filename =~ m/^(.*\-\d.*)\.tgz$/o) {
		push(@$list, $1);
	}
}

sub did_it_fork($self, $pid)
{
	if (!defined $pid) {
		$self->{state}->fatal("Cannot fork: #1", $!);
	}
	if ($pid == 0) {
		delete $SIG{'WINCH'};
		delete $SIG{'CONT'};
		delete $SIG{'INFO'};
	}
}

sub uncompress($self, $object, @p)
{
	require IO::Uncompress::Gunzip;
	my $fh = IO::Uncompress::Gunzip->new(@p, MultiStream => 1);
	my $result = "";
	if ($object->{is_signed}) {
		my $h = $fh->getHeaderInfo;
		if ($h) {
			for my $line (split /\n/, $h->{Comment}) {
				if ($line =~ m/^key=.*\/(.*)\.sec$/) {
					$object->{signer} = $1;
				} elsif ($line =~ m/^date=(.*)$/) {
					$object->{signdate} = $1;
				}
			}
		} else {
			$fh->close;
			return undef;
		}
	}
	return $fh;
}

sub keytype($self)
{
	if ($self->{state}->defines("FW_UPDATE")) {
		return "fw";
	} else {
		return "pkg";
	}
}

sub signify_pipe($self, $object, @p)
{
	CORE::open STDERR, ">>", $object->{errors};
	exec {OpenBSD::Paths->signify}
	    ("signify",
	    "-zV",
	    "-t", $self->keytype,
	    @p)
	or $self->{state}->fatal("Can't run #1: #2",
	    OpenBSD::Paths->signify, $!);
}

sub check_signed($self, $object)
{
	if ($object->{repository}{trusted}) {
		return 0;
	}
	if ($self->{state}{signature_style} eq 'new') {
		$object->{is_signed} = 1;
		return 1;
	} else {
		return 0;
	}
}

package OpenBSD::PackageRepository::Local;
our @ISA=qw(OpenBSD::PackageRepository);
use OpenBSD::Error;

sub is_local_file($)
{
	return 1;
}

sub urlscheme($)
{
	return 'file';
}

my $pkg_db;

sub pkg_db($)
{
	if (!defined $pkg_db) {
		use OpenBSD::Paths;
		$pkg_db = $ENV{"PKG_DBDIR"} || OpenBSD::Paths->pkgdb;
	}
	return $pkg_db;
}

sub parse_fullurl($class, $r, $state)
{
	my $ok = $class->strip_urlscheme($r);
	my $o = $class->parse_url($r, $state);
	if (!$ok && $o->{path} eq $class->pkg_db."/") {
		return $class->installed->new(0, $state);
	} else {
		if ($o->{path} eq './') {
			$o->can_be_empty;
		}
		return $class->unique($o);
	}
}

# wrapper around copy, that sometimes does not copy
sub may_copy($self, $object, $destdir)
{
	my $src = $self->relative_url($object->{name});
	require File::Spec;
	my (undef, undef, $base) = File::Spec->splitpath($src);
	my $dest = File::Spec->catfile($destdir, $base);
	if (File::Spec->canonpath($dest) eq File::Spec->canonpath($src)) {
	    	return;
	}
	if (-f $dest) {
		my ($ddev, $dino) = (stat $dest)[0,1];
		my ($sdev, $sino) = (stat $src)[0, 1];
		if ($ddev == $sdev and $sino == $dino) {
			return;
		}
	}
	$self->{state}->copy_file($src, $destdir);
}

sub open_pipe($self, $object)
{
	if (defined $self->{state}->cache_directory) {
		$self->may_copy($object, $self->{state}->cache_directory);
	}
	my $name = $self->relative_url($object->{name});
	if ($self->check_signed($object)) {
		$self->make_error_file($object);
		my $pid = open(my $fh, "-|");
		$self->did_it_fork($pid);
		if ($pid) {
			$object->{pid} = $pid;
			return $self->uncompress($object, $fh);
		} else {
			$self->signify_pipe($object, "-x", $name);
		}
	} else {
		return $self->uncompress($object, $name);
	}
}

sub may_exist($self, $name)
{
	return -r $self->relative_url($name);
}

my $local = [];

sub opened($)
{
	return $local;
}

sub maxcount($)
{
	return 3;
}

sub list($self)
{
	my $l = [];
	my $dname = $self->baseurl;
	opendir(my $dir, $dname) or return $l;
	while (my $e = readdir $dir) {
		next unless -f "$dname/$e";
		$self->add_to_list($l, $e);
	}
	close($dir);
	return $l;
}

package OpenBSD::PackageRepository::Distant;
our @ISA=qw(OpenBSD::PackageRepository);

sub baseurl($self)
{
	return "//$self->{host}$self->{path}";
}

sub setup_session($)
{
	# nothing to do except for https
}

sub parse_url($class, $r, $state)
{
	# same heuristics as ftp(1):
	# find host part, rest is parsed as a local url
	if (my ($host, $path) = $$r =~ m/^\/\/(.*?)(\/.*)$/) {

		$$r = $path;
		my $o = $class->SUPER::parse_url($r, $state);
		$o->{host} = $host;
		if (defined $o->{release}) {
			$o->can_be_empty;
			$$r = $class->urlscheme."://$o->{host}$o->{release}:$$r";
		}
		$o->setup_session;
		return $o;
	} else {
		return undef;
	}
}

my $buffsize = 2 * 1024 * 1024;

sub pkg_copy($self, $in, $object)
{
	my $name = $object->{name};
	my $dir = $object->{cache_dir};

	my ($copy, $filename) = OpenBSD::Temp::permanent_file($dir, $name) or
		$self->{state}->fatal(OpenBSD::Temp->last_error);
	chmod((0666 & ~umask), $filename);
	$object->{tempname} = $filename;
	my $handler = sub {
		my ($sig) = @_;
		unlink $filename;
		close($in);
		$SIG{$sig} = 'DEFAULT';
		kill $sig, $$;
	};

	my $nonempty = 0;
	my $error = 0;
	{

	local $SIG{'PIPE'} =  $handler;
	local $SIG{'INT'} =  $handler;
	local $SIG{'HUP'} =  $handler;
	local $SIG{'QUIT'} =  $handler;
	local $SIG{'KILL'} =  $handler;
	local $SIG{'TERM'} =  $handler;

	my ($buffer, $n);
	# copy stuff over
	do {
		$n = sysread($in, $buffer, $buffsize);
		if (!defined $n) {
			$self->{state}->fatal("Error reading: #1", $!);
		}
		if ($n > 0) {
			$nonempty = 1;
		}
		if (!$error) {
			my $r = syswrite $copy, $buffer;
			if (!defined $r || $r < $n) {
				$error = 1;
			}
		}
		syswrite STDOUT, $buffer;
	} while ($n != 0);
	close($copy);
	}

	if ($nonempty && !$error) {
		rename $filename, "$dir/$name.tgz";
	} else {
		unlink $filename;
	}
	close($in);
}

sub open_pipe($self, $object)
{
	$self->make_error_file($object);
	my $d = $self->{state}->cache_directory;
	if (defined $d) {
		$object->{cache_dir} = $d;
		if (! -d -w $d) {
			$self->{state}->fatal("bad PKG_CACHE directory #1", $d);
		}
		$object->{cache_dir} = $d;
	}
	$object->{parent} = $$;

	my ($rdfh, $wrfh);

	pipe($rdfh, $wrfh);
	my $pid2 = fork();
	$self->did_it_fork($pid2);
	if ($pid2) {
		$object->{pid2} = $pid2;
		close($wrfh);
	} else {
		open STDERR, '>>', $object->{errors};
		open(STDOUT, '>&', $wrfh);
		close($rdfh);
		close($wrfh);
		if (defined $d) {
			my $pid3 = open(my $in, "-|");
			$self->did_it_fork($pid3);
			if ($pid3) {
				$self->dont_cleanup;
				$self->pkg_copy($in, $object);
			} else {
				$self->grab_object($object);
			}
		} else {
			$self->grab_object($object);
		}
		exit(0);
	}

	if ($self->check_signed($object)) {
		my $pid = open(my $fh, "-|");
		$self->did_it_fork($pid);
		if ($pid) {
			$object->{pid} = $pid;
			close($rdfh);
		} else {
			open(STDIN, '<&', $rdfh) or
			    $self->{state}->fatal("Bad dup: #1", $!);
			close($rdfh);
			$self->signify_pipe($object);
		}

		return $self->uncompress($object, $fh);
	} else {
		return $self->uncompress($object, $rdfh);
	}
}

sub finish_and_close($self, $object)
{
	if (defined $object->{cache_dir}) {
		while (defined $object->next) {
		}
	}
	$self->SUPER::finish_and_close($object);
}

package OpenBSD::PackageRepository::HTTPorFTP;
our @ISA=qw(OpenBSD::PackageRepository::Distant);

our %distant = ();

my ($fetch_uid, $fetch_gid, $fetch_user);

sub fill_up_fetch_data($self)
{
	if ($< == 0) {
		$fetch_user = '_pkgfetch';
		unless ((undef, undef, $fetch_uid, $fetch_gid) = 
		    getpwnam($fetch_user)) {
			$self->{state}->fatal(
			    "Couldn't change identity: can't find #1 user", 
			    $fetch_user);
		}
	} else {
		($fetch_user) = getpwuid($<);
    	}
}

sub fetch_id($self)
{
	if (!defined $fetch_user) {
		$self->fill_up_fetch_data;
	}
	return ($fetch_uid, $fetch_gid, $fetch_user);
}

sub ftp_cmd($self)
{
	return OpenBSD::Paths->ftp;
}

sub drop_privileges_and_setup_env($self)
{
	my ($uid, $gid, $user) = $self->fetch_id;
	if (defined $uid) {
		# we happen right before exec, so change id permanently
		$self->{state}->change_user($uid, $gid);
	}
	# create sanitized env for ftp
	my %newenv = (
		HOME => '/var/empty',
		USER => $user,
		LOGNAME => $user,
		SHELL => '/bin/sh',
		LC_ALL => 'C', # especially, laundry error messages
		PATH => '/bin:/usr/bin'
	    );

	# copy selected stuff;
	for my $k (qw(
	    TERM
	    FTPMODE 
	    FTPSERVER
	    FTPSERVERPORT
	    ftp_proxy 
	    http_proxy 
	    http_cookies
	    ALL_PROXY
	    FTP_PROXY
	    HTTPS_PROXY
	    HTTP_PROXY
	    NO_PROXY)) {
	    	if (exists $ENV{$k}) {
			$newenv{$k} = $ENV{$k};
		}
	}
	# don't forget to swap!
	%ENV = %newenv;
}


sub grab_object($self, $object)
{
	my ($ftp, @extra) = split(/\s+/, $self->ftp_cmd);
	$self->drop_privileges_and_setup_env;
	exec {$ftp}
	    $ftp,
	    @extra,
	    "-o",
	    "-", $self->url($object->{name})
	or $self->{state}->fatal("Can't run #1: #2", $self->ftp_cmd, $!);
}

sub open_read_ftp($self, $cmd, $errors = undef)
{
	my $child_pid = open(my $fh, '-|');
	if ($child_pid) {
		$self->{pipe_pid} = $child_pid;
		return $fh;
	} else {
		open STDERR, '>>', $errors if defined $errors;

		$self->drop_privileges_and_setup_env;
		exec($cmd) 
		or $self->{state}->fatal("Can't run #1: #2", $cmd, $!);
	}
}

sub close_read_ftp($self, $fh)
{
	close($fh);
	waitpid $self->{pipe_pid}, 0;
}

sub maxcount($)
{
	return 1;
}

sub opened($self)
{
	my $k = $self->{host};
	if (!defined $distant{$k}) {
		$distant{$k} = [];
	}
	return $distant{$k};
}

sub should_have($self, $pkgname)
{
	if (defined $self->{lasterror} && $self->{lasterror} == 421) {
		return (defined $self->{list}) &&
			grep { $_ eq $pkgname } @{$self->{list}};
	} else {
		return 0;
	}
}

sub try_until_success($self, $pkgname, $code)
{
	for (my $retry = 5; $retry <= 160; $retry *= 2) {
		undef $self->{lasterror};
		my $o = &$code();
		if (defined $o) {
			return $o;
		}
		if (defined $self->{lasterror} && 
		    ($self->{lasterror} == 550 || $self->{lasterror} == 404)) {
			last;
		}
		if ($self->should_have($pkgname)) {
			$self->errsay("Temporary error, sleeping #1 seconds",
				$retry);
			sleep($retry);
		}
	}
	return undef;
}

sub find($self, $pkgname, @extra)
{
	return $self->try_until_success($pkgname,
	    sub() {
	    	return $self->SUPER::find($pkgname, @extra); });

}

sub grabPlist($self, $pkgname, @extra)
{
	return $self->try_until_success($pkgname,
	    sub() {
	    	return $self->SUPER::grabPlist($pkgname, @extra); });
}

sub list($self)
{
	if (!defined $self->{list}) {
		$self->make_room;
		my $error = OpenBSD::Temp->file;
		if (!defined $error) {
			$self->{state}->fatal(OpenBSD::Temp->last_error);
		}
		$self->{list} = $self->obtain_list($error);
		$self->parse_problems($error);
	}
	return $self->{list};
}

sub get_http_list($self, $error)
{
	my $fullname = $self->url;
	my $l = [];
	my $fh = $self->open_read_ftp($self->ftp_cmd." -o - $fullname", 
	    $error) or return;
	while(<$fh>) {
		chomp;
		for my $pkg (m/\<A[^>]*\s+HREF=\"(.*?\.tgz)\"/gio) {
			$pkg = $1 if $pkg =~ m|^.*/(.*)$|;
			# decode uri-encoding; from URI::Escape
			$pkg =~ s/%([0-9A-Fa-f]{2})/chr(hex($1))/eg;
			$self->add_to_list($l, $pkg);
		}
	}
	$self->close_read_ftp($fh);
	return $l;
}

package OpenBSD::PackageRepository::HTTP;
our @ISA=qw(OpenBSD::PackageRepository::HTTPorFTP);

sub urlscheme($)
{
	return 'http';
}

sub obtain_list($self, $error)
{
	return $self->get_http_list($error);
}

package OpenBSD::PackageRepository::HTTPS;
our @ISA=qw(OpenBSD::PackageRepository::HTTP);

sub urlscheme($)
{
	return 'https';
}

sub setup_session($self)
{
	require OpenBSD::Temp;
	$self->{count} = 0;
	local $>;
	my ($uid, $gid, $user) = $self->fetch_id;
	if (defined $uid) {
		$> = $uid;
	}
	my ($fh, undef) = OpenBSD::Temp::fh_file("session",
		sub($name) { unlink($name); });
	if (!defined $fh) {
		$self->{state}->fatal(OpenBSD::Temp->last_error);
	}
	$self->{fh} = $fh; # XXX store the full fh and not the fileno
}

sub ftp_cmd($self)
{
	return $self->SUPER::ftp_cmd." -S session=/dev/fd/".fileno($self->{fh});
}

sub drop_privileges_and_setup_env($self)
{
	$self->SUPER::drop_privileges_and_setup_env;
	# reset the CLOEXEC flag on that one
	use Fcntl;
	fcntl($self->{fh}, F_SETFD, 0);
}

package OpenBSD::PackageRepository::FTP;
our @ISA=qw(OpenBSD::PackageRepository::HTTPorFTP);

sub urlscheme($)
{
	return 'ftp';
}

sub _list($self, $cmd, $error)
{
	my $l =[];
	my $fh = $self->open_read_ftp($cmd, $error) or return;
	while(<$fh>) {
		chomp;
		next if m/^\d\d\d\s+\S/;
		if (m/No such file or directory|Failed to change directory/i) {
			$self->{no_such_dir} = 1;
		}
		next unless m/^(?:\.\/)?(\S+\.tgz)\s*$/;
		$self->add_to_list($l, $1);
	}
	$self->close_read_ftp($fh);
	return $l;
}

sub get_ftp_list($self, $error)
{
	my $fullname = $self->url;
	return $self->_list("echo 'nlist'| ".$self->ftp_cmd." $fullname", 
	    $error);
}

sub obtain_list($self, $error)
{
	if (defined $ENV{'ftp_proxy'} && $ENV{'ftp_proxy'} ne '') {
		return $self->get_http_list($error);
	} else {
		return $self->get_ftp_list($error);
	}
}

1;
