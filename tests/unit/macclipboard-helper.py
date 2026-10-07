#!/usr/bin/env python3
"""Test the actual JXA helper against an isolated Mac pasteboard.

Run on macOS with permission to access the pasteboard server. Never accesses
the general (user) clipboard. No SSH server or extra Mac packages are needed.
"""
import pathlib
import subprocess
import unittest

SOURCE = pathlib.Path(__file__).resolve().parents[2] / 'vncviewer/MacClipboardProtocol.cxx'
HELPER = SOURCE.read_text().split('R"JXA(', 1)[1].split(')JXA"', 1)[0]
HELPER = HELPER.replace('function run(args)', 'function helperRun(args)')
HELPER = HELPER.replace('$.NSPasteboard.generalPasteboard', 'testBoard')
assert 'generalPasteboard' not in HELPER


def execute(body, value=b''):
    script = HELPER + '''
var testBoard;
function run() {
  testBoard = $.NSPasteboard.pasteboardWithUniqueName;
  if (testBoard.isNil()) throw Error('Cannot access isolated pasteboard');
  try { BODY } finally { testBoard.releaseGlobally; }
}
'''.replace('BODY', body)
    return subprocess.run(['/usr/bin/osascript', '-l', 'JavaScript', '-e', script],
                          input=value, capture_output=True, timeout=30)


class MacClipboardHelper(unittest.TestCase):
    def test_roundtrip(self):
        for value in ['', '日本語 😀\nsecond line\n', r'{\rtf1 literal}',
                      '%!PS-Adobe literal', 'a' * 1048576]:
            with self.subTest(length=len(value)):
                encoded = value.encode()
                result = execute('helperRun(["write"]); helperRun(["read"]);', encoded)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                first, rest = result.stdout.split(b'\n', 1)
                self.assertRegex(first, rb'^IVNC1 [0-9]+ T$')
                self.assertEqual(rest, encoded + first + b'\n' + encoded)

    def test_nontext_is_distinct_from_empty_string(self):
        result = execute('testBoard.clearContents; helperRun(["read"]);')
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertRegex(result.stdout, rb'^IVNC1 [0-9]+ N\n$')

    def test_bad_input(self):
        for value in [b'\xff', b'x' * 1048577]:
            result = execute('helperRun(["write"]);', value)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b'')

    def test_files_are_not_transferred_as_names(self):
        result = execute('''
          testBoard.clearContents;
          testBoard.setStringForType($('example.txt'), $.NSPasteboardTypeString);
          testBoard.setStringForType($('file:///tmp/example.txt'), $.NSPasteboardTypeFileURL);
          helperRun(["read"]);
        ''')
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertRegex(result.stdout, rb'^IVNC1 [0-9]+ N\n$')


if __name__ == '__main__':
    unittest.main()
