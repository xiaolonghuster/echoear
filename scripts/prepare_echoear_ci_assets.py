"""Fetch the pinned public EchoEar resource for CI; never use account credentials."""
from pathlib import Path
import hashlib
import io
import urllib.request
import zipfile

URL = 'https://github.com/xiaolonghuster/echoear/releases/download/echoear-v0.19.0-rc.1/waveshare-install.zip'
SHA256 = '57e20c0c0a5a2cf99a1e73d2418ffa4c8153de06ee9db5c2b619cc324ba3f9cb'


def main():
    with urllib.request.urlopen(URL, timeout=60) as response:
        data = response.read(16 * 1024 * 1024)
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError('Pinned EchoEar resource archive checksum mismatch')
    output = Path(__file__).resolve().parents[2] / 'assets/custom/assets-with-font.bin'
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        resource = archive.read('0x800000-assets-with-font.bin')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(resource)


if __name__ == '__main__':
    main()
