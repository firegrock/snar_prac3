"""Real CLI + pyserial + PTY; sensor/device are explicitly SYNTHETIC.

Not a test of RF, FIFO or ESP HTTPD. Uses fixed-fixture wire format and file output.
"""
import collections
import json
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import threading
import time
import tty
import unittest
from snar_imu.protocol import Framer, SAMPLE, encode, cobs_encode
from test_host import meta


class Emulator:
    def __init__(self):
        self.master, self.slave = pty.openpty(); tty.setraw(self.slave)
        self.port = os.ttyname(self.slave); self.stop = threading.Event(); self.framer = Framer()
        self.rows = collections.deque(maxlen=832); self.seq = 0; self.cursor = None
        self.experiment = False; self.metadata = meta(); self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def send(self, kind, payload):
        os.write(self.master, cobs_encode(encode(kind,payload,123,456,789,1))+b"\0")

    def reply(self, j, **result):
        self.send(3,json.dumps(dict(request_id=j["request_id"],ok=True,**result)).encode())

    def command(self, j):
        c=j["command"]
        if c=="get_info": self.reply(j,result=dict(device_id=1,device_uid="123",boot_id="456",stream_id="789",config_id=1))
        elif c=="get_config": self.reply(j,result=self.metadata)
        elif c=="get_status": self.reply(j,result=dict(sensor_ok=True,experiment=self.experiment,wizard=dict(state=0),sensor_error=""))
        elif c=="start_experiment":
            self.experiment=True; self.metadata["initial_state"]["before_seq"]=self.seq; self.reply(j)
        elif c=="end_experiment": self.experiment=False;self.reply(j)
        elif c=="get_events": self.reply(j,events=[],next_event=0)
        elif c in ("subscribe","resume"):
            self.cursor=j.get("from_seq",self.seq) if c=="subscribe" else j["last_seq"]+1
            self.reply(j,metadata=self.metadata)
        elif c=="unsubscribe": self.cursor=None;self.reply(j)
        else: self.reply(j)

    def run(self):
        tick=time.monotonic(); hello=0; corrupted=False
        while not self.stop.is_set():
            now=time.monotonic()
            if now-hello>.2:
                self.send(5,b'{"source":"SYNTHETIC PTY"}');hello=now
            if now-tick>=.01:
                tick+=.01;i=self.seq;self.seq+=1
                self.rows.append((i,(i+1)*10000,0,0,8192,0,0,0,0,1.,0.,0.,0.,0))
                if self.cursor is not None:
                    rows=[r for r in self.rows if r[0]>=self.cursor][:8]
                    if rows:
                        payload=struct.pack("<H",len(rows))+b"".join(SAMPLE.pack(*r) for r in rows)
                        # Deliberately corrupt one frame, then recover via resume.
                        if not corrupted and self.cursor>40:
                            data=bytearray(encode(1,payload,123,456,789,1));data[-1]^=1
                            os.write(self.master,cobs_encode(data)+b"\0");corrupted=True
                        else:self.send(1,payload)
                        self.cursor=rows[-1][0]+1
            if select.select([self.master],[],[],.002)[0]:
                try:
                    for p in self.framer.feed(os.read(self.master,4096)):
                        if p.kind==2:self.command(json.loads(p.payload))
                except OSError:pass

    def close(self):
        self.stop.set();self.thread.join(2);os.close(self.master);os.close(self.slave)


class CLIIntegration(unittest.TestCase):
    def test_sigint_corrupt_frame_and_resume(self):
        try: import serial
        except ImportError:self.skipTest("pyserial not installed; use .venv/bin/python")
        emulator=Emulator()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                run=Path(tmp)/"run"
                env=dict(os.environ,PYTHONPATH="host")
                # Darwin PTYs reject IOSSIOSPEED for 460800 (ENOTTY). PTY has no
                # physical baud: inject a standard speed in THIS child only.
                launch="import serial\noriginal=serial.Serial\ndef pty_serial(*a,**kw):\n kw['baudrate']=115200\n return original(*a,**kw)\nserial.Serial=pty_serial\nfrom snar_imu.cli import main\nmain()"
                p=subprocess.Popen([sys.executable,"-c",launch,"record","--serial",emulator.port,"--device-id","1","--output",str(run)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
                try:
                    time.sleep(1.8);p.send_signal(signal.SIGINT);out,err=p.communicate(timeout=10)
                except BaseException:
                    p.kill();p.communicate();raise
                self.assertEqual(p.returncode,0,(out,err))
                quality=json.loads((run/"quality.json").read_text())
                self.assertGreater(quality["unique_samples"],50)
                self.assertEqual(quality["irrecoverable_missing_samples"],0)
                self.assertGreater(quality["frame_errors"],0)
                self.assertIn("DELIVERY_HOLE",(run/"events.jsonl").read_text())
                self.assertFalse(emulator.experiment)
        finally:emulator.close()


if __name__=="__main__":unittest.main()
