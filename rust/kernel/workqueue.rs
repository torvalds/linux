// SPDX-License-Identifier: GPL-2.0

//! Work queues.
//!
//! This file has two components: The raw work item API, and the safe work item API.
//!
//! One pattern that is used in both APIs is the `ID` const generic, which exists to allow a single
//! type to define multiple `work_struct` fields. This is done by choosing an id for each field,
//! and using that id to specify which field you wish to use. (The actual value doesn't matter, as
//! long as you use different values for different fields of the same struct.) Since these IDs are
//! generic, they are used only at compile-time, so they shouldn't exist in the final binary.
//!
//! # The raw API
//!
//! The raw API consists of the `RawWorkItem` trait, where the work item needs to provide an
//! arbitrary function that knows how to enqueue the work item. It should usually not be used
//! directly, but if you want to, you can use it without using the pieces from the safe API.
//!
//! # The safe API
//!
//! The safe API is used via the `Work` struct and `WorkItem` traits. Furthermore, it also includes
//! a trait called `WorkItemPointer`, which is usually not used directly by the user.
//!
//!  * The `Work` struct is the Rust wrapper for the C `work_struct` type.
//!  * The `WorkItem` trait is implemented for structs that can be enqueued to a workqueue.
//!  * The `WorkItemPointer` trait is implemented for the pointer type that points at a something
//!    that implements `WorkItem`.
//!
//! ## Example
//!
//! This example defines a struct that holds an integer and can be scheduled on the workqueue. When
//! the struct is executed, it will print the integer. Since there is only one `work_struct` field,
//! we do not need to specify ids for the fields.
//!
//! ```
//! use kernel::prelude::*;
//! use kernel::sync::Arc;
//! use kernel::workqueue::{self, Work, WorkItem};
//! use kernel::{impl_has_work, new_work};
//!
//! #[pin_data]
//! struct MyStruct {
//!     value: i32,
//!     #[pin]
//!     work: Work<MyStruct>,
//! }
//!
//! impl_has_work! {
//!     impl HasWork<Self> for MyStruct { self.work }
//! }
//!
//! impl MyStruct {
//!     fn new(value: i32) -> Result<Arc<Self>> {
//!         Arc::pin_init(pin_init!(MyStruct {
//!             value,
//!             work <- new_work!("MyStruct::work"),
//!         }))
//!     }
//! }
//!
//! impl WorkItem for MyStruct {
//!     type Pointer = Arc<MyStruct>;
//!
//!     fn run(this: Arc<MyStruct>) {
//!         pr_info!("The value is: {}", this.value);
//!     }
//! }
//!
//! /// This method will enqueue the struct for execution on the system workqueue, where its value
//! /// will be printed.
//! fn print_later(val: Arc<MyStruct>) {
//!     let _ = workqueue::system().enqueue(val);
//! }
//! ```
//!
//! The following example shows how multiple `work_struct` fields can be used:
//!
//! ```
//! use kernel::prelude::*;
//! use kernel::sync::Arc;
//! use kernel::workqueue::{self, Work, WorkItem};
//! use kernel::{impl_has_work, new_work};
//!
//! #[pin_data]
//! struct MyStruct {
//!     value_1: i32,
//!     value_2: i32,
//!     #[pin]
//!     work_1: Work<MyStruct, 1>,
//!     #[pin]
//!     work_2: Work<MyStruct, 2>,
//! }
//!
//! impl_has_work! {
//!     impl HasWork<Self, 1> for MyStruct { self.work_1 }
//!     impl HasWork<Self, 2> for MyStruct { self.work_2 }
//! }
//!
//! impl MyStruct {
//!     fn new(value_1: i32, value_2: i32) -> Result<Arc<Self>> {
//!         Arc::pin_init(pin_init!(MyStruct {
//!             value_1,
//!             value_2,
//!             work_1 <- new_work!("MyStruct::work_1"),
//!             work_2 <- new_work!("MyStruct::work_2"),
//!         }))
//!     }
//! }
//!
//! impl WorkItem<1> for MyStruct {
//!     type Pointer = Arc<MyStruct>;
//!
//!     fn run(this: Arc<MyStruct>) {
//!         pr_info!("The value is: {}", this.value_1);
//!     }
//! }
//!
//! impl WorkItem<2> for MyStruct {
//!     type Pointer = Arc<MyStruct>;
//!
//!     fn run(this: Arc<MyStruct>) {
//!         pr_info!("The second value is: {}", this.value_2);
//!     }
//! }
//!
//! fn print_1_later(val: Arc<MyStruct>) {
//!     let _ = workqueue::system().enqueue::<Arc<MyStruct>, 1>(val);
//! }
//!
//! fn print_2_later(val: Arc<MyStruct>) {
//!     let _ = workqueue::system().enqueue::<Arc<MyStruct>, 2>(val);
//! }
//! ```
//!
//! C header: [`include/linux/workqueue.h`](../../../../include/linux/workqueue.h)

