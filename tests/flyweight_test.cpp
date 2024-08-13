#include <catch2/catch_test_macros.hpp>
#include <flyweight.hpp>

TEST_CASE("flyweight<int, int>", "[flyweight]") {
    flyweight::flyweight<int, int> ints;
    SECTION("dummy") {
        auto one = ints.get(1);
        REQUIRE(*one == 1);
    }
}
