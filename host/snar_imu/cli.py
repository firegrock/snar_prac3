"""CLI и меню; один владелец сокета, все файлы записываются постепенно."""
import argparse
import csv
import json
import select
import sys
import time
from pathlib import Path
from .client import Client, SerialLink, WSLink, IdentityError, DeviceError
from .protocol import Packet
from .storage import RunWriter, validate_run, compare_runs, write_json


def dump(value):
    print(json.dumps(value, ensure_ascii=False, indent=2), flush=True)


def make_client(args):
    factory = (lambda: SerialLink(args.serial)) if args.serial else (lambda: WSLink(args.ws))
    return Client(factory, args.device_id, args.uid)


def calibration_csv(cal, path):
    if Path(path).exists(): raise FileExistsError(str(path))
    names = ["gbx", "gby", "gbz", "abx", "aby", "abz", "asx", "asy", "asz", "temperature_c"]
    with Path(path).open("w", newline="") as f:
        w = csv.writer(f); w.writerow(names)
        w.writerow(cal["gyro_bias"]+cal["accel_bias"]+cal["accel_scale"]+[cal["temperature_c"]])


def recording(client, args, calibration=None, keep_open=False):
    base = Path(args.output)
    if base.exists():
        raise FileExistsError("Каталог уже существует; перезапись запрещена: "+str(base))
    if not client.link: client.connect()
    status = client.call("get_status")["result"]
    if not status["sensor_ok"]:
        raise DeviceError("Измерительный тракт в ошибке: "+status["sensor_error"])
    started = not args.observe and calibration is None
    start_confirmed = False
    try:
        if started:
            client.call("start_experiment"); start_confirmed = True
        metadata = client.call("get_config")["result"]
        metadata["recording_start_seq"] = metadata["initial_state"]["before_seq"] if started else None
        writer = RunWriter(base, metadata, client.link.name)
    except BaseException:
        if start_confirmed:
            try: client.call("end_experiment")
            except Exception: print("Начало подтверждено, завершение НЕ подтверждено; проверьте owner/status.", file=sys.stderr)
        raise
    event_cursor = 0; segment = 1; pending_resume = False; calibration_completed = False
    last_packet = time.monotonic()

    def incoming(value):
        nonlocal pending_resume, last_packet
        if isinstance(value, Packet) and value.kind == 1:
            if value.key != writer.key:
                raise ConnectionError("Новая граница boot/stream — требуется новый сегмент")
            rows = value.samples()
            if writer.last is not None and rows[0][0] > writer.last+1:
                # Do NOT ACK past a missing transport packet; recover from last flushed.
                writer.event("DELIVERY_HOLE", expected=writer.last+1, received=rows[0][0])
                pending_resume = True
                return
            writer.packet(value)
            # Samples consumed while waiting for RPC replies are still live
            # traffic. Otherwise a phase alignment can cause a false timeout,
            # needless serial reopen and an unintended bridge-induced reboot.
            last_packet = time.monotonic()
        elif isinstance(value, dict) and "event" in value:
            writer.event(value["event"], device_event=value)
            if value["event"] == "GAP":
                # Explicitly advance continuity anchor; missing interval stays in events/quality.
                oldest = value["oldest_seq"]
                writer.event("IRRECOVERABLE", from_seq=(writer.last+1 if writer.last is not None else None), to_seq=oldest-1)
                writer.last = oldest-1 if oldest else None
                pending_resume = False
            if value["event"] == "STREAM_BOUNDARY":
                raise ConnectionError("Stream changed")

    client.on_message = incoming
    try:
        client.subscribe(metadata, from_seq=metadata["initial_state"]["before_seq"] if started else None)
        if calibration:
            client.call("cal_gyro" if calibration == "gyro" else "cal_accel")
            print("CALIBRATION: raw сохраняются. Шесть положений: вводите +X,-X,+Y,-Y,+Z,-Z после установки; q — отмена.")
        else:
            print("Файловая запись: %s. Ctrl+C — завершить. Метки: --interactive и 'm текст'." % writer.path, flush=True)
        begin = time.monotonic(); last_packet = time.monotonic(); last_ack = last_poll = 0
        while not args.duration or time.monotonic()-begin < args.duration:
            try:
                value = client.link.recv(0.1)
                if value is not None:
                    incoming(client.message(value))
                    if isinstance(value, Packet) and value.kind == 1: last_packet = time.monotonic()
                now = time.monotonic()
                if pending_resume:
                    pending_resume = False
                    client.subscribe(metadata, last=writer.last, mode="history")
                if writer.last is not None and now-last_ack>0.4:
                    client.ack(writer.last); last_ack = now
                if now-last_poll>0.8:
                    ev = client.call("get_events", after=event_cursor)
                    for e in ev.get("events", []): writer.event("DEVICE_EVENT", device_event=e)
                    if ev.get("detail_unavailable"): writer.event("EVENT_DETAIL_UNAVAILABLE")
                    event_cursor = ev.get("next_event", event_cursor)
                    st = client.call("get_status")["result"]
                    writer.event("DEVICE_STATUS", status=st)
                    if not st["sensor_ok"]: print("SENSOR ERROR: "+st["sensor_error"], file=sys.stderr)
                    if calibration and st["wizard"]["state"] in (-1, 4):
                        if st["wizard"]["state"] == -1: raise DeviceError("Калибровка отклонена: "+st["wizard"]["reason"])
                        cal = client.call("cal_export", source="candidate")["result"]
                        write_json(writer.path/"candidate.json", cal); calibration_csv(cal, writer.path/"candidate_numeric.csv")
                        calibration_completed = True
                        print("Кандидат сохранён. НЕ применён. Отдельно: command cal_apply, command cal_save.")
                        break
                    last_poll = now
                if now-last_packet>3:
                    # A lease-expired subscription is not necessarily a broken
                    # serial port. Probe/resume on the SAME open link first:
                    # reopening some USB bridges would itself reset the board.
                    probe=client.call("get_info")["result"]
                    if probe["device_id"]!=client.device_id or str(probe["device_uid"])!=client.uid:
                        raise IdentityError("Идентичность изменилась на открытом канале")
                    if tuple(str(probe[k]) for k in ("device_uid","boot_id","stream_id"))!=writer.key:
                        raise ConnectionError("Новый boot/stream на открытом канале")
                    reply=client.subscribe(metadata,last=writer.last,mode="history")
                    writer.event("SUBSCRIPTION_RESUMED",reply=reply)
                    last_packet=time.monotonic()
                interactive = getattr(args, "interactive", False) or calibration == "six"
                if interactive and sys.stdin.isatty() and select.select([sys.stdin], [], [], 0)[0]:
                    text = sys.stdin.readline().strip()
                    if text == "q": break
                    if text.startswith("m "): client.call("mark", text=text[2:98])
                    if calibration == "six" and text in ("+X", "-X", "+Y", "-Y", "+Z", "-Z"):
                        client.call("cal_face", face=("+X", "-X", "+Y", "-Y", "+Z", "-Z").index(text))
            except IdentityError:
                raise
            except (ConnectionError, TimeoutError, OSError) as exc:
                writer.event("DISCONNECT", reason=str(exc)); print("Разрыв; reconnect/resume: "+str(exc), file=sys.stderr)
                while True:
                    try:
                        # Do not process pre-verification packets with an old writer.
                        client.on_message = lambda value: None
                        info = client.connect()
                        new_key = tuple(str(info[k]) for k in ("device_uid", "boot_id", "stream_id"))
                        if new_key != writer.key:
                            if calibration: raise DeviceError("Калибровка прервана границей boot/stream; кандидат не подтверждён")
                            writer.event("BOUNDARY", new_identity=info); writer.close(); segment += 1
                            print("Новый boot/stream. Начинается ОТДЕЛЬНЫЙ сегмент %d, старое время не склеивается." % segment)
                            if started: client.call("start_experiment")
                            metadata = client.call("get_config")["result"]
                            metadata["recording_start_seq"] = metadata["initial_state"]["before_seq"] if started else None
                            writer = RunWriter(str(base)+"_segment_%03d" % segment, metadata, client.link.name)
                            event_cursor = 0
                        client.on_message = incoming
                        reply = client.subscribe(metadata, last=writer.last, mode="history",
                                                 from_seq=metadata.get("recording_start_seq") if started and writer.last is None else None)
                        if reply.get("gap"): writer.event("RESUME_GAP", available=reply)
                        writer.event("RECONNECTED"); last_packet = time.monotonic(); break
                    except IdentityError: raise
                    except (ConnectionError, TimeoutError, OSError): time.sleep(0.3)
    except KeyboardInterrupt:
        print("\nЗавершение записи…", flush=True)
    finally:
        client.on_message = lambda value: None
        try:
            client.call("unsubscribe")
            if started: client.call("end_experiment")
            if calibration and not calibration_completed: client.call("cal_cancel")
        except Exception as e:
            writer.event("END_NOT_CONFIRMED", reason=str(e))
            print("Завершение на устройстве не подтверждено; проверьте status.", file=sys.stderr)
        framer = getattr(client.link, "framer", None)
        writer.frame_errors = framer.errors if framer else getattr(client.link, "errors", 0)
        writer.close()
        if not keep_open: client.close()
        dump(json.loads((writer.path/"quality.json").read_text()))


