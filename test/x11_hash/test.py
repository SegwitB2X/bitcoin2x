import x11_hash
from binascii import unhexlify, hexlify

import unittest

header_hex = ("02000000" +
    "b67a40f3cd5804437a108f105533739c37e6229bc1adcab385140b59fd0f0000" +
    "a71c1aade44bf8425bec0deb611c20b16da3442818ef20489ca1e2512be43eef"
    "814cdb52" +
    "f0ff0f1e" +
    "dbf70100")

best_hash = '434341c0ecf9a2b4eec2644cfadf4d0a07830358aed12d0ed654121dd9070000'

class TestSequenceFunctions(unittest.TestCase):

    def setUp(self):
        self.block_header = unhexlify(header_hex)
        self.best_hash = best_hash

    def test_x11_hash(self):
        self.pow_hash = hexlify(x11_hash.getPoWHash(self.block_header))
        self.assertEqual(self.pow_hash.decode(), self.best_hash)


if __name__ == '__main__':
    unittest.main()

