// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POS_H
#define BITCOIN_POS_H

#include "wallet/wallet.h"
#include "primitives/block.h"
#include "timedata.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "utiltime.h"
#include "util.h"
#include "net.h"
#include "miner.h"
#include "txdb.h"

#include <boost/thread.hpp>

class CBlock;

extern CChain chainActive;
bool IsInitialBlockDownload();
std::vector<unsigned char> GenerateCoinbaseCommitment(CBlock& block, const CBlockIndex* pindexPrev, const Consensus::Params& consensusParams);
bool ProcessNewBlock(const CChainParams& chainparams, const std::shared_ptr<const CBlock> pblock, bool fForceProcessing, bool* fNewBlock);

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;
static const int STAKE_TIMESTAMP_MASK = 15;
static const int POS_EXPECTED_HEIGHT = 501450;
static const int STAKE_MIN_AGE = 0; //8 hours
static const bool DEFAULT_STAKING = true;

extern bool fStakeRun;
extern int64_t nLastCoinStakeSearchInterval;
extern unsigned int nModifierInterval;
extern CCoinsViewCache *pcoinsTip;
extern CCriticalSection cs_main;

/** Check mined proof-of-stake block */
bool CheckStake(CBlock* pblock, CWallet& wallet);

bool SignBlock(CBlock& block, CWallet& wallet);

/** Generate a new block, without valid proof-of-work */
void StakeB2X(bool fStake, CWallet *pwallet);

uint256 ComputeStakeModifierV2(const CBlockIndex* pindexPrev, const uint256& kernel);

// Check whether stake kernel meets hash target
// Sets hashProofOfStake on success return
bool CheckStakeKernelHash(uint256 bnStakeModifierV2, int nPrevHeight, uint32_t nBits, uint32_t nTime, uint32_t nTimeBlockFrom, const COutPoint& prevout, const CTxOut& txout);

// Check kernel hash target and coinstake signature
// Sets hashProofOfStake on success return
bool CheckProofOfStake(CCoinsViewCache* view, uint256 bnStakeModifierV2, int nPrevHeight, uint32_t nBits, uint32_t nTime, const COutPoint& prevout);

#endif // BITCOIN_POS_H