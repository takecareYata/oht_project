# MISRA C 적용 기록

이 문서는 구현 단계별 누적 기록이다. 초기 골격에 대한 설명도 포함하며, 현재 SPI 구현과 코드 흐름은
[SPI 통신 규약](SPI_PROTOCOL.md), [코드 읽는 순서](CODE_GUIDE.md)를 함께 확인한다.

기준은 MISRA C:2012와 C99이다. 적용 대상은 현재 `app/`의 `main.c`, 네 CSU의
`.c`·`.h`, `main.h`이다. 공통 자료형과 상수도 `main.h`에 통합한다.
사용자가 요청한 POSIX 스레드, TCP 클라이언트 및
`void *` 인터페이스의 제한적인 사용 범위를 아래와 같이 기록한다. 이는 전체 MISRA 준수 선언이나
모든 규칙에 대한 검사 완료 보고서가 아니다.

## 현재 SPI v2 적용 기록 (이전 단계 기록보다 우선)

- [TSspi.c](../app/TSspi.c)는 24바이트 요청/응답을 필드별 big endian 변환하고 CRC-16/CCITT-FALSE로 검증한다. 구조체 저장 크기·패딩에 의존하지 않는다. TCP 합산 체크섬은 유지한다.
- 공유 큐와 캐시는 mutex로 보호한다. 장치 fd와 실제 SPI 축 컨텍스트는 SPI 태스크만, 중앙 FSM 상태는 중앙 태스크만 변경한다. 검사/조회 API는 문서화된 잠금과 큐를 사용한다.
- Linux spidev의 tx_buf/rx_buf는 UAPI가 요구하는 uint64 필드이므로 uintptr_t를 통한 주소 변환이 필요하다. 이는 pthread/void* 예외와 별도의 플랫폼 의존 편차이며 이전 spidev 기록을 따른다. 커널 헤더와 POSIX API도 외부 구성요소다.
- 고정 폭 정수·범위 검사·예약 값 검사·정의된 부호 변환·모노토닉 시간 차이를 사용한다. 모든 enum/구조체 정의는 헤더에 둔다.
- SPI/중앙 태스크는 반복 실행한다. E84는 여전히 초기 골격이다. 초기 복구에 실제 이동이 포함되며 고장 후 자동 ARM/작업 재개는 하지 않는다.
- make, make check의 경고 오류/정적 분석 및 make test의 UBSan 모의 통합 시험을 통과했다. 전체 MISRA 인증이나 전용 MISRA 검사 완료를 의미하지 않는다. 하위 기록의 과거 골격/28바이트/위치 비교/정지 API 없음 설명은 현 구현에 적용하지 않는다.

## DEV-PTHREAD-01: POSIX 스레드 인터페이스

- 허용 범위: `<pthread.h>`, `pthread_t`, `pthread_create()`, `pthread_join()`과
  그 콜백 규약. POSIX 함수의 반환 코드와 ISO C의 `main()` 반환형에는 인터페이스가
  요구하는 `int`를 사용한다. 일반 애플리케이션 수치에는 기존 고정 폭 정수형과
  배열 인덱스용 `size_t`를 유지한다.
- 근거: 사용자가 요구한 `main()` → 메인 작업 스레드 → 네 CSU 태스크 구조를
  Linux/POSIX에서 실행하기 위해 필요하다. POSIX 라이브러리는 별도로 검증해야 할
  외부 구성요소이며, 이 기록으로 해당 라이브러리 전체의 MISRA 준수를 주장하지 않는다.
- 제약: pthread 반환값은 `errno` 대신 반환 코드 자체로 검사한다. 생성에 성공한
  스레드만 join하며, 일부 생성이나 join이 실패해도 이미 생성된 다른 스레드의
  join을 계속 시도한다. 실패는 프로세스의 실패 종료 상태로 전달한다.
- 동기화: 네 CSU 태스크는 현재 공유 컨텍스트를 읽거나 쓰지 않는다. 메인 작업
  스레드의 결과는 정적 수명 객체에 기록하고, `main()`은 해당 스레드의 join 성공
  후에만 읽는다. join 실패 시에도 전달한 결과 객체의 수명은 유지된다.
