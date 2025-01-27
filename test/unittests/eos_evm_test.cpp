// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

/// This file contains EVM unit tests that access or modify the contract storage.

#include "evm_fixture.hpp"
#include <array>

#include <evmone/instructions_traits.hpp>

using namespace evmc::literals;
using evmone::test::evm;
using namespace evmone;
using namespace evmone::test;

evmc_revision evm_version_to_revision[] = {
    EVMC_ISTANBUL,  // eos_evm_version=0
    EVMC_SHANGHAI,  // eos_evm_version=1
    EVMC_SHANGHAI,  // eos_evm_version=2
    EVMC_SHANGHAI   // eos_evm_version=3
};

TEST_P(evm, sstore_cost_eos_evm)
{
    static constexpr auto O = 0x000000000000000000_bytes32;
    static constexpr auto X = 0x00ffffffffffffffff_bytes32;
    static constexpr auto Y = 0x010000000000000000_bytes32;
    static constexpr auto Z = 0x010000000000000001_bytes32;
    static constexpr auto key = 0xde_bytes32;

    static constexpr int64_t b = 3 + 3;  // Cost of other instructions.

    const auto test = [this](const evmc::bytes32& original, const evmc::bytes32& current,
                          const evmc::bytes32& value, evmc_storage_status s, uint64_t version) {
        auto storage_cost = gas_params.get_storage_cost(version);

        int64_t expected_gas_used{0};
        int64_t expected_gas_refund{0};
        int64_t storage_gas_consumed{0};
        int64_t storage_gas_refund{0};
        int64_t speculative_cpu_gas_consumed{0};

        if (version >= 3)
        {
            speculative_cpu_gas_consumed = std::max(storage_cost[s].gas_cost, 0l);
            storage_gas_consumed = std::max(storage_cost[s].gas_refund, 0l);
            storage_gas_refund = -std::min(storage_cost[s].gas_refund, 0l);
            expected_gas_used = b + speculative_cpu_gas_consumed + storage_gas_consumed + 100;
            expected_gas_refund = -std::min(storage_cost[s].gas_cost, 0l);
        }
        else
        {
            expected_gas_used = b + storage_cost[s].gas_cost;
            expected_gas_refund = storage_cost[s].gas_refund;
        }

        auto& storage_entry = host.accounts[msg.recipient].storage[key];
        storage_entry.original = original;
        storage_entry.current = current;
        storage_entry.access_status = EVMC_ACCESS_WARM;
        execute(sstore(key, value));
        EXPECT_EQ(storage_entry.current, value);
        EXPECT_GAS_USED(EVMC_SUCCESS, expected_gas_used);
        EXPECT_EQ(result.gas_refund, expected_gas_refund);
        EXPECT_EQ(result.storage_gas_consumed, storage_gas_consumed);
        EXPECT_EQ(result.storage_gas_refund, storage_gas_refund);
        EXPECT_EQ(result.speculative_cpu_gas_consumed , speculative_cpu_gas_consumed);
    };

    eos_evm_version = 1;
    rev = evm_version_to_revision[eos_evm_version];

    test(O, O, O, EVMC_STORAGE_ASSIGNED, eos_evm_version);      // assigned
    test(X, O, O, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(O, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(X, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(Y, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(O, Y, Z, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(X, Y, Z, EVMC_STORAGE_ASSIGNED, eos_evm_version);

    test(O, O, Z, EVMC_STORAGE_ADDED, eos_evm_version);             // added
    test(X, X, O, EVMC_STORAGE_DELETED, eos_evm_version);           // deleted
    test(X, X, Z, EVMC_STORAGE_MODIFIED, eos_evm_version);          // modified
    test(X, O, Z, EVMC_STORAGE_DELETED_ADDED, eos_evm_version);     // deleted added
    test(X, Y, O, EVMC_STORAGE_MODIFIED_DELETED, eos_evm_version);  // modified
    test(X, O, X, EVMC_STORAGE_DELETED_RESTORED, eos_evm_version);  // deleted restored
    test(O, Y, O, EVMC_STORAGE_ADDED_DELETED, eos_evm_version);     // added deleted
    test(X, Y, X, EVMC_STORAGE_MODIFIED_RESTORED, eos_evm_version); // modified restored

    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];
    gas_params = evmone::gas_parameters{};

    test(O, O, O, EVMC_STORAGE_ASSIGNED, eos_evm_version);  // assigned
    test(X, O, O, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(O, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(X, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(Y, Y, Y, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(O, Y, Z, EVMC_STORAGE_ASSIGNED, eos_evm_version);
    test(X, Y, Z, EVMC_STORAGE_ASSIGNED, eos_evm_version);

    test(O, O, Z, EVMC_STORAGE_ADDED, eos_evm_version);              // added
    test(X, X, O, EVMC_STORAGE_DELETED, eos_evm_version);            // deleted
    test(X, X, Z, EVMC_STORAGE_MODIFIED, eos_evm_version);           // modified
    test(X, O, Z, EVMC_STORAGE_DELETED_ADDED, eos_evm_version);      // deleted added
    test(X, Y, O, EVMC_STORAGE_MODIFIED_DELETED, eos_evm_version);   // modified deleted
    test(X, O, X, EVMC_STORAGE_DELETED_RESTORED, eos_evm_version);   // deleted restored
    test(O, Y, O, EVMC_STORAGE_ADDED_DELETED, eos_evm_version);      // added deleted
    test(X, Y, X, EVMC_STORAGE_MODIFIED_RESTORED, eos_evm_version);  // modified restored
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

    gas_params.G_newaccount = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);
    execute(code);

    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (100 + 25005 + 9000 + 2500) + 3 + 6 + 3 + 3 + 0);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 0);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];
    host.recorded_calls.clear();
    host.recorded_account_accesses.clear();

    execute(code);

    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (100 + 25005 + 9000 + 2500) + 3 + 6 + 3 + 3 + 0);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 25005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 6700);
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
        eos_evm_version = version;
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
        // [21]	CALL                                              // [100|700] + ? + 9000 + [2500|0]
        // (?=depends on the eos_evm_version) [22]	PUSH1	00 // 3 [24]	MSTORE // 6 [25]
        // PUSH1	20                                        // 3 [27]	PUSH1	00 // 3 [29]
        // RETURN                                            // 0

        msg.recipient = msg_dst;

        gas_params.G_newaccount = 25005;
        gas_params.G_txnewaccount = 25006;

        host.accounts[msg.recipient].set_balance(1024);
        execute(code);
    };

    call_reserved_address(0);
    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (700 + 25000 + 9000 + 0) + 3 + 6 + 3 + 3 + 0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    call_reserved_address(1);
    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (100 + 0 + 9000 + 2500) + 3 + 6 + 3 + 3 + 0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    call_reserved_address(2);
    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (100 + 0 + 9000 + 2500) + 3 + 6 + 3 + 3 + 0);
    EXPECT_OUTPUT_INT(1);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().recipient, call_dst);
    EXPECT_EQ(host.recorded_calls.back().gas, 2300);

    call_reserved_address(3);
    EXPECT_GAS_USED(
        EVMC_SUCCESS, 3 + 3 + 3 + 3 + 3 + 3 + 3 + (100 + 0 + 9000 + 2500) + 3 + 6 + 3 + 3 + 0);
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
    host.accounts[msg.recipient].set_balance(1024);

    // Bytecode created by `selfdestruct(0x02)`
    //              |  cost
    // PUSH1        |   3
    // SELFDESTRUCT |  5000

    execute(50000, selfdestruct(0x02));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 0);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 5000 + 25005);
    ASSERT_EQ(host.recorded_selfdestructs[msg.recipient].size(), 1);
    EXPECT_EQ(host.recorded_selfdestructs[msg.recipient].back(), 0x02_address);

    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];
    host.recorded_calls.clear();
    host.recorded_account_accesses.clear();
    host.recorded_selfdestructs.clear();

    execute(50000, selfdestruct(0x02));
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 25005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 5000 + 25005);
    ASSERT_EQ(host.recorded_selfdestructs[msg.recipient].size(), 1);
    EXPECT_EQ(host.recorded_selfdestructs[msg.recipient].back(), 0x02_address);
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
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 0);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 3 + 3 + 32005);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 17705);

    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];
    host.recorded_calls.clear();

    execute(50000, create());

    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 32005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 3 + 3 + 32005);
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
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 0);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 3 + 3 + 3 + 32005);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 116140);

    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];
    host.recorded_calls.clear();

    execute(150000, code);

    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 32005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);
    EXPECT_EQ(gas_used, 3 + 3 + 3 + 3 + 32005);
    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 116140);
}