use crate::{bindings, prelude::*, sync::Arc, sync::LockClassKey, types::Opaque};
use alloc::alloc::AllocError;
use alloc::boxed::Box;
use core::marker::PhantomData;
use core::pin::Pin;

/// Creates a [`Work`] initialiser with the given name and a newly-created lock class.
#[macro_export]
macro_rules! new_work {
    ($($name:literal)?) => {
        $crate::workqueue::Work::new($crate::optional_name!($($name)?), $crate::static_lock_class!())
    };
}

/// A kernel work queue.
///
/// Wraps the kernel's C `struct workqueue_struct`.
///
/// It allows work items to be queued to run on thread pools managed by the kernel. Several are
/// always available, for example, `system`, `system_highpri`, `system_long`, etc.
#[repr(transparent)]
pub struct Queue(Opaque<bindings::workqueue_struct>);

// SAFETY: Accesses to workqueues used by [`Queue`] are thread-safe.
unsafe impl Send for Queue {}
// SAFETY: Accesses to workqueues used by [`Queue`] are thread-safe.
unsafe impl Sync for Queue {}

impl Queue {
    /// Use the provided `struct workqueue_struct` with Rust.
    ///
    /// # Safety
    ///
    /// The caller must ensure that the provided raw pointer is not dangling, that it points at a
    /// valid workqueue, and that it remains valid until the end of 'a.
    pub unsafe fn from_raw<'a>(ptr: *const bindings::workqueue_struct) -> &'a Queue {
        // SAFETY: The `Queue` type is `#[repr(transparent)]`, so the pointer cast is valid. The
        // caller promises that the pointer is not dangling.
        unsafe { &*(ptr as *const Queue) }
    }

    /// Enqueues a work item.
    ///
    /// This may fail if the work item is already enqueued in a workqueue.
    ///
    /// The work item will be submitted using `WORK_CPU_UNBOUND`.
    pub fn enqueue<W, const ID: u64>(&self, w: W) -> W::EnqueueOutput
    where
        W: RawWorkItem<ID> + Send + 'static,
    {
        let queue_ptr = self.0.get();

        // SAFETY: We only return `false` if the `work_struct` is already in a workqueue. The other
        // `__enqueue` requirements are not relevant since `W` is `Send` and static.
        //
        // The call to `bindings::queue_work_on` will dereference the provided raw pointer, which
        // is ok because `__enqueue` guarantees that the pointer is valid for the duration of this
        // closure.
        //
        // Furthermore, if the C workqueue code accesses the pointer after this call to
        // `__enqueue`, then the work item was successfully enqueued, and `bindings::queue_work_on`
        // will have returned true. In this case, `__enqueue` promises that the raw pointer will
        // stay valid until we call the function pointer in the `work_struct`, so the access is ok.
        unsafe {
            w.__enqueue(move |work_ptr| {
                bindings::queue_work_on(bindings::WORK_CPU_UNBOUND as _, queue_ptr, work_ptr)
            })
        }
    }

    /// Tries to spawn the given function or closure as a work item.
    ///
    /// This method can fail because it allocates memory to store the work item.
    pub fn try_spawn<T: 'static + Send + FnOnce()>(&self, func: T) -> Result<(), AllocError> {
        let init = pin_init!(ClosureWork {
            work <- new_work!("Queue::try_spawn"),
            func: Some(func),
        });

        self.enqueue(Box::pin_init(init).map_err(|_| AllocError)?);
        Ok(())
    }
}

/// A helper type used in `try_spawn`.
#[pin_data]
struct ClosureWork<T> {
    #[pin]
    work: Work<ClosureWork<T>>,
    func: Option<T>,
}

