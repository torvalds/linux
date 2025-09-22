#!/usr/bin/perl
# connect non-blocking, wait for EOF from stdin, shutdown socket, wait for EOF

use strict;
use warnings;
use Errno qw(EINPROGRESS);
use Socket qw(:DEFAULT SOCK_NONBLOCK inet_pton SOL_SOCKET SO_KEEPALIVE);

@ARGV == 3 || @ARGV == 4
    or die "usage: client.pl bind-addr connect-addr connect-port\n".
    "    [shutdown|keepalive]\n";
my ($bindaddr, $connectaddr, $connectport, $action) = @ARGV;
$action ||= "";

socket(my $s, PF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0)
    or die "socket: $!";

if ($action =~ /keepalive/) {
    setsockopt($s, SOL_SOCKET, SO_KEEPALIVE, 1)
	or die "setsockopt: $!";
}

my $bindip = inet_pton(AF_INET, $bindaddr)
    or die "inet_pton bind-addr $bindaddr";
bind($s, sockaddr_in(0, $bindip))
    or die "bind $bindaddr: $!";
my $connectip = inet_pton(AF_INET, $connectaddr)
    or die "inet_pton connect-addr $connectaddr";
$connectport =~ /^\d+$/
    or die "non numeric connect-port $connectport";
connect($s, sockaddr_in($connectport, $connectip)) || $!{EINPROGRESS}
    or die "connect $connectaddr $connectport: $!";

getc();

if ($action =~ /shutdown/) {
    shutdown($s, SHUT_WR)
	or die "shutdown: $!";
}

my $timeout = 10;
my $rin = '';
vec($rin, fileno($s),  1) = 1;
select($rin, undef, undef, $timeout)
    or die "select: $!";
close($s)
    or die "close: $!";

exit 0;
