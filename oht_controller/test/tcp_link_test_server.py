#!/usr/bin/env python3
"""TStcp TCP 클라이언트에 LinkTest 프레임을 보내는 서버 테스터."""

import argparse
from dataclasses import dataclass, replace
import math
import socket
import struct
import sys


FRAME_FORMAT = struct.Struct("!HIIHH")
LINK_TEST_MSGID = 0x0000
PAYLOAD_SIZE = 4
EXPECTED_RESPONSE_PAYLOAD = 0x1234


@dataclass(frozen=True)
class Frame:
    msgid: int
    len: int
    payload: int
    reserved: int
    crc: int

    def encode(self) -> bytes:
        return FRAME_FORMAT.pack(
            self.msgid, self.len, self.payload, self.reserved, self.crc
        )

    def calculate_crc(self) -> int:
        """crc 필드를 제외한 전송 바이트 합계의 하위 16비트."""
        return sum(self.encode()[:-2]) & 0xFFFF

    @classmethod
    def decode(cls, data: bytes) -> "Frame":
        return cls(*FRAME_FORMAT.unpack(data))


def receive_frame(connection: socket.socket) -> bytes:
    """TCP 분할 수신을 고려하여 정확히 한 프레임(14바이트)을 받는다."""
    data = bytearray()
    while len(data) < FRAME_FORMAT.size:
        chunk = connection.recv(FRAME_FORMAT.size - len(data))
        if not chunk:
            raise ConnectionError(
                f"응답 수신 중 연결 종료: {len(data)}/{FRAME_FORMAT.size}바이트"
            )
        data.extend(chunk)
    return bytes(data)


def print_frame(label: str, frame: Frame, raw: bytes) -> None:
    print(f"\n[{label}] {len(raw)}바이트", flush=True)
    print(f"  raw      : {raw.hex(' ')}")
    print(f"  msgid    : 0x{frame.msgid:04X}")
    print(f"  len      : {frame.len} (0x{frame.len:08X})")
    print(f"  payload  : 0x{frame.payload:08X} ({frame.payload})")
    print(f"  reserved : 0x{frame.reserved:04X}")
    print(f"  crc      : 0x{frame.crc:04X}", flush=True)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="서버 IPv4 주소")
    parser.add_argument("--port", type=int, default=5000, help="서버 포트 (기본: 5000)")
    parser.add_argument(
        "--payload", type=lambda value: int(value, 0), default=0,
        help="요청 payload: 10진수 또는 0x 접두사 16진수 (기본: 0)",
    )
    parser.add_argument(
        "--timeout", type=float, default=30.0,
        help="연결 및 수신 대기 제한시간, 초 (기본: 30)",
    )
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port는 1~65535 범위여야 합니다.")
    if not 0 <= args.payload <= 0xFFFFFFFF:
        parser.error("--payload는 0~0xFFFFFFFF 범위여야 합니다.")
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout은 양의 유한한 값이어야 합니다.")
    return args


def main() -> int:
    args = parse_arguments()
    request = Frame(LINK_TEST_MSGID, PAYLOAD_SIZE, args.payload, 0, 0)
    request = replace(request, crc=request.calculate_crc())
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind((args.host, args.port))
            server.listen(1)
            server.settimeout(args.timeout)
            print(
                f"서버 대기: {args.host}:{args.port}\n"
                "다른 터미널에서 app/build/oht_controller를 실행하세요.",
                flush=True,
            )
            connection, address = server.accept()
            with connection:
                connection.settimeout(args.timeout)
                print(f"클라이언트 연결: {address[0]}:{address[1]}", flush=True)
                raw_request = request.encode()
                connection.sendall(raw_request)
                print_frame("TX LinkTest 요청", request, raw_request)

                raw_response = receive_frame(connection)
                response = Frame.decode(raw_response)
                print_frame("RX LinkTest 응답", response, raw_response)

                expected = Frame(
                    LINK_TEST_MSGID, PAYLOAD_SIZE, EXPECTED_RESPONSE_PAYLOAD,
                    request.reserved, 0,
                )
                expected = replace(expected, crc=expected.calculate_crc())
                if response != expected:
                    print(f"\nFAIL: 예상 응답과 다릅니다. 예상값: {expected}", file=sys.stderr)
                    return 1

                print("\nPASS: LinkTest 응답 및 바이트 합산 CRC 확인", flush=True)
    except socket.timeout:
        print("FAIL: 연결 또는 응답 수신 대기시간을 초과했습니다.", file=sys.stderr)
        return 1
    except (OSError, struct.error) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\n테스트를 중단했습니다.", file=sys.stderr)
        return 130
    return 0


if __name__ == "__main__":
    sys.exit(main())
