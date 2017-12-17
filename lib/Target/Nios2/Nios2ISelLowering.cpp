//===-- Nios2ISelLowering.cpp - Nios2 DAG Lowering Implementation -----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that Nios2 uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "Nios2ISelLowering.h"
#include "Nios2MachineFunction.h"
#include "Nios2TargetMachine.h"
#include "Nios2Subtarget.h"
#include "InstPrinter/Nios2InstPrinter.h"
#include "MCTargetDesc/Nios2BaseInfo.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "nios2-lower"

// If I is a shifted mask, set the size (Size) and the first bit of the
// mask (Pos), and return true.
// For example, if I is 0x003ff800, (Pos, Size) = (11, 11).
//static bool IsShiftedMask(uint64_t I, uint64_t &Pos, uint64_t &Size) {
//  if (!isShiftedMask_64(I))
//     return false;
//
//  Size = CountPopulation_64(I);
//  Pos = CountTrailingZeros_64(I);
//  return true;
//}

static SDValue GetGlobalReg(SelectionDAG &DAG, EVT Ty) {
  Nios2FunctionInfo *FI = DAG.getMachineFunction().getInfo<Nios2FunctionInfo>();
  return DAG.getRegister(FI->getGlobalBaseReg(), Ty);
}

const char *Nios2TargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case Nios2ISD::Hi:                return "Nios2ISD::Hi";
  case Nios2ISD::Lo:                return "Nios2ISD::Lo";
  case Nios2ISD::GPRel:             return "Nios2ISD::GPRel";
  case Nios2ISD::Ret:               return "Nios2ISD::Ret";
  case Nios2ISD::Wrapper:           return "Nios2ISD::Wrapper";
  case Nios2ISD::JmpLink:           return "Nios2ISD::JmpLink";
  case Nios2ISD::Select:            return "Nios2ISD::Select";
  default:                          return NULL;
  }
}

Nios2TargetLowering::Nios2TargetLowering(const Nios2TargetMachine &TM, 
                                         const Nios2Subtarget &STI)
  : TargetLowering(TM), Subtarget(STI) {

  // Nios2 does not have i1 type, so use i32 for
  // setcc operations results (slt, sgt, ...).
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent); // FIXME: Is this correct?

  // Set up the register classes
  addRegisterClass(MVT::i32, &Nios2::CPURegsRegClass);

  // Load extented operations for i1 types must be promoted
  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
  }

  // Used by legalize types to correctly generate the setcc result.
  // Without this, every float setcc comes with a AND/OR with the result,
  // we don't want this, since the fpcmp result goes to a flag register,
  // which is used implicitly by brcond and select operations.
  AddPromotedToType(ISD::SETCC, MVT::i1, MVT::i32);

  // Nios2 Custom Operations
  setOperationAction(ISD::GlobalAddress,      MVT::i32,   Custom);
  setOperationAction(ISD::BlockAddress,       MVT::i32,   Custom);
  //setOperationAction(ISD::GlobalTLSAddress,   MVT::i32,   Custom);
  //setOperationAction(ISD::JumpTable,          MVT::i32,   Custom);
  //setOperationAction(ISD::ConstantPool,       MVT::i32,   Custom);
  setOperationAction(ISD::SELECT,             MVT::i32,   Expand);
  //setOperationAction(ISD::BRCOND,             MVT::Other, Custom);
  setOperationAction(ISD::VASTART,            MVT::Other, Custom);
  setOperationAction(ISD::ATOMIC_FENCE,       MVT::Other, Custom);

  setOperationAction(ISD::SREM, MVT::i32, Expand);
  setOperationAction(ISD::UREM, MVT::i32, Expand);
  setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  setOperationAction(ISD::ADDC, MVT::i32, Expand);
  setOperationAction(ISD::SUBC, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI,          MVT::i32,   Expand);
  setOperationAction(ISD::SMUL_LOHI,          MVT::i32,   Expand);
  setOperationAction(ISD::SHL_PARTS,          MVT::i32,   Custom);
  setOperationAction(ISD::SRA_PARTS,          MVT::i32,   Custom);
  setOperationAction(ISD::SRL_PARTS,          MVT::i32,   Custom);

  // Operations not directly supported by Nios2.
  setOperationAction(ISD::BR_JT,             MVT::Other, Expand);
  setOperationAction(ISD::BR_CC,             MVT::i32,   Expand);
  setOperationAction(ISD::SELECT_CC,         MVT::i32,   Custom);
  setOperationAction(ISD::UINT_TO_FP,        MVT::i32,   Expand);
  setOperationAction(ISD::FP_TO_UINT,        MVT::i32,   Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1,    Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8,    Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16,   Expand);
  setOperationAction(ISD::CTPOP,             MVT::i32,   Expand);
  setOperationAction(ISD::CTTZ,              MVT::i32,   Expand);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF,   MVT::i32,   Expand);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF,   MVT::i32,   Expand);
  setOperationAction(ISD::ROTL,              MVT::i32,   Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32,  Expand);

  setOperationAction(ISD::VAARG,             MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,            MVT::Other, Expand);
  setOperationAction(ISD::VAEND,             MVT::Other, Expand);

  // Use the default for now
  setOperationAction(ISD::STACKSAVE,         MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,      MVT::Other, Expand);

  setOperationAction(ISD::ATOMIC_LOAD,       MVT::i32,    Expand);
  setOperationAction(ISD::ATOMIC_STORE,      MVT::i32,    Expand);

  setInsertFencesForAtomic(true);

  setOperationAction(ISD::CTLZ, MVT::i32, Expand);
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  setMinFunctionAlignment(2);

  setStackPointerRegisterToSaveRestore(Nios2::SP);
  computeRegisterProperties(Subtarget.getRegisterInfo());

  //setExceptionPointerRegister(Nios2::EA);
  //setExceptionSelectorRegister(Nios2::R1);
 
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  MaxStoresPerMemcpy = 16;
}

bool Nios2TargetLowering::allowsUnalignedMemoryAccesses(EVT VT, bool *Fast) const {
  MVT::SimpleValueType SVT = VT.getSimpleVT().SimpleTy;
  switch (SVT) {
  case MVT::i32:
    if (Fast)
      *Fast = true;
    return true;
  default:
    return false;
  }
}

EVT Nios2TargetLowering::getSetCCResultType(const DataLayout &DL, 
                                            LLVMContext &Context,
                                            EVT VT) const {
  return MVT::i32;
}

//static SDValue PerformSELECTCombine(SDNode *N, SelectionDAG &DAG,
//                                    TargetLowering::DAGCombinerInfo &DCI,
//                                    const Nios2Subtarget *Subtarget) {
//  if (DCI.isBeforeLegalizeOps())
//    return SDValue();
//
//  SDValue SetCC = N->getOperand(0);
//
//  if ((SetCC.getOpcode() != ISD::SETCC) ||
//      !SetCC.getOperand(0).getValueType().isInteger())
//    return SDValue();
//
//  SDValue False = N->getOperand(2);
//  EVT FalseTy = False.getValueType();
//
//  if (!FalseTy.isInteger())
//    return SDValue();
//
//  ConstantSDNode *CN = dyn_cast<ConstantSDNode>(False);
//
//  if (!CN || CN->getZExtValue())
//    return SDValue();
//
//  const DebugLoc DL = N->getDebugLoc();
//  ISD::CondCode CC = cast<CondCodeSDNode>(SetCC.getOperand(2))->get();
//  SDValue True = N->getOperand(1);
//
//  SetCC = DAG.getSetCC(DL, SetCC.getValueType(), SetCC.getOperand(0),
//                       SetCC.getOperand(1), ISD::getSetCCInverse(CC, true));
//
//  return DAG.getNode(ISD::SELECT, DL, FalseTy, SetCC, False, True);
//}
//
//static SDValue PerformANDCombine(SDNode *N, SelectionDAG &DAG,
//                                 TargetLowering::DAGCombinerInfo &DCI,
//                                 const Nios2Subtarget *Subtarget) {
//  // Pattern match EXT.
//  //  $dst = and ((sra or srl) $src , pos), (2**size - 1)
//  //  => ext $dst, $src, size, pos
//  if (DCI.isBeforeLegalizeOps() || !Subtarget->hasNios232r2())
//    return SDValue();
//
//  SDValue ShiftRight = N->getOperand(0), Mask = N->getOperand(1);
//  unsigned ShiftRightOpc = ShiftRight.getOpcode();
//
//  // Op's first operand must be a shift right.
//  if (ShiftRightOpc != ISD::SRA && ShiftRightOpc != ISD::SRL)
//    return SDValue();
//
//  // The second operand of the shift must be an immediate.
//  ConstantSDNode *CN;
//  if (!(CN = dyn_cast<ConstantSDNode>(ShiftRight.getOperand(1))))
//    return SDValue();
//
//  uint64_t Pos = CN->getZExtValue();
//  uint64_t SMPos, SMSize;
//
//  // Op's second operand must be a shifted mask.
//  if (!(CN = dyn_cast<ConstantSDNode>(Mask)) ||
//      !IsShiftedMask(CN->getZExtValue(), SMPos, SMSize))
//    return SDValue();
//
//  // Return if the shifted mask does not start at bit 0 or the sum of its size
//  // and Pos exceeds the word's size.
//  EVT ValTy = N->getValueType(0);
//  if (SMPos != 0 || Pos + SMSize > ValTy.getSizeInBits())
//    return SDValue();
//
//  return DAG.getNode(Nios2ISD::Ext, N->getDebugLoc(), ValTy,
//                     ShiftRight.getOperand(0), DAG.getConstant(Pos, MVT::i32),
//                     DAG.getConstant(SMSize, MVT::i32));
//}
//
//static SDValue PerformORCombine(SDNode *N, SelectionDAG &DAG,
//                                TargetLowering::DAGCombinerInfo &DCI,
//                                const Nios2Subtarget *Subtarget) {
//  // Pattern match INS.
//  //  $dst = or (and $src1 , mask0), (and (shl $src, pos), mask1),
//  //  where mask1 = (2**size - 1) << pos, mask0 = ~mask1
//  //  => ins $dst, $src, size, pos, $src1
//  if (DCI.isBeforeLegalizeOps() || !Subtarget->hasNios232r2())
//    return SDValue();
//
//  SDValue And0 = N->getOperand(0), And1 = N->getOperand(1);
//  uint64_t SMPos0, SMSize0, SMPos1, SMSize1;
//  ConstantSDNode *CN;
//
//  // See if Op's first operand matches (and $src1 , mask0).
//  if (And0.getOpcode() != ISD::AND)
//    return SDValue();
//
//  if (!(CN = dyn_cast<ConstantSDNode>(And0.getOperand(1))) ||
//      !IsShiftedMask(~CN->getSExtValue(), SMPos0, SMSize0))
//    return SDValue();
//
//  // See if Op's second operand matches (and (shl $src, pos), mask1).
//  if (And1.getOpcode() != ISD::AND)
//    return SDValue();
//
//  if (!(CN = dyn_cast<ConstantSDNode>(And1.getOperand(1))) ||
//      !IsShiftedMask(CN->getZExtValue(), SMPos1, SMSize1))
//    return SDValue();
//
//  // The shift masks must have the same position and size.
//  if (SMPos0 != SMPos1 || SMSize0 != SMSize1)
//    return SDValue();
//
//  SDValue Shl = And1.getOperand(0);
//  if (Shl.getOpcode() != ISD::SHL)
//    return SDValue();
//
//  if (!(CN = dyn_cast<ConstantSDNode>(Shl.getOperand(1))))
//    return SDValue();
//
//  unsigned Shamt = CN->getZExtValue();
//
//  // Return if the shift amount and the first bit position of mask are not the
//  // same.
//  EVT ValTy = N->getValueType(0);
//  if ((Shamt != SMPos0) || (SMPos0 + SMSize0 > ValTy.getSizeInBits()))
//    return SDValue();
//
//  return DAG.getNode(Nios2ISD::Ins, N->getDebugLoc(), ValTy, Shl.getOperand(0),
//                     DAG.getConstant(SMPos0, MVT::i32),
//                     DAG.getConstant(SMSize0, MVT::i32), And0.getOperand(0));
//}
//
//static SDValue PerformADDCombine(SDNode *N, SelectionDAG &DAG,
//                                 TargetLowering::DAGCombinerInfo &DCI,
//                                 const Nios2Subtarget *Subtarget) {
//  // (add v0, (add v1, abs_lo(tjt))) => (add (add v0, v1), abs_lo(tjt))
//
//  if (DCI.isBeforeLegalizeOps())
//    return SDValue();
//
//  SDValue Add = N->getOperand(1);
//
//  if (Add.getOpcode() != ISD::ADD)
//    return SDValue();
//
//  SDValue Lo = Add.getOperand(1);
//
//  if ((Lo.getOpcode() != Nios2ISD::Lo) ||
//      (Lo.getOperand(0).getOpcode() != ISD::TargetJumpTable))
//    return SDValue();
//
//  EVT ValTy = N->getValueType(0);
//  DebugLoc DL = N->getDebugLoc();
//
//  SDValue Add1 = DAG.getNode(ISD::ADD, DL, ValTy, N->getOperand(0),
//                             Add.getOperand(0));
//  return DAG.getNode(ISD::ADD, DL, ValTy, Add1, Lo);
//}