def menu(args):
    client = make_client(args); client.connect()
    try:
        while True:
            print("\n1 статус | 2 запись | 3 мастер gyro | 4 мастер six | 5 экспорт | 6 импорт | 7 применить | 8 сохранить NVS | 9 команда | 0 выход")
            choice = input("> ").strip()
            if choice == "0": break
            if choice == "1": dump(client.call("get_status"))
            elif choice in ("2", "3", "4"):
                output = input("Новый каталог опыта: ").strip()
                record_args = argparse.Namespace(output=output, duration=0, observe=False, interactive=True)
                recording(client, record_args, {"3": "gyro", "4": "six"}.get(choice), keep_open=True)
            elif choice == "5":
                p = Path(input("Новый JSON-файл: ").strip())
                if p.exists(): raise FileExistsError(str(p))
                cal = client.call("cal_export")["result"]; write_json(p, cal); calibration_csv(cal, p.with_suffix(".csv"))
            elif choice == "6":
                cal = json.loads(Path(input("JSON: ").strip()).read_text())
                transfer = cal["device_uid"] != client.uid and input("Другая плата. Для переноса введите TRANSFER: ") == "TRANSFER"
                dump(client.call("cal_import", calibration=cal, confirm_transfer=transfer))
            elif choice in ("7", "8"):
                if input("Подтвердите YES: ") == "YES": dump(client.call("cal_apply" if choice == "7" else "cal_save"))
            elif choice == "9":
                cmd = input("Имя команды: ").strip(); params = json.loads(input("Аргументы JSON, например {}: ") or "{}")
                dump(client.call(cmd, **params))
    except (KeyboardInterrupt, EOFError): pass
    finally: client.close()


