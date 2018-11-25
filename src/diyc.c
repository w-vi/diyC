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
#include <dirent.h>
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
    char **args; /* Container command and arguments */
    char path[PATH_MAX + 1]; /* Container fs directory  $(PWD)/containers/<id>*/
    char ip[IPLEN + 1]; /*IP address of the container */
    char image[IMAGELEN + 1]; /* Path of the conatiner image $(PWD)/images/<image> */
} container_t;

struct clone_stack {
    char stack[4096] __attribute__ ((aligned(16))); /* Stack for the clone call */
    char ptr[0]; /* This ppointer actually point to the top of the stack array. */
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

    if ((out = open(dst, O_RDWR | O_CREAT, 0666)) == -1)  {
    //if ((out = open(dst, O_RDWR | O_CREAT)) == -1)  {
        close(in);
        return -1;
    }

    fstat(in, &fileinfo);
    result = sendfile(out, in, &bytes, fileinfo.st_size);

    close(in);
    close(out);

    return result;
}


/* Wrapper for pivot root syscall.
 * see pivot_root(2)
 */
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

/* Change the root and prepare the filesystem for the container, in
 * this case we just copy resolv.conf from the host so that dns
 * resolving works in the container.
 */
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

    /* Change to new root so we can safely remove the old root*/
    chdir("/");

    if (copy_file("/.pivot_root/etc/resolv.conf", "/etc/resolv.conf") < 0) die("copy resolv.conf");
    if (copy_file("/.pivot_root/etc/nsswitch.conf", "/etc/nsswitch.conf") < 0) die("copy nsswitch.conf");

    /* Unmount the old root and remove it so it is not accessible from
     * the container */
    if (umount2("/.pivot_root", MNT_DETACH) < 0) die("error unmount pivot_root");

    if (rmdir("/.pivot_root") < 0) die("rmdir pivot_root");

    return 0;
}

/* Setup cgroups for memory limit.
 * Cgroups are accessed through he /sys/fs/cgroup/<subgroup>
 * filesystem so we just create a subdir in /sys/fs/cgroup/memory/
 * which gets then populated by the kernel so we then just write the
 * desired values.
 */
static int
cgroup_setup(pid_t pid, unsigned int limit)
{
    char cgroup_dir[PATH_MAX + 1];
    char cgroup_file[PATH_MAX + 1];
    unsigned long mem = limit * (1024 * 1024);

    LOG("HOST| Setting up cgroups with memory limit %d MB (%lu)", limit, mem);

    snprintf(cgroup_dir, PATH_MAX, "/sys/fs/cgroup/memory/%u", pid);
    if (mkdir(cgroup_dir, 0700) < 0 && errno != EEXIST) die("making cgroup");

    /* Maximum allowed memory for the container */
    snprintf(cgroup_file, PATH_MAX, "%s/memory.limit_in_bytes", cgroup_dir);

    FILE *fp = fopen(cgroup_file, "w+");
    if (NULL == fp) die("Could not set memory limit");
    fprintf(fp, "%lu\n", mem);
    fclose(fp);

    /* No swap */
    snprintf(cgroup_file, PATH_MAX, "%s/memory.memsw.limit_in_bytes", cgroup_dir);
    fp = fopen(cgroup_file, "w");
    if (NULL == fp) die("Could not set swap limit");
    fprintf(fp, "0\n");
    fclose(fp);

    /* Add the container pid to the group */
    snprintf(cgroup_file, PATH_MAX, "%s/cgroup.procs", cgroup_dir);
    fp = fopen(cgroup_file, "a");
    if (NULL == fp) die("Could not add proc to cgroup.procs");
    fprintf(fp, "%d\n", pid);
    fclose(fp);

    return 0;
}


/* Create the veth pair and bring it up.
 * Using the system() function and ip tool (iproute2 package) to avoid
 * the netlink code which would have been pretty complex and would
 * obscure the main parts.
 */
