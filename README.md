# kaleidoscope

My First Language Frontend with LLVM Tutorial

## 前提

根据下面链接安装llvm

https://llvm.org/docs/CMake.html#quick-start 

## 编译

````
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR={path to llvm config cmake} .. && cmake --build .
# {path to llvm config cmake} 是llvm的安装路径下LLVMConfig.cmake所在的目录，
# 一般你llvm是安装到标准的目录，cmake在查找包能找到话，可以不加-DLLVM_DIR 这个选项
````
这时候就会生成一个可执行文件kaleidocscope,运行它
````
$ ./kaleidocscope
````

## 语法

只有一个类型，浮点类型

### 支持函数定义：
````
ready> def add(x y) x+y;
ready> add(1,3);
ready> Evaluated to 4.000000
````

### 支持外部函数：
````
ready> extern sin(x);
ready> sin(1.0);
ready> Evaluated to 0.841471
````

### 支持条件判断
````
ready> def fib(x)                          
        if x < 3 then
            1
        else
            fib(x-1)+fib(x-2);
ready> fib(4);  
ready> Evaluated to 3.000000
````

### 支持循环
````
extern putchard(char);
def printstar(n)
  for i = 1, i < n, 1.0 in
    putchard(42);  # ascii 42 = '*'

# print 100 '*' characters
printstar(100);
````

### 支持操作符重载

#### 二元操作符重载

下面代码实现了对`:`的重载，实现了类似多表达式的（多条语句）语法
````
ready> extern printd(x);
ready> def binary : 1 (x y) 0;  # Low-precedence operator that ignores operands.
ready> printd(123) : printd(456) : printd(789);
123.000000
456.000000
789.000000
Evaluated to 0.000000
````

#### 一元操作符重载

````
ready> def unary!(v)
  if v then
    0
  else
    1;
ready> !1;
ready> Evaluated to 0.000000
ready> !0;
ready> Evaluated to 1.000000
````

### 支持定义变量
````
# Define ':' for sequencing: as a low-precedence operator that ignores operands
# and just returns the RHS.
def binary : 1 (x y) y;

def fibi(x)
  var a = 1, b = 1, c in
  (for i = 3, i < x in
     c = a + b :
     a = b :
     b = c) :
  b;

# Call it.
fibi(10);
# Evaluated to 55.000000
````

## 参考

https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html

