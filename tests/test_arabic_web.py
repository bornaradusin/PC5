#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The real HTTP server supplies Arabic to normal and capability phone pages."""
import json
import os
from pathlib import Path
import re
import sys
import urllib.parse
from test_web import Server, request

serve, out, root, export = sys.argv[1:]
os.environ['PC5_TEST_LANG_DIR'] = str(Path(out) / 'lang')
server = Server(serve, root, str(Path(out) / 'arabic-install'))
del os.environ['PC5_TEST_LANG_DIR']
try:
    code, body, _ = request(server.base, 'lang.json')
    language = json.loads(body)
    assert code == 200 and language['lang'] == 'ar'
    assert language['strings']['Choose files'] == 'اختيار ملفات'
    def command(text):
        server.process.stdin.write(text + '\n'); server.process.stdin.flush()
        return server.process.stdout.readline().strip()
    for name, action in [('phone', 'phone 3'), ('appearance', 'appearance-begin')]:
        path = urllib.parse.urlsplit(command(action)).path.lstrip('/')
        code, body, _ = request(server.base, path)
        html = body.decode()
        assert code == 200
        match = re.search(r'const pc5Language=(.*?);function t\(s\)', html)
        assert match
        dictionary = json.loads(match[1])
        assert dictionary['lang'] == 'ar' and dictionary['strings']['Cover'] == 'الغلاف'
        assert '<' not in match[1]
        assert "pc5Language.lang==='ar'?'rtl':'ltr'" in html
        assert "t('Uploading…')" in html
        assert len(re.findall('<script>', html)) == len(re.findall('</script>', html)) == 2
        Path(export).mkdir(parents=True, exist_ok=True)
        (Path(export) / (name + '.html')).write_text(html, encoding='utf8')
        command('phone-end')
    command('phone-only')
    assert request(server.base, 'lang.json')[0] == 404
    print('Arabic HTTP dictionaries and phone pages: passed')
finally:
    server.stop()
