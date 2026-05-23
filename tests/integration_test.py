import socket
import requests
from pathlib import Path
import sys

BASE_URL = "http://localhost:3490"
TIMEOUT = 5


def make_request(method, url, **kwargs):
    try:
        return requests.request(method, url, timeout=TIMEOUT, **kwargs)
    except requests.exceptions.ConnectionError:
        sys.exit(f"\nErr: Server went down during test: {current_test}")


# requests removes any `../../` Path Traversal symbols so using sockets
current_test = "Path Traversal (../) => 403"
print(f"Running: {current_test}")
try:
    with socket.create_connection(("localhost", 3490), timeout=TIMEOUT) as s:
        s.sendall(b"GET /../../etc/passwd HTTP/1.1\r\nHost: localhost\r\n\r\n")
        response = s.recv(1024).decode("utf-8", errors="ignore")
except Exception as e:
    sys.exit(f"\nErr: Server is down when running test: {current_test}: {e}")
assert "403" in response, f"`403` expected, got: {response}"

current_test = "Too long path => 403"
print(f"Running: {current_test}")
res = make_request("GET", f"{BASE_URL}/{'1' * 512}")
assert res.status_code == 403, res.status_code

current_test = "Too long headers => 431"
print(f"Running: {current_test}")
res = make_request("GET", BASE_URL, headers={"Large-Header": "a" * 4096})
assert res.status_code == 431, res.status_code

current_test = "Wrong path => 404"
print(f"Running: {current_test}")
res = make_request("GET", f"{BASE_URL}/file_not_exists.txt")
assert res.status_code == 404, res.status_code

current_test = "Method Not Allowed => 405"
print(f"Running: {current_test}")
res = make_request("POST", BASE_URL)
assert res.status_code == 405, res.status_code

current_test = "GET / => 200 OK"
print(f"Running: {current_test}")
res = make_request("GET", BASE_URL)
assert res.status_code == 200, res.status_code
assert res.text == Path("index.html").read_text()

print("\nTests finished.")
