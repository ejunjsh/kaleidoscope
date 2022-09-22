#ifndef HELPER_H
#define HELPER_H

/// LogError* - These are little helper functions for error handling.
inline std::unique_ptr<ExprAST> LogError(const char *Str) {
  fprintf(stderr, "Error: %s\n", Str);
  return nullptr;
}

inline std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
  LogError(Str);
  return nullptr;
}

#endif // HELPER_H