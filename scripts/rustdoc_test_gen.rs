// SPDX-License-Identifier: GPL-2.0

//! Generates KUnit tests from saved `rustdoc`-generated tests.
//!
//! KUnit passes a context (`struct kunit *`) to each test, which should be forwarded to the other
//! KUnit functions and macros.
//!
//! However, we want to keep this as an implementation detail because:
//!
//!   - Test code should not care about the implementation.
//!
//!   - Documentation looks worse if it needs to carry extra details unrelated to the piece
//!     being described.
//!
//!   - Test code should be able to define functions and call them, without having to carry
//!     the context.
//!
//!   - Later on, we may want to be able to test non-kernel code (e.g. `core` or third-party
//!     crates) which likely use the standard library `assert*!` macros.
//!
//! For this reason, instead of the passed context, `kunit_get_current_test()` is used instead
//! (i.e. `current->kunit_test`).
//!
//! Note that this means other threads/tasks potentially spawned by a given test, if failing, will
//! report the failure in the kernel log but will not fail the actual test. Saving the pointer in
//! e.g. a `static` per test does not fully solve the issue either, because currently KUnit does
//! not support assertions (only expectations) from other tasks. Thus leave that feature for
//! the future, which simplifies the code here too. We could also simply not allow `assert`s in
//! other tasks, but that seems overly constraining, and we do want to support them, eventually.

use std::{
    fs,
    fs::File,
    io::{
        BufWriter,
        Read,
        Write, //
    },
    path::{
        Path,
        PathBuf, //
    }, //
};

/// Find the real path to the original file based on the `file` portion of the test name.
///
/// `rustdoc` generated `file`s look like `sync_locked_by_rs`. Underscores (except the last one)
/// may represent an actual underscore in a directory/file, or a path separator. Thus the actual
/// file might be `sync_locked_by.rs`, `sync/locked_by.rs`, `sync_locked/by.rs` or
/// `sync/locked/by.rs`. This function walks the file system to determine which is the real one.
///
/// This does require that ambiguities do not exist, but that seems fair, especially since this is
/// all supposed to be temporary until `rustdoc` gives us proper metadata to build this. If such
/// ambiguities are detected, they are diagnosed and the script panics.
fn find_real_path<'a>(srctree: &Path, valid_paths: &'a mut Vec<PathBuf>, file: &str) -> &'a str {
    valid_paths.clear();

    let potential_components: Vec<&str> = file.strip_suffix("_rs").unwrap().split('_').collect();

    find_candidates(srctree, valid_paths, Path::new(""), &potential_components);
    fn find_candidates(
        srctree: &Path,
        valid_paths: &mut Vec<PathBuf>,
        prefix: &Path,
        potential_components: &[&str],
    ) {
        // The base case: check whether all the potential components left, joined by underscores,
        // is a file.
        let joined_potential_components = potential_components.join("_") + ".rs";
        if srctree
            .join("rust/kernel")
            .join(prefix)
            .join(&joined_potential_components)
            .is_file()
        {
            // Avoid `srctree` here in order to keep paths relative to it in the KTAP output.
            valid_paths.push(
                Path::new("rust/kernel")
                    .join(prefix)
                    .join(joined_potential_components),
            );
        }

        // In addition, check whether each component prefix, joined by underscores, is a directory.
        // If not, there is no need to check for combinations with that prefix.
        for i in 1..potential_components.len() {
            let (components_prefix, components_rest) = potential_components.split_at(i);
            let prefix = prefix.join(components_prefix.join("_"));
            if srctree.join("rust/kernel").join(&prefix).is_dir() {
                find_candidates(srctree, valid_paths, &prefix, components_rest);
            }
        }
    }

    match valid_paths.as_slice() {
        [] => panic!(
            "No path candidates found for `{file}`. This is likely a bug in the build system, or \
            some files went away while compiling."
        ),
        [valid_path] => valid_path.to_str().unwrap(),
        valid_paths => {
            use std::fmt::Write;

            let mut candidates = String::new();
            for path in valid_paths {
                writeln!(&mut candidates, "    {path:?}").unwrap();
            }
            panic!(
                "Several path candidates found for `{file}`, please resolve the ambiguity by \
                renaming a file or folder. Candidates:\n{candidates}",
            );
        }
    }
}

