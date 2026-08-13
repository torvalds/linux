// SPDX-License-Identifier: GPL-2.0

//! PCI interrupt infrastructure.

use super::Device;
use crate::{
    bindings,
    device,
    device::Bound,
    error::to_result,
    irq::{
        self,
        IrqRequest, //
    },
    prelude::*, //
};
use core::num::NonZero;

/// IRQ type flags for PCI interrupt allocation.
#[derive(Debug, Clone, Copy)]
pub enum IrqType {
    /// INTx interrupts.
    Intx,
    /// Message Signaled Interrupts (MSI).
    Msi,
    /// Extended Message Signaled Interrupts (MSI-X).
    MsiX,
}

impl IrqType {
    /// Convert to the corresponding kernel flags.
    const fn as_raw(self) -> u32 {
        match self {
            IrqType::Intx => bindings::PCI_IRQ_INTX,
            IrqType::Msi => bindings::PCI_IRQ_MSI,
            IrqType::MsiX => bindings::PCI_IRQ_MSIX,
        }
    }
}

/// Set of IRQ types that can be used for PCI interrupt allocation.
#[derive(Debug, Clone, Copy, Default)]
pub struct IrqTypes(u32);

impl IrqTypes {
    /// Create a set containing all IRQ types (MSI-X, MSI, and INTx).
    pub const fn all() -> Self {
        Self(bindings::PCI_IRQ_ALL_TYPES)
    }

    /// Build a set of IRQ types.
    ///
    /// # Examples
    ///
    /// ```ignore
    /// // Create a set with only MSI and MSI-X (no INTx interrupts).
    /// let msi_only = IrqTypes::default()
    ///     .with(IrqType::Msi)
    ///     .with(IrqType::MsiX);
    /// ```
    pub const fn with(self, irq_type: IrqType) -> Self {
        Self(self.0 | irq_type.as_raw())
    }

    /// Get the raw flags value.
    const fn as_raw(self) -> u32 {
        self.0
    }
}

/// Represents an allocated IRQ vector for a specific PCI device.
///
/// This type ties an IRQ vector to the device it was allocated for,
/// ensuring the vector is only used with the correct device.
#[derive(Clone, Copy)]
pub struct IrqVector<'a> {
    dev: &'a Device<Bound>,
    reg: &'a IrqVectorRegistration<'a>,
    index: u32,
}

impl<'a> IrqVector<'a> {
    /// Creates a new [`IrqVector`] for the given device and index.
    ///
    /// # Safety
    ///
    /// - `index` must be a valid IRQ vector index for `reg`.
    /// - `dev` must be the device `reg` was allocated from.
    #[inline]
    unsafe fn new(dev: &'a Device<Bound>, reg: &'a IrqVectorRegistration<'a>, index: u32) -> Self {
        Self { dev, reg, index }
    }

    /// Returns the raw vector index.
    fn index(&self) -> u32 {
        self.index
    }

    /// Returns the [`IrqVectorRegistration`] this vector was derived from.
    #[inline]
    pub fn vectors(&self) -> &'a IrqVectorRegistration<'a> {
        self.reg
    }
}

impl<'a> TryInto<IrqRequest<'a>> for IrqVector<'a> {
    type Error = Error;

    fn try_into(self) -> Result<IrqRequest<'a>> {
        // SAFETY: `self.dev.as_raw()` returns a valid pointer to a `struct pci_dev`.
        let irq = unsafe { bindings::pci_irq_vector(self.dev.as_raw(), self.index()) };
        if irq < 0 {
            return Err(crate::error::Error::from_errno(irq));
        }
        // SAFETY: `irq` is guaranteed to be a valid IRQ number for `self.dev`.
        Ok(unsafe { IrqRequest::new(self.dev.as_ref(), irq as u32) })
    }
}

/// An allocation of PCI interrupt vectors for a device.
///
/// This type owns the vector allocation; dropping it frees the vectors. IRQ handlers borrow from
/// this registration and must be dropped before it is.
///
/// # Invariants
///
/// `dev` has an allocation of `len` interrupt vectors.
pub struct IrqVectorRegistration<'a> {
    dev: &'a Device<Bound>,
    len: NonZero<usize>,
}

impl<'a> IrqVectorRegistration<'a> {
    /// Returns the number of allocated vectors.
    ///
    /// This is at least the `min_vecs` that [`Device::alloc_irq_vectors`] was asked for.
    #[inline]
    #[allow(clippy::len_without_is_empty)]
    pub fn len(&self) -> usize {
        self.len.get()
    }

    /// Returns the [`IrqVector`] at `index`.
    ///
    /// Returns [`EINVAL`] if the `index` is out of bounds for the length reported by
    /// [`Self::len()`].
    #[inline]
    pub fn index(&self, index: usize) -> Result<IrqVector<'_>> {
        if index >= self.len.get() {
            return Err(EINVAL);
        }

