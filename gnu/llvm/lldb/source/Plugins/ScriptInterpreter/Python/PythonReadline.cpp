#include "PythonReadline.h"

#ifdef LLDB_USE_LIBEDIT_READLINE_COMPAT_MODULE

#include <cstdio>

#include <editline/readline.h>

// Simple implementation of the Python readline module using libedit.
// In the event that libedit is excluded from the build, this turns
// back into a null implementation that blocks the module from pulling
// in the GNU readline shared lib, which causes linkage confusion when
// both readline and libedit's readline compatibility symbols collide.
//
// Currently it only installs a PyOS_ReadlineFunctionPointer, without
// implementing any of the readline module methods. This is meant to
// work around LLVM pr18841 to avoid seg faults in the stock Python
// readline.so linked against GNU readline.
//
// Bug on the cpython side: https://bugs.python.org/issue38634

PyDoc_STRVAR(moduleDocumentation,
             "Simple readline module implementation based on libedit.");

static struct PyModuleDef readline_module = {
    PyModuleDef_HEAD_INIT, // m_base
    "lldb_editline",       // m_name
    moduleDocumentation,   // m_doc
    -1,                    // m_size
    nullptr,               // m_methods
    nullptr,               // m_reload
    nullptr,               // m_traverse
    nullptr,               // m_clear
    nullptr,               // m_free
};

static char *simple_readline(FILE *stdin, FILE *stdout, const char *prompt) {
  rl_instream = stdin;
  rl_outstream = stdout;
  char *line = readline(prompt);
  if (!line) {
    char *ret = (char *)PyMem_RawMalloc(1);
    if (ret != nullptr)
      *ret = '\0';
    return ret;
  }
  if (*line)
    add_history(line);
  int n = strlen(line);
  char *ret = (char *)PyMem_RawMalloc(n + 2);
  if (ret) {
    memcpy(ret, line, n);
    free(line);
    ret[n] = '\n';
    ret[n + 1] = '\0';
  }
  return ret;
}

PyMODINIT_FUNC initlldb_readline(void) {
  PyOS_ReadlineFunctionPointer = simple_readline;

  return PyModule_Create(&readline_module);
}
#endif