impl<T> ClosureWork<T> {
    fn project(self: Pin<&mut Self>) -> &mut Option<T> {
        // SAFETY: The `func` field is not structurally pinned.
        unsafe { &mut self.get_unchecked_mut().func }
    }
}

impl<T: FnOnce()> WorkItem for ClosureWork<T> {
    type Pointer = Pin<Box<Self>>;

    fn run(mut this: Pin<Box<Self>>) {
        if let Some(func) = this.as_mut().project().take() {
            (func)()
        }
    }
}

/// A raw work item.
///
/// This is the low-level trait that is designed for being as general as possible.
///
/// The `ID` parameter to this trait exists so that a single type can provide multiple
/// implementations of this trait. For example, if a struct has multiple `work_struct` fields, then
/// you will implement this trait once for each field, using a different id for each field. The
/// actual value of the id is not important as long as you use different ids for different fields
/// of the same struct. (Fields of different structs need not use different ids.)
///
/// Note that the id is used only to select the right method to call during compilation. It wont be
/// part of the final executable.
///
/// # Safety
///
/// Implementers must ensure that any pointers passed to a `queue_work_on` closure by `__enqueue`
/// remain valid for the duration specified in the guarantees section of the documentation for
/// `__enqueue`.
pub unsafe trait RawWorkItem<const ID: u64> {
    /// The return type of [`Queue::enqueue`].
    type EnqueueOutput;

    /// Enqueues this work item on a queue using the provided `queue_work_on` method.
    ///
    /// # Guarantees
    ///
    /// If this method calls the provided closure, then the raw pointer is guaranteed to point at a
    /// valid `work_struct` for the duration of the call to the closure. If the closure returns
    /// true, then it is further guaranteed that the pointer remains valid until someone calls the
    /// function pointer stored in the `work_struct`.
    ///
    /// # Safety
    ///
    /// The provided closure may only return `false` if the `work_struct` is already in a workqueue.
    ///
    /// If the work item type is annotated with any lifetimes, then you must not call the function
    /// pointer after any such lifetime expires. (Never calling the function pointer is okay.)
    ///
    /// If the work item type is not [`Send`], then the function pointer must be called on the same
    /// thread as the call to `__enqueue`.
    unsafe fn __enqueue<F>(self, queue_work_on: F) -> Self::EnqueueOutput
    where
        F: FnOnce(*mut bindings::work_struct) -> bool;
}

/// Defines the method that should be called directly when a work item is executed.
///
/// This trait is implemented by `Pin<Box<T>>` and `Arc<T>`, and is mainly intended to be
/// implemented for smart pointer types. For your own structs, you would implement [`WorkItem`]
/// instead. The `run` method on this trait will usually just perform the appropriate
/// `container_of` translation and then call into the `run` method from the [`WorkItem`] trait.
///
/// This trait is used when the `work_struct` field is defined using the [`Work`] helper.
///
/// # Safety
///
/// Implementers must ensure that [`__enqueue`] uses a `work_struct` initialized with the [`run`]
/// method of this trait as the function pointer.
///
/// [`__enqueue`]: RawWorkItem::__enqueue
/// [`run`]: WorkItemPointer::run
pub unsafe trait WorkItemPointer<const ID: u64>: RawWorkItem<ID> {
    /// Run this work item.
    ///
    /// # Safety
    ///
    /// The provided `work_struct` pointer must originate from a previous call to `__enqueue` where
    /// the `queue_work_on` closure returned true, and the pointer must still be valid.
    unsafe extern "C" fn run(ptr: *mut bindings::work_struct);
}

/// Defines the method that should be called when this work item is executed.
///
/// This trait is used when the `work_struct` field is defined using the [`Work`] helper.
pub trait WorkItem<const ID: u64 = 0> {
    /// The pointer type that this struct is wrapped in. This will typically be `Arc<Self>` or
    /// `Pin<Box<Self>>`.
    type Pointer: WorkItemPointer<ID>;

    /// The method that should be called when this work item is executed.
    fn run(this: Self::Pointer);
}

/// Links for a work item.
///
/// This struct contains a function pointer to the `run` function from the [`WorkItemPointer`]
/// trait, and defines the linked list pointers necessary to enqueue a work item in a workqueue.
///
/// Wraps the kernel's C `struct work_struct`.
///
/// This is a helper type used to associate a `work_struct` with the [`WorkItem`] that uses it.
#[repr(transparent)]
pub struct Work<T: ?Sized, const ID: u64 = 0> {
    work: Opaque<bindings::work_struct>,
    _inner: PhantomData<T>,
}