// [*1]
// pc=parent context|cc=child context|scgc=speculative cpu gas consumed|cref=cpu refund
// sgc=storage gas consumed|sref=storage gas refund||left=gas left
TEST_P(evm, storage_gas_refund_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    static constexpr auto O = 0x000000000000000000_bytes32;
    static constexpr auto X = 0x00ffffffffffffffff_bytes32;
    static constexpr evmc::bytes32 keys[] = {0xda_bytes32, 0xdb_bytes32, 0xdc_bytes32};

    auto init_storage = [&](const auto& key, const auto& value) {
        auto& storage_entry = host.accounts[msg.recipient].storage[key];
        storage_entry.original = value;
        storage_entry.current = value;
        storage_entry.access_status = EVMC_ACCESS_WARM;
    };

    init_storage(keys[0], X);
    init_storage(keys[1], X);
    init_storage(keys[2], X);

    auto code = sstore(keys[0], O) + sstore(keys[1], O) + sstore(keys[2], O);

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   |
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |  150000 |
    // |          3 |    -    |       0|       0|      0|       0|  149997 | push8 0x00
    // |          3 |    -    |       0|       0|      0|       0|  149994 | push1 0xda
    // |       2900 |    -    |    2800|       0|      0|   20000|  147094 | sstore
    // |          3 |    -    |       0|       0|      0|       0|  147091 | push8 0x00
    // |          3 |    -    |       0|       0|      0|       0|  147088 | push1 0xdb
    // |       2900 |    -    |    2800|       0|      0|   20000|  144188 | sstore
    // |          3 |    -    |       0|       0|      0|       0|  144185 | push8 0x00
    // |          3 |    -    |       0|       0|      0|       0|  144182 | push1 0xdb
    // |       2900 |    -    |    2800|       0|      0|   20000|  141282 | sstore
    // ---------------------------------------------------------------------------------END
    //                             8400|               0|   60000|  141282 |

    execute(150000, code);
    const auto gas_left_ = 141282;
    const auto gas_used_ = 150000 - gas_left_;

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 0);
    EXPECT_EQ(result.storage_gas_refund, 60000);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 8400);

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, (100+3+3)*3 );
}

