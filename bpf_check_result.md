# 현재 환경 BPF 점검 결과

점검 시각: `2026-09-19T10:29:09+09:00`

## 1. 최종 요약

현재 환경은 eBPF 전체가 비활성화된 상태는 아니다. systemd와 Snap이 이미 BPF 프로그램을 로드하고 있으며, JIT와 cgroup/network BPF도 동작 중이다.

하지만 현재 작업 중인 `exec_guard`의 핵심 경로인 `fmod_ret/security_bprm_check`를 사용하기에는 커널 지원이 부족하다.

- 실행 중인 커널이 BTF를 제공하지 않음
- `CONFIG_BPF_LSM` 비활성화
- `tracing`, `ext`, `lsm` BPF program type 미지원
- 따라서 CO-RE 빌드와 fmod_ret/BPF LSM attach를 현재 환경에서 검증할 수 없음

## 2. 시스템 정보

```text
OS       Ubuntu 24.04.4 LTS
Kernel   6.8.12-1021-tegra
Arch     aarch64 / ARM64
```

확인 명령:

```sh
uname -a
cat /etc/os-release
uname -m
```

## 3. 사용자 공간 도구

| 항목 | 확인 결과 |
| --- | --- |
| clang | Ubuntu clang 18.1.3 |
| llvm-strip | `/usr/bin/llvm-strip` |
| libbpf-dev | 1.3.0 |
| bpftool | v7.4.0, libbpf v1.4 |
| gcc / make | 설치됨 |
| libelf-dev / zlib1g-dev | 설치됨 |

`/usr/sbin/bpftool`은 현재 Tegra 커널 버전용 패키지를 찾는 wrapper라 다음 경고를 출력한다.

```text
WARNING: bpftool not found for kernel 6.8.12-1021
```

실제 설치된 bpftool은 다음 경로에서 정상 실행된다.

```sh
/usr/lib/linux-tools/6.8.0-139-generic/bpftool
```

## 4. 커널 설정

`/proc/config.gz`에서 확인한 주요 설정이다.

```text
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_JIT_DEFAULT_ON=y
CONFIG_BPF_UNPRIV_DEFAULT_OFF=y
CONFIG_BPF_EVENTS=y
CONFIG_CGROUP_BPF=y
CONFIG_NET_CLS_BPF=y
CONFIG_NET_SCH_INGRESS=y
CONFIG_FTRACE=y

# CONFIG_BPF_LSM is not set
# CONFIG_BPF_JIT_ALWAYS_ON is not set
# CONFIG_FTRACE_SYSCALLS is not set
```

해석:

- BPF syscall, JIT, events, cgroup BPF, 네트워크 BPF는 활성화됨
- 비특권 BPF는 기본적으로 제한됨
- BPF LSM은 비활성화됨
- `CONFIG_DEBUG_INFO=y`는 있으나 `CONFIG_DEBUG_INFO_BTF=y`는 확인되지 않음

## 5. BTF 상태

다음 경로가 존재하지 않는다.

```text
/sys/kernel/btf
/sys/kernel/btf/vmlinux
```

따라서 Makefile의 다음 명령은 실패한다.

```sh
bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h
```

오류:

```text
Error: failed to load BTF from /sys/kernel/btf/vmlinux: No such file or directory
```

현재 BPF 소스는 `vmlinux.h`와 CO-RE를 사용한다.

```c
#include "vmlinux.h"
#include <bpf/bpf_core_read.h>
```

그러므로 BTF가 없는 현재 환경에서는 BPF object 컴파일 전에 중단된다. `bpftool cgroup tree`에서도 기존 프로그램을 출력하면서 다음 경고가 반복된다.

```text
libbpf: failed to find valid kernel BTF
```

## 6. `bpftool feature probe` 결과

### 지원됨

- JIT compiler
- `kprobe`, `tracepoint`, `raw_tracepoint`
- `cgroup_skb`, `cgroup_device`, `cgroup_sock`
- `xdp`, `perf_event`, `struct_ops`, `syscall`, `netfilter`
- `hash`, `array`, `ringbuf`, `task_storage` 등 다수 map type

### 지원되지 않음

```text
eBPF program_type tracing is NOT available
eBPF program_type ext is NOT available
eBPF program_type lsm is NOT available
eBPF map_type inode_storage is NOT available
```

따라서 현재 커널에서는 BPF LSM 기반 실행 차단이 불가능하다. `fmod_ret`도 tracing/ext와 BPF trampoline/BTF 지원에 의존하므로 현재 환경에서 attach 가능성이 보장되지 않는다.

심볼에 `security_bprm_check`, `bpf_trampoline_get`, `bpf_trampoline_link_prog`가 존재하더라도, 심볼 존재만으로 fmod_ret attach가 가능한 것은 아니다.

## 7. 로드된 BPF 프로그램

실제 bpftool로 다음 프로그램이 로드·JIT 실행 중인 것을 확인했다.

