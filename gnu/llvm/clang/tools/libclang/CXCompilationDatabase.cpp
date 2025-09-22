#include "clang-c/CXCompilationDatabase.h"
#include "CXString.h"
#include "clang/Tooling/CompilationDatabase.h"
#include <cstdio>

using namespace clang;
using namespace clang::tooling;

// FIXME: do something more useful with the error message
CXCompilationDatabase
clang_CompilationDatabase_fromDirectory(const char *BuildDir,
                                        CXCompilationDatabase_Error *ErrorCode)
{
  std::string ErrorMsg;
  CXCompilationDatabase_Error Err = CXCompilationDatabase_NoError;

  std::unique_ptr<CompilationDatabase> db =
      CompilationDatabase::loadFromDirectory(BuildDir, ErrorMsg);

  if (!db) {
    fprintf(stderr, "LIBCLANG TOOLING ERROR: %s\n", ErrorMsg.c_str());
    Err = CXCompilationDatabase_CanNotLoadDatabase;
  }

  if (ErrorCode)
    *ErrorCode = Err;

  return db.release();
}

void
clang_CompilationDatabase_dispose(CXCompilationDatabase CDb)
{
  delete static_cast<CompilationDatabase *>(CDb);
}

struct AllocatedCXCompileCommands
{
  std::vector<CompileCommand> CCmd;

  AllocatedCXCompileCommands(std::vector<CompileCommand> Cmd)
      : CCmd(std::move(Cmd)) {}
};

CXCompileCommands
clang_CompilationDatabase_getCompileCommands(CXCompilationDatabase CDb,
                                             const char *CompleteFileName)
{
  if (CompilationDatabase *db = static_cast<CompilationDatabase *>(CDb)) {
    std::vector<CompileCommand> CCmd(db->getCompileCommands(CompleteFileName));
    if (!CCmd.empty())
      return new AllocatedCXCompileCommands(std::move(CCmd));
  }

  return nullptr;
}

CXCompileCommands
clang_CompilationDatabase_getAllCompileCommands(CXCompilationDatabase CDb) {
  if (CompilationDatabase *db = static_cast<CompilationDatabase *>(CDb)) {
    std::vector<CompileCommand> CCmd(db->getAllCompileCommands());
    if (!CCmd.empty())
      return new AllocatedCXCompileCommands(std::move(CCmd));
  }

  return nullptr;
}

void
clang_CompileCommands_dispose(CXCompileCommands Cmds)
{
  delete static_cast<AllocatedCXCompileCommands *>(Cmds);
}

unsigned
clang_CompileCommands_getSize(CXCompileCommands Cmds)
{
  if (!Cmds)
    return 0;

  AllocatedCXCompileCommands *ACC =
    static_cast<AllocatedCXCompileCommands *>(Cmds);

  return ACC->CCmd.size();
}

CXCompileCommand
clang_CompileCommands_getCommand(CXCompileCommands Cmds, unsigned I)
{
  if (!Cmds)
    return nullptr;

  AllocatedCXCompileCommands *ACC =
    static_cast<AllocatedCXCompileCommands *>(Cmds);

  if (I >= ACC->CCmd.size())
    return nullptr;

  return &ACC->CCmd[I];
}

CXString
clang_CompileCommand_getDirectory(CXCompileCommand CCmd)
{
  if (!CCmd)
    return cxstring::createNull();

  CompileCommand *cmd = static_cast<CompileCommand *>(CCmd);
  return cxstring::createRef(cmd->Directory.c_str());
}

CXString
clang_CompileCommand_getFilename(CXCompileCommand CCmd)
{
  if (!CCmd)
    return cxstring::createNull();

  CompileCommand *cmd = static_cast<CompileCommand *>(CCmd);
  return cxstring::createRef(cmd->Filename.c_str());
}

unsigned
clang_CompileCommand_getNumArgs(CXCompileCommand CCmd)
{
  if (!CCmd)
    return 0;

  return static_cast<CompileCommand *>(CCmd)->CommandLine.size();
}

CXString
clang_CompileCommand_getArg(CXCompileCommand CCmd, unsigned Arg)
{
  if (!CCmd)
    return cxstring::createNull();

  CompileCommand *Cmd = static_cast<CompileCommand *>(CCmd);

  if (Arg >= Cmd->CommandLine.size())
    return cxstring::createNull();

  return cxstring::createRef(Cmd->CommandLine[Arg].c_str());
}

unsigned
clang_CompileCommand_getNumMappedSources(CXCompileCommand CCmd)
{
  // Left here for backward compatibility. No mapped sources exists in the C++
  // backend anymore.
  return 0;
}

CXString
clang_CompileCommand_getMappedSourcePath(CXCompileCommand CCmd, unsigned I)
{
  // Left here for backward compatibility. No mapped sources exists in the C++
  // backend anymore.
  return cxstring::createNull();
}

CXString
clang_CompileCommand_getMappedSourceContent(CXCompileCommand CCmd, unsigned I)
{
  // Left here for backward compatibility. No mapped sources exists in the C++
  // backend anymore.
  return cxstring::createNull();
}
