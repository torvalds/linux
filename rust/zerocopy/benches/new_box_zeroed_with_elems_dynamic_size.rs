use zerocopy::*;

#[path = "formats/coco_dynamic_size.rs"]
mod format;

#[unsafe(no_mangle)]
fn bench_new_box_zeroed_with_elems_dynamic_size(count: usize) -> Option<Box<format::LocoPacket>> {
    FromZeros::new_box_zeroed_with_elems(count).ok()
}
