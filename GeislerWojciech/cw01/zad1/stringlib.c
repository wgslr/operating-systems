
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stringlib.h"


char **create(size_t count) {
    char** array = calloc(count, sizeof(char*));
    return array;
}



void free_array(char **array, size_t size) {
    for(int i = 0; i < size; ++i){
        if(array[i] != NULL) {
            free(array[i]);
        }
    }
    free(array);
}

void set_value(char** array, size_t pos, char* data) {
    size_t len = strlen(data);
    array[pos] = malloc(len * sizeof(char));
    memcpy(array[pos], data, len);
}