TEST_P(evm, speculative_cpu_gas_consumed_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    static constexpr auto O = 0x000000000000000000_bytes32;
    static constexpr auto X = 0x0000000000000000ff_bytes32;
    static constexpr evmc::bytes32 keys[] = {0xda_bytes32, 0xdb_bytes32, 0xdc_bytes32};

    auto init_storage = [&](const auto& key, const auto& value) {
        auto& storage_entry = host.accounts[msg.recipient].storage[key];
        storage_entry.original = value;
        storage_entry.current = value;
        storage_entry.access_status = EVMC_ACCESS_WARM;
    };

    init_storage(keys[0], O);
    init_storage(keys[1], O);
    init_storage(keys[2], O);

    auto code = sstore(keys[0], X) + sstore(keys[1], X) + sstore(keys[2], X);

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   |
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |  150000 |
    // |          3 |    -    |       0|       0|      0|       0|  149997 | push8 0xff
    // |          3 |    -    |       0|       0|      0|       0|  149994 | push1 0xda
    // |      22900 |    -    |    2800|       0|  20000|       0|  127094 | sstore
    // |          3 |    -    |       0|       0|      0|       0|  127091 | push8 0xff
    // |          3 |    -    |       0|       0|      0|       0|  127088 | push1 0xdb
    // |      22900 |    -    |    2800|       0|  20000|       0|  104188 | sstore
    // |          3 |    -    |       0|       0|      0|       0|  104185 | push8 0xff
    // |          3 |    -    |       0|       0|      0|       0|  104182 | push1 0xdb
    // |      22900 |    -    |    2800|       0|  20000|       0|   81282 | sstore
    // ---------------------------------------------------------------------------------END
    //                             8400|           60000|            81282 |
    execute(150000, code);

    const auto gas_left_ = 81282;
    const auto gas_used_ = 150000 - gas_left_;

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 60000);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 8400);

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, 318); //100*3 + (3+3)*3
}

