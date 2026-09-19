#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <bpf/libbpf.h>

#include "exec_guard.skel.h"

static volatile sig_atomic_t exiting;

static void on_signal(int signal_number)
{
	(void)signal_number;
	exiting = 1;
}

static void libbpf_log(enum libbpf_print_level level, const char *format,
			       va_list args)
{
	if (level == LIBBPF_DEBUG)
		return;
	vfprintf(stderr, format, args);
}

int main(int argc, char **argv)
{
	struct exec_guard_bpf *skel;
	const char *prefix = argc > 1 ? argv[1] : "/tmp/blocked";
	int err;

	if (strlen(prefix) >= sizeof(skel->rodata->blocked_prefix)) {
		fprintf(stderr, "prefix is too long (maximum 127 bytes)\n");
		return EXIT_FAILURE;
	}

	libbpf_set_print(libbpf_log);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	skel = exec_guard_bpf__open();
	if (!skel) {
		fprintf(stderr, "failed to open BPF skeleton\n");
		return EXIT_FAILURE;
	}

	strncpy(skel->rodata->blocked_prefix, prefix,
			sizeof(skel->rodata->blocked_prefix) - 1);

	err = exec_guard_bpf__load(skel);
	if (err) {
		fprintf(stderr, "failed to load BPF program: %s\n", strerror(-err));
		goto cleanup;
	}

	err = exec_guard_bpf__attach(skel);
	if (err) {
		fprintf(stderr, "failed to attach fmod_ret program: %s\n", strerror(-err));
		goto cleanup;
	}

	printf("blocking execution of paths beginning with '%s'\n", prefix);
	printf("press Ctrl-C to stop\n");
	while (!exiting)
		sleep(1);

cleanup:
	exec_guard_bpf__destroy(skel);
	return err ? EXIT_FAILURE : EXIT_SUCCESS;
}