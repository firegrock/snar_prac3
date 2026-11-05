"""Explicit selected-board gyro master/import/apply/save/reboot-readback test.

Requires --confirm-apply-save. Does not claim six-face calibration or physical
immobility; it checks the on-device engineering acceptance criteria and NVS.
"""
import argparse
import json
from pathlib import Path
import time
from snar_imu.client import Client,SerialLink,DeviceError
from snar_imu.protocol import Packet
from snar_imu.storage import RunWriter,write_json


def run(args):
    root=Path(args.output);root.mkdir(parents=True,exist_ok=False)
    client=Client(lambda:SerialLink(args.serial),args.device_id);client.connect()
    metadata=client.call("get_config")["result"];writer=RunWriter(root/"raw_master",metadata,"usb")
    def incoming(value):
        if isinstance(value,Packet) and value.kind==1:writer.packet(value)
        elif isinstance(value,dict) and "event" in value:writer.event(value["event"],device_event=value)
    client.on_message=incoming;candidate=None
    try:
        client.subscribe(metadata);client.call("cal_gyro");deadline=time.monotonic()+40;ack=poll=0
        while time.monotonic()<deadline:
            p=client.link.recv(.1)
            if p is not None:incoming(client.message(p))
            if writer.last is not None and time.monotonic()-ack>.4:client.ack(writer.last);ack=time.monotonic()
            if time.monotonic()-poll>.8:
                status=client.call("get_status")["result"];writer.event("STATUS",status=status);poll=time.monotonic()
                if status["wizard"]["state"]==-1:raise RuntimeError("Rejected: "+status["wizard"]["reason"])
                if status["wizard"]["state"]==4:
                    candidate=client.call("cal_export",source="candidate")["result"];break
        if candidate is None:raise TimeoutError("Gyro master")
        client.call("unsubscribe");client.on_message=lambda value:None;writer.close()
        write_json(root/"candidate.json",candidate)
        # Invalid coefficients must not replace the good candidate.
        bad=dict(candidate);bad["accel_scale"]=[-1,1,1]
        rejected=False
        try:client.call("cal_import",calibration=bad)
        except DeviceError:rejected=True
        assert rejected
        client.call("cal_import",calibration=candidate)
        client.call("cal_apply");applied=client.call("cal_export")["result"]
        client.call("cal_save");before=client.info["boot_id"];uid=client.uid
        client.close()
        # This actual USB bridge resets at open; do not deliberately toggle reset.
        client=Client(lambda:SerialLink(args.serial),args.device_id,uid);client.connect()
        loaded=client.call("cal_export")["result"];status=client.call("get_status")["result"]
        result=dict(source="HARDWARE gyro only",candidate=candidate,invalid_scale_rejected=rejected,
                    applied=applied,loaded=loaded,nvs_status=status["nvs_status"],boot_changed=client.info["boot_id"]!=before,
                    coefficients_equal=all(applied[k]==loaded[k] for k in ("gyro_bias","accel_bias","accel_scale")),
                    six_faces="NOT RUN: requires six physical placements")
        result["status"]="PASS" if result["coefficients_equal"] and status["nvs_status"]=="LOADED" and result["boot_changed"] else "FAIL"
        write_json(root/"report.json",result);print(json.dumps(result,ensure_ascii=False,indent=2))
    finally:
        client.on_message=lambda value:None
        try:client.call("unsubscribe")
        except Exception:pass
        writer.close();client.close()


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--serial",required=True);p.add_argument("--device-id",type=int,choices=range(1,5),required=True);p.add_argument("--output",required=True);p.add_argument("--confirm-apply-save",action="store_true",required=True);run(p.parse_args())
