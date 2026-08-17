use zerocopy::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_zero_dynamic_size(source: &mut format::LocoPacket) {
    source.zero()
}
