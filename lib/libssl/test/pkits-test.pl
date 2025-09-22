# test/pkits-test.pl
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

# Perl utility to run PKITS tests for RFC3280 compliance. 

my $ossl_path;

if ( -f "../apps/openssl" ) {
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

my $pkitsdir = "pkits/smime";
my $pkitsta = "pkits/certs/TrustAnchorRootCertificate.crt";

die "Can't find PKITS test data" if !-d $pkitsdir;

my $nist1 = "2.16.840.1.101.3.2.1.48.1";
my $nist2 = "2.16.840.1.101.3.2.1.48.2";
my $nist3 = "2.16.840.1.101.3.2.1.48.3";
my $nist4 = "2.16.840.1.101.3.2.1.48.4";
my $nist5 = "2.16.840.1.101.3.2.1.48.5";
my $nist6 = "2.16.840.1.101.3.2.1.48.6";

my $apolicy = "X509v3 Any Policy";

# This table contains the chapter headings of the accompanying PKITS
# document. They provide useful informational output and their names
# can be converted into the filename to test.

my @testlists = (
    [ "4.1", "Signature Verification" ],
    [ "4.1.1", "Valid Signatures Test1",                        0 ],
    [ "4.1.2", "Invalid CA Signature Test2",                    7 ],
    [ "4.1.3", "Invalid EE Signature Test3",                    7 ],
    [ "4.1.4", "Valid DSA Signatures Test4",                    0 ],
    [ "4.1.5", "Valid DSA Parameter Inheritance Test5",         0 ],
    [ "4.1.6", "Invalid DSA Signature Test6",                   7 ],
    [ "4.2",   "Validity Periods" ],
    [ "4.2.1", "Invalid CA notBefore Date Test1",               9 ],
    [ "4.2.2", "Invalid EE notBefore Date Test2",               9 ],
    [ "4.2.3", "Valid pre2000 UTC notBefore Date Test3",        0 ],
    [ "4.2.4", "Valid GeneralizedTime notBefore Date Test4",    0 ],
    [ "4.2.5", "Invalid CA notAfter Date Test5",                10 ],
    [ "4.2.6", "Invalid EE notAfter Date Test6",                10 ],
    [ "4.2.7", "Invalid pre2000 UTC EE notAfter Date Test7",    10 ],
    [ "4.2.8", "Valid GeneralizedTime notAfter Date Test8",     0 ],
    [ "4.3",   "Verifying Name Chaining" ],
    [ "4.3.1", "Invalid Name Chaining EE Test1",                20 ],
    [ "4.3.2", "Invalid Name Chaining Order Test2",             20 ],
    [ "4.3.3", "Valid Name Chaining Whitespace Test3",          0 ],
    [ "4.3.4", "Valid Name Chaining Whitespace Test4",          0 ],
    [ "4.3.5", "Valid Name Chaining Capitalization Test5",      0 ],
    [ "4.3.6", "Valid Name Chaining UIDs Test6",                0 ],
    [ "4.3.7", "Valid RFC3280 Mandatory Attribute Types Test7", 0 ],
    [ "4.3.8", "Valid RFC3280 Optional Attribute Types Test8",  0 ],
    [ "4.3.9", "Valid UTF8String Encoded Names Test9",          0 ],
    [ "4.3.10", "Valid Rollover from PrintableString to UTF8String Test10", 0 ],
    [ "4.3.11", "Valid UTF8String Case Insensitive Match Test11",           0 ],
    [ "4.4",    "Basic Certificate Revocation Tests" ],
    [ "4.4.1",  "Missing CRL Test1",                                        3 ],
    [ "4.4.2", "Invalid Revoked CA Test2",          23 ],
    [ "4.4.3", "Invalid Revoked EE Test3",          23 ],
    [ "4.4.4", "Invalid Bad CRL Signature Test4",   8 ],
    [ "4.4.5", "Invalid Bad CRL Issuer Name Test5", 3 ],
    [ "4.4.6", "Invalid Wrong CRL Test6",           3 ],
    [ "4.4.7", "Valid Two CRLs Test7",              0 ],

    # The test document suggests these should return certificate revoked...
    # Subsequent discussion has concluded they should not due to unhandled
    # critical CRL extensions.
    [ "4.4.8", "Invalid Unknown CRL Entry Extension Test8", 36 ],
    [ "4.4.9", "Invalid Unknown CRL Extension Test9",       36 ],

    [ "4.4.10", "Invalid Unknown CRL Extension Test10",             36 ],
    [ "4.4.11", "Invalid Old CRL nextUpdate Test11",                12 ],
    [ "4.4.12", "Invalid pre2000 CRL nextUpdate Test12",            12 ],
    [ "4.4.13", "Valid GeneralizedTime CRL nextUpdate Test13",      0 ],
    [ "4.4.14", "Valid Negative Serial Number Test14",              0 ],
    [ "4.4.15", "Invalid Negative Serial Number Test15",            23 ],
    [ "4.4.16", "Valid Long Serial Number Test16",                  0 ],
    [ "4.4.17", "Valid Long Serial Number Test17",                  0 ],
    [ "4.4.18", "Invalid Long Serial Number Test18",                23 ],
    [ "4.4.19", "Valid Separate Certificate and CRL Keys Test19",   0 ],
    [ "4.4.20", "Invalid Separate Certificate and CRL Keys Test20", 23 ],

    # CRL path is revoked so get a CRL path validation error
    [ "4.4.21", "Invalid Separate Certificate and CRL Keys Test21",      54 ],
    [ "4.5",    "Verifying Paths with Self-Issued Certificates" ],
    [ "4.5.1",  "Valid Basic Self-Issued Old With New Test1",            0 ],
    [ "4.5.2",  "Invalid Basic Self-Issued Old With New Test2",          23 ],
    [ "4.5.3",  "Valid Basic Self-Issued New With Old Test3",            0 ],
    [ "4.5.4",  "Valid Basic Self-Issued New With Old Test4",            0 ],
    [ "4.5.5",  "Invalid Basic Self-Issued New With Old Test5",          23 ],
    [ "4.5.6",  "Valid Basic Self-Issued CRL Signing Key Test6",         0 ],
    [ "4.5.7",  "Invalid Basic Self-Issued CRL Signing Key Test7",       23 ],
    [ "4.5.8",  "Invalid Basic Self-Issued CRL Signing Key Test8",       20 ],
    [ "4.6",    "Verifying Basic Constraints" ],
    [ "4.6.1",  "Invalid Missing basicConstraints Test1",                24 ],
    [ "4.6.2",  "Invalid cA False Test2",                                24 ],
    [ "4.6.3",  "Invalid cA False Test3",                                24 ],
    [ "4.6.4",  "Valid basicConstraints Not Critical Test4",             0 ],
    [ "4.6.5",  "Invalid pathLenConstraint Test5",                       25 ],
    [ "4.6.6",  "Invalid pathLenConstraint Test6",                       25 ],
    [ "4.6.7",  "Valid pathLenConstraint Test7",                         0 ],
    [ "4.6.8",  "Valid pathLenConstraint Test8",                         0 ],
    [ "4.6.9",  "Invalid pathLenConstraint Test9",                       25 ],
    [ "4.6.10", "Invalid pathLenConstraint Test10",                      25 ],
    [ "4.6.11", "Invalid pathLenConstraint Test11",                      25 ],
    [ "4.6.12", "Invalid pathLenConstraint Test12",                      25 ],
    [ "4.6.13", "Valid pathLenConstraint Test13",                        0 ],
    [ "4.6.14", "Valid pathLenConstraint Test14",                        0 ],
    [ "4.6.15", "Valid Self-Issued pathLenConstraint Test15",            0 ],
    [ "4.6.16", "Invalid Self-Issued pathLenConstraint Test16",          25 ],
    [ "4.6.17", "Valid Self-Issued pathLenConstraint Test17",            0 ],
    [ "4.7",    "Key Usage" ],
    [ "4.7.1",  "Invalid keyUsage Critical keyCertSign False Test1",     20 ],
    [ "4.7.2",  "Invalid keyUsage Not Critical keyCertSign False Test2", 20 ],
    [ "4.7.3",  "Valid keyUsage Not Critical Test3",                     0 ],
    [ "4.7.4",  "Invalid keyUsage Critical cRLSign False Test4",         35 ],
    [ "4.7.5",  "Invalid keyUsage Not Critical cRLSign False Test5",     35 ],

    # Certificate policy tests need special handling. They can have several
    # sub tests and we need to check the outputs are correct.

    [ "4.8", "Certificate Policies" ],
    [
        "4.8.1.1",
        "All Certificates Same Policy Test1",
        "-policy anyPolicy -explicit_policy",
        "True", $nist1, $nist1, 0
    ],
    [
        "4.8.1.2",
        "All Certificates Same Policy Test1",
        "-policy $nist1 -explicit_policy",
        "True", $nist1, $nist1, 0
    ],
    [
        "4.8.1.3",
        "All Certificates Same Policy Test1",
        "-policy $nist2 -explicit_policy",
        "True", $nist1, "<empty>", 43
    ],
    [
        "4.8.1.4",
        "All Certificates Same Policy Test1",
        "-policy $nist1 -policy $nist2 -explicit_policy",
        "True", $nist1, $nist1, 0
    ],
    [
        "4.8.2.1",
        "All Certificates No Policies Test2",
        "-policy anyPolicy",
        "False", "<empty>", "<empty>", 0
    ],
    [
        "4.8.2.2",
        "All Certificates No Policies Test2",
        "-policy anyPolicy -explicit_policy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.3.1",
        "Different Policies Test3",
        "-policy anyPolicy",
        "False", "<empty>", "<empty>", 0
    ],
    [
        "4.8.3.2",
        "Different Policies Test3",
        "-policy anyPolicy -explicit_policy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.3.3",
        "Different Policies Test3",
        "-policy $nist1 -policy $nist2 -explicit_policy",
        "True", "<empty>", "<empty>", 43
    ],

    [
        "4.8.4",
        "Different Policies Test4",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.5",
        "Different Policies Test5",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.6.1",
        "Overlapping Policies Test6",
        "-policy anyPolicy",
        "True", $nist1, $nist1, 0
    ],
    [
        "4.8.6.2",
        "Overlapping Policies Test6",
        "-policy $nist1",
        "True", $nist1, $nist1, 0
    ],
    [
        "4.8.6.3",
        "Overlapping Policies Test6",
        "-policy $nist2",
        "True", $nist1, "<empty>", 43
    ],
    [
        "4.8.7",
        "Different Policies Test7",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.8",
        "Different Policies Test8",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.9",
        "Different Policies Test9",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.10.1",
        "All Certificates Same Policies Test10",
        "-policy $nist1",
        "True", "$nist1:$nist2", "$nist1", 0
    ],
    [
        "4.8.10.2",
        "All Certificates Same Policies Test10",
        "-policy $nist2",
        "True", "$nist1:$nist2", "$nist2", 0
    ],
    [
        "4.8.10.3",
        "All Certificates Same Policies Test10",
        "-policy anyPolicy",
        "True", "$nist1:$nist2", "$nist1:$nist2", 0
    ],
    [
        "4.8.11.1",
        "All Certificates AnyPolicy Test11",
        "-policy anyPolicy",
        "True", "$apolicy", "$apolicy", 0
    ],
    [
        "4.8.11.2",
        "All Certificates AnyPolicy Test11",
        "-policy $nist1",
        "True", "$apolicy", "$nist1", 0
    ],
    [
        "4.8.12",
        "Different Policies Test12",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.8.13.1",
        "All Certificates Same Policies Test13",
        "-policy $nist1",
        "True", "$nist1:$nist2:$nist3", "$nist1", 0
    ],
    [
        "4.8.13.2",
        "All Certificates Same Policies Test13",
        "-policy $nist2",
        "True", "$nist1:$nist2:$nist3", "$nist2", 0
    ],
    [
        "4.8.13.3",
        "All Certificates Same Policies Test13",
        "-policy $nist3",
        "True", "$nist1:$nist2:$nist3", "$nist3", 0
    ],
    [
        "4.8.14.1",       "AnyPolicy Test14",
        "-policy $nist1", "True",
        "$nist1",         "$nist1",
        0
    ],
    [
        "4.8.14.2",       "AnyPolicy Test14",
        "-policy $nist2", "True",
        "$nist1",         "<empty>",
        43
    ],
    [
        "4.8.15",
        "User Notice Qualifier Test15",
        "-policy anyPolicy",
        "False", "$nist1", "$nist1", 0
    ],
    [
        "4.8.16",
        "User Notice Qualifier Test16",
        "-policy anyPolicy",
        "False", "$nist1", "$nist1", 0
    ],
    [
        "4.8.17",
        "User Notice Qualifier Test17",
        "-policy anyPolicy",
        "False", "$nist1", "$nist1", 0
    ],
    [
        "4.8.18.1",
        "User Notice Qualifier Test18",
        "-policy $nist1",
        "True", "$nist1:$nist2", "$nist1", 0
    ],
    [
        "4.8.18.2",
        "User Notice Qualifier Test18",
        "-policy $nist2",
        "True", "$nist1:$nist2", "$nist2", 0
    ],
    [
        "4.8.19",
        "User Notice Qualifier Test19",
        "-policy anyPolicy",
        "False", "$nist1", "$nist1", 0
    ],
    [
        "4.8.20",
        "CPS Pointer Qualifier Test20",
        "-policy anyPolicy -explicit_policy",
        "True", "$nist1", "$nist1", 0
    ],
    [ "4.9", "Require Explicit Policy" ],
    [
        "4.9.1",
        "Valid RequireExplicitPolicy Test1",
        "-policy anyPolicy",
        "False", "<empty>", "<empty>", 0
    ],
    [
        "4.9.2",
        "Valid RequireExplicitPolicy Test2",
        "-policy anyPolicy",
        "False", "<empty>", "<empty>", 0
    ],
    [
        "4.9.3",
        "Invalid RequireExplicitPolicy Test3",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.9.4",
        "Valid RequireExplicitPolicy Test4",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.9.5",
        "Invalid RequireExplicitPolicy Test5",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.9.6",
        "Valid Self-Issued requireExplicitPolicy Test6",
        "-policy anyPolicy",
        "False", "<empty>", "<empty>", 0
    ],
    [
        "4.9.7",
        "Invalid Self-Issued requireExplicitPolicy Test7",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.9.8",
        "Invalid Self-Issued requireExplicitPolicy Test8",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [ "4.10", "Policy Mappings" ],
    [
        "4.10.1.1",
        "Valid Policy Mapping Test1",
        "-policy $nist1",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.10.1.2",
        "Valid Policy Mapping Test1",
        "-policy $nist2",
        "True", "$nist1", "<empty>", 43
    ],
    [
        "4.10.1.3",
        "Valid Policy Mapping Test1",
        "-policy anyPolicy -inhibit_map",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.10.2.1",
        "Invalid Policy Mapping Test2",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.10.2.2",
        "Invalid Policy Mapping Test2",
        "-policy anyPolicy -inhibit_map",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.10.3.1",
        "Valid Policy Mapping Test3",
        "-policy $nist1",
        "True", "$nist2", "<empty>", 43
    ],
    [
        "4.10.3.2",
        "Valid Policy Mapping Test3",
        "-policy $nist2",
        "True", "$nist2", "$nist2", 0
    ],
    [
        "4.10.4",
        "Invalid Policy Mapping Test4",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.10.5.1",
        "Valid Policy Mapping Test5",
        "-policy $nist1",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.10.5.2",
        "Valid Policy Mapping Test5",
        "-policy $nist6",
        "True", "$nist1", "<empty>", 43
    ],
    [
        "4.10.6.1",
        "Valid Policy Mapping Test6",
        "-policy $nist1",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.10.6.2",
        "Valid Policy Mapping Test6",
        "-policy $nist6",
        "True", "$nist1", "<empty>", 43
    ],
    [ "4.10.7", "Invalid Mapping From anyPolicy Test7", 42 ],
    [ "4.10.8", "Invalid Mapping To anyPolicy Test8",   42 ],
    [
        "4.10.9",
        "Valid Policy Mapping Test9",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.10.10",
        "Invalid Policy Mapping Test10",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.10.11",
        "Valid Policy Mapping Test11",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],

    # TODO: check notice display
    [
        "4.10.12.1",
        "Valid Policy Mapping Test12",
        "-policy $nist1",
        "True", "$nist1:$nist2", "$nist1", 0
    ],

    # TODO: check notice display
    [
        "4.10.12.2",
        "Valid Policy Mapping Test12",
        "-policy $nist2",
        "True", "$nist1:$nist2", "$nist2", 0
    ],
    [
        "4.10.13",
        "Valid Policy Mapping Test13",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],

    # TODO: check notice display
    [
        "4.10.14",
        "Valid Policy Mapping Test14",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [ "4.11", "Inhibit Policy Mapping" ],
    [
        "4.11.1",
        "Invalid inhibitPolicyMapping Test1",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.2",
        "Valid inhibitPolicyMapping Test2",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.11.3",
        "Invalid inhibitPolicyMapping Test3",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.4",
        "Valid inhibitPolicyMapping Test4",
        "-policy anyPolicy",
        "True", "$nist2", "$nist2", 0
    ],
    [
        "4.11.5",
        "Invalid inhibitPolicyMapping Test5",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.6",
        "Invalid inhibitPolicyMapping Test6",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.7",
        "Valid Self-Issued inhibitPolicyMapping Test7",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.11.8",
        "Invalid Self-Issued inhibitPolicyMapping Test8",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.9",
        "Invalid Self-Issued inhibitPolicyMapping Test9",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.10",
        "Invalid Self-Issued inhibitPolicyMapping Test10",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.11.11",
        "Invalid Self-Issued inhibitPolicyMapping Test11",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [ "4.12", "Inhibit Any Policy" ],
    [
        "4.12.1",
        "Invalid inhibitAnyPolicy Test1",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.12.2",
        "Valid inhibitAnyPolicy Test2",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.12.3.1",
        "inhibitAnyPolicy Test3",
        "-policy anyPolicy",
        "True", "$nist1", "$nist1", 0
    ],
    [
        "4.12.3.2",
        "inhibitAnyPolicy Test3",
        "-policy anyPolicy -inhibit_any",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.12.4",
        "Invalid inhibitAnyPolicy Test4",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.12.5",
        "Invalid inhibitAnyPolicy Test5",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [
        "4.12.6",
        "Invalid inhibitAnyPolicy Test6",
        "-policy anyPolicy",
        "True", "<empty>", "<empty>", 43
    ],
    [ "4.12.7",  "Valid Self-Issued inhibitAnyPolicy Test7",      0 ],
    [ "4.12.8",  "Invalid Self-Issued inhibitAnyPolicy Test8",    43 ],
    [ "4.12.9",  "Valid Self-Issued inhibitAnyPolicy Test9",      0 ],
    [ "4.12.10", "Invalid Self-Issued inhibitAnyPolicy Test10",   43 ],
    [ "4.13",    "Name Constraints" ],
    [ "4.13.1",  "Valid DN nameConstraints Test1",                0 ],
    [ "4.13.2",  "Invalid DN nameConstraints Test2",              47 ],
    [ "4.13.3",  "Invalid DN nameConstraints Test3",              47 ],
    [ "4.13.4",  "Valid DN nameConstraints Test4",                0 ],
    [ "4.13.5",  "Valid DN nameConstraints Test5",                0 ],
    [ "4.13.6",  "Valid DN nameConstraints Test6",                0 ],
    [ "4.13.7",  "Invalid DN nameConstraints Test7",              48 ],
    [ "4.13.8",  "Invalid DN nameConstraints Test8",              48 ],
    [ "4.13.9",  "Invalid DN nameConstraints Test9",              48 ],
    [ "4.13.10", "Invalid DN nameConstraints Test10",             48 ],
    [ "4.13.11", "Valid DN nameConstraints Test11",               0 ],
    [ "4.13.12", "Invalid DN nameConstraints Test12",             47 ],
    [ "4.13.13", "Invalid DN nameConstraints Test13",             47 ],
    [ "4.13.14", "Valid DN nameConstraints Test14",               0 ],
    [ "4.13.15", "Invalid DN nameConstraints Test15",             48 ],
    [ "4.13.16", "Invalid DN nameConstraints Test16",             48 ],
    [ "4.13.17", "Invalid DN nameConstraints Test17",             48 ],
    [ "4.13.18", "Valid DN nameConstraints Test18",               0 ],
    [ "4.13.19", "Valid Self-Issued DN nameConstraints Test19",   0 ],
    [ "4.13.20", "Invalid Self-Issued DN nameConstraints Test20", 47 ],
    [ "4.13.21", "Valid RFC822 nameConstraints Test21",           0 ],
    [ "4.13.22", "Invalid RFC822 nameConstraints Test22",         47 ],
    [ "4.13.23", "Valid RFC822 nameConstraints Test23",           0 ],
    [ "4.13.24", "Invalid RFC822 nameConstraints Test24",         47 ],
    [ "4.13.25", "Valid RFC822 nameConstraints Test25",           0 ],
    [ "4.13.26", "Invalid RFC822 nameConstraints Test26",         48 ],
    [ "4.13.27", "Valid DN and RFC822 nameConstraints Test27",    0 ],
    [ "4.13.28", "Invalid DN and RFC822 nameConstraints Test28",  47 ],
    [ "4.13.29", "Invalid DN and RFC822 nameConstraints Test29",  47 ],
    [ "4.13.30", "Valid DNS nameConstraints Test30",              0 ],
    [ "4.13.31", "Invalid DNS nameConstraints Test31",            47 ],
    [ "4.13.32", "Valid DNS nameConstraints Test32",              0 ],
    [ "4.13.33", "Invalid DNS nameConstraints Test33",            48 ],
    [ "4.13.34", "Valid URI nameConstraints Test34",              0 ],
    [ "4.13.35", "Invalid URI nameConstraints Test35",            47 ],
    [ "4.13.36", "Valid URI nameConstraints Test36",              0 ],
    [ "4.13.37", "Invalid URI nameConstraints Test37",            48 ],
    [ "4.13.38", "Invalid DNS nameConstraints Test38",            47 ],
    [ "4.14",    "Distribution Points" ],
    [ "4.14.1",  "Valid distributionPoint Test1",                 0 ],
    [ "4.14.2",  "Invalid distributionPoint Test2",               23 ],
    [ "4.14.3",  "Invalid distributionPoint Test3",               44 ],
    [ "4.14.4",  "Valid distributionPoint Test4",                 0 ],
    [ "4.14.5",  "Valid distributionPoint Test5",                 0 ],
    [ "4.14.6",  "Invalid distributionPoint Test6",               23 ],
    [ "4.14.7",  "Valid distributionPoint Test7",                 0 ],
    [ "4.14.8",  "Invalid distributionPoint Test8",               44 ],
    [ "4.14.9",  "Invalid distributionPoint Test9",               44 ],
    [ "4.14.10", "Valid No issuingDistributionPoint Test10",      0 ],
    [ "4.14.11", "Invalid onlyContainsUserCerts CRL Test11",      44 ],
    [ "4.14.12", "Invalid onlyContainsCACerts CRL Test12",        44 ],
    [ "4.14.13", "Valid onlyContainsCACerts CRL Test13",          0 ],
    [ "4.14.14", "Invalid onlyContainsAttributeCerts Test14",     44 ],
    [ "4.14.15", "Invalid onlySomeReasons Test15",                23 ],
    [ "4.14.16", "Invalid onlySomeReasons Test16",                23 ],
    [ "4.14.17", "Invalid onlySomeReasons Test17",                3 ],
    [ "4.14.18", "Valid onlySomeReasons Test18",                  0 ],
    [ "4.14.19", "Valid onlySomeReasons Test19",                  0 ],
    [ "4.14.20", "Invalid onlySomeReasons Test20",                23 ],
    [ "4.14.21", "Invalid onlySomeReasons Test21",                23 ],
    [ "4.14.22", "Valid IDP with indirectCRL Test22",             0 ],
    [ "4.14.23", "Invalid IDP with indirectCRL Test23",           23 ],
    [ "4.14.24", "Valid IDP with indirectCRL Test24",             0 ],
    [ "4.14.25", "Valid IDP with indirectCRL Test25",             0 ],
    [ "4.14.26", "Invalid IDP with indirectCRL Test26",           44 ],
    [ "4.14.27", "Invalid cRLIssuer Test27",                      3 ],
    [ "4.14.28", "Valid cRLIssuer Test28",                        0 ],
    [ "4.14.29", "Valid cRLIssuer Test29",                        0 ],

    # Although this test is valid it has a circular dependency. As a result
    # an attempt is made to recursively check a CRL path and rejected due to
    # a CRL path validation error. PKITS notes suggest this test does not
    # need to be run due to this issue.
    [ "4.14.30", "Valid cRLIssuer Test30",                                 54 ],
    [ "4.14.31", "Invalid cRLIssuer Test31",                               23 ],
    [ "4.14.32", "Invalid cRLIssuer Test32",                               23 ],
    [ "4.14.33", "Valid cRLIssuer Test33",                                 0 ],
    [ "4.14.34", "Invalid cRLIssuer Test34",                               23 ],
    [ "4.14.35", "Invalid cRLIssuer Test35",                               44 ],
    [ "4.15",    "Delta-CRLs" ],
    [ "4.15.1",  "Invalid deltaCRLIndicator No Base Test1",                3 ],
    [ "4.15.2",  "Valid delta-CRL Test2",                                  0 ],
    [ "4.15.3",  "Invalid delta-CRL Test3",                                23 ],
    [ "4.15.4",  "Invalid delta-CRL Test4",                                23 ],
    [ "4.15.5",  "Valid delta-CRL Test5",                                  0 ],
    [ "4.15.6",  "Invalid delta-CRL Test6",                                23 ],
    [ "4.15.7",  "Valid delta-CRL Test7",                                  0 ],
    [ "4.15.8",  "Valid delta-CRL Test8",                                  0 ],
    [ "4.15.9",  "Invalid delta-CRL Test9",                                23 ],
    [ "4.15.10", "Invalid delta-CRL Test10",                               12 ],
    [ "4.16",    "Private Certificate Extensions" ],
    [ "4.16.1",  "Valid Unknown Not Critical Certificate Extension Test1", 0 ],
    [ "4.16.2",  "Invalid Unknown Critical Certificate Extension Test2",   34 ],
);


my $verbose = 1;

my $numtest = 0;
my $numfail = 0;

my $ossl = "ossl/apps/openssl";

my $ossl_cmd = "$ossl_path cms -verify -verify_retcode ";
$ossl_cmd .= "-CAfile pkitsta.pem -crl_check_all -x509_strict ";

# Check for expiry of trust anchor
system "$ossl_path x509 -inform DER -in $pkitsta -checkend 0";
if ($? == 256)
	{
	print STDERR "WARNING: using older expired data\n";
	$ossl_cmd .= "-attime 1291940972 ";
	}

$ossl_cmd .= "-policy_check -extended_crl -use_deltas -out /dev/null 2>&1 ";

system "$ossl_path x509 -inform DER -in $pkitsta -out pkitsta.pem";

die "Can't create trust anchor file" if $?;

print "Running PKITS tests:\n" if $verbose;

foreach (@testlists) {
    my $argnum = @$_;
    if ( $argnum == 2 ) {
        my ( $tnum, $title ) = @$_;
        print "$tnum $title\n" if $verbose;
    }
    elsif ( $argnum == 3 ) {
        my ( $tnum, $title, $exp_ret ) = @$_;
        my $filename = $title;
        $exp_ret += 32 if $exp_ret;
        $filename =~ tr/ -//d;
        $filename = "Signed${filename}.eml";
        if ( !-f "$pkitsdir/$filename" ) {
            print "\"$filename\" not found\n";
        }
        else {
            my $ret;
            my $test_fail = 0;
            my $errmsg    = "";
            my $cmd       = $ossl_cmd;
            $cmd .= "-in $pkitsdir/$filename -policy anyPolicy";
            my $cmdout = `$cmd`;
            $ret = $? >> 8;
            if ( $? & 0xff ) {
                $errmsg .= "Abnormal OpenSSL termination\n";
                $test_fail = 1;
            }
            if ( $exp_ret != $ret ) {
                $errmsg .= "Return code:$ret, ";
                $errmsg .= "expected $exp_ret\n";
                $test_fail = 1;
            }
            if ($test_fail) {
                print "$tnum $title : Failed!\n";
                print "Filename: $pkitsdir/$filename\n";
                print $errmsg;
                print "Command output:\n$cmdout\n";
                $numfail++;
            }
            $numtest++;
        }
    }
    elsif ( $argnum == 7 ) {
        my ( $tnum, $title, $exargs, $exp_epol, $exp_aset, $exp_uset, $exp_ret )
          = @$_;
        my $filename = $title;
        $exp_ret += 32 if $exp_ret;
        $filename =~ tr/ -//d;
        $filename = "Signed${filename}.eml";
        if ( !-f "$pkitsdir/$filename" ) {
            print "\"$filename\" not found\n";
        }
        else {
            my $ret;
            my $cmdout    = "";
            my $errmsg    = "";
            my $epol      = "";
            my $aset      = "";
            my $uset      = "";
            my $pol       = -1;
            my $test_fail = 0;
            my $cmd       = $ossl_cmd;
            $cmd .= "-in $pkitsdir/$filename $exargs -policy_print";
            @oparr = `$cmd`;
            $ret   = $? >> 8;

            if ( $? & 0xff ) {
                $errmsg .= "Abnormal OpenSSL termination\n";
                $test_fail = 1;
            }
            foreach (@oparr) {
                my $test_failed = 0;
                $cmdout .= $_;
                if (/^Require explicit Policy: (.*)$/) {
                    $epol = $1;
                }
                if (/^Authority Policies/) {
                    if (/empty/) {
                        $aset = "<empty>";
                    }
                    else {
                        $pol = 1;
                    }
                }
                $test_fail = 1 if (/leak/i);
                if (/^User Policies/) {
                    if (/empty/) {
                        $uset = "<empty>";
                    }
                    else {
                        $pol = 2;
                    }
                }
                if (/\s+Policy: (.*)$/) {
                    if ( $pol == 1 ) {
                        $aset .= ":" if $aset ne "";
                        $aset .= $1;
                    }
                    elsif ( $pol == 2 ) {
                        $uset .= ":" if $uset ne "";
                        $uset .= $1;
                    }
                }
            }

            if ( $epol ne $exp_epol ) {
                $errmsg .= "Explicit policy:$epol, ";
                $errmsg .= "expected $exp_epol\n";
                $test_fail = 1;
            }
            if ( $aset ne $exp_aset ) {
                $errmsg .= "Authority policy set :$aset, ";
                $errmsg .= "expected $exp_aset\n";
                $test_fail = 1;
            }
            if ( $uset ne $exp_uset ) {
                $errmsg .= "User policy set :$uset, ";
                $errmsg .= "expected $exp_uset\n";
                $test_fail = 1;
            }

            if ( $exp_ret != $ret ) {
                print "Return code:$ret, expected $exp_ret\n";
                $test_fail = 1;
            }

            if ($test_fail) {
                print "$tnum $title : Failed!\n";
                print "Filename: $pkitsdir/$filename\n";
                print "Command output:\n$cmdout\n";
                $numfail++;
            }
            $numtest++;
        }
    }
}

if ($numfail) {
    print "$numfail tests failed out of $numtest\n";
}
else {
    print "All Tests Successful.\n";
}

unlink "pkitsta.pem";

