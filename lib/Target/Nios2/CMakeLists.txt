set(LLVM_TARGET_DEFINITIONS Nios2.td)

tablegen(LLVM Nios2GenRegisterInfo.inc -gen-register-info)
tablegen(LLVM Nios2GenInstrInfo.inc -gen-instr-info)
tablegen(LLVM Nios2GenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM Nios2GenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM Nios2GenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM Nios2GenDAGISel.inc -gen-dag-isel)
tablegen(LLVM Nios2GenCallingConv.inc -gen-callingconv)
tablegen(LLVM Nios2GenSubtargetInfo.inc -gen-subtarget)
tablegen(LLVM Nios2GenAsmMatcher.inc -gen-asm-matcher)
add_public_tablegen_target(Nios2CommonTableGen)

add_llvm_target(Nios2CodeGen
  Nios2AsmPrinter.cpp
  Nios2FrameLowering.cpp
  Nios2InstrInfo.cpp
  Nios2ISelDAGToDAG.cpp
  Nios2ISelLowering.cpp
  Nios2MachineFunction.cpp
  Nios2MCInstLower.cpp
  Nios2RegisterInfo.cpp
  Nios2SelectionDAGInfo.cpp
  Nios2Subtarget.cpp
  Nios2TargetMachine.cpp
  )

add_dependencies(LLVMNios2CodeGen intrinsics_gen)

add_subdirectory(InstPrinter)
add_subdirectory(TargetInfo)
add_subdirectory(MCTargetDesc)
