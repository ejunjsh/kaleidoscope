#ifndef LEXER_H
#define LEXER_H

//===----------------------------------------------------------------------===//
// Lexer
// 词法分析
//===----------------------------------------------------------------------===//

// The lexer returns tokens [0-255] if it is an unknown character, otherwise one
// of these for known things.
// 词法分析解析不认识的字符时会返回[0-255]范围的值，否则会返回下面的关键字
enum Token {
  tok_eof = -1,  // 文件结束

  // commands
  // 定义和声明方法
  tok_def = -2,
  tok_extern = -3,

  // primary
  // 表达式由变量和字面量（数字）组成
  tok_identifier = -4,
  tok_number = -5,

  // control
  // 控制流程相关关键字
  tok_if = -6,
  tok_then = -7,
  tok_else = -8,
  tok_for = -9,
  tok_in = -10,

  // operators
  // 操作符相关
  tok_binary = -11, // 一元操作符
  tok_unary = -12, // 二元操作符

  // var definition
  // 变量定义
  tok_var = -13
};

/// gettok - Return the next token from standard input.
/// 返回标准输入的下一个关键字
int gettok() ;

extern std::string IdentifierStr; // Filled in if tok_identifier 由处理tok_identifier时填充
extern double NumVal;             // Filled in if tok_number   由处理tok_number时填充

#endif // LEXER_H