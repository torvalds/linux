# test/cms-examples.pl
# Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
# project.
#
# ====================================================================
# Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
#
# 3. All advertising materials mentioning features or use of this
#    software must display the following acknowledgment:
#    "This product includes software developed by the OpenSSL Project
#    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
#
# 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
#    endorse or promote products derived from this software without
#    prior written permission. For written permission, please contact
#    licensing@OpenSSL.org.
#
# 5. Products derived from this software may not be called "OpenSSL"
#    nor may "OpenSSL" appear in their names without prior written
#    permission of the OpenSSL Project.
#
# 6. Redistributions of any form whatsoever must retain the following
#    acknowledgment:
#    "This product includes software developed by the OpenSSL Project
#    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
#
# THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
# EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
# ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
# OF THE POSSIBILITY OF SUCH DAMAGE.
# ====================================================================

# Perl script to run tests against S/MIME examples in RFC4134
# Assumes RFC is in current directory and called "rfc4134.txt"

use MIME::Base64;

my $badttest = 0;
my $verbose  = 1;

my $cmscmd;
my $exdir  = "./";
my $exfile = "./rfc4134.txt";

if (-f "../apps/openssl")
	{
	$cmscmd = "../util/shlib_wrap.sh ../apps/openssl cms";
	}
elsif (-f "..\\out32dll\\openssl.exe")
	{
	$cmscmd = "..\\out32dll\\openssl.exe cms";
	}
elsif (-f "..\\out32\\openssl.exe")
	{
	$cmscmd = "..\\out32\\openssl.exe cms";
	}

my @test_list = (
    [ "3.1.bin"  => "dataout" ],
    [ "3.2.bin"  => "encode, dataout" ],
    [ "4.1.bin"  => "encode, verifyder, cont, dss" ],
    [ "4.2.bin"  => "encode, verifyder, cont, rsa" ],
    [ "4.3.bin"  => "encode, verifyder, cont_extern, dss" ],
    [ "4.4.bin"  => "encode, verifyder, cont, dss" ],
    [ "4.5.bin"  => "verifyder, cont, rsa" ],
    [ "4.6.bin"  => "encode, verifyder, cont, dss" ],
    [ "4.7.bin"  => "encode, verifyder, cont, dss" ],
    [ "4.8.eml"  => "verifymime, dss" ],
    [ "4.9.eml"  => "verifymime, dss" ],
    [ "4.10.bin" => "encode, verifyder, cont, dss" ],
    [ "4.11.bin" => "encode, certsout" ],
    [ "5.1.bin"  => "encode, envelopeder, cont" ],
    [ "5.2.bin"  => "encode, envelopeder, cont" ],
    [ "5.3.eml"  => "envelopemime, cont" ],
    [ "6.0.bin"  => "encode, digest, cont" ],
    [ "7.1.bin"  => "encode, encrypted, cont" ],
    [ "7.2.bin"  => "encode, encrypted, cont" ]
);

# Extract examples from RFC4134 text.
# Base64 decode all examples, certificates and
# private keys are converted to PEM format.

my ( $filename, $data );

my @cleanup = ( "cms.out", "cms.err", "tmp.der", "tmp.txt" );

$data = "";

open( IN, $exfile ) || die "Can't Open RFC examples file $exfile";

while (<IN>) {
    next unless (/^\|/);
    s/^\|//;
    next if (/^\*/);
    if (/^>(.*)$/) {
        $filename = $1;
        next;
    }
    if (/^</) {
        $filename = "$exdir/$filename";
        if ( $filename =~ /\.bin$/ || $filename =~ /\.eml$/ ) {
            $data = decode_base64($data);
            open OUT, ">$filename";
            binmode OUT;
            print OUT $data;
            close OUT;
            push @cleanup, $filename;
        }
        elsif ( $filename =~ /\.cer$/ ) {
            write_pem( $filename, "CERTIFICATE", $data );
        }
        elsif ( $filename =~ /\.pri$/ ) {
            write_pem( $filename, "PRIVATE KEY", $data );
        }
        $data     = "";
        $filename = "";
    }
    else {
        $data .= $_;
    }

}

my $secretkey =
  "73:7c:79:1f:25:ea:d0:e0:46:29:25:43:52:f7:dc:62:91:e5:cb:26:91:7a:da:32";

foreach (@test_list) {
    my ( $file, $tlist ) = @$_;
    print "Example file $file:\n";
    if ( $tlist =~ /encode/ ) {
        run_reencode_test( $exdir, $file );
    }
    if ( $tlist =~ /certsout/ ) {
        run_certsout_test( $exdir, $file );
    }
    if ( $tlist =~ /dataout/ ) {
        run_dataout_test( $exdir, $file );
    }
    if ( $tlist =~ /verify/ ) {
        run_verify_test( $exdir, $tlist, $file );
    }
    if ( $tlist =~ /digest/ ) {
        run_digest_test( $exdir, $tlist, $file );
    }
    if ( $tlist =~ /encrypted/ ) {
        run_encrypted_test( $exdir, $tlist, $file, $secretkey );
    }
    if ( $tlist =~ /envelope/ ) {
        run_envelope_test( $exdir, $tlist, $file );
    }

}

foreach (@cleanup) {
    unlink $_;
}

if ($badtest) {
    print "\n$badtest TESTS FAILED!!\n";
}
else {
    print "\n***All tests successful***\n";
}

sub write_pem {
    my ( $filename, $str, $data ) = @_;

    $filename =~ s/\.[^.]*$/.pem/;

    push @cleanup, $filename;

    open OUT, ">$filename";

    print OUT "-----BEGIN $str-----\n";
    print OUT $data;
    print OUT "-----END $str-----\n";

    close OUT;
}

