#include "../include/main.h"
#include "../include/network.h"
#include "../include/utils.h"
#include "../include/worker.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void parse_arguments(server_context *ctx);
static void validate_arguments(server_context *ctx);
static void prefork_workers(server_context *ctx);
static void monitor_workers(server_context *ctx);
static void spawn_worker(server_context *ctx, int slot);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t monitor_exit_flag = 0;

static void monitor_signal_handler(int sig)
{
    (void)sig;
    monitor_exit_flag = 1;
}

static server_context init_context(void)
{
    server_context ctx = {0};
    ctx.exit_code      = EXIT_SUCCESS;
    ctx.listen_fd      = -1;
    ctx.num_workers    = DEFAULT_WORKERS;
    ctx.library_path   = "./libhandler.so";
    ctx.db_path        = "./posts.db";
    return ctx;
}

int main(int argc, char **argv)
{
    server_context ctx = init_context();
    ctx.argc           = argc;
    ctx.argv           = argv;

    parse_arguments(&ctx);
    validate_arguments(&ctx);
    init_server_socket(&ctx);
    prefork_workers(&ctx);
    monitor_workers(&ctx);
    cleanup_server(&ctx);

    return ctx.exit_code;
}

/* Argument parsing */

static void parse_arguments(server_context *ctx)
{
    int         opt;
    const char *optstring = ":p:f:i:w:l:d:h";
    opterr                = 0;

    while((opt = getopt(ctx->argc, ctx->argv, optstring)) != -1)
    {
        if(opt != ':' && opt != '?' && opt != 'h' && optarg != NULL && optarg[0] == '-')
        {
            fprintf(stderr, "Error: Option '-%c' requires an argument.\n", opt);
            ctx->exit_code = EXIT_FAILURE;
            print_usage(ctx);
        }

        switch(opt)
        {
            case 'p':
                ctx->user_entered_port = optarg;
                break;
            case 'f':
            {
                if(optarg == NULL)
                {
                    fprintf(stderr, "Error: Option '-f' requires an argument.\n");
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                const char *real = realpath(optarg, NULL);
                if(real == NULL)
                {
                    fprintf(stderr, "Error: Failed getting real path of root directory \"%s\".\n", optarg);
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                ctx->root_directory = real;
                break;
            }

            case 'i':
                ctx->ip_address = optarg;
                break;
            case 'w':
            {
                if(optarg == NULL)
                {
                    fprintf(stderr, "Error: Option '-w' requires an argument.\n");
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                char *end;
                long  n = strtol(optarg, &end, BASE_TEN);
                if(*end != '\0' || n < 1)
                {
                    fprintf(stderr, "Error: Invalid worker count '%s'.\n", optarg);
                    ctx->exit_code = EXIT_FAILURE;
                    print_usage(ctx);
                }
                ctx->num_workers = (int)n;
                break;
            }
            case 'l':
                ctx->library_path = optarg;
                break;
            case 'd':
                ctx->db_path = optarg;
                break;
            case 'h':
                ctx->exit_code = EXIT_SUCCESS;
                print_usage(ctx);
            case ':':
                fprintf(stderr, "Error: Option '-%c' requires an argument.\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
            case '?':
                fprintf(stderr, "Error: Unknown option '-%c'.\n", optopt);
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
            default:
                ctx->exit_code = EXIT_FAILURE;
                print_usage(ctx);
        }
    }
}

static void validate_arguments(server_context *ctx)
{
    if(ctx->user_entered_port == NULL)
    {
        fputs("Error: Port number is required (-p <port>).\n", stderr);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    if(ctx->root_directory == NULL)
    {
        fputs("Error: Root directory is required (-f <dir>).\n", stderr);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }

    char *endptr;
    errno           = 0;
    unsigned long p = strtoul(ctx->user_entered_port, &endptr, PORT_INPUT_BASE);
    if(errno != 0 || *endptr != '\0' || p > MAX_PORT_NUMBER)
    {
        fprintf(stderr, "Error: Invalid port number '%s'.\n", ctx->user_entered_port);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }
    ctx->port_number = (uint16_t)p;

    if(ctx->ip_address == NULL)
    {
        ctx->ip_address = "0.0.0.0";
    }

    if(convert_address(ctx) == -1)
    {
        fprintf(stderr, "Error: '%s' is not a valid IP address.\n", ctx->ip_address);
        ctx->exit_code = EXIT_FAILURE;
        print_usage(ctx);
    }
}

/* Worker management */

static void spawn_worker(server_context *ctx, int slot)
{
    pid_t pid = fork();
    if(pid < 0)
    {
        perror("fork");
        return;
    }
    if(pid == 0)
    {
        run_worker(ctx);
        exit(EXIT_SUCCESS);
    }
    ctx->worker_pids[slot] = pid;
    printf("[monitor] spawned worker pid=%d in slot %d\n", pid, slot);
}

static void prefork_workers(server_context *ctx)
{
    ctx->worker_pids = calloc((size_t)ctx->num_workers, sizeof(pid_t));
    if(ctx->worker_pids == NULL)
    {
        perror("calloc worker_pids");
        ctx->exit_code = EXIT_FAILURE;
        cleanup_server(ctx);
        exit(EXIT_FAILURE);
    }

    for(int i = 0; i < ctx->num_workers; i++)
    {
        spawn_worker(ctx, i);
    }
}

static void monitor_workers(server_context *ctx)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = monitor_signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("[monitor] running with %d workers\n", ctx->num_workers);

    while(!monitor_exit_flag)
    {
        int   status;
        pid_t dead = waitpid(-1, &status, 0);

        if(dead == -1)
        {
            if(errno == EINTR)
            {
                /* Signal received — loop condition will catch it */
                continue;
            }
            break;
        }

        /* Find which slot died and restart it */
        for(int i = 0; i < ctx->num_workers; i++)
        {
            if(ctx->worker_pids[i] == dead)
            {
                if(!monitor_exit_flag)
                {
                    printf("[monitor] worker pid=%d (slot %d) exited, restarting\n", dead, i);
                    spawn_worker(ctx, i);
                }
                break;
            }
        }
    }

    /* Shutdown — signal all workers and wait for them */
    printf("[monitor] shutting down workers\n");
    for(int i = 0; i < ctx->num_workers; i++)
    {
        if(ctx->worker_pids[i] > 0)
        {
            kill(ctx->worker_pids[i], SIGTERM);
        }
    }
    while(waitpid(-1, NULL, 0) > 0)
    {
    }
    printf("[monitor] all workers exited\n");
}

/* Usage/Quit*/

__attribute__((noreturn)) void print_usage(const server_context *ctx)
{
    fprintf(stderr, "Usage: %s -p <port> -f <root_dir> [-i <ip>] [-w <workers>] [-l <libpath>] [-d <dbpath>] [-h]\n", ctx->argv[0]);
    fputs("\nOptions:\n", stderr);
    fputs("  -p <port>      Port to listen on (required)\n", stderr);
    fputs("  -f <path>      Document root directory (required)\n", stderr);
    fputs("  -i <ip>        IP address to bind (default: 0.0.0.0)\n", stderr);
    fputs("  -w <n>         Number of worker processes (default: 5)\n", stderr);
    fputs("  -l <path>      Path to handler shared library (default: ./libhandler.so)\n", stderr);
    fputs("  -d <path>      Path to ndbm database file (default: ./posts.db)\n", stderr);
    fputs("  -h             Show this help and exit\n", stderr);
    quit(ctx);
}

__attribute__((noreturn)) void quit(const server_context *ctx)
{
    if(ctx->exit_message != NULL)
    {
        fputs(ctx->exit_message, stderr);
    }
    exit(ctx->exit_code);
}