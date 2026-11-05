"""Verify four REAL compile databases, default sensor config and embedded assets."""
import json
from pathlib import Path
for n in range(1,5):
    directory=Path("build-%02d"%n)
    database=json.loads((directory/"compile_commands.json").read_text())
    app=[row for row in database if row["file"].endswith("/main/app.c")]
    assert len(app)==1 and "-DLAB_DEVICE_ID=%d"%n in app[0]["command"]
    config=(directory/"config/sdkconfig.h").read_text()
    for setting in ("#define CONFIG_HTTPD_WS_SUPPORT 1", "#define CONFIG_LWIP_MAX_SOCKETS 16", "#define CONFIG_ESP_CONSOLE_NONE 1"):
        assert setting in config,(n,setting)
    assert (directory/"snar_imu_lab.bin").stat().st_size<0x1f0000
    print("PASS build-%02d: compile definition, websocket, socket budget, clean UART, image fits"%n)
