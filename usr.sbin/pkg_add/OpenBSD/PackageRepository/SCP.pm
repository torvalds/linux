# ex:ts=8 sw=4:
# $OpenBSD: SCP.pm,v 1.31 2023/06/13 09:07:18 espie Exp $
#
# Copyright (c) 2003-2006 Marc Espie <espie@openbsd.org>
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

use OpenBSD::PackageRepository::Persistent;

package OpenBSD::PackageRepository::SCP;
our @ISA=qw(OpenBSD::PackageRepository::Persistent);

use IPC::Open2;
use IO::Handle;
use OpenBSD::Paths;

sub urlscheme($)
{
	return 'scp';
}

# Any SCP repository uses one single connection, reliant on a perl at end.
# The connection starts by xfering and firing up the `distant' script.
sub initiate($self)
{
	my ($rdfh, $wrfh);

	$self->{controller} = open2($rdfh, $wrfh, OpenBSD::Paths->ssh,
	    $self->{host}, 'perl', '-x');
	$self->{cmdfh} = $wrfh;
	$self->{getfh} = $rdfh;
	$wrfh->autoflush(1);
	while(<DATA>) {
		# compress script a bit
		next if m/^\#/o && !m/^\#!/o;
		s/^\s*//o;
		next if m/^$/o;
		print $wrfh $_;
	}
	seek(DATA, 0, 0);
}

1;
__DATA__
# Distant connection script.
#! /usr/bin/perl

use v5.36;
my $pid;
my $token = 0;
$|= 1;

sub batch($code)
{
	if (defined $pid) {
		waitpid($pid, 0);
		undef $pid;
	}
	$token++;
	$pid = fork();
	if (!defined $pid) {
		say "ERROR: fork failed: $!";
	}
	if ($pid == 0) {
		&$code();
		exit(0);
	}
}

sub abort_batch()
{
	if (defined $pid) {
		kill 1, $pid;
		waitpid($pid, 0);
		undef $pid;
	}
	say "\nABORTED $token";
}

my $dirs = {};

sub expand_tilde($arg)
{
	return $dirs->{$arg} //= (getpwnam($arg))[7]."/";
}

while (<STDIN>) {
	chomp;
	if (m/^LIST\s+(.*)$/o) {
		my $dname = $1;
		$dname =~ s/^\/\~(.*?)\//expand_tilde($1)/e;
		batch(sub() {
			my $d;
			if (opendir($d, $dname)) {
				print "SUCCESS: directory $dname\n";
			} else {
				print "ERROR: bad directory $dname $!\n";
			}
			while (my $e = readdir($d)) {
				next if $e eq '.' or $e eq '..';
				next unless $e =~ m/(.+)\.tgz$/;
				next unless -f "$dname/$e";
				print "$1\n";
			}
			print "\n";
			closedir($d);
		});
	} elsif (m/^GET\s+(.*)$/o) {
		my $fname = $1;
		$fname =~ s/^\/\~(.*?)\//expand_tilde($1)/e;
		batch(sub() {
			if (open(my $fh, '<', $fname)) {
				my $size = (stat $fh)[7];
				print "TRANSFER: $size\n";
				my $buffer = '';
				while (read($fh, $buffer, 1024 * 1024) > 0) {
					print $buffer;
				}
				close($fh);
			} else {
				print "ERROR: bad file $fname $!\n";
			}
		});
	} elsif (m/^BYE$/o) {
		exit(0);
	} elsif (m/^ABORT$/o) {
		abort_batch();
	} else {
		print "ERROR: Unknown command\n";
	}
}
__END__
