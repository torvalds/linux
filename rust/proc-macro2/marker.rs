use alloc::rc::Rc;
use core::marker::PhantomData;
use core::panic::{RefUnwindSafe, UnwindSafe};

// Zero sized marker with the correct set of autotrait impls we want all proc
// macro types to have.
#[derive(Copy, Clone)]
#[cfg_attr(
    all(procmacro2_semver_exempt, any(not(wrap_proc_macro), super_unstable)),
    derive(PartialEq, Eq)
)]
pub(crate) struct ProcMacroAutoTraits(PhantomData<Rc<()>>);

pub(crate) const MARKER: ProcMacroAutoTraits = ProcMacroAutoTraits(PhantomData);

impl UnwindSafe for ProcMacroAutoTraits {}
impl RefUnwindSafe for ProcMacroAutoTraits {}
