#pragma once

#ifndef DYNAMIC_VALUE_HPP
#define DYNAMIC_VALUE_HPP

#include <memory>
#include <typeindex>
#include <type_traits>
#include <stdexcept>
#include <utility>
#include <string>
#include <optional>
#include <concepts>

// ====================== 导出宏 ======================
#ifndef DYNAMIC_VALUE_STATIC
#define DYNAMIC_VALUE_STATIC
#endif
#ifdef DYNAMIC_VALUE_STATIC
    #define DYNAMIC_VALUE_API
#else
    #ifdef _WIN32
        #ifdef BUILDING_DYNAMIC_VALUE_DLL
            #define DYNAMIC_VALUE_API __declspec(dllexport)
        #else
            #define DYNAMIC_VALUE_API __declspec(dllimport)
        #endif
    #else
        #ifdef BUILDING_DYNAMIC_VALUE_DLL
            #define DYNAMIC_VALUE_API __attribute__((visibility("default")))
        #else
            #define DYNAMIC_VALUE_API
        #endif
    #endif
#endif

class cast_exception : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
        virtual ~cast_exception() = default;
    };


// ====================== 类型擦除基类 ======================
struct ValueHolderBase {
    virtual ~ValueHolderBase() = default;
    [[nodiscard]] virtual std::type_index type() const noexcept = 0;
    [[nodiscard]] virtual std::unique_ptr<ValueHolderBase> clone() const = 0;
    [[nodiscard]] virtual bool equals(const ValueHolderBase& other) const noexcept = 0;
};

// ====================== 类型擦除实现 ======================
template<typename T>
struct ValueHolder final : public ValueHolderBase {
    static_assert(!std::is_reference_v<T>, "ValueHolder cannot hold references");
    static_assert(!std::is_abstract_v<T>, "ValueHolder cannot hold abstract types");

    T value;

    template<typename U>
    explicit ValueHolder(U&& v) noexcept(std::is_nothrow_constructible_v<T, U>)
        : value(std::forward<U>(v)) {}

    template<typename... Args>
    explicit ValueHolder(std::in_place_t, Args&&... args)
        : value(std::forward<Args>(args)...) {}

    [[nodiscard]] std::type_index type() const noexcept override {
        return std::type_index(typeid(T));
    }

    [[nodiscard]] std::unique_ptr<ValueHolderBase> clone() const override {
        return std::make_unique<ValueHolder<T>>(value);
    }

    [[nodiscard]] bool equals(const ValueHolderBase& other) const noexcept override {
        if (type() != other.type()) return false;
        const auto& other_holder = static_cast<const ValueHolder<T>&>(other);
        if constexpr (requires { value == other_holder.value; }) {
            return value == other_holder.value;
        }
        return false;
    }

    [[nodiscard]] T& get() noexcept { return value; }
    [[nodiscard]] const T& get() const noexcept { return value; }
};

// DynamicValue 核心类
class DYNAMIC_VALUE_API DynamicValue {
private:
    std::unique_ptr<ValueHolderBase> m_ptr;

public:
    DynamicValue() noexcept = default;

    template<typename T>
    explicit(!std::is_convertible_v<T, DynamicValue>)
    DynamicValue(T&& value)
        : m_ptr(std::make_unique<ValueHolder<std::decay_t<T>>>(std::forward<T>(value))) {}

    template<typename T, typename... Args>
    explicit DynamicValue(std::in_place_type_t<T>, Args&&... args)
        : m_ptr(std::make_unique<ValueHolder<std::decay_t<T>>>(std::in_place, std::forward<Args>(args)...)) {}

    DynamicValue(const DynamicValue& other);
    DynamicValue(DynamicValue&& other) noexcept;
    ~DynamicValue();

    DynamicValue& operator=(const DynamicValue& other);
    DynamicValue& operator=(DynamicValue&& other) noexcept;

    template<typename T>
    DynamicValue& operator=(T&& value) {
        DynamicValue temp(std::forward<T>(value));
        swap(temp);
        return *this;
    }

    void swap(DynamicValue& other) noexcept;

    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr != nullptr; }
    [[nodiscard]] std::type_index type() const noexcept {
        return m_ptr ? m_ptr->type() : std::type_index(typeid(void));
    }

    template<typename T>
    [[nodiscard]] T* get_ptr() noexcept {
        if (!m_ptr) return nullptr;
        if (m_ptr->type() == std::type_index(typeid(T))) {
            return &static_cast<ValueHolder<T>*>(m_ptr.get())->get();
        }
        return nullptr;
    }

    template<typename T>
    [[nodiscard]] const T* get_ptr() const noexcept {
        if (!m_ptr) return nullptr;
        if (m_ptr->type() == std::type_index(typeid(T))) {
            return &static_cast<const ValueHolder<T>*>(m_ptr.get())->get();
        }
        return nullptr;
    }

    template<typename T>
    [[nodiscard]] T& get() {
        if (auto* p = get_ptr<T>()) return *p;
        throw cast_exception("DynamicValue type mismatch or empty");
    }

    template<typename T>
    [[nodiscard]] const T& get() const {
        if (auto* p = get_ptr<T>()) return *p;
        throw cast_exception("DynamicValue type mismatch or empty");
    }

    template<typename T>
    [[nodiscard]] std::optional<std::reference_wrapper<const T>> try_get() const noexcept {
        if (auto* p = get_ptr<T>()) return std::ref(*p);
        return std::nullopt;
    }

    [[nodiscard]] bool operator==(const DynamicValue& other) const noexcept;
    [[nodiscard]] bool operator!=(const DynamicValue& other) const noexcept { return !(*this == other); }
};

// ====================== 辅助 ======================
template<typename T>
[[nodiscard]] bool is_type(const DynamicValue& v) noexcept {
    return v.type() == std::type_index(typeid(T));
}

template<typename T, typename... Args>
[[nodiscard]] DynamicValue make_dynamic_value(Args&&... args) {
    return DynamicValue(std::in_place_type<T>, std::forward<Args>(args)...);
}

template<size_t N>
[[nodiscard]] DynamicValue make_dynamic_value(const char (&str)[N]) {
    return DynamicValue(std::string(str, N - 1));
}

#endif // DYNAMIC_VALUE_HPP