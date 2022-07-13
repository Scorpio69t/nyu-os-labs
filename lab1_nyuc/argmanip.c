#include<stdlib.h>
#include<stdarg.h>
#include<string.h>
#include "argmanip.h"
//
//char **manipulate_args(int argc, const char *const *argv, int (*const manip)(int));
//void free_copied_args(char **args, ...);

char **manipulate_args(int argc, const char *const *argv, int (*const manip)(int))
{
    char * *args = malloc((argc+1) * sizeof(char*));

    for(int j=0; j<argc; j++){
        int len = strlen(*(argv+j));
        args[j] = malloc((len+1) * sizeof(char));

        for (int i = 0; *(*(argv+j)+i) != '\0'; ++i) {
            if(*(*(argv+j)+i)>='A' && *(*(argv+j)+i) <='Z'){
                *(*(args+j)+i) = manip(*(*(argv+j)+i));
            }
            else if(*(*(argv+j)+i)>='a' && *(*(argv+j)+i) <='z'){
                *(*(args+j)+i) = manip(*(*(argv+j)+i));
            }
            else{
                *(*(args+j)+i) = *(*(argv+j)+i);
            }
        }
        *(*(args+j)+len) = '\0';
    }
    args[argc] = NULL;
    return args;
}

void free_copied_args(char **args, ...){

    va_list ap;
    va_start(ap, args);

    char** p = NULL;

    for(p = args; p != NULL; p = va_arg(ap,char**)) {
        for (int j = 0; *(p+j) !=NULL ; ++j) {
            free(*(p+j));
        }
        free(p);
    }
    va_end(ap);
}