// SAFETY: Kernel work items are usable from any thread.
//
// We do not need to constrain `T` since the work item does not actually contain a `T`.
unsafe impl<T: ?Sized, const ID: u64> Send for Work<T, ID> {}
// SAFETY: Kernel work items are usable from any thread.
//
// We do not need to constrain `T` since the work item does not actually contain a `T`.
unsafe impl<T: ?Sized, const ID: u64> Sync for Work<T, ID> {}

impl<T: ?Sized, const ID: u64> Work<T, ID> {
    /// Creates a new instance of [`Work`].
    #[inline]
    #[allow(clippy::new_ret_no_self)]
    pub fn new(name: &'static CStr, key: &'static LockClassKey) -> impl PinInit<Self>
    where
        T: WorkItem<ID>,
    {
        // SAFETY: The `WorkItemPointer` implementation promises that `run` can be used as the work
        // item function.
        unsafe {
            kernel::init::pin_init_from_closure(move |slot| {
                let slot = Self::raw_get(slot);
                bindings::init_work_with_key(
                    slot,
                    Some(T::Pointer::run),
                    false,
                    name.as_char_ptr(),
                    key.as_ptr(),
                );
                Ok(())
            })
        }
    }

    /// Get a pointer to the inner `work_struct`.
    ///
    /// # Safety
    ///
    /// The provided pointer must not be dangling and must be properly aligned. (But the memory
    /// need not be initialized.)
    #[inline]
    pub unsafe fn raw_get(ptr: *const Self) -> *mut bindings::work_struct {
        // SAFETY: The caller promises that the pointer is aligned and not dangling.
        //
        // A pointer cast would also be ok due to `#[repr(transparent)]`. We use `addr_of!` so that
        // the compiler does not complain that the `work` field is unused.
        unsafe { Opaque::raw_get(core::ptr::addr_of!((*ptr).work)) }
    }
}

/// Declares that a type has a [`Work<T, ID>`] field.
///
/// The intended way of using this trait is via the [`impl_has_work!`] macro. You can use the macro
/// like this:
///
/// ```no_run
/// use kernel::impl_has_work;
/// use kernel::prelude::*;
/// use kernel::workqueue::Work;
///
/// struct MyWorkItem {
///     work_field: Work<MyWorkItem, 1>,
/// }
///
/// impl_has_work! {
///     impl HasWork<MyWorkItem, 1> for MyWorkItem { self.work_field }
/// }
/// ```
///
/// Note that since the `Work` type is annotated with an id, you can have several `work_struct`
/// fields by using a different id for each one.
///
/// # Safety
///
/// The [`OFFSET`] constant must be the offset of a field in Self of type [`Work<T, ID>`]. The methods on
/// this trait must have exactly the behavior that the definitions given below have.
///
/// [`Work<T, ID>`]: Work
/// [`impl_has_work!`]: crate::impl_has_work
/// [`OFFSET`]: HasWork::OFFSET
pub unsafe trait HasWork<T, const ID: u64 = 0> {
    /// The offset of the [`Work<T, ID>`] field.
    ///
    /// [`Work<T, ID>`]: Work
    const OFFSET: usize;

    /// Returns the offset of the [`Work<T, ID>`] field.
    ///
    /// This method exists because the [`OFFSET`] constant cannot be accessed if the type is not Sized.
    ///
    /// [`Work<T, ID>`]: Work
    /// [`OFFSET`]: HasWork::OFFSET
    #[inline]
    fn get_work_offset(&self) -> usize {
        Self::OFFSET
    }

    /// Returns a pointer to the [`Work<T, ID>`] field.
    ///
    /// # Safety
    ///
    /// The provided pointer must point at a valid struct of type `Self`.
    ///
    /// [`Work<T, ID>`]: Work
    #[inline]
    unsafe fn raw_get_work(ptr: *mut Self) -> *mut Work<T, ID> {
        // SAFETY: The caller promises that the pointer is valid.
        unsafe { (ptr as *mut u8).add(Self::OFFSET) as *mut Work<T, ID> }
    }

