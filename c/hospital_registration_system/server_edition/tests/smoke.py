"""Local smoke test. Uses temporary data and real TCP clients."""
import concurrent.futures
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
MAGIC = 0x31535248
REQUEST = struct.Struct("<IIqqii16s16s65s")
HEADER = struct.Struct("<IIIIi")
DAY = struct.Struct("<q7i")
APP = struct.Struct("<qiiqq4i")
OK, AUTH, BAD, UNAVAILABLE, EMPTY, BUSY = range(6)
(LOGIN, REGISTER, LOGOUT, AI, DEPTS, DOCTORS, DAYS, PENDING, BOOK, PAY,
 APPS, RESCHEDULE, PAYMENTS, QUEUE, CALL, FINISH, CAPACITY) = range(1, 18)


class Connection:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=10)

    def close(self):
        self.socket.close()

    def read(self, length):
        out = bytearray()
        while len(out) < length:
            part = self.socket.recv(length - len(out))
            if not part:
                raise RuntimeError("Server disconnected")
            out.extend(part)
        return out

    def ask(self, op, id=0, visit=0, a=0, b=0, name=b"", phone=b"", password=b"", split=False):
        req = REQUEST.pack(MAGIC, op, id, visit, a, b, name, phone, password)
        if split:
            self.socket.sendall(req[:3])
            time.sleep(0.03)
            req = req[3:]
        self.socket.sendall(req)
        magic, status, rows, size, next_offset = HEADER.unpack(self.read(HEADER.size))
        assert magic == MAGIC and size <= 22800 and next_offset == -1
        data = self.read(size)
        return status, rows, data

    def login(self, id, password=b"test-only", role=1):
        return self.ask(LOGIN, id=id, a=role, password=password)[0]


def available_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def launch(exe, port, log):
    with log.open("ab") as out:
        return subprocess.Popen([str(exe), "127.0.0.1", str(port)], cwd=ROOT,
                                stdin=subprocess.DEVNULL, stdout=out, stderr=out)


def wait_server(proc, port):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("Server exited during startup")
        try:
            probe = Connection(port)
        except OSError:
            time.sleep(0.05)
        else:
            probe.close()
            return
    raise RuntimeError("Server did not start")


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        proc.wait(timeout=10)


def app_id(reply):
    status, rows, data = reply
    assert (status, rows) == (OK, 1)
    return APP.unpack(data)[0]


def exercise(port):
    doc_id = 100001
    id_a, id_b = 100000001, 100000002
    a, b, doctor, guest = (Connection(port) for _ in range(4))
    try:
        assert guest.ask(AI, split=True)[0] == AUTH
        assert guest.ask(DEPTS)[0] == AUTH
        for conn, id in ((a, id_a), (b, id_b)):
            assert conn.ask(REGISTER, id=id, a=0, b=20000229, name=b"Demo",
                            phone=b"0123456789", password=b"test-only")[0] == OK
            assert conn.login(id) == OK
        assert guest.login(id_a) == BUSY
        assert a.ask(AI)[0] == OK
        assert doctor.login(doc_id, b"demo101", 2) == OK
        assert doctor.ask(AI)[0] == AUTH
        assert doctor.ask(CAPACITY, a=1, b=1)[0] == OK
        assert a.ask(CAPACITY, a=99, b=99)[0] == AUTH
        assert doctor.ask(PAYMENTS)[0] == AUTH
        status, rows, data = a.ask(DAYS, a=doc_id)
        assert (status, rows) == (OK, 7)
        days = list(DAY.iter_unpack(data))
        today, tomorrow, later = [row[0] for row in days[:3]]
        barrier = threading.Barrier(2)

        def reserve(conn):
            barrier.wait(timeout=5)
            return conn.ask(BOOK, visit=tomorrow, a=doc_id, b=0)

        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            f_a, f_b = pool.submit(reserve, a), pool.submit(reserve, b)
            replies = f_a.result(), f_b.result()
        assert sorted(reply[0] for reply in replies) == [OK, UNAVAILABLE]
        winner_idx = 0 if replies[0][0] == OK else 1
        winner, loser = (a, b) if winner_idx == 0 else (b, a)
        winner_id = id_a if winner_idx == 0 else id_b
        booking_id = app_id(replies[winner_idx])
        assert loser.ask(PAY, id=booking_id, a=1)[0] == UNAVAILABLE
        assert loser.ask(APPS)[1] == 0
        assert doctor.ask(CAPACITY, a=0, b=1)[0] == UNAVAILABLE
        assert winner.ask(PAY, id=booking_id, a=1)[0] == OK
        assert winner.ask(PAY, id=booking_id, a=1)[0] == UNAVAILABLE
        assert winner.ask(PAYMENTS)[1] == 1
        assert loser.ask(PAYMENTS)[1] == 0
        assert doctor.ask(CALL)[0] == EMPTY

        current_period = 0 if days[0][6] else 1
        assert days[0][6 + current_period]
        call_id = app_id(loser.ask(BOOK, visit=today, a=doc_id, b=current_period))
        assert loser.ask(PAY, id=call_id, a=1)[0] == OK
        assert loser.ask(QUEUE)[1] == 1
        status, rows, data = doctor.ask(CALL)
        assert (status, rows, struct.unpack_from("<q", data)[0]) == (OK, 1, call_id)
        assert doctor.ask(FINISH, id=booking_id, a=2)[0] == UNAVAILABLE
        assert doctor.ask(FINISH, id=call_id, a=2)[0] == OK
        assert winner.ask(RESCHEDULE, id=call_id, visit=later, b=0)[0] == UNAVAILABLE
        assert loser.ask(RESCHEDULE, id=call_id, visit=later, b=0)[0] == OK
        assert loser.ask(PAYMENTS)[1] == 1
        assert winner.ask(LOGOUT)[0] == OK
        assert winner.ask(AI)[0] == AUTH
        assert loser.ask(AI)[0] == OK
        print("Concurrent booking, role isolation, duplicate login, payment, queue, call and reschedule passed.")
        return winner_id, booking_id
    finally:
        for conn in a, b, doctor, guest:
            conn.close()


