/*===-- llvm-c/lto.h - LTO Public C Interface ---------------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header provides public interface to an abstract link time optimization*|
|* library.  LLVM provides an implementation of this interface for use with   *|
|* llvm bitcode files.                                                        *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_LTO_H
#define LLVM_C_LTO_H

#include "llvm-c/ExternC.h"

#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif
#include <sys/types.h>

#ifndef __cplusplus
#if !defined(_MSC_VER)
#include <stdbool.h>
typedef bool lto_bool_t;
#else
/* MSVC in particular does not have anything like _Bool or bool in C, but we can
   at least make sure the type is the same size.  The implementation side will
   use C++ bool. */
typedef unsigned char lto_bool_t;
#endif
#else
typedef bool lto_bool_t;
#endif

/**
 * @defgroup LLVMCLTO LTO
 * @ingroup LLVMC
 *
 * @{
 */

#define LTO_API_VERSION 29

/**
 * \since prior to LTO_API_VERSION=3
 */
typedef enum {
    LTO_SYMBOL_ALIGNMENT_MASK              = 0x0000001F, /* log2 of alignment */
    LTO_SYMBOL_PERMISSIONS_MASK            = 0x000000E0,
    LTO_SYMBOL_PERMISSIONS_CODE            = 0x000000A0,
    LTO_SYMBOL_PERMISSIONS_DATA            = 0x000000C0,
    LTO_SYMBOL_PERMISSIONS_RODATA          = 0x00000080,
    LTO_SYMBOL_DEFINITION_MASK             = 0x00000700,
    LTO_SYMBOL_DEFINITION_REGULAR          = 0x00000100,
    LTO_SYMBOL_DEFINITION_TENTATIVE        = 0x00000200,
    LTO_SYMBOL_DEFINITION_WEAK             = 0x00000300,
    LTO_SYMBOL_DEFINITION_UNDEFINED        = 0x00000400,
    LTO_SYMBOL_DEFINITION_WEAKUNDEF        = 0x00000500,
    LTO_SYMBOL_SCOPE_MASK                  = 0x00003800,
    LTO_SYMBOL_SCOPE_INTERNAL              = 0x00000800,
    LTO_SYMBOL_SCOPE_HIDDEN                = 0x00001000,
    LTO_SYMBOL_SCOPE_PROTECTED             = 0x00002000,
    LTO_SYMBOL_SCOPE_DEFAULT               = 0x00001800,
    LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN = 0x00002800,
    LTO_SYMBOL_COMDAT                      = 0x00004000,
    LTO_SYMBOL_ALIAS                       = 0x00008000
} lto_symbol_attributes;

/**
 * \since prior to LTO_API_VERSION=3
 */
typedef enum {
    LTO_DEBUG_MODEL_NONE         = 0,
    LTO_DEBUG_MODEL_DWARF        = 1
} lto_debug_model;

/**
 * \since prior to LTO_API_VERSION=3
 */
typedef enum {
    LTO_CODEGEN_PIC_MODEL_STATIC         = 0,
    LTO_CODEGEN_PIC_MODEL_DYNAMIC        = 1,
    LTO_CODEGEN_PIC_MODEL_DYNAMIC_NO_PIC = 2,
    LTO_CODEGEN_PIC_MODEL_DEFAULT        = 3
} lto_codegen_model;

/** opaque reference to a loaded object module */
typedef struct LLVMOpaqueLTOModule *lto_module_t;

/** opaque reference to a code generator */
typedef struct LLVMOpaqueLTOCodeGenerator *lto_code_gen_t;

/** opaque reference to a thin code generator */
typedef struct LLVMOpaqueThinLTOCodeGenerator *thinlto_code_gen_t;

LLVM_C_EXTERN_C_BEGIN

/**
 * Returns a printable string.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern const char*
lto_get_version(void);

/**
 * Returns the last error string or NULL if last operation was successful.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern const char*
lto_get_error_message(void);

/**
 * Checks if a file is a loadable object file.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_module_is_object_file(const char* path);

/**
 * Checks if a file is a loadable object compiled for requested target.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_module_is_object_file_for_target(const char* path,
                                     const char* target_triple_prefix);

/**
 * Return true if \p Buffer contains a bitcode file with ObjC code (category
 * or class) in it.
 *
 * \since LTO_API_VERSION=20
 */
extern lto_bool_t
lto_module_has_objc_category(const void *mem, size_t length);

