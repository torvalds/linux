// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 ****************************************************************************
 *
 *                   "DHRYSTONE" Benchmark Program
 *                   -----------------------------
 *
 *  Version:    C, Version 2.1
 *
 *  File:       dhry_1.c (part 2 of 3)
 *
 *  Date:       May 25, 1988
 *
 *  Author:     Reinhold P. Weicker
 *
 ****************************************************************************
 */

#include "dhry.h"

#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/string.h>

/* Global Variables: */

int Int_Glob;
char Ch_1_Glob;

static Rec_Pointer Ptr_Glob, Next_Ptr_Glob;
static Boolean Bool_Glob;
static char Ch_2_Glob;
static int Arr_1_Glob[50];
static int Arr_2_Glob[50][50];

static void Proc_3(Rec_Pointer *Ptr_Ref_Par)
/******************/
/* executed once */
/* Ptr_Ref_Par becomes Ptr_Glob */
{
	if (Ptr_Glob) {
		/* then, executed */
		*Ptr_Ref_Par = Ptr_Glob->Ptr_Comp;
	}
	Proc_7(10, Int_Glob, &Ptr_Glob->variant.var_1.Int_Comp);
} /* Proc_3 */


static void Proc_1(Rec_Pointer Ptr_Val_Par)
/******************/
/* executed once */
{
	Rec_Pointer Next_Record = Ptr_Val_Par->Ptr_Comp;
						/* == Ptr_Glob_Next */
	/* Local variable, initialized with Ptr_Val_Par->Ptr_Comp,    */
	/* corresponds to "rename" in Ada, "with" in Pascal           */

	*Ptr_Val_Par->Ptr_Comp = *Ptr_Glob;
	Ptr_Val_Par->variant.var_1.Int_Comp = 5;
	Next_Record->variant.var_1.Int_Comp =
		Ptr_Val_Par->variant.var_1.Int_Comp;
	Next_Record->Ptr_Comp = Ptr_Val_Par->Ptr_Comp;
	Proc_3(&Next_Record->Ptr_Comp);
	/* Ptr_Val_Par->Ptr_Comp->Ptr_Comp == Ptr_Glob->Ptr_Comp */
	if (Next_Record->Discr == Ident_1) {
		/* then, executed */
		Next_Record->variant.var_1.Int_Comp = 6;
		Proc_6(Ptr_Val_Par->variant.var_1.Enum_Comp,
		       &Next_Record->variant.var_1.Enum_Comp);
		Next_Record->Ptr_Comp = Ptr_Glob->Ptr_Comp;
		Proc_7(Next_Record->variant.var_1.Int_Comp, 10,
		       &Next_Record->variant.var_1.Int_Comp);
	} else {
		/* not executed */
		*Ptr_Val_Par = *Ptr_Val_Par->Ptr_Comp;
	}
} /* Proc_1 */


static void Proc_2(One_Fifty *Int_Par_Ref)
/******************/
/* executed once */
/* *Int_Par_Ref == 1, becomes 4 */
{
	One_Fifty  Int_Loc;
	Enumeration   Enum_Loc;

	Int_Loc = *Int_Par_Ref + 10;
	do {
		/* executed once */
		if (Ch_1_Glob == 'A') {
			/* then, executed */
			Int_Loc -= 1;
			*Int_Par_Ref = Int_Loc - Int_Glob;
			Enum_Loc = Ident_1;
		} /* if */
	} while (Enum_Loc != Ident_1); /* true */
} /* Proc_2 */


static void Proc_4(void)
/*******/
/* executed once */
{
	Boolean Bool_Loc;

	Bool_Loc = Ch_1_Glob == 'A';
	Bool_Glob = Bool_Loc | Bool_Glob;
	Ch_2_Glob = 'B';
} /* Proc_4 */


static void Proc_5(void)
/*******/
/* executed once */
{
	Ch_1_Glob = 'A';
	Bool_Glob = false;
} /* Proc_5 */


