# On-node tests

`PTYDRIVE` (`ptydrive.c`, from vms-histsh) plays the user's terminal: it spawns DCL on a
pseudo-terminal and types a script at it, printing what came back with control
characters made visible. Full-screen tests of VTREE run under it. It includes
`probes/probe_common.h`; upload both to the work directory.
