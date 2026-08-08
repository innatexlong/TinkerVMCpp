//
// Created by Administrator on 2026/8/2.
//
module;
#include <cstdint>
#include <exception>
#include <istream>
#include <print>
#include <bit>
#include <cstring>

export module parser;

import env;
import exceptions;
import io_utils;

namespace parser {
    export enum class Opcode : std::uint8_t {
        add, addc, sub, mul, div, mod,
        new_, del, retc, retv, movc, mov  // return_const return_variable
    };

    export env::TempVariable func([[maybe_unused]] env::Env& env, std::istream& is) {
        while (true) {
            char c;
            io_utils::get(is, c);
            switch (static_cast<Opcode>(c)) {
                // case Opcode::add: {
                //     // TODO: add
                //     env::pos_t l_num_pos;
                //     io_utils::read(is, reinterpret_cast<char*>(&l_num_pos), sizeof(decltype(l_num_pos)));
                //     if constexpr (std::endian::native == std::endian::little) {
                //         l_num_pos = std::byteswap(l_num_pos);
                //     }
                //     env::pos_t r_num_pos;
                //     io_utils::read(is, reinterpret_cast<char*>(&r_num_pos), sizeof(decltype(r_num_pos)));
                //     if constexpr (std::endian::native == std::endian::little) {
                //         r_num_pos = std::byteswap(r_num_pos);
                //     }
                //     std::println(":{}",
                //         env.memory_pool.get<std::uint32_t>(l_num_pos) + env.memory_pool.get<std::uint32_t>(r_num_pos)
                //     );
                //     break;
                // }
                case Opcode::add: {
                    std::uint32_t var_id_res, var_id_l, var_id_r;
                    io_utils::read(is, reinterpret_cast<char*>(&var_id_res), sizeof(var_id_res));
                    io_utils::read(is, reinterpret_cast<char*>(&var_id_l), sizeof(var_id_l));
                    io_utils::read(is, reinterpret_cast<char*>(&var_id_r), sizeof(var_id_r));
                    // if constexpr (std::endian::native == std::endian::little) {
                    //     var_id_res = std::byteswap(var_id_res);
                    //     var_id_l = std::byteswap(var_id_l);
                    //     var_id_r = std::byteswap(var_id_r);
                    // }

                    auto it_res = env.variables.find(var_id_res);
                    auto it_l = env.variables.find(var_id_l);
                    auto it_r = env.variables.find(var_id_r);
                    if (it_res == env.variables.end() || it_l == env.variables.end() || it_r == env.variables.end())
                        throw exceptions::SegFault("Variable not found");

                    const auto& var_res = it_res->second;
                    const auto& var_l = it_l->second;
                    const auto& var_r = it_r->second;

                    // 检查类型是否一致（或按需转换）
                    if (var_res.type != var_l.type || var_l.type != var_r.type)
                        throw exceptions::SizeMismatch("Add operands must have same type");

                    switch (var_l.type) {
                        case env::VarType::Char: {
                            throw std::runtime_error("Char is not implemented yet");
                            break;
                        }
                        case env::VarType::UInt32: {
                            auto val_l = env.memory_pool.get<std::uint32_t>(var_l.ptr);
                            auto val_r = env.memory_pool.get<std::uint32_t>(var_r.ptr);
                            env.memory_pool.set<std::uint32_t>(var_res.ptr, val_l + val_r);
                            std::println("{}", val_l + val_r);
                            break;
                        }
                        case env::VarType::Float32: {
                            // 假设 MemoryPool 支持 float 存取，需额外实现 get<float>
                            const auto val_l = static_cast<float>(env.memory_pool.get<uint32_t>(var_l.ptr));
                            const auto val_r = static_cast<float>(env.memory_pool.get<uint32_t>(var_r.ptr));
                            env.memory_pool.set<std::uint32_t>(var_res.ptr, std::bit_cast<uint32_t>(val_l + val_r));
                            std::println("{}", val_l + val_r);
                            break;
                        }
                        case env::VarType::Str: {
                            throw exceptions::InvalidOperation("Str is not implemented yet");
                        }
                        case env::VarType::VarPtr: {
                            throw exceptions::InvalidOperation("VarPtr is not implemented yet");
                        }
                        default:
                            throw exceptions::InvalidOperation("Unsupported type for addition");
                    }
                    break;
                }
                case Opcode::addc: {
                    // TODO: addc
                    break;
                }
                case Opcode::sub: {
                    // TODO: sub
                    break;
                }
                case Opcode::mul: {
                    // TODO: mul
                    break;
                }
                case Opcode::div: {
                    // TODO: div
                    break;
                }
                case Opcode::mod: {
                    // TODO: mod
                    break;
                }
                case Opcode::new_: {
                    std::uint32_t var_id, size;
                    io_utils::read(is, reinterpret_cast<char*>(&var_id), sizeof(var_id));
                    io_utils::read(is, reinterpret_cast<char*>(&size), sizeof(size));
                    // 字节序交换
                    // if constexpr (std::endian::native == std::endian::little) {
                    //     var_id = std::byteswap(var_id);
                    //     size = std::byteswap(size);
                    // }
                    // 假设指令还携带类型（VarType），这里暂缺，可约定默认或扩展指令
                    std::uint32_t ptr = env.memory_pool.allocate(size);
                    env::Variable var;
                    var.id = var_id;
                    var.ptr = ptr;
                    var.type = env::VarType::UInt32; // 或从指令读取
                    var.size = size;
                    env.variables[var_id] = var;
                    break;
                }
                case Opcode::del: {
                    std::uint32_t var_id;
                    io_utils::read(is, reinterpret_cast<char*>(&var_id), sizeof(var_id));
                    // 字节序交换...
                    auto it = env.variables.find(var_id);
                    if (it == env.variables.end())
                        throw exceptions::SegFault("Variable not found");
                    const auto& var = it->second;
                    // 需要知道分配的大小，可在Variable中增加size字段，或从类型推导
                    // 这里假设我们存储了size（需修改Variable结构）
                    env.memory_pool.deallocate(var.ptr, var.size);
                    env.variables.erase(it);
                    break;
                }
                case Opcode::retc: {  // ret <uint32_t>
                    // if constexpr (std::endian::native == std::endian::little) {
                    //     ret_code = std::byteswap(ret_code);
                    // }

                    std::vector<std::uint8_t> ret(sizeof(std::uint32_t), 0);
                    io_utils::read(is, reinterpret_cast<char*>(ret.data()), sizeof(std::uint32_t));
                    return env::TempVariable{.value = std::move(ret), .type = env::VarType::UInt32};
                }
                case Opcode::retv: {
                    // TODO
                    std::vector<std::uint8_t> ret_var(sizeof(env::pos_t), 0);
                    env::pos_t var_id;
                    io_utils::read(is, reinterpret_cast<char*>(&var_id), sizeof(env::pos_t));
                    const auto& var = env.variables.find(var_id);
                    if (var == env.variables.end()) {
                        throw exceptions::VarNotFound(std::format("Var not found {}", var_id));
                    }
                    return env::TempVariable{.value = std::move(ret_var), .type = env::VarType::VarPtr};
                    break;
                }
                case Opcode::mov: {
                    // TODO
                    std::uint32_t src_var, dest_var;
                    io_utils::read(is, reinterpret_cast<char*>(&src_var), sizeof(src_var));
                    io_utils::read(is, reinterpret_cast<char*>(&dest_var), sizeof(dest_var));
                    
                    break;
                }
                case Opcode::movc: {
                    std::uint32_t val, dest_var;
                    io_utils::read(is, reinterpret_cast<char*>(&val), sizeof(val));
                    io_utils::read(is, reinterpret_cast<char*>(&dest_var), sizeof(dest_var));
                    // TODO: 实现 movc
                    break;
                }
                default: throw exceptions::InvalidOperation("Unknown operation");
            }
        }
    }

