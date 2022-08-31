# kaleidoscope

````
# Compile
clang++ -g toy.cc `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o toy
# Run
./toy
````
