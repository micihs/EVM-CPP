#pragma once

#include "baseline.hpp"
#include "execution_state.hpp"
#include "instructions_traits.hpp"
#include "instructions_xmacro.hpp"
#include <ethash/keccak.hpp>

namespace evm
{
using code_iterator = const uint8_t*;

class StackTop
{
    uint256* m_top;

public:
    StackTop(uint256* top) noexcept : m_top{top} {}
    [[nodiscard]] uint256& operator[](int index) noexcept { return m_top[-index]; }
    [[nodiscard]] uint256& top() noexcept { return *m_top; }
    [[nodiscard]] uint256& pop() noexcept { return *m_top--; }
    void push(const uint256& value) noexcept { *++m_top = value; }
};

struct StopToken
{
    const evmc_status_code status;
};

constexpr auto max_buffer_size = std::numeric_limits<uint32_t>::max();

constexpr auto word_size = 32;

inline constexpr int64_t num_words(uint64_t size_in_bytes) noexcept
{
    return static_cast<int64_t>((size_in_bytes + (word_size - 1)) / word_size);
}

[[gnu::noinline]] inline bool grow_memory(ExecutionState& state, uint64_t new_size) noexcept
{
    const auto new_words = num_words(new_size);
    const auto current_words = static_cast<int64_t>(state.memory.size() / word_size);
    const auto new_cost = 3 * new_words + new_words * new_words / 512;
    const auto current_cost = 3 * current_words + current_words * current_words / 512;
    const auto cost = new_cost - current_cost;

    if ((state.gas_left -= cost) < 0)
        return false;

    state.memory.grow(static_cast<size_t>(new_words * word_size));
    return true;
}

inline bool check_memory(ExecutionState& state, const uint256& offset, uint64_t size) noexcept
{
    if (((offset[3] | offset[2] | offset[1]) != 0) || (offset[0] > max_buffer_size))
        return false;

    const auto new_size = static_cast<uint64_t>(offset) + size;
    if (new_size > state.memory.size())
        return grow_memory(state, new_size);

    return true;
}

inline bool check_memory(ExecutionState& state, const uint256& offset, const uint256& size) noexcept
{
    if (size == 0)
        return true;

    if (((size[3] | size[2] | size[1]) != 0) || (size[0] > max_buffer_size))
        return false;

    return check_memory(state, offset, static_cast<uint64_t>(size));
}

namespace instr::core
{

inline void noop(StackTop /*stack*/) noexcept {}
inline constexpr auto pop = noop;
inline constexpr auto jumpdest = noop;

template <evmc_status_code Status>
inline StopToken stop_impl() noexcept
{
    return {Status};
}
inline constexpr auto stop = stop_impl<EVMC_SUCCESS>;
inline constexpr auto invalid = stop_impl<EVMC_INVALID_INSTRUCTION>;

inline void add(StackTop stack) noexcept
{
    stack.top() += stack.pop();
}

inline void mul(StackTop stack) noexcept
{
    stack.top() *= stack.pop();
}

inline void sub(StackTop stack) noexcept
{
    stack[1] = stack[0] - stack[1];
}

inline void div(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? stack[0] / v : 0;
}

inline void sdiv(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? intx::sdivrem(stack[0], v).quot : 0;
}

inline void mod(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? stack[0] % v : 0;
}

inline void smod(StackTop stack) noexcept
{
    auto& v = stack[1];
    v = v != 0 ? intx::sdivrem(stack[0], v).rem : 0;
}

inline void addmod(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    const auto& y = stack.pop();
    auto& m = stack.top();
    m = m != 0 ? intx::addmod(x, y, m) : 0;
}

inline void mulmod(StackTop stack) noexcept
{
    const auto& x = stack[0];
    const auto& y = stack[1];
    auto& m = stack[2];
    m = m != 0 ? intx::mulmod(x, y, m) : 0;
}

inline evmc_status_code exp(StackTop stack, ExecutionState& state) noexcept
{
    const auto& base = stack.pop();
    auto& exponent = stack.top();

    const auto exponent_significant_bytes =
        static_cast<int>(intx::count_significant_bytes(exponent));
    const auto exponent_cost = state.rev >= EVMC_SPURIOUS_DRAGON ? 50 : 10;
    const auto additional_cost = exponent_significant_bytes * exponent_cost;
    if ((state.gas_left -= additional_cost) < 0)
        return EVMC_OUT_OF_GAS;

    exponent = intx::exp(base, exponent);
    return EVMC_SUCCESS;
}

inline void signextend(StackTop stack) noexcept
{
    const auto& ext = stack.pop();
    auto& x = stack.top();

    if (ext < 31)
    {
        const auto e = ext[0];
        const auto sign_word_index =
            static_cast<size_t>(e / sizeof(e));
        const auto sign_byte_index = e % sizeof(e);
        auto& sign_word = x[sign_word_index];

        const auto sign_byte_offset = sign_byte_index * 8;
        const auto sign_byte = sign_word >> sign_byte_offset;
        const auto sext_byte = static_cast<uint64_t>(int64_t{static_cast<int8_t>(sign_byte)});
        const auto sext = sext_byte << sign_byte_offset;

        const auto sign_mask = ~uint64_t{0} << sign_byte_offset;
        const auto value = sign_word & ~sign_mask;
        sign_word = sext | value;
        const auto sign_ex = static_cast<uint64_t>(static_cast<int64_t>(sext_byte) >> 8);

        for (size_t i = 3; i > sign_word_index; --i)
            x[i] = sign_ex;
    }
}

inline void lt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = x < stack[0];
}

inline void gt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = stack[0] < x;
}