SDValue Nios2TargetLowering::lowerShiftLeftParts(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0), Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);

  // if shamt < 32:
  //  lo = (shl lo, shamt)
  //  hi = (or (shl hi, shamt) (srl (srl lo, 1), ~shamt))
  // else:
  //  lo = 0
  //  hi = (shl lo, shamt[4:0])
  SDValue Not = DAG.getNode(ISD::XOR, DL, MVT::i32, Shamt,
                            DAG.getConstant(-1, DL, MVT::i32));
  SDValue ShiftRight1Lo = DAG.getNode(ISD::SRL, DL, MVT::i32, Lo,
                                      DAG.getConstant(1, DL, MVT::i32));
  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, MVT::i32, ShiftRight1Lo,
                                     Not);
  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, MVT::i32, Hi, Shamt);
  SDValue Or = DAG.getNode(ISD::OR, DL, MVT::i32, ShiftLeftHi, ShiftRightLo);
  SDValue ShiftLeftLo = DAG.getNode(ISD::SHL, DL, MVT::i32, Lo, Shamt);
  SDValue Cond = DAG.getNode(ISD::AND, DL, MVT::i32, Shamt,
                             DAG.getConstant(0x20, DL, MVT::i32));
  Lo = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond,
                   DAG.getConstant(0, DL, MVT::i32), ShiftLeftLo);
  Hi = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond, ShiftLeftLo, Or);

  SDValue Ops[2] = {Lo, Hi};
  return DAG.getMergeValues(Ops, DL);
}

SDValue Nios2TargetLowering::lowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
                                                 bool IsSRA) const {
  SDLoc DL(Op);
  SDValue Lo = Op.getOperand(0), Hi = Op.getOperand(1);
  SDValue Shamt = Op.getOperand(2);

  // if shamt < 32:
  //  lo = (or (shl (shl hi, 1), ~shamt) (srl lo, shamt))
  //  if isSRA:
  //    hi = (sra hi, shamt)
  //  else:
  //    hi = (srl hi, shamt)
  // else:
  //  if isSRA:
  //   lo = (sra hi, shamt[4:0])
  //   hi = (sra hi, 31)
  //  else:
  //   lo = (srl hi, shamt[4:0])
  //   hi = 0
  SDValue Not = DAG.getNode(ISD::XOR, DL, MVT::i32, Shamt,
                            DAG.getConstant(-1, DL, MVT::i32));
  SDValue ShiftLeft1Hi = DAG.getNode(ISD::SHL, DL, MVT::i32, Hi,
                                     DAG.getConstant(1, DL, MVT::i32));
  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, MVT::i32, ShiftLeft1Hi, Not);
  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, MVT::i32, Lo, Shamt);
  SDValue Or = DAG.getNode(ISD::OR, DL, MVT::i32, ShiftLeftHi, ShiftRightLo);
  SDValue ShiftRightHi = DAG.getNode(IsSRA ? ISD::SRA : ISD::SRL, DL, MVT::i32,
                                     Hi, Shamt);
  SDValue Cond = DAG.getNode(ISD::AND, DL, MVT::i32, Shamt,
                             DAG.getConstant(0x20, DL, MVT::i32));
  SDValue Shift31 = DAG.getNode(ISD::SRA, DL, MVT::i32, Hi,
                                DAG.getConstant(31, DL, MVT::i32));
  Lo = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond, ShiftRightHi, Or);
  Hi = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond,
                   IsSRA ? Shift31 : DAG.getConstant(0, DL, MVT::i32),
                   ShiftRightHi);

  SDValue Ops[2] = {Lo, Hi};
  return DAG.getMergeValues(Ops, DL);
}

SDValue Nios2TargetLowering::lowerSELECT_CC(SDValue Op,
    SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Cond = DAG.getNode(ISD::SETCC, DL,
                             MVT::i32,
                             Op.getOperand(0), Op.getOperand(1),
                             Op.getOperand(4));
  // Wrap select nodes
  return DAG.getNode(Nios2ISD::Select, DL, Op.getValueType(), Cond, Op.getOperand(2),
                     Op.getOperand(3));
}

