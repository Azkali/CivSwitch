#!/usr/bin/env python3
"""Decode the dry-run reports as a host would; no USB or third-party packages."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

BINARY = str(pathlib.Path(sys.argv.pop(1)).resolve())
UNSHIFTED = "abcdefghijklmnopqrstuvwxyz1234567890\n\x1b\b\t -=[]\\#;'`,./"
SHIFTED = "ABCDEFGHIJKLMNOPQRSTUVWXYZ!@#$%^&*()\n\x1b\b\t _+{}|~:\"~<>?"


def run(data, *args, check=True):
    with tempfile.NamedTemporaryFile() as file:
        file.write(data)
        file.flush()
        result = subprocess.run([BINARY, "--payload", file.name, "--dry-run", *args],
                                text=True, capture_output=True, check=check)
    steps = [json.loads(line) for line in result.stdout.splitlines()]
    return steps, result


def decode(steps):
    last = 0
    text = []
    for step in steps:
        report = bytes.fromhex(step["report"])
        assert len(report) == 8
        assert report[1] == 0 and not any(report[3:])
        assert report[0] in (0, 2)
        key = report[2]
        if key and key != last:
            text.append((SHIFTED if report[0] else UNSHIFTED)[key - 4])
        last = key
    return "".join(text)


class TyperTests(unittest.TestCase):
    def test_plain_smoke(self):
        steps, result = run(b"hello world\n", "--mode", "text", "--cps", "20")
        self.assertEqual(decode(steps), "hello world\n")
        self.assertNotIn("pad-A", result.stderr)
        self.assertEqual([s["ms"] for s in steps[1:-1]], [32, 18] * 12)

    def test_full_us_map(self):
        data = bytes(range(32, 127)) + b"\t"
        steps, _ = run(data, "--mode", "text")
        self.assertEqual(decode(steps), data.decode())

    def test_pico_batches(self):
        steps, result = run(b"aA!\n\n  \nBb\r\n")
        self.assertEqual(decode(steps), "\x1baA!\n\n\x1bBb\n\n")
        self.assertEqual(result.stderr.count("TODO: pad-A"), 8)
        self.assertEqual(steps[0]["ms"], 1000)
        self.assertEqual(steps[-1], {"kind": "report", "ms": 0, "report": "0000000000000000"})

    def test_default_gap_only_repeated_usage(self):
        steps, _ = run(b"abBcc")
        # start delay + ten preamble steps + Esc, WAIT, PAD, WAIT
        keys = steps[15:]
        self.assertEqual([(s["report"], s["ms"]) for s in keys[:7]], [
            ("0000040000000000", 32), ("0000050000000000", 32),
            ("0000000000000000", 32), ("0200050000000000", 32),
            ("0000060000000000", 32), ("0000000000000000", 32),
            ("0000060000000000", 32)])

    def test_unknown_bytes(self):
        steps, result = run(b"a\x00\xff\xc3\xa9b", "--mode", "text")
        self.assertEqual(decode(steps), "ab")
        self.assertEqual(result.stderr.count("skipping unsupported byte"), 4)
        steps, result = run(b"\xff\nabc\n\x00")
        self.assertEqual(decode(steps), "\x1babc\n\n")
        self.assertEqual(result.stderr.count("TODO: pad-A"), 6)

    def test_empty_input(self):
        steps, result = run(b"")
        self.assertEqual(decode(steps), "")
        self.assertEqual(result.stderr.count("TODO: pad-A"), 5)

    def test_newlines(self):
        for mode in ("enter", "cr"):
            steps, _ = run(b"a\r\nb\rc\n\n", "--mode", "text", "--newline", mode)
            self.assertEqual(decode(steps), "a\nb\nc\n\n")
        steps, _ = run(b"a\r\nb\nc", "--mode", "text", "--newline", "none")
        self.assertEqual(decode(steps), "abc")
        steps, _ = run(b"a\nb", "--newline", "none")
        self.assertEqual(decode(steps), "\x1ba\x1bb")

    def test_numeric_validation(self):
        for args in (("--cps", "0"), ("--cps", "-1"), ("--cps", "NaN"),
                     ("--cps", "32"), ("--cps", "500", "--press-ms", "2"),
                     ("--press-ms", "0"), ("--press-ms", "32x"),
                     ("--start-delay-ms", "4294967296"), ("--newline", "lf"),
                     ("--mode", "other"), ("stray",)):
            _, result = run(b"a", *args, check=False)
            self.assertEqual(result.returncode, 2, args)
        steps, _ = run(b"aa", "--cps", "500", "--press-ms", "1", "--start-delay-ms", "0")
        self.assertEqual(decode(steps), "\x1baa\n\n")

    def test_payload_open_failure(self):
        result = subprocess.run([BINARY, "--payload", "/nonexistent/t3-test"], capture_output=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"open payload", result.stderr)


if __name__ == "__main__":
    unittest.main()
