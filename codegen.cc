//===----------------------------------------------------------------------===//
// Code Generation
// IR代码生成
//===----------------------------------------------------------------------===//
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Utils.h"
#include "ast.h"
#include "parser.h"
#include "lexer.h"
#include "helper.h"
#include "codegen.h"


std::unique_ptr<LLVMContext> TheContext;
std::unique_ptr<Module> TheModule;
std::unique_ptr<IRBuilder<>> Builder;
std::map<std::string, AllocaInst *> NamedValues;
std::unique_ptr<legacy::FunctionPassManager> TheFPM;
std::unique_ptr<KaleidoscopeJIT> TheJIT;
std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
ExitOnError ExitOnErr;

Value *LogErrorV(const char *Str) {
  LogError(Str);
  return nullptr;
}

// 查找函数
Function *getFunction(std::string Name) {
  // First, see if the function has already been added to the current module.
  // 当前模块找到，直接返回
  if (auto *F = TheModule->getFunction(Name))
    return F;

  // If not, check whether we can codegen the declaration from some existing
  // prototype.
  // 否则看看原型里面有没有，有的话，直接代码生成（声明）这个原型
  auto FI = FunctionProtos.find(Name);
  if (FI != FunctionProtos.end())
    return FI->second->codegen();

  // If no existing prototype exists, return null.
  // 没有返回空
  return nullptr;
}

/// CreateEntryBlockAlloca - Create an alloca instruction in the entry block of
/// the function.  This is used for mutable variables etc.
/// 生成一个alloca指令到函数的入口，专门用来给可变变量使用
AllocaInst *CreateEntryBlockAlloca(Function *TheFunction,
                                          StringRef VarName) {
  IRBuilder<> TmpB(&TheFunction->getEntryBlock(),
                   TheFunction->getEntryBlock().begin());
  return TmpB.CreateAlloca(Type::getDoubleTy(*TheContext), nullptr, VarName);
}

// 对数字的代码生成
Value *NumberExprAST::codegen() {
  return ConstantFP::get(*TheContext, APFloat(Val));
}

// 对变量生成代码
Value *VariableExprAST::codegen() {
  // Look this variable up in the function.
  // 函数里面查找变量
  AllocaInst *A = NamedValues[Name];
  if (!A)
    return LogErrorV("Unknown variable name");

  // Load the value.
  // 加载值
  return Builder->CreateLoad(A->getAllocatedType(), A, Name.c_str());
}

// 一元操作生成代码
Value *UnaryExprAST::codegen() {
  Value *OperandV = Operand->codegen();
  if (!OperandV)
    return nullptr;

  Function *F = getFunction(std::string("unary") + Opcode);
  if (!F)
    return LogErrorV("Unknown unary operator");

  return Builder->CreateCall(F, OperandV, "unop");
}

Value *BinaryExprAST::codegen() {
  // Special case '=' because we don't want to emit the LHS as an expression.
  // 特别的列子是赋值表达式，这里不会把左节点输出
  if (Op == '=') {
    // Assignment requires the LHS to be an identifier.
    // This assume we're building without RTTI because LLVM builds that way by
    // default.  If you build LLVM with RTTI this can be changed to a
    // dynamic_cast for automatic error checking.
    // 赋值需要左节点是个标识符（变量）
    // 这里假定你的LLVM在编译的时候是没有RTTI，因为LLVM编译默认就是没有RTTI
    // 如果你编译LLVM是有RTTI，则下面要改成dynamic_cast来自动错误检查
    VariableExprAST *LHSE = static_cast<VariableExprAST *>(LHS.get());
    if (!LHSE)
      return LogErrorV("destination of '=' must be a variable");
    // Codegen the RHS.
    // 代码生成右节点
    Value *Val = RHS->codegen();
    if (!Val)
      return nullptr;

    // Look up the name.
    // 查找变量名
    Value *Variable = NamedValues[LHSE->getName()];
    if (!Variable)
      return LogErrorV("Unknown variable name");
    // 保存val到变量名
    Builder->CreateStore(Val, Variable);
    return Val;
  }

  // 其他情况
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder->CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder->CreateFSub(L, R, "subtmp");
  case '*':
    return Builder->CreateFMul(L, R, "multmp");
  case '<':
    L = Builder->CreateFCmpULT(L, R, "cmptmp");
    // Convert bool 0/1 to double 0.0 or 1.0
    // 转换0/1到0.0或1.0
    return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
  default:
    break;
  }

  // If it wasn't a builtin binary operator, it must be a user defined one. Emit
  // a call to it.
  // 如果不是内建的操作符，那肯定是用户定义的，输出一个函数调用
  Function *F = getFunction(std::string("binary") + Op);
  assert(F && "binary operator not found!");

  Value *Ops[] = {L, R};
  return Builder->CreateCall(F, Ops, "binop");
}

