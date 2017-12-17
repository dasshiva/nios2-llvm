//===-- Nios2ISelDAGToDAG.cpp - A Dag to Dag Inst Selector for Nios2 --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the NIOS2 target.
//
//===----------------------------------------------------------------------===//

#include "Nios2.h"
#include "Nios2MachineFunction.h"
#include "Nios2RegisterInfo.h"
#include "Nios2Subtarget.h"
#include "Nios2TargetMachine.h"
#include "Nios2TargetObjectFile.h"
#include "MCTargetDesc/Nios2BaseInfo.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "nios2-isel"

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Nios2DAGToDAGISel - NIOS2 specific code to select NIOS2 machine
// instructions for SelectionDAG operations.
//===----------------------------------------------------------------------===//
namespace llvm {

class Nios2DAGToDAGISel : public SelectionDAGISel {
public:
  explicit Nios2DAGToDAGISel(Nios2TargetMachine &tm) 
    : SelectionDAGISel(tm), Subtarget(nullptr) {}

  // Pass Name
  const char *getPassName() const override {
    return "NIOS2 DAG->DAG Pattern Instruction Selection";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

protected:
  /// Subtarget - Keep a pointer to the Nios2Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const Nios2Subtarget *Subtarget;

private:
  // Include the pieces autogenerated from the target description.
  #include "Nios2GenDAGISel.inc"

  SDNode *getGlobalBaseReg();

  SDNode *Select(SDNode *N);

  // Complex Pattern.
  bool SelectAddr(SDNode *Parent, SDValue N, SDValue &Base, SDValue &Offset);

  // getImm - Return a target constant with the specified value.
  inline SDValue getImm(const SDNode *Node, unsigned Imm) {
    return CurDAG->getTargetConstant(Imm, SDLoc(Node), Node->getValueType(0));
  }
  // getRegister - Return the register number
  inline SDValue getRegister(unsigned reg, MVT type) {
    return CurDAG->getRegister(reg, type);
  }

  void ProcessFunctionAfterISel(MachineFunction &MF);
  bool replaceUsesWithZeroReg(MachineRegisterInfo *MRI, const MachineInstr&);
  void InitGlobalBaseReg(MachineFunction &MF);

  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    unsigned ConstraintCode,
                                    std::vector<SDValue> &OutOps) override;
};

// Insert instructions to initialize the global base register in the
// first MBB of the function. When the ABI is O32 and the relocation model is
// PIC, the necessary instructions are emitted later to prevent optimization
// passes from moving them.
void Nios2DAGToDAGISel::InitGlobalBaseReg(MachineFunction &MF) {
  Nios2FunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();

  if (!Nios2FI->globalBaseRegSet())
    return;

  MachineBasicBlock &MBB = MF.front();
  MachineBasicBlock::iterator I = MBB.begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL = I != MBB.end() ? I->getDebugLoc() : DebugLoc();
  unsigned V0, GlobalBaseReg = Nios2FI->getGlobalBaseReg();
  const TargetRegisterClass *RC;

  RC = (const TargetRegisterClass*)&Nios2::CPURegsRegClass;

  V0 = RegInfo.createVirtualRegister(RC);

  if (MF.getTarget().getRelocationModel() == Reloc::Static) {
    // Set global register to __gnu_local_gp.
    //
    // lui   $v0, %hi(__gnu_local_gp)
    // addiu $globalbasereg, $v0, %lo(__gnu_local_gp)
    BuildMI(MBB, I, DL, TII.get(Nios2::ORhi), V0)
      .addExternalSymbol("__gnu_local_gp", Nios2II::MO_HIADJ16);
    BuildMI(MBB, I, DL, TII.get(Nios2::ADDi), GlobalBaseReg).addReg(V0)
      .addExternalSymbol("__gnu_local_gp", Nios2II::MO_LO16);
    return;
  }

  MF.getRegInfo().addLiveIn(Nios2::GP);
  MBB.addLiveIn(Nios2::GP);
  // For O32 ABI, the following instruction sequence is emitted to initialize
  // the global base register:
  //
  //  0. lui   $2, %hi(_gp_disp)
  //  1. addiu $2, $2, %lo(_gp_disp)
  //  2. addu  $globalbasereg, $2, $t9
  //
  // We emit only the last instruction here.
  //
  // GNU linker requires that the first two instructions appear at the beginning
  // of a function and no instructions be inserted before or between them.
  // The two instructions are emitted during lowering to MC layer in order to
  // avoid any reordering.
  //
  // Register $2 (Nios2::V0) is added to the list of live-in registers to ensure
  // the value instruction 1 (addiu) defines is valid when instruction 2 (addu)
  // reads it.
  MF.getRegInfo().addLiveIn(Nios2::R2);
  MBB.addLiveIn(Nios2::R2);
  BuildMI(MBB, I, DL, TII.get(Nios2::ADD), GlobalBaseReg)
    .addReg(Nios2::R2).addReg(Nios2::GP);
}

bool Nios2DAGToDAGISel::replaceUsesWithZeroReg(MachineRegisterInfo *MRI,
                                               const MachineInstr& MI) {
  unsigned DstReg = 0, ZeroReg = 0;

  // Check if MI is "addiu $dst, $zero, 0" or "daddiu $dst, $zero, 0".
  if ((MI.getOpcode() == Nios2::ADDi) &&
      (MI.getOperand(1).getReg() == Nios2::ZERO) &&
      (MI.getOperand(2).getImm() == 0)) {
    DstReg = MI.getOperand(0).getReg();
    ZeroReg = Nios2::ZERO;
  }

  if (!DstReg)
    return false;

  // Replace uses with ZeroReg.
  for (MachineRegisterInfo::use_iterator U = MRI->use_begin(DstReg),
       E = MRI->use_end(); U != E;) {
    MachineOperand &MO = *U;
    unsigned OpNo = U.getOperandNo();
    MachineInstr *MI = MO.getParent();
    ++U;

    // Do not replace if it is a phi's operand or is tied to def operand.
    if (MI->isPHI() || MI->isRegTiedToDefOperand(OpNo) || MI->isPseudo())
      continue;

    MO.setReg(ZeroReg);
  }

  return true;
}

void Nios2DAGToDAGISel::ProcessFunctionAfterISel(MachineFunction &MF) {
  InitGlobalBaseReg(MF);

  MachineRegisterInfo *MRI = &MF.getRegInfo();

  for (MachineFunction::iterator MFI = MF.begin(), MFE = MF.end(); MFI != MFE;
       ++MFI)
    for (MachineBasicBlock::iterator I = MFI->begin(); I != MFI->end(); ++I)
      replaceUsesWithZeroReg(MRI, *I);
}

bool Nios2DAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<Nios2Subtarget>();
  bool Ret = SelectionDAGISel::runOnMachineFunction(MF);

