// Copyright (c) 2012-2013 The PPCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_POS_H
#define BITCOIN_POS_H

class CWallet;

static const int STAKE_TIMESTAMP_MASK = 15;
static const int POS_EXPECTED_HEIGHT = 501450;
static const bool DEFAULT_STAKE = false;

/** Generate a new block, without valid proof-of-work */
void StakeBitcoins(bool fStake, CWallet *pwallet);

/** Check mined proof-of-stake block */
bool CheckStake(CBlock* pblock, CWallet& wallet);


#endif // BITCOIN_POS_H