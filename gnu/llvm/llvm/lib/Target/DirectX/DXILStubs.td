// DXIL doesn't actually use registers, but this gets the boilerplate code
// generated through tablegen.
let Namespace = "dxil" in {
def DXIL : Register<"dxil">;
def DXILClass : RegisterClass<"dxil", [i32], 32, (add DXIL)>;
}

class DXILInst : Instruction {
  let Namespace = "dxil";
  let DecoderNamespace = "dxil";

  dag OutOperandList = (outs);
  dag InOperandList =  (ins);
  let AsmString = "dummy";
  let Pattern = [];
}

def DummyInst : DXILInst;