- 제한: 나중에 반복 처리, 공유 상태 또는 종료 요청을 추가할 때 별도의 동기화와
  종료 설계를 검토해야 한다. TCP 태스크는 서버 연결 후 EOF/오류까지 수신한다.
  나머지 세 태스크는 현재 초기화 골격을 호출한 뒤 종료한다.
- 관련 검토 영역: 비표준 라이브러리와 구현 의존성, 외부 코드의 계약, 정수형 및
  동시성. POSIX 사용만을 근거로 이 영역 전체를 분석에서 제외하지 않는다.

## DEV-VOID-01: 스레드 콜백의 void 포인터

- 허용 범위: pthread 콜백의 `void * argument`와 `void *` 반환형, 결과 객체를
  `void *`로 전달한 뒤 원래 `main_task_result_t *`로 복원하는 변환 한 곳.
  역변환은 MISRA C:2012 Rule 11.5의 검토 대상이다. 고정된 POSIX 콜백 형식상
  변경할 수 없는 인수 한정자는 같은 인터페이스 범위에서 검토한다.
- 근거: `pthread_create()`가 요구하는 정확한 함수 원형을 유지하기 위해 필요하다.
- 제약: 변환 전에 NULL 여부를 확인하고, 원래 형식으로만 복원한다. 콜백 주소를
  다른 함수 포인터 형식으로 캐스팅하지 않는다. 정수와 포인터 사이의 변환이나
  다른 구조체 포인터 사이의 변환은 이 예외에 포함하지 않는다.
- 수명·정렬: 전달 객체는 정적 수명을 가진 실제 `main_task_result_t` 객체이므로
  콜백 실행 중 수명과 정렬이 유지된다. 네 CSU 태스크에는 NULL만 전달하며 인수를
  역참조하지 않는다. 종료하는 스레드 콜백은 NULL을 반환한다. TCP 태스크는 재접속 루프를 계속 실행한다.
- `(void)parameter`는 미구현 입력을 명시적으로 소비하는 표현이다. `void *`
  포인터 캐스팅과는 구분하며, 이 예외를 임의의 형변환 허용 근거로 사용하지 않는다.

## DEV-SOCKET-01: 요청된 TCP 클라이언트의 POSIX 인터페이스

- 근거와 범위: 후속 사용자 요청의 TCP 클라이언트 연결·수신을 위해
  `getaddrinfo()`, `freeaddrinfo()`, `socket()`, `connect()`, `recv()`, `send()`, `close()`, `sleep()`, `write()`와
  해당 시스템 자료형을 사용한다. 시스템 헤더 계약에 따른 `_POSIX_C_SOURCE`
  매크로 및 POSIX가 요구하는 `int`·`ssize_t`·`socklen_t`를 포함한다. 예약된
  기능 선택 매크로의 정의는 Rule 21.1과 구현 의존성 검토 대상으로 기록한다.
  이 범위는 새 TCP 기능의 플랫폼 의존성을 명시하며, 다른 규칙을 일괄 면제하지 않는다.
- 소켓 주소는 `getaddrinfo()`가 반환하는 `struct sockaddr *`를 직접 사용한다.
  `sockaddr_in *`를 다른 구조체 포인터로 캐스팅하거나 구조체를 원시 바이트
  버퍼로 해석하지 않는다. 성공한 주소 조회 결과는 반드시 `freeaddrinfo()`로 해제한다.
- 연결 실패 시 임시 소켓을 닫는다. 연결에 성공한 소켓의 소유권은 반환값을
  받는 호출자에게 넘어가며, `TStcp_Init_Client()`는 수신 종료·오류 시 닫는다.
  유효하지 않은 음수 소켓은 수신하거나 닫지 않는다.