fn main() {
    let srctree = std::env::var("srctree").unwrap();
    let srctree = Path::new(&srctree);

    let mut paths = fs::read_dir("rust/test/doctests/kernel")
        .unwrap()
        .map(|entry| entry.unwrap().path())
        .collect::<Vec<_>>();

    // Sort paths.
    paths.sort();

    let mut tests = String::new();
    let mut body = String::new();
    let mut last_file = String::new();
    let mut number = 0;
    let mut valid_paths: Vec<PathBuf> = Vec::new();
    let mut real_path: &str = "";
    for path in paths {
        // The `name` follows the `{file}_{line}_{number}` pattern (see description in
        // `scripts/rustdoc_test_builder.rs`). Discard the `number`.
        let name = path.file_name().unwrap().to_str().unwrap().to_string();

        // Extract the `file` and the `line`, discarding the `number`.
        let (file, line) = name.rsplit_once('_').unwrap().0.rsplit_once('_').unwrap();

        // Generate an ID sequence ("test number") for each one in the file.
        if file == last_file {
            number += 1;
        } else {
            number = 0;
            last_file = file.to_string();

            // Figure out the real path, only once per file.
            real_path = find_real_path(srctree, &mut valid_paths, file);
        }

        // Generate a KUnit name (i.e. test name and C symbol) for this test.
        //
        // We avoid the line number, like `rustdoc` does, to make things slightly more stable for
        // bisection purposes. However, to aid developers in mapping back what test failed, we will
        // print a diagnostics line in the KTAP report.
        let kunit_name = format!("rust_doctest_kernel_{file}_{number}");

        // Read the test's text contents to dump it below.
        body.clear();
        File::open(path).unwrap().read_to_string(&mut body).unwrap();

        // Calculate how many lines before `main` function (including the `main` function line).
        let body_offset = body
            .lines()
            .take_while(|line| !line.contains("fn main() {"))
            .count()
            + 1;

        use std::fmt::Write;
        write!(
            tests,
            r#"/// Generated `{name}` KUnit test case from a Rust documentation test.
#[test]
fn {kunit_name}() {{
    /// Overrides the usual [`file!`] macro with one that expands to the real path.
    #[allow(unused)]
    macro_rules! file {{
        () => {{ "{real_path}" }}
    }}

    /// Overrides the usual [`line!`] macro with one that expands to the real line number.
    #[allow(unused)]
    macro_rules! line {{
        // NOTE: This does not expand to a literal, but a constant expression.
        // Therefore code that depends on `line!()` expanding to a literal needs special adjustment.
        () => {{ const {{ ::core::line!() - __DOCTEST_ANCHOR + {line} }} }}
    }}

    // Many tests need the prelude, so provide it by default.
    #[allow(unused)]
    use ::kernel::prelude::*;

    // Unconditionally print the location of the original doctest (i.e. rather than the location in
    // the generated file) so that developers can easily map the test back to the source code.
    //
    // This information is also printed when assertions fail, but this helps in the successful cases
    // when the user is running KUnit manually, or when passing `--raw_output` to `kunit.py`.
    //
    // This follows the syntax for declaring test metadata in the proposed KTAP v2 spec, which may
    // be used for the proposed KUnit test attributes API. Thus hopefully this will make migration
    // easier later on.
    ::kernel::kunit::info(fmt!("    # {kunit_name}.location: {real_path}:{line}\n"));

    /// The anchor where the test code body starts.
    #[allow(unused)]
    static __DOCTEST_ANCHOR: u32 = ::core::line!() + {body_offset} + 2;
    {{
        #![allow(unreachable_pub, clippy::disallowed_names)]
        {body}
        main();
    }}
}}

"#
        )
        .unwrap();
    }

    let tests = tests.trim();

    write!(
        BufWriter::new(File::create("rust/doctests_kernel_generated.rs").unwrap()),
        r#"//! `kernel` crate documentation tests.

const __LOG_PREFIX: &[u8] = b"rust_doctests_kernel\0";

/// Dummy module type for doctest context.
struct LocalModule;

use kernel::{{
    str::CStr,
    ModuleMetadata,
    ThisModule, //
}};
use core::ptr::null_mut;

impl ModuleMetadata for LocalModule {{
    const NAME: &'static CStr = c"rust_doctests_kernel";
    const THIS_MODULE: ThisModule = {{
        // SAFETY: `try_module_get`/`module_put` handle null module pointers gracefully.
        unsafe {{ ThisModule::from_ptr(null_mut()) }}
    }};
}}

#[kernel::macros::kunit_tests(rust_doctests_kernel)]
mod tests {{
{tests}
}}
"#
    )
    .unwrap();
}
