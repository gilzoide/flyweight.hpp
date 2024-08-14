#pragma once

#include <functional>
#include <tuple>
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
template<typename T, typename... Args>
struct default_creator {
	T operator()(Args&&... args) {
		return T { std::forward<Args>(args)... };
	}
};

/// The default Deleter functor used by flyweight.
/// Doesn't do anything, as the value's destructor will be called when erased from flyweight's map.
template<typename T>
struct default_deleter {
	void operator()(T&) {
		// no-op
	}
};

template<typename T, typename... Args>
class flyweight {
public:
	flyweight() : creator(default_creator<T, Args...>{}), deleter(default_deleter<T>{}) {}

	template<typename Creator>
	flyweight(Creator&& creator)
		: creator([creator](Args&&... args) { return creator(std::forward<Args>(args)...); })
		, deleter(default_deleter<T>{})
	{
	}

	template<typename Creator, typename Deleter>
	flyweight(Creator&& creator, Deleter&& deleter)
		: creator([creator](Args&&... args) { return creator(std::forward<Args>(args)...); })
		, deleter([deleter](T& value) { deleter(value); })
	{
	}

	T& get(Args&&... args) {
		std::tuple<Args...> arg_tuple = { std::forward<Args>(args)... };
		auto it = map.find(arg_tuple);
		if (it == map.end()) {
			it = map.emplace(arg_tuple, creator(std::forward<Args>(args)...)).first;
		}
		return it->second;
	}
	T& get_tuple(const std::tuple<Args...>& arg_tuple) {
		auto it = map.find(arg_tuple);
		if (it == map.end()) {
			it = map.emplace(arg_tuple, detail::apply(creator, (std::tuple<Args...>) arg_tuple)).first;
		}
		return it->second;
	}

	struct autorelease_value {
		T value;

		autorelease_value(const T& value, flyweight& flyweight, const std::tuple<Args...>& arg_tuple)
			: value(value)
			, flyweight(flyweight)
			, arg_tuple(arg_tuple)
		{
		}

		T& operator*() {
			return value;
		}
		T& operator->() {
			return value;
		}
		operator T&() {
			return value;
		}

		~autorelease_value() {
			flyweight.release_tuple(arg_tuple);
		}

	private:
		flyweight& flyweight;
		std::tuple<Args...> arg_tuple;
	};
	autorelease_value get_autorelease(Args&&... args) {
		return get_autorelease_tuple({ std::forward<Args>(args)... });
	}
	autorelease_value get_autorelease_tuple(const std::tuple<Args...>& arg_tuple) {
		return {
			get_tuple(arg_tuple),
			*this,
			arg_tuple,
		};
	}

	bool is_loaded(Args&&... args) const {
		return is_loaded_tuple({ std::forward<Args>(args)... });
	}
	bool is_loaded_tuple(const std::tuple<Args...>& arg_tuple) const {
		return map.find(arg_tuple) != map.end();
	}

	bool release(Args&&... args) {
		return release_tuple({ std::forward<Args>(args)... });
	}
	bool release_tuple(const std::tuple<Args...>& arg_tuple) {
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

protected:
	std::unordered_map<std::tuple<Args...>, T, detail::tuple_hasher<Args...>> map;
	std::function<T(Args&&...)> creator;
	std::function<void(T&)> deleter;
};

template<typename T, typename... Args>
class flyweight_refcounted : public flyweight<detail::refcounted_value<T>, Args...> {
	using base = flyweight<detail::refcounted_value<T>, Args...>;

public:
	flyweight_refcounted() : base() {}

	template<typename Creator>
	flyweight_refcounted(Creator&& creator) : base(creator) {}

	template<typename Creator, typename Deleter>
	flyweight_refcounted(Creator&& creator, Deleter&& deleter) : base(creator, deleter) {}

	T& get(Args&&... args) {
		auto& value = base::get(std::forward<Args>(args)...);
		return value.reference();
	}
	T& get_tuple(const std::tuple<Args...>& arg_tuple) {
		auto& value = base::get_tuple(arg_tuple);
		return value.reference();
	}

	struct autorelease_value {
		T value;

		autorelease_value(const T& value, flyweight_refcounted& flyweight, const std::tuple<Args...>& arg_tuple)
			: value(value)
			, flyweight(flyweight)
			, arg_tuple(arg_tuple)
		{
		}

		autorelease_value(const autorelease_value& other)
			: value(other.flyweight.get_tuple(other.args))
			, flyweight(other.flyweight)
			, arg_tuple(other.arg_tuple)
		{
		}
		autorelease_value& operator=(const autorelease_value& other)
		{
			// release previous value
			flyweight.release_tuple(arg_tuple);
			// now update fields, making sure to increment reference count
			value = other.flyweight.get_tuple(other.arg_tuple);
			flyweight = other.flyweight;
			arg_tuple = other.arg_tuple;
		}

		T& operator*() {
			return value;
		}
		T& operator->() {
			return value;
		}
		operator T&() {
			return value;
		}

		~autorelease_value() {
			flyweight.release_tuple(arg_tuple);
		}

	private:
		flyweight_refcounted& flyweight;
		std::tuple<Args...> arg_tuple;
	};
	autorelease_value get_autorelease(Args&&... args) {
		return get_autorelease_tuple({ std::forward<Args>(args)... });
	}
	autorelease_value get_autorelease_tuple(const std::tuple<Args...>& arg_tuple) {
		return {
			get_tuple(arg_tuple),
			*this,
			arg_tuple,
		};
	}

	size_t load_count(Args&&... args) const {
		return load_count_tuple({ std::forward<Args>(args)... });
	}
	size_t load_count_tuple(const std::tuple<Args...>& arg_tuple) const {
		auto it = base::map.find(arg_tuple);
		if (it != base::map.end()) {
			return it->second.refcount;
		}
		else {
			return 0;
		}
	}

	bool release(Args&&... args) {
		return release_tuple({ std::forward<Args>(args)... });
	}
	bool release_tuple(const std::tuple<Args...>& arg_tuple) {
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
};

}
