// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Google LLC.

//! DebugFS Abstraction
//!
//! C header: [`include/linux/debugfs.h`](srctree/include/linux/debugfs.h)

// When DebugFS is disabled, many parameters are dead. Linting for this isn't helpful.
#![cfg_attr(not(CONFIG_DEBUG_FS), allow(unused_variables))]

use crate::prelude::*;
use crate::str::CStr;
#[cfg(CONFIG_DEBUG_FS)]
use crate::sync::Arc;
use crate::uaccess::UserSliceReader;
use core::fmt;
use core::marker::PhantomData;
use core::marker::PhantomPinned;
#[cfg(CONFIG_DEBUG_FS)]
use core::mem::ManuallyDrop;
use core::ops::Deref;

mod traits;
pub use traits::{Reader, Writer};

mod callback_adapters;
use callback_adapters::{FormatAdapter, NoWriter, WritableAdapter};
mod file_ops;
use file_ops::{FileOps, ReadFile, ReadWriteFile, WriteFile};
#[cfg(CONFIG_DEBUG_FS)]
mod entry;
#[cfg(CONFIG_DEBUG_FS)]
use entry::Entry;

/// Owning handle to a DebugFS directory.
///
/// The directory in the filesystem represented by [`Dir`] will be removed when handle has been
/// dropped *and* all children have been removed.
// If we have a parent, we hold a reference to it in the `Entry`. This prevents the `dentry`
// we point to from being cleaned up if our parent `Dir`/`Entry` is dropped before us.
//
// The `None` option indicates that the `Arc` could not be allocated, so our children would not be
// able to refer to us. In this case, we need to silently fail. All future child directories/files
// will silently fail as well.
#[derive(Clone)]
pub struct Dir(#[cfg(CONFIG_DEBUG_FS)] Option<Arc<Entry<'static>>>);

impl Dir {
    /// Create a new directory in DebugFS. If `parent` is [`None`], it will be created at the root.
    fn create(name: &CStr, parent: Option<&Dir>) -> Self {
        #[cfg(CONFIG_DEBUG_FS)]
        {
            let parent_entry = match parent {
                // If the parent couldn't be allocated, just early-return
                Some(Dir(None)) => return Self(None),
                Some(Dir(Some(entry))) => Some(entry.clone()),
                None => None,
            };
            Self(
                // If Arc creation fails, the `Entry` will be dropped, so the directory will be
                // cleaned up.
                Arc::new(Entry::dynamic_dir(name, parent_entry), GFP_KERNEL).ok(),
            )
        }
        #[cfg(not(CONFIG_DEBUG_FS))]
        Self()
    }

    /// Creates a DebugFS file which will own the data produced by the initializer provided in
    /// `data`.
    fn create_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
        file_ops: &'static FileOps<T>,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Sync + 'static,
    {
        let scope = Scope::<T>::new(data, move |data| {
            #[cfg(CONFIG_DEBUG_FS)]
            if let Some(parent) = &self.0 {
                // SAFETY: Because data derives from a scope, and our entry will be dropped before
                // the data is dropped, it is guaranteed to outlive the entry we return.
                unsafe { Entry::dynamic_file(name, parent.clone(), data, file_ops) }
            } else {
                Entry::empty()
            }
        });
        try_pin_init! {
            File {
                scope <- scope
            } ? E
        }
    }

    /// Create a new directory in DebugFS at the root.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// let debugfs = Dir::new(c_str!("parent"));
    /// ```
    pub fn new(name: &CStr) -> Self {
        Dir::create(name, None)
    }

    /// Creates a subdirectory within this directory.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// let parent = Dir::new(c_str!("parent"));
    /// let child = parent.subdir(c_str!("child"));
    /// ```
    pub fn subdir(&self, name: &CStr) -> Self {
        Dir::create(name, Some(self))
    }

    /// Creates a read-only file in this directory.
    ///
    /// The file's contents are produced by invoking [`Writer::write`] on the value initialized by
    /// `data`.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// # use kernel::prelude::*;
    /// # let dir = Dir::new(c_str!("my_debugfs_dir"));
    /// let file = KBox::pin_init(dir.read_only_file(c_str!("foo"), 200), GFP_KERNEL)?;
    /// // "my_debugfs_dir/foo" now contains the number 200.
    /// // The file is removed when `file` is dropped.
    /// # Ok::<(), Error>(())
    /// ```
    pub fn read_only_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Writer + Send + Sync + 'static,
    {
        let file_ops = &<T as ReadFile<_>>::FILE_OPS;
        self.create_file(name, data, file_ops)
    }

    /// Creates a read-only file in this directory, with contents from a callback.
    ///
    /// `f` must be a function item or a non-capturing closure.
    /// This is statically asserted and not a safety requirement.
    ///
    /// # Examples
    ///
    /// ```
    /// # use core::sync::atomic::{AtomicU32, Ordering};
    /// # use kernel::c_str;
    /// # use kernel::debugfs::Dir;
    /// # use kernel::prelude::*;
    /// # let dir = Dir::new(c_str!("foo"));
    /// let file = KBox::pin_init(
    ///     dir.read_callback_file(c_str!("bar"),
    ///     AtomicU32::new(3),
    ///     &|val, f| {
    ///       let out = val.load(Ordering::Relaxed);
    ///       writeln!(f, "{out:#010x}")
    ///     }),
    ///     GFP_KERNEL)?;
    /// // Reading "foo/bar" will show "0x00000003".
    /// file.store(10, Ordering::Relaxed);
    /// // Reading "foo/bar" will now show "0x0000000a".
    /// # Ok::<(), Error>(())
    /// ```
    pub fn read_callback_file<'a, T, E: 'a, F>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
        _f: &'static F,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Send + Sync + 'static,
        F: Fn(&T, &mut fmt::Formatter<'_>) -> fmt::Result + Send + Sync,
    {
        let file_ops = <FormatAdapter<T, F>>::FILE_OPS.adapt();
        self.create_file(name, data, file_ops)
    }

    /// Creates a read-write file in this directory.
    ///
    /// Reading the file uses the [`Writer`] implementation.
    /// Writing to the file uses the [`Reader`] implementation.
    pub fn read_write_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Writer + Reader + Send + Sync + 'static,
    {
        let file_ops = &<T as ReadWriteFile<_>>::FILE_OPS;
        self.create_file(name, data, file_ops)
    }

    /// Creates a read-write file in this directory, with logic from callbacks.
    ///
    /// Reading from the file is handled by `f`. Writing to the file is handled by `w`.
    ///
    /// `f` and `w` must be function items or non-capturing closures.
    /// This is statically asserted and not a safety requirement.
    pub fn read_write_callback_file<'a, T, E: 'a, F, W>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
        _f: &'static F,
        _w: &'static W,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Send + Sync + 'static,
        F: Fn(&T, &mut fmt::Formatter<'_>) -> fmt::Result + Send + Sync,
        W: Fn(&T, &mut UserSliceReader) -> Result + Send + Sync,
    {
        let file_ops =
            <WritableAdapter<FormatAdapter<T, F>, W> as file_ops::ReadWriteFile<_>>::FILE_OPS
                .adapt()
                .adapt();
        self.create_file(name, data, file_ops)
    }

    /// Creates a write-only file in this directory.
    ///
    /// The file owns its backing data. Writing to the file uses the [`Reader`]
    /// implementation.
    ///
    /// The file is removed when the returned [`File`] is dropped.
    pub fn write_only_file<'a, T, E: 'a>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Reader + Send + Sync + 'static,
    {
        self.create_file(name, data, &T::FILE_OPS)
    }

    /// Creates a write-only file in this directory, with write logic from a callback.
    ///
    /// `w` must be a function item or a non-capturing closure.
    /// This is statically asserted and not a safety requirement.
    pub fn write_callback_file<'a, T, E: 'a, W>(
        &'a self,
        name: &'a CStr,
        data: impl PinInit<T, E> + 'a,
        _w: &'static W,
    ) -> impl PinInit<File<T>, E> + 'a
    where
        T: Send + Sync + 'static,
        W: Fn(&T, &mut UserSliceReader) -> Result + Send + Sync,
    {
        let file_ops = <WritableAdapter<NoWriter<T>, W> as WriteFile<_>>::FILE_OPS
            .adapt()
            .adapt();
        self.create_file(name, data, file_ops)
    }

    // While this function is safe, it is intentionally not public because it's a bit of a
    // footgun.
    //
    // Unless you also extract the `entry` later and schedule it for `Drop` at the appropriate
    // time, a `ScopedDir` with a `Dir` parent will never be deleted.
    fn scoped_dir<'data>(&self, name: &CStr) -> ScopedDir<'data, 'static> {
        #[cfg(CONFIG_DEBUG_FS)]
        {
            let parent_entry = match &self.0 {
                None => return ScopedDir::empty(),
                Some(entry) => entry.clone(),
            };
            ScopedDir {
                entry: ManuallyDrop::new(Entry::dynamic_dir(name, Some(parent_entry))),
                _phantom: PhantomData,
            }
        }
        #[cfg(not(CONFIG_DEBUG_FS))]
        ScopedDir::empty()
    }

    /// Creates a new scope, which is a directory associated with some data `T`.
    ///
    /// The created directory will be a subdirectory of `self`. The `init` closure is called to
    /// populate the directory with files and subdirectories. These files can reference the data
    /// stored in the scope.
    ///
    /// The entire directory tree created within the scope will be removed when the returned
    /// `Scope` handle is dropped.
    pub fn scope<'a, T: 'a, E: 'a, F>(
        &'a self,
        data: impl PinInit<T, E> + 'a,
        name: &'a CStr,
        init: F,
    ) -> impl PinInit<Scope<T>, E> + 'a
    where
        F: for<'data, 'dir> FnOnce(&'data T, &'dir ScopedDir<'data, 'dir>) + 'a,
    {
        Scope::new(data, |data| {
            let scoped = self.scoped_dir(name);
            init(data, &scoped);
            scoped.into_entry()
        })
    }
}

#[pin_data]
/// Handle to a DebugFS scope, which ensures that attached `data` will outlive the DebugFS entry
/// without moving.
///
/// This is internally used to back [`File`], and used in the API to represent the attachment
/// of a directory lifetime to a data structure which may be jointly accessed by a number of
/// different files.
///
/// When dropped, a `Scope` will remove all directories and files in the filesystem backed by the
/// attached data structure prior to releasing the attached data.
pub struct Scope<T> {
    // This order is load-bearing for drops - `_entry` must be dropped before `data`.
    #[cfg(CONFIG_DEBUG_FS)]
    _entry: Entry<'static>,
    #[pin]
    data: T,
    // Even if `T` is `Unpin`, we still can't allow it to be moved.
    #[pin]
    _pin: PhantomPinned,
}

#[pin_data]
/// Handle to a DebugFS file, owning its backing data.
///
/// When dropped, the DebugFS file will be removed and the attached data will be dropped.
pub struct File<T> {
    #[pin]
    scope: Scope<T>,
}

#[cfg(not(CONFIG_DEBUG_FS))]
impl<'b, T: 'b> Scope<T> {
    fn new<E: 'b, F>(data: impl PinInit<T, E> + 'b, init: F) -> impl PinInit<Self, E> + 'b
    where
        F: for<'a> FnOnce(&'a T) + 'b,
    {
        try_pin_init! {
            Self {
                data <- data,
                _pin: PhantomPinned
            } ? E
        }
        .pin_chain(|scope| {
            init(&scope.data);
            Ok(())
        })
    }
}

