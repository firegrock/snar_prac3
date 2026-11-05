"""Wire v1. Explicit LE, IEEE754 float32, CRC-32/ISO-HDLC; no C ABI."""
import json
import math
import struct
import zlib
from dataclasses import dataclass

HEADER = struct.Struct("<2sBBHHQQQI")
SAMPLE = struct.Struct("<IQ7h4fI")
MAX_PACKET = 6184
MAX_COMMAND = 4096
COLUMNS = ["seq", "t_us", "ax", "ay", "az", "gx", "gy", "gz", "temperature", "qw", "qx", "qy", "qz", "flags"]


@dataclass
class Packet:
    kind: int
    delivery: int
    uid: str
    boot: str
    stream: str
    config: int
    payload: bytes

    @property
    def key(self):
        return self.uid, self.boot, self.stream

    def samples(self):
        if self.kind != 1 or len(self.payload) < 2:
            raise ValueError("Not a sample packet")
        n, = struct.unpack_from("<H", self.payload)
        if not 1 <= n <= 8 or len(self.payload) != 2 + n * SAMPLE.size:
            raise ValueError("Invalid sample count/size")
        result = [SAMPLE.unpack_from(self.payload, 2 + i*SAMPLE.size) for i in range(n)]
        if any(not all(math.isfinite(x) for x in row[9:13]) for row in result):
            raise ValueError("Nonfinite quaternion")
        return result


def encode(kind, payload, uid=0, boot=0, stream=0, config=0, delivery=0):
    n = HEADER.size + len(payload) + 4
    if not 40 <= n <= MAX_PACKET:
        raise ValueError("Packet too long")
    data = HEADER.pack(b"SI", 1, kind, n, delivery, int(uid), int(boot), int(stream), config) + payload
    return data + struct.pack("<I", zlib.crc32(data) & 0xffffffff)


def decode(data):
    if not 40 <= len(data) <= MAX_PACKET:
        raise ValueError("Invalid packet length")
    magic, version, kind, n, delivery, uid, boot, stream, config = HEADER.unpack_from(data)
    if magic != b"SI" or version != 1 or n != len(data) or kind not in range(1, 6):
        raise ValueError("Invalid header")
    if struct.unpack_from("<I", data, n-4)[0] != zlib.crc32(data[:-4]) & 0xffffffff:
        raise ValueError("CRC mismatch")
    return Packet(kind, delivery, str(uid), str(boot), str(stream), config, data[HEADER.size:-4])


def cobs_encode(data):
    out = bytearray([0]); index = 0; code = 1
    for b in data:
        if b == 0:
            out[index] = code; index = len(out); out.append(0); code = 1
        else:
            out.append(b); code += 1
            if code == 255:
                out[index] = code; index = len(out); out.append(0); code = 1
    out[index] = code
    return bytes(out)


def cobs_decode(data):
    out = bytearray(); i = 0
    while i < len(data):
        c = data[i]; i += 1
        if not c or i+c-1 > len(data) or 0 in data[i:i+c-1]:
            raise ValueError("Invalid COBS")
        out.extend(data[i:i+c-1]); i += c-1
        if c != 255 and i < len(data):
            out.append(0)
    if len(out) > MAX_PACKET:
        raise ValueError("Decoded frame too long")
    return bytes(out)


class Framer:
    """Bounded UART resynchronizer: discard boot noise/oversized frame to next 0."""
    def __init__(self):
        self.data = bytearray(); self.discard = False; self.errors = 0

    def feed(self, data):
        packets = []
        for b in data:
            if b == 0:
                if self.data and not self.discard:
                    try:
                        packets.append(decode(cobs_decode(self.data)))
                    except ValueError:
                        self.errors += 1
                self.data.clear(); self.discard = False
            elif not self.discard:
                self.data.append(b)
                if len(self.data) > MAX_PACKET + MAX_PACKET//254 + 2:
                    self.errors += 1; self.data.clear(); self.discard = True
        return packets


def json_bytes(value):
    data = json.dumps(value, ensure_ascii=True, allow_nan=False, separators=(",", ":")).encode("utf-8")
    if len(data) > MAX_COMMAND:
        raise ValueError("Command exceeds 4096 bytes")
    return data
