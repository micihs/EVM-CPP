#pragma once

#include "execution_state.hpp"
#include <evmc/evmc.hpp>
#include <evmc/instructions.h>
#include <evmc/utils.h>
#include <intx/intx.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace evm::advanced
{
struct Instruction;

struct BlockInfo
{
    uint32_t gas_cost = 0;
    int16_t stack_req = 0;
    int16_t stack_max_growth = 0;
};
static_assert(sizeof(BlockInfo) == 8);

class Stack
{
public:
    uint256* top_item = nullptr;

private:
    uint256* m_bottom = nullptr;

public:
    explicit Stack(uint256* stack_space_bottom) noexcept { reset(stack_space_bottom); }
    [[nodiscard]] int size() const noexcept { return static_cast<int>(top_item - m_bottom); }
    [[nodiscard]] uint256& top() noexcept { return *top_item; }
    [[nodiscard]] uint256& operator[](int index) noexcept { return *(top_item - index); }
    [[nodiscard]] const uint256& operator[](int index) const noexcept
    {
        return *(top_item - index);
    }
    void push(const uint256& item) noexcept { *++top_item = item; }
    uint256 pop() noexcept { return *top_item--; }
    void reset(uint256* stack_space_bottom) noexcept
    {
        m_bottom = stack_space_bottom;
        top_item = m_bottom;
    }
};

struct AdvancedExecutionState : ExecutionState
{
    Stack stack;
    uint32_t current_block_cost = 0;

    AdvancedExecutionState() noexcept : stack{stack_space.bottom()} {}

    AdvancedExecutionState(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code) noexcept
      : ExecutionState{message, revision, host_interface, host_ctx, _code},
        stack{stack_space.bottom()}
    {}

    const Instruction* exit(evmc_status_code status_code) noexcept
    {
        status = status_code;
        return nullptr;
    }

    void reset(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code) noexcept
    {
        ExecutionState::reset(message, revision, host_interface, host_ctx, _code);
        stack.reset(stack_space.bottom());
        analysis.advanced = nullptr;
        current_block_cost = 0;
    }
};

union InstructionArgument
{
    int64_t number;
    const intx::uint256* push_value;
    uint64_t small_push_value;
    BlockInfo block{};
};
static_assert(
    sizeof(InstructionArgument) == sizeof(uint64_t), "Incorrect size of instruction_argument");

using instruction_exec_fn = const Instruction* (*)(const Instruction*, AdvancedExecutionState&);

enum intrinsic_opcodes
{
    OPX_BEGINBLOCK = OP_JUMPDEST
};

struct OpTableEntry
{
    instruction_exec_fn fn;
    int16_t gas_cost;
    int8_t stack_req;
    int8_t stack_change;
};

using OpTable = std::array<OpTableEntry, 256>;

struct Instruction
{
    instruction_exec_fn fn = nullptr;
    InstructionArgument arg;

    explicit constexpr Instruction(instruction_exec_fn f) noexcept : fn{f}, arg{} {}
};

struct AdvancedCodeAnalysis
{
    std::vector<Instruction> instrs;
    std::vector<intx::uint256> push_values;
    std::vector<int32_t> jumpdest_offsets;
    std::vector<int32_t> jumpdest_targets;
};

inline int find_jumpdest(const AdvancedCodeAnalysis& analysis, int offset) noexcept
{
    const auto begin = std::begin(analysis.jumpdest_offsets);
    const auto end = std::end(analysis.jumpdest_offsets);
    const auto it = std::lower_bound(begin, end, offset);
    return (it != end && *it == offset) ?
               analysis.jumpdest_targets[static_cast<size_t>(it - begin)] :
               -1;
}
EVMC_EXPORT AdvancedCodeAnalysis analyze(evmc_revision rev, bytes_view code) noexcept;
EVMC_EXPORT const OpTable& get_op_table(evmc_revision rev) noexcept;

}