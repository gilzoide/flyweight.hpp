/** @file flyweight.hpp
 * Single header with a templated implementation of the Flyweight design pattern
 *
 * Example of asset management using flyweight objects:
 * ```cpp
 * #include "flyweight.hpp"
 *
 * // 1. Declare your flyweight instance.
 * // The first template type is the key type.
 * // The second template type is the value type that will be created with the passed key.
 * // The third optional template type is the map type used to maintain the values, defaults to std::unordered_map.
 * // In this example, we're defining a resource manager for images.
 * flyweight::flyweight<std::string, Image> my_image_flyweight {
 *     // (optional) Pass a creator functor that will be called to create values.
 *     [](const std::string& image_name) {
 *         return LoadImage(image_name);
 *     },
 *     // (optional) Pass a deleter functor that will be called when values are released.
 *     [](Image& image) {
 *         UnloadImage(image);
 *     },
 * };
 *
 *
 * // 2. Get values from the flyweight.
 * // The first time the value will be created.
 * Image& image1 = my_image_flyweight.get("image1");
 * assert(my_image_flyweight.is_loaded("image1"));
 * // Subsequent gets will return the same reference to the same value.
 * Image& also_image1 = my_image_flyweight.get("image1");
 * assert(&image1 == &also_image1);
 *
 *
 * // 3. When you don't need values anymore, release them to the flyweight again.
 * my_image_flyweight.release("image1");
 * assert(!my_image_flyweight.is_loaded("image1"));
 * // Releasing a value that is not loaded is a harmless no-op.
 * my_image_flyweight.release("image that's not loaded");
 *
 *
 * // 4. (optional) Use RAII to automatically release a value by getting it with `get_autorelease`.
 * {
 *     auto autoreleased_image1 = my_image_flyweight.get_autorelease("image1");
 *     assert(my_image_flyweight.is_loaded("image1"));
 *     // Autoreleased values are simple wrappers to the original value reference.
 *     Image& image1 = autoreleased_image1;
 * }
 * // At this point "image1" was automatically released.
 * assert(!my_image_flyweight.is_loaded("image1"));
 * ```
 */
/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 *
 * In jurisdictions that recognize copyright laws, the author or authors
 * of this software dedicate any and all copyright interest in the
 * software to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and
 * successors. We intend this dedication to be an overt act of
 * relinquishment in perpetuity of all present and future rights to this
 * software under copyright law.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * For more information, please refer to <http://unlicense.org/>
 */
#ifndef __FLYWEIGHT_HPP__
#define __FLYWEIGHT_HPP__

#include <functional>
#include <mutex>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace flyweight {

namespace detail {
	/// Reference counted value, used for flyweight_refcounted
	template<typename T>
	struct refcounted_value {
		T value;
		long long refcount = 0;

		/// Construct a value with an initial reference count of 0.
		/// `reference` should be called right after constructing this.
		refcounted_value(T&& value) : value(value) {}

		operator T&() {
			return value;
		}
		operator const T&() const {
			return value;
		}

		/// Increment the reference count.
		refcounted_value& reference() {
			refcount++;
			return *this;
		}

		/// Decrement the reference count.
		/// @return `true` in case the count reached zero, `false` otherwise.
		bool dereference() {
			--refcount;
			return refcount <= 0;
		}
	};

	struct dummy_mutex {};
	struct dummy_lock {
		template<typename T> dummy_lock(T) {}
	};
}


/// The default Creator functor used by flyweight.
/// Constructs the value `T` from arguments of type `Args`.
/// @tparam T  Type that will be created.
/// @tparam Args...  Arguments used to construct the value.
template<typename T, typename... Args>
struct default_creator {
	/// Default creator implementation, just call the value constructor forwarding the passed arguments.
	T operator()(Args&&... args) {
		return T { std::forward<Args>(args)... };
	}
};

