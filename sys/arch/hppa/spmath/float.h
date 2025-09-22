/*	$OpenBSD: float.h,v 1.8 2012/06/25 16:55:13 deraadt Exp $	*/
/*
  (c) Copyright 1986 HEWLETT-PACKARD COMPANY
  To anyone who acknowledges that this file is provided "AS IS"
  without any express or implied warranty:
      permission to use, copy, modify, and distribute this file
  for any purpose is hereby granted without fee, provided that
  the above copyright notice and this notice appears in all
  copies, and that the name of Hewlett-Packard Company not be
  used in advertising or publicity pertaining to distribution
  of the software without specific, written prior permission.
  Hewlett-Packard Company makes no representations about the
  suitability of this software for any purpose.
*/
/* @(#)float.h: Revision: 2.14.88.2 Date: 93/12/08 13:27:42 */

#include "fpbits.h"
#include "hppa.h"

/*
 * Declare the basic structures for the 3 different
 * floating-point precisions.
 *
 * Single number
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|       exp     |               mantissa                      |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define	Sall(object) (object)
#define	Ssign(object) Bitfield_extract( 0,  1,object)
#define	Ssignedsign(object) Bitfield_signed_extract( 0,  1,object)
#define	Sexponent(object) Bitfield_extract( 1,  8,object)
#define	Smantissa(object) Bitfield_mask( 9, 23,object)
#define	Ssignaling(object) Bitfield_extract( 9,  1,object)
#define	Ssignalingnan(object) Bitfield_extract( 1,  9,object)
#define	Shigh2mantissa(object) Bitfield_extract( 9,  2,object)
#define	Sexponentmantissa(object) Bitfield_mask( 1, 31,object)
#define	Ssignexponent(object) Bitfield_extract( 0,  9,object)
#define	Shidden(object) Bitfield_extract( 8,  1,object)
#define	Shiddenoverflow(object) Bitfield_extract( 7,  1,object)
#define	Shiddenhigh7mantissa(object) Bitfield_extract( 8,  8,object)
#define	Shiddenhigh3mantissa(object) Bitfield_extract( 8,  4,object)
#define	Slow(object) Bitfield_mask( 31,  1,object)
#define	Slow4(object) Bitfield_mask( 28,  4,object)
#define	Slow31(object) Bitfield_mask( 1, 31,object)
#define	Shigh31(object) Bitfield_extract( 0, 31,object)
#define	Ssignedhigh31(object) Bitfield_signed_extract( 0, 31,object)
#define	Shigh4(object) Bitfield_extract( 0,  4,object)
#define	Sbit24(object) Bitfield_extract( 24,  1,object)
#define	Sbit28(object) Bitfield_extract( 28,  1,object)
#define	Sbit29(object) Bitfield_extract( 29,  1,object)
#define	Sbit30(object) Bitfield_extract( 30,  1,object)
#define	Sbit31(object) Bitfield_mask( 31,  1,object)

#define Deposit_ssign(object,value) Bitfield_deposit(value,0,1,object)
#define Deposit_sexponent(object,value) Bitfield_deposit(value,1,8,object)
#define Deposit_smantissa(object,value) Bitfield_deposit(value,9,23,object)
#define Deposit_shigh2mantissa(object,value) Bitfield_deposit(value,9,2,object)
#define Deposit_sexponentmantissa(object,value) \
    Bitfield_deposit(value,1,31,object)
#define Deposit_ssignexponent(object,value) Bitfield_deposit(value,0,9,object)
#define Deposit_slow(object,value) Bitfield_deposit(value,31,1,object)
#define Deposit_shigh4(object,value) Bitfield_deposit(value,0,4,object)

#define	Is_ssign(object) Bitfield_mask( 0,  1,object)
#define	Is_ssignaling(object) Bitfield_mask( 9,  1,object)
#define	Is_shidden(object) Bitfield_mask( 8,  1,object)
#define	Is_shiddenoverflow(object) Bitfield_mask( 7,  1,object)
#define	Is_slow(object) Bitfield_mask( 31,  1,object)
#define	Is_sbit24(object) Bitfield_mask( 24,  1,object)
#define	Is_sbit28(object) Bitfield_mask( 28,  1,object)
#define	Is_sbit29(object) Bitfield_mask( 29,  1,object)
#define	Is_sbit30(object) Bitfield_mask( 30,  1,object)
#define	Is_sbit31(object) Bitfield_mask( 31,  1,object)

/*
 * Double number.
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|       exponent      |          mantissa part 1              |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    mantissa part 2                            |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define Dallp1(object) (object)
#define Dsign(object) Bitfield_extract( 0,  1,object)
#define Dsignedsign(object) Bitfield_signed_extract( 0,  1,object)
#define Dexponent(object) Bitfield_extract( 1,  11,object)
#define Dmantissap1(object) Bitfield_mask( 12, 20,object)
#define Dsignaling(object) Bitfield_extract( 12,  1,object)
#define Dsignalingnan(object) Bitfield_extract( 1,  12,object)
#define Dhigh2mantissa(object) Bitfield_extract( 12,  2,object)
#define Dexponentmantissap1(object) Bitfield_mask( 1, 31,object)
#define Dsignexponent(object) Bitfield_extract( 0, 12,object)
#define Dhidden(object) Bitfield_extract( 11,  1,object)
#define Dhiddenoverflow(object) Bitfield_extract( 10,  1,object)
#define Dhiddenhigh7mantissa(object) Bitfield_extract( 11,  8,object)
#define Dhiddenhigh3mantissa(object) Bitfield_extract( 11,  4,object)
#define Dlowp1(object) Bitfield_mask( 31,  1,object)
#define Dlow31p1(object) Bitfield_mask( 1, 31,object)
#define Dhighp1(object) Bitfield_extract( 0,  1,object)
#define Dhigh4p1(object) Bitfield_extract( 0,  4,object)
#define Dhigh31p1(object) Bitfield_extract( 0, 31,object)
#define Dsignedhigh31p1(object) Bitfield_signed_extract( 0, 31,object)
#define Dbit3p1(object) Bitfield_extract( 3,  1,object)

#define Deposit_dsign(object,value) Bitfield_deposit(value,0,1,object)
#define Deposit_dexponent(object,value) Bitfield_deposit(value,1,11,object)
#define Deposit_dmantissap1(object,value) Bitfield_deposit(value,12,20,object)
#define Deposit_dhigh2mantissa(object,value) Bitfield_deposit(value,12,2,object)
#define Deposit_dexponentmantissap1(object,value) \
    Bitfield_deposit(value,1,31,object)
#define Deposit_dsignexponent(object,value) Bitfield_deposit(value,0,12,object)
#define Deposit_dlowp1(object,value) Bitfield_deposit(value,31,1,object)
#define Deposit_dhigh4p1(object,value) Bitfield_deposit(value,0,4,object)

#define Is_dsign(object) Bitfield_mask( 0,  1,object)
#define Is_dsignaling(object) Bitfield_mask( 12,  1,object)
#define Is_dhidden(object) Bitfield_mask( 11,  1,object)
#define Is_dhiddenoverflow(object) Bitfield_mask( 10,  1,object)
#define Is_dlowp1(object) Bitfield_mask( 31,  1,object)
#define Is_dhighp1(object) Bitfield_mask( 0,  1,object)
#define Is_dbit3p1(object) Bitfield_mask( 3,  1,object)

#define Dallp2(object) (object)
#define Dmantissap2(object) (object)
#define Dlowp2(object) Bitfield_mask( 31,  1,object)
#define Dlow4p2(object) Bitfield_mask( 28,  4,object)
#define Dlow31p2(object) Bitfield_mask( 1, 31,object)
#define Dhighp2(object) Bitfield_extract( 0,  1,object)
#define Dhigh31p2(object) Bitfield_extract( 0, 31,object)
#define Dbit2p2(object) Bitfield_extract( 2,  1,object)
#define Dbit3p2(object) Bitfield_extract( 3,  1,object)
#define Dbit21p2(object) Bitfield_extract( 21,  1,object)
#define Dbit28p2(object) Bitfield_extract( 28,  1,object)
#define Dbit29p2(object) Bitfield_extract( 29,  1,object)
#define Dbit30p2(object) Bitfield_extract( 30,  1,object)
#define Dbit31p2(object) Bitfield_mask( 31,  1,object)

#define Deposit_dlowp2(object,value) Bitfield_deposit(value,31,1,object)

#define Is_dlowp2(object) Bitfield_mask( 31,  1,object)
#define Is_dhighp2(object) Bitfield_mask( 0,  1,object)
#define Is_dbit2p2(object) Bitfield_mask( 2,  1,object)
#define Is_dbit3p2(object) Bitfield_mask( 3,  1,object)
#define Is_dbit21p2(object) Bitfield_mask( 21,  1,object)
#define Is_dbit28p2(object) Bitfield_mask( 28,  1,object)
#define Is_dbit29p2(object) Bitfield_mask( 29,  1,object)
#define Is_dbit30p2(object) Bitfield_mask( 30,  1,object)
#define Is_dbit31p2(object) Bitfield_mask( 31,  1,object)

/*
 * Quad number.
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|          exponent           |      mantissa part 1          |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    mantissa part 2                            |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    mantissa part 3                            |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    mantissa part 4                            |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
typedef struct
    {
    union
	{
	struct { unsigned qallp1; } u_qallp1;
/* Not needed for now...
	Bitfield_extract( 0,  1,u_qsign,qsign)
	Bitfield_signed_extract( 0,  1,u_qsignedsign,qsignedsign)
	Bitfield_extract( 1, 15,u_qexponent,qexponent)
	Bitfield_extract(16, 16,u_qmantissap1,qmantissap1)
	Bitfield_extract(16,  1,u_qsignaling,qsignaling)
	Bitfield_extract(1,  16,u_qsignalingnan,qsignalingnan)
	Bitfield_extract(16,  2,u_qhigh2mantissa,qhigh2mantissa)
	Bitfield_extract( 1, 31,u_qexponentmantissap1,qexponentmantissap1)
	Bitfield_extract( 0, 16,u_qsignexponent,qsignexponent)
	Bitfield_extract(15,  1,u_qhidden,qhidden)
	Bitfield_extract(14,  1,u_qhiddenoverflow,qhiddenoverflow)
	Bitfield_extract(15,  8,u_qhiddenhigh7mantissa,qhiddenhigh7mantissa)
	Bitfield_extract(15,  4,u_qhiddenhigh3mantissa,qhiddenhigh3mantissa)
	Bitfield_extract(31,  1,u_qlowp1,qlowp1)
	Bitfield_extract( 1, 31,u_qlow31p1,qlow31p1)
	Bitfield_extract( 0,  1,u_qhighp1,qhighp1)
	Bitfield_extract( 0,  4,u_qhigh4p1,qhigh4p1)
	Bitfield_extract( 0, 31,u_qhigh31p1,qhigh31p1)
  */
	} quad_u1;
    union
	{
	struct { unsigned qallp2; } u_qallp2;
  /* Not needed for now...
	Bitfield_extract(31,  1,u_qlowp2,qlowp2)
	Bitfield_extract( 1, 31,u_qlow31p2,qlow31p2)
	Bitfield_extract( 0,  1,u_qhighp2,qhighp2)
	Bitfield_extract( 0, 31,u_qhigh31p2,qhigh31p2)
   */
	} quad_u2;
    union
	{
	struct { unsigned qallp3; } u_qallp3;
  /* Not needed for now...
	Bitfield_extract(31,  1,u_qlowp3,qlowp3)
	Bitfield_extract( 1, 31,u_qlow31p3,qlow31p3)
	Bitfield_extract( 0,  1,u_qhighp3,qhighp3)
	Bitfield_extract( 0, 31,u_qhigh31p3,qhigh31p3)
   */
	} quad_u3;
    union
	{
	struct { unsigned qallp4; } u_qallp4;
    /* Not need for now...
	Bitfield_extract(31,  1,u_qlowp4,qlowp4)
	Bitfield_extract( 1, 31,u_qlow31p4,qlow31p4)
	Bitfield_extract( 0,  1,u_qhighp4,qhighp4)
	Bitfield_extract( 0, 31,u_qhigh31p4,qhigh31p4)
     */
	} quad_u4;
    } quad_floating_point;

