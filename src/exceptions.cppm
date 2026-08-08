// exceptions.cppm
module;
#include <unordered_map>
#include <exception>
#include <queue>
#include <typeindex>
#include <cstdint>

export module exceptions;

// 1. 静态注册表（内部实现，不暴露给外部）
namespace exceptions {
    export {
        enum class Code : std::uint32_t {
            SegFault = 0xC'0000005u,
            VarNotFound = 0xD'0000005u,
            InvalidOperation = 0x0000000Au,
            SizeMismatch,
            InvalidVarType,
            OutOfMemory,
        };
        class SegFault : public std::logic_error {
            public:
                using std::logic_error::logic_error;
                ~SegFault() override = default;
        };
        class VarNotFound : public std::logic_error {
            public:
                using std::logic_error::logic_error;
                ~VarNotFound() override = default;
        };
        class InvalidOperation : public std::logic_error {
            public:
                using std::logic_error::logic_error;
                ~InvalidOperation() override = default;
        };
        class SizeMismatch : public std::logic_error {
            public:
                using std::logic_error::logic_error;
                ~SizeMismatch() override = default;
        };
        class OutOfMemory : public std::runtime_error {
            public:
                using std::runtime_error::runtime_error;
                ~OutOfMemory() override = default;
        };
        class InvalidVarType : public std::logic_error {
            public:
                using std::logic_error::logic_error;
                ~InvalidVarType() override = default;
        };
    }

    namespace {
        // 使用 type_index 作为 key，避免动态 cast 的性能开销
        struct ExitCodeRegistry {
            std::unordered_map<std::type_index, std::uint32_t> map;

            ExitCodeRegistry() {
                // 在这里统一注册所有异常及其返回值
                map[typeid(SegFault)] = static_cast<std::uint32_t>(Code::SegFault);
                map[typeid(VarNotFound)] = static_cast<std::uint32_t>(Code::VarNotFound);
                map[typeid(InvalidOperation)] = static_cast<std::uint32_t>(Code::InvalidOperation);
                map[typeid(SizeMismatch)] = static_cast<std::uint32_t>(Code::SizeMismatch);

                map[typeid(std::exception)] = 1;
                map[typeid(std::out_of_range)] = 2;
                map[typeid(std::invalid_argument)] = 3;
                map[typeid(std::bad_alloc)] = 4;
                // 将来新增项目自定义异常，也在这里添加
                // 例如：map[typeid(NetworkError)] = 10;
            }
        };

        const ExitCodeRegistry registry; // 全局单例
    }

    // 2. 导出函数的实现
    export std::uint32_t map_exception_to_code(const std::exception& e) {
        // 精确匹配（查找最派生类型）
        if (const auto it = registry.map.find(typeid(e)); it != registry.map.end()) {
            return it->second;
        }

        return 4294967295;
    }
}