/// The default Deleter functor used by flyweight.
/// Doesn't do anything, as the value's destructor will be called when released from the flyweight.
/// @tparam T  Type that will be deleted.
template<typename T>
struct default_deleter {
	/// Default deleter implementation, a no-op.
	void operator()(T&) {
		// no-op
	}
};

/// Value wrapper that releases it back to the owning flyweight upon destruction.
template<typename Key, typename T, typename Flyweight>
struct autorelease_value {
	T& value;

	/// Constructor.
	autorelease_value(Flyweight& flyweight, const Key& key)
		: value(flyweight.get(key))
		, flyweight(flyweight)
		, key(key)
	{
	}

	/// Copy constructor.
	autorelease_value(const autorelease_value& other) : autorelease_value(other.flyweight, other.key) {}

	/// Copy assignment.
	/// Releases the previously referenced value and calls `flyweight.get` to make sure reference counting is correct.
	autorelease_value& operator=(const autorelease_value& other)
	{
		// release previous value
		flyweight.release(key);
		// re-get the value to make sure reference counting is correct
		value = other.flyweight.get(other.key);
		flyweight = other.flyweight;
		key = other.key;
	}

	/// Returns the wrapped value.
	T& operator*() {
		return value;
	}
	const T& operator*() const {
		return value;
	}
	/// Returns the wrapped value.
	T& operator->() {
		return value;
	}
	const T& operator->() const {
		return value;
	}
	/// Returns the wrapped value.
	operator T&() {
		return value;
	}
	operator const T&() const {
		return value;
	}

	/// Release the value back to the owning flyweight.
	~autorelease_value() {
		flyweight.release(key);
	}

private:
	Flyweight& flyweight;
	Key key;
};

/**
 * Factory for flyweight objects of type `T`, created with a key of type `Key`.
 *
 * When getting a value, the flyweight first checks for an existing value associated with the passed key, returning a reference to it if found.
 * Otherwise, a new value is created and cached for future requests.
 * When no longer needed, an existing value may be released by calling `flyweight::release`.
 *
 * Flyweights are useful as a way to load heavy objects only once and sharing them whenever necessary, for example images used as icons in an interactive app.
 * Flyweights can also be used to implement string interning and function result memoization.
 *
 * @tparam Key  Key mapped to loaded values.
 * @tparam T  Value type.
 * @tparam Map  Internal type used to map keys to values. Defaults to `std::unordered_map`.
 * @tparam Mutex  Internal type used for a mutex. Defaults to `detail::dummy_mutex`.
 * @tparam Lock  Internal type used for locking the mutex. Defaults to `detail::dummy_lock`.
 */
template<typename Key, typename T, typename Map = std::unordered_map<Key, T>, typename Mutex = detail::dummy_mutex, typename Lock = detail::dummy_lock>
class flyweight {
public:
	using key_type = Key;
	using value_type = T;
	using autorelease_value_type = autorelease_value<Key, T, flyweight>;

	/// Default constructor.
	/// Uses `default_creator` as the value creator and `default_deleter` as the value deleter.
	flyweight() : creator(default_creator<T, Key>{}), deleter(default_deleter<T>{}) {}

	/// Constructor with custom value creator functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with a const reference to the key passed to `flyweight::get`.
	template<typename Creator>
	flyweight(Creator&& creator)
		: creator([creator](const Key& key) { return creator(key); })
		, deleter(default_deleter<T>{})
	{
	}

	/// Constructor with custom value creator functor and deleter functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with a const reference to the key passed to `flyweight::get`.
	/// @param deleter  Deleter functor that will be called when releasing a mapped value.
	///                 It will be called by `flyweight::release` with a reference to the value.
	template<typename Creator, typename Deleter>
	flyweight(Creator&& creator, Deleter&& deleter)
		: creator([creator](const Key& key) { return creator(key); })
		, deleter([deleter](T& value) { deleter(value); })
	{
	}

	/// Calls the deleter functor to all remaining values, to ensure everything is cleaned up properly.
	~flyweight() {
		Lock lock { mutex };
		for (auto it : map) {
			deleter(it.second);
		}
	}

