#include "data.hpp"
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <bit>  // C++20 for byteswap, but we use builtins for compatibility

static inline uint32_t to_big_endian(uint32_t host) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return host;                     // 若本机是大端，无需转换
    } else {
        return std::byteswap(host);      // 小端交换字节序
    }
}

// 将大端（网络序）转换为主机字节序
static inline uint32_t from_big_endian(uint32_t big) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return big;
    } else {
        return std::byteswap(big);
    }
}


namespace data {
    // ========== 上下文管理器 ==========
    packing_context::packing_context(const type_registry& reg, const configuration& cfg, compression_manager& comp)
        : max_depth(cfg.global_get("max_recursion_depth").get<int64_t>()),
        registry(reg), config(cfg), compression(comp)
    {
        if (max_depth < 1) throw invalid_parameter_error("max_recursion_depth must be > 0");
    }

    packing_context::depth_guard::depth_guard(packing_context& ctx) : ctx_(ctx) {
        if (++ctx_.depth > ctx_.max_depth) {
            --ctx_.depth;
            throw recursion_error("Recursion depth exceeded");
        }
    }
    packing_context::depth_guard::~depth_guard() { --ctx_.depth; }

    // ========== configuration ==========
    configuration::configuration() { init(); }

    void configuration::init() {
        clear();
        _global_configs = {
            {"compress_preset", DynamicValue(static_cast<int64_t>(6))},
            {"compression_module", DynamicValue(std::string(""))},
            {"compress_data", DynamicValue(false)},
            {"disallow_decompress", DynamicValue(false)},
            {"chunked_compress", DynamicValue(false)},
            {"disallow_chunked_decompress", DynamicValue(false)},
            {"compress.chunk_size", DynamicValue(static_cast<int64_t>(65535))},
            {"decompress.chunk_size", DynamicValue(static_cast<int64_t>(65535))},
            {"multiple_compress", DynamicValue(false)},
            {"disallow_multiple_decompress", DynamicValue(false)},

            {"max_container_length", DynamicValue(static_cast<int64_t>(1048576))},
            {"max_recursion_depth", DynamicValue(static_cast<int64_t>(100))},

            {"allow_version_mismatch", DynamicValue(false)},
            {"version_prefix_match_length", DynamicValue(static_cast<int64_t>(3))},
            {"read_version", DynamicValue(true)},
            {"write_version", DynamicValue(true)}
        };

        ConfigDict zstd_dict;
        zstd_dict["compress_data"] = DynamicValue(false);
        zstd_dict["support_preset"] = DynamicValue(true);
        zstd_dict["chunked_compress"] = DynamicValue(false);
        zstd_dict["chunked_decompress"] = DynamicValue(false);
        _module_configs["zstd"] = std::move(zstd_dict);

        _packer_configs.clear();
    }

    void configuration::clear() noexcept {
        _global_configs.clear();
        _packer_configs.clear();
        _module_configs.clear();
    }

    DynamicValue configuration::global_get(const std::string& key) const {
        auto it = _global_configs.find(key);
        if (it == _global_configs.end())
            throw unknown_config_error("No global config for key: " + key);
        return it->second;
    }

    void configuration::global_set(const std::string& key, const DynamicValue& value) {
        _global_configs[key] = value;
    }

    DynamicValue configuration::packer_get(std::type_index ti, const std::string& key) const {
        auto it = _packer_configs.find(ti);
        if (it == _packer_configs.end())
            throw unknown_config_error("No packer config for type");
        auto sub = it->second.find(key);
        if (sub == it->second.end())
            throw unknown_config_error("Key not found in packer config");
        return sub->second;
    }

    void configuration::packer_set(std::type_index ti, const std::string& key, const DynamicValue& value) {
        _packer_configs[ti][key] = value;
    }

    DynamicValue configuration::module_get(const std::string& module, const std::string& key) const {
        auto it = _module_configs.find(module);
        if (it == _module_configs.end())
            throw unknown_config_error("No module config for: " + module);
        auto sub = it->second.find(key);
        if (sub == it->second.end())
            throw unknown_config_error("Key not found in module config");
        return sub->second;
    }

    void configuration::module_set(const std::string& module, const std::string& key, const DynamicValue& value) {
        _module_configs[module][key] = value;
    }

    // ========== type_registry 成员函数 ==========
    [[nodiscard]] Packer type_registry::get_packer_from_type(std::type_index ti) const {
        auto it = type_to_entry.find(ti);
        if (it == type_to_entry.end())
            throw unknown_type_error("No packer for type");
        return it->second.packer;
    }

