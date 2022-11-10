// SPDX-License-Identifier: GPL-2.0

//! String representations.

use core::fmt::{self, Write};
use core::ops::{self, Deref, Index};

use crate::{
    bindings,
    error::{code::*, Error},
};

/// Byte string without UTF-8 validity guarantee.
///
/// `BStr` is simply an alias to `[u8]`, but has a more evident semantical meaning.
pub type BStr = [u8];

/// Creates a new [`BStr`] from a string literal.
///
/// `b_str!` converts the supplied string literal to byte string, so non-ASCII
/// characters can be included.
///
/// # Examples
///
/// ```
/// # use kernel::b_str;
/// # use kernel::str::BStr;
/// const MY_BSTR: &BStr = b_str!("My awesome BStr!");
/// ```
#[macro_export]
macro_rules! b_str {
    ($str:literal) => {{
        const S: &'static str = $str;
        const C: &'static $crate::str::BStr = S.as_bytes();
        C
    }};
}

/// Possible errors when using conversion functions in [`CStr`].
#[derive(Debug, Clone, Copy)]
pub enum CStrConvertError {
    /// Supplied bytes contain an interior `NUL`.
    InteriorNul,

    /// Supplied bytes are not terminated by `NUL`.
    NotNulTerminated,
}

impl From<CStrConvertError> for Error {
    #[inline]
    fn from(_: CStrConvertError) -> Error {
        EINVAL
    }
}

/// A string that is guaranteed to have exactly one `NUL` byte, which is at the
/// end.
///
/// Used for interoperability with kernel APIs that take C strings.
#[repr(transparent)]
pub struct CStr([u8]);

impl CStr {
    /// Returns the length of this string excluding `NUL`.
    #[inline]
    pub const fn len(&self) -> usize {
        self.len_with_nul() - 1
    }

    /// Returns the length of this string with `NUL`.
    #[inline]
    pub const fn len_with_nul(&self) -> usize {
        // SAFETY: This is one of the invariant of `CStr`.
        // We add a `unreachable_unchecked` here to hint the optimizer that
        // the value returned from this function is non-zero.
        if self.0.is_empty() {
            unsafe { core::hint::unreachable_unchecked() };
        }
        self.0.len()
    }

    /// Returns `true` if the string only includes `NUL`.
    #[inline]
    pub const fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// Wraps a raw C string pointer.
    ///
    /// # Safety
    ///
    /// `ptr` must be a valid pointer to a `NUL`-terminated C string, and it must
    /// last at least `'a`. When `CStr` is alive, the memory pointed by `ptr`
    /// must not be mutated.
    #[inline]
    pub unsafe fn from_char_ptr<'a>(ptr: *const core::ffi::c_char) -> &'a Self {
        // SAFETY: The safety precondition guarantees `ptr` is a valid pointer
        // to a `NUL`-terminated C string.
        let len = unsafe { bindings::strlen(ptr) } + 1;
        // SAFETY: Lifetime guaranteed by the safety precondition.
        let bytes = unsafe { core::slice::from_raw_parts(ptr as _, len as _) };
        // SAFETY: As `len` is returned by `strlen`, `bytes` does not contain interior `NUL`.
        // As we have added 1 to `len`, the last byte is known to be `NUL`.
        unsafe { Self::from_bytes_with_nul_unchecked(bytes) }
    }

    /// Creates a [`CStr`] from a `[u8]`.
    ///
    /// The provided slice must be `NUL`-terminated, does not contain any
    /// interior `NUL` bytes.
    pub const fn from_bytes_with_nul(bytes: &[u8]) -> Result<&Self, CStrConvertError> {
        if bytes.is_empty() {
            return Err(CStrConvertError::NotNulTerminated);
        }
        if bytes[bytes.len() - 1] != 0 {
            return Err(CStrConvertError::NotNulTerminated);
        }
        let mut i = 0;
        // `i + 1 < bytes.len()` allows LLVM to optimize away bounds checking,
        // while it couldn't optimize away bounds checks for `i < bytes.len() - 1`.
        while i + 1 < bytes.len() {
            if bytes[i] == 0 {
                return Err(CStrConvertError::InteriorNul);
            }
            i += 1;
        }
        // SAFETY: We just checked that all properties hold.
        Ok(unsafe { Self::from_bytes_with_nul_unchecked(bytes) })
    }

