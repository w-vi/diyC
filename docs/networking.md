# Containers and networking

Containers without network access are in many cases pretty useless. If
the container should not share network resources with the host that is
has it's own network namespace we need to connect it somehow because
by default new network namespace has only loop device, you can add the
network device to a namespace but it can reside only in one of them.
Considering dozens of containers on a single host we are not going to
have enough physical interfaces.


## Virtual Ethernet devices

In Linux you can create virtual Ethernet devices, they come in pairs,
a so called veth pair, which you can think of as two Ethernet cards
connected by a cable. using ip tool you can easily assign one of the
pair to the container's namespace and the other somewhere
else. Ideally
to [Linux bridge](https://wiki.linuxfoundation.org/networking/bridge)
or [Open vSwitch](http://openvswitch.org/) so we can create virtual
networks.


## Connecting containers using bridge

Using the Linux bridge is the easiest, we create
the bridge which we call here `diyc0` and assign it IPv4 address `172.16.0.1/24`.

```bash
# Create the bridge
$ ip link add name diyc0 type bridge
# Assign it IPv4
$ ip addr add dev diyc0 172.16.0.1/24
$ ip link set diyc0 up
```

Before we run the container we connect the created `vethXYZ` device to the
bridge that's the host half of the pair. The other one is later added
to the network namespace of the container, effectively connecting the
two namespace.

```bash
# Create the veth pair
$ ip link add vethXYZ type veth peer name veth1
# Connect the host half to bridge
$ ip link set vethXYZ master diyc0
# Add the veth1 to the container's network namespace
# <PID> is pid of the container process
$ ip link set veth1 netns <PID>
```

*Below: Connecting containers using veth pairs and Linux bridge*
![veth Bridge](img/veth-bridge.png)


!!! seealso "Code"
    Code in diyC uses `system()` function to shell out so you can
    easily replicate it on the command line.
    [diyc.c:265-302](https://github.com/w-vi/diyC/blob/master/src/diyc.c#L265-L302)


## iptables

Once the containers are hooked up using veth pairs and a bridge it
comes to iptables to do NAT and routing to allow containers to connect
to the outer world. See the `net-setup` code in the
[Makefile](https://github.com/w-vi/diyC/blob/master/Makefile#L28-L37)
to inspect the exact rules. Basically setup NAT and forwarding on the container
network in our case `172.16.0.0/16`.


## More to read

- [Linux Switching – Interconnecting Namespaces](http://www.opencloudblog.com/?p=66)
- [Linux Network Namespaces](http://www.opencloudblog.com/?p=42)
