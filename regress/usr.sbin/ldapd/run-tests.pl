#!/usr/bin/perl

use strict;
use warnings;
use 5.010;

use Net::LDAP;
use Net::LDAP::LDIF;
use Net::LDAP::Entry;
use Data::Dumper;

my $ldap;
my $base="dc=bar,dc=quux";
$base="dc=example,dc=com";

BEGIN {
        $ldap = Net::LDAP->new('ldapi://%2ftmp%2fldapi');
        my $mesg = $ldap->bind;
        $mesg->code && die $mesg->error;
}

END {
	$ldap->unbind;
}

        my $mesg = $ldap->search(base => $base, scope => "sub", filter => "(objectClass=inetOrgPerson)");
        $mesg->code && die $mesg->error;
        say $mesg->count." ldap entries";
        for (my $i=0 ; $i < $mesg->count ; $i++) {
                say $mesg->entry($i)->dn();
        }
