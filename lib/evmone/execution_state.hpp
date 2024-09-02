// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <string>
#include <vector>
#include <optional>

#include "instructions_traits.hpp"

namespace evmone
{
struct StorageStoreCost
{
    int64_t gas_cost;
    int64_t gas_refund;
};

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


/// Provides memory for EVM stack.
class StackSpace
{
public:
    /// The maximum number of EVM stack items.
    static constexpr auto limit = 1024;

    /// Returns the pointer to the "bottom", i.e. below the stack space.
    [[nodiscard, clang::no_sanitize("bounds")]] uint256* bottom() noexcept
    {
        return m_stack_space - 1;
    }

private:
    /// The storage allocated for maximum possible number of items.
    /// Items are aligned to 256 bits for better packing in cache lines.
    alignas(sizeof(uint256)) uint256 m_stack_space[limit];
};


/// The EVM memory.
///
/// The implementations uses initial allocation of 4k and then grows capacity with 2x factor.
/// Some benchmarks has been done to confirm 4k is ok-ish value.
class Memory
{
    /// The size of allocation "page".
    static constexpr size_t page_size = 4 * 1024;

    /// Pointer to allocated memory.
    uint8_t* m_data = nullptr;

    /// The "virtual" size of the memory.
    size_t m_size = 0;

    /// The size of allocated memory. The initialization value is the initial capacity.
    size_t m_capacity = page_size;

    [[noreturn, gnu::cold]] static void handle_out_of_memory() noexcept { std::terminate(); }

    void allocate_capacity() noexcept
    {
        m_data = static_cast<uint8_t*>(std::realloc(m_data, m_capacity));
        if (m_data == nullptr)
            handle_out_of_memory();
    }

public:
    /// Creates Memory object with initial capacity allocation.
    Memory() noexcept { allocate_capacity(); }

    /// Frees all allocated memory.
    ~Memory() noexcept { std::free(m_data); }

    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    uint8_t& operator[](size_t index) noexcept { return m_data[index]; }

    [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }
    [[nodiscard]] size_t size() const noexcept { return m_size; }

    /// Grows the memory to the given size. The extend is filled with zeros.
    ///
    /// @param new_size  New memory size. Must be larger than the current size and multiple of 32.
    void grow(size_t new_size) noexcept
    {
        // Restriction for future changes. EVM always has memory size as multiple of 32 bytes.
        INTX_REQUIRE(new_size % 32 == 0);

        // Allow only growing memory. Include hint for optimizing compiler.
        INTX_REQUIRE(new_size > m_size);

        if (new_size > m_capacity)
        {
            m_capacity *= 2;  // Double the capacity.

            if (m_capacity < new_size)  // If not enough.
            {
                // Set capacity to required size rounded to multiple of page_size.
                m_capacity = ((new_size + (page_size - 1)) / page_size) * page_size;
            }

            allocate_capacity();
        }
        std::memset(m_data + m_size, 0, new_size - m_size);
        m_size = new_size;
    }

    /// Virtually clears the memory by setting its size to 0. The capacity stays unchanged.
    void clear() noexcept { m_size = 0; }
};

struct gas_parameters {

    using storage_cost_t = std::array<StorageStoreCost, EVMC_STORAGE_MODIFIED_RESTORED + 1>;

    gas_parameters(){}

    gas_parameters(uint64_t txnewaccount, uint64_t newaccount, uint64_t txcreate, uint64_t codedeposit, uint64_t sset) :
        G_txnewaccount{txnewaccount},
        G_newaccount{newaccount},
        G_txcreate{txcreate},
        G_codedeposit{codedeposit},
        G_sset{sset}{}

    uint64_t G_txnewaccount = 0;
    uint64_t G_newaccount = 25000;
    uint64_t G_txcreate = 32000;
    uint64_t G_codedeposit = 200;
    uint64_t G_sset = 20000;

