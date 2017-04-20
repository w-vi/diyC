# Welcome to diyC

A simple educational Linux container runtime. It is intentionally
simple and leaves a lot of stuff out. It is a single C file of roughly
400 lines showing the core features of the Linux used to build
containers. It includes also the creation of a container from an image
to clarify how these work together.

## Important note

It plays with iptables to get the routing and isolation running so if
you have your own iptables rules make sure to save them before doing
anything else `sudo iptables-save` and `sudo iptables-restore` to
recover them in case something goes awry.

## Prerequisites

This is a educational piece of software and is not very portable, here
are the prerequisites:

- recent Linux kernel supporting needed namespaces and cgroups
- overlayfs installed
- ip tool (iproute2 package)
- iptables
- gcc
- make
- bash

Apart from overlayfs most of the distros are prepared and ready, if
not please consult your distro package manager. Overlayfs is in the
mainline kernel so it should be straightforward. It was merged in
version 3.18 but has been improved a lot so you should aim for kernel
4.x.

*Note*: Kernel needs to be configured to support following namespaces
PID, mount, network and UTS, cgroups are needed as well. Most of the GNU/Linux distros have
this support enabled by default.


## Installation

1. `make setup`

It creates the necessary directory structure as well as prepares the
networking part like iptables rules, bridge (diyc0) and so on. To
remove the networking bits like bridge and iptables rules run `make
net-clean` which removes them all.

2. `make`

Builds the runtime.

3. Done

It also builds a `nsexec` which executes a local command in namespaces. See `nsexec --help` to see what namespaces are available. Usage is very simple `sudo ./nsexec -pnu myhost bash` will start a new bash in new pid, network and UTS namespace.


## Preparing images

The image import and creation is not present but because images are
just **TARBALLS** there is no need for anything fancy.


### Creating the tarball using docker

Using docker is the most straightforward. `docker pull` the image you
want, spin it up by `docker run -ti <image> <command>` and in
different terminal do `docker export <container> > myimage.tar`. You
have the tarball ready.

### Installing image

`make setup` creates an `images` subdirectory so `mkdir
images/myimage` followed by `tar -xf myimage.tar -C images/myimage/`
should do the trick.

See [Usage to run a container](usage.md)
