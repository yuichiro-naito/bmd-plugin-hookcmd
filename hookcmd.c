#include <sys/param.h>
#include <sys/limits.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <bmd_plugin.h>
#include <fcntl.h>
#include <libgen.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define GID_NOBODY	    65534

#define ARRAY_FOREACH(p, a) for (p = &a[0]; p < &a[nitems(a)]; p++)

struct command_conf {
	const char *type;
	struct vm *vm;
	nvlist_t *config;
};

extern PLUGIN_DESC plugin_desc;

static int
hookcmd_parse_config(nvlist_t *config, const char *key, const char *val)
{
	const char *k = NULL;
	const char *const *p;
	static const char *const keys[] = { "prestart", "hookcmd", "poststop" };
	struct stat sb;

	ARRAY_FOREACH(p, keys)
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

static const char *const state_name[] = { "TERMINATE", "LOAD", "RUN", "STOP",
	"REMOVE", "RESTART", "PRESTART", "POSTSTOP" };

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
	char **args, *buf;
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
			group = (pwd = getpwuid(user)) ? pwd->pw_gid :
							 GID_NOBODY;
		setgid(group);
		setuid(user);
	}

	if ((fd = open(cmd0, O_RDONLY)) < 0) {
		plugin_errlog(&plugin_desc, "failed to open %s", cmd0);
		goto err;
	}

	if (!do_setuid) {
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

	plugin_infolog(&plugin_desc, "%s: %s: %s: %s", get_name(conf), key,
	    state_name[get_state(vm)], cmd0);

	fexecve(fd, args, environ);
err:
	exit(1);
}

static int
wait_command(int id, void *data, int *status, struct vm **vm)
{
	struct command_conf *c = data;
	const char *type = c->type;
	const char *name = get_name(vm_get_conf(c->vm)), *cmd0;
	nvlist_t *config = c->config;
	int st;

	if (vm)
		*vm = c->vm;

	free(c);

	if (waitpid(id, &st, WNOHANG) < 0)
		return -1;

	cmd0 = nvlist_get_string(config, type);
	if (WIFSIGNALED(st))
		plugin_errlog(&plugin_desc, "%s: %s stoppped by signal %d%s",
		    name, cmd0, WTERMSIG(st),
		    (WCOREDUMP(st) ? " coredumped" : ""));
	else if (WIFSTOPPED(st))
		plugin_errlog(&plugin_desc, "%s: %s stopped by signal %d", name,
		    cmd0, WSTOPSIG(st));

	if (WIFEXITED(st) && WEXITSTATUS(st) > 0)
		plugin_errlog(&plugin_desc, "%s: %s returned %d", name, cmd0,
		    WEXITSTATUS(st));

	if (WIFEXITED(st) && WEXITSTATUS(st) == 0)
		plugin_infolog(&plugin_desc, "%s: %s: done", name, type);
	if (status)
		*status = st;
	return 0;
}

static int
on_hookcmd_exit(int id, void *data)
{
	int status;
	return wait_command(id, data, &status, NULL);
}

static int
on_prestart_exit(int id, void *data)
{
	struct vm *vm;
	int status;

	if (wait_command(id, data, &status, &vm) < 0)
		goto err;

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
on_poststop_exit(int id, void *data)
{
	struct vm *vm;
	int status;

	if (wait_command(id, data, &status, &vm) < 0)
		return -1;
	plugin_cleanup_virtualmachine(&plugin_desc, vm);

	return 0;
}

static struct command_conf *
create_command_conf(const char *type, struct vm *vm, nvlist_t *config)
{
	struct command_conf *c;

	if ((c = malloc(sizeof(*c))) == NULL)
		return NULL;

	c->type = type;
	c->vm = vm;
	c->config = config;

	return c;
}

static int
do_command(const char *type, struct vm *vm, nvlist_t *config, bool do_setuid,
    plugin_call_back cb)
{
	struct command_conf *c;
	pid_t pid;

	/*
	  If command path is not defined, do nothing not an error.
	 */
	if (!nvlist_exists_string(config, type))
		return 0;

	if ((c = create_command_conf(type, vm, config)) == NULL)
		return -1;

	if ((pid = fork()) < 0) {
		free(c);
		return -1;
	}

	if (pid == 0)
		exec_command(vm, config, type, do_setuid);

	plugin_wait_for_process(pid, cb, c);
	return 1;
}

static void
hookcmd_status_change(struct vm *vm, nvlist_t *config)
{
	do_command("hookcmd", vm, config, true, on_hookcmd_exit);
}

static int
hookcmd_prestart(struct vm *vm, nvlist_t *config)
{
	return do_command("prestart", vm, config, false, on_prestart_exit);
}

static int
hookcmd_poststop(struct vm *vm, nvlist_t *config)
{
	return do_command("poststop", vm, config, false, on_poststop_exit);
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