/**
 * Checks if a buffer is a loadable object file.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t lto_module_is_object_file_in_memory(const void *mem,
                                                      size_t length);

/**
 * Checks if a buffer is a loadable object compiled for requested target.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_module_is_object_file_in_memory_for_target(const void* mem, size_t length,
                                              const char* target_triple_prefix);

/**
 * Loads an object file from disk.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_module_t
lto_module_create(const char* path);

/**
 * Loads an object file from memory.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_module_t
lto_module_create_from_memory(const void* mem, size_t length);

/**
 * Loads an object file from memory with an extra path argument.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=9
 */
extern lto_module_t
lto_module_create_from_memory_with_path(const void* mem, size_t length,
                                        const char *path);

/**
 * Loads an object file in its own context.
 *
 * Loads an object file in its own LLVMContext.  This function call is
 * thread-safe.  However, modules created this way should not be merged into an
 * lto_code_gen_t using \a lto_codegen_add_module().
 *
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=11
 */
extern lto_module_t
lto_module_create_in_local_context(const void *mem, size_t length,
                                   const char *path);

/**
 * Loads an object file in the codegen context.
 *
 * Loads an object file into the same context as \c cg.  The module is safe to
 * add using \a lto_codegen_add_module().
 *
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=11
 */
extern lto_module_t
lto_module_create_in_codegen_context(const void *mem, size_t length,
                                     const char *path, lto_code_gen_t cg);

/**
 * Loads an object file from disk. The seek point of fd is not preserved.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=5
 */
extern lto_module_t
lto_module_create_from_fd(int fd, const char *path, size_t file_size);

/**
 * Loads an object file from disk. The seek point of fd is not preserved.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=5
 */
extern lto_module_t
lto_module_create_from_fd_at_offset(int fd, const char *path, size_t file_size,
                                    size_t map_size, off_t offset);

/**
 * Frees all memory internally allocated by the module.
 * Upon return the lto_module_t is no longer valid.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern void
lto_module_dispose(lto_module_t mod);

/**
 * Returns triple string which the object module was compiled under.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern const char*
lto_module_get_target_triple(lto_module_t mod);

/**
 * Sets triple string with which the object will be codegened.
 *
 * \since LTO_API_VERSION=4
 */
extern void
lto_module_set_target_triple(lto_module_t mod, const char *triple);

/**
 * Returns the number of symbols in the object module.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern unsigned int
lto_module_get_num_symbols(lto_module_t mod);

/**
 * Returns the name of the ith symbol in the object module.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern const char*
lto_module_get_symbol_name(lto_module_t mod, unsigned int index);

/**
 * Returns the attributes of the ith symbol in the object module.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_symbol_attributes
lto_module_get_symbol_attribute(lto_module_t mod, unsigned int index);

/**
 * Returns the module's linker options.
 *
 * The linker options may consist of multiple flags. It is the linker's
 * responsibility to split the flags using a platform-specific mechanism.
 *
 * \since LTO_API_VERSION=16
 */
extern const char*
lto_module_get_linkeropts(lto_module_t mod);

/**
 * If targeting mach-o on darwin, this function gets the CPU type and subtype
 * that will end up being encoded in the mach-o header. These are the values
 * that can be found in mach/machine.h.
 *
 * \p out_cputype and \p out_cpusubtype must be non-NULL.
 *
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=27
 */
extern lto_bool_t lto_module_get_macho_cputype(lto_module_t mod,
                                               unsigned int *out_cputype,
                                               unsigned int *out_cpusubtype);

/**
 * This function can be used by the linker to check if a given module has
 * any constructor or destructor functions.
 *
 * Returns true if the module has either the @llvm.global_ctors or the
 * @llvm.global_dtors symbol. Otherwise returns false.
 *
 * \since LTO_API_VERSION=29
 */
extern lto_bool_t lto_module_has_ctor_dtor(lto_module_t mod);
/**
 * Diagnostic severity.
 *
 * \since LTO_API_VERSION=7
 */
typedef enum {
  LTO_DS_ERROR = 0,
  LTO_DS_WARNING = 1,
  LTO_DS_REMARK = 3, // Added in LTO_API_VERSION=10.
  LTO_DS_NOTE = 2
} lto_codegen_diagnostic_severity_t;

/**
 * Diagnostic handler type.
 * \p severity defines the severity.
 * \p diag is the actual diagnostic.
 * The diagnostic is not prefixed by any of severity keyword, e.g., 'error: '.
 * \p ctxt is used to pass the context set with the diagnostic handler.
 *
 * \since LTO_API_VERSION=7
 */
