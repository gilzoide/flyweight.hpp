add_subdirectory(Catch2)

add_executable(flyweight_test flyweight_test.cpp)
target_link_libraries(flyweight_test flyweight.hpp Catch2::Catch2WithMain)
set_target_properties(flyweight_test PROPERTIES CXX_STANDARD 11)

add_test(NAME test COMMAND flyweight_test)
