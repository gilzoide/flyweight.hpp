#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <flyweight.hpp>

TEST_CASE("flyweight<int, int>", "[flyweight]") {
	flyweight::flyweight_refcounted<int, int> ints { [](int i) { return i; }, [](int&) {} };
	SECTION("dummy") {
		auto one = ints.get_tuple(1);
		REQUIRE(one == 1);
		REQUIRE(ints.reference_count(1) == 1);

		auto other_one = ints.get_autorelease_tuple(1);
		REQUIRE(ints.reference_count(1) == 2);

		ints.release(1);
		REQUIRE(ints.reference_count(1) == 1);

		ints.release(1);
		REQUIRE(ints.reference_count(1) == 0);
		ints.release(1);
		REQUIRE(ints.reference_count(1) == 0);
	}
}

TEST_CASE("flyweight<const std::string, std::string_view>", "[flyweight]") {
	flyweight::flyweight<const std::string, std::string_view> strings;
	SECTION("dummy") {
		auto& one = strings.get("Test 1");
		REQUIRE(one == "Test 1");
	}
}