int dhry(int n)
/*****/

  /* main program, corresponds to procedures        */
  /* Main and Proc_0 in the Ada version             */
{
	One_Fifty Int_1_Loc;
	One_Fifty Int_2_Loc;
	One_Fifty Int_3_Loc;
	char Ch_Index;
	Enumeration Enum_Loc;
	Str_30 Str_1_Loc;
	Str_30 Str_2_Loc;
	int Run_Index;
	int Number_Of_Runs;
	ktime_t Begin_Time, End_Time;
	u32 User_Time;

	/* Initializations */

	Next_Ptr_Glob = (Rec_Pointer)kzalloc(sizeof(Rec_Type), GFP_ATOMIC);
	if (!Next_Ptr_Glob)
		return -ENOMEM;

	Ptr_Glob = (Rec_Pointer)kzalloc(sizeof(Rec_Type), GFP_ATOMIC);
	if (!Ptr_Glob) {
		kfree(Next_Ptr_Glob);
		return -ENOMEM;
	}

	Ptr_Glob->Ptr_Comp = Next_Ptr_Glob;
	Ptr_Glob->Discr = Ident_1;
	Ptr_Glob->variant.var_1.Enum_Comp = Ident_3;
	Ptr_Glob->variant.var_1.Int_Comp = 40;
	strcpy(Ptr_Glob->variant.var_1.Str_Comp,
	       "DHRYSTONE PROGRAM, SOME STRING");
	strcpy(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");

	Arr_2_Glob[8][7] = 10;
	/* Was missing in published program. Without this statement,    */
	/* Arr_2_Glob[8][7] would have an undefined value.             */
	/* Warning: With 16-Bit processors and Number_Of_Runs > 32000,  */
	/* overflow may occur for this array element.                   */

	pr_debug("Dhrystone Benchmark, Version 2.1 (Language: C)\n");

	Number_Of_Runs = n;

	pr_debug("Execution starts, %d runs through Dhrystone\n",
		 Number_Of_Runs);

	/***************/
	/* Start timer */
	/***************/

	Begin_Time = ktime_get();

	for (Run_Index = 1; Run_Index <= Number_Of_Runs; ++Run_Index) {
		Proc_5();
		Proc_4();
		/* Ch_1_Glob == 'A', Ch_2_Glob == 'B', Bool_Glob == true */
		Int_1_Loc = 2;
		Int_2_Loc = 3;
		strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");
		Enum_Loc = Ident_2;
		Bool_Glob = !Func_2(Str_1_Loc, Str_2_Loc);
		/* Bool_Glob == 1 */
		while (Int_1_Loc < Int_2_Loc) {
			/* loop body executed once */
			Int_3_Loc = 5 * Int_1_Loc - Int_2_Loc;
			/* Int_3_Loc == 7 */
			Proc_7(Int_1_Loc, Int_2_Loc, &Int_3_Loc);
			/* Int_3_Loc == 7 */
			Int_1_Loc += 1;
		} /* while */
		/* Int_1_Loc == 3, Int_2_Loc == 3, Int_3_Loc == 7 */
		Proc_8(Arr_1_Glob, Arr_2_Glob, Int_1_Loc, Int_3_Loc);
		/* Int_Glob == 5 */
		Proc_1(Ptr_Glob);
		for (Ch_Index = 'A'; Ch_Index <= Ch_2_Glob; ++Ch_Index) {
			/* loop body executed twice */
			if (Enum_Loc == Func_1(Ch_Index, 'C')) {
				/* then, not executed */
				Proc_6(Ident_1, &Enum_Loc);
				strcpy(Str_2_Loc, "DHRYSTONE PROGRAM, 3'RD STRING");
				Int_2_Loc = Run_Index;
				Int_Glob = Run_Index;
			}
		}
		/* Int_1_Loc == 3, Int_2_Loc == 3, Int_3_Loc == 7 */
		Int_2_Loc = Int_2_Loc * Int_1_Loc;
		Int_1_Loc = Int_2_Loc / Int_3_Loc;
		Int_2_Loc = 7 * (Int_2_Loc - Int_3_Loc) - Int_1_Loc;
		/* Int_1_Loc == 1, Int_2_Loc == 13, Int_3_Loc == 7 */
		Proc_2(&Int_1_Loc);
		/* Int_1_Loc == 5 */

	} /* loop "for Run_Index" */

	/**************/
	/* Stop timer */
	/**************/

	End_Time = ktime_get();

#define dhry_assert_int_eq(val, expected)				\
	if (val != expected)						\
		pr_err("%s: %d (FAIL, expected %d)\n", #val, val,	\
		       expected);					\
	else								\
		pr_debug("%s: %d (OK)\n", #val, val)

#define dhry_assert_char_eq(val, expected)				\
	if (val != expected)						\
		pr_err("%s: %c (FAIL, expected %c)\n", #val, val,	\
		       expected);					\
	else								\
		pr_debug("%s: %c (OK)\n", #val, val)

#define dhry_assert_string_eq(val, expected)				\
	if (strcmp(val, expected))					\
		pr_err("%s: %s (FAIL, expected %s)\n", #val, val,	\
		       expected);					\
	else								\
		pr_debug("%s: %s (OK)\n", #val, val)

	pr_debug("Execution ends\n");
	pr_debug("Final values of the variables used in the benchmark:\n");
	dhry_assert_int_eq(Int_Glob, 5);
	dhry_assert_int_eq(Bool_Glob, 1);
	dhry_assert_char_eq(Ch_1_Glob, 'A');
	dhry_assert_char_eq(Ch_2_Glob, 'B');
	dhry_assert_int_eq(Arr_1_Glob[8], 7);
	dhry_assert_int_eq(Arr_2_Glob[8][7], Number_Of_Runs + 10);
	pr_debug("Ptr_Comp: %px\n", Ptr_Glob->Ptr_Comp);
	dhry_assert_int_eq(Ptr_Glob->Discr, 0);
	dhry_assert_int_eq(Ptr_Glob->variant.var_1.Enum_Comp, 2);
	dhry_assert_int_eq(Ptr_Glob->variant.var_1.Int_Comp, 17);
	dhry_assert_string_eq(Ptr_Glob->variant.var_1.Str_Comp,
			      "DHRYSTONE PROGRAM, SOME STRING");
	if (Next_Ptr_Glob->Ptr_Comp != Ptr_Glob->Ptr_Comp)
		pr_err("Next_Ptr_Glob->Ptr_Comp: %px (expected %px)\n",
		       Next_Ptr_Glob->Ptr_Comp, Ptr_Glob->Ptr_Comp);
	else
		pr_debug("Next_Ptr_Glob->Ptr_Comp: %px\n",
			 Next_Ptr_Glob->Ptr_Comp);
	dhry_assert_int_eq(Next_Ptr_Glob->Discr, 0);
	dhry_assert_int_eq(Next_Ptr_Glob->variant.var_1.Enum_Comp, 1);
	dhry_assert_int_eq(Next_Ptr_Glob->variant.var_1.Int_Comp, 18);
	dhry_assert_string_eq(Next_Ptr_Glob->variant.var_1.Str_Comp,
			      "DHRYSTONE PROGRAM, SOME STRING");
	dhry_assert_int_eq(Int_1_Loc, 5);
	dhry_assert_int_eq(Int_2_Loc, 13);
	dhry_assert_int_eq(Int_3_Loc, 7);
	dhry_assert_int_eq(Enum_Loc, 1);
	dhry_assert_string_eq(Str_1_Loc, "DHRYSTONE PROGRAM, 1'ST STRING");
	dhry_assert_string_eq(Str_2_Loc, "DHRYSTONE PROGRAM, 2'ND STRING");

	User_Time = ktime_to_ms(ktime_sub(End_Time, Begin_Time));

	kfree(Ptr_Glob);
	kfree(Next_Ptr_Glob);

	/* Measurements should last at least 2 seconds */
	if (User_Time < 2 * MSEC_PER_SEC)
		return -EAGAIN;

	return div_u64(mul_u32_u32(MSEC_PER_SEC, Number_Of_Runs), User_Time);
}
