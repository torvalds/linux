use core::cmp::Ordering;

/// A line-column pair representing the start or end of a `Span`.
///
/// This type is semver exempt and not exposed by default.
#[cfg_attr(docsrs, doc(cfg(feature = "span-locations")))]
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct LineColumn {
    /// The 1-indexed line in the source file on which the span starts or ends
    /// (inclusive).
    pub line: usize,
    /// The 0-indexed column (in UTF-8 characters) in the source file on which
    /// the span starts or ends (inclusive).
    pub column: usize,
}

impl Ord for LineColumn {
    fn cmp(&self, other: &Self) -> Ordering {
        self.line
            .cmp(&other.line)
            .then(self.column.cmp(&other.column))
    }
}

impl PartialOrd for LineColumn {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}
