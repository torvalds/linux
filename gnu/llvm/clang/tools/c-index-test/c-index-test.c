/* c-index-test.c */

#include "clang-c/BuildSystem.h"
#include "clang-c/CXCompilationDatabase.h"
#include "clang-c/CXErrorCode.h"
#include "clang-c/CXSourceLocation.h"
#include "clang-c/CXString.h"
#include "clang-c/Documentation.h"
#include "clang-c/Index.h"
#include "clang/Config/config.h"
#include "llvm/Support/AutoConvert.h"
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CLANG_HAVE_LIBXML
#include <libxml/parser.h>
#include <libxml/relaxng.h>
#include <libxml/xmlerror.h>
#endif

#ifdef _WIN32
#  include <direct.h>
#else
#  include <unistd.h>
#endif

extern int indextest_core_main(int argc, const char **argv);
extern int indextest_perform_shell_execution(const char *command_line);

/******************************************************************************/
/* Utility functions.                                                         */
/******************************************************************************/

#ifdef _MSC_VER
char *basename(const char* path)
{
    char* base1 = (char*)strrchr(path, '/');
    char* base2 = (char*)strrchr(path, '\\');
    if (base1 && base2)
        return((base1 > base2) ? base1 + 1 : base2 + 1);
    else if (base1)
        return(base1 + 1);
    else if (base2)
        return(base2 + 1);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
    return ((char *)path);
#ifdef __clang__
#pragma clang diagnostic pop
#endif
}
char *dirname(char* path)
{
    char* base1 = (char*)strrchr(path, '/');
    char* base2 = (char*)strrchr(path, '\\');
    if (base1 && base2)
        if (base1 > base2)
          *base1 = 0;
        else
          *base2 = 0;
    else if (base1)
        *base1 = 0;
    else if (base2)
        *base2 = 0;

    return path;
}
#else
extern char *basename(const char *);
extern char *dirname(char *);
#endif

CXIndex createIndexWithInvocationEmissionPath(int ExcludeDeclarationsFromPCH,
                                              int DisplayDiagnostics) {
    CXIndex Idx;

    CXIndexOptions Opts;
    memset(&Opts, 0, sizeof(Opts));
    Opts.Size = sizeof(CXIndexOptions);
    Opts.ExcludeDeclarationsFromPCH = ExcludeDeclarationsFromPCH;
    Opts.DisplayDiagnostics = DisplayDiagnostics;
    Opts.InvocationEmissionPath = getenv("CINDEXTEST_INVOCATION_EMISSION_PATH");

    Idx = clang_createIndexWithOptions(&Opts);
    if (!Idx) {
        fprintf(stderr,
                "clang_createIndexWithOptions() failed. "
                "CINDEX_VERSION_MINOR = %d, sizeof(CXIndexOptions) = %u\n",
                CINDEX_VERSION_MINOR, Opts.Size);
    }
    return Idx;
}

/** Return the default parsing options. */
static unsigned getDefaultParsingOptions(void) {
  unsigned options = CXTranslationUnit_DetailedPreprocessingRecord;

  if (getenv("CINDEXTEST_EDITING"))
    options |= clang_defaultEditingTranslationUnitOptions();
  if (getenv("CINDEXTEST_COMPLETION_CACHING"))
    options |= CXTranslationUnit_CacheCompletionResults;
  if (getenv("CINDEXTEST_COMPLETION_NO_CACHING"))
    options &= ~CXTranslationUnit_CacheCompletionResults;
  if (getenv("CINDEXTEST_SKIP_FUNCTION_BODIES"))
    options |= CXTranslationUnit_SkipFunctionBodies;
  if (getenv("CINDEXTEST_COMPLETION_BRIEF_COMMENTS"))
    options |= CXTranslationUnit_IncludeBriefCommentsInCodeCompletion;
  if (getenv("CINDEXTEST_CREATE_PREAMBLE_ON_FIRST_PARSE"))
    options |= CXTranslationUnit_CreatePreambleOnFirstParse;
  if (getenv("CINDEXTEST_KEEP_GOING"))
    options |= CXTranslationUnit_KeepGoing;
  if (getenv("CINDEXTEST_LIMIT_SKIP_FUNCTION_BODIES_TO_PREAMBLE"))
    options |= CXTranslationUnit_LimitSkipFunctionBodiesToPreamble;
  if (getenv("CINDEXTEST_INCLUDE_ATTRIBUTED_TYPES"))
    options |= CXTranslationUnit_IncludeAttributedTypes;
  if (getenv("CINDEXTEST_VISIT_IMPLICIT_ATTRIBUTES"))
    options |= CXTranslationUnit_VisitImplicitAttributes;
  if (getenv("CINDEXTEST_IGNORE_NONERRORS_FROM_INCLUDED_FILES"))
    options |= CXTranslationUnit_IgnoreNonErrorsFromIncludedFiles;

  return options;
}

static void ModifyPrintingPolicyAccordingToEnv(CXPrintingPolicy Policy) {
  struct Mapping {
    const char *name;
    enum CXPrintingPolicyProperty property;
  };
  struct Mapping mappings[] = {
      {"CINDEXTEST_PRINTINGPOLICY_INDENTATION", CXPrintingPolicy_Indentation},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSSPECIFIERS",
       CXPrintingPolicy_SuppressSpecifiers},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSTAGKEYWORD",
       CXPrintingPolicy_SuppressTagKeyword},
      {"CINDEXTEST_PRINTINGPOLICY_INCLUDETAGDEFINITION",
       CXPrintingPolicy_IncludeTagDefinition},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSSCOPE",
       CXPrintingPolicy_SuppressScope},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSUNWRITTENSCOPE",
       CXPrintingPolicy_SuppressUnwrittenScope},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSINITIALIZERS",
       CXPrintingPolicy_SuppressInitializers},
      {"CINDEXTEST_PRINTINGPOLICY_CONSTANTARRAYSIZEASWRITTEN",
       CXPrintingPolicy_ConstantArraySizeAsWritten},
      {"CINDEXTEST_PRINTINGPOLICY_ANONYMOUSTAGLOCATIONS",
       CXPrintingPolicy_AnonymousTagLocations},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSSTRONGLIFETIME",
       CXPrintingPolicy_SuppressStrongLifetime},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSLIFETIMEQUALIFIERS",
       CXPrintingPolicy_SuppressLifetimeQualifiers},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSTEMPLATEARGSINCXXCONSTRUCTORS",
       CXPrintingPolicy_SuppressTemplateArgsInCXXConstructors},
      {"CINDEXTEST_PRINTINGPOLICY_BOOL", CXPrintingPolicy_Bool},
      {"CINDEXTEST_PRINTINGPOLICY_RESTRICT", CXPrintingPolicy_Restrict},
      {"CINDEXTEST_PRINTINGPOLICY_ALIGNOF", CXPrintingPolicy_Alignof},
      {"CINDEXTEST_PRINTINGPOLICY_UNDERSCOREALIGNOF",
       CXPrintingPolicy_UnderscoreAlignof},
      {"CINDEXTEST_PRINTINGPOLICY_USEVOIDFORZEROPARAMS",
       CXPrintingPolicy_UseVoidForZeroParams},
      {"CINDEXTEST_PRINTINGPOLICY_TERSEOUTPUT", CXPrintingPolicy_TerseOutput},
      {"CINDEXTEST_PRINTINGPOLICY_POLISHFORDECLARATION",
       CXPrintingPolicy_PolishForDeclaration},
      {"CINDEXTEST_PRINTINGPOLICY_HALF", CXPrintingPolicy_Half},
      {"CINDEXTEST_PRINTINGPOLICY_MSWCHAR", CXPrintingPolicy_MSWChar},
      {"CINDEXTEST_PRINTINGPOLICY_INCLUDENEWLINES",
       CXPrintingPolicy_IncludeNewlines},
      {"CINDEXTEST_PRINTINGPOLICY_MSVCFORMATTING",
       CXPrintingPolicy_MSVCFormatting},
      {"CINDEXTEST_PRINTINGPOLICY_CONSTANTSASWRITTEN",
       CXPrintingPolicy_ConstantsAsWritten},
      {"CINDEXTEST_PRINTINGPOLICY_SUPPRESSIMPLICITBASE",
       CXPrintingPolicy_SuppressImplicitBase},
      {"CINDEXTEST_PRINTINGPOLICY_FULLYQUALIFIEDNAME",
       CXPrintingPolicy_FullyQualifiedName},
  };

  unsigned i;
  for (i = 0; i < sizeof(mappings) / sizeof(struct Mapping); i++) {
    char *value = getenv(mappings[i].name);
    if (value) {
      clang_PrintingPolicy_setProperty(Policy, mappings[i].property,
                                       (unsigned)strtoul(value, 0L, 10));
    }
  }
}

/** Returns 0 in case of success, non-zero in case of a failure. */
static int checkForErrors(CXTranslationUnit TU);

static void describeLibclangFailure(enum CXErrorCode Err) {
  switch (Err) {
  case CXError_Success:
    fprintf(stderr, "Success\n");
    return;

  case CXError_Failure:
    fprintf(stderr, "Failure (no details available)\n");
    return;

  case CXError_Crashed:
    fprintf(stderr, "Failure: libclang crashed\n");
    return;

  case CXError_InvalidArguments:
    fprintf(stderr, "Failure: invalid arguments passed to a libclang routine\n");
    return;

  case CXError_ASTReadError:
    fprintf(stderr, "Failure: AST deserialization error occurred\n");
    return;
  }
}

static void PrintExtent(FILE *out, unsigned begin_line, unsigned begin_column,
                        unsigned end_line, unsigned end_column) {
  fprintf(out, "[%d:%d - %d:%d]", begin_line, begin_column,
          end_line, end_column);
}

static unsigned CreateTranslationUnit(CXIndex Idx, const char *file,
                                      CXTranslationUnit *TU) {
  enum CXErrorCode Err = clang_createTranslationUnit2(Idx, file, TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit from '%s'!\n", file);
    describeLibclangFailure(Err);
    *TU = 0;
    return 0;
  }
  return 1;
}

void free_remapped_files(struct CXUnsavedFile *unsaved_files,
                         int num_unsaved_files) {
  int i;
  for (i = 0; i != num_unsaved_files; ++i) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
#endif
    free((char *)unsaved_files[i].Filename);
    free((char *)unsaved_files[i].Contents);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif
  }
  free(unsaved_files);
}

static int parse_remapped_files_with_opt(const char *opt_name,
                                         int argc, const char **argv,
                                         int start_arg,
                                         struct CXUnsavedFile **unsaved_files,
                                         int *num_unsaved_files) {
  int i;
  int arg;
  int prefix_len = strlen(opt_name);
  int arg_indices[20];
  *unsaved_files = 0;
  *num_unsaved_files = 0;

  /* Count the number of remapped files. */
  for (arg = start_arg; arg < argc; ++arg) {
    if (strncmp(argv[arg], opt_name, prefix_len))
      continue;

    assert(*num_unsaved_files < (int)(sizeof(arg_indices)/sizeof(int)));
    arg_indices[*num_unsaved_files] = arg;
    ++*num_unsaved_files;
  }

  if (*num_unsaved_files == 0)
    return 0;

  *unsaved_files
    = (struct CXUnsavedFile *)malloc(sizeof(struct CXUnsavedFile) *
                                     *num_unsaved_files);
  assert(*unsaved_files);
  for (i = 0; i != *num_unsaved_files; ++i) {
    struct CXUnsavedFile *unsaved = *unsaved_files + i;
    const char *arg_string = argv[arg_indices[i]] + prefix_len;
    int filename_len;
    char *filename;
    char *contents;
    FILE *to_file;
    const char *sep = strchr(arg_string, ',');
    if (!sep) {
      fprintf(stderr,
              "error: %sfrom:to argument is missing comma\n", opt_name);
      free_remapped_files(*unsaved_files, i);
      *unsaved_files = 0;
      *num_unsaved_files = 0;
      return -1;
    }

    /* Open the file that we're remapping to. */
    to_file = fopen(sep + 1, "rb");
    if (!to_file) {
      fprintf(stderr, "error: cannot open file %s that we are remapping to\n",
              sep + 1);
      free_remapped_files(*unsaved_files, i);
      *unsaved_files = 0;
      *num_unsaved_files = 0;
      return -1;
    }

    /* Determine the length of the file we're remapping to. */
    fseek(to_file, 0, SEEK_END);
    unsaved->Length = ftell(to_file);
    fseek(to_file, 0, SEEK_SET);

    /* Read the contents of the file we're remapping to. */
    contents = (char *)malloc(unsaved->Length + 1);
    assert(contents);
    if (fread(contents, 1, unsaved->Length, to_file) != unsaved->Length) {
      fprintf(stderr, "error: unexpected %s reading 'to' file %s\n",
              (feof(to_file) ? "EOF" : "error"), sep + 1);
      fclose(to_file);
      free_remapped_files(*unsaved_files, i);
      free(contents);
      *unsaved_files = 0;
      *num_unsaved_files = 0;
      return -1;
    }
    contents[unsaved->Length] = 0;
    unsaved->Contents = contents;

    /* Close the file. */
    fclose(to_file);

    /* Copy the file name that we're remapping from. */
    filename_len = sep - arg_string;
    filename = (char *)malloc(filename_len + 1);
    assert(filename);
    memcpy(filename, arg_string, filename_len);
    filename[filename_len] = 0;
    unsaved->Filename = filename;
  }

  return 0;
}

static int parse_remapped_files(int argc, const char **argv, int start_arg,
                                struct CXUnsavedFile **unsaved_files,
                                int *num_unsaved_files) {
  return parse_remapped_files_with_opt("-remap-file=", argc, argv, start_arg,
      unsaved_files, num_unsaved_files);
}

static int parse_remapped_files_with_try(int try_idx,
                                         int argc, const char **argv,
                                         int start_arg,
                                         struct CXUnsavedFile **unsaved_files,
                                         int *num_unsaved_files) {
  struct CXUnsavedFile *unsaved_files_no_try_idx;
  int num_unsaved_files_no_try_idx;
  struct CXUnsavedFile *unsaved_files_try_idx;
  int num_unsaved_files_try_idx;
  int ret;
  char opt_name[32];

  ret = parse_remapped_files(argc, argv, start_arg,
      &unsaved_files_no_try_idx, &num_unsaved_files_no_try_idx);
  if (ret)
    return ret;

  sprintf(opt_name, "-remap-file-%d=", try_idx);
  ret = parse_remapped_files_with_opt(opt_name, argc, argv, start_arg,
      &unsaved_files_try_idx, &num_unsaved_files_try_idx);
  if (ret)
    return ret;

  if (num_unsaved_files_no_try_idx == 0) {
    *unsaved_files = unsaved_files_try_idx;
    *num_unsaved_files = num_unsaved_files_try_idx;
    return 0;
  }
  if (num_unsaved_files_try_idx == 0) {
    *unsaved_files = unsaved_files_no_try_idx;
    *num_unsaved_files = num_unsaved_files_no_try_idx;
    return 0;
  }

  *num_unsaved_files = num_unsaved_files_no_try_idx + num_unsaved_files_try_idx;
  *unsaved_files
    = (struct CXUnsavedFile *)realloc(unsaved_files_no_try_idx,
                                      sizeof(struct CXUnsavedFile) *
                                        *num_unsaved_files);
  assert(*unsaved_files);
  memcpy(*unsaved_files + num_unsaved_files_no_try_idx,
         unsaved_files_try_idx, sizeof(struct CXUnsavedFile) *
            num_unsaved_files_try_idx);
  free(unsaved_files_try_idx);
  return 0;
}

static const char *parse_comments_schema(int argc, const char **argv) {
  const char *CommentsSchemaArg = "-comments-xml-schema=";
  const char *CommentSchemaFile = NULL;

  if (argc == 0)
    return CommentSchemaFile;

  if (!strncmp(argv[0], CommentsSchemaArg, strlen(CommentsSchemaArg)))
    CommentSchemaFile = argv[0] + strlen(CommentsSchemaArg);

  return CommentSchemaFile;
}

/******************************************************************************/
/* Pretty-printing.                                                           */
/******************************************************************************/

static const char *FileCheckPrefix = "CHECK";

static void PrintCString(const char *CStr) {
  if (CStr != NULL && CStr[0] != '\0') {
    for ( ; *CStr; ++CStr) {
      const char C = *CStr;
      switch (C) {
        case '\n': printf("\\n"); break;
        case '\r': printf("\\r"); break;
        case '\t': printf("\\t"); break;
        case '\v': printf("\\v"); break;
        case '\f': printf("\\f"); break;
        default:   putchar(C);    break;
      }
    }
  }
}

static void PrintCStringWithPrefix(const char *Prefix, const char *CStr) {
  printf(" %s=[", Prefix);
  PrintCString(CStr);
  printf("]");
}

static void PrintCXStringAndDispose(CXString Str) {
  PrintCString(clang_getCString(Str));
  clang_disposeString(Str);
}

static void PrintCXStringWithPrefix(const char *Prefix, CXString Str) {
  PrintCStringWithPrefix(Prefix, clang_getCString(Str));
}

static void PrintCXStringWithPrefixAndDispose(const char *Prefix,
                                              CXString Str) {
  PrintCStringWithPrefix(Prefix, clang_getCString(Str));
  clang_disposeString(Str);
}

static void PrintRange(CXSourceRange R, const char *str) {
  CXFile begin_file, end_file;
  unsigned begin_line, begin_column, end_line, end_column;

  clang_getFileLocation(clang_getRangeStart(R), &begin_file, &begin_line,
                        &begin_column, 0);
  clang_getFileLocation(clang_getRangeEnd(R), &end_file, &end_line, &end_column,
                        0);
  if (!begin_file || !end_file)
    return;

  if (str)
    printf(" %s=", str);
  PrintExtent(stdout, begin_line, begin_column, end_line, end_column);
}

static enum DisplayType {
    DisplayType_Spelling,
    DisplayType_DisplayName,
    DisplayType_Pretty
} wanted_display_type = DisplayType_Spelling;

static void printVersion(const char *Prefix, CXVersion Version) {
  if (Version.Major < 0)
    return;
  printf("%s%d", Prefix, Version.Major);
  
  if (Version.Minor < 0)
    return;
  printf(".%d", Version.Minor);

  if (Version.Subminor < 0)
    return;
  printf(".%d", Version.Subminor);
}

struct CommentASTDumpingContext {
  int IndentLevel;
};

static void DumpCXCommentInternal(struct CommentASTDumpingContext *Ctx,
                                  CXComment Comment) {
  unsigned i;
  unsigned e;
  enum CXCommentKind Kind = clang_Comment_getKind(Comment);

  Ctx->IndentLevel++;
  for (i = 0, e = Ctx->IndentLevel; i != e; ++i)
    printf("  ");

  printf("(");
  switch (Kind) {
  case CXComment_Null:
    printf("CXComment_Null");
    break;
  case CXComment_Text:
    printf("CXComment_Text");
    PrintCXStringWithPrefixAndDispose("Text",
                                      clang_TextComment_getText(Comment));
    if (clang_Comment_isWhitespace(Comment))
      printf(" IsWhitespace");
    if (clang_InlineContentComment_hasTrailingNewline(Comment))
      printf(" HasTrailingNewline");
    break;
  case CXComment_InlineCommand:
    printf("CXComment_InlineCommand");
    PrintCXStringWithPrefixAndDispose(
        "CommandName",
        clang_InlineCommandComment_getCommandName(Comment));
    switch (clang_InlineCommandComment_getRenderKind(Comment)) {
    case CXCommentInlineCommandRenderKind_Normal:
      printf(" RenderNormal");
      break;
    case CXCommentInlineCommandRenderKind_Bold:
      printf(" RenderBold");
      break;
    case CXCommentInlineCommandRenderKind_Monospaced:
      printf(" RenderMonospaced");
      break;
    case CXCommentInlineCommandRenderKind_Emphasized:
      printf(" RenderEmphasized");
      break;
    case CXCommentInlineCommandRenderKind_Anchor:
      printf(" RenderAnchor");
      break;
    }
    for (i = 0, e = clang_InlineCommandComment_getNumArgs(Comment);
         i != e; ++i) {
      printf(" Arg[%u]=", i);
      PrintCXStringAndDispose(
          clang_InlineCommandComment_getArgText(Comment, i));
    }
    if (clang_InlineContentComment_hasTrailingNewline(Comment))
      printf(" HasTrailingNewline");
    break;
  case CXComment_HTMLStartTag: {
    unsigned NumAttrs;
    printf("CXComment_HTMLStartTag");
    PrintCXStringWithPrefixAndDispose(
        "Name",
        clang_HTMLTagComment_getTagName(Comment));
    NumAttrs = clang_HTMLStartTag_getNumAttrs(Comment);
    if (NumAttrs != 0) {
      printf(" Attrs:");
      for (i = 0; i != NumAttrs; ++i) {
        printf(" ");
        PrintCXStringAndDispose(clang_HTMLStartTag_getAttrName(Comment, i));
        printf("=");
        PrintCXStringAndDispose(clang_HTMLStartTag_getAttrValue(Comment, i));
      }
    }
    if (clang_HTMLStartTagComment_isSelfClosing(Comment))
      printf(" SelfClosing");
    if (clang_InlineContentComment_hasTrailingNewline(Comment))
      printf(" HasTrailingNewline");
    break;
  }
  case CXComment_HTMLEndTag:
    printf("CXComment_HTMLEndTag");
    PrintCXStringWithPrefixAndDispose(
        "Name",
        clang_HTMLTagComment_getTagName(Comment));
    if (clang_InlineContentComment_hasTrailingNewline(Comment))
      printf(" HasTrailingNewline");
    break;
  case CXComment_Paragraph:
    printf("CXComment_Paragraph");
    if (clang_Comment_isWhitespace(Comment))
      printf(" IsWhitespace");
    break;
  case CXComment_BlockCommand:
    printf("CXComment_BlockCommand");
    PrintCXStringWithPrefixAndDispose(
        "CommandName",
        clang_BlockCommandComment_getCommandName(Comment));
    for (i = 0, e = clang_BlockCommandComment_getNumArgs(Comment);
         i != e; ++i) {
      printf(" Arg[%u]=", i);
      PrintCXStringAndDispose(
          clang_BlockCommandComment_getArgText(Comment, i));
    }
    break;
  case CXComment_ParamCommand:
    printf("CXComment_ParamCommand");
    switch (clang_ParamCommandComment_getDirection(Comment)) {
    case CXCommentParamPassDirection_In:
      printf(" in");
      break;
    case CXCommentParamPassDirection_Out:
      printf(" out");
      break;
    case CXCommentParamPassDirection_InOut:
      printf(" in,out");
      break;
    }
    if (clang_ParamCommandComment_isDirectionExplicit(Comment))
      printf(" explicitly");
    else
      printf(" implicitly");
    PrintCXStringWithPrefixAndDispose(
        "ParamName",
        clang_ParamCommandComment_getParamName(Comment));
    if (clang_ParamCommandComment_isParamIndexValid(Comment))
      printf(" ParamIndex=%u", clang_ParamCommandComment_getParamIndex(Comment));
    else
      printf(" ParamIndex=Invalid");
    break;
  case CXComment_TParamCommand:
    printf("CXComment_TParamCommand");
    PrintCXStringWithPrefixAndDispose(
        "ParamName",
        clang_TParamCommandComment_getParamName(Comment));
    if (clang_TParamCommandComment_isParamPositionValid(Comment)) {
      printf(" ParamPosition={");
      for (i = 0, e = clang_TParamCommandComment_getDepth(Comment);
           i != e; ++i) {
        printf("%u", clang_TParamCommandComment_getIndex(Comment, i));
        if (i != e - 1)
          printf(", ");
      }
      printf("}");
    } else
      printf(" ParamPosition=Invalid");
    break;
  case CXComment_VerbatimBlockCommand:
    printf("CXComment_VerbatimBlockCommand");
    PrintCXStringWithPrefixAndDispose(
        "CommandName",
        clang_BlockCommandComment_getCommandName(Comment));
    break;
  case CXComment_VerbatimBlockLine:
    printf("CXComment_VerbatimBlockLine");
    PrintCXStringWithPrefixAndDispose(
        "Text",
        clang_VerbatimBlockLineComment_getText(Comment));
    break;
  case CXComment_VerbatimLine:
    printf("CXComment_VerbatimLine");
    PrintCXStringWithPrefixAndDispose(
        "Text",
        clang_VerbatimLineComment_getText(Comment));
    break;
  case CXComment_FullComment:
    printf("CXComment_FullComment");
    break;
  }
  if (Kind != CXComment_Null) {
    const unsigned NumChildren = clang_Comment_getNumChildren(Comment);
    unsigned i;
    for (i = 0; i != NumChildren; ++i) {
      printf("\n// %s: ", FileCheckPrefix);
      DumpCXCommentInternal(Ctx, clang_Comment_getChild(Comment, i));
    }
  }
  printf(")");
  Ctx->IndentLevel--;
}