inline void slt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = slt(x, stack[0]);
}

inline void sgt(StackTop stack) noexcept
{
    const auto& x = stack.pop();
    stack[0] = slt(stack[0], x);
}

inline void eq(StackTop stack) noexcept
{
    stack[1] = stack[0] == stack[1];
}

inline void iszero(StackTop stack) noexcept
{
    stack.top() = stack.top() == 0;
}

inline void and_(StackTop stack) noexcept
{
    stack.top() &= stack.pop();
}

inline void or_(StackTop stack) noexcept
{
    stack.top() |= stack.pop();
}

inline void xor_(StackTop stack) noexcept
{
    stack.top() ^= stack.pop();
}

inline void not_(StackTop stack) noexcept
{
    stack.top() = ~stack.top();
}

inline void byte(StackTop stack) noexcept
{
    const auto& n = stack.pop();
    auto& x = stack.top();

    const bool n_valid = n < 32;
    const uint64_t byte_mask = (n_valid ? 0xff : 0);

    const auto index = 31 - static_cast<unsigned>(n[0] % 32);
    const auto word = x[index / 8];
    const auto byte_index = index % 8;
    const auto byte = (word >> (byte_index * 8)) & byte_mask;
    x = byte;
}

inline void shl(StackTop stack) noexcept
{
    stack.top() <<= stack.pop();
}

inline void shr(StackTop stack) noexcept
{
    stack.top() >>= stack.pop();
}

inline void sar(StackTop stack) noexcept
{
    const auto& y = stack.pop();
    auto& x = stack.top();

    const bool is_neg = static_cast<int64_t>(x[3]) < 0;
    const auto sign_mask = is_neg ? ~uint256{} : uint256{};

    const auto mask_shift = (y < 256) ? (256 - y[0]) : 0;
    x = (x >> y) | (sign_mask << mask_shift);
}

inline evmc_status_code keccak256(StackTop stack, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    auto& size = stack.top();

    if (!check_memory(state, index, size))
        return EVMC_OUT_OF_GAS;

    const auto i = static_cast<size_t>(index);
    const auto s = static_cast<size_t>(size);
    const auto w = num_words(s);
    const auto cost = w * 6;
    if ((state.gas_left -= cost) < 0)
        return EVMC_OUT_OF_GAS;

    auto data = s != 0 ? &state.memory[i] : nullptr;
    size = intx::be::load<uint256>(ethash::keccak256(data, s));
    return EVMC_SUCCESS;
}


