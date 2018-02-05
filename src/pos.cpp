// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pos.h"

bool fStakeRun = false;
int64_t nLastCoinStakeSearchInterval = 0;
unsigned int nModifierInterval = 10 * 60;

#ifdef ENABLE_WALLET
// novacoin: attempt to generate suitable proof-of-stake
bool SignBlock(CBlock& block, CWallet& wallet)
{
    block.nVersion |= VERSIONBITS_POS;
    static int64_t nLastCoinStakeSearchTime = GetAdjustedTime(); // startup timestamp

    CKey key;
    CMutableTransaction txCoinStake;

    int64_t nSearchTime = block.nTime; // search to current time

    if (nSearchTime > nLastCoinStakeSearchTime)
    {
        if (wallet.CreateCoinStake(wallet, block.nBits, block.nTime, txCoinStake, key))
        {
            block.vtx.insert(block.vtx.begin() + 1, MakeTransactionRef(std::move(txCoinStake)));
            CMutableTransaction tx(*block.vtx[0]);
            tx.vout.pop_back();
            block.vtx[0] = MakeTransactionRef(std::move(tx));
            GenerateCoinbaseCommitment(block, chainActive.Tip(), Params().GetConsensus());
            block.hashMerkleRoot = BlockMerkleRoot(block);
            block.prevoutStake = block.vtx[1]->vin[0].prevout;

            return key.Sign(block.GetHash(), block.vchBlockSig);
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    } else {
        LogPrint(BCLog::STAKE, "%s: Already tried to sign within interval, exiting\n", __func__);
    }

    return false;
}
#endif

void ThreadStakeMiner(CWallet *pwallet)
{

    // Make this thread recognisable as the mining thread
    RenameThread("b2x-staker");
    std::shared_ptr<CReserveScript> coinbase_script;
    pwallet->GetScriptForMining(coinbase_script);

    while (fStakeRun)
    {
        while (pwallet->IsLocked()) {
            nLastCoinStakeSearchInterval = 0;
            LogPrint(BCLog::STAKE, "%s: Wallet locked, waitig\n", __func__);
            MilliSleep(10000);
        }

        while ((chainActive.Height() + 1) % 10 || chainActive.Height() < Params().GetConsensus().posHeight) {
            LogPrint(BCLog::STAKE, "%s: Waiting for PoS\n", __func__);
            MilliSleep(10000);
        }

        // while (IsInitialBlockDownload()) {
        //     LogPrint(BCLog::STAKE, "%s: Initial blockchain downloading, waitig\n", __func__);
        //     MilliSleep(10000);
        //     continue;
        // }

        // if (g_connman && (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)) {
        //     LogPrint(BCLog::STAKE, "%s: No connections, waitig\n", __func__);
        //     MilliSleep(10000);
        //     continue;
        // }

        auto pblocktemplate(BlockAssembler(Params()).CreateNewBlock(coinbase_script->reserveScript));
        
        if (!pblocktemplate.get()) {
            LogPrint(BCLog::STAKE, "%s: Failed to create block template, exiting\n", __func__);
            return;
        }
            
        std::shared_ptr<CBlock> pblock = std::make_shared<CBlock>(pblocktemplate->block);

        if (CreatePoSBlock(pblock, *pwallet)) {
                LOCK(cs_main);
                if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash()) {
                    LogPrint(BCLog::STAKE, "%s: Generated block is stale, starting over\n", __func__);
                }
                // Track how many getdata requests this block gets
                {
                    LOCK(pwallet->cs_wallet);
                    pwallet->mapRequestCount[pblock->GetHash()] = 0;
                }
                // Process this block the same as if we had received it from another node
                if (!ProcessNewBlock(Params(), pblock, true, nullptr)) {
                    LogPrint(BCLog::STAKE, "%s: block not accepted, starting over\n", __func__);
                }
        }
        else {
            LogPrint(BCLog::STAKE, "%s: Failed to create PoS block, waiting\n", __func__);
            MilliSleep(10000);
        }
        MilliSleep(500);
    }
}

