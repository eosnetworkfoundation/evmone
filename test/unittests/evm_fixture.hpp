// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019-2020 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "evmone/eof.hpp"
#include <evmc/mocked_host.hpp>
#include <gtest/gtest.h>
#include <intx/intx.hpp>
#include <test/utils/bytecode.hpp>
#include <evmone/vm.hpp>
#include <evmone/execution_state.hpp>
#include <evmone/baseline.hpp>
#include <evmone/advanced_execution.hpp>
#include <evmone/advanced_analysis.hpp>

#define EXPECT_STATUS(STATUS_CODE)                                           \
    EXPECT_EQ(result.status_code, STATUS_CODE);                              \
    if constexpr (STATUS_CODE != EVMC_SUCCESS && STATUS_CODE != EVMC_REVERT) \
    {                                                                        \
        EXPECT_EQ(result.gas_left, 0);                                       \
    }                                                                        \
    (void)0

#define EXPECT_GAS_USED(STATUS_CODE, GAS_USED)  \
    EXPECT_EQ(result.status_code, STATUS_CODE); \
    EXPECT_EQ(gas_used, GAS_USED)

#define EXPECT_OUTPUT_INT(X)                                 \
    ASSERT_EQ(result.output_size, sizeof(intx::uint256));    \
    EXPECT_EQ(hex({result.output_data, result.output_size}), \
        hex({intx::be::store<evmc_bytes32>(intx::uint256{X}).bytes, sizeof(evmc_bytes32)}))


namespace evmone::test
{

struct TestHost : evmc::MockedHost {
    uint64_t eos_evm_version=0;

    std::optional<uint64_t> extract_reserved_address(const evmc::address& addr) const {
        constexpr uint8_t reserved_address_prefix[] = {0xbb, 0xbb, 0xbb, 0xbb,
                                                    0xbb, 0xbb, 0xbb, 0xbb,
                                                    0xbb, 0xbb, 0xbb, 0xbb};

        if(!std::equal(std::begin(reserved_address_prefix), std::end(reserved_address_prefix), static_cast<evmc::bytes_view>(addr).begin()))
            return std::nullopt;
        uint64_t reserved;
        memcpy(&reserved, static_cast<evmc::bytes_view>(addr).data()+sizeof(reserved_address_prefix), sizeof(reserved));
        return be64toh(reserved);
    }

    bool is_reserved_address(const evmc::address& addr) const {
        return extract_reserved_address(addr) != std::nullopt;
    }

    bool account_exists(const evmc::address& addr) const noexcept override
    {
        if(eos_evm_version >= 1 && is_reserved_address(addr)) {
            return true;
        }
        return evmc::MockedHost::account_exists(addr);
    }
};

/// The "evm" test fixture with generic unit tests for EVMC-compatible VM implementations.
class evm : public testing::TestWithParam<evmc::VM*>
{
protected:
    /// Reports if execution is done by evmone/Advanced.
    static bool is_advanced() noexcept;

    /// The VM handle.
    evmc::VM& vm;

    /// The EVM revision for unit test execution. Byzantium by default.
    /// TODO: Add alias evmc::revision.
    evmc_revision rev = EVMC_BYZANTIUM;

    /// The message to be executed by a unit test (with execute() method).
    /// TODO: Add evmc::message with default constructor.
    evmc_message msg{};

    /// The result of execution (available after execute() is invoked).
    evmc::Result result;

    /// The result output. Updated by execute().
    bytes_view output;

    /// The total amount of gas used during execution.
    int64_t gas_used = 0;

    TestHost host;

    evm() noexcept : vm{*GetParam()} {}

    uint64_t eos_evm_version = 0;
    evmone::gas_parameters gas_params;

    /// Executes the supplied code.
    ///
    /// @param gas    The gas limit for execution.
    /// @param code   The EVM bytecode.
    /// @param input  The EVM "calldata" input.
    /// The execution result will be available in the `result` field.
    /// The `gas_used` field  will be updated accordingly.
    void execute(int64_t gas, const bytecode& code, bytes_view input = {}) noexcept
    {
        result = evmc::Result{};
        host.eos_evm_version = eos_evm_version;

        msg.input_data = input.data();
        msg.input_size = input.size();
        msg.gas = gas;

        if (rev >= EVMC_BERLIN)  // Add EIP-2929 tweak.
        {
            host.access_account(msg.sender);
            host.access_account(msg.recipient);
        }

        if (rev >= EVMC_PRAGUE && is_eof_container(code))
        {
            ASSERT_EQ(get_error_message(validate_eof(rev, ContainerKind::runtime, code)),
                get_error_message(EOFValidationError::success));
        }


        if(!is_advanced()) {

            auto& evm_ = *static_cast<VM*>(vm.get_raw_pointer());
            const bytes_view container = code;
            const auto eof_enabled = rev >= instr::REV_EOF1;

            // Since EOF validation recurses into subcontainers, it only makes sense to do for top level
            // message calls. The condition for `msg->kind` inside differentiates between creation tx code
            // (initcode) and already deployed code (runtime).
            if (evm_.validate_eof && eof_enabled && is_eof_container(container) && msg.depth == 0)
            {
                const auto container_kind =
                    (msg.kind == EVMC_EOFCREATE ? ContainerKind::initcode : ContainerKind::runtime);
                if (validate_eof(rev, container_kind, container) != EOFValidationError::success)
                    result = evmc::Result{evmc_make_result(EVMC_CONTRACT_VALIDATION_FAILURE, 0, 0, 0, 0, 0, nullptr, 0)};
            }
            if( result.status_code != EVMC_CONTRACT_VALIDATION_FAILURE ) {
                const auto code_analysis = evmone::baseline::analyze(container, eof_enabled);
                evmone::ExecutionState state;
                state.reset(msg, rev, evmc::MockedHost::get_interface(), host.to_context(), code, gas_params, eos_evm_version);
                result = evmc::Result{evmone::baseline::execute(evm_, msg, code_analysis, state)};
            }
        } else {
            evmone::advanced::AdvancedCodeAnalysis analysis;
            const bytes_view container = code;
            if (is_eof_container(container))
            {
                if (rev >= EVMC_PRAGUE)
                {
                    const auto eof1_header = read_valid_eof1_header(container);
                    analysis = evmone::advanced::analyze(rev, eof1_header.get_code(container, 0));
                }
                else{
                    // Skip analysis, because it will recognize 01 section id as OP_ADD and return
                    // EVMC_STACKUNDERFLOW.
                    result = evmc::Result{evmc::make_result(EVMC_UNDEFINED_INSTRUCTION, 0, 0, 0l, 0l, 0l, nullptr, 0)};
                }
            }
            else {
                analysis = evmone::advanced::analyze(rev, container);
            }

            if( result.status_code != EVMC_UNDEFINED_INSTRUCTION ) {
                evmone::advanced::AdvancedExecutionState state;
                state.reset(msg, rev, evmc::MockedHost::get_interface(), host.to_context(), code, gas_params, eos_evm_version);
                result = evmc::Result{evmone::advanced::execute(state, analysis)};
            }
        }

        output = {result.output_data, result.output_size};
        gas_used = msg.gas - result.gas_left;
    }

    /// Executes the supplied code.
    ///
    /// @param code   The EVM bytecode.
    /// @param input  The EVM "calldata" input.
    /// The execution result will be available in the `result` field.
    /// The `gas_used` field  will be updated accordingly.
    void execute(const bytecode& code, bytes_view input = {}) noexcept
    {
        execute(std::numeric_limits<int64_t>::max(), code, input);
    }
};
}  // namespace evmone::test