Value *CallExprAST::codegen() {
  // Look up the name in the global module table.
  // 查找函数是否存在
  Function *CalleeF = getFunction(Callee);
  if (!CalleeF)
    return LogErrorV("Unknown function referenced");

  // If argument mismatch error.
  // 验证下参数
  if (CalleeF->arg_size() != Args.size())
    return LogErrorV("Incorrect # arguments passed");

  std::vector<Value *> ArgsV;
  for (unsigned i = 0, e = Args.size(); i != e; ++i) {
    ArgsV.push_back(Args[i]->codegen());
    if (!ArgsV.back())
      return nullptr;
  }

  return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

// 条件判断生成代码
Value *IfExprAST::codegen() {
  Value *CondV = Cond->codegen();
  if (!CondV)
    return nullptr;

  // Convert condition to a bool by comparing non-equal to 0.0.
  // 通过比较不等于0.0来转换条件为一个布尔值
  CondV = Builder->CreateFCmpONE(
      CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create blocks for the then and else cases.  Insert the 'then' block at the
  // end of the function.
  // 为then和else创建块，然后把then块放到TheFunction后面
  BasicBlock *ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
  BasicBlock *ElseBB = BasicBlock::Create(*TheContext, "else");
  BasicBlock *MergeBB = BasicBlock::Create(*TheContext, "ifcont");

  Builder->CreateCondBr(CondV, ThenBB, ElseBB);

  // Emit then value.
  // 输出then的值
  Builder->SetInsertPoint(ThenBB);

  Value *ThenV = Then->codegen();
  if (!ThenV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Then' can change the current block, update ThenBB for the PHI.
  // then的代码生成可能改变了当前块，所以为PHI更新ThenBB
  ThenBB = Builder->GetInsertBlock();

  // Emit else block.
  // 输出else块
  TheFunction->getBasicBlockList().push_back(ElseBB);
  Builder->SetInsertPoint(ElseBB);

  Value *ElseV = Else->codegen();
  if (!ElseV)
    return nullptr;

  Builder->CreateBr(MergeBB);
  // Codegen of 'Else' can change the current block, update ElseBB for the PHI.
  // Else的代码生成可能改变了当前块，所以为PHI更新ElseBB
  ElseBB = Builder->GetInsertBlock();

  // Emit merge block.
  // 输出merge块
  TheFunction->getBasicBlockList().push_back(MergeBB);
  Builder->SetInsertPoint(MergeBB);
  PHINode *PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");

  PN->addIncoming(ThenV, ThenBB);
  PN->addIncoming(ElseV, ElseBB);
  return PN;
}

// Output for-loop as:
//   var = alloca double
//   ...
//   start = startexpr
//   store start -> var
//   goto loop
// loop:
//   ...
//   bodyexpr
//   ...
// loopend:
//   step = stepexpr
//   endcond = endexpr
//
//   curvar = load var
//   nextvar = curvar + step
//   store nextvar -> var
//   br endcond, loop, endloop
// outloop:
// for循环的代码生成
Value *ForExprAST::codegen() {
  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Create an alloca for the variable in the entry block.
  // 在入口块分配一个变量存储
  AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);

  // Emit the start code first, without 'variable' in scope.
  // 输出start代码先
  Value *StartVal = Start->codegen();
  if (!StartVal)
    return nullptr;

  // Store the value into the alloca.
  // 保存这个值到之前的分配好的变量存储
  Builder->CreateStore(StartVal, Alloca);

  // Make the new basic block for the loop header, inserting after current
  // block.
  // 在当前块后面插入一个新块
  BasicBlock *LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);

  // Insert an explicit fall through from the current block to the LoopBB.
  // 直接进入这个新块
  Builder->CreateBr(LoopBB);

  // Start insertion in LoopBB.
  // 开始在新块插入
  Builder->SetInsertPoint(LoopBB);

  // Within the loop, the variable is defined equal to the PHI node.  If it
  // shadows an existing variable, we have to restore it, so save it now.
  // 保存之前的变量值，循环结束会恢复它的
  AllocaInst *OldVal = NamedValues[VarName];
  NamedValues[VarName] = Alloca;

  // Emit the body of the loop.  This, like any other expr, can change the
  // current BB.  Note that we ignore the value computed by the body, but don't
  // allow an error.
  // 输出循环体，像其他表达式，可以改变当前块。注意这里忽略循环体的返回值，并且不允许错误
  if (!Body->codegen())
    return nullptr;

  // Emit the step value.
  // 输出step值
  Value *StepVal = nullptr;
  if (Step) {
    StepVal = Step->codegen();
    if (!StepVal)
      return nullptr;
  } else {
    // If not specified, use 1.0.
    // 不指定，就用1.0
    StepVal = ConstantFP::get(*TheContext, APFloat(1.0));
  }

  // Compute the end condition.
  // 计算结束条件
  Value *EndCond = End->codegen();
  if (!EndCond)
    return nullptr;

  // Reload, increment, and restore the alloca.  This handles the case where
  // the body of the loop mutates the variable.
  // 加载和递增和恢复之前那个变量，这里用来处理循环体里面修改了这个变量的情况
  Value *CurVar =
      Builder->CreateLoad(Alloca->getAllocatedType(), Alloca, VarName.c_str());
  Value *NextVar = Builder->CreateFAdd(CurVar, StepVal, "nextvar");
  Builder->CreateStore(NextVar, Alloca);

  // Convert condition to a bool by comparing non-equal to 0.0.
  // 转换条件到一个布尔值，并比较是否不等于0.0
  EndCond = Builder->CreateFCmpONE(
      EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");

  // Create the "after loop" block and insert it.
  // 创建循环后的块，并插入它
  BasicBlock *AfterBB =
      BasicBlock::Create(*TheContext, "afterloop", TheFunction);

  // Insert the conditional branch into the end of LoopEndBB.
  // 插入一个条件分支，用来判断是否结束还是继续循环
  Builder->CreateCondBr(EndCond, LoopBB, AfterBB);

  // Any new code will be inserted in AfterBB.
  // 设置后面的代码都要插入到块AfterBB
  Builder->SetInsertPoint(AfterBB);

  // Restore the unshadowed variable.
  // 恢复之前变量的值
  if (OldVal)
    NamedValues[VarName] = OldVal;
  else
    NamedValues.erase(VarName);

  // for expr always returns 0.0.
  // 一直返回0.0
  return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

// 变量定义生成代码
Value *VarExprAST::codegen() {
  std::vector<AllocaInst *> OldBindings;

  Function *TheFunction = Builder->GetInsertBlock()->getParent();

  // Register all variables and emit their initializer.
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i) {
    const std::string &VarName = VarNames[i].first;
    ExprAST *Init = VarNames[i].second.get();

    // Emit the initializer before adding the variable to scope, this prevents
    // the initializer from referencing the variable itself, and permits stuff
    // like this:
    //  var a = 1 in
    //    var a = a in ...   # refers to outer 'a'.
    // 在把变量放入作用域，先输出初始化，这样可以阻止引用自己，和实现像上面的东西
    Value *InitVal;
    if (Init) {
      InitVal = Init->codegen();
      if (!InitVal)
        return nullptr;
    } else { // If not specified, use 0.0. 默认为0.0
      InitVal = ConstantFP::get(*TheContext, APFloat(0.0));
    }

    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, VarName);
    Builder->CreateStore(InitVal, Alloca);

    // Remember the old variable binding so that we can restore the binding when
    // we unrecurse.
    // 保存之前的变量绑定，方便递归回来的时候恢复
    OldBindings.push_back(NamedValues[VarName]);

    // Remember this binding.
    // 记住这个变量绑定
    NamedValues[VarName] = Alloca;
  }

  // Codegen the body, now that all vars are in scope.
  // 输出内容，这样现在所有变量都在作用域
  Value *BodyVal = Body->codegen();
  if (!BodyVal)
    return nullptr;

  // Pop all our variables from scope.
  // 退出作用域时，恢复之前的变量
  for (unsigned i = 0, e = VarNames.size(); i != e; ++i)
    NamedValues[VarNames[i].first] = OldBindings[i];

  // Return the body computation.
  // 返回内容的值
  return BodyVal;
}

// 对原型函数输出代码
Function *PrototypeAST::codegen() {
  // Make the function type:  double(double,double) etc.
  // 创建函数类型：
  std::vector<Type *> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
  FunctionType *FT =
      FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

  Function *F =
      Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());

  // Set names for all arguments.
  // 给每个参数设置名字
  unsigned Idx = 0;
  for (auto &Arg : F->args())
    Arg.setName(Args[Idx++]);

  return F;
}

