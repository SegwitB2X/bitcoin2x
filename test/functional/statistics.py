#!/usr/bin/env python3

from test_framework.test_framework import BitcoinTestFramework
from test_framework.mininode import COIN
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)
import math, time

class StatisticsTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True
        self.extra_args = [["-addrindex"]]

    def test_active_accounts(self):
        node = self.nodes[0]


    def run_test(self):
        self.test_active_accounts()

        node = self.nodes[0]
        addr_a = node.getnewaddress()
        addr_b = node.getnewaddress()
        addr_c = node.getnewaddress()

        assert_equal(0, node.countaddresseswithbalance())
        assert_equal(0, node.averagebalance())

        node.generate(1)
        assert_equal(1, node.countaddresseswithbalance())
        assert_equal(50, node.averagebalance())

        node.generatetoaddress(50, addr_a)
        assert_equal(2, node.countaddresseswithbalance())
        assert_equal(2550 / 2, node.averagebalance())

        node.generatetoaddress(50, addr_b)
        assert_equal(3, node.countaddresseswithbalance())
        assert_equal(5050 // 3, math.floor(node.averagebalance()))

        node.sendtoaddress(addr_a, 50, "", "", True)
        node.generatetoaddress(6, addr_a)
        assert_equal(2, node.countaddresseswithbalance())
        assert_equal(5350 / 2, node.averagebalance())
        assert_equal(3, node.countactiveaddresses())

        node.generatetoaddress(1, addr_c)
        self.last_block_time = int(time.time())
        assert_equal(4, node.countactiveaddresses())
        
        # 24+ hours forward, previous blocks are out of range now
        self.last_block_time += 25 * 60 * 60
        node.setmocktime(self.last_block_time)
        node.generatetoaddress(1, addr_c)
        assert_equal(1, node.countactiveaddresses())

        # Send from addr_a to addr_b
        self.last_block_time += 3 * 60 * 60
        node.setmocktime(self.last_block_time)
        node.sendtoaddress(addr_b, 50, "", "", True)
        node.generatetoaddress(1, addr_c)
        assert_equal(3, node.countactiveaddresses())

if __name__ == '__main__':
    StatisticsTest().main()
