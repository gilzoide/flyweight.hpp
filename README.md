# flyweight.hpp
Single header implementation of the [Flyweight](https://en.wikipedia.org/wiki/Flyweight_pattern) design pattern for C++11.


## Features
- Single header: copy [flyweight.hpp](flyweight.hpp) to your project, `#include "flyweight.hpp"` and that's it
- Supports C++11 and above
- Generic implementation that supports any value type, as well as any number of argument types
- Use `flyweight::get` to get values.
  The first time a set of arguments is passed, the value will be created.
  Subsequent calls with the same parameters return the same value reference.
- Use `flyweight::release` to release values, destroying them and releasing memory
- Supports custom creator functors when the flyweight object is got for the first time
- Supports custom deleter functors when the object is released
- Use `flyweight::get_autorelease` for a RAII idiom that automatically releases values
- Alternative `flyweight_refcounted` that employs reference counting.
  Reference counts are incremented when calling `get` and decremented when calling `release`.
  The value is destroyed only when the reference count reaches zero.


## Usage example
String interning implementation:
```cpp
#include <assert>
#include <string>
#include <string_view>
#include "flyweight.hpp"

// 1. Define your flyweight instance.
// By default, values will be constructed using the passed arguments.
flyweight::flyweight<const std::string, std::string_view> interned_strings;

// 2. Get values.
// The first time the value will be created.
const std::string& some_string = interned_strings.get("some string");
assert(interned_strings.is_loaded("some string"));
assert(some_string == "some string");
// Subsequent gets will return the same reference to the same value.
const std::string& also_some_string = interned_strings.get("some string");
assert(&some_string == &also_some_string);

// 3. Release values when you don't need nor want them anymore.
interned_strings.release("some string");
assert(!interned_strings.is_loaded("some string"));
```

File data caching with reference counting:
```cpp
#include <assert>
#include <string_view>
#include <vector>
#include "flyweight.hpp"

using file_data = std::vector<uint8_t>;

// 1. Define your flyweight instance.
// In this case, we use the reference count enabled flyweight implementation.
flyweight::flyweight_refcounted<file_data, std::string_view> file_data_cache {
    // (optional) Pass a creator functor that will be called to create values.
    [](std::string_view image_name) {
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
```


## Integrating with CMake
You can integrate flyweight.hpp with CMake targets by adding a copy of this repository and linking with the `flyweight.hpp` target:
```cmake
add_subdirectory(path/to/flyweight.hpp)
target_link_libraries(my_awesome_target flyweight.hpp)
```
