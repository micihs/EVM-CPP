#include "advanced_analysis.hpp"
#include "opcodes_helpers.h"
#include <cassert>

namespace evm::advanced
{

template <typename To, typename T>
inline constexpr To clamp(T x) noexcept
{
    constexpr auto max = std::numeric_limits<To>::max();
    return x <= max ? static_cast<To>(x) : max;
}

struct BlockAnalysis
{
    int64_t gas_cost = 0;
    int stack_req = 0;
    int stack_max_growth = 0;
    int stack_change = 0;
    size_t begin_block_index = 0;
    explicit BlockAnalysis(size_t index) noexcept : begin_block_index{index} {}
    [[nodiscard]] BlockInfo close() const noexcept
    {
        return {clamp<decltype(BlockInfo{}.gas_cost)>(gas_cost),
            clamp<decltype(BlockInfo{}.stack_req)>(stack_req),
            clamp<decltype(BlockInfo{}.stack_max_growth)>(stack_max_growth)};
    }
};

AdvancedCodeAnalysis analyze(evmc_revision rev, bytes_view code) noexcept
{
    const auto& op_tbl = get_op_table(rev);
    const auto opx_beginblock_fn = op_tbl[OPX_BEGINBLOCK].fn;
    AdvancedCodeAnalysis analysis;
    const auto max_instrs_size = code.size() + 2;
    analysis.instrs.reserve(max_instrs_size);
    const auto max_args_storage_size = code.size() + 1;
    analysis.push_values.reserve(max_args_storage_size);

    analysis.instrs.emplace_back(opx_beginblock_fn);
    auto block = BlockAnalysis{0};
    const auto code_begin = code.data();
    const auto code_end = code_begin + code.size();
    auto code_pos = code_begin;
    while (code_pos != code_end)
    {
        const auto opcode = *code_pos++;
        const auto& opcode_info = op_tbl[opcode];

        if (opcode == OP_JUMPDEST)
        {
            analysis.instrs[block.begin_block_index].arg.block = block.close();
            block = BlockAnalysis{analysis.instrs.size()};

            analysis.jumpdest_offsets.emplace_back(static_cast<int32_t>(code_pos - code_begin - 1));
            analysis.jumpdest_targets.emplace_back(static_cast<int32_t>(analysis.instrs.size()));
        }

        analysis.instrs.emplace_back(opcode_info.fn);

        block.stack_req = std::max(block.stack_req, opcode_info.stack_req - block.stack_change);
        block.stack_change += opcode_info.stack_change;
        block.stack_max_growth = std::max(block.stack_max_growth, block.stack_change);

        block.gas_cost += opcode_info.gas_cost;

        auto& instr = analysis.instrs.back();

        switch (opcode)
        {
        default:
            break;

        case OP_JUMP:
        case OP_STOP:
        case OP_RETURN:
        case OP_REVERT:
        case OP_SELFDESTRUCT:
            while (code_pos != code_end && *code_pos != OP_JUMPDEST)
            {
                if (*code_pos >= OP_PUSH1 && *code_pos <= OP_PUSH32)
                {
                    const auto push_size = static_cast<size_t>(*code_pos - OP_PUSH1) + 1;
                    code_pos = std::min(code_pos + push_size + 1, code_end);
                }
                else
                    ++code_pos;
            }
            break;

        case OP_JUMPI:

            analysis.instrs[block.begin_block_index].arg.block = block.close();
            block = BlockAnalysis{analysis.instrs.size() - 1};
            break;

        case ANY_SMALL_PUSH:
        {
            const auto push_size = static_cast<size_t>(opcode - OP_PUSH1) + 1;
            const auto push_end = std::min(code_pos + push_size, code_end);

            uint64_t value = 0;
            auto insert_bit_pos = (push_size - 1) * 8;
            while (code_pos < push_end)
            {
                value |= uint64_t{*code_pos++} << insert_bit_pos;
                insert_bit_pos -= 8;
            }
            instr.arg.small_push_value = value;
            break;
        }

        case ANY_LARGE_PUSH:
        {
            const auto push_size = static_cast<size_t>(opcode - OP_PUSH1) + 1;
            const auto push_end = code_pos + push_size;
            auto& push_value = analysis.push_values.emplace_back();
            const auto push_value_bytes = intx::as_bytes(push_value);
            auto insert_pos = &push_value_bytes[push_size - 1];
            while (code_pos < push_end && code_pos < code_end)
                *insert_pos-- = *code_pos++;

            instr.arg.push_value = &push_value;
            break;
        }

        case OP_GAS:
        case OP_CALL:
        case OP_CALLCODE:
        case OP_DELEGATECALL:
        case OP_STATICCALL:
        case OP_CREATE:
        case OP_CREATE2:
        case OP_SSTORE:
            instr.arg.number = block.gas_cost;
            break;

        case OP_PC:
            instr.arg.number = code_pos - code_begin - 1;
            break;
        }
    }

    analysis.instrs[block.begin_block_index].arg.block = block.close();
    analysis.instrs.emplace_back(op_tbl[OP_STOP].fn);
    assert(analysis.instrs.size() <= max_instrs_size);
    assert(analysis.push_values.size() <= max_args_storage_size);

    return analysis;
}
}