SDValue Nios2TargetLowering::
LowerOperation(SDValue Op, SelectionDAG &DAG) const
{
  switch (Op.getOpcode())
  {
    //case ISD::BRCOND:             return LowerBRCOND(Op, DAG);
    case ISD::ConstantPool:       return LowerConstantPool(Op, DAG);
    case ISD::GlobalAddress:      return lowerGlobalAddress(Op, DAG);
    case ISD::SHL_PARTS:          return lowerShiftLeftParts(Op, DAG);
    case ISD::SRA_PARTS:          return lowerShiftRightParts(Op, DAG, true);
    case ISD::SRL_PARTS:          return lowerShiftRightParts(Op, DAG, false);
    //case ISD::BlockAddress:       return LowerBlockAddress(Op, DAG);
    //case ISD::GlobalTLSAddress:   return LowerGlobalTLSAddress(Op, DAG);
    //case ISD::JumpTable:          return LowerJumpTable(Op, DAG);
    //case ISD::SELECT:             return LowerSELECT(Op, DAG);
    case ISD::SELECT_CC:          return lowerSELECT_CC(Op, DAG);
    //case ISD::SETCC:              return LowerSETCC(Op, DAG);
    case ISD::VASTART:            return LowerVASTART(Op, DAG);
    //case ISD::FCOPYSIGN:          return LowerFCOPYSIGN(Op, DAG);
    //case ISD::FRAMEADDR:          return LowerFRAMEADDR(Op, DAG);
    //case ISD::RETURNADDR:         return LowerRETURNADDR(Op, DAG);
    case ISD::ATOMIC_FENCE:       return LowerATOMIC_FENCE(Op, DAG);
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Lower helper functions
//===----------------------------------------------------------------------===//

// AddLiveIn - This helper function adds the specified physical register to the
// MachineFunction as a live in value.  It also creates a corresponding
// virtual register for it.
static unsigned
addLiveIn(MachineFunction &MF, unsigned PReg, const TargetRegisterClass *RC)
{
  assert(RC->contains(PReg) && "Not the correct regclass!");
  unsigned VReg = MF.getRegInfo().createVirtualRegister(RC);
  MF.getRegInfo().addLiveIn(PReg, VReg);
  return VReg;
}

SDValue Nios2TargetLowering::lowerGlobalAddress(SDValue Op,
                                               SelectionDAG &DAG) const {
  // FIXME there isn't actually debug info here
  SDLoc dl(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  // %hi/%lo relocation
  SDValue GAHi = DAG.getTargetGlobalAddress(GV, dl, MVT::i32, 0,
                                            Nios2II::MO_HIADJ16);
  SDValue GALo = DAG.getTargetGlobalAddress(GV, dl, MVT::i32, 0,
                                            Nios2II::MO_LO16);
  SDValue HiPart = DAG.getNode(Nios2ISD::Hi, dl, MVT::i32, GAHi);
  SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, MVT::i32, GALo);
  return DAG.getNode(ISD::ADD, dl, MVT::i32, HiPart, Lo);
}

//SDValue Nios2TargetLowering::LowerBlockAddress(SDValue Op,
//                                              SelectionDAG &DAG) const {
//  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
//  // FIXME there isn't actually debug info here
//  DebugLoc dl = Op.getDebugLoc();
//
//  if (getTargetMachine().getRelocationModel() != Reloc::PIC_ && !IsN64) {
//    // %hi/%lo relocation
//    SDValue BAHi = DAG.getTargetBlockAddress(BA, MVT::i32, 0, Nios2II::MO_ABS_HI);
//    SDValue BALo = DAG.getTargetBlockAddress(BA, MVT::i32, 0, Nios2II::MO_ABS_LO);
//    SDValue Hi = DAG.getNode(Nios2ISD::Hi, dl, MVT::i32, BAHi);
//    SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, MVT::i32, BALo);
//    return DAG.getNode(ISD::ADD, dl, MVT::i32, Hi, Lo);
//  }
//
//  EVT ValTy = Op.getValueType();
//  unsigned GOTFlag = HasNios264 ? Nios2II::MO_GOT_PAGE : Nios2II::MO_GOT;
//  unsigned OFSTFlag = HasNios264 ? Nios2II::MO_GOT_OFST : Nios2II::MO_ABS_LO;
//  SDValue BAGOTOffset = DAG.getTargetBlockAddress(BA, ValTy, 0, GOTFlag);
//  BAGOTOffset = DAG.getNode(Nios2ISD::Wrapper, dl, ValTy,
//                            GetGlobalReg(DAG, ValTy), BAGOTOffset);
//  SDValue BALOOffset = DAG.getTargetBlockAddress(BA, ValTy, 0, OFSTFlag);
//  SDValue Load = DAG.getLoad(ValTy, dl, DAG.getEntryNode(), BAGOTOffset,
//                             MachinePointerInfo(), false, false, false, 0);
//  SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, ValTy, BALOOffset);
//  return DAG.getNode(ISD::ADD, dl, ValTy, Load, Lo);
//}

//SDValue Nios2TargetLowering::
//LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const
//{
//  // If the relocation model is PIC, use the General Dynamic TLS Model or
//  // Local Dynamic TLS model, otherwise use the Initial Exec or
//  // Local Exec TLS Model.
//
//  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
//  DebugLoc dl = GA->getDebugLoc();
//  const GlobalValue *GV = GA->getGlobal();
//  EVT PtrVT = getPointerTy();
//
//  TLSModel::Model model = getTargetMachine().getTLSModel(GV);
//
//  if (model == TLSModel::GeneralDynamic || model == TLSModel::LocalDynamic) {
//    // General Dynamic and Local Dynamic TLS Model.
//    unsigned Flag = (model == TLSModel::LocalDynamic) ? Nios2II::MO_TLSLDM
//                                                      : Nios2II::MO_TLSGD;
//
//    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, Flag);
//    SDValue Argument = DAG.getNode(Nios2ISD::Wrapper, dl, PtrVT,
//                                   GetGlobalReg(DAG, PtrVT), TGA);
//    unsigned PtrSize = PtrVT.getSizeInBits();
//    IntegerType *PtrTy = Type::getIntNTy(*DAG.getContext(), PtrSize);
//
//    SDValue TlsGetAddr = DAG.getExternalSymbol("__tls_get_addr", PtrVT);
//
//    ArgListTy Args;
//    ArgListEntry Entry;
//    Entry.Node = Argument;
//    Entry.Ty = PtrTy;
//    Args.push_back(Entry);
//
//    TargetLowering::CallLoweringInfo CLI(DAG.getEntryNode(), PtrTy,
//                  false, false, false, false, 0, CallingConv::C,
//                  /*isTailCall=*/false, /*doesNotRet=*/false,
//                  /*isReturnValueUsed=*/true,
//                  TlsGetAddr, Args, DAG, dl);
//    std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
//
//    SDValue Ret = CallResult.first;
//
//    if (model != TLSModel::LocalDynamic)
//      return Ret;
//
//    SDValue TGAHi = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
//                                               Nios2II::MO_DTPREL_HI);
//    SDValue Hi = DAG.getNode(Nios2ISD::Hi, dl, PtrVT, TGAHi);
//    SDValue TGALo = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
//                                               Nios2II::MO_DTPREL_LO);
//    SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, PtrVT, TGALo);
//    SDValue Add = DAG.getNode(ISD::ADD, dl, PtrVT, Hi, Ret);
//    return DAG.getNode(ISD::ADD, dl, PtrVT, Add, Lo);
//  }
//
//  SDValue Offset;
//  if (model == TLSModel::InitialExec) {
//    // Initial Exec TLS Model
//    SDValue TGA = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
//                                             Nios2II::MO_GOTTPREL);
//    TGA = DAG.getNode(Nios2ISD::Wrapper, dl, PtrVT, GetGlobalReg(DAG, PtrVT),
//                      TGA);
//    Offset = DAG.getLoad(PtrVT, dl,
//                         DAG.getEntryNode(), TGA, MachinePointerInfo(),
//                         false, false, false, 0);
//  } else {
//    // Local Exec TLS Model
//    assert(model == TLSModel::LocalExec);
//    SDValue TGAHi = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
//                                               Nios2II::MO_TPREL_HI);
//    SDValue TGALo = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0,
//                                               Nios2II::MO_TPREL_LO);
//    SDValue Hi = DAG.getNode(Nios2ISD::Hi, dl, PtrVT, TGAHi);
//    SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, PtrVT, TGALo);
//    Offset = DAG.getNode(ISD::ADD, dl, PtrVT, Hi, Lo);
//  }
//
//  SDValue ThreadPointer = DAG.getNode(Nios2ISD::ThreadPointer, dl, PtrVT);
//  return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer, Offset);
//}

//SDValue Nios2TargetLowering::
//LowerJumpTable(SDValue Op, SelectionDAG &DAG) const
//{
//  SDValue HiPart, JTI, JTILo;
//  // FIXME there isn't actually debug info here
//  DebugLoc dl = Op.getDebugLoc();
//  bool IsPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;
//  EVT PtrVT = Op.getValueType();
//  JumpTableSDNode *JT = cast<JumpTableSDNode>(Op);
//
//  if (!IsPIC && !IsN64) {
//    JTI = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, Nios2II::MO_ABS_HI);
//    HiPart = DAG.getNode(Nios2ISD::Hi, dl, PtrVT, JTI);
//    JTILo = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, Nios2II::MO_ABS_LO);
//  } else {// Emit Load from Global Pointer
//    unsigned GOTFlag = HasNios264 ? Nios2II::MO_GOT_PAGE : Nios2II::MO_GOT;
//    unsigned OfstFlag = HasNios264 ? Nios2II::MO_GOT_OFST : Nios2II::MO_ABS_LO;
//    JTI = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, GOTFlag);
//    JTI = DAG.getNode(Nios2ISD::Wrapper, dl, PtrVT, GetGlobalReg(DAG, PtrVT),
//                      JTI);
//    HiPart = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), JTI,
//                         MachinePointerInfo(), false, false, false, 0);
//    JTILo = DAG.getTargetJumpTable(JT->getIndex(), PtrVT, OfstFlag);
//  }
//
//  SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, PtrVT, JTILo);
//  return DAG.getNode(ISD::ADD, dl, PtrVT, HiPart, Lo);
//}

SDValue Nios2TargetLowering::
LowerConstantPool(SDValue Op, SelectionDAG &DAG) const
{
  SDValue ResNode;
  ConstantPoolSDNode *N = cast<ConstantPoolSDNode>(Op);
  const Constant *C = N->getConstVal();
  // FIXME there isn't actually debug info here
  SDLoc dl(Op);

  // gp_rel relocation
  // FIXME: we should reference the constant pool using small data sections,
  // but the asm printer currently doesn't support this feature without
  // hacking it. This feature should come soon so we can uncomment the
  // stuff below.
  //if (IsInSmallSection(C->getType())) {
  //  SDValue GPRelNode = DAG.getNode(Nios2ISD::GPRel, MVT::i32, CP);
  //  SDValue GOT = DAG.getGLOBAL_OFFSET_TABLE(MVT::i32);
  //  ResNode = DAG.getNode(ISD::ADD, MVT::i32, GOT, GPRelNode);

  if (getTargetMachine().getRelocationModel() != Reloc::PIC_) {
    SDValue CPHi = DAG.getTargetConstantPool(C, MVT::i32, N->getAlignment(),
                                             N->getOffset(), Nios2II::MO_HIADJ16);
    SDValue CPLo = DAG.getTargetConstantPool(C, MVT::i32, N->getAlignment(),
                                             N->getOffset(), Nios2II::MO_LO16);
    SDValue HiPart = DAG.getNode(Nios2ISD::Hi, dl, MVT::i32, CPHi);
    SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, MVT::i32, CPLo);
    ResNode = DAG.getNode(ISD::ADD, dl, MVT::i32, HiPart, Lo);
  } else {
    EVT ValTy = Op.getValueType();
    unsigned GOTFlag = Nios2II::MO_GOT;
    unsigned OFSTFlag = Nios2II::MO_LO16;
    SDValue CP = DAG.getTargetConstantPool(C, ValTy, N->getAlignment(),
                                           N->getOffset(), GOTFlag);
    CP = DAG.getNode(Nios2ISD::Wrapper, dl, ValTy, GetGlobalReg(DAG, ValTy), CP);
    SDValue Load = DAG.getLoad(ValTy, dl, DAG.getEntryNode(), CP,
                               MachinePointerInfo::getConstantPool(DAG.getMachineFunction()), false,
                               false, false, 0);
    SDValue CPLo = DAG.getTargetConstantPool(C, ValTy, N->getAlignment(),
                                             N->getOffset(), OFSTFlag);
    SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, ValTy, CPLo);
    ResNode = DAG.getNode(ISD::ADD, dl, ValTy, Load, Lo);
  }

  return ResNode;
}

SDValue Nios2TargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  Nios2FunctionInfo *FuncInfo = MF.getInfo<Nios2FunctionInfo>();

  SDLoc dl(Op);
  SDValue FI = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(),
                                 getPointerTy(DAG.getDataLayout()));

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, FI, Op.getOperand(1),
                      MachinePointerInfo(SV), false, false, 0);
}

//SDValue Nios2TargetLowering::
//LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
//  // check the depth
//  assert((cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue() == 0) &&
//         "Frame address can only be determined for current frame.");
//
//  MachineFrameInfo *MFI = DAG.getMachineFunction().getFrameInfo();
//  MFI->setFrameAddressIsTaken(true);
//  EVT VT = Op.getValueType();
//  DebugLoc dl = Op.getDebugLoc();
//  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl,
//                                         IsN64 ? Nios2::FP_64 : Nios2::FP, VT);
//  return FrameAddr;
//}
//
//SDValue Nios2TargetLowering::LowerRETURNADDR(SDValue Op,
//                                            SelectionDAG &DAG) const {
//  // check the depth
//  assert((cast<ConstantSDNode>(Op.getOperand(0))->getZExtValue() == 0) &&
//         "Return address can be determined only for current frame.");
//
//  MachineFunction &MF = DAG.getMachineFunction();
//  MachineFrameInfo *MFI = MF.getFrameInfo();
//  EVT VT = Op.getValueType();
//  unsigned RA = IsN64 ? Nios2::RA_64 : Nios2::RA;
//  MFI->setReturnAddressIsTaken(true);
//
//  // Return RA, which contains the return address. Mark it an implicit live-in.
//  unsigned Reg = MF.addLiveIn(RA, getRegClassFor(VT));
//  return DAG.getCopyFromReg(DAG.getEntryNode(), Op.getDebugLoc(), Reg, VT);
//}
//

SDValue Nios2TargetLowering::LowerATOMIC_FENCE(SDValue Op,
                                              SelectionDAG &DAG) const {
  // FIXME: Need pseudo-fence for 'singlethread' fences
  // FIXME: Set SType for weaker fences where supported/appropriate.
//  unsigned SType = 0;
  SDLoc dl(Op);
  return DAG.getNode(Nios2ISD::Sync, dl, MVT::Other, Op.getOperand(0));
//  return DAG.getNode(Nios2ISD::Sync, dl, MVT::Other, Op.getOperand(0),
//                     DAG.getConstant(SType, MVT::i32));
}

