"""Exercises the HTTP API of a real clock, which tests/ cannot do without one.

Usage: python tools/check_api.py HOST STORE_FILE [REPORT_FILE]

It replaces the store of the clock with STORE_FILE and changes its timezone, so use a clock that is
not in service. Needs aioesphomeapi, which ESPHome installs, as the reference for the timezones.
"""
import base64
import hashlib
import http.client
import json
import socket
import sys
import time

HOST, STORE = sys.argv[1], sys.argv[2]
REPORT = sys.argv[3] if len(sys.argv) > 3 else None
results = []


def call(method, path, body=None, headers=None, password=None, timeout=60):
    headers = dict(headers or {})
    if method == "POST":
        headers.setdefault("X-Dekatron", "1")
    if password is not None:
        headers["Authorization"] = "Basic " + base64.b64encode(f"admin:{password}".encode()).decode()
    if isinstance(body, (dict, list)):
        body = json.dumps(body).encode()
        headers.setdefault("Content-Type", "application/json")
    elif body is not None:
        headers.setdefault("Content-Type", "application/octet-stream")
    connection = http.client.HTTPConnection(HOST, 80, timeout=timeout)
    started = time.time()
    connection.request(method, path, body=body, headers=headers)
    response = connection.getresponse()
    data = response.read()
    elapsed = time.time() - started
    connection.close()
    try:
        parsed = json.loads(data) if response.getheader("Content-Type", "").startswith("application/json") else data
    except ValueError:
        parsed = data
    return response.status, parsed, elapsed


def check(name, condition, detail=""):
    results.append((name, bool(condition), detail))
    print(("PASS " if condition else "FAIL ") + name + (f"  {detail}" if detail else ""), flush=True)


store = open(STORE, "rb").read()

status, info, _ = call("GET", "/api/info")
check("info is public", status == 200 and info.get("product") == "dekatron", info)

status, state, _ = call("GET", "/api/state")
check("state answers", status == 200 and "store" in state, state["store"])
print("   free heap", state["free_heap"], "largest", state["largest_block"], "faces", state["faces"], flush=True)

status, error, _ = call("POST", "/api/stop", headers={"X-Dekatron": ""})
check("post without X-Dekatron header is refused", status == 400, (status, error))
connection = http.client.HTTPConnection(HOST, 80, timeout=10)
connection.request("POST", "/api/stop", body=b"x=1", headers={"Content-Type": "application/x-www-form-urlencoded"})
response = connection.getresponse()
response.read()
check("form post is refused", response.status == 400, response.status)
connection.close()

status, error, _ = call("GET", "/api/nothing")
check("unknown call is 404", status == 404, (status, error))

# Upload while an effect is drawing.
status, played, _ = call("POST", "/api/play", {"type": "effect", "name": "matrix", "duration": 40})
check("play matrix", status == 200, (status, played))
time.sleep(2)
status, state, _ = call("GET", "/api/state")
print("   animation", state["animation"], flush=True)
status, uploaded, took = call("POST", "/api/store", store)
check("store upload", status == 200 and uploaded.get("ok") is True, uploaded)
if status == 200:
    ms, frames = uploaded["ms"], uploaded["frames"]
    print(f"   {len(store)} bytes in {ms} ms ({len(store) / ms * 1000 / 1024:.0f} KB/s), {frames} frames drawn meanwhile "
          f"= {frames / (ms / 1000):.1f} fps, round trip {took:.1f} s", flush=True)
status, state, _ = call("GET", "/api/state")
check("store is ready", state["store"]["ready"] is True, state["store"])
check("faces include the uploaded one", state["faces"][-1] == "Neon", state["faces"])
check("clips listed", [c["name"] for c in state["clips"]] == ["boot", "connecting", "online", "countdown_end"], state["clips"])
print("   free heap after", state["free_heap"], "largest", state["largest_block"], flush=True)

status, downloaded, took = call("GET", "/api/store")
check("store download equals what was sent", status == 200 and hashlib.sha256(downloaded).digest() == hashlib.sha256(store).digest(),
      f"{len(downloaded)} bytes in {took:.1f} s")

# A wrong file keeps the store.
status, error, _ = call("POST", "/api/store", b"this is not a store" * 50)
check("garbage is refused", status in (422, 400), (status, error))
status, state, _ = call("GET", "/api/state")
check("store survives garbage", state["store"]["ready"] is True, state["store"])
damaged = bytearray(store)
damaged[2000] ^= 0xFF
status, error, _ = call("POST", "/api/store", bytes(damaged))
check("damaged store is refused by its CRC", status == 422, (status, error))
status, state, _ = call("GET", "/api/state")
print("   after a damaged upload the store is ready:", state["store"]["ready"], state["store"]["status"], flush=True)
if not state["store"]["ready"]:
    status, uploaded, _ = call("POST", "/api/store", store)
    check("store restored", status == 200, uploaded)

