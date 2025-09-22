#!/usr/local/bin/perl

$NUMBER=0x01;
$UPPER=0x02;
$LOWER=0x04;
$UNDER=0x100;
$PUNCTUATION=0x200;
$WS=0x10;
$ESC=0x20;
$QUOTE=0x40;
$DQUOTE=0x400;
$COMMENT=0x80;
$FCOMMENT=0x800;
$EOF=0x08;
$HIGHBIT=0x1000;

foreach (0 .. 255)
	{
	$v=0;
	$c=sprintf("%c",$_);
	$v|=$NUMBER	if ($c =~ /[0-9]/);
	$v|=$UPPER	if ($c =~ /[A-Z]/);
	$v|=$LOWER	if ($c =~ /[a-z]/);
	$v|=$UNDER	if ($c =~ /_/);
	$v|=$PUNCTUATION if ($c =~ /[!\.%&\*\+,\/;\?\@\^\~\|-]/);
	$v|=$WS		if ($c =~ /[ \t\r\n]/);
	$v|=$ESC	if ($c =~ /\\/);
	$v|=$QUOTE	if ($c =~ /['`"]/); # for emacs: "`'}/)
	$v|=$COMMENT	if ($c =~ /\#/);
	$v|=$EOF	if ($c =~ /\0/);
	$v|=$HIGHBIT	if ($c =~/[\x80-\xff]/);

	push(@V_def,$v);
	}

foreach (0 .. 255)
	{
	$v=0;
	$c=sprintf("%c",$_);
	$v|=$NUMBER	if ($c =~ /[0-9]/);
	$v|=$UPPER	if ($c =~ /[A-Z]/);
	$v|=$LOWER	if ($c =~ /[a-z]/);
	$v|=$UNDER	if ($c =~ /_/);
	$v|=$PUNCTUATION if ($c =~ /[!\.%&\*\+,\/;\?\@\^\~\|-]/);
	$v|=$WS		if ($c =~ /[ \t\r\n]/);
	$v|=$DQUOTE	if ($c =~ /["]/); # for emacs: "}/)
	$v|=$FCOMMENT	if ($c =~ /;/);
	$v|=$EOF	if ($c =~ /\0/);
	$v|=$HIGHBIT	if ($c =~/[\x80-\xff]/);

	push(@V_w32,$v);
	}

print <<"EOF";
/* crypto/conf/conf_def.h */
/* Copyright (C) 1995-1998 Eric Young (eay\@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay\@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 * 
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh\@cryptsoft.com).
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
 *     Eric Young (eay\@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from 
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh\@cryptsoft.com)"
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

/* THIS FILE WAS AUTOMAGICALLY GENERATED!
   Please modify and use keysets.pl to regenerate it. */

#define CONF_NUMBER		$NUMBER
#define CONF_UPPER		$UPPER
#define CONF_LOWER		$LOWER
#define CONF_UNDER		$UNDER
#define CONF_PUNCTUATION	$PUNCTUATION
#define CONF_WS			$WS
#define CONF_ESC		$ESC
#define CONF_QUOTE		$QUOTE
#define CONF_DQUOTE		$DQUOTE
#define CONF_COMMENT		$COMMENT
#define CONF_FCOMMENT		$FCOMMENT
#define CONF_EOF		$EOF
#define CONF_HIGHBIT		$HIGHBIT
#define CONF_ALPHA		(CONF_UPPER|CONF_LOWER)
#define CONF_ALPHA_NUMERIC	(CONF_ALPHA|CONF_NUMBER|CONF_UNDER)
#define CONF_ALPHA_NUMERIC_PUNCT (CONF_ALPHA|CONF_NUMBER|CONF_UNDER| \\
					CONF_PUNCTUATION)

#define KEYTYPES(c)		((unsigned short *)((c)->meth_data))
#define IS_COMMENT(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_COMMENT)
#define IS_FCOMMENT(c,a)	(KEYTYPES(c)[(a)&0xff]&CONF_FCOMMENT)
#define IS_EOF(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_EOF)
#define IS_ESC(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_ESC)
#define IS_NUMBER(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_NUMBER)
#define IS_WS(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_WS)
#define IS_ALPHA_NUMERIC(c,a)	(KEYTYPES(c)[(a)&0xff]&CONF_ALPHA_NUMERIC)
#define IS_ALPHA_NUMERIC_PUNCT(c,a) \\
				(KEYTYPES(c)[(a)&0xff]&CONF_ALPHA_NUMERIC_PUNCT)
#define IS_QUOTE(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_QUOTE)
#define IS_DQUOTE(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_DQUOTE)
#define IS_HIGHBIT(c,a)		(KEYTYPES(c)[(a)&0xff]&CONF_HIGHBIT)


EOF

print "static unsigned short CONF_type_default[256]={";

for ($i=0; $i<256; $i++)
	{
	print "\n\t" if ($i % 8) == 0;
	printf "0x%04X,",$V_def[$i];
	}

print "\n\t};\n\n";

print "static unsigned short CONF_type_win32[256]={";

for ($i=0; $i<256; $i++)
	{
	print "\n\t" if ($i % 8) == 0;
	printf "0x%04X,",$V_w32[$i];
	}

print "\n\t};\n\n";