def main():
    with tempfile.TemporaryDirectory(prefix="hospital_server_test_") as path:
        sandbox = Path(path)
        server_dir = sandbox / "server"
        tools_dir = sandbox / "tools"
        server_dir.mkdir()
        tools_dir.mkdir()
        origin = ROOT / "server"
        for name in ("data_tool.exe", "departments.txt", "doctors.txt"):
            shutil.copyfile(ROOT / "tools" / name, tools_dir / name)
        init = subprocess.run([str(tools_dir / "data_tool.exe")], cwd=sandbox,
                              input=b"1\n2\n\n2\n2\n\n0\n", capture_output=True, timeout=20)
        assert init.returncode == 0 and b"Saved departments. Total: 2." in init.stdout
        assert b"Saved doctors. Total: 2." in init.stdout
        exe = server_dir / "hospital_server.exe"
        shutil.copyfile(origin / exe.name, exe)
        log = sandbox / "server.log"
        port = available_port()
        proc = launch(exe, port, log)
        try:
            wait_server(proc, port)
            other = subprocess.run([str(exe), "127.0.0.1", str(available_port())],
                                   capture_output=True, timeout=8)
            assert other.returncode and b"data/.lock" in other.stderr

            winner_id, booking_id = exercise(port)
            # Move the client and use a local replacement script. No AI API request is made.
            client_dir = sandbox / "clients" / "console"
            client_dir.mkdir(parents=True)
            console = client_dir / "hospital_console.exe"
            shutil.copyfile(ROOT / "clients/console" / console.name, console)
            common_resource = sandbox / "clients" / "common" / "resource"
            common_resource.mkdir(parents=True)
            (common_resource / "rules.txt").write_text("Shared rules marker\n", encoding="ascii")
            local_ai = sandbox / "clients" / "local_ai"
            local_ai.mkdir()
            (local_ai / "ai_assistant.py").write_text("print('Local AI started', flush=True)\n")
            env = os.environ.copy()
            env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")
            text = f"1\n{winner_id}\ntest-only\n3\n6\n5\n0\n0\n"
            cli = subprocess.run([str(console), "127.0.0.1", str(port)], input=text.encode(),
                                 capture_output=True, timeout=10, cwd=sandbox, env=env)
            assert cli.returncode == 0 and str(booking_id).encode() in cli.stdout
            assert b"Local AI started" in cli.stdout
            assert b"Shared rules marker" in cli.stdout
            gui = ROOT / "clients/easyx/hospital_easyx.exe"
            cli = subprocess.run([str(gui), "127.0.0.1", str(port)], input=b"0\n",
                                 capture_output=True, timeout=10, cwd=sandbox)
            assert cli.returncode == 0 and b"Connected" in cli.stdout
            print("Both clients connected from a different working directory and the console launched local AI.")
        finally:
            stop(proc)

        proc = launch(exe, port, log)
        try:
            wait_server(proc, port)
            patient = Connection(port)
            try:
                assert patient.login(winner_id) == OK
                assert patient.ask(PAYMENTS)[1] == 1
                assert app_id(patient.ask(APPS)) == booking_id
            finally:
                patient.close()
        finally:
            stop(proc)
        assert b"test-only" not in log.read_bytes()
        assert b"demo101" not in log.read_bytes()
        print("Server restart preserved the booking and payment. Logs omitted passwords.")


if __name__ == "__main__":
    main()
