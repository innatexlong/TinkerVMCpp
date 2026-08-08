// env.cppm
module;
#include <vector>
#include <cstdint>
#include <cstring>
#include <concepts>
#include <unordered_map>
#include <format>
#include <memory>

export module env;

import exceptions;

namespace env {
    export typedef std::uint32_t pos_t;
    export typedef std::uint32_t id_t;
    export typedef std::uint32_t size_type;

    template<typename T>
    concept ValidValueType =
        std::same_as<T, std::uint8_t>  ||
        std::same_as<T, std::uint16_t> ||
        std::same_as<T, std::uint32_t> ||
        std::same_as<T, std::uint64_t> ||
        std::same_as<T, std::int8_t>  ||
        std::same_as<T, std::int16_t> ||
        std::same_as<T, std::int32_t> ||
        std::same_as<T, std::int64_t> ;

    export class SymbolTable {
        std::unordered_map<std::string, std::uint32_t> table{};

    };

    export enum class VarType : uint32_t { Char, UInt32, Float32, Str, VarPtr };


    export struct Variable {
        id_t id{0};
        pos_t ptr{0};  // 最多处理4GB
        size_type size;
        VarType type;
    };

    export struct TempVariable {
        std::vector<std::uint8_t> value;
        VarType type;
    };

    export class MemoryPool {
        public:
            struct FreeBlock {
                std::uint32_t start;
                std::uint32_t size;
                FreeBlock* next;
            };

            explicit MemoryPool(const std::uint32_t total_size = 0x0F000000)
                : pool_size(total_size), values(total_size, 0), is_used(total_size, 0)
            {
                // 初始整个内存为一个空闲块
                head = new FreeBlock{.start = 0, .size = total_size, .next = nullptr};
            }

            ~MemoryPool() {
                // 释放所有FreeBlock节点
                while (head) {
                    auto* cur_node = head;
                    head = head->next;
                    delete cur_node;
                }
            }

            // 分配 size 字节，返回起始地址，失败抛出异常
            std::uint32_t allocate(std::uint32_t size) {
                // 对齐要求：假设需要4字节对齐
                size = (size + 3) & ~3u; // 向上对齐到4字节

                FreeBlock* prev = nullptr;
                FreeBlock* cur = head;
                while (cur) {
                    if (cur->size >= size) {
                        // 找到合适块
                        std::uint32_t addr = cur->start;
                        if (cur->size == size) {
                            // 完全匹配，移除该块
                            if (prev) prev->next = cur->next;
                            else head = cur->next;
                            delete cur;
                        } else {
                            // 拆分：剩余块留在链表中
                            cur->start += size;
                            cur->size -= size;
                        }
                        // 标记已用（可选，但 is_used 可用于调试）
                        for (std::uint32_t i = addr; i < addr + size; ++i)
                            is_used[i] = 1;
                        return addr;
                    }
                    prev = cur;
                    cur = cur->next;
                }
                throw exceptions::OutOfMemory("No enough memory");
            }

            // 释放起始地址 addr 和大小 size（应与分配时一致）
            void deallocate(std::uint32_t addr, std::uint32_t size) {
                size = (size + 3) & ~3u; // 对齐
                // 标记未用
                for (std::uint32_t i = addr; i < addr + size; ++i)
                    is_used[i] = 0;

                // 插入到链表中，并合并相邻块
                auto* new_block = new FreeBlock{.start = addr, .size = size, .next = nullptr};
                FreeBlock* prev = nullptr;
                FreeBlock* cur = head;
                // 按地址顺序插入
                while (cur && cur->start < addr) {
                    prev = cur;
                    cur = cur->next;
                }
                // 插入到 prev 和 cur 之间
                new_block->next = cur;
                if (prev) prev->next = new_block;
                else head = new_block;

                // 合并：与下一个块合并
                if (new_block->next && new_block->start + new_block->size == new_block->next->start) {
                    new_block->size += new_block->next->size;
                    auto* to_del = new_block->next;
                    new_block->next = to_del->next;
                    delete to_del;
                }
                // 与上一个块合并
                if (prev && prev->start + prev->size == new_block->start) {
                    prev->size += new_block->size;
                    prev->next = new_block->next;
                    delete new_block;
                }
            }

