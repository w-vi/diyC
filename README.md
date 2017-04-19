# DIYC

A simple educational Linux container runtime.

## Important note

It plays with iptables to get the routing and isolation running so if
you have your own iptables rules make sure to save them before doing
anything else `sudo iptables-save` and `sudo iptables-restore` to
recover them in case something goes awry.

## Prerequisites

This is a educational piece of software and is not very portable, here
are the prerequisites:

- recent Linux kernel supporting needed namespaces and cgroups
- aufs installed
- ip tool (iproute2 package)
- iptables
- gcc
- make
- bash

Apart from aufs most of the distros are prepared and ready, if not
please consult your distro package manager.

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

`make setup` creates an `images` subdirectory so
`mkdir images/myimage` followed by
`tar -xf myimage.tar -C images/myimage/`
should do the trick.


## Usage

`diyc [hv][-m NUMBER] [-ip IPV4 ADDRESS] <NAME> <IMAGE> <CMD>`

    -h, --help           print the help

    -i, --ip             ip address of the container, if not set then host
                         network is used. It must be in the 172.16.0/16 network
                         as the bridge diyc0 is 172.16.0.1

    -m, --mem            maximum size of the memory in MB allowed for the container
                         by default there no explicit limit defined.

    -v, --verbose        more verbose output

    <NAME>               name of the container, needs to be unique

    <IMAGE>              image to be used for the container, must be a directory name
                         under the images directory

    <CMD>                command to be executed inside the container



### Example to get a container running

```sh

$ git clone git@github.com:w-vi/diyc.git
Cloning into 'diyc'...
remote: Counting objects: 9, done.
remote: Compressing objects: 100% (8/8), done.
remote: Total 9 (delta 1), reused 9 (delta 1), pack-reused 0
Receiving objects: 100% (9/9), 12.95 KiB | 0 bytes/s, done.
Resolving deltas: 100% (1/1), done.

$ cd diyc

$ make setup
sudo iptables -A FORWARD -i enp5s0 -o veth -j ACCEPT || true
sudo iptables -A FORWARD -o enp5s0 -i veth -j ACCEPT || true
sudo iptables -t nat -A POSTROUTING -s 172.16.0.0/16 -j MASQUERADE || true
sudo ip link add name diyc0 type bridge || true
sudo ip addr add dev diyc0 172.16.0.1/24 || true
sudo ip link set diyc0 up || true
sudo iptables -A FORWARD -i enp5s0 -o diyc0 -j ACCEPT || true
sudo iptables -A FORWARD -o enp5s0 -i diyc0 -j ACCEPT || true
sudo iptables -A FORWARD -o diyc0 -i diyc0 -j ACCEPT || true
mkdir -p containers
mkdir -p images

$ make
gcc -std=c99 -Wall -Werror -O2 src/diyc.c -o diyc
gcc -std=c99 -Wall -Werror -O2 src/nsexec.c -o nsexec

$ docker pull debian
Using default tag: latest
latest: Pulling from library/debian
Digest: sha256:72f784399fd2719b4cb4e16ef8e369a39dc67f53d978cd3e2e7bf4e502c7b793
Status: Image is up to date for debian:latest

$ docker run -ti debian /bin/bash

$ docker ps
CONTAINER ID        IMAGE               COMMAND             CREATED             STATUS              PORTS               NAMES
2c924241399c        debian              "/bin/bash"         21 seconds ago      Up 20 seconds                           epic_leavitt

$ docker export 2c924241399c >! debian.tar

$ mkdir images/debian

$ tar -xf debian.tar -C images/debian/

$ sudo ./diyc -v -m 10 -i 172.16.0.30 my1 debian bash

> root@my1:/# exit
  exit

```
