# test/cms-test.pl
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

# CMS, PKCS7 consistency test script. Run extensive tests on
# OpenSSL PKCS#7 and CMS implementations.

my $ossl_path;
my $redir = " 2> cms.err > cms.out";
# Make VMS work
if ( $^O eq "VMS" && -f "OSSLX:openssl.exe" ) {
    $ossl_path = "pipe mcr OSSLX:openssl";
}
# Make MSYS work
elsif ( $^O eq "MSWin32" && -f "../apps/openssl.exe" ) {
    $ossl_path = "cmd /c ..\\apps\\openssl";
}
elsif ( -f "../apps/openssl$ENV{EXE_EXT}" ) {
    $ossl_path = "../util/shlib_wrap.sh ../apps/openssl";
}
elsif ( -f "..\\out32dll\\openssl.exe" ) {
    $ossl_path = "..\\out32dll\\openssl.exe";
}
elsif ( -f "..\\out32\\openssl.exe" ) {
    $ossl_path = "..\\out32\\openssl.exe";
}
else {
    die "Can't find OpenSSL executable";
}

my $pk7cmd   = "$ossl_path smime ";
my $cmscmd   = "$ossl_path cms ";
my $smdir    = "smime-certs";
my $halt_err = 1;

my $badcmd = 0;
my $ossl8 = `$ossl_path version -v` =~ /0\.9\.8/;

my @smime_pkcs7_tests = (

    [
        "signed content DER format, RSA key",
        "-sign -in smcont.txt -outform \"DER\" -nodetach"
          . " -certfile $smdir/smroot.pem"
          . " -signer $smdir/smrsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed detached content DER format, RSA key",
        "-sign -in smcont.txt -outform \"DER\""
          . " -signer $smdir/smrsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt -content smcont.txt"
    ],

    [
        "signed content test streaming BER format, RSA",
        "-sign -in smcont.txt -outform \"DER\" -nodetach"
          . " -stream -signer $smdir/smrsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed content DER format, DSA key",
        "-sign -in smcont.txt -outform \"DER\" -nodetach"
          . " -signer $smdir/smdsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed detached content DER format, DSA key",
        "-sign -in smcont.txt -outform \"DER\""
          . " -signer $smdir/smdsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt -content smcont.txt"
    ],

    [
        "signed detached content DER format, add RSA signer",
        "-resign -inform \"DER\" -in test.cms -outform \"DER\""
          . " -signer $smdir/smrsa1.pem -out test2.cms",
        "-verify -in test2.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt -content smcont.txt"
    ],

    [
        "signed content test streaming BER format, DSA key",
        "-sign -in smcont.txt -outform \"DER\" -nodetach"
          . " -stream -signer $smdir/smdsa1.pem -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed content test streaming BER format, 2 DSA and 2 RSA keys",
        "-sign -in smcont.txt -outform \"DER\" -nodetach"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
"signed content test streaming BER format, 2 DSA and 2 RSA keys, no attributes",
        "-sign -in smcont.txt -outform \"DER\" -noattr -nodetach"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed content test streaming S/MIME format, 2 DSA and 2 RSA keys",
        "-sign -in smcont.txt -nodetach"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms " . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
"signed content test streaming multipart S/MIME format, 2 DSA and 2 RSA keys",
        "-sign -in smcont.txt"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms " . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "enveloped content test streaming S/MIME format, 3 recipients",
        "-encrypt -in smcont.txt"
          . " -stream -out test.cms"
          . " $smdir/smrsa1.pem $smdir/smrsa2.pem $smdir/smrsa3.pem ",
        "-decrypt -recip $smdir/smrsa1.pem -in test.cms -out smtst.txt"
    ],

    [
"enveloped content test streaming S/MIME format, 3 recipients, 3rd used",
        "-encrypt -in smcont.txt"
          . " -stream -out test.cms"
          . " $smdir/smrsa1.pem $smdir/smrsa2.pem $smdir/smrsa3.pem ",
        "-decrypt -recip $smdir/smrsa3.pem -in test.cms -out smtst.txt"
    ],

    [
"enveloped content test streaming S/MIME format, 3 recipients, key only used",
        "-encrypt -in smcont.txt"
          . " -stream -out test.cms"
          . " $smdir/smrsa1.pem $smdir/smrsa2.pem $smdir/smrsa3.pem ",
        "-decrypt -inkey $smdir/smrsa3.pem -in test.cms -out smtst.txt"
    ],

    [
"enveloped content test streaming S/MIME format, AES-256 cipher, 3 recipients",
        "-encrypt -in smcont.txt"
          . " -aes256 -stream -out test.cms"
          . " $smdir/smrsa1.pem $smdir/smrsa2.pem $smdir/smrsa3.pem ",
        "-decrypt -recip $smdir/smrsa1.pem -in test.cms -out smtst.txt"
    ],

);

