"""Constant-memory independent gyro check from saved raw, SI in S.

No recalibration, no hidden motion rejection, no reference accuracy claim.
"""
import argparse
import csv
import json
import math
from pathlib import Path


def statistics(path):
    m=json.loads((path/"metadata.json").read_text());c=m["config"];cal=m["calibration"]
    n=0;mean=[0.,0.,0.,0.];m2=[0.,0.,0.,0.];saturated=0
    with (path/"samples.csv").open(newline="") as f:
        for row in csv.DictReader(f):
            v=[int(row[k])/c["gyro_lsb_per_dps"]*math.pi/180 for k in ("gx","gy","gz")]+[int(row["temperature"])/340+36.53]
            n+=1
            for i in range(4):
                d=v[i]-mean[i];mean[i]+=d/n;m2[i]+=d*(v[i]-mean[i])
            saturated+=bool(int(row["flags"])&8)
    return dict(source="Independent raw statistics, NOT a new calibration",device_uid=m["device_uid"],boot_id=m["boot_id"],samples=n,
                axes="S",nominal_mean_rad_s=mean[:3],std_rad_s=[math.sqrt(x/(n-1)) if n>1 else None for x in m2[:3]],
                applied_bias_rad_s=cal["gyro_bias"] if c["use_gyro_bias"] else [0,0,0],
                corrected_mean_rad_s=[mean[i]-(cal["gyro_bias"][i] if c["use_gyro_bias"] else 0) for i in range(3)],temperature_mean_c=mean[3],saturated_samples=saturated)


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("run",type=Path);a=p.parse_args();print(json.dumps(statistics(a.run),indent=2))
