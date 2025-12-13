// SPDX-License-Identifier: GPL-2.0

//! Unified device property interface.
//!
//! C header: [`include/linux/property.h`](srctree/include/linux/property.h)

use core::{mem::MaybeUninit, ptr};

use super::private::Sealed;
use crate::{
    alloc::KVec,
    bindings,
    error::{to_result, Result},
    fmt,
    prelude::*,
    str::{CStr, CString},
    types::{ARef, Opaque},
};

/// A reference-counted fwnode_handle.
///
/// This structure represents the Rust abstraction for a
/// C `struct fwnode_handle`. This implementation abstracts the usage of an
/// already existing C `struct fwnode_handle` within Rust code that we get
/// passed from the C side.
///
/// # Invariants
///
/// A `FwNode` instance represents a valid `struct fwnode_handle` created by the
/// C portion of the kernel.
///
/// Instances of this type are always reference-counted, that is, a call to
/// `fwnode_handle_get` ensures that the allocation remains valid at least until
/// the matching call to `fwnode_handle_put`.
#[repr(transparent)]
pub struct FwNode(Opaque<bindings::fwnode_handle>);

impl FwNode {
    /// # Safety
    ///
    /// Callers must ensure that:
    /// - The reference count was incremented at least once.
    /// - They relinquish that increment. That is, if there is only one
    ///   increment, callers must not use the underlying object anymore -- it is
    ///   only safe to do so via the newly created `ARef<FwNode>`.
    unsafe fn from_raw(raw: *mut bindings::fwnode_handle) -> ARef<Self> {
        // SAFETY: As per the safety requirements of this function:
        // - `NonNull::new_unchecked`:
        //   - `raw` is not null.
        // - `ARef::from_raw`:
        //   - `raw` has an incremented refcount.
        //   - that increment is relinquished, i.e. it won't be decremented
        //     elsewhere.
        // CAST: It is safe to cast from a `*mut fwnode_handle` to
        // `*mut FwNode`, because `FwNode` is  defined as a
        // `#[repr(transparent)]` wrapper around `fwnode_handle`.
        unsafe { ARef::from_raw(ptr::NonNull::new_unchecked(raw.cast())) }
    }

    /// Obtain the raw `struct fwnode_handle *`.
    pub(crate) fn as_raw(&self) -> *mut bindings::fwnode_handle {
        self.0.get()
    }

    /// Returns `true` if `&self` is an OF node, `false` otherwise.
    pub fn is_of_node(&self) -> bool {
        // SAFETY: The type invariant of `Self` guarantees that `self.as_raw() is a pointer to a
        // valid `struct fwnode_handle`.
        unsafe { bindings::is_of_node(self.as_raw()) }
    }

