"""打包离线遥控首屏：内联脚本、确定性 gzip、精确 CSP 哈希及缓存版本。"""
import argparse
import base64
import gzip
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / 'main/car/web'


def build(output: Path):
    html = (WEB/'remote.html').read_text(encoding='utf-8')
    script = (WEB/'remote.js').read_text(encoding='utf-8')
    capture = (WEB/'capture.js').read_bytes()
    capture_version = hashlib.sha256(capture).hexdigest()[:16]
    module_uri = '/capture.js?v=' + capture_version
    script = script.replace("addModule('/capture.js')", f"addModule('{module_uri}')")
    external = '<script defer src="/remote.js"></script>'
    if html.count(external) != 1 or html.count('</body>') != 1 or '</script' in script.lower():
        raise ValueError('遥控页面结构不符合打包要求，拒绝生成不完整首屏')
    html = html.replace(external, '<link rel="icon" href="data:,">')
    html = html.replace('</body>', '<script>' + script + '</script></body>')
    page = html.encode('utf-8')
    compressed = gzip.compress(page, compresslevel=9, mtime=0)
    script_hash = base64.b64encode(hashlib.sha256(script.encode('utf-8')).digest()).decode('ascii')
    policy = ("default-src 'self'; script-src 'self' 'sha256-" + script_hash + "'; "
              "style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self' wss://192.168.4.1:8443; frame-ancestors 'none'")
    metadata = {
        'page_csp': policy,
        'page_etag': '"' + hashlib.sha256(page).hexdigest() + '"',
        'gzip_etag': '"' + hashlib.sha256(compressed).hexdigest() + '"',
        'capture_etag': '"' + hashlib.sha256(capture).hexdigest() + '"',
        'script_etag': '"' + hashlib.sha256((WEB/'remote.js').read_bytes()).hexdigest() + '"',
        'capture_uri': module_uri,
        'plain_bytes': len(page),
        'gzip_bytes': len(compressed),
    }
    output.mkdir(parents=True, exist_ok=True)
    outputs = {'remote_page.html': page, 'remote_page.html.gz': compressed,
               'remote_page.json': (json.dumps(metadata, ensure_ascii=False, indent=2)+'\n').encode('utf-8')}
    header = '#pragma once\n// 构建工具生成，不要手工编辑。\n'
    for key, value in metadata.items():
        if isinstance(value, str):
            header += f'constexpr const char kRemote_{key}[] = {json.dumps(value)};\n'
    outputs['remote_page_metadata.h'] = header.encode('utf-8')
    for name, content in outputs.items():
        target = output/name
        if not target.exists() or target.read_bytes() != content:
            target.write_bytes(content)
    return metadata


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = build(args.output)
    print(f"Remote first page: {result['plain_bytes']} -> {result['gzip_bytes']} bytes (gzip)")
