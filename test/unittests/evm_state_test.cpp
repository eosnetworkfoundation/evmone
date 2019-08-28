// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2019 The evmone Authors.
// Licensed under the Apache License, Version 2.0.

/// This file contains EVM unit tests that access or modify Ethereum state
/// or other kind of external execution context.

#include "evm_fixture.hpp"
#include <test/utils/bytecode.hpp>

using namespace evmc::literals;
using evm_state = evm;

TEST_F(evm_state, code)
{
    // CODESIZE 2 0 CODECOPY RETURN(0,9)
    auto s = "38600260003960096000f3";
    execute(s);
    EXPECT_EQ(gas_used, 23);
    ASSERT_EQ(result.output_size, 9);
    auto a = from_hex({&s[4], 18});
    EXPECT_EQ(bytes(&result.output_data[0], 9), a);
}

TEST_F(evm_state, storage)
{
    accounts[msg.destination] = {};
    const auto code = sstore(0xee, 0xff) + sload(0xee) + mstore8(0) + ret(0, 1);
    execute(100000, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 99776 - 20000);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0xff);
}

TEST_F(evm_state, sstore_pop_stack)
{
    accounts[msg.destination] = {};
    execute(100000, "60008060015560005360016000f3");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 1);
    EXPECT_EQ(result.output_data[0], 0);
}

TEST_F(evm_state, sload_cost_pre_tangerine_whistle)
{
    rev = EVMC_HOMESTEAD;
    const auto& account = accounts[msg.destination];
    execute(56, "60008054");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    EXPECT_EQ(account.storage.size(), 0);
}