def parser():
    p = argparse.ArgumentParser(description="СНАР IMU: USB/Wi-Fi регистратор (не управление Comau)")
    sub = p.add_subparsers(dest="action", required=True)
    sub.add_parser("ports", help="список последовательных портов, без автоподключения")
    for action in ("menu", "status", "command", "record", "calibrate", "export-cal", "import-cal"):
        s = sub.add_parser(action)
        group = s.add_mutually_exclusive_group(required=True); group.add_argument("--serial"); group.add_argument("--ws")
        s.add_argument("--device-id", type=int, choices=range(1, 5), required=True); s.add_argument("--uid")
        if action == "command": s.add_argument("name"); s.add_argument("--params", default="{}")
        if action in ("record", "calibrate"):
            s.add_argument("--output", required=True); s.add_argument("--duration", type=float, default=0); s.add_argument("--interactive", action="store_true"); s.add_argument("--observe", action="store_true")
        if action == "calibrate": s.add_argument("--kind", choices=("gyro", "six"), required=True)
        if action in ("export-cal", "import-cal"): s.add_argument("file")
        if action == "export-cal": s.add_argument("--candidate", action="store_true")
        if action == "import-cal": s.add_argument("--confirm-transfer", action="store_true")
    s = sub.add_parser("validate"); s.add_argument("path")
    s = sub.add_parser("compare"); s.add_argument("a"); s.add_argument("b")
    s = sub.add_parser("cal-from-csv"); s.add_argument("csv"); s.add_argument("--base", required=True); s.add_argument("--output", required=True)
    return p


def main():
    args = parser().parse_args()
    try:
        if args.action == "ports":
            from serial.tools import list_ports
            for p in list_ports.comports(): print("%s\t%s\t%s" % (p.device, p.description, p.hwid))
        elif args.action == "validate":
            q = validate_run(args.path); dump(q)
            if q["structural_errors"] or not q["unique_samples"]: sys.exit(2)
        elif args.action == "compare":
            result = compare_runs(args.a, args.b); dump(result)
            if result["result"] != "PASS": sys.exit(2)
        elif args.action == "cal-from-csv":
            path = Path(args.output)
            if path.exists(): raise FileExistsError(str(path))
            cal = json.loads(Path(args.base).read_text())
            with Path(args.csv).open(newline="") as f:
                rows = list(csv.reader(f))
            if len(rows) != 2 or len(rows[1]) != 10: raise ValueError("Ожидается заголовок и одна строка из 10 чисел")
            values = [float(x) for x in rows[1]]
            import math
            if not all(math.isfinite(v) for v in values) or any(not 0.5 <= v <= 1.5 for v in values[6:9]): raise ValueError("Неверные коэффициенты")
            cal.update(gyro_bias=values[:3], accel_bias=values[3:6], accel_scale=values[6:9], temperature_c=values[9], method="OCTAVE_AXIS_MODEL")
            write_json(path, cal)
        elif args.action == "menu": menu(args)
        else:
            client = make_client(args)
            if args.action in ("record", "calibrate"):
                try: recording(client, args, args.kind if args.action == "calibrate" else None)
                finally: client.close()
            else:
                try:
                    client.connect()
                    if args.action == "status": dump(client.call("get_status"))
                    elif args.action == "command": dump(client.call(args.name, **json.loads(args.params)))
                    elif args.action == "export-cal":
                        path = Path(args.file)
                        if path.exists(): raise FileExistsError(str(path))
                        cal = client.call("cal_export", source="candidate" if args.candidate else "active")["result"]
                        write_json(path, cal); calibration_csv(cal, path.with_suffix(".csv"))
                    elif args.action == "import-cal":
                        dump(client.call("cal_import", calibration=json.loads(Path(args.file).read_text()), confirm_transfer=args.confirm_transfer))
                finally: client.close()
    except KeyboardInterrupt:
        print("\nОтменено пользователем.", file=sys.stderr); sys.exit(130)
    except (ValueError, RuntimeError, OSError, TimeoutError) as exc:
        print("ОШИБКА: "+str(exc), file=sys.stderr); sys.exit(2)