TEST_P(evm, call_gas_state_integration_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x00000000000000000000000000000000000000fe_address;
    const auto code = 4 * push(0) + push(1) + push(call_dst) + push(10) + OP_CALL;

    msg.recipient = msg_dst;

    gas_params.G_newaccount = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   | [*1]
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |  100000 |
    // |          3 |    -    |       0|       0|      0|       0|   99997 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99994 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99991 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99988 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99985 | PUSH1 0x01
    // |          3 |    -    |       0|       0|      0|       0|   99982 | PUSH20 0xad
    // |          3 |    -    |       0|       0|      0|       0|   99979 | PUSH1 0x0a
    // |        100 |    -    |       0|       0|  25005|       0|   65674 | CALL (100 + 2500 + 9000 + 25005 - 2300)
    // ---------------------------------------------------------------------------------- call (gas:2310)
    //              |         |       0|       0|      0|       0|    2310 |
    //                                .        .       .        .        .
    //                                .        .       .        .        .
    //            - |end-state|       0|     200|      3|     100|       0 |
    // ---------------------------------------------------------------------------------- call end
    //              |         |       0|     200|      0|      97|         | => post integrate

    // CALL result
    host.call_result.gas_left = 0; // used 2310 (2307 cpu + 3 storage)
    host.call_result.gas_refund = 200;
    host.call_result.storage_gas_consumed = 3;
    host.call_result.storage_gas_refund = 100;
    host.call_result.speculative_cpu_gas_consumed = 0;

    execute(100000, code);

    const auto gas_left_ = 65674 - (2310 - 3);
    const auto gas_used_ = 100000 - gas_left_;

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.gas_refund, 200);
    EXPECT_EQ(result.storage_gas_consumed, 25005);
    EXPECT_EQ(result.storage_gas_refund, 97);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 6700);

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, 3*7 + 100 + 2500 + (2310 - 3) );
}

TEST_P(evm, create_gas_state_propagation_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    // Set CREATE opcode static gas cost to 32005
    gas_params.G_txcreate = 32005;

    // 50000-(3+3+3+32005) = 17986
    // Gas for the message sent after CREATE = 17986 - int(17986/64) = 17705 (gas_in)

    // Contract 'initialization' result
    host.call_result.gas_left = 5;  // used 17700 (17000 cpu + 700 storage)
    host.call_result.gas_refund = 100;
    host.call_result.storage_gas_consumed = 2000;
    host.call_result.storage_gas_refund = 1000;
    host.call_result.speculative_cpu_gas_consumed = 0;

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   |
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |   50000 |
    // |          3 |    -    |       0|       0|      0|       0|   49997 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   49994 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   49991 | PUSH1 0x00
    // |      32005 |    -    |       0|       0|  32005|       0|   17986 | CREATE
    // ---------------------------------------------------------------------------------- call (gas:17705)
    //              |         |       0|       0|      0|       0|   17705 |
    //                                .        .       .        .        . |
    //                                .        .       .        .        . |
    //            - |end-state|       0|     100|   2000|    1000|       5 |
    // ---------------------------------------------------------------------------------- call end
    //              |         |       0|     100|  33005|       0|    1286 | => post integrate

    // Run the 4 instructions and 'execute' initialization
    execute(50000, create());

    const auto gas_left_ = 17986 - (17705 - 5) + 1000; //1286
    const auto gas_used_ = 50000 - gas_left_;

    EXPECT_EQ(gas_used, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_refund, 100);                          // not used in integration
    EXPECT_EQ(result.storage_gas_consumed, 32005 + 2000 - 1000);// parent_storage+child_storage-storage_gas_refund
    EXPECT_EQ(result.storage_gas_refund, 0);                    // consumed
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 0);          // 0

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, 3+3+3+(17700-2000));

    ASSERT_EQ(host.recorded_calls.size(), 1);
    EXPECT_EQ(host.recorded_calls.back().gas, 17705);
}

TEST_P(evm, call_gas_state_integration_out_of_gas_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x00000000000000000000000000000000000000fe_address;
    const auto code = 4 * push(0) + push(1) + push(call_dst) + push(10) + OP_CALL;

    msg.recipient = msg_dst;

    gas_params.G_newaccount = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   | [*1]
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |  100000 |
    // |          3 |    -    |       0|       0|      0|       0|   99997 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99994 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99991 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99988 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99985 | PUSH1 0x01
    // |          3 |    -    |       0|       0|      0|       0|   99982 | PUSH20 0xad
    // |          3 |    -    |       0|       0|      0|       0|   99979 | PUSH1 0x0a
    // |        100 |    -    |       0|       0|      0|       0|   65674 | CALL (100 + 2500 + 9000 + 25005 - 2300)
    // ---------------------------------------------------------------------------------- call (gas:2310)
    //              |         |       0|       0|      0|       0|    2310 |
    //                   .            .        .       .        .        . |
    //                   .            .        .       .        .        . |
    //            - |end-state|       0|       0|      0|       0|    1000 | (oog)
    // ---------------------------------------------------------------------------------- call end
    //              |         |       0|        |      0|        |   64364 | => post integrate

    // CALL result
    host.call_result.status_code = EVMC_OUT_OF_GAS;
    host.call_result.gas_left = 1000;
    host.call_result.gas_refund = 0;
    host.call_result.storage_gas_consumed = 0;
    host.call_result.storage_gas_refund = 0;
    host.call_result.speculative_cpu_gas_consumed = 0;

    execute(100000, code);

    const auto gas_left_ = 65674 - (2310 - 1000); //64364
    const auto gas_used_ = 100000 - gas_left_;

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 25005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 6700);

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, 3*7 + 100 + 2500 + (2310 - 1000) );
}