typedef void (*lto_diagnostic_handler_t)(
    lto_codegen_diagnostic_severity_t severity, const char *diag, void *ctxt);

/**
 * Set a diagnostic handler and the related context (void *).
 * This is more general than lto_get_error_message, as the diagnostic handler
 * can be called at anytime within lto.
 *
 * \since LTO_API_VERSION=7
 */
extern void lto_codegen_set_diagnostic_handler(lto_code_gen_t,
                                               lto_diagnostic_handler_t,
                                               void *);

/**
 * Instantiates a code generator.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 * All modules added using \a lto_codegen_add_module() must have been created
 * in the same context as the codegen.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_code_gen_t
lto_codegen_create(void);

/**
 * Instantiate a code generator in its own context.
 *
 * Instantiates a code generator in its own context.  Modules added via \a
 * lto_codegen_add_module() must have all been created in the same context,
 * using \a lto_module_create_in_codegen_context().
 *
 * \since LTO_API_VERSION=11
 */
extern lto_code_gen_t
lto_codegen_create_in_local_context(void);

/**
 * Frees all code generator and all memory it internally allocated.
 * Upon return the lto_code_gen_t is no longer valid.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern void
lto_codegen_dispose(lto_code_gen_t);

/**
 * Add an object module to the set of modules for which code will be generated.
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \c cg and \c mod must both be in the same context.  See \a
 * lto_codegen_create_in_local_context() and \a
 * lto_module_create_in_codegen_context().
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_codegen_add_module(lto_code_gen_t cg, lto_module_t mod);

/**
 * Sets the object module for code generation. This will transfer the ownership
 * of the module to the code generator.
 *
 * \c cg and \c mod must both be in the same context.
 *
 * \since LTO_API_VERSION=13
 */
extern void
lto_codegen_set_module(lto_code_gen_t cg, lto_module_t mod);

/**
 * Sets if debug info should be generated.
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_codegen_set_debug_model(lto_code_gen_t cg, lto_debug_model);

/**
 * Sets which PIC code model to generated.
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \since prior to LTO_API_VERSION=3
 */
extern lto_bool_t
lto_codegen_set_pic_model(lto_code_gen_t cg, lto_codegen_model);

/**
 * Sets the cpu to generate code for.
 *
 * \since LTO_API_VERSION=4
 */
extern void
lto_codegen_set_cpu(lto_code_gen_t cg, const char *cpu);

/**
 * Sets the location of the assembler tool to run. If not set, libLTO
 * will use gcc to invoke the assembler.
 *
 * \since LTO_API_VERSION=3
 */
extern void
lto_codegen_set_assembler_path(lto_code_gen_t cg, const char* path);

/**
 * Sets extra arguments that libLTO should pass to the assembler.
 *
 * \since LTO_API_VERSION=4
 */
extern void
lto_codegen_set_assembler_args(lto_code_gen_t cg, const char **args,
                               int nargs);

/**
 * Adds to a list of all global symbols that must exist in the final generated
 * code. If a function is not listed there, it might be inlined into every usage
 * and optimized away.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern void
lto_codegen_add_must_preserve_symbol(lto_code_gen_t cg, const char* symbol);

/**
 * Writes a new object file at the specified path that contains the
 * merged contents of all modules added so far.
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=5
 */
extern lto_bool_t
lto_codegen_write_merged_modules(lto_code_gen_t cg, const char* path);

/**
 * Generates code for all added modules into one native object file.
 * This calls lto_codegen_optimize then lto_codegen_compile_optimized.
 *
 * On success returns a pointer to a generated mach-o/ELF buffer and
 * length set to the buffer size.  The buffer is owned by the
 * lto_code_gen_t and will be freed when lto_codegen_dispose()
 * is called, or lto_codegen_compile() is called again.
 * On failure, returns NULL (check lto_get_error_message() for details).
 *
 * \since prior to LTO_API_VERSION=3
 */
extern const void*
lto_codegen_compile(lto_code_gen_t cg, size_t* length);

/**
 * Generates code for all added modules into one native object file.
 * This calls lto_codegen_optimize then lto_codegen_compile_optimized (instead
 * of returning a generated mach-o/ELF buffer, it writes to a file).
 *
 * The name of the file is written to name. Returns true on error.
 *
 * \since LTO_API_VERSION=5
 */
extern lto_bool_t
lto_codegen_compile_to_file(lto_code_gen_t cg, const char** name);

/**
 * Runs optimization for the merged module. Returns true on error.
 *
 * \since LTO_API_VERSION=12
 */
