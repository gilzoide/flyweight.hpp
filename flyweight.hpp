/** @file flyweight.hpp
 * Single header with a templated implementation of the Flyweight design pattern
 *
 * Example of asset management using flyweight objects:
 * ```cpp
 * #include "flyweight.hpp"
 *
 * // 1. Declare your flyweight instance.
 * // The first template type is the value type.
 * // The rest are argument types used to create values.
 * // In this example, we're defining a resource manager for images.
 * flyweight::flyweight<Image, std::string> my_image_flyweight {
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
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace flyweight {

namespace detail {
	/// Combine two integer hash values
	/// @see https://github.com/boostorg/multiprecision/blob/de3243f3e5427c6ab5b050aac03bc89c6e03e2bc/include/boost/multiprecision/detail/hash.hpp#L35-L41
	constexpr static size_t hash_combine(std::size_t a, std::size_t b) {
		return a ^ b + 0x9e3779b9 + (a << 6) + (a >> 2);
	}

	/// Hasher implementation for std::tuple<...>
	template<typename... Args>
	struct tuple_hasher {
		constexpr size_t operator()(const std::tuple<Args...>& value) const {
			return hash_tuple<std::tuple<Args...>, 0, Args...>(value);
		}

		template<typename TTuple, size_t I, typename T>
		constexpr static size_t hash_tuple(const TTuple& v) {
			return std::hash<T>{}(std::get<I>(v));
		}

		template<typename TTuple, size_t I, typename T1, typename T2, typename... Rest>
		constexpr static size_t hash_tuple(const TTuple& v) {
			return hash_combine(
				hash_tuple<TTuple, I, T1>(v),
				hash_tuple<TTuple, I+1, T2, Rest...>(v)
			);
		}
	};

	/// Reference counted value, used for flyweight_refcounted
	template<typename T>
	struct refcounted_value {
		T value;
		long long refcount;

		/// Construct a value with an initial reference count of 0.
		/// `reference` should be called right after constructing this.
		refcounted_value(T&& value) : value(value), refcount(0) {}

		operator T&() {
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

#if __cpp_lib_apply
	template<typename Fn, typename... Args>
	auto apply(Fn&& f, std::tuple<Args...>&& t) {
		return std::apply(std::move(f), std::move(t));
	}
#else
	// Unpacking tuple with int sequence on C++11
	// Reference: https://stackoverflow.com/a/7858971
	template<int...>
	struct seq {};

	template<int N, int... S>
	struct gens : gens<N-1, N-1, S...> {};

	template<int... S>
	struct gens<0, S...> {
		using type = seq<S...>;
	};

	template<typename... Args>
	struct apply_impl {
		template<typename Fn, int... S>
		static auto invoke(Fn&& f, std::tuple<Args...>&& t, seq<S...>) {
			return f(std::forward<Args>(std::get<S>(t))...);
		}
	};

	template<typename Fn, typename... Args>
	auto apply(Fn&& f, std::tuple<Args...>&& t) {
		return apply_impl<Args...>::invoke(std::move(f), std::move(t), typename gens<sizeof...(Args)>::type{});
	}
#endif
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
template<typename T, typename Flyweight, typename ArgTuple>
struct autorelease_value {
	T& value;

	/// Constructor.
	autorelease_value(T& value, Flyweight& flyweight, const ArgTuple& arg_tuple)
		: value(value)
		, flyweight(flyweight)
		, arg_tuple(arg_tuple)
	{
	}

	/// Copy constructor.
	/// Calls `flyweight::get_tuple` to make sure reference counting is correct.
	autorelease_value(const autorelease_value& other)
		: value(other.flyweight.get(other.arg_tuple))
		, flyweight(other.flyweight)
		, arg_tuple(other.arg_tuple)
	{
	}
	/// Copy assignment.
	/// Releases the previously referenced value and calls `flyweight.get_tuple` to make sure reference counting is correct.
	autorelease_value& operator=(const autorelease_value& other)
	{
		// release previous value
		flyweight.release(arg_tuple);
		// re-get the value to make sure reference counting is correct
		value = other.flyweight.get(other.arg_tuple);
		flyweight = other.flyweight;
		arg_tuple = other.arg_tuple;
	}

	/// Returns the wrapped value.
	T& operator*() {
		return value;
	}
	/// Returns the wrapped value.
	T& operator->() {
		return value;
	}
	/// Returns the wrapped value.
	operator T&() {
		return value;
	}

	/// Release the value back to the owning flyweight.
	~autorelease_value() {
		flyweight.release(arg_tuple);
	}

private:
	Flyweight& flyweight;
	ArgTuple arg_tuple;
};

/**
 * Factory for flyweight objects of type `T`, created with arguments of type `Args...`.
 *
 * When getting a value, the flyweight first checks for an existing value associated with the passed arguments, returning a reference to it if found.
 * Otherwise, a new value is created and cached for future requests.
 * When no longer needed, an existing value may be released by calling `flyweight::release`.
 *
 * Flyweights are useful as a way to load heavy objects only once and sharing them whenever necessary, for example images used as icons in an interactive app.
 * Flyweights can also be used to implement function result memoization.
 *
 * @tparam T  Value type.
 * @tparam Args  Arguments mapped to loaded values.
 */
