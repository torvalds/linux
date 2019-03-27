$NetBSD: README.txt,v 1.1 2000/06/06 08:15:02 bjh21 Exp $
$FreeBSD$

Package Overview for SoftFloat Release 2a

John R. Hauser
1998 December 13


SoftFloat is a software implementation of floating-point that conforms to
the IEC/IEEE Standard for Binary Floating-Point Arithmetic.  SoftFloat is
distributed in the form of C source code.  Compiling the SoftFloat sources
generates two things:

-- A SoftFloat object file (typically `softfloat.o') containing the complete
   set of IEC/IEEE floating-point routines.

-- A `timesoftfloat' program for evaluating the speed of the SoftFloat
   routines.  (The SoftFloat module is linked into this program.)

The SoftFloat package is documented in four text files:

   softfloat.txt          Documentation for using the SoftFloat functions.
   softfloat-source.txt   Documentation for compiling SoftFloat.
   softfloat-history.txt  History of major changes to SoftFloat.
   timesoftfloat.txt      Documentation for using `timesoftfloat'.

Other files in the package comprise the source code for SoftFloat.

Please be aware that some work is involved in porting this software to other
targets.  It is not just a matter of getting `make' to complete without
error messages.  I would have written the code that way if I could, but
there are fundamental differences between systems that I can't make go away.
You should not attempt to compile SoftFloat without first reading both
`softfloat.txt' and `softfloat-source.txt'.

At the time of this writing, the most up-to-date information about
SoftFloat and the latest release can be found at the Web page `http://
HTTP.CS.Berkeley.EDU/~jhauser/arithmetic/SoftFloat.html'.

