"""Selected real board: 50/100/200 Hz files, FIFO counters and UART resume.

One serial open; no reset GPIO, NVS writes or physical motion. Capture ~20s/rate.
"""
import argparse
import json
from pathlib import Path
import time
from snar_imu.client import Client,SerialLink
from snar_imu.protocol import Packet
from snar_imu.storage import RunWriter,write_json


def run(args):
    root=Path(args.output);root.mkdir(parents=True,exist_ok=False)
    client=Client(lambda:SerialLink(args.serial),args.device_id);client.connect();report=[]
    try:
        for rate in (50,100,200):
            client.call("set_config",config=dict(sample_rate_hz=rate))
            status=client.call("get_status")["result"]
            if not status["sensor_ok"]:raise RuntimeError(status["sensor_error"])
            client.call("start_experiment");metadata=client.call("get_config")["result"]
            metadata["recording_start_seq"]=metadata["initial_state"]["before_seq"]
            writer=RunWriter(root/("rate_%d"%rate),metadata,"usb")
            def incoming(value):
                if isinstance(value,Packet) and value.kind==1:writer.packet(value)
                elif isinstance(value,dict) and "event" in value:writer.event(value["event"],device_event=value)
            client.on_message=incoming
            client.subscribe(metadata,from_seq=metadata["recording_start_seq"])
            deadline=time.monotonic()+args.seconds;ack=0
            while time.monotonic()<deadline:
                p=client.link.recv(.1)
                if p is not None:incoming(client.message(p))
                if writer.last is not None and time.monotonic()-ack>.4:client.ack(writer.last);ack=time.monotonic()
            # Application pause under history: unsubscribe, then rejoin SAME stream.
            client.call("unsubscribe");client.on_message=lambda value:None
            while client.link.recv(.05) is not None:pass
            last=writer.last;time.sleep(.6)
            client.on_message=incoming;reply=client.subscribe(metadata,last=last)
            writer.event("SHORT_RESUME",reply=reply)
            until=time.monotonic()+.7
            while time.monotonic()<until:
                p=client.link.recv(.1)
                if p is not None:incoming(client.message(p))
                if writer.last is not None:client.ack(writer.last)
            client.call("unsubscribe");client.on_message=lambda value:None
            final=client.call("get_status")["result"];writer.event("FINAL_DEVICE_STATUS",status=final)
            client.call("end_experiment");writer.close()
            report.append(dict(rate=rate,status=final,quality=json.loads((writer.path/"quality.json").read_text())))
        client.call("set_config",config=dict(sample_rate_hz=100))
    finally:
        client.on_message=lambda value:None
        try:client.call("unsubscribe");client.call("end_experiment")
        except Exception:pass
        client.close();write_json(root/"report.json",report)
    print(json.dumps(report,ensure_ascii=False,indent=2))


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--serial",required=True);p.add_argument("--device-id",type=int,required=True,choices=range(1,5));p.add_argument("--output",required=True);p.add_argument("--seconds",type=float,default=20);run(p.parse_args())