//SDValue Nios2TargetLowering::LowerShiftLeftParts(SDValue Op,
//                                                SelectionDAG &DAG) const {
//  DebugLoc DL = Op.getDebugLoc();
//  SDValue Lo = Op.getOperand(0), Hi = Op.getOperand(1);
//  SDValue Shamt = Op.getOperand(2);
//
//  // if shamt < 32:
//  //  lo = (shl lo, shamt)
//  //  hi = (or (shl hi, shamt) (srl (srl lo, 1), ~shamt))
//  // else:
//  //  lo = 0
//  //  hi = (shl lo, shamt[4:0])
//  SDValue Not = DAG.getNode(ISD::XOR, DL, MVT::i32, Shamt,
//                            DAG.getConstant(-1, MVT::i32));
//  SDValue ShiftRight1Lo = DAG.getNode(ISD::SRL, DL, MVT::i32, Lo,
//                                      DAG.getConstant(1, MVT::i32));
//  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, MVT::i32, ShiftRight1Lo,
//                                     Not);
//  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, MVT::i32, Hi, Shamt);
//  SDValue Or = DAG.getNode(ISD::OR, DL, MVT::i32, ShiftLeftHi, ShiftRightLo);
//  SDValue ShiftLeftLo = DAG.getNode(ISD::SHL, DL, MVT::i32, Lo, Shamt);
//  SDValue Cond = DAG.getNode(ISD::AND, DL, MVT::i32, Shamt,
//                             DAG.getConstant(0x20, MVT::i32));
//  Lo = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond,
//                   DAG.getConstant(0, MVT::i32), ShiftLeftLo);
//  Hi = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond, ShiftLeftLo, Or);
//
//  SDValue Ops[2] = {Lo, Hi};
//  return DAG.getMergeValues(Ops, 2, DL);
//}
//
//SDValue Nios2TargetLowering::LowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
//                                                 bool IsSRA) const {
//  DebugLoc DL = Op.getDebugLoc();
//  SDValue Lo = Op.getOperand(0), Hi = Op.getOperand(1);
//  SDValue Shamt = Op.getOperand(2);
//
//  // if shamt < 32:
//  //  lo = (or (shl (shl hi, 1), ~shamt) (srl lo, shamt))
//  //  if isSRA:
//  //    hi = (sra hi, shamt)
//  //  else:
//  //    hi = (srl hi, shamt)
//  // else:
//  //  if isSRA:
//  //   lo = (sra hi, shamt[4:0])
//  //   hi = (sra hi, 31)
//  //  else:
//  //   lo = (srl hi, shamt[4:0])
//  //   hi = 0
//  SDValue Not = DAG.getNode(ISD::XOR, DL, MVT::i32, Shamt,
//                            DAG.getConstant(-1, MVT::i32));
//  SDValue ShiftLeft1Hi = DAG.getNode(ISD::SHL, DL, MVT::i32, Hi,
//                                     DAG.getConstant(1, MVT::i32));
//  SDValue ShiftLeftHi = DAG.getNode(ISD::SHL, DL, MVT::i32, ShiftLeft1Hi, Not);
//  SDValue ShiftRightLo = DAG.getNode(ISD::SRL, DL, MVT::i32, Lo, Shamt);
//  SDValue Or = DAG.getNode(ISD::OR, DL, MVT::i32, ShiftLeftHi, ShiftRightLo);
//  SDValue ShiftRightHi = DAG.getNode(IsSRA ? ISD::SRA : ISD::SRL, DL, MVT::i32,
//                                     Hi, Shamt);
//  SDValue Cond = DAG.getNode(ISD::AND, DL, MVT::i32, Shamt,
//                             DAG.getConstant(0x20, MVT::i32));
//  SDValue Shift31 = DAG.getNode(ISD::SRA, DL, MVT::i32, Hi,
//                                DAG.getConstant(31, MVT::i32));
//  Lo = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond, ShiftRightHi, Or);
//  Hi = DAG.getNode(ISD::SELECT, DL, MVT::i32, Cond,
//                   IsSRA ? Shift31 : DAG.getConstant(0, MVT::i32),
//                   ShiftRightHi);
//
//  SDValue Ops[2] = {Lo, Hi};
//  return DAG.getMergeValues(Ops, 2, DL);
//}
//
//static SDValue CreateLoadLR(unsigned Opc, SelectionDAG &DAG, LoadSDNode *LD,
//                            SDValue Chain, SDValue Src, unsigned Offset) {
//  SDValue Ptr = LD->getBasePtr();
//  EVT VT = LD->getValueType(0), MemVT = LD->getMemoryVT();
//  EVT BasePtrVT = Ptr.getValueType();
//  DebugLoc DL = LD->getDebugLoc();
//  SDVTList VTList = DAG.getVTList(VT, MVT::Other);
//
//  if (Offset)
//    Ptr = DAG.getNode(ISD::ADD, DL, BasePtrVT, Ptr,
//                      DAG.getConstant(Offset, BasePtrVT));
//
//  SDValue Ops[] = { Chain, Ptr, Src };
//  return DAG.getMemIntrinsicNode(Opc, DL, VTList, Ops, 3, MemVT,
//                                 LD->getMemOperand());
//}
//
//// Expand an unaligned 32 or 64-bit integer load node.
//SDValue Nios2TargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
//  LoadSDNode *LD = cast<LoadSDNode>(Op);
//  EVT MemVT = LD->getMemoryVT();
//
//  // Return if load is aligned or if MemVT is neither i32 nor i64.
//  if ((LD->getAlignment() >= MemVT.getSizeInBits() / 8) ||
//      ((MemVT != MVT::i32) && (MemVT != MVT::i64)))
//    return SDValue();
//
//  bool IsLittle = Subtarget->isLittle();
//  EVT VT = Op.getValueType();
//  ISD::LoadExtType ExtType = LD->getExtensionType();
//  SDValue Chain = LD->getChain(), Undef = DAG.getUNDEF(VT);
//
//  assert((VT == MVT::i32) || (VT == MVT::i64));
//
//  // Expand
//  //  (set dst, (i64 (load baseptr)))
//  // to
//  //  (set tmp, (ldl (add baseptr, 7), undef))
//  //  (set dst, (ldr baseptr, tmp))
//  if ((VT == MVT::i64) && (ExtType == ISD::NON_EXTLOAD)) {
//    SDValue LDL = CreateLoadLR(Nios2ISD::LDL, DAG, LD, Chain, Undef,
//                               IsLittle ? 7 : 0);
//    return CreateLoadLR(Nios2ISD::LDR, DAG, LD, LDL.getValue(1), LDL,
//                        IsLittle ? 0 : 7);
//  }
//
//  SDValue LWL = CreateLoadLR(Nios2ISD::LWL, DAG, LD, Chain, Undef,
//                             IsLittle ? 3 : 0);
//  SDValue LWR = CreateLoadLR(Nios2ISD::LWR, DAG, LD, LWL.getValue(1), LWL,
//                             IsLittle ? 0 : 3);
//
//  // Expand
//  //  (set dst, (i32 (load baseptr))) or
//  //  (set dst, (i64 (sextload baseptr))) or
//  //  (set dst, (i64 (extload baseptr)))
//  // to
//  //  (set tmp, (lwl (add baseptr, 3), undef))
//  //  (set dst, (lwr baseptr, tmp))
//  if ((VT == MVT::i32) || (ExtType == ISD::SEXTLOAD) ||
//      (ExtType == ISD::EXTLOAD))
//    return LWR;
//
//  assert((VT == MVT::i64) && (ExtType == ISD::ZEXTLOAD));
//
//  // Expand
//  //  (set dst, (i64 (zextload baseptr)))
//  // to
//  //  (set tmp0, (lwl (add baseptr, 3), undef))
//  //  (set tmp1, (lwr baseptr, tmp0))
//  //  (set tmp2, (shl tmp1, 32))
//  //  (set dst, (srl tmp2, 32))
//  DebugLoc DL = LD->getDebugLoc();
//  SDValue Const32 = DAG.getConstant(32, MVT::i32);
//  SDValue SLL = DAG.getNode(ISD::SHL, DL, MVT::i64, LWR, Const32);
//  SDValue SRL = DAG.getNode(ISD::SRL, DL, MVT::i64, SLL, Const32);
//  SDValue Ops[] = { SRL, LWR.getValue(1) };
//  return DAG.getMergeValues(Ops, 2, DL);
//}
//
//static SDValue CreateStoreLR(unsigned Opc, SelectionDAG &DAG, StoreSDNode *SD,
//                             SDValue Chain, unsigned Offset) {
//  SDValue Ptr = SD->getBasePtr(), Value = SD->getValue();
//  EVT MemVT = SD->getMemoryVT(), BasePtrVT = Ptr.getValueType();
//  DebugLoc DL = SD->getDebugLoc();
//  SDVTList VTList = DAG.getVTList(MVT::Other);
//
//  if (Offset)
//    Ptr = DAG.getNode(ISD::ADD, DL, BasePtrVT, Ptr,
//                      DAG.getConstant(Offset, BasePtrVT));
//
//  SDValue Ops[] = { Chain, Value, Ptr };
//  return DAG.getMemIntrinsicNode(Opc, DL, VTList, Ops, 3, MemVT,
//                                 SD->getMemOperand());
//}
//
//// Expand an unaligned 32 or 64-bit integer store node.
//SDValue Nios2TargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
//  StoreSDNode *SD = cast<StoreSDNode>(Op);
//  EVT MemVT = SD->getMemoryVT();
//
//  // Return if store is aligned or if MemVT is neither i32 nor i64.
//  if ((SD->getAlignment() >= MemVT.getSizeInBits() / 8) ||
//      ((MemVT != MVT::i32) && (MemVT != MVT::i64)))
//    return SDValue();
//
//  bool IsLittle = Subtarget->isLittle();
//  SDValue Value = SD->getValue(), Chain = SD->getChain();
//  EVT VT = Value.getValueType();
//
//  // Expand
//  //  (store val, baseptr) or
//  //  (truncstore val, baseptr)
//  // to
//  //  (swl val, (add baseptr, 3))
//  //  (swr val, baseptr)
//  if ((VT == MVT::i32) || SD->isTruncatingStore()) {
//    SDValue SWL = CreateStoreLR(Nios2ISD::SWL, DAG, SD, Chain,
//                                IsLittle ? 3 : 0);
//    return CreateStoreLR(Nios2ISD::SWR, DAG, SD, SWL, IsLittle ? 0 : 3);
//  }
//
//  assert(VT == MVT::i64);
//
//  // Expand
//  //  (store val, baseptr)
//  // to
//  //  (sdl val, (add baseptr, 7))
//  //  (sdr val, baseptr)
//  SDValue SDL = CreateStoreLR(Nios2ISD::SDL, DAG, SD, Chain, IsLittle ? 7 : 0);
//  return CreateStoreLR(Nios2ISD::SDR, DAG, SD, SDL, IsLittle ? 0 : 7);
//}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// TODO: Implement a generic logic using tblgen that can support this.
// Nios2 O32 ABI rules:
// ---
// i32 - Passed in A0, A1, A2, A3 and stack
// f32 - Only passed in f32 registers if no int reg has been used yet to hold
//       an argument. Otherwise, passed in A1, A2, A3 and stack.
// f64 - Only passed in two aliased f32 registers if no int reg has been used
//       yet to hold an argument. Otherwise, use A2, A3 and stack. If A1 is
//       not used, it must be shadowed. If only A3 is avaiable, shadow it and
//       go to stack.
//
//  For vararg functions, all arguments are passed in A0, A1, A2, A3 and stack.
//===----------------------------------------------------------------------===//