- `recv()`의 음수 결과 뒤에만 `errno`를 검사하고 EINTR은 재수신한다.
  양수 결과를 확인한 뒤에만 `size_t`로 변환한다. 수신 요청 길이는 항상
  프레임 버퍼의 남은 크기이며, 완료 후 누적 길이를 0으로 초기화한다.
- Linux에서 실패한 `close()`를 같은 FD에 재시도하지 않는다. close 실패는
  초기화 실패 또는 수신 결과 코드로 처리한다. 다른 플랫폼으로 이식할 때는
  close/EINTR 계약을 다시 확인해야 한다.
- 소켓과 수신 버퍼는 TCP 태스크만 소유한다. 추가 공유 상태는 도입하지 않았다.
  현재 연결/수신은 블로킹 방식이다. TCP 태스크는 연결 실패·EOF·수신/송신 오류
  이후 1초 대기 후 재접속하며, POSIX sleep()의 중단 시 남은 시간을 다시 대기한다.
  연결 제한시간과 애플리케이션 수준의 정상 종료 요청 기능은 미구현이다.
- `TStcp_Parse`, `TStcp_LinkTest`는 이번 사용자 요청에서 지정한 이름을 사용한다.
  첨부 명명 규칙의 일반적인 Init/Get/Set 접두사와 달라지는 지점을 명시한다.

## 식별자 명명 규칙

후속 사용자 요청에 따라 파일명 접두사는 대소문자를 바꾸지 않고 그대로 사용한다.
매크로, 헤더 가드, 열거 상수 및 공개 자료형에도 같은 기준을 적용한다.
예: `TStcp_FRAME_SIZE`, `TStcp_frame_t`, `TSspi_config_t`, `TSe84_SIGNAL_NONE`,
`main_TASK_COUNT`, `main_COMMAND_NONE`. 매크로의 파일명 뒤 부분은 대문자로 유지한다.
함수 인수, 지역변수와 구조체 멤버는 기존 이름을 유지한다.

## 반영한 사항

- `fprintf()`, `strerror()` 및 관련 헤더를 제거했다. Rule 21.6의 표준 입출력
  사용을 없앴으며, 오류 문자열 대신 종료 상태로 성공/실패를 전달한다.
- 사용하지 않는 메인 설정 매크로, 모듈 전역 컨텍스트 객체와 전용 자료형을
  제거했다. 실제 사용이 시작될 때 필요한 상태와 동기화를 함께 도입한다.
- 미구현 Set 함수의 인수는 `(void)`로 명시적으로 소비한다. Getter는 NULL을
  검사하고, 운전 불허·명령 없음·축 없음·Heartbeat 미확인·신호 없음의 기본값을
  기록한다. 이 값은 실제 장치 상태가 아니다.
- 읽기 전용 입력 포인터는 `const`를 유지한다. 출력 포인터만 전달 객체를 수정한다.
- 메인 작업 스레드의 진입 함수는 `static`으로 제한했다. 함수 포인터 원형에도
  인수 이름을 명시하고, 태스크 함수의 주소는 `&`로 표시한다.
- 함수 원형, 헤더 가드, 중괄호, 단일 반환 위치와 부울 제어식을 유지했다.
  배열 길이와 초기화 항목을 일치시키고, pthread가 성공적으로 채운 ID만 사용한다.

## 남아 있는 검토 항목

- 그림에서 요구한 공개 CSU 함수, 인터페이스 자료형 및 신호 매크로 일부는 아직
  호출·사용되지 않는다. Rule 2.3/2.5/8.7 등의 적용 여부를 전체 프로그램 분석과
  실제 기능 구현 시 재검토해야 한다. 이는 추가 예외 승인이나 준수 판정이 아니다.
- 미구현 함수는 프로토콜, FSM, Heartbeat 또는 하드웨어 제어를 수행하지 않는다.
  설정값 0은 미설정 상태이며 실제 통신 규격의 기본 설정값이 아니다.
