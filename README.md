# kaleidoscope

````
# Compile
clang++ -g -O3 toy.cc `llvm-config --cxxflags --ldflags --system-libs --libs core` -o toy
# Run
./toy
````
