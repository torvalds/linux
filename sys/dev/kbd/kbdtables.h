/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1992-1998 Sen Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef KBD_DFLT_KEYMAP

/* US iso8859 */
#define ISO_ACCENTCHARS
/*
 * Automatically generated from /usr/share/syscons/keymaps/us.iso.kbd.
 * DO NOT EDIT!
 */
static keymap_t key_map = { 0x6d, {
/*                                                         alt
 * scan                       cntrl          alt    alt   cntrl
 * code  base   shift  cntrl  shift   alt   shift  cntrl  shift    spcl flgs
 * ---------------------------------------------------------------------------
 */
/*00*/{{  NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
/*01*/{{ 0x1B,  0x1B,  0x1B,  0x1B,  0x1B,  0x1B,   DBG,  0x1B, }, 0x02,0x00 },
/*02*/{{  '1',   '!',   NOP,   NOP,   '1',   '!',   NOP,   NOP, }, 0x33,0x00 },
/*03*/{{  '2',   '@',  0x00,  0x00,   '2',   '@',  0x00,  0x00, }, 0x00,0x00 },
/*04*/{{  '3',   '#',   NOP,   NOP,   '3',   '#',   NOP,   NOP, }, 0x33,0x00 },
/*05*/{{  '4',   '$',   NOP,   NOP,   '4',   '$',   NOP,   NOP, }, 0x33,0x00 },
/*06*/{{  '5',   '%',   NOP,   NOP,   '5',   '%',   NOP,   NOP, }, 0x33,0x00 },
/*07*/{{  '6',   '^',  0x1E,  0x1E,   '6',   '^',  0x1E,  0x1E, }, 0x00,0x00 },
/*08*/{{  '7',   '&',   NOP,   NOP,   '7',   '&',   NOP,   NOP, }, 0x33,0x00 },
/*09*/{{  '8',   '*',   NOP,   NOP,   '8',   '*',   NOP,   NOP, }, 0x33,0x00 },
/*0a*/{{  '9',   '(',   NOP,   NOP,   '9',   '(',   NOP,   NOP, }, 0x33,0x00 },
/*0b*/{{  '0',   ')',   NOP,   NOP,   '0',   ')',   NOP,   NOP, }, 0x33,0x00 },
/*0c*/{{  '-',   '_',  0x1F,  0x1F,   '-',   '_',  0x1F,  0x1F, }, 0x00,0x00 },
/*0d*/{{  '=',   '+',   NOP,   NOP,   '=',   '+',   NOP,   NOP, }, 0x33,0x00 },
/*0e*/{{ 0x08,  0x08,  0x7F,  0x7F,  0x08,  0x08,  0x7F,  0x7F, }, 0x00,0x00 },
/*0f*/{{ 0x09,  BTAB,   NOP,   NOP,  0x09,  BTAB,   NOP,   NOP, }, 0x77,0x00 },
/*10*/{{  'q',   'Q',  0x11,  0x11,   'q',   'Q',  0x11,  0x11, }, 0x00,0x01 },
/*11*/{{  'w',   'W',  0x17,  0x17,   'w',   'W',  0x17,  0x17, }, 0x00,0x01 },
/*12*/{{  'e',   'E',  0x05,  0x05,   'e',   'E',  0x05,  0x05, }, 0x00,0x01 },
/*13*/{{  'r',   'R',  0x12,  0x12,   'r',   'R',  0x12,  0x12, }, 0x00,0x01 },
/*14*/{{  't',   'T',  0x14,  0x14,   't',   'T',  0x14,  0x14, }, 0x00,0x01 },
/*15*/{{  'y',   'Y',  0x19,  0x19,   'y',   'Y',  0x19,  0x19, }, 0x00,0x01 },
/*16*/{{  'u',   'U',  0x15,  0x15,   'u',   'U',  0x15,  0x15, }, 0x00,0x01 },
/*17*/{{  'i',   'I',  0x09,  0x09,   'i',   'I',  0x09,  0x09, }, 0x00,0x01 },
/*18*/{{  'o',   'O',  0x0F,  0x0F,   'o',   'O',  0x0F,  0x0F, }, 0x00,0x01 },
/*19*/{{  'p',   'P',  0x10,  0x10,   'p',   'P',  0x10,  0x10, }, 0x00,0x01 },
/*1a*/{{  '[',   '{',  0x1B,  0x1B,   '[',   '{',  0x1B,  0x1B, }, 0x00,0x00 },
/*1b*/{{  ']',   '}',  0x1D,  0x1D,   ']',   '}',  0x1D,  0x1D, }, 0x00,0x00 },
/*1c*/{{ 0x0D,  0x0D,  0x0A,  0x0A,  0x0D,  0x0D,  0x0A,  0x0A, }, 0x00,0x00 },
/*1d*/{{ LCTR,  LCTR,  LCTR,  LCTR,  LCTR,  LCTR,  LCTR,  LCTR, }, 0xFF,0x00 },
/*1e*/{{  'a',   'A',  0x01,  0x01,   'a',   'A',  0x01,  0x01, }, 0x00,0x01 },
/*1f*/{{  's',   'S',  0x13,  0x13,   's',   'S',  0x13,  0x13, }, 0x00,0x01 },
/*20*/{{  'd',   'D',  0x04,  0x04,   'd',   'D',  0x04,  0x04, }, 0x00,0x01 },
/*21*/{{  'f',   'F',  0x06,  0x06,   'f',   'F',  0x06,  0x06, }, 0x00,0x01 },
/*22*/{{  'g',   'G',  0x07,  0x07,   'g',   'G',  0x07,  0x07, }, 0x00,0x01 },
/*23*/{{  'h',   'H',  0x08,  0x08,   'h',   'H',  0x08,  0x08, }, 0x00,0x01 },
/*24*/{{  'j',   'J',  0x0A,  0x0A,   'j',   'J',  0x0A,  0x0A, }, 0x00,0x01 },
/*25*/{{  'k',   'K',  0x0B,  0x0B,   'k',   'K',  0x0B,  0x0B, }, 0x00,0x01 },
/*26*/{{  'l',   'L',  0x0C,  0x0C,   'l',   'L',  0x0C,  0x0C, }, 0x00,0x01 },
/*27*/{{  ';',   ':',   NOP,   NOP,   ';',   ':',   NOP,   NOP, }, 0x33,0x00 },
/*28*/{{ '\'',   '"',   NOP,   NOP,  '\'',   '"',   NOP,   NOP, }, 0x33,0x00 },
/*29*/{{  '`',   '~',   NOP,   NOP,   '`',   '~',   NOP,   NOP, }, 0x33,0x00 },
/*2a*/{{  LSH,   LSH,   LSH,   LSH,   LSH,   LSH,   LSH,   LSH, }, 0xFF,0x00 },
/*2b*/{{ '\\',   '|',  0x1C,  0x1C,  '\\',   '|',  0x1C,  0x1C, }, 0x00,0x00 },
/*2c*/{{  'z',   'Z',  0x1A,  0x1A,   'z',   'Z',  0x1A,  0x1A, }, 0x00,0x01 },
/*2d*/{{  'x',   'X',  0x18,  0x18,   'x',   'X',  0x18,  0x18, }, 0x00,0x01 },
/*2e*/{{  'c',   'C',  0x03,  0x03,   'c',   'C',  0x03,  0x03, }, 0x00,0x01 },
/*2f*/{{  'v',   'V',  0x16,  0x16,   'v',   'V',  0x16,  0x16, }, 0x00,0x01 },
/*30*/{{  'b',   'B',  0x02,  0x02,   'b',   'B',  0x02,  0x02, }, 0x00,0x01 },
/*31*/{{  'n',   'N',  0x0E,  0x0E,   'n',   'N',  0x0E,  0x0E, }, 0x00,0x01 },
/*32*/{{  'm',   'M',  0x0D,  0x0D,   'm',   'M',  0x0D,  0x0D, }, 0x00,0x01 },
/*33*/{{  ',',   '<',   NOP,   NOP,   ',',   '<',   NOP,   NOP, }, 0x33,0x00 },
/*34*/{{  '.',   '>',   NOP,   NOP,   '.',   '>',   NOP,   NOP, }, 0x33,0x00 },
/*35*/{{  '/',   '?',   NOP,   NOP,   '/',   '?',   NOP,   NOP, }, 0x33,0x00 },
/*36*/{{  RSH,   RSH,   RSH,   RSH,   RSH,   RSH,   RSH,   RSH, }, 0xFF,0x00 },
/*37*/{{  '*',   '*',   '*',   '*',   '*',   '*',   '*',   '*', }, 0x00,0x00 },
/*38*/{{ LALT,  LALT,  LALT,  LALT,  LALT,  LALT,  LALT,  LALT, }, 0xFF,0x00 },
/*39*/{{  ' ',   ' ',  0x00,   ' ',   ' ',   ' ',  SUSP,   ' ', }, 0x02,0x00 },
/*3a*/{{  CLK,   CLK,   CLK,   CLK,   CLK,   CLK,   CLK,   CLK, }, 0xFF,0x00 },
/*3b*/{{ F( 1), F(13), F(25), F(37), S( 1), S(11), S( 1), S(11),}, 0xFF,0x00 },
/*3c*/{{ F( 2), F(14), F(26), F(38), S( 2), S(12), S( 2), S(12),}, 0xFF,0x00 },
/*3d*/{{ F( 3), F(15), F(27), F(39), S( 3), S(13), S( 3), S(13),}, 0xFF,0x00 },
/*3e*/{{ F( 4), F(16), F(28), F(40), S( 4), S(14), S( 4), S(14),}, 0xFF,0x00 },
/*3f*/{{ F( 5), F(17), F(29), F(41), S( 5), S(15), S( 5), S(15),}, 0xFF,0x00 },
/*40*/{{ F( 6), F(18), F(30), F(42), S( 6), S(16), S( 6), S(16),}, 0xFF,0x00 },
/*41*/{{ F( 7), F(19), F(31), F(43), S( 7), S( 7), S( 7), S( 7),}, 0xFF,0x00 },
/*42*/{{ F( 8), F(20), F(32), F(44), S( 8), S( 8), S( 8), S( 8),}, 0xFF,0x00 },
/*43*/{{ F( 9), F(21), F(33), F(45), S( 9), S( 9), S( 9), S( 9),}, 0xFF,0x00 },
/*44*/{{ F(10), F(22), F(34), F(46), S(10), S(10), S(10), S(10),}, 0xFF,0x00 },
/*45*/{{  NLK,   NLK,   NLK,   NLK,   NLK,   NLK,   NLK,   NLK, }, 0xFF,0x00 },
/*46*/{{  SLK,   SLK,   SLK,   SLK,   SLK,   SLK,   SLK,   SLK, }, 0xFF,0x00 },
/*47*/{{ F(49),  '7',   '7',   '7',   '7',   '7',   '7',   '7', }, 0x80,0x02 },
/*48*/{{ F(50),  '8',   '8',   '8',   '8',   '8',   '8',   '8', }, 0x80,0x02 },
/*49*/{{ F(51),  '9',   '9',   '9',   '9',   '9',   '9',   '9', }, 0x80,0x02 },
/*4a*/{{ F(52),  '-',   '-',   '-',   '-',   '-',   '-',   '-', }, 0x80,0x02 },
/*4b*/{{ F(53),  '4',   '4',   '4',   '4',   '4',   '4',   '4', }, 0x80,0x02 },
/*4c*/{{ F(54),  '5',   '5',   '5',   '5',   '5',   '5',   '5', }, 0x80,0x02 },
/*4d*/{{ F(55),  '6',   '6',   '6',   '6',   '6',   '6',   '6', }, 0x80,0x02 },
/*4e*/{{ F(56),  '+',   '+',   '+',   '+',   '+',   '+',   '+', }, 0x80,0x02 },
/*4f*/{{ F(57),  '1',   '1',   '1',   '1',   '1',   '1',   '1', }, 0x80,0x02 },
/*50*/{{ F(58),  '2',   '2',   '2',   '2',   '2',   '2',   '2', }, 0x80,0x02 },
/*51*/{{ F(59),  '3',   '3',   '3',   '3',   '3',   '3',   '3', }, 0x80,0x02 },
/*52*/{{ F(60),  '0',   '0',   '0',   '0',   '0',   '0',   '0', }, 0x80,0x02 },
/*53*/{{ 0x7F,   '.',   '.',   '.',   '.',   '.',   RBT,   RBT, }, 0x03,0x02 },
/*54*/{{  NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
/*55*/{{  NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
/*56*/{{  NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
/*57*/{{ F(11), F(23), F(35), F(47), S(11), S(11), S(11), S(11),}, 0xFF,0x00 },
/*58*/{{ F(12), F(24), F(36), F(48), S(12), S(12), S(12), S(12),}, 0xFF,0x00 },
/*59*/{{ 0x0D,  0x0D,  0x0A,  0x0A,  0x0D,  0x0D,  0x0A,  0x0A, }, 0x00,0x00 },
/*5a*/{{ RCTR,  RCTR,  RCTR,  RCTR,  RCTR,  RCTR,  RCTR,  RCTR, }, 0xFF,0x00 },
/*5b*/{{  '/',   '/',   '/',   '/',   '/',   '/',   '/',   '/', }, 0x00,0x02 },
/*5c*/{{ NEXT,  PREV,   DBG,   DBG,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
/*5d*/{{ RALT,  RALT,  RALT,  RALT,  RALT,  RALT,  RALT,  RALT, }, 0xFF,0x00 },
/*5e*/{{ F(49), F(49), F(49), F(49), F(49), F(49), F(49), F(49),}, 0xFF,0x00 },
/*5f*/{{ F(50), F(50), F(50), F(50), F(50), F(50), F(50), F(50),}, 0xFF,0x00 },
/*60*/{{ F(51), F(51), F(51), F(51), F(51), F(51), F(51), F(51),}, 0xFF,0x00 },
/*61*/{{ F(53), F(53), F(53), F(53), F(53), F(53), F(53), F(53),}, 0xFF,0x00 },
/*62*/{{ F(55), F(55), F(55), F(55), F(55), F(55), F(55), F(55),}, 0xFF,0x00 },
/*63*/{{ F(57), F(57), F(57), F(57), F(57), F(57), F(57), F(57),}, 0xFF,0x00 },
/*64*/{{ F(58), F(58), F(58), F(58), F(58), F(58), F(58), F(58),}, 0xFF,0x00 },
/*65*/{{ F(59), F(59), F(59), F(59), F(59), F(59), F(59), F(59),}, 0xFF,0x00 },
/*66*/{{ F(60),PASTE,  F(60), F(60), F(60), F(60), F(60), F(60),}, 0xFF,0x00 },
/*67*/{{ F(61), F(61), F(61), F(61), F(61), F(61),  RBT,  F(61),}, 0xFF,0x00 },
/*68*/{{  SLK,  SPSC,   SLK,  SPSC,  SUSP,   NOP,  SUSP,   NOP, }, 0xFF,0x00 },
/*69*/{{ F(62), F(62), F(62), F(62), F(62), F(62), F(62), F(62),}, 0xFF,0x00 },
/*6a*/{{ F(63), F(63), F(63), F(63), F(63), F(63), F(63), F(63),}, 0xFF,0x00 },
/*6b*/{{ F(64), F(64), F(64), F(64), F(64), F(64), F(64), F(64),}, 0xFF,0x00 },
/*6c*/{{  NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP,   NOP, }, 0xFF,0x00 },
} };

#if defined(NO_ACCENTCHARS)
static accentmap_t accent_map = { 0,		/* empty accent map */
  {
    { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, 
    { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 },
  }
};
#endif

#if defined(ISO_ACCENTCHARS)
static accentmap_t accent_map = { 15,	      /* iso8859 accent map */
  {
    /* dgra=0 */
    {  '`', { {  'a',0xe0 }, {  'A',0xc0 }, {  'e',0xe8 }, {  'E',0xc8 },
	      {  'i',0xec }, {  'I',0xcc }, {  'o',0xf2 }, {  'O',0xd2 },
	      {  'u',0xf9 }, {  'U',0xd9 }, }, },
    /* dacu=1 */
    { 0xb4, { {  'a',0xe1 }, {  'A',0xc1 }, {  'e',0xe9 }, {  'E',0xc9 },
	      {  'i',0xed }, {  'I',0xcd }, {  'o',0xf3 }, {  'O',0xd3 },
	      {  'u',0xfa }, {  'U',0xda }, {  'y',0xfd }, {  'Y',0xdd }, }, },
    /* dcir=2 */
    {  '^', { {  'a',0xe2 }, {  'A',0xc2 }, {  'e',0xea }, {  'E',0xca },
	      {  'i',0xee }, {  'I',0xce }, {  'o',0xf4 }, {  'O',0xd4 },
	      {  'u',0xfb }, {  'U',0xdb }, }, },
    /* dtil=3 */
    {  '~', { {  'a',0xe3 }, {  'A',0xc3 }, {  'n',0xf1 }, {  'N',0xd1 },
	      {  'o',0xf5 }, {  'O',0xd5 }, }, },
    /* dmac=4 */
    { 0 }, 
    /* dbre=5 */
    { 0 }, 
    /* ddot=6 */
    { 0 }, 
    /* duml=7 */
    { 0xa8, { {  'a',0xe4 }, {  'A',0xc4 }, {  'e',0xeb }, {  'E',0xcb },
	      {  'i',0xef }, {  'I',0xcf }, {  'o',0xf6 }, {  'O',0xd6 },
	      {  'u',0xfc }, {  'U',0xdc }, {  'y',0xff }, }, },
    /* dsla=8 */
    { 0 }, 
    /* drin=9 */
    { 0xb0, { {  'a',0xe5 }, {  'A',0xc5 }, }, },
    /* dced=10 */
    { 0xb8, { {  'c',0xe7 }, {  'C',0xc7 }, }, },
    /* dapo=11 */
    { 0 }, 
    /* ddac=12 */
    { 0 }, 
    /* dogo=13 */
    { 0 }, 
    /* dcar=14 */
    { 0 }, 
  }
};
#endif /* ISO_ACCENTCHARS */

#endif /* !KBD_DFLT_KEYMAP */

static fkeytab_t fkey_tab[96] = {
/* 01-04 */	{"\033[M", 3}, {"\033[N", 3}, {"\033[O", 3}, {"\033[P", 3},
/* 05-08 */	{"\033[Q", 3}, {"\033[R", 3}, {"\033[S", 3}, {"\033[T", 3},
/* 09-12 */	{"\033[U", 3}, {"\033[V", 3}, {"\033[W", 3}, {"\033[X", 3},
/* 13-16 */	{"\033[Y", 3}, {"\033[Z", 3}, {"\033[a", 3}, {"\033[b", 3},
/* 17-20 */	{"\033[c", 3}, {"\033[d", 3}, {"\033[e", 3}, {"\033[f", 3},
/* 21-24 */	{"\033[g", 3}, {"\033[h", 3}, {"\033[i", 3}, {"\033[j", 3},
/* 25-28 */	{"\033[k", 3}, {"\033[l", 3}, {"\033[m", 3}, {"\033[n", 3},
/* 29-32 */	{"\033[o", 3}, {"\033[p", 3}, {"\033[q", 3}, {"\033[r", 3},
/* 33-36 */	{"\033[s", 3}, {"\033[t", 3}, {"\033[u", 3}, {"\033[v", 3},
/* 37-40 */	{"\033[w", 3}, {"\033[x", 3}, {"\033[y", 3}, {"\033[z", 3},
/* 41-44 */	{"\033[@", 3}, {"\033[[", 3}, {"\033[\\",3}, {"\033[]", 3},
/* 45-48 */     {"\033[^", 3}, {"\033[_", 3}, {"\033[`", 3}, {"\033[{", 3},
/* 49-52 */	{"\033[H", 3}, {"\033[A", 3}, {"\033[I", 3}, {"-"     , 1},
/* 53-56 */	{"\033[D", 3}, {"\033[E", 3}, {"\033[C", 3}, {"+"     , 1},
/* 57-60 */	{"\033[F", 3}, {"\033[B", 3}, {"\033[G", 3}, {"\033[L", 3},
/* 61-64 */	{"\177", 1},   {"\033[J", 3}, {"\033[~", 3}, {"\033[}", 3},
/* 65-68 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 69-72 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 73-76 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 77-80 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 81-84 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 85-88 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 89-92 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}      ,
/* 93-96 */	{"", 0}      , {"", 0}      , {"", 0}      , {"", 0}
};
