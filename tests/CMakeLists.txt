add_subdirectory(Catch2)
set_target_properties(Catch2 PROPERTIES CXX_STANDARD 17)

add_executable(flyweight_test flyweight_test.cpp)
target_link_libraries(flyweight_test flyweight.hpp Catch2::Catch2WithMain)
set_target_properties(flyweight_test PROPERTIES CXX_STANDARD 17)

add_test(NAME test COMMAND flyweight_test)
