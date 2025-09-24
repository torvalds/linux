// SPDX-License-Identifier: GPL-2.0

//! Support for module parameters.
//!
//! C header: [`include/linux/moduleparam.h`](srctree/include/linux/moduleparam.h)

use crate::prelude::*;
use crate::str::BStr;
use bindings;
use kernel::sync::SetOnce;

/// Newtype to make `bindings::kernel_param` [`Sync`].
#[repr(transparent)]
#[doc(hidden)]
pub struct KernelParam(bindings::kernel_param);

impl KernelParam {
    #[doc(hidden)]
    pub const fn new(val: bindings::kernel_param) -> Self {
        Self(val)
    }
}

// SAFETY: C kernel handles serializing access to this type. We never access it
// from Rust module.
unsafe impl Sync for KernelParam {}

/// Types that can be used for module parameters.
// NOTE: This trait is `Copy` because drop could produce unsoundness during teardown.
pub trait ModuleParam: Sized + Copy {
    /// Parse a parameter argument into the parameter value.
    fn try_from_param_arg(arg: &BStr) -> Result<Self>;
}

/// Set the module parameter from a string.
///
/// Used to set the parameter value at kernel initialization, when loading
/// the module or when set through `sysfs`.
///
/// See `struct kernel_param_ops.set`.
///
/// # Safety
///
/// - If `val` is non-null then it must point to a valid null-terminated string that must be valid
///   for reads for the duration of the call.
/// - `param` must be a pointer to a `bindings::kernel_param` initialized by the rust module macro.
///   The pointee must be valid for reads for the duration of the call.
///
/// # Note
///
/// - The safety requirements are satisfied by C API contract when this function is invoked by the
///   module subsystem C code.
/// - Currently, we only support read-only parameters that are not readable from `sysfs`. Thus, this
///   function is only called at kernel initialization time, or at module load time, and we have
///   exclusive access to the parameter for the duration of the function.
///
/// [`module!`]: macros::module
unsafe extern "C" fn set_param<T>(val: *const c_char, param: *const bindings::kernel_param) -> c_int
where
    T: ModuleParam,
{
    // NOTE: If we start supporting arguments without values, val _is_ allowed
    // to be null here.
    if val.is_null() {
        // TODO: Use pr_warn_once available.
        crate::pr_warn!("Null pointer passed to `module_param::set_param`");
        return EINVAL.to_errno();
    }

    // SAFETY: By function safety requirement, val is non-null, null-terminated
    // and valid for reads for the duration of this function.
    let arg = unsafe { CStr::from_char_ptr(val) };

    crate::error::from_result(|| {
        let new_value = T::try_from_param_arg(arg)?;

        // SAFETY: By function safety requirements, this access is safe.
        let container = unsafe { &*((*param).__bindgen_anon_1.arg.cast::<SetOnce<T>>()) };

        container
            .populate(new_value)
            .then_some(0)
            .ok_or(kernel::error::code::EEXIST)
    })
}

macro_rules! impl_int_module_param {
    ($ty:ident) => {
        impl ModuleParam for $ty {
            fn try_from_param_arg(arg: &BStr) -> Result<Self> {
                <$ty as crate::str::parse_int::ParseInt>::from_str(arg)
            }
        }
    };
}

impl_int_module_param!(i8);
impl_int_module_param!(u8);
impl_int_module_param!(i16);
impl_int_module_param!(u16);
impl_int_module_param!(i32);
impl_int_module_param!(u32);
impl_int_module_param!(i64);
impl_int_module_param!(u64);
impl_int_module_param!(isize);
impl_int_module_param!(usize);

/// A wrapper for kernel parameters.
///
/// This type is instantiated by the [`module!`] macro when module parameters are
/// defined. You should never need to instantiate this type directly.
///
/// Note: This type is `pub` because it is used by module crates to access
/// parameter values.
pub struct ModuleParamAccess<T> {
    value: SetOnce<T>,
    default: T,
}

// SAFETY: We only create shared references to the contents of this container,
// so if `T` is `Sync`, so is `ModuleParamAccess`.
unsafe impl<T: Sync> Sync for ModuleParamAccess<T> {}

impl<T> ModuleParamAccess<T> {
    #[doc(hidden)]
    pub const fn new(default: T) -> Self {
        Self {
            value: SetOnce::new(),
            default,
        }
    }

    /// Get a shared reference to the parameter value.
    // Note: When sysfs access to parameters are enabled, we have to pass in a
    // held lock guard here.
    pub fn value(&self) -> &T {
        self.value.as_ref().unwrap_or(&self.default)
    }

    /// Get a mutable pointer to `self`.
    ///
    /// NOTE: In most cases it is not safe deref the returned pointer.
    pub const fn as_void_ptr(&self) -> *mut c_void {
        core::ptr::from_ref(self).cast_mut().cast()
    }
}

#[doc(hidden)]
/// Generate a static [`kernel_param_ops`](srctree/include/linux/moduleparam.h) struct.
///
/// # Examples
///
/// ```ignore
/// make_param_ops!(
///     /// Documentation for new param ops.
///     PARAM_OPS_MYTYPE, // Name for the static.
///     MyType // A type which implements [`ModuleParam`].
/// );
/// ```
macro_rules! make_param_ops {
    ($ops:ident, $ty:ty) => {
        #[doc(hidden)]
        pub static $ops: $crate::bindings::kernel_param_ops = $crate::bindings::kernel_param_ops {
            flags: 0,
            set: Some(set_param::<$ty>),
            get: None,
            free: None,
        };
    };
}

make_param_ops!(PARAM_OPS_I8, i8);
make_param_ops!(PARAM_OPS_U8, u8);
make_param_ops!(PARAM_OPS_I16, i16);
make_param_ops!(PARAM_OPS_U16, u16);
make_param_ops!(PARAM_OPS_I32, i32);
make_param_ops!(PARAM_OPS_U32, u32);
make_param_ops!(PARAM_OPS_I64, i64);
make_param_ops!(PARAM_OPS_U64, u64);
make_param_ops!(PARAM_OPS_ISIZE, isize);
make_param_ops!(PARAM_OPS_USIZE, usize);