bool CreatePoSBlock(std::shared_ptr<CBlock> pblock, CWallet& wallet) {
    if (!SignBlock(*pblock, wallet)) {
        LogPrint(BCLog::STAKE, "%s: failed to sign block\n", __func__);
        return false;
    }
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!CheckProofOfStake(pcoinsTip, pindexPrev->bnStakeModifierV2, pindexPrev->nHeight, pblock->nBits, pblock->nTime, pblock->vtx[1]->vin[0].prevout)) {
        LogPrint(BCLog::STAKE, "%s: check new PoS failed\n", __func__);
        return false;
    }
    return true;
}

void StakeB2X(bool fStake, CWallet *pwallet)
{
    static boost::thread_group* stakeThread = NULL;

    if (stakeThread != NULL)
    {
        stakeThread->interrupt_all();
        delete stakeThread;
        stakeThread = NULL;
    }

    if(fStake && pwallet)
	{
	    stakeThread = new boost::thread_group();
	    stakeThread->create_thread(boost::bind(&ThreadStakeMiner, pwallet));
	}
    fStakeRun = fStake;
}

using namespace std;

// Stake Modifier (hash modifier of proof-of-stake):
// The purpose of stake modifier is to prevent a txout (coin) owner from
// computing future proof-of-stake generated by this txout at the time
// of transaction confirmation. To meet kernel protocol, the txout
// must hash with a future stake modifier to generate the proof.
uint256 ComputeStakeModifierV2(const CBlockIndex* pindexPrev, const uint256& kernel)
{
    if (!pindexPrev)
        return uint256();  // genesis block's modifier is 0

    CDataStream ss(SER_GETHASH, 0);
    ss << kernel << pindexPrev->bnStakeModifierV2;
    return Hash(ss.begin(), ss.end());
}

bool IsCanonicalBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork()) {
        return block.vchBlockSig.empty();
    }

    return IsLowDERSignature(block.vchBlockSig, NULL, false);
}

bool CheckBlockSignature(const CBlock& block)
{
    if (block.IsProofOfWork())
        return block.vchBlockSig.empty();

    if (block.vchBlockSig.empty())
        return false;

    std::vector<valtype> vSolutions;
    txnouttype whichType;

    if (!IsCanonicalBlockSignature(block))
        return false;

    const CTxOut& txout = block.vtx[1]->vout[1];

    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY)
    {
        valtype& vchPubKey = vSolutions[0];
        return CPubKey(vchPubKey).Verify(block.GetHash(), block.vchBlockSig);
    }
    else
    {
        // Block signing key also can be encoded in the nonspendable output
        // This allows to not pollute UTXO set with useless outputs e.g. in case of multisig staking

        const CScript& script = txout.scriptPubKey;
        CScript::const_iterator pc = script.begin();
        opcodetype opcode;
        valtype vchPushValue;

        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (opcode != OP_RETURN)
            return false;
        if (!script.GetOp(pc, opcode, vchPushValue))
            return false;
        if (!IsCompressedOrUncompressedPubKey(vchPushValue))
            return false;
        return CPubKey(vchPushValue).Verify(block.GetHash(), block.vchBlockSig);
    }

    return false;
}


uint256 GetStakeHashProof(const COutPoint& prevout, uint32_t nTime, uint32_t nPrevTime, uint256 nStakeModifier) {
    CDataStream ss(SER_GETHASH, 0);
    ss << nStakeModifier;
    ss << nPrevTime << prevout.hash << prevout.n << nTime;
    return Hash(ss.begin(), ss.end());
}

