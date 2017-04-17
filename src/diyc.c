/* diyc.c

   diyc - naive linux container runtime implementation
   Copyright (C) 2017  Vilibald Wanča

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <fts.h>
#include <getopt.h>

#ifndef FALSE
# define FALSE 0
#endif

#ifndef TRUE
# define TRUE 1
#endif

#define IDLEN 16
#define BRIDGE "diyc0"
#define IPLEN 16
#define IMAGELEN 127

const char *domain = "diyc";

/* A simple error-handling function: print an error message based
   on the value in 'errno' and terminate the calling process */
#define die(msg)                            \
do {                                        \
    perror(msg);                            \
    exit(EXIT_FAILURE);                     \
} while (0)

/* Quick logging macro to allow logging iff verbose output */
#define LOG(x...) { if (verbose) { fprintf(stdout, x); fprintf(stdout, "\n"); } }

static int verbose;
static char cwd[PATH_MAX + 1];

/* Container representation */
typedef struct container {
    char id[IDLEN]; /* Name of the container */
    int pipe_fd[2];  /* Pipe used to synchronize parent and child */
    char **args;
    char root[PATH_MAX + 1];
    char path[PATH_MAX + 1]; /*container image directory*/
    char ip[IPLEN + 1];
    char image[IMAGELEN + 1];
} container_t;

struct clone_stack {
    char stack[4096] __attribute__ ((aligned(16))); /* Stack for the clone call */
    char ptr[0];
};

static void
usage(char *name)
{
    printf("Execute a naive container environment.\n");
    printf("See https://github.com/w-vi/diyc for more information.\n\n");
    printf("Usage: %s [hv][-m NUMBER] [-ip IPV4 ADDRESS] <NAME> <IMAGE> <CMD>\n\n", name);

    printf("\
    -h, --help           print this help\n\n");
    printf("\
    -i, --ip             ip address of the container, if not set then host \n\
                         network is used. It must be in the 172.16.0/16 network \n\
                         as the bridge diyc0 is 172.16.0.1\n\n");
    printf("\
    -m, --mem            maximum size of the memory in MB allowed for the container\n\
                         by default there no explicit limit defined.\n\n");

    printf("\
    -v, --verbose        more verbose output\n\n");

    printf("\
    <NAME>               name of the container, needs to be unique\n\n");

    printf("\
    <IMAGE>              image to be used for the container, must be a directory name\n\
                         under the images directory\n\n");

    printf("\
    <CMD>                command to be executed inside the container\n");

    exit(EXIT_FAILURE);
}


static int
copy_file(char *src, char *dst)
{
    int in, out;
    off_t bytes = 0;
    struct stat fileinfo = {0};
    int result = 0;

    if ((in = open(src, O_RDONLY)) == -1) {
        return -1;
    }

    if ((out = open(dst, O_RDWR | O_CREAT)) == -1)  {
        close(in);
        return -1;
    }

    fstat(in, &fileinfo);
    result = sendfile(out, in, &bytes, fileinfo.st_size);

    close(in);
    close(out);

    return result;
}


/* Wrapper for pivot root syscall */
static int
pivot_root(char *new, char *old)
{
    LOG("CONTAINER| Setting up pivot_root\nCONTAINER| old: %s\nCONTAINER| new: %s", old, new);

    if (mount(new, new, "bind", MS_BIND | MS_REC, "") < 0) {
        die("pivot_root mount bind");
    }

    if (mkdir(old, 0700) < 0 && errno != EEXIST) {
        die("pivot_root %mkdir old");
    }

    return syscall(SYS_pivot_root, new, old);
}

static int
change_root(char *path)
{
    char oldpath[PATH_MAX + 1];
    char oldroot[PATH_MAX + 1];
    char newroot[PATH_MAX + 1];

    realpath(path, newroot);
    snprintf(oldpath, PATH_MAX, "%s/.pivot_root", newroot);
    realpath(oldpath, oldroot);

    LOG("CONTAINER| Calling pivot root");

    if (pivot_root(newroot, oldroot) < 0) die("pivot_root");

    chdir("/");

    if (copy_file("/.pivot_root/etc/resolv.conf", "/etc/resolv.conf") < 0) die("copy resolv.conf");

    if (umount2("/.pivot_root", MNT_DETACH) < 0) die("error unmount pivot_root");

    if (rmdir("/.pivot_root") < 0) die("rmdir pivot_root");

    return 0;
}

static int
cgroup_setup(pid_t pid, unsigned int limit)
{
    unsigned long mem = limit * (1024 * 1024);

    LOG("HOST| Setting up cgroups with memory limit %d MB (%lu)", limit, mem);

    if (mkdir("/sys/fs/cgroup/memory/diyc", 0700) < 0 && errno != EEXIST) die("making cgroup");

    FILE *fp = fopen("/sys/fs/cgroup/memory/diyc/memory.limit_in_bytes", "w+");
    if (NULL == fp) die("Could not set memory limit");
    fprintf(fp, "%lu", mem);
    fclose(fp);

    fp = fopen("/sys/fs/cgroup/memory/diyc/memory.memsw.limit_in_bytes", "w");
    if (NULL == fp) die("Could not set swap limit");
    fprintf(fp, "0");
    fclose(fp);

    fp = fopen("/sys/fs/cgroup/memory/diyc/cgroup.procs", "a");
    if (NULL == fp) die("Could not add proc to cgroup.procs");
    fprintf(fp, "%d\n", pid);
    fclose(fp);

    return 0;
}

static int
create_peer(char *id)
{
    char *set_int;
    char *set_int_up;
    char *add_to_bridge;

    asprintf(&set_int, "ip link add veth%s type veth peer name veth1", id);
    system(set_int);
    free(set_int);

    asprintf(&set_int_up, "ip link set veth%s up", id);
    system(set_int_up);
    free(set_int_up);

    asprintf(&add_to_bridge, "ip link set veth%s master %s", id, BRIDGE);
    system(add_to_bridge);
    free(add_to_bridge);

    return 0;
}

