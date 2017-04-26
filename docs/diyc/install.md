# Getting started

!!! danger "iptables"
    diyC plays with iptables to get the routing and isolation running so if
    you have your own iptables rules make sure to save them before doing
    anything else. Just `sudo iptables-save` and `sudo iptables-restore` to
    recover them in case something goes awry.

## Prerequisites

This is a educational piece of software and has not been tested on
many systems yet, to give it a go make sure you have the following:

- recent Linux kernel supporting needed namespaces and cgroups
- overlayfs 
- ip tool ([iproute2 package](https://wiki.linuxfoundation.org/networking/iproute2))
- iptables 
- gcc
- make
- bash

Overlayfs is in the mainline kernel so it everything should be
straight forward it was merged in version 3.18 but has been improved a
lot so you should aim for kernel 4.x and in that case you have all the
namespaces and cgroups too.

!!! note "Kernel configuration"
    Kernel needs to be configured to support following namespaces
    PID, mount, network and UTS, cgroups are needed as well. Most of the
    GNU/Linux distros have this support enabled by default.


## Installation

1. `make setup`

    It creates the necessary directory structure as well as prepares the
    networking part like iptables rules, bridge (diyc0) and so on. To
    remove the networking bits like bridge and iptables rules run `make
    net-clean` which removes them all.

2. `make`

    Builds the runtime.

3. Done


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
should do the trick. The relation of images and containers is
described in a section of it's
own. [Images and Containers](../images-containers.md).

!!! sealso "Example"
    See [how to run a container](usage.md)
