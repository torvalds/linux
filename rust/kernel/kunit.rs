// SPDX-License-Identifier: GPL-2.0

//! KUnit-based macros for Rust unit tests.
//!
//! C header: [`include/kunit/test.h`](srctree/include/kunit/test.h)
//!
//! Reference: <https://docs.kernel.org/dev-tools/kunit/index.html>

use crate::fmt;
use crate::prelude::*;

#[cfg(CONFIG_PRINTK)]
use crate::c_str;

/// Prints a KUnit error-level message.
///
/// Public but hidden since it should only be used from KUnit generated code.
#[doc(hidden)]
pub fn err(args: fmt::Arguments<'_>) {
    // SAFETY: The format string is null-terminated and the `%pA` specifier matches the argument we
    // are passing.
    #[cfg(CONFIG_PRINTK)]
    unsafe {
        bindings::_printk(
            c_str!("\x013%pA").as_char_ptr(),
            core::ptr::from_ref(&args).cast::<c_void>(),
        );
    }
}

/// Prints a KUnit info-level message.
///
/// Public but hidden since it should only be used from KUnit generated code.
#[doc(hidden)]
pub fn info(args: fmt::Arguments<'_>) {
    // SAFETY: The format string is null-terminated and the `%pA` specifier matches the argument we
    // are passing.
    #[cfg(CONFIG_PRINTK)]
    unsafe {
        bindings::_printk(
            c_str!("\x016%pA").as_char_ptr(),
            core::ptr::from_ref(&args).cast::<c_void>(),
        );
    }
}

/// Asserts that a boolean expression is `true` at runtime.
///
/// Public but hidden since it should only be used from generated tests.
///
/// Unlike the one in `core`, this one does not panic; instead, it is mapped to the KUnit
/// facilities. See [`assert!`] for more details.
#[doc(hidden)]
#[macro_export]
macro_rules! kunit_assert {
    ($name:literal, $file:literal, $diff:expr, $condition:expr $(,)?) => {
        'out: {
            // Do nothing if the condition is `true`.
            if $condition {
                break 'out;
            }

            static FILE: &'static $crate::str::CStr = $crate::c_str!($file);
            static LINE: i32 = ::core::line!() as i32 - $diff;
            static CONDITION: &'static $crate::str::CStr = $crate::c_str!(stringify!($condition));

            // SAFETY: FFI call without safety requirements.
            let kunit_test = unsafe { $crate::bindings::kunit_get_current_test() };
            if kunit_test.is_null() {
                // The assertion failed but this task is not running a KUnit test, so we cannot call
                // KUnit, but at least print an error to the kernel log. This may happen if this
                // macro is called from an spawned thread in a test (see
                // `scripts/rustdoc_test_gen.rs`) or if some non-test code calls this macro by
                // mistake (it is hidden to prevent that).
                //
                // This mimics KUnit's failed assertion format.
                $crate::kunit::err($crate::prelude::fmt!(
                    "    # {}: ASSERTION FAILED at {FILE}:{LINE}\n",
                    $name
                ));
                $crate::kunit::err($crate::prelude::fmt!(
                    "    Expected {CONDITION} to be true, but is false\n"
                ));
                $crate::kunit::err($crate::prelude::fmt!(
                    "    Failure not reported to KUnit since this is a non-KUnit task\n"
                ));
                break 'out;
            }

            #[repr(transparent)]
            struct Location($crate::bindings::kunit_loc);

            #[repr(transparent)]
            struct UnaryAssert($crate::bindings::kunit_unary_assert);

            // SAFETY: There is only a static instance and in that one the pointer field points to
            // an immutable C string.
            unsafe impl Sync for Location {}

            // SAFETY: There is only a static instance and in that one the pointer field points to
            // an immutable C string.
            unsafe impl Sync for UnaryAssert {}

            static LOCATION: Location = Location($crate::bindings::kunit_loc {
                file: $crate::str::as_char_ptr_in_const_context(FILE),
                line: LINE,
            });
            static ASSERTION: UnaryAssert = UnaryAssert($crate::bindings::kunit_unary_assert {
                assert: $crate::bindings::kunit_assert {},
                condition: $crate::str::as_char_ptr_in_const_context(CONDITION),
                expected_true: true,
            });

            // SAFETY:
            //   - FFI call.
            //   - The `kunit_test` pointer is valid because we got it from
            //     `kunit_get_current_test()` and it was not null. This means we are in a KUnit
            //     test, and that the pointer can be passed to KUnit functions and assertions.
            //   - The string pointers (`file` and `condition` above) point to null-terminated
            //     strings since they are `CStr`s.
            //   - The function pointer (`format`) points to the proper function.
            //   - The pointers passed will remain valid since they point to `static`s.
            //   - The format string is allowed to be null.
            //   - There are, however, problems with this: first of all, this will end up stopping
            //     the thread, without running destructors. While that is problematic in itself,
            //     it is considered UB to have what is effectively a forced foreign unwind
            //     with `extern "C"` ABI. One could observe the stack that is now gone from
            //     another thread. We should avoid pinning stack variables to prevent library UB,
            //     too. For the moment, given that test failures are reported immediately before the
            //     next test runs, that test failures should be fixed and that KUnit is explicitly
            //     documented as not suitable for production environments, we feel it is reasonable.
            unsafe {
                $crate::bindings::__kunit_do_failed_assertion(
                    kunit_test,
                    ::core::ptr::addr_of!(LOCATION.0),
                    $crate::bindings::kunit_assert_type_KUNIT_ASSERTION,
                    ::core::ptr::addr_of!(ASSERTION.0.assert),
                    Some($crate::bindings::kunit_unary_assert_format),
                    ::core::ptr::null(),
                );
            }

            // SAFETY: FFI call; the `test` pointer is valid because this hidden macro should only
            // be called by the generated documentation tests which forward the test pointer given
            // by KUnit.
            unsafe {
                $crate::bindings::__kunit_abort(kunit_test);
            }
        }
    };
}

