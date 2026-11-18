"""Exercise the offline tool without touching the project's database."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

from smoke import ROOT, available_port, launch, stop, wait_server


def main():
    with tempfile.TemporaryDirectory(prefix="hospital_data_tool_") as tmp:
        temp = Path(tmp)
        server = temp / "server"
        server.mkdir()
        tools = temp / "tools"
        tools.mkdir()
        for name in ("data_tool.exe", "departments.txt", "doctors.txt"):
            shutil.copyfile(ROOT / "tools" / name, tools / name)

        def tool(*commands, expected_exit=0):
            result = subprocess.run([str(tools / "data_tool.exe")], cwd=temp,
                                    input=("\n".join(commands) + "\n").encode("ascii"),
                                    capture_output=True, timeout=15)
            assert result.returncode == expected_exit, result.stderr
            return (result.stdout + result.stderr).decode("gbk", errors="replace")

        assert "Saved departments. Total: 2." in tool("1", "2", "", "0")
        assert "Saved doctors. Total: 2." in tool("2", "2", "", "0")
        assert "[hidden]" in tool("2", "1", "0")
        assert "demo101" not in tool("2", "1", "0")
        data = server / "data"

        extra = tools / "extra.txt"
        extra.write_text("10101 DontOverwrite Site Text\n30101 Proposed Site Text\n30101 Duplicate Site Text\n",
                         encoding="ascii")
        out = tool("1", "2", "extra.txt", "1", "1", "0")
        assert "Duplicate keys skipped: 2." in out and "30101 Proposed" in out
        assert "DontOverwrite" not in out and "30101 Duplicate" not in out
        assert "30101 Revised" in tool("1", "3", "30101", "30101 Revised Site Text", "1", "1", "0")
        assert "Saved departments. Total: 2." in tool("1", "4", "30101", "0")

        current = (data / "departments.bin").read_bytes()
        extra.write_text("30101 Proposed Site Text\nbroken\n", encoding="ascii")
        assert "line 2. No changes saved." in tool("1", "6", "extra.txt", "0")
        assert "missing department" in tool("1", "5", "YES", "0")
        assert (data / "departments.bin").read_bytes() == current
        extra.write_text("20101 SyncTwo Site Text\n10101 SyncOne Site Text\n10101 Ignore Site Text\n",
                         encoding="ascii")
        assert "Saved departments. Total: 2." in tool("1", "6", "extra.txt", "YES", "0")
        listed = tool("1", "1", "0")
        assert "10101 SyncOne" in listed and "20101 SyncTwo" in listed and "Ignore" not in listed

        (tools / "patients.txt").write_text("100000001 Demo 1 20000229 0100 test-only\n", encoding="ascii")
        assert "Saved patients. Total: 1." in tool("3", "2", "", "0")
        now = int(time.time())
        today = time.localtime(now)
        midnight = int(time.mktime((today.tm_year, today.tm_mon, today.tm_mday, 0, 0, 0, 0, 0, -1)))
        app = f"1 100000001 100001 {midnight} 0"
        (tools / "appointments.txt").write_text(f"{app} 4 {now}\n", encoding="ascii")
        (tools / "payments.txt").write_text(f"1 1 30 {now}\n", encoding="ascii")
        assert "Saved appointments. Total: 1." in tool("4", "2", "", "0")
        assert "Saved payments. Total: 1." in tool("5", "2", "", "0")
        assert "Saved appointments. Total: 1." in tool("4", "3", "1", f"{app} 3 {now}", "0")
        assert "conflicts with its payment" in tool("5", "4", "1", "0")
        assert "missing appointment" in tool("4", "5", "YES", "0")
        assert "Saved appointments. Total: 1." in tool("4", "3", "1", f"{app} 4 {now}", "0")
        assert "Saved payments. Total: 0." in tool("5", "5", "YES", "0")
        assert "Saved appointments. Total: 0." in tool("4", "5", "YES", "0")

        exe = server / "hospital_server.exe"
        shutil.copyfile(ROOT / "server" / exe.name, exe)
        port = available_port()
        proc = launch(exe, port, temp / "server.log")
        try:
            wait_server(proc, port)
            assert "Cannot lock server/data" in tool("0", expected_exit=1)
        finally:
            stop(proc)
    print("Offline catalog initialization and five-table import, dedupe, edit, delete, clear, sync, and lock passed.")


if __name__ == "__main__":
    main()