#[cfg(CONFIG_DEBUG_FS)]
impl<'b, T: 'b> Scope<T> {
    fn entry_mut(self: Pin<&mut Self>) -> &mut Entry<'static> {
        // SAFETY: _entry is not structurally pinned.
        unsafe { &mut Pin::into_inner_unchecked(self)._entry }
    }

    fn new<E: 'b, F>(data: impl PinInit<T, E> + 'b, init: F) -> impl PinInit<Self, E> + 'b
    where
        F: for<'a> FnOnce(&'a T) -> Entry<'static> + 'b,
    {
        try_pin_init! {
            Self {
                _entry: Entry::empty(),
                data <- data,
                _pin: PhantomPinned
            } ? E
        }
        .pin_chain(|scope| {
            *scope.entry_mut() = init(&scope.data);
            Ok(())
        })
    }
}

impl<'a, T: 'a> Scope<T> {
    /// Creates a new scope, which is a directory at the root of the debugfs filesystem,
    /// associated with some data `T`.
    ///
    /// The `init` closure is called to populate the directory with files and subdirectories. These
    /// files can reference the data stored in the scope.
    ///
    /// The entire directory tree created within the scope will be removed when the returned
    /// `Scope` handle is dropped.
    pub fn dir<E: 'a, F>(
        data: impl PinInit<T, E> + 'a,
        name: &'a CStr,
        init: F,
    ) -> impl PinInit<Self, E> + 'a
    where
        F: for<'data, 'dir> FnOnce(&'data T, &'dir ScopedDir<'data, 'dir>) + 'a,
    {
        Scope::new(data, |data| {
            let scoped = ScopedDir::new(name);
            init(data, &scoped);
            scoped.into_entry()
        })
    }
}