template<typename T, typename... Args>
class flyweight {
public:
	using value = T;
	using autorelease_value = autorelease_value<T, flyweight, std::tuple<Args...>>;

	/// Default constructor.
	/// Uses `default_creator` as the value creator and `default_deleter` as the value deleter.
	flyweight() : creator(default_creator<T, Args...>{}), deleter(default_deleter<T>{}) {}

	/// Constructor with custom value creator functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with the arguments passed to `flyweight::get` or `flyweight::get_tuple`.
	template<typename Creator>
	flyweight(Creator&& creator)
		: creator([creator](Args&&... args) { return creator(std::forward<Args>(args)...); })
		, deleter(default_deleter<T>{})
	{
	}

	/// Constructor with custom value creator functor and deleter functor.
	/// @param creator  Creator functor that will be called when creating a mapped value for the first time.
	///                 It will be called with the arguments passed to `flyweight::get` or `flyweight::get_tuple`.
	/// @param deleter  Deleter functor that will be called when releasing a mapped value.
	///                 It will be called by `flyweight::release` or `flyweight::release_tuple` with a reference to the value.
	template<typename Creator, typename Deleter>
	flyweight(Creator&& creator, Deleter&& deleter)
		: creator([creator](Args&&... args) { return creator(std::forward<Args>(args)...); })
		, deleter([deleter](T& value) { deleter(value); })
	{
	}

