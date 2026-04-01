#include <gtest/gtest.h>

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "utiltest.h"

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
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    ASSERT_TRUE(params.nPowAllowMinDifficultyBlocksAfterHeight != boost::none);

    size_t lastBlk = 2 * params.nPowAveragingWindow;
    // Build a stable chain of equal-difficulty blocks ending immediately before
    // the min-difficulty activation height.
    std::vector<CBlockIndex> blocks(lastBlk + 1);
    int activation = params.nPowAllowMinDifficultyBlocksAfterHeight.get();
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = activation - static_cast<int>(lastBlk) + i - 1;
        blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(blocks[i].nHeight) : 1269211443;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }
    EXPECT_EQ(blocks[lastBlk].nHeight, activation - 1);

    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
    bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);

    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1) * 6 + 1;

    // One block before activation, a long timestamp gap should still use the
    // normal retarget path.
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // At activation height, the same timestamp gap should trigger emergency
    // minimum-difficulty.
    blocks[lastBlk].nHeight = activation;
    unsigned int powLimitCompact = UintToArith256(params.powLimit).GetCompact();
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), powLimitCompact);

    // The rule should be temporary: a timely follow-up block returns to normal
    // retarget logic.
    CBlockIndex emergencyBlock;
    emergencyBlock.pprev = &blocks[lastBlk];
    emergencyBlock.nHeight = blocks[lastBlk].nHeight + 1;
    emergencyBlock.nTime = next.nTime;
    emergencyBlock.nBits = powLimitCompact;
    emergencyBlock.nChainWork = blocks[lastBlk].nChainWork + GetBlockProof(blocks[lastBlk]);

    CBlockHeader timely;
    timely.nTime = emergencyBlock.nTime + params.PoWTargetSpacing(emergencyBlock.nHeight + 1);
    EXPECT_NE(GetNextWorkRequired(&emergencyBlock, &timely, params), powLimitCompact);
}