    /// Creates a [`CStr`] from a `[u8]` without performing any additional
    /// checks.
    ///
    /// # Safety
    ///
    /// `bytes` *must* end with a `NUL` byte, and should only have a single
    /// `NUL` byte (or the string will be truncated).
    #[inline]
    pub const unsafe fn from_bytes_with_nul_unchecked(bytes: &[u8]) -> &CStr {
        // SAFETY: Properties of `bytes` guaranteed by the safety precondition.
        unsafe { core::mem::transmute(bytes) }
    }

    /// Returns a C pointer to the string.
    #[inline]
    pub const fn as_char_ptr(&self) -> *const core::ffi::c_char {
        self.0.as_ptr() as _
    }

    /// Convert the string to a byte slice without the trailing 0 byte.
    #[inline]
    pub fn as_bytes(&self) -> &[u8] {
        &self.0[..self.len()]
    }

    /// Convert the string to a byte slice containing the trailing 0 byte.
    #[inline]
    pub const fn as_bytes_with_nul(&self) -> &[u8] {
        &self.0
    }

    /// Yields a [`&str`] slice if the [`CStr`] contains valid UTF-8.
    ///
    /// If the contents of the [`CStr`] are valid UTF-8 data, this
    /// function will return the corresponding [`&str`] slice. Otherwise,
    /// it will return an error with details of where UTF-8 validation failed.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::str::CStr;
    /// let cstr = CStr::from_bytes_with_nul(b"foo\0").unwrap();
    /// assert_eq!(cstr.to_str(), Ok("foo"));
    /// ```
    #[inline]
    pub fn to_str(&self) -> Result<&str, core::str::Utf8Error> {
        core::str::from_utf8(self.as_bytes())
    }

    /// Unsafely convert this [`CStr`] into a [`&str`], without checking for
    /// valid UTF-8.
    ///
    /// # Safety
    ///
    /// The contents must be valid UTF-8.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::str::CStr;
    /// // SAFETY: String literals are guaranteed to be valid UTF-8
    /// // by the Rust compiler.
    /// let bar = c_str!("ツ");
    /// assert_eq!(unsafe { bar.as_str_unchecked() }, "ツ");
    /// ```
    #[inline]
    pub unsafe fn as_str_unchecked(&self) -> &str {
        unsafe { core::str::from_utf8_unchecked(self.as_bytes()) }
    }
}

impl fmt::Display for CStr {
    /// Formats printable ASCII characters, escaping the rest.
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::str::CStr;
    /// # use kernel::str::CString;
    /// let penguin = c_str!("🐧");
    /// let s = CString::try_from_fmt(fmt!("{}", penguin)).unwrap();
    /// assert_eq!(s.as_bytes_with_nul(), "\\xf0\\x9f\\x90\\xa7\0".as_bytes());
    ///
    /// let ascii = c_str!("so \"cool\"");
    /// let s = CString::try_from_fmt(fmt!("{}", ascii)).unwrap();
    /// assert_eq!(s.as_bytes_with_nul(), "so \"cool\"\0".as_bytes());
    /// ```
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        for &c in self.as_bytes() {
            if (0x20..0x7f).contains(&c) {
                // Printable character.
                f.write_char(c as char)?;
            } else {
                write!(f, "\\x{:02x}", c)?;
            }
        }
        Ok(())
    }
}

impl fmt::Debug for CStr {
    /// Formats printable ASCII characters with a double quote on either end, escaping the rest.
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::str::CStr;
    /// # use kernel::str::CString;
    /// let penguin = c_str!("🐧");
    /// let s = CString::try_from_fmt(fmt!("{:?}", penguin)).unwrap();
    /// assert_eq!(s.as_bytes_with_nul(), "\"\\xf0\\x9f\\x90\\xa7\"\0".as_bytes());
    ///
    /// // Embedded double quotes are escaped.
    /// let ascii = c_str!("so \"cool\"");
    /// let s = CString::try_from_fmt(fmt!("{:?}", ascii)).unwrap();
    /// assert_eq!(s.as_bytes_with_nul(), "\"so \\\"cool\\\"\"\0".as_bytes());
    /// ```
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("\"")?;
        for &c in self.as_bytes() {
            match c {
                // Printable characters.
                b'\"' => f.write_str("\\\"")?,
                0x20..=0x7e => f.write_char(c as char)?,
                _ => write!(f, "\\x{:02x}", c)?,
            }
        }
        f.write_str("\"")
    }
}

