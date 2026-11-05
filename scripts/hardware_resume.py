"""Real UART application resume: immutable replay, duplicates and exact GAP.

Uses 200Hz so 832 records are 4.16s; does NOT test radio reassociation.
Restores 100Hz after the explicit experiment. No NVS writes or physical motion.
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
    client=Client(lambda:SerialLink(args.serial),args.device_id);client.connect()
    client.call("set_config",config=dict(sample_rate_hz=200));client.call("start_experiment")
    metadata=client.call("get_config")["result"];writer=RunWriter(root/"raw",metadata,"usb")
    reference={};compared=0;gaps=[]
    def incoming(value):
        nonlocal compared
        if isinstance(value,Packet) and value.kind==1:
            for row in value.samples():
                if row[0] in reference:
                    assert tuple(row)==reference[row[0]],"Replay changed immutable raw/t/q/flags"
                    compared+=1
                elif len(reference)<600:reference[row[0]]=tuple(row)
            writer.packet(value)
        elif isinstance(value,dict) and "event" in value:
            writer.event(value["event"],device_event=value)
            if value["event"]=="GAP":gaps.append(value)
    client.on_message=incoming
    def receive(seconds):
        end=time.monotonic()+seconds;ack=0
        while time.monotonic()<end:
            p=client.link.recv(.05)
            if p is not None:incoming(client.message(p))
            if writer.last is not None and time.monotonic()-ack>.2:client.ack(writer.last);ack=time.monotonic()
    try:
        client.subscribe(metadata,mode="history");receive(.7)
        client.call("unsubscribe");client.on_message=lambda value:None
        while client.link.recv(.03) is not None:pass
        time.sleep(.2);client.on_message=incoming
        resume_from=min(reference)+10
        short=client.subscribe(metadata,last=resume_from);receive(.5)
        assert not short["gap"] and compared>0
        client.call("unsubscribe");client.on_message=lambda value:None
        while client.link.recv(.03) is not None:pass
        last=writer.last;time.sleep(5.3);client.on_message=incoming
        long=client.subscribe(metadata,last=last);receive(2.0)
        assert long["gap"] and gaps
        client.call("unsubscribe");client.on_message=lambda value:None
        status=client.call("get_status")["result"];writer.event("FINAL_DEVICE_STATUS",status=status)
        assert status["boot_id"]==metadata["boot_id"] and status["stream_id"]==metadata["stream_id"]
        assert writer.last>last+832//2,"GAP sent but history did not make forward progress"
        assert status["next_seq"]-writer.last<40,"Resume failed to catch up to live"
        assert len(gaps)<10,"GAP storm: oldest cursor chasing without data progress"
        client.call("end_experiment");writer.close()
        write_json(root/"report.json",dict(status="PASS",source="HARDWARE UART application resume, NOT radio",immutable_records_compared=compared,live_lag_records=status["next_seq"]-writer.last,short_reply=short,long_reply=long,gap_events=gaps,quality=json.loads((writer.path/"quality.json").read_text())))
        client.call("set_config",config=dict(sample_rate_hz=100))
    finally:
        client.on_message=lambda value:None
        try:client.call("unsubscribe");client.call("end_experiment")
        except Exception:pass
        writer.close();client.close()
    print("PASS real UART replay equality / duplicates / short recovery / long explicit GAP / same boot+stream")


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--serial",required=True);p.add_argument("--device-id",type=int,required=True,choices=range(1,5));p.add_argument("--output",required=True);run(p.parse_args())