    [[nodiscard]] Unpacker type_registry::get_unpacker_from_identifier(uint32_t identifier) const {
        auto it = identifier_to_type.find(identifier);
        if (it == identifier_to_type.end()) {
            // 调试输出（可移除）
            auto arr = uint32_to_identifier(identifier);
            std::cerr << "Unknown identifier: ";
            for (int i=0; i<4; ++i) std::cerr << std::hex << (int)arr[i] << " ";
            std::cerr << std::endl;
            throw unknown_identifier_error("Unknown identifier");
        }
        return type_to_entry.at(it->second).unpacker;
    }

    Unpacker type_registry::get_unpacker(std::istream& is) const {
        uint32_t id = read_identifier(is);
        return get_unpacker_from_identifier(id);
    }

    Unpacker type_registry::get_unpacker_allow_end(std::istream& is) const {
        uint32_t id = read_identifier_allow_end(is);
        if ((id & 0xFF000000) == 0xFF000000) {
            // 结束标记（首字节 0xFF）
            return Unpacker{nullptr};
        }
        return get_unpacker_from_identifier(id);
    }

    uint32_t type_registry::get_identifier_from_type(std::type_index ti) const {
        auto it = type_to_entry.find(ti);
        if (it == type_to_entry.end())
            throw unknown_type_error("No identifier for type");
        return it->second.identifier;
    }

    void type_registry::clear() noexcept {
        type_to_entry.clear();
        identifier_to_type.clear();
    }

    void type_registry::reset() {
        clear();
        init(true);
    }

    // ========== 工具函数 ==========
    namespace detail {
        void consume_endmarker(std::istream& is) {
            uint8_t byte;
            is.read(reinterpret_cast<char*>(&byte), 1);
            if (!is) throw std::runtime_error("EOF while reading end marker");
            if (byte != 0xFF) throw std::runtime_error("Expected end marker (0xFF)");
        }

        std::array<uint8_t, 2> int_to_2byte_big_endian(uint16_t value) noexcept {
            return { { static_cast<uint8_t>((value >> 8) & 0xFF),
                    static_cast<uint8_t>(value & 0xFF) } };
        }

        // 读取变长标识符，返回 uint32_t（大端序存储）
        uint32_t read_variable_identifier(std::istream& is, bool allow_end) {
            uint8_t first;
            is.read(reinterpret_cast<char*>(&first), 1);
            if (!is) throw std::runtime_error("EOF reading identifier first byte");

            if (first == 0xFF) {
                if (!allow_end)
                    throw invalid_identifier_error("End marker encountered where identifier expected");
                return 0xFF000000u;  // 首字节 0xFF，其余为 0
            }

            uint32_t id = static_cast<uint32_t>(first) << 24;
            size_t payload = 0;
            if (first == 0xFE) payload = 3;
            else if (first == 0xFD) payload = 2;
            else if (first == 0xFC) payload = 1;
            else {
                // 1字节标识符，已经完整
                return id;
            }

            // 读取 payload 个字节，填充到低 24 位（大端）
            for (size_t i = 0; i < payload; ++i) {
                uint8_t byte;
                is.read(reinterpret_cast<char*>(&byte), 1);
                if (!is) throw std::runtime_error("Truncated identifier");
                id |= static_cast<uint32_t>(byte) << (24 - 8 * (i + 1));
            }
            return id;
        }
    } // namespace detail

    uint32_t type_registry::read_identifier(std::istream& is) const {
        return detail::read_variable_identifier(is, false);
    }

    uint32_t type_registry::read_identifier_allow_end(std::istream& is) const {
        return detail::read_variable_identifier(is, true);
    }

    // ========== 序列化/反序列化函数 ==========
    void pack_int64(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        const int64_t num = val.get<int64_t>();
        context.registry.write_identifier<int64_t>(os);

        uint64_t v = (num < 0) ? -static_cast<uint64_t>(num) : static_cast<uint64_t>(num);
        bool negative = num < 0;

        if (negative) {
            uint8_t first = static_cast<uint8_t>((v & 0x7F) | 0x80);
            os.put(first);
            v >>= 7;
        } else {
            if (v == 0) {
                os.put(0x00);
                os.put('\xFF');
                return;
            }
            // 无符号正数，不设置标志位
            while (v > 0) {
                os.put(static_cast<uint8_t>(v & 0x7F));
                v >>= 7;
            }
        }
        os.put('\xFF');
    }

    DynamicValue unpack_int64(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        uint64_t value = 0;
        int shift = 0;
        uint8_t byte;
        is.read(reinterpret_cast<char*>(&byte), 1);
        if (!is) throw std::runtime_error("EOF");

        bool negative = (byte & 0x80) != 0;
        value = byte & 0x7F;
        shift = 7;

        while (true) {
            is.read(reinterpret_cast<char*>(&byte), 1);
            if (!is) throw std::runtime_error("EOF");
            if (byte == 0xFF) break;
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        }

        int64_t result = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
        return DynamicValue(result);
    }

