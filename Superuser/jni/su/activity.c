/*
** Copyright 2010, Adam Shanks (@ChainsDD)
** Copyright 2008, Zinx Verituse (@zinxv)
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <paths.h>
#include <stdio.h>
#include <stdarg.h>

#include "su.h"

static void kill_child(pid_t pid) {
    LOGD("killing child %d", pid);
    if (pid) {
        sigset_t set, old;

        sigemptyset(&set);
        sigaddset(&set, SIGCHLD);
        if (sigprocmask(SIG_BLOCK, &set, &old)) {
            PLOGE("sigprocmask(SIG_BLOCK)");
            return;
        }
        if (kill(pid, SIGKILL))
            PLOGE("kill (%d)", pid);
        else if (sigsuspend(&old) && errno != EINTR)
            PLOGE("sigsuspend");
        if (sigprocmask(SIG_SETMASK, &old, NULL))
            PLOGE("sigprocmask(SIG_BLOCK)");
    }
}

// TODO: leverage this with exec_log?
static int silent_run(char* command) {
    char *args[] = { "sh", "-c", command, NULL, };
    set_identity(0);
    pid_t pid;
    pid = fork();
    /* Parent */
    if (pid < 0) {
        PLOGE("fork");
        return -1;
    }
    else if (pid > 0) {
        return 0;
    }
    int zero = open("/dev/zero", O_RDONLY | O_CLOEXEC);
    dup2(zero, 0);
    int null = open("/dev/null", O_WRONLY | O_CLOEXEC);
    dup2(null, 1);
    dup2(null, 2);
    execv(_PATH_BSHELL, args);
    PLOGE("exec am");
    _exit(EXIT_FAILURE);
    return -1;
}

int send_result(struct su_context *ctx, allow_t allow) {
    return 0;
}

int send_request(struct su_context *ctx) {
    pid_t pid;

    // if su is operating in MULTIUSER_MODEL_OWNER,
    // and the user requestor is not the owner,
    // the owner needs to be notified of the request.
    // so there will be two activities shown.
    int needs_owner_login_prompt = 0;
    char user[64];
    if (ctx->user.multiuser_mode == MULTIUSER_MODE_OWNER_ONLY) {
        snprintf(user, sizeof(user), "");
    }
    else if (ctx->user.multiuser_mode == MULTIUSER_MODE_OWNER) {
        if (0 != ctx->user.android_user_id) {
            needs_owner_login_prompt = 1;
        }
        snprintf(user, sizeof(user), "--user 0");
    }
    else if (ctx->user.multiuser_mode == MULTIUSER_MODE_USER) {
        snprintf(user, sizeof(user), "--user %d", ctx->user.android_user_id);
    }
    else {
        // unknown mode?
        return -1;
    }

    pid = fork();
    if (!pid) {
        if (needs_owner_login_prompt) {
            // in multiuser mode, the owner gets the su prompt
            pid = fork();
            if (!pid) {
                char notify_command[ARG_MAX];

                // start the activity that confirms the request
                snprintf(notify_command, sizeof(notify_command),
                    "exec /system/bin/am " ACTION_NOTIFY " --ei caller_uid %d --user %d",
                    ctx->from.uid, ctx->user.android_user_id);

                return silent_run(notify_command);
            }
        }
        
        char request_command[ARG_MAX];

        // start the activity that confirms the request
        snprintf(request_command, sizeof(request_command),
            "exec /system/bin/am " ACTION_REQUEST " --es socket '%s' %s",
            ctx->sock_path, user);

        return silent_run(request_command);
    }
    
    /* Parent */
    if (pid < 0) {
        PLOGE("fork");
        return -1;
    }
    return 0;
}
