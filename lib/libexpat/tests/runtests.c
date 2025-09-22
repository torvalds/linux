/* Run the Expat test suite
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 2001-2006 Fred L. Drake, Jr. <fdrake@users.sourceforge.net>
   Copyright (c) 2003      Greg Stein <gstein@users.sourceforge.net>
   Copyright (c) 2005-2007 Steven Solie <steven@solie.ca>
   Copyright (c) 2005-2012 Karl Waclawek <karl@waclawek.net>
   Copyright (c) 2016-2023 Sebastian Pipping <sebastian@pipping.org>
   Copyright (c) 2017-2022 Rhodri James <rhodri@wildebeest.org.uk>
   Copyright (c) 2017      Joe Orton <jorton@redhat.com>
   Copyright (c) 2017      José Gutiérrez de la Concha <jose@zeroc.com>
   Copyright (c) 2018      Marco Maggi <marco.maggi-ipsu@poste.it>
   Copyright (c) 2019      David Loffredo <loffredo@steptools.com>
   Copyright (c) 2020      Tim Gates <tim.gates@iress.com>
   Copyright (c) 2021      Donghee Na <donghee.na@python.org>
   Copyright (c) 2022      Sean McBride <sean@rogue-research.com>
   Copyright (c) 2023      Sony Corporation / Snild Dolkow <snild@sony.com>
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "expat_config.h"

#include <stdio.h>
#include <string.h>

#include "expat.h"
#include "internal.h"
#include "minicheck.h"
#include "common.h"

#include "basic_tests.h"
#include "ns_tests.h"
#include "misc_tests.h"
#include "alloc_tests.h"
#include "nsalloc_tests.h"
#include "acc_tests.h"

XML_Parser g_parser = NULL;

static Suite *
make_suite(void) {
  Suite *s = suite_create("basic");

  make_basic_test_case(s);
  make_namespace_test_case(s);
  make_miscellaneous_test_case(s);
  make_alloc_test_case(s);
  make_nsalloc_test_case(s);
#if XML_GE == 1
  make_accounting_test_case(s);
#endif

  return s;
}

int
main(int argc, char *argv[]) {
  int i, nf;
  int verbosity = CK_NORMAL;
  Suite *s = make_suite();
  SRunner *sr = srunner_create(s);

  for (i = 1; i < argc; ++i) {
    char *opt = argv[i];
    if (strcmp(opt, "-v") == 0 || strcmp(opt, "--verbose") == 0)
      verbosity = CK_VERBOSE;
    else if (strcmp(opt, "-q") == 0 || strcmp(opt, "--quiet") == 0)
      verbosity = CK_SILENT;
    else {
      fprintf(stderr, "runtests: unknown option '%s'\n", opt);
      return 2;
    }
  }
  if (verbosity != CK_SILENT)
    printf("Expat version: %" XML_FMT_STR "\n", XML_ExpatVersion());

  for (g_chunkSize = 0; g_chunkSize <= 5; g_chunkSize++) {
    for (int enabled = 0; enabled <= 1; ++enabled) {
      char context[100];
#if defined(XML_TESTING)
      g_reparseDeferralEnabledDefault = enabled;
#endif
      snprintf(context, sizeof(context), "chunksize=%d deferral=%d",
               g_chunkSize, enabled);
      context[sizeof(context) - 1] = '\0';
      srunner_run_all(sr, context, verbosity);
    }
  }
  srunner_summarize(sr, verbosity);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
