#ifndef DATA_IMPL_HPP
#define DATA_IMPL_HPP

#include "data.hpp"
#include <typeindex>
#include <stdexcept>
#include <cstring>
#include <bit>

namespace data {

    // ---------- register_type ----------
    template<typename T>
    void type_registry::register_type(std::array<uint8_t, 4> identifier, Packer packer, Unpacker unpacker, bool overwrite) {
        const uint32_t id = identifier_to_uint32(identifier);

        // 1. 保留首字节 0xFF 作为结束标记
        if ((id & 0xFF000000) == 0xFF000000) {
            throw std::invalid_argument("Identifier starting with 0xFF is reserved for end marker");
        }

        const std::type_index ti = typeid(T);
        Entry new_entry{id, packer, unpacker};

        // 2. 检查冲突
        auto type_it = type_to_entry.find(ti);
        auto id_it    = identifier_to_type.find(id);

        if (!overwrite) {
            if (type_it != type_to_entry.end() || id_it != identifier_to_type.end()) {
                throw duplicate_registration_error("Type or identifier already registered");
            }
        } else {
            // 如果类型已存在且标识符不同，需要移除旧的标识符映射
            if (type_it != type_to_entry.end() && type_it->second.identifier != id) {
                identifier_to_type.erase(type_it->second.identifier);
            }
            // 如果标识符已存在且指向不同类型，需要移除旧类型映射（但保留新类型）
            if (id_it != identifier_to_type.end() && id_it->second != ti) {
                // 如果旧类型恰好是当前要覆盖的类型（同类型不同标识符），上面已处理
                // 这里只需从 identifier_to_type 中移除旧映射，type_to_entry 中旧类型仍存在，但稍后会被覆盖
                // 但更安全：如果旧类型存在且不是当前类型，应从 type_to_entry 中移除？
                // 根据语义，覆盖只针对当前类型，不应该删除其他类型的注册。
                // 但若标识符被其他类型占用，该其他类型的注册应被删除（因为标识符被抢占）。
                // 为简化，我们允许覆盖时，如果标识符被其他类型占用，则删除该其他类型的注册。
                // 但这样可能意外删除用户类型，谨慎起见，我们选择不允许覆盖不同标识符的现有类型。
                // 更好的设计：若标识符已被其他类型注册且 overwrite=true，则抛出异常或忽略。
                // 这里我们严格处理：只允许在类型相同且标识符不同的情况下覆盖（即更新标识符），
                // 或标识符相同且类型相同（更新packer/unpacker）。
                // 若标识符被其他类型占用，则抛出异常。
                if (type_it == type_to_entry.end() || type_it->second.identifier != id) {
                    // 标识符被其他类型占用，且当前类型未注册或标识符不同 → 不允许
                    throw duplicate_registration_error("Identifier already used by another type, overwrite not allowed");
                }
                // 否则是相同类型，可以覆盖
            }
        }

        // 3. 插入/更新
        type_to_entry[ti] = new_entry;
        identifier_to_type.emplace(id, ti);
    }

    // ---------- write_identifier ----------
    template<typename T>
    void type_registry::write_identifier(std::ostream& os) const {
        auto it = type_to_entry.find(typeid(T));
        if (it == type_to_entry.end())
            throw unknown_type_error("No identifier for type");

        uint32_t id = it->second.identifier;
        uint8_t first = (id >> 24) & 0xFF;
        if (first == 0xFF) throw std::runtime_error("Invalid identifier (0xFF)");

        os.put(first);  // 写入首字节
        // 根据首字节决定后续有效字节数
        int payload = 0;
        switch (first) {
            case 0xFE: payload = 3; break;
            case 0xFD: payload = 2; break;
            case 0xFC: payload = 1; break;
            // 其余 payload = 0
        }

        // 从高位到低位依次写入有效字节（大端序）
        for (int i = 0; i < payload; ++i) {
            uint8_t byte = (id >> (24 - 8*(i+1))) & 0xFF;
            os.put(byte);
        }
        if (os.fail()) throw std::runtime_error("Failed to write identifier");
    }

} // namespace data

#endif // DATA_IMPL_HPP