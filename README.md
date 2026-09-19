# libbpf `fmod_ret` execution guard

This example changes the return value of `security_bprm_check()` to deny
execution of paths with a configured prefix. The default prefix is
`/tmp/blocked`.

## Install dependencies on Ubuntu 24.04

```sh
sudo apt update
sudo apt install -y clang llvm libbpf-dev libelf-dev zlib1g-dev bpftool make gcc
```

The running kernel must expose BTF at `/sys/kernel/btf/vmlinux`, and the
kernel must support BPF trampolines and `BPF_MODIFY_RETURN`. A distribution
kernel is recommended over a vendor kernel for the first test.

## Build and run

```sh
make
sudo ./exec_guard /tmp/blocked
```

In another terminal:

```sh
mkdir -p /tmp/blocked
cp /bin/true /tmp/blocked/true
/tmp/blocked/true          # expected: Permission denied
/bin/true                  # expected: succeeds
```

Stop the guard with Ctrl-C. Loading and attaching BPF generally requires
root or suitable BPF capabilities.

## Important limitations

`fmod_ret` is a kernel-function return override, not a stable security policy
API. It depends on the target function's BTF ID and on kernel support for
modify-return trampolines. It also runs after the function has executed, so
it is not equivalent to a complete mandatory-access-control policy.

For production policy enforcement, prefer a BPF LSM program attached to
`lsm/bprm_check_security` (with a kernel built with `CONFIG_BPF_LSM`). Use
auditd or a ring buffer/perf buffer if an event record is required; this
minimal sample only allows or denies execution.