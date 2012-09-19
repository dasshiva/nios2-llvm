add_llvm_library(LLVMNios2Desc
  Nios2AsmBackend.cpp
  Nios2MCAsmInfo.cpp
  Nios2MCCodeEmitter.cpp
  Nios2MCTargetDesc.cpp
  Nios2ELFObjectWriter.cpp
  )

add_dependencies(LLVMNios2Desc Nios2CommonTableGen)
