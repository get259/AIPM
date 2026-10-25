"""验证签名、IP、用途和固件嵌入边界，不安装证书。"""
import ipaddress
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / '.cache/remote-cert-tools'))
from cryptography import x509
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.x509.oid import ExtendedKeyUsageOID

folder = ROOT / '.local-remote-tls'
ca = x509.load_pem_x509_certificate((folder/'root-ca.crt').read_bytes())
leaf = x509.load_pem_x509_certificate((folder/'server-cert.pem').read_bytes())
key = serialization.load_pem_private_key((folder/'server-key.pem').read_bytes(), password=None)
ca.public_key().verify(leaf.signature, leaf.tbs_certificate_bytes, padding.PKCS1v15(), leaf.signature_hash_algorithm)
assert leaf.issuer == ca.subject
assert key.public_key().public_numbers() == leaf.public_key().public_numbers()
assert leaf.not_valid_before_utc <= datetime.now(timezone.utc) < leaf.not_valid_after_utc
assert ipaddress.ip_address('192.168.4.1') in leaf.extensions.get_extension_for_class(x509.SubjectAlternativeName).value.get_values_for_type(x509.IPAddress)
assert ExtendedKeyUsageOID.SERVER_AUTH in leaf.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
assert ca.extensions.get_extension_for_class(x509.BasicConstraints).value.ca
firmware = (ROOT/'.pio/build/esp32s3/firmware.bin').read_bytes()
assert (folder/'root-ca.crt').read_bytes() in firmware
assert (folder/'car-root-ca.cer').read_bytes() in firmware
assert (folder/'server-cert.pem').read_bytes() in firmware
assert (folder/'server-key.pem').read_bytes() in firmware
assert (folder/'root-ca-key.pem').read_bytes() not in firmware
for name in ['capture.js']:
    assert (ROOT/'main/car/web'/name).read_bytes() in firmware, f'stale firmware asset: {name}'
for name in ['remote_page.html', 'remote_page.html.gz']:
    assert (ROOT/'.pio/build/esp32s3/web-remote-assets'/name).read_bytes() in firmware, f'stale bundled asset: {name}'
print('Certificates and firmware: chain, server key, validity, IP SAN, EKU, private-CA boundary and current web assets passed')
