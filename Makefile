SHELL = /bin/sh

BRIDGE := diyc0
ETH0 := $(shell ip -o -4 route show to default | awk '{print $$5}')

CC = gcc
CFLAGS = -std=c99 -Wall -Werror -O2
#CFLAGS = -std=c99 -Wall -Werror -O0 -ggdb



all: diyc nsexec

diyc: src/diyc.c
	$(CC) $(CFLAGS) src/diyc.c -o $@

nsexec: src/nsexec.c
	$(CC) $(CFLAGS) src/nsexec.c -o $@

.PHONY: clean, net-setup, net-clean, setup
clean:
	rm -rf nsexec diyc

setup: net-setup
	mkdir -p containers
	mkdir -p images

net-setup:
	sudo iptables -A FORWARD -i $(ETH0) -o veth -j ACCEPT || true
	sudo iptables -A FORWARD -o $(ETH0) -i veth -j ACCEPT || true
	sudo iptables -t nat -A POSTROUTING -s 172.16.0.0/16 -j MASQUERADE || true
	sudo ip link add name $(BRIDGE) type bridge || true
	sudo ip addr add dev $(BRIDGE) 172.16.0.1/24 || true
	sudo ip link set $(BRIDGE) up || true
	sudo iptables -A FORWARD -i $(ETH0) -o $(BRIDGE) -j ACCEPT || true
	sudo iptables -A FORWARD -o $(ETH0) -i $(BRIDGE) -j ACCEPT || true
	sudo iptables -A FORWARD -o $(BRIDGE) -i $(BRIDGE) -j ACCEPT || true

net-clean:
	sudo iptables -D FORWARD -i $(ETH0) -o veth -j ACCEPT || true
	sudo iptables -D FORWARD -o $(ETH0) -i veth -j ACCEPT || true
	sudo iptables -t nat -D POSTROUTING -s 172.16.0.0/16 -j MASQUERADE || true
	sudo iptables -D FORWARD -i $(ETH0) -o $(BRIDGE) -j ACCEPT || true
	sudo iptables -D FORWARD -o $(ETH0) -i $(BRIDGE) -j ACCEPT || true
	sudo iptables -D FORWARD -o $(BRIDGE) -i $(BRIDGE) -j ACCEPT || true
	sudo ip link delete $(BRIDGE) || true