static int
network_setup(pid_t pid)
{
    char *set_pid_ns;

    asprintf(&set_pid_ns,"ip link set veth1 netns %d", pid);
    system(set_pid_ns);
    free(set_pid_ns);
    return 0;
}

static int
container_exec(void *arg)
{
    int err = 0;
    char ch;
    container_t *c = (container_t *)arg;
    char aufs_opts[1024];

    close(c->pipe_fd[1]);    /* Close our descriptor for the write end
                                of the pipe so that we see EOF when
                                parent closes its descriptor */

    LOG("CONTAINER| Waiting for parent to finish setup");

    if (read(c->pipe_fd[0], &ch, 1) != 0) {
        die("Failure in child: read from pipe returned != 0\n");
    }

    /* if (unshare(CLONE_NEWNS) < 0) die("unshare issue NEWNS"); */

    if (mkdir(c->path, 0700) < 0 && errno != EEXIST) die("container dir");

    if (mount("/", "/", "none", MS_PRIVATE | MS_REC, NULL) < 0 ) {
        die("mount private");
    }

    LOG("CONTAINER| root on host: %s", c->root);

    snprintf(aufs_opts, 1023, "br=%s=rw:/home/wvi/src/diyc/images/%s=ro none", c->path, c->image);
    LOG("CONTAINER| aufs opts: %s", aufs_opts);

    if (mount("", c->root, "aufs", MS_RELATIME, aufs_opts) < 0) die("mount aufs");

    /* if (unshare(CLONE_NEWCGROUP) < 0) die("unshare issue cgroup"); */

    if (umount2("/proc", MNT_DETACH) < 0) die("unmount proc");

    change_root(c->root);

    LOG("CONTAINER| Root changed");

    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_RELATIME, NULL) < 0 ) {
        die("mount devtmpfs");
    }
    LOG("CONTAINER| /dev mounted");

    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL) < 0) die("mount proc");
    LOG("CONTAINER| /proc mounted");

    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin", 1);
    unsetenv("LC_ALL");

    // set new system info
    LOG("CONTAINER| Setting hostname %s and domain %s", c->id, domain);
    setdomainname(domain, strlen(domain));
    sethostname(c->id,strlen(c->id));

    if (c->ip[0] != '\0') {
        char *ip_cmd;

        LOG("CONTAINER| Setting up network");

        system("ip link set veth1 up");
        asprintf(&ip_cmd, "ip addr add %s/24 dev veth1", c->ip);
        system(ip_cmd);
        free(ip_cmd);
        system("ip route add default via 172.16.0.1");
    }

    LOG("CONTAINER| Executing command %s", c->args[0]);

    err = execvp(c->args[0], c->args);
    if (0 != err) {
        printf("execvp error: %s\n", strerror(errno));
    }

    return err;
}


int
main(int argc, char *argv[])
{
    int opt;
    int long_index = 0;
    extern char *optarg;
    unsigned int memory = 0;
    int flags =  SIGCHLD | CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWUTS;
    container_t c;
    pid_t pid = -1;
    struct clone_stack stack;

    verbose = 0;
    memset(c.ip, 0, IPLEN);
    snprintf(c.root, PATH_MAX, "/tmp/diyc-XXXXXX");

    static const struct option long_opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "ip", required_argument, NULL, 'i' },
        { "mem", required_argument, NULL, 'm' },
        { "verbose", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    /* The + sign is for getopt to leave the extra arguments alone as
       they belong to the executed command. see man 3 getopt_long*/
    while ((opt = getopt_long(argc, argv,"+hvi:m:",
                              long_opts, &long_index )) != -1) {
        switch (opt) {
        case 'i': strncpy(c.ip, optarg, IPLEN); break;
        case 'm': memory = atoi(optarg); break;
        case 'v': verbose = TRUE; break;
        case 'h': usage(argv[0]); break;
        case '?':
        default: usage(argv[0]);
        }
    }

    if ((argc - optind) < 3) {
        printf("%d", (argc - optind));
        fprintf(stderr, "Not enough arguments\n");
        usage(argv[0]);
    }

    strncpy(c.id, argv[optind++], IDLEN);
    strncpy(c.image, argv[optind++], IMAGELEN);
    c.args = &argv[optind];

    if (pipe(c.pipe_fd) == -1) die("pipe");

    if (NULL == getcwd(cwd, PATH_MAX)) die("getcwd()");

    snprintf(c.path, PATH_MAX, "%s/containers/%s", cwd, c.id);

    LOG("HOST| Starting container %s using image %s", c.id, c.image);

    if (c.ip[0] != '\0')  {
        flags |=  CLONE_NEWNET;
        create_peer(c.id);
    }

    if (memory) flags |= CLONE_NEWCGROUP;

    if (NULL == mkdtemp(c.root)) die("Container root dir failed.");

    pid = clone(container_exec, stack.ptr, flags, &c);

    if (pid < 0) die("SYSCALL clone failed.");

    LOG("HOST| Cloned setting up environment");

    if (c.ip[0] != '\0') {
        LOG("HOST| Network setup");
        network_setup(pid);
    }

    if (memory) cgroup_setup(pid, memory);

    /* Close the write end of the pipe, to signal to the child that we
       are ready */
    close(c.pipe_fd[1]);

    LOG("HOST| Waiting for container to finish.")
        waitpid(pid, NULL, 0);

    if (memory) rmdir("/sys/fs/cgroup/memory/diyc");

    rmdir(c.root);

    LOG("HOST| Container exited");
    return 0;
}