    const storage_cost_t& get_storage_cost(uint64_t version) {
        if(!storage_cost.has_value()) {
            storage_cost = generate_storage_cost_table(version);
        }
        return *storage_cost;
    }

private:
    storage_cost_t generate_storage_cost_table(uint64_t version) {
        int64_t warm_access = instr::warm_storage_read_cost;
        int64_t set         = static_cast<int64_t>(G_sset);
        int64_t reset       = 5000 - instr::cold_sload_cost;
        int64_t clear       = 4800;

        storage_cost_t st;
        if( version >= 3) {
            int64_t cpu_gas_to_change_slot  = reset - warm_access; //cpu cost of adding or removing or mutating a slot in the db
            int64_t storage_gas_to_add_slot = set - reset; //storage cost of adding a new slot into the db

            st[EVMC_STORAGE_ASSIGNED]          = {warm_access                         ,                        0};
            st[EVMC_STORAGE_ADDED]             = {warm_access + cpu_gas_to_change_slot,  storage_gas_to_add_slot};
            st[EVMC_STORAGE_DELETED]           = {warm_access + cpu_gas_to_change_slot, -storage_gas_to_add_slot};
            st[EVMC_STORAGE_MODIFIED]          = {warm_access + cpu_gas_to_change_slot,                        0};
            st[EVMC_STORAGE_DELETED_ADDED]     = {warm_access                         ,  storage_gas_to_add_slot};
            st[EVMC_STORAGE_MODIFIED_DELETED]  = {warm_access                         , -storage_gas_to_add_slot};
            st[EVMC_STORAGE_DELETED_RESTORED]  = {warm_access - cpu_gas_to_change_slot,  storage_gas_to_add_slot};
            st[EVMC_STORAGE_ADDED_DELETED]     = {warm_access - cpu_gas_to_change_slot, -storage_gas_to_add_slot};
            st[EVMC_STORAGE_MODIFIED_RESTORED] = {warm_access - cpu_gas_to_change_slot,                        0};
        } else {
            st[EVMC_STORAGE_ASSIGNED]          = {warm_access, 0};
            st[EVMC_STORAGE_ADDED]             = {set, 0};
            st[EVMC_STORAGE_DELETED]           = {reset, clear};
            st[EVMC_STORAGE_MODIFIED]          = {reset, 0};
            st[EVMC_STORAGE_DELETED_ADDED]     = {warm_access,-clear};
            st[EVMC_STORAGE_MODIFIED_DELETED]  = {warm_access, clear};
            st[EVMC_STORAGE_DELETED_RESTORED]  = {warm_access, reset - warm_access - clear};
            st[EVMC_STORAGE_ADDED_DELETED]     = {warm_access, set - warm_access};
            st[EVMC_STORAGE_MODIFIED_RESTORED] = {warm_access, reset - warm_access};
        }
        return st;
    }

    std::optional<storage_cost_t> storage_cost;
};

/// Generic execution state for generic instructions implementations.
// NOLINTNEXTLINE(clang-analyzer-optin.performance.Padding)
class ExecutionState
{
public:
    int64_t apply_gas_refund(int64_t gas_cost) {
        if (eos_evm_version >= 3) {
            int64_t d = gas_cost - gas_refund;
            gas_refund = std::max(-d, 0l);
            return std::max(d, 0l);
        }
        return gas_cost;
    }

    int64_t apply_storage_gas_refund(int64_t gas_cost){
        if (eos_evm_version >= 3) {
            int64_t d = gas_cost - storage_gas_refund;
            storage_gas_refund = std::max(-d, 0l);
            return std::max(d, 0l);
        }
        return gas_cost;
    }

    int64_t gas_refund = 0;
    int64_t storage_gas_consumed = 0;
    int64_t storage_gas_refund = 0;

    Memory memory;
    const evmc_message* msg = nullptr;
    evmc::HostContext host;
    evmc_revision rev = {};
    bytes return_data;

    /// Reference to original EVM code container.
    /// For legacy code this is a reference to entire original code.
    /// For EOF-formatted code this is a reference to entire container.
    bytes_view original_code;

    evmc_status_code status = EVMC_SUCCESS;
    size_t output_offset = 0;
    size_t output_size = 0;

    uint64_t eos_evm_version=0;
    gas_parameters gas_params;

private:
    evmc_tx_context m_tx = {};

public:
    /// Pointer to code analysis.
    /// This should be set and used internally by execute() function of a particular interpreter.
    union
    {
        const baseline::CodeAnalysis* baseline = nullptr;
        const advanced::AdvancedCodeAnalysis* advanced;
    } analysis{};

    std::vector<const uint8_t*> call_stack;

    /// Stack space allocation.
    ///
    /// This is the last field to make other fields' offsets of reasonable values.
    StackSpace stack_space;

    ExecutionState() noexcept = default;

    ExecutionState(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code) noexcept
      : msg{&message}, host{host_interface, host_ctx}, rev{revision}, original_code{_code}
    {}

    /// Resets the contents of the ExecutionState so that it could be reused.
    void reset(const evmc_message& message, evmc_revision revision,
        const evmc_host_interface& host_interface, evmc_host_context* host_ctx,
        bytes_view _code, gas_parameters _gas_params) noexcept
    {
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
        gas_params = _gas_params;
        storage_gas_consumed = 0;
        storage_gas_refund = 0;
    }

    [[nodiscard]] bool in_static_mode() const { return (msg->flags & EVMC_STATIC) != 0; }

    const evmc_tx_context& get_tx_context() noexcept
    {
        if (INTX_UNLIKELY(m_tx.block_timestamp == 0))
            m_tx = host.get_tx_context();
        return m_tx;
    }
};
}  // namespace evmone