//static bool CC_Nios2O32(unsigned ValNo, MVT ValVT,
//                       MVT LocVT, CCValAssign::LocInfo LocInfo,
//                       ISD::ArgFlagsTy ArgFlags, CCState &State) {
//
//  static const unsigned IntRegsSize=4, FloatRegsSize=2;
//
//  static const uint16_t IntRegs[] = {
//      Nios2::A0, Nios2::A1, Nios2::A2, Nios2::A3
//  };
//  // ByVal Args
//  if (ArgFlags.isByVal()) {
//    State.HandleByVal(ValNo, ValVT, LocVT, LocInfo,
//                      1 /*MinSize*/, 4 /*MinAlign*/, ArgFlags);
//    unsigned NextReg = (State.getNextStackOffset() + 3) / 4;
//    for (unsigned r = State.getFirstUnallocated(IntRegs, IntRegsSize);
//         r < std::min(IntRegsSize, NextReg); ++r)
//      State.AllocateReg(IntRegs[r]);
//    return false;
//  }
//
//  // Promote i8 and i16
//  if (LocVT == MVT::i8 || LocVT == MVT::i16) {
//    LocVT = MVT::i32;
//    if (ArgFlags.isSExt())
//      LocInfo = CCValAssign::SExt;
//    else if (ArgFlags.isZExt())
//      LocInfo = CCValAssign::ZExt;
//    else
//      LocInfo = CCValAssign::AExt;
//  }
//
//  unsigned Reg;
//
//  // f32 and f64 are allocated in A0, A1, A2, A3 when either of the following
//  // is true: function is vararg, argument is 3rd or higher, there is previous
//  // argument which is not f32 or f64.
//  bool AllocateFloatsInIntReg = State.isVarArg() || ValNo > 1
//      || State.getFirstUnallocated(F32Regs, FloatRegsSize) != ValNo;
//  unsigned OrigAlign = ArgFlags.getOrigAlign();
//  bool isI64 = (ValVT == MVT::i32 && OrigAlign == 8);
//
//  if (ValVT == MVT::i32 || (ValVT == MVT::f32 && AllocateFloatsInIntReg)) {
//    Reg = State.AllocateReg(IntRegs, IntRegsSize);
//    // If this is the first part of an i64 arg,
//    // the allocated register must be either A0 or A2.
//    if (isI64 && (Reg == Nios2::A1 || Reg == Nios2::A3))
//      Reg = State.AllocateReg(IntRegs, IntRegsSize);
//    LocVT = MVT::i32;
//  } else if (ValVT == MVT::f64 && AllocateFloatsInIntReg) {
//    // Allocate int register and shadow next int register. If first
//    // available register is Nios2::A1 or Nios2::A3, shadow it too.
//    Reg = State.AllocateReg(IntRegs, IntRegsSize);
//    if (Reg == Nios2::A1 || Reg == Nios2::A3)
//      Reg = State.AllocateReg(IntRegs, IntRegsSize);
//    State.AllocateReg(IntRegs, IntRegsSize);
//    LocVT = MVT::i32;
//  } else if (ValVT.isFloatingPoint() && !AllocateFloatsInIntReg) {
//    // we are guaranteed to find an available float register
//    if (ValVT == MVT::f32) {
//      Reg = State.AllocateReg(F32Regs, FloatRegsSize);
//      // Shadow int register
//      State.AllocateReg(IntRegs, IntRegsSize);
//    } else {
//      Reg = State.AllocateReg(F64Regs, FloatRegsSize);
//      // Shadow int registers
//      unsigned Reg2 = State.AllocateReg(IntRegs, IntRegsSize);
//      if (Reg2 == Nios2::A1 || Reg2 == Nios2::A3)
//        State.AllocateReg(IntRegs, IntRegsSize);
//      State.AllocateReg(IntRegs, IntRegsSize);
//    }
//  } else
//    llvm_unreachable("Cannot handle this ValVT.");
//
//  unsigned SizeInBytes = ValVT.getSizeInBits() >> 3;
//  unsigned Offset = State.AllocateStack(SizeInBytes, OrigAlign);
//
//  if (!Reg)
//    State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset, LocVT, LocInfo));
//  else
//    State.addLoc(CCValAssign::getReg(ValNo, ValVT, Reg, LocVT, LocInfo));
//
//  return false; // CC must always match
//}
//
#include "Nios2GenCallingConv.inc"

//static void
//AnalyzeNios264CallOperands(CCState &CCInfo,
//                          const SmallVectorImpl<ISD::OutputArg> &Outs) {
//  unsigned NumOps = Outs.size();
//  for (unsigned i = 0; i != NumOps; ++i) {
//    MVT ArgVT = Outs[i].VT;
//    ISD::ArgFlagsTy ArgFlags = Outs[i].Flags;
//    bool R;
//
//    if (Outs[i].IsFixed)
//      R = CC_Nios2N(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo);
//    else
//      R = CC_Nios2N_VarArg(i, ArgVT, ArgVT, CCValAssign::Full, ArgFlags, CCInfo);
//
//    if (R) {
//#ifndef NDEBUG
//      dbgs() << "Call operand #" << i << " has unhandled type "
//             << EVT(ArgVT).getEVTString();
//#endif
//      llvm_unreachable(0);
//    }
//  }
//}

//===----------------------------------------------------------------------===//
//                  Call Calling Convention Implementation
//===----------------------------------------------------------------------===//

static const unsigned O32IntRegsSize = 4;

static const ArrayRef<MCPhysReg> O32IntRegs = {
  Nios2::R4, Nios2::R5, Nios2::R6, Nios2::R7
};

// Write ByVal Arg to arg registers and stack.
static void
WriteByValArg(SDValue Chain, SDLoc dl,
              SmallVector<std::pair<unsigned, SDValue>, 16> &RegsToPass,
              SmallVector<SDValue, 8> &MemOpChains, SDValue StackPtr,
              MachineFrameInfo *MFI, SelectionDAG &DAG, SDValue Arg,
              const CCValAssign &VA, const ISD::ArgFlagsTy &Flags,
              MVT PtrType, bool isLittle) {
  unsigned LocMemOffset = VA.isMemLoc() ? VA.getLocMemOffset() : 0;
  unsigned Offset = 0;
  uint32_t RemainingSize = Flags.getByValSize();
  unsigned ByValAlign = Flags.getByValAlign();

  // Copy the first 4 words of byval arg to registers R4 - R7.
  // FIXME: Use a stricter alignment if it enables better optimization in passes
  //        run later.
  for (; RemainingSize >= 4 && LocMemOffset < 4 * 4;
       Offset += 4, RemainingSize -= 4, LocMemOffset += 4) {
    SDValue LoadPtr = DAG.getNode(ISD::ADD, dl, MVT::i32, Arg,
                                  DAG.getConstant(Offset, dl, MVT::i32));
    SDValue LoadVal = DAG.getLoad(MVT::i32, dl, Chain, LoadPtr,
                                  MachinePointerInfo(), false, false, false,
                                  std::min(ByValAlign, (unsigned )4));
    MemOpChains.push_back(LoadVal.getValue(1));
    unsigned DstReg = O32IntRegs[LocMemOffset / 4];
    RegsToPass.push_back(std::make_pair(DstReg, LoadVal));
  }

  if (RemainingSize == 0)
    return;

  // If there still is a register available for argument passing, write the
  // remaining part of the structure to it using subword loads and shifts.
  if (LocMemOffset < 4 * 4) {
    assert(RemainingSize <= 3 && RemainingSize >= 1 &&
           "There must be one to three bytes remaining.");
    unsigned LoadSize = (RemainingSize == 3 ? 2 : RemainingSize);
    SDValue LoadPtr = DAG.getNode(ISD::ADD, dl, MVT::i32, Arg,
                                  DAG.getConstant(Offset, dl, MVT::i32));
    unsigned Alignment = std::min(ByValAlign, (unsigned )4);
    SDValue LoadVal = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, Chain,
                                     LoadPtr, MachinePointerInfo(),
                                     MVT::getIntegerVT(LoadSize * 8), false,
                                     false, false, Alignment);
    MemOpChains.push_back(LoadVal.getValue(1));

    // If target is big endian, shift it to the most significant half-word or
    // byte.
    if (!isLittle)
      LoadVal = DAG.getNode(ISD::SHL, dl, MVT::i32, LoadVal,
                            DAG.getConstant(32 - LoadSize * 8, dl, MVT::i32));

    Offset += LoadSize;
    RemainingSize -= LoadSize;

    // Read second subword if necessary.
    if (RemainingSize != 0)  {
      assert(RemainingSize == 1 && "There must be one byte remaining.");
      LoadPtr = DAG.getNode(ISD::ADD, dl, MVT::i32, Arg,
                            DAG.getConstant(Offset, dl, MVT::i32));
      unsigned Alignment = std::min(ByValAlign, (unsigned )2);
      SDValue Subword = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i32, Chain,
                                       LoadPtr, MachinePointerInfo(),
                                       MVT::i8, false, false, false, Alignment);
      MemOpChains.push_back(Subword.getValue(1));
      // Insert the loaded byte to LoadVal.
      // FIXME: Use INS if supported by target.
      unsigned ShiftAmt = isLittle ? 16 : 8;
      SDValue Shift = DAG.getNode(ISD::SHL, dl, MVT::i32, Subword,
                                  DAG.getConstant(ShiftAmt,dl,  MVT::i32));
      LoadVal = DAG.getNode(ISD::OR, dl, MVT::i32, LoadVal, Shift);
    }

    unsigned DstReg = O32IntRegs[LocMemOffset / 4];
    RegsToPass.push_back(std::make_pair(DstReg, LoadVal));
    return;
  }

  // Copy remaining part of byval arg using memcpy.
  SDValue Src = DAG.getNode(ISD::ADD, dl, MVT::i32, Arg,
                            DAG.getConstant(Offset,dl,  MVT::i32));
  SDValue Dst = DAG.getNode(ISD::ADD, dl, MVT::i32, StackPtr,
                            DAG.getIntPtrConstant(LocMemOffset, dl));
  Chain = DAG.getMemcpy(Chain, dl, Dst, Src,
                        DAG.getConstant(RemainingSize, dl, MVT::i32),
                        std::min(ByValAlign, (unsigned)4),
                        /*isVolatile=*/false, /*AlwaysInline=*/false,
                        /*isTailCall=*/false,
                        MachinePointerInfo(), MachinePointerInfo());
  MemOpChains.push_back(Chain);
}