static void DumpCXComment(CXComment Comment) {
  struct CommentASTDumpingContext Ctx;
  Ctx.IndentLevel = 1;
  printf("\n// %s:  CommentAST=[\n// %s:", FileCheckPrefix, FileCheckPrefix);
  DumpCXCommentInternal(&Ctx, Comment);
  printf("]");
}

static void ValidateCommentXML(const char *Str, const char *CommentSchemaFile) {
#ifdef CLANG_HAVE_LIBXML
  xmlRelaxNGParserCtxtPtr RNGParser;
  xmlRelaxNGPtr Schema;
  xmlDocPtr Doc;
  xmlRelaxNGValidCtxtPtr ValidationCtxt;
  int status;

  if (!CommentSchemaFile)
    return;

  RNGParser = xmlRelaxNGNewParserCtxt(CommentSchemaFile);
  if (!RNGParser) {
    printf(" libXMLError");
    return;
  }
  Schema = xmlRelaxNGParse(RNGParser);

  Doc = xmlParseDoc((const xmlChar *) Str);

  if (!Doc) {
    const xmlError *Error = xmlGetLastError();
    printf(" CommentXMLInvalid [not well-formed XML: %s]", Error->message);
    return;
  }

  ValidationCtxt = xmlRelaxNGNewValidCtxt(Schema);
  status = xmlRelaxNGValidateDoc(ValidationCtxt, Doc);
  if (!status)
    printf(" CommentXMLValid");
  else if (status > 0) {
    const xmlError *Error = xmlGetLastError();
    printf(" CommentXMLInvalid [not valid XML: %s]", Error->message);
  } else
    printf(" libXMLError");

  xmlRelaxNGFreeValidCtxt(ValidationCtxt);
  xmlFreeDoc(Doc);
  xmlRelaxNGFree(Schema);
  xmlRelaxNGFreeParserCtxt(RNGParser);
#endif
}

static void PrintCursorComments(CXCursor Cursor,
                                const char *CommentSchemaFile) {
  {
    CXString RawComment;
    const char *RawCommentCString;
    CXString BriefComment;
    const char *BriefCommentCString;

    RawComment = clang_Cursor_getRawCommentText(Cursor);
    RawCommentCString = clang_getCString(RawComment);
    if (RawCommentCString != NULL && RawCommentCString[0] != '\0') {
      PrintCStringWithPrefix("RawComment", RawCommentCString);
      PrintRange(clang_Cursor_getCommentRange(Cursor), "RawCommentRange");

      BriefComment = clang_Cursor_getBriefCommentText(Cursor);
      BriefCommentCString = clang_getCString(BriefComment);
      if (BriefCommentCString != NULL && BriefCommentCString[0] != '\0')
        PrintCStringWithPrefix("BriefComment", BriefCommentCString);
      clang_disposeString(BriefComment);
    }
    clang_disposeString(RawComment);
  }

  {
    CXComment Comment = clang_Cursor_getParsedComment(Cursor);
    if (clang_Comment_getKind(Comment) != CXComment_Null) {
      PrintCXStringWithPrefixAndDispose("FullCommentAsHTML",
                                        clang_FullComment_getAsHTML(Comment));
      {
        CXString XML;
        XML = clang_FullComment_getAsXML(Comment);
        PrintCXStringWithPrefix("FullCommentAsXML", XML);
        ValidateCommentXML(clang_getCString(XML), CommentSchemaFile);
        clang_disposeString(XML);
      }

      DumpCXComment(Comment);
    }
  }
}

typedef struct {
  unsigned line;
  unsigned col;
} LineCol;

static int lineCol_cmp(const void *p1, const void *p2) {
  const LineCol *lhs = p1;
  const LineCol *rhs = p2;
  if (lhs->line != rhs->line)
    return (int)lhs->line - (int)rhs->line;
  return (int)lhs->col - (int)rhs->col;
}

static CXString CursorToText(CXCursor Cursor) {
  CXString text;
  switch (wanted_display_type) {
  case DisplayType_Spelling:
    return clang_getCursorSpelling(Cursor);
  case DisplayType_DisplayName:
    return clang_getCursorDisplayName(Cursor);
  case DisplayType_Pretty: {
    CXPrintingPolicy Policy = clang_getCursorPrintingPolicy(Cursor);
    ModifyPrintingPolicyAccordingToEnv(Policy);
    text = clang_getCursorPrettyPrinted(Cursor, Policy);
    clang_PrintingPolicy_dispose(Policy);
    return text;
  }
  }
  assert(0 && "unknown display type"); /* no llvm_unreachable in C. */
  /* Set to NULL to prevent uninitialized variable warnings. */
  text.data = NULL;
  text.private_flags = 0;
  return text;
}

