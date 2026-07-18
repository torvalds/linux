use zerocopy::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
unsafe fn bench_split_at_unchecked_dynamic_size(
    source: &format::CocoPacket,
    len: usize,
) -> Split<&format::CocoPacket> {
    unsafe { source.split_at_unchecked(len) }
}