    export template <bool catch_exception = false, bool print_caught_exception = true>
    std::uint32_t main(env::Env &env, std::istream &is) {
        if constexpr (catch_exception) {
            try {
                switch (env::TempVariable ret = func(env, is); ret.type) {
                    case env::VarType::Char: {
                        if (ret.value.size() == 1) {
                            return ret.value[0];
                        }
                        throw exceptions::SegFault("Size of temp variable as a return value must be 1");
                    }
                    case env::VarType::UInt32: {
                        if (ret.value.size() == 4) {
                            env::pos_t value;
                            std::memcpy(&value, ret.value.data(), sizeof(value));
                            return value;
                        }
                        throw std::runtime_error("Size of temp variable as a return value must be 4 bytes");
                    }
                    case env::VarType::Float32: {
                        throw exceptions::InvalidVarType("main() must return uint32");
                    }
                    case env::VarType::Str: {
                        throw exceptions::InvalidVarType("main() must return uint32, not Str");
                    }
                    case env::VarType::VarPtr: {
                        if (ret.value.size() != sizeof(env::pos_t)) {
                            throw exceptions::SizeMismatch(
                                std::format("Size of variable ptr must be 4, not {}", ret.value.size())
                            );
                        }
                        env::pos_t id;
                        std::memcpy(&id, ret.value.data(), sizeof(id));
                        auto&& var_it = env.variables.find(id);
                        if (var_it == env.variables.end()) {
                            throw exceptions::VarNotFound(
                                std::format("Invalid variable pointer {}", id)
                            );
                        }
                        auto& var = var_it->second;
                        if (var.type != env::VarType::UInt32) {
                            throw exceptions::InvalidVarType(
                                std::format("main() must return uint32_t, not (Unknown type) {}", var.type)
                            );
                        }
                        if (var.size != sizeof(std::uint32_t)) {
                            throw exceptions::InvalidVarType(
                                std::format("Size of uint32_t variable must be 4, not {}", var.size)
                            );
                        }
                        std::uint32_t ret_val;
                        std::memcpy(&ret_val, env.memory_pool.get(var.ptr, var.size), sizeof(ret_val));
                        return ret_val;
                    }
                    default: {
                        throw exceptions::InvalidVarType(
                            std::format("Unknown var type {}", static_cast<std::uint32_t>(ret.type))
                        );
                    }
                }
            } catch (const std::exception& e) {
                if (print_caught_exception) { std::println("Exception caught: {}", e.what()); }
                return exceptions::map_exception_to_code(e);
            }
        } else {
            auto ret = func(env, is);
            switch (ret.type) {
                case env::VarType::Char: {
                    if (ret.value.size() == 1) {
                        return ret.value[0];
                    }
                    return ret.value[0];
                }
                case env::VarType::UInt32: {
                    if (ret.value.size() == 4) {
                        env::pos_t value;
                        std::memcpy(ret.value.data(), &value, sizeof(value));
                        return value;
                    }
                }
                default: {
                    throw exceptions::InvalidVarType(std::format("Unknown var type {}", ret.type));
                }
            }
        }
    }

