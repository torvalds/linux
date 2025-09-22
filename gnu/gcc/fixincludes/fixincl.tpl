[= AutoGen5 Template -*- Mode: C -*-
x=fixincl.x =]
[= (dne " * " "/* ")=]
 */
/* DO NOT CVS-MERGE THIS FILE, EITHER [=`date`=]
 *
 * You must regenerate it.  Use the ./genfixes script.
 *
 *
 * This is part of the fixincl program used to install modified versions of
 * certain ANSI-incompatible system header files which are fixed to work
 * correctly with ANSI C and placed in a directory that GNU C will search.
 *
 * This file contains [=(count "fix")=] fixup descriptions.
 *
 * See README for more information.
 *
 *  inclhack copyright (c) 1998, 1999, 2000, 2001
 *  The Free Software Foundation, Inc.
 *
 [=(define re-ct 0) (define max-mach 0) (define ct 0)
   (define HACK "") (define Hack "")    (define tmp "")
   (gpl "inclhack" " *  ")=]
 */
[=

FOR fix =]
/* * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  Description of [=
    (set! Hack (string-capitalize! (get "hackname")))
    (set! HACK (string-upcase!     (get "hackname")))
    (if (and (not (exist? "test_text")) (not (exist? "replace")))
        (error (sprintf "include fix '%s' has no test text" Hack )) )
    (. Hack)=] fix
 */[=

# Note that this is not just for debugging purposes, but in case
  some C fix wishes to refer to the regexps it is paired with.
  See commentary at the top of fixfixes.c.
=]
tSCC z[=(. Hack)=]Name[] =
     "[=hackname=]";

/*
 *  File name selection pattern
 */[=

  IF (exist? "files")=]
tSCC z[=(. Hack)=]List[] =
  "[=FOR files =]|[=files=][=ENDFOR=]|";[=

  ELSE =]
#define z[=(. Hack)=]List (char*)NULL[=
  ENDIF (exist? "files") =]
/*
 *  Machine/OS name selection pattern
 */[=

  IF (exist? "mach")=]
tSCC* apz[=(. Hack)=]Machs[] = {[=
    (set! ct 0) =][=

    FOR mach =]
        [=
      (set! tmp (get "mach"))
      (set! ct (+ ct (string-length tmp) 5))
      (kr-string tmp)=],[=
    ENDFOR=]
        (const char*)NULL };[=

    (if (> ct max-mach) (set! max-mach ct)) =][=

  ELSE =]
#define apz[=(. Hack)=]Machs (const char**)NULL[=
  ENDIF (exist? "files") =][=

  IF (exist? "select")=]

/*
 *  content selection pattern - do fix if pattern found
 */[=
    FOR select =]
tSCC z[=(. Hack)=]Select[=(for-index)=][] =
       [=(kr-string (get "select"))=];[=
    ENDFOR select =][=
  ENDIF =][=

  IF (exist? "bypass")=]

/*
 *  content bypass pattern - skip fix if pattern found
 */[=
    FOR bypass =]
tSCC z[=(. Hack)=]Bypass[=(for-index)=][] =
       [=(kr-string (get "bypass"))=];[=
    ENDFOR bypass =][=
  ENDIF =][=

  IF (exist? "test")=]

/*
 *  perform the 'test' shell command - do fix on success
 */[=
    FOR test =]
tSCC z[=(. Hack)=]Test[=(for-index)=][] =
       [=(kr-string (get "test"))=];[=
    ENDFOR  =][=
  ENDIF     =][=

  IF (exist? "c_test")=]

/*
 *  perform the C function call test
 */[=
    FOR c_test =]
tSCC z[=(. Hack)=]FTst[=(for-index)=][] = "[=c_test=]";[=
    ENDFOR c_test =][=
  ENDIF =][=

  IF (set! ct (+ (count "select") (count "bypass") 
              (count "test") (count "c_test")))

     (= ct 0)
=]
#define [=(. HACK)=]_TEST_CT  0
#define a[=(. Hack)=]Tests   (tTestDesc*)NULL[=
  ELSE =]

#define    [=(. HACK)=]_TEST_CT  [=(. ct)=][=
	(set! re-ct (+ re-ct (count "select") (count "bypass"))) =]
static tTestDesc a[=(. Hack)=]Tests[] = {[=

    FOR test =]
  { TT_TEST,     z[=(. Hack)=]Test[=(for-index)=],   0 /* unused */ },[=
    ENDFOR test =][=

    FOR c_test =]
  { TT_FUNCTION, z[=(. Hack)=]FTst[=(for-index)=],   0 /* unused */ },[=
    ENDFOR c_test =][=

    FOR bypass =]
  { TT_NEGREP,   z[=(. Hack)=]Bypass[=(for-index)=], (regex_t*)NULL },[=
    ENDFOR bypass =][=

    FOR select =]
  { TT_EGREP,    z[=(. Hack)=]Select[=(for-index)=], (regex_t*)NULL },[=
    ENDFOR select =] };[=
  ENDIF =]

/*
 *  Fix Command Arguments for [=(. Hack)=]
 */
static const char* apz[=(. Hack)=]Patch[] = {[=
    IF   (exist? "sed")=] "sed"[=
      FOR sed=],
    "-e", [=(kr-string (get "sed"))=][=
      ENDFOR sed=],[=

    ELIF (exist? "shell")=] "sh", "-c",
    [=(kr-string (get "shell"))=],[=

    ELIF (exist? "c_fix")=]
    [=(kr-string (get "c_fix"))=],[=

      FOR c_fix_arg =]
    [=(kr-string (get "c_fix_arg"))=],[=
      ENDFOR c_fix_arg =][=

    ELIF (> (len "replace") 0) =]
[=(kr-string (get "replace"))=],[=

    ENDIF=]
    (char*)NULL };
[=ENDFOR fix=]

/* * * * * * * * * * * * * * * * * * * * * * * * * *
 *
 *  List of all fixes
 */[=
#  as of this writing, 49 bytes are needed by the case statement format.
   We also must allow for the size of the target machine machine name.
   This allows for a 79 byte machine name.  Better be enough.
=]
#define REGEX_COUNT          [= (. re-ct) =]
#define MACH_LIST_SIZE_LIMIT [= (+ 128 max-mach) =]
#define FIX_COUNT            [= (count "fix") =]

/*
 *  Enumerate the fixes[= # in a way that minimizes diffs :-) =]
 */
typedef enum {[=

FOR fix "," =]
    [=(string-upcase! (get "hackname"))=]_FIXIDX[=
ENDFOR

=]
} t_fixinc_idx;

tFixDesc fixDescList[ FIX_COUNT ] = {[=


FOR fix ",\n" =][=
    (set! Hack (string-capitalize! (get "hackname")))
    (set! HACK (string-upcase!     (get "hackname"))) =]
  {  z[=(. Hack)=]Name,    z[=(. Hack)=]List,
     apz[=(. Hack)=]Machs,
     [=(. HACK)=]_TEST_CT, [=
       IF (exist? "not_machine") =]FD_MACH_IFNOT[=
       ELSE                      =]FD_MACH_ONLY[=
       ENDIF =][=
       IF    (exist? "shell")    =] | FD_SHELL_SCRIPT[=
       ELIF  (exist? "c_fix")    =] | FD_SUBROUTINE[=
       ELIF  (exist? "replace")  =] | FD_REPLACEMENT[=
       ENDIF =],
     a[=(. Hack)=]Tests,   apz[=(. Hack)=]Patch, 0 }[=

ENDFOR =]
};