inline void address(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.msg->recipient));
}

inline evmc_status_code balance(StackTop stack, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = intx::be::trunc<evmc::address>(x);

    if (state.rev >= EVMC_BERLIN && state.host.access_account(addr) == EVMC_ACCESS_COLD)
    {
        if ((state.gas_left -= instr::additional_cold_account_access_cost) < 0)
            return EVMC_OUT_OF_GAS;
    }

    x = intx::be::load<uint256>(state.host.get_balance(addr));
    return EVMC_SUCCESS;
}

inline void origin(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().tx_origin));
}

inline void caller(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.msg->sender));
}

inline void callvalue(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.msg->value));
}

inline void calldataload(StackTop stack, ExecutionState& state) noexcept
{
    auto& index = stack.top();

    if (state.msg->input_size < index)
        index = 0;
    else
    {
        const auto begin = static_cast<size_t>(index);
        const auto end = std::min(begin + 32, state.msg->input_size);

        uint8_t data[32] = {};
        for (size_t i = 0; i < (end - begin); ++i)
            data[i] = state.msg->input_data[begin + i];

        index = intx::be::load<uint256>(data);
    }
}

inline void calldatasize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.msg->input_size);
}

inline evmc_status_code calldatacopy(StackTop stack, ExecutionState& state) noexcept
{
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(state, mem_index, size))
        return EVMC_OUT_OF_GAS;

    auto dst = static_cast<size_t>(mem_index);
    auto src = state.msg->input_size < input_index ? state.msg->input_size :
                                                     static_cast<size_t>(input_index);
    auto s = static_cast<size_t>(size);
    auto copy_size = std::min(s, state.msg->input_size - src);

    const auto copy_cost = num_words(s) * 3;
    if ((state.gas_left -= copy_cost) < 0)
        return EVMC_OUT_OF_GAS;

    if (copy_size > 0)
        std::memcpy(&state.memory[dst], &state.msg->input_data[src], copy_size);

    if (s - copy_size > 0)
        std::memset(&state.memory[dst + copy_size], 0, s - copy_size);

    return EVMC_SUCCESS;
}

inline void codesize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.original_code.size());
}

inline evmc_status_code codecopy(StackTop stack, ExecutionState& state) noexcept
{
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(state, mem_index, size))
        return EVMC_OUT_OF_GAS;

    const auto code_size = state.original_code.size();
    const auto dst = static_cast<size_t>(mem_index);
    const auto src = code_size < input_index ? code_size : static_cast<size_t>(input_index);
    const auto s = static_cast<size_t>(size);
    const auto copy_size = std::min(s, code_size - src);

    const auto copy_cost = num_words(s) * 3;
    if ((state.gas_left -= copy_cost) < 0)
        return EVMC_OUT_OF_GAS;
    if (copy_size > 0)
        std::memcpy(&state.memory[dst], &state.original_code[src], copy_size);

    if (s - copy_size > 0)
        std::memset(&state.memory[dst + copy_size], 0, s - copy_size);

    return EVMC_SUCCESS;
}


inline void gasprice(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().tx_gas_price));
}

inline void basefee(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().block_base_fee));
}

inline evmc_status_code extcodesize(StackTop stack, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = intx::be::trunc<evmc::address>(x);

    if (state.rev >= EVMC_BERLIN && state.host.access_account(addr) == EVMC_ACCESS_COLD)
    {
        if ((state.gas_left -= instr::additional_cold_account_access_cost) < 0)
            return EVMC_OUT_OF_GAS;
    }

    x = state.host.get_code_size(addr);
    return EVMC_SUCCESS;
}