# A client that goes away halfway.
sock = socket.create_connection((HOST, 80), timeout=10)
sock.sendall((f"POST /api/store HTTP/1.1\r\nHost: {HOST}\r\nX-Dekatron: 1\r\nContent-Type: application/octet-stream\r\n"
              f"Content-Length: {len(store)}\r\n\r\n").encode() + store[: len(store) // 3])
time.sleep(1)
sock.close()
time.sleep(1)
status, state, _ = call("GET", "/api/state")
print("   after a cut upload:", state["store"], flush=True)
status, error, _ = call("POST", "/api/store", store)
print("   upload right after the cut:", status, error, flush=True)
time.sleep(11)
status, uploaded, _ = call("POST", "/api/store", store)
check("upload works again after the abandoned one times out", status == 200 and uploaded.get("ok") is True, (status, uploaded))

# Time zones against the reference of ESPHome.
sys.path.insert(0, "")
from aioesphomeapi.posix_tz import DSTRuleType, parse_posix_tz

for zone in ("CET-1CEST,M3.5.0,M10.5.0/3", "EST5EDT,M3.2.0,M11.1.0", "<-03>3", "UTC0", "IST-5:30",
             "NZST-12NZDT,M9.5.0,M4.1.0/3", "AEST-10AEDT,M10.1.0,M4.1.0/3"):
    status, answer, _ = call("POST", "/api/settings", {"timezone": zone})
    reference = parse_posix_tz(zone)
    ok = status == 200 and answer["parsed"]["std"] == reference.std_offset_seconds
    if reference.dst_start.type != DSTRuleType.NONE and ok:
        mine = answer["parsed"]
        ok = (mine["dst"] == reference.dst_offset_seconds
              and mine["start"] == [int(reference.dst_start.type), reference.dst_start.month, reference.dst_start.week,
                                    reference.dst_start.day_of_week, reference.dst_start.day, reference.dst_start.time_seconds]
              and mine["end"] == [int(reference.dst_end.type), reference.dst_end.month, reference.dst_end.week,
                                  reference.dst_end.day_of_week, reference.dst_end.day, reference.dst_end.time_seconds])
    check(f"timezone {zone}", ok, answer.get("parsed") if status == 200 else (status, answer))
for bad in ("Europe/Madrid", "CET", "CET-1CEST", "CET-1CEST,M13.1.0,M10.5.0", "x" * 60):
    status, answer, _ = call("POST", "/api/settings", {"timezone": bad})
    check(f"bad timezone {bad[:20]!r} is refused", status == 422, (status, answer))
status, answer, _ = call("POST", "/api/settings", {"timezone": "CET-1CEST,M3.5.0,M10.5.0/3", "boot_animation": False})
check("boot animation off", status == 200 and answer["boot_animation"] is False, answer)
status, answer, _ = call("POST", "/api/settings", {"boot_animation": True})
check("boot animation on, timezone kept", status == 200 and answer["timezone"].startswith("CET"), answer)

# Password.
status, answer, _ = call("POST", "/api/password", {"password": "correct horse"})
check("password set", status == 200, (status, answer))
status, answer, _ = call("GET", "/api/state")
check("state needs the password", status == 401, (status, answer))
status, answer, _ = call("GET", "/api/state", password="wrong")
check("wrong password is refused", status == 401, (status, answer))
status, answer, _ = call("GET", "/api/state", password="correct horse")
check("right password works", status == 200, status)
status, answer, _ = call("GET", "/api/info")
check("info stays public and says auth", status == 200 and answer["auth"] is True, answer)
status, answer, _ = call("POST", "/api/stop")
check("stop needs the password", status == 401, (status, answer))
for label, extra, expected in (("form post with the header but no password", {"X-Dekatron": "1"}, 401),
                               ("form post without the header", {}, 400)):
    connection = http.client.HTTPConnection(HOST, 80, timeout=10)
    connection.request("POST", "/api/stop", body=b"x=1", headers={"Content-Type": "application/x-www-form-urlencoded", **extra})
    response = connection.getresponse()
    response.read()
    check(label + " is refused", response.status == expected, response.status)
    connection.close()
status, answer, _ = call("POST", "/api/play", b"x" * 2000, headers={"Content-Type": "text/plain"}, password="correct horse")
check("an oversized body is refused", status == 422, (status, answer))
status, answer, _ = call("POST", "/api/settings", {"boot_animation": True}, password="correct horse")
check("settings with the password work", status == 200, (status, answer))
status, answer, _ = call("POST", "/api/store", store[:100])
check("upload needs the password", status == 401, (status, answer))
status, answer, _ = call("POST", "/api/password", {"password": ""}, password="correct horse")
check("password removed with the password", status == 200, (status, answer))
status, answer, _ = call("GET", "/api/state")
check("open again", status == 200, status)

status, state, _ = call("GET", "/api/state")
print("   final heap", state["free_heap"], "largest", state["largest_block"], flush=True)
failed = [name for name, ok, _ in results if not ok]
if REPORT:
    open(REPORT, "w").write(json.dumps({"failed": failed, "total": len(results)}))
print(f"\n{len(results) - len(failed)} of {len(results)} passed", flush=True)
