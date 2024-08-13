#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <flyweight.hpp>

TEST_CASE("flyweight<int, int>", "[flyweight]") {
	flyweight::flyweight<int, int> ints;
	SECTION("dummy") {
		auto one = ints.get(1);
		REQUIRE(*one == 1);
	}
}

TEST_CASE("flyweight<std::string, std::string_view>", "[flyweight]") {
	flyweight::flyweight_autorelease<std::string, std::string_view> ints;
	SECTION("dummy") {
		auto one = ints.get("Test 1");
		REQUIRE(*one == "Test 1");
	}
}
