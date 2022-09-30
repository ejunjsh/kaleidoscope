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

//===----------------------------------------------------------------------===//
// Syntax  Parser 
// 语法分析
//===----------------------------------------------------------------------===//

/// CurTok/getNextToken - Provide a simple token buffer.  CurTok is the current
/// token the parser is looking at.  getNextToken reads another token from the
/// lexer and updates CurTok with its results.
/// 下面两个提供了简单的关键字缓存，CurTok保存解析器当前的关键字，getNextToken从词法分析获得下个关键字，并保存到CurTok
int CurTok;
int getNextToken() { return CurTok = gettok(); }

/// BinopPrecedence - This holds the precedence for each binary operator that is
/// defined.
// 保存二元操作符的优先级
std::map<char, int> BinopPrecedence;

/// GetTokPrecedence - Get the precedence of the pending binary operator token.
// 返回操作符对应的优先级
int GetTokPrecedence() {
  if (!isascii(CurTok))
    return -1;

  // Make sure it's a declared binop.
  // 确认是定义过的二元操作
  int TokPrec = BinopPrecedence[CurTok];
  if (TokPrec <= 0)
    return -1;
  return TokPrec;
}



std::unique_ptr<ExprAST> ParseExpression();

/// numberexpr ::= number
/// 解析数字字面量
std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken(); // consume the number 跳过这个数字
  return std::move(Result);
}

/// parenexpr ::= '(' expression ')'
/// 解析带括号的表达式
std::unique_ptr<ExprAST> ParseParenExpr() {
  getNextToken(); // eat (. 跳过左括号
  auto V = ParseExpression();
  if (!V)
    return nullptr;

  if (CurTok != ')')
    return LogError("expected ')'");
  getNextToken(); // eat ). 跳过右括号
  return V;
}

/// identifierexpr
///   ::= identifier
///   ::= identifier '(' expression* ')'
// 解析标识符（有可能是一个变量，有可能是函数调用）
std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;

  getNextToken(); // eat identifier. 跳过标识符

  if (CurTok != '(') // Simple variable ref. 简单的变量引用
    return std::make_unique<VariableExprAST>(IdName);

  // Call.
  // 否则就是函数调用
  getNextToken(); // eat ( 跳过左括号
  // 遍历，找到所有参数
  std::vector<std::unique_ptr<ExprAST>> Args;
  if (CurTok != ')') {
    while (true) {
      if (auto Arg = ParseExpression()) // 参数也可能是表达式
        Args.push_back(std::move(Arg));
      else
        return nullptr;

      if (CurTok == ')')
        break;

      if (CurTok != ',')
        return LogError("Expected ')' or ',' in argument list");
      getNextToken();
    }
  }

  // Eat the ')'.跳过右括号
  getNextToken();

  return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/// ifexpr ::= 'if' expression 'then' expression 'else' expression
/// 解析条件表达式
std::unique_ptr<ExprAST> ParseIfExpr() {
  getNextToken(); // eat the if. 跳过if

  // condition. 
  // 解析条件里面的表达式
  auto Cond = ParseExpression();
  if (!Cond)
    return nullptr;

  if (CurTok != tok_then)
    return LogError("expected then");
  getNextToken(); // eat the then 跳过then

  auto Then = ParseExpression(); // 解析then里面的表达式
  if (!Then)
    return nullptr;

  if (CurTok != tok_else)
    return LogError("expected else");

  getNextToken();

  auto Else = ParseExpression(); // 解析else里面的表达式
  if (!Else)
    return nullptr;

  return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then),
                                      std::move(Else));
}