/* Extension - An additional structure to hold the guard, round and
 *             sticky bits during computations.
 */
#define Extall(object) (object)
#define Extsign(object) Bitfield_extract( 0,  1,object)
#define Exthigh31(object) Bitfield_extract( 0, 31,object)
#define Extlow31(object) Bitfield_extract( 1, 31,object)
#define Extlow(object) Bitfield_extract( 31,  1,object)

/*
 * Declare the basic structures for the 3 different
 * fixed-point precisions.
 *
 * Single number
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|                    integer                                  |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
typedef int sgl_integer;

/*
 * Double number.
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|                     high integer                            |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                       low integer                             |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
struct dint {
	int  wd0;
	unsigned int wd1;
};

struct dblwd {
	unsigned int wd0;
	unsigned int wd1;
};

/*
 * Quad number.
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |s|                  integer part1                              |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    integer part 2                             |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    integer part 3                             |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                    integer part 4                             |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */

struct quadwd {
	int  wd0;
	unsigned int wd1;
	unsigned int wd2;
	unsigned int wd3;
};

typedef struct quadwd quad_integer;


/* useful typedefs */
typedef int sgl_floating_point;
typedef struct dblwd dbl_floating_point;
typedef struct dint dbl_integer;

/*
 * Define the different precisions' parameters.
 */