  ProcessFunctionAfterISel(MF);

  return Ret;
}

/// getGlobalBaseReg - Output the instructions required to put the
/// GOT address into a register.
SDNode *Nios2DAGToDAGISel::getGlobalBaseReg() {
  unsigned GlobalBaseReg = MF->getInfo<Nios2FunctionInfo>()->getGlobalBaseReg();
  return CurDAG->getRegister(GlobalBaseReg, getTargetLowering()->getPointerTy(CurDAG->getDataLayout())).getNode();
}

/// ComplexPattern used on Nios2InstrInfo
/// Used on Nios2 Load/Store instructions
bool Nios2DAGToDAGISel::
SelectAddr(SDNode *Parent, SDValue Addr, SDValue &Base, SDValue &Offset) {
  SDLoc DL(Addr);
  EVT ValTy = Addr.getValueType();

  // if Address is FI, get the TargetFrameIndex.
  if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>(Addr)) {
    Base   = CurDAG->getTargetFrameIndex(FIN->getIndex(), ValTy);
    Offset = CurDAG->getTargetConstant(0, DL, ValTy);
    return true;
  }

  // on PIC code Load GA
  if (Addr.getOpcode() == Nios2ISD::Wrapper) {
    Base   = Addr.getOperand(0);
    Offset = Addr.getOperand(1);
    return true;
  }

  if (TM.getRelocationModel() != Reloc::PIC_) {
    if ((Addr.getOpcode() == ISD::TargetExternalSymbol ||
        Addr.getOpcode() == ISD::TargetGlobalAddress))
      return false;
  }

  // Addresses of the form FI+const or FI|const
  if (CurDAG->isBaseWithConstantOffset(Addr)) {
    ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Addr.getOperand(1));
    if (isInt<16>(CN->getSExtValue())) {

      // If the first operand is a FI, get the TargetFI Node
      if (FrameIndexSDNode *FIN = dyn_cast<FrameIndexSDNode>
                                  (Addr.getOperand(0)))
        Base = CurDAG->getTargetFrameIndex(FIN->getIndex(), ValTy);
      else
        Base = Addr.getOperand(0);

      Offset = CurDAG->getTargetConstant(CN->getZExtValue(), DL, ValTy);
      return true;
    }
  }

  // Operand is a result from an ADD.
  if (Addr.getOpcode() == ISD::ADD) {
    // When loading from constant pools, load the lower address part in
    // the instruction itself. Example, instead of:
    //  lui $2, %hi($CPI1_0)
    //  addiu $2, $2, %lo($CPI1_0)
    //  lwc1 $f0, 0($2)
    // Generate:
    //  lui $2, %hi($CPI1_0)
    //  lwc1 $f0, %lo($CPI1_0)($2)
    if (Addr.getOperand(1).getOpcode() == Nios2ISD::Lo ||
        Addr.getOperand(1).getOpcode() == Nios2ISD::GPRel) {
      SDValue Opnd0 = Addr.getOperand(1).getOperand(0);
      if (isa<ConstantPoolSDNode>(Opnd0) || isa<GlobalAddressSDNode>(Opnd0) ||
          isa<JumpTableSDNode>(Opnd0)) {
        Base = Addr.getOperand(0);
        Offset = Opnd0;
        return true;
      }
    }
  }

  Base   = Addr;
  Offset = CurDAG->getTargetConstant(0, DL, ValTy);
  return true;
}