    void pack_double(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        const double num = val.get<double>();
        context.registry.write_identifier<double>(os);
        os.write(reinterpret_cast<const char*>(&num), sizeof(double));
        os.put('\xFF');
    }

    DynamicValue unpack_double(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        double num;
        is.read(reinterpret_cast<char*>(&num), sizeof(double));
        detail::consume_endmarker(is);
        return DynamicValue(num);
    }

    void pack_vector(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        const auto& vector = val.get<std::vector<DynamicValue>>();
        context.registry.write_identifier<std::vector<DynamicValue>>(os);
        for (const auto& item : vector) {
            Packer p = context.registry.get_packer_from_type(item.type());
            p(os, item, context);
        }
        os.put('\xFF');
    }

    DynamicValue unpack_vector(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        std::vector<DynamicValue> vector;
        while (true) {
            Unpacker unpacker = context.registry.get_unpacker_allow_end(is);
            if (!unpacker) break;  // 结束标记
            vector.push_back(unpacker(is, context));
        }
        return DynamicValue(vector);
    }

    void pack_string(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        const auto& str = val.get<std::string>();
        if (str.size() > 65535ULL) throw std::overflow_error("String too long");
        context.registry.write_identifier<std::string>(os);
        auto len_bytes = detail::int_to_2byte_big_endian(static_cast<uint16_t>(str.size()));
        os.write(reinterpret_cast<const char*>(len_bytes.data()), 2);
        os.write(str.data(), str.size());
        os.put('\xFF');
    }

    DynamicValue unpack_string(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        uint8_t len_buf[2];
        is.read(reinterpret_cast<char*>(len_buf), 2);
        if (!is) throw std::runtime_error("EOF");
        uint16_t len = (static_cast<uint16_t>(len_buf[0]) << 8) | len_buf[1];
        std::string str;
        str.resize(len);
        if (len > 0) {
            is.read(str.data(), len);
            if (!is) throw std::runtime_error("EOF reading string");
        }
        detail::consume_endmarker(is);
        return DynamicValue(std::move(str));
    }

    void pack_bytes(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        const auto& bytes = val.get<std::vector<uint8_t>>();
        if (bytes.size() > 65535) throw std::overflow_error("Bytes too long");
        context.registry.write_identifier<std::vector<uint8_t>>(os);
        auto len_bytes = detail::int_to_2byte_big_endian(static_cast<uint16_t>(bytes.size()));
        os.write(reinterpret_cast<const char*>(len_bytes.data()), 2);
        os.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        os.put('\xFF');
    }

    DynamicValue unpack_bytes(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        uint8_t len_buf[2];
        is.read(reinterpret_cast<char*>(len_buf), 2);
        if (!is) throw std::runtime_error("EOF");
        uint16_t len = (static_cast<uint16_t>(len_buf[0]) << 8) | len_buf[1];
        std::vector<uint8_t> bytes(len);
        if (len > 0) {
            is.read(reinterpret_cast<char*>(bytes.data()), len);
            if (!is) throw std::runtime_error("EOF reading bytes");
        }
        detail::consume_endmarker(is);
        return DynamicValue(std::move(bytes));
    }

    void pack_null(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        (void)val.get<std::nullptr_t>();  // 类型检查
        context.registry.write_identifier<std::nullptr_t>(os);
        os.put('\xFF');
    }

    DynamicValue unpack_null(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        detail::consume_endmarker(is);
        return DynamicValue(nullptr);
    }

    void pack_bool(std::ostream& os, const DynamicValue& val, packing_context& context) {
        packing_context::depth_guard guard(context);
        context.registry.write_identifier<bool>(os);
        bool b = val.get<bool>();
        os.put(b ? 0x01 : 0x00);
        os.put('\xFF');
    }

    DynamicValue unpack_bool(std::istream& is, packing_context& context) {
        packing_context::depth_guard guard(context);
        uint8_t byte;
        is.read(reinterpret_cast<char*>(&byte), 1);
        detail::consume_endmarker(is);
        return DynamicValue(byte != 0);
    }

    // ========== type_registry::init ==========
    void type_registry::init(bool overwrite) {
        // 注意：标识符使用大端序存储，这里数组按 {0x00,0x00,0x00,0x00} 表示 0x00000000
        register_type<int64_t>({{0x00,0x00,0x00,0x00}}, pack_int64, unpack_int64, overwrite);
        register_type<double>({{0x01,0x00,0x00,0x00}}, pack_double, unpack_double, overwrite);
        register_type<std::vector<DynamicValue>>({{0x05,0x00,0x00,0x00}}, pack_vector, unpack_vector, overwrite);
        register_type<std::string>({{0x08,0x00,0x00,0x00}}, pack_string, unpack_string, overwrite);
        register_type<std::vector<uint8_t>>({{0x09,0x00,0x00,0x00}}, pack_bytes, unpack_bytes, overwrite);
        register_type<std::nullptr_t>({{0x0A,0x00,0x00,0x00}}, pack_null, unpack_null, overwrite);
        register_type<bool>({{0x0B,0x00,0x00,0x00}}, pack_bool, unpack_bool, overwrite);
    }