#define SGL_BITLENGTH 32
#define SGL_EMAX 127
#define SGL_BIAS 127
#define SGL_WRAP 192
#define SGL_INFINITY_EXPONENT (SGL_EMAX+SGL_BIAS+1)
#define SGL_THRESHOLD 32
#define SGL_EXP_LENGTH 8
#define SGL_P 24

#define DBL_BITLENGTH 64
#define DBL_EMAX 1023
#define DBL_BIAS 1023
#define DBL_WRAP 1536
#define DBL_INFINITY_EXPONENT (DBL_EMAX+DBL_BIAS+1)
#define DBL_THRESHOLD 64
#define DBL_EXP_LENGTH 11
#define DBL_P 53

#define QUAD_BITLENGTH 128
#define QUAD_EMAX 16383
#define QUAD_BIAS 16383
#define QUAD_WRAP 24576
#define QUAD_INFINITY_EXPONENT (QUAD_EMAX+QUAD_BIAS+1)
#define QUAD_P 113

/* Boolean Values etc. */
#define FALSE 0
#define TRUE (!FALSE)
#define NOT !
#define XOR ^

/* Declare status register equivalent to FPUs architecture.
 *
 *  0 1 2 3 4 5 6 7 8 910 1 2 3 4 5 6 7 8 920 1 2 3 4 5 6 7 8 930 1
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |V|Z|O|U|I|C|  rsv  |  model    | version |RM |rsv|T|r|V|Z|O|U|I|
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define Cbit(object) Bitfield_extract( 5, 1,object)
#define Tbit(object) Bitfield_extract( 25, 1,object)
#define Roundingmode(object) Bitfield_extract( 21, 2,object)
#define Invalidtrap(object) Bitfield_extract( 27, 1,object)
#define Divisionbyzerotrap(object) Bitfield_extract( 28, 1,object)
#define Overflowtrap(object) Bitfield_extract( 29, 1,object)
#define Underflowtrap(object) Bitfield_extract( 30, 1,object)
#define Inexacttrap(object) Bitfield_extract( 31, 1,object)
#define Invalidflag(object) Bitfield_extract( 0, 1,object)
#define Divisionbyzeroflag(object) Bitfield_extract( 1, 1,object)
#define Overflowflag(object) Bitfield_extract( 2, 1,object)
#define Underflowflag(object) Bitfield_extract( 3, 1,object)
#define Inexactflag(object) Bitfield_extract( 4, 1,object)
#define Allflags(object) Bitfield_extract( 0, 5,object)

/* Definitions relevant to the status register */

