#include "version.h"

/* This is the trailing component of the string reported as the
   version number by all components of the compiler.  For an official
   FSF release, it is empty.  If you distribute a modified version of
   GCC, please change this string to indicate that.  The suggested
   format is a leading space, followed by your organization's name
   in parentheses.  You may also wish to include a number indicating
   the revision of your modified compiler.  */

#define VERSUFFIX ""

/* This is the location of the online document giving instructions for
   reporting bugs.  If you distribute a modified version of GCC,
   please change this to refer to a document giving instructions for
   reporting bugs to you, not us.  (You are of course welcome to
   forward us bugs reported to you, if you determine that they are
   not bugs in your modifications.)  */

const char bug_report_url[] = "<URL:http://gcc.gnu.org/bugs.html>";

/* The complete version string, assembled from several pieces.
   BASEVER, DATESTAMP, and DEVPHASE are defined by the Makefile.  */

const char version_string[] = BASEVER DATESTAMP DEVPHASE VERSUFFIX;