        // SAFETY: `index` is within bounds of this registration's allocation, and `self.dev` is
        // the device it was allocated from.
        Ok(unsafe { IrqVector::new(self.dev, self, index as u32) })
    }
}

impl Drop for IrqVectorRegistration<'_> {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: By the type invariant, `self.dev.as_raw()` is a valid pointer to a
        // `struct pci_dev` that has successfully allocated IRQ vectors.
        unsafe { bindings::pci_free_irq_vectors(self.dev.as_raw()) };
    }
}

impl Device<device::Bound> {
    /// Returns a [`kernel::irq::Registration`] for the given IRQ vector.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the resulting [`irq::Registration`] or otherwise prevent
    /// its [`Drop`] implementation from running.
    pub unsafe fn request_irq<'a, T: crate::irq::Handler + 'a>(
        &'a self,
        vector: IrqVector<'a>,
        flags: irq::Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> impl PinInit<irq::Registration<'a, T>, Error> + 'a {
        pin_init::pin_init_scope(move || {
            let request = vector.try_into()?;

            // SAFETY: Caller guarantees the Registration will not be leaked.
            Ok(unsafe { irq::Registration::<T>::new(request, flags, name, handler) })
        })
    }

    /// Returns a [`kernel::irq::ThreadedRegistration`] for the given IRQ vector.
    ///
    /// # Safety
    ///
    /// Callers must not `mem::forget()` the resulting [`irq::ThreadedRegistration`] or otherwise
    /// prevent its [`Drop`] implementation from running.
    pub unsafe fn request_threaded_irq<'a, T: crate::irq::ThreadedHandler + 'a>(
        &'a self,
        vector: IrqVector<'a>,
        flags: irq::Flags,
        name: &'static CStr,
        handler: impl PinInit<T, Error> + 'a,
    ) -> impl PinInit<irq::ThreadedRegistration<'a, T>, Error> + 'a {
        pin_init::pin_init_scope(move || {
            let request = vector.try_into()?;

            // SAFETY: Caller guarantees the Registration will not be leaked.
            Ok(unsafe { irq::ThreadedRegistration::<T>::new(request, flags, name, handler) })
        })
    }

    /// Allocate IRQ vectors for this PCI device.
    ///
    /// Allocates between `min_vecs` and `max_vecs` interrupt vectors for the device.
    /// The allocation will use MSI-X, MSI, or INTx interrupts based on the `irq_types`
    /// parameter and hardware capabilities. When multiple types are specified, the kernel
    /// will try them in order of preference: MSI-X first, then MSI, then INTx interrupts.
    ///
    /// The allocated vectors are freed when the returned [`IrqVectorRegistration`] is dropped.
    /// IRQ handlers registered via [`Self::request_irq`] or [`Self::request_threaded_irq`]
    /// borrow from the registration, so the compiler ensures they are freed first.
    ///
    /// # Arguments
    ///
    /// * `min_vecs` - Minimum number of vectors required.
    /// * `max_vecs` - Maximum number of vectors to allocate.
    /// * `irq_types` - Types of interrupts that can be used.
    ///
    /// # Returns
    ///
    /// Returns the IRQ vector registration, or an error if `min_vecs` vectors cannot be
    /// allocated.
    ///
    /// # Examples
    ///
    /// ```
    /// # use kernel::{ device::Bound, pci};
    /// # fn no_run(dev: &pci::Device<Bound>) -> Result {
    /// // Allocate using any available interrupt type in the order mentioned above.
    /// let vectors = dev.alloc_irq_vectors(1, 32, pci::IrqTypes::all())?;
    ///
    /// // Allocate MSI or MSI-X only (no INTx interrupts).
    /// let msi_only = pci::IrqTypes::default()
    ///     .with(pci::IrqType::Msi)
    ///     .with(pci::IrqType::MsiX);
    /// let vectors = dev.alloc_irq_vectors(4, 16, msi_only)?;
    /// # Ok(())
    /// # }
    /// ```
    pub fn alloc_irq_vectors(
        &self,
        min_vecs: u32,
        max_vecs: u32,
        irq_types: IrqTypes,
    ) -> Result<IrqVectorRegistration<'_>> {
        // SAFETY:
        // - `self.as_raw()` is guaranteed to be a valid pointer to a `struct pci_dev`
        //   by the type invariant of `Device`.
        // - `pci_alloc_irq_vectors` internally validates all other parameters
        //   and returns error codes.
        let ret = unsafe {
            bindings::pci_alloc_irq_vectors(self.as_raw(), min_vecs, max_vecs, irq_types.as_raw())
        };
        to_result(ret)?;

        let len = NonZero::new(ret as usize).ok_or(EINVAL)?;

        // INVARIANT: `pci_alloc_irq_vectors()` allocated `len` vectors for `self`.
        Ok(IrqVectorRegistration { dev: self, len })
    }
}
