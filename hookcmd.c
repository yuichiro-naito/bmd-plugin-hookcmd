#include <sys/limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <pwd.h>

#include <bmd_plugin.h>

#define GID_NOBODY   65534

static int
hookcmd_parse_config(nvlist_t *config, const char *key, const char *val)
{
	struct stat sb;

	if (strcasecmp(key, "hookcmd"))
		return 1;

	if (stat(val, &sb) < 0 ||
	    (sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
		return -1;

	nvlist_add_string(config, "hookcmd", val);
	return 0;
}

static int
on_process_exit(int id, void *data __unused)
{
	return waitpid(id, NULL, WNOHANG);
}

static void
hookcmd_status_change(struct vm *vm, nvlist_t *config)
{
	pid_t pid;
	uid_t user;
	gid_t group;
	struct passwd *pwd;
	struct vm_conf *conf = vm_get_conf(vm);
	static const char * const state_name[] =
		{ "TERMINATE", "LOAD", "RUN",
		"STOP", "REMOVE", "RESTART" };

	if (! nvlist_exists_string(config, "hookcmd") ||
	    (pid = fork()) < 0)
		return;

	if (pid == 0) {
		const char *cmd0;
		char *cmd1, *cmd2;
		FILE *fp;
		char  **args, *buf;
		size_t len;

		cmd0 = nvlist_get_string(config, "hookcmd");
		if ((cmd1 = strdup(cmd0)) == NULL)
			return;
		cmd2 = basename(cmd1);

		fp = open_memstream(&buf, &len);
		if (fp == NULL)
			exit(1);
		fprintf(fp, "%s\n%s\n%s\n", cmd2, get_name(conf),
			state_name[get_state(vm)]);

		fclose(fp);

		args = split_args(buf);
		if (args == NULL)
			exit(1);

		if ((user = get_owner(conf)) > 0) {
			if ((group = get_group(conf)) == UINT_MAX)
				group = (pwd = getpwuid(user)) ?
					pwd->pw_gid : GID_NOBODY;
			setgid(group);
			setuid(user);
		}

		execv(cmd0, args);
		exit(1);
	}

	plugin_wait_for_process(pid, on_process_exit, NULL);
}

PLUGIN_DESC plugin_desc = {
	.version = PLUGIN_VERSION,
	.name = "hookcmd",
	.initialize = NULL,
	.finalize = NULL,
	.on_status_change = hookcmd_status_change,
	.parse_config = hookcmd_parse_config,
	.method = NULL,
	.on_reload_config = NULL,
};
