#ifndef DATA_HPP
#define DATA_HPP

#include "dynamic_value.hpp"
#include "hasharray.hpp"

#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <string>
#include <array>
#include <ostream>
#include <istream>
#include <stdexcept>
#include <memory>

// ====================== 导出宏 ======================
#define DATA_API
#ifndef DATA_API
#  ifdef _WIN32
#    ifdef BUILDING_DLL
#      define DATA_API __declspec(dllexport)
#    else
#      define DATA_API __declspec(dllimport)
#    endif
#  else
#    ifdef BUILDING_DLL
#      define DATA_API __attribute__((visibility("default")))
#    else
#      define DATA_API
#    endif
#  endif
#endif

namespace data {

    // 前向声明
    class type_registry;
    class configuration;
    class compression_manager;
    class packing_context;

    // ====================== 异常类 ======================

    class DATA_API type_mismatch_error : public std::logic_error {
        public:
            using std::logic_error::logic_error;
            virtual ~type_mismatch_error() = default;
    };
    class DATA_API duplicate_registration_error : public std::logic_error {
        public:
            using std::logic_error::logic_error;
            virtual ~duplicate_registration_error() = default;
    };
    class DATA_API unknown_type_error : public std::logic_error {
        public:
            using std::logic_error::logic_error;
            virtual ~unknown_type_error() = default;
    };
    class DATA_API unknown_identifier_error : public std::logic_error {
        public:
            using std::logic_error::logic_error;
            virtual ~unknown_identifier_error() = default;
    };
    class DATA_API unknown_config_error : public std::logic_error {
        public:
            using std::logic_error::logic_error;

            ~unknown_config_error() override = default;
    };