impl AsRef<BStr> for CStr {
    #[inline]
    fn as_ref(&self) -> &BStr {
        self.as_bytes()
    }
}

impl Deref for CStr {
    type Target = BStr;

    #[inline]
    fn deref(&self) -> &Self::Target {
        self.as_bytes()
    }
}

impl Index<ops::RangeFrom<usize>> for CStr {
    type Output = CStr;

    #[inline]
    fn index(&self, index: ops::RangeFrom<usize>) -> &Self::Output {
        // Delegate bounds checking to slice.
        // Assign to _ to mute clippy's unnecessary operation warning.
        let _ = &self.as_bytes()[index.start..];
        // SAFETY: We just checked the bounds.
        unsafe { Self::from_bytes_with_nul_unchecked(&self.0[index.start..]) }
    }
}

impl Index<ops::RangeFull> for CStr {
    type Output = CStr;

    #[inline]
    fn index(&self, _index: ops::RangeFull) -> &Self::Output {
        self
    }
}

mod private {
    use core::ops;

    // Marker trait for index types that can be forward to `BStr`.
    pub trait CStrIndex {}

    impl CStrIndex for usize {}
    impl CStrIndex for ops::Range<usize> {}
    impl CStrIndex for ops::RangeInclusive<usize> {}
    impl CStrIndex for ops::RangeToInclusive<usize> {}
}

impl<Idx> Index<Idx> for CStr
where
    Idx: private::CStrIndex,
    BStr: Index<Idx>,
{
    type Output = <BStr as Index<Idx>>::Output;

    #[inline]
    fn index(&self, index: Idx) -> &Self::Output {
        &self.as_bytes()[index]
    }
}

/// Allows formatting of [`fmt::Arguments`] into a raw buffer.
///
/// It does not fail if callers write past the end of the buffer so that they can calculate the
/// size required to fit everything.
///
/// # Invariants
///
/// The memory region between `pos` (inclusive) and `end` (exclusive) is valid for writes if `pos`
/// is less than `end`.
pub(crate) struct RawFormatter {
    // Use `usize` to use `saturating_*` functions.
    #[allow(dead_code)]
    beg: usize,
    pos: usize,
    end: usize,
}

impl RawFormatter {
    /// Creates a new instance of [`RawFormatter`] with the given buffer pointers.
    ///
    /// # Safety
    ///
    /// If `pos` is less than `end`, then the region between `pos` (inclusive) and `end`
    /// (exclusive) must be valid for writes for the lifetime of the returned [`RawFormatter`].
    pub(crate) unsafe fn from_ptrs(pos: *mut u8, end: *mut u8) -> Self {
        // INVARIANT: The safety requierments guarantee the type invariants.
        Self {
            beg: pos as _,
            pos: pos as _,
            end: end as _,
        }
    }

    /// Returns the current insert position.
    ///
    /// N.B. It may point to invalid memory.
    pub(crate) fn pos(&self) -> *mut u8 {
        self.pos as _
    }
}

impl fmt::Write for RawFormatter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        // `pos` value after writing `len` bytes. This does not have to be bounded by `end`, but we
        // don't want it to wrap around to 0.
        let pos_new = self.pos.saturating_add(s.len());

        // Amount that we can copy. `saturating_sub` ensures we get 0 if `pos` goes past `end`.
        let len_to_copy = core::cmp::min(pos_new, self.end).saturating_sub(self.pos);

        if len_to_copy > 0 {
            // SAFETY: If `len_to_copy` is non-zero, then we know `pos` has not gone past `end`
            // yet, so it is valid for write per the type invariants.
            unsafe {
                core::ptr::copy_nonoverlapping(
                    s.as_bytes().as_ptr(),
                    self.pos as *mut u8,
                    len_to_copy,
                )
            };
        }

        self.pos = pos_new;
        Ok(())
    }
}
