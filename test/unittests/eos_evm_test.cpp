// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

/// This file contains EVM unit tests that access or modify the contract storage.

#include "evm_fixture.hpp"
#include <array>

using namespace evmc::literals;
using evmone::test::evm;

evmc_revision evm_version_to_revision[]={
    EVMC_ISTANBUL, //eos_evm_version=0
    EVMC_SHANGHAI, //eos_evm_version=1
    EVMC_SHANGHAI  //eos_evm_version=2
};

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

    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

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
    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x00000000000000000000000000000000000000fe_address;
    const auto code = 4 * push(0) + push(1) + push(call_dst) + push(0) + OP_CALL + ret_top();

    // [00]	PUSH1	00                                        // 3
    // [02]	PUSH1	00                                        // 3
    // [04]	PUSH1	00                                        // 3
    // [06]	PUSH1	00                                        // 3
    // [08]	PUSH1	01                                        // 3
    // [0a]	PUSH20	00000000000000000000000000000000000000ad  // 3
    // [1f]	PUSH1	00                                        // 3
    // [21]	CALL                                              // 100 + 25005 + 9000 + 2500
    // [22]	PUSH1	00                                        // 3
    // [24]	MSTORE                                            // 6
    // [25]	PUSH1	20                                        // 3
    // [27]	PUSH1	00                                        // 3
    // [29]	RETURN                                            // 0

    msg.recipient = msg_dst;

    //----------------------------------------------
    // Test account creation from inside a contract
    //----------------------------------------------

    gas_params.G_newaccount   = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);
    execute(code);

    EXPECT_GAS_USED(EVMC_SUCCESS, 3+3+3+3+3+3+3+(100+25005+9000+2500)+3+6+3+3+0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);
}

TEST_P(evm, call_reserved_address_cost_eos_evm)
{
    constexpr auto call_dst = 0xbbbbbbbbbbbbbbbbbbbbbbbb3ab3400000000000_address;  //'beto'
    auto call_reserved_address = [&](uint64_t version) {
        host.recorded_calls.clear();
        host.recorded_account_accesses.clear();
        eos_evm_version=version;
        rev = evm_version_to_revision[eos_evm_version];

        constexpr auto msg_dst = 0x00000000000000000000000000000000000000ad_address;
        const auto code = 4 * push(0) + push(1) + push(call_dst) + push(0) + OP_CALL + ret_top();

        // [00]	PUSH1	00                                        // 3
        // [02]	PUSH1	00                                        // 3
        // [04]	PUSH1	00                                        // 3
        // [06]	PUSH1	00                                        // 3
        // [08]	PUSH1	01                                        // 3
        // [0a]	PUSH20	00000000000000000000000000000000000000ad  // 3
        // [1f]	PUSH1	00                                        // 3
        // [21]	CALL                                              // [100|700] + ? + 9000 + [2500|0] (?=depends on the eos_evm_version)
        // [22]	PUSH1	00                                        // 3
        // [24]	MSTORE                                            // 6
        // [25]	PUSH1	20                                        // 3
        // [27]	PUSH1	00                                        // 3
        // [29]	RETURN                                            // 0

        msg.recipient = msg_dst;

        //----------------------------------------------
        // Test account creation from inside a contract
        //----------------------------------------------

        gas_params.G_newaccount   = 25005;
        gas_params.G_txnewaccount = 25006;

        host.accounts[msg.recipient].set_balance(1024);
        execute(code);
    };

    call_reserved_address(0);
    EXPECT_GAS_USED(EVMC_SUCCESS, 3+3+3+3+3+3+3+(700+25000+9000+0)+3+6+3+3+0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    call_reserved_address(1);
    EXPECT_GAS_USED(EVMC_SUCCESS, 3+3+3+3+3+3+3+(100+0+9000+2500)+3+6+3+3+0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    call_reserved_address(2);
    EXPECT_GAS_USED(EVMC_SUCCESS, 3+3+3+3+3+3+3+(100+0+9000+2500)+3+6+3+3+0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

}

TEST_P(evm, selfdestruct_eos_evm)
{
    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

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
    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

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
    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

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