extern lto_bool_t
lto_codegen_optimize(lto_code_gen_t cg);

/**
 * Generates code for the optimized merged module into one native object file.
 * It will not run any IR optimizations on the merged module.
 *
 * On success returns a pointer to a generated mach-o/ELF buffer and length set
 * to the buffer size.  The buffer is owned by the lto_code_gen_t and will be
 * freed when lto_codegen_dispose() is called, or
 * lto_codegen_compile_optimized() is called again. On failure, returns NULL
 * (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=12
 */
extern const void*
lto_codegen_compile_optimized(lto_code_gen_t cg, size_t* length);

/**
 * Returns the runtime API version.
 *
 * \since LTO_API_VERSION=12
 */
extern unsigned int
lto_api_version(void);

/**
 * Parses options immediately, making them available as early as possible. For
 * example during executing codegen::InitTargetOptionsFromCodeGenFlags. Since
 * parsing shud only happen once, only one of lto_codegen_debug_options or
 * lto_set_debug_options should be called.
 *
 * This function takes one or more options separated by spaces.
 * Warning: passing file paths through this function may confuse the argument
 * parser if the paths contain spaces.
 *
 * \since LTO_API_VERSION=28
 */
extern void lto_set_debug_options(const char *const *options, int number);

/**
 * Sets options to help debug codegen bugs. Since parsing shud only happen once,
 * only one of lto_codegen_debug_options or lto_set_debug_options
 * should be called.
 *
 * This function takes one or more options separated by spaces.
 * Warning: passing file paths through this function may confuse the argument
 * parser if the paths contain spaces.
 *
 * \since prior to LTO_API_VERSION=3
 */
extern void
lto_codegen_debug_options(lto_code_gen_t cg, const char *);

/**
 * Same as the previous function, but takes every option separately through an
 * array.
 *
 * \since prior to LTO_API_VERSION=26
 */
extern void lto_codegen_debug_options_array(lto_code_gen_t cg,
                                            const char *const *, int number);

/**
 * Initializes LLVM disassemblers.
 * FIXME: This doesn't really belong here.
 *
 * \since LTO_API_VERSION=5
 */
extern void
lto_initialize_disassembler(void);

/**
 * Sets if we should run internalize pass during optimization and code
 * generation.
 *
 * \since LTO_API_VERSION=14
 */
extern void
lto_codegen_set_should_internalize(lto_code_gen_t cg,
                                   lto_bool_t ShouldInternalize);

/**
 * Set whether to embed uselists in bitcode.
 *
 * Sets whether \a lto_codegen_write_merged_modules() should embed uselists in
 * output bitcode.  This should be turned on for all -save-temps output.
 *
 * \since LTO_API_VERSION=15
 */
extern void
lto_codegen_set_should_embed_uselists(lto_code_gen_t cg,
                                      lto_bool_t ShouldEmbedUselists);

/** Opaque reference to an LTO input file */
typedef struct LLVMOpaqueLTOInput *lto_input_t;

/**
  * Creates an LTO input file from a buffer. The path
  * argument is used for diagnotics as this function
  * otherwise does not know which file the given buffer
  * is associated with.
  *
  * \since LTO_API_VERSION=24
  */
extern lto_input_t lto_input_create(const void *buffer,
                                    size_t buffer_size,
                                    const char *path);

/**
  * Frees all memory internally allocated by the LTO input file.
  * Upon return the lto_module_t is no longer valid.
  *
  * \since LTO_API_VERSION=24
  */
extern void lto_input_dispose(lto_input_t input);

/**
  * Returns the number of dependent library specifiers
  * for the given LTO input file.
  *
  * \since LTO_API_VERSION=24
  */
extern unsigned lto_input_get_num_dependent_libraries(lto_input_t input);

/**
  * Returns the ith dependent library specifier
  * for the given LTO input file. The returned
  * string is not null-terminated.
  *
  * \since LTO_API_VERSION=24
  */
extern const char * lto_input_get_dependent_library(lto_input_t input,
                                                    size_t index,
                                                    size_t *size);

/**
 * Returns the list of libcall symbols that can be generated by LTO
 * that might not be visible from the symbol table of bitcode files.
 *
 * \since prior to LTO_API_VERSION=25
 */
extern const char *const *lto_runtime_lib_symbols_list(size_t *size);

/**
 * @} // endgoup LLVMCLTO
 * @defgroup LLVMCTLTO ThinLTO
 * @ingroup LLVMC
 *
 * @{
 */