/// LowerCall - functions arguments are copied from virtual regs to
/// (physical regs)/(stack frame), CALLSEQ_START and CALLSEQ_END are emitted.
/// TODO: isTailCall.
SDValue
Nios2TargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                              SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVector<ISD::OutputArg, 32> &Outs = CLI.Outs;
  SmallVector<SDValue, 32> &OutVals     = CLI.OutVals;
  SmallVector<ISD::InputArg, 32> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool isVarArg                         = CLI.IsVarArg;

  // MIPs target does not yet support tail call optimization.
  isTailCall = false;

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFL = MF.getSubtarget().getFrameLowering();
  bool IsPIC = getTargetMachine().getRelocationModel() == Reloc::PIC_;
  Nios2FunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::Fast)
    CCInfo.AnalyzeCallOperands(Outs, CC_Nios2_FastCC);
  else
    CCInfo.AnalyzeCallOperands(Outs, CC_Nios2);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NextStackOffset = CCInfo.getNextStackOffset();
  unsigned StackAlignment = TFL->getStackAlignment();
  NextStackOffset = RoundUpToAlignment(NextStackOffset, StackAlignment);

  // Update size of the maximum argument space.
  // For O32, a minimum of four words (16 bytes) of argument space is
  // allocated.
  if (CallConv != CallingConv::Fast)
    NextStackOffset = std::max(NextStackOffset, (unsigned)16);

  // Chain is the output chain of the last Load/Store or CopyToReg node.
  // ByValChain is the output chain of the last Memcpy node created for copying
  // byval arguments to the stack.
  SDValue NextStackOffsetVal = DAG.getIntPtrConstant(NextStackOffset, dl, true);
  Chain = DAG.getCALLSEQ_START(Chain, NextStackOffsetVal, dl);

  SDValue StackPtr = DAG.getCopyFromReg(Chain, dl, Nios2::SP, getPointerTy(DAG.getDataLayout()));

  if (Nios2FI->getMaxCallFrameSize() < NextStackOffset)
    Nios2FI->setMaxCallFrameSize(NextStackOffset);

  // With EABI is it possible to have 16 args on registers.
  SmallVector<std::pair<unsigned, SDValue>, 16> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    SDValue Arg = OutVals[i];
    CCValAssign &VA = ArgLocs[i];
    MVT ValVT = VA.getValVT(), LocVT = VA.getLocVT();
    ISD::ArgFlagsTy Flags = Outs[i].Flags;

    // ByVal Arg.
    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      WriteByValArg(Chain, dl, RegsToPass, MemOpChains, StackPtr,
                      MFI, DAG, Arg, VA, Flags, getPointerTy(DAG.getDataLayout()),
                      Subtarget.isLittle());
      continue;
    }

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full:
      if (VA.isRegLoc()) {
        if (ValVT == MVT::f32 && LocVT == MVT::i32)
          Arg = DAG.getNode(ISD::BITCAST, dl, LocVT, Arg);
      }
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, LocVT, Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, LocVT, Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, LocVT, Arg);
      break;
    }

    // Arguments that can be passed on register must be kept at
    // RegsToPass vector
    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
      continue;
    }

    // Register can't get to this point...
    assert(VA.isMemLoc());

    // emit ISD::STORE whichs stores the
    // parameter value to a stack Location
    SDValue PtrOff = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()), StackPtr,
                                 DAG.getIntPtrConstant(VA.getLocMemOffset(), dl));
    MemOpChains.push_back(DAG.getStore(Chain, dl, Arg, PtrOff,
                                       MachinePointerInfo(), false, false, 0));
  }

  // Transform all store nodes into one single node because all store
  // nodes are independent of each other.
  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  unsigned char OpFlag;
  bool IsPICCall = IsPIC; // true if calls are translated to jalr $25
  bool GlobalOrExternal = false;
  SDValue CalleeLo;

  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    if (IsPICCall && G->getGlobal()->hasInternalLinkage()) {
      OpFlag = Nios2II::MO_GOT_PAGE;
      unsigned char LoFlag = Nios2II::MO_LO16;
      Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl, getPointerTy(DAG.getDataLayout()), 0,
                                          OpFlag);
      CalleeLo = DAG.getTargetGlobalAddress(G->getGlobal(), dl, getPointerTy(DAG.getDataLayout()),
                                            0, LoFlag);
    } else {
      OpFlag = IsPICCall ? Nios2II::MO_GOT_CALL : Nios2II::MO_NO_FLAG;
      Callee = DAG.getTargetGlobalAddress(G->getGlobal(), dl,
                                          getPointerTy(DAG.getDataLayout()), 0, OpFlag);
    }

    GlobalOrExternal = true;
  }
  else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    OpFlag = Nios2II::MO_NO_FLAG;
    Callee = DAG.getTargetExternalSymbol(S->getSymbol(), getPointerTy(DAG.getDataLayout()),
                                         OpFlag);
    GlobalOrExternal = true;
  }

  SDValue InFlag;

  // Create nodes that load address of callee and copy it to T9
  if (IsPICCall) {
    if (GlobalOrExternal) {
      // Load callee address
      Callee = DAG.getNode(Nios2ISD::Wrapper, dl, getPointerTy(DAG.getDataLayout()),
                           GetGlobalReg(DAG, getPointerTy(DAG.getDataLayout())), Callee);
      SDValue LoadValue = DAG.getLoad(getPointerTy(DAG.getDataLayout()), dl, DAG.getEntryNode(),
                                      Callee, MachinePointerInfo::getGOT(DAG.getMachineFunction()),
                                      false, false, false, 0);

      // Use GOT+LO if callee has internal linkage.
      if (CalleeLo.getNode()) {
        SDValue Lo = DAG.getNode(Nios2ISD::Lo, dl, getPointerTy(DAG.getDataLayout()), CalleeLo);
        Callee = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()), LoadValue, Lo);
      } else
        Callee = LoadValue;
    }
  }

  // Insert node "GP copy globalreg" before call to function.
  // Lazy-binding stubs require GP to point to the GOT.
  if (IsPICCall) {
    unsigned GPReg = Nios2::GP;
    EVT Ty = MVT::i32;
    RegsToPass.push_back(std::make_pair(GPReg, GetGlobalReg(DAG, Ty)));
  }

  // Build a sequence of copy-to-reg nodes chained together with token
  // chain and flag operands which copy the outgoing args into registers.
  // The InFlag in necessary since all emitted instructions must be
  // stuck together.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InFlag);
    InFlag = Chain.getValue(1);
  }

  // Nios2JmpLink = #chain, #target_address, #opt_in_flags...
  //             = Chain, Callee, Reg#1, Reg#2, ...
  //
  // Returns a chain & a flag for retval copy to use.
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  // Add argument registers to the end of the list so that they are
  // known live into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  const uint32_t *Mask = TRI->getCallPreservedMask(DAG.getMachineFunction(), CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InFlag.getNode())
    Ops.push_back(InFlag);

  Chain  = DAG.getNode(Nios2ISD::JmpLink, dl, NodeTys, Ops);
  InFlag = Chain.getValue(1);

  // Create the CALLSEQ_END node.
  Chain = DAG.getCALLSEQ_END(Chain, NextStackOffsetVal,
                             DAG.getIntPtrConstant(0, dl, true), InFlag, dl);
  InFlag = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InFlag, CallConv, isVarArg,
                         Ins, dl, DAG, InVals);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue
Nios2TargetLowering::LowerCallResult(SDValue Chain, SDValue InFlag,
                                    CallingConv::ID CallConv, bool isVarArg,
                                    const SmallVectorImpl<ISD::InputArg> &Ins,
                                    SDLoc dl, SelectionDAG &DAG,
                                    SmallVectorImpl<SDValue> &InVals) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 RVLocs, *DAG.getContext());

  CCInfo.AnalyzeCallResult(Ins, RetCC_Nios2);

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    Chain = DAG.getCopyFromReg(Chain, dl, RVLocs[i].getLocReg(),
                               RVLocs[i].getValVT(), InFlag).getValue(1);
    InFlag = Chain.getValue(2);
    InVals.push_back(Chain.getValue(0));
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//             Formal Arguments Calling Convention Implementation
//===----------------------------------------------------------------------===//
static void ReadByValArg(MachineFunction &MF, SDValue Chain, SDLoc dl,
                         std::vector<SDValue> &OutChains,
                         SelectionDAG &DAG, unsigned NumWords, SDValue FIN,
                         const CCValAssign &VA, const ISD::ArgFlagsTy &Flags,
                         const Argument *FuncArg) {
  unsigned LocMem = VA.isMemLoc() ? VA.getLocMemOffset() : 0;
  unsigned FirstWord = LocMem / 4;

  // copy register R0 - R3 to frame object
  for (unsigned i = 0; i < NumWords; ++i) {
    unsigned CurWord = FirstWord + i;
    if (CurWord >= O32IntRegsSize)
      break;

    unsigned SrcReg = O32IntRegs[CurWord];
    unsigned Reg = addLiveIn(MF, SrcReg, &Nios2::CPURegsRegClass);
    SDValue StorePtr = DAG.getNode(ISD::ADD, dl, MVT::i32, FIN,
                                   DAG.getConstant(i * 4, dl, MVT::i32));
    SDValue Store = DAG.getStore(Chain, dl, DAG.getRegister(Reg, MVT::i32),
                                 StorePtr, MachinePointerInfo(FuncArg, i * 4),
                                 false, false, 0);
    OutChains.push_back(Store);
  }
}

// Create frame object on stack and copy registers used for byval passing to it.
//static unsigned
//CopyNios264ByValRegs(MachineFunction &MF, SDValue Chain, DebugLoc dl,
//                    std::vector<SDValue> &OutChains, SelectionDAG &DAG,
//                    const CCValAssign &VA, const ISD::ArgFlagsTy &Flags,
//                    MachineFrameInfo *MFI, bool IsRegLoc,
//                    SmallVectorImpl<SDValue> &InVals, Nios2FunctionInfo *Nios2FI,
//                    EVT PtrTy, const Argument *FuncArg) {
//  const uint16_t *Reg = Nios264IntRegs + 8;
//  int FOOffset; // Frame object offset from virtual frame pointer.
//
//  if (IsRegLoc) {
//    Reg = std::find(Nios264IntRegs, Nios264IntRegs + 8, VA.getLocReg());
//    FOOffset = (Reg - Nios264IntRegs) * 8 - 8 * 8;
//  }
//  else
//    FOOffset = VA.getLocMemOffset();
//
//  // Create frame object.
//  unsigned NumRegs = (Flags.getByValSize() + 7) / 8;
//  unsigned LastFI = MFI->CreateFixedObject(NumRegs * 8, FOOffset, true);
//  SDValue FIN = DAG.getFrameIndex(LastFI, PtrTy);
//  InVals.push_back(FIN);
//
//  // Copy arg registers.
//  for (unsigned I = 0; (Reg != Nios264IntRegs + 8) && (I < NumRegs);
//       ++Reg, ++I) {
//    unsigned VReg = AddLiveIn(MF, *Reg, &Nios2::CPU64RegsRegClass);
//    SDValue StorePtr = DAG.getNode(ISD::ADD, dl, PtrTy, FIN,
//                                   DAG.getConstant(I * 8, PtrTy));
//    SDValue Store = DAG.getStore(Chain, dl, DAG.getRegister(VReg, MVT::i64),
//                                 StorePtr, MachinePointerInfo(FuncArg, I * 8),
//                                 false, false, 0);
//    OutChains.push_back(Store);
//  }
//
//  return LastFI;
//}