    // ========== 压缩管理器 ==========
    void compression_manager::register_module(const std::string& name, Compressor comp, Decompressor decomp) {
        modules[name] = {comp, decomp};
    }

    void compression_manager::init(const configuration& cfg) {
        current_module_name = cfg.global_get("compression_module").get<std::string>();
        if (current_module_name.empty()) {
            current_module.reset();   // 清空
            return;
        }
        auto it = modules.find(current_module_name);
        if (it == modules.end()) {
            throw std::runtime_error("Compression module not registered: " + current_module_name);
        }
        // 拷贝一份，不再依赖 map
        current_module = it->second;
    }

    std::vector<uint8_t> compression_manager::compress(const uint8_t* data, size_t size, const std::string& module) const {
        if (!module.empty()) {
            auto it = modules.find(module);
            if (it == modules.end())
                throw std::runtime_error("Module not found: " + module);
            return it->second.compress(data, size);
        }
        if (!current_module.has_value())
            throw std::runtime_error("No compression module selected");
        return current_module->compress(data, size);
    }

    std::vector<uint8_t> compression_manager::decompress(const uint8_t* data, size_t size, const std::string& module) const {
        if (!module.empty()) {
            auto it = modules.find(module);
            if (it == modules.end())
                throw std::runtime_error("Module not found: " + module);
            return it->second.decompress(data, size);
        }
        if (!current_module.has_value())
            throw std::runtime_error("No compression module selected");
        return current_module->decompress(data, size);
    }

    // ========== 版本信息 ==========
    inline constexpr std::array<uint8_t, 5> version = {{0, 5, 0, 0, 0}};

    const std::array<uint8_t, 5>& get_version() noexcept { return version; }

    void check_version(const std::array<uint8_t, 5>& other, const configuration& config) {
        if (config.global_get("allow_version_mismatch").get<bool>()) return;
        if (version[0] == other[0] && version[1] == other[1] && version[2] == other[2]) {
            return;
        }
        int64_t tolerance = config.global_get("version_prefix_match_length").get<int64_t>();
        if (tolerance <= 0) throw std::runtime_error("Version mismatch");
        int64_t len = std::min(tolerance, 5LL);
        for (int i = 0; i < len; ++i) {
            if (version[i] != other[i]) throw std::runtime_error("Version mismatch");
        }
    }

    void check_version(const uint8_t other[5], const configuration& config) {
        std::array<uint8_t,5> arr;
        std::copy(other, other+5, arr.begin());
        check_version(arr, config);
    }

    void write_version(std::ostream& os) {
        os.write(reinterpret_cast<const char*>(version.data()), 3);
        if (!os) throw std::runtime_error("EOF writing version");
    }

    std::array<uint8_t, 5> read_version(std::istream& is) {
        std::array<uint8_t, 5> ver = {{0, 0, 0, version[3], version[4]}};
        is.read(reinterpret_cast<char*>(ver.data()), 3);
        if (is.gcount() != 3) throw std::runtime_error("EOF reading version");
        return ver;
    }

    // ========== 全局序列化/反序列化 ==========

    void pack(std::ostream& os, const DynamicValue& val, packing_context& context) {
        context.depth = 0;
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool write_ver = context.config.global_get("write_version").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        // ---------- 步骤1：将数据（含版本头）序列化到原始内存缓冲区 ----------
        std::vector<uint8_t> raw_data;
        std::ostringstream raw_oss(std::ios::binary);
        if (write_ver) {
            raw_oss.write(reinterpret_cast<const char*>(get_version().data()), 3);
        }
        Packer p = context.registry.get_packer_from_type(val.type());
        if (!p) throw type_mismatch_error("No packer for type");
        p(raw_oss, val, context);
        const std::string& raw_str = raw_oss.str();
        raw_data.assign(raw_str.begin(), raw_str.end());

        // ---------- 如果不需要压缩，直接写入原始数据 ----------
        if (!do_compress) {
            os.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
            if (os.fail()) throw std::runtime_error("Failed to write uncompressed data");
            return;
        }

        // ---------- 准备压缩模块 ----------
        std::string module = context.config.global_get("compression_module").get<std::string>();
        if (module.empty()) throw std::runtime_error("compression_module not set when compress_data is true");

        // ---------- 分支 A：整块压缩（单块） ----------
        if (!chunked) {
            auto compressed = context.compression.compress(raw_data.data(), raw_data.size(), module);
            uint32_t size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
            os.write(reinterpret_cast<const char*>(&size_be), 4);
            os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            if (os.fail()) throw std::runtime_error("Failed to write whole compressed block");
            return;
        }

        // ---------- 分支 B：分块压缩 ----------
        size_t chunk_size = static_cast<size_t>(
            context.config.global_get("compress.chunk_size").get<int64_t>()
        );
        if (chunk_size == 0) chunk_size = 65535; // 兜底

        uint32_t num_chunks = static_cast<uint32_t>((raw_data.size() + chunk_size - 1) / chunk_size);
        uint32_t num_chunks_be = to_big_endian(num_chunks);
        os.write(reinterpret_cast<const char*>(&num_chunks_be), 4);

        for (uint32_t i = 0; i < num_chunks; ++i) {
            size_t offset = static_cast<size_t>(i) * chunk_size;
            size_t len = std::min(chunk_size, raw_data.size() - offset);

            auto compressed = context.compression.compress(raw_data.data() + offset, len, module);
            uint32_t comp_size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
            os.write(reinterpret_cast<const char*>(&comp_size_be), 4);
            os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
        }
        if (os.fail()) throw std::runtime_error("Failed to write chunked compressed data");
    }

