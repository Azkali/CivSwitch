#!/usr/bin/env python3
"""Compare actual Pico-generated steps to T3 dry-run steps using synthetic text.

Usage: check-parity.py /path/to/CivSwitch /path/to/hidkbd_ffs
Never reads or generates the user's payload. No third-party Python packages.
"""
import ast
import json
import pathlib
import re
import subprocess
import sys
import tempfile


def main():
    repo = pathlib.Path(sys.argv[1]).resolve()
    binary = pathlib.Path(sys.argv[2]).resolve()
    generator = repo / "PicoSwitch/tools/payload_to_macro.py"
    engine = repo / "PicoSwitch/include/MacroEngine.h"
    # Fail if the source is unavailable; upstream is not a substitute for the fork.
    source = generator.read_text()
    header = engine.read_text()
    match = re.search(r"#define\s+MACRO_START_DELAY_MS\s+(\d+)", header)
    if not match:
        raise RuntimeError("cannot find Pico start delay")
    start_delay = int(match[1])
    names = {"HID_KEY_" + c: 4 + i for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ")}
    names.update({"HID_KEY_" + c: 0x1e + i for i, c in enumerate("1234567890")})
    names.update({"HID_KEY_" + k: v for k, v in {
        "ENTER": 0x28, "ESCAPE": 0x29, "TAB": 0x2b, "SPACE": 0x2c,
        "MINUS": 0x2d, "EQUAL": 0x2e, "BRACKET_LEFT": 0x2f, "BRACKET_RIGHT": 0x30,
        "BACKSLASH": 0x31, "SEMICOLON": 0x33, "APOSTROPHE": 0x34, "GRAVE": 0x35,
        "COMMA": 0x36, "PERIOD": 0x37, "SLASH": 0x38}.items()})
    names.update({"KEYBOARD_MODIFIER_LEFTSHIFT": 2, "0": 0})
    timing = (pathlib.Path(__file__).resolve().parents[1] / "src/timing.h").read_text()
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name.endswith("_MS") or name == "PREAMBLE_A_COUNT":
                expected = ast.literal_eval(node.value)
                value = re.search(r"\b" + name + r"\s*=\s*(\d+)", timing)
                if not value or int(value[1]) != expected:
                    raise AssertionError(f"timing constant mismatch: {name}")
    def step(ms, report=bytes(8), kind="report"):
        return {"kind": kind, "ms": ms, "report": report.hex()}
    cases = [b"", b"hello world\n", b"aAbBcc!!\n\n \t\n", bytes(range(32, 127)) + b"\t\n",
             b"\r\na\rb\n\n", b"a\x00b\n", "café\n".encode(),
             b"\x00\nabc",  # Pico requires valid UTF-8; byte skips are tested separately.
             b"\n".join(f"Line {n}: AaBb !@#$%^&*() []{{}} \\ / \" ' == 0011".encode() for n in range(10))]
    with tempfile.TemporaryDirectory() as temp:
        payload = pathlib.Path(temp) / "synthetic.txt"
        output = pathlib.Path(temp) / "macro.h"
        for index, data in enumerate(cases):
            payload.write_bytes(data)
            subprocess.run([sys.executable, str(generator), str(payload), str(output)], check=True, capture_output=True)
            expected = [step(start_delay)]
            for line in output.read_text().splitlines():
                m = re.fullmatch(r"\s*(KEYS|WAIT|PAD_BTN|END)\((.*?)\),", line)
                if not m:
                    continue
                op, args = m.groups()
                args = [a.strip() for a in args.split(",")]
                if op == "KEYS":
                    report = bytes([names[args[1]], 0, *[names[a] for a in args[2:]]])
                    expected.append(step(int(args[0]), report))
                elif op == "WAIT": expected.append(step(int(args[0])))
                elif op == "PAD_BTN": expected.append(step(int(args[0]), kind="pad-A"))
                else: expected.append(step(0))
            result = subprocess.run([str(binary), "--payload", str(payload), "--dry-run"],
                                    check=True, capture_output=True, text=True)
            actual = [json.loads(line) for line in result.stdout.splitlines()]
            if actual != expected:
                for i in range(max(len(actual), len(expected))):
                    a = actual[i] if i < len(actual) else None
                    e = expected[i] if i < len(expected) else None
                    if a != e: raise AssertionError(f"case {index}, step {i}: T3={a}, Pico={e}")
    print(f"Pico parity passed for {len(cases)} synthetic payloads: {generator}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, AssertionError, subprocess.CalledProcessError) as error:
        sys.exit(f"Parity check failed: {error}")
