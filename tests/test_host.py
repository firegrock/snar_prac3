import csv
import json
import os
from pathlib import Path
import random
import struct
import tempfile
import unittest
from snar_imu.protocol import *
from snar_imu.storage import RunWriter, validate_run, compare_runs
from snar_imu.client import Client, IdentityError


def meta():
    return dict(device_id=1, device_uid="123", boot_id="456", stream_id="789", config_id=1,
                config=dict(sample_rate_hz=100, accel_lsb_per_g=8192, gyro_lsb_per_dps=65.5, g0=9.80665, Kp=2, Ki=0, use_gyro_bias=True, R_BS=[1,0,0,0,1,0,0,0,1]),
                calibration=dict(gyro_bias=[0]*3,accel_bias=[0]*3,accel_scale=[1]*3),
                initial_state=dict(before_seq=0,q=[1,0,0,0],integral=[0]*3,previous_t_us="0",ready=True))


def samples(start=0, n=8, replay=0, boot=456, stream=789, uid=123):
    rows = [(i, (i+1)*10000, 0, 0, 8192, -1, 2, -3, 0, 1., 0., 0., 0., 0) for i in range(start, start+n)]
    return decode(encode(1, struct.pack("<H", n)+b"".join(SAMPLE.pack(*r) for r in rows), uid, boot, stream, 1, replay))


class CodecTests(unittest.TestCase):
    def test_c_vector(self):
        p = decode(Path("tests/out/vector.bin").read_bytes())
        self.assertEqual(p.uid, str(0xfedcba9876543210)); self.assertEqual(p.boot, str(2**64-1))
        row = p.samples()[0]; self.assertEqual(row[:5], (0x12345678, 2**53+1, -32768, 32767, -1))
        self.assertEqual(zlib.crc32(b"123456789"), 0xcbf43926)
        self.assertEqual(encode(p.kind, p.payload, p.uid, p.boot, p.stream, p.config, p.delivery), Path("tests/out/vector.bin").read_bytes())

    def test_cobs_noise_crc_lengths(self):
        rng = random.Random(42)
        for n in [0,1,254,255,500,MAX_PACKET]:
            b = bytes(rng.randrange(256) for _ in range(n)); self.assertEqual(cobs_decode(cobs_encode(b)), b)
        data = encode(5, b"{}", 1, 2, 3)
        for b in (data[:-1], data+b"x", b"x"*7000, data[:-4]+b"\0"*4):
            with self.assertRaises(ValueError): decode(b)
        framer = Framer(); wire = cobs_encode(data)+b"\0"
        packets = framer.feed(b"ROM BOOT garbage\0" + wire[:-1] + wire + wire)
        self.assertEqual(len(packets), 1); self.assertGreaterEqual(framer.errors, 2)
        self.assertEqual(len(framer.feed(b"x"*20000+b"\0"+wire)), 1)
        for byte in wire:
            framer.feed(bytes([byte]))
        with self.assertRaises(ValueError): json_bytes({"x": float("nan")})
        with self.assertRaises(ValueError): decode(encode(1,b"\0\0")).samples()


class StorageTests(unittest.TestCase):
    def test_dedup_recovery_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp)/"a"; w = RunWriter(a, meta(), "usb")
            w.packet(samples()); w.packet(samples(0,8,1)); w.packet(samples(8,8,1))
            with self.assertRaises(ValueError): w.packet(samples(0,8,boot=999))
            with self.assertRaises(ValueError): w.packet(samples(0,8,uid=124))
            with self.assertRaises(ValueError): w.packet(samples(0,8,stream=999))
            w.close(); q=json.loads((a/"quality.json").read_text()); self.assertEqual(q["unique_samples"],16);self.assertEqual(q["duplicates"],8);self.assertEqual(q["recovered"],8)
            with self.assertRaises(FileExistsError): RunWriter(a,meta(),"wifi")
            b=Path(tmp)/"b";w=RunWriter(b,meta(),"wifi");w.packet(samples());w.packet(samples(8));w.close()
            self.assertEqual(compare_runs(a,b)["result"],"PASS")

    def test_gap_and_order_bounded_export(self):
        with tempfile.TemporaryDirectory() as tmp:
            a=Path(tmp)/"a";w=RunWriter(a,meta(),"usb");w.packet(samples(16));w.packet(samples(0));w.close()
            q=validate_run(a);self.assertEqual(q["irrecoverable_missing_samples"],8)
            with (a/"samples.csv").open() as f: rows=list(csv.reader(f))
            self.assertEqual(int(rows[1][0]),0)

    def test_initial_gap_and_device_counters(self):
        with tempfile.TemporaryDirectory() as tmp:
            m=meta();m["recording_start_seq"]=0
            a=Path(tmp)/"a";w=RunWriter(a,m,"usb");w.packet(samples(16));w.event("DEVICE_STATUS",status=dict(resyncs=2,read_errors=1));w.event("DEVICE_STATUS",status=dict(resyncs=3,read_errors=2));w.close()
            q=validate_run(a);self.assertEqual(q["irrecoverable_missing_samples"],16);self.assertEqual(q["device_counter_delta_observed"]["resyncs"],1)


class FakeLink:
    name="test"
    def __init__(self, uid="123", device_id=1): self.uid=uid;self.device_id=device_id;self.queue=[];self.closed=False;self.sent=[]
    def send(self, value):
        self.sent.append(value)
        self.queue.append(dict(request_id=value["request_id"], ok=True,result=dict(device_uid=self.uid,device_id=self.device_id,boot_id="1",stream_id="2")))
    def recv(self, timeout): return self.queue.pop(0) if self.queue else None
    def close(self): self.closed=True


class IdentityTests(unittest.TestCase):
    def test_no_mutations_before_handshake(self):
        link=FakeLink("999");c=Client(lambda:link,1,"123")
        with self.assertRaises(IdentityError): c.connect()
        self.assertTrue(link.closed);self.assertEqual([x["command"] for x in link.sent],["get_info"])
    def test_pins_uid_across_reconnect(self):
        links=[FakeLink("123"),FakeLink("124")];c=Client(lambda:links.pop(0),1);c.connect()
        with self.assertRaises(IdentityError): c.connect()


if __name__ == "__main__": unittest.main()