    export inline unsigned char hex_char_to_byte(const char c) {
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wconversion"
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error(std::format("Invalid hex character {}", c));
        #pragma GCC diagnostic pop
    }

    // 内部已做字节序转换和空白字符跳过，外部调用者无须处理
    export [[nodiscard]] inline std::uint32_t hex_to_uint32(std::istream& input) {
        std::uint32_t res = 0;
        while (true) {
            char cur;
            io_utils::get(input, cur);
            if (std::isspace(cur)) {
                break;
            }
            if (cur == '\'') {
                continue;
            }
            res = (res << 4) | hex_char_to_byte(cur);
        }
        if constexpr (std::endian::native == std::endian::big) {
            res = std::byteswap(res);
        }
        return res;
    }

    export inline void compile_hex(std::istream& input, std::ostream& output) {
        char c;
        while (input.get(c)) {           // 逐字符读取，自动处理 EOF
            if (std::isspace(static_cast<unsigned char>(c)))
                continue;               // 跳过空白

            // 读取第一个十六进制数字
            const unsigned char high = hex_char_to_byte(c);

            // 读取第二个十六进制数字
            if (!input.get(c)) {
                throw std::runtime_error("Unexpected EOF: missing second hex digit");
            }
            if (std::isspace(static_cast<unsigned char>(c))) {
                // 不允许空白出现在两个数字之间
                throw std::runtime_error("Unexpected whitespace in hex pair");
            }
            // 不允许分割单个字节
            const unsigned char low = hex_char_to_byte(c);

            unsigned char byte = (high << 4) | low;
            output.write(reinterpret_cast<const char*>(&byte), 1);
        }
    }

