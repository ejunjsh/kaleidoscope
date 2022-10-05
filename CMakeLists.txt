cmake_minimum_required(VERSION 3.13.4)
set(CMAKE_CXX_STANDARD 17)
project(kaleidocscope)


find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(kaleidocscope codegen.cc driver.cc lexer.cc parser.cc ulib.cc)

llvm_map_components_to_libnames(llvm_libs core orcjit native)

# Link against LLVM libraries
target_link_libraries(kaleidocscope ${llvm_libs})