impl<T> Deref for Scope<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.data
    }
}

impl<T> Deref for File<T> {
    type Target = T;
    fn deref(&self) -> &T {
        &self.scope
    }
}

/// A handle to a directory which will live at most `'dir`, accessing data that will live for at
/// least `'data`.
///
/// Dropping a ScopedDir will not delete or clean it up, this is expected to occur through dropping
/// the `Scope` that created it.
pub struct ScopedDir<'data, 'dir> {
    #[cfg(CONFIG_DEBUG_FS)]
    entry: ManuallyDrop<Entry<'dir>>,
    _phantom: PhantomData<fn(&'data ()) -> &'dir ()>,
}

impl<'data, 'dir> ScopedDir<'data, 'dir> {
    /// Creates a subdirectory inside this `ScopedDir`.
    ///
    /// The returned directory handle cannot outlive this one.
    pub fn dir<'dir2>(&'dir2 self, name: &CStr) -> ScopedDir<'data, 'dir2> {
        #[cfg(not(CONFIG_DEBUG_FS))]
        let _ = name;
        ScopedDir {
            #[cfg(CONFIG_DEBUG_FS)]
            entry: ManuallyDrop::new(Entry::dir(name, Some(&*self.entry))),
            _phantom: PhantomData,
        }
    }

