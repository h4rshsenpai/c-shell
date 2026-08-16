#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pwd.h>
#include <limits.h>     // defines PATH_MAX = 4096, HOST_NAME_MAX = 64, LOGIN_NAME_MAX = 256 

static char SHELL_HOME[PATH_MAX] = {0};


const char* get_home_dir(void) {
    
    // if init_home() never runs, set home to NULL
    return SHELL_HOME[0]? SHELL_HOME : NULL;
}

void init_home(void) {

    if(!getcwd(SHELL_HOME, sizeof(SHELL_HOME))) {

        // getcwd() can fail for many reasons - permission issue, buffer size, directory deleted from parent, ...
        // confirm if home directory was set by getcwd()
        uid_t id = getuid();
        struct passwd* pw = getpwuid(id);
        
        if (pw && pw->pw_dir) {   
            strncpy(SHELL_HOME, pw->pw_dir, sizeof(SHELL_HOME)-1);
            SHELL_HOME[sizeof(SHELL_HOME)-1] = '\0';

        } else {
            // getcwd() failed, set SHELL_HOME to "/"
            SHELL_HOME[0] = '/'; SHELL_HOME[1] = '\0';
        }
    }
}

// make this static?
// void pretty_cwd(char* cwdir, ) {

// }

void print_prompt(void) {
   
    char hostname[HOST_NAME_MAX+1];     // POSIX standardizes HOST_NAME_MAX to exclude null terminator
    char username[LOGIN_NAME_MAX]; 
    char cwdir[PATH_MAX];

    // set hostname to "?" if gethostname() fails 
    if(gethostname(hostname, sizeof(hostname)) == -1) { 
        snprintf(hostname, sizeof(hostname), "?");
    }
    else { hostname[HOST_NAME_MAX] = '\0'; }
    
    uid_t uid = getuid();  
    struct passwd* pw = getpwuid(uid);

    if (pw && pw->pw_name) {
        snprintf(username, sizeof(username), "%s", pw->pw_name);
    } else { username[0]='?'; username[1]='\0'; }
    
    // pretty_cwd(cwdir, sizeof(cwdir))
   
    getcwd(cwdir, sizeof(cwdir));
    fprintf(stdout, "<%s@%s:%s> ", hostname, username, cwdir);
    fflush(stdout);
}

int main() {
    print_prompt();
    return 0;
}