    /// Returns an object that implements [`Display`](fmt::Display) for
    /// printing the name of a node.
    ///
    /// This is an alternative to the default `Display` implementation, which
    /// prints the full path.
    pub fn display_name(&self) -> impl fmt::Display + '_ {
        struct FwNodeDisplayName<'a>(&'a FwNode);

        impl fmt::Display for FwNodeDisplayName<'_> {
            fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                // SAFETY: `self` is valid by its type invariant.
                let name = unsafe { bindings::fwnode_get_name(self.0.as_raw()) };
                if name.is_null() {
                    return Ok(());
                }
                // SAFETY:
                // - `fwnode_get_name` returns null or a valid C string.
                // - `name` was checked to be non-null.
                let name = unsafe { CStr::from_char_ptr(name) };
                fmt::Display::fmt(name, f)
            }
        }

        FwNodeDisplayName(self)
    }

    /// Checks if property is present or not.
    pub fn property_present(&self, name: &CStr) -> bool {
        // SAFETY: By the invariant of `CStr`, `name` is null-terminated.
        unsafe { bindings::fwnode_property_present(self.as_raw().cast_const(), name.as_char_ptr()) }
    }

    /// Returns firmware property `name` boolean value.
    pub fn property_read_bool(&self, name: &CStr) -> bool {
        // SAFETY:
        // - `name` is non-null and null-terminated.
        // - `self.as_raw()` is valid because `self` is valid.
        unsafe { bindings::fwnode_property_read_bool(self.as_raw(), name.as_char_ptr()) }
    }

    /// Returns the index of matching string `match_str` for firmware string
    /// property `name`.
    pub fn property_match_string(&self, name: &CStr, match_str: &CStr) -> Result<usize> {
        // SAFETY:
        // - `name` and `match_str` are non-null and null-terminated.
        // - `self.as_raw` is valid because `self` is valid.
        let ret = unsafe {
            bindings::fwnode_property_match_string(
                self.as_raw(),
                name.as_char_ptr(),
                match_str.as_char_ptr(),
            )
        };
        to_result(ret)?;
        Ok(ret as usize)
    }

    /// Returns firmware property `name` integer array values in a [`KVec`].
    pub fn property_read_array_vec<'fwnode, 'name, T: PropertyInt>(
        &'fwnode self,
        name: &'name CStr,
        len: usize,
    ) -> Result<PropertyGuard<'fwnode, 'name, KVec<T>>> {
        let mut val: KVec<T> = KVec::with_capacity(len, GFP_KERNEL)?;

        let res = T::read_array_from_fwnode_property(self, name, val.spare_capacity_mut());
        let res = match res {
            Ok(_) => {
                // SAFETY:
                // - `len` is equal to `val.capacity - val.len`, because
                //   `val.capacity` is `len` and `val.len` is zero.
                // - All elements within the interval [`0`, `len`) were initialized
                //   by `read_array_from_fwnode_property`.
                unsafe { val.inc_len(len) }
                Ok(val)
            }
            Err(e) => Err(e),
        };
        Ok(PropertyGuard {
            inner: res,
            fwnode: self,
            name,
        })
    }

    /// Returns integer array length for firmware property `name`.
    pub fn property_count_elem<T: PropertyInt>(&self, name: &CStr) -> Result<usize> {
        T::read_array_len_from_fwnode_property(self, name)
    }

    /// Returns the value of firmware property `name`.
    ///
    /// This method is generic over the type of value to read. The types that
    /// can be read are strings, integers and arrays of integers.
    ///
    /// Reading a [`KVec`] of integers is done with the separate
    /// method [`Self::property_read_array_vec`], because it takes an
    /// additional `len` argument.
    ///
    /// Reading a boolean is done with the separate method
    /// [`Self::property_read_bool`], because this operation is infallible.
    ///
    /// For more precise documentation about what types can be read, see
    /// the [implementors of Property][Property#implementors] and [its
    /// implementations on foreign types][Property#foreign-impls].
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::{c_str, device::{Device, property::FwNode}, str::CString};
    /// fn examples(dev: &Device) -> Result {
    ///     let fwnode = dev.fwnode().ok_or(ENOENT)?;
    ///     let b: u32 = fwnode.property_read(c_str!("some-number")).required_by(dev)?;
    ///     if let Some(s) = fwnode.property_read::<CString>(c_str!("some-str")).optional() {
    ///         // ...
    ///     }
    ///     Ok(())
    /// }
    /// ```
    pub fn property_read<'fwnode, 'name, T: Property>(
        &'fwnode self,
        name: &'name CStr,
    ) -> PropertyGuard<'fwnode, 'name, T> {
        PropertyGuard {
            inner: T::read_from_fwnode_property(self, name),
            fwnode: self,
            name,
        }
    }

    /// Returns first matching named child node handle.
    pub fn get_child_by_name(&self, name: &CStr) -> Option<ARef<Self>> {
        // SAFETY: `self` and `name` are valid by their type invariants.
        let child =
            unsafe { bindings::fwnode_get_named_child_node(self.as_raw(), name.as_char_ptr()) };
        if child.is_null() {
            return None;
        }
        // SAFETY:
        // - `fwnode_get_named_child_node` returns a pointer with its refcount
        //   incremented.
        // - That increment is relinquished, i.e. the underlying object is not
        //   used anymore except via the newly created `ARef`.
        Some(unsafe { Self::from_raw(child) })
    }

    /// Returns an iterator over a node's children.
    pub fn children<'a>(&'a self) -> impl Iterator<Item = ARef<FwNode>> + 'a {
        let mut prev: Option<ARef<FwNode>> = None;

        core::iter::from_fn(move || {
            let prev_ptr = match prev.take() {
                None => ptr::null_mut(),
                Some(prev) => {
                    // We will pass `prev` to `fwnode_get_next_child_node`,
                    // which decrements its refcount, so we use
                    // `ARef::into_raw` to avoid decrementing the refcount
                    // twice.
                    let prev = ARef::into_raw(prev);
                    prev.as_ptr().cast()
                }
            };
            // SAFETY:
            // - `self.as_raw()` is valid by its type invariant.
            // - `prev_ptr` may be null, which is allowed and corresponds to
            //   getting the first child. Otherwise, `prev_ptr` is valid, as it
            //   is the stored return value from the previous invocation.
            // - `prev_ptr` has its refount incremented.
            // - The increment of `prev_ptr` is relinquished, i.e. the
            //   underlying object won't be used anymore.
            let next = unsafe { bindings::fwnode_get_next_child_node(self.as_raw(), prev_ptr) };
            if next.is_null() {
                return None;
            }
            // SAFETY:
            // - `next` is valid because `fwnode_get_next_child_node` returns a
            //   pointer with its refcount incremented.
            // - That increment is relinquished, i.e. the underlying object
            //   won't be used anymore, except via the newly created
            //   `ARef<Self>`.
            let next = unsafe { FwNode::from_raw(next) };
            prev = Some(next.clone());
            Some(next)
        })
    }

    /// Finds a reference with arguments.
    pub fn property_get_reference_args(
        &self,
        prop: &CStr,
        nargs: NArgs<'_>,
        index: u32,
    ) -> Result<FwNodeReferenceArgs> {
        let mut out_args = FwNodeReferenceArgs::default();

        let (nargs_prop, nargs) = match nargs {
            NArgs::Prop(nargs_prop) => (nargs_prop.as_char_ptr(), 0),
            NArgs::N(nargs) => (ptr::null(), nargs),
        };

        // SAFETY:
        // - `self.0.get()` is valid.
        // - `prop.as_char_ptr()` is valid and zero-terminated.
        // - `nargs_prop` is valid and zero-terminated if `nargs`
        //   is zero, otherwise it is allowed to be a null-pointer.
        // - The function upholds the type invariants of `out_args`,
        //   namely:
        //   - It may fill the field `fwnode` with a valid pointer,
        //     in which case its refcount is incremented.
        //   - It may modify the field `nargs`, in which case it
        //     initializes at least as many elements in `args`.
        let ret = unsafe {
            bindings::fwnode_property_get_reference_args(
                self.0.get(),
                prop.as_char_ptr(),
                nargs_prop,
                nargs,
                index,
                &mut out_args.0,
            )
        };
        to_result(ret)?;

        Ok(out_args)
    }
}

