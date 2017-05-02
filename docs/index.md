# Linux containers from scratch - diyC

Linux containers exist for a while and are now a mainstream
topic. This is an introduction on how they are created and what they
actually are made of. If you want to see the code then head directly
to [the GitHub repo](https://github.com/w-vi/diyc) otherwise read on
the topic you are interested in.

!!! note 
    Any suggestions or comments are welcome please don't hesitate
    and [file an issue on GitHub](https://github.com/w-vi/diyc/issues/new).


## What is a Linux container

Containers in some form exist for quite while even though [we don't
always think of them as containers](http://www.networkworld.com/article/2226996/cisco-subnet/software-containers--used-more-frequently-than-most-realize.html). 

> Linux container to put it simply it is a usual Linux process (or
> group of processes) with a limited (altered) view of the system.

It is achieved by utilizing
[Operating system level virtualization](https://en.wikipedia.org/wiki/Operating-system-level_virtualization).


## Building containers from scratch

Follwing are the kernel features and other bits needed to build
containers from scratch.

- [Linux Namespaces](namespaces.md)
- [Linux cgroups](cgroups.md)
- [Networking](networking.md)
- [Images and containers](images-containers.md)

One topic which is not covered although it should is
[capabilities(7)](http://man7.org/linux/man-pages/man7/capabilities.7.html) and
privilages like low port binding .


## What is diyC

It is a simple educational Linux container runtime. It is
intentionally simple and leaves a lot of stuff out. It is
a [single C file](https://github.com/w-vi/diyC/blob/master/src/diyc.c)
of roughly 500 lines including comments showing the core features of
the Linux used to build containers. It includes also the creation of a
container from an image to clarify how images and containers are
related.

## nsexec

Part of the project is also
a [`nsexec` binary](https://github.com/w-vi/diyC/blob/master/src/nsexec.c) 
which is is a very simple program that executes a local (host) command
in Linux namespaces. See `nsexec --help` to see what namespaces are
available. Usage is very simple `sudo ./nsexec -pnu myhost bash` will
start a new bash in new pid, network and UTS namespace.
