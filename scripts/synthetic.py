"""Generate clearly labelled SYNTHETIC stationary run without hardware."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"host"))
from snar_imu.protocol import encode, decode, SAMPLE
from snar_imu.storage import RunWriter
import struct

metadata = dict(device_id=1, device_uid="0", boot_id="0", stream_id="0", config_id=1,
    source="SYNTHETIC — NOT A HARDWARE MEASUREMENT", firmware_version="SYNTHETIC",
    sdk_version="NOT APPLICABLE", quaternion="Hamilton qw,qx,qy,qz; B->W; AFTER sample",
    axes="S=B, W right-handed Z up, yaw relative", time="SYNTHETIC monotonic us",
    config=dict(sample_rate_hz=100, accel_range_g=4, gyro_range_dps=500, dlpf=4, smplrt_div=9,
        accel_lsb_per_g=8192, gyro_lsb_per_dps=65.5, g0=9.80665,Kp=2,Ki=0,use_gyro_bias=True,R_BS=[1,0,0,0,1,0,0,0,1]),
    calibration=dict(schema_version=1, device_uid="0", method="SYNTHETIC", axes="S", gyro_units="rad/s",accel_units="m/s^2",
        gyro_bias=[0,0,0],accel_bias=[0,0,0],accel_scale=[1,1,1],temperature_c=36.53,gyro_std=[0,0,0],gyro_n=0,
        face_mean=[[0,0,0]]*6,face_std=[[0,0,0]]*6,face_n=[0]*6,accel_range_g=4,gyro_range_dps=500),
    initial_state=dict(before_seq=0,q=[1,0,0,0],integral=[0,0,0],previous_t_us="0",ready=True,still_n=0,still_mean=[0]*3,still_m2=[0]*3))

def generate(path):
    w=RunWriter(path,metadata,"SYNTHETIC")
    for start in range(0,600,8):
        rows=[(i,(i+1)*10000,0,0,8192,0,0,0,0,1.,0.,0.,0.,0) for i in range(start,min(start+8,600))]
        w.packet(decode(encode(1,struct.pack('<H',len(rows))+b''.join(SAMPLE.pack(*r) for r in rows),0,0,0,1)))
    w.event("SYNTHETIC",note="Not hardware data");w.close()

if __name__ == '__main__': generate(sys.argv[1] if len(sys.argv)>1 else 'examples/synthetic/static')
