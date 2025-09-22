# Copied from Net::LDAP's t/common.pl - perl licence
#
#
BEGIN {

  $SERVER_EXE = '/usr/sbin/ldapd';
  $SERVER_TYPE = 'ldapd+ssl+ipc';

  undef $SERVER_EXE unless $SERVER_EXE and -x $SERVER_EXE;

  # fallback for the host to connect - needs to support IPv4 & IPv6
  $HOST     ||= 'localhost';

  # Where to put temporary files while testing
  # the Makefile is setup to delete temp/ when make clean is run
  $TEMPDIR  = "./temp";

  $PASSWD   = 'secret';
  $BASEDN   = "o=University of Michigan, c=US";
  $MANAGERDN= "cn=Manager, o=University of Michigan, c=US";
  $JAJDN    = "cn=James A Jones 1, ou=Alumni Association, ou=People, o=University of Michigan, c=US";
  $BABSDN   = "cn=Barbara Jensen, ou=Information Technology Division, ou=People, o=University of Michigan, c=US";
  $PORT = 6640;
  @LDAPD  = ($SERVER_EXE, "-r", "./temp", "-f", "./nldapd.conf", "-s" , "./ctrlsock", "-dv"),

  $LDAP_VERSION ||= 3;
  mkdir($TEMPDIR,0777);
  die "$TEMPDIR is not a directory" unless -d $TEMPDIR;
}

use Test::More;
use Net::LDAP;
use Net::LDAP::LDIF;
use Net::LDAP::Util qw(canonical_dn);
use File::Path qw(rmtree);
use File::Basename qw(basename);
use File::Compare qw(compare_text);

my $pid;

sub start_server {
  my %arg = (version => 3, @_);

  return 0
    unless ($LDAP_VERSION >= $arg{version}
	and $LDAPD[0] and -x $LDAPD[0]
	and (!$arg{ssl} or $SSL_PORT)
	and (!$arg{ipc} or $IPC_SOCK));

  note("@LDAPD")  if $ENV{TEST_VERBOSE};

  my $log = $TEMPDIR . "/" . basename($0,'.t');

  unless ($pid = fork) {
    die "fork: $!" unless defined $pid;

    open(STDERR, ">$log");
    open(STDOUT, ">&STDERR");
    close(STDIN);

    exec(@LDAPD) or die "cannot exec @LDAPD";
  }

  sleep 2; # wait for server to start
  return 1;
}

sub kill_server {
  if ($pid) {
    kill 9, $pid;
    sleep 2;
    undef $pid;
  }
}

END {
  kill_server();
}

sub client {
  my %arg = @_;
  my $ldap;
  my $count;
  local $^W = 0;
  my %opt = map { $_ => $arg{$_} } grep { exists($arg{$_}) } qw/inet4 inet6 debug/;

  if ($arg{ssl}) {
    require Net::LDAPS;
    until($ldap = Net::LDAPS->new($HOST, %opt, port => $SSL_PORT, version => 3)) {
      die "ldaps://$HOST:$SSL_PORT/ $@" if ++$count > 10;
      sleep 1;
    }
  }
  elsif ($arg{ipc}) {
    require Net::LDAPI;
    until($ldap = Net::LDAPI->new($IPC_SOCK)) {
      die "ldapi://$IPC_SOCK/ $@" if ++$count > 10;
      sleep 1;
    }
  }
  elsif ($arg{url}) {
    print "Trying $arg{url}\n";
    until($ldap = Net::LDAP->new($arg{url}, %opt)) {
      die "$arg{url} $@" if ++$count > 10;
      sleep 1;
    }
  }
  else {
    until($ldap = Net::LDAP->new($HOST, %opt, port => $PORT, version => $LDAP_VERSION)) {
      die "ldap://$HOST:$PORT/ $@" if ++$count > 10;
      sleep 1;
    }
  }
  $ldap;
}

sub compare_ldif {
  my($test,$mesg) = splice(@_,0,2);

  unless (ok(!$mesg->code, $mesg->error)) {
    skip($mesg->error, 2);
    return;
  }

  my $ldif = Net::LDAP::LDIF->new("$TEMPDIR/${test}-out.ldif","w", lowercase => 1);
  unless (ok($ldif, "Read ${test}-out.ldif")) {
    skip("Read error", 1);
    return;
  }

  my @canon_opt = (casefold => 'lower', separator => ', ');
  foreach $entry (@_) {
    $entry->dn(canonical_dn($entry->dn, @canon_opt));
    foreach $attr ($entry->attributes) {
      $entry->delete($attr) if $attr =~ /^(modifiersname|modifytimestamp|creatorsname|createtimestamp)$/i;
      if ($attr =~ /^(seealso|member|owner)$/i) {
	$entry->replace($attr => [ map { canonical_dn($_, @canon_opt) } $entry->get_value($attr) ]);
      }
    }
    $ldif->write($entry);
  }

  $ldif->done; # close the file;

  ok(!compare_text("$TEMPDIR/${test}-out.ldif", "data/${test}-cmp.ldif"), "data/${test}-cmp.ldif");
}

sub ldif_populate {
  my ($ldap, $file, $change) = @_;
  my $ok = 1;

  my $ldif = Net::LDAP::LDIF->new($file,"r", changetype => $change || 'add')
	or return;

  while (my $e = $ldif->read_entry) {
    $mesg = $e->update($ldap);
    if ($mesg->code) {
      $ok = 0;
      Net::LDAP::LDIF->new(qw(- w))->write_entry($e);
      print "# ",$mesg->code,": ",$mesg->error,"\n";
    }
  }
  $ok;
}

1;