TEST_P(evm, call_gas_state_integration_revert_eos_evm)
{
    // Gas V3
    eos_evm_version = 3;
    rev = evm_version_to_revision[eos_evm_version];

    constexpr auto call_dst = 0x00000000000000000000000000000000000000ad_address;
    constexpr auto msg_dst = 0x00000000000000000000000000000000000000fe_address;
    const auto code = 4 * push(0) + push(1) + push(call_dst) + push(10) + OP_CALL;

    msg.recipient = msg_dst;

    gas_params.G_newaccount = 25005;
    gas_params.G_txnewaccount = 25006;

    host.accounts[msg.recipient].set_balance(1024);

    // |     pc     |    cc   |  scgc  |  cref  |  sgc  |  sref  |  left   | [*1]
    // ---------------------------------------------------------------------
    // |            |         |        |        |       |        |  100000 |
    // |          3 |    -    |       0|       0|      0|       0|   99997 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99994 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99991 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99988 | PUSH1 0x00
    // |          3 |    -    |       0|       0|      0|       0|   99985 | PUSH1 0x01
    // |          3 |    -    |       0|       0|      0|       0|   99982 | PUSH20 0xad
    // |          3 |    -    |       0|       0|      0|       0|   99979 | PUSH1 0x0a
    // |        100 |    -    |       0|       0|      0|       0|   65674 | CALL (100 + 2500 + 9000 + 25005 - 2300)
    // ---------------------------------------------------------------------------------- call (gas:2310)
    //              |         |       0|       0|      0|       0|    2310 |
    //                   .            .        .       .        .        . |
    //                   .            .        .       .        .        . |
    //            - |end-state|       0|       0|      0|       0|    1000 | (revert)
    // ---------------------------------------------------------------------------------- call end
    //              |         |       0|        |      0|        |   64364 | => post integrate

    // CALL result
    host.call_result.status_code = EVMC_REVERT;
    host.call_result.gas_left = 1000;
    host.call_result.gas_refund = 0;
    host.call_result.storage_gas_consumed = 0;
    host.call_result.storage_gas_refund = 0;
    host.call_result.speculative_cpu_gas_consumed = 0;

    execute(100000, code);

    const auto gas_left_ = 65674 - (2310 - 1000); //64364
    const auto gas_used_ = 100000 - gas_left_;

    EXPECT_GAS_USED(EVMC_SUCCESS, gas_used_);
    EXPECT_EQ(result.gas_left, gas_left_);
    EXPECT_EQ(result.gas_refund, 0);
    EXPECT_EQ(result.storage_gas_consumed, 25005);
    EXPECT_EQ(result.storage_gas_refund, 0);
    EXPECT_EQ(result.speculative_cpu_gas_consumed, 6700);

    const auto real_cpu_consumed = (gas_used_ - result.storage_gas_consumed) - result.speculative_cpu_gas_consumed;
    EXPECT_EQ(real_cpu_consumed, 3*7 + 100 + 2500 + (2310 - 1000) );
}

TEST_P(evm, eos_evm_test_apply_discount_factor)
{
    evmone::gas_parameters non_scaled {
        1000,  //G_txnewaccount
        2000,  //G_newaccount
        3000,  //G_txcreate
        4000,  //G_codedeposit
        5000   //G_sset
    };

    intx::uint256 factor_num{1};
    intx::uint256 factor_den{2};

    auto scaled = gas_parameters::apply_discount_factor(factor_num, factor_den, non_scaled);

    EXPECT_EQ(scaled.G_txnewaccount,  500);
    EXPECT_EQ(scaled.G_newaccount  , 1000);
    EXPECT_EQ(scaled.G_txcreate    , 1500);
    EXPECT_EQ(scaled.G_codedeposit , 2000);
    EXPECT_EQ(scaled.G_sset        , 2500);
}
