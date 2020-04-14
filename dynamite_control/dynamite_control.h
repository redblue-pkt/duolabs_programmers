#ifndef _DYNAMITE_CONTROL_H
#define _DYNAMITE_CONTROL_H

int fd;

char* unconstchar(const char* s) {
    int i;
    char* res;
    for (i = 0; s[i] != '\0'; i++) {
        res[i] = s[i];
    }
    res[i] = '\0';
    return res;
}

#define DYNAMITE_DEVICE "/dev/dynamite_programmer"

#endif
