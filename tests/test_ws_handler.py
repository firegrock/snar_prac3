"""Compile the actual production handler with SDK socket mocks, not a rewrite."""
from pathlib import Path
import subprocess
import tempfile
import unittest


class HandlerRegression(unittest.TestCase):
    def test_first_frame_without_handshake_callback(self):
        root=Path(__file__).resolve().parents[1]
        source=(root/"main/transport.c").read_text()
        start=source.index("static esp_err_t ws_handler(")
        end=source.index("\nextern const char html_start",start)
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory)
            (d/"ws_handler_under_test.inc").write_text(source[start:end])
            binary=d/"ws_handler_test"
            subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror","-I"+str(d),str(root/"tests/ws_handler_harness.c"),"-o",str(binary)],check=True)
            result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
            self.assertIn("PASS production WS handler",result.stdout)


if __name__=="__main__":unittest.main()
