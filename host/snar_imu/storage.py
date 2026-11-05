import csv
import json
import math
import sqlite3
import time
from pathlib import Path
from .protocol import COLUMNS


def write_json(path, obj):
    temporary = path.with_suffix(path.suffix+".tmp")
    with temporary.open("w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, allow_nan=False, indent=2); f.write("\n"); f.flush()
    temporary.replace(path)


def numeric_metadata(path, m):
    c = m["config"]; cal = m["calibration"]; i = m["initial_state"]
    # Одна числовая строка, идентификаторы остаются строками metadata.json.
    names = ["rate_hz", "accel_lsb_g", "gyro_lsb_dps", "g0", "Kp", "Ki", "use_gyro_bias"]
    values = [c["sample_rate_hz"], c["accel_lsb_per_g"], c["gyro_lsb_per_dps"], c["g0"], c["Kp"], c["Ki"], int(c["use_gyro_bias"])]
    for group, data in [("gyro_bias", cal["gyro_bias"]), ("accel_bias", cal["accel_bias"]), ("accel_scale", cal["accel_scale"]), ("R_BS", c["R_BS"]), ("q_initial", i["q"]), ("integral_initial", i["integral"])]:
        names.extend(group+str(k) for k in range(len(data))); values.extend(data)
    names.extend(["before_seq", "previous_t_us", "ready", "still_n"])
    values.extend([i["before_seq"], i["previous_t_us"], int(i["ready"]), i.get("still_n", 0)])
    for key in ("still_mean", "still_m2"):
        names.extend(key+str(k) for k in range(3)); values.extend(i.get(key, [0, 0, 0]))
    with path.open("w", newline="") as f:
        w = csv.writer(f); w.writerow(names); w.writerow(values)


class RunWriter:
    """SQLite disk index + streaming CSV. No multi-hour in-memory sort/dedup."""
    def __init__(self, path, metadata, transport):
        self.path = Path(path); self.path.mkdir(parents=True, exist_ok=False)
        self.metadata = metadata; self.transport = transport
        self.key = tuple(str(metadata[x]) for x in ("device_uid", "boot_id", "stream_id"))
        write_json(self.path/"metadata.json", metadata); numeric_metadata(self.path/"metadata_numeric.csv", metadata)
        self.db = sqlite3.connect(str(self.path/"index.sqlite3"))
        self.db.execute("CREATE TABLE samples (seq INTEGER PRIMARY KEY,t_us INTEGER,ax INTEGER,ay INTEGER,az INTEGER,gx INTEGER,gy INTEGER,gz INTEGER,temperature INTEGER,qw REAL,qx REAL,qy REAL,qz REAL,flags INTEGER)")
        self.samples_file = (self.path/"samples.csv").open("w", newline="")
        self.csv = csv.writer(self.samples_file); self.csv.writerow(COLUMNS)
        self.transport_file = (self.path/"transport.csv").open("w", newline="")
        self.tx = csv.writer(self.transport_file); self.tx.writerow(["host_unix_ns", "transport", "seq", "replay", "duplicate"])
        self.events_file = (self.path/"events.jsonl").open("w", encoding="utf-8")
        self.received = self.recovered = self.duplicates = self.frame_errors = 0
        self.last = None; self.closed = False
        self.event("OPEN", transport=transport)

    def event(self, kind, **data):
        json.dump(dict(host_unix_ns=str(time.time_ns()), type=kind, **data), self.events_file, ensure_ascii=False, allow_nan=False)
        self.events_file.write("\n"); self.events_file.flush()

    def packet(self, packet):
        if packet.key != self.key or packet.config != self.metadata["config_id"]:
            raise ValueError("STREAM_BOUNDARY: packet identity/config differs; new segment required")
        rows = packet.samples()
        for row in rows:
            self.received += 1
            old = self.db.execute("SELECT * FROM samples WHERE seq=?", (row[0],)).fetchone()
            duplicate = old is not None
            if duplicate:
                self.duplicates += 1
                if tuple(row) != old:
                    self.event("INTEGRITY_ERROR", seq=row[0]); raise ValueError("Same key, different immutable sample")
            else:
                self.db.execute("INSERT INTO samples VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", row)
                self.csv.writerow(row)
                if packet.delivery & 1:
                    self.recovered += 1
                self.last = max(row[0], self.last if self.last is not None else row[0])
            self.tx.writerow([time.time_ns(), self.transport, row[0], int(bool(packet.delivery & 1)), int(duplicate)])
        # ACK only after durable-to-file-buffer flush; no power-fail transaction claim.
        self.db.commit(); self.samples_file.flush(); self.transport_file.flush()

    def close(self):
        if self.closed:
            return
        self.event("CLOSE"); self.samples_file.close(); self.transport_file.close(); self.events_file.close()
        # Bounded-memory ordered export (also supports future out-of-order transports).
        with (self.path/"samples.ordered.tmp").open("w", newline="") as f:
            w = csv.writer(f); w.writerow(COLUMNS)
            for row in self.db.execute("SELECT * FROM samples ORDER BY seq"):
                w.writerow(row)
        (self.path/"samples.ordered.tmp").replace(self.path/"samples.csv")
        self.db.close(); self.closed = True
        quality = validate_run(self.path)
        quality.update(received=self.received, recovered=self.recovered, duplicates=self.duplicates, frame_errors=self.frame_errors)
        write_json(self.path/"quality.json", quality)


def validate_run(path):
    path = Path(path)
    with (path/"metadata.json").open(encoding="utf-8") as f:
        m = json.load(f)
    rate = m["config"]["sample_rate_hz"]
    count = gaps = missing = invalid = sensor_flags = time_gaps = errors = 0
    first = previous = None; last = None; dt_min = None; dt_max = None; dt_sum = 0; dt_n = 0
    # Constant-space histogram, not storing all dt of multi-hour runs.
    histogram = {}
    with (path/"samples.csv").open(newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != COLUMNS:
            raise ValueError("Unexpected CSV columns")
        for row in reader:
            values = [int(row[k]) if k not in ("qw", "qx", "qy", "qz") else float(row[k]) for k in COLUMNS]
            seq, t = values[:2]; q = values[9:13]; flags = values[13]; count += 1
            if not all(math.isfinite(x) for x in q) or abs(sum(x*x for x in q)-1)>1e-4:
                errors += 1
            if not all(-32768 <= x <= 32767 for x in values[2:9]):
                errors += 1
            if flags & 1: invalid += 1
            if flags & 16: sensor_flags += 1
            if first is None:
                first = (seq, t)
                expected = m.get("recording_start_seq")
                if expected is not None and seq > expected: gaps += 1; missing += seq-expected
            if previous:
                delta = seq-previous[0]; dt = t-previous[1]
                if delta <= 0 or dt <= 0: errors += 1
                if delta > 1: gaps += 1; missing += delta-1
                if dt <= 0 or dt > 2.5e6/rate: time_gaps += 1
                if delta == 1 and dt > 0:
                    dt_min = dt if dt_min is None else min(dt_min, dt); dt_max = dt if dt_max is None else max(dt_max, dt)
                    dt_sum += dt; dt_n += 1
                    bin_us = int(round(dt/100.0))*100
                    if len(histogram)<4096 or bin_us in histogram: histogram[bin_us] = histogram.get(bin_us, 0)+1
            previous = last = (seq, t)
    quality = dict(unique_samples=count, irrecoverable_gaps=gaps, irrecoverable_missing_samples=missing,
                   orientation_invalid=invalid, sensor_gap_flags=sensor_flags, time_gaps=time_gaps,
                   structural_errors=errors, dt_min_us=dt_min, dt_max_us=dt_max,
                   dt_mean_us=(dt_sum/dt_n if dt_n else None), dt_histogram_100us=histogram,
                   measured_rate_hz=((count-1)*1e6/(last[1]-first[1]) if count>1 and last[1]>first[1] else None),
                   completeness="EMPTY" if not count else "GAPS" if missing or time_gaps or sensor_flags else "NO_DETECTED_GAPS",
                   note="seq continuity does not prove no pre-record sensor loss; inspect events and device counters")
    counter_names = ("fifo_overflows", "irq_overflows", "sync_errors", "read_errors", "resyncs", "alignment_discards", "transport_drops")
    first_counters = last_counters = None
    events_path = path/"events.jsonl"
    event_errors = 0
    if events_path.exists():
        with events_path.open(encoding="utf-8") as f:
            for line in f:
                try:
                    event = json.loads(line)
                except (ValueError, UnicodeError):
                    event_errors += 1
                    continue
                status = event.get("status")
                if isinstance(status, dict) and "resyncs" in status:
                    counters = {key: status.get(key, 0) for key in counter_names}
                    if first_counters is None: first_counters = counters
                    last_counters = counters
    quality.update(device_counters_first=first_counters, device_counters_last=last_counters,
                   device_counter_delta_observed=({k:last_counters[k]-first_counters[k] for k in counter_names} if first_counters is not None else None),
                   malformed_event_lines=event_errors)
    return quality


def compare_runs(a, b):
    def metadata(p):
        with (Path(p)/"metadata.json").open() as f: return json.load(f)
    ma, mb = metadata(a), metadata(b)
    for key in ("device_uid", "boot_id", "stream_id", "config_id"):
        if ma[key] != mb[key]: raise ValueError("Different full record keys: "+key)
    def rows(p):
        with (Path(p)/"samples.csv").open(newline="") as f:
            for r in csv.reader(f):
                if r[0] != "seq": yield r
    ia, ib = iter(rows(a)), iter(rows(b)); ra, rb = next(ia, None), next(ib, None)
    shared = mismatch = only_a = only_b = 0
    while ra is not None or rb is not None:
        if rb is None or (ra is not None and int(ra[0])<int(rb[0])): only_a += 1; ra = next(ia, None)
        elif ra is None or int(rb[0])<int(ra[0]): only_b += 1; rb = next(ib, None)
        else:
            shared += 1
            # Floats recorded from IEEE754 float32; compare value, not host arrival.
            if [float(x) for x in ra[2:]] != [float(x) for x in rb[2:]] or ra[:2] != rb[:2]: mismatch += 1
            ra, rb = next(ia, None), next(ib, None)
    return dict(shared=shared, mismatches=mismatch, only_a=only_a, only_b=only_b, result="PASS" if shared and not mismatch else "FAIL")