	/// Gets the value associated to the passed arguments.
	/// If the value was already created, a reference to the existing value is returned.
	/// Otherwise, the value is created using the creator functor passed on the flyweight's constructor.
	/// @param args  Arguments that represent a value.
	///              These will be forwarded to the creator functor if the value is not loaded yet.
	/// @return Reference to the value mapped to the passed arguments.
	T& get(const std::tuple<Args...>& arg_tuple) {
		auto it = map.find(arg_tuple);
		if (it == map.end()) {
			it = map.emplace(arg_tuple, detail::apply(creator, (std::tuple<Args...>) arg_tuple)).first;
		}
		return it->second;
	}
	/// Alternative to `flyweight::get` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto get(Args&&... args) -> typename std::enable_if<uses_multiple_args, T&>::type {
		return get(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Alternative to `flyweight::get` that returns an `autorelease_value`.
	/// This enables the RAII idiom for automatically releasing values.
	/// @see get
	autorelease_value get_autorelease(const std::tuple<Args...>& arg_tuple) {
		return {
			get(arg_tuple),
			*this,
			arg_tuple,
		};
	}
	/// Alternative to `flyweight::get_autorelease` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto get_autorelease(Args&&... args) -> typename std::enable_if<uses_multiple_args, autorelease_value>::type {
		return get_autorelease(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Check whether the value mapped to the passed arguments is loaded.
	bool is_loaded(const std::tuple<Args...>& arg_tuple) const {
		return map.find(arg_tuple) != map.end();
	}
	/// Alternative to `flyweight::is_loaded` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto is_loaded(Args&&... args) const -> typename std::enable_if<uses_multiple_args, bool>::type {
		return is_loaded(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Release the value mapped to the passed arguments.
	/// Trying to release a value that is not loaded is a no-op.
	/// @return `true` if a loaded value was released, `false` otherwise.
	bool release(const std::tuple<Args...>& arg_tuple) {
		auto it = map.find(arg_tuple);
		if (it != map.end()) {
			deleter(it->second);
			map.erase(it);
			return true;
		}
		else {
			return false;
		}
	}
	/// Alternative to `flyweight::release` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto release(Args&&... args) -> typename std::enable_if<uses_multiple_args, bool>::type {
		return release(std::tuple<Args...> { std::forward<Args>(args)... });
	}

protected:
	/// Value map.
	/// Maps the tuple of arguments to an already loaded value of type `T`.
	std::unordered_map<std::tuple<Args...>, T, detail::tuple_hasher<Args...>> map;
	/// Creator function.
	/// Wraps the creator functor passed when constructing the flyweight, if any.
	std::function<T(Args&&...)> creator;
	/// Deleter function.
	/// Wraps the deleter functor passed when constructing the flyweight, if any.
	std::function<void(T&)> deleter;
};


/**
 * `flyweight` subclass that employs reference counting.
 *
 * The reference count increments for each call to `flyweight_refcounted::get` and decrements for each call to `flyweight_refcounted::release` using the same arguments.
 * When calling `flyweight_refcounted::release`, the value is actually deleted from the cache only when the reference count reaches zero.
 *
 * This is specially useful for implementing resource management of heavy objects, like images used as icons in an interactive app.
 *
 * @see flyweight
 */
template<typename T, typename... Args>
class flyweight_refcounted : public flyweight<detail::refcounted_value<T>, Args...> {
	using base = flyweight<detail::refcounted_value<T>, Args...>;
public:
	using autorelease_value = autorelease_value<T, flyweight_refcounted, std::tuple<Args...>>;

	/// @see flyweight()
	flyweight_refcounted() : base() {}

	/// @see flyweight(Creator&&)
	template<typename Creator>
	flyweight_refcounted(Creator&& creator) : base(creator) {}

	/// @see flyweight(Creator&&, Deleter&&)
	template<typename Creator, typename Deleter>
	flyweight_refcounted(Creator&& creator, Deleter&& deleter) : base(creator, deleter) {}

	/// Override for `flyweight::get` with reference counting.
	/// @see flyweight::get
	T& get(const std::tuple<Args...>& arg_tuple) {
		auto& value = base::get(arg_tuple);
		return value.reference();
	}
	/// Alternative to `flyweight_refcounted::get` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto get(Args&&... args) -> typename std::enable_if<uses_multiple_args, T&>::type {
		return get(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Override for `flyweight::get_autorelease` with reference counting.
	/// @see flyweight::get_autorelease
	autorelease_value get_autorelease(const std::tuple<Args...>& arg_tuple) {
		return {
			get(arg_tuple),
			*this,
			arg_tuple,
		};
	}
	/// Alternative to `flyweight_refcounted::get_autorelease` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto get_autorelease(Args&&... args) -> typename std::enable_if<uses_multiple_args, autorelease_value>::type {
		return get_autorelease(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Checks the current reference count for the value mapped to the passed arguments.
	/// @see reference_count_tuple
	size_t reference_count(const std::tuple<Args...>& arg_tuple) const {
		auto it = base::map.find(arg_tuple);
		if (it != base::map.end()) {
			return it->second.refcount;
		}
		else {
			return 0;
		}
	}
	/// Alternative to `flyweight_refcounted::reference_count` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto reference_count(Args&&... args) const -> typename std::enable_if<uses_multiple_args, size_t>::type {
		return reference_count(std::tuple<Args...> { std::forward<Args>(args)... });
	}

	/// Override for `flyweight::release` with reference counting.
	/// The value will actually be released only when the reference count reaches zero.
	bool release(const std::tuple<Args...>& arg_tuple) {
		auto it = base::map.find(arg_tuple);
		if (it != base::map.end() && it->second.dereference()) {
			base::deleter(it->second);
			base::map.erase(it);
			return true;
		}
		else {
			return false;
		}
	}
	/// Alternative to `flyweight_refcounted::release` with the arguments unpacked.
	template<bool uses_multiple_args = (sizeof...(Args) > 1)>
	auto release(Args&&... args) -> typename std::enable_if<uses_multiple_args, bool>::type {
		return release(std::tuple<Args...> { std::forward<Args>(args)... });
	}
};

}

# endif  // __FLYWEIGHT_HPP__