/// The number of arguments to request [`FwNodeReferenceArgs`].
pub enum NArgs<'a> {
    /// The name of the property of the reference indicating the number of
    /// arguments.
    Prop(&'a CStr),
    /// The known number of arguments.
    N(u32),
}

/// The return value of [`FwNode::property_get_reference_args`].
///
/// This structure represents the Rust abstraction for a C
/// `struct fwnode_reference_args` which was initialized by the C side.
///
/// # Invariants
///
/// If the field `fwnode` is valid, it owns an increment of its refcount.
///
/// The field `args` contains at least as many initialized elements as indicated
/// by the field `nargs`.
#[repr(transparent)]
#[derive(Default)]
pub struct FwNodeReferenceArgs(bindings::fwnode_reference_args);

impl Drop for FwNodeReferenceArgs {
    fn drop(&mut self) {
        if !self.0.fwnode.is_null() {
            // SAFETY:
            // - By the type invariants of `FwNodeReferenceArgs`, its field
            //   `fwnode` owns an increment of its refcount.
            // - That increment is relinquished. The underlying object won't be
            //   used anymore because we are dropping it.
            let _ = unsafe { FwNode::from_raw(self.0.fwnode) };
        }
    }
}

impl FwNodeReferenceArgs {
    /// Returns the slice of reference arguments.
    pub fn as_slice(&self) -> &[u64] {
        // SAFETY: As per the safety invariant of `FwNodeReferenceArgs`, `nargs`
        // is the minimum number of elements in `args` that is valid.
        unsafe { core::slice::from_raw_parts(self.0.args.as_ptr(), self.0.nargs as usize) }
    }

    /// Returns the number of reference arguments.
    pub fn len(&self) -> usize {
        self.0.nargs as usize
    }

    /// Returns `true` if there are no reference arguments.
    pub fn is_empty(&self) -> bool {
        self.0.nargs == 0
    }
}

impl fmt::Debug for FwNodeReferenceArgs {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", self.as_slice())
    }
}

// SAFETY: Instances of `FwNode` are always reference-counted.
unsafe impl crate::types::AlwaysRefCounted for FwNode {
    fn inc_ref(&self) {
        // SAFETY: The existence of a shared reference guarantees that the
        // refcount is non-zero.
        unsafe { bindings::fwnode_handle_get(self.as_raw()) };
    }

    unsafe fn dec_ref(obj: ptr::NonNull<Self>) {
        // SAFETY: The safety requirements guarantee that the refcount is
        // non-zero.
        unsafe { bindings::fwnode_handle_put(obj.cast().as_ptr()) }
    }
}

enum Node<'a> {
    Borrowed(&'a FwNode),
    Owned(ARef<FwNode>),
}

