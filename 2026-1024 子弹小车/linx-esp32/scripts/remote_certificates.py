"""生成或续签专用于小车 HTTPS 的本地证书，不安装系统根证书。"""
import argparse
import ipaddress
from datetime import datetime, timedelta, timezone
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID


def save_key(path, key):
    path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / '.local-remote-tls')
    parser.add_argument('--renew', action='store_true', help='沿用原 CA，重新签发 90 天服务器证书')
    args = parser.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    now = datetime.now(timezone.utc)
    ca_key_path, ca_path = folder/'root-ca-key.pem', folder/'root-ca.crt'
    if ca_key_path.exists() != ca_path.exists():
        raise SystemExit('根证书与私钥不完整；请恢复原文件，工具不会自动替换根 CA')
    if ca_path.exists():
        ca_key = serialization.load_pem_private_key(ca_key_path.read_bytes(), password=None)
        ca = x509.load_pem_x509_certificate(ca_path.read_bytes())
        if ca.public_key().public_numbers() != ca_key.public_key().public_numbers():
            raise SystemExit('根证书与私钥不匹配')
        if ca.not_valid_after_utc < now + timedelta(days=91):
            raise SystemExit('根 CA 即将过期，需手动规划更换并重新安装')
        if not args.renew and (folder/'server-cert.pem').exists() and (folder/'server-key.pem').exists():
            leaf = x509.load_pem_x509_certificate((folder/'server-cert.pem').read_bytes())
            if leaf.not_valid_after_utc <= now:
                raise SystemExit('服务器证书已过期，请使用 --renew 续签并重新构建固件')
            print('已有证书，未覆盖；有效期至', leaf.not_valid_after_utc.isoformat())
            print('根证书 SHA256:', ca.fingerprint(hashes.SHA256()).hex(':'))
            return
    else:
        ca_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Car Remote Local CA')])
        ca = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
              .public_key(ca_key.public_key()).serial_number(x509.random_serial_number())
              .not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=3650))
              .add_extension(x509.BasicConstraints(ca=True, path_length=0), critical=True)
              .add_extension(x509.KeyUsage(False, False, False, False, False, True, True, False, False), critical=True)
              .add_extension(x509.SubjectKeyIdentifier.from_public_key(ca_key.public_key()), critical=False)
              .sign(ca_key, hashes.SHA256()))
        save_key(ca_key_path, ca_key)
        ca_path.write_bytes(ca.public_bytes(serialization.Encoding.PEM))
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'Car Remote 192.168.4.1')])
    leaf = (x509.CertificateBuilder().subject_name(name).issuer_name(ca.subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(now-timedelta(minutes=5)).not_valid_after(now+timedelta(days=90))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.SubjectAlternativeName([x509.IPAddress(ipaddress.ip_address('192.168.4.1'))]), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
            .add_extension(x509.KeyUsage(True, False, True, False, False, False, False, False, False), critical=True)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca_key.public_key()), critical=False)
            .sign(ca_key, hashes.SHA256()))
    save_key(folder/'server-key.pem', key)
    (folder/'server-cert.pem').write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
    # DER 便于安卓、iPhone 和 Windows 导入，不包含任何私钥。
    (folder/'car-root-ca.cer').write_bytes(ca.public_bytes(serialization.Encoding.DER))
    print('证书目录:', folder)
    print('服务器证书有效期至:', leaf.not_valid_after_utc.isoformat())
    print('根证书 SHA256:', ca.fingerprint(hashes.SHA256()).hex(':'))
    print('只向手机分发 root-ca.crt 或 car-root-ca.cer；根 CA 私钥不要复制进固件或分享。')


if __name__ == '__main__':
    main()
