#[path = "formats/coco_dynamic_padding.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_ref_from_suffix_with_elems_dynamic_padding(
    source: &[u8],
    count: usize,
) -> Option<&format::LocoPacket> {
    match zerocopy::FromBytes::ref_from_suffix_with_elems(source, count) {
        Ok((_rest, packet)) => Some(packet),
        _ => None,
    }
}
