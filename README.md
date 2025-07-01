# bmd-plugin-hookcmd
The bmd plugin hookcmd spawns the specific command on the following conditions.

1. Before starting a VM.
2. After stopping a VM.
3. A VM's state is changed.

## Configuration

This plugin expands VM configuration and add 'hookcmd' parameter to specify
which command is spawned.

| parameter | value |
|-----------|-------|
| prestart  | File path to the spawn command |
| poststop  | File path to the spawn command |
| hookcmd   | File path to the spawn command |

`prestart` command is spawned before starting a VM. The VM will start after
`prestart` command exits sucessfully. If `prestart` command exits with
non-zero value, the VM won't start.

`poststop` command is spawned after stopping a VM.

`hookcmd` command is spawned when a VM state is changed.

## Command Parameter

The spawned command receives following arguments.

```
command <VM name> <VM state>
```

`VM name` is a name of VM. `VM state` is one of following values.

| VM state  | when |
|-----------|------|
| PRESTART  | Before VM starts. |
| LOAD      | VM loader is invoked. |
| RUN       | VM start running. |
| POSTSTOP  | After VM stops. |
| TERMINATE | VM is terminated. |
