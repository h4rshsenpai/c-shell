#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <limits.h>     // defines PATH_MAX = 4096, HOST_NAME_MAX = 64, LOGIN_NAME_MAX = 256 

static char SHELL_HOME_DIR[PATH_MAX] = {0};
static char SHELL_HOST_NAME[HOST_NAME_MAX+1];     // POSIX standardizes HOST_NAME_MAX to exclude null terminator
static char SHELL_USER_NAME[LOGIN_NAME_MAX];

void shell_init(void) {

    // set hostname
    // set to "?" if gethostname() fails 
    if(gethostname(SHELL_HOST_NAME, sizeof(SHELL_HOST_NAME)) == -1) { 
        snprintf(SHELL_HOST_NAME, sizeof(SHELL_HOST_NAME), "?");
    }
    else { SHELL_HOST_NAME[HOST_NAME_MAX] = '\0'; }
    
    //set username
    // set to ? if passwd database entry does not exit
    uid_t uid = getuid();  
    struct passwd* pw = getpwuid(uid);

    if (pw && pw->pw_name) {
        snprintf(SHELL_USER_NAME, sizeof(SHELL_USER_NAME), "%s", pw->pw_name);
    } else { SHELL_USER_NAME[0]='?'; SHELL_USER_NAME[1]='\0'; }

    // set home directory for shell
    if(!getcwd(SHELL_HOME_DIR, sizeof(SHELL_HOME_DIR))) {
        // use passwd dir if getcwd() fails
        uid_t id = getuid();
        struct passwd* pw = getpwuid(id);
        
        if (pw && pw->pw_dir) {   
            strncpy(SHELL_HOME_DIR, pw->pw_dir, sizeof(SHELL_HOME_DIR)-1);
            SHELL_HOME_DIR[sizeof(SHELL_HOME_DIR)-1] = '\0';

        } else {
            // getcwd() failed, no entry in passwd database either
            // set SHELL_HOME to "/"
            SHELL_HOME_DIR[0] = '/'; SHELL_HOME_DIR[1] = '\0';
        }
    }
}

const char* shell_get_home(void) {    
    // if init_home() never runs, set home to NULL
    return SHELL_HOME_DIR[0]? SHELL_HOME_DIR : NULL;
}

void get_cwd_relative(char* cwdir, size_t size) {

    char cwd[PATH_MAX];
    char cwd_rel[PATH_MAX];

    if (!getcwd(cwd, size)) {
        snprintf(cwdir, size, "?");
        return;
    }
    
    // resolve cwdir relative to home
    const char* home = shell_get_home();
    // are first strlen(home) letters of cwdir same as home?
    if (home && !strncmp(home, cwd, strlen(home))) { 
        
        // is cwd == home?
        if (cwd[strlen(home)] == '\0') {
            snprintf(cwdir, size, "~");
            return;
        }
        // is cwd a subdirectory of home?
        if (cwd[strlen(home)] == '/') {
            // remove home prefix from cwd, write remaining into cwd_rel
            snprintf(cwd_rel, sizeof(cwd_rel), "%s", cwd + strlen(home));             
            snprintf(cwdir, size, "~%s", cwd_rel);
            return;
        }
    }
    // if user does cd .. from home 
    snprintf(cwdir, size, cwd);
    return;
}

void print_prompt(void) {
   
    char* hostname = SHELL_HOST_NAME;
    char* username = SHELL_USER_NAME;
   
    char cwdir[PATH_MAX];
    if (!getcwd(cwdir, sizeof(cwdir))) {
        snprintf(cwdir, sizeof(cwdir), "/");
    }
    // resolve absolute path 
    get_cwd_relative(cwdir, sizeof(cwdir));

    fprintf(stdout, "<%s@%s:%s> ", username, hostname, cwdir);
    fflush(stdout);
}
