// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "arith_uint256.h"
#include "chain.h"
#include "primitives/block.h"
#include "uint256.h"

static const int64_t DGWPastBlocksMax = 24;

const CBlockIndex* GetPrevPoS(const CBlockIndex* pindexLast, const Consensus::Params& params)  {
    auto cur = pindexLast;
    while (cur != nullptr && cur->nHeight % 10) {
        cur = cur->pprev;
    }
    return cur->nHeight > params.posHeight ? cur : nullptr;
}

static unsigned int DarkGravityWave(const CBlockIndex* pindexLast, const Consensus::Params& params, bool isHardfork) {
    /* current difficulty formula, dash - DarkGravity v3, written by Evan Duffield - evan@dash.org */
    if (pindexLast == nullptr) return UintToArith256(params.powLimit).GetCompact();
    const CBlockIndex *BlockLastSolved = pindexLast;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = 24;
    int64_t PastBlocksMax = DGWPastBlocksMax;
    int64_t CountBlocks = 0;
    arith_uint256 PastDifficultyAverage;
    arith_uint256 PastDifficultyAveragePrev;

    bool isNextPoS = !((pindexLast->nHeight + 1) % 10);
    bool isPoSPeriod = (pindexLast->nHeight) > params.posHeight;

    auto limit = isNextPoS ? params.posLimit : params.powLimit;

    if (BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin || 
        (isPoSPeriod && isNextPoS && (BlockLastSolved->nHeight - params.posHeight) / 10 < PastBlocksMin)) {
        return UintToArith256(limit).GetCompact();
    }


    if (isPoSPeriod && isNextPoS) {BlockLastSolved = GetPrevPoS(pindexLast, params); }
    const CBlockIndex *BlockReading = BlockLastSolved;
    

    for (unsigned int i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
        if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
        CountBlocks++;

        if(CountBlocks <= PastBlocksMin) {
            if (CountBlocks == 1) { PastDifficultyAverage.SetCompact(BlockReading->nBits); }
            else { PastDifficultyAverage = ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) / (CountBlocks + 1); }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (isPoSPeriod) {
            if (isNextPoS) {
                auto prevPoS = GetPrevPoS(BlockReading->pprev, params);
                nActualTimespan += prevPoS != nullptr ? prevPoS->GetBlockTime() - prevPoS->pprev->GetBlockTime() : 0;    
            } else {
                nActualTimespan += BlockReading->GetBlockTime() - BlockReading->pprev->GetBlockTime();
            }
        } else {
            nActualTimespan += LastBlockTime > 0 ? LastBlockTime - BlockReading->GetBlockTime() : 0;
            LastBlockTime = BlockReading->GetBlockTime();
            if (BlockReading->pprev == nullptr) { break; }
        }

        if (BlockReading->pprev == NULL) { assert(BlockReading); break; }
        BlockReading = isPoSPeriod ? 
                        isNextPoS ? GetPrevPoS(BlockReading->pprev, params) :
                        BlockReading->pprev->IsProofOfStake() ? BlockReading->pprev->pprev : BlockReading->pprev 
                                    : BlockReading->pprev;
    }

    arith_uint256 bnNew(PastDifficultyAverage);

    const auto targetSpacing = isHardfork ? isNextPoS && isPoSPeriod ? params.nPosTargetSpacing : params.nPowTargetSpacing : params.nBtcPowTargetSpacing;
    int64_t _nTargetTimespan = (CountBlocks ? CountBlocks : 1) * targetSpacing;

    if (nActualTimespan < _nTargetTimespan/3)
        nActualTimespan = _nTargetTimespan/3;
    if (nActualTimespan > _nTargetTimespan*3)
        nActualTimespan = _nTargetTimespan*3;

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > UintToArith256(limit)){
        bnNew = UintToArith256(limit);
    }

    return bnNew.GetCompact();
}

static unsigned int BitcoinNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params, bool isHardfork)
{
    // Go back by what we want to be 14 days worth of blocks
    const auto difficultyAdjustmentInterval = isHardfork ? params.DifficultyAdjustmentInterval() : params.BitcoinDifficultyAdjustmentInterval();
    int nHeightFirst = pindexLast->nHeight - (difficultyAdjustmentInterval-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();
    if (pindexLast->nHeight + 1 >= params.hardforkHeight &&
        pindexLast->nHeight + 1 < params.hardforkHeight + DGWPastBlocksMax)
        return nProofOfWorkLimit;
    if (params.fPowNoRetargeting) return pindexLast->nBits;

    const auto isHardfork = pindexLast->nHeight + 1 >= params.hardforkHeight;
    const auto difficultyAdjustmentInterval = isHardfork
        ? params.DifficultyAdjustmentInterval()
        : params.BitcoinDifficultyAdjustmentInterval();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % difficultyAdjustmentInterval != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            const auto powTargetSpacing = isHardfork ? params.nPowTargetSpacing : params.nBtcPowTargetSpacing;
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + powTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % difficultyAdjustmentInterval != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    return pblock->IsBitcoinX()
        ? DarkGravityWave(pindexLast, params, isHardfork)
        : BitcoinNextWorkRequired(pindexLast, pblock, params, isHardfork);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
