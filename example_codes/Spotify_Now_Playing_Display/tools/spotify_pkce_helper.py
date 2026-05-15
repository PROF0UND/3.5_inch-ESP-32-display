#!/usr/bin/env python3
"""One-time Spotify PKCE helper for the ESP32 now-playing display.

Create a Spotify app at https://developer.spotify.com/dashboard, add
http://127.0.0.1:8080/callback as a redirect URI, then run:

    python spotify_pkce_helper.py YOUR_CLIENT_ID

The script opens your browser, waits for Spotify to redirect back locally,
and prints values to paste into spotify_config.h.
"""

from __future__ import annotations

import base64
import hashlib
import http.server
import json
import secrets
import ssl
import string
import sys
import threading
import urllib.parse
import urllib.request
import webbrowser


REDIRECT_URI = "http://127.0.0.1:8080/callback"
AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
SCOPE = "user-read-currently-playing"


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    code: str | None = None
    error: str | None = None
    event = threading.Event()

    def do_GET(self) -> None:
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)
        CallbackHandler.code = params.get("code", [None])[0]
        CallbackHandler.error = params.get("error", [None])[0]

        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        if CallbackHandler.code:
            message = "Spotify login complete. You can close this tab."
        else:
            message = "Spotify login failed. Return to the terminal."
        self.wfile.write(f"<html><body><h1>{message}</h1></body></html>".encode())
        CallbackHandler.event.set()

    def log_message(self, format: str, *args: object) -> None:
        return


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode().rstrip("=")


def make_verifier(length: int = 64) -> str:
    alphabet = string.ascii_letters + string.digits + "-._~"
    return "".join(secrets.choice(alphabet) for _ in range(length))


def post_form(url: str, form: dict[str, str], *, insecure: bool = False) -> dict[str, object]:
    body = urllib.parse.urlencode(form).encode()
    request = urllib.request.Request(
        url,
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    context = ssl._create_unverified_context() if insecure else None
    with urllib.request.urlopen(request, timeout=30, context=context) as response:
        return json.loads(response.read().decode())


def main() -> int:
    if len(sys.argv) not in (2, 3):
        print("Usage: python spotify_pkce_helper.py YOUR_CLIENT_ID [--insecure]")
        return 2

    client_id = sys.argv[1].strip()
    insecure = len(sys.argv) == 3 and sys.argv[2] == "--insecure"
    if len(sys.argv) == 3 and not insecure:
        print("Unknown option. Did you mean --insecure?")
        return 2

    if insecure:
        print("Warning: --insecure disables HTTPS certificate verification for the token request.")

    verifier = make_verifier()
    challenge = b64url(hashlib.sha256(verifier.encode()).digest())
    state = secrets.token_urlsafe(24)

    params = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPE,
        "state": state,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
    }
    auth_link = f"{AUTH_URL}?{urllib.parse.urlencode(params)}"

    server = http.server.HTTPServer(("127.0.0.1", 8080), CallbackHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    print("Opening Spotify login in your browser...")
    print(auth_link)
    webbrowser.open(auth_link)

    CallbackHandler.event.wait(timeout=180)
    server.shutdown()

    if CallbackHandler.error:
        print(f"Spotify returned an error: {CallbackHandler.error}")
        return 1
    if not CallbackHandler.code:
        print("Timed out waiting for Spotify login.")
        return 1

    try:
        token = post_form(
            TOKEN_URL,
            {
                "client_id": client_id,
                "grant_type": "authorization_code",
                "code": CallbackHandler.code,
                "redirect_uri": REDIRECT_URI,
                "code_verifier": verifier,
            },
            insecure=insecure,
        )
    except urllib.error.URLError as exc:
        print(f"Token request failed: {exc}")
        if "CERTIFICATE_VERIFY_FAILED" in str(exc):
            print()
            print("Your Python install cannot find trusted CA certificates.")
            print("Try again with:")
            print(f"python {sys.argv[0]} {client_id} --insecure")
            print()
            print("A cleaner fix is to run the script with the standard python.org Windows Python.")
        return 1

    refresh_token = token.get("refresh_token")
    if not refresh_token:
        print("Spotify did not return a refresh token.")
        print(json.dumps(token, indent=2))
        return 1

    print()
    print("Paste these into example_codes/Spotify_Now_Playing_Display/spotify_config.h:")
    print()
    print(f'#define SPOTIFY_CLIENT_ID "{client_id}"')
    print(f'#define SPOTIFY_REFRESH_TOKEN "{refresh_token}"')
    print()
    print("Keep spotify_config.h private; it is ignored by Git.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
