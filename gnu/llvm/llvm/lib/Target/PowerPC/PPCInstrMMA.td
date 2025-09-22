
// Mask immediates for MMA instructions (2, 4 and 8 bits).
def Msk2Imm : ImmLeaf<i32, [{ return isUInt<2>(Imm); }]>;
def Msk4Imm : ImmLeaf<i32, [{ return isUInt<4>(Imm); }]>;
def Msk8Imm : ImmLeaf<i32, [{ return isUInt<8>(Imm); }]>;

def MMA : Predicate<"Subtarget->hasMMA()">;


// Multiclass definitions for MMA accumulator instructions.
// ----------------------------------------------------------------------------

// Defines 2 unmasked instructions where the xo field for acc/non-acc version
// is even/odd.
multiclass ACC_UM_XOEO<bits<6> opcode, bits<8> xo, dag IOL, string asmbase,
                       string asmstr> {
  let Predicates = [MMA, IsNotISAFuture] in {
  def NAME :
    XX3Form_AT3_XAB6<opcode, !or(xo, 0x01), (outs acc:$AT), IOL,
                     !strconcat(asmbase#" ", asmstr), IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PP :
    XX3Form_AT3_XAB6<opcode, xo, (outs acc:$AT), !con((ins acc:$ATi), IOL),
                     !strconcat(asmbase#"pp ", asmstr), IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, IsISAFuture], isCodeGenOnly = 1 in {
  def NAME#W :
    XX3Form_AT3_XAB6<opcode, !or(xo, 0x01), (outs wacc:$AT), IOL,
                     !strconcat(asmbase#" ", asmstr), IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def WPP :
    XX3Form_AT3_XAB6<opcode, xo, (outs wacc:$AT), !con((ins wacc:$ATi), IOL),
                     !strconcat(asmbase#"pp ", asmstr), IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 4 instructions, masked/unmasked with masks 8, 4, 4 bits.
// The XO field for acc/non-acc version is even/odd.
multiclass ACC_UM_M844_XOEO<bits<6> opcode, bits<8> xo, dag IOL, string asmbase,
                            string asmstr> {
  defm NAME : ACC_UM_XOEO<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_XY4P8_XAB6<
      opcode, !or(xo, 0x01), (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u8imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_XY4P8_XAB6<
      opcode, xo, (outs acc:$AT),
      !con((ins acc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u8imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_XY4P8_XAB6<
      opcode, !or(xo, 0x01), (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u8imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_XY4P8_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con((ins wacc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u8imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 4 instructions, masked/unmasked with masks 4, 4, 4 bits.
// The XO field for acc/non-acc version is even/odd.
multiclass ACC_UM_M444_XOEO<bits<6> opcode, bits<8> xo, dag IOL, string asmbase,
                            string asmstr> {
  defm NAME : ACC_UM_XOEO<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_XYP4_XAB6<
      opcode, !or(xo, 0x01), (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u4imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_XYP4_XAB6<
      opcode, xo, (outs acc:$AT),
      !con((ins acc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u4imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_XYP4_XAB6<
      opcode, !or(xo, 0x01), (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u4imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_XYP4_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con((ins wacc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u4imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 4 instructions, masked/unmasked with masks 2, 4, 4 bits.
// The XO field for acc/non-acc version is even/odd.
multiclass ACC_UM_M244_XOEO<bits<6> opcode, bits<8> xo, dag IOL, string asmbase,
                            string asmstr> {
  defm NAME : ACC_UM_XOEO<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x01), (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, xo, (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x01), (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 4 instructions, masked/unmasked with masks 2, 4, 4 bits.
// Upper nibble of XO field for acc/non-acc version is 0x4/0x6.
multiclass ACC_UM_M244_XO46<bits<6> opcode, bits<8> xo, dag IOL, string asmbase,
                            string asmstr> {
  let Predicates = [MMA, IsNotISAFuture] in {
  def NAME :
    XX3Form_AT3_XAB6<opcode, xo, (outs acc:$AT), IOL,
                     !strconcat(asmbase#" ", asmstr), IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PP :
    XX3Form_AT3_XAB6<
      opcode, !or(xo, 0x20), (outs acc:$AT), !con((ins acc:$ATi), IOL),
      !strconcat(asmbase#"pp ", asmstr), IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, xo, (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x20), (outs acc:$AT),
      !con((ins acc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, IsISAFuture], isCodeGenOnly = 1 in {
  def NAME#W :
    XX3Form_AT3_XAB6<opcode, xo, (outs wacc:$AT), IOL,
                     !strconcat(asmbase#" ", asmstr), IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def WPP :
    XX3Form_AT3_XAB6<
      opcode, !or(xo, 0x20), (outs wacc:$AT), !con((ins wacc:$ATi), IOL),
      !strconcat(asmbase#"pp ", asmstr), IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x20), (outs acc:$AT),
      !con((ins wacc:$ATi),
           !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 10 instructions, operand negating, unmasked, masked with 2, 4, 4
// bits. Upper nibble are masked with 0x8, 0x4, 0xC for negating operands.
multiclass ACC_NEG_UM_M244_XOM84C<bits<6> opcode, bits<8> xo, dag IOL,
                                  string asmbase, string asmstr> {
  defm NAME : ACC_UM_M244_XOEO<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, IsNotISAFuture] in {
  def PN : XX3Form_AT3_XAB6<
             opcode, !or(xo, 0x80), (outs acc:$AT), !con((ins acc:$ATi), IOL),
             !strconcat(asmbase#"pn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def NP : XX3Form_AT3_XAB6<
             opcode, !or(xo, 0x40), (outs acc:$AT), !con((ins acc:$ATi), IOL),
             !strconcat(asmbase#"np ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def NN : XX3Form_AT3_XAB6<
             opcode, !or(xo, 0xC0), (outs acc:$AT), !con((ins acc:$ATi), IOL),
             !strconcat(asmbase#"nn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, IsISAFuture], isCodeGenOnly = 1 in {
  def WPN : XX3Form_AT3_XAB6<
              opcode, !or(xo, 0x80), (outs wacc:$AT), !con((ins wacc:$ATi), IOL),
              !strconcat(asmbase#"pn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def WNP : XX3Form_AT3_XAB6<
              opcode, !or(xo, 0x40), (outs wacc:$AT), !con((ins wacc:$ATi), IOL),
              !strconcat(asmbase#"np ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def WNN : XX3Form_AT3_XAB6<
              opcode, !or(xo, 0xC0), (outs wacc:$AT), !con((ins wacc:$ATi), IOL),
              !strconcat(asmbase#"nn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME#PN :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x80), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x40), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NN :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0xC0), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#WPN :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x80), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNP :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0x40), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNN :
    MMIRR_XX3Form_XY4P2_XAB6<
      opcode, !or(xo, 0xC0), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK, u2imm:$PMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK, $PMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 5 instructions, unmasked, operand negating.
// Upper nibble are masked with 0x8, 0x4, 0xC for negating operands.
multiclass ACC_NEG_UM_XOM84C<bits<6> opcode, bits<8> xo, dag IOL,
                             string asmbase, string asmstr> {
  defm NAME : ACC_UM_XOEO<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, IsNotISAFuture] in {
  def PN : XX3Form_AT3_XAB6<opcode, !or(xo, 0x80), (outs acc:$AT),
                            !con((ins acc:$ATi), IOL),
                            !strconcat(asmbase#"pn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def NP : XX3Form_AT3_XAB6<opcode, !or(xo, 0x40), (outs acc:$AT),
                            !con((ins acc:$ATi), IOL),
                            !strconcat(asmbase#"np ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def NN : XX3Form_AT3_XAB6<opcode, !or(xo, 0xC0), (outs acc:$AT),
                            !con((ins acc:$ATi), IOL),
                            !strconcat(asmbase#"nn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, IsISAFuture], isCodeGenOnly = 1 in {
  def WPN : XX3Form_AT3_XAB6<opcode, !or(xo, 0x80), (outs wacc:$AT),
                            !con((ins wacc:$ATi), IOL),
                            !strconcat(asmbase#"pn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def WNP : XX3Form_AT3_XAB6<opcode, !or(xo, 0x40), (outs wacc:$AT),
                            !con((ins wacc:$ATi), IOL),
                            !strconcat(asmbase#"np ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def WNN : XX3Form_AT3_XAB6<opcode, !or(xo, 0xC0), (outs wacc:$AT),
                            !con((ins wacc:$ATi), IOL),
                            !strconcat(asmbase#"nn ", asmstr), IIC_VecFP, []>,
           RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 10 instructions, operand negating, unmasked, masked with 4, 4 bits.
// Upper nibble are masked with 0x8, 0x4, 0xC for negating operands.
multiclass ACC_NEG_UM_M44_XOM84C<bits<6> opcode, bits<8> xo, dag IOL,
                                 string asmbase, string asmstr> {
  defm NAME : ACC_NEG_UM_XOM84C<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x01), (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, xo, (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#PN :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x80), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NP :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x40), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NN :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0xC0), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x01), (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WPN :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x80), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNP :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0x40), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNN :
    MMIRR_XX3Form_XY4_XAB6<
      opcode, !or(xo, 0xC0), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u4imm:$YMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// Defines 10 instructions, operand negating, unmasked, masked with 4, 2 bits.
// Upper nibble are masked with 0x8, 0x4, 0xC for negating operands.
multiclass ACC_NEG_UM_M42_XOM84C<bits<6> opcode, bits<8> xo, dag IOL,
                                 string asmbase, string asmstr> {
  defm NAME : ACC_NEG_UM_XOM84C<opcode, xo, IOL, asmbase, asmstr>;
  let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PM#NAME :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x01), (outs acc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#PP :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, xo, (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#PN :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x80), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NP :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x40), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#NN :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0xC0), (outs acc:$AT),
      !con((ins acc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
  let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PM#NAME#W :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x01), (outs wacc:$AT),
      !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK)),
      !strconcat("pm"#asmbase#" ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"@earlyclobber $AT">;
  def PM#NAME#WPP :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, xo, (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"pp ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WPN :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x80), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"pn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNP :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0x40), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"np ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def PM#NAME#WNN :
    MMIRR_XX3Form_X4Y2_XAB6<
      opcode, !or(xo, 0xC0), (outs wacc:$AT),
      !con((ins wacc:$ATi), !con(IOL, (ins u4imm:$XMSK, u2imm:$YMSK))),
      !strconcat("pm"#asmbase#"nn ", asmstr#", $XMSK, $YMSK"),
      IIC_VecFP, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  }
}

// End of class definitions.
//-----------------------------------------------------------------------------

let Predicates = [MMA, IsNotISAFuture] in {
  def XXMFACC :
    XForm_AT3<31, 0, 177, (outs acc:$ATo), (ins acc:$AT), "xxmfacc $AT",
              IIC_VecGeneral,
              [(set v512i1:$ATo, (int_ppc_mma_xxmfacc v512i1:$AT))]>,
              RegConstraint<"$ATo = $AT">, NoEncode<"$ATo">;
  def XXMTACC :
    XForm_AT3<31, 1, 177, (outs acc:$AT), (ins acc:$ATi), "xxmtacc $AT",
              IIC_VecGeneral,
              [(set v512i1:$AT, (int_ppc_mma_xxmtacc v512i1:$ATi))]>,
              RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  def KILL_PAIR : PPCPostRAExpPseudo<(outs vsrprc:$XTp), (ins vsrprc:$XSp),
                                      "#KILL_PAIR", []>,
                                      RegConstraint<"$XTp = $XSp">;
  def BUILD_UACC : PPCPostRAExpPseudo<(outs acc:$AT), (ins uacc:$AS),
                                      "#BUILD_UACC $AT, $AS", []>;
  // We define XXSETACCZ as rematerializable to undo CSE of that intrinsic in
  // the backend. We avoid CSE here because it generates a copy of the acc
  // register and this copy is more expensive than calling the intrinsic again.
  let isAsCheapAsAMove = 1, isReMaterializable = 1 in {
    def XXSETACCZ :
      XForm_AT3<31, 3, 177, (outs acc:$AT), (ins), "xxsetaccz $AT", IIC_VecGeneral,
                [(set v512i1:$AT, (int_ppc_mma_xxsetaccz))]>;
  }
  def XVI8GER4SPP :
    XX3Form_AT3_XAB6<59, 99, (outs acc:$AT), (ins acc:$ATi, vsrc:$XA, vsrc:$XB),
                     "xvi8ger4spp $AT, $XA, $XB", IIC_VecGeneral, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
  let mayStore = 1 in {
    def SPILL_ACC: PPCEmitTimePseudo<(outs), (ins acc:$AT, memrix16:$dst),
                                     "#SPILL_ACC", []>;
    def SPILL_UACC: PPCEmitTimePseudo<(outs), (ins uacc:$AT, memrix16:$dst),
                                     "#SPILL_UACC", []>;
  }
  let mayLoad = 1, hasSideEffects = 0 in {
    def RESTORE_ACC: PPCEmitTimePseudo<(outs acc:$AT), (ins memrix16:$src),
                                       "#RESTORE_ACC", []>;
    def RESTORE_UACC: PPCEmitTimePseudo<(outs uacc:$AT), (ins memrix16:$src),
                                       "#RESTORE_UACC", []>;
  }
}

let Predicates = [MMA, IsISAFuture], isCodeGenOnly = 1 in {
  // For Future and up XXMFACCW and XXMTACCW will not have patterns.
  // On Future CPU the wacc registers no longer overlap with the vsr registers
  // and so register allocation would have to know to match 4 vsr registers
  // with one wacc register.
  // On top of that Future CPU has a more convenient way to move between vsrs
  // and wacc registers using xxextfdmr512 and xxinstdmr512.
  def XXMFACCW :
    XForm_AT3<31, 0, 177, (outs wacc:$ATo), (ins wacc:$AT), "xxmfacc $AT",
              IIC_VecGeneral, []>,
              RegConstraint<"$ATo = $AT">, NoEncode<"$ATo">;
  def XXMTACCW :
    XForm_AT3<31, 1, 177, (outs wacc:$AT), (ins wacc:$ATi), "xxmtacc $AT",
              IIC_VecGeneral, []>,
              RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;

  let isAsCheapAsAMove = 1, isReMaterializable = 1 in {
    def XXSETACCZW :
      XForm_AT3<31, 3, 177, (outs wacc:$AT), (ins), "xxsetaccz $AT",
                IIC_VecGeneral, [(set v512i1:$AT, (int_ppc_mma_xxsetaccz))]>;
  }

  def XVI8GER4WSPP :
    XX3Form_AT3_XAB6<59, 99, (outs wacc:$AT),
                     (ins wacc:$ATi, vsrc:$XA, vsrc:$XB),
                     "xvi8ger4spp $AT, $XA, $XB", IIC_VecGeneral, []>,
                     RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;

  let mayStore = 1 in {
    def SPILL_WACC: PPCEmitTimePseudo<(outs), (ins wacc:$AT, memrix16:$dst),
                                      "#SPILL_WACC", []>;
  }
  let mayLoad = 1, hasSideEffects = 0 in {
    def RESTORE_WACC: PPCEmitTimePseudo<(outs wacc:$AT), (ins memrix16:$src),
                                        "#RESTORE_WACC", []>;
  }
}

let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def PMXVI8GER4SPP :
    MMIRR_XX3Form_XYP4_XAB6<59, 99, (outs acc:$AT),
                            (ins acc:$ATi, vsrc:$XA,vsrc:$XB, u4imm:$XMSK,
                             u4imm:$YMSK, u4imm:$PMSK),
                            "pmxvi8ger4spp $AT, $XA, $XB, $XMSK, $YMSK, $PMSK",
                            IIC_VecGeneral, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
}

let Predicates = [MMA, PrefixInstrs, IsISAFuture], isCodeGenOnly = 1 in {
  def PMXVI8GER4WSPP :
    MMIRR_XX3Form_XYP4_XAB6<59, 99, (outs wacc:$AT),
                            (ins wacc:$ATi, vsrc:$XA,vsrc:$XB, u4imm:$XMSK,
                             u4imm:$YMSK, u4imm:$PMSK),
                            "pmxvi8ger4spp $AT, $XA, $XB, $XMSK, $YMSK, $PMSK",
                            IIC_VecGeneral, []>,
    RegConstraint<"$ATi = $AT">, NoEncode<"$ATi">;
}

// MMA accumulating/non-accumulating instructions.
//------------------------------------------------------------------------------

// XVBF16GER2, XVBF16GER2PP, XVBF16GER2PN, XVBF16GER2NP, XVBF16GER2NN
// PMXVBF16GER2, PMXVBF16GER2PP, PMXVBF16GER2PN, PMXVBF16GER2NP, PMXVBF16GER2NN
defm XVBF16GER2 : ACC_NEG_UM_M244_XOM84C<59, 50, (ins vsrc:$XA, vsrc:$XB),
                                         "xvbf16ger2", "$AT, $XA, $XB">;

// XVI4GER8, XVI4GER8PP, PMXVI4GER8,  PMXVI4GER8PP
defm XVI4GER8 : ACC_UM_M844_XOEO<59, 34, (ins vsrc:$XA, vsrc:$XB),
                                 "xvi4ger8", "$AT, $XA, $XB">;

// XVI8GER4, XVI8GER4PP, PMXVI8GER4, PMXVI8GER4PP
defm XVI8GER4 : ACC_UM_M444_XOEO<59, 2, (ins vsrc:$XA, vsrc:$XB),
                                 "xvi8ger4", "$AT, $XA, $XB">;

// XVI16GER2, XVI16GER2PP, PMXVI16GER2, PMXVI16GER2PP
defm XVI16GER2 : ACC_UM_M244_XO46<59, 75, (ins vsrc:$XA, vsrc:$XB),
                                  "xvi16ger2", "$AT, $XA, $XB">;

// XVI16GER2S, XVI16GER2SPP, PMXVI16GER2S, PMXVI16GER2SPP
defm XVI16GER2S : ACC_UM_M244_XOEO<59, 42, (ins vsrc:$XA, vsrc:$XB),
                                   "xvi16ger2s", "$AT, $XA, $XB">;

// XVF16GER2, XVF16GER2PP, XVF16GER2PN, XVF16GER2NP, XVF16GER2NN
// PMXVF16GER2, PMXVF16GER2PP, PMXVF16GER2PN, PMXVF16GER2NP, PMXVF16GER2NN
defm XVF16GER2 : ACC_NEG_UM_M244_XOM84C<59, 18, (ins vsrc:$XA, vsrc:$XB),
                                        "xvf16ger2", "$AT, $XA, $XB">;

// XVF32GER, XVF32GERPP, XVF32GERPN, XVF32GERNP, XVF32GERPP
// PMXVF32GER, PMXVF32GERPP, PMXVF32GERPN, PMXVF32GERNP, PMXVF32GERPP
defm XVF32GER : ACC_NEG_UM_M44_XOM84C<59, 26, (ins vsrc:$XA, vsrc:$XB),
                                      "xvf32ger", "$AT, $XA, $XB">;

// XVF64GER, XVF64GERPP, XVF64GERPN, XVF64GERNP, XVF64GERNN
// PMXVF64GER, PMXVF64GERPP, PMXVF64GERPN, PMXVF64GERNP, PMXVF64GERNN
defm XVF64GER : ACC_NEG_UM_M42_XOM84C<59, 58, (ins vsrpevenrc:$XA, vsrc:$XB),
                                      "xvf64ger", "$AT, $XA, $XB">;
//------------------------------------------------------------------------------

// MMA Intrinsics
let Predicates = [MMA, IsNotISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvi4ger8 v16i8:$XA, v16i8:$XB)),
            (XVI4GER8 RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi4ger8pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI4GER8PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4 v16i8:$XA, v16i8:$XB)),
            (XVI8GER4 RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI8GER4PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2s v16i8:$XA, v16i8:$XB)),
            (XVI16GER2S RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2spp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI16GER2SPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}

let Predicates = [MMA, IsISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvi4ger8 v16i8:$XA, v16i8:$XB)),
            (XVI4GER8W RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi4ger8pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI4GER8WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4 v16i8:$XA, v16i8:$XB)),
            (XVI8GER4W RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI8GER4WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2s v16i8:$XA, v16i8:$XB)),
            (XVI16GER2SW RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2spp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI16GER2SWPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}

let Predicates = [MMA, IsNotISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2 v16i8:$XA, v16i8:$XB)),
            (XVF16GER2 RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2PN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2NP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2NN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}

let Predicates = [MMA, IsISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2 v16i8:$XA, v16i8:$XB)),
            (XVF16GER2W RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2WPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2WNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF16GER2WNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}

let Predicates = [MMA, IsNotISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvf32ger v16i8:$XA, v16i8:$XB)),
            (XVF32GER RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gerpp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gerpn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gernp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gernn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64ger v256i1:$XA, v16i8:$XB)),
            (XVF64GER $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gerpp v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERPP $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gerpn v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERPN $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gernp v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERNP $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gernn v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERNN $ATi, $XA, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2 v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2 RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2PN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2NP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2NN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2 v16i8:$XA, v16i8:$XB)),
            (XVI16GER2 RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4spp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI8GER4SPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}

let Predicates = [MMA, IsISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_xvf32ger v16i8:$XA, v16i8:$XB)),
            (XVF32GERW RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gerpp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERWPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gerpn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERWPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gernp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERWNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf32gernn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVF32GERWNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64ger v256i1:$XA, v16i8:$XB)),
            (XVF64GERW $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gerpp v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERWPP $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gerpn v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERWPN $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gernp v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERNP $ATi, $XA, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvf64gernn v512i1:$ATi, v256i1:$XA, v16i8:$XB)),
            (XVF64GERWNN $ATi, $XA, RCCp.BToVSRC)>;

  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2 v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2W RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2WPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2WNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvbf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVBF16GER2WNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2 v16i8:$XA, v16i8:$XB)),
            (XVI16GER2W RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
  def : Pat<(v512i1 (int_ppc_mma_xvi8ger4spp v512i1:$ATi, v16i8:$XA, v16i8:$XB)),
            (XVI8GER4WSPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC)>;
}
// MMA Intrinsics

let Predicates = [MMA, PrefixInstrs, IsNotISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_pmxvi4ger8 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK, Msk8Imm:$PMSK)),
            (PMXVI4GER8 RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK, Msk8Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi4ger8pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                              Msk8Imm:$PMSK)),
            (PMXVI4GER8PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk8Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK, Msk4Imm:$PMSK)),
            (PMXVI8GER4 RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK, Msk4Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                              Msk4Imm:$PMSK)),
            (PMXVI8GER4PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk4Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2s v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                              Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVI16GER2S RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2spp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVI16GER2SPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                             Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVF16GER2 RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                         Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2PN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2NP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2NN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvf32ger v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK)),
            (PMXVF32GER RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gerpp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gerpn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gernp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gernn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvf64ger v256i1:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk2Imm:$YMSK)),
            (PMXVF64GER $XA, RCCp.BToVSRC, Msk4Imm:$XMSK, Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gerpp v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERPP $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gerpn v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERPN $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gernp v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERNP $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gernn v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERNN $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                              Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVBF16GER2 RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2PN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2NP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2NN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                             Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVI16GER2 RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                         Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4spp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVI8GER4SPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVI16GER2PP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
}

let Predicates = [MMA, PrefixInstrs, IsISAFuture] in {
  def : Pat<(v512i1 (int_ppc_mma_pmxvi4ger8 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK, Msk8Imm:$PMSK)),
            (PMXVI4GER8W RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK, Msk8Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi4ger8pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                              Msk8Imm:$PMSK)),
            (PMXVI4GER8WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk8Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK, Msk4Imm:$PMSK)),
            (PMXVI8GER4W RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK, Msk4Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                              Msk4Imm:$PMSK)),
            (PMXVI8GER4WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk4Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2s v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                              Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVI16GER2SW RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2spp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVI16GER2SWPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                             Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVF16GER2W RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                         Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2WPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2WNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVF16GER2WNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvf32ger v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk4Imm:$YMSK)),
            (PMXVF32GERW RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                        Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gerpp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERWPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gerpn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERWPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gernp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERWNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf32gernn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk4Imm:$YMSK)),
            (PMXVF32GERWNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvf64ger v256i1:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                            Msk2Imm:$YMSK)),
            (PMXVF64GERW $XA, RCCp.BToVSRC, Msk4Imm:$XMSK, Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gerpp v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERWPP $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gerpn v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERWPN $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gernp v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERWNP $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvf64gernn v512i1:$ATi, v256i1:$XA, v16i8:$XB,
                                              Msk4Imm:$XMSK, Msk2Imm:$YMSK)),
            (PMXVF64GERWNN $ATi, $XA, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk2Imm:$YMSK)>;

  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                              Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVBF16GER2W RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                          Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2pn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2WPN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2np v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2WNP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvbf16ger2nn v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                                Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                                Msk2Imm:$PMSK)),
            (PMXVBF16GER2WNN $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                            Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2 v16i8:$XA, v16i8:$XB, Msk4Imm:$XMSK,
                                             Msk4Imm:$YMSK, Msk2Imm:$PMSK)),
            (PMXVI16GER2W RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                         Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi8ger4spp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVI8GER4WSPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
  def : Pat<(v512i1 (int_ppc_mma_pmxvi16ger2pp v512i1:$ATi, v16i8:$XA, v16i8:$XB,
                                               Msk4Imm:$XMSK, Msk4Imm:$YMSK,
                                               Msk2Imm:$PMSK)),
            (PMXVI16GER2WPP $ATi, RCCp.AToVSRC, RCCp.BToVSRC, Msk4Imm:$XMSK,
                           Msk4Imm:$YMSK, Msk2Imm:$PMSK)>;
}

def ConcatsMMA {
  dag VecsToVecPair0 =
    (v256i1 (INSERT_SUBREG
      (INSERT_SUBREG (IMPLICIT_DEF), $vs0, sub_vsx1),
      $vs1, sub_vsx0));
  dag VecsToVecPair1 =
    (v256i1 (INSERT_SUBREG
      (INSERT_SUBREG (IMPLICIT_DEF), $vs2, sub_vsx1),
      $vs3, sub_vsx0));
  dag VecsToVecQuad =
    (BUILD_UACC (INSERT_SUBREG
                  (INSERT_SUBREG (v512i1 (IMPLICIT_DEF)),
                                 (KILL_PAIR VecsToVecPair0), sub_pair0),
                  (KILL_PAIR VecsToVecPair1), sub_pair1));
}

def Extracts {
  dag Pair0 = (v256i1 (EXTRACT_SUBREG $v, sub_pair0));
  dag Pair1 = (v256i1 (EXTRACT_SUBREG $v, sub_pair1));
  dag Vec0 = (v4i32 (EXTRACT_SUBREG Pair0, sub_vsx0));
  dag Vec1 = (v4i32 (EXTRACT_SUBREG Pair0, sub_vsx1));
  dag Vec2 = (v4i32 (EXTRACT_SUBREG Pair1, sub_vsx0));
  dag Vec3 = (v4i32 (EXTRACT_SUBREG Pair1, sub_vsx1));
}

let Predicates = [MMA, IsNotISAFuture] in {
  def : Pat<(v512i1 (PPCAccBuild v4i32:$vs1, v4i32:$vs0, v4i32:$vs3, v4i32:$vs2)),
            (XXMTACC ConcatsMMA.VecsToVecQuad)>;
  def : Pat<(v512i1 (int_ppc_mma_assemble_acc v16i8:$vs1, v16i8:$vs0,
                                              v16i8:$vs3, v16i8:$vs2)),
            (XXMTACC ConcatsMMA.VecsToVecQuad)>;
  def : Pat<(v512i1 (PPCxxmfacc v512i1:$AS)), (XXMFACC acc:$AS)>;
  def : Pat<(v4i32 (PPCAccExtractVsx acc:$v, 0)),
            Extracts.Vec0>;
  def : Pat<(v4i32 (PPCAccExtractVsx acc:$v, 1)),
            Extracts.Vec1>;
  def : Pat<(v4i32 (PPCAccExtractVsx acc:$v, 2)),
            Extracts.Vec2>;
  def : Pat<(v4i32 (PPCAccExtractVsx acc:$v, 3)),
            Extracts.Vec3>;
}

let Predicates = [MMA, IsISAFuture] in {
  def : Pat<(v512i1 (PPCAccBuild v4i32:$vs1, v4i32:$vs0, v4i32:$vs3, v4i32:$vs2)),
            (DMXXINSTFDMR512 ConcatsMMA.VecsToVecPair0, ConcatsMMA.VecsToVecPair1)>;
  def : Pat<(v512i1 (int_ppc_mma_assemble_acc v16i8:$vs1, v16i8:$vs0,
                                              v16i8:$vs3, v16i8:$vs2)),
            (DMXXINSTFDMR512 ConcatsMMA.VecsToVecPair0, ConcatsMMA.VecsToVecPair1)>;
  def : Pat<(v512i1 immAllZerosV), (XXSETACCZW)>;
}
