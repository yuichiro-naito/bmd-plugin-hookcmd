# bmd-plugin-hookcmd
The bmd plugin hookcmd spawns the specific command when VM's status is changed.

## Configuration

This plugin expands VM configuration and add 'hookcmd' parameter to specify
which command is spawned.

| parameter | value |
|-----------|-------|
| hookcmd   | File path to the spawn command |

## Command Parameter

The spawned command receives following arguments.

```
command <VM name> <VM state>
```

`VM name` is a name of VM. `VM state` is one of following values.

| VM state  | when |
|-----------|------|
| LOAD      | VM loader is invoked. |
| RUN       | VM start running. |
| TERMINATE | VM is terminated. |
