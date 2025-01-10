// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#include <evmone/eof.hpp>
#include <gtest/gtest.h>
#include <test/utils/bytecode.hpp>
#include <test/utils/utils.hpp>

using namespace evmone;
using namespace evmone::test;

TEST(eof, is_eof_container)
{
    EXPECT_FALSE(is_eof_container(""_hex));
    EXPECT_FALSE(is_eof_container("EF"_hex));
    EXPECT_FALSE(is_eof_container("EF01"_hex));
    EXPECT_FALSE(is_eof_container("EF02"_hex));
    EXPECT_FALSE(is_eof_container("EFFF"_hex));
    EXPECT_FALSE(is_eof_container("00"_hex));
    EXPECT_FALSE(is_eof_container("FE"_hex));

    EXPECT_TRUE(is_eof_container("EF00"_hex));
    EXPECT_TRUE(is_eof_container("EF00 01 010004 0200010001 00 00000000 00"_hex));
    EXPECT_TRUE(is_eof_container("EF00 01 010004 0200010001 030004 00 00000000 00 AABBCCDD"_hex));
    EXPECT_TRUE(is_eof_container("EF00 02 ABCFEF"_hex));
}

TEST(eof, read_valid_eof1_header)
{
    struct TestCase
    {
        std::string code;
        uint16_t types_size;
        uint16_t data_size;
        std::vector<uint16_t> code_sizes;
        std::vector<uint16_t> container_sizes;
    };

    std::string code_sections_256;
    for (int i = 0; i < 255; ++i)
        code_sections_256 += "E5" + hex(big_endian(static_cast<uint16_t>(i + 1)));
    code_sections_256 += "5B5B00";

    std::string eofcreate_256;
    for (int i = 0; i < 256; ++i)
        eofcreate_256 += "5F5F5F5FEC" + hex(static_cast<uint8_t>(i)) + "50";
    eofcreate_256 += "00";
    const auto eofcreate_256_size = static_cast<uint16_t>(eofcreate_256.size() / 2);

    const TestCase test_cases[] = {
        {"EF00 01 010004 0200010001 040000 00 00800000 00", 4, 0, {1}, {}},
        {"EF00 01 010004 0200010006 040000 00 00800002 600160005500", 4, 0, {6}, {}},
        {"EF00 01 010004 0200010001 040001 00 00800000 00 AA", 4, 1, {1}, {}},
        {"EF00 01 010004 0200010006 040004 00 00800002 600160005500 AABBCCDD", 4, 4, {6}, {}},
        {"EF00 01 01000C 020003000300030003 040000 00 008000000080000000800000 E50001 E50002 "
         "5B5B00",
            12, 0, {3, 3, 3}, {}},
        {"EF00 01 01000C 020003000300030003 040004 00 008000000080000000800000 E50001 E50002 "
         "5B5B00 FFFFFFFF",
            12, 4, {3, 3, 3}, {}},
        {"EF00 01 010004 0200010100 041000 00 00800000" + hex(255 * bytecode("5B")) + "00" +
                std::string(8192, 'F'),
            4, 4096, {256}, {}},
        {"EF00 01 010400 020100" + hex(256 * bytecode("0003")) + " 041000 00 " +
                hex(256 * bytecode("00800000")) + code_sections_256 + std::string(8192, 'F'),
            4 * 256, 4096, std::vector<uint16_t>(256, 3), {}},
        {"EF00 01 010004 0200010007 0300010014 040000 00 00800004 5F5F5F5FEC0000 "
         "EF000101000402000100010400000000800000FE",
            4, 0, {7}, {20}},
        {"EF00 01 010004 0200010015 030003001400160018 040000 00 00800004 "
         "5F5F5F5FEC00505F5F5F5FEC01505F5F5F5FEC0200 "
         "EF000101000402000100010400000000800000FE EF0001010004020001000304000000008000025F5FFD "
         "EF00010100040200010005040000000080000260015F55FE",
            4, 0, {21}, {20, 22, 24}},
        {"EF00 01 010004 0200010015 030003001400160018 040003 00 00800004 "
         "5F5F5F5FEC00505F5F5F5FEC01505F5F5F5FEC0200 "
         "EF000101000402000100010400000000800000FE EF0001010004020001000304000000008000025F5FFD "
         "EF00010100040200010005040000000080000260015F55FE ddeeff",
            4, 3, {21}, {20, 22, 24}},
        {"EF00 01 01000C 020003000300030015 030003001400160018 040003 00 008000000080000000800004 "
         "E50001 E50002 5F5F5F5FEC00505F5F5F5FEC01505F5F5F5FEC0200 "
         "EF000101000402000100010400000000800000FE "
         "EF0001010004020001000304000000008000025F5FFD "
         "EF00010100040200010005040000000080000260015F55FE ddeeff",
            12, 3, {3, 3, 21}, {20, 22, 24}},
        {"EF00 01 010004 020001" + hex(big_endian(eofcreate_256_size)) + "030100" +
                hex(256 * bytecode("0014")) + "040000 00 00800004" + eofcreate_256 +
                hex(256 * bytecode("EF000101000402000100010400000000800000FE")),
            4, 0, {eofcreate_256_size}, std::vector<uint16_t>(256, 20)},
    };

    for (const auto& test_case : test_cases)
    {
        const auto code = from_spaced_hex(test_case.code).value();
        EXPECT_EQ(
            validate_eof(EVMC_PRAGUE, ContainerKind::runtime, code), EOFValidationError::success)
            << test_case.code;

        const auto header = read_valid_eof1_header(code);
        EXPECT_EQ(header.code_sizes, test_case.code_sizes) << test_case.code;
        EXPECT_EQ(header.data_size, test_case.data_size) << test_case.code;
        EXPECT_EQ(header.types.size() * 4, test_case.types_size) << test_case.code;
        EXPECT_EQ(header.container_sizes, test_case.container_sizes) << test_case.code;
    }
}

TEST(eof, get_error_message)
{
    EXPECT_EQ(evmone::get_error_message(EOFValidationError::success), "success");
    EXPECT_EQ(evmone::get_error_message(EOFValidationError::invalid_prefix), "invalid_prefix");
    EXPECT_EQ(evmone::get_error_message(EOFValidationError::stack_overflow), "stack_overflow");
    EXPECT_EQ(evmone::get_error_message(EOFValidationError::impossible), "impossible");

    // NOLINTNEXTLINE(*.EnumCastOutOfRange)
    EXPECT_EQ(evmone::get_error_message(static_cast<EOFValidationError>(-1)), "<unknown>");
}
