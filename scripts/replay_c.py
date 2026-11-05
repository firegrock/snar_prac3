"""Replay saved raw through THE production C core with the recorded initial state.

This is NOT a substitute for executing Octave and NOT a sensor accuracy test.
Refuses missing boundaries/rows; compares q/-q, prints implementation discrepancy.
"""
import argparse
import csv
import ctypes as C
import json
import math
from pathlib import Path
import subprocess


class Config(C.Structure):
    _fields_=[("rate",C.c_int),("accel_range",C.c_int),("gyro_range",C.c_int),("dlpf",C.c_int),
              ("kp",C.c_float),("ki",C.c_float),("g0",C.c_float),("r_bs",C.c_int8*9),("use_bias",C.c_bool)]


class Cal(C.Structure):
    _fields_=[("version",C.c_uint32),("uid",C.c_uint64),("gyro_bias",C.c_float*3),
              ("accel_bias",C.c_float*3),("accel_scale",C.c_float*3),("temperature",C.c_float),
              ("gyro_std",C.c_float*3),("accel_std",(C.c_float*3)*6),("face_mean",(C.c_float*3)*6),
              ("gyro_n",C.c_uint32),("face_n",C.c_uint32*6),("accel_range",C.c_int),("gyro_range",C.c_int),
              ("method",C.c_char*24),("valid",C.c_bool)]


class Att(C.Structure):
    _fields_=[("q",C.c_float*4),("integral",C.c_float*3),("prev_us",C.c_int64),("ready",C.c_bool),
              ("still_n",C.c_uint32),("still_mean",C.c_float*3),("still_m2",C.c_float*3)]


def replay(path, tolerance):
    root=Path(__file__).resolve().parents[1];out=root/"tests/out";out.mkdir(exist_ok=True)
    library=out/"liblab_replay.so"
    subprocess.run(["cc","-std=c11","-O2","-ffp-contract=off","-shared","-fPIC","-I"+str(root/"components/lab_core/include"),str(root/"components/lab_core/core.c"),str(root/"components/lab_core/wire.c"),"-lm","-o",str(library)],check=True)
    lib=C.CDLL(str(library));lib.lab_att_step.restype=C.c_uint32
    lib.lab_correct.argtypes=[C.POINTER(Config),C.POINTER(Cal),C.POINTER(C.c_int16),C.POINTER(C.c_float),C.POINTER(C.c_float),C.POINTER(C.c_float)]
    lib.lab_att_step.argtypes=[C.POINTER(Att),C.POINTER(Config),C.POINTER(C.c_float),C.POINTER(C.c_float),C.c_int64]
    metadata=json.loads((path/"metadata.json").read_text());c=metadata["config"];cal=metadata["calibration"];initial=metadata["initial_state"]
    config=Config(c["sample_rate_hz"],c["accel_range_g"],c["gyro_range_dps"],c["dlpf"],c["Kp"],c["Ki"],c["g0"],(C.c_int8*9)(*c["R_BS"]),c["use_gyro_bias"])
    coeff=Cal();coeff.version=1;coeff.uid=int(metadata["device_uid"])
    for name in ("gyro_bias","accel_bias","accel_scale"):setattr(coeff,name,(C.c_float*3)(*cal[name]))
    att=Att();att.q=(C.c_float*4)(*initial["q"]);att.integral=(C.c_float*3)(*initial["integral"]);att.prev_us=int(initial["previous_t_us"]);att.ready=initial["ready"];att.still_n=initial.get("still_n",0)
    for name in ("still_mean","still_m2"):setattr(att,name,(C.c_float*3)(*initial.get(name,[0]*3)))
    maximum=0;count=0;expected=initial["before_seq"];started=False
    with (path/"samples.csv").open(newline="") as f:
        for row in csv.DictReader(f):
            seq=int(row["seq"])
            if seq<expected and not started:continue
            if seq!=expected:raise ValueError("Missing boundary/sample %d, got %d; exact replay refused"%(expected,seq))
            started=True;expected+=1
            # Explicit resync flag: the firmware resets dynamic state on sensor loss.
            if int(row["flags"])&16:lib.lab_att_reset(C.byref(att))
            raw=(C.c_int16*7)(*[int(row[key]) for key in ("ax","ay","az","gx","gy","gz","temperature")]);a=(C.c_float*3)();w=(C.c_float*3)();t=C.c_float()
            lib.lab_correct(C.byref(config),C.byref(coeff),raw,a,w,C.byref(t))
            lib.lab_att_step(C.byref(att),C.byref(config),a,w,int(row["t_us"]))
            reference=[float(row[key]) for key in ("qw","qx","qy","qz")]
            error=min(math.sqrt(sum((att.q[i]-reference[i])**2 for i in range(4))),math.sqrt(sum((att.q[i]+reference[i])**2 for i in range(4))))
            maximum=max(maximum,error);count+=1
    if not started:raise ValueError("Initial boundary unavailable; exact replay refused")
    result=dict(status="PASS" if maximum<tolerance else "FAIL",samples=count,max_q_sign_invariant_norm=maximum,tolerance=tolerance,source="Production C replay; not Octave, not sensor accuracy")
    print(json.dumps(result,indent=2));return result["status"]=="PASS"


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("run",type=Path);p.add_argument("--tolerance",type=float,default=2e-4);args=p.parse_args();raise SystemExit(0 if replay(args.run,args.tolerance) else 1)
