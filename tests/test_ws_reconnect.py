"""Real websocket-client + CLI against a SYNTHETIC local RFC6455 test peer.

Not ESP HTTPD and not Wi-Fi radio. Disconnects once, then checks resume/dedup.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import socketserver
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from snar_imu.protocol import encode, SAMPLE
from test_host import meta


class Peer(socketserver.BaseRequestHandler):
    def send(self, opcode, payload):
        header=bytes([0x80|opcode])
        if len(payload)<126:header+=bytes([len(payload)])
        else:header+=bytes([126])+struct.pack('!H',len(payload))
        self.request.sendall(header+payload)

    def text(self, value):self.send(1,json.dumps(value).encode())

    def handle(self):
        server=self.server;server.connections+=1;self.request.settimeout(2);data=b''
        while b'\r\n\r\n' not in data:data+=self.request.recv(4096)
        key=[line.split(b':',1)[1].strip() for line in data.split(b'\r\n') if line.lower().startswith(b'sec-websocket-key:')][0]
        accept=base64.b64encode(hashlib.sha1(key+b'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest())
        self.request.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: '+accept+b'\r\n\r\n')
        buffer=b'';cursor=None;subscribed_at=None
        try:
            while not server.stopping.is_set():
                next_seq=int((time.monotonic()-server.started)*100)
                if not server.rpc_only and cursor is not None and cursor<next_seq:
                    rows=[(i,(i+1)*10000,0,0,8192,0,0,0,0,1.,0.,0.,0.,0) for i in range(cursor,min(cursor+8,next_seq))]
                    self.send(2,encode(1,struct.pack('<H',len(rows))+b''.join(SAMPLE.pack(*r) for r in rows),123,456,789,1,1 if server.disconnected else 0));cursor=rows[-1][0]+1
                    if server.close_once and not server.disconnected and time.monotonic()-subscribed_at>.4:
                        server.disconnected=True;return # true TCP close -> websocket exception
                    if server.expire_once and not server.expired and time.monotonic()-subscribed_at>.4:
                        server.expired=True;cursor=None # lease expiry, TCP remains healthy
                if not select.select([self.request],[],[],.005)[0]:continue
                incoming=self.request.recv(8192)
                if not incoming:return
                buffer+=incoming
                while len(buffer)>=2:
                    opcode=buffer[0]&15;n=buffer[1]&127;offset=2
                    if n==126:
                        if len(buffer)<4:break
                        n=struct.unpack_from('!H',buffer,2)[0];offset=4
                    if n==127:raise ValueError('Oversized test request')
                    if len(buffer)<offset+4+n:break
                    mask=buffer[offset:offset+4];offset+=4
                    payload=bytes(b^mask[i%4] for i,b in enumerate(buffer[offset:offset+n]));buffer=buffer[offset+n:]
                    if opcode==8:return
                    if opcode!=1:continue
                    j=json.loads(payload);command=j['command'];reply=dict(request_id=j['request_id'],ok=True)
                    if command=='get_info':reply['result']=dict(device_id=1,device_uid='123',boot_id='456',stream_id='789',config_id=1)
                    elif command=='get_config':reply['result']=server.metadata
                    elif command=='get_status':reply['result']=dict(sensor_ok=True,sensor_error='',wizard=dict(state=0))
                    elif command=='get_events':reply.update(events=[],next_event=0)
                    elif command=='start_experiment':
                        server.metadata['initial_state']['before_seq']=next_seq
                    elif command in ('subscribe','resume'):
                        cursor=j.get('from_seq',next_seq) if command=='subscribe' else j['last_seq']+1
                        server.resume_count+=command=='resume';subscribed_at=time.monotonic();reply['metadata']=server.metadata
                    elif command=='unsubscribe':cursor=None
                    if server.rpc_only and cursor is not None and command in ('get_events','get_status'):
                        # All measurement traffic is interleaved BEFORE an RPC
                        # reply, so Client.call consumes it via on_message.
                        while cursor<next_seq:
                            rows=[(i,(i+1)*10000,0,0,8192,0,0,0,0,1.,0.,0.,0.,0) for i in range(cursor,min(cursor+8,next_seq))]
                            self.send(2,encode(1,struct.pack('<H',len(rows))+b''.join(SAMPLE.pack(*r) for r in rows),123,456,789,1));cursor=rows[-1][0]+1
                    self.text(reply)
        except (OSError,ConnectionError):return


class TestReconnect(unittest.TestCase):
    def test_tcp_disconnect_and_resume(self):
        try:import websocket
        except ImportError:self.skipTest('websocket-client not installed')
        class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
        server=Server(('127.0.0.1',0),Peer);server.started=time.monotonic();server.stopping=threading.Event();server.metadata=meta();server.disconnected=False;server.resume_count=0;server.connections=0;server.rpc_only=False;server.close_once=True;server.expire_once=False;server.expired=False
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'run'
                p=subprocess.run([sys.executable,'-m','snar_imu','record','--ws','ws://127.0.0.1:%d/ws'%server.server_address[1],'--device-id','1','--output',str(path),'--duration','1.5'],env=dict(os.environ,PYTHONPATH='host',NO_PROXY='127.0.0.1'),capture_output=True,timeout=15)
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr));self.assertTrue(server.disconnected);self.assertGreaterEqual(server.resume_count,1)
                quality=json.loads((path/'quality.json').read_text());self.assertEqual(quality['irrecoverable_missing_samples'],0);self.assertGreater(quality['unique_samples'],50)
                events=(path/'events.jsonl').read_text();self.assertIn('DISCONNECT',events);self.assertIn('RECONNECTED',events)
        finally:
            server.stopping.set();server.shutdown();server.server_close();thread.join(2)

    def test_rpc_consumed_samples_prevent_false_disconnect(self):
        try:import websocket
        except ImportError:self.skipTest('websocket-client not installed')
        class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
        server=Server(('127.0.0.1',0),Peer);server.started=time.monotonic();server.stopping=threading.Event();server.metadata=meta();server.disconnected=False;server.resume_count=0;server.connections=0;server.rpc_only=True;server.close_once=False;server.expire_once=False;server.expired=False
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'run'
                p=subprocess.run([sys.executable,'-m','snar_imu','record','--ws','ws://127.0.0.1:%d/ws'%server.server_address[1],'--device-id','1','--output',str(path),'--duration','3.7'],env=dict(os.environ,PYTHONPATH='host',NO_PROXY='127.0.0.1'),capture_output=True,timeout=15)
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr));self.assertEqual(server.connections,1,'False reconnect despite valid samples inside RPC')
                self.assertNotIn('DISCONNECT',(path/'events.jsonl').read_text())
                self.assertNotIn('SUBSCRIPTION_RESUMED',(path/'events.jsonl').read_text())
        finally:
            server.stopping.set();server.shutdown();server.server_close();thread.join(2)

    def test_lease_resume_without_reopening_link(self):
        try:import websocket
        except ImportError:self.skipTest('websocket-client not installed')
        class Server(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
        server=Server(('127.0.0.1',0),Peer);server.started=time.monotonic();server.stopping=threading.Event();server.metadata=meta();server.disconnected=False;server.resume_count=0;server.connections=0;server.rpc_only=False;server.close_once=False;server.expire_once=True;server.expired=False
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                path=Path(tmp)/'run'
                p=subprocess.run([sys.executable,'-m','snar_imu','record','--ws','ws://127.0.0.1:%d/ws'%server.server_address[1],'--device-id','1','--output',str(path),'--duration','4.4'],env=dict(os.environ,PYTHONPATH='host',NO_PROXY='127.0.0.1'),capture_output=True,timeout=15)
                self.assertEqual(p.returncode,0,(p.stdout,p.stderr));self.assertTrue(server.expired);self.assertEqual(server.connections,1);self.assertGreaterEqual(server.resume_count,1)
                self.assertIn('SUBSCRIPTION_RESUMED',(path/'events.jsonl').read_text());self.assertNotIn('DISCONNECT',(path/'events.jsonl').read_text())
                self.assertEqual(json.loads((path/'quality.json').read_text())['irrecoverable_missing_samples'],0)
        finally:
            server.stopping.set();server.shutdown();server.server_close();thread.join(2)


if __name__=='__main__':unittest.main()