    DynamicValue unpack(std::istream& is, packing_context& context) {
        context.depth = 0;
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool read_ver = context.config.global_get("read_version").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        // ---------- 如果不需要解压，直接读取原始数据 ----------
        if (!do_compress) {
            if (read_ver) {
                auto ver = read_version(is);
                check_version(ver, context.config);
            }
            Unpacker unpacker = context.registry.get_unpacker(is);
            return unpacker(is, context);
        }

        std::string module = context.config.global_get("compression_module").get<std::string>();
        if (module.empty()) throw std::runtime_error("compression_module not set for decompress");

        std::vector<uint8_t> raw_data;

        // ---------- 分支 A：整块解压 ----------
        if (!chunked) {
            uint32_t comp_size_be;
            is.read(reinterpret_cast<char*>(&comp_size_be), 4);
            if (is.gcount() != 4) throw std::runtime_error("EOF reading compressed size");
            size_t comp_size = from_big_endian(comp_size_be);

            std::vector<uint8_t> compressed(comp_size);
            is.read(reinterpret_cast<char*>(compressed.data()), comp_size);
            if (is.gcount() != static_cast<std::streamsize>(comp_size))
                throw std::runtime_error("Truncated compressed data");

            raw_data = context.compression.decompress(compressed.data(), comp_size, module);
        }
        // ---------- 分支 B：分块解压 ----------
        else {
            if (context.config.global_get("disallow_chunked_decompress").get<bool>()) {
                throw std::runtime_error("Chunked decompression is disallowed by config");
            }

            uint32_t num_chunks_be;
            is.read(reinterpret_cast<char*>(&num_chunks_be), 4);
            if (is.gcount() != 4) throw std::runtime_error("EOF reading chunk count");
            uint32_t num_chunks = from_big_endian(num_chunks_be);

            raw_data.reserve(num_chunks * static_cast<size_t>(
                context.config.global_get("decompress.chunk_size").get<int64_t>()
            )); // 预留空间优化性能

            for (uint32_t i = 0; i < num_chunks; ++i) {
                uint32_t comp_size_be;
                is.read(reinterpret_cast<char*>(&comp_size_be), 4);
                if (is.gcount() != 4) throw std::runtime_error("EOF reading chunk size");
                size_t comp_size = from_big_endian(comp_size_be);

                std::vector<uint8_t> compressed(comp_size);
                is.read(reinterpret_cast<char*>(compressed.data()), comp_size);
                if (is.gcount() != static_cast<std::streamsize>(comp_size))
                    throw std::runtime_error("Truncated chunk data");

                auto chunk_raw = context.compression.decompress(compressed.data(), comp_size, module);
                raw_data.insert(raw_data.end(), chunk_raw.begin(), chunk_raw.end());
            }
        }

        // ---------- 将解压后的完整原始数据转为流，进行反序列化 ----------
        std::istringstream raw_is(
            std::string(reinterpret_cast<const char*>(raw_data.data()), raw_data.size()),
            std::ios::binary
        );
        if (read_ver) {
            auto ver = read_version(raw_is);
            check_version(ver, context.config);
        }
        Unpacker unpacker = context.registry.get_unpacker(raw_is);
        return unpacker(raw_is, context);
    }