	/// Gets the value associated to the passed key.
	/// If the value was already created, a reference to the existing value is returned.
	/// Otherwise, the value is created using the creator functor passed on the flyweight's constructor.
	/// @param key Key that represent a value.
	///            It will be passed to the creator functor if the value is not loaded yet.
	/// @return Reference to the value mapped to the passed key.
	T& get(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it == map.end()) {
			it = map.emplace(key, creator(key)).first;
		}
		return it->second;
	}

	/// Alternative to `flyweight::get` that returns an `autorelease_value`.
	/// This enables the RAII idiom for automatically releasing values.
	/// @see get
	autorelease_value_type get_autorelease(const Key& key) {
		return {
			*this,
			key,
		};
	}

	/// Gets the existing value associated to the passed key.
	/// If the value was not created yet, returns `nullptr`.
	/// @param key Key that represents a value.
	/// @return Pointer to the existing value, or `nullptr` if the value is not loaded.
	T *peek(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it == map.end()) {
			return nullptr;
		}
		else {
			return &it->second;
		}
	}

	/// Check whether the value mapped to the passed key is loaded.
	bool is_loaded(const Key& key) {
		Lock lock { mutex };
		return map.find(key) != map.end();
	}

	/// Release the value mapped to the passed key.
	/// Trying to release a value that is not loaded is a no-op.
	/// @return `true` if a loaded value was released, `false` otherwise.
	bool release(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it != map.end()) {
			deleter(it->second);
			map.erase(it);
			return true;
		}
		else {
			return false;
		}
	}

	/// Release all values, calling the deleter functor on them.
	void clear() {
		Lock lock { mutex };
		for (auto it : map) {
			deleter(it.second);
		}
		map.clear();
	}

protected:
	/// Value map.
	/// Maps the tuple of arguments to an already loaded value of type `T`.
	Map map;
	/// Creator function.
	/// Wraps the creator functor passed when constructing the flyweight, if any.
	std::function<T(const Key&)> creator;
	/// Deleter function.
	/// Wraps the deleter functor passed when constructing the flyweight, if any.
	std::function<void(T&)> deleter;
	Mutex mutex;
};

/**
 * Alternative to `flyweight` that uses `std::mutex` and `std::lock_guard` for thread safety.
 */
template<typename Key, typename T, typename Map = std::unordered_map<Key, T>>
using flyweight_threadsafe = flyweight<Key, T, Map, std::mutex, std::lock_guard<std::mutex>>;

/**
 * Factory for flyweight objects of type `T`, created with a key of type `Key`, that employs reference counting.
 *
 * The reference count increments for each call to `flyweight_refcounted::get` and decrements for each call to `flyweight_refcounted::release` using the same key.
 * When calling `flyweight_refcounted::release`, the value is actually deleted from the cache only when the reference count reaches zero.
 *
 * This is specially useful for implementing resource management of heavy objects, like images used as icons in an interactive app.
 *
 * @tparam Key  Key mapped to loaded values.
 * @tparam T  Value type.
 * @tparam Map  Internal type used to map keys to values. Defaults to `std::unordered_map`.
 * @tparam Mutex  Internal type used for a mutex. Defaults to `detail::dummy_mutex`.
 * @tparam Lock  Internal type used for locking the mutex. Defaults to `detail::dummy_lock`.
 */
template<typename Key, typename T, typename Map = std::unordered_map<Key, detail::refcounted_value<T>>, typename Mutex = detail::dummy_mutex, typename Lock = detail::dummy_lock>
class flyweight_refcounted {
public:
	using key_type = Key;
	using value_type = T;
	using autorelease_value_type = autorelease_value<Key, T, flyweight_refcounted>;

	/// Default constructor.
	/// Uses `default_creator` as the value creator and `default_deleter` as the value deleter.
	flyweight_refcounted() : creator(default_creator<T, Key>{}), deleter(default_deleter<T>{}) {}

