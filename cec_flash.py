#!/usr/bin/python
#
# Copyright (C) 2016 Russ Dill <russ.dill@gmail.com>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# CEC Bootloader programmer.
#
# This is a programmer for a CEC based bootloader. It programs based
# on hexfiles and currently uses a HID based CEC device. It could be
# easily adapted to the standard Linux kernel interface or other CEC
# libraries.

import cec
import cec_msg
import hexfile
import sys
import crcmod
import struct
import progress.bar

crc16 = crcmod.mkCrcFun(0x18005, 0xffff)

class cec_flasher(cec.device):
    def __init__(self, idx=0):
        super(cec_flasher, self).__init__(idx)
        self.logical_addresses(1 << 0xf)
	self.read(timeout_ms=1)

    def tx_done(self, status):
        self.response = status

    def receive_msg(self, msg, length, status):
        extended = []

        if length < len(msg):
            msg = msg[:length]
        elif length > len(msg):
            extended.append('%d/%d' % (len(msg), length))

        if status & 0x80:
            extended.append('Nack')
        if status & 0x40:
            extended.append('Overrun')
        extended = ', '.join(extended)

        (source, target, opcode, extra, args) = cec_msg.decode(msg)

        ret = cec.cec_to_str(source, target, opcode, *extra, **args)
        if extended:
            ret += ' ' + extended

    def write_sync(self, b):
        self.write(b)
        self.response = None
        while self.response is None:
            self.read()
        return self.response

    def cmd(self, cmd, b=''):
	b = struct.pack('<BBB', 0xf0, 0x89, cmd) + b
	b += struct.pack('<H', crc16(b))
        (source, target, opcode, extra, args) = cec_msg.decode(b)
        ret = cec.cec_to_str(source, target, opcode, *extra, **args)
        return self.write_sync(b)

    def ping(self):
        if not self.cmd(0):
            raise Exception('Could not detect CEC bootloader')

    def erase(self):
        if not self.cmd(3):
            raise Execption('Erase failed')

    def write_data(self, b):
        if len(b) != 8:
            raise Exception('Unhandled data size')
        retries = 10
        while retries:
            if self.cmd(5, b):
                return
            retries -= 1
        raise Exception('Write failed')

    def run(self):
        if not self.cmd(1):
            raise Execption('Run failed')

    def enter(self):
        self.write_sync('\x80\x89\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\xb1')

def pages(b, pagesize):
    out = ''
    for i in b:
        out += chr(i)
        if len(out) == pagesize:
            yield out
            out = ''
    if len(out):
        out += '\xff' * (pagesize - len(out))
        yield out
    while True:
        yield '\xff' * pagesize

def rjmp_to_addr(rjmp, base):
    opcode, = struct.unpack('<H', rjmp)

    if (opcode & 0xf000) != 0xc000:
        raise Exception('Vector table reset does not contain an rjmp')

    offset = ((opcode & 0xfff) + 1) * 2

    # Sign extend
    if offset & 0x2000:
        offset |= 0xffffe000

    return (offset + base) & 0xffffffff

def patch_rjmp(dest, base):
    offset = (dest - base) / 2 - 1
    return struct.pack('<H', 0xc000 | (offset & 0xfff))

def patch_jmp(dest):
    return struct.pack('<HH', 0x940c, dest / 2)

# Hardcoded
bootloader_start = 0xec0
flash_end = (bootloader_start & ~4095) + 4096
pagesize = 0x40


f = hexfile.load(sys.argv[1])
if len(f.segments) != 1:
    raise Exception('Can only handle continuous hexfiles')
seg = f.segments[0]
if not 0 in seg:
    raise Exception('Hexfile must contain vector table')
if seg.size > bootloader_start - 4:
    raise Exception('Too large')

dev = cec_flasher()
dev.enter()
dev.ping()
print 'Erasing'
dev.erase()
print 'Programming %d bytes' % seg.size

bar = progress.bar.Bar('Flashing', max=bootloader_start/pagesize,
		suffix='Page %(index)d/%(max)d, %(eta)ds')
for addr, page in zip(range(0, bootloader_start, pagesize), pages(seg.data, pagesize)):

    if addr == 0:
        user_reset = rjmp_to_addr(page[:2], 0)
        page = patch_rjmp(bootloader_start, flash_end) + page[2:]

    elif addr == bootloader_start - pagesize:
        page = page [:-4] + patch_jmp(user_reset)

    bar.next()
    for chunk in [page[i:i+8] for i in range(0, pagesize, 8)]:
        dev.write_data(chunk)

bar.finish()
print 'Done flashing, running'
dev.run()
