// SPDX-License-Identifier: Apache-2.0 OR MIT

#![allow(clippy::undocumented_unsafe_blocks)]
#![cfg_attr(feature = "alloc", feature(allocator_api))]
#![allow(clippy::missing_safety_doc)]

use core::{
    cell::{Cell, UnsafeCell},
    marker::PhantomPinned,
    ops::{Deref, DerefMut},
    pin::Pin,
    sync::atomic::{AtomicBool, Ordering},
};
use std::{
    sync::Arc,
    thread::{self, park, sleep, Builder, Thread},
    time::Duration,
};

use pin_init::*;
#[expect(unused_attributes)]
#[path = "./linked_list.rs"]
pub mod linked_list;
use linked_list::*;

pub struct SpinLock {
    inner: AtomicBool,
}

impl SpinLock {
    #[inline]
    pub fn acquire(&self) -> SpinLockGuard<'_> {
        while self
            .inner
            .compare_exchange(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            while self.inner.load(Ordering::Relaxed) {
                thread::yield_now();
            }
        }
        SpinLockGuard(self)
    }

    #[inline]
    #[allow(clippy::new_without_default)]
    pub const fn new() -> Self {
        Self {
            inner: AtomicBool::new(false),
        }
    }
}

pub struct SpinLockGuard<'a>(&'a SpinLock);

impl Drop for SpinLockGuard<'_> {
    #[inline]
    fn drop(&mut self) {
        self.0.inner.store(false, Ordering::Release);
    }
}

#[pin_data]
pub struct CMutex<T> {
    #[pin]
    wait_list: ListHead,
    spin_lock: SpinLock,
    locked: Cell<bool>,
    #[pin]
    data: UnsafeCell<T>,
}

impl<T> CMutex<T> {
    #[inline]
    pub fn new(val: impl PinInit<T>) -> impl PinInit<Self> {
        pin_init!(CMutex {
            wait_list <- ListHead::new(),
            spin_lock: SpinLock::new(),
            locked: Cell::new(false),
            data <- unsafe {
                pin_init_from_closure(|slot: *mut UnsafeCell<T>| {
                    val.__pinned_init(slot.cast::<T>())
                })
            },
        })
    }

    #[inline]
    pub fn lock(&self) -> Pin<CMutexGuard<'_, T>> {
        let mut sguard = self.spin_lock.acquire();
        if self.locked.get() {
            stack_pin_init!(let wait_entry = WaitEntry::insert_new(&self.wait_list));
            // println!("wait list length: {}", self.wait_list.size());
            while self.locked.get() {
                drop(sguard);
                park();
                sguard = self.spin_lock.acquire();
            }
            // This does have an effect, as the ListHead inside wait_entry implements Drop!
            #[expect(clippy::drop_non_drop)]
            drop(wait_entry);
        }
        self.locked.set(true);
        unsafe {
            Pin::new_unchecked(CMutexGuard {
                mtx: self,
                _pin: PhantomPinned,
            })
        }
    }

    #[allow(dead_code)]
    pub fn get_data_mut(self: Pin<&mut Self>) -> &mut T {
        // SAFETY: we have an exclusive reference and thus nobody has access to data.
        unsafe { &mut *self.data.get() }
    }
}

unsafe impl<T: Send> Send for CMutex<T> {}
unsafe impl<T: Send> Sync for CMutex<T> {}

pub struct CMutexGuard<'a, T> {
    mtx: &'a CMutex<T>,
    _pin: PhantomPinned,
}

impl<T> Drop for CMutexGuard<'_, T> {
    #[inline]
    fn drop(&mut self) {
        let sguard = self.mtx.spin_lock.acquire();
        self.mtx.locked.set(false);
        if let Some(list_field) = self.mtx.wait_list.next() {
            let wait_entry = list_field.as_ptr().cast::<WaitEntry>();
            unsafe { (*wait_entry).thread.unpark() };
        }
        drop(sguard);
    }
}

impl<T> Deref for CMutexGuard<'_, T> {
    type Target = T;

    #[inline]
    fn deref(&self) -> &Self::Target {
        unsafe { &*self.mtx.data.get() }
    }
}

impl<T> DerefMut for CMutexGuard<'_, T> {
    #[inline]
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.mtx.data.get() }
    }
}

#[pin_data]
#[repr(C)]
struct WaitEntry {
    #[pin]
    wait_list: ListHead,
    thread: Thread,
}

impl WaitEntry {
    #[inline]
    fn insert_new(list: &ListHead) -> impl PinInit<Self> + '_ {
        pin_init!(Self {
            thread: thread::current(),
            wait_list <- ListHead::insert_prev(list),
        })
    }
}

#[cfg(not(any(feature = "std", feature = "alloc")))]
fn main() {}

#[allow(dead_code)]
#[cfg_attr(test, test)]
#[cfg(any(feature = "std", feature = "alloc"))]
fn main() {
    let mtx: Pin<Arc<CMutex<usize>>> = Arc::pin_init(CMutex::new(0)).unwrap();
    let mut handles = vec![];
    let thread_count = 20;
    let workload = if cfg!(miri) { 100 } else { 1_000 };
    for i in 0..thread_count {
        let mtx = mtx.clone();
        handles.push(
            Builder::new()
                .name(format!("worker #{i}"))
                .spawn(move || {
                    for _ in 0..workload {
                        *mtx.lock() += 1;
                    }
                    println!("{i} halfway");
                    sleep(Duration::from_millis((i as u64) * 10));
                    for _ in 0..workload {
                        *mtx.lock() += 1;
                    }
                    println!("{i} finished");
                })
                .expect("should not fail"),
        );
    }
    for h in handles {
        h.join().expect("thread panicked");
    }
    println!("{:?}", &*mtx.lock());
    assert_eq!(*mtx.lock(), workload * thread_count * 2);
}
