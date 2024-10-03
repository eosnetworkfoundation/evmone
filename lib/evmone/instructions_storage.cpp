// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include "instructions.hpp"

namespace evmone::instr::core
{
namespace
{
/// The gas cost specification for storage instructions.
struct StorageCostSpec
{
    bool net_cost;        ///< Is this net gas cost metering schedule?
    int64_t warm_access;  ///< Storage warm access cost, YP: G_{warmaccess}
    int64_t set;          ///< Storage addition cost, YP: G_{sset}
    int64_t reset;        ///< Storage modification cost, YP: G_{sreset}
    int64_t clear;        ///< Storage deletion refund, YP: R_{sclear}
};

/// Table of gas cost specification for storage instructions per EVM revision.
/// TODO: This can be moved to instruction traits and be used in other places: e.g.
///       SLOAD cost, replacement for warm_storage_read_cost.
constexpr auto storage_cost_spec = []() noexcept {
    std::array<StorageCostSpec, EVMC_MAX_REVISION + 1> tbl{};

    // Legacy cost schedule.
    for (auto rev : {EVMC_FRONTIER, EVMC_HOMESTEAD, EVMC_TANGERINE_WHISTLE, EVMC_SPURIOUS_DRAGON,
             EVMC_BYZANTIUM, EVMC_PETERSBURG})
        tbl[rev] = {false, 200, 20000, 5000, 15000};

    // Net cost schedule.
    tbl[EVMC_CONSTANTINOPLE] = {true, 200, 20000, 5000, 15000};
    tbl[EVMC_ISTANBUL] = {true, 800, 20000, 5000, 15000};
    tbl[EVMC_BERLIN] = {
        true, instr::warm_storage_read_cost, 20000, 5000 - instr::cold_sload_cost, 15000};
    tbl[EVMC_LONDON] = {
        true, instr::warm_storage_read_cost, 20000, 5000 - instr::cold_sload_cost, 4800};
    tbl[EVMC_PARIS] = tbl[EVMC_LONDON];
    tbl[EVMC_SHANGHAI] = tbl[EVMC_LONDON];
    tbl[EVMC_CANCUN] = tbl[EVMC_LONDON];
    return tbl;
}();

// The lookup table of SSTORE costs by the storage update status.
constexpr auto sstore_costs = []() noexcept {
    std::array<std::array<evmone::StorageStoreCost, EVMC_STORAGE_MODIFIED_RESTORED + 1>,
        EVMC_MAX_REVISION + 1>
        tbl{};

    for (size_t rev = EVMC_FRONTIER; rev <= EVMC_MAX_REVISION; ++rev)
    {
        auto& e = tbl[rev];
        if (const auto c = storage_cost_spec[rev]; !c.net_cost)  // legacy
        {
            e[EVMC_STORAGE_ADDED]             = {c.set, 0};
            e[EVMC_STORAGE_DELETED]           = {c.reset, c.clear};
            e[EVMC_STORAGE_MODIFIED]          = {c.reset, 0};
            e[EVMC_STORAGE_ASSIGNED]          = e[EVMC_STORAGE_MODIFIED];
            e[EVMC_STORAGE_DELETED_ADDED]     = e[EVMC_STORAGE_ADDED];
            e[EVMC_STORAGE_MODIFIED_DELETED]  = e[EVMC_STORAGE_DELETED];
            e[EVMC_STORAGE_DELETED_RESTORED]  = e[EVMC_STORAGE_ADDED];
            e[EVMC_STORAGE_ADDED_DELETED]     = e[EVMC_STORAGE_DELETED];
            e[EVMC_STORAGE_MODIFIED_RESTORED] = e[EVMC_STORAGE_MODIFIED];
        }
        else  // net cost
        {
            e[EVMC_STORAGE_ASSIGNED]          = {c.warm_access, 0};
            e[EVMC_STORAGE_ADDED]             = {c.set, 0};
            e[EVMC_STORAGE_DELETED]           = {c.reset, c.clear};
            e[EVMC_STORAGE_MODIFIED]          = {c.reset, 0};
            e[EVMC_STORAGE_DELETED_ADDED]     = {c.warm_access, -c.clear};
            e[EVMC_STORAGE_MODIFIED_DELETED]  = {c.warm_access, c.clear};
            e[EVMC_STORAGE_DELETED_RESTORED]  = {c.warm_access, c.reset - c.warm_access - c.clear};
            e[EVMC_STORAGE_ADDED_DELETED]     = {c.warm_access, c.set - c.warm_access};
            e[EVMC_STORAGE_MODIFIED_RESTORED] = {c.warm_access, c.reset - c.warm_access};
        }
    }

    return tbl;
}();
}  // namespace

Result sload(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    auto& x = stack.top();
    const auto key = intx::be::store<evmc::bytes32>(x);

    if (state.rev >= EVMC_BERLIN &&
        state.host.access_storage(state.msg->recipient, key) == EVMC_ACCESS_COLD)
    {
        // The warm storage access cost is already applied (from the cost table).
        // Here we need to apply additional cold storage access cost.
        int64_t additional_cold_sload_cost = instr::cold_sload_cost - instr::warm_storage_read_cost;
        if ((gas_left -= additional_cold_sload_cost) < 0)
            return {EVMC_OUT_OF_GAS, gas_left};
    }

    x = intx::be::load<uint256>(state.host.get_storage(state.msg->recipient, key));

    return {EVMC_SUCCESS, gas_left};
}

Result sstore(StackTop stack, int64_t gas_left, ExecutionState& state) noexcept
{
    if (state.in_static_mode())
        return {EVMC_STATIC_MODE_VIOLATION, gas_left};

    //TODO: should we use state.total_gas_left(gas_left) instead of gas_left?
    if (state.rev >= EVMC_ISTANBUL && gas_left <= 2300)
        return {EVMC_OUT_OF_GAS, gas_left};

    const auto key = intx::be::store<evmc::bytes32>(stack.pop());
    const auto value = intx::be::store<evmc::bytes32>(stack.pop());

    const auto gas_cost_cold =
        (state.rev >= EVMC_BERLIN &&
            state.host.access_storage(state.msg->recipient, key) == EVMC_ACCESS_COLD) ?
            instr::cold_sload_cost :
            0;
    const auto status = state.host.set_storage(state.msg->recipient, key, value);
    const auto& storage_cost = state.eos_evm_version > 0 ? state.gas_params.get_storage_cost(state.eos_evm_version) : sstore_costs[state.rev];

    if( state.eos_evm_version >= 3) {
        auto [cpu_gas_to_changle_slot_delta, storage_gas_delta] = storage_cost[status];
        const auto real_cpu_gas_consumed = instr::warm_storage_read_cost + gas_cost_cold;

        const auto storage_gas_consumed = state.gas_state.apply_storage_gas_delta(storage_gas_delta);
        const auto speculative_cpu_gas_consumed = state.gas_state.apply_speculative_cpu_gas_delta(cpu_gas_to_changle_slot_delta);

        const auto gas_cost = storage_gas_consumed + real_cpu_gas_consumed + speculative_cpu_gas_consumed;
        if ((gas_left -= gas_cost) < 0)
            return {EVMC_OUT_OF_GAS, gas_left};
    } else {
        auto [gas_cost_warm, gas_refund] = storage_cost[status];
        const auto gas_cost = gas_cost_warm + gas_cost_cold;
        if ((gas_left -= gas_cost) < 0)
            return {EVMC_OUT_OF_GAS, gas_left};
        state.gas_state.add_cpu_gas_refund(gas_refund);
    }

    return {EVMC_SUCCESS, gas_left};
}
}  // namespace evmone::instr::core
