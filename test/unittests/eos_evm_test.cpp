// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

/// This file contains EVM unit tests that access or modify the contract storage.

#include "evm_fixture.hpp"
#include <array>

using namespace evmc::literals;
using evmone::test::evm;

TEST_P(evm, sstore_cost_eos_evm)
{
    static constexpr auto O = 0x000000000000000000_bytes32;
    static constexpr auto X = 0x00ffffffffffffffff_bytes32;
    static constexpr auto Y = 0x010000000000000000_bytes32;
    static constexpr auto Z = 0x010000000000000001_bytes32;
    static constexpr auto key = 0xde_bytes32;

    static constexpr int64_t b = 6;  // Cost of other instructions.

    const auto test = [this](const evmc::bytes32& original, const evmc::bytes32& current,
                          const evmc::bytes32& value, evmc_storage_status s) {
        auto expected_gas_used = b + gas_params.storage_cost[s].gas_cost;
        auto expected_gas_refund = gas_params.storage_cost[s].gas_refund;
        auto& storage_entry = host.accounts[msg.recipient].storage[key];
        storage_entry.original = original;
        storage_entry.current = current;
        storage_entry.access_status = EVMC_ACCESS_WARM;
        execute(sstore(key, value));
        EXPECT_EQ(storage_entry.current, value);
        EXPECT_GAS_USED(EVMC_SUCCESS, expected_gas_used);
        EXPECT_EQ(result.gas_refund, expected_gas_refund);
    };

    rev = EVMC_ISTANBUL;
    eos_evm_version = 1;

    test(O, O, O, EVMC_STORAGE_ASSIGNED );          // assigned
    test(X, O, O, EVMC_STORAGE_ASSIGNED );
    test(O, Y, Y, EVMC_STORAGE_ASSIGNED );
    test(X, Y, Y, EVMC_STORAGE_ASSIGNED );
    test(Y, Y, Y, EVMC_STORAGE_ASSIGNED );
    test(O, Y, Z, EVMC_STORAGE_ASSIGNED );
    test(X, Y, Z, EVMC_STORAGE_ASSIGNED );

    test(O, O, Z, EVMC_STORAGE_ADDED );             // added
    test(X, X, O, EVMC_STORAGE_DELETED );           // deleted
    test(X, X, Z, EVMC_STORAGE_MODIFIED );          // modified
    test(X, O, Z, EVMC_STORAGE_DELETED_ADDED );     // deleted added
    test(X, Y, O, EVMC_STORAGE_MODIFIED_DELETED );  // modified deleted
    test(X, O, X, EVMC_STORAGE_DELETED_RESTORED );  // deleted restored
    test(O, Y, O, EVMC_STORAGE_ADDED_DELETED );     // added deleted
    test(X, Y, X, EVMC_STORAGE_MODIFIED_RESTORED ); // modified restored
}

TEST_P(evm, call_new_account_creation_cost_eos_evm)
{
    rev=EVMC_ISTANBUL;
    eos_evm_version=1;

    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x00000000000000000000000000000000000000fe_address;
    const auto code = 4 * push(0) + push(1) + push(call_dst) + push(0) + OP_CALL + ret_top();

    //         | cost
    // PUSH1   |  3
    // PUSH1   |  3
    // PUSH1   |  3
    // PUSH1   |  3
    // PUSH1   |  3
    // PUSH20  |  3
    // PUSH1   |  3
    // CALL    | 700
    // PUSH1   |  3
    // MSTORE  |  3
    // PUSH1   |  3
    // PUSH1   |  3
    // RETURN  |  3

    msg.recipient = msg_dst;

    //----------------------------------------------
    // Test account creation from inside a contract
    //----------------------------------------------

    gas_params.G_newaccount   = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);
    execute(code);

    // PUSHs+RETURN + CALL + G_newaccount + has_value
    EXPECT_GAS_USED(EVMC_SUCCESS, 3*12 + 700 + 25005 + 9000);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);
}

TEST_P(evm, selfdestruct_eos_evm)
{
    rev=EVMC_ISTANBUL;
    eos_evm_version=1;

    gas_params.G_newaccount = 25005;

    msg.recipient = 0x01_address;
    const auto& selfdestructs = host.recorded_selfdestructs[msg.recipient];
    host.accounts[msg.recipient].set_balance(1024);

    // Bytecode created by `selfdestruct(0x02)`
    //              |  cost
    // PUSH1        |   3
    // SELFDESTRUCT |  5000

    execute(50000, selfdestruct(0x02));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 3+5000+25005);
    ASSERT_EQ(selfdestructs.size(), 1);
    EXPECT_EQ(selfdestructs.back(), 0x02_address);

}

TEST_P(evm, create_gas_cost_eos_evm)
{
    rev=EVMC_ISTANBUL;
    eos_evm_version=1;

    // Set CREATE opcode static gas cost to 32005
    gas_params.G_txcreate = 32005;

    // Bytecode created by `create()`
    //    inst     |  cost
    // PUSH1 0x00  |   3
    // PUSH1 0x00  |   3
    // PUSH1 0x00  |   3
    // CREATE      |   32005

    // 50000-(3+3+3+32005) = 17986
    // Gas of message sent after CREATE = 17986 - int(17986/64) = 17705

    // Hardcode gas used for the processing of the message to 0 (gas_left = gas_in). 
    // The mocked host call implementation simply records the call in `host.recorded_calls`
    // and returns whatever is set in `host.call_result`.
    host.call_result.gas_left = 17705;

    // Run the 4 instructions
    execute(50000, create());
    
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 3+3+3+32005);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 17705);
}

TEST_P(evm, create2_gas_cost_eos_evm)
{
    rev=EVMC_ISTANBUL;
    eos_evm_version=1;

    // Set CREATE2 opcode static gas cost to 32005
    gas_params.G_txcreate = 32005;
    const auto code = static_cast<bytecode>(create2().salt(0x5a));

    // Bytecode created by `create2().salt(0x5a)`
    //    inst     |  cost
    // PUSH1 0x5a  |   3
    // PUSH1 0x00  |   3
    // PUSH1 0x00  |   3
    // PUSH1 0x00  |   3
    // CREATE2     |   32005

    // 150000-(3+3+3+3+32005) = 117983
    // Gas of message sent after CREATE2 = 117983 - int(117983/64) = 116140

    // Hardcode gas used for the processing of the message to 0 (gas_left = gas_in). 
    // The mocked host call implementation simply records the call in `host.recorded_calls`
    // and returns whatever is set in `host.call_result`.
    host.call_result.gas_left = 116140;

    // Run the 5 instructions
    execute(150000, code);
    
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 3+3+3+3+32005);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 116140);
}
