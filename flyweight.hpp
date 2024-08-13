#pragma once

#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace flyweight {

namespace detail {
    template<typename... Args>
    struct tuple_hasher {
        constexpr size_t operator()(const std::tuple<Args...>& value) const {
            return hash_tuple<std::tuple<Args...>, 0, Args...>(value);
        }

        constexpr static size_t hash_combine(std::size_t a, std::size_t b) {
            return a ^ b + 0x9e3779b9 + (a << 6) + (a >> 2);
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
}


template<typename T, typename... Args>
class flyweight {
public:
    T* get(Args&&... args) {
        std::tuple<Args...> arg_tuple { std::forward<Args>(args)... };
        auto it = map.emplace(arg_tuple, std::unique_ptr<T>{});
        if (it.second) {
            it.first->second = std::make_unique<T>(std::forward<Args>(args)...);
        }
        return it.first->second.get();
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
    std::unordered_map<std::tuple<Args...>, std::unique_ptr<T>, detail::tuple_hasher<Args...>> map;
};


template<typename T, typename... Args>
class flyweight_shared {
public:
    std::shared_ptr<T> get(Args... args) {
        std::tuple<Args...> arg_tuple { args... };
        auto it = map.emplace(arg_tuple, std::shared_ptr<T>{});
        if (it.second) {
            it.first->second = std::make_shared<T>(args...);
        }
        return it.first->second;
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
    std::unordered_map<std::tuple<Args...>, std::shared_ptr<T>, detail::tuple_hasher<Args...>> map;
};


template<typename T, typename... Args>
class flyweight_autorelease {
    struct autorelease : public T {
        autorelease(flyweight_autorelease *flyweight_ref, Args&&... args)
            : T(std::forward<Args>(args)...)
            , flyweight_ref(flyweight_ref)
            , key(std::forward<Args>(args)...)
        {
        }

        ~autorelease() {
            flyweight_ref->release_tuple(key);
        }

        flyweight_autorelease *flyweight_ref;
        std::tuple<Args...> key;
    };
public:
    std::shared_ptr<T> get(Args&&... args) {
        std::tuple<Args...> arg_tuple { std::forward<Args>(args)... };
        auto it = map.emplace(arg_tuple, std::weak_ptr<T>{});
        if (!it.second && !it.first->second.expired()) {
            return it.first->second.lock();
        }

        std::shared_ptr<T> new_value = std::make_shared<autorelease>(this, std::forward<Args>(args)...);
        it.first->second = new_value;
        return new_value;
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
    std::unordered_map<std::tuple<Args...>, std::weak_ptr<T>, detail::tuple_hasher<Args...>> map;
};

}