    fn create_file<T: Sync>(&self, name: &CStr, data: &'data T, vtable: &'static FileOps<T>) {
        #[cfg(CONFIG_DEBUG_FS)]
        core::mem::forget(Entry::file(name, &self.entry, data, vtable));
    }

    /// Creates a read-only file in this directory.
    ///
    /// The file's contents are produced by invoking [`Writer::write`].
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn read_only_file<T: Writer + Send + Sync + 'static>(&self, name: &CStr, data: &'data T) {
        self.create_file(name, data, &T::FILE_OPS)
    }

    /// Creates a read-only file in this directory, with contents from a callback.
    ///
    /// The file contents are generated by calling `f` with `data`.
    ///
    ///
    /// `f` must be a function item or a non-capturing closure.
    /// This is statically asserted and not a safety requirement.
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn read_callback_file<T, F>(&self, name: &CStr, data: &'data T, _f: &'static F)
    where
        T: Send + Sync + 'static,
        F: Fn(&T, &mut fmt::Formatter<'_>) -> fmt::Result + Send + Sync,
    {
        let vtable = <FormatAdapter<T, F> as ReadFile<_>>::FILE_OPS.adapt();
        self.create_file(name, data, vtable)
    }

    /// Creates a read-write file in this directory.
    ///
    /// Reading the file uses the [`Writer`] implementation on `data`. Writing to the file uses
    /// the [`Reader`] implementation on `data`.
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn read_write_file<T: Writer + Reader + Send + Sync + 'static>(
        &self,
        name: &CStr,
        data: &'data T,
    ) {
        let vtable = &<T as ReadWriteFile<_>>::FILE_OPS;
        self.create_file(name, data, vtable)
    }

    /// Creates a read-write file in this directory, with logic from callbacks.
    ///
    /// Reading from the file is handled by `f`. Writing to the file is handled by `w`.
    ///
    /// `f` and `w` must be function items or non-capturing closures.
    /// This is statically asserted and not a safety requirement.
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn read_write_callback_file<T, F, W>(
        &self,
        name: &CStr,
        data: &'data T,
        _f: &'static F,
        _w: &'static W,
    ) where
        T: Send + Sync + 'static,
        F: Fn(&T, &mut fmt::Formatter<'_>) -> fmt::Result + Send + Sync,
        W: Fn(&T, &mut UserSliceReader) -> Result + Send + Sync,
    {
        let vtable = <WritableAdapter<FormatAdapter<T, F>, W> as ReadWriteFile<_>>::FILE_OPS
            .adapt()
            .adapt();
        self.create_file(name, data, vtable)
    }

    /// Creates a write-only file in this directory.
    ///
    /// Writing to the file uses the [`Reader`] implementation on `data`.
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn write_only_file<T: Reader + Send + Sync + 'static>(&self, name: &CStr, data: &'data T) {
        let vtable = &<T as WriteFile<_>>::FILE_OPS;
        self.create_file(name, data, vtable)
    }

    /// Creates a write-only file in this directory, with write logic from a callback.
    ///
    /// Writing to the file is handled by `w`.
    ///
    /// `w` must be a function item or a non-capturing closure.
    /// This is statically asserted and not a safety requirement.
    ///
    /// This function does not produce an owning handle to the file. The created
    /// file is removed when the [`Scope`] that this directory belongs
    /// to is dropped.
    pub fn write_only_callback_file<T, W>(&self, name: &CStr, data: &'data T, _w: &'static W)
    where
        T: Send + Sync + 'static,
        W: Fn(&T, &mut UserSliceReader) -> Result + Send + Sync,
    {
        let vtable = &<WritableAdapter<NoWriter<T>, W> as WriteFile<_>>::FILE_OPS
            .adapt()
            .adapt();
        self.create_file(name, data, vtable)
    }

    fn empty() -> Self {
        ScopedDir {
            #[cfg(CONFIG_DEBUG_FS)]
            entry: ManuallyDrop::new(Entry::empty()),
            _phantom: PhantomData,
        }
    }
    #[cfg(CONFIG_DEBUG_FS)]
    fn into_entry(self) -> Entry<'dir> {
        ManuallyDrop::into_inner(self.entry)
    }
    #[cfg(not(CONFIG_DEBUG_FS))]
    fn into_entry(self) {}
}

impl<'data> ScopedDir<'data, 'static> {
    // This is safe, but intentionally not exported due to footgun status. A ScopedDir with no
    // parent will never be released by default, and needs to have its entry extracted and used
    // somewhere.
    fn new(name: &CStr) -> ScopedDir<'data, 'static> {
        ScopedDir {
            #[cfg(CONFIG_DEBUG_FS)]
            entry: ManuallyDrop::new(Entry::dir(name, None)),
            _phantom: PhantomData,
        }
    }
}
