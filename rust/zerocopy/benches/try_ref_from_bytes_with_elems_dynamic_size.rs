#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_try_ref_from_bytes_with_elems_dynamic_size(
    source: &[u8],
    count: usize,
) -> Option<&format::CocoPacket> {
    zerocopy::TryFromBytes::try_ref_from_bytes_with_elems(source, count).ok()
}
