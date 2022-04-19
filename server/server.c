#include <stdio.h>
#include <stdlib.h>
#include <protocol.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <signal.h>

#include "server_conf.h"
#include "threadpool.h"
#include "medialib.h"
#include "tokenbucket.h"
#include "channel.h"

#include "list.h"

int serversd;
ThreadPool_t *pool;
struct sockaddr_in sndaddr;

server_conf_t server_conf = {
    .rcvport = DEFAULT_RECVPORT,
    .media_dir = DEFAULT_MEDIADIR,
    .runmode = RUN_FOREGROUND,
    .ifname = DEFAULT_IF,
    .mgroup = DEFAULT_MGROUP};

static mlib_listdesc_t *list;

static void daemon_exit(int s) // 信号捕捉函数，用于推出前清理
{
    threadpool_destroy(pool);
    mlib_freechnlist(list);
    mlib_freechncontext();
    close(serversd);
    exit(EXIT_SUCCESS);
}

static int socket_init()
{
    int ret;
    struct ip_mreqn mreq;

    serversd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serversd < 0)
    {
        fprintf(stderr, "socket() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    inet_pton(AF_INET, server_conf.mgroup, &mreq.imr_multiaddr);
    inet_pton(AF_INET, "0.0.0.0", &mreq.imr_address);
    mreq.imr_ifindex = if_nametoindex(server_conf.ifname);
    ret = setsockopt(serversd, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq));
    if (ret < 0)
    {
        fprintf(stderr, "setsockopt() : %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    sndaddr.sin_family = AF_INET;
    sndaddr.sin_port = htons(atoi(server_conf.rcvport));
    inet_pton(AF_INET, server_conf.mgroup, &sndaddr.sin_addr);

    return 0;
}

int main(int argc, char **argv)
{
    int i, ret;
    int list_size;
    struct sigaction action;

    action.sa_flags = 0;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    // sigaddset(&action.sa_mask, SIGQUIT);
    sigaddset(&action.sa_mask, SIGTSTP);
    action.sa_handler = daemon_exit;
    sigaction(SIGINT, &action, NULL); // 注册信号捕捉函数
    // sigaction(SIGQUIT, &action, NULL);
    sigaction(SIGTSTP, &action, NULL);

    socket_init();

    pool = threadpool_create(5, 20, 20);
    if (pool == NULL)
    {
        fprintf(stderr, "threadpool_create() : failed ...\n");
        exit(EXIT_FAILURE);
    }

    ret = mlib_getchnlist(&list, &list_size);
    if (ret < 0)
    {
        fprintf(stderr, "mlib_getchnlist() : failed ...\n");
        exit(EXIT_FAILURE);
    }

    ret = thr_list_create(list, list_size);
    if (ret < 0)
    {
        fprintf(stderr, "thr_list_create() : failed ...\n");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < list_size; i++)
    {
        ret = thr_channel_create(list[i].chnid);
        if (ret < 0)
        {
            fprintf(stderr, "thr_channel_create() : failed ...\n");
            exit(EXIT_FAILURE);
        }
    }

    while (1)
        pause();
    exit(EXIT_SUCCESS);
}