impl fmt::Display for FwNode {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // The logic here is the same as the one in lib/vsprintf.c
        // (fwnode_full_name_string).

        // SAFETY: `self.as_raw()` is valid by its type invariant.
        let num_parents = unsafe { bindings::fwnode_count_parents(self.as_raw()) };

        for depth in (0..=num_parents).rev() {
            let fwnode = if depth == 0 {
                Node::Borrowed(self)
            } else {
                // SAFETY: `self.as_raw()` is valid.
                let ptr = unsafe { bindings::fwnode_get_nth_parent(self.as_raw(), depth) };
                // SAFETY:
                // - The depth passed to `fwnode_get_nth_parent` is
                //   within the valid range, so the returned pointer is
                //   not null.
                // - The reference count was incremented by
                //   `fwnode_get_nth_parent`.
                // - That increment is relinquished to
                //   `FwNode::from_raw`.
                Node::Owned(unsafe { FwNode::from_raw(ptr) })
            };
            // Take a reference to the owned or borrowed `FwNode`.
            let fwnode: &FwNode = match &fwnode {
                Node::Borrowed(f) => f,
                Node::Owned(f) => f,
            };

            // SAFETY: `fwnode` is valid by its type invariant.
            let prefix = unsafe { bindings::fwnode_get_name_prefix(fwnode.as_raw()) };
            if !prefix.is_null() {
                // SAFETY: `fwnode_get_name_prefix` returns null or a
                // valid C string.
                let prefix = unsafe { CStr::from_char_ptr(prefix) };
                fmt::Display::fmt(prefix, f)?;
            }
            fmt::Display::fmt(&fwnode.display_name(), f)?;
        }

        Ok(())
    }
}

/// Implemented for types that can be read as properties.
///
/// This is implemented for strings, integers and arrays of integers. It's used
/// to make [`FwNode::property_read`] generic over the type of property being
/// read. There are also two dedicated methods to read other types, because they
/// require more specialized function signatures:
/// - [`property_read_bool`](FwNode::property_read_bool)
/// - [`property_read_array_vec`](FwNode::property_read_array_vec)
///
/// It must be public, because it appears in the signatures of other public
/// functions, but its methods shouldn't be used outside the kernel crate.
pub trait Property: Sized + Sealed {
    /// Used to make [`FwNode::property_read`] generic.
    fn read_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<Self>;
}

impl Sealed for CString {}

impl Property for CString {
    fn read_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<Self> {
        let mut str: *mut u8 = ptr::null_mut();
        let pstr: *mut _ = &mut str;

        // SAFETY:
        // - `name` is non-null and null-terminated.
        // - `fwnode.as_raw` is valid because `fwnode` is valid.
        let ret = unsafe {
            bindings::fwnode_property_read_string(fwnode.as_raw(), name.as_char_ptr(), pstr.cast())
        };
        to_result(ret)?;

        // SAFETY:
        // - `pstr` is a valid pointer to a NUL-terminated C string.
        // - It is valid for at least as long as `fwnode`, but it's only used
        //   within the current function.
        // - The memory it points to is not mutated during that time.
        let str = unsafe { CStr::from_char_ptr(*pstr) };
        Ok(str.try_into()?)
    }
}

