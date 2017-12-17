//===-- Nios2ISelLowering.h - Nios2 DAG Lowering Interface --------*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_NIOS2_NIOS2ISELLOWERING_H
#define LLVM_LIB_TARGET_NIOS2_NIOS2ISELLOWERING_H

#include "Nios2.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/Target/TargetLowering.h"

namespace llvm {
  namespace Nios2ISD {
    enum NodeType {
      // Start the numbering from where ISD NodeType finishes.
      FIRST_NUMBER = ISD::BUILTIN_OP_END,
      // Get the Higher 16 bits from a 32-bit immediate
      // No relation with Mips Hi register
      Hi,

      // Get the Lower 16 bits from a 32-bit immediate
      // No relation with Mips Lo register
      Lo,

      // Return
      Ret,

      // Select node: ins condition, true value, false value
      Select, 

      GPRel,

      JmpLink,

      Wrapper,

      Sync,

      // Read and write control registers
      ReadCtrl,
      WriteCtrl
    };
  }

  //===--------------------------------------------------------------------===//
  // TargetLowering Implementation
  //===--------------------------------------------------------------------===//
  class Nios2Subtarget;

  class Nios2TargetLowering : public TargetLowering  {
  public:
    explicit Nios2TargetLowering(const Nios2TargetMachine &TM, 
                                 const Nios2Subtarget &STI);

    virtual MVT getShiftAmountTy(EVT LHSTy) const { return MVT::i32; }

    virtual bool allowsUnalignedMemoryAccesses(EVT VT, bool *Fast) const;

    /// LowerOperation - Provide custom lowering hooks for some operations.
    SDValue LowerOperation(SDValue Op, SelectionDAG &DAG) const override;

    /// getTargetNodeName - This method returns the name of a target specific
    //  DAG node.
    const char *getTargetNodeName(unsigned Opcode) const override;

    /// getSetCCResultType - get the ISD::SETCC result ValueType
    EVT getSetCCResultType(const DataLayout &DL, 
                           LLVMContext &Context, EVT VT) const override;

  private:
    // Subtarget Info
    const Nios2Subtarget &Subtarget;

    // Lower Operand helpers
    SDValue LowerCallResult(SDValue Chain, SDValue InFlag,
                            CallingConv::ID CallConv, bool isVarArg,
                            const SmallVectorImpl<ISD::InputArg> &Ins,
                            SDLoc dl, SelectionDAG &DAG,
                            SmallVectorImpl<SDValue> &InVals) const;

    // Lower Operand specifics
    //SDValue LowerBRCOND(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerConstantPool(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerBlockAddress(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerJumpTable(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerSELECT(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerSETCC(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const;
    //SDValue LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const;
    SDValue LowerMEMBARRIER(SDValue Op, SelectionDAG& DAG) const;
    SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG& DAG) const;
    SDValue lowerShiftRightParts(SDValue Op, SelectionDAG &DAG,
                                                 bool IsSRA) const;
    SDValue lowerShiftLeftParts(SDValue Op, SelectionDAG &DAG) const;
    SDValue lowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const;

    virtual SDValue
      LowerFormalArguments(SDValue Chain,
                           CallingConv::ID CallConv, bool isVarArg,
                           const SmallVectorImpl<ISD::InputArg> &Ins,
                           SDLoc dl, SelectionDAG &DAG,
                           SmallVectorImpl<SDValue> &InVals) const;

    virtual SDValue
      LowerCall(TargetLowering::CallLoweringInfo &CLI,
                SmallVectorImpl<SDValue> &InVals) const;

    virtual SDValue
      LowerReturn(SDValue Chain,
                  CallingConv::ID CallConv, bool isVarArg,
                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                  const SmallVectorImpl<SDValue> &OutVals,
                  SDLoc dl, SelectionDAG &DAG) const;

    virtual bool isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const;

    virtual MachineBasicBlock *EmitInstrWithCustomInserter(MachineInstr *MI,
                                  MachineBasicBlock *MBB) const;

    virtual void LowerAsmOperandForConstraint(SDValue Op,
        std::string &Constraint,
        std::vector<SDValue>&Ops,
        SelectionDAG &DAG) const;

    virtual ConstraintType
      getConstraintType(const std::string &Constraint) const;

    virtual std::pair<unsigned, const TargetRegisterClass*>
      getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const;

    std::pair<unsigned, const TargetRegisterClass *>
      parseRegForInlineAsmConstraint(const StringRef &C, MVT VT) const;
  };
}

#endif // LLVM_LIB_TARGET_NIOS2_NIOS2ISELLOWERING_H