my @smime_cms_tests = (

    [
        "signed content test streaming BER format, 2 DSA and 2 RSA keys, keyid",
        "-sign -in smcont.txt -outform \"DER\" -nodetach -keyid"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms -inform \"DER\" "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed content test streaming PEM format, 2 DSA and 2 RSA keys",
        "-sign -in smcont.txt -outform PEM -nodetach"
          . " -signer $smdir/smrsa1.pem -signer $smdir/smrsa2.pem"
          . " -signer $smdir/smdsa1.pem -signer $smdir/smdsa2.pem"
          . " -stream -out test.cms",
        "-verify -in test.cms -inform PEM "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed content MIME format, RSA key, signed receipt request",
        "-sign -in smcont.txt -signer $smdir/smrsa1.pem -nodetach"
          . " -receipt_request_to test\@openssl.org -receipt_request_all"
          . " -out test.cms",
        "-verify -in test.cms "
          . " \"-CAfile\" $smdir/smroot.pem -out smtst.txt"
    ],

    [
        "signed receipt MIME format, RSA key",
        "-sign_receipt -in test.cms"
          . " -signer $smdir/smrsa2.pem"
          . " -out test2.cms",
        "-verify_receipt test2.cms -in test.cms"
          . " \"-CAfile\" $smdir/smroot.pem"
    ],

    [
        "enveloped content test streaming S/MIME format, 3 recipients, keyid",
        "-encrypt -in smcont.txt"
          . " -stream -out test.cms -keyid"
          . " $smdir/smrsa1.pem $smdir/smrsa2.pem $smdir/smrsa3.pem ",
        "-decrypt -recip $smdir/smrsa1.pem -in test.cms -out smtst.txt"
    ],

    [
        "enveloped content test streaming PEM format, KEK",
        "-encrypt -in smcont.txt -outform PEM -aes128"
          . " -stream -out test.cms "
          . " -secretkey 000102030405060708090A0B0C0D0E0F "
          . " -secretkeyid C0FEE0",
        "-decrypt -in test.cms -out smtst.txt -inform PEM"
          . " -secretkey 000102030405060708090A0B0C0D0E0F "
          . " -secretkeyid C0FEE0"
    ],

    [
        "enveloped content test streaming PEM format, KEK, key only",
        "-encrypt -in smcont.txt -outform PEM -aes128"
          . " -stream -out test.cms "
          . " -secretkey 000102030405060708090A0B0C0D0E0F "
          . " -secretkeyid C0FEE0",
        "-decrypt -in test.cms -out smtst.txt -inform PEM"
          . " -secretkey 000102030405060708090A0B0C0D0E0F "
    ],

    [
        "data content test streaming PEM format",
        "-data_create -in smcont.txt -outform PEM -nodetach"
          . " -stream -out test.cms",
        "-data_out -in test.cms -inform PEM -out smtst.txt"
    ],

    [
        "encrypted content test streaming PEM format, 128 bit RC2 key",
        "\"-EncryptedData_encrypt\" -in smcont.txt -outform PEM"
          . " -rc2 -secretkey 000102030405060708090A0B0C0D0E0F"
          . " -stream -out test.cms",
        "\"-EncryptedData_decrypt\" -in test.cms -inform PEM "
          . " -secretkey 000102030405060708090A0B0C0D0E0F -out smtst.txt"
    ],

    [
        "encrypted content test streaming PEM format, 40 bit RC2 key",
        "\"-EncryptedData_encrypt\" -in smcont.txt -outform PEM"
          . " -rc2 -secretkey 0001020304"
          . " -stream -out test.cms",
        "\"-EncryptedData_decrypt\" -in test.cms -inform PEM "
          . " -secretkey 0001020304 -out smtst.txt"
    ],

    [
        "encrypted content test streaming PEM format, triple DES key",
        "\"-EncryptedData_encrypt\" -in smcont.txt -outform PEM"
          . " -des3 -secretkey 000102030405060708090A0B0C0D0E0F1011121314151617"
          . " -stream -out test.cms",
        "\"-EncryptedData_decrypt\" -in test.cms -inform PEM "
          . " -secretkey 000102030405060708090A0B0C0D0E0F1011121314151617"
          . " -out smtst.txt"
    ],

    [
        "encrypted content test streaming PEM format, 128 bit AES key",
        "\"-EncryptedData_encrypt\" -in smcont.txt -outform PEM"
          . " -aes128 -secretkey 000102030405060708090A0B0C0D0E0F"
          . " -stream -out test.cms",
        "\"-EncryptedData_decrypt\" -in test.cms -inform PEM "
          . " -secretkey 000102030405060708090A0B0C0D0E0F -out smtst.txt"
    ],

);

