import collections
import json
import time
import uuid
from .protocol import Framer, Packet, decode, encode, cobs_encode, json_bytes


class IdentityError(RuntimeError):
    pass


class DeviceError(RuntimeError):
    pass


class SerialLink:
    name = "usb"

    def __init__(self, port):
        import serial
        # Не выполняем reset DTR/RTS. Некоторые мосты/драйверы всё же дают импульс
        # при open; поэтому boot_id всегда проверяется заново.
        self.port = serial.Serial(port=None, baudrate=460800, timeout=0.1, write_timeout=1,
                                  rtscts=False, dsrdtr=False, exclusive=True)
        self.port.dtr = False; self.port.rts = False
        self.port.port = port; self.port.open()
        self.framer = Framer(); self.queue = collections.deque()
        # Open can cause a real reset on a USB bridge even with inactive lines.
        # Do not send into ROM boot baud or a not-yet-installed RX driver.
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            packet = self.recv(0.3)
            if isinstance(packet, Packet) and packet.kind == 5:
                self.hello = packet
                break
        else:
            self.port.close()
            raise TimeoutError("Не найден валидный HELLO (COBS/CRC, UART 460800)")

    def send(self, value):
        self.port.write(cobs_encode(encode(2, json_bytes(value))) + b"\0")

    def recv(self, timeout=1):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.queue:
                return self.queue.popleft()
            self.queue.extend(self.framer.feed(self.port.read(min(4096, self.port.in_waiting or 1))))
        return None

    def close(self):
        self.port.close()


class WSLink:
    name = "wifi"

    def __init__(self, url):
        import websocket
        try:
            self.ws = websocket.create_connection(url, timeout=2, http_proxy_host=None,
                                                  enable_multithread=True)
        except (websocket.WebSocketException, OSError) as e:
            raise ConnectionError(str(e)) from e
        self.errors = 0

    def send(self, value):
        import websocket
        try:
            self.ws.send(json_bytes(value).decode("ascii"))
        except (websocket.WebSocketException, OSError) as e:
            raise ConnectionError(str(e)) from e

    def recv(self, timeout=1):
        import websocket
        self.ws.settimeout(timeout)
        try:
            value = self.ws.recv()
        except websocket.WebSocketTimeoutException:
            return None
        except (websocket.WebSocketException, OSError) as e:
            raise ConnectionError(str(e)) from e
        if value == "" or value == b"":
            raise ConnectionError("WebSocket closed")
        if isinstance(value, bytes):
            try:
                return decode(value)
            except ValueError:
                self.errors += 1
                return None
        return json.loads(value)

    def close(self):
        self.ws.close()


class Client:
    def __init__(self, factory, device_id, expected_uid=None):
        self.factory = factory; self.device_id = device_id; self.uid = expected_uid
        self.client_id = "py-"+uuid.uuid4().hex
        self.link = None; self.info = None; self.on_message = lambda value: None
        self.requests = 0

    def connect(self):
        if self.link:
            self.link.close()
        self.info = None
        self.link = self.factory()
        # Только read-only handshake до проверки; никаких изменяющих команд.
        observer = self.on_message
        self.on_message = lambda value: None
        try:
            info = self.call("get_info")["result"]
        finally:
            self.on_message = observer
        if info["device_id"] != self.device_id or (self.uid is not None and str(info["device_uid"]) != str(self.uid)):
            self.link.close(); self.link = None
            raise IdentityError("Номер/UID не совпадает: ожидался %s/%s, получен %s/%s" %
                                (self.device_id, self.uid, info["device_id"], info["device_uid"]))
        self.uid = str(info["device_uid"]); self.info = info
        print("Комплект %02d · UID %s · boot %s · %s" % (self.device_id, self.uid, info["boot_id"], self.link.name), flush=True)
        return info

    def request(self, command, **args):
        self.requests += 1
        value = dict(request_id="%s-%d" % (self.client_id, self.requests), client_id=self.client_id, command=command, **args)
        if self.info:
            value.update(device_id=self.device_id, device_uid=self.uid)
        return value

    @staticmethod
    def message(value):
        if isinstance(value, Packet) and value.kind in (3, 4, 5):
            return json.loads(value.payload.decode("utf-8"))
        return value

    def call(self, command, **args):
        value = self.request(command, **args)
        # Retries reuse EXACT request_id and bytes. Mutating dispatcher deduplicates.
        for attempt in range(2):
            self.link.send(value); deadline = time.monotonic()+3
            while time.monotonic() < deadline:
                raw = self.link.recv(0.2)
                if raw is None:
                    continue
                msg = self.message(raw)
                if isinstance(msg, dict) and msg.get("request_id") == value["request_id"]:
                    if not msg.get("ok"):
                        raise DeviceError(json.dumps(msg.get("error"), ensure_ascii=False))
                    return msg
                self.on_message(msg)
        raise TimeoutError("Нет ответа на "+command)

    def subscribe(self, metadata, last=None, mode="live", from_seq=None):
        args = dict(device_uid=self.uid, boot_id=metadata["boot_id"], stream_id=metadata["stream_id"])
        if last is not None:
            args["last_seq"] = last
            return self.call("resume", **args)
        args["mode"] = mode
        if from_seq is not None:
            args["from_seq"] = from_seq
        return self.call("subscribe", **args)

    def ack(self, seq):
        # Response is consumed by normal receive loop, not a blocking RPC.
        self.link.send(self.request("ack", seq=seq))

    def close(self):
        if self.link:
            self.link.close(); self.link = None
