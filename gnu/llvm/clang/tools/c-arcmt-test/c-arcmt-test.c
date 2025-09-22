/* c-arcmt-test.c */

#include "clang-c/Index.h"
#include "llvm/Support/AutoConvert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif

static int print_remappings(const char *path) {
  CXRemapping remap;
  unsigned i, N;
  CXString origFname;
  CXString transFname;

  remap = clang_getRemappings(path);
  if (!remap)
    return 1;

  N = clang_remap_getNumFiles(remap);
  for (i = 0; i != N; ++i) {
    clang_remap_getFilenames(remap, i, &origFname, &transFname);

    fprintf(stdout, "%s\n", clang_getCString(origFname));
    fprintf(stdout, "%s\n", clang_getCString(transFname));

    clang_disposeString(origFname);
    clang_disposeString(transFname);
  }

  clang_remap_dispose(remap);
  return 0;
}

static int print_remappings_filelist(const char **files, unsigned numFiles) {
  CXRemapping remap;
  unsigned i, N;
  CXString origFname;
  CXString transFname;

  remap = clang_getRemappingsFromFileList(files, numFiles);
  if (!remap)
    return 1;

  N = clang_remap_getNumFiles(remap);
  for (i = 0; i != N; ++i) {
    clang_remap_getFilenames(remap, i, &origFname, &transFname);

    fprintf(stdout, "%s\n", clang_getCString(origFname));
    fprintf(stdout, "%s\n", clang_getCString(transFname));

    clang_disposeString(origFname);
    clang_disposeString(transFname);
  }

  clang_remap_dispose(remap);
  return 0;
}

/******************************************************************************/
/* Command line processing.                                                   */
/******************************************************************************/

static void print_usage(void) {
  fprintf(stderr,
    "usage: c-arcmt-test -mt-migrate-directory <path>\n"
    "       c-arcmt-test <remap-file-path1> <remap-file-path2> ...\n\n\n");
}

/***/

int carcmttest_main(int argc, const char **argv) {
  clang_enableStackTraces();
  if (argc == 3 && strncmp(argv[1], "-mt-migrate-directory", 21) == 0)
    return print_remappings(argv[2]);

  if (argc > 1)
    return print_remappings_filelist(argv+1, argc-1);
  
  print_usage();
  return 1;
}

/***/

/* We intentionally run in a separate thread to ensure we at least minimal
 * testing of a multithreaded environment (for example, having a reduced stack
 * size). */

typedef struct thread_info {
  int argc;
  const char **argv;
  int result;
} thread_info;
void thread_runner(void *client_data_v) {
  thread_info *client_data = client_data_v;
  client_data->result = carcmttest_main(client_data->argc, client_data->argv);
}

static void flush_atexit(void) {
  /* stdout, and surprisingly even stderr, are not always flushed on process
   * and thread exit, particularly when the system is under heavy load. */
  fflush(stdout);
  fflush(stderr);
}

int main(int argc, const char **argv) {
#ifdef __MVS__
  if (enableAutoConversion(fileno(stdout)) == -1)
    fprintf(stderr, "Setting conversion on stdout failed\n");

  if (enableAutoConversion(fileno(stderr)) == -1)
    fprintf(stderr, "Setting conversion on stderr failed\n");
#endif

  thread_info client_data;

  atexit(flush_atexit);

#if defined(_WIN32)
  if (getenv("LIBCLANG_LOGGING") == NULL)
    putenv("LIBCLANG_LOGGING=1");
  _setmode( _fileno(stdout), _O_BINARY );
#else
  setenv("LIBCLANG_LOGGING", "1", /*overwrite=*/0);
#endif

  if (getenv("CINDEXTEST_NOTHREADS"))
    return carcmttest_main(argc, argv);

  client_data.argc = argc;
  client_data.argv = argv;
  clang_executeOnThread(thread_runner, &client_data, 0);
  return client_data.result;
}