/// LowerFormalArguments - transform physical registers into virtual registers
/// and generate load operations for arguments places on the stack.
SDValue
Nios2TargetLowering::LowerFormalArguments(SDValue Chain,
                                         CallingConv::ID CallConv,
                                         bool isVarArg,
                                      const SmallVectorImpl<ISD::InputArg> &Ins,
                                         SDLoc dl, SelectionDAG &DAG,
                                         SmallVectorImpl<SDValue> &InVals)
                                          const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo *MFI = MF.getFrameInfo();
  Nios2FunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();

  Nios2FI->setVarArgsFrameIndex(0);

  // Used with vargs to acumulate store chains.
  std::vector<SDValue> OutChains;

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 ArgLocs, *DAG.getContext());

  if (CallConv == CallingConv::Fast)
    CCInfo.AnalyzeFormalArguments(Ins, CC_Nios2_FastCC);
  else
    CCInfo.AnalyzeFormalArguments(Ins, CC_Nios2);

  Function::const_arg_iterator FuncArg =
    DAG.getMachineFunction().getFunction()->arg_begin();
  int LastFI = 0;// Nios2FI->LastInArgFI is 0 at the entry of this function.

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i, ++FuncArg) {
    CCValAssign &VA = ArgLocs[i];
    EVT ValVT = VA.getValVT();
    ISD::ArgFlagsTy Flags = Ins[i].Flags;
    bool IsRegLoc = VA.isRegLoc();

    if (Flags.isByVal()) {
      assert(Flags.getByValSize() &&
             "ByVal args of size 0 should have been ignored by front-end.");
      unsigned NumWords = (Flags.getByValSize() + 3) / 4;
      LastFI = MFI->CreateFixedObject(NumWords * 4,
          VA.isMemLoc() ? VA.getLocMemOffset() : 0,
          true);
      SDValue FIN = DAG.getFrameIndex(LastFI, getPointerTy(DAG.getDataLayout()));
      InVals.push_back(FIN);
      ReadByValArg(MF, Chain, dl, OutChains, DAG, NumWords, FIN, VA, Flags,
                   &*FuncArg);
      continue;
    }

    // Arguments stored on registers
    if (IsRegLoc) {
      EVT RegVT = VA.getLocVT();
      unsigned ArgReg = VA.getLocReg();
      const TargetRegisterClass *RC;

      if (RegVT == MVT::i32)
        RC = &Nios2::CPURegsRegClass;
      else
        llvm_unreachable("RegVT not supported by FormalArguments Lowering");

      // Transform the arguments stored on
      // physical registers into virtual ones
      unsigned Reg = addLiveIn(DAG.getMachineFunction(), ArgReg, RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, RegVT);

      // If this is an 8 or 16-bit value, it has been passed promoted
      // to 32 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      if (VA.getLocInfo() != CCValAssign::Full) {
        unsigned Opcode = 0;
        if (VA.getLocInfo() == CCValAssign::SExt)
          Opcode = ISD::AssertSext;
        else if (VA.getLocInfo() == CCValAssign::ZExt)
          Opcode = ISD::AssertZext;
        if (Opcode)
          ArgValue = DAG.getNode(Opcode, dl, RegVT, ArgValue,
                                 DAG.getValueType(ValVT));
        ArgValue = DAG.getNode(ISD::TRUNCATE, dl, ValVT, ArgValue);
      }

      InVals.push_back(ArgValue);
    } else { // VA.isRegLoc()

      // sanity check
      assert(VA.isMemLoc());

      // The stack pointer offset is relative to the caller stack frame.
      LastFI = MFI->CreateFixedObject(ValVT.getSizeInBits()/8,
                                      VA.getLocMemOffset(), true);

      // Create load nodes to retrieve arguments from the stack
      SDValue FIN = DAG.getFrameIndex(LastFI, getPointerTy(DAG.getDataLayout()));
      InVals.push_back(DAG.getLoad(ValVT, dl, Chain, FIN,
                                   MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), LastFI),
                                   false, false, false, 0));
    }
  }

  // The mips ABIs for returning structs by value requires that we copy
  // the sret argument into $v0 for the return. Save the argument into
  // a virtual register so that we can access it from the return points.
  if (DAG.getMachineFunction().getFunction()->hasStructRetAttr()) {
    unsigned Reg = Nios2FI->getSRetReturnReg();
    if (!Reg) {
      Reg = MF.getRegInfo().createVirtualRegister(getRegClassFor(MVT::i32));
      Nios2FI->setSRetReturnReg(Reg);
    }
    SDValue Copy = DAG.getCopyToReg(DAG.getEntryNode(), dl, Reg, InVals[0]);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Copy, Chain);
  }

  if (isVarArg) {
    unsigned NumOfRegs = 4;
    ArrayRef<MCPhysReg> ArgRegs = O32IntRegs;
    unsigned Idx = CCInfo.getFirstUnallocated(ArgRegs);
    int FirstRegSlotOffset = 0;
    const TargetRegisterClass *RC = (const TargetRegisterClass*)&Nios2::CPURegsRegClass;
    unsigned RegSize = RC->getSize();
    int RegSlotOffset = FirstRegSlotOffset + Idx * RegSize;

    // Offset of the first variable argument from stack pointer.
    int FirstVaArgOffset;

    FirstVaArgOffset =
        (CCInfo.getNextStackOffset() + RegSize - 1) / RegSize * RegSize;

    // Record the frame index of the first variable argument
    // which is a value necessary to VASTART.
    LastFI = MFI->CreateFixedObject(RegSize, FirstVaArgOffset, true);
    Nios2FI->setVarArgsFrameIndex(LastFI);

    // Copy the integer registers that have not been used for argument passing
    // to the argument register save area. For O32, the save area is allocated
    // in the caller's stack frame, while for N32/64, it is allocated in the
    // callee's stack frame.
    for (int StackOffset = RegSlotOffset;
         Idx < NumOfRegs; ++Idx, StackOffset += RegSize) {
      unsigned Reg = addLiveIn(DAG.getMachineFunction(), ArgRegs[Idx], RC);
      SDValue ArgValue = DAG.getCopyFromReg(Chain, dl, Reg,
                                            MVT::getIntegerVT(RegSize * 8));
      LastFI = MFI->CreateFixedObject(RegSize, StackOffset, true);
      SDValue PtrOff = DAG.getFrameIndex(LastFI, getPointerTy(DAG.getDataLayout()));
      OutChains.push_back(DAG.getStore(Chain, dl, ArgValue, PtrOff,
                                       MachinePointerInfo(), false, false, 0));
    }
  }

  Nios2FI->setLastInArgFI(LastFI);

  // All stores are grouped in one node to allow the matching between
  // the size of Ins and InVals. This only happens when on varg functions
  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains);
  }

  return Chain;
}

//===----------------------------------------------------------------------===//
//               Return Value Calling Convention Implementation
//===----------------------------------------------------------------------===//

SDValue
Nios2TargetLowering::LowerReturn(SDValue Chain,
                                CallingConv::ID CallConv, bool isVarArg,
                                const SmallVectorImpl<ISD::OutputArg> &Outs,
                                const SmallVectorImpl<SDValue> &OutVals,
                                SDLoc dl, SelectionDAG &DAG) const {

  // CCValAssign - represent the assignment of
  // the return value to a location
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slot.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(),
                 RVLocs, *DAG.getContext());

  // Analize return values.
  CCInfo.AnalyzeReturn(Outs, RetCC_Nios2);

  SDValue Flag;
  SmallVector<SDValue, 4> RetOps(1, Chain);

  // Copy the result values into the output registers.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), OutVals[i], Flag);

    // guarantee that all emitted copies are
    // stuck together, avoiding something bad
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // The mips ABIs for returning structs by value requires that we copy
  // the sret argument into $v0 for the return. We saved the argument into
  // a virtual register in the entry block, so now we copy the value out
  // and into $v0.
  if (DAG.getMachineFunction().getFunction()->hasStructRetAttr()) {
    MachineFunction &MF = DAG.getMachineFunction();
    Nios2FunctionInfo *Nios2FI = MF.getInfo<Nios2FunctionInfo>();
    unsigned Reg = Nios2FI->getSRetReturnReg();

    if (!Reg)
      llvm_unreachable("sret virtual register not created in the entry block");
    SDValue Val = DAG.getCopyFromReg(Chain, dl, Reg, getPointerTy(DAG.getDataLayout()));

    Chain = DAG.getCopyToReg(Chain, dl, Nios2::R2, Val, Flag);
    Flag = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(Nios2::R2, getPointerTy(DAG.getDataLayout())));
  }

  RetOps[0] = Chain;

  // Return on Nios2 is always a "jr $ra"
  if (Flag.getNode())
    RetOps.push_back(Flag);

  // Return Void
  return DAG.getNode(Nios2ISD::Ret, dl, MVT::Other, RetOps);
}

