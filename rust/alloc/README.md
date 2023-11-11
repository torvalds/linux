# `alloc`

These source files come from the Rust standard library, hosted in
the <https://github.com/rust-lang/rust> repository, licensed under
"Apache-2.0 OR MIT" and adapted for kernel use. For copyright details,
see <https://github.com/rust-lang/rust/blob/master/COPYRIGHT>.

Please note that these files should be kept as close as possible to
upstream. In general, only additions should be performed (e.g. new
methods). Eventually, changes should make it into upstream so that,
at some point, this fork can be dropped from the kernel tree.


## Rationale

On one hand, kernel folks wanted to keep `alloc` in-tree to have more
freedom in both workflow and actual features if actually needed
(e.g. receiver types if we ended up using them), which is reasonable.

On the other hand, Rust folks wanted to keep `alloc` as close as
upstream as possible and avoid as much divergence as possible, which
is also reasonable.

We agreed on a middle-ground: we would keep a subset of `alloc`
in-tree that would be as small and as close as possible to upstream.
Then, upstream can start adding the functions that we add to `alloc`
etc., until we reach a point where the kernel already knows exactly
what it needs in `alloc` and all the new methods are merged into
upstream, so that we can drop `alloc` from the kernel tree and go back
to using the upstream one.

By doing this, the kernel can go a bit faster now, and Rust can
slowly incorporate and discuss the changes as needed.
