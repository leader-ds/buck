#include <gtest/gtest.h>

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "utiltest.h"

namespace {

std::vector<CBlockIndex> BuildFixedDifficultyChain(const Consensus::Params& params, int startHeight)
{
    size_t lastBlk = 2 * params.nPowAveragingWindow;
    std::vector<CBlockIndex> blocks(lastBlk + 1);
    for (size_t i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = startHeight + i;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(blocks[i].nHeight) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }
    return blocks;
}

}

void TestDifficultyAveragingImpl(const Consensus::Params& params)
{
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(i) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += GetRand(params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1)/2) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
}

TEST(PoW, DifficultyAveraging) {
    SelectParams(CBaseChainParams::MAIN);
    TestDifficultyAveragingImpl(Params().GetConsensus());
}

TEST(PoW, DifficultyAveragingBlossom) {
    TestDifficultyAveragingImpl(RegtestActivateBlossom(true));
    RegtestDeactivateBlossom();
}

TEST(PoW, MinDifficultyRules) {
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = params.nPowAllowMinDifficultyBlocksAfterHeight.get() + i;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(i) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Create a new block at the target spacing
    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1);

    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block up to the edge of the min-difficulty limit
    next.nTime += params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 5;

    // Result should be unchanged, modulo integer division precision loss
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block over the min-difficulty limit
    next.nTime += 1;

    // Result should be the minimum difficulty
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params),
              UintToArith256(params.powLimit).GetCompact());
}

TEST(PoW, EmergencyMinDifficultyRules) {
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus();
    std::vector<CBlockIndex> blocks = BuildFixedDifficultyChain(params, 4000000);
    const size_t lastBlk = blocks.size() - 1;

    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1);

    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);

    // 1) emergency is not configured -> no effect.
    params.nPowEmergencyMinDifficultyAfterHeight = boost::none;
    params.nPowEmergencyMinDifficultyDelayMultiplier = boost::none;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 30;
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Configure emergency rule for remaining checks.
    params.nPowEmergencyMinDifficultyAfterHeight = blocks[lastBlk].nHeight;
    params.nPowEmergencyMinDifficultyDelayMultiplier = 12;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1);

    // 2) activation height has not been reached -> no effect.
    params.nPowEmergencyMinDifficultyAfterHeight = blocks[lastBlk].nHeight + 1;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 30;
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // 3) activation reached, delay below threshold -> no effect.
    params.nPowEmergencyMinDifficultyAfterHeight = blocks[lastBlk].nHeight;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 12;
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // 4) activation reached, delay above threshold -> powLimit.
    next.nTime += 1;
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), UintToArith256(params.powLimit).GetCompact());

    // 5) baseline retarget behavior remains unchanged during normal operation.
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1);
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    const size_t firstBlk = lastBlk - static_cast<size_t>(params.nPowAveragingWindow);
    EXPECT_EQ(
        GetNextWorkRequired(&blocks[lastBlk], &next, params),
        CalculateNextWorkRequired(
            bnAvg,
            blocks[lastBlk].GetMedianTimePast(),
            blocks[firstBlk].GetMedianTimePast(),
            params,
            blocks[lastBlk].nHeight + 1));
}