- GCC 경고 및 GCC 분석기는 MISRA 규칙 검사기를 대체하지 않는다. MISRA 전용
  분석기는 현재 환경에 설치되어 있지 않다. 규칙 전체의 검사 결과, 적용성 판단
  및 외부 라이브러리 검증은 완료되지 않았다. 최초 두 예외와 후속 TCP 요청의
  플랫폼 의존 범위를 위에 기록했으며, 그 밖의 위반을 일괄 예외로 처리하지 않았다.

## 재현 가능한 컴파일 검사

GCC 11.4.0, Linux 환경에서 미사용 경고를 억제하지 않고 아래 옵션으로 빌드한다.
출력 경로는 현재 환경에서 쓰기 가능한 임시 위치이다.

```sh
gcc -std=c99 -pthread -O2 -Wall -Wextra -Werror -Wpedantic \
    -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes \
    -Wmissing-prototypes -Wmissing-declarations -Wcast-qual \
    -Wcast-align=strict -Wformat=2 -Wundef -Wvla -Wnull-dereference \
    -Wswitch-enum -Wswitch-default -Wwrite-strings -Wdouble-promotion \
    -Wpointer-arith -Wbad-function-cast -Wredundant-decls -fanalyzer \
    main.c TScontrol.c TSspi.c TSe84.c TStcp.c \
    -o /tmp/oht_controller_misra_check
```

## 최초 스레드 골격 변경의 검증 결과

- 위 옵션으로 전체 컴파일·링크와 GCC 분석을 통과했다. 경고 억제 옵션은 사용하지
  않았다. 정상 실행의 종료 코드는 성공이었다.
- 임시 테스트에서 pthread 호출을 계측해 첫 스레드 하나가 네 CSU 태스크를
  생성하고, 생성된 다섯 스레드를 모두 join하는 것을 확인했다.
- 다섯 create 위치 각각에 실패를 주입해 실패 종료 상태와 기존 스레드의 join을
  확인했다. 다섯 join 위치 각각에서도 오류 반환 시 실패 상태가 전달되고 다른
  join 시도가 계속되는 것을 확인했다. join 오류 테스트는 테스트 프로세스의
  수명을 안전하게 유지하도록 실제 join을 마친 뒤 오류를 반환하는 방식이다.
- 모든 Getter의 기본값 기록 및 NULL 출력 포인터 처리를 실행 검사했다.
- 테스트 계측·오류 주입 코드는 임시 폴더에서만 실행했으며 제품 소스에 포함하지
  않았다. 이 검증 결과는 위에 명시한 MISRA 전용 분석의 미수행 범위를 대체하지 않는다.

## TCP 클라이언트 추가: 임시 프로토콜 및 검증

- 서버 기본값은 `127.0.0.1:5000`이다. `TStcp_SERVER_ADDRESS`,
  `TStcp_SERVER_PORT`를 문자열 매크로로 지정해 컴파일 시 바꿀 수 있다.
- 프레임은 msgid 2바이트, len 4바이트, payload 4바이트, reserved 2바이트,
  crc 2바이트인 고정 14바이트로 구성한다. len과 payload는 uint32_t이다. 다중 바이트 정수는 빅 엔디언이다.
  구조체 패딩에 의존하지 않고 바이트별로 해석한다. 가변 길이 payload,
  동기화 시작 표식 및 재동기화 규약은 아직 없다.
- LinkTest ID는 임시로 `0x0001`이다. 완성된 프레임은 `TStcp_Parse()`에서
  분기하고, 이 ID이면 `TStcp_LinkTest()`를 호출한다. 미지원 ID는 소비하고
  다음 프레임을 계속 수신한다. LinkTest는 같은 msgid/reserved를 사용하고
  len=4, payload=0x1234, crc=0인 응답을 송신한다.
- CRC는 수신 값을 읽고 일반 송신 시 호출자가 제공한 값을 그대로 전송한다. 알고리즘·초기값·검사 범위가 미정이므로 검증하거나 계산하지 않는다.
  LinkTest는 payload 변경 후 수신 CRC를 재사용하지 않고 0을 넣는다. 따라서
  현재 parser의 성공은 CRC 검증 성공을 의미하지 않는다.