    /// Returns a pointer to the struct containing the [`Work<T, ID>`] field.
    ///
    /// # Safety
    ///
    /// The pointer must point at a [`Work<T, ID>`] field in a struct of type `Self`.
    ///
    /// [`Work<T, ID>`]: Work
    #[inline]
    unsafe fn work_container_of(ptr: *mut Work<T, ID>) -> *mut Self
    where
        Self: Sized,
    {
        // SAFETY: The caller promises that the pointer points at a field of the right type in the
        // right kind of struct.
        unsafe { (ptr as *mut u8).sub(Self::OFFSET) as *mut Self }
    }
}

/// Used to safely implement the [`HasWork<T, ID>`] trait.
///
/// # Examples
///
/// ```
/// use kernel::impl_has_work;
/// use kernel::sync::Arc;
/// use kernel::workqueue::{self, Work};
///
/// struct MyStruct {
///     work_field: Work<MyStruct, 17>,
/// }
///
/// impl_has_work! {
///     impl HasWork<MyStruct, 17> for MyStruct { self.work_field }
/// }
/// ```
///
/// [`HasWork<T, ID>`]: HasWork
#[macro_export]
macro_rules! impl_has_work {
    ($(impl$(<$($implarg:ident),*>)?
       HasWork<$work_type:ty $(, $id:tt)?>
       for $self:ident $(<$($selfarg:ident),*>)?
       { self.$field:ident }
    )*) => {$(
        // SAFETY: The implementation of `raw_get_work` only compiles if the field has the right
        // type.
        unsafe impl$(<$($implarg),*>)? $crate::workqueue::HasWork<$work_type $(, $id)?> for $self $(<$($selfarg),*>)? {
            const OFFSET: usize = ::core::mem::offset_of!(Self, $field) as usize;

            #[inline]
            unsafe fn raw_get_work(ptr: *mut Self) -> *mut $crate::workqueue::Work<$work_type $(, $id)?> {
                // SAFETY: The caller promises that the pointer is not dangling.
                unsafe {
                    ::core::ptr::addr_of_mut!((*ptr).$field)
                }
            }
        }
    )*};
}

impl_has_work! {
    impl<T> HasWork<Self> for ClosureWork<T> { self.work }
}

unsafe impl<T, const ID: u64> WorkItemPointer<ID> for Arc<T>
where
    T: WorkItem<ID, Pointer = Self>,
    T: HasWork<T, ID>,
{
    unsafe extern "C" fn run(ptr: *mut bindings::work_struct) {
        // SAFETY: The `__enqueue` method always uses a `work_struct` stored in a `Work<T, ID>`.
        let ptr = ptr as *mut Work<T, ID>;
        // SAFETY: This computes the pointer that `__enqueue` got from `Arc::into_raw`.
        let ptr = unsafe { T::work_container_of(ptr) };
        // SAFETY: This pointer comes from `Arc::into_raw` and we've been given back ownership.
        let arc = unsafe { Arc::from_raw(ptr) };

        T::run(arc)
    }
}

unsafe impl<T, const ID: u64> RawWorkItem<ID> for Arc<T>
where
    T: WorkItem<ID, Pointer = Self>,
    T: HasWork<T, ID>,
{
    type EnqueueOutput = Result<(), Self>;

    unsafe fn __enqueue<F>(self, queue_work_on: F) -> Self::EnqueueOutput
    where
        F: FnOnce(*mut bindings::work_struct) -> bool,
    {
        // Casting between const and mut is not a problem as long as the pointer is a raw pointer.
        let ptr = Arc::into_raw(self).cast_mut();

        // SAFETY: Pointers into an `Arc` point at a valid value.
        let work_ptr = unsafe { T::raw_get_work(ptr) };
        // SAFETY: `raw_get_work` returns a pointer to a valid value.
        let work_ptr = unsafe { Work::raw_get(work_ptr) };

        if queue_work_on(work_ptr) {
            Ok(())
        } else {
            // SAFETY: The work queue has not taken ownership of the pointer.
            Err(unsafe { Arc::from_raw(ptr) })
        }
    }
}