// BlackCoin kernel protocol
// coinstake must meet hash target according to the protocol:
// kernel (input 0) must meet the formula
//     hash(nStakeModifier + txPrev.block.nTime + txPrev.nTime + txPrev.vout.hash + txPrev.vout.n + nTime) < bnTarget * nWeight
// this ensures that the chance of getting a coinstake is proportional to the
// amount of coins one owns.
// The reason this hash is chosen is the following:
//   nStakeModifier: scrambles computation to make it very difficult to precompute
//                   future proof-of-stake
//   txPrev.block.nTime: prevent nodes from guessing a good timestamp to
//                       generate transaction for future advantage,
//                       obsolete since v3
//   txPrev.vout.hash: hash of txPrev, to reduce the chance of nodes
//                     generating coinstake at the same time
//   txPrev.vout.n: output number of txPrev, to reduce the chance of nodes
//                  generating coinstake at the same time
//   nTime: current timestamp
//   block/tx hash should not be used here as they can be generated in vast
//   quantities so as to generate blocks faster, degrading the system back into
//   a proof-of-work situation.
//
bool CheckStakeKernelHash(uint256 bnStakeModifierV2, int nPrevHeight, uint32_t nBits, uint32_t nTime, uint32_t nTimeBlockFrom, const COutPoint& prevout, const CTxOut& txout)
{
    uint32_t nStakeTime = nTimeBlockFrom & ~STAKE_TIMESTAMP_MASK;

    if (nTime < nStakeTime) {
        LogPrint(BCLog::STAKE, "%s: nTime violation\n", __func__);
        return false;
    }

    // Base target
    arith_uint256 bnTarget;
    bnTarget.SetCompact(nBits);

    // Weighted target
    int64_t nValueIn = txout.nValue;
    arith_uint256 bnWeight = arith_uint256(nValueIn);
    bnTarget *= bnWeight;

    // Calculate hash
    uint256 hashProofOfStake = GetStakeHashProof(prevout, nTime, nStakeTime, bnStakeModifierV2);

    LogPrint(BCLog::STAKE, "CheckStakeKernelHash() : using block at height=%d timestamp=%s for block from timestamp=%s\n",
        nPrevHeight,
        DateTimeStrFormat(nStakeTime),
        DateTimeStrFormat(nTimeBlockFrom));
    LogPrint(BCLog::STAKE, "CheckStakeKernelHash() : check nTimeBlockFrom=%u nStakeTime=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
        nTimeBlockFrom, nStakeTime, prevout.n, nTime,
        hashProofOfStake.ToString());

    // Now check if proof-of-stake hash meets target protocol
    if (UintToArith256(hashProofOfStake) > bnTarget)
        return error("CheckStakeKernelHash() : target not met %s < %s\n", bnTarget.ToString(), hashProofOfStake.ToString());

    LogPrint(BCLog::STAKE, "CheckStakeKernelHash() : using block at height=%d timestamp=%s for block from timestamp=%s\n",
        nPrevHeight,
        DateTimeStrFormat(nStakeTime),
        DateTimeStrFormat(nTimeBlockFrom));
    LogPrint(BCLog::STAKE, "CheckStakeKernelHash() : pass nTimeBlockFrom=%u nPrevout=%u nTimeTx=%u hashProof=%s\n",
        nTimeBlockFrom, prevout.n, nTime,
        hashProofOfStake.ToString());


    return true;
}

bool CheckProofOfStake(CCoinsViewCache* view, uint256 bnStakeModifierV2, int nPrevHeight, uint32_t nBits, uint32_t nTime, const COutPoint& prevout) {
    if (!view->HaveCoin(prevout)) {
        LogPrint(BCLog::STAKE, "%s: inputs missing/spent\n", __func__);
        return false;
    }

    auto coin = view->AccessCoin(prevout);

    if (nPrevHeight + 1 - coin.nHeight < COINBASE_MATURITY) {
        LogPrint(BCLog::STAKE, "%s: tried to stake at depth %d\n", __func__, nPrevHeight + 1 - coin.nHeight);
        return false;
    }

    auto prevTime = chainActive[coin.nHeight]->nTime;

    if (nTime - prevTime < STAKE_MIN_AGE) {
        LogPrint(BCLog::STAKE, "%s: tried to stake at age %d\n", __func__, nTime - prevTime);
        return false;
    }
    if (!CheckStakeKernelHash(bnStakeModifierV2, nPrevHeight, nBits, nTime, prevTime, prevout, coin.out)) {
        LogPrint(BCLog::STAKE, "%s: check kernel failed on coinstake %s\n", __func__, prevout.hash.ToString());
        return false;
    }
    return true;
}