/**
 * Type to wrap a single object returned by ThinLTO.
 *
 * \since LTO_API_VERSION=18
 */
typedef struct {
  const char *Buffer;
  size_t Size;
} LTOObjectBuffer;

/**
 * Instantiates a ThinLTO code generator.
 * Returns NULL on error (check lto_get_error_message() for details).
 *
 *
 * The ThinLTOCodeGenerator is not intended to be reuse for multiple
 * compilation: the model is that the client adds modules to the generator and
 * ask to perform the ThinLTO optimizations / codegen, and finally destroys the
 * codegenerator.
 *
 * \since LTO_API_VERSION=18
 */
extern thinlto_code_gen_t thinlto_create_codegen(void);

/**
 * Frees the generator and all memory it internally allocated.
 * Upon return the thinlto_code_gen_t is no longer valid.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_dispose(thinlto_code_gen_t cg);

/**
 * Add a module to a ThinLTO code generator. Identifier has to be unique among
 * all the modules in a code generator. The data buffer stays owned by the
 * client, and is expected to be available for the entire lifetime of the
 * thinlto_code_gen_t it is added to.
 *
 * On failure, returns NULL (check lto_get_error_message() for details).
 *
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_add_module(thinlto_code_gen_t cg,
                                       const char *identifier, const char *data,
                                       int length);

/**
 * Optimize and codegen all the modules added to the codegenerator using
 * ThinLTO. Resulting objects are accessible using thinlto_module_get_object().
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_process(thinlto_code_gen_t cg);

/**
 * Returns the number of object files produced by the ThinLTO CodeGenerator.
 *
 * It usually matches the number of input files, but this is not a guarantee of
 * the API and may change in future implementation, so the client should not
 * assume it.
 *
 * \since LTO_API_VERSION=18
 */
extern unsigned int thinlto_module_get_num_objects(thinlto_code_gen_t cg);

/**
 * Returns a reference to the ith object file produced by the ThinLTO
 * CodeGenerator.
 *
 * Client should use \p thinlto_module_get_num_objects() to get the number of
 * available objects.
 *
 * \since LTO_API_VERSION=18
 */
extern LTOObjectBuffer thinlto_module_get_object(thinlto_code_gen_t cg,
                                                 unsigned int index);

/**
 * Returns the number of object files produced by the ThinLTO CodeGenerator.
 *
 * It usually matches the number of input files, but this is not a guarantee of
 * the API and may change in future implementation, so the client should not
 * assume it.
 *
 * \since LTO_API_VERSION=21
 */
unsigned int thinlto_module_get_num_object_files(thinlto_code_gen_t cg);

/**
 * Returns the path to the ith object file produced by the ThinLTO
 * CodeGenerator.
 *
 * Client should use \p thinlto_module_get_num_object_files() to get the number
 * of available objects.
 *
 * \since LTO_API_VERSION=21
 */
const char *thinlto_module_get_object_file(thinlto_code_gen_t cg,
                                           unsigned int index);

/**
 * Sets which PIC code model to generate.
 * Returns true on error (check lto_get_error_message() for details).
 *
 * \since LTO_API_VERSION=18
 */
extern lto_bool_t thinlto_codegen_set_pic_model(thinlto_code_gen_t cg,
                                                lto_codegen_model);

/**
 * Sets the path to a directory to use as a storage for temporary bitcode files.
 * The intention is to make the bitcode files available for debugging at various
 * stage of the pipeline.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_savetemps_dir(thinlto_code_gen_t cg,
                                              const char *save_temps_dir);

/**
 * Set the path to a directory where to save generated object files. This
 * path can be used by a linker to request on-disk files instead of in-memory
 * buffers. When set, results are available through
 * thinlto_module_get_object_file() instead of thinlto_module_get_object().
 *
 * \since LTO_API_VERSION=21
 */
void thinlto_set_generated_objects_dir(thinlto_code_gen_t cg,
                                       const char *save_temps_dir);

/**
 * Sets the cpu to generate code for.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_cpu(thinlto_code_gen_t cg, const char *cpu);

/**
 * Disable CodeGen, only run the stages till codegen and stop. The output will
 * be bitcode.
 *
 * \since LTO_API_VERSION=19
 */
extern void thinlto_codegen_disable_codegen(thinlto_code_gen_t cg,
                                            lto_bool_t disable);

/**
 * Perform CodeGen only: disable all other stages.
 *
 * \since LTO_API_VERSION=19
 */