my @smime_cms_comp_tests = (

    [
        "compressed content test streaming PEM format",
        "-compress -in smcont.txt -outform PEM -nodetach"
          . " -stream -out test.cms",
        "-uncompress -in test.cms -inform PEM -out smtst.txt"
    ]

);

print "CMS => PKCS#7 compatibility tests\n";

run_smime_tests( \$badcmd, \@smime_pkcs7_tests, $cmscmd, $pk7cmd );

print "CMS <= PKCS#7 compatibility tests\n";

run_smime_tests( \$badcmd, \@smime_pkcs7_tests, $pk7cmd, $cmscmd );

print "CMS <=> CMS consistency tests\n";

run_smime_tests( \$badcmd, \@smime_pkcs7_tests, $cmscmd, $cmscmd );
run_smime_tests( \$badcmd, \@smime_cms_tests,   $cmscmd, $cmscmd );

if ( `$ossl_path version -f` =~ /ZLIB/ ) {
    run_smime_tests( \$badcmd, \@smime_cms_comp_tests, $cmscmd, $cmscmd );
}
else {
    print "Zlib not supported: compression tests skipped\n";
}

print "Running modified tests for OpenSSL 0.9.8 cms backport\n" if($ossl8);

if ($badcmd) {
    print "$badcmd TESTS FAILED!!\n";
}
else {
    print "ALL TESTS SUCCESSFUL.\n";
}

unlink "test.cms";
unlink "test2.cms";
unlink "smtst.txt";
unlink "cms.out";
unlink "cms.err";

sub run_smime_tests {
    my ( $rv, $aref, $scmd, $vcmd ) = @_;

    foreach $smtst (@$aref) {
        my ( $tnam, $rscmd, $rvcmd ) = @$smtst;
	if ($ossl8)
		{
		# Skip smime resign: 0.9.8 smime doesn't support -resign	
		next if ($scmd =~ /smime/ && $rscmd =~ /-resign/);
		# Disable streaming: option not supported in 0.9.8
		$tnam =~ s/streaming//;	
		$rscmd =~ s/-stream//;	
		$rvcmd =~ s/-stream//;
		}
        system("$scmd$rscmd$redir");
        if ($?) {
            print "$tnam: generation error\n";
            $$rv++;
            exit 1 if $halt_err;
            next;
        }
        system("$vcmd$rvcmd$redir");
        if ($?) {
            print "$tnam: verify error\n";
            $$rv++;
            exit 1 if $halt_err;
            next;
        }
	if (!cmp_files("smtst.txt", "smcont.txt")) {
            print "$tnam: content verify error\n";
            $$rv++;
            exit 1 if $halt_err;
            next;
	}
        print "$tnam: OK\n";
    }
}

sub cmp_files {
    use FileHandle;
    my ( $f1, $f2 ) = @_;
    my $fp1 = FileHandle->new();
    my $fp2 = FileHandle->new();

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

