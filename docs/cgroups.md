# Linux cgroups a.k.a. Control Groups

Without setting limits on resources the isolation of containers
doesn't work well because a rogue container can eat up all the memory
or file descriptors of the host system and thus bring everything
down.

We can use usual limits 
[getrlimit(2)](http://man7.org/linux/man-pages/man2/setrlimit.2.html)
to achieve something but it is far from enough and brings issues
because of UIDs mappings, capabilities and so on.

## Cgroups

Cgroups is a Linux kernel feature to limit, police and account the
resource usage for a set of processes and containers are just
that.

Cgroups are hierarchical and child cgroups inherit certain attributes
from their parent cgroup and cannot override those set by
parent. Every process or thread belongs to one cgroup in given
subsystem.


There are actually two versions of the implementation
[v1](https://www.kernel.org/doc/Documentation/cgroup-v1/)
and [v2](https://www.kernel.org/doc/Documentation/cgroup-v2.txt) where
the main difference is that v1 operates on thread level but v2 is
simpler and considers only processes.


## Cgroup subsystems

Some of the subsystems are listed here see
[Fedora Resource Management Guide](https://docs.fedoraproject.org/en-US/Fedora/17/html-single/Resource_Management_Guide/index.html#sec-How_Control_Groups_Are_Organized) for
all the details.

+ *blkio* - limits on IO
+ *cpu* - cpu scheduling
+ *cpuset* - assigns CPU(s)n on multicore systems
+ *devices* - controls access to devices
+ *memory* - memory limits like rss, swap etc.

## Managing cgroups

Cgroups have no syscalls but are managed through a filesystem under
`/sys/fs/cgroup` where each of the subsystem is directory.

It is very easy to create a new group in any of the subsystems just
create subdirectory and kernel will populate it with the files.

```bash

$ mkdir /sys/fs/cgroup/memory/mygroup
$ ls -la /sys/fs/cgroup/memory/mygroup

total 0
drwxr-xr-x 2 root root 0 28.04.2017 13:05 ./
dr-xr-xr-x 6 root root 0 28.04.2017 13:03 ../
-rw-r--r-- 1 root root 0 28.04.2017 13:05 cgroup.clone_children
--w--w--w- 1 root root 0 28.04.2017 13:05 cgroup.event_control
-rw-r--r-- 1 root root 0 28.04.2017 13:05 cgroup.procs
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.failcnt
--w------- 1 root root 0 28.04.2017 13:05 memory.force_empty
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.failcnt
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.limit_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.max_usage_in_bytes
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.slabinfo
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.tcp.failcnt
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.tcp.limit_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.tcp.max_usage_in_bytes
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.tcp.usage_in_bytes
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.kmem.usage_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.limit_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.max_usage_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.memsw.failcnt
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.memsw.limit_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.memsw.max_usage_in_bytes
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.memsw.usage_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.move_charge_at_immigrate
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.numa_stat
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.oom_control
---------- 1 root root 0 28.04.2017 13:05 memory.pressure_level
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.soft_limit_in_bytes
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.stat
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.swappiness
-r--r--r-- 1 root root 0 28.04.2017 13:05 memory.usage_in_bytes
-rw-r--r-- 1 root root 0 28.04.2017 13:05 memory.use_hierarchy
-rw-r--r-- 1 root root 0 28.04.2017 13:05 notify_on_release
-rw-r--r-- 1 root root 0 28.04.2017 13:05 tasks

```

Now we can set a memory limit to 10 MB for this group.

```bash
$ echo 1000000 > /sys/fs/cgroup/memory/mygroup/memory.limit_in_bytes
```

To add a process to our group we need to just append the pid to
`cgroup.procs` file.

```bash
 echo $$ > /sys/fs/cgroup/memory/groupname/cgroup.procs
```

The shell and it's children are now limited to 10MB which you can
easily check by allocation enough memory.

Use `rmdir` to remove the group. The group cannot be removed untill
there are no processes running in that group.


!!! seealso "Code"
    The cgroup and limits are set up after the `clone` when the pid of
    the container is known.
    [diyc.c:198-237](https://github.com/w-vi/diyC/blob/master/src/diyc.c#L198-L238)  
    [Example using diyc](diyc/usage#example-limit-memory-used-by-cgroups)


## More to read

- [Linux kernel cgroups v1](https://www.kernel.org/doc/Documentation/cgroup-v1/)
- [Linux kernel cgroups v2](https://www.kernel.org/doc/Documentation/cgroup-v2.txt)
- [Control groups on Ubuntu Server Guide](https://help.ubuntu.com/lts/serverguide/cgroups.html)
- [RHEL Resource Management Guide](https://access.redhat.com/documentation/en-US/Red_Hat_Enterprise_Linux/6/html/Resource_Management_Guide/ch01.html)
  