inline evmc_status_code extcodecopy(StackTop stack, ExecutionState& state) noexcept
{
    const auto addr = intx::be::trunc<evmc::address>(stack.pop());
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(state, mem_index, size))
        return EVMC_OUT_OF_GAS;

    const auto s = static_cast<size_t>(size);
    const auto copy_cost = num_words(s) * 3;
    if ((state.gas_left -= copy_cost) < 0)
        return EVMC_OUT_OF_GAS;

    if (state.rev >= EVMC_BERLIN && state.host.access_account(addr) == EVMC_ACCESS_COLD)
    {
        if ((state.gas_left -= instr::additional_cold_account_access_cost) < 0)
            return EVMC_OUT_OF_GAS;
    }

    if (s > 0)
    {
        const auto src =
            (max_buffer_size < input_index) ? max_buffer_size : static_cast<size_t>(input_index);
        const auto dst = static_cast<size_t>(mem_index);
        const auto num_bytes_copied = state.host.copy_code(addr, src, &state.memory[dst], s);
        if (const auto num_bytes_to_clear = s - num_bytes_copied; num_bytes_to_clear > 0)
            std::memset(&state.memory[dst + num_bytes_copied], 0, num_bytes_to_clear);
    }

    return EVMC_SUCCESS;
}

inline void returndatasize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.return_data.size());
}

inline evmc_status_code returndatacopy(StackTop stack, ExecutionState& state) noexcept
{
    const auto& mem_index = stack.pop();
    const auto& input_index = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(state, mem_index, size))
        return EVMC_OUT_OF_GAS;

    auto dst = static_cast<size_t>(mem_index);
    auto s = static_cast<size_t>(size);

    if (state.return_data.size() < input_index)
        return EVMC_INVALID_MEMORY_ACCESS;
    auto src = static_cast<size_t>(input_index);

    if (src + s > state.return_data.size())
        return EVMC_INVALID_MEMORY_ACCESS;

    const auto copy_cost = num_words(s) * 3;
    if ((state.gas_left -= copy_cost) < 0)
        return EVMC_OUT_OF_GAS;

    if (s > 0)
        std::memcpy(&state.memory[dst], &state.return_data[src], s);

    return EVMC_SUCCESS;
}

inline evmc_status_code extcodehash(StackTop stack, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto addr = intx::be::trunc<evmc::address>(x);

    if (state.rev >= EVMC_BERLIN && state.host.access_account(addr) == EVMC_ACCESS_COLD)
    {
        if ((state.gas_left -= instr::additional_cold_account_access_cost) < 0)
            return EVMC_OUT_OF_GAS;
    }

    x = intx::be::load<uint256>(state.host.get_code_hash(addr));
    return EVMC_SUCCESS;
}


inline void blockhash(StackTop stack, ExecutionState& state) noexcept
{
    auto& number = stack.top();

    const auto upper_bound = state.get_tx_context().block_number;
    const auto lower_bound = std::max(upper_bound - 256, decltype(upper_bound){0});
    const auto n = static_cast<int64_t>(number);
    const auto header =
        (number < upper_bound && n >= lower_bound) ? state.host.get_block_hash(n) : evmc::bytes32{};
    number = intx::be::load<uint256>(header);
}

inline void coinbase(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().block_coinbase));
}

inline void timestamp(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_timestamp));
}

inline void number(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_number));
}

inline void prevrandao(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().block_prev_randao));
}

inline void gaslimit(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(static_cast<uint64_t>(state.get_tx_context().block_gas_limit));
}

inline void chainid(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.get_tx_context().chain_id));
}

inline void selfbalance(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(intx::be::load<uint256>(state.host.get_balance(state.msg->recipient)));
}

inline evmc_status_code mload(StackTop stack, ExecutionState& state) noexcept
{
    auto& index = stack.top();

    if (!check_memory(state, index, 32))
        return EVMC_OUT_OF_GAS;

    index = intx::be::unsafe::load<uint256>(&state.memory[static_cast<size_t>(index)]);
    return EVMC_SUCCESS;
}

inline evmc_status_code mstore(StackTop stack, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    const auto& value = stack.pop();

    if (!check_memory(state, index, 32))
        return EVMC_OUT_OF_GAS;

    intx::be::unsafe::store(&state.memory[static_cast<size_t>(index)], value);
    return EVMC_SUCCESS;
}