/// forexpr ::= 'for' identifier '=' expr ',' expr (',' expr)? 'in' expression
/// 解析for循环表达式
std::unique_ptr<ExprAST> ParseForExpr() {
  getNextToken(); // eat the for. 跳过for

  if (CurTok != tok_identifier)
    return LogError("expected identifier after for");

  std::string IdName = IdentifierStr;
  getNextToken(); // eat identifier. 跳过标识符

  if (CurTok != '=')
    return LogError("expected '=' after for");
  getNextToken(); // eat '='. 跳过‘=’

  auto Start = ParseExpression(); // 解析标识符=后面的表达式
  if (!Start)
    return nullptr;
  if (CurTok != ',')
    return LogError("expected ',' after for start value");
  getNextToken();

  auto End = ParseExpression(); // 解析循环判断条件表达式
  if (!End)
    return nullptr;

  // The step value is optional.
  // 循环步长是可选的
  std::unique_ptr<ExprAST> Step;
  if (CurTok == ',') {
    getNextToken();
    Step = ParseExpression(); // 解析步长的表达式
    if (!Step)
      return nullptr;
  }

  if (CurTok != tok_in)
    return LogError("expected 'in' after for");
  getNextToken(); // eat 'in'. 跳过‘in’

  auto Body = ParseExpression(); // 解析循环内容表达式
  if (!Body)
    return nullptr;

  return std::make_unique<ForExprAST>(IdName, std::move(Start), std::move(End),
                                       std::move(Step), std::move(Body));
}

/// varexpr ::= 'var' identifier ('=' expression)?
//                    (',' identifier ('=' expression)?)* 'in' expression
// 解析定义变量表达式
std::unique_ptr<ExprAST> ParseVarExpr() {
  getNextToken(); // eat the var. 跳过var

  std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> VarNames;

  // At least one variable name is required.
  // 至少要定义一个变量吧
  if (CurTok != tok_identifier)
    return LogError("expected identifier after var");

  while (true) {
    std::string Name = IdentifierStr;
    getNextToken(); // eat identifier. 跳过标识符

    // Read the optional initializer.
    // 处理可选的初始化
    std::unique_ptr<ExprAST> Init = nullptr;
    if (CurTok == '=') {
      getNextToken(); // eat the '='. 跳过‘=’

      Init = ParseExpression(); // 解析初始化的表达式
      if (!Init)
        return nullptr;
    }

    VarNames.push_back(std::make_pair(Name, std::move(Init)));

    // End of var list, exit loop.
    // 定义变量结束，退出循环
    if (CurTok != ',')
      break;
    getNextToken(); // eat the ','. 跳过‘,’

    if (CurTok != tok_identifier)
      return LogError("expected identifier list after var");
  }

  // At this point, we have to have 'in'.
  // 处理‘in’
  if (CurTok != tok_in)
    return LogError("expected 'in' keyword after 'var'");
  getNextToken(); // eat 'in'. 跳过‘in’

  auto Body = ParseExpression(); // 解析内容表达式
  if (!Body)
    return nullptr;

  return std::make_unique<VarExprAST>(std::move(VarNames), std::move(Body));
}

/// primary
///   ::= identifierexpr
///   ::= numberexpr
///   ::= parenexpr
///   ::= ifexpr
///   ::= forexpr
///   ::= varexpr
/// primary 表示操作符两边的表达式
std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
  default:
    return LogError("unknown token when expecting an expression");
  case tok_identifier:
    return ParseIdentifierExpr();
  case tok_number:
    return ParseNumberExpr();
  case '(':
    return ParseParenExpr();
  case tok_if:
    return ParseIfExpr();
  case tok_for:
    return ParseForExpr();
  case tok_var:
    return ParseVarExpr();
  }
}

/// unary
///   ::= primary
///   ::= '!' unary
/// 解析一元表达式
std::unique_ptr<ExprAST> ParseUnary() {
  // If the current token is not an operator, it must be a primary expr.
  // 如果当前关键字不是一个操作符，那就肯定是primary表达式
  if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
    return ParsePrimary();

  // If this is a unary operator, read it.
  // 一元操作符，解析它
  int Opc = CurTok;
  getNextToken();
  if (auto Operand = ParseUnary()) // 递归下去，直到把所有一元解析出来
    return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
  return nullptr;
}

