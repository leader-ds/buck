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
    SelectParams(CBaseChainParams::MAIN);
    Consensus::Params params = Params().GetConsensus();
    const unsigned int powLimitCompact = UintToArith256(params.powLimit).GetCompact();
    const uint32_t activationHeight = 1000;
    const uint32_t emergencyDelayMultiplier = 12;
    const int64_t targetSpacing = params.PoWTargetSpacing(activationHeight + 1);

    size_t lastBlk = 2 * params.nPowAveragingWindow;

    auto BuildBlocks = [&](uint32_t baseHeight) {
        std::vector<CBlockIndex> blocks(lastBlk + 1);
        for (int i = 0; i <= lastBlk; i++) {
            blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
            blocks[i].nHeight = baseHeight + i;
            blocks[i].nTime = i ? blocks[i - 1].nTime + params.PoWTargetSpacing(blocks[i - 1].nHeight + 1) : 1269211443;
            blocks[i].nBits = 0x1e7fffff;
            blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
        }
        return blocks;
    };

    auto expectedRetarget = [&](const std::vector<CBlockIndex>& blocks) {
        arith_uint256 bnRes;
        bnRes.SetCompact(0x1e7fffff);
        bnRes /= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
        bnRes *= params.AveragingWindowTimespan(blocks[lastBlk].nHeight + 1);
        return bnRes.GetCompact();
    };

    // 1. No emergency fields configured => no effect.
    params.nPowEmergencyMinDifficultyAfterHeight = boost::none;
    params.nPowEmergencyMinDifficultyDelayMultiplier = boost::none;
    auto blocksNoEmergency = BuildBlocks(activationHeight + 50);
    CBlockHeader delayedNoEmergency;
    delayedNoEmergency.nTime = blocksNoEmergency[lastBlk].nTime + targetSpacing * emergencyDelayMultiplier + 1;
    EXPECT_EQ(GetNextWorkRequired(&blocksNoEmergency[lastBlk], &delayedNoEmergency, params),
              expectedRetarget(blocksNoEmergency));

    // Configure emergency fields for the remaining cases.
    params.nPowEmergencyMinDifficultyAfterHeight = activationHeight;
    params.nPowEmergencyMinDifficultyDelayMultiplier = emergencyDelayMultiplier;

    // 2. Configured, but before activation height => no effect.
    auto blocksBeforeActivation = BuildBlocks(activationHeight - lastBlk - 2);
    CBlockHeader delayedBeforeActivation;
    delayedBeforeActivation.nTime = blocksBeforeActivation[lastBlk].nTime + targetSpacing * emergencyDelayMultiplier + 1;
    EXPECT_EQ(GetNextWorkRequired(&blocksBeforeActivation[lastBlk], &delayedBeforeActivation, params),
              expectedRetarget(blocksBeforeActivation));

    // 3. After activation, delay <= 12 * targetSpacing => no effect.
    auto blocksAfterActivation = BuildBlocks(activationHeight + 1);
    CBlockHeader atThreshold;
    atThreshold.nTime = blocksAfterActivation[lastBlk].nTime + targetSpacing * emergencyDelayMultiplier;
    EXPECT_EQ(GetNextWorkRequired(&blocksAfterActivation[lastBlk], &atThreshold, params),
              expectedRetarget(blocksAfterActivation));

    // 4. After activation, delay > 12 * targetSpacing => powLimit.
    CBlockHeader overThreshold;
    overThreshold.nTime = blocksAfterActivation[lastBlk].nTime + targetSpacing * emergencyDelayMultiplier + 1;
    EXPECT_EQ(GetNextWorkRequired(&blocksAfterActivation[lastBlk], &overThreshold, params), powLimitCompact);

    // 5. Block after an emergency block, mined on time => back to normal retarget.
    blocksAfterActivation[lastBlk].nBits = powLimitCompact;
    CBlockHeader afterEmergencyOnTime;
    afterEmergencyOnTime.nTime = blocksAfterActivation[lastBlk].nTime + targetSpacing;
    EXPECT_NE(GetNextWorkRequired(&blocksAfterActivation[lastBlk], &afterEmergencyOnTime, params), powLimitCompact);
}