//===----------------------------------------------------------------------===//
//                           Nios2 Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
Nios2TargetLowering::ConstraintType Nios2TargetLowering::
getConstraintType(const std::string &Constraint) const
{
  // Nios2 specific constraints
  // GCC config/mips/constraints.md
  //
  // 'd' : An address register. Equivalent to r
  //       unless generating MIPS16 code.
  // 'y' : Equivalent to r; retained for
  //       backwards compatibility.
  // 'c' : A register suitable for use in an indirect
  //       jump. This will always be $25 for -mabicalls.
  // 'l' : The lo register. 1 word storage.
  // 'x' : The hilo register pair. Double word storage.
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
      default : break;
      case 'd':
      case 'y':
      case 'f':
      case 'c':
      case 'l':
      case 'x':
        return C_RegisterClass;
      case 'R':
        return C_Memory;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
//TargetLowering::ConstraintWeight
//Nios2TargetLowering::getSingleConstraintMatchWeight(
//    AsmOperandInfo &info, const char *constraint) const {
//  ConstraintWeight weight = CW_Invalid;
//  Value *CallOperandVal = info.CallOperandVal;
//    // If we don't have a value, we can't do a match,
//    // but allow it at the lowest weight.
//  if (CallOperandVal == NULL)
//    return CW_Default;
//  Type *type = CallOperandVal->getType();
//  // Look at the constraint type.
//  switch (*constraint) {
//  default:
//    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
//    break;
//  case 'd':
//  case 'y':
//    if (type->isIntegerTy())
//      weight = CW_Register;
//    break;
//  case 'f': // FPU or MSA register
//    if (Subtarget->hasMSA() && type->isVectorTy() &&
//        cast<VectorType>(type)->getBitWidth() == 128)
//      weight = CW_Register;
//    else if (type->isFloatTy())
//      weight = CW_Register;
//    break;
//  case 'c': // $25 for indirect jumps
//  case 'l': // lo register
//  case 'x': // hilo register pair
//    if (type->isIntegerTy())
//      weight = CW_SpecificReg;
//    break;
//  case 'I': // signed 16 bit immediate
//  case 'J': // integer zero
//  case 'K': // unsigned 16 bit immediate
//  case 'L': // signed 32 bit immediate where lower 16 bits are 0
//  case 'N': // immediate in the range of -65535 to -1 (inclusive)
//  case 'O': // signed 15 bit immediate (+- 16383)
//  case 'P': // immediate in the range of 65535 to 1 (inclusive)
//    if (isa<ConstantInt>(CallOperandVal))
//      weight = CW_Constant;
//    break;
//  case 'R':
//    weight = CW_Memory;
//    break;
//  }
//  return weight;
//}

/// This is a helper function to parse a physical register string and split it
/// into non-numeric and numeric parts (Prefix and Reg). The first boolean flag
/// that is returned indicates whether parsing was successful. The second flag
/// is true if the numeric part exists.
static std::pair<bool, bool>
parsePhysicalReg(const StringRef &C, std::string &Prefix,
                 unsigned long long &Reg) {
  if (C.front() != '{' || C.back() != '}')
    return std::make_pair(false, false);

  // Search for the first numeric character.
  StringRef::const_iterator I, B = C.begin() + 1, E = C.end() - 1;
  I = std::find_if(B, E, std::ptr_fun(isdigit));

  Prefix.assign(B, I - B);

  // The second flag is set to false if no numeric characters were found.
  if (I == E)
    return std::make_pair(true, false);

  // Parse the numeric characters.
  return std::make_pair(!getAsUnsignedInteger(StringRef(I, E - I), 10, Reg),
                        true);
}

std::pair<unsigned, const TargetRegisterClass *> Nios2TargetLowering::
parseRegForInlineAsmConstraint(const StringRef &C, MVT VT) const {
  std::string Prefix;
  unsigned long long Reg;

  std::pair<bool, bool> R = parsePhysicalReg(C, Prefix, Reg);

  if (!R.first)
    return std::make_pair((unsigned)0, (const TargetRegisterClass*)0);

//  if ((Prefix == "hi" || Prefix == "lo")) { // Parse hi/lo.
//    // No numeric characters follow "hi" or "lo".
//    if (R.second)
//      return std::make_pair((unsigned)0, (const TargetRegisterClass*)0);
//
//    RC = TRI->getRegClass(Prefix == "hi" ?
//                          Nios2::HI32RegClassID : Nios2::LO32RegClassID);
//    return std::make_pair(*(RC->begin()), RC);
//  }

  if (R.second) {
    if (Prefix == "r")
      return std::make_pair(Reg, &Nios2::CPURegsRegClass);
    else if (Prefix == "ctl")
      return std::make_pair(Reg, &Nios2::CtlRegsRegClass);
  } else {
    Reg = StringSwitch<unsigned long long>(Prefix)
      .Case("zero", Nios2::ZERO)
      .Case("at", Nios2::AT)
      .Case("et", Nios2::ET)
      .Case("bt", Nios2::BT)
      .Case("gp", Nios2::GP)
      .Case("sp", Nios2::SP)
      .Case("fp", Nios2::FP)
      .Case("ea", Nios2::EA)
      .Case("ba", Nios2::BA)
      .Case("ra", Nios2::RA)
      .Default(0);
    if (Reg != 0)
      return std::make_pair(Reg, &Nios2::CPURegsRegClass);

    Reg = StringSwitch<unsigned long long>(Prefix)
      .Case("status", Nios2::CTL0)
      .Case("estatus", Nios2::CTL1)
      .Case("bstatus", Nios2::CTL2)
      .Case("ienable", Nios2::CTL3)
      .Case("ipending", Nios2::CTL4)
      .Case("cpuid", Nios2::CTL5)
      .Case("exception", Nios2::CTL7)
      .Case("pteaddr", Nios2::CTL8)
      .Case("tlbacc", Nios2::CTL9)
      .Case("tlbmisc", Nios2::CTL10)
      .Case("badaddr", Nios2::CTL12)
      .Case("config", Nios2::CTL13)
      .Case("mpubase", Nios2::CTL14)
      .Case("mpuacc", Nios2::CTL15)
      .Default(0);
    if (Reg != 0)
      return std::make_pair(Reg, &Nios2::CtlRegsRegClass);
  }
  return std::make_pair((unsigned)0, (const TargetRegisterClass*)0);
}

/// Given a register class constraint, like 'r', if this corresponds directly
/// to an LLVM register class, return a register of 0 and the register class
/// pointer.
std::pair<unsigned, const TargetRegisterClass*> 
Nios2TargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI, 
                                                  StringRef Constraint, 
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'd': // Address register. Same as 'r' unless generating MIPS16 code.
    case 'y': // Same as 'r'. Exists for compatibility.
    case 'r':
      if (VT == MVT::i32 || VT == MVT::i16 || VT == MVT::i8) {
        return std::make_pair(0U, &Nios2::CPURegsRegClass);
      }
      // This will generate an error message
      return std::make_pair(0u, static_cast<const TargetRegisterClass*>(0));
    }
  }

//  std::pair<unsigned, const TargetRegisterClass *> R;
//  R = parseRegForInlineAsmConstraint(Constraint, VT);
//
//  if (R.second)
//    return R;
//
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void Nios2TargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     std::string &Constraint,
                                                     std::vector<SDValue>&Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result(0, 0);

  // Only support length 1 constraints for now.
  if (Constraint.length() > 1) return;

  SDLoc DL(Op);
  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default: break; // This will fall through to the generic implementation
  case 'I': // Signed 16 bit constant
    // If this fails, the parent routine will give an error
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getSExtValue();
      if (isInt<16>(Val)) {
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  case 'J': // integer zero
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getZExtValue();
      if (Val == 0) {
        Result = DAG.getTargetConstant(0, DL, Type);
        break;
      }
    }
    return;
  case 'K': // unsigned 16 bit immediate
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      uint64_t Val = (uint64_t)C->getZExtValue();
      if (isUInt<16>(Val)) {
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  case 'L': // signed 32 bit immediate where lower 16 bits are 0
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getSExtValue();
      if ((isInt<32>(Val)) && ((Val & 0xffff) == 0)){
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  case 'N': // immediate in the range of -65535 to -1 (inclusive)
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getSExtValue();
      if ((Val >= -65535) && (Val <= -1)) {
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  case 'O': // signed 15 bit immediate
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getSExtValue();
      if ((isInt<15>(Val))) {
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  case 'P': // immediate in the range of 1 to 65535 (inclusive)
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
      EVT Type = Op.getValueType();
      int64_t Val = C->getSExtValue();
      if ((Val <= 65535) && (Val >= 1)) {
        Result = DAG.getTargetConstant(Val, DL, Type);
        break;
      }
    }
    return;
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }

  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}


bool
Nios2TargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The Nios2 target isn't yet aware of offsets.
  return false;
}

MachineBasicBlock *
Nios2TargetLowering::EmitInstrWithCustomInserter(MachineInstr *MI,
                                                 MachineBasicBlock *BB) const {
  MachineFunction *MF = BB->getParent();
  const TargetInstrInfo *TII = Subtarget.getInstrInfo();
  MachineBasicBlock::iterator I = MachineBasicBlock::iterator(MI);

  DEBUG(dbgs() << "Custom inserting " << *MI);

  switch (MI->getOpcode()) {
    case Nios2::MOVFI: {
      const DebugLoc DL = MI->getDebugLoc();
      // Expand to dst = src + imm
      MachineOperand &dst = MI->getOperand(0);
      MachineOperand &src = MI->getOperand(1);
      MachineOperand &imm = MI->getOperand(2);
      BuildMI(*BB, I, DL, TII->get(Nios2::ADDi))
        .addOperand(dst).addOperand(src).addOperand(imm);
      MI->eraseFromParent();
      return BB;
    }
    case Nios2::SELECT: {
      /*
       * SELECT res, a, x, y
       * ==>
       * bneq a, ZERO, BB1
       * br BB2
       * BB1:
       * resx = COPY x
       * br ExitBB
       * BB2:
       * resy = COPY y
       * br ExitBB
       * ExitBB:
       * res = PHI resx, resy
       *
       */
      const DebugLoc DL = MI->getDebugLoc();
      unsigned res = MI->getOperand(0).getReg();
      MachineOperand &a = MI->getOperand(1);
      MachineOperand &x = MI->getOperand(2);
      MachineOperand &y = MI->getOperand(3);
      const TargetRegisterClass *RC = getRegClassFor(MVT::i32);
      unsigned resx = MF->getRegInfo().createVirtualRegister(RC);
      unsigned resy = MF->getRegInfo().createVirtualRegister(RC);
      const BasicBlock *LLVM_BB = BB->getBasicBlock();
      MachineBasicBlock *BB1 = MF->CreateMachineBasicBlock(LLVM_BB);
      MachineBasicBlock *BB2 = MF->CreateMachineBasicBlock(LLVM_BB);
      MachineBasicBlock *ExitBB = MF->CreateMachineBasicBlock(LLVM_BB);
      // Add new BBs
      MachineFunction::iterator FIt = std::next(MachineFunction::iterator(BB));
      MF->insert(FIt, BB1);
      MF->insert(FIt, BB2);
      MF->insert(FIt, ExitBB);

      BuildMI(*BB, I, DL, TII->get(Nios2::BEQ))
        .addOperand(a).addReg(Nios2::ZERO).addMBB(BB1);
      BuildMI(*BB, I, DL, TII->get(Nios2::BR))
        .addMBB(BB2);
      /* BB1:
       * resx = COPY x
       * br ExitBB
       */
      BuildMI(BB1, DL, TII->get(TargetOpcode::COPY))
        .addReg(resx, RegState::Define).addOperand(x);
      BuildMI(BB1, DL, TII->get(Nios2::BR))
        .addMBB(ExitBB);
      /* BB1:
       * resy = COPY x
       * br ExitBB
       */
      BuildMI(BB2, DL, TII->get(TargetOpcode::COPY))
        .addReg(resy, RegState::Define).addOperand(y);
      BuildMI(BB2, DL, TII->get(Nios2::BR))
        .addMBB(ExitBB);

      /* res = PHI resx, resy */
      BuildMI(ExitBB, DL, TII->get(TargetOpcode::PHI), res)
        .addReg(resx).addMBB(BB1)
        .addReg(resy).addMBB(BB2);

      ExitBB->splice(ExitBB->end(), BB, std::next(I), BB->end());
      ExitBB->transferSuccessorsAndUpdatePHIs(BB);
      BB->addSuccessor(BB1);
      BB->addSuccessor(BB2);
      BB1->addSuccessor(ExitBB);
      BB2->addSuccessor(ExitBB);
      MI->eraseFromParent();
      return ExitBB;
    }
    default:
      llvm_unreachable("Unhandled custom insterted instruction!");
  }

  return NULL;
}