/* Rounding Modes */
#define ROUNDNEAREST 0
#define ROUNDZERO    1
#define ROUNDPLUS    2
#define ROUNDMINUS   3

/* Exceptions */
#define NOEXCEPTION		0x0
#define INVALIDEXCEPTION	0x20
#define DIVISIONBYZEROEXCEPTION	0x10
#define OVERFLOWEXCEPTION	0x08
#define UNDERFLOWEXCEPTION	0x04
#define INEXACTEXCEPTION	0x02
#define UNIMPLEMENTEDEXCEPTION	0x01

/* Declare exception registers equivalent to FPUs architecture
 *
 *  0 1 2 3 4 5 6 7 8 910 1 2 3 4 5 6 7 8 920 1 2 3 4 5 6 7 8 930 1
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |excepttype |  r1     | r2/ext  |  operation  |parm |n| t/cond  |
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define Allexception(object) (object)
#define Exceptiontype(object) Bitfield_extract( 0, 6,object)
#define Instructionfield(object) Bitfield_mask( 6,26,object)
#define Parmfield(object) Bitfield_extract( 23, 3,object)
#define Rabit(object) Bitfield_extract( 24, 1,object)
#define Ibit(object) Bitfield_extract( 25, 1,object)

#define Set_exceptiontype(object,value) Bitfield_deposit(value, 0, 6,object)
#define Set_parmfield(object,value) Bitfield_deposit(value, 23, 3,object)
#define Set_exceptiontype_and_instr_field(exception,instruction,object) \
    object = ((exception) << 26) | (instruction)

/* Declare the condition field
 *
 *  0 1 2 3 4 5 6 7 8 910 1 2 3 4 5 6 7 8 920 1 2 3 4 5 6 7 8 930 1
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 * |                                                     |G|L|E|U|X|
 * +-------+-------+-------+-------+-------+-------+-------+-------+
 */
