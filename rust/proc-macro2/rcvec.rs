use alloc::rc::Rc;
use alloc::vec;
use core::mem;
use core::panic::RefUnwindSafe;
use core::slice;

pub(crate) struct RcVec<T> {
    inner: Rc<Vec<T>>,
}

pub(crate) struct RcVecBuilder<T> {
    inner: Vec<T>,
}

pub(crate) struct RcVecMut<'a, T> {
    inner: &'a mut Vec<T>,
}

#[derive(Clone)]
pub(crate) struct RcVecIntoIter<T> {
    inner: vec::IntoIter<T>,
}

impl<T> RcVec<T> {
    pub(crate) fn is_empty(&self) -> bool {
        self.inner.is_empty()
    }

    pub(crate) fn len(&self) -> usize {
        self.inner.len()
    }

    pub(crate) fn iter(&self) -> slice::Iter<T> {
        self.inner.iter()
    }

    pub(crate) fn make_mut(&mut self) -> RcVecMut<T>
    where
        T: Clone,
    {
        RcVecMut {
            inner: Rc::make_mut(&mut self.inner),
        }
    }

    pub(crate) fn get_mut(&mut self) -> Option<RcVecMut<T>> {
        let inner = Rc::get_mut(&mut self.inner)?;
        Some(RcVecMut { inner })
    }

    pub(crate) fn make_owned(mut self) -> RcVecBuilder<T>
    where
        T: Clone,
    {
        let vec = if let Some(owned) = Rc::get_mut(&mut self.inner) {
            mem::take(owned)
        } else {
            Vec::clone(&self.inner)
        };
        RcVecBuilder { inner: vec }
    }
}

impl<T> RcVecBuilder<T> {
    pub(crate) fn new() -> Self {
        RcVecBuilder { inner: Vec::new() }
    }

    pub(crate) fn with_capacity(cap: usize) -> Self {
        RcVecBuilder {
            inner: Vec::with_capacity(cap),
        }
    }

    pub(crate) fn push(&mut self, element: T) {
        self.inner.push(element);
    }

    pub(crate) fn extend(&mut self, iter: impl IntoIterator<Item = T>) {
        self.inner.extend(iter);
    }

    pub(crate) fn as_mut(&mut self) -> RcVecMut<T> {
        RcVecMut {
            inner: &mut self.inner,
        }
    }

    pub(crate) fn build(self) -> RcVec<T> {
        RcVec {
            inner: Rc::new(self.inner),
        }
    }
}

impl<'a, T> RcVecMut<'a, T> {
    pub(crate) fn push(&mut self, element: T) {
        self.inner.push(element);
    }

    pub(crate) fn extend(&mut self, iter: impl IntoIterator<Item = T>) {
        self.inner.extend(iter);
    }

    pub(crate) fn as_mut(&mut self) -> RcVecMut<T> {
        RcVecMut { inner: self.inner }
    }

    pub(crate) fn take(self) -> RcVecBuilder<T> {
        let vec = mem::take(self.inner);
        RcVecBuilder { inner: vec }
    }
}

impl<T> Clone for RcVec<T> {
    fn clone(&self) -> Self {
        RcVec {
            inner: Rc::clone(&self.inner),
        }
    }
}

impl<T> IntoIterator for RcVecBuilder<T> {
    type Item = T;
    type IntoIter = RcVecIntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        RcVecIntoIter {
            inner: self.inner.into_iter(),
        }
    }
}

impl<T> Iterator for RcVecIntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<Self::Item> {
        self.inner.next()
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        self.inner.size_hint()
    }
}

impl<T> RefUnwindSafe for RcVec<T> where T: RefUnwindSafe {}