/// Asserts that two expressions are equal to each other (using [`PartialEq`]).
///
/// Public but hidden since it should only be used from generated tests.
///
/// Unlike the one in `core`, this one does not panic; instead, it is mapped to the KUnit
/// facilities. See [`assert!`] for more details.
#[doc(hidden)]
#[macro_export]
macro_rules! kunit_assert_eq {
    ($name:literal, $file:literal, $diff:expr, $left:expr, $right:expr $(,)?) => {{
        // For the moment, we just forward to the expression assert because, for binary asserts,
        // KUnit supports only a few types (e.g. integers).
        $crate::kunit_assert!($name, $file, $diff, $left == $right);
    }};
}

trait TestResult {
    fn is_test_result_ok(&self) -> bool;
}

impl TestResult for () {
    fn is_test_result_ok(&self) -> bool {
        true
    }
}

impl<T, E> TestResult for Result<T, E> {
    fn is_test_result_ok(&self) -> bool {
        self.is_ok()
    }
}

/// Returns whether a test result is to be considered OK.
///
/// This will be `assert!`ed from the generated tests.
#[doc(hidden)]
#[expect(private_bounds)]
pub fn is_test_result_ok(t: impl TestResult) -> bool {
    t.is_test_result_ok()
}

/// Represents an individual test case.
///
/// The [`kunit_unsafe_test_suite!`] macro expects a NULL-terminated list of valid test cases.
/// Use [`kunit_case_null`] to generate such a delimiter.
#[doc(hidden)]
pub const fn kunit_case(
    name: &'static kernel::str::CStr,
    run_case: unsafe extern "C" fn(*mut kernel::bindings::kunit),
) -> kernel::bindings::kunit_case {
    kernel::bindings::kunit_case {
        run_case: Some(run_case),
        name: kernel::str::as_char_ptr_in_const_context(name),
        attr: kernel::bindings::kunit_attributes {
            speed: kernel::bindings::kunit_speed_KUNIT_SPEED_NORMAL,
        },
        generate_params: None,
        status: kernel::bindings::kunit_status_KUNIT_SUCCESS,
        module_name: core::ptr::null_mut(),
        log: core::ptr::null_mut(),
        param_init: None,
        param_exit: None,
    }
}

/// Represents the NULL test case delimiter.
///
/// The [`kunit_unsafe_test_suite!`] macro expects a NULL-terminated list of test cases. This
/// function returns such a delimiter.
#[doc(hidden)]
pub const fn kunit_case_null() -> kernel::bindings::kunit_case {
    kernel::bindings::kunit_case {
        run_case: None,
        name: core::ptr::null_mut(),
        generate_params: None,
        attr: kernel::bindings::kunit_attributes {
            speed: kernel::bindings::kunit_speed_KUNIT_SPEED_NORMAL,
        },
        status: kernel::bindings::kunit_status_KUNIT_SUCCESS,
        module_name: core::ptr::null_mut(),
        log: core::ptr::null_mut(),
        param_init: None,
        param_exit: None,
    }
}

