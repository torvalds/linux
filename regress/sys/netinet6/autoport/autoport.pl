#!/usr/bin/perl
# $OpenBSD: autoport.pl,v 1.2 2017/07/03 19:35:06 bluhm Exp $

use strict;
use warnings;
use Socket qw(PF_INET PF_INET6 SOCK_STREAM SOMAXCONN
    inet_pton sockaddr_in sockaddr_in6);
use Errno;

my ($pf, $host, $sin, $badsock);

if (@ARGV < 3 or @ARGV > 4) {
	die "usage: $0 <pf> <listen> <first> [count]\n"
}

if ($> != 0) {
	die "run this script as root\n"
}

my ($af, $test_listen, $test_first, $test_count) = @ARGV;

$test_count = SOMAXCONN if (not defined $test_count);

my $test_last = $test_first + $test_count;

if ($test_first  <= 0 || 65536 <= $test_first ||
    $test_last   <= 0 || 65536 <= $test_last ||
    $test_listen <= 0 || 65536 <= $test_listen) {
	die "invalid port number\n";
}

if ($test_first > $test_last) {
	die "first must be lower than last\n";
}

if ($test_listen >= $test_first && $test_listen <= $test_last) {
	die "listen must be outside the [first..last] range\n";
}

if ($af == "4") {
	$pf = PF_INET;
	$sin = sockaddr_in($test_listen, inet_pton($pf,"127.0.0.1"));
} elsif ($af == "6") {
	$pf = PF_INET6;
	$sin = sockaddr_in6($test_listen, inet_pton($pf,"::1"));
} else {
	die "af must be 4 or 6\n";
}

system("sysctl net.inet.ip.portfirst");
system("sysctl net.inet.ip.portlast");

my $orig_first = qx( sysctl -n net.inet.ip.portfirst );
chomp $orig_first;
my $orig_last  = qx( sysctl -n net.inet.ip.portlast );
chomp $orig_last;

END {
	system("sysctl net.inet.ip.portfirst=$orig_first") if $orig_first;
	system("sysctl net.inet.ip.portlast=$orig_last") if $orig_last;
}

# first < last

socket(my $servsock, $pf, SOCK_STREAM, getprotobyname("tcp"))
    or die "socket servsock failed: $!";
bind($servsock, $sin)
    or die "bind servsock to $test_listen failed: $!";
listen($servsock, SOMAXCONN)
    or die "listen servsock failed: $!";

my $rc_f = 0;

print "testing with portfirst < portlast\n";

system("sysctl net.inet.ip.portfirst=$test_first");
system("sysctl net.inet.ip.portlast=$test_last");

my @socka;
for ($test_first .. $test_last) {
	socket(my $sock, $pf, SOCK_STREAM, getprotobyname("tcp"))
	    or die "socket sock failed: $!";
	unless (connect($sock, $sin)) {
		print "FAIL: connect sock to $test_listen failed '$!',",
		    " but should succeed\n";
		$rc_f = 1;
	}
	push @socka, $sock;
}

socket($badsock, $pf, SOCK_STREAM, getprotobyname("tcp"))
    or die "socket badsock failed: $!";
if (connect($badsock, $sin)) {
	print "FAIL: connect badsock to $test_listen succeeded,",
	    " but should fail\n";
	$rc_f = 1;
} elsif (not $!{EADDRNOTAVAIL}) {
	print "FAIL: connect badsock to $test_listen failed with errno '$!',",
	    " but should be EADDRNOTAVAIL\n";
	$rc_f = 1;
}
close($badsock)
    or die "close badsock failed: $!";

while (my $sock = pop @socka) {
	close($sock)
	    or die "close sock failed: $!";
}

close($servsock)
    or die "close servsock failed: $!";

if ($rc_f == 0) {
	print "subtest f PASS\n"
} else {
	print "subtest f FAIL\n"
}

# first > last

socket($servsock, $pf, SOCK_STREAM, getprotobyname("tcp"))
    or die "socket servsock failed: $!";
bind($servsock, $sin)
    or die "bind servsock to $test_listen failed: $!";
listen($servsock, SOMAXCONN)
    or die "listen servsock failed: $!";

my $rc_b = 0;

print "testing with portfirst > portlast\n";

system("sysctl net.inet.ip.portfirst=$test_last");
system("sysctl net.inet.ip.portlast=$test_first");

for ($test_first .. $test_last) {
	socket(my $sock, $pf, SOCK_STREAM, getprotobyname("tcp"))
	    or die "socket sock failed: $!";
	unless (connect($sock, $sin)) {
		print "FAIL: connect sock to $test_listen failed '$!',",
		    "but should succeed\n";
		$rc_b = 1;
	}
	push @socka, $sock;
}

socket($badsock, $pf, SOCK_STREAM, getprotobyname("tcp"))
    or die "socket badsock failed: $!";
if (connect($badsock, $sin)) {
	print "FAIL: connect badsock to $test_listen succeeded,",
	    " but should fail\n";
	$rc_b = 1;
} elsif (not $!{EADDRNOTAVAIL}) {
	print "FAIL: connect badsock to $test_listen failed with errno '$!',",
	    " but should be EADDRNOTAVAIL\n";
	$rc_b = 1;
}
close($badsock)
    or die "close badsock failed: $!";

while (my $sock = pop @socka) {
	close($sock)
	    or die "close sock failed: $!";
}

close($servsock)
    or die "close servsock failed: $!";

if ($rc_b == 0) {
	print "subtest b PASS\n"
} else {
	print "subtest b FAIL\n"
}

exit ($rc_f || $rc_b);