            [[nodiscard]] bool is_allocated(const std::uint32_t pos, const size_type size) const noexcept {
                if (pos > values.size() || size > values.size() - pos) {
                    return false;
                }
                for (pos_t i = pos; i < pos + size; ++i) {
                    if (!is_used[i]) {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] bool is_not_allocated(const std::uint32_t pos, const size_type size) const noexcept {
                if (pos > values.size() || size > values.size() - pos) {
                    return true;
                }
                for (pos_t i = pos; i < pos + size; ++i) {
                    if (!is_used[i]) {
                        return true;
                    }
                }
                return false;
            }

            template<ValidValueType T>
            [[nodiscard]] T get(const std::uint32_t pos) const {
                if (pos + sizeof(T) > values.size()) {
                    throw exceptions::SegFault(std::format("Out of range {}", pos));
                }
                for (pos_t i = pos; i < pos + sizeof(T); ++i) {
                    if (!is_used[i]) {
                        throw exceptions::SegFault(std::format("Not allocated {}", pos));
                    }
                }
                T result;
                std::memcpy(&result, &values[pos], sizeof(T));
                return result;
            }

            [[nodiscard]] const std::uint8_t* get(const std::uint32_t pos, const size_type size) const {
                if (pos > values.size() || size > values.size() - pos) {
                    throw exceptions::SegFault(std::format("Out of range {}", pos));
                }
                for (std::uint32_t i = pos; i < pos + size; ++i) {
                    if (!is_used[i]) {
                        throw exceptions::SegFault(std::format("Not allocated {}", pos));
                    }
                }
                return &values[pos];
            }

            template<ValidValueType T>
            void set(const std::uint32_t pos, const T value) {
                if (pos + sizeof(T) > values.size()) {
                    throw exceptions::SegFault(std::format("Out of range {}", pos));
                }
                for (std::uint32_t i = pos; i < pos + sizeof(T); ++i) {
                    if (!is_used[i]) {
                        throw exceptions::SegFault(std::format("Not allocated {}", pos));
                    }
                }
                std::memcpy(&values[pos], &value, sizeof(T));
            }

            [[nodiscard]] std::uint32_t size() const noexcept { return pool_size; }

        private:
            std::uint32_t pool_size;
            std::vector<std::uint8_t> values;
            std::vector<std::uint8_t> is_used;
            FreeBlock* head = nullptr;
    };

    export struct Env {
        MemoryPool memory_pool;
        std::unordered_map<std::uint32_t, Variable> variables{};
        // std::shared_ptr<Env> parent = nullptr;
    };

    // export template<ValidValueType T>
    // T getValue(const Env& env, const std::size_t pos) {
    //     if (pos + sizeof(T) > env.memory_pool.size()) {
    //         throw std::out_of_range("getValue out of range");
    //     }
    //     T result;
    //     std::memcpy(&result, &env.memory_pool.get<T>(pos), sizeof(T));
    //     return result;
    // }

    // export template<ValidValueType T>
    // void setValue(Env& env, const std::size_t pos, const T value) {
    //     if (pos + sizeof(T) > env.memory_pool.size()) {
    //         throw std::out_of_range("setValue out of range");
    //     }
    //     std::memcpy(&env.memory_pool.get<T>(pos), &value, sizeof(T));
    // }
}

template <>
struct std::formatter<env::VarType> : std::formatter<std::string_view> {
    // 继承 parse，直接使用父类的 parse
    auto format(const env::VarType type, std::format_context& ctx) const {
        std::string_view name;
        switch (type) {
            using enum env::VarType;
            case Char:     name = "Char"; break;
            case UInt32:     name = "UInt32"; break;
            case Float32:   name = "Float32"; break;
            case Str:  name = "Str"; break;
            case VarPtr: name = "VarPtr"; break;
            default:      name = "???"; break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};