/// binoprhs
///   ::= ('+' unary)*
/// 解析二元操作（这个函数很巧妙，建议看多几次才明白）
std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec,
                                              std::unique_ptr<ExprAST> LHS) {
  // If this is a binop, find its precedence.
  // 根据优先级处理二元操作
  while (true) {
    int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    // 如果这是一个至少与当前二元操作绑定一样紧密的二元操作，那么就使用它，否则我们就返回。
    // 通俗点就是遇到更小的优先级的操作符了，或者不是操作符，就退出（这里有点难，建议看多几次理解下）
    if (TokPrec < ExprPrec)
      return LHS;

    // Okay, we know this is a binop.
    // 取出二元操作符
    int BinOp = CurTok;
    getNextToken(); // eat binop 跳过这个二元操作符

    // Parse the unary expression after the binary operator.
    // 解析二元操作符后面的一元表达式
    auto RHS = ParseUnary();
    if (!RHS)
      return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    // 如果BinOp与RHS的绑定比RHS之后的操作符更紧密，那么让挂起的操作符将RHS作为其LHS。
    // 通俗点就是遇到更高优先级的了，就在调用这个ParseBinOpRHS函数递归进去，直到遇到更小的优先级才退出（这里有点难，建议看多几次理解下）
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS)
        return nullptr;
    }

    // Merge LHS/RHS.
    // 合并左右节点
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/// expression
///   ::= unary binoprhs
///
/// 表达式解析的主要入口函数
std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParseUnary();
  if (!LHS)
    return nullptr;

  return ParseBinOpRHS(0, std::move(LHS));
}

/// prototype
///   ::= id '(' id* ')'
///   ::= binary LETTER number? (id, id)
///   ::= unary LETTER (id)
/// 解析原型（函数声明）（包括普通函数和操作符重载的函数）
std::unique_ptr<PrototypeAST> ParsePrototype() {
  std::string FnName;

  unsigned Kind = 0; // 0 = identifier, 1 = unary, 2 = binary.
  unsigned BinaryPrecedence = 30;

  switch (CurTok) {
  default:
    return LogErrorP("Expected function name in prototype");
  case tok_identifier:
    FnName = IdentifierStr;
    Kind = 0;
    getNextToken();
    break;
  case tok_unary:
    getNextToken();
    if (!isascii(CurTok))
      return LogErrorP("Expected unary operator");
    FnName = "unary";
    FnName += (char)CurTok;
    Kind = 1;
    getNextToken();
    break;
  case tok_binary:
    getNextToken();
    if (!isascii(CurTok))
      return LogErrorP("Expected binary operator");
    FnName = "binary";
    FnName += (char)CurTok;
    Kind = 2;
    getNextToken();

    // Read the precedence if present.
    // 如果有优先级，就读取他
    if (CurTok == tok_number) {
      if (NumVal < 1 || NumVal > 100)
        return LogErrorP("Invalid precedence: must be 1..100");
      BinaryPrecedence = (unsigned)NumVal;
      getNextToken();
    }
    break;
  }

  if (CurTok != '(')
    return LogErrorP("Expected '(' in prototype");

  std::vector<std::string> ArgNames;
  while (getNextToken() == tok_identifier)
    ArgNames.push_back(IdentifierStr);
  if (CurTok != ')')
    return LogErrorP("Expected ')' in prototype");

  // success.
  getNextToken(); // eat ')'. 跳过‘)’

  // Verify right number of names for operator.
  // 验证操作符重载的参数是否正确
  if (Kind && ArgNames.size() != Kind)
    return LogErrorP("Invalid number of operands for operator");

  return std::make_unique<PrototypeAST>(FnName, ArgNames, Kind != 0,
                                         BinaryPrecedence);
}

/// definition ::= 'def' prototype expression
/// 解析函数定义表达式
 std::unique_ptr<FunctionAST> ParseDefinition() {
  getNextToken(); // eat def. 跳过'def'
  auto Proto = ParsePrototype();
  if (!Proto)
    return nullptr;

  if (auto E = ParseExpression())
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  return nullptr;
}

/// toplevelexpr ::= expression
/// 解析顶层表达式（JIT执行开始的就是顶层表达式）
 std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
  if (auto E = ParseExpression()) {
    // Make an anonymous proto.
    // 创建一个匿名的原型
    auto Proto = std::make_unique<PrototypeAST>("__anon_expr",
                                                 std::vector<std::string>());
    return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
  }
  return nullptr;
}

/// external ::= 'extern' prototype
/// 解析外部函数原型，用来引用外部的函数
 std::unique_ptr<PrototypeAST> ParseExtern() {
  getNextToken(); // eat extern. 跳过‘extern’
  return ParsePrototype();
}