/// Select instructions not customized! Used for
/// expanded, promoted and normal instructions
SDNode* Nios2DAGToDAGISel::Select(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();
  SDLoc dl(Node);

  // Dump information about the Node being selected
  DEBUG(errs() << "Selecting: "; Node->dump(CurDAG); errs() << "\n");

  // If we have a custom node, we already have selected!
  if (Node->isMachineOpcode()) {
    DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    return NULL;
  }

  ///
  // Instruction Selection not handled by the auto-generated
  // tablegen selection should be handled here.
  ///

  switch(Opcode) {
  default: break;

  case ISD::SUBE:
  case ISD::ADDE: {
    SDValue InFlag = Node->getOperand(2), CmpLHS;
    unsigned Opc = InFlag.getOpcode(); (void)Opc;
    assert(((Opc == ISD::ADDC || Opc == ISD::ADDE) ||
            (Opc == ISD::SUBC || Opc == ISD::SUBE)) &&
           "(ADD|SUB)E flag operand must come from (ADD|SUB)C/E insn");

    unsigned MOp;
    if (Opcode == ISD::ADDE) {
      CmpLHS = InFlag.getValue(0);
      MOp = Nios2::ADD;
    } else {
      CmpLHS = InFlag.getOperand(0);
      MOp = Nios2::SUB;
    }

    SDValue Ops[] = { CmpLHS, InFlag.getOperand(1) };

    SDValue LHS = Node->getOperand(0);
    SDValue RHS = Node->getOperand(1);

    EVT VT = LHS.getValueType();
    SDNode *Carry = CurDAG->getMachineNode(Nios2::CMPLTu, dl, VT, Ops);
    SDNode *AddCarry = CurDAG->getMachineNode(Nios2::ADD, dl, VT,
                                              SDValue(Carry,0), RHS);

    return CurDAG->SelectNodeTo(Node, MOp, VT, MVT::Glue,
                                LHS, SDValue(AddCarry,0));
  }

  }

  // Select the default instruction
  SDNode *ResNode = SelectCode(Node);

  DEBUG(errs() << "=> ");
  if (ResNode == NULL || ResNode == Node)
    DEBUG(Node->dump(CurDAG));
  else
    DEBUG(ResNode->dump(CurDAG));
  DEBUG(errs() << "\n");
  return ResNode;
}

bool Nios2DAGToDAGISel::
SelectInlineAsmMemoryOperand(const SDValue &Op, unsigned ConstraintCode,
                             std::vector<SDValue> &OutOps) {
  assert(ConstraintCode == 'm' && "unexpected asm memory constraint");
  OutOps.push_back(Op);
  return false;
}

/// createNios2ISelDag - This pass converts a legalized DAG into a
/// NIOS2-specific DAG, ready for instruction scheduling.
FunctionPass *llvm::createNios2ISelDag(Nios2TargetMachine &TM) {
  return new Nios2DAGToDAGISel(TM);
}

} // namspace llvm