inline evmc_status_code mstore8(StackTop stack, ExecutionState& state) noexcept
{
    const auto& index = stack.pop();
    const auto& value = stack.pop();

    if (!check_memory(state, index, 1))
        return EVMC_OUT_OF_GAS;

    state.memory[static_cast<size_t>(index)] = static_cast<uint8_t>(value);
    return EVMC_SUCCESS;
}

evmc_status_code sload(StackTop stack, ExecutionState& state) noexcept;

evmc_status_code sstore(StackTop stack, ExecutionState& state) noexcept;

inline code_iterator jump_impl(ExecutionState& state, const uint256& dst) noexcept
{
    const auto& jumpdest_map = state.analysis.baseline->jumpdest_map;
    if (dst >= jumpdest_map.size() || !jumpdest_map[static_cast<size_t>(dst)])
    {
        state.status = EVMC_BAD_JUMP_DESTINATION;
        return nullptr;
    }

    return state.analysis.baseline->executable_code + static_cast<size_t>(dst);
}

inline code_iterator jump(StackTop stack, ExecutionState& state, code_iterator /*pos*/) noexcept
{
    return jump_impl(state, stack.pop());
}

inline code_iterator jumpi(StackTop stack, ExecutionState& state, code_iterator pos) noexcept
{
    const auto& dst = stack.pop();
    const auto& cond = stack.pop();
    return cond ? jump_impl(state, dst) : pos + 1;
}

inline code_iterator pc(StackTop stack, ExecutionState& state, code_iterator pos) noexcept
{
    stack.push(static_cast<uint64_t>(pos - state.analysis.baseline->executable_code));
    return pos + 1;
}

inline void msize(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.memory.size());
}

inline void gas(StackTop stack, ExecutionState& state) noexcept
{
    stack.push(state.gas_left);
}

inline void push0(StackTop stack) noexcept
{
    stack.push({});
}


template <size_t Len>
inline uint64_t load_partial_push_data(code_iterator pos) noexcept
{
    static_assert(Len > 4 && Len < 8);
    return intx::be::unsafe::load<uint64_t>(pos) >> (8 * (sizeof(uint64_t) - Len));
}

template <>
inline uint64_t load_partial_push_data<1>(code_iterator pos) noexcept
{
    return pos[0];
}

template <>
inline uint64_t load_partial_push_data<2>(code_iterator pos) noexcept
{
    return intx::be::unsafe::load<uint16_t>(pos);
}

template <>
inline uint64_t load_partial_push_data<3>(code_iterator pos) noexcept
{
    return intx::be::unsafe::load<uint32_t>(pos) >> 8;
}

template <>
inline uint64_t load_partial_push_data<4>(code_iterator pos) noexcept
{
    return intx::be::unsafe::load<uint32_t>(pos);
}

template <size_t Len>
inline code_iterator push(StackTop stack, ExecutionState& /*state*/, code_iterator pos) noexcept
{
    constexpr auto num_full_words = Len / sizeof(uint64_t);
    constexpr auto num_partial_bytes = Len % sizeof(uint64_t);
    auto data = pos + 1;
    uint256 r;

    if constexpr (num_partial_bytes != 0)
    {
        r[num_full_words] = load_partial_push_data<num_partial_bytes>(data);
        data += num_partial_bytes;
    }

    for (size_t i = 0; i < num_full_words; ++i)
    {
        r[num_full_words - 1 - i] = intx::be::unsafe::load<uint64_t>(data);
        data += sizeof(uint64_t);
    }

    stack.push(r);
    return pos + (Len + 1);
}

template <int N>
inline void dup(StackTop stack) noexcept
{
    static_assert(N >= 1 && N <= 16);
    stack.push(stack[N - 1]);
}

template <int N>
inline void swap(StackTop stack) noexcept
{
    static_assert(N >= 1 && N <= 16);
    std::swap(stack.top(), stack[N]);
}

