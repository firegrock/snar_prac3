"""Read-only diagnostics + explicit reversible command tests on SELECTED board.

Does not move Comau, change eFuse or erase flash. Does reset software orientation
twice with the SAME request_id to test idempotency; does not save NVS.
"""
import argparse
import json
import time
from pathlib import Path
from snar_imu.client import Client, SerialLink


def run(args):
    client=Client(lambda:SerialLink(args.serial),args.device_id,args.uid);client.connect()
    report={"source":"HARDWARE UART (NOT SYNTHETIC)","info":client.info,"checks":[]}
    def check(name,ok,detail):
        report["checks"].append(dict(name=name,status="PASS" if ok else "FAIL",detail=detail))
    try:
        metadata=client.call("get_config")["result"]
        check("metadata",metadata["device_id"]==args.device_id and len(metadata["config"]["R_BS"])==9 and isinstance(metadata["boot_id"],str),metadata)
        before=client.call("get_info")["result"]
        request=client.request("reset_orientation")
        def same_request():
            client.link.send(request);deadline=time.monotonic()+3
            while time.monotonic()<deadline:
                p=client.link.recv(.2)
                if p is not None:
                    m=client.message(p)
                    if isinstance(m,dict) and m.get("request_id")==request["request_id"]:return m
            raise TimeoutError("reset reply")
        first=same_request();one=client.call("get_info")["result"];second=same_request();two=client.call("get_info")["result"]
        check("request_id_idempotent",first==second and one["stream_id"]==two["stream_id"] and before["stream_id"]!=one["stream_id"],dict(first=first,second=second,stream_after_first=one["stream_id"],stream_after_second=two["stream_id"]))
        client.link.port.write(b"invalid boot/noise\0")
        check("UART_noise_recovery",client.call("get_info")["result"]["device_uid"]==client.uid,"Injected one invalid COBS frame; valid command still works")
        states=[];deadline=time.monotonic()+args.seconds
        while time.monotonic()<deadline:
            states.append(client.call("get_status")["result"]);time.sleep(.5)
        report["status_history"]=states
        check("diagnostics_no_reboot",all(s["boot_id"]==client.info["boot_id"] for s in states),"status loop %.1f s"%args.seconds)
        check("sensor_pipeline",all(s["sensor_ok"] for s in states) and states[-1]["next_seq"]>0,dict(sensor_error=states[-1]["sensor_error"],mpu_whoami=states[-1].get("mpu_whoami"),samples=states[-1]["next_seq"]))
        report["lcd"]={"status":"NOT RUN" if not states[-1]["lcd_ok"] else "NOT RUN", "reason":"Visual/electrical verification requires human; lcd_ok only ACK/init", "driver_status":states[-1]["lcd_ok"]}
        report["heap"]={"free_min":min(s["free_heap"] for s in states),"free_max":max(s["free_heap"] for s in states),"minimum_free_heap":min(s["minimum_free_heap"] for s in states)}
        report["events"]=client.call("get_events",after=0)
    finally:
        client.close()
        path=Path(args.output)
        if path.exists(): raise FileExistsError(str(path))
        path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n")
    print(json.dumps(report["checks"],ensure_ascii=False,indent=2))


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("--serial",required=True);p.add_argument("--device-id",type=int,choices=range(1,5),required=True);p.add_argument("--uid");p.add_argument("--seconds",type=float,default=15);p.add_argument("--output",required=True);run(p.parse_args())