#define Allexception(object) (object)
#define Greaterthanbit(object) Bitfield_extract( 27, 1,object)
#define Lessthanbit(object) Bitfield_extract( 28, 1,object)
#define Equalbit(object) Bitfield_extract( 29, 1,object)
#define Unorderedbit(object) Bitfield_extract( 30, 1,object)
#define Exceptionbit(object) Bitfield_extract( 31, 1,object)

/* An alias name for the status register */
#define Fpustatus_register (*status)

/**************************************************
 * Status register referencing and manipulation.  *
 **************************************************/

/* Rounding mode */
#define Rounding_mode()  Roundingmode(Fpustatus_register)
#define Is_rounding_mode(rmode) \
    (Roundingmode(Fpustatus_register) == rmode)
#define Set_rounding_mode(value) \
    Bitfield_deposit(value,21,2,Fpustatus_register)

/* Boolean testing of the trap enable bits */
#define Is_invalidtrap_enabled() Invalidtrap(Fpustatus_register)
#define Is_divisionbyzerotrap_enabled() Divisionbyzerotrap(Fpustatus_register)
#define Is_overflowtrap_enabled() Overflowtrap(Fpustatus_register)
#define Is_underflowtrap_enabled() Underflowtrap(Fpustatus_register)
#define Is_inexacttrap_enabled() Inexacttrap(Fpustatus_register)

/* Set the indicated flags in the status register */
#define Set_invalidflag() Bitfield_deposit(1,0,1,Fpustatus_register)
#define Set_divisionbyzeroflag() Bitfield_deposit(1,1,1,Fpustatus_register)
#define Set_overflowflag() Bitfield_deposit(1,2,1,Fpustatus_register)
#define Set_underflowflag() Bitfield_deposit(1,3,1,Fpustatus_register)
#define Set_inexactflag() Bitfield_deposit(1,4,1,Fpustatus_register)

#define Clear_all_flags() Bitfield_deposit(0,0,5,Fpustatus_register)

/* Manipulate the trap and condition code bits (tbit and cbit) */
#define Set_tbit() Bitfield_deposit(1,25,1,Fpustatus_register)
#define Clear_tbit() Bitfield_deposit(0,25,1,Fpustatus_register)
#define Is_tbit_set() Tbit(Fpustatus_register)
#define Is_cbit_set() Cbit(Fpustatus_register)

#ifdef TIMEX
#define Set_status_cbit(value) \
	Bitfield_deposit(Bitfield_extract(10,10,Fpustatus_register),11,10,Fpustatus_register); \
	Bitfield_deposit(Bitfield_extract(5,1,Fpustatus_register),10,1,Fpustatus_register); \
	Bitfield_deposit(value,5,1,Fpustatus_register)
#else
#define Set_status_cbit(value) Bitfield_deposit(value,5,1,Fpustatus_register)
#endif

/*******************************
 * Condition field referencing *
 *******************************/
#define Unordered(cond) Unorderedbit(cond)
#define Equal(cond) Equalbit(cond)
#define Lessthan(cond) Lessthanbit(cond)
#define Greaterthan(cond) Greaterthanbit(cond)
#define Exception(cond) Exceptionbit(cond)


/* Defines for the extension */
#define Ext_isone_sign(extent) (Extsign(extent))
#define Ext_isnotzero(extent) \
    (Extall(extent))
#define Ext_isnotzero_lower(extent) \
    (Extlow31(extent))
#define Ext_leftshiftby1(extent) \
    Extall(extent) <<= 1
#define Ext_negate(extent) \
    (int )Extall(extent) = 0 - (int )Extall(extent)
#define Ext_setone_low(extent) Bitfield_deposit(1,31,1,extent)