TEST_F(evm_state, sstore_cost)
{
    auto& storage = accounts[msg.destination].storage;

    auto v1 = evmc::bytes32{};
    v1.bytes[31] = 1;

    auto revs = {EVMC_BYZANTIUM, EVMC_CONSTANTINOPLE, EVMC_PETERSBURG};
    for (auto r : revs)
    {
        rev = r;

        // Added:
        storage.clear();
        execute(20006, sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        storage.clear();
        execute(20005, sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

        // Deleted:
        storage.clear();
        storage[v1] = v1;
        execute(5006, sstore(1, push(0)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        storage[v1] = v1;
        execute(5005, sstore(1, push(0)));
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

        // Modified:
        storage.clear();
        storage[v1] = v1;
        execute(5006, sstore(1, push(2)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        storage[v1] = v1;
        execute(5005, sstore(1, push(2)));
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

        // Unchanged:
        storage.clear();
        storage[v1] = v1;
        execute(sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 206 : 5006);
        execute(205, sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);

        // Added & unchanged:
        storage.clear();
        execute(sstore(1, push(1)) + sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 20212 : 25012);

        // Modified again:
        storage.clear();
        storage[v1] = {v1, true};
        execute(sstore(1, push(2)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 206 : 5006);

        // Added & modified again:
        storage.clear();
        execute(sstore(1, push(1)) + sstore(1, push(2)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 20212 : 25012);

        // Modified & modified again:
        storage.clear();
        storage[v1] = v1;
        execute(sstore(1, push(2)) + sstore(1, push(3)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 5212 : 10012);

        // Modified & modified again back to original:
        storage.clear();
        storage[v1] = v1;
        execute(sstore(1, push(2)) + sstore(1, push(1)));
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(gas_used, rev == EVMC_CONSTANTINOPLE ? 5212 : 10012);
    }
}

TEST_F(evm_state, tx_context)
{
    tx_context.block_timestamp = 0xdd;
    tx_context.block_coinbase.bytes[1] = 0xcc;
    tx_context.block_number = 0x1100;
    tx_context.block_difficulty.bytes[1] = 0xdd;
    tx_context.block_gas_limit = 0x990000;
    tx_context.tx_gas_price.bytes[2] = 0x66;
    tx_context.tx_origin.bytes[2] = 0x55;

    std::string s;
    s += "4241173a17";        // TIMESTAMP COINBASE OR GASPRICE OR
    s += "4317441745173217";  // NUMBER OR DIFFICULTY OR GASLIMIT OR ORIGIN OR
    s += "600052";            // m[0..] =
    s += "60206000f3";        // RETURN(0,32)
    execute(47, s);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[31], 0xdd);
    EXPECT_EQ(result.output_data[30], 0x11);
    EXPECT_EQ(result.output_data[29], 0x99);
    EXPECT_EQ(result.output_data[14], 0x55);
    EXPECT_EQ(result.output_data[13], 0xcc);
    EXPECT_EQ(result.output_data[2], 0x66);
    EXPECT_EQ(result.output_data[1], 0xdd);
}

TEST_F(evm_state, balance)
{
    accounts[msg.destination].set_balance(0x0504030201);
    auto code = bytecode{} + OP_ADDRESS + OP_BALANCE + mstore(0) + ret(32 - 6, 6);
    execute(417, code);
    EXPECT_GAS_USED(EVMC_SUCCESS, 417);
    ASSERT_EQ(result.output_size, 6);
    EXPECT_EQ(result.output_data[0], 0);
    EXPECT_EQ(result.output_data[1], 0x05);
    EXPECT_EQ(result.output_data[2], 0x04);
    EXPECT_EQ(result.output_data[3], 0x03);
    EXPECT_EQ(result.output_data[4], 0x02);
    EXPECT_EQ(result.output_data[5], 0x01);
}

TEST_F(evm_state, log)
{
    for (auto op : {OP_LOG0, OP_LOG1, OP_LOG2, OP_LOG3, OP_LOG4})
    {
        const auto n = op - OP_LOG0;
        const auto code =
            push(1) + push(2) + push(3) + push(4) + mstore8(2, 0x77) + push(2) + push(2) + op;
        recorded_logs.clear();
        execute(code);
        EXPECT_GAS_USED(EVMC_SUCCESS, 421 + n * 375);
        ASSERT_EQ(recorded_logs.size(), 1);
        const auto& last_log = recorded_logs.back();
        ASSERT_EQ(last_log.data.size(), 2);
        EXPECT_EQ(last_log.data[0], 0x77);
        EXPECT_EQ(last_log.data[1], 0);
        ASSERT_EQ(last_log.topics.size(), n);
        for (size_t i = 0; i < static_cast<size_t>(n); ++i)
        {
            EXPECT_EQ(last_log.topics[i].bytes[31], 4 - i);
        }
    }
}

TEST_F(evm_state, log0_empty)
{
    auto code = push(0) + OP_DUP1 + OP_LOG0;
    execute(code);
    ASSERT_EQ(recorded_logs.size(), 1);
    const auto& last_log = recorded_logs.back();
    EXPECT_EQ(last_log.topics.size(), 0);
    EXPECT_EQ(last_log.data.size(), 0);
}

TEST_F(evm_state, log_data_cost)
{
    for (auto op : {OP_LOG0, OP_LOG1, OP_LOG2, OP_LOG3, OP_LOG4})
    {
        auto num_topics = op - OP_LOG0;
        auto code = push(0) + (4 * OP_DUP1) + push(1) + push(0) + op;
        auto cost = 407 + num_topics * 375;
        EXPECT_EQ(recorded_logs.size(), 0);
        execute(cost, code);
        EXPECT_EQ(result.status_code, EVMC_SUCCESS);
        EXPECT_EQ(recorded_logs.size(), 1);
        recorded_logs.clear();

        EXPECT_EQ(recorded_logs.size(), 0);
        execute(cost - 1, code);
        EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
        EXPECT_EQ(recorded_logs.size(), 0) << to_name(op);
        recorded_logs.clear();
    }
}

TEST_F(evm_state, selfdestruct)
{
    rev = EVMC_SPURIOUS_DRAGON;
    execute("6009ff");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 5003);
    ASSERT_EQ(recorded_selfdestructs.size(), 1);
    EXPECT_EQ(recorded_selfdestructs.back().beneficiary.bytes[19], 9);

    rev = EVMC_HOMESTEAD;
    execute("6007ff");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 3);
    ASSERT_EQ(recorded_selfdestructs.size(), 2);
    EXPECT_EQ(recorded_selfdestructs.back().beneficiary.bytes[19], 7);

    rev = EVMC_TANGERINE_WHISTLE;
    execute("6008ff");
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 30003);
    ASSERT_EQ(recorded_selfdestructs.size(), 3);
    EXPECT_EQ(recorded_selfdestructs.back().beneficiary.bytes[19], 8);
}

TEST_F(evm_state, selfdestruct_with_balance)
{
    auto code = "6000ff";
    msg.destination.bytes[0] = 1;
    accounts[msg.destination].set_balance(1);

    rev = EVMC_TANGERINE_WHISTLE;
    execute(30003, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);

    execute(30002, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
    EXPECT_EQ(result.gas_left, 0);

    rev = EVMC_HOMESTEAD;
    execute(3, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);

    accounts[{}] = {};

    rev = EVMC_TANGERINE_WHISTLE;
    execute(5003, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);

    rev = EVMC_HOMESTEAD;
    execute(3, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(result.gas_left, 0);
}


TEST_F(evm_state, blockhash)
{
    blockhash.bytes[13] = 0x13;

    tx_context.block_number = 0;
    auto code = "60004060005260206000f3";
    execute(code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 38);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[13], 0);
    EXPECT_EQ(recorded_blockhashes.size(), 0);

    tx_context.block_number = 257;
    execute(code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 38);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[13], 0);
    EXPECT_EQ(recorded_blockhashes.size(), 0);

    tx_context.block_number = 256;
    execute(code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(gas_used, 38);
    ASSERT_EQ(result.output_size, 32);
    EXPECT_EQ(result.output_data[13], 0x13);
    ASSERT_EQ(recorded_blockhashes.size(), 1);
    EXPECT_EQ(recorded_blockhashes.back(), 0);
}

TEST_F(evm_state, extcode)
{
    auto addr = evmc_address{};
    std::fill(std::begin(addr.bytes), std::end(addr.bytes), uint8_t{0xff});
    addr.bytes[19]--;

    accounts[addr].code = {'a', 'b', 'c', 'd'};

    auto code = std::string{};
    code += "6002600003803b60019003";  // S = EXTCODESIZE(-2) - 1
    code += "90600080913c";            // EXTCODECOPY(-2, 0, 0, S)
    code += "60046000f3";              // RETURN(0, 4)

    execute(code);
    EXPECT_EQ(gas_used, 1445);
    ASSERT_EQ(result.output_size, 4);
    EXPECT_EQ(bytes_view(result.output_data, 3), bytes_view(accounts[addr].code.data(), 3));
    EXPECT_EQ(result.output_data[3], 0);
    ASSERT_EQ(recorded_account_accesses.size(), 2);
    EXPECT_EQ(recorded_account_accesses[0].bytes[19], 0xfe);
    EXPECT_EQ(recorded_account_accesses[1].bytes[19], 0xfe);
}

TEST_F(evm_state, extcodehash)
{
    auto& hash = accounts[{}].codehash;
    std::fill(std::begin(hash.bytes), std::end(hash.bytes), uint8_t{0xee});

    auto code = "60003f60005260206000f3";

    rev = EVMC_BYZANTIUM;
    execute(code);
    EXPECT_EQ(result.status_code, EVMC_UNDEFINED_INSTRUCTION);

    rev = EVMC_CONSTANTINOPLE;
    execute(code);
    EXPECT_EQ(gas_used, 418);
    ASSERT_EQ(result.output_size, 32);
    auto expected_hash = bytes(32, 0xee);
    EXPECT_EQ(bytes_view(result.output_data, result.output_size),
        bytes_view(std::begin(hash.bytes), std::size(hash.bytes)));
}

TEST_F(evm_state, codecopy_empty)
{
    execute(push(0) + 2 * OP_DUP1 + OP_CODECOPY + OP_MSIZE + ret_top());
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
}

TEST_F(evm_state, extcodecopy_empty)
{
    execute(push(0) + 3 * OP_DUP1 + OP_EXTCODECOPY + OP_MSIZE + ret_top());
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    EXPECT_EQ(std::count(result.output_data, result.output_data + result.output_size, 0), 32);
}

TEST_F(evm_state, codecopy_memory_cost)
{
    auto code = push(1) + push(0) + push(0) + OP_CODECOPY;
    execute(18, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(17, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_state, extcodecopy_memory_cost)
{
    auto code = push(1) + push(0) + 2 * OP_DUP1 + OP_EXTCODECOPY;
    execute(718, code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    execute(717, code);
    EXPECT_EQ(result.status_code, EVMC_OUT_OF_GAS);
}

TEST_F(evm_state, extcodecopy_nonzero_index)
{
    constexpr auto addr = 0x000000000000000000000000000000000000000a_address;
    constexpr auto index = 15;

    auto& extcode = accounts[addr].code;
    extcode.assign(16, 0x00);
    extcode[index] = 0xc0;
    auto code = push(2) + push(index) + push(0) + push(0xa) + OP_EXTCODECOPY + ret(0, 2);
    EXPECT_EQ(code.length() + 1, index);
    execute(code);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[0], 0xc0);
    EXPECT_EQ(result.output_data[1], 0);
    ASSERT_EQ(recorded_account_accesses.size(), 1);
    EXPECT_EQ(recorded_account_accesses.back().bytes[19], 0xa);
}

TEST_F(evm_state, extcodecopy_fill_tail)
{
    auto addr = evmc_address{};
    addr.bytes[19] = 0xa;

    auto& extcode = accounts[addr].code;
    extcode = {0xff, 0xfe};
    extcode.resize(1);
    auto code = push(2) + push(0) + push(0) + push(0xa) + OP_EXTCODECOPY + ret(0, 2);
    execute(code);
    ASSERT_EQ(recorded_account_accesses.size(), 1);
    EXPECT_EQ(recorded_account_accesses.back().bytes[19], 0xa);
    EXPECT_EQ(result.status_code, EVMC_SUCCESS);
    ASSERT_EQ(result.output_size, 2);
    EXPECT_EQ(result.output_data[0], 0xff);
    EXPECT_EQ(result.output_data[1], 0);
}

TEST_F(evm_state, extcodecopy_buffer_overflow)
{
    const auto code = bytecode{} + OP_NUMBER + OP_TIMESTAMP + OP_CALLDATASIZE + OP_ADDRESS +
                      OP_EXTCODECOPY + ret(OP_CALLDATASIZE, OP_NUMBER);

    accounts[msg.destination].code = code;

    const auto s = static_cast<int>(code.size());
    const auto values = {0, 1, s - 1, s, s + 1, 5000};
    for (auto offset : values)
    {
        for (auto size : values)
        {
            tx_context.block_timestamp = offset;
            tx_context.block_number = size;

            execute(code);
            EXPECT_STATUS(EVMC_SUCCESS);
            EXPECT_EQ(result.output_size, size);
        }
    }
}