template <size_t NumTopics>
inline evmc_status_code log(StackTop stack, ExecutionState& state) noexcept
{
    static_assert(NumTopics <= 4);

    if (state.in_static_mode())
        return EVMC_STATIC_MODE_VIOLATION;

    const auto& offset = stack.pop();
    const auto& size = stack.pop();

    if (!check_memory(state, offset, size))
        return EVMC_OUT_OF_GAS;

    const auto o = static_cast<size_t>(offset);
    const auto s = static_cast<size_t>(size);

    const auto cost = int64_t(s) * 8;
    if ((state.gas_left -= cost) < 0)
        return EVMC_OUT_OF_GAS;

    std::array<evmc::bytes32, NumTopics> topics;
    for (auto& topic : topics)
        topic = intx::be::store<evmc::bytes32>(stack.pop());

    const auto data = s != 0 ? &state.memory[o] : nullptr;
    state.host.emit_log(state.msg->recipient, data, s, topics.data(), NumTopics);
    return EVMC_SUCCESS;
}

template <evmc_opcode Op>
evmc_status_code call_impl(StackTop stack, ExecutionState& state) noexcept;
inline constexpr auto call = call_impl<OP_CALL>;
inline constexpr auto callcode = call_impl<OP_CALLCODE>;
inline constexpr auto delegatecall = call_impl<OP_DELEGATECALL>;
inline constexpr auto staticcall = call_impl<OP_STATICCALL>;

template <evmc_opcode Op>
evmc_status_code create_impl(StackTop stack, ExecutionState& state) noexcept;
inline constexpr auto create = create_impl<OP_CREATE>;
inline constexpr auto create2 = create_impl<OP_CREATE2>;

template <evmc_status_code StatusCode>
inline StopToken return_impl(StackTop stack, ExecutionState& state) noexcept
{
    const auto& offset = stack[0];
    const auto& size = stack[1];

    if (!check_memory(state, offset, size))
        return {EVMC_OUT_OF_GAS};

    state.output_size = static_cast<size_t>(size);
    if (state.output_size != 0)
        state.output_offset = static_cast<size_t>(offset);
    return {StatusCode};
}
inline constexpr auto return_ = return_impl<EVMC_SUCCESS>;
inline constexpr auto revert = return_impl<EVMC_REVERT>;

inline StopToken selfdestruct(StackTop stack, ExecutionState& state) noexcept
{
    if (state.in_static_mode())
        return {EVMC_STATIC_MODE_VIOLATION};

    const auto beneficiary = intx::be::trunc<evmc::address>(stack[0]);

    if (state.rev >= EVMC_BERLIN && state.host.access_account(beneficiary) == EVMC_ACCESS_COLD)
    {
        if ((state.gas_left -= instr::cold_account_access_cost) < 0)
            return {EVMC_OUT_OF_GAS};
    }

    if (state.rev >= EVMC_TANGERINE_WHISTLE)
    {
        if (state.rev == EVMC_TANGERINE_WHISTLE || state.host.get_balance(state.msg->recipient))
        {
            if (!state.host.account_exists(beneficiary))
            {
                if ((state.gas_left -= 25000) < 0)
                    return {EVMC_OUT_OF_GAS};
            }
        }
    }

    if (state.host.selfdestruct(state.msg->recipient, beneficiary))
    {
        if (state.rev < EVMC_LONDON)
            state.gas_refund += 24000;
    }
    return {EVMC_SUCCESS};
}

template <evmc_opcode Op>
inline constexpr auto impl = nullptr;

#undef ON_OPCODE_IDENTIFIER
#define ON_OPCODE_IDENTIFIER(OPCODE, IDENTIFIER) \
    template <>                                  \
    inline constexpr auto impl<OPCODE> = IDENTIFIER;
MAP_OPCODES
#undef ON_OPCODE_IDENTIFIER
#define ON_OPCODE_IDENTIFIER ON_OPCODE_IDENTIFIER_DEFAULT
}  // namespace instr::core
}