/// Registers a KUnit test suite.
///
/// # Safety
///
/// `test_cases` must be a NULL terminated array of valid test cases,
/// whose lifetime is at least that of the test suite (i.e., static).
///
/// # Examples
///
/// ```ignore
/// extern "C" fn test_fn(_test: *mut kernel::bindings::kunit) {
///     let actual = 1 + 1;
///     let expected = 2;
///     assert_eq!(actual, expected);
/// }
///
/// static mut KUNIT_TEST_CASES: [kernel::bindings::kunit_case; 2] = [
///     kernel::kunit::kunit_case(kernel::c_str!("name"), test_fn),
///     kernel::kunit::kunit_case_null(),
/// ];
/// kernel::kunit_unsafe_test_suite!(suite_name, KUNIT_TEST_CASES);
/// ```
#[doc(hidden)]
#[macro_export]
macro_rules! kunit_unsafe_test_suite {
    ($name:ident, $test_cases:ident) => {
        const _: () = {
            const KUNIT_TEST_SUITE_NAME: [::kernel::ffi::c_char; 256] = {
                let name_u8 = ::core::stringify!($name).as_bytes();
                let mut ret = [0; 256];

                if name_u8.len() > 255 {
                    panic!(concat!(
                        "The test suite name `",
                        ::core::stringify!($name),
                        "` exceeds the maximum length of 255 bytes."
                    ));
                }

                let mut i = 0;
                while i < name_u8.len() {
                    ret[i] = name_u8[i] as ::kernel::ffi::c_char;
                    i += 1;
                }

                ret
            };

            static mut KUNIT_TEST_SUITE: ::kernel::bindings::kunit_suite =
                ::kernel::bindings::kunit_suite {
                    name: KUNIT_TEST_SUITE_NAME,
                    #[allow(unused_unsafe)]
                    // SAFETY: `$test_cases` is passed in by the user, and
                    // (as documented) must be valid for the lifetime of
                    // the suite (i.e., static).
                    test_cases: unsafe {
                        ::core::ptr::addr_of_mut!($test_cases)
                            .cast::<::kernel::bindings::kunit_case>()
                    },
                    suite_init: None,
                    suite_exit: None,
                    init: None,
                    exit: None,
                    attr: ::kernel::bindings::kunit_attributes {
                        speed: ::kernel::bindings::kunit_speed_KUNIT_SPEED_NORMAL,
                    },
                    status_comment: [0; 256usize],
                    debugfs: ::core::ptr::null_mut(),
                    log: ::core::ptr::null_mut(),
                    suite_init_err: 0,
                    is_init: false,
                };

            #[used(compiler)]
            #[allow(unused_unsafe)]
            #[cfg_attr(not(target_os = "macos"), link_section = ".kunit_test_suites")]
            static mut KUNIT_TEST_SUITE_ENTRY: *const ::kernel::bindings::kunit_suite =
                // SAFETY: `KUNIT_TEST_SUITE` is static.
                unsafe { ::core::ptr::addr_of_mut!(KUNIT_TEST_SUITE) };
        };
    };
}

/// Returns whether we are currently running a KUnit test.
///
/// In some cases, you need to call test-only code from outside the test case, for example, to
/// create a function mock. This function allows to change behavior depending on whether we are
/// currently running a KUnit test or not.
///
/// # Examples
///
/// This example shows how a function can be mocked to return a well-known value while testing:
///
/// ```
/// # use kernel::kunit::in_kunit_test;
/// fn fn_mock_example(n: i32) -> i32 {
///     if in_kunit_test() {
///         return 100;
///     }
///
///     n + 1
/// }
///
/// let mock_res = fn_mock_example(5);
/// assert_eq!(mock_res, 100);
/// ```
pub fn in_kunit_test() -> bool {
    // SAFETY: `kunit_get_current_test()` is always safe to call (it has fallbacks for
    // when KUnit is not enabled).
    !unsafe { bindings::kunit_get_current_test() }.is_null()
}

#[kunit_tests(rust_kernel_kunit)]
mod tests {
    use super::*;

    #[test]
    fn rust_test_kunit_example_test() {
        assert_eq!(1 + 1, 2);
    }

    #[test]
    fn rust_test_kunit_in_kunit_test() {
        assert!(in_kunit_test());
    }

    #[test]
    #[cfg(not(all()))]
    fn rust_test_kunit_always_disabled_test() {
        // This test should never run because of the `cfg`.
        assert!(false);
    }
}
