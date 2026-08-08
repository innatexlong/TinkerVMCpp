#include "dynamic_value.hpp"
#include <string>

// DynamicValue 默认构造函数
DynamicValue::DynamicValue(const DynamicValue& other)
    : m_ptr(other.m_ptr ? other.m_ptr->clone() : nullptr) {}

DynamicValue::DynamicValue(DynamicValue&& other) noexcept = default;

DynamicValue::~DynamicValue() = default;

// ========== 赋值运算符 ==========
DynamicValue& DynamicValue::operator=(const DynamicValue& other) {
    if (this != &other) {
        DynamicValue temp(other);
        swap(temp);
    }
    return *this;
}

DynamicValue& DynamicValue::operator=(DynamicValue&& other) noexcept = default;

// ========== swap ==========
void DynamicValue::swap(DynamicValue& other) noexcept {
    m_ptr.swap(other.m_ptr);
}

// ========== operator== ==========
bool DynamicValue::operator==(const DynamicValue& other) const noexcept {
    if (m_ptr == other.m_ptr) return true;
    if (!m_ptr || !other.m_ptr) return false;
    return m_ptr->equals(*other.m_ptr);
}