sub run_reencode_test {
    my ( $cmsdir, $tfile ) = @_;
    unlink "tmp.der";

    system( "$cmscmd -cmsout -inform DER -outform DER"
          . " -in $cmsdir/$tfile -out tmp.der" );

    if ($?) {
        print "\tReencode command FAILED!!\n";
        $badtest++;
    }
    elsif ( !cmp_files( "$cmsdir/$tfile", "tmp.der" ) ) {
        print "\tReencode FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tReencode passed\n" if $verbose;
    }
}

sub run_certsout_test {
    my ( $cmsdir, $tfile ) = @_;
    unlink "tmp.der";
    unlink "tmp.pem";

    system( "$cmscmd -cmsout -inform DER -certsout tmp.pem"
          . " -in $cmsdir/$tfile -out tmp.der" );

    if ($?) {
        print "\tCertificate output command FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tCertificate output passed\n" if $verbose;
    }
}

sub run_dataout_test {
    my ( $cmsdir, $tfile ) = @_;
    unlink "tmp.txt";

    system(
        "$cmscmd -data_out -inform DER" . " -in $cmsdir/$tfile -out tmp.txt" );

    if ($?) {
        print "\tDataout command FAILED!!\n";
        $badtest++;
    }
    elsif ( !cmp_files( "$cmsdir/ExContent.bin", "tmp.txt" ) ) {
        print "\tDataout compare FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tDataout passed\n" if $verbose;
    }
}

sub run_verify_test {
    my ( $cmsdir, $tlist, $tfile ) = @_;
    unlink "tmp.txt";

    $form   = "DER"                     if $tlist =~ /verifyder/;
    $form   = "SMIME"                   if $tlist =~ /verifymime/;
    $cafile = "$cmsdir/CarlDSSSelf.pem" if $tlist =~ /dss/;
    $cafile = "$cmsdir/CarlRSASelf.pem" if $tlist =~ /rsa/;

    $cmd =
        "$cmscmd -verify -inform $form"
      . " -CAfile $cafile"
      . " -in $cmsdir/$tfile -out tmp.txt";

    $cmd .= " -content $cmsdir/ExContent.bin" if $tlist =~ /cont_extern/;

    system("$cmd 2>cms.err 1>cms.out");

    if ($?) {
        print "\tVerify command FAILED!!\n";
        $badtest++;
    }
    elsif ( $tlist =~ /cont/
        && !cmp_files( "$cmsdir/ExContent.bin", "tmp.txt" ) )
    {
        print "\tVerify content compare FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tVerify passed\n" if $verbose;
    }
}

sub run_envelope_test {
    my ( $cmsdir, $tlist, $tfile ) = @_;
    unlink "tmp.txt";

    $form = "DER"   if $tlist =~ /envelopeder/;
    $form = "SMIME" if $tlist =~ /envelopemime/;

    $cmd =
        "$cmscmd -decrypt -inform $form"
      . " -recip $cmsdir/BobRSASignByCarl.pem"
      . " -inkey $cmsdir/BobPrivRSAEncrypt.pem"
      . " -in $cmsdir/$tfile -out tmp.txt";

    system("$cmd 2>cms.err 1>cms.out");

    if ($?) {
        print "\tDecrypt command FAILED!!\n";
        $badtest++;
    }
    elsif ( $tlist =~ /cont/
        && !cmp_files( "$cmsdir/ExContent.bin", "tmp.txt" ) )
    {
        print "\tDecrypt content compare FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tDecrypt passed\n" if $verbose;
    }
}

sub run_digest_test {
    my ( $cmsdir, $tlist, $tfile ) = @_;
    unlink "tmp.txt";

    my $cmd =
      "$cmscmd -digest_verify -inform DER" . " -in $cmsdir/$tfile -out tmp.txt";

    system("$cmd 2>cms.err 1>cms.out");

    if ($?) {
        print "\tDigest verify command FAILED!!\n";
        $badtest++;
    }
    elsif ( $tlist =~ /cont/
        && !cmp_files( "$cmsdir/ExContent.bin", "tmp.txt" ) )
    {
        print "\tDigest verify content compare FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tDigest verify passed\n" if $verbose;
    }
}

sub run_encrypted_test {
    my ( $cmsdir, $tlist, $tfile, $key ) = @_;
    unlink "tmp.txt";

    system( "$cmscmd -EncryptedData_decrypt -inform DER"
          . " -secretkey $key"
          . " -in $cmsdir/$tfile -out tmp.txt" );

    if ($?) {
        print "\tEncrypted Data command FAILED!!\n";
        $badtest++;
    }
    elsif ( $tlist =~ /cont/
        && !cmp_files( "$cmsdir/ExContent.bin", "tmp.txt" ) )
    {
        print "\tEncrypted Data content compare FAILED!!\n";
        $badtest++;
    }
    else {
        print "\tEncryptedData verify passed\n" if $verbose;
    }
}

sub cmp_files {
    my ( $f1, $f2 ) = @_;
    my ( $fp1, $fp2 );

    my ( $rd1, $rd2 );

    if ( !open( $fp1, "<$f1" ) ) {
        print STDERR "Can't Open file $f1\n";
        return 0;
    }

    if ( !open( $fp2, "<$f2" ) ) {
        print STDERR "Can't Open file $f2\n";
        return 0;
    }

    binmode $fp1;
    binmode $fp2;

    my $ret = 0;

    for ( ; ; ) {
        $n1 = sysread $fp1, $rd1, 4096;
        $n2 = sysread $fp2, $rd2, 4096;
        last if ( $n1 != $n2 );
        last if ( $rd1 ne $rd2 );

        if ( $n1 == 0 ) {
            $ret = 1;
            last;
        }

    }

    close $fp1;
    close $fp2;

    return $ret;

}

