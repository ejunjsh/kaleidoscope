# kaleidoscope

My First Language Frontend with LLVM Tutorial

## 前提

根据下面链接安装llvm

https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project

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

## 参考

https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html