- `TStcp_Init_Communication()`은 소켓 FD 또는 -1을 반환한다.
  `TStcp_Init_Client()`는 수신 결과를 반환한다. 태스크는 수신 결과를 현재
  명시적으로 버리므로 TCP 접속 실패가 프로세스 종료 코드에 반영되지는 않는다.
  메인 스레드의 기존 상태는 스레드 생성/join 결과를 나타낸다.
- 경고 억제 없는 C99 전체 빌드·링크 및 GCC 분석을 통과했다.
- 로컬 TCP 서버로 실제 연결, 분할 프레임 조립, 여러 연속 프레임 처리,
  미지원 ID 이후의 정상 수신을 확인했다. 함수 진입 계측으로 세 LinkTest
  프레임에 대해 처리 함수가 정확히 세 번 호출되는 것을 확인했다.
- NULL parser 인수, 음수 소켓, EOF, 불완전 프레임의 EOF, 접속 거부 및
  payload/정수 필드의 바이트 순서를 검사했다. 시험 소스는 임시 폴더에서만 사용했다.

## 참고 자료

- [MISRA 공식 준수 지침](https://misra.org.uk/compliance/)
- [MISRA 공식 C:2012 규칙 목록](https://gitlab.com/MISRA/MISRA-C/MISRA-C-2012/tools)
- [GCC 11.4 경고 옵션](https://gcc.gnu.org/onlinedocs/gcc-11.4.0/gcc/Warning-Options.html)
- [Linux/POSIX recv 계약](https://man7.org/linux/man-pages/man3/recv.3p.html)
- [Linux close 계약](https://man7.org/linux/man-pages/man2/close.2.html)

## len/payload 변경 및 송신 추가 검증

- TStcp_frame_t의 선언 순서는 msgid(uint16_t), len(uint32_t),
  payload(uint32_t), reserved(uint16_t), crc(uint16_t)이다. 구조체 메모리 크기와
  무관하게 전송은 14바이트이다. 수신 len은 원본 그대로 보관하며, 현재 고정
  프레임을 가변 길이로 바꾸거나 len 값으로 버퍼 크기를 결정하지 않는다.
- `TStcp_Send(const TStcp_frame_t * frame)`는 빅 엔디언으로
  직렬화한다. 부분 송신은 나머지를 이어 보내고 EINTR은 재시도한다. 진행이 없는
  0 반환이나 다른 오류는 SEND_ERROR이다. MSG_NOSIGNAL로 SIGPIPE를 방지한다.
- parser와 LinkTest 처리 함수는 소켓을 명시적으로 인수받는다. 수신 버퍼를
  직접 수정하지 않고 응답 구조체를 복사하여 구성한다. 기존 Get/Set_LinkTest의
  응답 payload 0x1234 의도를 유지하고 오타 및 const 객체 수정 문제를 수정했다.
- 송신 함수 자체는 소켓을 닫지 않는다. 송신 실패 뒤에는 일부 프레임이 이미
  전송되었을 수 있으므로 호출자가 연결을 정리해야 한다. 현재 수신 루프는
  처리 함수의 송신 오류를 전달받으면 루프를 종료하고 소켓을 닫는다. 같은
  소켓에 대한 다중 스레드 송신은 호출자가 직렬화해야 한다.
- 전체 C99 빌드·링크와 GCC 분석을 통과했다. 임시 모의 send/recv/close로
  14바이트 인코딩, 32비트 필드 디코딩, 부분 송신, EINTR 재시도, 0/EPIPE 오류,
  NULL/음수 인수, 분할·연속 프레임, 미지원 ID, LinkTest 응답, 불완전 수신과
  송신 실패 시 소켓 정리를 확인했다. 이번 검증에는 실제 네트워크를 사용하지 않았다.

## TCP 태스크 상시 실행 및 재접속

- TCP 태스크는 연결→수신/응답→연결 정리→1초 대기를 반복한다. 서버가 없거나
  EOF/수신/송신 오류가 발생해도 태스크와 프로세스는 종료하지 않는다.
  `TStcp_RECONNECT_DELAY_SECONDS`로 재시도 간격을 지정한다.
- 메인 작업 스레드는 기존처럼 TCP 태스크를 join하며 대기한다. 명시적인 상시
  실행 루프에는 도달할 수 없는 return 문을 두지 않았다. 다른 세 CSU 태스크는
  여전히 초기화 골격이며 별도의 반복 처리는 아직 구현하지 않았다.
- 실행은 포그라운드에서 유지한다. fork/세션 분리 또는 부팅 자동 시작 설정은
  추가하지 않았다. Ctrl+C나 SIGTERM의 기본 동작으로 프로세스를 종료할 수 있다.
- make 및 make check를 통과했다. 실제 로컬 TCP로 서버 없이 실행 유지, 재접속
  대기 중 CPU의 빈 루프 방지, 서버 테스터를 두 번 재실행해 동일 PID에서
  LinkTest 응답 성공과 EOF 이후 실행 유지, SIGTERM 종료를 확인했다.

## 외부 연동 진단 로그

- 진단 함수 이름은 `TStcp_Debug_Write`, `TStcp_Debug_Message`,
  `TStcp_Debug_Value`로 통일했다. 사용자 지정 Debug 이름을 적용한다.
- `TStcp.h`의 `TStcp_DEBUG_ENABLED`가 1이면 진단 함수를 컴파일하고, 0이면
  함수 정의·선언과 출력 코드를 제외한다. 비활성 호출은 `((void)0)` 매크로로
  바뀌며 인수도 평가하지 않는다. 로그 인수에 상태 변경 등의 부수 효과를 넣지 않는다.
- 헤더의 값을 바꾼 뒤 make하면 반영된다. 컴파일 옵션으로 바꾸려면
  `make clean` 후 `make CPPFLAGS=-DTStcp_DEBUG_ENABLED=0`을 사용할 수 있다.
  Makefile은 컴파일 옵션 변경 자체를 의존성으로 추적하지 않으므로 이 경우
  기존 오브젝트를 먼저 정리한다.
- 켜짐/꺼짐 구성의 출력과 인수 평가 여부를 실행 검사했다. 꺼짐 구성 전체
  빌드를 통과했으며, 전처리 결과와 실행 파일에서 디버그 함수·메시지가 제외됨을
  확인했다. 기본 구성의 make 및 make check도 통과했다.
- TStcp_DEBUG_ENABLED의 기본값은 1이다. 연결 시도/성공, recv 반환 바이트 수,
  해석한 msgid/len/payload, 미지원 ID, LinkTest 처리 진입, 송신 완료 바이트 수와
  recv/send 오류의 errno를 stderr에 기록한다. 수치는 16진수로 출력한다.
- 사용자가 추가한 printf 디버그 문장은 POSIX write 기반의 즉시 출력으로 교체했다.
  errno는 송수신 실패 직후 보관한 다음 출력하므로 진단 출력이 오류 판단을
  바꾸지 않는다. 로그 출력 실패는 통신 처리 결과에 반영하지 않는다.
- make 및 make check를 통과했다. 모의 수신에서 14바이트 빅 엔디언의 정상 응답과
  연속 프레임 처리를 확인했다. 16바이트 네이티브 리틀 엔디언 구조체에서는
  msgid=0x0100으로 해석되어 응답하지 않는 증상을 재현했다. 이것은 재현한
  가설이며 실제 외부 서버의 원시 바이트를 확인하기 전 원인 확정은 하지 않는다.
- -DTStcp_DEBUG_ENABLED=0으로 로그를 끈 구성도 컴파일 검사했다.

## void 메시지 처리 API 및 내부 소켓 관리

- TStcp_Parse, TStcp_LinkTest, TStcp_Set_LinkTest 및 TStcp_Send는 void형이며
  프레임 포인터만 받는다. Get_LinkTest도 기존 void형을 유지한다.
- TStcp_context는 파일 내부 static 객체이다. 연결 소켓과 직전 처리 결과를
  보관하며 TCP 태스크만 접근한다. 위 API와 결과 조회도 TCP 태스크에서만
  호출한다. 다른 태스크가 사용하려면 요청 큐 등으로 TCP 태스크에 전달해야 한다.
- TStcp_Init_Communication은 내부 소켓을 생성·보관하고, 이미 연결되어 있으면
  같은 소켓을 반환한다. TStcp_Init_Client(void)는 내부 소켓으로 수신한 뒤
  종료 시 닫고 내부 소켓 값을 -1로 무효화한다. 소켓의 소유권은 호출자에게
  이전하지 않는다. 연결 및 수신 함수의 반환형은 기존 상태 확인용으로 유지한다.
- void 처리 함수의 결과는 TStcp_Get_Result(&result)로 조회한다. 수신 루프도
  파싱 후 내부 결과를 확인하므로 송신 실패 시 연결 정리와 재접속이 유지된다.
- 사용자가 추가한 GET_COMMAND(0x0010) 분기는 LinkTest 응답 대신
  TStcp_Get_Command(frame)을 호출한다. 중앙 FSM 전달 기능은 아직 골격이다.
- make 및 make check를 통과했다. 디버그 비활성 모의 통신으로 LinkTest 응답,
  분할/연속 프레임, 미지원 ID, 소켓 재사용 및 EOF 후 무효화, 송신 오류 전달과
  소켓 정리, 접속 실패, NULL 처리와 GET_COMMAND 분기를 확인했다.

## CRC 규칙 확정: 바이트 합계의 하위 16비트

- `uint16_t TStcp_Get_Crc(const TStcp_frame_t * frame)`를 추가했다. msgid,
  len, payload, reserved를 전송 형식으로 직렬화한 12바이트를 한 바이트씩
  더하고 `sum & 0xFFFF`를 반환한다. 기존 crc 멤버와 구조체 패딩은 읽지 않는다.
  NULL이면 0을 반환하며 프레임이나 통신 상태를 수정하지 않는다.
- LinkTest 및 명령 결과 응답에서 `response.crc = TStcp_Get_Crc(&response)`로
  송신 전 계산한다. 명령 결과 응답의 reserved는 0으로 초기화한다.
- 기존 문서의 CRC 미정/응답 CRC=0 설명은 이 규칙으로 대체한다. 수신 프레임의
  CRC 불일치를 거부하는 기능은 이번 변경에서 추가하지 않았다.
- Python 테스터도 같은 규칙으로 요청을 생성하고 응답을 검사한다. LinkTest
  ID는 현재 C 헤더와 같은 0x0000이다. 기본 LinkTest 응답 CRC는 0x004A이다.
- 0/최댓값/100개 임의 값, 기존 CRC 변경에 대한 결과 불변, NULL 입력을 검사했다.
  제어 모듈을 대체한 모의 송신에서 LinkTest와 명령 결과 응답의 CRC를 확인했다.
  현재 전체 빌드는 별도 편집 중인 TScontrol.c의 미완성 초기화 식과 함수 선언
  누락으로 실패하며, 해당 사용자 편집 내용은 변경하지 않았다.

## SPI 진단 보조 스레드

[TSspi.c](../app/TSspi.c)의 TSspi_Debug 계열은 DEBUG_ENABLED 전처리로 제외할 수 있다. 진단 구조체는 [TSspi.h](../app/TSspi.h)에 두며 고정 크기 버퍼와 정적 수명 문자열만 사용한다. SPI 생산자는 trylock으로 즉시 반환하고, 보조 소비자는 복사 후 잠금을 풀고 출력한다. POSIX pthread 속성/분리 스레드/시그널 마스크와 write는 플랫폼 의존 검토 대상이다. 동적 메모리를 직접 할당하지 않지만 pthread 구현의 자원·메모리 사용은 외부 라이브러리 범위다. 활성 진단에는 실행 비용이 있으므로 실물 실시간성 검증과 MISRA 전용 검사를 대신하지 않는다.