    class DATA_API recursion_error : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
            virtual ~recursion_error() = default;
    };
    class DATA_API invalid_identifier_error : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
            virtual ~invalid_identifier_error() = default;
    };
    class DATA_API invalid_parameter_error : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
            virtual ~invalid_parameter_error() = default;
    };

    // ====================== 上下文管理器 ======================
    class DATA_API packing_context {
    private:
        const uint64_t max_depth;
    public:
        uint64_t depth = 0;
        packing_context(const type_registry& reg, const configuration& cfg, compression_manager& comp);

        class depth_guard {
        public:
            explicit depth_guard(packing_context& ctx);
            ~depth_guard();
        private:
            packing_context& ctx_;
        };

        const type_registry& registry;
        const configuration& config;
        const compression_manager& compression;
    };

    // ====================== 类型注册表 ======================

    // 使用函数指针替代 std::function，消除类型擦除开销
    using Packer   = void (*)(std::ostream&, const DynamicValue&, packing_context&);
    using Unpacker = DynamicValue (*)(std::istream&, packing_context&);

    // 标识符统一使用 uint32_t（大端序存储，便于哈希）
    // 转换函数：4字节数组 ↔ uint32_t
    [[nodiscard]] constexpr uint32_t identifier_to_uint32(std::array<uint8_t, 4> arr) noexcept {
        return (static_cast<uint32_t>(arr[0]) << 24) |
               (static_cast<uint32_t>(arr[1]) << 16) |
               (static_cast<uint32_t>(arr[2]) << 8)  |
               static_cast<uint32_t>(arr[3]);
    }

    [[nodiscard]] constexpr std::array<uint8_t, 4> uint32_to_identifier(uint32_t id) noexcept {
        return { {
            static_cast<uint8_t>((id >> 24) & 0xFF),
            static_cast<uint8_t>((id >> 16) & 0xFF),
            static_cast<uint8_t>((id >> 8)  & 0xFF),
            static_cast<uint8_t>(id & 0xFF)
        } };
    }

    class DATA_API type_registry {
        public:
            type_registry() = default;

            // 注册类型：identifier 为 4 字节数组，内部转为 uint32_t 存储
            template<typename T>
            void register_type(std::array<uint8_t, 4> identifier, Packer packer, Unpacker unpacker, bool overwrite = false);

            // 获取打包器
            Packer get_packer_from_type(std::type_index ti) const;

            // 通过标识符获取解包器
            Unpacker get_unpacker_from_identifier(uint32_t identifier) const;

            // 从流中读取头部并获取解包器
            Unpacker get_unpacker(std::istream& is) const;
            Unpacker get_unpacker_allow_end(std::istream& is) const;

            // 获取类型对应的标识符
            uint32_t get_identifier_from_type(std::type_index ti) const;

            // 向流写入类型头部（变长编码）
            template<typename T>
            void write_identifier(std::ostream& os) const;

            // 从流中读取头部（返回 uint32_t）
            [[nodiscard]] uint32_t read_identifier(std::istream& is) const;
            [[nodiscard]] uint32_t read_identifier_allow_end(std::istream& is) const;

            // 初始化内置类型
            void init(bool overwrite = true);

            void clear() noexcept;
            void reset();

        private:
            struct Entry {
                uint32_t identifier;
                Packer   packer;
                Unpacker unpacker;
            };

            // 主表：类型 → Entry
            std::unordered_map<std::type_index, Entry> type_to_entry;
            // 反向表：标识符 → 类型（用于反序列化查类型）
            std::unordered_map<uint32_t, std::type_index> identifier_to_type;
    };

    // ====================== 配置类 ======================
    class DATA_API configuration {
        public:
            using ConfigValue = DynamicValue;
            using ConfigDict  = std::unordered_map<std::string, ConfigValue>;

            configuration();
            void init();
            void clear() noexcept;

            DynamicValue global_get(const std::string& key) const;
            void global_set(const std::string& key, const DynamicValue& value);

            DynamicValue packer_get(std::type_index ti, const std::string& key) const;
            void packer_set(std::type_index ti, const std::string& key, const DynamicValue& value);

            DynamicValue module_get(const std::string& module, const std::string& key) const;
            void module_set(const std::string& module, const std::string& key, const DynamicValue& value);

        private:
            using GlobalConfig = std::unordered_map<std::string, ConfigValue>;
            using PackerConfig = std::unordered_map<std::type_index, ConfigDict>;
            using ModuleConfig = std::unordered_map<std::string, ConfigDict>;

            GlobalConfig  _global_configs;
            PackerConfig  _packer_configs;
            ModuleConfig  _module_configs;
    };

    // ====================== 压缩管理器 ======================
    class DATA_API compression_manager {
        public:
            using Compressor   = std::vector<uint8_t> (*)(const uint8_t*, size_t);
            using Decompressor = std::vector<uint8_t> (*)(const uint8_t*, size_t);

            // 注册模块（可随时调用，但不会影响已初始化的 current_module）
            void register_module(const std::string& name, Compressor comp, Decompressor decomp);

            // 初始化：从已注册模块中选择一个作为当前模块，并拷贝其函数指针
            void init(const configuration& cfg);

            // 压缩（若 current_module 未初始化则抛异常）
            std::vector<uint8_t> compress(const uint8_t* data, size_t size, const std::string& module = {}) const;
            // 解压（若 current_module 未初始化则抛异常）
            std::vector<uint8_t> decompress(const uint8_t* data, size_t size, const std::string& module = {}) const;

            // 检查当前模块是否已设置
            bool has_current_module() const noexcept { return current_module.has_value(); }

        private:
            struct Module {
                Compressor   compress;
                Decompressor decompress;
            };

            std::unordered_map<std::string, Module> modules;
            std::optional<Module> current_module;
            std::string current_module_name;
    };

    // ====================== 工具函数 ======================
    namespace detail {
        DATA_API void consume_endmarker(std::istream& is);
        DATA_API std::array<uint8_t, 2> int_to_2byte_big_endian(uint16_t value) noexcept;
        DATA_API uint32_t read_variable_identifier(std::istream& is, bool allow_end);
    }

    // ====================== 序列化/反序列化函数 ======================
    DATA_API void pack_int64(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_int64(std::istream& is, packing_context& context);

    DATA_API void pack_double(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_double(std::istream& is, packing_context& context);

    DATA_API void pack_vector(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_vector(std::istream& is, packing_context& context);

    DATA_API void pack_string(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_string(std::istream& is, packing_context& context);

    DATA_API void pack_bytes(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_bytes(std::istream& is, packing_context& context);

    DATA_API void pack_null(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_null(std::istream& is, packing_context& context);

    DATA_API void pack_bool(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack_bool(std::istream& is, packing_context& context);

    // ====================== 全局序列化/反序列化接口 ======================
    DATA_API void pack(std::ostream& os, const DynamicValue& val, packing_context& context);
    DATA_API DynamicValue unpack(std::istream& is, packing_context& context);

    DATA_API void pack_multiple(
        std::vector<std::reference_wrapper<std::ostream>>& os_vector,
        const std::vector<DynamicValue>& vals,
        packing_context& context
    );
    DATA_API std::vector<DynamicValue> unpack_multiple(std::istream& is, packing_context& context);

    // ====================== 版本信息 ======================
    DATA_API const std::array<uint8_t, 5>& get_version() noexcept;
    DATA_API void check_version(const std::array<uint8_t, 5>& other_version, const configuration& config);
    DATA_API void check_version(const uint8_t other_version[5], const configuration& config);
    DATA_API void write_version(std::ostream& os);
    DATA_API std::array<uint8_t, 5> read_version(std::istream& is);

} // namespace data

// ====================== 模板实现 ======================
#include "data_impl.hpp"

#endif // DATA_HPP