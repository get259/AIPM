"""只预览布局，不模拟设备在线、录音或 UART。"""
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2] / 'main/car/web'


class Preview(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_GET(self):
        if self.path == '/mobile':
            page = ('<!doctype html><meta charset="utf-8"><title>390px 手机布局预览</title>'
                    '<body style="margin:0;background:#dbe2eb"><iframe title="手机遥控页面" '
                    'src="/remote.html" style="border:0;width:390px;height:844px"></iframe></body>')
            content = page.encode('utf-8')
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(content)))
            self.end_headers()
            self.wfile.write(content)
        else:
            super().do_GET()


if __name__ == '__main__':
    print('Desktop: http://127.0.0.1:8766/remote.html ; Mobile: http://127.0.0.1:8766/mobile')
    ThreadingHTTPServer(('127.0.0.1', 8766), Preview).serve_forever()
