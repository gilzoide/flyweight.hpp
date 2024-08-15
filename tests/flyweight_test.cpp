#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <flyweight.hpp>

#undef assert
#define assert(...) REQUIRE(__VA_ARGS__)
TEST_CASE("README.md examples", "[flyweight][readme]") {
	SECTION("File data") {
		using file_data = std::vector<uint8_t>;

		// 1. Define your flyweight instance.
		// In this case, we use the reference count enabled flyweight implementation.
		flyweight::flyweight_refcounted<file_data, std::string> file_data_cache {
			// (optional) Pass a creator functor that will be called to create values.
			[](const std::string& image_name) {
				file_data data;
				// read file data into vector...
				return data;
			},
			// (optional) Pass a deleter functor that will be called when values are released.
			// No need in this case, std::vector will delete the memory automatically when released.
		};

		// 2. Get values.
		// The first time the value will be created.
		file_data& file1_data = file_data_cache.get("file1");
		assert(file_data_cache.is_loaded("file1"));
		// At this point, the reference count for "file1" is 1
		assert(file_data_cache.reference_count("file1") == 1);
		// Subsequent gets will increment the reference count by 1.
		file_data& also_file1_data = file_data_cache.get("file1");
		assert(file_data_cache.reference_count("file1") == 2);

		// 3. Release values when you don't need nor want them anymore.
		// This decrements the reference count by 1.
		file_data_cache.release("file1");
		assert(file_data_cache.reference_count("file1") == 1);
		// The value is only actually unloaded when the reference count reaches zero.
		assert(file_data_cache.is_loaded("file1"));


		// 4. You can use `get_autorelease` for RAII style automatic release to the flyweight.
		{
			auto autoreleased_file1_data = file_data_cache.get_autorelease("file1");
			assert(file_data_cache.reference_count("file1") == 2);
			// Autoreleased values are simply wrappers to the original value reference.
			file_data& file1_data_again = autoreleased_file1_data.value;
			assert(&file1_data_again == &file1_data);
		}
		// At this point, `autoreleased_file1_data` released "file1" back to the flyweight.
		assert(file_data_cache.reference_count("file1") == 1);
	}
}
