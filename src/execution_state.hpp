#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <string>
#include <vector>

namespace evm
{
namespace advanced
{
struct AdvancedCodeAnalysis;
}
namespace baseline
{
class CodeAnalysis;
}

using uint256 = intx::uint256;
using bytes = std::basic_string<uint8_t>;
using bytes_view = std::basic_string_view<uint8_t>;

class StackSpace
{
public:
    static constexpr auto limit = 1024;
    [[nodiscard, clang::no_sanitize("bounds")]] uint256* bottom() noexcept
    {
        return m_stack_space - 1;
    }

private:
    alignas(sizeof(uint256)) uint256 m_stack_space[limit];
};

class Memory
{
    static constexpr size_t page_size = 4 * 1024;
    uint8_t* m_data = nullptr;
    size_t m_size = 0;
    size_t m_capacity = page_size;

    [[noreturn, gnu::cold]] static void handle_out_of_memory() noexcept { std::terminate(); }

    void allocate_capacity() noexcept
    {
        m_data = static_cast<uint8_t*>(std::realloc(m_data, m_capacity));
        if (m_data == nullptr)
            handle_out_of_memory();
    }

public:
    Memory() noexcept { allocate_capacity(); }
    ~Memory() noexcept { std::free(m_data); }

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    uint8_t& operator[](size_t index) noexcept { return m_data[index]; }

    [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
    [[nodiscard]] size_t size() const noexcept { return m_size; }

    void grow(size_t new_size) noexcept
    {
        assert(new_size % 32 == 0);
        assert(new_size > m_size);
        if (new_size <= m_size)
            INTX_UNREACHABLE();

        if (new_size > m_capacity)
        {
            m_capacity *= 2;

            if (m_capacity < new_size)
            {
                m_capacity = ((new_size + (page_size - 1)) / page_size) * page_size;
            }

            allocate_capacity();
        }
        std::memset(m_data + m_size, 0, new_size - m_size);
        m_size = new_size;
    }
    void clear() noexcept { m_size = 0; }
};

class ExecutionState
{
public:
    int64_t gas_left = 0;
    int64_t gas_refund = 0;
    Memory memory;
    const evmc_message* msg = nullptr;
    evmc::HostContext host;
    evmc_revision rev = {};
    bytes return_data;
    bytes_view original_code;

    evmc_status_code status = EVMC_SUCCESS;
    size_t output_offset = 0;
    size_t output_size = 0;

private:
    evmc_tx_context m_tx = {};

public:

    union
    {
        const baseline::CodeAnalysis* baseline = nullptr;
        const advanced::AdvancedCodeAnalysis* advanced;
    } analysis{};

    StackSpace stack_space;

    ExecutionState() noexcept = default;

    ExecutionState(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code) noexcept
      : gas_left{message.gas},
        msg{&message},
        host{host_interface, host_ctx},
        rev{revision},
        original_code{_code}
    {}

    void reset(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code) noexcept
    {
        gas_left = message.gas;
        gas_refund = 0;
        memory.clear();
        msg = &message;
        host = {host_interface, host_ctx};
        rev = revision;
        return_data.clear();
        original_code = _code;
        status = EVMC_SUCCESS;
        output_offset = 0;
        output_size = 0;
        m_tx = {};
    }

    [[nodiscard]] bool in_static_mode() const { return (msg->flags & EVMC_STATIC) != 0; }

    const evmc_tx_context& get_tx_context() noexcept
    {
        if (INTX_UNLIKELY(m_tx.block_timestamp == 0))
            m_tx = host.get_tx_context();
        return m_tx;
    }
};
}  // namespace evm