    void pack_multiple(
        std::vector<std::reference_wrapper<std::ostream>>& os_vector,
        const std::vector<DynamicValue>& vals,
        packing_context& context
    ) {
        if (os_vector.size() != vals.size())
            throw std::invalid_argument("Size mismatch");

        const bool multi_compress = context.config.global_get("multiple_compress").get<bool>();
        const bool write_ver = context.config.global_get("write_version").get<bool>();

        // 如果不开启多流压缩，每个流独立走单值 pack（这会调用上面的 pack，但可能重复写版本头）
        // 为了和单值 pack 行为完全一致，我们直接调用 pack 函数
        if (!multi_compress) {
            for (size_t i = 0; i < vals.size(); ++i) {
                // 由于 pack 内部会再次查询配置，我们直接调用它
                // 注意：这会为每个流单独写版本头，符合原来的行为
                pack(os_vector[i].get(), vals[i], context);
            }
            return;
        }

        // ====== 多流压缩模式：所有值合并为一个原始块 ======
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        // 1. 将所有值序列化到同一个原始缓冲区（版本头只写一次）
        std::vector<uint8_t> raw_data;
        std::ostringstream raw_oss(std::ios::binary);
        if (write_ver) {
            raw_oss.write(reinterpret_cast<const char*>(get_version().data()), 3);
        }
        for (const auto& val : vals) {
            context.depth = 0; // 每个值重置深度
            Packer p = context.registry.get_packer_from_type(val.type());
            if (!p) throw type_mismatch_error("No packer for type");
            p(raw_oss, val, context);
        }
        const std::string& raw_str = raw_oss.str();
        raw_data.assign(raw_str.begin(), raw_str.end());

        // 2. 如果不压缩，直接把原始数据写到每个流
        if (!do_compress) {
            for (auto& os_ref : os_vector) {
                auto& os = os_ref.get();
                os.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
                if (os.fail()) throw std::runtime_error("Failed to write raw multi data");
            }
            return;
        }

        // 3. 压缩（整块或分块），并将结果写入每个流
        std::string module = context.config.global_get("compression_module").get<std::string>();
        if (module.empty()) throw std::runtime_error("compression_module not set");

        std::vector<uint8_t> final_output; // 暂存最终要写入的内容
        std::ostringstream tmp_os(std::ios::binary);

        if (!chunked) {
            auto compressed = context.compression.compress(raw_data.data(), raw_data.size(), module);
            uint32_t size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
            tmp_os.write(reinterpret_cast<const char*>(&size_be), 4);
            tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
        } else {
            size_t chunk_size = static_cast<size_t>(
                context.config.global_get("compress.chunk_size").get<int64_t>()
            );
            if (chunk_size == 0) chunk_size = 65535;
            uint32_t num_chunks = static_cast<uint32_t>((raw_data.size() + chunk_size - 1) / chunk_size);
            uint32_t num_chunks_be = to_big_endian(num_chunks);
            tmp_os.write(reinterpret_cast<const char*>(&num_chunks_be), 4);

            for (uint32_t i = 0; i < num_chunks; ++i) {
                size_t offset = static_cast<size_t>(i) * chunk_size;
                size_t len = std::min(chunk_size, raw_data.size() - offset);
                auto compressed = context.compression.compress(raw_data.data() + offset, len, module);
                uint32_t comp_size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
                tmp_os.write(reinterpret_cast<const char*>(&comp_size_be), 4);
                tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            }
        }

        const std::string& tmp_str = tmp_os.str();
        for (auto& os_ref : os_vector) {
            auto& os = os_ref.get();
            os.write(tmp_str.data(), tmp_str.size());
            if (os.fail()) throw std::runtime_error("Failed to write compressed multi data");
        }
    }

