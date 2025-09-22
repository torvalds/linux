include "llvm/TableGen/SearchableTable.td"
include "llvm/IR/Intrinsics.td"

def AMDGPUImageDMaskIntrinsicTable : GenericTable {
  let FilterClass = "AMDGPUImageDMaskIntrinsic";
  let Fields = ["Intr"];

  let PrimaryKey = ["Intr"];
  let PrimaryKeyName = "getAMDGPUImageDMaskIntrinsic";
  let PrimaryKeyEarlyOut = 1;
}
