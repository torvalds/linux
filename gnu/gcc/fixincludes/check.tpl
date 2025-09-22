[= autogen5 template sh=check.sh =]
[=
#
#  This file contanes the shell template to run tests on the fixes
#
=]#!/bin/sh

set -e
TESTDIR=tests
TESTBASE=`cd $1;${PWDCMD-pwd}`

[ -d ${TESTDIR} ] || mkdir ${TESTDIR}
cd ${TESTDIR}
TESTDIR=`${PWDCMD-pwd}`

TARGET_MACHINE='*'
DESTDIR=`${PWDCMD-pwd}`/res
SRCDIR=`${PWDCMD-pwd}`/inc
FIND_BASE='.'
VERBOSE=[=` echo ${VERBOSE-1} `=]
INPUT=`${PWDCMD-pwd}`
ORIGDIR=${INPUT}

export TARGET_MACHINE DESTDIR SRCDIR FIND_BASE VERBOSE INPUT ORIGDIR

rm -rf ${DESTDIR} ${SRCDIR}
mkdir ${DESTDIR} ${SRCDIR}
(
[=
  (shellf
    "for f in %s
     do case $f in
        */* ) echo $f | sed 's;/[^/]*$;;' ;;
        esac
     done | sort -u | \
     while read g
     do echo \"  mkdir \\${SRCDIR}/$g || mkdir -p \\${SRCDIR}/$g || exit 1\"
     done" (join " " (stack "fix.files"))  ) =]
) 2> /dev/null[= # suppress 'No such file or directory' messages =]
cd inc
[=
(define sfile "")
(define HACK  "")
(define dfile "")              =][=

FOR fix                        =][=

  IF (> (count "test_text") 1) =][=
    (set! HACK (string-upcase! (get "hackname")))
    (set! sfile (if (exist? "files") (get "files[]") "testing.h"))
    (set! dfile (string-append
          (if (*==* sfile "/")
              (shellf "echo \"%s\"|sed 's,/[^/]*,/,'" sfile )
              "" )
          (string-tr! (get "hackname") "_A-Z" "-a-z")
    )           )              =][=

    FOR test_text (for-from 1) =]
cat >> [=(. sfile)=] <<_HACK_EOF_


#if defined( [=(. HACK)=]_CHECK_[=(for-index)=] )
[=test_text=]
#endif  /* [=(. HACK)=]_CHECK_[=(for-index)=] */
_HACK_EOF_
echo [=(. sfile)=] | ../../fixincl
mv -f [=(. sfile)=] [=(. dfile)=]-[=(for-index)=].h
[ -f ${DESTDIR}/[=(. sfile)=] ] && [=#
   =]mv ${DESTDIR}/[=(. sfile)=] ${DESTDIR}/[=(. dfile)=]-[=(for-index)=].h[=

    ENDFOR  test_text =][=

  ENDIF  multi-test   =][=

ENDFOR  fix

=][=

FOR fix  =][=
  (set! HACK (string-upcase! (get "hackname")))  =][=

  IF (not (exist? "test_text")) =][=
    (if (not (exist? "replace"))
        (error (sprintf "include fix '%s' has no test text"
                        (get "hackname") )) )
         =][=
  ELSE   =]
cat >> [=
    IF (exist? "files") =][=
      files[0] =][=
    ELSE =]testing.h[=
    ENDIF =] <<_HACK_EOF_


#if defined( [=(. HACK)=]_CHECK )
[=test_text=]
#endif  /* [=(. HACK)=]_CHECK */
_HACK_EOF_
[=ENDIF =][=

ENDFOR  fix

=]

find . -type f | sed 's;^\./;;' | sort | ../../fixincl
cd ${DESTDIR}

exitok=true

find * -type f -print > ${TESTDIR}/LIST

#  Special hack for sys/types.h:  the #define-d types for size_t,
#  ptrdiff_t and wchar_t are different for each port.  Therefore,
#  strip off the defined-to type so that the test results are the
#  same for all platforms.
#
sed 's/\(#define __[A-Z_]*_TYPE__\).*/\1/' sys/types.h > XX
mv -f XX sys/types.h

#  The following subshell weirdness is for saving an exit
#  status from within a while loop that reads input.  If you can
#  think of a cleaner way, suggest away, please...
#
exitok=`
exec < ${TESTDIR}/LIST
while read f
do
  if [ ! -f ${TESTBASE}/$f ]
  then
    echo "Newly fixed header:  $f" >&2
    exitok=false

  elif cmp $f ${TESTBASE}/$f >&2
  then
    :

  else
    ${DIFF:-diff} -c $f ${TESTBASE}/$f >&2 || :
    exitok=false
  fi
done
echo $exitok`

cd $TESTBASE

find * -type f -print | \
fgrep -v 'CVS/' | \
fgrep -v '.svn/' > ${TESTDIR}/LIST

exitok=`
exec < ${TESTDIR}/LIST
while read f
do
  if [ -s $f ] && [ ! -f ${DESTDIR}/$f ]
  then
    echo "Missing header fix:  $f" >&2
    exitok=false
  fi
done
echo $exitok`

echo
if $exitok
then
  cd ${TESTDIR}
  rm -rf inc res LIST
  cd ..
  rmdir ${TESTDIR} > /dev/null 2>&1 || :
  echo All fixinclude tests pass >&2
else
  echo There were fixinclude test FAILURES  >&2
fi
$exitok[=

(if (defined? 'set-writable) (set-writable))

=]
