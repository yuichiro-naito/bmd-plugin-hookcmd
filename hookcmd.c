#include <sys/limits.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
#include <pwd.h>

#include <bmd_plugin.h>

#define GID_NOBODY   65534

#define ARRAY_FOREACH(p, a) \
	for (p = &a[0]; p < &a[nitems(a)]; p++)

extern PLUGIN_DESC plugin_desc;

static int
hookcmd_parse_config(nvlist_t *config, const char *key, const char *val)
{
	const char *k = NULL;
	const char *const *p;
	static const char *const keys[] = { "prestart","hookcmd", "poststop" };
	struct stat sb;

	ARRAY_FOREACH (p, keys)
		if (strcasecmp(key, *p) == 0) {
			k = *p;
			break;
		}
	if (k == NULL)
		return 1;

	if (stat(val, &sb) < 0 ||
	    (sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) == 0)
		return -1;

	nvlist_add_string(config, k, val);
	return 0;
}

static int
on_process_exit(int id, void *data __unused)
{
	return waitpid(id, NULL, WNOHANG);
}

static const char * const state_name[] = {
	"TERMINATE", "LOAD", "RUN",
	"STOP", "REMOVE", "RESTART", "PRESTART", "POSTSTOP"
};

static void
exec_command(struct vm *vm, nvlist_t *config, const char *key, bool do_setuid)
{
	int fd;
	uid_t user;
	gid_t group;
	struct passwd *pwd;
	const char *cmd0;
	char *cmd1, *cmd2;
	FILE *fp;
	char  **args, *buf;
	size_t len;
	struct vm_conf *conf = vm_get_conf(vm);
	extern char **environ;
	struct stat st;

	cmd0 = nvlist_get_string(config, key);
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

	vm_conf_export_env(conf);

	if (do_setuid && (user = get_owner(conf)) > 0) {
		if ((group = get_group(conf)) == UINT_MAX)
			group = (pwd = getpwuid(user)) ?
				pwd->pw_gid : GID_NOBODY;
		setgid(group);
		setuid(user);
	}

	if ((fd = open(cmd0, O_RDONLY)) < 0) {
		plugin_errlog(&plugin_desc, "failed to open %s", cmd0);
		goto err;
	}

	if (! do_setuid) {
		if (fstat(fd, &st) < 0) {
			plugin_errlog(&plugin_desc, "failed to stat %s", cmd0);
			goto err;
		}
		if (st.st_uid != 0) {
			plugin_errlog(&plugin_desc, "'%s' is not owned by root",
				       cmd0);
			goto err;
		}
	}

	fexecve(fd, args, environ);
err:
	exit(1);
}

static void
hookcmd_status_change(struct vm *vm, nvlist_t *config)
{
	pid_t pid;

	if (! nvlist_exists_string(config, "hookcmd") ||
	    (pid = fork()) < 0)
		return;

	if (pid == 0)
		exec_command(vm, config, "hookcmd", true);

	plugin_wait_for_process(pid, on_process_exit, NULL);
}

struct prestart_conf {
	struct vm *vm;
	nvlist_t  *config;
};

static int
on_prestart_exit(int id, void *data)
{
	struct prestart_conf *c = data;
	struct vm *vm = c->vm;
	nvlist_t *config = c->config;
	const char *cmd0;
	int status;

	free(c);

	if (waitpid(id, &status, WNOHANG) < 0)
		goto err;

	cmd0 = nvlist_get_string(config, "prestart");

	if (WIFSIGNALED(status))
		plugin_errlog(&plugin_desc, "%s stoppped by signal %d%s", cmd0,
			      WTERMSIG(status),
			      (WCOREDUMP(status) ? " coredumped" : ""));
	else if (WIFSTOPPED(status))
		plugin_errlog(&plugin_desc, "%s stopped by signal %d", cmd0,
			      WSTOPSIG(status));

	if (WIFEXITED(status) && WEXITSTATUS(status) > 0)
			plugin_errlog(&plugin_desc, "%s returned %d", cmd0,
				      WEXITSTATUS(status));

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		plugin_start_virtualmachine(&plugin_desc, vm);
	else
		plugin_stop_virtualmachine(&plugin_desc, vm);

	return 0;
err:
	plugin_stop_virtualmachine(&plugin_desc, vm);
	return -1;
}

static int
hookcmd_prestart(struct vm *vm, nvlist_t *config)
{
	pid_t pid;
	struct prestart_conf *c;

	if (! nvlist_exists_string(config, "prestart"))
		return 0;

	if ((c = malloc(sizeof(*c))) == NULL)
		return -1;

	c->vm = vm;
	c->config = config;

	if ((pid = fork()) < 0) {
		free(c);
		return -1;
	}

	if (pid == 0)
		exec_command(vm, config, "prestart", false);

	plugin_wait_for_process(pid, on_prestart_exit, c);
	return 1;
}

static int
on_poststop_exit(int id, void *data)
{
	struct vm *vm = data;

	waitpid(id, NULL, WNOHANG);
	plugin_cleanup_virtualmachine(&plugin_desc, vm);

	return 0;
}

static int
hookcmd_poststop(struct vm *vm, nvlist_t *config)
{
	pid_t pid;

	if (! nvlist_exists_string(config, "poststop"))
		return 0;

	if ((pid = fork()) < 0)
		return -1;

	if (pid == 0)
		exec_command(vm, config, "poststop", false);

	plugin_wait_for_process(pid, on_poststop_exit, vm);
	return 1;
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
	.loader_method = NULL,
	.prestart = hookcmd_prestart,
	.poststop = hookcmd_poststop,
};