// 给函数生成代码
Function *FunctionAST::codegen() {
  // Transfer ownership of the prototype to the FunctionProtos map, but keep a
  // reference to it for use below.
  // 把原型函数放进FunctionProtos，保留一个引用给下面用
  auto &P = *Proto;
  FunctionProtos[Proto->getName()] = std::move(Proto);
  Function *TheFunction = getFunction(P.getName());
  if (!TheFunction)
    return nullptr;

  // If this is an operator, install it.
  // 如果是一个操作符，安装它
  if (P.isBinaryOp())
    BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

  // Create a new basic block to start insertion into.
  // 创建一个块，并开始插入它
  BasicBlock *BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
  Builder->SetInsertPoint(BB);

  // Record the function arguments in the NamedValues map.
  // 情况变量表
  NamedValues.clear();
  for (auto &Arg : TheFunction->args()) {
    // Create an alloca for this variable.
    // 为每个参数变量创建存储
    AllocaInst *Alloca = CreateEntryBlockAlloca(TheFunction, Arg.getName());

    // Store the initial value into the alloca.
    // 保存初始化值
    Builder->CreateStore(&Arg, Alloca);

    // Add arguments to variable symbol table.
    // 把参数加入到变量表
    NamedValues[std::string(Arg.getName())] = Alloca;
  }

  // 生成函数体代码
  if (Value *RetVal = Body->codegen()) {
    // Finish off the function.
    // 创建返回值
    Builder->CreateRet(RetVal);

    // Validate the generated code, checking for consistency.
    // 验证生成的代码，验证下一致性
    verifyFunction(*TheFunction);

    // Run the optimizer on the function.
    // 运行优化器优化下这个函数
    TheFPM->run(*TheFunction);

    return TheFunction;
  }

  // Error reading body, remove function.
  // 读取函数体出错了，移除函数
  TheFunction->eraseFromParent();

  // 移除操作符
  if (P.isBinaryOp())
    BinopPrecedence.erase(P.getOperatorName());
  return nullptr;
}