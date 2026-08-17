#[path = "formats/coco_static_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_ref_from_bytes_static_size(source: &[u8]) -> Option<&format::LocoPacket> {
    zerocopy::FromBytes::ref_from_bytes(source).ok()
}
