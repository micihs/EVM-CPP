#pragma once

#include <evmc/evmc.h>
#include <evmc/utils.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace evm
{
using bytes_view = std::basic_string_view<uint8_t>;

struct EOF1Header
{
    uint16_t code_size = 0;
    uint16_t data_size = 0;
    [[nodiscard]] EVMC_EXPORT size_t code_begin() const noexcept;
};

[[nodiscard]] EVMC_EXPORT bool is_eof_code(bytes_view code) noexcept;
[[nodiscard]] EVMC_EXPORT EOF1Header read_valid_eof1_header(
    bytes_view::const_iterator code) noexcept;

enum class EOFValidationError
{
    success,
    starts_with_format,
    invalid_prefix,
    eof_version_mismatch,
    eof_version_unknown,

    incomplete_section_size,
    code_section_missing,
    multiple_code_sections,
    multiple_data_sections,
    unknown_section_id,
    zero_section_size,
    section_headers_not_terminated,
    invalid_section_bodies_size,
    undefined_instruction,
    missing_terminating_instruction,

    impossible,
};

[[nodiscard]] uint8_t get_eof_version(bytes_view container) noexcept;
[[nodiscard]] EVMC_EXPORT EOFValidationError validate_eof(
    evmc_revision rev, bytes_view container) noexcept;
} 