unsafe impl<T, const ID: u64> WorkItemPointer<ID> for Pin<Box<T>>
where
    T: WorkItem<ID, Pointer = Self>,
    T: HasWork<T, ID>,
{
    unsafe extern "C" fn run(ptr: *mut bindings::work_struct) {
        // SAFETY: The `__enqueue` method always uses a `work_struct` stored in a `Work<T, ID>`.
        let ptr = ptr as *mut Work<T, ID>;
        // SAFETY: This computes the pointer that `__enqueue` got from `Arc::into_raw`.
        let ptr = unsafe { T::work_container_of(ptr) };
        // SAFETY: This pointer comes from `Arc::into_raw` and we've been given back ownership.
        let boxed = unsafe { Box::from_raw(ptr) };
        // SAFETY: The box was already pinned when it was enqueued.
        let pinned = unsafe { Pin::new_unchecked(boxed) };

        T::run(pinned)
    }
}

unsafe impl<T, const ID: u64> RawWorkItem<ID> for Pin<Box<T>>
where
    T: WorkItem<ID, Pointer = Self>,
    T: HasWork<T, ID>,
{
    type EnqueueOutput = ();

    unsafe fn __enqueue<F>(self, queue_work_on: F) -> Self::EnqueueOutput
    where
        F: FnOnce(*mut bindings::work_struct) -> bool,
    {
        // SAFETY: We're not going to move `self` or any of its fields, so its okay to temporarily
        // remove the `Pin` wrapper.
        let boxed = unsafe { Pin::into_inner_unchecked(self) };
        let ptr = Box::into_raw(boxed);

        // SAFETY: Pointers into a `Box` point at a valid value.
        let work_ptr = unsafe { T::raw_get_work(ptr) };
        // SAFETY: `raw_get_work` returns a pointer to a valid value.
        let work_ptr = unsafe { Work::raw_get(work_ptr) };

        if !queue_work_on(work_ptr) {
            // SAFETY: This method requires exclusive ownership of the box, so it cannot be in a
            // workqueue.
            unsafe { ::core::hint::unreachable_unchecked() }
        }
    }
}

/// Returns the system work queue (`system_wq`).
///
/// It is the one used by `schedule[_delayed]_work[_on]()`. Multi-CPU multi-threaded. There are
/// users which expect relatively short queue flush time.
///
/// Callers shouldn't queue work items which can run for too long.
pub fn system() -> &'static Queue {
    // SAFETY: `system_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_wq) }
}

/// Returns the system high-priority work queue (`system_highpri_wq`).
///
/// It is similar to the one returned by [`system`] but for work items which require higher
/// scheduling priority.
pub fn system_highpri() -> &'static Queue {
    // SAFETY: `system_highpri_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_highpri_wq) }
}

/// Returns the system work queue for potentially long-running work items (`system_long_wq`).
///
/// It is similar to the one returned by [`system`] but may host long running work items. Queue
/// flushing might take relatively long.
pub fn system_long() -> &'static Queue {
    // SAFETY: `system_long_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_long_wq) }
}

/// Returns the system unbound work queue (`system_unbound_wq`).
///
/// Workers are not bound to any specific CPU, not concurrency managed, and all queued work items
/// are executed immediately as long as `max_active` limit is not reached and resources are
/// available.
pub fn system_unbound() -> &'static Queue {
    // SAFETY: `system_unbound_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_unbound_wq) }
}

/// Returns the system freezable work queue (`system_freezable_wq`).
///
/// It is equivalent to the one returned by [`system`] except that it's freezable.
///
/// A freezable workqueue participates in the freeze phase of the system suspend operations. Work
/// items on the workqueue are drained and no new work item starts execution until thawed.
pub fn system_freezable() -> &'static Queue {
    // SAFETY: `system_freezable_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_freezable_wq) }
}

/// Returns the system power-efficient work queue (`system_power_efficient_wq`).
///
/// It is inclined towards saving power and is converted to "unbound" variants if the
/// `workqueue.power_efficient` kernel parameter is specified; otherwise, it is similar to the one
/// returned by [`system`].
pub fn system_power_efficient() -> &'static Queue {
    // SAFETY: `system_power_efficient_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_power_efficient_wq) }
}

/// Returns the system freezable power-efficient work queue (`system_freezable_power_efficient_wq`).
///
/// It is similar to the one returned by [`system_power_efficient`] except that is freezable.
///
/// A freezable workqueue participates in the freeze phase of the system suspend operations. Work
/// items on the workqueue are drained and no new work item starts execution until thawed.
pub fn system_freezable_power_efficient() -> &'static Queue {
    // SAFETY: `system_freezable_power_efficient_wq` is a C global, always available.
    unsafe { Queue::from_raw(bindings::system_freezable_power_efficient_wq) }
}