    void pack_multiple(
        std::ostream& os,
        const std::vector<DynamicValue>& vals,
        packing_context& context
    ) {
        const bool multi_compress = context.config.global_get("multiple_compress").get<bool>();
        const bool write_ver = context.config.global_get("write_version").get<bool>();

        // 如果不开启多流压缩，每个流独立走单值 pack（这会调用上面的 pack，但可能重复写版本头）
        // 为了和单值 pack 行为完全一致，我们直接调用 pack 函数
        if (!multi_compress) {
            for (size_t i = 0; i < vals.size(); ++i) {
                // 由于 pack 内部会再次查询配置，我们直接调用它
                // 注意：这会为每个流单独写版本头，符合原来的行为
                pack(os, vals[i], context);
            }
            return;
        }

        // ====== 多流压缩模式：所有值合并为一个原始块 ======
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        // 1. 将所有值序列化到同一个原始缓冲区（版本头只写一次）
        std::vector<uint8_t> raw_data;
        std::ostringstream raw_oss(std::ios::binary);
        if (write_ver) {
            raw_oss.write(reinterpret_cast<const char*>(get_version().data()), 3);
        }
        for (const auto& val : vals) {
            context.depth = 0; // 每个值重置深度
            Packer p = context.registry.get_packer_from_type(val.type());
            if (!p) throw type_mismatch_error("No packer for type");
            p(raw_oss, val, context);
        }
        const std::string& raw_str = raw_oss.str();
        raw_data.assign(raw_str.begin(), raw_str.end());

        // 2. 如果不压缩，直接把原始数据写到每个流
        if (!do_compress) {
            os.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
            if (os.fail()) throw std::runtime_error("Failed to write raw multi data");
            return;
        }

        // 3. 压缩（整块或分块），并将结果写入每个流
        std::string module = context.config.global_get("compression_module").get<std::string>();
        if (module.empty()) throw std::runtime_error("compression_module not set");

        std::vector<uint8_t> final_output; // 暂存最终要写入的内容
        std::ostringstream tmp_os(std::ios::binary);

        if (!chunked) {
            auto compressed = context.compression.compress(raw_data.data(), raw_data.size(), module);
            uint32_t size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
            tmp_os.write(reinterpret_cast<const char*>(&size_be), 4);
            tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
        } else {
            size_t chunk_size = static_cast<size_t>(
                context.config.global_get("compress.chunk_size").get<int64_t>()
            );
            if (chunk_size == 0) chunk_size = 65535;
            uint32_t num_chunks = static_cast<uint32_t>((raw_data.size() + chunk_size - 1) / chunk_size);
            uint32_t num_chunks_be = to_big_endian(num_chunks);
            tmp_os.write(reinterpret_cast<const char*>(&num_chunks_be), 4);

            for (uint32_t i = 0; i < num_chunks; ++i) {
                size_t offset = static_cast<size_t>(i) * chunk_size;
                size_t len = std::min(chunk_size, raw_data.size() - offset);
                auto compressed = context.compression.compress(raw_data.data() + offset, len, module);
                uint32_t comp_size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
                tmp_os.write(reinterpret_cast<const char*>(&comp_size_be), 4);
                tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            }
        }

        const std::string& tmp_str = tmp_os.str();
        os.write(tmp_str.data(), tmp_str.size());
        if (os.fail()) throw std::runtime_error("Failed to write compressed multi data");
    }

    void pack_multiple(
        std::vector<std::reference_wrapper<std::ostream>>& os_vector,
        const DynamicValue& val,
        packing_context& context
    ) {
        const bool multi_compress = context.config.global_get("multiple_compress").get<bool>();
        const bool write_ver = context.config.global_get("write_version").get<bool>();

        // 如果不开启多流压缩，每个流独立走单值 pack（这会调用上面的 pack，但可能重复写版本头）
        // 为了和单值 pack 行为完全一致，我们直接调用 pack 函数
        if (!multi_compress) {
            for (auto& os_ref : os_vector) {
                // 由于 pack 内部会再次查询配置，我们直接调用它
                // 注意：这会为每个流单独写版本头，符合原来的行为
                pack(os_ref.get(), val, context);
            }
            return;
        }

        // ====== 多流压缩模式：所有值合并为一个原始块 ======
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        // 1. 将所有值序列化到同一个原始缓冲区（版本头只写一次）
        std::vector<uint8_t> raw_data;
        std::ostringstream raw_oss(std::ios::binary);
        if (write_ver) {
            raw_oss.write(reinterpret_cast<const char*>(get_version().data()), 3);
        }

        Packer p = context.registry.get_packer_from_type(val.type());
        if (!p) throw type_mismatch_error("No packer for type");
        p(raw_oss, val, context);

        const std::string& raw_str = raw_oss.str();
        raw_data.assign(raw_str.begin(), raw_str.end());

        // 2. 如果不压缩，直接把原始数据写到每个流
        if (!do_compress) {
            for (auto& os_ref : os_vector) {
                auto& os = os_ref.get();
                os.write(reinterpret_cast<const char*>(raw_data.data()), raw_data.size());
                if (os.fail()) throw std::runtime_error("Failed to write raw multi data");
            }
            return;
        }

        // 3. 压缩（整块或分块），并将结果写入每个流
        std::string module = context.config.global_get("compression_module").get<std::string>();
        if (module.empty()) throw std::runtime_error("compression_module not set");

        std::vector<uint8_t> final_output; // 暂存最终要写入的内容
        std::ostringstream tmp_os(std::ios::binary);

        if (!chunked) {
            auto compressed = context.compression.compress(raw_data.data(), raw_data.size(), module);
            uint32_t size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
            tmp_os.write(reinterpret_cast<const char*>(&size_be), 4);
            tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
        } else {
            size_t chunk_size = static_cast<size_t>(
                context.config.global_get("compress.chunk_size").get<int64_t>()
            );
            if (chunk_size == 0) chunk_size = 65535;
            uint32_t num_chunks = static_cast<uint32_t>((raw_data.size() + chunk_size - 1) / chunk_size);
            uint32_t num_chunks_be = to_big_endian(num_chunks);
            tmp_os.write(reinterpret_cast<const char*>(&num_chunks_be), 4);

            for (uint32_t i = 0; i < num_chunks; ++i) {
                size_t offset = static_cast<size_t>(i) * chunk_size;
                size_t len = std::min(chunk_size, raw_data.size() - offset);
                auto compressed = context.compression.compress(raw_data.data() + offset, len, module);
                uint32_t comp_size_be = to_big_endian(static_cast<uint32_t>(compressed.size()));
                tmp_os.write(reinterpret_cast<const char*>(&comp_size_be), 4);
                tmp_os.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
            }
        }

        const std::string& tmp_str = tmp_os.str();
        for (auto& os_ref : os_vector) {
            auto& os = os_ref.get();
            os.write(tmp_str.data(), tmp_str.size());
            if (os.fail()) throw std::runtime_error("Failed to write compressed multi data");
        }
    }

