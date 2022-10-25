#pragma once

#include "tracing.hpp"
#include <evmc/evmc.h>

#if defined(_MSC_VER) && !defined(__clang__)
#define EVM_CGOTO_SUPPORTED 0
#else
#define EVM_CGOTO_SUPPORTED 1
#endif

namespace evm
{
class VM : public evmc_vm
{
public:
    bool cgoto = EVM_CGOTO_SUPPORTED;
private:
    std::unique_ptr<Tracer> m_first_tracer;
public:
    inline constexpr VM() noexcept;
    void add_tracer(std::unique_ptr<Tracer> tracer) noexcept
    {
        auto* end = &m_first_tracer;
        while (*end)
            end = &(*end)->m_next_tracer;
        *end = std::move(tracer);
    }
    [[nodiscard]] Tracer* get_tracer() const noexcept { return m_first_tracer.get(); }
};
}