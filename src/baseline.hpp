#pragma once

#include <evmc/evmc.h>
#include <evmc/utils.h>
#include <memory>
#include <string_view>
#include <vector>

namespace evm
{
using bytes_view = std::basic_string_view<uint8_t>;

class ExecutionState;
class VM;

namespace baseline
{
    class CodeAnalysis
    {
    public:
        using JumpdestMap = std::vector<bool>;
        const uint8_t* executable_code;
        JumpdestMap jumpdest_map;

    private:
        std::unique_ptr<uint8_t[]> m_padded_code;

    public:
        CodeAnalysis(std::unique_ptr<uint8_t[]> padded_code, JumpdestMap map)
        : executable_code{padded_code.get()},
            jumpdest_map{std::move(map)},
            m_padded_code{std::move(padded_code)}
        {}

        CodeAnalysis(const uint8_t* code, JumpdestMap map)
        : executable_code{code}, jumpdest_map{std::move(map)}
        {}
    };
    static_assert(std::is_move_constructible_v<CodeAnalysis>);
    static_assert(std::is_move_assignable_v<CodeAnalysis>);
    static_assert(!std::is_copy_constructible_v<CodeAnalysis>);
    static_assert(!std::is_copy_assignable_v<CodeAnalysis>);
    EVMC_EXPORT CodeAnalysis analyze(evmc_revision rev, bytes_view code);
    evmc_result execute(evmc_vm* vm, const evmc_host_interface* host, evmc_host_context* ctx,
        evmc_revision rev, const evmc_message* msg, const uint8_t* code, size_t code_size) noexcept;
    EVMC_EXPORT evmc_result execute(
        const VM&, ExecutionState& state, const CodeAnalysis& analysis) noexcept;

    }
}