    export inline void compile_assembly(std::istream& input, std::ostream& output) {
        char c;
        while (input.get(c)) {
            if (isspace(c)) {
                continue;
            }
            switch (c) {
                case ';': {
                    input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    break;
                }
                case 'a': {
                    char end[2];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'd' && end[1] == 'd') {
                        // TODO
                        output.put(static_cast<char>(Opcode::add));
                        char next;
                        io_utils::get(input, next);
                        if (next == ' ') {
                            env::id_t dest = hex_to_uint32(input);
                            env::id_t src1 = hex_to_uint32(input);
                            env::id_t src2 = hex_to_uint32(input);
                            io_utils::write(output, reinterpret_cast<char*>(&dest), sizeof(dest));
                            io_utils::write(output, reinterpret_cast<char*>(&src1), sizeof(src1));
                            io_utils::write(output, reinterpret_cast<char*>(&src2), sizeof(src2));
                            break;
                        }
                        throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}{}", c, end, next));
                        break;
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}", c, end));
                    break;
                }
                case 'd': {
                    char end[2];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'i' && end[1] == 'v') {
                        output.put(static_cast<char>(Opcode::div));
                        char next;
                        io_utils::get(input, next);
                        if (next == ' ') {
                            // TODO
                        }
                        throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}{}", c, end, next));
                        break;
                    } if (end[0] == 'e' && end[1] == 'l') {
                        output.put(static_cast<char>(Opcode::del));
                        char next;
                        io_utils::get(input, next);
                        if (next == ' ') {
                            // TODO
                            env::id_t var_id = hex_to_uint32(input);
                            io_utils::write(output, reinterpret_cast<char*>(&var_id), sizeof(var_id));
                        }
                        throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}{}", c, end, next));
                        break;
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}", c, end));
                    break;
                }
                case 'm': {
                    char end[2];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'o' && end[1] == 'v') {
                        char next;
                        io_utils::get(input, next);
                        if (next == 'c') {
                            io_utils::get(input, next);
                            if (next == ' ') {
                                output.put(static_cast<char>(Opcode::movc));
                                std::uint32_t val = hex_to_uint32(input);
                                std::uint32_t dest_var = hex_to_uint32(input);
                                io_utils::write(output, reinterpret_cast<char*>(&val), sizeof(val));
                                io_utils::write(output, reinterpret_cast<char*>(&dest_var), sizeof(dest_var));
                                break;
                            }
                            throw exceptions::InvalidOperation(
                                std::format("Invalid assembly movc{}", c, end, next)
                            );
                        } if (next == ' ') {
                            output.put(static_cast<char>(Opcode::mov));
                            std::uint32_t src_var = hex_to_uint32(input);
                            std::uint32_t dest_var = hex_to_uint32(input);
                            io_utils::write(output, reinterpret_cast<char*>(&src_var), sizeof(src_var));
                            io_utils::write(output, reinterpret_cast<char*>(&dest_var), sizeof(dest_var));
                            break;
                        }
                        throw exceptions::InvalidOperation(std::format("Invalid assembly mov{}", next));
                        break;
                    } if (end[0] == 'u' && end[1] == 'l') {
                        output.put(static_cast<char>(Opcode::mul));
                        break;
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}", c, end));
                    break;
                }
                case 'n': {
                    char end[3];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'e' && end[1] == 'w' && end[2] == ' ') {
                        output.put(static_cast<char>(Opcode::new_));
                        env::id_t id = hex_to_uint32(input);
                        uint32_t size = hex_to_uint32(input);
                        io_utils::write(output, reinterpret_cast<char*>(&id), sizeof(id));
                        io_utils::write(output, reinterpret_cast<char*>(&size), sizeof(size));
                        break;
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.3s}", c, end));
                }
                case 'r': {
                    char end[3];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'e' && end[1] == 't') {
                        if (end[2] == 'c') {
                            char next;
                            io_utils::get(input, next);
                            if (next != ' ') {
                                throw exceptions::InvalidOperation(std::format("Invalid assembly retc{}", next));
                            }
                            output.put(static_cast<char>(Opcode::retc));
                            std::uint32_t const_value = hex_to_uint32(input);
                            io_utils::write(output, reinterpret_cast<char*>(&const_value), sizeof(const_value));
                            break;
                        } if (end[2] == 'v') {
                            char next;
                            io_utils::get(input, next);
                            if (next != ' ') {
                                throw exceptions::InvalidOperation(std::format("Invalid assembly retv{}", next));
                            }
                            output.put(static_cast<char>(Opcode::retv));
                            std::uint32_t var = hex_to_uint32(input);
                            io_utils::write(output, reinterpret_cast<char*>(&var), sizeof(var));
                            break;
                        }
                        throw exceptions::InvalidOperation(std::format("Invalid assembly r{:.3s}", end));
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly r{:.3s}", end));
                    break;
                }
                case 's': {
                    char end[2];
                    io_utils::read(input, end, sizeof(end));
                    if (end[0] == 'u' && end[1] == 'b') {
                        output.put(static_cast<char>(Opcode::sub));
                        auto var_l = hex_to_uint32(input);
                        auto var_r = hex_to_uint32(input);
                        io_utils::write(output, reinterpret_cast<char*>(&var_l), sizeof(var_l));
                        io_utils::write(output, reinterpret_cast<char*>(&var_r), sizeof(var_r));
                        break;
                    }
                    throw exceptions::InvalidOperation(std::format("Invalid assembly {}{:.2s}", c, end));
                    break;
                }
                default: {
                    throw exceptions::InvalidOperation(std::format("Invalid assembly leading char {}", c));
                }
            }
        }
    }
}
