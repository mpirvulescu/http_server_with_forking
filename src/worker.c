#include "../include/worker.h"
#include "../include/handler.h"
#include "../include/network.h"
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef void (*handler_fn)(int, const char *, const char *);

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t worker_exit_flag = 0;

static void worker_signal_handler(int sig)
{
    (void)sig;
    worker_exit_flag = 1;
}

static void *load_library(const char *path)
{
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if(handle == NULL)
    {
        fprintf(stderr, "[worker %d] dlopen failed: %s\n", getpid(), dlerror());
    }
    return handle;
}

static handler_fn get_handler_fn(void *handle)
{
    handler_fn fn;
    *(void **)(&fn) = dlsym(handle, "handle_request");
    if(fn == NULL)
    {
        fprintf(stderr, "[worker %d] dlsym failed: %s\n", getpid(), dlerror());
    }
    return fn;
}

static time_t get_mtime(const char *path)
{
    struct stat st;
    if(stat(path, &st) != 0)
    {
        return (time_t)-1;
    }
    return st.st_mtime;
}

void run_worker(server_context *ctx)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = worker_signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    void      *lib_handle = load_library(ctx->library_path);
    handler_fn handler    = lib_handle ? get_handler_fn(lib_handle) : NULL;
    time_t     lib_mtime  = get_mtime(ctx->library_path);

    if(lib_handle == NULL || handler == NULL)
    {
        fprintf(stderr, "[worker %d] failed to load handler, exiting\n", getpid());
        exit(EXIT_FAILURE);
    }

    printf("[worker %d] ready\n", getpid());

    while(!worker_exit_flag)
    {
        time_t current_mtime = get_mtime(ctx->library_path);
        if(current_mtime != (time_t)-1 && current_mtime != lib_mtime)
        {
            printf("[worker %d] library changed, reloading\n", getpid());
            dlclose(lib_handle);
            lib_handle = load_library(ctx->library_path);
            if(lib_handle != NULL)
            {
                handler_fn new_fn = get_handler_fn(lib_handle);
                if(new_fn != NULL)
                {
                    handler   = new_fn;
                    lib_mtime = current_mtime;
                    printf("[worker %d] library reloaded successfully\n", getpid());
                }
                else
                {
                    fprintf(stderr, "[worker %d] dlsym after reload failed, exiting\n", getpid());
                    dlclose(lib_handle);
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                fprintf(stderr, "[worker %d] dlopen after reload failed, exiting\n", getpid());
                exit(EXIT_FAILURE);
            }
        }

        char host[NI_MAXHOST];
        char serv[NI_MAXSERV];
        int  client_fd = accept_client(ctx, host, serv);

        if(client_fd == -1)
        {
            continue;
        }

        printf("Accepted new connection from %s:%s\n", host, serv);

        handler(client_fd, ctx->root_directory, ctx->db_path);

        close_client(client_fd);
    }

    printf("[worker %d] exiting cleanly\n", getpid());
    if(lib_handle)
    {
        dlclose(lib_handle);
    }
    exit(EXIT_SUCCESS);
}