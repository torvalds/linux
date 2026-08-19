// SPDX-License-Identifier: GPL-2.0

//! PCI interrupt infrastructure.

use super::Device;
use crate::{
    bindings,
    device,
    device::Bound,
    error::to_result,
    irq::IrqRequest,
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

    /// Construct from raw value.
    #[inline]
    const fn from_raw(raw: u32) -> Self {
        match raw {
            bindings::PCI_IRQ_MSIX => IrqType::MsiX,
            bindings::PCI_IRQ_MSI => IrqType::Msi,
            _ => IrqType::Intx,
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

/// A resolved IRQ vector from a PCI interrupt vector allocation.
///
/// Created by [`IrqVectorRegistration::index`]. Convert to [`IrqRequest`] via [`From`] to register
/// a handler with [`irq::Registration::new`](crate::irq::Registration::new).
pub struct IrqVector<'a> {
    request: IrqRequest<'a>,
    reg: &'a IrqVectorRegistration<'a>,
}

impl<'a> IrqVector<'a> {
    /// Creates a new [`IrqVector`] with an already resolved [`IrqRequest`].
    ///
    /// # Safety
    ///
    /// `request` must have been resolved from `reg`.
    #[inline]
    unsafe fn new(request: IrqRequest<'a>, reg: &'a IrqVectorRegistration<'a>) -> Self {
        Self { request, reg }
    }

    /// Returns the [`IrqVectorRegistration`] this vector was derived from.
    #[inline]
    pub fn vectors(&self) -> &'a IrqVectorRegistration<'a> {
        self.reg
    }

    /// Returns the interrupt type the PCI core selected for this vector's allocation.
    #[inline]
    pub fn irq_type(&self) -> IrqType {
        self.reg.irq_type()
    }
}

impl<'a> From<IrqVector<'a>> for IrqRequest<'a> {
    #[inline]
    fn from(vector: IrqVector<'a>) -> Self {
        vector.request
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

    /// Returns the interrupt type the PCI core selected for this allocation.
    #[inline]
    pub fn irq_type(&self) -> IrqType {
        // SAFETY: `self.dev.as_raw()` is a valid pointer to a `struct pci_dev`.
        IrqType::from_raw(unsafe { bindings::pci_irq_type(self.dev.as_raw()) })
    }

    /// Returns the [`IrqVector`] at `index`.
    ///
    /// Returns [`EINVAL`] if the `index` is out of bounds for the length reported by
    /// [`Self::len()`].
    #[inline]
    pub fn index(&self, index: usize) -> Result<IrqVector<'_>> {
        // SAFETY: `self.dev.as_raw()` is a valid pointer to a `struct pci_dev`.
        let irq = unsafe { bindings::pci_irq_vector(self.dev.as_raw(), index as u32) };
        if irq < 0 {
            return Err(Error::from_errno(irq));
        }

        // SAFETY: `irq` is a valid IRQ number for `self.dev`, resolved from this registration.
        Ok(unsafe { IrqVector::new(IrqRequest::new(self.dev.as_ref(), irq as u32), self) })
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
    /// Allocate IRQ vectors for this PCI device.
    ///
    /// Allocates between `min_vecs` and `max_vecs` interrupt vectors for the device.
    /// The allocation will use MSI-X, MSI, or INTx interrupts based on the `irq_types`
    /// parameter and hardware capabilities. When multiple types are specified, the kernel
    /// will try them in order of preference: MSI-X first, then MSI, then INTx interrupts.
    ///
    /// The allocated vectors are freed when the returned [`IrqVectorRegistration`] is dropped.
    /// Use [`IrqVectorRegistration::index`] to obtain an [`IrqVector`] for a given vector
    /// index.
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
