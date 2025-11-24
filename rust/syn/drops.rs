use std::iter;
use std::mem::ManuallyDrop;
use std::ops::{Deref, DerefMut};
use std::option;
use std::slice;

#[repr(transparent)]
pub(crate) struct NoDrop<T: ?Sized>(ManuallyDrop<T>);

impl<T> NoDrop<T> {
    pub(crate) fn new(value: T) -> Self
    where
        T: TrivialDrop,
    {
        NoDrop(ManuallyDrop::new(value))
    }
}

impl<T: ?Sized> Deref for NoDrop<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T: ?Sized> DerefMut for NoDrop<T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

pub(crate) trait TrivialDrop {}

impl<T> TrivialDrop for iter::Empty<T> {}
impl<T> TrivialDrop for slice::Iter<'_, T> {}
impl<T> TrivialDrop for slice::IterMut<'_, T> {}
impl<T> TrivialDrop for option::IntoIter<&T> {}
impl<T> TrivialDrop for option::IntoIter<&mut T> {}

#[test]
fn test_needs_drop() {
    use std::mem::needs_drop;

    struct NeedsDrop;

    impl Drop for NeedsDrop {
        fn drop(&mut self) {}
    }

    assert!(needs_drop::<NeedsDrop>());

    // Test each of the types with a handwritten TrivialDrop impl above.
    assert!(!needs_drop::<iter::Empty<NeedsDrop>>());
    assert!(!needs_drop::<slice::Iter<NeedsDrop>>());
    assert!(!needs_drop::<slice::IterMut<NeedsDrop>>());
    assert!(!needs_drop::<option::IntoIter<&NeedsDrop>>());
    assert!(!needs_drop::<option::IntoIter<&mut NeedsDrop>>());
}
