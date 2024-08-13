#pragma once

#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace flyweight {

namespace detail {
	/// Combine two hash values
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

	template<typename T, typename TPtrReturn, typename TPtrStored, typename... Args>
	class flyweight_impl {
		template<typename TMapPtrStored>
		using map_type = std::unordered_map<std::tuple<Args...>, TMapPtrStored, tuple_hasher<Args...>>;

	public:
		TPtrReturn get(Args&&... args) {
			return get_impl(map, std::forward<Args>(args)...);
		}

		bool is_loaded(Args&&... args) const {
			return is_loaded_tuple({ std::forward<Args>(args)... });
		}
		bool is_loaded_tuple(const std::tuple<Args...>& arg_tuple) const {
			return map.find(arg_tuple) != map.end();
		}

		void release(Args&&... args) {
			release_tuple({ std::forward<Args>(args)... });
		}
		void release_tuple(const std::tuple<Args...>& arg_tuple) {
			map.erase(arg_tuple);
		}

	protected:
		map_type<TPtrStored> map;

	private:
		static T* get_impl(map_type<std::unique_ptr<T>>& map, Args&&... args) {
			std::tuple<Args...> arg_tuple { std::forward<Args>(args)... };
			auto it = map.emplace(arg_tuple, std::unique_ptr<T>{});
			if (it.second) {
				it.first->second = std::make_unique<T>(std::forward<Args>(args)...);
			}
			return it.first->second.get();
		}

		static std::shared_ptr<T> get_impl(map_type<std::shared_ptr<T>>& map, Args&&... args) {
			std::tuple<Args...> arg_tuple { args... };
			auto it = map.emplace(arg_tuple, std::shared_ptr<T>{});
			if (it.second) {
				it.first->second = std::make_shared<T>(std::forward<Args>(args)...);
			}
			return it.first->second;
		}

		struct autorelease : public T {
			autorelease(map_type<std::weak_ptr<T>>& map_ref, Args&&... args)
				: T(std::forward<Args>(args)...)
				, map_ref(map_ref)
				, key(std::forward<Args>(args)...)
			{
			}

			~autorelease() {
				map_ref.erase(key);
			}

			map_type<std::weak_ptr<T>>& map_ref;
			std::tuple<Args...> key;
		};
		static std::shared_ptr<T> get_impl(map_type<std::weak_ptr<T>>& map, Args&&... args) {
			std::tuple<Args...> arg_tuple { std::forward<Args>(args)... };
			auto it = map.emplace(arg_tuple, std::weak_ptr<T>{});
			if (!it.second) {
				if (std::shared_ptr<T> value = it.first->second.lock()) {
					return value;
				}
			}

			std::shared_ptr<T> new_value = std::make_shared<autorelease>(map, std::forward<Args>(args)...);
			it.first->second = new_value;
			return new_value;
		}
	};
}

template<typename T, typename... Args>
using flyweight = detail::flyweight_impl<T, T*, std::unique_ptr<T>, Args...>;

template<typename T, typename... Args>
using flyweight_shared = detail::flyweight_impl<T, std::shared_ptr<T>, std::shared_ptr<T>, Args...>;

template<typename T, typename... Args>
using flyweight_autorelease = typename std::enable_if<std::is_class<T>::value,
	detail::flyweight_impl<T, std::shared_ptr<T>, std::weak_ptr<T>, Args...>
>::type;

}