extern void thinlto_codegen_set_codegen_only(thinlto_code_gen_t cg,
                                             lto_bool_t codegen_only);

/**
 * Parse -mllvm style debug options.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_debug_options(const char *const *options, int number);

/**
 * Test if a module has support for ThinLTO linking.
 *
 * \since LTO_API_VERSION=18
 */
extern lto_bool_t lto_module_is_thinlto(lto_module_t mod);

/**
 * Adds a symbol to the list of global symbols that must exist in the final
 * generated code. If a function is not listed there, it might be inlined into
 * every usage and optimized away. For every single module, the functions
 * referenced from code outside of the ThinLTO modules need to be added here.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_add_must_preserve_symbol(thinlto_code_gen_t cg,
                                                     const char *name,
                                                     int length);

/**
 * Adds a symbol to the list of global symbols that are cross-referenced between
 * ThinLTO files. If the ThinLTO CodeGenerator can ensure that every
 * references from a ThinLTO module to this symbol is optimized away, then
 * the symbol can be discarded.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_add_cross_referenced_symbol(thinlto_code_gen_t cg,
                                                        const char *name,
                                                        int length);

/**
 * @} // endgoup LLVMCTLTO
 * @defgroup LLVMCTLTO_CACHING ThinLTO Cache Control
 * @ingroup LLVMCTLTO
 *
 * These entry points control the ThinLTO cache. The cache is intended to
 * support incremental builds, and thus needs to be persistent across builds.
 * The client enables the cache by supplying a path to an existing directory.
 * The code generator will use this to store objects files that may be reused
 * during a subsequent build.
 * To avoid filling the disk space, a few knobs are provided:
 *  - The pruning interval limits the frequency at which the garbage collector
 *    will try to scan the cache directory to prune expired entries.
 *    Setting to a negative number disables the pruning.
 *  - The pruning expiration time indicates to the garbage collector how old an
 *    entry needs to be to be removed.
 *  - Finally, the garbage collector can be instructed to prune the cache until
 *    the occupied space goes below a threshold.
 * @{
 */

/**
 * Sets the path to a directory to use as a cache storage for incremental build.
 * Setting this activates caching.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_cache_dir(thinlto_code_gen_t cg,
                                          const char *cache_dir);

/**
 * Sets the cache pruning interval (in seconds). A negative value disables the
 * pruning. An unspecified default value will be applied, and a value of 0 will
 * force prunning to occur.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_cache_pruning_interval(thinlto_code_gen_t cg,
                                                       int interval);

/**
 * Sets the maximum cache size that can be persistent across build, in terms of
 * percentage of the available space on the disk. Set to 100 to indicate
 * no limit, 50 to indicate that the cache size will not be left over half the
 * available space. A value over 100 will be reduced to 100, a value of 0 will
 * be ignored. An unspecified default value will be applied.
 *
 * The formula looks like:
 *  AvailableSpace = FreeSpace + ExistingCacheSize
 *  NewCacheSize = AvailableSpace * P/100
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_final_cache_size_relative_to_available_space(
    thinlto_code_gen_t cg, unsigned percentage);

/**
 * Sets the expiration (in seconds) for an entry in the cache. An unspecified
 * default value will be applied. A value of 0 will be ignored.
 *
 * \since LTO_API_VERSION=18
 */
extern void thinlto_codegen_set_cache_entry_expiration(thinlto_code_gen_t cg,
                                                       unsigned expiration);

/**
 * Sets the maximum size of the cache directory (in bytes). A value over the
 * amount of available space on the disk will be reduced to the amount of
 * available space. An unspecified default value will be applied. A value of 0
 * will be ignored.
 *
 * \since LTO_API_VERSION=22
 */
extern void thinlto_codegen_set_cache_size_bytes(thinlto_code_gen_t cg,
                                                 unsigned max_size_bytes);

/**
 * Same as thinlto_codegen_set_cache_size_bytes, except the maximum size is in
 * megabytes (2^20 bytes).
 *
 * \since LTO_API_VERSION=23
 */
extern void
thinlto_codegen_set_cache_size_megabytes(thinlto_code_gen_t cg,
                                         unsigned max_size_megabytes);

/**
 * Sets the maximum number of files in the cache directory. An unspecified
 * default value will be applied. A value of 0 will be ignored.
 *
 * \since LTO_API_VERSION=22
 */
extern void thinlto_codegen_set_cache_size_files(thinlto_code_gen_t cg,
                                                 unsigned max_size_files);

/**
 * @} // endgroup LLVMCTLTO_CACHING
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_LTO_H */