| 유형 | 이름 예시 |
| --- | --- |
| `cgroup_device` | `sd_devices` |
| `cgroup_skb` | `sd_fw_egress` |
| `cgroup_skb` | `sd_fw_ingress` |
| `cgroup_device` | `s_cups_cupsd` |
| `cgroup_device` | `s_chromium_chro` |

확인된 예시 ID는 `74`부터 `104` 사이에 있으며, 출력에는 `xlated`, `jited`, `memlock` 정보가 표시된다. 이는 BPF syscall과 JIT가 실제로 동작한다는 근거다.

## 8. BPF filesystem, map, cgroup

bpffs는 정상 마운트되어 있다.

```text
bpf on /sys/fs/bpf type bpf
rw,nosuid,nodev,noexec,relatime,mode=700
```

Pinned 객체:

```text
/sys/fs/bpf/snap/snap_chromium_chromium
/sys/fs/bpf/snap/snap_cups_cupsd
```

확인된 map:

```text
1: hash  name s_cups_cupsd     key 9B value 1B max_entries 1000
2: hash  name s_chromium_chro  key 9B value 1B max_entries 1000
```

cgroup은 cgroup v2이며 다음 attach가 실제 존재한다.

- `cgroup_device`
- `cgroup_inet_ingress`
- `cgroup_inet_egress`

결론적으로 bpffs와 cgroup BPF 경로는 정상이고, 문제는 BPF filesystem 미마운트가 아니다.

## 9. 보안 및 권한

```text
kernel.unprivileged_bpf_disabled = 1
kernel.kptr_restrict = 1
kernel.dmesg_restrict = 1
```

현재 일반 사용자 프로세스의 capability는 다음과 같다.

```text
CapEff: 0000000000000000
```

`cap_bpf` 등이 bounding set에는 있지만 effective set에는 없다. BPF 로드와 attach에는 `sudo` 또는 적절한 capability가 필요하다.

활성 LSM:

```text
capability,landlock,yama,apparmor
```

`/sys/kernel/security/lockdown`은 존재하지 않아 lockdown 상태는 확인할 수 없었다.

## 10. 커널 자료와 로그

확인된 항목:

- `/sys/fs/bpf`: bpffs 마운트됨
- `/sys/kernel/tracing`: tracefs 존재
- `/sys/kernel/debug`: debugfs 마운트됨
- `/lib/modules/6.8.12-1021-tegra`: 존재
- Tegra 전용 헤더 디렉터리: `/usr/src/linux-headers-6.8.12-1021-tegra-ubuntu24.04_aarch64`
- `/sys/kernel/kheaders.tar.xz`: 없음
- `/sys/kernel/btf/vmlinux`: 없음

커널 로그에서는 BPF 오류 대신 systemd 빌드 정보인 `-BPF_FRAMEWORK`가 확인되었다. 이는 systemd 자체 BPF framework 기능이 없다는 정보이며, 커널 eBPF 지원이 꺼졌다는 의미는 아니다.

## 11. 현재 샘플 판정

현재 샘플의 hook:

```c
SEC("fmod_ret/security_bprm_check")
```

| 단계 | 판정 | 이유 |
| --- | --- | --- |
| clang/libbpf 설치 | 가능 | 필요한 도구 설치됨 |
| `vmlinux.h` 생성 | 불가 | BTF 경로 없음 |
| BPF object 컴파일 | 현재 불가 | `vmlinux.h` 생성 실패 |
| BPF LSM 사용 | 불가 | `CONFIG_BPF_LSM` 및 `lsm` type 없음 |
| tracing/ext attach | 불가 또는 미보장 | feature probe에서 미지원 |
| 일반 cgroup/network BPF | 가능 | 기존 프로그램이 로드·JIT 실행 중 |
| 파일 생성 차단 | 샘플 목적과 불일치 | 샘플은 실행 요청 차단용 |

## 12. 권장 조치

실행 차단 정책을 구현하려면 다음 설정을 포함한 커널로 부팅하는 것이 가장 명확하다.

```text
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_LSM=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_FTRACE=y
```

부팅 후 다음을 확인한다.

```sh
test -r /sys/kernel/btf/vmlinux && echo "BTF available"
bpftool feature probe kernel
```

파일 생성 자체를 차단하려면 현재 `security_bprm_check` 샘플을 확장하면 안 된다. `bprm`은 실행 요청에 해당한다. 파일 생성 목적이라면 BPF LSM inode hook, fanotify, auditd, AppArmor 또는 SELinux를 별도로 검토해야 한다.

## 결론

현재 환경에서는 JIT, cgroup BPF, 네트워크 BPF, bpffs, 기존 프로그램 로드는 확인되었다. 그러나 실행 중인 Tegra 커널에 BTF가 없고 `CONFIG_BPF_LSM`이 없으며 `tracing`, `ext`, `lsm` program type이 지원되지 않는다.

따라서 현재 `exec_guard`가 동작하지 않는 1차 원인은 clang이나 bpftool 설치 문제가 아니라, Tegra 커널의 BTF 및 BPF 프로그램 타입 구성이다.