//===----------------------------------------------------------------------===//
// "Library" functions that can be "extern'd" from user code.
// 库函数，可以由用户代码调用的函数
//===----------------------------------------------------------------------===//
#include <cstdio>

#ifdef _WIN32
#define DLLEXPORT __declspec(dllexport)
#else
#define DLLEXPORT
#endif

/// putchard - putchar that takes a double and returns 0.
// 打印一个字符
extern "C" DLLEXPORT double putchard(double X) {
  fputc((char)X, stderr);
  return 0;
}

/// printd - printf that takes a double prints it as "%f\n", returning 0.
// 打印一个双精度浮点数
extern "C" DLLEXPORT double printd(double X) {
  fprintf(stderr, "%f\n", X);
  return 0;
}