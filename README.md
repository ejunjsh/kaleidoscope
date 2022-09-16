# kaleidoscope

````
$ clang++ -g -O3 toy.cc `llvm-config --cxxflags --ldflags --system-libs --libs all` -o toy
$ ./toy
ready> def average(x y) (x + y) * 0.5; # ctrl+d
$ clang++ main.cc output.o -o main
$ ./main
````


