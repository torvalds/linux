
Package Overview for TestFloat Release 2a

John R. Hauser
1998 December 16


TestFloat is a program for testing that a floating-point implementation
conforms to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
TestFloat is distributed in the form of C source code.  The TestFloat
package actually provides two related programs:

-- The `testfloat' program tests a system's floating-point for conformance
   to the IEC/IEEE Standard.  This program uses the SoftFloat software
   floating-point implementation as a basis for comparison.

-- The `testsoftfloat' program tests SoftFloat itself for conformance to
   the IEC/IEEE Standard.  These tests are performed by comparing against a
   separate, slower software floating-point that is included in the TestFloat
   package.

TestFloat depends on SoftFloat, but SoftFloat is not included in the
TestFloat package.  SoftFloat can be obtained through the Web page `http://
HTTP.CS.Berkeley.EDU/~jhauser/arithmetic/SoftFloat.html'.

TestFloat is documented in three text files:

   testfloat.txt          Documentation for using the TestFloat programs
                              (both `testfloat' and `testsoftfloat').
   testfloat-source.txt   Documentation for porting and compiling TestFloat.
   testfloat-history.txt  History of major changes to TestFloat.

The following file is also provided:

   systemBugs.txt         Information about processor bugs found using
                              TestFloat.

Other files in the package comprise the source code for TestFloat.

Please be aware that some work is involved in porting this software to other
targets.  It is not just a matter of getting `make' to complete without
error messages.  I would have written the code that way if I could, but
there are fundamental differences between systems that I can't make go away.
You should not attempt to compile the TestFloat sources without first
reading `testfloat-source.txt'.

At the time of this writing, the most up-to-date information about
TestFloat and the latest release can be found at the Web page `http://
HTTP.CS.Berkeley.EDU/~jhauser/arithmetic/TestFloat.html'.

