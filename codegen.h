#ifndef CODEGEN_H
#define CODEGEN_H

#include "KaleidoscopeJIT.h" 

using namespace llvm;
using namespace llvm::orc;

extern std::unique_ptr<LLVMContext> TheContext;
extern std::unique_ptr<Module> TheModule;
extern std::unique_ptr<IRBuilder<>> Builder;
extern std::unique_ptr<legacy::FunctionPassManager> TheFPM;
extern std::unique_ptr<KaleidoscopeJIT> TheJIT;
extern std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
extern ExitOnError ExitOnErr;


#endif // CODEGEN_H