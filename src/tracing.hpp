#pragma once

#include <evmc/instructions.h>
#include <intx/intx.hpp>
#include <memory>
#include <ostream>
#include <string_view>

namespace evm
{
using bytes_view = std::basic_string_view<uint8_t>;

class ExecutionState;

class Tracer
{
    friend class VM;
    std::unique_ptr<Tracer> m_next_tracer;
public:
    virtual ~Tracer() = default;

    void notify_execution_start(
        evmc_revision rev, const evmc_message& msg, bytes_view code) noexcept
    {
        on_execution_start(rev, msg, code);
        if (m_next_tracer)
            m_next_tracer->notify_execution_start(rev, msg, code);
    }

    void notify_execution_end(const evmc_result& result) noexcept
    {
        on_execution_end(result);
        if (m_next_tracer)
            m_next_tracer->notify_execution_end(result);
    }

    void notify_instruction_start(
        uint32_t pc, intx::uint256* stack_top, int stack_height,
        const ExecutionState& state) noexcept
    {
        on_instruction_start(pc, stack_top, stack_height, state);
        if (m_next_tracer)
            m_next_tracer->notify_instruction_start(pc, stack_top, stack_height, state);
    }

private:
    virtual void on_execution_start(
        evmc_revision rev, const evmc_message& msg, bytes_view code) noexcept = 0;
    virtual void on_instruction_start(uint32_t pc, const intx::uint256* stack_top, int stack_height,
        const ExecutionState& state) noexcept = 0;
    virtual void on_execution_end(const evmc_result& result) noexcept = 0;
};
EVMC_EXPORT std::unique_ptr<Tracer> create_histogram_tracer(std::ostream& out);
EVMC_EXPORT std::unique_ptr<Tracer> create_instruction_tracer(std::ostream& out);
}