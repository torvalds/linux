//===----------------------------------------------------------------------===//
// C Language Family Front-end
//===----------------------------------------------------------------------===//
                                                             Chris Lattner

I. Introduction:
 
 clang: noun
    1. A loud, resonant, metallic sound.
    2. The strident call of a crane or goose.
    3. C-language family front-end toolkit.

 The world needs better compiler tools, tools which are built as libraries. This
 design point allows reuse of the tools in new and novel ways. However, building
 the tools as libraries isn't enough: they must have clean APIs, be as
 decoupled from each other as possible, and be easy to modify/extend.  This
 requires clean layering, decent design, and avoiding tying the libraries to a
 specific use.  Oh yeah, did I mention that we want the resultant libraries to
 be as fast as possible? :)

 This front-end is built as a component of the LLVM toolkit that can be used
 with the LLVM backend or independently of it.  In this spirit, the API has been
 carefully designed as the following components:
 
   libsupport  - Basic support library, reused from LLVM.

   libsystem   - System abstraction library, reused from LLVM.
   
   libbasic    - Diagnostics, SourceLocations, SourceBuffer abstraction,
                 file system caching for input source files.  This depends on
                 libsupport and libsystem.

   libast      - Provides classes to represent the C AST, the C type system,
                 builtin functions, and various helpers for analyzing and
                 manipulating the AST (visitors, pretty printers, etc).  This
                 library depends on libbasic.


   liblex      - C/C++/ObjC lexing and preprocessing, identifier hash table,
                 pragma handling, tokens, and macros.  This depends on libbasic.

   libparse    - C (for now) parsing and local semantic analysis. This library
                 invokes coarse-grained 'Actions' provided by the client to do
                 stuff (e.g. libsema builds ASTs).  This depends on liblex.

   libsema     - Provides a set of parser actions to build a standardized AST
                 for programs.  AST's are 'streamed' out a top-level declaration
                 at a time, allowing clients to use decl-at-a-time processing,
                 build up entire translation units, or even build 'whole
                 program' ASTs depending on how they use the APIs.  This depends
                 on libast and libparse.

   librewrite  - Fast, scalable rewriting of source code.  This operates on
                 the raw syntactic text of source code, allowing a client
                 to insert and delete text in very large source files using
                 the same source location information embedded in ASTs.  This
                 is intended to be a low-level API that is useful for
                 higher-level clients and libraries such as code refactoring.

   libanalysis - Source-level dataflow analysis useful for performing analyses
                 such as computing live variables.  It also includes a
                 path-sensitive "graph-reachability" engine for writing
                 analyses that reason about different possible paths of
                 execution through source code.  This is currently being
                 employed to write a set of checks for finding bugs in software.

   libcodegen  - Lower the AST to LLVM IR for optimization & codegen.  Depends
                 on libast.
                 
   clang       - An example driver, client of the libraries at various levels.
                 This depends on all these libraries, and on LLVM VMCore.

 This front-end has been intentionally built as a DAG of libraries, making it
 easy to  reuse individual parts or replace pieces if desired. For example, to
 build a preprocessor, you take the Basic and Lexer libraries. If you want an
 indexer, you take those plus the Parser library and provide some actions for
 indexing.  If you want a refactoring, static analysis, or source-to-source
 compiler tool, it makes sense to take those plus the AST building and semantic
 analyzer library.  Finally, if you want to use this with the LLVM backend,
 you'd take these components plus the AST to LLVM lowering code.
 
 In the future I hope this toolkit will grow to include new and interesting
 components, including a C++ front-end, ObjC support, and a whole lot of other
 things.

 Finally, it should be pointed out that the goal here is to build something that
 is high-quality and industrial-strength: all the obnoxious features of the C
 family must be correctly supported (trigraphs, preprocessor arcana, K&R-style
 prototypes, GCC/MS extensions, etc).  It cannot be used if it is not 'real'.


II. Usage of clang driver:

 * Basic Command-Line Options:
   - Help: clang --help
   - Standard GCC options accepted: -E, -I*, -i*, -pedantic, -std=c90, etc.
   - To make diagnostics more gcc-like: -fno-caret-diagnostics -fno-show-column
   - Enable metric printing: -stats

 * -fsyntax-only is currently the default mode.

 * -E mode works the same way as GCC.

 * -Eonly mode does all preprocessing, but does not print the output,
     useful for timing the preprocessor.
 
 * -fsyntax-only is currently partially implemented, lacking some
     semantic analysis (some errors and warnings are not produced).

 * -parse-noop parses code without building an AST.  This is useful
     for timing the cost of the parser without including AST building
     time.
 
 * -parse-ast builds ASTs, but doesn't print them.  This is most
     useful for timing AST building vs -parse-noop.
 
 * -parse-ast-print pretty prints most expression and statements nodes.

 * -parse-ast-check checks that diagnostic messages that are expected
     are reported and that those which are reported are expected.

 * -dump-cfg builds ASTs and then CFGs.  CFGs are then pretty-printed.

 * -view-cfg builds ASTs and then CFGs.  CFGs are then visualized by
     invoking Graphviz.

     For more information on getting Graphviz to work with clang/LLVM,
     see: https://llvm.org/docs/ProgrammersManual.html#ViewGraph


III. Current advantages over GCC:

 * Column numbers are fully tracked (no 256 col limit, no GCC-style pruning).
 * All diagnostics have column numbers, includes 'caret diagnostics', and they
   highlight regions of interesting code (e.g. the LHS and RHS of a binop).
 * Full diagnostic customization by client (can format diagnostics however they
   like, e.g. in an IDE or refactoring tool) through DiagnosticClient interface.
 * Built as a framework, can be reused by multiple tools.
 * All languages supported linked into same library (no cc1,cc1obj, ...).
 * mmap's code in read-only, does not dirty the pages like GCC (mem footprint).
 * LLVM License, can be linked into non-GPL projects.
 * Full diagnostic control, per diagnostic.  Diagnostics are identified by ID.
 * Significantly faster than GCC at semantic analysis, parsing, preprocessing
   and lexing.
 * Defers exposing platform-specific stuff to as late as possible, tracks use of
   platform-specific features (e.g. #ifdef PPC) to allow 'portable bytecodes'.
 * The lexer doesn't rely on the "lexer hack": it has no notion of scope and
   does not categorize identifiers as types or variables -- this is up to the
   parser to decide.

Potential Future Features:

 * Fine grained diag control within the source (#pragma enable/disable warning).
 * Better token tracking within macros?  (Token came from this line, which is
   a macro argument instantiated here, recursively instantiated here).
 * Fast #import with a module system.
 * Dependency tracking: change to header file doesn't recompile every function
   that texually depends on it: recompile only those functions that need it.
   This is aka 'incremental parsing'.


IV. Missing Functionality / Improvements

Lexer:
 * Source character mapping.  GCC supports ASCII and UTF-8.
   See GCC options: -ftarget-charset and -ftarget-wide-charset.
 * Universal character support.  Experimental in GCC, enabled with
   -fextended-identifiers.
 * -fpreprocessed mode.

Preprocessor:
 * #assert/#unassert
 * MSExtension: "L#param" stringizes to a wide string literal.
 * Add support for -M*

Traditional Preprocessor:
 * Currently, we have none. :)

