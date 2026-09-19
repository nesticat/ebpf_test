#include "vmlinux.h"

#include <bpf/bpf_core_read.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

const volatile char blocked_prefix[128] = "/tmp/blocked";

static __always_inline int has_prefix(const char *value, const char *prefix)
{
	#pragma unroll
	for (int i = 0; i < sizeof(blocked_prefix); i++) {
		if (prefix[i] == '\0')
			return 1;
		if (value[i] != prefix[i])
			return 0;
	}
	return 0;
}

SEC("fmod_ret/security_bprm_check")
int BPF_PROG(block_new_exec, int ret, struct linux_binprm *bprm)
{
	char filename[256] = {};
	const char *name;

	/* Preserve a denial returned by another security check. */
	if (ret != 0)
		return ret;

	name = BPF_CORE_READ(bprm, filename);
	if (!name)
		return 0;

	if (bpf_probe_read_kernel_str(filename, sizeof(filename), name) < 0)
		return 0;

	if (has_prefix(filename, blocked_prefix))
		return -13; /* -EACCES */

	return 0;
}

char LICENSE[] SEC("license") = "GPL";