	/// Constructor with custom value creator functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with a const reference to the key passed to `flyweight_refcounted::get`.
	template<typename Creator>
	flyweight_refcounted(Creator&& creator)
		: creator([creator](const Key& key) { return creator(key); })
		, deleter(default_deleter<T>{})
	{
	}

	/// Constructor with custom value creator functor and deleter functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with a const reference to the key passed to `flyweight_refcounted::get`.
	/// @param deleter  Deleter functor that will be called when releasing a mapped value.
	///                 It will be called by `flyweight_refcounted::release` with a reference to the value.
	template<typename Creator, typename Deleter>
	flyweight_refcounted(Creator&& creator, Deleter&& deleter)
		: creator([creator](const Key& key) { return creator(key); })
		, deleter([deleter](T& value) { deleter(value); })
	{
	}

	/// Calls the deleter functor to all remaining values, to ensure everything is cleaned up properly.
	~flyweight_refcounted() {
		Lock lock { mutex };
		for (auto it : map) {
			deleter(it.second);
		}
	}

	/// Gets the value associated to the passed arguments.
	/// If the value was already created, a reference to the existing value is returned.
	/// Otherwise, the value is created using the creator functor passed on the flyweight's constructor.
	/// @param key Key that represent a value.
	///            It will be passed to the creator functor if the value is not loaded yet.
	/// @return Reference to the value mapped to the passed arguments.
	T& get(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it == map.end()) {
			it = map.emplace(key, creator(key)).first;
		}
		return it->second.reference();
	}

	/// Alternative to `flyweight_refcounted::get` that returns an `autorelease_value`.
	/// This enables the RAII idiom for automatically releasing values.
	/// @see get
	autorelease_value_type get_autorelease(const Key& key) {
		return {
			*this,
			key,
		};
	}

	/// Gets the existing value associated to the passed key.
	/// If the value was not created yet, returns `nullptr`.
	/// @param key Key that represents a value.
	/// @return Pointer to the existing value, or `nullptr` if the value is not loaded.
	T *peek(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it == map.end()) {
			return nullptr;
		}
		else {
			return &it->second.value;
		}
	}

	/// Check whether the value mapped to the passed key is loaded.
	bool is_loaded(const Key& key) {
		Lock lock { mutex };
		return map.find(key) != map.end();
	}

	/// Get the current reference count for the value mapped to the passed key.
	size_t reference_count(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it != map.end()) {
			return it->second.refcount;
		}
		else {
			return 0;
		}
	}

	/// Decrements the reference count for the value mapped to the passed key.
	/// The value is only actually released when the reference count reaches zero.
	/// Trying to release a value that is not loaded is a no-op.
	/// @return `true` if a loaded value was released, `false` otherwise.
	bool release(const Key& key) {
		Lock lock { mutex };
		auto it = map.find(key);
		if (it != map.end() && it->second.dereference()) {
			deleter(it->second);
			map.erase(it);
			return true;
		}
		else {
			return false;
		}
	}

	/// Release all values, calling the deleter functor on them.
	void clear() {
		Lock lock { mutex };
		for (auto it : map) {
			deleter(it.second);
		}
		map.clear();
	}

protected:
	/// Value map.
	/// Maps the tuple of arguments to an already loaded value of type `T`.
	Map map;
	/// Creator function.
	/// Wraps the creator functor passed when constructing the flyweight, if any.
	std::function<T(const Key&)> creator;
	/// Deleter function.
	/// Wraps the deleter functor passed when constructing the flyweight, if any.
	std::function<void(T&)> deleter;
	Mutex mutex;
};

/**
 * Alternative to `flyweight_refcounted` that uses `std::mutex` and `std::lock_guard` for thread safety.
 */
template<typename Key, typename T, typename Map = std::unordered_map<Key, detail::refcounted_value<T>>>
using flyweight_refcounted_threadsafe = flyweight_refcounted<Key, T, Map, std::mutex, std::lock_guard<std::mutex>>;

}

# endif  // __FLYWEIGHT_HPP__
