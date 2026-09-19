# 현재 환경에서 eBPF 파일 이벤트 차단이 동작하지 않는 이유

## 결론

현재 환경에서는 사용자 공간 도구 설치는 완료되었지만, 실행 중인 Tegra 커널이 eBPF 프로그램을 빌드하고 로드하는 데 필요한 BTF 정보를 제공하지 않기 때문에 샘플이 동작하지 않는다.

또한 현재 샘플은 엄밀히 말해 **파일 생성 이벤트**를 차단하는 프로그램이 아니다. `security_bprm_check`에 `fmod_ret`를 연결하여 특정 경로의 **실행 요청**을 차단하는 예제다.

## 확인된 환경

- 커널: `6.8.12-1021-tegra`
- 아키텍처: ARM64
- 설치 완료:
  - `clang 18.1.3`
  - `llvm 18`
  - `libbpf-dev 1.3.0`
  - `bpftool`
  - `linux-tools-common`
  - `linux-tools-6.8.0-139`
  - `gcc`, `make`, `libelf-dev`, `zlib1g-dev`

## 직접적인 빌드 실패 원인

샘플의 `Makefile`은 다음 명령으로 실행 중인 커널의 BTF에서 `vmlinux.h`를 생성한다.

```sh
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

하지만 현재 시스템에는 다음 파일이 없다.

```text
/sys/kernel/btf/vmlinux
```

따라서 빌드는 다음 오류로 중단된다.

```text
Error: failed to load BTF from /sys/kernel/btf/vmlinux: No such file or directory
```

`vmlinux.h`는 커널 내부 자료구조인 `struct linux_binprm` 등을 BPF 프로그램에서 사용하기 위한 헤더다. 이 헤더를 생성하지 못하면 BPF 소스 컴파일 단계로 진행할 수 없다.

## BTF가 필요한 이유

현재 BPF 프로그램은 CO-RE 방식을 사용한다.

```c
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
```

CO-RE 프로그램은 실행 중인 커널의 BTF를 이용해 커널 자료구조의 실제 레이아웃을 확인하고, 커널 버전에 맞게 로딩된다. 따라서 다음 조건 중 하나가 필요하다.

- 커널이 `/sys/kernel/btf/vmlinux`를 제공해야 한다.
- 또는 해당 커널에서 추출한 별도의 BTF 정보로 `vmlinux.h`를 생성해야 한다.

현재 Tegra 커널은 BTF 파일을 노출하지 않으므로 `bpftool`을 설치한 것만으로는 해결되지 않는다.

## BPF LSM 관련 제한

이전 커널 설정 확인 결과 다음 설정이 활성화되어 있지 않았다.

```text
# CONFIG_BPF_LSM is not set
```

따라서 다음과 같은 BPF LSM 방식은 현재 커널에서는 사용할 수 없다.

```c
SEC("lsm/bprm_check_security")
```

운영 목적의 실행 차단 정책에는 BPF LSM이 더 적합하지만, 현재 커널을 `CONFIG_BPF_LSM=y`로 다시 빌드하거나 해당 옵션이 활성화된 커널로 부팅해야 한다.

## `fmod_ret` 방식의 제한

현재 샘플은 다음 hook을 사용한다.

```c
SEC("fmod_ret/security_bprm_check")
```

이 방식은 다음 조건에 의존한다.

- 대상 커널 함수가 BTF에 존재해야 한다.
- 커널이 BPF modify-return trampoline을 지원해야 한다.
- 대상 함수에 `fmod_ret`를 attach할 수 있어야 한다.
- 커널의 보안 설정과 lockdown 정책이 BPF 로딩을 허용해야 한다.

따라서 BTF가 추가된 뒤에도 attach 단계에서 실패할 수 있다. 특히 vendor 커널은 Ubuntu generic 커널과 BPF 기능 및 설정이 다를 수 있다.

## 파일 생성과 실행은 다른 이벤트다

현재 샘플이 검사하는 것은 새 파일 생성이 아니라 실행 요청이다.

| 목적 | 적합한 접근 |
| --- | --- |
| 특정 파일 실행 허용/차단 | `fmod_ret/security_bprm_check` 또는 BPF LSM `bprm_check_security` |
| 파일 생성 이벤트 감시 | `fanotify`, auditd, LSM inode hook 등 |
| 파일 생성 자체 차단 | BPF LSM의 inode 관련 hook 또는 기존 MAC 정책 |

`exec_guard`에서 `/tmp/blocked` 아래의 실행 파일을 차단하더라도, 그 경로에 파일을 복사하거나 생성하는 동작까지 차단하지는 않는다.

## 해결을 위해 필요한 다음 단계

### 1. BTF 지원 커널 확인

```sh
test -r /sys/kernel/btf/vmlinux && echo "BTF available"
```

파일이 존재해야 한다.

### 2. 커널 설정 확인

```sh
grep -E 'CONFIG_(BPF|BPF_SYSCALL|BPF_JIT|BPF_LSM|DEBUG_INFO_BTF|FTRACE)' \
  /boot/config-$(uname -r)
```

최소한 BPF 기능과 BTF가 필요하고, BPF LSM 방식을 사용하려면 `CONFIG_BPF_LSM=y`도 필요하다.

### 3. BTF 지원 커널로 부팅 후 빌드

```sh
make clean
make
```

### 4. 로딩 권한 확인

```sh
sudo ./exec_guard /tmp/blocked
```

컨테이너 안에서 실행 중이라면 host 커널의 BPF 권한, BPF filesystem, capability, lockdown 정책도 함께 확인해야 한다.

## 최종 판단

현재 실패의 1차 원인은 `clang`이나 `bpftool` 미설치가 아니다. 필요한 도구는 설치되었다. 현재 실행 중인 `6.8.12-1021-tegra` 커널이 `/sys/kernel/btf/vmlinux`를 제공하지 않는 것이 샘플 빌드가 멈추는 직접 원인이다.

그 다음 단계에서는 BTF 지원 여부와 `fmod_ret` attach 가능 여부를 확인해야 한다. 파일 생성 차단이 목적이라면 실행 차단 샘플을 그대로 확장하지 말고, 생성 hook 또는 BPF LSM 기반 설계로 변경해야 한다.