static int create_peer(char*id)
{
    char *set_int; char *set_int_up; char *add_to_bridge;

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

/* Move the veth1 device to the childs new netwrok namespace,
 * effectivelly connecting the parent's and child's namespaces. Again
 * using just system() to avoid netlink code.
 */
static int
network_setup(pid_t pid)
{
    char *set_pid_ns;

    asprintf(&set_pid_ns,"ip link set veth1 netns %d", pid);
    system(set_pid_ns);
    free(set_pid_ns);
    return 0;
}

/* Main container function which is responsible to set up the
 * environmnet for the main container process. This is the function
 * run by clone(2).
 */
static int
container_exec(void *arg)
{
    int err = 0;
    char ch;
    container_t *c = (container_t *)arg;
    char *ovfs_opts;
    char *upper;
    char *work;
    char *merged;


    close(c->pipe_fd[1]);    /* Close our descriptor for the write end
                                of the pipe so that we see EOF when
                                parent closes its descriptor */

    LOG("CONTAINER| Waiting for parent to finish setup");

    if (read(c->pipe_fd[0], &ch, 1) != 0) {
        die("Failure in child: read from pipe returned != 0\n");
    }

    /* remount / as private, on some systems / is shared */
    if (mount("/", "/", "none", MS_PRIVATE | MS_REC, NULL) < 0 ) {
        die("mount / private");
    }

    /* Create all the directories needed for overlayFS
     * Whats basically happening is:

       mount -t overlay overlay -o lowerdir=<image>,\
       upperdir=containers/<container>/upper,\
       workdir=containers/<container>/work \
       containers/<container>/merged
     */
    asprintf(&upper, "%s/upper", c->path);
    asprintf(&work, "%s/work", c->path);
    asprintf(&merged, "%s/merged", c->path);
    if (mkdir(c->path, 0700) < 0 && errno != EEXIST) die("container dir");
    if (mkdir(upper, 0700) < 0 && errno != EEXIST) die("container upper dir");
    if (mkdir(work, 0700) < 0 && errno != EEXIST) die("container work dir");
    if (mkdir(merged, 0700) < 0 && errno != EEXIST) die("container merged dir");

    asprintf(&ovfs_opts, "lowerdir=%s/images/%s,upperdir=%s,workdir=%s",cwd, c->image, upper, work);

    LOG("CONTAINER| overlayfs opts: %s", ovfs_opts);

    LOG("CONTAINER| root on host: %s", merged);

    if (mount("", merged, "overlay", MS_RELATIME, ovfs_opts) < 0) die("mount overlay");

    /* Unmount old proc as otherwise it'll be still showing all the host info. */
    if (umount2("/proc", MNT_DETACH) < 0) die("unmount proc");

    change_root(merged);

    LOG("CONTAINER| Root changed");

    free(ovfs_opts);
    free(upper);
    free(work);
    free(merged);

    /* Mount new /dev, here we can actually create just some subset of
     * devices, but for the sake of the simplicity just create a new
     * one.*/
    if (mount("devtmpfs", "/dev", "devtmpfs", MS_NOSUID | MS_RELATIME, NULL) < 0 ) {
        die("mount devtmpfs");
    }
    LOG("CONTAINER| /dev mounted");

    /* Mount new /proc so commands like ps show correct information */
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC | MS_RELATIME, NULL) < 0) die("mount proc");
    LOG("CONTAINER| /proc mounted");

    /* Setting env variables here just to make sure that the shell in
     * container works correctly, otherwise ther PATH and others ENV
     * variables are the same as from the parent process. Not really
     * making any effort here to clean it up, which we otherwise
     * should.*/
    setenv("PATH", "/bin:/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin", 1);
    unsetenv("LC_ALL");

    /* Set new system info */
    LOG("CONTAINER| Setting hostname %s and domain %s", c->id, domain);
    setdomainname(domain, strlen(domain));
    sethostname(c->id,strlen(c->id));

    /* If we have an IP address setup the network, assign IP and add
     * route to the gateway, bridge diyc0 which has by default
     * 172.16.0.1. Again using ip tool and system() to avoid netlink
     * code. */
    if (c->ip[0] != '\0') {
        char *ip_cmd;

        LOG("CONTAINER| Setting up network");

        system("ip link set veth1 up");
        asprintf(&ip_cmd, "ip addr add %s/24 dev veth1", c->ip);
        system(ip_cmd);
        free(ip_cmd);
        system("ip route add default via 172.16.0.1");
    }

    if (access(c->args[0], R_OK | X_OK) != 0) {
        printf("access %s failed %s:\n", c->args[0], strerror(errno));
    }

    /* Ready to execute the container command.*/
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
    char cgroup_dir[PATH_MAX + 1];

    verbose = 0;
    memset(c.ip, 0, IPLEN);

    static const struct option long_opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "ip", required_argument, NULL, 'i' },
        { "mem", required_argument, NULL, 'm' },
        { "verbose", no_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    /* The + sign is for getopt to leave the extra arguments alone as
     * they belong to the executed command. see man 3 getopt_long*/
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

    /* Create a pipe for child parent synchronization some stuff needs
     * to be done by parent (networking, cgroups) before child can
     * proceed */
    if (pipe(c.pipe_fd) == -1) die("pipe");

    if (NULL == getcwd(cwd, PATH_MAX)) die("getcwd()");

    /* Directory where the container filesystem will reside */
    snprintf(c.path, PATH_MAX, "%s/containers/%s", cwd, c.id);

    LOG("HOST| Starting container %s using image %s", c.id, c.image);

    /* If the IP address is provided, we want to run in new network
     * namespace and create the veth pair.  */
    if (c.ip[0] != '\0')  {
        flags |=  CLONE_NEWNET;
        create_peer(c.id);
    }

    /* Execute the child see clone(2) for more details, but it's
     * basically fork with namespaces the container is spawned in
     * container_exec function.*/
    pid = clone(container_exec, stack.ptr, flags, &c);

    if (pid < 0) die("SYSCALL clone failed.");

    LOG("HOST| Cloned setting up environment");

    /*If we have new network namespace add the veth1 to child's
     * namespace.*/
    if (c.ip[0] != '\0') {
        LOG("HOST| Network setup");
        network_setup(pid);
    }

    /* If limiting the memory, create the cgroup group and add the child. */
    if (memory) cgroup_setup(pid, memory);

    /* Close the write end of the pipe, to signal to the child that we
       are ready. */
    close(c.pipe_fd[1]);

    /* Now we wait for the child/container to finish. */
    LOG("HOST| Waiting for container to finish.");
    waitpid(pid, NULL, 0);

    /* We can remove the cgroup if it was created. */
    if (memory) {
        snprintf(cgroup_dir, PATH_MAX, "/sys/fs/cgroup/memory/%d", pid);
        rmdir(cgroup_dir);
    }

    LOG("HOST| Container exited");
    return 0;
}
