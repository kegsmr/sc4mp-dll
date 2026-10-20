"""Throwaway fake launcher: listens like GameBridge, runs the harness with SC4MP_BRIDGE_PORT, and checks what arrives."""
import json, os, socket, subprocess, sys, time

listener = socket.socket()
listener.bind(("127.0.0.1", 0))
listener.listen(1)
listener.settimeout(20)
port = listener.getsockname()[1]
print("listening on", port, flush=True)

proc = subprocess.Popen([sys.argv[1]], env=dict(os.environ, SC4MP_BRIDGE_PORT=str(port)))

def read_lines(conn, count):
	f = conn.makefile("rb")
	out = []
	for _ in range(count):
		line = f.readline()
		if not line:
			break
		out.append(json.loads(line.decode("utf-8")))
		print("<- from DLL:", line.decode("utf-8").rstrip(), flush=True)
	return out

# First connection: hello + 4 queued events, then reply and drop it
conn, _ = listener.accept()
first = read_lines(conn, 5)
for msg in [{"type": "notify", "text": "Keg: café \U0001F600"}, {"type": "something_new"}, "not json"]:
	data = (msg if isinstance(msg, str) else json.dumps(msg)) + "\n"
	print("-> to DLL:", data.rstrip(), flush=True)
	conn.sendall(data.encode())
time.sleep(0.5)
conn.close()
print("dropped the connection", flush=True)

# Second connection: hello again + city_exited
conn, _ = listener.accept()
second = read_lines(conn, 2)

proc.wait(timeout=30)
types1 = [m["type"] for m in first]
types2 = [m["type"] for m in second]
print("first:", types1)
print("second:", types2)
assert types1 == ["hello", "city_loaded", "pre_save", "saved", "chat"], types1
assert first[1]["name"] == "Café \"Town\"" and first[1]["established"] is True
assert all(isinstance(m["time"], int) for m in first + second)
assert types2 == ["hello", "city_exited"], types2
print("ALL CHECKS PASSED")