    std::vector<DynamicValue> unpack_multiple(std::istream& is, packing_context& context) {
        const bool multi_compress = context.config.global_get("multiple_compress").get<bool>();
        const bool read_ver = context.config.global_get("read_version").get<bool>();

        // 非多流压缩模式：逐个调用 unpack
        if (!multi_compress) {
            std::vector<DynamicValue> result;
            while (true) {
                if (is.peek() == EOF) break;
                // 由于 unpack 会处理版本头，直接调用
                result.push_back(unpack(is, context));
            }
            return result;
        }

        // ====== 多流压缩模式：读出一个完整的大块，解压，然后从中解析多个值 ======
        const bool do_compress = context.config.global_get("compress_data").get<bool>();
        const bool chunked = do_compress && context.config.global_get("chunked_compress").get<bool>();

        std::vector<uint8_t> raw_data;

        if (!do_compress) {
            // 没有压缩，直接读取所有原始数据（需知道何时结束，这里使用读全部）
            // 因为是多流压缩模式，我们假定整个流就是压缩后的一个整体，否则走上面的分支。
            std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(is)),
                                        std::istreambuf_iterator<char>());
            raw_data.swap(buffer);
        } else {
            std::string module = context.config.global_get("compression_module").get<std::string>();
            if (module.empty()) throw std::runtime_error("compression_module not set");

            if (!chunked) {
                uint32_t comp_size_be;
                is.read(reinterpret_cast<char*>(&comp_size_be), 4);
                if (is.gcount() != 4) throw std::runtime_error("EOF reading compressed size");
                size_t comp_size = from_big_endian(comp_size_be);
                std::vector<uint8_t> compressed(comp_size);
                is.read(reinterpret_cast<char*>(compressed.data()), comp_size);
                if (is.gcount() != static_cast<std::streamsize>(comp_size))
                    throw std::runtime_error("Truncated compressed data");
                raw_data = context.compression.decompress(compressed.data(), comp_size, module);
            } else {
                if (context.config.global_get("disallow_chunked_decompress").get<bool>()) {
                    throw std::runtime_error("Chunked decompression is disallowed");
                }
                uint32_t num_chunks_be;
                is.read(reinterpret_cast<char*>(&num_chunks_be), 4);
                if (is.gcount() != 4) throw std::runtime_error("EOF reading chunk count");
                uint32_t num_chunks = from_big_endian(num_chunks_be);

                for (uint32_t i = 0; i < num_chunks; ++i) {
                    uint32_t comp_size_be;
                    is.read(reinterpret_cast<char*>(&comp_size_be), 4);
                    if (is.gcount() != 4) throw std::runtime_error("EOF reading chunk size");
                    size_t comp_size = from_big_endian(comp_size_be);
                    std::vector<uint8_t> compressed(comp_size);
                    is.read(reinterpret_cast<char*>(compressed.data()), comp_size);
                    if (is.gcount() != static_cast<std::streamsize>(comp_size))
                        throw std::runtime_error("Truncated chunk data");
                    auto chunk_raw = context.compression.decompress(compressed.data(), comp_size, module);
                    raw_data.insert(raw_data.end(), chunk_raw.begin(), chunk_raw.end());
                }
            }
        }

        // 从解压后的原始流中解析出所有 DynamicValue
        std::istringstream raw_is(
            std::string(reinterpret_cast<const char*>(raw_data.data()), raw_data.size()),
            std::ios::binary
        );
        std::vector<DynamicValue> result;
        while (true) {
            if (raw_is.peek() == EOF) break;
            if (read_ver) {
                auto ver = read_version(raw_is);
                check_version(ver, context.config);
            }
            Unpacker unpacker = context.registry.get_unpacker(raw_is);
            result.push_back(unpacker(raw_is, context));
        }
        return result;
    }

} // namespace data