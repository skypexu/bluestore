set(SRC_EXAMPLE1
    example1.cc)

add_executable(example1
  example1.cc)
target_link_libraries(example1 os global pthread)