/// Implemented for all integers that can be read as properties.
///
/// This helper trait is needed on top of the existing [`Property`]
/// trait to associate the integer types of various sizes with their
/// corresponding `fwnode_property_read_*_array` functions.
///
/// It must be public, because it appears in the signatures of other public
/// functions, but its methods shouldn't be used outside the kernel crate.
pub trait PropertyInt: Copy + Sealed {
    /// Reads a property array.
    fn read_array_from_fwnode_property<'a>(
        fwnode: &FwNode,
        name: &CStr,
        out: &'a mut [MaybeUninit<Self>],
    ) -> Result<&'a mut [Self]>;

    /// Reads the length of a property array.
    fn read_array_len_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<usize>;
}
// This macro generates implementations of the traits `Property` and
// `PropertyInt` for integers of various sizes. Its input is a list
// of pairs separated by commas. The first element of the pair is the
// type of the integer, the second one is the name of its corresponding
// `fwnode_property_read_*_array` function.
macro_rules! impl_property_for_int {
    ($($int:ty: $f:ident),* $(,)?) => { $(
        impl Sealed for $int {}
        impl<const N: usize> Sealed for [$int; N] {}

        impl PropertyInt for $int {
            fn read_array_from_fwnode_property<'a>(
                fwnode: &FwNode,
                name: &CStr,
                out: &'a mut [MaybeUninit<Self>],
            ) -> Result<&'a mut [Self]> {
                // SAFETY:
                // - `fwnode`, `name` and `out` are all valid by their type
                //   invariants.
                // - `out.len()` is a valid bound for the memory pointed to by
                //   `out.as_mut_ptr()`.
                // CAST: It's ok to cast from `*mut MaybeUninit<$int>` to a
                // `*mut $int` because they have the same memory layout.
                let ret = unsafe {
                    bindings::$f(
                        fwnode.as_raw(),
                        name.as_char_ptr(),
                        out.as_mut_ptr().cast(),
                        out.len(),
                    )
                };
                to_result(ret)?;
                // SAFETY: Transmuting from `&'a mut [MaybeUninit<Self>]` to
                // `&'a mut [Self]` is sound, because the previous call to a
                // `fwnode_property_read_*_array` function (which didn't fail)
                // fully initialized the slice.
                Ok(unsafe { core::mem::transmute::<&mut [MaybeUninit<Self>], &mut [Self]>(out) })
            }

            fn read_array_len_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<usize> {
                // SAFETY:
                // - `fwnode` and `name` are valid by their type invariants.
                // - It's ok to pass a null pointer to the
                //   `fwnode_property_read_*_array` functions if `nval` is zero.
                //   This will return the length of the array.
                let ret = unsafe {
                    bindings::$f(
                        fwnode.as_raw(),
                        name.as_char_ptr(),
                        ptr::null_mut(),
                        0,
                    )
                };
                to_result(ret)?;
                Ok(ret as usize)
            }
        }

        impl Property for $int {
            fn read_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<Self> {
                let val: [_; 1] = <[$int; 1]>::read_from_fwnode_property(fwnode, name)?;
                Ok(val[0])
            }
        }

        impl<const N: usize> Property for [$int; N] {
            fn read_from_fwnode_property(fwnode: &FwNode, name: &CStr) -> Result<Self> {
                let mut val: [MaybeUninit<$int>; N] = [const { MaybeUninit::uninit() }; N];

                <$int>::read_array_from_fwnode_property(fwnode, name, &mut val)?;

                // SAFETY: `val` is always initialized when
                // `fwnode_property_read_*_array` is successful.
                Ok(val.map(|v| unsafe { v.assume_init() }))
            }
        }
    )* };
}
impl_property_for_int! {
    u8: fwnode_property_read_u8_array,
    u16: fwnode_property_read_u16_array,
    u32: fwnode_property_read_u32_array,
    u64: fwnode_property_read_u64_array,
    i8: fwnode_property_read_u8_array,
    i16: fwnode_property_read_u16_array,
    i32: fwnode_property_read_u32_array,
    i64: fwnode_property_read_u64_array,
}

/// A helper for reading device properties.
///
/// Use [`Self::required_by`] if a missing property is considered a bug and
/// [`Self::optional`] otherwise.
///
/// For convenience, [`Self::or`] and [`Self::or_default`] are provided.
pub struct PropertyGuard<'fwnode, 'name, T> {
    /// The result of reading the property.
    inner: Result<T>,
    /// The fwnode of the property, used for logging in the "required" case.
    fwnode: &'fwnode FwNode,
    /// The name of the property, used for logging in the "required" case.
    name: &'name CStr,
}

impl<T> PropertyGuard<'_, '_, T> {
    /// Access the property, indicating it is required.
    ///
    /// If the property is not present, the error is automatically logged. If a
    /// missing property is not an error, use [`Self::optional`] instead. The
    /// device is required to associate the log with it.
    pub fn required_by(self, dev: &super::Device) -> Result<T> {
        if self.inner.is_err() {
            dev_err!(
                dev,
                "{}: property '{}' is missing\n",
                self.fwnode,
                self.name
            );
        }
        self.inner
    }

    /// Access the property, indicating it is optional.
    ///
    /// In contrast to [`Self::required_by`], no error message is logged if
    /// the property is not present.
    pub fn optional(self) -> Option<T> {
        self.inner.ok()
    }

    /// Access the property or the specified default value.
    ///
    /// Do not pass a sentinel value as default to detect a missing property.
    /// Use [`Self::required_by`] or [`Self::optional`] instead.
    pub fn or(self, default: T) -> T {
        self.inner.unwrap_or(default)
    }
}

impl<T: Default> PropertyGuard<'_, '_, T> {
    /// Access the property or a default value.
    ///
    /// Use [`Self::or`] to specify a custom default value.
    pub fn or_default(self) -> T {
        self.inner.unwrap_or_default()
    }
}
