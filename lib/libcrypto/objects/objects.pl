#!/usr/local/bin/perl

open (NUMIN,"$ARGV[1]") || die "Can't open number file $ARGV[1]";
$max_nid=0;
$o=0;
while(<NUMIN>)
	{
	chop;
	$o++;
	s/#.*$//;
	next if /^\s*$/;
	$_ = 'X'.$_;
	($Cname,$mynum) = split;
	$Cname =~ s/^X//;
	if (defined($nidn{$mynum}))
		{ die "$ARGV[1]:$o:There's already an object with NID ",$mynum," on line ",$order{$mynum},"\n"; }
	if (defined($nid{$Cname}))
		{ die "$ARGV[1]:$o:There's already an object with name ",$Cname," on line ",$order{$nid{$Cname}},"\n"; }
	$nid{$Cname} = $mynum;
	$nidn{$mynum} = $Cname;
	$order{$mynum} = $o;
	$max_nid = $mynum if $mynum > $max_nid;
	}
close NUMIN;

open (IN,"$ARGV[0]") || die "Can't open input file $ARGV[0]";
$Cname="";
$o=0;
while (<IN>)
	{
	chop;
	$o++;
        if (/^!module\s+(.*)$/)
		{
		$module = $1."-";
		$module =~ s/\./_/g;
		$module =~ s/-/_/g;
		}
        if (/^!global$/)
		{ $module = ""; }
	if (/^!Cname\s+(.*)$/)
		{ $Cname = $1; }
	if (/^!Alias\s+(.+?)\s+(.*)$/)
		{
		$Cname = $module.$1;
		$myoid = $2;
		$myoid = &process_oid($myoid);
		$Cname =~ s/-/_/g;
		$ordern{$o} = $Cname;
		$order{$Cname} = $o;
		$obj{$Cname} = $myoid;
		$_ = "";
		$Cname = "";
		}
	s/!.*$//;
	s/#.*$//;
	next if /^\s*$/;
	($myoid,$mysn,$myln) = split ':';
	$mysn =~ s/^\s*//;
	$mysn =~ s/\s*$//;
	$myln =~ s/^\s*//;
	$myln =~ s/\s*$//;
	$myoid =~ s/^\s*//;
	$myoid =~ s/\s*$//;
	if ($myoid ne "")
		{
		$myoid = &process_oid($myoid);
		}

	if ($Cname eq "" && !($myln =~ / /))
		{
		$Cname = $myln;
		$Cname =~ s/\./_/g;
		$Cname =~ s/-/_/g;
		if ($Cname ne "" && defined($ln{$module.$Cname}))
			{ die "objects.txt:$o:There's already an object with long name ",$ln{$module.$Cname}," on line ",$order{$module.$Cname},"\n"; }
		}
	if ($Cname eq "")
		{
		$Cname = $mysn;
		$Cname =~ s/-/_/g;
		if ($Cname ne "" && defined($sn{$module.$Cname}))
			{ die "objects.txt:$o:There's already an object with short name ",$sn{$module.$Cname}," on line ",$order{$module.$Cname},"\n"; }
		}
	if ($Cname eq "")
		{
		$Cname = $myln;
		$Cname =~ s/-/_/g;
		$Cname =~ s/\./_/g;
		$Cname =~ s/ /_/g;
		if ($Cname ne "" && defined($ln{$module.$Cname}))
			{ die "objects.txt:$o:There's already an object with long name ",$ln{$module.$Cname}," on line ",$order{$module.$Cname},"\n"; }
		}
	$Cname =~ s/\./_/g;
	$Cname =~ s/-/_/g;
	$Cname = $module.$Cname;
	$ordern{$o} = $Cname;
	$order{$Cname} = $o;
	$sn{$Cname} = $mysn;
	$ln{$Cname} = $myln;
	$obj{$Cname} = $myoid;
	if (!defined($nid{$Cname}))
		{
		$max_nid++;
		$nid{$Cname} = $max_nid;
		$nidn{$max_nid} = $Cname;
print STDERR "Added OID $Cname\n";
		}
	$Cname="";
	}
close IN;

#XXX don't modify input files
#open (NUMOUT,">$ARGV[1]") || die "Can't open output file $ARGV[1]";
#foreach (sort { $a <=> $b } keys %nidn)
#	{
#	print NUMOUT $nidn{$_},"\t\t",$_,"\n";
#	}
#close NUMOUT;

open (OUT,">$ARGV[2]") || die "Can't open output file $ARGV[2]";
print OUT <<'EOF';
/* crypto/objects/obj_mac.h */

/* THIS FILE IS GENERATED FROM objects.txt by objects.pl via the
 * following command:
 * perl objects.pl objects.txt obj_mac.num obj_mac.h
 */

/* Copyright (C) 1995-1997 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 * 
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 * 
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 * 
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#define SN_undef			"UNDEF"
#define LN_undef			"undefined"
#define NID_undef			0
#define OBJ_undef			0L

EOF

foreach (sort { $a <=> $b } keys %ordern)
	{
	$Cname=$ordern{$_};
	print OUT "#define SN_",$Cname,"\t\t\"",$sn{$Cname},"\"\n" if $sn{$Cname} ne "";
	print OUT "#define LN_",$Cname,"\t\t\"",$ln{$Cname},"\"\n" if $ln{$Cname} ne "";
	print OUT "#define NID_",$Cname,"\t\t",$nid{$Cname},"\n" if $nid{$Cname} ne "";
	print OUT "#define OBJ_",$Cname,"\t\t",$obj{$Cname},"\n" if $obj{$Cname} ne "";
	print OUT "\n";
	}

close OUT;

sub process_oid
	{
	local($oid)=@_;
	local(@a,$oid_pref);

	@a = split(/\s+/,$myoid);
	$pref_oid = "";
	$pref_sep = "";
	if (!($a[0] =~ /^[0-9]+$/))
		{
		$a[0] =~ s/-/_/g;
		if (!defined($obj{$a[0]}))
			{ die "$ARGV[0]:$o:Undefined identifier ",$a[0],"\n"; }
		$pref_oid = "OBJ_" . $a[0];
		$pref_sep = ",";
		shift @a;
		}
	$oids = join('L,',@a) . "L";
	if ($oids ne "L")
		{
		$oids = $pref_oid . $pref_sep . $oids;
		}
	else
		{
		$oids = $pref_oid;
		}
	return($oids);
	}
