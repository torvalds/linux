## Process this with automake to create Makefile.in

AUTOMAKE_OPTIONS = foreign dejagnu

EXPECT = `if [ -f ../../expect/expect ] ; then \
	  echo ../../expect/expect ; \
	  else echo expect ; fi`

RUNTEST = `if [ -f ${srcdir}/../../dejagnu/runtest ] ; then \
	   echo ${srcdir}/../../dejagnu/runtest ; \
	   else echo runtest ;  fi`