static void PrintCursor(CXCursor Cursor, const char *CommentSchemaFile) {
  CXTranslationUnit TU = clang_Cursor_getTranslationUnit(Cursor);
  if (clang_isInvalid(Cursor.kind)) {
    CXString ks = clang_getCursorKindSpelling(Cursor.kind);
    printf("Invalid Cursor => %s", clang_getCString(ks));
    clang_disposeString(ks);
  }
  else {
    CXString string, ks;
    CXCursor Referenced;
    unsigned line, column;
    CXCursor SpecializationOf;
    CXCursor *overridden;
    unsigned num_overridden;
    unsigned RefNameRangeNr;
    CXSourceRange CursorExtent;
    CXSourceRange RefNameRange;
    int AlwaysUnavailable;
    int AlwaysDeprecated;
    CXString UnavailableMessage;
    CXString DeprecatedMessage;
    CXPlatformAvailability PlatformAvailability[2];
    int NumPlatformAvailability;
    int I;

    ks = clang_getCursorKindSpelling(Cursor.kind);
    string = CursorToText(Cursor);
    printf("%s=%s", clang_getCString(ks),
                    clang_getCString(string));
    clang_disposeString(ks);
    clang_disposeString(string);

    Referenced = clang_getCursorReferenced(Cursor);
    if (!clang_equalCursors(Referenced, clang_getNullCursor())) {
      if (clang_getCursorKind(Referenced) == CXCursor_OverloadedDeclRef) {
        unsigned I, N = clang_getNumOverloadedDecls(Referenced);
        printf("[");
        for (I = 0; I != N; ++I) {
          CXCursor Ovl = clang_getOverloadedDecl(Referenced, I);
          CXSourceLocation Loc;
          if (I)
            printf(", ");
          
          Loc = clang_getCursorLocation(Ovl);
          clang_getFileLocation(Loc, 0, &line, &column, 0);
          printf("%d:%d", line, column);          
        }
        printf("]");
      } else {
        CXSourceLocation Loc = clang_getCursorLocation(Referenced);
        clang_getFileLocation(Loc, 0, &line, &column, 0);
        printf(":%d:%d", line, column);
      }

      if (clang_getCursorKind(Referenced) == CXCursor_TypedefDecl) {
        CXType T = clang_getCursorType(Referenced);
        if (clang_Type_isTransparentTagTypedef(T)) {
          CXType Underlying = clang_getTypedefDeclUnderlyingType(Referenced);
          CXString S = clang_getTypeSpelling(Underlying);
          printf(" (Transparent: %s)", clang_getCString(S));
          clang_disposeString(S);
        }
      }
    }

    if (clang_isCursorDefinition(Cursor))
      printf(" (Definition)");
    
    switch (clang_getCursorAvailability(Cursor)) {
      case CXAvailability_Available:
        break;
        
      case CXAvailability_Deprecated:
        printf(" (deprecated)");
        break;
        
      case CXAvailability_NotAvailable:
        printf(" (unavailable)");
        break;

      case CXAvailability_NotAccessible:
        printf(" (inaccessible)");
        break;
    }
    
    NumPlatformAvailability
      = clang_getCursorPlatformAvailability(Cursor,
                                            &AlwaysDeprecated,
                                            &DeprecatedMessage,
                                            &AlwaysUnavailable,
                                            &UnavailableMessage,
                                            PlatformAvailability, 2);
    if (AlwaysUnavailable) {
      printf("  (always unavailable: \"%s\")",
             clang_getCString(UnavailableMessage));
    } else if (AlwaysDeprecated) {
      printf("  (always deprecated: \"%s\")",
             clang_getCString(DeprecatedMessage));
    } else {
      for (I = 0; I != NumPlatformAvailability; ++I) {
        if (I >= 2)
          break;
        
        printf("  (%s", clang_getCString(PlatformAvailability[I].Platform));
        if (PlatformAvailability[I].Unavailable)
          printf(", unavailable");
        else {
          printVersion(", introduced=", PlatformAvailability[I].Introduced);
          printVersion(", deprecated=", PlatformAvailability[I].Deprecated);
          printVersion(", obsoleted=", PlatformAvailability[I].Obsoleted);
        }
        if (clang_getCString(PlatformAvailability[I].Message)[0])
          printf(", message=\"%s\"",
                 clang_getCString(PlatformAvailability[I].Message));
        printf(")");
      }
    }
    for (I = 0; I != NumPlatformAvailability; ++I) {
      if (I >= 2)
        break;
      clang_disposeCXPlatformAvailability(PlatformAvailability + I);
    }
    
    clang_disposeString(DeprecatedMessage);
    clang_disposeString(UnavailableMessage);

    if (clang_CXXConstructor_isDefaultConstructor(Cursor))
      printf(" (default constructor)");

    if (clang_CXXConstructor_isMoveConstructor(Cursor))
      printf(" (move constructor)");
    if (clang_CXXConstructor_isCopyConstructor(Cursor))
      printf(" (copy constructor)");
    if (clang_CXXConstructor_isConvertingConstructor(Cursor))
      printf(" (converting constructor)");
    if (clang_CXXField_isMutable(Cursor))
      printf(" (mutable)");
    if (clang_CXXMethod_isDefaulted(Cursor))
      printf(" (defaulted)");
    if (clang_CXXMethod_isDeleted(Cursor))
      printf(" (deleted)");
    if (clang_CXXMethod_isStatic(Cursor))
      printf(" (static)");
    if (clang_CXXMethod_isVirtual(Cursor))
      printf(" (virtual)");
    if (clang_CXXMethod_isConst(Cursor))
      printf(" (const)");
    if (clang_CXXMethod_isPureVirtual(Cursor))
      printf(" (pure)");
    if (clang_CXXMethod_isCopyAssignmentOperator(Cursor))
      printf(" (copy-assignment operator)");
    if (clang_CXXMethod_isMoveAssignmentOperator(Cursor))
      printf(" (move-assignment operator)");
    if (clang_CXXMethod_isExplicit(Cursor))
      printf(" (explicit)");
    if (clang_CXXRecord_isAbstract(Cursor))
      printf(" (abstract)");
    if (clang_EnumDecl_isScoped(Cursor))
      printf(" (scoped)");
    if (clang_Cursor_isVariadic(Cursor))
      printf(" (variadic)");
    if (clang_Cursor_isObjCOptional(Cursor))
      printf(" (@optional)");
    if (clang_isInvalidDeclaration(Cursor))
      printf(" (invalid)");

    switch (clang_getCursorExceptionSpecificationType(Cursor))
    {
      case CXCursor_ExceptionSpecificationKind_None:
        break;

      case CXCursor_ExceptionSpecificationKind_DynamicNone:
        printf(" (noexcept dynamic none)");
        break;

      case CXCursor_ExceptionSpecificationKind_Dynamic:
        printf(" (noexcept dynamic)");
        break;

      case CXCursor_ExceptionSpecificationKind_MSAny:
        printf(" (noexcept dynamic any)");
        break;

      case CXCursor_ExceptionSpecificationKind_BasicNoexcept:
        printf(" (noexcept)");
        break;

      case CXCursor_ExceptionSpecificationKind_ComputedNoexcept:
        printf(" (computed-noexcept)");
        break;

      case CXCursor_ExceptionSpecificationKind_Unevaluated:
      case CXCursor_ExceptionSpecificationKind_Uninstantiated:
      case CXCursor_ExceptionSpecificationKind_Unparsed:
        break;
    }

    {
      CXString language;
      CXString definedIn;
      unsigned generated;
      if (clang_Cursor_isExternalSymbol(Cursor, &language, &definedIn,
                                        &generated)) {
        printf(" (external lang: %s, defined: %s, gen: %d)",
            clang_getCString(language), clang_getCString(definedIn), generated);
        clang_disposeString(language);
        clang_disposeString(definedIn);
      }
    }

    if (Cursor.kind == CXCursor_IBOutletCollectionAttr) {
      CXType T =
        clang_getCanonicalType(clang_getIBOutletCollectionType(Cursor));
      CXString S = clang_getTypeKindSpelling(T.kind);
      printf(" [IBOutletCollection=%s]", clang_getCString(S));
      clang_disposeString(S);
    }
    
    if (Cursor.kind == CXCursor_CXXBaseSpecifier) {
      enum CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(Cursor);
      unsigned isVirtual = clang_isVirtualBase(Cursor);
      const char *accessStr = 0;

      switch (access) {
        case CX_CXXInvalidAccessSpecifier:
          accessStr = "invalid"; break;
        case CX_CXXPublic:
          accessStr = "public"; break;
        case CX_CXXProtected:
          accessStr = "protected"; break;
        case CX_CXXPrivate:
          accessStr = "private"; break;
      }      
      
      printf(" [access=%s isVirtual=%s]", accessStr,
             isVirtual ? "true" : "false");
    }

    SpecializationOf = clang_getSpecializedCursorTemplate(Cursor);
    if (!clang_equalCursors(SpecializationOf, clang_getNullCursor())) {
      CXSourceLocation Loc = clang_getCursorLocation(SpecializationOf);
      CXString Name = clang_getCursorSpelling(SpecializationOf);
      clang_getFileLocation(Loc, 0, &line, &column, 0);
      printf(" [Specialization of %s:%d:%d]",
             clang_getCString(Name), line, column);
      clang_disposeString(Name);

      if (Cursor.kind == CXCursor_FunctionDecl
          || Cursor.kind == CXCursor_StructDecl
          || Cursor.kind == CXCursor_ClassDecl
          || Cursor.kind == CXCursor_ClassTemplatePartialSpecialization) {
        /* Collect the template parameter kinds from the base template. */
        int NumTemplateArgs = clang_Cursor_getNumTemplateArguments(Cursor);
        int I;
        if (NumTemplateArgs < 0) {
          printf(" [no template arg info]");
        }
        for (I = 0; I < NumTemplateArgs; I++) {
          enum CXTemplateArgumentKind TAK =
              clang_Cursor_getTemplateArgumentKind(Cursor, I);
          switch(TAK) {
            case CXTemplateArgumentKind_Type:
              {
                CXType T = clang_Cursor_getTemplateArgumentType(Cursor, I);
                CXString S = clang_getTypeSpelling(T);
                printf(" [Template arg %d: kind: %d, type: %s]",
                       I, TAK, clang_getCString(S));
                clang_disposeString(S);
              }
              break;
            case CXTemplateArgumentKind_Integral:
              printf(" [Template arg %d: kind: %d, intval: %lld]",
                     I, TAK, clang_Cursor_getTemplateArgumentValue(Cursor, I));
              break;
            default:
              printf(" [Template arg %d: kind: %d]\n", I, TAK);
          }
        }
      }
    }

    clang_getOverriddenCursors(Cursor, &overridden, &num_overridden);
    if (num_overridden) {      
      unsigned I;
      LineCol lineCols[50];
      assert(num_overridden <= 50);
      printf(" [Overrides ");
      for (I = 0; I != num_overridden; ++I) {
        CXSourceLocation Loc = clang_getCursorLocation(overridden[I]);
        clang_getFileLocation(Loc, 0, &line, &column, 0);
        lineCols[I].line = line;
        lineCols[I].col = column;
      }
      /* Make the order of the override list deterministic. */
      qsort(lineCols, num_overridden, sizeof(LineCol), lineCol_cmp);
      for (I = 0; I != num_overridden; ++I) {
        if (I)
          printf(", ");
        printf("@%d:%d", lineCols[I].line, lineCols[I].col);
      }
      printf("]");
      clang_disposeOverriddenCursors(overridden);
    }
    
    if (Cursor.kind == CXCursor_InclusionDirective) {
      CXFile File = clang_getIncludedFile(Cursor);
      CXString Included = clang_getFileName(File);
      const char *IncludedString = clang_getCString(Included);
      printf(" (%s)", IncludedString ? IncludedString : "(null)");
      clang_disposeString(Included);
      
      if (clang_isFileMultipleIncludeGuarded(TU, File))
        printf("  [multi-include guarded]");
    }
    
    CursorExtent = clang_getCursorExtent(Cursor);
    RefNameRange = clang_getCursorReferenceNameRange(Cursor, 
                                                   CXNameRange_WantQualifier
                                                 | CXNameRange_WantSinglePiece
                                                 | CXNameRange_WantTemplateArgs,
                                                     0);
    if (!clang_equalRanges(CursorExtent, RefNameRange))
      PrintRange(RefNameRange, "SingleRefName");
    
    for (RefNameRangeNr = 0; 1; RefNameRangeNr++) {
      RefNameRange = clang_getCursorReferenceNameRange(Cursor, 
                                                   CXNameRange_WantQualifier
                                                 | CXNameRange_WantTemplateArgs,
                                                       RefNameRangeNr);
      if (clang_equalRanges(clang_getNullRange(), RefNameRange))
        break;
      if (!clang_equalRanges(CursorExtent, RefNameRange))
        PrintRange(RefNameRange, "RefName");
    }

    PrintCursorComments(Cursor, CommentSchemaFile);

    {
      unsigned PropAttrs = clang_Cursor_getObjCPropertyAttributes(Cursor, 0);
      if (PropAttrs != CXObjCPropertyAttr_noattr) {
        printf(" [");
        #define PRINT_PROP_ATTR(A) \
          if (PropAttrs & CXObjCPropertyAttr_##A) printf(#A ",")
        PRINT_PROP_ATTR(readonly);
        PRINT_PROP_ATTR(getter);
        PRINT_PROP_ATTR(assign);
        PRINT_PROP_ATTR(readwrite);
        PRINT_PROP_ATTR(retain);
        PRINT_PROP_ATTR(copy);
        PRINT_PROP_ATTR(nonatomic);
        PRINT_PROP_ATTR(setter);
        PRINT_PROP_ATTR(atomic);
        PRINT_PROP_ATTR(weak);
        PRINT_PROP_ATTR(strong);
        PRINT_PROP_ATTR(unsafe_unretained);
        PRINT_PROP_ATTR(class);
        printf("]");
      }
    }

    if (Cursor.kind == CXCursor_ObjCPropertyDecl) {
      CXString Name = clang_Cursor_getObjCPropertyGetterName(Cursor);
      CXString Spelling = clang_getCursorSpelling(Cursor);
      const char *CName = clang_getCString(Name);
      const char *CSpelling = clang_getCString(Spelling);
      if (CName && strcmp(CName, CSpelling)) {
        printf(" (getter=%s)", CName);
      }
      clang_disposeString(Spelling);
      clang_disposeString(Name);
    }

    if (Cursor.kind == CXCursor_ObjCPropertyDecl) {
      CXString Name = clang_Cursor_getObjCPropertySetterName(Cursor);
      CXString Spelling = clang_getCursorSpelling(Cursor);
      const char *CName = clang_getCString(Name);
      const char *CSpelling = clang_getCString(Spelling);
      char *DefaultSetter = malloc(strlen(CSpelling) + 5);
      sprintf(DefaultSetter, "set%s:", CSpelling);
      DefaultSetter[3] &= ~(1 << 5); /* Make uppercase */
      if (CName && strcmp(CName, DefaultSetter)) {
        printf(" (setter=%s)", CName);
      }
      free(DefaultSetter);
      clang_disposeString(Spelling);
      clang_disposeString(Name);
    }

    {
      unsigned QT = clang_Cursor_getObjCDeclQualifiers(Cursor);
      if (QT != CXObjCDeclQualifier_None) {
        printf(" [");
        #define PRINT_OBJC_QUAL(A) \
          if (QT & CXObjCDeclQualifier_##A) printf(#A ",")
        PRINT_OBJC_QUAL(In);
        PRINT_OBJC_QUAL(Inout);
        PRINT_OBJC_QUAL(Out);
        PRINT_OBJC_QUAL(Bycopy);
        PRINT_OBJC_QUAL(Byref);
        PRINT_OBJC_QUAL(Oneway);
        printf("]");
      }
    }
  }
}

static const char* GetCursorSource(CXCursor Cursor) {
  CXSourceLocation Loc = clang_getCursorLocation(Cursor);
  CXString source;
  CXFile file;
  clang_getExpansionLocation(Loc, &file, 0, 0, 0);
  source = clang_getFileName(file);
  if (!clang_getCString(source)) {
    clang_disposeString(source);
    return "<invalid loc>";
  }
  else {
    const char *b = basename(clang_getCString(source));
    clang_disposeString(source);
    return b;
  }
}

static CXString createCXString(const char *CS) {
  CXString Str;
  Str.data = (const void *) CS;
  Str.private_flags = 0;
  return Str;
}

/******************************************************************************/
/* Callbacks.                                                                 */
/******************************************************************************/

typedef void (*PostVisitTU)(CXTranslationUnit);

void PrintDiagnostic(CXDiagnostic Diagnostic) {
  FILE *out = stderr;
  CXFile file;
  CXString Msg;
  unsigned display_opts = CXDiagnostic_DisplaySourceLocation
    | CXDiagnostic_DisplayColumn | CXDiagnostic_DisplaySourceRanges
    | CXDiagnostic_DisplayOption;
  unsigned i, num_fixits;

  if (clang_getDiagnosticSeverity(Diagnostic) == CXDiagnostic_Ignored)
    return;

  Msg = clang_formatDiagnostic(Diagnostic, display_opts);
  fprintf(stderr, "%s\n", clang_getCString(Msg));
  clang_disposeString(Msg);

  clang_getFileLocation(clang_getDiagnosticLocation(Diagnostic), &file, 0, 0,
                        0);
  if (!file)
    return;

  num_fixits = clang_getDiagnosticNumFixIts(Diagnostic);
  fprintf(stderr, "Number FIX-ITs = %d\n", num_fixits);
  for (i = 0; i != num_fixits; ++i) {
    CXSourceRange range;
    CXString insertion_text = clang_getDiagnosticFixIt(Diagnostic, i, &range);
    CXSourceLocation start = clang_getRangeStart(range);
    CXSourceLocation end = clang_getRangeEnd(range);
    unsigned start_line, start_column, end_line, end_column;
    CXFile start_file, end_file;
    clang_getFileLocation(start, &start_file, &start_line, &start_column, 0);
    clang_getFileLocation(end, &end_file, &end_line, &end_column, 0);
    if (clang_equalLocations(start, end)) {
      /* Insertion. */
      if (start_file == file)
        fprintf(out, "FIX-IT: Insert \"%s\" at %d:%d\n",
                clang_getCString(insertion_text), start_line, start_column);
    } else if (strcmp(clang_getCString(insertion_text), "") == 0) {
      /* Removal. */
      if (start_file == file && end_file == file) {
        fprintf(out, "FIX-IT: Remove ");
        PrintExtent(out, start_line, start_column, end_line, end_column);
        fprintf(out, "\n");
      }
    } else {
      /* Replacement. */
      if (start_file == end_file) {
        fprintf(out, "FIX-IT: Replace ");
        PrintExtent(out, start_line, start_column, end_line, end_column);
        fprintf(out, " with \"%s\"\n", clang_getCString(insertion_text));
      }
    }
    clang_disposeString(insertion_text);
  }
}

void PrintDiagnosticSet(CXDiagnosticSet Set) {
  int i = 0, n = clang_getNumDiagnosticsInSet(Set);
  for ( ; i != n ; ++i) {
    CXDiagnostic Diag = clang_getDiagnosticInSet(Set, i);
    CXDiagnosticSet ChildDiags = clang_getChildDiagnostics(Diag);
    PrintDiagnostic(Diag);
    if (ChildDiags)
      PrintDiagnosticSet(ChildDiags);
  }  
}

void PrintDiagnostics(CXTranslationUnit TU) {
  CXDiagnosticSet TUSet = clang_getDiagnosticSetFromTU(TU);
  PrintDiagnosticSet(TUSet);
  clang_disposeDiagnosticSet(TUSet);
}

void PrintMemoryUsage(CXTranslationUnit TU) {
  unsigned long total = 0;
  unsigned i = 0;
  CXTUResourceUsage usage = clang_getCXTUResourceUsage(TU);
  fprintf(stderr, "Memory usage:\n");
  for (i = 0 ; i != usage.numEntries; ++i) {
    const char *name = clang_getTUResourceUsageName(usage.entries[i].kind);
    unsigned long amount = usage.entries[i].amount;
    total += amount;
    fprintf(stderr, "  %s : %ld bytes (%f MBytes)\n", name, amount,
            ((double) amount)/(1024*1024));
  }
  fprintf(stderr, "  TOTAL = %ld bytes (%f MBytes)\n", total,
          ((double) total)/(1024*1024));
  clang_disposeCXTUResourceUsage(usage);  
}

/******************************************************************************/
/* Logic for testing traversal.                                               */
/******************************************************************************/

static void PrintCursorExtent(CXCursor C) {
  CXSourceRange extent = clang_getCursorExtent(C);
  PrintRange(extent, "Extent");
}

/* Data used by the visitors. */
typedef struct {
  CXTranslationUnit TU;
  enum CXCursorKind *Filter;
  const char *CommentSchemaFile;
} VisitorData;


enum CXChildVisitResult FilteredPrintingVisitor(CXCursor Cursor,
                                                CXCursor Parent,
                                                CXClientData ClientData) {
  VisitorData *Data = (VisitorData *)ClientData;
  if (!Data->Filter || (Cursor.kind == *(enum CXCursorKind *)Data->Filter)) {
    CXSourceLocation Loc = clang_getCursorLocation(Cursor);
    unsigned line, column;
    clang_getFileLocation(Loc, 0, &line, &column, 0);
    printf("// %s: %s:%d:%d: ", FileCheckPrefix,
           GetCursorSource(Cursor), line, column);
    PrintCursor(Cursor, Data->CommentSchemaFile);
    PrintCursorExtent(Cursor);
    if (clang_isDeclaration(Cursor.kind)) {
      enum CX_CXXAccessSpecifier access = clang_getCXXAccessSpecifier(Cursor);
      const char *accessStr = 0;

      switch (access) {
        case CX_CXXInvalidAccessSpecifier: break;
        case CX_CXXPublic:
          accessStr = "public"; break;
        case CX_CXXProtected:
          accessStr = "protected"; break;
        case CX_CXXPrivate:
          accessStr = "private"; break;
      }

      if (accessStr)
        printf(" [access=%s]", accessStr);
    }
    printf("\n");
    return CXChildVisit_Recurse;
  }

  return CXChildVisit_Continue;
}

static enum CXChildVisitResult FunctionScanVisitor(CXCursor Cursor,
                                                   CXCursor Parent,
                                                   CXClientData ClientData) {
  const char *startBuf, *endBuf;
  unsigned startLine, startColumn, endLine, endColumn, curLine, curColumn;
  CXCursor Ref;
  VisitorData *Data = (VisitorData *)ClientData;

  if (Cursor.kind != CXCursor_FunctionDecl ||
      !clang_isCursorDefinition(Cursor))
    return CXChildVisit_Continue;

  clang_getDefinitionSpellingAndExtent(Cursor, &startBuf, &endBuf,
                                       &startLine, &startColumn,
                                       &endLine, &endColumn);
  /* Probe the entire body, looking for both decls and refs. */
  curLine = startLine;
  curColumn = startColumn;

  while (startBuf < endBuf) {
    CXSourceLocation Loc;
    CXFile file;
    CXString source;

    if (*startBuf == '\n') {
      startBuf++;
      curLine++;
      curColumn = 1;
    } else if (*startBuf != '\t')
      curColumn++;

    Loc = clang_getCursorLocation(Cursor);
    clang_getFileLocation(Loc, &file, 0, 0, 0);

    source = clang_getFileName(file);
    if (clang_getCString(source)) {
      CXSourceLocation RefLoc
        = clang_getLocation(Data->TU, file, curLine, curColumn);
      Ref = clang_getCursor(Data->TU, RefLoc);
      if (Ref.kind == CXCursor_NoDeclFound) {
        /* Nothing found here; that's fine. */
      } else if (Ref.kind != CXCursor_FunctionDecl) {
        printf("// %s: %s:%d:%d: ", FileCheckPrefix, GetCursorSource(Ref),
               curLine, curColumn);
        PrintCursor(Ref, Data->CommentSchemaFile);
        printf("\n");
      }
    }
    clang_disposeString(source);
    startBuf++;
  }

  return CXChildVisit_Continue;
}

/******************************************************************************/
/* USR testing.                                                               */
/******************************************************************************/

enum CXChildVisitResult USRVisitor(CXCursor C, CXCursor parent,
                                   CXClientData ClientData) {
  VisitorData *Data = (VisitorData *)ClientData;
  if (!Data->Filter || (C.kind == *(enum CXCursorKind *)Data->Filter)) {
    CXString USR = clang_getCursorUSR(C);
    const char *cstr = clang_getCString(USR);
    if (!cstr || cstr[0] == '\0') {
      clang_disposeString(USR);
      return CXChildVisit_Recurse;
    }
    printf("// %s: %s %s", FileCheckPrefix, GetCursorSource(C), cstr);

    PrintCursorExtent(C);
    printf("\n");
    clang_disposeString(USR);

    return CXChildVisit_Recurse;
  }

  return CXChildVisit_Continue;
}

/******************************************************************************/
/* Inclusion stack testing.                                                   */
/******************************************************************************/

void InclusionVisitor(CXFile includedFile, CXSourceLocation *includeStack,
                      unsigned includeStackLen, CXClientData data) {

  unsigned i;
  CXString fname;

  fname = clang_getFileName(includedFile);
  printf("file: %s\nincluded by:\n", clang_getCString(fname));
  clang_disposeString(fname);

  for (i = 0; i < includeStackLen; ++i) {
    CXFile includingFile;
    unsigned line, column;
    clang_getFileLocation(includeStack[i], &includingFile, &line, &column, 0);
    fname = clang_getFileName(includingFile);
    printf("  %s:%d:%d\n", clang_getCString(fname), line, column);
    clang_disposeString(fname);
  }
  printf("\n");
}

void PrintInclusionStack(CXTranslationUnit TU) {
  clang_getInclusions(TU, InclusionVisitor, NULL);
}

/******************************************************************************/
/* Linkage testing.                                                           */
/******************************************************************************/

static enum CXChildVisitResult PrintLinkage(CXCursor cursor, CXCursor p,
                                            CXClientData d) {
  const char *linkage = 0;

  if (clang_isInvalid(clang_getCursorKind(cursor)))
    return CXChildVisit_Recurse;

  switch (clang_getCursorLinkage(cursor)) {
    case CXLinkage_Invalid: break;
    case CXLinkage_NoLinkage: linkage = "NoLinkage"; break;
    case CXLinkage_Internal: linkage = "Internal"; break;
    case CXLinkage_UniqueExternal: linkage = "UniqueExternal"; break;
    case CXLinkage_External: linkage = "External"; break;
  }

  if (linkage) {
    PrintCursor(cursor, NULL);
    printf("linkage=%s\n", linkage);
  }

  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Visibility testing.                                                        */
/******************************************************************************/

static enum CXChildVisitResult PrintVisibility(CXCursor cursor, CXCursor p,
                                               CXClientData d) {
  const char *visibility = 0;

  if (clang_isInvalid(clang_getCursorKind(cursor)))
    return CXChildVisit_Recurse;

  switch (clang_getCursorVisibility(cursor)) {
    case CXVisibility_Invalid: break;
    case CXVisibility_Hidden: visibility = "Hidden"; break;
    case CXVisibility_Protected: visibility = "Protected"; break;
    case CXVisibility_Default: visibility = "Default"; break;
  }

  if (visibility) {
    PrintCursor(cursor, NULL);
    printf("visibility=%s\n", visibility);
  }

  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Typekind testing.                                                          */
/******************************************************************************/

static void PrintTypeAndTypeKind(CXType T, const char *Format) {
  CXString TypeSpelling, TypeKindSpelling;

  TypeSpelling = clang_getTypeSpelling(T);
  TypeKindSpelling = clang_getTypeKindSpelling(T.kind);
  printf(Format,
         clang_getCString(TypeSpelling),
         clang_getCString(TypeKindSpelling));
  clang_disposeString(TypeSpelling);
  clang_disposeString(TypeKindSpelling);
}

static enum CXVisitorResult FieldVisitor(CXCursor C,
                                         CXClientData client_data) {
    (*(int *) client_data)+=1;
    return CXVisit_Continue;
}

static void PrintTypeTemplateArgs(CXType T, const char *Format) {
  int NumTArgs = clang_Type_getNumTemplateArguments(T);
  if (NumTArgs != -1 && NumTArgs != 0) {
    int i;
    CXType TArg;
    printf(Format, NumTArgs);
    for (i = 0; i < NumTArgs; ++i) {
      TArg = clang_Type_getTemplateArgumentAsType(T, i);
      if (TArg.kind != CXType_Invalid) {
        PrintTypeAndTypeKind(TArg, " [type=%s] [typekind=%s]");
      }
    }
    /* Ensure that the returned type is invalid when indexing off-by-one. */
    TArg = clang_Type_getTemplateArgumentAsType(T, i);
    assert(TArg.kind == CXType_Invalid);
    printf("]");
  }
}

static void PrintNullabilityKind(CXType T, const char *Format) {
  enum CXTypeNullabilityKind N = clang_Type_getNullability(T);

  const char *nullability = 0;
  switch (N) {
  case CXTypeNullability_NonNull:
    nullability = "nonnull";
    break;
  case CXTypeNullability_Nullable:
    nullability = "nullable";
    break;
  case CXTypeNullability_NullableResult:
    nullability = "nullable_result";
    break;
  case CXTypeNullability_Unspecified:
    nullability = "unspecified";
    break;
  case CXTypeNullability_Invalid:
    break;
  }

  if (nullability) {
    printf(Format, nullability);
  }
}

static enum CXChildVisitResult PrintType(CXCursor cursor, CXCursor p,
                                         CXClientData d) {
  if (!clang_isInvalid(clang_getCursorKind(cursor))) {
    CXType T = clang_getCursorType(cursor);
    CXType PT = clang_getPointeeType(T);
    enum CXRefQualifierKind RQ = clang_Type_getCXXRefQualifier(T);
    PrintCursor(cursor, NULL);
    PrintTypeAndTypeKind(T, " [type=%s] [typekind=%s]");
    PrintNullabilityKind(T, " [nullability=%s]");
    if (clang_isConstQualifiedType(T))
      printf(" const");
    if (clang_isVolatileQualifiedType(T))
      printf(" volatile");
    if (clang_isRestrictQualifiedType(T))
      printf(" restrict");
    if (RQ == CXRefQualifier_LValue)
      printf(" lvalue-ref-qualifier");
    if (RQ == CXRefQualifier_RValue)
      printf(" rvalue-ref-qualifier");
    /* Print the template argument types if they exist. */
    PrintTypeTemplateArgs(T, " [templateargs/%d=");
    /* Print the canonical type if it is different. */
    {
      CXType CT = clang_getCanonicalType(T);
      if (!clang_equalTypes(T, CT)) {
        PrintTypeAndTypeKind(CT, " [canonicaltype=%s] [canonicaltypekind=%s]");
        PrintTypeTemplateArgs(CT, " [canonicaltemplateargs/%d=");
      }
    }
    /* Print the value type if it exists. */
    {
      CXType VT = clang_Type_getValueType(T);
      if (VT.kind != CXType_Invalid)
        PrintTypeAndTypeKind(VT, " [valuetype=%s] [valuetypekind=%s]");
    }
    /* Print the modified type if it exists. */
    {
      CXType MT = clang_Type_getModifiedType(T);
      if (MT.kind != CXType_Invalid) {
        PrintTypeAndTypeKind(MT, " [modifiedtype=%s] [modifiedtypekind=%s]");
      }
    }
    /* Print the return type if it exists. */
    {
      CXType RT = clang_getCursorResultType(cursor);
      if (RT.kind != CXType_Invalid) {
        PrintTypeAndTypeKind(RT, " [resulttype=%s] [resulttypekind=%s]");
      }
      PrintNullabilityKind(RT, " [resultnullability=%s]");
    }
    /* Print the argument types if they exist. */
    {
      int NumArgs = clang_Cursor_getNumArguments(cursor);
      if (NumArgs != -1 && NumArgs != 0) {
        int i;
        printf(" [args=");
        for (i = 0; i < NumArgs; ++i) {
          CXType T = clang_getCursorType(clang_Cursor_getArgument(cursor, i));
          if (T.kind != CXType_Invalid) {
            PrintTypeAndTypeKind(T, " [%s] [%s]");
            PrintNullabilityKind(T, " [%s]");
          }
        }
        printf("]");
      }
    }
    /* Print ObjC base types, type arguments, and protocol list if available. */
    {
      CXType BT = clang_Type_getObjCObjectBaseType(PT);
      if (BT.kind != CXType_Invalid) {
        PrintTypeAndTypeKind(BT, " [basetype=%s] [basekind=%s]");
      }
    }
    {
      unsigned NumTypeArgs = clang_Type_getNumObjCTypeArgs(PT);
      if (NumTypeArgs > 0) {
        unsigned i;
        printf(" [typeargs=");
        for (i = 0; i < NumTypeArgs; ++i) {
          CXType TA = clang_Type_getObjCTypeArg(PT, i);
          if (TA.kind != CXType_Invalid) {
            PrintTypeAndTypeKind(TA, " [%s] [%s]");
          }
        }
        printf("]");
      }
    }
    {
      unsigned NumProtocols = clang_Type_getNumObjCProtocolRefs(PT);
      if (NumProtocols > 0) {
        unsigned i;
        printf(" [protocols=");
        for (i = 0; i < NumProtocols; ++i) {
          CXCursor P = clang_Type_getObjCProtocolDecl(PT, i);
          if (!clang_isInvalid(clang_getCursorKind(P))) {
            PrintCursor(P, NULL);
          }
        }
        printf("]");
      }
    }
    /* Print if this is a non-POD type. */
    printf(" [isPOD=%d]", clang_isPODType(T));
    /* Print the pointee type. */
    {
      if (PT.kind != CXType_Invalid) {
        PrintTypeAndTypeKind(PT, " [pointeetype=%s] [pointeekind=%s]");
      }
    }
    /* Print the number of fields if they exist. */
    {
      int numFields = 0;
      if (clang_Type_visitFields(T, FieldVisitor, &numFields)){
        if (numFields != 0) {
          printf(" [nbFields=%d]", numFields);
        }
      }
    }

    /* Print if it is an anonymous record or namespace. */
    {
      unsigned isAnon = clang_Cursor_isAnonymous(cursor);
      if (isAnon != 0) {
        printf(" [isAnon=%d]", isAnon);
      }
    }

    /* Print if it is an anonymous record decl */
    {
      unsigned isAnonRecDecl = clang_Cursor_isAnonymousRecordDecl(cursor);
      printf(" [isAnonRecDecl=%d]", isAnonRecDecl);
    }

    /* Print if it is an inline namespace decl */
    {
      unsigned isInlineNamespace = clang_Cursor_isInlineNamespace(cursor);
      if (isInlineNamespace != 0)
        printf(" [isInlineNamespace=%d]", isInlineNamespace);
    }

    printf("\n");
  }
  return CXChildVisit_Recurse;
}

static void PrintSingleTypeSize(CXType T, const char *TypeKindFormat,
                                const char *SizeFormat,
                                const char *AlignFormat) {
  PrintTypeAndTypeKind(T, TypeKindFormat);
  /* Print the type sizeof if applicable. */
  {
    long long Size = clang_Type_getSizeOf(T);
    if (Size >= 0 || Size < -1 ) {
      printf(SizeFormat, Size);
    }
  }
  /* Print the type alignof if applicable. */
  {
    long long Align = clang_Type_getAlignOf(T);
    if (Align >= 0 || Align < -1) {
      printf(AlignFormat, Align);
    }
  }

  /* Print the return type if it exists. */
  {
    CXType RT = clang_getResultType(T);
    if (RT.kind != CXType_Invalid)
      PrintSingleTypeSize(RT, " [resulttype=%s] [resulttypekind=%s]",
                              " [resultsizeof=%lld]", " [resultalignof=%lld]");
  }
}

static enum CXChildVisitResult PrintTypeSize(CXCursor cursor, CXCursor p,
                                             CXClientData d) {
  CXType T;
  enum CXCursorKind K = clang_getCursorKind(cursor);
  if (clang_isInvalid(K))
    return CXChildVisit_Recurse;
  T = clang_getCursorType(cursor);
  PrintCursor(cursor, NULL);
  PrintSingleTypeSize(T, " [type=%s] [typekind=%s]", " [sizeof=%lld]",
                      " [alignof=%lld]");
  /* Print the record field offset if applicable. */
  {
    CXString FieldSpelling = clang_getCursorSpelling(cursor);
    const char *FieldName = clang_getCString(FieldSpelling);
    /* recurse to get the first parent record that is not anonymous. */
    unsigned RecordIsAnonymous = 0;
    if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
      CXCursor Record;
      CXCursor Parent = p;
      do {
        Record = Parent;
        Parent = clang_getCursorSemanticParent(Record);
        RecordIsAnonymous = clang_Cursor_isAnonymous(Record);
        /* Recurse as long as the parent is a CXType_Record and the Record
           is anonymous */
      } while ( clang_getCursorType(Parent).kind == CXType_Record &&
                RecordIsAnonymous > 0);
      {
        long long Offset = clang_Type_getOffsetOf(clang_getCursorType(Record),
                                                  FieldName);
        long long Offset2 = clang_Cursor_getOffsetOfField(cursor);
        if (Offset == Offset2){
            printf(" [offsetof=%lld]", Offset);
        } else {
            /* Offsets will be different in anonymous records. */
            printf(" [offsetof=%lld/%lld]", Offset, Offset2);
        }
      }
    }
    clang_disposeString(FieldSpelling);
  }
  /* Print if its a bitfield */
  {
    int IsBitfield = clang_Cursor_isBitField(cursor);
    if (IsBitfield)
      printf(" [BitFieldSize=%d]", clang_getFieldDeclBitWidth(cursor));
  }

  printf("\n");

  return CXChildVisit_Recurse;
}

static enum CXChildVisitResult PrintBinOps(CXCursor C, CXCursor p,
                                           CXClientData d) {
  enum CXCursorKind ck = clang_getCursorKind(C);
  enum CX_BinaryOperatorKind bok;
  CXString opstr;
  if (ck != CXCursor_BinaryOperator && ck != CXCursor_CompoundAssignOperator)
    return CXChildVisit_Recurse;

  PrintCursor(C, NULL);
  bok = clang_Cursor_getBinaryOpcode(C);
  opstr = clang_Cursor_getBinaryOpcodeStr(bok);
  printf(" BinOp=%s %d\n", clang_getCString(opstr), bok);
  clang_disposeString(opstr);
  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Mangling testing.                                                          */
/******************************************************************************/

static enum CXChildVisitResult PrintMangledName(CXCursor cursor, CXCursor p,
                                                CXClientData d) {
  CXString MangledName;
  if (clang_isUnexposed(clang_getCursorKind(cursor)))
    return CXChildVisit_Recurse;
  if (clang_getCursorKind(cursor) == CXCursor_LinkageSpec)
    return CXChildVisit_Recurse;
  PrintCursor(cursor, NULL);
  MangledName = clang_Cursor_getMangling(cursor);
  printf(" [mangled=%s]\n", clang_getCString(MangledName));
  clang_disposeString(MangledName);
  return CXChildVisit_Continue;
}

static enum CXChildVisitResult PrintManglings(CXCursor cursor, CXCursor p,
                                              CXClientData d) {
  unsigned I, E;
  CXStringSet *Manglings = NULL;
  if (clang_isUnexposed(clang_getCursorKind(cursor)))
    return CXChildVisit_Recurse;
  if (!clang_isDeclaration(clang_getCursorKind(cursor)))
    return CXChildVisit_Recurse;
  if (clang_getCursorKind(cursor) == CXCursor_LinkageSpec)
    return CXChildVisit_Recurse;
  if (clang_getCursorKind(cursor) == CXCursor_ParmDecl)
    return CXChildVisit_Continue;
  PrintCursor(cursor, NULL);
  Manglings = clang_Cursor_getCXXManglings(cursor);
  if (Manglings) {
    for (I = 0, E = Manglings->Count; I < E; ++I)
      printf(" [mangled=%s]", clang_getCString(Manglings->Strings[I]));
    clang_disposeStringSet(Manglings);
    printf("\n");
  }
  Manglings = clang_Cursor_getObjCManglings(cursor);
  if (Manglings) {
    for (I = 0, E = Manglings->Count; I < E; ++I)
      printf(" [mangled=%s]", clang_getCString(Manglings->Strings[I]));
    clang_disposeStringSet(Manglings);
    printf("\n");
  }
  return CXChildVisit_Recurse;
}

static enum CXChildVisitResult
PrintSingleSymbolSGFs(CXCursor cursor, CXCursor parent, CXClientData data) {
  CXString SGFData = clang_getSymbolGraphForCursor(cursor);
  const char *SGF = clang_getCString(SGFData);
  if (SGF)
    printf("%s\n", SGF);

  clang_disposeString(SGFData);

  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Bitwidth testing.                                                          */
/******************************************************************************/

static enum CXChildVisitResult PrintBitWidth(CXCursor cursor, CXCursor p,
                                             CXClientData d) {
  int Bitwidth;
  if (clang_getCursorKind(cursor) != CXCursor_FieldDecl)
    return CXChildVisit_Recurse;

  Bitwidth = clang_getFieldDeclBitWidth(cursor);
  if (Bitwidth >= 0) {
    PrintCursor(cursor, NULL);
    printf(" bitwidth=%d\n", Bitwidth);
  }

  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Type declaration testing                                                   */
/******************************************************************************/

static enum CXChildVisitResult PrintTypeDeclaration(CXCursor cursor, CXCursor p,
                                             CXClientData d) {
  CXCursor typeDeclaration = clang_getTypeDeclaration(clang_getCursorType(cursor));

  if (clang_isDeclaration(typeDeclaration.kind)) {
    PrintCursor(cursor, NULL);
    PrintTypeAndTypeKind(clang_getCursorType(typeDeclaration), " [typedeclaration=%s] [typekind=%s]\n");
  }

  return CXChildVisit_Recurse;
}

/******************************************************************************/
/* Declaration attributes testing                                             */
/******************************************************************************/

static enum CXChildVisitResult PrintDeclAttributes(CXCursor cursor, CXCursor p,
                                                   CXClientData d) {
  if (clang_isDeclaration(cursor.kind)) {
    printf("\n");
    PrintCursor(cursor, NULL);
    return CXChildVisit_Recurse;
  } else if (clang_isAttribute(cursor.kind)) {
    printf(" ");
    PrintCursor(cursor, NULL);
  }
  return CXChildVisit_Continue;
}

/******************************************************************************/
/* Target information testing.                                                */
/******************************************************************************/

static int print_target_info(int argc, const char **argv) {
  CXIndex Idx;
  CXTranslationUnit TU;
  CXTargetInfo TargetInfo;
  CXString Triple;
  const char *FileName;
  enum CXErrorCode Err;
  int PointerWidth;

  if (argc == 0) {
    fprintf(stderr, "No filename specified\n");
    return 1;
  }

  FileName = argv[1];

  Idx = clang_createIndex(0, 1);
  Err = clang_parseTranslationUnit2(Idx, FileName, argv, argc, NULL, 0,
                                    getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Couldn't parse translation unit!\n");
    describeLibclangFailure(Err);
    clang_disposeIndex(Idx);
    return 1;
  }

  TargetInfo = clang_getTranslationUnitTargetInfo(TU);

  Triple = clang_TargetInfo_getTriple(TargetInfo);
  printf("TargetTriple: %s\n", clang_getCString(Triple));
  clang_disposeString(Triple);

  PointerWidth = clang_TargetInfo_getPointerWidth(TargetInfo);
  printf("PointerWidth: %d\n", PointerWidth);

  clang_TargetInfo_dispose(TargetInfo);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(Idx);
  return 0;
}

/******************************************************************************/
/* Loading ASTs/source.                                                       */
/******************************************************************************/

static int perform_test_load(CXIndex Idx, CXTranslationUnit TU,
                             const char *filter, const char *prefix,
                             CXCursorVisitor Visitor,
                             PostVisitTU PV,
                             const char *CommentSchemaFile) {

  if (prefix)
    FileCheckPrefix = prefix;

  if (Visitor) {
    enum CXCursorKind K = CXCursor_NotImplemented;
    enum CXCursorKind *ck = &K;
    VisitorData Data;

    /* Perform some simple filtering. */
    if (!strcmp(filter, "all") || !strcmp(filter, "local")) ck = NULL;
    else if (!strcmp(filter, "all-display") || 
             !strcmp(filter, "local-display")) {
      ck = NULL;
      wanted_display_type = DisplayType_DisplayName;
    }
    else if (!strcmp(filter, "all-pretty") ||
             !strcmp(filter, "local-pretty")) {
      ck = NULL;
      wanted_display_type = DisplayType_Pretty;
    }
    else if (!strcmp(filter, "none")) K = (enum CXCursorKind) ~0;
    else if (!strcmp(filter, "category")) K = CXCursor_ObjCCategoryDecl;
    else if (!strcmp(filter, "interface")) K = CXCursor_ObjCInterfaceDecl;
    else if (!strcmp(filter, "protocol")) K = CXCursor_ObjCProtocolDecl;
    else if (!strcmp(filter, "function")) K = CXCursor_FunctionDecl;
    else if (!strcmp(filter, "typedef")) K = CXCursor_TypedefDecl;
    else if (!strcmp(filter, "scan-function")) Visitor = FunctionScanVisitor;
    else {
      fprintf(stderr, "Unknown filter for -test-load-tu: %s\n", filter);
      return 1;
    }

    Data.TU = TU;
    Data.Filter = ck;
    Data.CommentSchemaFile = CommentSchemaFile;
    clang_visitChildren(clang_getTranslationUnitCursor(TU), Visitor, &Data);
  }

  if (PV)
    PV(TU);

  PrintDiagnostics(TU);
  if (checkForErrors(TU) != 0) {
    clang_disposeTranslationUnit(TU);
    return -1;
  }

  clang_disposeTranslationUnit(TU);
  return 0;
}

int perform_test_load_tu(const char *file, const char *filter,
                         const char *prefix, CXCursorVisitor Visitor,
                         PostVisitTU PV) {
  CXIndex Idx;
  CXTranslationUnit TU;
  int result;
  Idx = clang_createIndex(/* excludeDeclsFromPCH */
                          !strcmp(filter, "local") ? 1 : 0,
                          /* displayDiagnostics=*/1);

  if (!CreateTranslationUnit(Idx, file, &TU)) {
    clang_disposeIndex(Idx);
    return 1;
  }

  result = perform_test_load(Idx, TU, filter, prefix, Visitor, PV, NULL);
  clang_disposeIndex(Idx);
  return result;
}

int perform_test_load_source(int argc, const char **argv,
                             const char *filter, CXCursorVisitor Visitor,
                             PostVisitTU PV) {
  CXIndex Idx;
  CXTranslationUnit TU;
  const char *CommentSchemaFile;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  int result;
  unsigned Repeats = 0;
  unsigned I;

  Idx =
      createIndexWithInvocationEmissionPath(/* excludeDeclsFromPCH */
                                            (!strcmp(filter, "local") ||
                                             !strcmp(filter, "local-display") ||
                                             !strcmp(filter, "local-pretty"))
                                                ? 1
                                                : 0,
                                            /* displayDiagnostics=*/1);
  if (!Idx)
    return -1;

  if ((CommentSchemaFile = parse_comments_schema(argc, argv))) {
    argc--;
    argv++;
  }

  if (parse_remapped_files(argc, argv, 0, &unsaved_files, &num_unsaved_files)) {
    clang_disposeIndex(Idx);
    return -1;
  }

  if (getenv("CINDEXTEST_EDITING"))
    Repeats = 5;

  Err = clang_parseTranslationUnit2(Idx, 0,
                                    argv + num_unsaved_files,
                                    argc - num_unsaved_files,
                                    unsaved_files, num_unsaved_files,
                                    getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    free_remapped_files(unsaved_files, num_unsaved_files);
    clang_disposeIndex(Idx);
    return 1;
  }

  for (I = 0; I != Repeats; ++I) {
    if (checkForErrors(TU) != 0)
      return -1;

    if (Repeats > 1) {
      clang_suspendTranslationUnit(TU);

      Err = clang_reparseTranslationUnit(TU, num_unsaved_files, unsaved_files,
                                         clang_defaultReparseOptions(TU));
      if (Err != CXError_Success) {
        describeLibclangFailure(Err);
        free_remapped_files(unsaved_files, num_unsaved_files);
        clang_disposeIndex(Idx);
        return 1;
      }
    }
  }

  result = perform_test_load(Idx, TU, filter, NULL, Visitor, PV,
                             CommentSchemaFile);
  free_remapped_files(unsaved_files, num_unsaved_files);
  clang_disposeIndex(Idx);
  return result;
}

int perform_test_reparse_source(int argc, const char **argv, int trials,
                                const char *filter, CXCursorVisitor Visitor,
                                PostVisitTU PV) {
  CXIndex Idx;
  CXTranslationUnit TU;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  int compiler_arg_idx = 0;
  enum CXErrorCode Err;
  int result, i;
  int trial;
  int execute_after_trial = 0;
  const char *execute_command = NULL;
  int remap_after_trial = 0;
  char *endptr = 0;
  
  Idx = clang_createIndex(/* excludeDeclsFromPCH */
                          !strcmp(filter, "local") ? 1 : 0,
                          /* displayDiagnostics=*/1);
  
  if (parse_remapped_files(argc, argv, 0, &unsaved_files, &num_unsaved_files)) {
    clang_disposeIndex(Idx);
    return -1;
  }

  for (i = 0; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0)
      break;
  }
  if (i < argc)
    compiler_arg_idx = i+1;
  if (num_unsaved_files > compiler_arg_idx)
    compiler_arg_idx = num_unsaved_files;
  
  /* Load the initial translation unit -- we do this without honoring remapped
   * files, so that we have a way to test results after changing the source. */
  Err = clang_parseTranslationUnit2(Idx, 0,
                                    argv + compiler_arg_idx,
                                    argc - compiler_arg_idx,
                                    0, 0, getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    free_remapped_files(unsaved_files, num_unsaved_files);
    clang_disposeIndex(Idx);
    return 1;
  }
  
  if (checkForErrors(TU) != 0)
    return -1;

  if (getenv("CINDEXTEST_EXECUTE_COMMAND")) {
    execute_command = getenv("CINDEXTEST_EXECUTE_COMMAND");
  }
  if (getenv("CINDEXTEST_EXECUTE_AFTER_TRIAL")) {
    execute_after_trial =
        strtol(getenv("CINDEXTEST_EXECUTE_AFTER_TRIAL"), &endptr, 10);
  }

  if (getenv("CINDEXTEST_REMAP_AFTER_TRIAL")) {
    remap_after_trial =
        strtol(getenv("CINDEXTEST_REMAP_AFTER_TRIAL"), &endptr, 10);
  }

  for (trial = 0; trial < trials; ++trial) {
    if (execute_command && trial == execute_after_trial) {
      result = indextest_perform_shell_execution(execute_command);
      if (result != 0)
        return result;
    }

    free_remapped_files(unsaved_files, num_unsaved_files);
    if (parse_remapped_files_with_try(trial, argc, argv, 0,
                                      &unsaved_files, &num_unsaved_files)) {
      clang_disposeTranslationUnit(TU);
      clang_disposeIndex(Idx);
      return -1;
    }

    Err = clang_reparseTranslationUnit(
        TU,
        trial >= remap_after_trial ? num_unsaved_files : 0,
        trial >= remap_after_trial ? unsaved_files : 0,
        clang_defaultReparseOptions(TU));
    if (Err != CXError_Success) {
      fprintf(stderr, "Unable to reparse translation unit!\n");
      describeLibclangFailure(Err);
      clang_disposeTranslationUnit(TU);
      free_remapped_files(unsaved_files, num_unsaved_files);
      clang_disposeIndex(Idx);
      return -1;      
    }

    if (checkForErrors(TU) != 0)
      return -1;
  }
  
  result = perform_test_load(Idx, TU, filter, NULL, Visitor, PV, NULL);

  free_remapped_files(unsaved_files, num_unsaved_files);
  clang_disposeIndex(Idx);
  return result;
}

static int perform_single_file_parse(const char *filename) {
  CXIndex Idx;
  CXTranslationUnit TU;
  enum CXErrorCode Err;
  int result;

  Idx = clang_createIndex(/* excludeDeclsFromPCH */1,
                          /* displayDiagnostics=*/1);

  Err = clang_parseTranslationUnit2(Idx, filename,
                                    /*command_line_args=*/NULL,
                                    /*num_command_line_args=*/0,
                                    /*unsaved_files=*/NULL,
                                    /*num_unsaved_files=*/0,
                                    CXTranslationUnit_SingleFileParse, &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    clang_disposeIndex(Idx);
    return 1;
  }

  result = perform_test_load(Idx, TU, /*filter=*/"all", /*prefix=*/NULL, FilteredPrintingVisitor, /*PostVisit=*/NULL,
                             /*CommentSchemaFile=*/NULL);
  clang_disposeIndex(Idx);
  return result;
}

static int perform_file_retain_excluded_cb(const char *filename) {
  CXIndex Idx;
  CXTranslationUnit TU;
  enum CXErrorCode Err;
  int result;

  Idx = clang_createIndex(/* excludeDeclsFromPCH */1,
                          /* displayDiagnostics=*/1);

  Err = clang_parseTranslationUnit2(Idx, filename,
                                    /*command_line_args=*/NULL,
                                    /*num_command_line_args=*/0,
                                    /*unsaved_files=*/NULL,
                                    /*num_unsaved_files=*/0,
                                    CXTranslationUnit_RetainExcludedConditionalBlocks, &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    clang_disposeIndex(Idx);
    return 1;
  }

  result = perform_test_load(Idx, TU, /*filter=*/"all", /*prefix=*/NULL, FilteredPrintingVisitor, /*PostVisit=*/NULL,
                             /*CommentSchemaFile=*/NULL);
  clang_disposeIndex(Idx);
  return result;
}

/******************************************************************************/
/* Logic for testing clang_getCursor().                                       */
/******************************************************************************/

static void print_cursor_file_scan(CXTranslationUnit TU, CXCursor cursor,
                                   unsigned start_line, unsigned start_col,
                                   unsigned end_line, unsigned end_col,
                                   const char *prefix) {
  printf("// %s: ", FileCheckPrefix);
  if (prefix)
    printf("-%s", prefix);
  PrintExtent(stdout, start_line, start_col, end_line, end_col);
  printf(" ");
  PrintCursor(cursor, NULL);
  printf("\n");
}

static int perform_file_scan(const char *ast_file, const char *source_file,
                             const char *prefix) {
  CXIndex Idx;
  CXTranslationUnit TU;
  FILE *fp;
  CXCursor prevCursor = clang_getNullCursor();
  CXFile file;
  unsigned line = 1, col = 1;
  unsigned start_line = 1, start_col = 1;

  if (!(Idx = clang_createIndex(/* excludeDeclsFromPCH */ 1,
                                /* displayDiagnostics=*/1))) {
    fprintf(stderr, "Could not create Index\n");
    return 1;
  }

  if (!CreateTranslationUnit(Idx, ast_file, &TU))
    return 1;

  if ((fp = fopen(source_file, "r")) == NULL) {
    fprintf(stderr, "Could not open '%s'\n", source_file);
    clang_disposeTranslationUnit(TU);
    return 1;
  }

  file = clang_getFile(TU, source_file);
  for (;;) {
    CXCursor cursor;
    int c = fgetc(fp);

    if (c == '\n') {
      ++line;
      col = 1;
    } else
      ++col;

    /* Check the cursor at this position, and dump the previous one if we have
     * found something new.
     */
    cursor = clang_getCursor(TU, clang_getLocation(TU, file, line, col));
    if ((c == EOF || !clang_equalCursors(cursor, prevCursor)) &&
        prevCursor.kind != CXCursor_InvalidFile) {
      print_cursor_file_scan(TU, prevCursor, start_line, start_col,
                             line, col, prefix);
      start_line = line;
      start_col = col;
    }
    if (c == EOF)
      break;

    prevCursor = cursor;
  }

  fclose(fp);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(Idx);
  return 0;
}

/******************************************************************************/
/* Logic for testing clang code completion.                                   */
/******************************************************************************/

/* Parse file:line:column from the input string. Returns 0 on success, non-zero
   on failure. If successful, the pointer *filename will contain newly-allocated
   memory (that will be owned by the caller) to store the file name. */
int parse_file_line_column(const char *input, char **filename, unsigned *line,
                           unsigned *column, unsigned *second_line,
                           unsigned *second_column) {
  /* Find the second colon. */
  const char *last_colon = strrchr(input, ':');
  unsigned values[4], i;
  unsigned num_values = (second_line && second_column)? 4 : 2;

  char *endptr = 0;
  if (!last_colon || last_colon == input) {
    if (num_values == 4)
      fprintf(stderr, "could not parse filename:line:column:line:column in "
              "'%s'\n", input);
    else
      fprintf(stderr, "could not parse filename:line:column in '%s'\n", input);
    return 1;
  }

  for (i = 0; i != num_values; ++i) {
    const char *prev_colon;

    /* Parse the next line or column. */
    values[num_values - i - 1] = strtol(last_colon + 1, &endptr, 10);
    if (*endptr != 0 && *endptr != ':') {
      fprintf(stderr, "could not parse %s in '%s'\n",
              (i % 2 ? "column" : "line"), input);
      return 1;
    }

    if (i + 1 == num_values)
      break;

    /* Find the previous colon. */
    prev_colon = last_colon - 1;
    while (prev_colon != input && *prev_colon != ':')
      --prev_colon;
    if (prev_colon == input) {
      fprintf(stderr, "could not parse %s in '%s'\n",
              (i % 2 == 0? "column" : "line"), input);
      return 1;
    }

    last_colon = prev_colon;
  }

  *line = values[0];
  *column = values[1];

  if (second_line && second_column) {
    *second_line = values[2];
    *second_column = values[3];
  }

  /* Copy the file name. */
  *filename = (char*)malloc(last_colon - input + 1);
  assert(*filename);
  memcpy(*filename, input, last_colon - input);
  (*filename)[last_colon - input] = 0;
  return 0;
}

const char *
clang_getCompletionChunkKindSpelling(enum CXCompletionChunkKind Kind) {
  switch (Kind) {
  case CXCompletionChunk_Optional: return "Optional";
  case CXCompletionChunk_TypedText: return "TypedText";
  case CXCompletionChunk_Text: return "Text";
  case CXCompletionChunk_Placeholder: return "Placeholder";
  case CXCompletionChunk_Informative: return "Informative";
  case CXCompletionChunk_CurrentParameter: return "CurrentParameter";
  case CXCompletionChunk_LeftParen: return "LeftParen";
  case CXCompletionChunk_RightParen: return "RightParen";
  case CXCompletionChunk_LeftBracket: return "LeftBracket";
  case CXCompletionChunk_RightBracket: return "RightBracket";
  case CXCompletionChunk_LeftBrace: return "LeftBrace";
  case CXCompletionChunk_RightBrace: return "RightBrace";
  case CXCompletionChunk_LeftAngle: return "LeftAngle";
  case CXCompletionChunk_RightAngle: return "RightAngle";
  case CXCompletionChunk_Comma: return "Comma";
  case CXCompletionChunk_ResultType: return "ResultType";
  case CXCompletionChunk_Colon: return "Colon";
  case CXCompletionChunk_SemiColon: return "SemiColon";
  case CXCompletionChunk_Equal: return "Equal";
  case CXCompletionChunk_HorizontalSpace: return "HorizontalSpace";
  case CXCompletionChunk_VerticalSpace: return "VerticalSpace";
  }

  return "Unknown";
}

static int checkForErrors(CXTranslationUnit TU) {
  unsigned Num, i;
  CXDiagnostic Diag;
  CXString DiagStr;

  if (!getenv("CINDEXTEST_FAILONERROR"))
    return 0;

  Num = clang_getNumDiagnostics(TU);
  for (i = 0; i != Num; ++i) {
    Diag = clang_getDiagnostic(TU, i);
    if (clang_getDiagnosticSeverity(Diag) >= CXDiagnostic_Error) {
      DiagStr = clang_formatDiagnostic(Diag,
                                       clang_defaultDiagnosticDisplayOptions());
      fprintf(stderr, "%s\n", clang_getCString(DiagStr));
      clang_disposeString(DiagStr);
      clang_disposeDiagnostic(Diag);
      return -1;
    }
    clang_disposeDiagnostic(Diag);
  }

  return 0;
}

static void print_completion_string(CXCompletionString completion_string,
                                    FILE *file) {
  int I, N;

  N = clang_getNumCompletionChunks(completion_string);
  for (I = 0; I != N; ++I) {
    CXString text;
    const char *cstr;
    enum CXCompletionChunkKind Kind
      = clang_getCompletionChunkKind(completion_string, I);

    if (Kind == CXCompletionChunk_Optional) {
      fprintf(file, "{Optional ");
      print_completion_string(
                clang_getCompletionChunkCompletionString(completion_string, I),
                              file);
      fprintf(file, "}");
      continue;
    } 

    if (Kind == CXCompletionChunk_VerticalSpace) {
      fprintf(file, "{VerticalSpace  }");
      continue;
    }

    text = clang_getCompletionChunkText(completion_string, I);
    cstr = clang_getCString(text);
    fprintf(file, "{%s %s}",
            clang_getCompletionChunkKindSpelling(Kind),
            cstr ? cstr : "");
    clang_disposeString(text);
  }

}

static void print_line_column(CXSourceLocation location, FILE *file) {
    unsigned line, column;
    clang_getExpansionLocation(location, NULL, &line, &column, NULL);
    fprintf(file, "%d:%d", line, column);
}

static void print_token_range(CXTranslationUnit translation_unit,
                              CXSourceLocation start, FILE *file) {
  CXToken *token = clang_getToken(translation_unit, start);

  fprintf(file, "{");
  if (token != NULL) {
    CXSourceRange token_range = clang_getTokenExtent(translation_unit, *token);
    print_line_column(clang_getRangeStart(token_range), file);
    fprintf(file, "-");
    print_line_column(clang_getRangeEnd(token_range), file);
    clang_disposeTokens(translation_unit, token, 1);
  }

  fprintf(file, "}");
}

static void print_completion_result(CXTranslationUnit translation_unit,
                                    CXCodeCompleteResults *completion_results,
                                    unsigned index,
                                    FILE *file) {
  CXCompletionResult *completion_result = completion_results->Results + index;
  CXString ks = clang_getCursorKindSpelling(completion_result->CursorKind);
  unsigned annotationCount;
  enum CXCursorKind ParentKind;
  CXString ParentName;
  CXString BriefComment;
  CXString Annotation;
  const char *BriefCommentCString;
  unsigned i;
  
  fprintf(file, "%s:", clang_getCString(ks));
  clang_disposeString(ks);

  print_completion_string(completion_result->CompletionString, file);
  fprintf(file, " (%u)", 
          clang_getCompletionPriority(completion_result->CompletionString));
  switch (clang_getCompletionAvailability(completion_result->CompletionString)){
  case CXAvailability_Available:
    break;
    
  case CXAvailability_Deprecated:
    fprintf(file, " (deprecated)");
    break;
    
  case CXAvailability_NotAvailable:
    fprintf(file, " (unavailable)");
    break;

  case CXAvailability_NotAccessible:
    fprintf(file, " (inaccessible)");
    break;
  }

  annotationCount = clang_getCompletionNumAnnotations(
        completion_result->CompletionString);
  if (annotationCount) {
    unsigned i;
    fprintf(file, " (");
    for (i = 0; i < annotationCount; ++i) {
      if (i != 0)
        fprintf(file, ", ");
      Annotation =
          clang_getCompletionAnnotation(completion_result->CompletionString, i);
      fprintf(file, "\"%s\"", clang_getCString(Annotation));
      clang_disposeString(Annotation);
    }
    fprintf(file, ")");
  }

  if (!getenv("CINDEXTEST_NO_COMPLETION_PARENTS")) {
    ParentName = clang_getCompletionParent(completion_result->CompletionString,
                                           &ParentKind);
    if (ParentKind != CXCursor_NotImplemented) {
      CXString KindSpelling = clang_getCursorKindSpelling(ParentKind);
      fprintf(file, " (parent: %s '%s')",
              clang_getCString(KindSpelling),
              clang_getCString(ParentName));
      clang_disposeString(KindSpelling);
    }
    clang_disposeString(ParentName);
  }

  BriefComment = clang_getCompletionBriefComment(
                                        completion_result->CompletionString);
  BriefCommentCString = clang_getCString(BriefComment);
  if (BriefCommentCString && *BriefCommentCString != '\0') {
    fprintf(file, "(brief comment: %s)", BriefCommentCString);
  }
  clang_disposeString(BriefComment);

  for (i = 0; i < clang_getCompletionNumFixIts(completion_results, index);
       ++i) {
    CXSourceRange correction_range;
    CXString FixIt = clang_getCompletionFixIt(completion_results, index, i,
                                              &correction_range);
    fprintf(file, " (requires fix-it: ");
    print_token_range(translation_unit, clang_getRangeStart(correction_range),
                      file);
    fprintf(file, " to \"%s\")", clang_getCString(FixIt));
    clang_disposeString(FixIt);
  }

  fprintf(file, "\n");
}

void print_completion_contexts(unsigned long long contexts, FILE *file) {
  fprintf(file, "Completion contexts:\n");
  if (contexts == CXCompletionContext_Unknown) {
    fprintf(file, "Unknown\n");
  }
  if (contexts & CXCompletionContext_AnyType) {
    fprintf(file, "Any type\n");
  }
  if (contexts & CXCompletionContext_AnyValue) {
    fprintf(file, "Any value\n");
  }
  if (contexts & CXCompletionContext_ObjCObjectValue) {
    fprintf(file, "Objective-C object value\n");
  }
  if (contexts & CXCompletionContext_ObjCSelectorValue) {
    fprintf(file, "Objective-C selector value\n");
  }
  if (contexts & CXCompletionContext_CXXClassTypeValue) {
    fprintf(file, "C++ class type value\n");
  }
  if (contexts & CXCompletionContext_DotMemberAccess) {
    fprintf(file, "Dot member access\n");
  }
  if (contexts & CXCompletionContext_ArrowMemberAccess) {
    fprintf(file, "Arrow member access\n");
  }
  if (contexts & CXCompletionContext_ObjCPropertyAccess) {
    fprintf(file, "Objective-C property access\n");
  }
  if (contexts & CXCompletionContext_EnumTag) {
    fprintf(file, "Enum tag\n");
  }
  if (contexts & CXCompletionContext_UnionTag) {
    fprintf(file, "Union tag\n");
  }
  if (contexts & CXCompletionContext_StructTag) {
    fprintf(file, "Struct tag\n");
  }
  if (contexts & CXCompletionContext_ClassTag) {
    fprintf(file, "Class name\n");
  }
  if (contexts & CXCompletionContext_Namespace) {
    fprintf(file, "Namespace or namespace alias\n");
  }
  if (contexts & CXCompletionContext_NestedNameSpecifier) {
    fprintf(file, "Nested name specifier\n");
  }
  if (contexts & CXCompletionContext_ObjCInterface) {
    fprintf(file, "Objective-C interface\n");
  }
  if (contexts & CXCompletionContext_ObjCProtocol) {
    fprintf(file, "Objective-C protocol\n");
  }
  if (contexts & CXCompletionContext_ObjCCategory) {
    fprintf(file, "Objective-C category\n");
  }
  if (contexts & CXCompletionContext_ObjCInstanceMessage) {
    fprintf(file, "Objective-C instance method\n");
  }
  if (contexts & CXCompletionContext_ObjCClassMessage) {
    fprintf(file, "Objective-C class method\n");
  }
  if (contexts & CXCompletionContext_ObjCSelectorName) {
    fprintf(file, "Objective-C selector name\n");
  }
  if (contexts & CXCompletionContext_MacroName) {
    fprintf(file, "Macro name\n");
  }
  if (contexts & CXCompletionContext_NaturalLanguage) {
    fprintf(file, "Natural language\n");
  }
}

int perform_code_completion(int argc, const char **argv, int timing_only) {
  const char *input = argv[1];
  char *filename = 0;
  unsigned line;
  unsigned column;
  CXIndex CIdx;
  int errorCode;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  CXCodeCompleteResults *results = 0;
  enum CXErrorCode Err;
  CXTranslationUnit TU;
  unsigned I, Repeats = 1;
  unsigned completionOptions = clang_defaultCodeCompleteOptions();

  if (getenv("CINDEXTEST_CODE_COMPLETE_PATTERNS"))
    completionOptions |= CXCodeComplete_IncludeCodePatterns;
  if (getenv("CINDEXTEST_COMPLETION_BRIEF_COMMENTS"))
    completionOptions |= CXCodeComplete_IncludeBriefComments;
  if (getenv("CINDEXTEST_COMPLETION_SKIP_PREAMBLE"))
    completionOptions |= CXCodeComplete_SkipPreamble;
  if (getenv("CINDEXTEST_COMPLETION_INCLUDE_FIXITS"))
    completionOptions |= CXCodeComplete_IncludeCompletionsWithFixIts;
  
  if (timing_only)
    input += strlen("-code-completion-timing=");
  else
    input += strlen("-code-completion-at=");

  if ((errorCode = parse_file_line_column(input, &filename, &line, &column,
                                          0, 0)))
    return errorCode;

  if (parse_remapped_files(argc, argv, 2, &unsaved_files, &num_unsaved_files))
    return -1;

  CIdx = createIndexWithInvocationEmissionPath(0, 0);
  if (!CIdx)
    return -1;

  if (getenv("CINDEXTEST_EDITING"))
    Repeats = 5;

  Err = clang_parseTranslationUnit2(CIdx, 0,
                                    argv + num_unsaved_files + 2,
                                    argc - num_unsaved_files - 2,
                                    0, 0, getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    return 1;
  }

  Err = clang_reparseTranslationUnit(TU, 0, 0,
                                     clang_defaultReparseOptions(TU));

  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to reparse translation unit!\n");
    describeLibclangFailure(Err);
    clang_disposeTranslationUnit(TU);
    return 1;
  }

  for (I = 0; I != Repeats; ++I) {
    results = clang_codeCompleteAt(TU, filename, line, column,
                                   unsaved_files, num_unsaved_files,
                                   completionOptions);
    if (!results) {
      fprintf(stderr, "Unable to perform code completion!\n");
      return 1;
    }
    if (I != Repeats-1)
      clang_disposeCodeCompleteResults(results);
  }

  if (results) {
    unsigned i, n = results->NumResults, containerIsIncomplete = 0;
    unsigned long long contexts;
    enum CXCursorKind containerKind;
    CXString objCSelector;
    const char *selectorString;
    if (!timing_only) {      
      /* Sort the code-completion results based on the typed text. */
      clang_sortCodeCompletionResults(results->Results, results->NumResults);

      for (i = 0; i != n; ++i)
        print_completion_result(TU, results, i, stdout);
    }
    n = clang_codeCompleteGetNumDiagnostics(results);
    for (i = 0; i != n; ++i) {
      CXDiagnostic diag = clang_codeCompleteGetDiagnostic(results, i);
      PrintDiagnostic(diag);
      clang_disposeDiagnostic(diag);
    }
    
    contexts = clang_codeCompleteGetContexts(results);
    print_completion_contexts(contexts, stdout);
    
    containerKind = clang_codeCompleteGetContainerKind(results,
                                                       &containerIsIncomplete);
    
    if (containerKind != CXCursor_InvalidCode) {
      /* We have found a container */
      CXString containerUSR, containerKindSpelling;
      containerKindSpelling = clang_getCursorKindSpelling(containerKind);
      printf("Container Kind: %s\n", clang_getCString(containerKindSpelling));
      clang_disposeString(containerKindSpelling);
      
      if (containerIsIncomplete) {
        printf("Container is incomplete\n");
      }
      else {
        printf("Container is complete\n");
      }
      
      containerUSR = clang_codeCompleteGetContainerUSR(results);
      printf("Container USR: %s\n", clang_getCString(containerUSR));
      clang_disposeString(containerUSR);
    }
    
    objCSelector = clang_codeCompleteGetObjCSelector(results);
    selectorString = clang_getCString(objCSelector);
    if (selectorString && strlen(selectorString) > 0) {
      printf("Objective-C selector: %s\n", selectorString);
    }
    clang_disposeString(objCSelector);
    
    clang_disposeCodeCompleteResults(results);
  }
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(CIdx);
  free(filename);

  free_remapped_files(unsaved_files, num_unsaved_files);

  return 0;
}

typedef struct {
  char *filename;
  unsigned line;
  unsigned column;
} CursorSourceLocation;

typedef void (*cursor_handler_t)(CXCursor cursor);

static int inspect_cursor_at(int argc, const char **argv,
                             const char *locations_flag,
                             cursor_handler_t handler) {
  CXIndex CIdx;
  int errorCode;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  CXTranslationUnit TU;
  CXCursor Cursor;
  CursorSourceLocation *Locations = 0;
  unsigned NumLocations = 0, Loc;
  unsigned Repeats = 1;
  unsigned I;
  
  /* Count the number of locations. */
  while (strstr(argv[NumLocations+1], locations_flag) == argv[NumLocations+1])
    ++NumLocations;

  /* Parse the locations. */
  assert(NumLocations > 0 && "Unable to count locations?");
  Locations = (CursorSourceLocation *)malloc(
                                  NumLocations * sizeof(CursorSourceLocation));
  assert(Locations);
  for (Loc = 0; Loc < NumLocations; ++Loc) {
    const char *input = argv[Loc + 1] + strlen(locations_flag);
    if ((errorCode = parse_file_line_column(input, &Locations[Loc].filename,
                                            &Locations[Loc].line,
                                            &Locations[Loc].column, 0, 0)))
      return errorCode;
  }

  if (parse_remapped_files(argc, argv, NumLocations + 1, &unsaved_files,
                           &num_unsaved_files))
    return -1;

  if (getenv("CINDEXTEST_EDITING"))
    Repeats = 5;

  /* Parse the translation unit. When we're testing clang_getCursor() after
     reparsing, don't remap unsaved files until the second parse. */
  CIdx = clang_createIndex(1, 1);
  Err = clang_parseTranslationUnit2(CIdx, argv[argc - 1],
                                   argv + num_unsaved_files + 1 + NumLocations,
                                   argc - num_unsaved_files - 2 - NumLocations,
                                   unsaved_files,
                                   Repeats > 1? 0 : num_unsaved_files,
                                   getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "unable to parse input\n");
    describeLibclangFailure(Err);
    return -1;
  }

  if (checkForErrors(TU) != 0)
    return -1;

  for (I = 0; I != Repeats; ++I) {
    if (Repeats > 1) {
      Err = clang_reparseTranslationUnit(TU, num_unsaved_files, unsaved_files,
                                         clang_defaultReparseOptions(TU));
      if (Err != CXError_Success) {
        describeLibclangFailure(Err);
        clang_disposeTranslationUnit(TU);
        return 1;
      }
    }

    if (checkForErrors(TU) != 0)
      return -1;
    
    for (Loc = 0; Loc < NumLocations; ++Loc) {
      CXFile file = clang_getFile(TU, Locations[Loc].filename);
      if (!file)
        continue;

      Cursor = clang_getCursor(TU,
                               clang_getLocation(TU, file, Locations[Loc].line,
                                                 Locations[Loc].column));

      if (checkForErrors(TU) != 0)
        return -1;

      if (I + 1 == Repeats) {
        handler(Cursor);
        free(Locations[Loc].filename);
      }
    }
  }
  
  PrintDiagnostics(TU);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(CIdx);
  free(Locations);
  free_remapped_files(unsaved_files, num_unsaved_files);
  return 0;
}

static void inspect_print_cursor(CXCursor Cursor) {
  CXTranslationUnit TU = clang_Cursor_getTranslationUnit(Cursor);
  CXCompletionString completionString = clang_getCursorCompletionString(
                                                                  Cursor);
  CXSourceLocation CursorLoc = clang_getCursorLocation(Cursor);
  CXString Spelling;
  const char *cspell;
  unsigned line, column;
  clang_getFileLocation(CursorLoc, 0, &line, &column, 0);
  printf("%d:%d ", line, column);
  PrintCursor(Cursor, NULL);
  PrintCursorExtent(Cursor);
  Spelling = clang_getCursorSpelling(Cursor);
  cspell = clang_getCString(Spelling);
  if (cspell && strlen(cspell) != 0) {
    unsigned pieceIndex;
    printf(" Spelling=%s (", cspell);
    for (pieceIndex = 0; ; ++pieceIndex) {
      CXSourceRange range =
        clang_Cursor_getSpellingNameRange(Cursor, pieceIndex, 0);
      if (clang_Range_isNull(range))
        break;
      PrintRange(range, 0);
    }
    printf(")");
  }
  clang_disposeString(Spelling);
  if (clang_Cursor_getObjCSelectorIndex(Cursor) != -1)
    printf(" Selector index=%d",
           clang_Cursor_getObjCSelectorIndex(Cursor));
  if (clang_Cursor_isDynamicCall(Cursor))
    printf(" Dynamic-call");
  if (Cursor.kind == CXCursor_ObjCMessageExpr ||
      Cursor.kind == CXCursor_MemberRefExpr) {
    CXType T = clang_Cursor_getReceiverType(Cursor);
    if (T.kind != CXType_Invalid) {
      CXString S = clang_getTypeKindSpelling(T.kind);
      printf(" Receiver-type=%s", clang_getCString(S));
      clang_disposeString(S);
    }
  }

  {
    CXModule mod = clang_Cursor_getModule(Cursor);
    CXFile astFile;
    CXString name, astFilename;
    unsigned i, numHeaders;
    if (mod) {
      astFile = clang_Module_getASTFile(mod);
      astFilename = clang_getFileName(astFile);
      name = clang_Module_getFullName(mod);
      numHeaders = clang_Module_getNumTopLevelHeaders(TU, mod);
      printf(" ModuleName=%s (%s) system=%d Headers(%d):",
             clang_getCString(name), clang_getCString(astFilename),
             clang_Module_isSystem(mod), numHeaders);
      clang_disposeString(name);
      clang_disposeString(astFilename);
      for (i = 0; i < numHeaders; ++i) {
        CXFile file = clang_Module_getTopLevelHeader(TU, mod, i);
        CXString filename = clang_getFileName(file);
        printf("\n%s", clang_getCString(filename));
        clang_disposeString(filename);
      }
    }
  }

  if (completionString != NULL) {
    printf("\nCompletion string: ");
    print_completion_string(completionString, stdout);
  }
  printf("\n");
}

static void display_evaluate_results(CXEvalResult result) {
  switch (clang_EvalResult_getKind(result)) {
    case CXEval_Int:
    {
      printf("Kind: Int, ");
      if (clang_EvalResult_isUnsignedInt(result)) {
        unsigned long long val = clang_EvalResult_getAsUnsigned(result);
        printf("unsigned, Value: %llu", val);
      } else {
        long long val = clang_EvalResult_getAsLongLong(result);
        printf("Value: %lld", val);
      }
      break;
    }
    case CXEval_Float:
    {
      double val = clang_EvalResult_getAsDouble(result);
      printf("Kind: Float , Value: %f", val);
      break;
    }
    case CXEval_ObjCStrLiteral:
    {
      const char* str = clang_EvalResult_getAsStr(result);
      printf("Kind: ObjCString , Value: %s", str);
      break;
    }
    case CXEval_StrLiteral:
    {
      const char* str = clang_EvalResult_getAsStr(result);
      printf("Kind: CString , Value: %s", str);
      break;
    }
    case CXEval_CFStr:
    {
      const char* str = clang_EvalResult_getAsStr(result);
      printf("Kind: CFString , Value: %s", str);
      break;
    }
    default:
      printf("Unexposed");
      break;
    }
}

static void inspect_evaluate_cursor(CXCursor Cursor) {
  CXSourceLocation CursorLoc = clang_getCursorLocation(Cursor);
  CXString Spelling;
  const char *cspell;
  unsigned line, column;
  CXEvalResult ER;

  clang_getFileLocation(CursorLoc, 0, &line, &column, 0);
  printf("%d:%d ", line, column);
  PrintCursor(Cursor, NULL);
  PrintCursorExtent(Cursor);
  Spelling = clang_getCursorSpelling(Cursor);
  cspell = clang_getCString(Spelling);
  if (cspell && strlen(cspell) != 0) {
    unsigned pieceIndex;
    printf(" Spelling=%s (", cspell);
    for (pieceIndex = 0; ; ++pieceIndex) {
      CXSourceRange range =
         clang_Cursor_getSpellingNameRange(Cursor, pieceIndex, 0);
      if (clang_Range_isNull(range))
        break;
      PrintRange(range, 0);
    }
    printf(")");
  }
  clang_disposeString(Spelling);

  ER = clang_Cursor_Evaluate(Cursor);
  if (!ER) {
    printf("Not Evaluatable");
  } else {
    display_evaluate_results(ER);
    clang_EvalResult_dispose(ER);
  }
  printf("\n");
}

static void inspect_macroinfo_cursor(CXCursor Cursor) {
  CXSourceLocation CursorLoc = clang_getCursorLocation(Cursor);
  CXString Spelling;
  const char *cspell;
  unsigned line, column;
  clang_getFileLocation(CursorLoc, 0, &line, &column, 0);
  printf("%d:%d ", line, column);
  PrintCursor(Cursor, NULL);
  PrintCursorExtent(Cursor);
  Spelling = clang_getCursorSpelling(Cursor);
  cspell = clang_getCString(Spelling);
  if (cspell && strlen(cspell) != 0) {
    unsigned pieceIndex;
    printf(" Spelling=%s (", cspell);
    for (pieceIndex = 0; ; ++pieceIndex) {
      CXSourceRange range =
         clang_Cursor_getSpellingNameRange(Cursor, pieceIndex, 0);
      if (clang_Range_isNull(range))
        break;
      PrintRange(range, 0);
    }
    printf(")");
  }
  clang_disposeString(Spelling);

  if (clang_Cursor_isMacroBuiltin(Cursor)) {
    printf("[builtin macro]");
  } else if (clang_Cursor_isMacroFunctionLike(Cursor)) {
    printf("[function macro]");
  }
  printf("\n");
}

static enum CXVisitorResult findFileRefsVisit(void *context,
                                         CXCursor cursor, CXSourceRange range) {
  if (clang_Range_isNull(range))
    return CXVisit_Continue;

  PrintCursor(cursor, NULL);
  PrintRange(range, "");
  printf("\n");
  return CXVisit_Continue;
}

static int find_file_refs_at(int argc, const char **argv) {
  CXIndex CIdx;
  int errorCode;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  CXTranslationUnit TU;
  CXCursor Cursor;
  CursorSourceLocation *Locations = 0;
  unsigned NumLocations = 0, Loc;
  unsigned Repeats = 1;
  unsigned I;
  
  /* Count the number of locations. */
  while (strstr(argv[NumLocations+1], "-file-refs-at=") == argv[NumLocations+1])
    ++NumLocations;

  /* Parse the locations. */
  assert(NumLocations > 0 && "Unable to count locations?");
  Locations = (CursorSourceLocation *)malloc(
                                  NumLocations * sizeof(CursorSourceLocation));
  assert(Locations);
  for (Loc = 0; Loc < NumLocations; ++Loc) {
    const char *input = argv[Loc + 1] + strlen("-file-refs-at=");
    if ((errorCode = parse_file_line_column(input, &Locations[Loc].filename,
                                            &Locations[Loc].line,
                                            &Locations[Loc].column, 0, 0)))
      return errorCode;
  }

  if (parse_remapped_files(argc, argv, NumLocations + 1, &unsaved_files,
                           &num_unsaved_files))
    return -1;

  if (getenv("CINDEXTEST_EDITING"))
    Repeats = 5;

  /* Parse the translation unit. When we're testing clang_getCursor() after
     reparsing, don't remap unsaved files until the second parse. */
  CIdx = clang_createIndex(1, 1);
  Err = clang_parseTranslationUnit2(CIdx, argv[argc - 1],
                                    argv + num_unsaved_files + 1 + NumLocations,
                                    argc - num_unsaved_files - 2 - NumLocations,
                                    unsaved_files,
                                    Repeats > 1? 0 : num_unsaved_files,
                                    getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "unable to parse input\n");
    describeLibclangFailure(Err);
    clang_disposeTranslationUnit(TU);
    return -1;
  }

  if (checkForErrors(TU) != 0)
    return -1;

  for (I = 0; I != Repeats; ++I) {
    if (Repeats > 1) {
      Err = clang_reparseTranslationUnit(TU, num_unsaved_files, unsaved_files,
                                         clang_defaultReparseOptions(TU));
      if (Err != CXError_Success) {
        describeLibclangFailure(Err);
        clang_disposeTranslationUnit(TU);
        return 1;
      }
    }

    if (checkForErrors(TU) != 0)
      return -1;
    
    for (Loc = 0; Loc < NumLocations; ++Loc) {
      CXFile file = clang_getFile(TU, Locations[Loc].filename);
      if (!file)
        continue;

      Cursor = clang_getCursor(TU,
                               clang_getLocation(TU, file, Locations[Loc].line,
                                                 Locations[Loc].column));

      if (checkForErrors(TU) != 0)
        return -1;

      if (I + 1 == Repeats) {
        CXCursorAndRangeVisitor visitor = { 0, findFileRefsVisit };
        PrintCursor(Cursor, NULL);
        printf("\n");
        clang_findReferencesInFile(Cursor, file, visitor);
        free(Locations[Loc].filename);

        if (checkForErrors(TU) != 0)
          return -1;
      }
    }
  }
  
  PrintDiagnostics(TU);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(CIdx);
  free(Locations);
  free_remapped_files(unsaved_files, num_unsaved_files);
  return 0;
}

static enum CXVisitorResult findFileIncludesVisit(void *context,
                                         CXCursor cursor, CXSourceRange range) {
  PrintCursor(cursor, NULL);
  PrintRange(range, "");
  printf("\n");
  return CXVisit_Continue;
}

static int find_file_includes_in(int argc, const char **argv) {
  CXIndex CIdx;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  CXTranslationUnit TU;
  const char **Filenames = 0;
  unsigned NumFilenames = 0;
  unsigned Repeats = 1;
  unsigned I, FI;

  /* Count the number of locations. */
  while (strstr(argv[NumFilenames+1], "-file-includes-in=") == argv[NumFilenames+1])
    ++NumFilenames;

  /* Parse the locations. */
  assert(NumFilenames > 0 && "Unable to count filenames?");
  Filenames = (const char **)malloc(NumFilenames * sizeof(const char *));
  assert(Filenames);
  for (I = 0; I < NumFilenames; ++I) {
    const char *input = argv[I + 1] + strlen("-file-includes-in=");
    /* Copy the file name. */
    Filenames[I] = input;
  }

  if (parse_remapped_files(argc, argv, NumFilenames + 1, &unsaved_files,
                           &num_unsaved_files))
    return -1;

  if (getenv("CINDEXTEST_EDITING"))
    Repeats = 2;

  /* Parse the translation unit. When we're testing clang_getCursor() after
     reparsing, don't remap unsaved files until the second parse. */
  CIdx = clang_createIndex(1, 1);
  Err = clang_parseTranslationUnit2(
      CIdx, argv[argc - 1],
      argv + num_unsaved_files + 1 + NumFilenames,
      argc - num_unsaved_files - 2 - NumFilenames,
      unsaved_files,
      Repeats > 1 ? 0 : num_unsaved_files, getDefaultParsingOptions(), &TU);

  if (Err != CXError_Success) {
    fprintf(stderr, "unable to parse input\n");
    describeLibclangFailure(Err);
    clang_disposeTranslationUnit(TU);
    return -1;
  }

  if (checkForErrors(TU) != 0)
    return -1;

  for (I = 0; I != Repeats; ++I) {
    if (Repeats > 1) {
      Err = clang_reparseTranslationUnit(TU, num_unsaved_files, unsaved_files,
                                         clang_defaultReparseOptions(TU));
      if (Err != CXError_Success) {
        describeLibclangFailure(Err);
        clang_disposeTranslationUnit(TU);
        return 1;
      }
    }

    if (checkForErrors(TU) != 0)
      return -1;

    for (FI = 0; FI < NumFilenames; ++FI) {
      CXFile file = clang_getFile(TU, Filenames[FI]);
      if (!file)
        continue;

      if (checkForErrors(TU) != 0)
        return -1;

      if (I + 1 == Repeats) {
        CXCursorAndRangeVisitor visitor = { 0, findFileIncludesVisit };
        clang_findIncludesInFile(TU, file, visitor);

        if (checkForErrors(TU) != 0)
          return -1;
      }
    }
  }

  PrintDiagnostics(TU);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(CIdx);
  free((void *)Filenames);
  free_remapped_files(unsaved_files, num_unsaved_files);
  return 0;
}

#define MAX_IMPORTED_ASTFILES 200

typedef struct {
  char **filenames;
  unsigned num_files;
} ImportedASTFilesData;

static ImportedASTFilesData *importedASTs_create(void) {
  ImportedASTFilesData *p;
  p = malloc(sizeof(ImportedASTFilesData));
  assert(p);
  p->filenames = malloc(MAX_IMPORTED_ASTFILES * sizeof(const char *));
  assert(p->filenames);
  p->num_files = 0;
  return p;
}

static void importedASTs_dispose(ImportedASTFilesData *p) {
  unsigned i;
  if (!p)
    return;

  for (i = 0; i < p->num_files; ++i)
    free(p->filenames[i]);
  free(p->filenames);
  free(p);
}

static void importedASTS_insert(ImportedASTFilesData *p, const char *file) {
  unsigned i;
  assert(p && file);
  for (i = 0; i < p->num_files; ++i)
    if (strcmp(file, p->filenames[i]) == 0)
      return;
  assert(p->num_files + 1 < MAX_IMPORTED_ASTFILES);
  p->filenames[p->num_files++] = strdup(file);
}

typedef struct IndexDataStringList_ {
  struct IndexDataStringList_ *next;
  char data[1]; /* Dynamically sized. */
} IndexDataStringList;

typedef struct {
  const char *check_prefix;
  int first_check_printed;
  int fail_for_error;
  int abort;
  CXString main_filename;
  ImportedASTFilesData *importedASTs;
  IndexDataStringList *strings;
  CXTranslationUnit TU;
} IndexData;

static void free_client_data(IndexData *index_data) {
  IndexDataStringList *node = index_data->strings;
  while (node) {
    IndexDataStringList *next = node->next;
    free(node);
    node = next;
  }
  index_data->strings = NULL;
}

static void printCheck(IndexData *data) {
  if (data->check_prefix) {
    if (data->first_check_printed) {
      printf("// %s-NEXT: ", data->check_prefix);
    } else {
      printf("// %s     : ", data->check_prefix);
      data->first_check_printed = 1;
    }
  }
}

static void printCXIndexFile(CXIdxClientFile file) {
  CXString filename = clang_getFileName((CXFile)file);
  printf("%s", clang_getCString(filename));
  clang_disposeString(filename);
}

static void printCXIndexLoc(CXIdxLoc loc, CXClientData client_data) {
  IndexData *index_data;
  CXString filename;
  const char *cname;
  CXIdxClientFile file;
  unsigned line, column;
  const char *main_filename;
  int isMainFile;
  
  index_data = (IndexData *)client_data;
  clang_indexLoc_getFileLocation(loc, &file, 0, &line, &column, 0);
  if (line == 0) {
    printf("<invalid>");
    return;
  }
  if (!file) {
    printf("<no idxfile>");
    return;
  }
  filename = clang_getFileName((CXFile)file);
  cname = clang_getCString(filename);
  main_filename = clang_getCString(index_data->main_filename);
  if (strcmp(cname, main_filename) == 0)
    isMainFile = 1;
  else
    isMainFile = 0;
  clang_disposeString(filename);

  if (!isMainFile) {
    printCXIndexFile(file);
    printf(":");
  }
  printf("%d:%d", line, column);
}

static unsigned digitCount(unsigned val) {
  unsigned c = 1;
  while (1) {
    if (val < 10)
      return c;
    ++c;
    val /= 10;
  }
}

static CXIdxClientContainer makeClientContainer(CXClientData *client_data,
                                                const CXIdxEntityInfo *info,
                                                CXIdxLoc loc) {
  IndexData *index_data;
  IndexDataStringList *node;
  const char *name;
  char *newStr;
  CXIdxClientFile file;
  unsigned line, column;
  
  name = info->name;
  if (!name)
    name = "<anon-tag>";

  clang_indexLoc_getFileLocation(loc, &file, 0, &line, &column, 0);

  node =
      (IndexDataStringList *)malloc(sizeof(IndexDataStringList) + strlen(name) +
                                    digitCount(line) + digitCount(column) + 2);
  assert(node);
  newStr = node->data;
  sprintf(newStr, "%s:%d:%d", name, line, column);

  /* Remember string so it can be freed later. */
  index_data = (IndexData *)client_data;
  node->next = index_data->strings;
  index_data->strings = node;

  return (CXIdxClientContainer)newStr;
}

static void printCXIndexContainer(const CXIdxContainerInfo *info) {
  CXIdxClientContainer container;
  container = clang_index_getClientContainer(info);
  if (!container)
    printf("[<<NULL>>]");
  else
    printf("[%s]", (const char *)container);
}

static const char *getEntityKindString(CXIdxEntityKind kind) {
  switch (kind) {
  case CXIdxEntity_Unexposed: return "<<UNEXPOSED>>";
  case CXIdxEntity_Typedef: return "typedef";
  case CXIdxEntity_Function: return "function";
  case CXIdxEntity_Variable: return "variable";
  case CXIdxEntity_Field: return "field";
  case CXIdxEntity_EnumConstant: return "enumerator";
  case CXIdxEntity_ObjCClass: return "objc-class";
  case CXIdxEntity_ObjCProtocol: return "objc-protocol";
  case CXIdxEntity_ObjCCategory: return "objc-category";
  case CXIdxEntity_ObjCInstanceMethod: return "objc-instance-method";
  case CXIdxEntity_ObjCClassMethod: return "objc-class-method";
  case CXIdxEntity_ObjCProperty: return "objc-property";
  case CXIdxEntity_ObjCIvar: return "objc-ivar";
  case CXIdxEntity_Enum: return "enum";
  case CXIdxEntity_Struct: return "struct";
  case CXIdxEntity_Union: return "union";
  case CXIdxEntity_CXXClass: return "c++-class";
  case CXIdxEntity_CXXNamespace: return "namespace";
  case CXIdxEntity_CXXNamespaceAlias: return "namespace-alias";
  case CXIdxEntity_CXXStaticVariable: return "c++-static-var";
  case CXIdxEntity_CXXStaticMethod: return "c++-static-method";
  case CXIdxEntity_CXXInstanceMethod: return "c++-instance-method";
  case CXIdxEntity_CXXConstructor: return "constructor";
  case CXIdxEntity_CXXDestructor: return "destructor";
  case CXIdxEntity_CXXConversionFunction: return "conversion-func";
  case CXIdxEntity_CXXTypeAlias: return "type-alias";
  case CXIdxEntity_CXXInterface: return "c++-__interface";
  case CXIdxEntity_CXXConcept:
    return "concept";
  }
  assert(0 && "Garbage entity kind");
  return 0;
}

static const char *getEntityTemplateKindString(CXIdxEntityCXXTemplateKind kind) {
  switch (kind) {
  case CXIdxEntity_NonTemplate: return "";
  case CXIdxEntity_Template: return "-template";
  case CXIdxEntity_TemplatePartialSpecialization:
    return "-template-partial-spec";
  case CXIdxEntity_TemplateSpecialization: return "-template-spec";
  }
  assert(0 && "Garbage entity kind");
  return 0;
}

static const char *getEntityLanguageString(CXIdxEntityLanguage kind) {
  switch (kind) {
  case CXIdxEntityLang_None: return "<none>";
  case CXIdxEntityLang_C: return "C";
  case CXIdxEntityLang_ObjC: return "ObjC";
  case CXIdxEntityLang_CXX: return "C++";
  case CXIdxEntityLang_Swift: return "Swift";
  }
  assert(0 && "Garbage language kind");
  return 0;
}

static void printEntityInfo(const char *cb,
                            CXClientData client_data,
                            const CXIdxEntityInfo *info) {
  const char *name;
  IndexData *index_data;
  unsigned i;
  index_data = (IndexData *)client_data;
  printCheck(index_data);

  if (!info) {
    printf("%s: <<NULL>>", cb);
    return;
  }

  name = info->name;
  if (!name)
    name = "<anon-tag>";

  printf("%s: kind: %s%s", cb, getEntityKindString(info->kind),
         getEntityTemplateKindString(info->templateKind));
  printf(" | name: %s", name);
  printf(" | USR: %s", info->USR);
  printf(" | lang: %s", getEntityLanguageString(info->lang));

  for (i = 0; i != info->numAttributes; ++i) {
    const CXIdxAttrInfo *Attr = info->attributes[i];
    printf("     <attribute>: ");
    PrintCursor(Attr->cursor, NULL);
  }
}

static void printBaseClassInfo(CXClientData client_data,
                               const CXIdxBaseClassInfo *info) {
  printEntityInfo("     <base>", client_data, info->base);
  printf(" | cursor: ");
  PrintCursor(info->cursor, NULL);
  printf(" | loc: ");
  printCXIndexLoc(info->loc, client_data);
}

static void printProtocolList(const CXIdxObjCProtocolRefListInfo *ProtoInfo,
                              CXClientData client_data) {
  unsigned i;
  for (i = 0; i < ProtoInfo->numProtocols; ++i) {
    printEntityInfo("     <protocol>", client_data,
                    ProtoInfo->protocols[i]->protocol);
    printf(" | cursor: ");
    PrintCursor(ProtoInfo->protocols[i]->cursor, NULL);
    printf(" | loc: ");
    printCXIndexLoc(ProtoInfo->protocols[i]->loc, client_data);
    printf("\n");
  }
}

static void printSymbolRole(CXSymbolRole role) {
  if (role & CXSymbolRole_Declaration)
    printf(" decl");
  if (role & CXSymbolRole_Definition)
    printf(" def");
  if (role & CXSymbolRole_Reference)
    printf(" ref");
  if (role & CXSymbolRole_Read)
    printf(" read");
  if (role & CXSymbolRole_Write)
    printf(" write");
  if (role & CXSymbolRole_Call)
    printf(" call");
  if (role & CXSymbolRole_Dynamic)
    printf(" dyn");
  if (role & CXSymbolRole_AddressOf)
    printf(" addr");
  if (role & CXSymbolRole_Implicit)
    printf(" implicit");
}

static void index_diagnostic(CXClientData client_data,
                             CXDiagnosticSet diagSet, void *reserved) {
  CXString str;
  const char *cstr;
  unsigned numDiags, i;
  CXDiagnostic diag;
  IndexData *index_data;
  index_data = (IndexData *)client_data;
  printCheck(index_data);

  numDiags = clang_getNumDiagnosticsInSet(diagSet);
  for (i = 0; i != numDiags; ++i) {
    diag = clang_getDiagnosticInSet(diagSet, i);
    str = clang_formatDiagnostic(diag, clang_defaultDiagnosticDisplayOptions());
    cstr = clang_getCString(str);
    printf("[diagnostic]: %s\n", cstr);
    clang_disposeString(str);  
  
    if (getenv("CINDEXTEST_FAILONERROR") &&
        clang_getDiagnosticSeverity(diag) >= CXDiagnostic_Error) {
      index_data->fail_for_error = 1;
    }
  }
}

static CXIdxClientFile index_enteredMainFile(CXClientData client_data,
                                       CXFile file, void *reserved) {
  IndexData *index_data;

  index_data = (IndexData *)client_data;
  printCheck(index_data);

  index_data->main_filename = clang_getFileName(file);

  printf("[enteredMainFile]: ");
  printCXIndexFile((CXIdxClientFile)file);
  printf("\n");

  return (CXIdxClientFile)file;
}

static CXIdxClientFile index_ppIncludedFile(CXClientData client_data,
                                            const CXIdxIncludedFileInfo *info) {
  IndexData *index_data;
  CXModule Mod;
  index_data = (IndexData *)client_data;
  printCheck(index_data);

  printf("[ppIncludedFile]: ");
  printCXIndexFile((CXIdxClientFile)info->file);
  printf(" | name: \"%s\"", info->filename);
  printf(" | hash loc: ");
  printCXIndexLoc(info->hashLoc, client_data);
  printf(" | isImport: %d | isAngled: %d | isModule: %d",
         info->isImport, info->isAngled, info->isModuleImport);
  
  Mod = clang_getModuleForFile(index_data->TU, (CXFile)info->file);
  if (Mod) {
    CXString str = clang_Module_getFullName(Mod);
    const char *cstr = clang_getCString(str);
    printf(" | module: %s", cstr);
    clang_disposeString(str);
  }

  printf("\n");

  return (CXIdxClientFile)info->file;
}

static CXIdxClientFile index_importedASTFile(CXClientData client_data,
                                         const CXIdxImportedASTFileInfo *info) {
  IndexData *index_data;
  index_data = (IndexData *)client_data;
  printCheck(index_data);

  if (index_data->importedASTs) {
    CXString filename = clang_getFileName(info->file);
    importedASTS_insert(index_data->importedASTs, clang_getCString(filename));
    clang_disposeString(filename);
  }
  
  printf("[importedASTFile]: ");
  printCXIndexFile((CXIdxClientFile)info->file);
  if (info->module) {
    CXString name = clang_Module_getFullName(info->module);
    printf(" | loc: ");
    printCXIndexLoc(info->loc, client_data);
    printf(" | name: \"%s\"", clang_getCString(name));
    printf(" | isImplicit: %d\n", info->isImplicit);
    clang_disposeString(name);
  } else {
    /* PCH file, the rest are not relevant. */
    printf("\n");
  }

  return (CXIdxClientFile)info->file;
}

static CXIdxClientContainer
index_startedTranslationUnit(CXClientData client_data, void *reserved) {
  IndexData *index_data;
  index_data = (IndexData *)client_data;
  printCheck(index_data);

  printf("[startedTranslationUnit]\n");
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#endif
  return (CXIdxClientContainer)"TU";
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
}

static void index_indexDeclaration(CXClientData client_data,
                                   const CXIdxDeclInfo *info) {
  IndexData *index_data;
  const CXIdxObjCCategoryDeclInfo *CatInfo;
  const CXIdxObjCInterfaceDeclInfo *InterInfo;
  const CXIdxObjCProtocolRefListInfo *ProtoInfo;
  const CXIdxObjCPropertyDeclInfo *PropInfo;
  const CXIdxCXXClassDeclInfo *CXXClassInfo;
  unsigned i;
  index_data = (IndexData *)client_data;

  printEntityInfo("[indexDeclaration]", client_data, info->entityInfo);
  printf(" | cursor: ");
  PrintCursor(info->cursor, NULL);
  printf(" | loc: ");
  printCXIndexLoc(info->loc, client_data);
  printf(" | semantic-container: ");
  printCXIndexContainer(info->semanticContainer);
  printf(" | lexical-container: ");
  printCXIndexContainer(info->lexicalContainer);
  printf(" | isRedecl: %d", info->isRedeclaration);
  printf(" | isDef: %d", info->isDefinition);
  if (info->flags & CXIdxDeclFlag_Skipped) {
    assert(!info->isContainer);
    printf(" | isContainer: skipped");
  } else {
    printf(" | isContainer: %d", info->isContainer);
  }
  printf(" | isImplicit: %d\n", info->isImplicit);

  for (i = 0; i != info->numAttributes; ++i) {
    const CXIdxAttrInfo *Attr = info->attributes[i];
    printf("     <attribute>: ");
    PrintCursor(Attr->cursor, NULL);
    printf("\n");
  }

  if (clang_index_isEntityObjCContainerKind(info->entityInfo->kind)) {
    const char *kindName = 0;
    CXIdxObjCContainerKind K = clang_index_getObjCContainerDeclInfo(info)->kind;
    switch (K) {
    case CXIdxObjCContainer_ForwardRef:
      kindName = "forward-ref"; break;
    case CXIdxObjCContainer_Interface:
      kindName = "interface"; break;
    case CXIdxObjCContainer_Implementation:
      kindName = "implementation"; break;
    }
    printCheck(index_data);
    printf("     <ObjCContainerInfo>: kind: %s\n", kindName);
  }

  if ((CatInfo = clang_index_getObjCCategoryDeclInfo(info))) {
    printEntityInfo("     <ObjCCategoryInfo>: class", client_data,
                    CatInfo->objcClass);
    printf(" | cursor: ");
    PrintCursor(CatInfo->classCursor, NULL);
    printf(" | loc: ");
    printCXIndexLoc(CatInfo->classLoc, client_data);
    printf("\n");
  }

  if ((InterInfo = clang_index_getObjCInterfaceDeclInfo(info))) {
    if (InterInfo->superInfo) {
      printBaseClassInfo(client_data, InterInfo->superInfo);
      printf("\n");
    }
  }

  if ((ProtoInfo = clang_index_getObjCProtocolRefListInfo(info))) {
    printProtocolList(ProtoInfo, client_data);
  }

  if ((PropInfo = clang_index_getObjCPropertyDeclInfo(info))) {
    if (PropInfo->getter) {
      printEntityInfo("     <getter>", client_data, PropInfo->getter);
      printf("\n");
    }
    if (PropInfo->setter) {
      printEntityInfo("     <setter>", client_data, PropInfo->setter);
      printf("\n");
    }
  }

  if ((CXXClassInfo = clang_index_getCXXClassDeclInfo(info))) {
    for (i = 0; i != CXXClassInfo->numBases; ++i) {
      printBaseClassInfo(client_data, CXXClassInfo->bases[i]);
      printf("\n");
    }
  }

  if (info->declAsContainer)
    clang_index_setClientContainer(
        info->declAsContainer,
        makeClientContainer(client_data, info->entityInfo, info->loc));
}

static void index_indexEntityReference(CXClientData client_data,
                                       const CXIdxEntityRefInfo *info) {
  printEntityInfo("[indexEntityReference]", client_data,
                  info->referencedEntity);
  printf(" | cursor: ");
  PrintCursor(info->cursor, NULL);
  printf(" | loc: ");
  printCXIndexLoc(info->loc, client_data);
  printEntityInfo(" | <parent>:", client_data, info->parentEntity);
  printf(" | container: ");
  printCXIndexContainer(info->container);
  printf(" | refkind: ");
  switch (info->kind) {
    case CXIdxEntityRef_Direct: printf("direct"); break;
    case CXIdxEntityRef_Implicit: printf("implicit"); break;
  }
  printf(" | role:");
  printSymbolRole(info->role);
  printf("\n");
}

static int index_abortQuery(CXClientData client_data, void *reserved) {
  IndexData *index_data;
  index_data = (IndexData *)client_data;
  return index_data->abort;
}

static IndexerCallbacks IndexCB = {
  index_abortQuery,
  index_diagnostic,
  index_enteredMainFile,
  index_ppIncludedFile,
  index_importedASTFile,
  index_startedTranslationUnit,
  index_indexDeclaration,
  index_indexEntityReference
};

static unsigned getIndexOptions(void) {
  unsigned index_opts;
  index_opts = 0;
  if (getenv("CINDEXTEST_SUPPRESSREFS"))
    index_opts |= CXIndexOpt_SuppressRedundantRefs;
  if (getenv("CINDEXTEST_INDEXLOCALSYMBOLS"))
    index_opts |= CXIndexOpt_IndexFunctionLocalSymbols;
  if (!getenv("CINDEXTEST_DISABLE_SKIPPARSEDBODIES"))
    index_opts |= CXIndexOpt_SkipParsedBodiesInSession;
  if (getenv("CINDEXTEST_INDEXIMPLICITTEMPLATEINSTANTIATIONS"))
    index_opts |= CXIndexOpt_IndexImplicitTemplateInstantiations;

  return index_opts;
}

static int index_compile_args(int num_args, const char **args,
                              CXIndexAction idxAction,
                              ImportedASTFilesData *importedASTs,
                              const char *check_prefix) {
  IndexData index_data;
  unsigned index_opts;
  int result;

  if (num_args == 0) {
    fprintf(stderr, "no compiler arguments\n");
    return -1;
  }

  index_data.check_prefix = check_prefix;
  index_data.first_check_printed = 0;
  index_data.fail_for_error = 0;
  index_data.abort = 0;
  index_data.main_filename = createCXString("");
  index_data.importedASTs = importedASTs;
  index_data.strings = NULL;
  index_data.TU = NULL;

  index_opts = getIndexOptions();
  result = clang_indexSourceFile(idxAction, &index_data,
                                 &IndexCB,sizeof(IndexCB), index_opts,
                                 0, args, num_args, 0, 0, 0,
                                 getDefaultParsingOptions());
  if (result != CXError_Success)
    describeLibclangFailure(result);

  if (index_data.fail_for_error)
    result = -1;

  clang_disposeString(index_data.main_filename);
  free_client_data(&index_data);
  return result;
}

static int index_ast_file(const char *ast_file,
                          CXIndex Idx,
                          CXIndexAction idxAction,
                          ImportedASTFilesData *importedASTs,
                          const char *check_prefix) {
  CXTranslationUnit TU;
  IndexData index_data;
  unsigned index_opts;
  int result;

  if (!CreateTranslationUnit(Idx, ast_file, &TU))
    return -1;

  index_data.check_prefix = check_prefix;
  index_data.first_check_printed = 0;
  index_data.fail_for_error = 0;
  index_data.abort = 0;
  index_data.main_filename = createCXString("");
  index_data.importedASTs = importedASTs;
  index_data.strings = NULL;
  index_data.TU = TU;

  index_opts = getIndexOptions();
  result = clang_indexTranslationUnit(idxAction, &index_data,
                                      &IndexCB,sizeof(IndexCB),
                                      index_opts, TU);
  if (index_data.fail_for_error)
    result = -1;

  clang_disposeTranslationUnit(TU);
  clang_disposeString(index_data.main_filename);
  free_client_data(&index_data);
  return result;
}

static int index_file(int argc, const char **argv, int full) {
  const char *check_prefix;
  CXIndex Idx;
  CXIndexAction idxAction;
  ImportedASTFilesData *importedASTs;
  int result;

  check_prefix = 0;
  if (argc > 0) {
    if (strstr(argv[0], "-check-prefix=") == argv[0]) {
      check_prefix = argv[0] + strlen("-check-prefix=");
      ++argv;
      --argc;
    }
  }

  if (!(Idx = clang_createIndex(/* excludeDeclsFromPCH */ 1,
                                /* displayDiagnostics=*/1))) {
    fprintf(stderr, "Could not create Index\n");
    return 1;
  }
  idxAction = clang_IndexAction_create(Idx);
  importedASTs = 0;
  if (full)
    importedASTs = importedASTs_create();

  result = index_compile_args(argc, argv, idxAction, importedASTs, check_prefix);
  if (result != 0)
    goto finished;

  if (full) {
    unsigned i;
    for (i = 0; i < importedASTs->num_files && result == 0; ++i) {
      result = index_ast_file(importedASTs->filenames[i], Idx, idxAction,
                              importedASTs, check_prefix);
    }
  }

finished:
  importedASTs_dispose(importedASTs);
  clang_IndexAction_dispose(idxAction);
  clang_disposeIndex(Idx);
  return result;
}

static int index_tu(int argc, const char **argv) {
  const char *check_prefix;
  CXIndex Idx;
  CXIndexAction idxAction;
  int result;

  check_prefix = 0;
  if (argc > 0) {
    if (strstr(argv[0], "-check-prefix=") == argv[0]) {
      check_prefix = argv[0] + strlen("-check-prefix=");
      ++argv;
      --argc;
    }
  }

  if (!(Idx = clang_createIndex(/* excludeDeclsFromPCH */ 1,
                                /* displayDiagnostics=*/1))) {
    fprintf(stderr, "Could not create Index\n");
    return 1;
  }
  idxAction = clang_IndexAction_create(Idx);

  result = index_ast_file(argv[0], Idx, idxAction,
                          /*importedASTs=*/0, check_prefix);

  clang_IndexAction_dispose(idxAction);
  clang_disposeIndex(Idx);
  return result;
}

static int index_compile_db(int argc, const char **argv) {
  const char *check_prefix;
  CXIndex Idx;
  CXIndexAction idxAction;
  int errorCode = 0;

  check_prefix = 0;
  if (argc > 0) {
    if (strstr(argv[0], "-check-prefix=") == argv[0]) {
      check_prefix = argv[0] + strlen("-check-prefix=");
      ++argv;
      --argc;
    }
  }

  if (argc == 0) {
    fprintf(stderr, "no compilation database\n");
    return -1;
  }

  if (!(Idx = clang_createIndex(/* excludeDeclsFromPCH */ 1,
                                /* displayDiagnostics=*/1))) {
    fprintf(stderr, "Could not create Index\n");
    return 1;
  }
  idxAction = clang_IndexAction_create(Idx);

  {
    const char *database = argv[0];
    CXCompilationDatabase db = 0;
    CXCompileCommands CCmds = 0;
    CXCompileCommand CCmd;
    CXCompilationDatabase_Error ec;
    CXString wd;
#define MAX_COMPILE_ARGS 512
    CXString cxargs[MAX_COMPILE_ARGS];
    const char *args[MAX_COMPILE_ARGS];
    char *tmp;
    unsigned len;
    char *buildDir;
    int i, a, numCmds, numArgs;

    len = strlen(database);
    tmp = (char *) malloc(len+1);
    assert(tmp);
    memcpy(tmp, database, len+1);
    buildDir = dirname(tmp);

    db = clang_CompilationDatabase_fromDirectory(buildDir, &ec);

    if (db) {

      if (ec!=CXCompilationDatabase_NoError) {
        printf("unexpected error %d code while loading compilation database\n", ec);
        errorCode = -1;
        goto cdb_end;
      }

      if (chdir(buildDir) != 0) {
        printf("Could not chdir to %s\n", buildDir);
        errorCode = -1;
        goto cdb_end;
      }

      CCmds = clang_CompilationDatabase_getAllCompileCommands(db);
      if (!CCmds) {
        printf("compilation db is empty\n");
        errorCode = -1;
        goto cdb_end;
      }

      numCmds = clang_CompileCommands_getSize(CCmds);

      if (numCmds==0) {
        fprintf(stderr, "should not get an empty compileCommand set\n");
        errorCode = -1;
        goto cdb_end;
      }

      for (i=0; i<numCmds && errorCode == 0; ++i) {
        CCmd = clang_CompileCommands_getCommand(CCmds, i);

        wd = clang_CompileCommand_getDirectory(CCmd);
        if (chdir(clang_getCString(wd)) != 0) {
          printf("Could not chdir to %s\n", clang_getCString(wd));
          errorCode = -1;
          goto cdb_end;
        }
        clang_disposeString(wd);

        numArgs = clang_CompileCommand_getNumArgs(CCmd);
        if (numArgs > MAX_COMPILE_ARGS){
          fprintf(stderr, "got more compile arguments than maximum\n");
          errorCode = -1;
          goto cdb_end;
        }
        for (a=0; a<numArgs; ++a) {
          cxargs[a] = clang_CompileCommand_getArg(CCmd, a);
          args[a] = clang_getCString(cxargs[a]);
        }

        errorCode = index_compile_args(numArgs, args, idxAction,
                                       /*importedASTs=*/0, check_prefix);

        for (a=0; a<numArgs; ++a)
          clang_disposeString(cxargs[a]);
      }
    } else {
      printf("database loading failed with error code %d.\n", ec);
      errorCode = -1;
    }

  cdb_end:
    clang_CompileCommands_dispose(CCmds);
    clang_CompilationDatabase_dispose(db);
    free(tmp);

  }

  clang_IndexAction_dispose(idxAction);
  clang_disposeIndex(Idx);
  return errorCode;
}

int perform_token_annotation(int argc, const char **argv) {
  const char *input = argv[1];
  char *filename = 0;
  unsigned line, second_line;
  unsigned column, second_column;
  CXIndex CIdx;
  CXTranslationUnit TU = 0;
  int errorCode;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  CXToken *tokens;
  unsigned num_tokens;
  CXSourceRange range;
  CXSourceLocation startLoc, endLoc;
  CXFile file = 0;
  CXCursor *cursors = 0;
  CXSourceRangeList *skipped_ranges = 0;
  enum CXErrorCode Err;
  unsigned i;

  input += strlen("-test-annotate-tokens=");
  if ((errorCode = parse_file_line_column(input, &filename, &line, &column,
                                          &second_line, &second_column)))
    return errorCode;

  if (parse_remapped_files(argc, argv, 2, &unsaved_files, &num_unsaved_files)) {
    free(filename);
    return -1;
  }

  CIdx = clang_createIndex(0, 1);
  Err = clang_parseTranslationUnit2(CIdx, argv[argc - 1],
                                    argv + num_unsaved_files + 2,
                                    argc - num_unsaved_files - 3,
                                    unsaved_files,
                                    num_unsaved_files,
                                    getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "unable to parse input\n");
    describeLibclangFailure(Err);
    clang_disposeIndex(CIdx);
    free(filename);
    free_remapped_files(unsaved_files, num_unsaved_files);
    return -1;
  }
  errorCode = 0;

  if (checkForErrors(TU) != 0) {
    errorCode = -1;
    goto teardown;
  }

  if (getenv("CINDEXTEST_EDITING")) {
    for (i = 0; i < 5; ++i) {
      Err = clang_reparseTranslationUnit(TU, num_unsaved_files, unsaved_files,
                                         clang_defaultReparseOptions(TU));
      if (Err != CXError_Success) {
        fprintf(stderr, "Unable to reparse translation unit!\n");
        describeLibclangFailure(Err);
        errorCode = -1;
        goto teardown;
      }
    }
  }

  if (checkForErrors(TU) != 0) {
    errorCode = -1;
    goto teardown;
  }

  file = clang_getFile(TU, filename);
  if (!file) {
    fprintf(stderr, "file %s is not in this translation unit\n", filename);
    errorCode = -1;
    goto teardown;
  }

  startLoc = clang_getLocation(TU, file, line, column);
  if (clang_equalLocations(clang_getNullLocation(), startLoc)) {
    fprintf(stderr, "invalid source location %s:%d:%d\n", filename, line,
            column);
    errorCode = -1;
    goto teardown;
  }

  endLoc = clang_getLocation(TU, file, second_line, second_column);
  if (clang_equalLocations(clang_getNullLocation(), endLoc)) {
    fprintf(stderr, "invalid source location %s:%d:%d\n", filename,
            second_line, second_column);
    errorCode = -1;
    goto teardown;
  }

  range = clang_getRange(startLoc, endLoc);
  clang_tokenize(TU, range, &tokens, &num_tokens);

  if (checkForErrors(TU) != 0) {
    errorCode = -1;
    goto teardown;
  }

  cursors = (CXCursor *)malloc(num_tokens * sizeof(CXCursor));
  assert(cursors);
  clang_annotateTokens(TU, tokens, num_tokens, cursors);

  if (checkForErrors(TU) != 0) {
    errorCode = -1;
    goto teardown;
  }

  skipped_ranges = clang_getSkippedRanges(TU, file);
  for (i = 0; i != skipped_ranges->count; ++i) {
    unsigned start_line, start_column, end_line, end_column;
    clang_getFileLocation(clang_getRangeStart(skipped_ranges->ranges[i]), 0,
                          &start_line, &start_column, 0);
    clang_getFileLocation(clang_getRangeEnd(skipped_ranges->ranges[i]), 0,
                          &end_line, &end_column, 0);
    printf("Skipping: ");
    PrintExtent(stdout, start_line, start_column, end_line, end_column);
    printf("\n");
  }
  clang_disposeSourceRangeList(skipped_ranges);

  for (i = 0; i != num_tokens; ++i) {
    const char *kind = "<unknown>";
    CXString spelling = clang_getTokenSpelling(TU, tokens[i]);
    CXSourceRange extent = clang_getTokenExtent(TU, tokens[i]);
    unsigned start_line, start_column, end_line, end_column;

    switch (clang_getTokenKind(tokens[i])) {
    case CXToken_Punctuation: kind = "Punctuation"; break;
    case CXToken_Keyword: kind = "Keyword"; break;
    case CXToken_Identifier: kind = "Identifier"; break;
    case CXToken_Literal: kind = "Literal"; break;
    case CXToken_Comment: kind = "Comment"; break;
    }
    clang_getFileLocation(clang_getRangeStart(extent), 0, &start_line,
                          &start_column, 0);
    clang_getFileLocation(clang_getRangeEnd(extent), 0, &end_line, &end_column,
                          0);
    printf("%s: \"%s\" ", kind, clang_getCString(spelling));
    clang_disposeString(spelling);
    PrintExtent(stdout, start_line, start_column, end_line, end_column);
    if (!clang_isInvalid(cursors[i].kind)) {
      printf(" ");
      PrintCursor(cursors[i], NULL);
    }
    printf("\n");
  }
  free(cursors);
  clang_disposeTokens(TU, tokens, num_tokens);

 teardown:
  PrintDiagnostics(TU);
  clang_disposeTranslationUnit(TU);
  clang_disposeIndex(CIdx);
  free(filename);
  free_remapped_files(unsaved_files, num_unsaved_files);
  return errorCode;
}

static int
perform_test_compilation_db(const char *database, int argc, const char **argv) {
  CXCompilationDatabase db;
  CXCompileCommands CCmds;
  CXCompileCommand CCmd;
  CXCompilationDatabase_Error ec;
  CXString wd;
  CXString arg;
  int errorCode = 0;
  char *tmp;
  unsigned len;
  char *buildDir;
  int i, j, a, numCmds, numArgs;

  len = strlen(database);
  tmp = (char *) malloc(len+1);
  assert(tmp);
  memcpy(tmp, database, len+1);
  buildDir = dirname(tmp);

  db = clang_CompilationDatabase_fromDirectory(buildDir, &ec);

  if (db) {

    if (ec!=CXCompilationDatabase_NoError) {
      printf("unexpected error %d code while loading compilation database\n", ec);
      errorCode = -1;
      goto cdb_end;
    }

    for (i=0; i<argc && errorCode==0; ) {
      if (strcmp(argv[i],"lookup")==0){
        CCmds = clang_CompilationDatabase_getCompileCommands(db, argv[i+1]);

        if (!CCmds) {
          printf("file %s not found in compilation db\n", argv[i+1]);
          errorCode = -1;
          break;
        }

        numCmds = clang_CompileCommands_getSize(CCmds);

        if (numCmds==0) {
          fprintf(stderr, "should not get an empty compileCommand set for file"
                          " '%s'\n", argv[i+1]);
          errorCode = -1;
          break;
        }

        for (j=0; j<numCmds; ++j) {
          CCmd = clang_CompileCommands_getCommand(CCmds, j);

          wd = clang_CompileCommand_getDirectory(CCmd);
          printf("workdir:'%s'", clang_getCString(wd));
          clang_disposeString(wd);

          printf(" cmdline:'");
          numArgs = clang_CompileCommand_getNumArgs(CCmd);
          for (a=0; a<numArgs; ++a) {
            if (a) printf(" ");
            arg = clang_CompileCommand_getArg(CCmd, a);
            printf("%s", clang_getCString(arg));
            clang_disposeString(arg);
          }
          printf("'\n");
        }

        clang_CompileCommands_dispose(CCmds);

        i += 2;
      }
    }
    clang_CompilationDatabase_dispose(db);
  } else {
    printf("database loading failed with error code %d.\n", ec);
    errorCode = -1;
  }

cdb_end:
  free(tmp);

  return errorCode;
}

/******************************************************************************/
/* USR printing.                                                              */
/******************************************************************************/

static int insufficient_usr(const char *kind, const char *usage) {
  fprintf(stderr, "USR for '%s' requires: %s\n", kind, usage);
  return 1;
}

static unsigned isUSR(const char *s) {
  return s[0] == 'c' && s[1] == ':';
}

static int not_usr(const char *s, const char *arg) {
  fprintf(stderr, "'%s' argument ('%s') is not a USR\n", s, arg);
  return 1;
}

static void print_usr(CXString usr) {
  const char *s = clang_getCString(usr);
  printf("%s\n", s);
  clang_disposeString(usr);
}

static void display_usrs(void) {
  fprintf(stderr, "-print-usrs options:\n"
        " ObjCCategory <class name> <category name>\n"
        " ObjCClass <class name>\n"
        " ObjCIvar <ivar name> <class USR>\n"
        " ObjCMethod <selector> [0=class method|1=instance method] "
            "<class USR>\n"
          " ObjCProperty <property name> <class USR>\n"
          " ObjCProtocol <protocol name>\n");
}

int print_usrs(const char **I, const char **E) {
  while (I != E) {
    const char *kind = *I;
    unsigned len = strlen(kind);
    switch (len) {
      case 8:
        if (memcmp(kind, "ObjCIvar", 8) == 0) {
          if (I + 2 >= E)
            return insufficient_usr(kind, "<ivar name> <class USR>");
          if (!isUSR(I[2]))
            return not_usr("<class USR>", I[2]);
          else {
            CXString x = createCXString(I[2]);
            print_usr(clang_constructUSR_ObjCIvar(I[1], x));
          }

          I += 3;
          continue;
        }
        break;
      case 9:
        if (memcmp(kind, "ObjCClass", 9) == 0) {
          if (I + 1 >= E)
            return insufficient_usr(kind, "<class name>");
          print_usr(clang_constructUSR_ObjCClass(I[1]));
          I += 2;
          continue;
        }
        break;
      case 10:
        if (memcmp(kind, "ObjCMethod", 10) == 0) {
          if (I + 3 >= E)
            return insufficient_usr(kind, "<method selector> "
                "[0=class method|1=instance method] <class USR>");
          if (!isUSR(I[3]))
            return not_usr("<class USR>", I[3]);
          else {
            CXString x = createCXString(I[3]);
            print_usr(clang_constructUSR_ObjCMethod(I[1], atoi(I[2]), x));
          }
          I += 4;
          continue;
        }
        break;
      case 12:
        if (memcmp(kind, "ObjCCategory", 12) == 0) {
          if (I + 2 >= E)
            return insufficient_usr(kind, "<class name> <category name>");
          print_usr(clang_constructUSR_ObjCCategory(I[1], I[2]));
          I += 3;
          continue;
        }
        if (memcmp(kind, "ObjCProtocol", 12) == 0) {
          if (I + 1 >= E)
            return insufficient_usr(kind, "<protocol name>");
          print_usr(clang_constructUSR_ObjCProtocol(I[1]));
          I += 2;
          continue;
        }
        if (memcmp(kind, "ObjCProperty", 12) == 0) {
          if (I + 2 >= E)
            return insufficient_usr(kind, "<property name> <class USR>");
          if (!isUSR(I[2]))
            return not_usr("<class USR>", I[2]);
          else {
            CXString x = createCXString(I[2]);
            print_usr(clang_constructUSR_ObjCProperty(I[1], x));
          }
          I += 3;
          continue;
        }
        break;
      default:
        break;
    }
    break;
  }

  if (I != E) {
    fprintf(stderr, "Invalid USR kind: %s\n", *I);
    display_usrs();
    return 1;
  }
  return 0;
}

int print_usrs_file(const char *file_name) {
  char line[2048];
  const char *args[128];
  unsigned numChars = 0;

  FILE *fp = fopen(file_name, "r");
  if (!fp) {
    fprintf(stderr, "error: cannot open '%s'\n", file_name);
    return 1;
  }

  /* This code is not really all that safe, but it works fine for testing. */
  while (!feof(fp)) {
    char c = fgetc(fp);
    if (c == '\n') {
      unsigned i = 0;
      const char *s = 0;

      if (numChars == 0)
        continue;

      line[numChars] = '\0';
      numChars = 0;

      if (line[0] == '/' && line[1] == '/')
        continue;

      s = strtok(line, " ");
      while (s) {
        args[i] = s;
        ++i;
        s = strtok(0, " ");
      }
      if (print_usrs(&args[0], &args[i]))
        return 1;
    }
    else
      line[numChars++] = c;
  }

  fclose(fp);
  return 0;
}

/******************************************************************************/
/* Command line processing.                                                   */
/******************************************************************************/
int write_pch_file(const char *filename, int argc, const char *argv[]) {
  CXIndex Idx;
  CXTranslationUnit TU;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  int result = 0;
  
  Idx = clang_createIndex(/* excludeDeclsFromPCH */1, /* displayDiagnostics=*/1);
  
  if (parse_remapped_files(argc, argv, 0, &unsaved_files, &num_unsaved_files)) {
    clang_disposeIndex(Idx);
    return -1;
  }

  Err = clang_parseTranslationUnit2(
      Idx, 0, argv + num_unsaved_files, argc - num_unsaved_files,
      unsaved_files, num_unsaved_files,
      CXTranslationUnit_Incomplete |
          CXTranslationUnit_DetailedPreprocessingRecord |
          CXTranslationUnit_ForSerialization,
      &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    free_remapped_files(unsaved_files, num_unsaved_files);
    clang_disposeTranslationUnit(TU);
    clang_disposeIndex(Idx);
    return 1;
  }

  switch (clang_saveTranslationUnit(TU, filename, 
                                    clang_defaultSaveOptions(TU))) {
  case CXSaveError_None:
    break;

  case CXSaveError_TranslationErrors:
    fprintf(stderr, "Unable to write PCH file %s: translation errors\n", 
            filename);
    result = 2;    
    break;

  case CXSaveError_InvalidTU:
    fprintf(stderr, "Unable to write PCH file %s: invalid translation unit\n", 
            filename);
    result = 3;    
    break;

  case CXSaveError_Unknown:
  default:
    fprintf(stderr, "Unable to write PCH file %s: unknown error \n", filename);
    result = 1;
    break;
  }
  
  clang_disposeTranslationUnit(TU);
  free_remapped_files(unsaved_files, num_unsaved_files);
  clang_disposeIndex(Idx);
  return result;
}

/******************************************************************************/
/* Serialized diagnostics.                                                    */
/******************************************************************************/

static const char *getDiagnosticCodeStr(enum CXLoadDiag_Error error) {
  switch (error) {
    case CXLoadDiag_CannotLoad: return "Cannot Load File";
    case CXLoadDiag_None: break;
    case CXLoadDiag_Unknown: return "Unknown";
    case CXLoadDiag_InvalidFile: return "Invalid File";
  }
  return "None";
}

static const char *getSeverityString(enum CXDiagnosticSeverity severity) {
  switch (severity) {
    case CXDiagnostic_Note: return "note";
    case CXDiagnostic_Error: return "error";
    case CXDiagnostic_Fatal: return "fatal";
    case CXDiagnostic_Ignored: return "ignored";
    case CXDiagnostic_Warning: return "warning";
  }
  return "unknown";
}

static void printIndent(unsigned indent) {
  if (indent == 0)
    return;
  fprintf(stderr, "+");
  --indent;
  while (indent > 0) {
    fprintf(stderr, "-");
    --indent;
  }
}

static void printLocation(CXSourceLocation L) {
  CXFile File;
  CXString FileName;
  unsigned line, column, offset;

  clang_getExpansionLocation(L, &File, &line, &column, &offset);
  FileName = clang_getFileName(File);

  fprintf(stderr, "%s:%d:%d", clang_getCString(FileName), line, column);
  clang_disposeString(FileName);
}

static void printRanges(CXDiagnostic D, unsigned indent) {
  unsigned i, n = clang_getDiagnosticNumRanges(D);
  
  for (i = 0; i < n; ++i) {
    CXSourceLocation Start, End;
    CXSourceRange SR = clang_getDiagnosticRange(D, i);
    Start = clang_getRangeStart(SR);
    End = clang_getRangeEnd(SR);
    
    printIndent(indent);
    fprintf(stderr, "Range: ");
    printLocation(Start);
    fprintf(stderr, " ");
    printLocation(End);
    fprintf(stderr, "\n");
  }
}

static void printFixIts(CXDiagnostic D, unsigned indent) {
  unsigned i, n = clang_getDiagnosticNumFixIts(D);
  fprintf(stderr, "Number FIXITs = %d\n", n);
  for (i = 0 ; i < n; ++i) {
    CXSourceRange ReplacementRange;
    CXString text;
    text = clang_getDiagnosticFixIt(D, i, &ReplacementRange);
    
    printIndent(indent);
    fprintf(stderr, "FIXIT: (");
    printLocation(clang_getRangeStart(ReplacementRange));
    fprintf(stderr, " - ");
    printLocation(clang_getRangeEnd(ReplacementRange));
    fprintf(stderr, "): \"%s\"\n", clang_getCString(text));
    clang_disposeString(text);
  }  
}

static void printDiagnosticSet(CXDiagnosticSet Diags, unsigned indent) {
  unsigned i, n;

  if (!Diags)
    return;
  
  n = clang_getNumDiagnosticsInSet(Diags);
  for (i = 0; i < n; ++i) {
    CXSourceLocation DiagLoc;
    CXDiagnostic D;
    CXFile File;
    CXString FileName, DiagSpelling, DiagOption, DiagCat;
    unsigned line, column, offset;
    const char *FileNameStr = 0, *DiagOptionStr = 0, *DiagCatStr = 0;
    
    D = clang_getDiagnosticInSet(Diags, i);
    DiagLoc = clang_getDiagnosticLocation(D);
    clang_getExpansionLocation(DiagLoc, &File, &line, &column, &offset);
    FileName = clang_getFileName(File);
    FileNameStr = clang_getCString(FileName);
    DiagSpelling = clang_getDiagnosticSpelling(D);

    printIndent(indent);
    
    fprintf(stderr, "%s:%d:%d: %s: %s",
            FileNameStr ? FileNameStr : "(null)",
            line,
            column,
            getSeverityString(clang_getDiagnosticSeverity(D)),
            clang_getCString(DiagSpelling));

    DiagOption = clang_getDiagnosticOption(D, 0);
    DiagOptionStr = clang_getCString(DiagOption);
    if (DiagOptionStr) {
      fprintf(stderr, " [%s]", DiagOptionStr);
    }
    
    DiagCat = clang_getDiagnosticCategoryText(D);
    DiagCatStr = clang_getCString(DiagCat);
    if (DiagCatStr) {
      fprintf(stderr, " [%s]", DiagCatStr);
    }
    
    fprintf(stderr, "\n");
    
    printRanges(D, indent);
    printFixIts(D, indent);
    
    /* Print subdiagnostics. */
    printDiagnosticSet(clang_getChildDiagnostics(D), indent+2);

    clang_disposeString(FileName);
    clang_disposeString(DiagSpelling);
    clang_disposeString(DiagOption);
    clang_disposeString(DiagCat);
  }  
}

static int read_diagnostics(const char *filename) {
  enum CXLoadDiag_Error error;
  CXString errorString;
  CXDiagnosticSet Diags = 0;
  
  Diags = clang_loadDiagnostics(filename, &error, &errorString);
  if (!Diags) {
    fprintf(stderr, "Trouble deserializing file (%s): %s\n",
            getDiagnosticCodeStr(error),
            clang_getCString(errorString));
    clang_disposeString(errorString);
    return 1;
  }
  
  printDiagnosticSet(Diags, 0);
  fprintf(stderr, "Number of diagnostics: %d\n",
          clang_getNumDiagnosticsInSet(Diags));
  clang_disposeDiagnosticSet(Diags);
  return 0;
}

static int perform_print_build_session_timestamp(void) {
  printf("%lld\n", clang_getBuildSessionTimestamp());
  return 0;
}

static int perform_test_single_symbol_sgf(const char *input, int argc,
                                          const char *argv[]) {
  CXIndex Idx;
  CXTranslationUnit TU;
  CXAPISet API;
  struct CXUnsavedFile *unsaved_files = 0;
  int num_unsaved_files = 0;
  enum CXErrorCode Err;
  int result = 0;
  CXString SGF;
  const char *usr;

  usr = input + strlen("-single-symbol-sgf-for=");

  Idx = createIndexWithInvocationEmissionPath(/* excludeDeclsFromPCH */ 1,
                                              /* displayDiagnostics=*/0);
  if (!Idx)
    return -1;

  if (parse_remapped_files(argc, argv, 0, &unsaved_files, &num_unsaved_files)) {
    result = -1;
    goto dispose_index;
  }

  Err = clang_parseTranslationUnit2(
      Idx, 0, argv + num_unsaved_files, argc - num_unsaved_files, unsaved_files,
      num_unsaved_files, getDefaultParsingOptions(), &TU);
  if (Err != CXError_Success) {
    fprintf(stderr, "Unable to load translation unit!\n");
    describeLibclangFailure(Err);
    result = 1;
    goto free_remapped_files;
  }

  Err = clang_createAPISet(TU, &API);
  if (Err != CXError_Success) {
    fprintf(stderr,
            "Unable to create API Set for API information extraction!\n");
    result = 2;
    goto dispose_tu;
  }

  SGF = clang_getSymbolGraphForUSR(usr, API);
  printf("%s", clang_getCString(SGF));

  clang_disposeString(SGF);
  clang_disposeAPISet(API);
dispose_tu:
  clang_disposeTranslationUnit(TU);
free_remapped_files:
  free_remapped_files(unsaved_files, num_unsaved_files);
dispose_index:
  clang_disposeIndex(Idx);
  return result;
}

static void inspect_single_symbol_sgf_cursor(CXCursor Cursor) {
  CXSourceLocation CursorLoc;
  CXString SGFData;
  const char *SGF;
  unsigned line, column;
  CursorLoc = clang_getCursorLocation(Cursor);
  clang_getSpellingLocation(CursorLoc, 0, &line, &column, 0);

  SGFData = clang_getSymbolGraphForCursor(Cursor);
  SGF = clang_getCString(SGFData);
  if (SGF)
    printf("%d:%d: %s\n", line, column, SGF);

  clang_disposeString(SGFData);
}

/******************************************************************************/
/* Command line processing.                                                   */
/******************************************************************************/

static CXCursorVisitor GetVisitor(const char *s) {
  if (s[0] == '\0')
    return FilteredPrintingVisitor;
  if (strcmp(s, "-usrs") == 0)
    return USRVisitor;
  if (strncmp(s, "-memory-usage", 13) == 0)
    return GetVisitor(s + 13);
  return NULL;
}

static void print_usage(void) {
  fprintf(stderr,
    "usage: c-index-test -code-completion-at=<site> <compiler arguments>\n"
    "       c-index-test -code-completion-timing=<site> <compiler arguments>\n"
    "       c-index-test -cursor-at=<site> <compiler arguments>\n"
    "       c-index-test -evaluate-cursor-at=<site> <compiler arguments>\n"
    "       c-index-test -get-macro-info-cursor-at=<site> <compiler arguments>\n"
    "       c-index-test -file-refs-at=<site> <compiler arguments>\n"
    "       c-index-test -file-includes-in=<filename> <compiler arguments>\n");
  fprintf(stderr,
    "       c-index-test -index-file [-check-prefix=<FileCheck prefix>] <compiler arguments>\n"
    "       c-index-test -index-file-full [-check-prefix=<FileCheck prefix>] <compiler arguments>\n"
    "       c-index-test -index-tu [-check-prefix=<FileCheck prefix>] <AST file>\n"
    "       c-index-test -index-compile-db [-check-prefix=<FileCheck prefix>] <compilation database>\n"
    "       c-index-test -test-file-scan <AST file> <source file> "
          "[FileCheck prefix]\n");
  fprintf(stderr,
    "       c-index-test -test-load-tu <AST file> <symbol filter> "
          "[FileCheck prefix]\n"
    "       c-index-test -test-load-tu-usrs <AST file> <symbol filter> "
           "[FileCheck prefix]\n"
    "       c-index-test -test-load-source <symbol filter> {<args>}*\n");
  fprintf(stderr,
    "       c-index-test -test-load-source-memory-usage "
    "<symbol filter> {<args>}*\n"
    "       c-index-test -test-load-source-reparse <trials> <symbol filter> "
    "          {<args>}*\n"
    "       c-index-test -test-load-source-usrs <symbol filter> {<args>}*\n"
    "       c-index-test -test-load-source-usrs-memory-usage "
          "<symbol filter> {<args>}*\n"
    "       c-index-test -test-annotate-tokens=<range> {<args>}*\n"
    "       c-index-test -test-inclusion-stack-source {<args>}*\n"
    "       c-index-test -test-inclusion-stack-tu <AST file>\n");
  fprintf(stderr,
    "       c-index-test -test-print-linkage-source {<args>}*\n"
    "       c-index-test -test-print-visibility {<args>}*\n"
    "       c-index-test -test-print-type {<args>}*\n"
    "       c-index-test -test-print-type-size {<args>}*\n"
    "       c-index-test -test-print-bitwidth {<args>}*\n"
    "       c-index-test -test-print-target-info {<args>}*\n"
    "       c-index-test -test-print-type-declaration {<args>}*\n"
    "       c-index-test -print-usr [<CursorKind> {<args>}]*\n"
    "       c-index-test -print-usr-file <file>\n");
  fprintf(stderr,
          "       c-index-test -single-symbol-sgfs <symbol filter> {<args>*}\n"
          "       c-index-test -single-symbol-sgf-at=<site> {<args>*}\n"
          "       c-index-test -single-symbol-sgf-for=<usr> {<args>}*\n");
  fprintf(stderr,
    "       c-index-test -write-pch <file> <compiler arguments>\n"
    "       c-index-test -compilation-db [lookup <filename>] database\n");
  fprintf(stderr,
    "       c-index-test -print-build-session-timestamp\n");
  fprintf(stderr,
    "       c-index-test -read-diagnostics <file>\n\n");
  fprintf(stderr,
    " <symbol filter> values:\n%s",
    "   all - load all symbols, including those from PCH\n"
    "   local - load all symbols except those in PCH\n"
    "   category - only load ObjC categories (non-PCH)\n"
    "   interface - only load ObjC interfaces (non-PCH)\n"
    "   protocol - only load ObjC protocols (non-PCH)\n"
    "   function - only load functions (non-PCH)\n"
    "   typedef - only load typdefs (non-PCH)\n"
    "   scan-function - scan function bodies (non-PCH)\n\n");
}

/***/

int cindextest_main(int argc, const char **argv) {
  clang_enableStackTraces();
  if (argc > 2 && strcmp(argv[1], "-read-diagnostics") == 0)
      return read_diagnostics(argv[2]);
  if (argc > 2 && strstr(argv[1], "-code-completion-at=") == argv[1])
    return perform_code_completion(argc, argv, 0);
  if (argc > 2 && strstr(argv[1], "-code-completion-timing=") == argv[1])
    return perform_code_completion(argc, argv, 1);
  if (argc > 2 && strstr(argv[1], "-cursor-at=") == argv[1])
    return inspect_cursor_at(argc, argv, "-cursor-at=", inspect_print_cursor);
  if (argc > 2 && strstr(argv[1], "-evaluate-cursor-at=") == argv[1])
    return inspect_cursor_at(argc, argv, "-evaluate-cursor-at=",
                             inspect_evaluate_cursor);
  if (argc > 2 && strstr(argv[1], "-get-macro-info-cursor-at=") == argv[1])
    return inspect_cursor_at(argc, argv, "-get-macro-info-cursor-at=",
                             inspect_macroinfo_cursor);
  if (argc > 2 && strstr(argv[1], "-file-refs-at=") == argv[1])
    return find_file_refs_at(argc, argv);
  if (argc > 2 && strstr(argv[1], "-file-includes-in=") == argv[1])
    return find_file_includes_in(argc, argv);
  if (argc > 2 && strcmp(argv[1], "-index-file") == 0)
    return index_file(argc - 2, argv + 2, /*full=*/0);
  if (argc > 2 && strcmp(argv[1], "-index-file-full") == 0)
    return index_file(argc - 2, argv + 2, /*full=*/1);
  if (argc > 2 && strcmp(argv[1], "-index-tu") == 0)
    return index_tu(argc - 2, argv + 2);
  if (argc > 2 && strcmp(argv[1], "-index-compile-db") == 0)
    return index_compile_db(argc - 2, argv + 2);
  else if (argc >= 4 && strncmp(argv[1], "-test-load-tu", 13) == 0) {
    CXCursorVisitor I = GetVisitor(argv[1] + 13);
    if (I)
      return perform_test_load_tu(argv[2], argv[3], argc >= 5 ? argv[4] : 0, I,
                                  NULL);
  }
  else if (argc >= 5 && strncmp(argv[1], "-test-load-source-reparse", 25) == 0){
    CXCursorVisitor I = GetVisitor(argv[1] + 25);
    if (I) {
      int trials = atoi(argv[2]);
      return perform_test_reparse_source(argc - 4, argv + 4, trials, argv[3], I, 
                                         NULL);
    }
  }
  else if (argc >= 4 && strncmp(argv[1], "-test-load-source", 17) == 0) {
    CXCursorVisitor I = GetVisitor(argv[1] + 17);
    
    PostVisitTU postVisit = 0;
    if (strstr(argv[1], "-memory-usage"))
      postVisit = PrintMemoryUsage;
    
    if (I)
      return perform_test_load_source(argc - 3, argv + 3, argv[2], I,
                                      postVisit);
  }
  else if (argc >= 3 && strcmp(argv[1], "-single-file-parse") == 0)
    return perform_single_file_parse(argv[2]);
  else if (argc >= 3 && strcmp(argv[1], "-retain-excluded-conditional-blocks") == 0)
    return perform_file_retain_excluded_cb(argv[2]);
  else if (argc >= 4 && strcmp(argv[1], "-test-file-scan") == 0)
    return perform_file_scan(argv[2], argv[3],
                             argc >= 5 ? argv[4] : 0);
  else if (argc > 2 && strstr(argv[1], "-test-annotate-tokens=") == argv[1])
    return perform_token_annotation(argc, argv);
  else if (argc > 2 && strcmp(argv[1], "-test-inclusion-stack-source") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all", NULL,
                                    PrintInclusionStack);
  else if (argc > 2 && strcmp(argv[1], "-test-inclusion-stack-tu") == 0)
    return perform_test_load_tu(argv[2], "all", NULL, NULL,
                                PrintInclusionStack);
  else if (argc > 2 && strcmp(argv[1], "-test-print-linkage-source") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all", PrintLinkage,
                                    NULL);
  else if (argc > 2 && strcmp(argv[1], "-test-print-visibility") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all", PrintVisibility,
                                    NULL);
  else if (argc > 2 && strcmp(argv[1], "-test-print-type") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all",
                                    PrintType, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-type-size") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all",
                                    PrintTypeSize, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-type-declaration") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all",
                                    PrintTypeDeclaration, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-decl-attributes") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all",
                                    PrintDeclAttributes, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-bitwidth") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all",
                                    PrintBitWidth, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-binops") == 0)
    return perform_test_load_source(argc - 2, argv + 2, "all", PrintBinOps, 0);
  else if (argc > 2 && strcmp(argv[1], "-test-print-mangle") == 0)
    return perform_test_load_tu(argv[2], "all", NULL, PrintMangledName, NULL);
  else if (argc > 2 && strcmp(argv[1], "-test-print-manglings") == 0)
    return perform_test_load_tu(argv[2], "all", NULL, PrintManglings, NULL);
  else if (argc > 2 && strcmp(argv[1], "-test-print-target-info") == 0)
    return print_target_info(argc - 2, argv + 2);
  else if (argc > 1 && strcmp(argv[1], "-print-usr") == 0) {
    if (argc > 2)
      return print_usrs(argv + 2, argv + argc);
    else {
      display_usrs();
      return 1;
    }
  }
  else if (argc > 2 && strcmp(argv[1], "-print-usr-file") == 0)
    return print_usrs_file(argv[2]);
  else if (argc > 2 && strcmp(argv[1], "-write-pch") == 0)
    return write_pch_file(argv[2], argc - 3, argv + 3);
  else if (argc > 2 && strcmp(argv[1], "-compilation-db") == 0)
    return perform_test_compilation_db(argv[argc-1], argc - 3, argv + 2);
  else if (argc == 2 && strcmp(argv[1], "-print-build-session-timestamp") == 0)
    return perform_print_build_session_timestamp();
  else if (argc > 3 && strcmp(argv[1], "-single-symbol-sgfs") == 0)
    return perform_test_load_source(argc - 3, argv + 3, argv[2],
                                    PrintSingleSymbolSGFs, NULL);
  else if (argc > 2 && strstr(argv[1], "-single-symbol-sgf-at=") == argv[1])
    return inspect_cursor_at(
        argc, argv, "-single-symbol-sgf-at=", inspect_single_symbol_sgf_cursor);
  else if (argc > 2 && strstr(argv[1], "-single-symbol-sgf-for=") == argv[1])
    return perform_test_single_symbol_sgf(argv[1], argc - 2, argv + 2);

  print_usage();
  return 1;
}

/***/

/* We intentionally run in a separate thread to ensure we at least minimal
 * testing of a multithreaded environment (for example, having a reduced stack
 * size). */

typedef struct thread_info {
  int (*main_func)(int argc, const char **argv);
  int argc;
  const char **argv;
  int result;
} thread_info;
void thread_runner(void *client_data_v) {
  thread_info *client_data = client_data_v;
  client_data->result = client_data->main_func(client_data->argc,
                                               client_data->argv);
}

static void flush_atexit(void) {
  /* stdout, and surprisingly even stderr, are not always flushed on process
   * and thread exit, particularly when the system is under heavy load. */
  fflush(stdout);
  fflush(stderr);
}

int main(int argc, const char **argv) {
  thread_info client_data;

#ifdef __MVS__
  if (enableAutoConversion(fileno(stdout)) == -1)
    fprintf(stderr, "Setting conversion on stdout failed\n");

  if (enableAutoConversion(fileno(stderr)) == -1)
    fprintf(stderr, "Setting conversion on stderr failed\n");
#endif

  atexit(flush_atexit);

#ifdef CLANG_HAVE_LIBXML
  LIBXML_TEST_VERSION
#endif

  if (argc > 1 && strcmp(argv[1], "core") == 0)
    return indextest_core_main(argc, argv);

  client_data.main_func = cindextest_main;
  client_data.argc = argc;
  client_data.argv = argv;

  if (getenv("CINDEXTEST_NOTHREADS"))
    return client_data.main_func(client_data.argc, client_data.argv);

  clang_executeOnThread(thread_runner, &client_data, 0);
  return client_data.result;
}
